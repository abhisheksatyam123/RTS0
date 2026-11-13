#ifndef RTSO_GRID_NAV_H
#define RTSO_GRID_NAV_H

// Phase 1-2 foundation: SoA-friendly navigation grid and waypoint path.
// Phase 8A: Port of 0 A.D. HierarchicalPathfinder 96x96 chunk region system.
// Derived from 0 A.D., GPL-2.0-or-later (Copyright (C) Wildfire Games).
//   Reference: HierarchicalPathfinder.h (CHUNK_SIZE=96), HierarchicalPathfinder.cpp:865 (flood-fill).
//   Adapted for 64x64 grid: CHUNK_SIZE=8 => 8 chunks per axis (64/8=8).
//   Adds: NavChunk, rebuild_regions(), find_path_hierarchical(), string_pull(),
//         and passability mask constants (PASS_INFANTRY, PASS_CAVALRY, PASS_SHIP).
// Vendor boundary: src/0ad/simulation2/helpers/Grid.h, HierarchicalPathfinder.cpp:865,
//                  LongPathfinder.cpp:400 (octile heuristic) are REFERENCE ONLY.
// Active runtime is this file + NavGrid/Territory owned by Simulation (src/simulation/simulation.h).

#include <climits>
#include <atomic>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <utility>
#include <unordered_map>

#include "nav/long_pathfinder.h"

namespace rtso {

// std::floor truncated to an int, as static_cast<int>(std::floor(v)) (and (int)std::floor(v)): the
// same integer for every value in int range. Built without SSE4.1, std::floor is a libm call, so
// the grid lookups that floor on every query use this instead. The floor is exact: for a finite v,
// (int)v truncates toward zero, and one less is the floor when v sits below that truncation.
inline int floor_index(double v) {
	const int t = static_cast<int>(v);
	return static_cast<double>(t) > v ? t - 1 : t;
}

// ---------------------------------------------------------------------------
// Minimal Grid<T> — standalone port of src/0ad/simulation2/helpers/Grid.h:412 concepts
// Original uses Fixed, SAFE_ARRAY_DELETE, ENSURE, SparseGrid, GridUpdateInformation.
// This version is header-only, std::vector-backed, no Fixed, no serialization.
// Keep API intentionally small; expand only when HierarchicalPathfinder needs it.
// ---------------------------------------------------------------------------
template <typename T>
struct Grid {
    int w = 0;
    int h = 0;
    std::vector<T> data;

    Grid() = default;
    Grid(int w_, int h_, T init = T{}) : w(w_), h(h_), data(static_cast<size_t>(w_) * static_cast<size_t>(h_), init) {}

    void resize(int w_, int h_, T init = T{}) {
        w = w_;
        h = h_;
        data.assign(static_cast<size_t>(w) * static_cast<size_t>(h), init);
    }

    T& at(int x, int y) { return data[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)]; }
    const T& at(int x, int y) const { return data[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)]; }

    void fill(T v) { std::fill(data.begin(), data.end(), v); }

    bool inside(int x, int y) const { return x >= 0 && x < w && y >= 0 && y < h; }

    bool blank() const { return w == 0 && h == 0; }

    // Convenience mirrors of 0AD Grid::reset/clear
    void reset(T v = T{}) { std::fill(data.begin(), data.end(), v); }
    void clear() { w = 0; h = 0; data.clear(); }

    bool operator==(const Grid& o) const { return w == o.w && h == o.h && data == o.data; }
    bool operator!=(const Grid& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// Nav cell types - matches passability intent from Pathfinding.h:IS_PASSABLE
// NAV_PASSABLE = free for PATHFINDING pass class, NAV_OBSTRUCTED = building/impassable,
// NAV_WATER = water too deep to wade (ships only), NAV_SHALLOW = water land units can wade
// and ships can sail (0 A.D. pathfinder.xml: default MaxWaterDepth 2 overlaps ship MinWaterDepth).
// ---------------------------------------------------------------------------
enum NavCell : uint8_t {
    NAV_PASSABLE   = 0,
    NAV_OBSTRUCTED = 1,
    NAV_WATER      = 2,
    NAV_SHALLOW    = 3
};

// RTS0 height units per 0 A.D. metre. An RTS0 unit is a 4 m tile, so a quarter unit per metre
// keeps 0 A.D.'s proportions; ported maps (tools/port_0ad_maps.py) and random maps
// (rms_generator.cpp kMetre) both use it.
static constexpr float HEIGHT_PER_METRE = 0.25f;

// 0 A.D. mods/mod/simulation/data/pathfinder.xml: land classes (default, large) have
// MaxWaterDepth 2 m. Depth is measured from the nav water line (water_h + 0.3, where cells
// turn to water).
static constexpr float NAV_WATER_MARGIN = 0.3f;
static constexpr float LAND_MAX_WATER_DEPTH = 2.0f * HEIGHT_PER_METRE;

// ---------------------------------------------------------------------------
// Passability classes, after 0 A.D.'s pathfinder.xml PassabilityClasses.
// PASS_INFANTRY : land units (0 A.D. "default"): land and shallow water no steeper than
//                 LAND_MAX_TERRAIN_SLOPE.
// PASS_CAVALRY  : mounted units. 0 A.D. gives them "default" too (and siege and elephants
//                 "large", which differs only in Clearance), so it crosses the same cells and
//                 shares PASS_INFANTRY's regions.
// PASS_SHIP     : naval units only — NAV_WATER cells passable, NAV_PASSABLE blocked. This is
//                 0 A.D.'s "ship-small" (fishing and merchant ships): its 3 m Clearance is under
//                 half an RTS0 navcell, so it blocks no more navcells.
// PASS_WARSHIP  : PASS_SHIP plus PASS_SHIP_CLEARANCE, 0 A.D.'s "ship" (warships and
//                 transports), Clearance 10 m. CCmpPathfinder ExpandImpassableCells blocks every
//                 navcell whose centre lies within the clearance (Chebyshev, in whole navcells) of
//                 one the class can't cross. RTS0 navcells are 8 m, so that is each water navcell
//                 with a land or structure navcell among its eight neighbours (the next ring is 16 m
//                 off). Only long paths use it (NavGrid::find_path): steps, pushes and regions stay
//                 on PASS_SHIP, so a warship beside a dock or a beach can always sail off.
// PASS_LARGE    : a bit added to PASS_INFANTRY or PASS_CAVALRY for 0 A.D.'s "large" class (siege,
//                 elephants, chariots, wagons, catafalques and formations holding one), Clearance
//                 3 m where "default" has 0.8 m. Trees grown by that clearance close more of a wood
//                 (Simulation::forest_cell_closed), and those navcells (large_closed) block it too.
//                 Like PASS_SHIP_CLEARANCE, only long paths use it, falling back to the default class
//                 when it finds no way; regions stay the default class's. find_path's pass on the
//                 terrain tiles also keeps the class's clearance off steep tiles, deep water and
//                 static obstructions (NavGrid::find_path_tiles), as ExpandImpassableCells and
//                 RasterizeRectWithClearance grow them per class.
//
// 0 A.D. slope references:
// - source/simulation2/helpers/Pathfinding.h (PathfinderPassability::IsPassable: steepness < m_MaxSlope)
// - source/simulation2/components/CCmpPathfinder.cpp (terrain.GetSlopeFixed(itile, jtile) per tile)
// - source/graphics/Terrain.cpp (CTerrain::GetSlopeFixed: corner height delta / TERRAIN_TILE_SIZE)
// - simulation/data/pathfinder.xml: MaxTerrainSlope 1.0 for default, large, building-land and
//   default-terrain-only, 1.25 for building-shore, none for the ship classes
// ---------------------------------------------------------------------------
static constexpr uint8_t PASS_INFANTRY = 1;
static constexpr uint8_t PASS_CAVALRY  = 2;
static constexpr uint8_t PASS_SHIP     = 4;
static constexpr uint8_t PASS_SHIP_CLEARANCE = 8;
static constexpr uint8_t PASS_WARSHIP  = PASS_SHIP | PASS_SHIP_CLEARANCE;
static constexpr uint8_t PASS_LARGE    = 16;
// find_path's fallback: navcells a Gaia mine partly covers are closed (NavGrid::mine_sub).
static constexpr uint8_t PASS_MINE_CLOSED = 32;

// 0 A.D. slopes are metres of rise per metre. An RTS0 unit is a 4 m tile and heights are
// HEIGHT_PER_METRE units a metre, so a slope in RTS0 units is 0 A.D.'s x 4 x HEIGHT_PER_METRE
// (1: the same). Ported maps hold one height sample per 4 tiles, which flattens their cliffs a little.
static constexpr float SLOPE_0AD_TO_RTS0 = HEIGHT_PER_METRE * 4.0f;
static constexpr float LAND_MAX_TERRAIN_SLOPE = 1.0f * SLOPE_0AD_TO_RTS0;   // default, large, building-land
static constexpr float SHORE_MAX_TERRAIN_SLOPE = 1.25f * SLOPE_0AD_TO_RTS0; // building-shore (docks)

// ---------------------------------------------------------------------------
// Phase 8A: NavChunk — mirrors 0 A.D. HierarchicalPathfinder::Chunk at our scale.
// CHUNK_SIZE=8 for 64x64 grid (64/8 = 8 chunks per axis).
// 0 A.D. uses CHUNK_SIZE=96 for 256-navcell maps (HierarchicalPathfinder.h:177).
// region_id=0 means impassable; 1..N are connected flood-fill regions within chunk.
// dirty=true => rebuild_regions() must re-flood-fill this chunk on next call.
// ---------------------------------------------------------------------------
static constexpr int CHUNK_SIZE = 8;  // navcells per chunk side (64/8 = 8 chunks per axis)

struct NavChunk {
    uint8_t region_id = 0; // local region label (0 = impassable, 1..N = connected regions)
    bool    dirty     = true;
};

struct WaypointPath;

// ---------------------------------------------------------------------------
// NavGrid — 64x64 (DEFAULT_RES) SoA-friendly mirror of 0AD's Grid<NavcellData>
// plus a dirty grid (mirrors GridUpdateInformation::dirtinessGrid, src/0ad/.../Grid.h:372).
//
// Phase 8A: Hierarchical region system ported from 0 A.D. HierarchicalPathfinder.
//   - CHUNK_SIZE=8 => 8x8=64 chunks for our 64x64 grid (0AD: CHUNK_SIZE=96 for 256x256).
//   - chunk_regions_: flat Grid<uint8_t> storing per-navcell local region ID per chunk.
//   - global_region_: maps (chunk_ci, chunk_cj, local_r) -> global connected-region ID.
//     Global regions enable O(1) reachability check (same global id = connected).
//   - rebuild_regions(): BFS flood-fill within each chunk (Chunk::InitRegions pattern,
//     HierarchicalPathfinder.cpp:50), then inter-chunk edge pass (ComputeNeighbors pattern).
//
// 0AD notes:
//   - HierarchicalPathfinder::CHUNK_SIZE = 96 navcells (src/0ad/.../HierarchicalPathfinder.h:177)
//   - Long octile: h = hv*10 + diagExtra ; 0AD exact is PathCost(hv,diag).data = hv*65536 + diag*92682
//     (see src/0ad/.../Pathfinding.h:85, LongPathfinder.cpp:400: di=|dx|, dj=|dz|, diag=min(di,dj),
//      return PathCost(di-diag+dj-diag, diag) ). find_path below uses exact 10/14 octile weights.
// ---------------------------------------------------------------------------
// Global region ids of one passability class, stored per chunk and local region: the local
// regions of chunk c (c = cj * chunks_per_axis + ci) are entries first[c] .. first[c + 1] - 1 of
// id, local region r at first[c] + r - 1. Mirrors HierarchicalPathfinder::m_GlobalRegions
// (HierarchicalPathfinder.h:298) without its std::map.
struct GlobalRegionIds {
    std::vector<uint32_t> first; // per chunk, plus one past the last
    std::vector<uint16_t> id;    // global id per local region, 0 = none

    void clear() { first.clear(); id.clear(); }
    uint16_t get(size_t chunk, uint8_t local_r) const {
        if (local_r == 0 || chunk + 1 >= first.size()) return 0;
        const uint32_t k = first[chunk] + local_r - 1u;
        return k < first[chunk + 1] ? id[k] : 0;
    }
};

struct NavGrid {
    static constexpr int DEFAULT_RES = 64;
    int res = DEFAULT_RES;
    float map_half = 64.f;

    Grid<uint8_t> cells; // NavCell per navcell
    // 1 where a wood is closed to PASS_LARGE but not to the default class; empty, or as many as cells.
    std::vector<uint8_t> large_closed;
    // The NavCell a wood closed (Simulation::rebuild_nav_grid's forest closure) on a navcell that stood passable, that
    // value; 0 elsewhere; empty, or as many as cells. Obstruct() clears a navcell's entry when a mine or the map edge
    // obstructs it. structural_open reads through it, so the 1 m re-plan does not take a wood's closure for ground.
    std::vector<uint8_t> wood_closed_from;
    // Obstructs navcell (x, y) for good: a mine's cover or the map's edge (not a wood's closure, wood_closed_from).
    void obstruct(int x, int y) {
        cells.at(x, y) = NAV_OBSTRUCTED;
        clear_wood_closure(x, y);
    }
    // Drops a wood's closure record for navcell (x, y) and keeps its value: the navcell is closed for good (a gate's line
    // or covered cell the gate pass leaves closed), so structural_open must not read through the wood to what it stood as.
    void clear_wood_closure(int x, int y) {
        const long long k = static_cast<long long>(y) * cells.w + x; // the index cells.at wrote (a mine's loop may pass the edge)
        if (!wood_closed_from.empty() && k >= 0 && k < static_cast<long long>(wood_closed_from.size())) wood_closed_from[static_cast<size_t>(k)] = 0;
    }
    // Whether navcell (cx, cy) is open to the 1 m re-plan's structure: terrain, buildings, mines and the map edge, as the
    // navcells stood before a wood closed them, and not steep (is_steep). Woods are left to the trees' own subcells
    // (tree_sub), which the 1 m search routes between; the search is not restricted by the navcell closure of a wood.
    bool structural_open(int cx, int cy) const {
        if (!cells.inside(cx, cy)) return false;
        const size_t i = static_cast<size_t>(cy) * static_cast<size_t>(cells.w) + static_cast<size_t>(cx);
        uint8_t c = cells.data[i];
        if (c == NAV_OBSTRUCTED && wood_closed_from.size() == cells.data.size() && wood_closed_from[i] != 0) c = wood_closed_from[i];
        if (c != NAV_PASSABLE && c != NAV_SHALLOW) return false;
        return !is_steep(cx, cy);
    }
    Grid<uint8_t> dirty; // 0=clean, 1=needs hierarchical rebuild
    // A circular map's passable edge radius (Simulation::past_map_edge), 0 on a square map.
    // The navcells whose centre lies past it are closed (Simulation::rebuild_nav_grid); square
    // maps close their 12 m frame instead (CCmpPathfinder.cpp:711 else branch); find_path_tiles'
    // nodes take the edge at their own spacing, as units do (point_terrain_open).
    float edge_r = 0.0f;
    // Gaia mines (BlockPathfinding), per passability class (0 default, 1 large): per navcell, the 8x8
    // 1 m subcells a mine's grown square covers by 0 A.D.'s four-corner rule (Rasterize.cpp), bit
    // (y%8)*8+(x%8) for subcell (x, y). Empty without mines (rebuild_nav_grid). The 8 m navcells stay
    // as they are; the long path checks every subcell its legs cross (avoid_mine_subcells).
    std::vector<uint64_t> mine_sub[2];
    bool mine_any() const { return !mine_sub[0].empty() || !mine_sub[1].empty(); }
    // A navcell a mine of this class covers in part (some subcells blocked, not all).
    bool mine_partial(int cls, int cx, int cy) const {
        if (mine_sub[cls].empty()) return false;
        const uint64_t m = mine_sub[cls][static_cast<size_t>(cy) * static_cast<size_t>(res) + static_cast<size_t>(cx)];
        return m != 0 && m != ~uint64_t(0);
    }
    // Whether the 1 m subcell at subcell coordinates (sx, sy) is blocked for this class.
    bool mine_subcell_blocked(int cls, int sx, int sy) const {
        if (mine_sub[cls].empty() || sx < 0 || sy < 0 || sx >= res * 8 || sy >= res * 8) return false;
        const uint64_t m = mine_sub[cls][static_cast<size_t>(sy / 8) * static_cast<size_t>(res) + static_cast<size_t>(sx / 8)];
        return ((m >> ((sy % 8) * 8 + sx % 8)) & 1u) != 0;
    }
    // Whether a long path leg of out (from (sx, sz)) crosses a blocked subcell of this class. The walk is
    // the navcell walker (CheckLineMovement) on the 1 m grid, so it visits every subcell the leg touches.
    // The start's and the goal's navcells are not checked: a unit stands in one, and a gatherer's goal is its mine.
    bool crosses_mine_sub(int cls, float sx, float sz, const struct WaypointPath &out) const;
    // Re-plans a path whose legs cross a blocked subcell with the partly covered navcells closed
    // (PASS_MINE_CLOSED). 0: no change; 1: out and the goal are replaced; 2: no route avoids the mines.
    int avoid_mine_subcells(float sx, float sz, float &gx, float &gz, struct WaypointPath &out, uint8_t pass) const;

    // Trees and bushes (C1), per passability class (0 default, 1 large), on the same 1 m subcells as mine_sub: a
    // subcell is blocked iff its four corners all lie within the class clearance plus 0 A.D.'s 1 m
    // CLEARANCE_EXTENSION_RADIUS (Pathfinding.h:160, rounded corners) of a live tree's square
    // (Simulation::rebuild_nav_grid, Rasterize.cpp:48). The navcells stay as they are; only the long path's legs are
    // checked against these masks (avoid_tree_subcells). Not 1 unit: that is nav::kClearanceExtensionRadius, the
    // navcell raster's deviation.
    // mutable: Simulation::refresh_tree_masks rebuilds them lazily (a const path query reads them).
    mutable std::vector<uint64_t> tree_sub[2];
    // Whether tree_sub was built from the live trees since init (cleared by init). A build is current until a tree edit
    // marks it stale (Simulation::tree_masks_dirty_), so a full rebuild that finds the masks current skips the build.
    mutable bool tree_sub_built = false;
    bool tree_any() const { return !tree_sub[0].empty() || !tree_sub[1].empty(); }
    bool tree_subcell_blocked(int cls, int sx, int sy) const {
        if (tree_sub[cls].empty() || sx < 0 || sy < 0 || sx >= res * 8 || sy >= res * 8) return false;
        const uint64_t m = tree_sub[cls][static_cast<size_t>(sy / 8) * static_cast<size_t>(res) + static_cast<size_t>(sx / 8)];
        return ((m >> ((sy % 8) * 8 + sx % 8)) & 1u) != 0;
    }
    // C1 water: per navcell, the 8x8 subcells of a NAV_WATER navcell that 0 A.D.'s 1 m depth rule closes (bit
    // (y%8)*8+(x%8), as tree_sub): a subcell whose ground lies deeper than the sea surface's depth limit
    // (Simulation::build_water_masks). Set only for NAV_WATER navcells; empty without water. mutable: refreshed lazily.
    mutable std::vector<uint64_t> water_sub;
    bool water_subcell_deep(int sx, int sy) const {
        if (water_sub.empty() || sx < 0 || sy < 0 || sx >= res * 8 || sy >= res * 8) return false;
        const uint64_t m = water_sub[static_cast<size_t>(sy / 8) * static_cast<size_t>(res) + static_cast<size_t>(sx / 8)];
        return ((m >> ((sy % 8) * 8 + sx % 8)) & 1u) != 0;
    }
    // Whether the terrain itself is water at navcell (cx, cy): set_water_from_heights' class by the centre height, which
    // the structures' closures do not change (water_sub is built from it, so the mask holds through a rebuild).
    bool terrain_water_navcell(int cx, int cy) const {
        const HeightSampleCache &h = height_cache_;
        const size_t n = static_cast<size_t>(res) * static_cast<size_t>(res);
        return h.water_valid && h.water.size() == n && h.water[static_cast<size_t>(cy) * static_cast<size_t>(res) + static_cast<size_t>(cx)] == NAV_WATER;
    }
    // Whether water_sub was built, and from which water_class_gen: refresh_water_masks.
    mutable bool water_built = false;
    mutable uint64_t water_built_gen = 0;
    // Counts the recomputations of the terrain water classes (height_cache_.water, set_water_from_heights): the mask is
    // built from them, so it is rebuilt when they change, and not when only the structures' closures do.
    uint64_t water_class_gen = 0;
    // Blocked for the 1 m search: a tree's or a mine's subcell of this class.
    bool sub_blocked(int cls, int sx, int sy) const { return tree_subcell_blocked(cls, sx, sy) || mine_subcell_blocked(cls, sx, sy); }
    // Whether a long path leg of out (from (sx, sz)) crosses a blocked tree subcell of this class, walked as crosses_mine_sub.
    bool crosses_tree_sub(int cls, float sx, float sz, const struct WaypointPath &out) const;
    // G171: whether one leg crosses a tree (TILE_OBS_GAIA flora/treasure) grown by the class clearance,
    // with a unit starting inside it allowed to walk out (crosses_tree_sub's left rule, at the clearance
    // instead of the subcells' clearance plus 1 m). One leg only, so find_path_on can gate the tile
    // path on the legs ImprovePathWaypoints never tested (first, goal, small, collinear) without
    // reverting a whole path that merely passes within that metre of a tree.
    bool leg_crosses_gaia_clearance(float ax, float az, float bx, float bz, int cls) const;
    // Whether any leg of a fresh tile path (from (sx, sz) through its waypoints) crosses as above.
    bool tile_path_crosses_tree_clearance(float sx, float sz, const struct WaypointPath &path, int cls) const;
    // Re-plans a path whose legs cross a tree subcell on the 1 m grid (avoid_tree_subcells). On success out and the goal
    // are replaced; otherwise they are left as the navcells gave them.
    void avoid_tree_subcells(float sx, float sz, float &gx, float &gz, struct WaypointPath &out, uint8_t pass, float rx, float rz) const;
    // Re-plans made, and re-plans that kept the navcell path (over budget, unreachable, or too long), over every thread.
    static inline std::atomic<uint64_t> tree_sub_replans{0};
    static inline std::atomic<uint64_t> tree_sub_fallbacks{0};
    static constexpr int TREE_BOX_PAD = 20;        // subcells (20 m at 1 m) the search box first grows by; doubled while clipped
    static constexpr int TREE_BFS_CELLS = 1 << 17; // the search box's cell budget

    // Phase 8A: Hierarchical chunk region data.
    // chunk_regions_: res x res, stores per-navcell local region ID within its chunk (0=impassable).
    // Mirrors HierarchicalPathfinder::Chunk::m_Regions[][].
    Grid<uint8_t> chunk_regions_; // local region id per navcell (within its chunk)

    // Global region per chunk and local region (GlobalRegionIds). Two navcells are connected
    // iff their global ids are equal and non-zero.
    GlobalRegionIds global_region_map_;
    Grid<uint8_t> ship_chunk_regions_;
    GlobalRegionIds ship_region_map_;

    // steep_cells_: res x res, 1 = steeper than max_slope, which no land unit crosses.
    Grid<uint8_t> steep_cells_;
    bool any_steep_ = false;
    float max_slope = LAND_MAX_TERRAIN_SLOPE;
    // The heightmap's tiles steeper than max_slope (height_cache_.tiles), once
    // compute_slopes_from_heights has run for the current terrain.
    bool tiles_ready_ = false;
    // Changes whenever the steep tiles or navcells, or the heightmap they come from, may have.
    uint32_t terrain_version_ = 0;

    bool regions_ready_ = false;

    // find_path_tiles' passability classes: 0.28's "default" (0.8 m) and "large" (3 m) Clearance
    // (pathfinder.xml; sim_types.h CLEARANCE_DEFAULT, CLEARANCE_LARGE).
    static constexpr int TILE_CLASSES = 2;
    static constexpr float TILE_PATH_CLEARANCE = 0.8f / 4.0f;
    static constexpr float TILE_PATH_CLEARANCE_LARGE = 3.0f / 4.0f;
    // A face of a structure with another structure less than TILE_CROWD_GAP beyond it (TileObstruction::crowd)
    // grows by TILE_CROWD_GROW for the large class: 1 m, 0.28's CLEARANCE_EXTENSION_RADIUS (Pathfinding.h:160),
    // which RasterizeRectWithClearance adds to every static obstruction's grow (Rasterize.cpp:48). So two
    // structures 6.5 to 7.5 m apart close the gap between them at every phase (G30), while a face with
    // nothing near it stays at RTS0's clearance, and a gap of exactly TILE_CROWD_GAP stays open (scenario 8
    // of test_building_steering). One world unit is 4 m.
    static constexpr float TILE_CROWD_GROW = 0.25f;
    static constexpr float TILE_CROWD_GAP = 2.0f;
    static constexpr uint8_t TILE_CROWD_FACE_U = 1, TILE_CROWD_FACE_NEG_U = 2, TILE_CROWD_FACE_V = 4, TILE_CROWD_FACE_NEG_V = 8;
    static int tile_class(uint8_t pass) { return (pass & PASS_LARGE) ? 1 : 0; }
    static float tile_clearance(int cls) { return cls ? TILE_PATH_CLEARANCE_LARGE : TILE_PATH_CLEARANCE; }
    // How far, in world units, the nodes' tests reach past a clearance, so that a square or a
    // rectangle that only touches a node's (a float's rounding either way) closes it.
    static constexpr float TILE_HAIR = 1e-3f;
    // A start region of at most this many nodes is walked whole when MakeGoalReachable moves a goal
    // the start can't reach (find_path_tiles), rather than scanned in rings round a far goal.
    static constexpr size_t TILE_POCKET_NODES = 1024;
    // tile_obs_parts_ground's floods round an obstruction stop at this many nodes (a part that big is
    // no pocket), unless two of them must be told apart.
    static constexpr size_t TILE_PART_FLOOD = 16384;

    // A static obstruction the tile pass keeps units off (CCmpObstructionManager::Rasterize: the
    // static shapes that BlockPathfinding): a rectangle centred on (x, z), hu along u = (ux, uz) and
    // hv along v = (-uz, ux), u = (1, 0) when uz is 0 (nav/footprint.h). key names it; a list is
    // sorted by key (Simulation::sync_tile_obstructions).
    struct TileObstruction {
        uint64_t key = 0;
        float x = 0.0f, z = 0.0f, ux = 1.0f, uz = 0.0f, hu = 0.0f, hv = 0.0f;
        // The faces (TILE_CROWD_FACE_*) of it that another structure of the same set lies within
        // TILE_CROWD_GAP beyond (mark_tile_crowds); the large class grows those faces.
        uint8_t crowd = 0;
        bool operator==(const TileObstruction &o) const {
            return key == o.key && x == o.x && z == o.z && ux == o.ux && uz == o.uz && hu == o.hu && hv == o.hv && crowd == o.crowd;
        }
        bool operator!=(const TileObstruction &o) const { return !(*this == o); }
    };
    // The structures, scenery and mines (rebuild_nav_grid's), and the trees, bushes and treasures;
    // with each list, the signature its lister gave it (0: none), which init() forgets with the list.
    static constexpr int TILE_OBS_STRUCTURES = 0, TILE_OBS_GAIA = 1, TILE_OBS_SETS = 2;
    std::vector<TileObstruction> tile_obs_[TILE_OBS_SETS];
    uint64_t tile_obs_sig_[TILE_OBS_SETS] = {};

    // update_tile_open's nodes (node_res() a side) per class: open (1) or not, each open node's
    // region (0: closed), and how many obstructions cover it (tile_obs_raster); whether each navcell
    // was deep water when they were worked out, and the summed-area table of those ((res + 1) a
    // side); and the terrain_version_, res, map size and circular edge they are for. Region ids are
    // only ever compared with each other: one region keeps its id while it neither splits nor merges.
    std::vector<uint8_t> tile_open_[TILE_CLASSES];
    std::vector<uint32_t> tile_region_[TILE_CLASSES];
    std::vector<uint16_t> tile_obs_count_[TILE_CLASSES];
    // The open nodes as bits, by row and by column (nav::JpsBits), node_words() words a line.
    std::vector<uint64_t> tile_bits_rows_[TILE_CLASSES], tile_bits_cols_[TILE_CLASSES];
    std::vector<uint8_t> tile_cell_water_;
    std::vector<uint32_t> tile_water_sum_;
    uint32_t tile_region_next_[TILE_CLASSES] = {};  // the last region id given out
    uint32_t tile_open_version_ = 0;
    unsigned tile_open_kept_ = 0; // tile_kept_mask() when they were worked out
    int tile_open_res_ = 0;
    float tile_open_half_ = 0.0f, tile_open_edge_r_ = 0.0f;
    bool tile_open_ready_ = false;
    // The obstructions by square bucket TILE_OBS_BUCKET world units a side, for the line tests: a
    // copy of each one in every bucket its box grown by the large clearance reaches into, so that a
    // change moves only the ones it touches.
    static constexpr float TILE_OBS_BUCKET = 4.0f;
    std::vector<std::vector<TileObstruction>> tile_obs_buckets_;
    int tile_obs_bw_ = 0;
    float tile_obs_half_ = 0.0f;
    // update_tile_open's scratch: flood fill stack, the navcells whose water changed, the nodes a
    // change closed and opened, the open nodes beside them and their regions, a visit stamp per node,
    // and the nodes whose obstruction cover started or ended; split_tile_region's seeds, the nodes
    // each group reached (and how far it has grown from them) and the groups' joins; the closed
    // nodes by cluster, the clusters and the regions to split with their seeds; flood_tile_regions'
    // runs and their numbers; and nodes_terrain_open's spans.
    std::vector<uint32_t> tile_stack_, tile_cells_, tile_closed_, tile_opened_, tile_edge_, tile_ids_, tile_mark_, tile_crossed_;
    uint32_t tile_mark_stamp_ = 0;
    std::vector<uint32_t> tile_seeds_, tile_grown_, tile_join_, tile_new_id_, tile_order_;
    struct TileCluster {
        uint32_t first, last;
        int i0, j0, i1, j1;
    };
    std::vector<TileCluster> tile_clusters_;
    struct TileRun {
        int j, a, b;  // the open nodes a to b - 1 of row j
        uint32_t up;  // a run of its region before it, or itself
    };
    std::vector<TileRun> tile_runs_;
    std::vector<uint32_t> tile_run_id_;
    std::vector<std::pair<uint32_t, uint32_t>> tile_split_;
    std::vector<std::vector<uint32_t>> tile_group_;
    struct TileSpan {
        bool in;
        int t0, t1, w0, w1;
    };
    std::vector<TileSpan> tile_span_;

    // Number of chunks per axis: res / CHUNK_SIZE
    int chunks_per_axis_ = DEFAULT_RES / CHUNK_SIZE; // 64/8 = 8

    void init(float map_half_, int res_) {
        res = res_;
        map_half = map_half_;
        cells = Grid<uint8_t>(res, res, NAV_PASSABLE);
        dirty = Grid<uint8_t>(res, res, 0);
        chunks_per_axis_ = res / CHUNK_SIZE;
        chunk_regions_ = Grid<uint8_t>(res, res, 0);
        ship_chunk_regions_ = Grid<uint8_t>(res, res, 0);
        steep_cells_ = Grid<uint8_t>(res, res, 0);
        any_steep_ = false;
        tiles_ready_ = false;
        ++terrain_version_;
        global_region_map_.clear();
        ship_region_map_.clear();
        regions_ready_ = false;
        tile_open_ready_ = false;
        edge_r = 0.0f;
        tree_sub[0].clear(); // C1: a new map's trees are masked afresh (rebuild_nav_grid)
        tree_sub[1].clear();
        tree_sub_built = false;
        water_sub.clear(); // C1: and its water subcells (refresh_water_masks)
        water_built = false;
        // A new map: Simulation::rebuild_nav_grid lists its obstructions afresh.
        for (int set = 0; set < TILE_OBS_SETS; ++set) {
            tile_obs_[set].clear();
            tile_obs_sig_[set] = 0;
        }
        tile_obs_buckets_.clear();
        tile_obs_bw_ = 0;
        tile_obs_half_ = 0.0f;
    }

    // World (x,z) in [-map_half, +map_half] -> cell (cx,cy) in [0,res)
    std::pair<int,int> world_to_cell(float x, float z) const {
        const float inv = static_cast<float>(res) / (map_half * 2.0f);
        int cx = static_cast<int>((x + map_half) * inv);
        int cy = static_cast<int>((z + map_half) * inv);
        cx = std::clamp(cx, 0, res - 1);
        cy = std::clamp(cy, 0, res - 1);
        return {cx, cy};
    }

    // Mark square footprint centered at (cell_x, cell_y). footprint=1 => 1x1, 2=>2x2 etc.
    // Mirrors CCmpObstructionManager rasterization (Pathfinding.h:CLEARANCE_EXTENSION_RADIUS).
    void mark_building(int cell_x, int cell_y, int footprint, bool obstructed) {
        uint8_t v = obstructed ? NAV_OBSTRUCTED : NAV_PASSABLE;
        mark_building(cell_x, cell_y, footprint, v);
    }

    void mark_building(int cell_x, int cell_y, int footprint, uint8_t v = NAV_OBSTRUCTED) {
        int f = std::max(1, footprint);
        for (int dy = 0; dy < f; ++dy) {
            for (int dx = 0; dx < f; ++dx) {
                int x = cell_x + dx;
                int y = cell_y + dy;
                if (cells.inside(x, y)) {
                    cells.at(x, y) = v;
                    dirty.at(x, y) = 1;
                }
            }
        }
    }

    bool is_passable(int cx, int cy) const {
        return is_passable_for(cx, cy, PASS_INFANTRY);
    }

    bool is_passable_ship(int cx, int cy) const {
        return is_passable_for(cx, cy, PASS_SHIP);
    }

    bool is_passable_for(int cx, int cy, uint8_t pass) const {
        if (!cells.inside(cx, cy)) return false;
        uint8_t c = cells.at(cx, cy);
        if (pass & PASS_SHIP) {
            if (c != NAV_WATER && c != NAV_SHALLOW) return false;
            if (!(pass & PASS_SHIP_CLEARANCE)) return true;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    if (!cells.inside(cx + dx, cy + dy)) return false;
                    const uint8_t n = cells.at(cx + dx, cy + dy);
                    if (n != NAV_WATER && n != NAV_SHALLOW) return false;
                }
            return true;
        }
        if (c != NAV_PASSABLE && c != NAV_SHALLOW) return false;
        if (is_steep(cx, cy)) return false;
        if ((pass & PASS_LARGE) && large_closed.size() == cells.data.size() &&
                large_closed[static_cast<size_t>(cy) * cells.w + cx]) return false;
        if ((pass & PASS_MINE_CLOSED) && mine_partial(tile_class(pass), cx, cy)) return false;
        return true;
    }

    // The navcells a line passes through, in order, as Pathfinding::CheckLineMovement (Pathfinding.cpp)
    // walks them: from the navcell holding the start, it leaves each across the edge the line crosses
    // (along y at an exact corner) until it reaches the navcell holding the end. Unlike a Bresenham
    // line it misses no navcell the line touches inside, and never steps diagonally between two.
    // The ends are in navcells (0 to res); visit(x, y) is called for each until it returns false.
    template <class Visit>
    static void walk_grid(int n, float fx0, float fy0, float fx1, float fy1, Visit &&visit) {
        int x = std::clamp(floor_index(fx0), 0, n - 1);
        int y = std::clamp(floor_index(fy0), 0, n - 1);
        const int x1 = std::clamp(floor_index(fx1), 0, n - 1);
        const int y1 = std::clamp(floor_index(fy1), 0, n - 1);
        const int di = x < x1 ? 1 : x1 < x ? -1 : 0;
        const int dj = y < y1 ? 1 : y1 < y ? -1 : 0;
        const float lx = fx1 - fx0, ly = fy1 - fy0;
        while (true) {
            if (!visit(x, y) || (x == x1 && y == y1)) return;
            if (di == 0 || x == x1) { y += dj; continue; }
            if (dj == 0 || y == y1) { x += di; continue; }
            // The edge along y the line heads for, from (x, ey) to (x + 1, ey): the line leaves
            // across it unless both its ends lie on one side of the line.
            const float ey = static_cast<float>(y + (dj + 1) / 2) - fy0;
            const float ea = static_cast<float>(x) - fx0;
            const float sa = ea * ly - ey * lx, sb = (ea + 1.0f) * ly - ey * lx;
            if ((sa < 0.0f && sb < 0.0f) || (sa > 0.0f && sb > 0.0f)) x += di;
            else y += dj;
        }
    }

    template <class Visit>
    void walk_line(float fx0, float fy0, float fx1, float fy1, Visit &&visit) const {
        walk_grid(res, fx0, fy0, fx1, fy1, visit);
    }

    // CheckLineMovement between two navcell centres: no navcell the line passes through is
    // impassable, except those it starts on (a unit stuck there may leave them).
    bool line_clear(int x0, int y0, int x1, int y1, uint8_t pass) const {
        bool on_impassable = true, clear = true;
        walk_line(x0 + 0.5f, y0 + 0.5f, x1 + 0.5f, y1 + 0.5f, [&](int x, int y) {
            if (is_passable_for(x, y, pass)) on_impassable = false;
            else if (!on_impassable) clear = false;
            return clear;
        });
        return clear;
    }

    // The point (x, z) in navcells (0 to res along each axis).
    std::pair<float, float> world_to_navcells(float x, float z) const {
        const float inv = static_cast<float>(res) / (map_half * 2.0f);
        return {(x + map_half) * inv, (z + map_half) * inv};
    }

    void string_pull(std::vector<std::pair<int,int>> &cells, uint8_t pass) const {
        if (cells.size() <= 2) return;
        std::vector<std::pair<int,int>> out;
        out.reserve(cells.size());
        size_t i = 0;
        out.push_back(cells[0]);
        while (i + 1 < cells.size()) {
            size_t best = i + 1;
            for (size_t j = cells.size() - 1; j > i + 1; --j) {
                if (line_clear(cells[i].first, cells[i].second, cells[j].first, cells[j].second, pass)) {
                    best = j;
                    break;
                }
            }
            out.push_back(cells[best]);
            i = best;
        }
        cells.swap(out);
    }

    // Port of HierarchicalPathfinder::Chunk::InitRegions (scan/flood per CHUNK_SIZE)
    // + ComputeNeighbors (union across chunk edges) + UpdateGlobalRegions.
    // Vendor: src/0ad/simulation2/helpers/HierarchicalPathfinder.cpp:50, :523, :652
    // Full remesh over all chunks (cold start / water-table swap).
    // Delegates to flood_single_chunk + recompute_global_regions so the
    // per-chunk and full paths share one implementation.
    void flood_chunk_regions(uint8_t pass, Grid<uint8_t> &local, GlobalRegionIds &global) {
        local.fill(0);
        global.clear();
        if (cells.blank()) return;
        for (int cj = 0; cj < chunks_per_axis_; ++cj)
            for (int ci = 0; ci < chunks_per_axis_; ++ci)
                flood_single_chunk(ci, cj, pass, local);
        recompute_global_regions(local, global);
    }

    void rebuild_regions() {
        flood_chunk_regions(PASS_INFANTRY, chunk_regions_, global_region_map_);
        flood_chunk_regions(PASS_SHIP, ship_chunk_regions_, ship_region_map_);
        regions_ready_ = true;
        update_tile_open(true);
        if (!dirty.blank()) dirty.fill(0);
    }

    // ---- find_path_tiles' grid ------------------------------------------------------------------
    // 0.28's long pathfinder plans on 1 m navcells (Pathfinding::NAVCELL_SIZE) that carry each
    // class's passability: terrain tiles steeper than MaxTerrainSlope and deep water
    // (CCmpPathfinder::TerrainUpdateHelper) and the static obstructions that BlockPathfinding
    // (CCmpObstructionManager::Rasterize), each grown by the class Clearance (ExpandImpassableCells,
    // RasterizeRectWithClearance). The pass on the terrain tiles plans on nodes half a tile apart
    // (2 m on a 256 map): on the tiles' corners, the middles of their sides and their centres.
    // A node is open to a class when a unit of the class can stand on it. The square of half side
    // the class's clearance round it reaches no steep tile and no deep-water navcell
    // (Simulation::land_step_barred, near_deep_water). It lies inside the map, and a circular map's
    // edge, by the clearance. And no obstruction grown by the clearance holds it, as separation()
    // and the steering keep units out of structures, mines, trees, bushes and treasures. Every step
    // between two open nodes keeps clear too, and so does a diagonal one past two more open nodes,
    // as JPS takes them (tile_obs_raster): a path along open nodes is one a unit can walk.
    // - "default", 0.8 m: a corner node is open when none of its four tiles is steep, a centre one
    //   when its own tile isn't. A gap one tile wide between steep tiles keeps its line of centres,
    //   which is 0.28's 2 m corridor of 1 m navcells (centres 1.5 m from either side).
    // - "large", 3 m: a corner node is open when its four tiles are level, any other one when the
    //   three tiles a side round it are. A gap two tiles wide keeps the line of corners between its
    //   tiles, as 0.28's 3 m expansion leaves 2 m of an 8 m gap open; a gap one tile wide is shut.

    // - Structures, for "large": 0.28 rasterizes every static obstruction grown by the class
    //   Clearance (RasterizeRectWithClearance, Rasterize.cpp:29-48), so a ram's or an elephant's
    //   long path never takes a gap between two structures closer than twice its 3 m: CheckMovement
    //   (CCmpPathfinder.cpp:971-985) would refuse the step into either one's square there, as
    //   Simulation::large_step_barred does. RTS0's 8 m navcells, closed only where a structure covers
    //   them whole, leave such gaps open, so the class keeps its nodes on every map, steep or level:
    //   on the heightmap's tiles when they are finer than the navcells, else on the map's own 4 m
    //   tiles (node_tiles). The default class keeps them only where tile_paths_on.

    // The tiles a side the nodes lie on, two nodes a tile: the heightmap's when they are finer than
    // the navcells, else the map's 4 m tiles (a world unit a side); 0 when neither is.
    int node_tiles() const {
        const int tw = steep_tile_res();
        if (tw > res) return tw;
        const int mt = static_cast<int>(2.0f * map_half + 0.5f);
        return mt > res ? mt : 0;
    }
    // Whether the nodes of class cls are kept: the "large" class's wherever there are nodes, the
    // default class's only where find_path plans near steep ground on them (tile_paths_on).
    bool tile_class_kept(int cls) const {
        if (cells.blank() || node_tiles() <= res) return false;
        return cls == 1 || tile_paths_on();
    }
    bool tile_nodes_on() const { return tile_class_kept(1); }
    // The nodes: node_res() a side, node_step() world units apart, node a at node_pos(a).
    int node_res() const { return 2 * node_tiles(); }
    int node_words() const { return (node_res() + 63) / 64; }
    float node_step() const { return map_half / static_cast<float>(std::max(node_tiles(), 1)); }
    float node_pos(int a) const { return static_cast<float>(a) * node_step() - map_half; }
    // The node nearest the world coordinate v (x or z).
    int node_of(float v) const {
        return std::clamp(floor_index((v + map_half) / node_step() + 0.5f), 0, std::max(node_res() - 1, 0));
    }

    // Sets node k of class cls open (1) or not, and its bits.
    void set_node_open(int cls, uint32_t k, uint8_t v) {
        tile_open_[cls][k] = v;
        const uint32_t nw = static_cast<uint32_t>(node_res()), words = static_cast<uint32_t>(node_words());
        const uint32_t a = k % nw, b = k / nw;
        uint64_t &r = tile_bits_rows_[cls][static_cast<size_t>(b) * words + (a >> 6)];
        uint64_t &c = tile_bits_cols_[cls][static_cast<size_t>(a) * words + (b >> 6)];
        const uint64_t ra = uint64_t{1} << (a & 63), cb = uint64_t{1} << (b & 63);
        if (v) {
            r |= ra;
            c |= cb;
        } else {
            r &= ~ra;
            c &= ~cb;
        }
    }
    void build_node_bits(int cls) {
        const int nw = node_res(), words = node_words();
        const size_t n = static_cast<size_t>(nw) * static_cast<size_t>(words);
        tile_bits_rows_[cls].assign(n, 0);
        tile_bits_cols_[cls].assign(n, 0);
        for (int b = 0; b < nw; ++b)
            for (int a = 0; a < nw; ++a)
                if (tile_open_[cls][static_cast<size_t>(b) * static_cast<size_t>(nw) + static_cast<size_t>(a)]) {
                    tile_bits_rows_[cls][static_cast<size_t>(b) * static_cast<size_t>(words) + static_cast<size_t>(a >> 6)] |= uint64_t{1} << (a & 63);
                    tile_bits_cols_[cls][static_cast<size_t>(a) * static_cast<size_t>(words) + static_cast<size_t>(b >> 6)] |= uint64_t{1} << (b & 63);
                }
    }

    // Whether a unit of class cls standing at (x, z) keeps its clearance (and a hair) off the terrain
    // and the map's edge (see above); node (a, b) is open to the terrain when the unit can stand on
    // it. live reads the navcells' deep water as they are now, not as the nodes were worked out.
    bool point_terrain_open(float x, float z, int cls, bool live = false) const {
        const float c = tile_clearance(cls), g = c + TILE_HAIR;
        if (x - g < -map_half || x + g > map_half || z - g < -map_half || z + g > map_half) return false;
        // Square maps keep 0.28's 12 m (3-unit) edge frame (CCmpPathfinder.cpp:711 else branch);
        // on circular maps the radial check below already covers it (|x| past the frame implies
        // past the circle).
        const float sq = map_half - 3.0f - c;
        if (sq <= 0.0f || std::fabs(x) > sq || std::fabs(z) > sq) return false;
        if (edge_r > 0.0f) {
            const float r = edge_r - c;
            if (r <= 0.0f || x * x + z * z > r * r) return false;
        }
        return !steep_tile_in(x - g, z - g, x + g, z + g) && !water_cell_in(x - g, z - g, x + g, z + g, live);
    }
    bool node_terrain_open(int a, int b, int cls, bool live = false) const {
        return point_terrain_open(node_pos(a), node_pos(b), cls, live);
    }
    // node_terrain_open (not live) of every node of class cls, into `open`: the same sums, with the
    // spans of tiles and navcells a column's or a row's boxes reach worked out once a line.
    void nodes_terrain_open(int cls, std::vector<uint8_t> &open) {
        const int nw = node_res();
        open.assign(static_cast<size_t>(nw) * static_cast<size_t>(nw), 0);
        if (nw <= 0 || res <= 0) return;
        const float c = tile_clearance(cls), g = c + TILE_HAIR;
        const HeightSampleCache &h = height_cache_;
        const int tw = h.tiles_w;
        const bool steep = any_steep_tile();
        const bool water = !tile_water_sum_.empty() && tile_water_sum_.back() != 0;
        const float tinv = static_cast<float>(tw) / (map_half * 2.0f), winv = static_cast<float>(res) / (map_half * 2.0f);
        auto tile = [&](float v) { return std::clamp(floor_index((v + map_half) * tinv), 0, tw - 1); };
        auto cell = [&](float v) { return std::clamp(floor_index((v + map_half) * winv), 0, res - 1); };
        tile_span_.resize(static_cast<size_t>(nw));
        for (int a = 0; a < nw; ++a) {
            const float v = node_pos(a);
            TileSpan &sp = tile_span_[static_cast<size_t>(a)];
            sp.in = !(v - g < -map_half || v + g > map_half);
            sp.t0 = steep ? tile(v - g) : 0;
            sp.t1 = steep ? tile(v + g) + 1 : 0;
            sp.w0 = cell(v - g);
            sp.w1 = cell(v + g) + 1;
        }
        const size_t sw = static_cast<size_t>(tw) + 1, ww = static_cast<size_t>(res) + 1;
        const uint32_t *t = steep ? h.tiles_sum.data() : nullptr, *wt = water ? tile_water_sum_.data() : nullptr;
        const float r = edge_r - c;
        const float sq = map_half - 3.0f - c;
        for (int b = 0; b < nw; ++b) {
            const TileSpan &zb = tile_span_[static_cast<size_t>(b)];
            if (!zb.in) continue;
            const float z = node_pos(b);
            if (sq <= 0.0f || std::fabs(z) > sq) continue;
            const size_t j0 = static_cast<size_t>(zb.t0) * sw, j1 = static_cast<size_t>(zb.t1) * sw;
            const size_t k0 = static_cast<size_t>(zb.w0) * ww, k1 = static_cast<size_t>(zb.w1) * ww;
            uint8_t *row = open.data() + static_cast<size_t>(b) * static_cast<size_t>(nw);
            for (int a = 0; a < nw; ++a) {
                const TileSpan &xa = tile_span_[static_cast<size_t>(a)];
                if (!xa.in) continue;
                const float x = node_pos(a);
                if (std::fabs(x) > sq) continue;
                if (edge_r > 0.0f) {
                    if (r <= 0.0f || x * x + z * z > r * r) continue;
                }
                if (t) {
                    const size_t i0 = static_cast<size_t>(xa.t0), i1 = static_cast<size_t>(xa.t1);
                    if (t[j1 + i1] - t[j0 + i1] - t[j1 + i0] + t[j0 + i0] != 0) continue;
                }
                if (wt) {
                    const size_t i0 = static_cast<size_t>(xa.w0), i1 = static_cast<size_t>(xa.w1);
                    if (wt[k1 + i1] - wt[k0 + i1] - wt[k1 + i0] + wt[k0 + i0] != 0) continue;
                }
                row[a] = 1;
            }
        }
    }
    // A deep-water navcell touching the box [x0, x1] x [z0, z1]: as the nodes were worked out
    // (tile_water_sum_), or with live, as the navcells are now.
    bool water_cell_in(float x0, float z0, float x1, float z1, bool live = false) const {
        if (res <= 0) return false;
        const float inv = static_cast<float>(res) / (map_half * 2.0f);
        auto cell = [&](float v) { return std::clamp(floor_index((v + map_half) * inv), 0, res - 1); };
        if (live) {
            if (cells.blank()) return false;
            for (int y = cell(z0); y <= cell(z1); ++y)
                for (int x = cell(x0); x <= cell(x1); ++x)
                    if (cells.at(x, y) == NAV_WATER) return true;
            return false;
        }
        if (tile_water_sum_.empty() || tile_water_sum_.back() == 0) return false;
        const size_t sw = static_cast<size_t>(res) + 1;
        const size_t i0 = static_cast<size_t>(cell(x0)), i1 = static_cast<size_t>(cell(x1)) + 1;
        const size_t j0 = static_cast<size_t>(cell(z0)) * sw, j1 = (static_cast<size_t>(cell(z1)) + 1) * sw;
        const uint32_t *t = tile_water_sum_.data();
        return t[j1 + i1] - t[j0 + i1] - t[j1 + i0] + t[j0 + i0] != 0;
    }
    // Navcells' deep water as the nodes read it (Simulation::near_deep_water reads the navcells).
    void build_tile_water() {
        const size_t n = cells.data.size(), sw = static_cast<size_t>(res) + 1;
        tile_cell_water_.resize(n);
        for (size_t c = 0; c < n; ++c) tile_cell_water_[c] = cells.data[c] == NAV_WATER ? 1 : 0;
        tile_water_sum_.assign(sw * sw, 0);
        for (int y = 0; y < res; ++y) {
            uint32_t row = 0;
            for (int x = 0; x < res; ++x) {
                row += tile_cell_water_[static_cast<size_t>(y) * static_cast<size_t>(res) + static_cast<size_t>(x)];
                tile_water_sum_[(static_cast<size_t>(y) + 1) * sw + static_cast<size_t>(x) + 1] =
                        tile_water_sum_[static_cast<size_t>(y) * sw + static_cast<size_t>(x) + 1] + row;
            }
        }
    }

    // The faces of a (TILE_CROWD_FACE_*) that b lies within TILE_CROWD_GAP beyond: each face's slab, as
    // deep as the gap and as wide as the face, is tested against b (tile_obs_overlap, which a touch does
    // not count in, so a gap of exactly the gap stays open).
    static uint8_t tile_obs_faces_toward(const TileObstruction &a, const TileObstruction &b) {
        const float depth = 0.5f * (TILE_CROWD_GAP - TILE_HAIR);
        uint8_t bits = 0;
        for (int f = 0; f < 4; ++f) {
            const float sign = (f & 1) ? -1.0f : 1.0f;
            TileObstruction slab;
            slab.x = a.x;
            slab.z = a.z;
            slab.ux = a.ux;
            slab.uz = a.uz;
            slab.hu = a.hu;
            slab.hv = a.hv;
            if (f < 2) {
                slab.x += sign * (a.hu + depth) * a.ux;
                slab.z += sign * (a.hu + depth) * a.uz;
                slab.hu = depth;
            } else {
                slab.x += sign * (a.hv + depth) * -a.uz;
                slab.z += sign * (a.hv + depth) * a.ux;
                slab.hv = depth;
            }
            if (tile_obs_overlap(slab, 0.0f, b)) bits |= static_cast<uint8_t>(1u << f);
        }
        return bits;
    }
    // The faces of o that another in `list` (sorted by key, as the sets are) lies within TILE_CROWD_GAP beyond,
    // o itself (its key) left out: o's crowd flags as mark_tile_crowds would set them.
    static uint8_t tile_obs_faces_of(const TileObstruction &o, const std::vector<TileObstruction> &list) {
        uint8_t bits = 0;
        for (const TileObstruction &n : list)
            if (n.key != o.key) bits |= tile_obs_faces_toward(o, n);
        return bits;
    }
    // o as the large class grows it: each face its crowd bits name moves out by TILE_CROWD_GROW, so the
    // box takes the half of that on each side it moved (its centre by half the difference).
    static TileObstruction tile_obs_crowd_grown(const TileObstruction &o) {
        if (!o.crowd) return o;
        const float gu_p = (o.crowd & TILE_CROWD_FACE_U) ? TILE_CROWD_GROW : 0.0f;
        const float gu_n = (o.crowd & TILE_CROWD_FACE_NEG_U) ? TILE_CROWD_GROW : 0.0f;
        const float gv_p = (o.crowd & TILE_CROWD_FACE_V) ? TILE_CROWD_GROW : 0.0f;
        const float gv_n = (o.crowd & TILE_CROWD_FACE_NEG_V) ? TILE_CROWD_GROW : 0.0f;
        const float su = 0.5f * (gu_p - gu_n), sv = 0.5f * (gv_p - gv_n);
        TileObstruction r = o;
        r.x = o.x + su * o.ux + sv * -o.uz;
        r.z = o.z + su * o.uz + sv * o.ux;
        r.hu = o.hu + 0.5f * (gu_p + gu_n);
        r.hv = o.hv + 0.5f * (gv_p + gv_n);
        return r;
    }
    // Sets each structure's crowd bits: the faces with another in `list` within TILE_CROWD_GAP beyond. A sweep
    // along x finds the pairs: two boxes within the gap of each other have centres at most twice the largest
    // half diagonal plus the gap apart in x. The bits depend on the boxes alone, not on the order of the list.
    static void mark_tile_crowds(std::vector<TileObstruction> &list) {
        float reach = 0.0f;
        for (TileObstruction &o : list) {
            o.crowd = 0;
            reach = std::max(reach, std::sqrt(o.hu * o.hu + o.hv * o.hv));
        }
        std::vector<uint32_t> by_x(list.size());
        for (size_t p = 0; p < by_x.size(); ++p) by_x[p] = static_cast<uint32_t>(p);
        std::sort(by_x.begin(), by_x.end(), [&](uint32_t a, uint32_t b) { return list[a].x < list[b].x; });
        const float window = 2.0f * reach + TILE_CROWD_GAP;
        for (size_t p = 0; p < by_x.size(); ++p) {
            TileObstruction &a = list[by_x[p]];
            for (size_t q = p + 1; q < by_x.size() && list[by_x[q]].x - a.x <= window; ++q) {
                TileObstruction &b = list[by_x[q]];
                a.crowd |= tile_obs_faces_toward(a, b);
                b.crowd |= tile_obs_faces_toward(b, a);
            }
        }
    }
    // Whether two lists hold the same boxes (their crowd flags aside), key by key.
    static bool same_tile_shapes(const std::vector<TileObstruction> &a, const std::vector<TileObstruction> &b) {
        if (a.size() != b.size()) return false;
        for (size_t k = 0; k < a.size(); ++k)
            if (a[k].key != b[k].key || a[k].x != b[k].x || a[k].z != b[k].z || a[k].ux != b[k].ux || a[k].uz != b[k].uz ||
                    a[k].hu != b[k].hu || a[k].hv != b[k].hv)
                return false;
        return true;
    }

    // Adds (delta 1) or takes off (delta -1) obstruction o's cover of the nodes of class cls, listing
    // in `crossed` each node whose count rose from 0 or fell to it. 0.28 blocks the navcells inside
    // the shape grown by the class Clearance (RasterizeRectWithClearance); a node is covered when the
    // obstruction grown by the clearance (and a hair) holds it, as separation() and the steering grow
    // a footprint by a unit's clearance along its own axes. The shape is grown by the clearance
    // alone, as forest_cell_closed grows trees, not also by RasterizeRectWithClearance's
    // CLEARANCE_EXTENSION_RADIUS (1 m): on nodes twice as far apart as 0.28's navcells, that metre
    // would close gaps beside cliffs that 0.28 leaves open. A rectangle narrower than a node spacing
    // is widened to one, so that no step between two nodes outside it crosses it from side to side.
    // Square with the map, such a rectangle can't lie across a step between two nodes outside it,
    // nor across the square of four, without holding one of them. A turned rectangle's corner can
    // cut across a step (a corner no more than a step's length from both of the step's nodes), so a
    // turned one also covers the two nodes of each step it cuts that way: a step to one of the eight
    // neighbours, outside the rectangle too (a diagonal one only when the two nodes beside it are
    // outside it as well, as JPS takes a diagonal step only past two open nodes). Then no step
    // between open nodes, and no square of four, crosses it, and a path along open nodes is one a
    // unit can walk, while the nodes beside its faces stay open as they do beside a shape square
    // with the map. For the large class a face the crowd bits name grows by TILE_CROWD_GROW more
    // (tile_obs_crowd_grown). The counts are tile_obs_count_[cls], or `count` when given.
    void tile_obs_raster(const TileObstruction &o_in, int cls, int delta, std::vector<uint32_t> *crossed) {
        tile_obs_raster(o_in, cls, delta, crossed, tile_obs_count_[cls]);
    }
    void tile_obs_raster(const TileObstruction &o_in, int cls, int delta, std::vector<uint32_t> *crossed,
            std::vector<uint16_t> &count) const {
        const int nw = node_res();
        if (nw <= 0) return;
        const TileObstruction o = cls == 1 ? tile_obs_crowd_grown(o_in) : o_in;
        const float s = node_step(), hs = 0.5f * s, g = tile_clearance(cls) + TILE_HAIR;
        const bool turned = o.uz != 0.0f;
        const float hu = std::max(o.hu + g, hs + TILE_HAIR), hv = std::max(o.hv + g, hs + TILE_HAIR);
        const float au = std::fabs(o.ux), av = std::fabs(o.uz);
        // A step that cuts a turned rectangle's corner has its nodes within s * sqrt(2) of it.
        const float reach = turned ? 1.5f * s : 0.0f;
        // The half sizes, along x and z, of the box round the nodes it may cover.
        const float ex = turned ? hu * au + hv * av + reach : hu, ez = turned ? hu * av + hv * au + reach : hv;
        const int a0 = std::max(0, static_cast<int>(std::ceil((o.x - ex + map_half) / s)));
        const int a1 = std::min(nw - 1, floor_index((o.x + ex + map_half) / s));
        const int b0 = std::max(0, static_cast<int>(std::ceil((o.z - ez + map_half) / s)));
        const int b1 = std::min(nw - 1, floor_index((o.z + ez + map_half) / s));
        // Whether node (a, b) lies inside the turned rectangle.
        auto inside = [&](int a, int b) {
            const float dx = node_pos(a) - o.x, dz = node_pos(b) - o.z;
            const float lu = dx * o.ux + dz * o.uz, lv = dz * o.ux - dx * o.uz;
            return std::fabs(lu) < hu && std::fabs(lv) < hv;
        };
        // Its corners. A step that crosses it from face to opposite face is at least 2 min(hu, hv)
        // long inside it, so one at least a step and a half wide each way is cut only at a corner;
        // a narrower one is tested at every node round it.
        const float cu = hu * o.ux, cv = hu * o.uz, du = -hv * o.uz, dv = hv * o.ux;
        const float corner_x[4] = {o.x + cu + du, o.x + cu - du, o.x - cu + du, o.x - cu - du};
        const float corner_z[4] = {o.z + cv + dv, o.z + cv - dv, o.z - cv + dv, o.z - cv - dv};
        const bool wide = std::min(hu, hv) >= 0.75f * s;
        auto near_corner = [&](float x, float z) {
            if (!wide) return true;
            for (int k = 0; k < 4; ++k)
                if (std::fabs(x - corner_x[k]) <= reach && std::fabs(z - corner_z[k]) <= reach) return true;
            return false;
        };
        // Whether a step from node (a, b), outside the rectangle, to a neighbour outside it cuts it.
        auto cut = [&](int a, int b) {
            const float x = node_pos(a), z = node_pos(b);
            if (!near_corner(x, z)) return false;
            for (int db = -1; db <= 1; ++db)
                for (int da = -1; da <= 1; ++da) {
                    const int a2 = a + da, b2 = b + db;
                    if ((da == 0 && db == 0) || a2 < 0 || b2 < 0 || a2 >= nw || b2 >= nw || inside(a2, b2)) continue;
                    if (da != 0 && db != 0 && (inside(a2, b) || inside(a, b2))) continue;
                    if (rect_segment_hit(o, hu, hv, x, z, node_pos(a2), node_pos(b2))) return true;
                }
            return false;
        };
        for (int b = b0; b <= b1; ++b)
            for (int a = a0; a <= a1; ++a) {
                bool in;
                if (!turned) {
                    in = std::fabs(node_pos(a) - o.x) < hu && std::fabs(node_pos(b) - o.z) < hv;
                } else {
                    in = inside(a, b) || cut(a, b);
                }
                if (!in) continue;
                const size_t k = static_cast<size_t>(b) * static_cast<size_t>(nw) + static_cast<size_t>(a);
                if (delta > 0) {
                    if (count[k]++ == 0 && crossed) crossed->push_back(static_cast<uint32_t>(k));
                } else if (count[k] > 0 && --count[k] == 0 && crossed) {
                    crossed->push_back(static_cast<uint32_t>(k));
                }
            }
    }

    // Whether obstruction o, put down among the static obstructions `obs`, would part the ground of
    // class cls round it: whether, of the open nodes beside those it closes, some that are joined
    // without it are no longer joined with it, the part cut off holding more nodes than o closes (a
    // pocket smaller than that, a node or two between it and a cliff, counts for nothing). The ground
    // is the terrain as the nodes take it (node_terrain_open: steep tiles, deep water as the navcells
    // have it now, and the map's edge, each kept off by the class clearance) with `obs` and o
    // rasterized as tile_obs_raster does, nodes joined to their four neighbours as the regions join
    // them (flood_tile_regions). The trees, bushes and treasures are left out, as they are cut,
    // gathered and picked up: a way only they narrow is still one o would shut once they are gone.
    // Simulation::place_starting_walls leaves out a tower of RTS0's own maps that would part either
    // class (Acropolis Heights: the only ramp up to the civic centre's plateau).
    bool tile_obs_parts_ground(const TileObstruction &o_in, const std::vector<TileObstruction> &obs, int cls) const {
        const int nw = node_res();
        if (nw <= 0 || cells.blank()) return false;
        const uint32_t w = static_cast<uint32_t>(nw);
        const size_t n = static_cast<size_t>(nw) * static_cast<size_t>(nw);
        // o as it would be listed: crowded by the obstructions it would stand among (the flags of obs are
        // those they have now, not the ones o would give them).
        TileObstruction o = o_in;
        o.crowd = tile_obs_faces_of(o_in, obs);
        std::vector<uint16_t> cover(n, 0);
        for (const TileObstruction &q : obs) tile_obs_raster(q, cls, 1, nullptr, cover);
        std::vector<uint32_t> shut; // the nodes o covers and nothing in obs does
        tile_obs_raster(o, cls, 1, &shut, cover);
        // Per node: bit 0 its terrain worked out, bit 1 open to it, bit 2 covered by o alone.
        std::vector<uint8_t> node(n, 0);
        for (const uint32_t k : shut) node[k] |= 4;
        auto open = [&](uint32_t k, bool with) {
            if (cover[k] && (with || !(node[k] & 4))) return false;
            if (!(node[k] & 1)) node[k] |= node_terrain_open(static_cast<int>(k % w), static_cast<int>(k / w), cls, true) ? 3 : 1;
            return (node[k] & 2) != 0;
        };
        std::vector<uint32_t> mark(n, 0);
        uint32_t stamp = 1;
        auto each_neighbour = [&](uint32_t k, auto &&f) {
            const uint32_t a = k % w, b = k / w;
            if (a > 0) f(k - 1);
            if (a + 1 < w) f(k + 1);
            if (b > 0) f(k - w);
            if (b + 1 < w) f(k + w);
        };
        // The open nodes beside the ones o closes, sorted.
        std::vector<uint32_t> edge;
        for (const uint32_t k : shut) {
            if (!open(k, false)) continue;
            each_neighbour(k, [&](uint32_t j) {
                if (mark[j] != stamp && open(j, true)) {
                    mark[j] = stamp;
                    edge.push_back(j);
                }
            });
        }
        if (edge.size() < 2) return false;
        std::sort(edge.begin(), edge.end());
        // A flood from edge node e, with o or without it, putting the edge nodes it reaches that
        // `want` lists and no group holds yet into group g. It stops when it runs out, at `cap`
        // nodes, or once none of those edge nodes is left and it has reached more than `pocket`
        // nodes (its part is no pocket). It returns the nodes it reached when it ran out, else `open`.
        // Breadth first, so that the edge nodes round o, a few steps apart, are met first.
        const size_t pocket = shut.size(), open_part = static_cast<size_t>(-1);
        std::vector<uint32_t> queue;
        auto flood = [&](size_t e, bool with, int g, std::vector<int> &group, const std::vector<uint8_t> &want, size_t &left, size_t cap) {
            size_t nodes = 0;
            queue.assign(1, edge[e]);
            mark[edge[e]] = ++stamp;
            while (nodes < queue.size() && nodes < cap && (left || nodes <= pocket)) {
                const uint32_t k = queue[nodes++];
                const auto it = std::lower_bound(edge.begin(), edge.end(), k);
                if (it != edge.end() && *it == k) {
                    const size_t i = static_cast<size_t>(it - edge.begin());
                    if (want[i] && group[i] < 0) {
                        group[i] = g;
                        --left;
                    }
                }
                each_neighbour(k, [&](uint32_t j) {
                    if (mark[j] != stamp && open(j, with)) {
                        mark[j] = stamp;
                        queue.push_back(j);
                    }
                });
            }
            return nodes == queue.size() ? nodes : open_part;
        };
        // With o: the groups the edge nodes fall into, each flood held to TILE_PART_FLOOD nodes; a
        // group of no more than `pocket` nodes is a pocket. Groups whose floods stopped short may
        // be one part: floods without the cap join them.
        const std::vector<uint8_t> all(edge.size(), 1);
        std::vector<int> with(edge.size(), -1);
        std::vector<size_t> size;
        size_t left = edge.size();
        for (size_t e = 0; e < edge.size(); ++e)
            if (with[e] < 0) size.push_back(flood(e, true, static_cast<int>(size.size()), with, all, left, TILE_PART_FLOOD));
        auto parts_of = [&]() { return std::count_if(size.begin(), size.end(), [&](size_t z) { return z > pocket; }); };
        if (parts_of() < 2) return false;
        for (size_t gr = 0; gr < size.size(); ++gr) {
            if (size[gr] != open_part) continue;
            std::vector<uint8_t> later(edge.size(), 0); // the edge nodes of the open groups after gr
            size_t from = edge.size(), todo = 0;
            for (size_t e = 0; e < edge.size(); ++e) {
                const size_t h = static_cast<size_t>(with[e]);
                if (h == gr && from == edge.size()) from = e;
                if (h > gr && size[h] == open_part) {
                    later[e] = 1;
                    ++todo;
                }
            }
            if (!todo) continue;
            std::vector<int> reached(edge.size(), -1);
            flood(from, true, 0, reached, later, todo, open_part);
            for (size_t e = 0; e < edge.size(); ++e)
                if (reached[e] == 0) {
                    size[static_cast<size_t>(with[e])] = 0; // one part with gr's
                    with[e] = static_cast<int>(gr);
                }
        }
        if (parts_of() < 2) return false;
        // Without o: the groups the edge nodes of those parts fall into; fewer, and o parts them.
        std::vector<uint8_t> want(edge.size());
        for (size_t e = 0; e < edge.size(); ++e) want[e] = size[static_cast<size_t>(with[e])] > pocket;
        std::vector<int> without(edge.size(), -1);
        left = static_cast<size_t>(std::count(want.begin(), want.end(), uint8_t{1}));
        int groups = 0;
        for (size_t e = 0; e < edge.size(); ++e)
            if (want[e] && without[e] < 0) flood(e, false, groups++, without, want, left, open_part);
        return groups < parts_of();
    }

    // Calls f on the index of each bucket (tile_obs_buckets_) that obstruction o's box, grown by the
    // large clearance, reaches into.
    template <class F>
    void each_tile_obs_bucket(const TileObstruction &o, F &&f) const {
        const int bw = tile_obs_bw_;
        auto bucket = [&](float v) { return std::clamp(floor_index((v + map_half) / TILE_OBS_BUCKET), 0, bw - 1); };
        const float e = std::max(o.hu, o.hv) * 1.4143f + TILE_PATH_CLEARANCE_LARGE + TILE_HAIR + (o.crowd ? TILE_CROWD_GROW : 0.0f);
        const int i0 = bucket(o.x - e), i1 = bucket(o.x + e), j0 = bucket(o.z - e), j1 = bucket(o.z + e);
        for (int j = j0; j <= j1; ++j)
            for (int i = i0; i <= i1; ++i) f(static_cast<size_t>(j) * static_cast<size_t>(bw) + static_cast<size_t>(i));
    }
    void tile_obs_bucket_add(const TileObstruction &o) {
        each_tile_obs_bucket(o, [&](size_t b) { tile_obs_buckets_[b].push_back(o); });
    }
    void tile_obs_bucket_remove(const TileObstruction &o) {
        each_tile_obs_bucket(o, [&](size_t b) {
            std::vector<TileObstruction> &v = tile_obs_buckets_[b];
            for (size_t k = 0; k < v.size(); ++k)
                if (v[k].key == o.key) {
                    v[k] = v.back();
                    v.pop_back();
                    break;
                }
        });
    }
    // The obstructions by bucket, for segment_hits_tile_obs, worked out afresh.
    void build_tile_obs_buckets() {
        const int bw = std::max(1, static_cast<int>(std::ceil(map_half * 2.0f / TILE_OBS_BUCKET)));
        tile_obs_bw_ = bw;
        tile_obs_half_ = map_half;
        tile_obs_buckets_.assign(static_cast<size_t>(bw) * static_cast<size_t>(bw), {});
        for (int set = 0; set < TILE_OBS_SETS; ++set)
            for (const TileObstruction &o : tile_obs_[set]) tile_obs_bucket_add(o);
    }
    // Walks two lists sorted by key: gone(o) for each obstruction only in `from`, came(o) for each
    // only in `to`, and both for one whose shape or place changed.
    template <class Gone, class Came>
    static void diff_tile_obs(const std::vector<TileObstruction> &from, const std::vector<TileObstruction> &to, Gone &&gone, Came &&came) {
        size_t i = 0, j = 0;
        while (i < from.size() || j < to.size()) {
            if (j == to.size() || (i < from.size() && from[i].key < to[j].key)) {
                gone(from[i++]);
            } else if (i == from.size() || to[j].key < from[i].key) {
                came(to[j++]);
            } else {
                if (from[i] != to[j]) {
                    gone(from[i]);
                    came(to[j]);
                }
                ++i;
                ++j;
            }
        }
    }
    // Whether the segment from (ax, az) to (bx, bz) passes inside obstruction o grown by g, its ends
    // included: the segment in the rectangle's own axes, clipped to the slabs |u| < hu, |v| < hv.
    static bool tile_obs_segment_hit(const TileObstruction &o, float ax, float az, float bx, float bz, float g) {
        return rect_segment_hit(o, o.hu + g, o.hv + g, ax, az, bx, bz);
    }
    // The same for o's rectangle with the half sizes hu and hv.
    static bool rect_segment_hit(const TileObstruction &o, float hu, float hv, float ax, float az, float bx, float bz) {
        const float pax = ax - o.x, paz = az - o.z, pbx = bx - o.x, pbz = bz - o.z;
        const float ua = o.uz == 0.0f ? pax : pax * o.ux + paz * o.uz, va = o.uz == 0.0f ? paz : paz * o.ux - pax * o.uz;
        const float ub = o.uz == 0.0f ? pbx : pbx * o.ux + pbz * o.uz, vb = o.uz == 0.0f ? pbz : pbz * o.ux - pbx * o.uz;
        float t0 = 0.0f, t1 = 1.0f;
        auto slab = [&](float p, float q, float h) {
            const float d = q - p;
            if (std::fabs(d) < 1e-9f) return std::fabs(p) < h;
            float s0 = (-h - p) / d, s1 = (h - p) / d;
            if (s0 > s1) std::swap(s0, s1);
            t0 = std::max(t0, s0);
            t1 = std::min(t1, s1);
            return t0 < t1;
        };
        return slab(ua, ub, hu) && slab(va, vb, hv);
    }
    // Calls f on each obstruction in the buckets the segment from (ax, az) to (bx, bz), grown by g,
    // passes through (one in several of them more than once), row by row, until f returns true;
    // whether it did. Every obstruction the segment comes within the large clearance of is among
    // them (each_tile_obs_bucket).
    template <class F>
    bool any_tile_obs_near_segment(float ax, float az, float bx, float bz, float g, F &&f) const {
        if ((tile_obs_[TILE_OBS_STRUCTURES].empty() && tile_obs_[TILE_OBS_GAIA].empty()) || tile_obs_half_ != map_half ||
                tile_obs_bw_ <= 0 || tile_obs_buckets_.size() != static_cast<size_t>(tile_obs_bw_) * static_cast<size_t>(tile_obs_bw_))
            return false;
        const int bw = tile_obs_bw_;
        auto bucket = [&](float v) { return std::clamp(floor_index((v + map_half) / TILE_OBS_BUCKET), 0, bw - 1); };
        const float dx = bx - ax, dz = bz - az;
        const int j0 = bucket(std::min(az, bz) - g), j1 = bucket(std::max(az, bz) + g);
        for (int j = j0; j <= j1; ++j) {
            // The part of the segment whose z lies in the row (grown by g).
            float t0 = 0.0f, t1 = 1.0f;
            if (std::fabs(dz) > 1e-6f) {
                const float zlo = static_cast<float>(j) * TILE_OBS_BUCKET - map_half - g, zhi = zlo + TILE_OBS_BUCKET + 2.0f * g;
                float u0 = (zlo - az) / dz, u1 = (zhi - az) / dz;
                if (u0 > u1) std::swap(u0, u1);
                t0 = std::max(t0, u0);
                t1 = std::min(t1, u1);
                if (t0 > t1) continue;
            }
            const float xa = ax + dx * t0, xb = ax + dx * t1;
            const int i0 = bucket(std::min(xa, xb) - g), i1 = bucket(std::max(xa, xb) + g);
            for (int i = i0; i <= i1; ++i) {
                const size_t b = static_cast<size_t>(j) * static_cast<size_t>(bw) + static_cast<size_t>(i);
                for (const TileObstruction &o : tile_obs_buckets_[b])
                    if (f(o)) return true;
            }
        }
        return false;
    }
    // Whether the segment from (ax, az) to (bx, bz) passes inside an obstruction grown by c and a
    // hair, its ends included (Pathfinding::CheckLineMovement over navcells that carry them).
    // The large class (cls 1) tests the structures as tile_obs_crowd_grown has them.
    bool segment_hits_tile_obs(float ax, float az, float bx, float bz, float c, int cls) const {
        const float g = c + TILE_HAIR;
        return any_tile_obs_near_segment(ax, az, bx, bz, g, [&](const TileObstruction &o) {
            return tile_obs_segment_hit(cls == 1 ? tile_obs_crowd_grown(o) : o, ax, az, bx, bz, g);
        });
    }

    bool tile_open_cached() const {
        const int nw = node_res();
        const size_t n = static_cast<size_t>(nw) * static_cast<size_t>(nw);
        if (!tile_open_ready_ || tile_open_version_ != terrain_version_ || tile_open_res_ != res || tile_open_half_ != map_half ||
                tile_open_edge_r_ != edge_r || tile_cell_water_.size() != cells.data.size())
            return false;
        const size_t nb = static_cast<size_t>(nw) * static_cast<size_t>(node_words());
        if (tile_open_kept_ != tile_kept_mask()) return false;
        for (int cls = 0; cls < TILE_CLASSES; ++cls)
            if (tile_class_kept(cls) && (tile_open_[cls].size() != n || tile_region_[cls].size() != n || tile_obs_count_[cls].size() != n ||
                    tile_bits_rows_[cls].size() != nb || tile_bits_cols_[cls].size() != nb))
                return false;
        return true;
    }
    // The classes whose nodes are kept, one bit each (tile_class_kept).
    unsigned tile_kept_mask() const {
        unsigned m = 0;
        for (int cls = 0; cls < TILE_CLASSES; ++cls)
            if (tile_class_kept(cls)) m |= 1u << cls;
        return m;
    }
    // Works the nodes out (rebuild_regions: all of them; rebuild_dirty_regions: those within the
    // large clearance of a dirty navcell whose deep water changed), as HierarchicalPathfinder::Update
    // redoes the dirty chunks. The steep tiles are the terrain's; the obstructions come and go
    // through set_tile_obstructions.
    void update_tile_open(bool all) {
        auto drop = [&](int cls) {
            tile_open_[cls].clear();
            tile_region_[cls].clear();
            tile_obs_count_[cls].clear();
            tile_bits_rows_[cls].clear();
            tile_bits_cols_[cls].clear();
        };
        if (!tile_nodes_on()) {
            tile_open_ready_ = false;
            for (int cls = 0; cls < TILE_CLASSES; ++cls) drop(cls);
            return;
        }
        const bool whole = all || !tile_open_cached() || dirty.blank();
        if (!whole && !any_dirty()) return;
        const int nw = node_res();
        const size_t n = static_cast<size_t>(nw) * static_cast<size_t>(nw);
        if (tile_mark_.size() != n) {
            tile_mark_.assign(n, 0);
            tile_mark_stamp_ = 0;
        }
        if (whole) {
            build_tile_water();
            if (tile_obs_half_ != map_half) build_tile_obs_buckets();
            for (int cls = 0; cls < TILE_CLASSES; ++cls) {
                if (!tile_class_kept(cls)) {
                    drop(cls);
                    continue;
                }
                tile_obs_count_[cls].assign(n, 0);
                for (int set = 0; set < TILE_OBS_SETS; ++set)
                    for (const TileObstruction &o : tile_obs_[set]) tile_obs_raster(o, cls, 1, nullptr);
                std::vector<uint8_t> &open = tile_open_[cls];
                nodes_terrain_open(cls, open);
                for (size_t k = 0; k < n; ++k)
                    if (tile_obs_count_[cls][k]) open[k] = 0;
                build_node_bits(cls);
                flood_tile_regions(cls);
            }
            tile_open_version_ = terrain_version_;
            tile_open_kept_ = tile_kept_mask();
            tile_open_res_ = res;
            tile_open_half_ = map_half;
            tile_open_edge_r_ = edge_r;
            tile_open_ready_ = true;
            return;
        }
        // The dirty navcells whose deep water changed (a structure or the edge closing one leaves
        // the nodes alone: they take structures from their obstructions and the edge themselves).
        tile_cells_.clear();
        for (size_t c = 0; c < tile_cell_water_.size(); ++c) {
            if (!dirty.data[c]) continue;
            if ((cells.data[c] == NAV_WATER ? 1 : 0) != tile_cell_water_[c]) tile_cells_.push_back(static_cast<uint32_t>(c));
        }
        if (tile_cells_.empty()) return;
        build_tile_water();
        const float cw = (map_half * 2.0f) / static_cast<float>(res), s = node_step();
        const float g = TILE_PATH_CLEARANCE_LARGE + 2.0f * TILE_HAIR;
        for (int cls = 0; cls < TILE_CLASSES; ++cls) {
            if (!tile_class_kept(cls)) continue;
            std::vector<uint8_t> &open = tile_open_[cls];
            tile_closed_.clear();
            tile_opened_.clear();
            for (const uint32_t c : tile_cells_) {
                const int cx = static_cast<int>(c % static_cast<uint32_t>(res)), cy = static_cast<int>(c / static_cast<uint32_t>(res));
                const float x0 = static_cast<float>(cx) * cw - map_half - g, x1 = static_cast<float>(cx + 1) * cw - map_half + g;
                const float z0 = static_cast<float>(cy) * cw - map_half - g, z1 = static_cast<float>(cy + 1) * cw - map_half + g;
                const int a0 = std::max(0, floor_index((x0 + map_half) / s));
                const int a1 = std::min(nw - 1, static_cast<int>(std::ceil((x1 + map_half) / s)));
                const int b0 = std::max(0, floor_index((z0 + map_half) / s));
                const int b1 = std::min(nw - 1, static_cast<int>(std::ceil((z1 + map_half) / s)));
                for (int b = b0; b <= b1; ++b)
                    for (int a = a0; a <= a1; ++a) {
                        const size_t k = static_cast<size_t>(b) * static_cast<size_t>(nw) + static_cast<size_t>(a);
                        const uint8_t v = tile_obs_count_[cls][k] == 0 && node_terrain_open(a, b, cls) ? 1 : 0;
                        if (v == open[k]) continue;
                        set_node_open(cls, static_cast<uint32_t>(k), v);
                        (v ? tile_opened_ : tile_closed_).push_back(static_cast<uint32_t>(k));
                    }
            }
            update_tile_regions(cls);
        }
    }

    // Sets the obstructions of one set (TILE_OBS_STRUCTURES or TILE_OBS_GAIA; `list` sorted by key,
    // taken over; sig, its lister's signature for it) and keeps the nodes and their regions up to
    // date: the obstructions gone or changed come off, the new or changed ones go on, and only the
    // nodes whose cover started or ended are worked out again, as CCmpObstructionManager marks the
    // grid dirty round a shape that changed.
    void set_tile_obstructions(int set, std::vector<TileObstruction> &list, uint64_t sig = 0) {
        std::vector<TileObstruction> &old = tile_obs_[set];
        tile_obs_sig_[set] = sig;
        // The crowd flags follow from the boxes: where the boxes are unchanged the flags are too.
        if (set == TILE_OBS_STRUCTURES) {
            if (same_tile_shapes(list, old))
                for (size_t k = 0; k < list.size(); ++k) list[k].crowd = old[k].crowd;
            else
                mark_tile_crowds(list);
        }
        if (list == old) return;
        if (tile_open_cached()) {
            const int nw = node_res();
            for (int cls = 0; cls < TILE_CLASSES; ++cls) {
                if (!tile_class_kept(cls)) continue;
                tile_crossed_.clear();
                diff_tile_obs(old, list, [&](const TileObstruction &o) { tile_obs_raster(o, cls, -1, &tile_crossed_); },
                        [&](const TileObstruction &o) { tile_obs_raster(o, cls, 1, &tile_crossed_); });
                std::vector<uint8_t> &open = tile_open_[cls];
                tile_closed_.clear();
                tile_opened_.clear();
                for (const uint32_t k : tile_crossed_) {
                    const int a = static_cast<int>(k % static_cast<uint32_t>(nw)), b = static_cast<int>(k / static_cast<uint32_t>(nw));
                    const uint8_t v = tile_obs_count_[cls][k] == 0 && node_terrain_open(a, b, cls) ? 1 : 0;
                    if (v == open[k]) continue; // also a node listed twice, once it is set
                    set_node_open(cls, k, v);
                    (v ? tile_opened_ : tile_closed_).push_back(k);
                }
                update_tile_regions(cls);
            }
        }
        if (tile_obs_half_ != map_half || tile_obs_buckets_.size() != static_cast<size_t>(tile_obs_bw_) * static_cast<size_t>(tile_obs_bw_)) {
            old.swap(list);
            build_tile_obs_buckets();
            return;
        }
        diff_tile_obs(old, list, [&](const TileObstruction &o) { tile_obs_bucket_remove(o); },
                [&](const TileObstruction &o) { tile_obs_bucket_add(o); });
        old.swap(list);
    }

    // The open nodes' regions, as HierarchicalPathfinder's global regions: the nodes a unit can walk
    // between (JPS never cuts a corner, so four neighbours connect them). Structures and the
    // off-world band close nodes, as 0.28's regions follow the class's passability with its static
    // obstructions, so a goal they seal off moves to the start's region before a search floods it
    // (MakeGoalReachable). flood_tile_regions numbers them all afresh, in row order.
    void flood_tile_regions(int cls) {
        const int nw = node_res();
        const std::vector<uint8_t> &open = tile_open_[cls];
        std::vector<uint32_t> &region = tile_region_[cls];
        region.assign(open.size(), 0);
        // The runs of open nodes along each row, each joined to the runs it touches in the row before
        // (the first run of a region heads it); then each run takes its region's number, numbered as
        // the rows meet them: the numbers a flood from each open node not yet reached, row by row,
        // would give.
        std::vector<TileRun> &runs = tile_runs_;
        runs.clear();
        auto head = [&](uint32_t r) {
            while (runs[r].up != r) {
                runs[r].up = runs[runs[r].up].up;
                r = runs[r].up;
            }
            return r;
        };
        size_t prev0 = 0, prev1 = 0;
        for (int j = 0; j < nw; ++j) {
            const uint8_t *row = open.data() + static_cast<size_t>(j) * static_cast<size_t>(nw);
            const size_t first = runs.size();
            size_t p = prev0;
            for (int a = 0; a < nw;) {
                if (!row[a]) {
                    ++a;
                    continue;
                }
                int b = a + 1;
                while (b < nw && row[b]) ++b;
                const uint32_t r = static_cast<uint32_t>(runs.size());
                runs.push_back({j, a, b, r});
                while (p < prev1 && runs[p].b <= a) ++p;
                for (size_t q = p; q < prev1 && runs[q].a < b; ++q) {
                    const uint32_t x = head(r), y = head(static_cast<uint32_t>(q));
                    if (x != y) runs[std::max(x, y)].up = std::min(x, y);
                }
                a = b;
            }
            prev0 = first;
            prev1 = runs.size();
        }
        tile_run_id_.resize(runs.size());
        uint32_t next = 0;
        for (uint32_t r = 0; r < static_cast<uint32_t>(runs.size()); ++r) {
            const uint32_t h = head(r);
            const uint32_t id = h == r ? ++next : tile_run_id_[h];
            tile_run_id_[r] = id;
            uint32_t *out = region.data() + static_cast<size_t>(runs[r].j) * static_cast<size_t>(nw);
            std::fill(out + runs[r].a, out + runs[r].b, id);
        }
        tile_region_next_[cls] = next;
    }

    // The regions after a change closed the nodes tile_closed_ and opened tile_opened_, as a flood
    // would leave them (the same nodes together; the ids may differ), without one in the common
    // cases, as HierarchicalPathfinder::Update redoes only the dirty chunks. First the closed nodes:
    // a region they border is still whole when, round each cluster of them, its open nodes beside
    // the cluster meet near it (a structure placed: the way round it); else split_tile_region finds
    // the parts it fell into, or a flood does when they lie round more than one cluster. Then each
    // run of opened nodes joins the one region beside it, or makes a new one; one that joins two
    // regions floods afresh.
    void update_tile_regions(int cls) {
        if (tile_closed_.empty() && tile_opened_.empty()) return;
        const int nw = node_res();
        const uint32_t w = static_cast<uint32_t>(nw);
        const std::vector<uint8_t> &open = tile_open_[cls];
        std::vector<uint32_t> &region = tile_region_[cls];
        std::vector<uint32_t> &stack = tile_stack_;
        auto stamp = [&]() {
            if (++tile_mark_stamp_ == 0) {
                std::fill(tile_mark_.begin(), tile_mark_.end(), 0u);
                tile_mark_stamp_ = 1;
            }
            return tile_mark_stamp_;
        };
        auto neighbours = [&](uint32_t k, auto &&f) {
            const uint32_t i = k % w, j = k / w;
            if (i > 0) f(k - 1);
            if (i + 1 < w) f(k + 1);
            if (j > 0) f(k - w);
            if (j + 1 < w) f(k + w);
        };
        if (!tile_closed_.empty()) {
            for (uint32_t k : tile_closed_) region[k] = 0;
            // The closed nodes in clusters that meet side to side (tile_order_, cluster by cluster),
            // each with its box. A way that crossed closed nodes crossed one cluster at a time, so a
            // region is whole when, cluster by cluster, its open nodes beside it all meet.
            const uint32_t sc = stamp();
            for (uint32_t k : tile_closed_) tile_mark_[k] = sc;
            const uint32_t sd = stamp();
            if (sd < sc) return flood_tile_regions(cls); // the stamps wrapped round
            tile_order_.clear();
            tile_clusters_.clear();
            for (const uint32_t k0 : tile_closed_) {
                if (tile_mark_[k0] != sc) continue;
                TileCluster c{static_cast<uint32_t>(tile_order_.size()), 0, nw, nw, -1, -1};
                tile_mark_[k0] = sd;
                tile_order_.push_back(k0);
                for (size_t x = c.first; x < tile_order_.size(); ++x) {
                    const uint32_t k = tile_order_[x];
                    const int i = static_cast<int>(k % w), j = static_cast<int>(k / w);
                    c.i0 = std::min(c.i0, i);
                    c.i1 = std::max(c.i1, i);
                    c.j0 = std::min(c.j0, j);
                    c.j1 = std::max(c.j1, j);
                    neighbours(k, [&](uint32_t q) {
                        if (tile_mark_[q] != sc) return;
                        tile_mark_[q] = sd;
                        tile_order_.push_back(q);
                    });
                }
                c.last = static_cast<uint32_t>(tile_order_.size());
                tile_clusters_.push_back(c);
            }
            struct Box {
                int i0, j0, i1, j1;
            };
            // Marks with s the open nodes in box b that meet node e through open nodes in it.
            auto fill = [&](uint32_t e, uint32_t s, const Box &b) {
                tile_mark_[e] = s;
                stack.assign(1, e);
                while (!stack.empty()) {
                    const uint32_t k = stack.back();
                    stack.pop_back();
                    const int i = static_cast<int>(k % w), j = static_cast<int>(k / w);
                    auto visit = [&](uint32_t q) {
                        if (open[q] && tile_mark_[q] != s) {
                            tile_mark_[q] = s;
                            stack.push_back(q);
                        }
                    };
                    if (i > b.i0) visit(k - 1);
                    if (i < b.i1) visit(k + 1);
                    if (j > b.j0) visit(k - w);
                    if (j < b.j1) visit(k + w);
                }
            };
            // Cluster by cluster, the open nodes beside it, region by region (an opened node, still
            // without one, waits for the runs below). They all meet in the box a tile round the
            // cluster, and the region is whole there; else they go in groups that meet in the box 8
            // tiles round it. With more than one group, each group's first node is a seed of the
            // region's split: every part the region fell into holds open nodes beside such a cluster.
            // Seeds round more than one cluster (a forest planted at once) would have far to grow
            // before they meet, and a flood is quicker.
            tile_split_.clear();
            const TileCluster *split_at = nullptr;
            for (const TileCluster &c : tile_clusters_) {
                auto grown = [&](int m) {
                    return Box{std::max(0, c.i0 - m), std::max(0, c.j0 - m), std::min(nw - 1, c.i1 + m), std::min(nw - 1, c.j1 + m)};
                };
                const Box near = grown(2), wide = grown(16);
                tile_edge_.clear();
                for (uint32_t x = c.first; x < c.last; ++x)
                    neighbours(tile_order_[x], [&](uint32_t q) {
                        if (open[q]) tile_edge_.push_back(q);
                    });
                tile_ids_.clear();
                for (const uint32_t first : tile_edge_) {
                    const uint32_t id = region[first];
                    if (id == 0 || std::find(tile_ids_.begin(), tile_ids_.end(), id) != tile_ids_.end()) continue;
                    tile_ids_.push_back(id);
                    const uint32_t s0 = stamp();
                    fill(first, s0, near);
                    bool met = true;
                    for (const uint32_t e : tile_edge_)
                        if (region[e] == id && tile_mark_[e] != s0) {
                            met = false;
                            break;
                        }
                    if (met) continue;
                    if (tile_mark_stamp_ > UINT32_MAX - static_cast<uint32_t>(tile_edge_.size()) - 1)
                        return flood_tile_regions(cls); // the stamps would wrap round
                    const size_t seeds = tile_split_.size();
                    const uint32_t base = tile_mark_stamp_ + 1;
                    for (const uint32_t e : tile_edge_) {
                        if (region[e] != id || (tile_mark_[e] >= base && tile_mark_[e] <= tile_mark_stamp_)) continue;
                        tile_split_.push_back({id, e});
                        fill(e, stamp(), wide);
                    }
                    if (tile_split_.size() == seeds + 1) {
                        tile_split_.pop_back(); // one group: whole
                    } else {
                        if (split_at && split_at != &c) return flood_tile_regions(cls);
                        split_at = &c;
                    }
                }
            }
            // The regions that may have split, each from all its seeds.
            std::sort(tile_split_.begin(), tile_split_.end());
            tile_split_.erase(std::unique(tile_split_.begin(), tile_split_.end()), tile_split_.end());
            for (size_t x = 0; x < tile_split_.size();) {
                size_t y = x;
                tile_seeds_.clear();
                for (; y < tile_split_.size() && tile_split_[y].first == tile_split_[x].first; ++y) tile_seeds_.push_back(tile_split_[y].second);
                if (!split_tile_region(cls)) return flood_tile_regions(cls);
                x = y;
            }
        }
        // Each run of newly opened nodes takes the one region beside it, or a new id.
        for (uint32_t k0 : tile_opened_) {
            if (region[k0] || !open[k0]) continue;
            const uint32_t s = stamp();
            uint32_t id = 0;
            bool two = false;
            tile_edge_.assign(1, k0);
            tile_mark_[k0] = s;
            stack.assign(1, k0);
            while (!stack.empty()) {
                const uint32_t k = stack.back();
                stack.pop_back();
                neighbours(k, [&](uint32_t q) {
                    if (!open[q] || tile_mark_[q] == s) return;
                    if (region[q]) {
                        two = two || (id != 0 && region[q] != id);
                        id = region[q];
                        return;
                    }
                    tile_mark_[q] = s;
                    tile_edge_.push_back(q);
                    stack.push_back(q);
                });
            }
            if (two) return flood_tile_regions(cls);
            if (id == 0) id = ++tile_region_next_[cls];
            for (uint32_t q : tile_edge_) region[q] = id;
        }
    }

    // A region after closed nodes cut through it, from tile_seeds_ (an open node of it in each group
    // that doesn't meet the others near the change): each group grows a node at a time in turn over
    // the open nodes, two that meet join, until at most one still grows. A group that stopped is a
    // whole region and takes a new id; the one still growing (or the first seed's, when none is)
    // keeps the region's. A pocket the change cut off costs its own size, not a flood of the map.
    // False when the visit stamps run out: the caller floods afresh.
    bool split_tile_region(int cls) {
        const uint32_t n = static_cast<uint32_t>(tile_seeds_.size());
        if (tile_mark_stamp_ > UINT32_MAX - n - 1) return false;
        const uint32_t base = tile_mark_stamp_ + 1;
        tile_mark_stamp_ += n;
        const uint32_t w = static_cast<uint32_t>(node_res());
        const std::vector<uint8_t> &open = tile_open_[cls];
        std::vector<uint32_t> &region = tile_region_[cls];
        if (tile_group_.size() < n) tile_group_.resize(n);
        tile_grown_.assign(n, 0);
        tile_join_.resize(n);
        for (uint32_t g = 0; g < n; ++g) {
            tile_join_[g] = g;
            tile_group_[g].assign(1, tile_seeds_[g]);
            tile_mark_[tile_seeds_[g]] = base + g;
        }
        auto root = [&](uint32_t g) {
            while (tile_join_[g] != g) g = tile_join_[g] = tile_join_[tile_join_[g]];
            return g;
        };
        // How many joined groups still grow (a group's own nodes not all grown from), and one of them.
        auto growing = [&](uint32_t &last) {
            tile_new_id_.assign(n, 0);
            uint32_t count = 0;
            for (uint32_t g = 0; g < n; ++g)
                if (tile_grown_[g] < tile_group_[g].size()) {
                    const uint32_t r = root(g);
                    if (!tile_new_id_[r]) {
                        tile_new_id_[r] = 1;
                        ++count;
                        last = r;
                    }
                }
            return count;
        };
        uint32_t last = 0;
        while (growing(last) > 1) {
            for (uint32_t g = 0; g < n; ++g) {
                if (tile_grown_[g] >= tile_group_[g].size()) continue;
                const uint32_t k = tile_group_[g][tile_grown_[g]++];
                const uint32_t i = k % w, j = k / w;
                auto visit = [&](uint32_t q) {
                    if (!open[q]) return;
                    const uint32_t m = tile_mark_[q];
                    if (m >= base && m < base + n) {
                        const uint32_t a = root(m - base), b = root(g);
                        if (a != b) tile_join_[std::max(a, b)] = std::min(a, b);
                        return;
                    }
                    tile_mark_[q] = base + g;
                    tile_group_[g].push_back(q);
                };
                if (i > 0) visit(k - 1);
                if (i + 1 < w) visit(k + 1);
                if (j > 0) visit(k - w);
                if (j + 1 < w) visit(k + w);
            }
        }
        const uint32_t keep = growing(last) == 1 ? root(last) : root(0);
        tile_new_id_.assign(n, 0);
        for (uint32_t g = 0; g < n; ++g) {
            const uint32_t r = root(g);
            if (r == keep) continue;
            if (!tile_new_id_[r]) tile_new_id_[r] = ++tile_region_next_[cls];
            for (const uint32_t q : tile_group_[g]) region[q] = tile_new_id_[r];
        }
        return true;
    }

    // The open node nearest the point (x, z) within max_r nodes of (i, j), its node, which it moves
    // to: the rule find_path_cells snaps an end on a closed navcell by, nearest the point itself
    // (FindNearestPassableNavcell for the start, MakeGoalReachable for the goal). Ring r holds no
    // node nearer than r - 0.5 node spacings; ties keep the scan order. False, with (i, j) left
    // alone, when none is open.
    template <class Open>
    bool snap_tile(int &i, int &j, float x, float z, int max_r, Open &&open) const {
        if (open(i, j)) return true;
        const float inv = 1.0f / node_step();
        const float fx = (x + map_half) * inv, fz = (z + map_half) * inv;
        float best = -1.0f;
        int bi = i, bj = j;
        for (int r = 1; r <= max_r; ++r) {
            const float ring = static_cast<float>(r) - 0.5f;
            if (best >= 0.0f && ring * ring >= best) break;
            for (int dj = -r; dj <= r; ++dj)
                for (int di = -r; di <= r; di += (dj == -r || dj == r || di == r) ? 1 : 2 * r) {
                    if (!open(i + di, j + dj)) continue;
                    const float ex = static_cast<float>(i + di) - fx, ez = static_cast<float>(j + dj) - fz;
                    const float d = ex * ex + ez * ez;
                    if (best < 0.0f || d < best) { best = d; bi = i + di; bj = j + dj; }
                }
        }
        if (best < 0.0f) return false;
        i = bi;
        j = bj;
        return true;
    }
    // Nodes in snap_tile's reach: 3 navcells, as find_path_cells snaps.
    int tile_snap_reach() const {
        return std::max(3, static_cast<int>(std::ceil(3.0f * static_cast<float>(node_res()) / static_cast<float>(std::max(res, 1)) - 1e-3f)));
    }

    // The region of class cls (update_tile_open) a unit at (x, z) walks in on the nodes: its node's,
    // or that of the open node it snaps to within max_r nodes (snap_tile). 0 when there is none, or
    // the nodes aren't kept for this terrain.
    uint32_t tile_region_at(float x, float z, int max_r, int cls) const {
        if (!tile_open_cached() || !tile_class_kept(cls)) return 0;
        const int nw = node_res();
        const std::vector<uint8_t> &open = tile_open_[cls];
        auto is_open = [&](int i, int j) {
            return i >= 0 && j >= 0 && i < nw && j < nw && open[static_cast<size_t>(j) * static_cast<size_t>(nw) + static_cast<size_t>(i)] != 0;
        };
        int i = node_of(x), j = node_of(z);
        if (!snap_tile(i, j, x, z, max_r, is_open)) return 0;
        return tile_region_[cls][static_cast<size_t>(j) * static_cast<size_t>(nw) + static_cast<size_t>(i)];
    }

    bool any_dirty() const {
        if (dirty.blank()) return false;
        for (uint8_t v : dirty.data)
            if (v) return true;
        return false;
    }

    // Chunk dirtiness — mirrors dirtinessGrid.any_set_in_square(i0,j0,i1,j1)
    // (HierarchicalPathfinder.cpp:477). A chunk is dirty iff any navcell inside it is set.
    bool chunk_is_dirty(int ci, int cj) const {
        if (dirty.blank()) return true;
        const int x0 = ci * CHUNK_SIZE;
        const int y0 = cj * CHUNK_SIZE;
        const int x1 = std::min(x0 + CHUNK_SIZE, res);
        const int y1 = std::min(y0 + CHUNK_SIZE, res);
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x)
                if (dirty.at(x, y)) return true;
        return false;
    }

    // Flood local regions inside ONE chunk only — factored out of
    // flood_chunk_regions() so dirty chunks can be re-flooded without
    // touching clean ones. Mirrors Chunk::InitRegions(ci, cj, grid, passClass)
    // (HierarchicalPathfinder.cpp:503).
    void flood_single_chunk(int ci, int cj, uint8_t pass, Grid<uint8_t> &local) {
        const int x0 = ci * CHUNK_SIZE;
        const int y0 = cj * CHUNK_SIZE;
        const int x1 = std::min(x0 + CHUNK_SIZE, res);
        const int y1 = std::min(y0 + CHUNK_SIZE, res);
        // Clear stale local labels in this chunk (region ids may be reused).
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x)
                local.at(x, y) = 0;
        uint8_t next_r = 1;
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                if (!is_passable_for(x, y, pass) || local.at(x, y) != 0) continue;
                std::vector<std::pair<int,int>> q;
                q.emplace_back(x, y);
                local.at(x, y) = next_r;
                for (size_t qi = 0; qi < q.size(); ++qi) {
                    int cx = q[qi].first, cy = q[qi].second;
                    static const int d[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
                    for (const auto &dir : d) {
                        int nx = cx + dir[0], ny = cy + dir[1];
                        if (nx < x0 || ny < y0 || nx >= x1 || ny >= y1) continue;
                        if (!is_passable_for(nx, ny, pass) || local.at(nx, ny) != 0) continue;
                        local.at(nx, ny) = next_r;
                        q.emplace_back(nx, ny);
                    }
                }
                ++next_r;
            }
        }
    }

    // Rebuild the global region map over current local labels (union-find +
    // ComputeNeighbors + UpdateGlobalRegions, HierarchicalPathfinder.cpp:523,
    // :575, :652). Like 0 A.D., the global flood runs at most once per update
    // even when only a few chunks were re-flooded (:467).
    // Global ids are numbered in the order of HierarchicalPathfinder::Recompute (:420-438):
    // chunks by row (cj, then ci), local regions in order, a new id at each region not yet
    // reached. So ids depend only on the regions, never on a hash map's iteration order.
    void recompute_global_regions(Grid<uint8_t> &local, GlobalRegionIds &global) {
        global.clear();
        if (cells.blank()) return;
        const int cpa = chunks_per_axis_;
        if (cpa <= 0) return;
        const size_t nchunks = static_cast<size_t>(cpa) * static_cast<size_t>(cpa);

        // Local regions per chunk: flood_single_chunk numbers them 1..n.
        global.first.assign(nchunks + 1, 0);
        for (int cj = 0; cj < cpa; ++cj)
            for (int ci = 0; ci < cpa; ++ci) {
                const int x0 = ci * CHUNK_SIZE, y0 = cj * CHUNK_SIZE;
                const int x1 = std::min(x0 + CHUNK_SIZE, res), y1 = std::min(y0 + CHUNK_SIZE, res);
                uint8_t n = 0;
                for (int y = y0; y < y1; ++y)
                    for (int x = x0; x < x1; ++x)
                        n = std::max(n, local.at(x, y));
                global.first[static_cast<size_t>(cj) * cpa + ci + 1] = n;
            }
        for (size_t c = 0; c < nchunks; ++c)
            global.first[c + 1] += global.first[c];
        const uint32_t nregions = global.first[nchunks];
        auto index = [&](int ci, int cj, uint8_t r) {
            return global.first[static_cast<size_t>(cj) * cpa + ci] + r - 1u;
        };

        std::vector<uint32_t> parent(nregions);
        for (uint32_t k = 0; k < nregions; ++k) parent[k] = k;
        auto findp = [&](uint32_t k) {
            uint32_t p = k;
            while (parent[p] != p) p = parent[p];
            while (parent[k] != p) {
                const uint32_t n = parent[k];
                parent[k] = p;
                k = n;
            }
            return p;
        };
        auto unite = [&](uint32_t a, uint32_t b) {
            a = findp(a);
            b = findp(b);
            if (a != b) parent[b] = a;
        };

        for (int cj = 0; cj < cpa; ++cj) {
            for (int ci = 0; ci < cpa - 1; ++ci) {
                int x = (ci + 1) * CHUNK_SIZE - 1;
                int x2 = x + 1;
                if (x2 >= res) continue;
                for (int y = cj * CHUNK_SIZE; y < (cj + 1) * CHUNK_SIZE && y < res; ++y) {
                    uint8_t r1 = local.at(x, y), r2 = local.at(x2, y);
                    if (r1 && r2) unite(index(ci, cj, r1), index(ci + 1, cj, r2));
                }
            }
        }
        for (int ci = 0; ci < cpa; ++ci) {
            for (int cj = 0; cj < cpa - 1; ++cj) {
                int y = (cj + 1) * CHUNK_SIZE - 1;
                int y2 = y + 1;
                if (y2 >= res) continue;
                for (int x = ci * CHUNK_SIZE; x < (ci + 1) * CHUNK_SIZE && x < res; ++x) {
                    uint8_t r1 = local.at(x, y), r2 = local.at(x, y2);
                    if (r1 && r2) unite(index(ci, cj, r1), index(ci, cj + 1, r2));
                }
            }
        }

        global.id.assign(nregions, 0);
        std::vector<uint16_t> root_id(nregions, 0);
        uint16_t next_g = 1;
        for (uint32_t k = 0; k < nregions; ++k) {
            const uint32_t root = findp(k);
            if (!root_id[root]) root_id[root] = next_g++;
            global.id[k] = root_id[root];
        }
    }

    // HierarchicalPathfinder::Update — re-flood only dirty chunks, then
    // rebuild the global map once (HierarchicalPathfinder.cpp:440-520).
    // Returns the number of chunk floods performed (0 when clean).
    int rebuild_dirty_chunks(uint8_t pass, Grid<uint8_t> &local, GlobalRegionIds &global) {
        int floods = 0;
        for (int cj = 0; cj < chunks_per_axis_; ++cj)
            for (int ci = 0; ci < chunks_per_axis_; ++ci) {
                if (!chunk_is_dirty(ci, cj)) continue;
                flood_single_chunk(ci, cj, pass, local);
                ++floods;
            }
        recompute_global_regions(local, global);
        return floods;
    }

    // HierarchicalPathfinder.cpp:480 — only recompute when dirtinessGrid is set.
    // Per-chunk: re-floods dirty chunks for both passes plus one global
    // rebuild each — never a full 64-chunk remesh for a single building.
    void rebuild_dirty_regions() {
        if (!regions_ready_) {
            rebuild_regions();
            return;
        }
        update_tile_open(false);
        if (!any_dirty()) return;
        rebuild_dirty_chunks(PASS_INFANTRY, chunk_regions_, global_region_map_);
        rebuild_dirty_chunks(PASS_SHIP, ship_chunk_regions_, ship_region_map_);
        regions_ready_ = true;
        if (!dirty.blank()) dirty.fill(0);
    }


    uint16_t global_region_at(int cx, int cy, uint8_t pass = PASS_INFANTRY) const {
        const Grid<uint8_t> *local = &chunk_regions_;
        const auto *gmap = &global_region_map_;
        if (pass == PASS_SHIP || (pass & PASS_SHIP)) {
            local = &ship_chunk_regions_;
            gmap = &ship_region_map_;
        }
        if (local->blank() || !local->inside(cx, cy)) return 0;
        uint8_t lr = local->at(cx, cy);
        if (!lr) return 0;
        const int ci = cx / CHUNK_SIZE, cj = cy / CHUNK_SIZE;
        if (ci >= chunks_per_axis_ || cj >= chunks_per_axis_) return 0;
        return gmap->get(static_cast<size_t>(cj) * chunks_per_axis_ + ci, lr);
    }

    uint16_t global_region_ship_at(int cx, int cy) const {
        return global_region_at(cx, cy, PASS_SHIP);
    }

    bool reachable(int sx, int sy, int tx, int ty, uint8_t pass = PASS_INFANTRY) const {
        if (!regions_ready_) return true;
        uint16_t a = global_region_at(sx, sy, pass);
        uint16_t b = global_region_at(tx, ty, pass);
        return a != 0 && a == b;
    }

    // Bilinear height sample matching Simulation::get_terrain_height.
    float sample_height_bilinear(float wx, float wz, const std::vector<float>& heights, int h_res) const {
        if (heights.empty() || h_res <= 0) return 0.0f;
        float fx = (wx + map_half) / (map_half * 2.0f) * static_cast<float>(h_res - 1);
        float fz = (wz + map_half) / (map_half * 2.0f) * static_cast<float>(h_res - 1);
        fx = std::clamp(fx, 0.0f, static_cast<float>(h_res - 1));
        fz = std::clamp(fz, 0.0f, static_cast<float>(h_res - 1));
        int x0 = floor_index(fx);
        int z0 = floor_index(fz);
        int x1 = std::min(x0 + 1, h_res - 1);
        int z1 = std::min(z0 + 1, h_res - 1);
        float tx = fx - static_cast<float>(x0);
        float tz = fz - static_cast<float>(z0);
        float h00 = heights[static_cast<size_t>(z0) * static_cast<size_t>(h_res) + static_cast<size_t>(x0)];
        float h10 = heights[static_cast<size_t>(z0) * static_cast<size_t>(h_res) + static_cast<size_t>(x1)];
        float h01 = heights[static_cast<size_t>(z1) * static_cast<size_t>(h_res) + static_cast<size_t>(x0)];
        float h11 = heights[static_cast<size_t>(z1) * static_cast<size_t>(h_res) + static_cast<size_t>(x1)];
        float h0 = h00 * (1.0f - tx) + h10 * tx;
        float h1 = h01 * (1.0f - tx) + h11 * tx;
        return h0 * (1.0f - tz) + h1 * tz;
    }

    // Computes fractional slope (rise / run) across navcell (cx, cy) using corner height delta.
    // Mirrors 0 A.D. CTerrain::GetSlopeFixed (source/graphics/Terrain.cpp:306-322).
    float compute_cell_slope(int cx, int cy, const std::vector<float>& heights, int h_res) const {
        if (heights.empty() || h_res <= 0 || res <= 0) return 0.0f;
        const float cell_w = (map_half * 2.0f) / static_cast<float>(res);
        float min_wx = (static_cast<float>(cx) / static_cast<float>(res)) * (map_half * 2.0f) - map_half;
        float max_wx = (static_cast<float>(cx + 1) / static_cast<float>(res)) * (map_half * 2.0f) - map_half;
        float min_wz = (static_cast<float>(cy) / static_cast<float>(res)) * (map_half * 2.0f) - map_half;
        float max_wz = (static_cast<float>(cy + 1) / static_cast<float>(res)) * (map_half * 2.0f) - map_half;

        float h00 = sample_height_bilinear(min_wx, min_wz, heights, h_res);
        float h10 = sample_height_bilinear(max_wx, min_wz, heights, h_res);
        float h01 = sample_height_bilinear(min_wx, max_wz, heights, h_res);
        float h11 = sample_height_bilinear(max_wx, max_wz, heights, h_res);

        float max_h = std::max(std::max(h00, h10), std::max(h01, h11));
        float min_h = std::min(std::min(h00, h10), std::min(h01, h11));
        return (cell_w > 0.0f) ? ((max_h - min_h) / cell_w) : 0.0f;
    }

    // Heightmap sampling results per navcell, kept for the next call. Terrain doesn't change during
    // a match, but Simulation::rebuild_nav_grid calls set_water_from_heights at every building
    // completion, and on a 512 map the sampling was most of that rebuild. The cache holds a copy
    // of the heightmap and the settings the results came from; any bitwise difference recomputes.
    struct HeightSampleCache {
        std::vector<float> heights;
        int h_res = -1, res = -1;
        float map_half = 0.0f;
        std::vector<uint8_t> steep; // compute_cell_slope() > steep_max_slope
        float steep_max_slope = 0.0f;
        bool steep_valid = false;
        std::vector<uint8_t> water; // NAV_WATER, NAV_SHALLOW or NAV_PASSABLE by centre height
        float water_thresh = 0.0f;
        bool water_valid = false;
        std::vector<uint8_t> tiles; // per heightmap tile, (h_res - 1) a side: steeper than tiles_max_slope
        std::vector<uint32_t> tiles_sum; // (tiles_w + 1) a side: steep tiles below and left of each corner
        int tiles_w = 0;
        float tiles_max_slope = 0.0f;
        bool any_tile = false;
        bool tiles_valid = false;
    };
    HeightSampleCache height_cache_;

    static bool same_bits(float a, float b) { return std::memcmp(&a, &b, sizeof a) == 0; }

    // Drops the cached results unless they came from this heightmap, h_res, res and map_half.
    void sync_height_cache(const std::vector<float>& heights, int h_res) {
        HeightSampleCache& c = height_cache_;
        if (c.h_res == h_res && c.res == res && same_bits(c.map_half, map_half) &&
            c.heights.size() == heights.size() &&
            std::memcmp(c.heights.data(), heights.data(), heights.size() * sizeof(float)) == 0)
            return;
        c.heights = heights;
        c.h_res = h_res;
        c.res = res;
        c.map_half = map_half;
        c.steep_valid = false;
        c.water_valid = false;
        c.tiles_valid = false;
        ++terrain_version_;
    }

    // Computes slopes for all navcells from heightmap, flagging cells steeper than max_slope.
    void compute_slopes_from_heights(const std::vector<float>& heights, int h_res) {
        if (heights.empty() || h_res <= 0 || res <= 0) return;
        if (steep_cells_.w != res || steep_cells_.h != res) {
            steep_cells_.resize(res, res, 0);
        }
        sync_height_cache(heights, h_res);
        HeightSampleCache& c = height_cache_;
        if (!c.steep_valid || !same_bits(c.steep_max_slope, max_slope)) {
            c.steep.resize(static_cast<size_t>(res) * static_cast<size_t>(res));
            for (int y = 0; y < res; ++y)
                for (int x = 0; x < res; ++x)
                    c.steep[static_cast<size_t>(y) * res + x] =
                        (compute_cell_slope(x, y, heights, h_res) > max_slope) ? 1 : 0;
            c.steep_max_slope = max_slope;
            c.steep_valid = true;
        }
        // 0.28's own granularity: CCmpPathfinder gives each 1 m navcell the slope of its 4 m terrain
        // tile ("exact slopes give kind of weird output"), GetSlopeFixed's highest corner less its
        // lowest over the tile's size. A heightmap tile is a 0.28 tile, half a navcell a side, so a
        // steep tile marks the cliff itself where a steep navcell may span the ground at its foot too.
        // A heightmap coarser than the navcells (a ported map's heights, a sample every 4 units or
        // more) has no finer slope to give: it keeps no tiles and the navcells' slopes serve alone.
        if (!c.tiles_valid || !same_bits(c.tiles_max_slope, max_slope)) {
            const int tw = h_res - 1 >= res ? h_res - 1 : 0;
            const float tile = (map_half * 2.0f) / static_cast<float>(std::max(tw, 1));
            c.tiles.assign(static_cast<size_t>(std::max(tw, 0)) * static_cast<size_t>(std::max(tw, 0)), 0);
            c.any_tile = false;
            for (int j = 0; j < tw; ++j)
                for (int i = 0; i < tw; ++i) {
                    const size_t v = static_cast<size_t>(j) * static_cast<size_t>(h_res) + static_cast<size_t>(i);
                    const float a = heights[v], b = heights[v + 1];
                    const float d = heights[v + static_cast<size_t>(h_res)], e = heights[v + static_cast<size_t>(h_res) + 1];
                    const float rise = std::max(std::max(a, b), std::max(d, e)) - std::min(std::min(a, b), std::min(d, e));
                    if (rise > max_slope * tile) {
                        c.tiles[static_cast<size_t>(j) * static_cast<size_t>(tw) + static_cast<size_t>(i)] = 1;
                        c.any_tile = true;
                    }
                }
            c.tiles_w = std::max(tw, 0);
            // Summed-area table, so steep_tile_in answers any box in four reads.
            const size_t sw = static_cast<size_t>(c.tiles_w) + 1;
            c.tiles_sum.assign(sw * sw, 0);
            for (int j = 0; j < c.tiles_w; ++j) {
                uint32_t row = 0;
                for (int i = 0; i < c.tiles_w; ++i) {
                    row += c.tiles[static_cast<size_t>(j) * static_cast<size_t>(c.tiles_w) + static_cast<size_t>(i)];
                    c.tiles_sum[(static_cast<size_t>(j) + 1) * sw + static_cast<size_t>(i) + 1] =
                            c.tiles_sum[static_cast<size_t>(j) * sw + static_cast<size_t>(i) + 1] + row;
                }
            }
            c.tiles_max_slope = max_slope;
            c.tiles_valid = true;
            ++terrain_version_;
        }
        tiles_ready_ = true;
        bool any = false;
        for (int y = 0; y < res; ++y) {
            for (int x = 0; x < res; ++x) {
                uint8_t steep = c.steep[static_cast<size_t>(y) * res + x];
                any |= steep != 0;
                if (steep_cells_.at(x, y) != steep) {
                    steep_cells_.at(x, y) = steep;
                    if (!dirty.blank()) dirty.at(x, y) = 1;
                }
            }
        }
        any_steep_ = any;
    }

    void set_slopes_from_heights(const std::vector<float>& heights, int h_res) {
        compute_slopes_from_heights(heights, h_res);
    }

    void set_steep(int cx, int cy, bool steep) {
        if (steep_cells_.blank() || !steep_cells_.inside(cx, cy)) return;
        uint8_t v = steep ? 1 : 0;
        if (steep) any_steep_ = true;
        if (steep_cells_.at(cx, cy) != v) {
            steep_cells_.at(cx, cy) = v;
            ++terrain_version_;
            if (!dirty.blank()) dirty.at(cx, cy) = 1;
        }
    }

    // Whether any navcell is steep: no slope to check on a flat map.
    bool any_steep() const { return any_steep_; }

    // Changes whenever the slopes may have (a new heightmap, map size or grid, or a navcell set
    // steep): a result worked out from the steep tiles and navcells holds while this stays the same.
    uint32_t terrain_version() const { return terrain_version_; }

    // Whether the terrain has a tile steeper than max_slope; steep_tile_res() tiles a side.
    bool any_steep_tile() const { return tiles_ready_ && height_cache_.any_tile; }
    int steep_tile_res() const { return tiles_ready_ ? height_cache_.tiles_w : 0; }
    // Whether find_path plans a land path near steep ground again on the tiles' nodes
    // (find_path_tiles): the terrain has a steep tile and its tiles are finer than the navcells (a
    // 512 map's are as wide: its navcells already take each tile's slope).
    bool tile_paths_on() const { return any_steep_tile() && steep_tile_res() > res; }
    bool tile_steep(int i, int j) const {
        const HeightSampleCache& c = height_cache_;
        return i >= 0 && j >= 0 && i < c.tiles_w && j < c.tiles_w &&
               c.tiles[static_cast<size_t>(j) * static_cast<size_t>(c.tiles_w) + static_cast<size_t>(i)] != 0;
    }
    // A tile steeper than max_slope within c of (x, z) each way. CCmpPathfinder makes every navcell
    // of such a tile impassable to land classes and ExpandImpassableCells grows that by the class
    // Clearance, in squares.
    bool near_steep_tile(float x, float z, float c) const {
        return steep_tile_in(x - c, z - c, x + c, z + c);
    }
    // A tile steeper than max_slope touching the box [x0, x1] x [z0, z1].
    bool steep_tile_in(float x0, float z0, float x1, float z1) const {
        if (!any_steep_tile()) return false;
        const HeightSampleCache& h = height_cache_;
        const int tw = h.tiles_w;
        const float inv = static_cast<float>(tw) / (map_half * 2.0f);
        auto tile = [&](float v) { return std::clamp(floor_index((v + map_half) * inv), 0, tw - 1); };
        const size_t sw = static_cast<size_t>(tw) + 1;
        const size_t i0 = static_cast<size_t>(tile(x0)), i1 = static_cast<size_t>(tile(x1)) + 1;
        const size_t j0 = static_cast<size_t>(tile(z0)) * sw, j1 = (static_cast<size_t>(tile(z1)) + 1) * sw;
        const uint32_t* t = h.tiles_sum.data();
        return t[j1 + i1] - t[j0 + i1] - t[j1 + i0] + t[j0 + i0] != 0;
    }

    bool is_steep(int cx, int cy) const {
        if (steep_cells_.blank() || !steep_cells_.inside(cx, cy)) return false;
        return steep_cells_.at(cx, cy) != 0;
    }


    // Tag water navcells and compute slopes from heightmap.
    // heights is res x res row-major with [-map_half, +map_half] extent.
    // Slopes exceeding max_slope are marked in steep_cells_ (impassable on land).
    // Any navcell whose bilinear-sampled height < water_h+0.3 becomes water (and dirty=1):
    // NAV_SHALLOW within LAND_MAX_WATER_DEPTH of that line, NAV_WATER below it.
    void set_water_from_heights(const std::vector<float>& heights, int res, float water_h) {
        if (heights.empty() || res <= 0) return;
        if (cells.blank()) return;

        // Compute slope steepness for land units (independent of water table)
        compute_slopes_from_heights(heights, res);

        // If water_h is sentinel (< -0.5 e.g. -1.0 in Simulation::terrain_water_height_),
        // treat as "no water" — leave grid untouched (matches rms_generator behaviour).
        if (water_h < -0.5f) return;

        const float thresh = water_h + NAV_WATER_MARGIN;
        const float deep = thresh - LAND_MAX_WATER_DEPTH;
        sync_height_cache(heights, res);
        HeightSampleCache& c = height_cache_;
        if (!c.water_valid || !same_bits(c.water_thresh, thresh)) {
            c.water.resize(static_cast<size_t>(this->res) * static_cast<size_t>(this->res));
            for (int y = 0; y < this->res; ++y) {
                for (int x = 0; x < this->res; ++x) {
                    // Navcell center in world
                    float wx = ((static_cast<float>(x) + 0.5f) / static_cast<float>(this->res)) * (map_half * 2.0f) - map_half;
                    float wz = ((static_cast<float>(y) + 0.5f) / static_cast<float>(this->res)) * (map_half * 2.0f) - map_half;
                    const float h = sample_height_bilinear(wx, wz, heights, res);
                    c.water[static_cast<size_t>(y) * this->res + x] =
                        h < deep ? NAV_WATER : (h < thresh ? NAV_SHALLOW : NAV_PASSABLE);
                }
            }
            c.water_thresh = thresh;
            c.water_valid = true;
            ++water_class_gen;
        }
        for (int y = 0; y < this->res; ++y) {
            for (int x = 0; x < this->res; ++x) {
                const uint8_t w = c.water[static_cast<size_t>(y) * this->res + x];
                if (w != NAV_PASSABLE) {
                    if (cells.at(x, y) != w) {
                        cells.at(x, y) = w;
                        dirty.at(x, y) = 1;
                    }
                } else {
                    // Only clear water tag if it was previously water; don't overwrite buildings
                    if (cells.at(x, y) == NAV_WATER || cells.at(x, y) == NAV_SHALLOW) {
                        cells.at(x, y) = NAV_PASSABLE;
                        dirty.at(x, y) = 1;
                    }
                }
            }
        }
    }

    // Long-range path on the NavGrid: snap, MakeGoalReachable, then JPS with octile 10/14 costs.
    // A warship (PASS_WARSHIP) whose path can't get through on its class (a channel 0 A.D. warships
    // use can be two 8 m navcells wide, which the clearance closes) takes the PASS_SHIP path.
    // A "large" path that finds no way takes the default class's, as woods its 8 m navcells close
    // whole would shut it off (PASS_LARGE), but not one planned round a gap too narrow for the class
    // (find_path_on's `narrow_gap`), which keeps the nodes' own end.
    // navcells_only: the long path on the navcells alone, with no tree re-plan (watch_tree_leg's fallback). The plan
    // records it in out.tree_off, so the walk's obstructed re-plans (obstructed_long_path) keep the same choice.
    bool find_path(float sx, float sz, float tx, float tz, WaypointPath &out, uint8_t pass = PASS_INFANTRY,
            bool navcells_only = false) const;
    bool find_path_planned(float sx, float sz, float tx, float tz, WaypointPath &out, uint8_t pass, bool navcells_only) const;
    // `narrow_gap`, when given, is set when the path returned was planned on the nodes round a gap
    // between two structures too narrow for the "large" class (path_through_narrow_gap).
    bool find_path_on(float sx, float sz, float tx, float tz, WaypointPath &out, uint8_t pass, bool *narrow_gap = nullptr,
            bool navcells_only = false) const;
    // find_path_on's search on the navcells alone, before its pass on the terrain tiles. It leaves
    // (tx, tz) where MakeGoalReachable moved the goal (ashore, into the start's region).
    bool find_path_cells(float sx, float sz, float &tx, float &tz, WaypointPath &out, uint8_t pass) const;
    // True for a point goal find_path_cells moves onto a passable navcell (the G163 end move).
    bool point_goal_snaps(float tx, float tz, uint8_t pass) const;

    // 0.28's long pathfinder plans on the navcells units move on (CCmpPathfinder::TerrainUpdateHelper),
    // where every terrain tile steeper than MaxTerrainSlope is impassable and grown by the class
    // Clearance (ExpandImpassableCells). RTS0's navcells are two terrain tiles a side, steep only where
    // their own averaged slope is, so a path on them can lead a land unit across a cliff narrower than
    // a navcell, whose tiles its steps keep off (Simulation::land_step_barred): it walks up to the
    // cliff's margin and stays there. A land path that comes within its class's clearance of a steep
    // tile is therefore planned again on nodes half a tile apart (find_path_tiles), as is one that
    // ends short of a goal the navcells close off (steep by their averaged slope) but the nodes'
    // regions join to the start, and one that passes through a static obstruction standing so near
    // a steep tile that no unit of the class fits between them: the navcells, closed only where an
    // obstruction covers them whole, leave the way round it to the steering, which the steep ground
    // may shut. The nodes keep each class's clearance off steep tiles, deep water, the map's edge
    // and the static obstructions (update_tile_open, tile_obs_raster).
    // Whether the walk from (sx, sz) along the rest of `path` comes within `c` of a steep tile.
    bool path_near_steep_tile(float sx, float sz, const WaypointPath &path, float c) const;
    // Whether the walk from (sx, sz) along the rest of `path` passes inside a static obstruction,
    // grown by the clearance of tile class cls, that stands within twice that clearance of a steep
    // tile.
    bool path_through_obstruction_near_steep(float sx, float sz, const WaypointPath &path, int cls) const;
    // Whether the walk from (sx, sz) along the rest of `path` passes inside a structure (the
    // TILE_OBS_STRUCTURES set) grown by the "large" clearance that stands within twice that clearance
    // of another structure: a gap a unit of the class can't get through, as each one's square grown by
    // the clearance takes in the whole gap between them (TestRaySquare, CCmpObstructionManager.cpp:
    // 918-932). The pass on the nodes then plans the path round them.
    bool path_through_narrow_gap(float sx, float sz, const WaypointPath &path) const;
    // Whether rectangle a, grown by `grow` on every side, and rectangle b overlap (touching does not
    // count): the separating axis test over both rectangles' axes (u = (1, 0) when uz is 0).
    static bool tile_obs_overlap(const TileObstruction &a, float grow, const TileObstruction &b) {
        const float ahu = a.hu + grow, ahv = a.hv + grow, dx = b.x - a.x, dz = b.z - a.z;
        const float axes[4][2] = {{a.ux, a.uz}, {-a.uz, a.ux}, {b.ux, b.uz}, {-b.uz, b.ux}};
        for (const auto &ax : axes) {
            const float ra = ahu * std::fabs(ax[0] * a.ux + ax[1] * a.uz) + ahv * std::fabs(ax[1] * a.ux - ax[0] * a.uz);
            const float rb = b.hu * std::fabs(ax[0] * b.ux + ax[1] * b.uz) + b.hv * std::fabs(ax[1] * b.ux - ax[0] * b.uz);
            if (std::fabs(dx * ax[0] + dz * ax[1]) >= ra + rb) return false;
        }
        return true;
    }
    // The land path of tile class cls (tile_class) on the nodes: JPS (nav/long_pathfinder.h) from the
    // node nearest the start a unit can stand on, to the goal's node or, when a unit can't stand there
    // or get there, the nearest one of the start's region (HierarchicalPathfinder::MakeGoalReachable),
    // its waypoints thinned from the goal back where the line between their neighbours keeps clear
    // (LongPathfinder::ImprovePathWaypoints, CheckLineMovement: tile_line_clear). False, with `out`
    // untouched, when there are no tiles finer than the navcells or no open node near the start (or,
    // without the regions, the goal).
    bool find_path_tiles(float sx, float sz, float tx, float tz, WaypointPath &out, bool &reached, int cls) const;
    // Whether a unit of class cls walking straight from (ax, az) to (bx, bz) keeps its clearance off
    // the steep tiles, deep water and static obstructions (the map's edge is convex: a line between
    // two points inside it stays inside).
    bool tile_line_clear(float ax, float az, float bx, float bz, int cls, bool live) const;
    // Whether the segment from (ax, az) to (bx, bz) comes within r of a steep tile, tested every
    // quarter tile along it; the start doesn't count. nav::detail::segment_near_steep (short paths)
    // is this test.
    bool segment_near_steep_tile(float ax, float az, float bx, float bz, float r) const;
    // Half of segment_near_steep_tile's sample spacing: every point of a segment lies within it of a
    // sample, so a radius grown by it catches a segment that clips a steep tile between two samples.
    // Without the heightmap's tiles (the large class's nodes on the map's tiles: node_tiles), the
    // samples are the nodes' tiles'.
    int sample_tiles() const { return steep_tile_res() > 0 ? steep_tile_res() : std::max(node_tiles(), 1); }
    float steep_sample_pad() const { return 0.25f * map_half / static_cast<float>(sample_tiles()); }
    // segment_near_steep_tile's samples, with the box test `hit(x0, z0, x1, z1)` in place of the steep
    // tiles'.
    template <class Hit>
    bool segment_box_hits(float ax, float az, float bx, float bz, float r, Hit &&hit) const;

    // Cell-level search behind find_path. Fills start..goal cells in walking
    // order and returns whether the goal was reached.
    bool search_cells_jps(int scx, int scy, int tcx, int tcy, uint8_t pass, std::vector<std::pair<int,int>> &path_cells) const;
};

// ---------------------------------------------------------------------------
// WaypointPath — SoA-friendly fixed-cap waypoint list.
//
// Mirrors src/0ad/simulation2/helpers/Pathfinding.h:69 WaypointPath (vector<Waypoint>)
// but stored as SoA-friendly fixed arrays (no heap per unit, cache-line friendly).
// MAX_WP=64 matches 0AD's pragmatic long-path waypoint cap; LongPathfinder JPS paths
// rarely exceed 32 waypoints on 64x64 navgrid. For longer maps, path will be truncated.
//
// Usage: Simulation::find_path() fills xs/zs + count; move_units() advances cur when
// unit enters ARRIVE_DIST of current waypoint (mirrors CCmpUnitMotion path following).
//
// Cost model note: 0AD PathCost octile = hv*10 + diag*14approx (actually hv*65536 + diag*92682
// fixed). JPS (nav/long_pathfinder.h) uses orth 10, diag 14 and the exact octile heuristic.
// See LongPathfinder.cpp:400-406 and Pathfinding.h:85.
// ---------------------------------------------------------------------------
struct WaypointPath {
    static constexpr int MAX_WP = 64;
    float xs[MAX_WP] = {};
    float zs[MAX_WP] = {};
    int count = 0;
    int cur = 0;
    // Target the path was requested for, when the caller tracks it (move_units does for
    // GATHER/BUILD/RETURN_DROPSITE paths, to know when the target moved away).
    bool has_goal = false;
    float goal_x = 0.f;
    float goal_z = 0.f;
    // C1 tree watch (Simulation::watch_tree_leg). These fields come after the 532 bytes tick_state_io saves: a loaded
    // walk starts unwatched and tree-aware (a documented reset rule). clear() resets them, so a new path starts fresh.
    bool tree_replanned = false; // avoid_tree_subcells re-planned this path (a leg a tree's subcell blocked)
    bool tree_off = false;       // planned on the navcells alone (find_path's navcells_only): obstructed re-plans keep so
    int watch_cur = -1;          // the waypoint the watch counts for
    float watch_best = 0.0f;     // the least distance to it so far, world units
    int watch_turns = 0;         // turns since that distance last fell by 1 m

    void clear() {
        count = 0; cur = 0; has_goal = false;
        tree_replanned = false; tree_off = false; watch_cur = -1; watch_best = 0.0f; watch_turns = 0;
    }

    bool done() const { return cur >= count; }

    std::pair<float,float> current() const {
        if (cur < 0 || cur >= count) return {0.f, 0.f};
        return {xs[cur], zs[cur]};
    }

    void advance() { if (cur < count) ++cur; }

    // Push helper for pathfinder (clamped to MAX_WP)
    bool push_back(float x, float z) {
        if (count >= MAX_WP) return false;
        xs[count] = x;
        zs[count] = z;
        ++count;
        return true;
    }
};

// ---------------------------------------------------------------------------
// NavGrid::find_path — snap, MakeGoalReachable, JPS, string pull
// ---------------------------------------------------------------------------
inline bool NavGrid::crosses_mine_sub(int cls, float sx, float sz, const WaypointPath &out) const {
    if (mine_sub[cls].empty() || out.count == 0) return false;
    const auto start = world_to_cell(sx, sz);
    const auto goal = world_to_cell(out.xs[out.count - 1], out.zs[out.count - 1]);
    // Subcell coordinates: 8 per navcell, so one subcell is 1 m (0.25 sim units at 8 m navcells).
    const float k = static_cast<float>(res) * 8.0f / (map_half * 2.0f);
    float px = sx, pz = sz;
    for (int leg = 0; leg < out.count; ++leg) {
        const float x1 = out.xs[leg], z1 = out.zs[leg];
        bool hit = false;
        // On the first leg a unit pushed into a blocked subcell walks out of it: blocked subcells count
        // only once the leg has reached an open one (0 A.D.'s units leave an obstruction the same way).
        bool left = leg > 0;
        walk_grid(res * 8, (px + map_half) * k, (pz + map_half) * k, (x1 + map_half) * k, (z1 + map_half) * k,
                [&](int x, int y) {
                    const int cx = x / 8, cy = y / 8;
                    if ((cx == start.first && cy == start.second) || (cx == goal.first && cy == goal.second)) return true;
                    if (!mine_subcell_blocked(cls, x, y)) left = true;
                    else if (left) hit = true;
                    return !hit;
                });
        if (hit) return true;
        px = x1;
        pz = z1;
    }
    return false;
}

inline int NavGrid::avoid_mine_subcells(float sx, float sz, float &gx, float &gz, WaypointPath &out, uint8_t pass) const {
    const int cls = tile_class(pass);
    if (!mine_any() || (pass & PASS_SHIP) || !crosses_mine_sub(cls, sx, sz, out)) return 0;
    WaypointPath closed;
    float cgx = gx, cgz = gz;
    // A closed search that finds no route, or whose route still crosses a mine, is no route: 0 A.D.'s
    // 1 m obstruction would stop the unit there, and steering would walk it through the square.
    if (!find_path_cells(sx, sz, cgx, cgz, closed, static_cast<uint8_t>(pass | PASS_MINE_CLOSED))) return 2;
    if (crosses_mine_sub(cls, sx, sz, closed)) return 2;
    out = closed;
    gx = cgx;
    gz = cgz;
    return 1;
}

// C1: crosses_mine_sub on the trees' masks, the same walk and exemptions: the subcells of a leg's start and goal navcells
// do not count as a crossing. The path is one walk across all its legs: a unit on blocked ground walks out of it (as
// crosses_mine_sub's comment says), and once the walk has stood on open ground, entering blocked ground is a crossing.
// The state carries over from leg to leg; a leg that starts on blocked ground (a waypoint the search left inside a
// tree's clearance) must not count its own first subcell as open ground.
inline bool NavGrid::crosses_tree_sub(int cls, float sx, float sz, const WaypointPath &out) const {
    if (tree_sub[cls].empty() || out.count == 0) return false;
    const auto start = world_to_cell(sx, sz);
    const auto goal = world_to_cell(out.xs[out.count - 1], out.zs[out.count - 1]);
    const float k = static_cast<float>(res) * 8.0f / (map_half * 2.0f);
    float px = sx, pz = sz;
    bool left = false; // the walk has stood on open ground
    for (int leg = 0; leg < out.count; ++leg) {
        const float x1 = out.xs[leg], z1 = out.zs[leg];
        bool hit = false;
        walk_grid(res * 8, (px + map_half) * k, (pz + map_half) * k, (x1 + map_half) * k, (z1 + map_half) * k,
                [&](int x, int y) {
                    const int cx = x / 8, cy = y / 8;
                    const bool exempt = (cx == start.first && cy == start.second) || (cx == goal.first && cy == goal.second);
                    if (!tree_subcell_blocked(cls, x, y)) left = true;
                    else if (left && !exempt) hit = true;
                    return !hit;
                });
        if (hit) return true;
        px = x1;
        pz = z1;
    }
    return false;
}

// G171: one leg against the trees' squares grown by the class clearance (tile_path_clearance, as
// tile_line_clear tests them), not the subcells' clearance plus 1 m. The leg's blocked intervals
// (one per tree square it enters) are merged: a leg that starts open and enters one crosses; one
// that starts inside one and leaves it (a unit standing within about a metre of a palm) crosses only
// if it re-enters blocked ground after it has stood on open ground, as crosses_tree_sub allows.
// Only the GAIA set is tested (TileObsKind K_FLORA/K_TREASURE, simulation.cpp: key bits 56+), so a
// leg past a house or along a shore is not this row's case.
inline bool NavGrid::leg_crosses_gaia_clearance(float ax, float az, float bx, float bz, int cls) const {
    const float g = tile_clearance(cls) + TILE_HAIR;
    thread_local std::vector<std::pair<float, float>> iv, merged;
    iv.clear();
    merged.clear();
    any_tile_obs_near_segment(ax, az, bx, bz, g, [&](const TileObstruction &o_in) {
        const uint64_t kind = o_in.key >> 56;
        if (kind != 5 && kind != 6) return false; // K_FLORA, K_TREASURE only
        const TileObstruction o = cls == 1 ? tile_obs_crowd_grown(o_in) : o_in;
        const float hu = o.hu + g, hv = o.hv + g;
        const float pax = ax - o.x, paz = az - o.z, pbx = bx - o.x, pbz = bz - o.z;
        const float ua = o.uz == 0.0f ? pax : pax * o.ux + paz * o.uz, va = o.uz == 0.0f ? paz : paz * o.ux - pax * o.uz;
        const float ub = o.uz == 0.0f ? pbx : pbx * o.ux + pbz * o.uz, vb = o.uz == 0.0f ? pbz : pbz * o.ux - pbx * o.uz;
        float t0 = 0.0f, t1 = 1.0f;
        auto slab = [&](float p, float q, float h) {
            const float d = q - p;
            if (std::fabs(d) < 1e-9f) return std::fabs(p) < h;
            float s0 = (-h - p) / d, s1 = (h - p) / d;
            if (s0 > s1) std::swap(s0, s1);
            t0 = std::max(t0, s0);
            t1 = std::min(t1, s1);
            return t0 < t1;
        };
        if (!slab(ua, ub, hu) || !slab(va, vb, hv)) return false;
        iv.emplace_back(t0, t1);
        return false;
    });
    if (iv.empty()) return false;
    std::sort(iv.begin(), iv.end());
    merged.push_back(iv[0]);
    for (size_t k = 1; k < iv.size(); ++k) {
        if (iv[k].first > merged.back().second + 1e-6f) merged.push_back(iv[k]);
        else merged.back().second = std::max(merged.back().second, iv[k].second);
    }
    if (merged[0].first > 1e-6f) return true; // starts on open ground, enters a tree's clearance
    return merged.size() > 1; // starts inside one, leaves it, then enters blocked ground again
}

inline bool NavGrid::tile_path_crosses_tree_clearance(float sx, float sz, const WaypointPath &path, int cls) const {
    if (path.count == 0) return false;
    const int cur = std::max(path.cur, 0);
    // Only the legs the thinning never tested: the first from the unit to its first node, the last
    // into the goal, and every leg of a small path (at most two jump points, kept count <= 2 plus
    // the start leg). A middle leg of a longer path is either a shortcut the thinning tested with
    // tile_line_clear at the same clearance, or a jump segment between open nodes, and re-testing
    // them all would revert whole paths that merely pass a tree at the clearance, as crosses_tree_sub
    // on the whole tile path did (G171's held candidate). A collinear shortcut in the middle is the
    // same line as its jump segments, so it does not enter a clearance they clear.
    auto leg = [&](int k) {
        const float px = k == cur ? sx : path.xs[k - 1];
        const float pz = k == cur ? sz : path.zs[k - 1];
        return leg_crosses_gaia_clearance(px, pz, path.xs[k], path.zs[k], cls);
    };
    if (leg(cur)) return true;
    if (path.count - cur <= 2) return leg(path.count - 1) || (path.count - cur == 2 && leg(cur + 1));
    return leg(path.count - 1);
}

// C1 (0 A.D. paths its units on 1 m obstruction navcells; RTS0 on 8 m ones, with the trees' squares on the 1 m ones
// only here). A path whose legs cross a tree's subcell is planned again on the 1 m grid, and the navcell path is kept
// when it crosses none (so routes that clear the trees keep their exact path):
//  - a 4-connected search over the subcells the class leaves open, in the box spanned by the start, the goal and the
//    path, grown by TREE_BOX_PAD and doubled while both the start's and the goal's regions run past its edges (a goal
//    the search cannot reach while its region stays inside the box is unreachable: see below). The goal's subcell is
//    exempt. A blocked subcell is entered only from a blocked one, so a unit standing in an obstruction walks out of it
//    and cannot walk back in on open ground;
//  - a goal the search does not reach ends where the nearest open subcell the search reached lies, ties by row then
//    column (MakeGoalReachable's rule, grid_nav.h find_path_cells);
//  - the cells are string-pulled at 1 m: a leg is kept only while it crosses no blocked subcell (the same rule as above).
// A mine's blocked subcells count as blocked too, so the new path never enters a mine's square the navcells avoided.
// Over budget (a box of more than TREE_BFS_CELLS subcells), with no open subcell reached, or with more waypoints than
// WaypointPath holds, the navcell path stays and the fallback is counted.
inline void NavGrid::avoid_tree_subcells(float sx, float sz, float &gx, float &gz, WaypointPath &out, uint8_t pass, float rx, float rz) const {
    const int cls = tile_class(pass);
    if (!tree_any() || (pass & PASS_SHIP) || !crosses_tree_sub(cls, sx, sz, out)) return;
    const int S = res * 8;
    const float k = static_cast<float>(S) / (map_half * 2.0f); // subcells per world unit: 1 m each
    auto sub_i = [&](float v) { return std::clamp(static_cast<int>((v + map_half) * k), 0, S - 1); };
    const int si = sub_i(sx), sj = sub_i(sz);
    const int gi = sub_i(rx), gj = sub_i(rz); // the goal the caller asked for: 1 m's nearest reachable point is ranked to it
    int lo0_i = std::min(si, gi), hi0_i = std::max(si, gi), lo0_j = std::min(sj, gj), hi0_j = std::max(sj, gj);
    for (int v = 0; v < out.count; ++v) {
        const int xi = sub_i(out.xs[v]), zj = sub_i(out.zs[v]);
        lo0_i = std::min(lo0_i, xi);
        hi0_i = std::max(hi0_i, xi);
        lo0_j = std::min(lo0_j, zj);
        hi0_j = std::max(hi0_j, zj);
    }
    // The requested goal's subcell and the navcell path's end (the 8 m goal) are open for the search (exempt).
    const int mi = sub_i(out.xs[out.count - 1]), mj = sub_i(out.zs[out.count - 1]);
    // A navcell its structure closes (a wall, a cliff, the map's edge, a mine's cover: structural_open) is never entered
    // by the re-plan, as the 8 m path never enters it; the navcell the walk starts in is the exception, so a unit standing
    // in a structure walks out of it. A wood's closure is not part of this: the trees' own subcells stand for it, so the
    // search routes between trees as the 8 m grid cannot. The goal's and the path's end's subcells are exempt.
    const int snav_i = si / 8, snav_j = sj / 8;
    auto goal_sub = [&](int i, int j) { return (i == gi && j == gj) || (i == mi && j == mj); };
    // A NAV_WATER navcell is closed by the 1 m depth rule per subcell (water_sub), not as a whole: 0 A.D.'s water
    // passability reads the ground under each 1 m navcell (the probe's dock: the 8 m navcell's closure cost 11 s).
    // A navcell's structural closure (structural_open) is the same for all its subcells and for the whole call, so it is
    // computed once per navcell per call: the box's subcells share their navcells, and the string pull tests them again.
    thread_local std::vector<uint32_t> nav_stamp;
    thread_local std::vector<uint8_t> nav_closed;
    thread_local uint32_t nav_gen = 0;
    if (++nav_gen == 0) { // the stamps wrapped: clear them
        std::fill(nav_stamp.begin(), nav_stamp.end(), 0u);
        nav_gen = 1;
    }
    if (nav_stamp.size() < cells.data.size()) {
        nav_stamp.resize(cells.data.size(), 0u);
        nav_closed.resize(cells.data.size(), 0);
    }
    auto navcell_closed = [&](int cx, int cy) -> bool {
        if (!cells.inside(cx, cy)) return true; // structural_open is false off the map
        const size_t k2 = static_cast<size_t>(cy) * static_cast<size_t>(cells.w) + static_cast<size_t>(cx);
        if (nav_stamp[k2] != nav_gen) {
            nav_stamp[k2] = nav_gen;
            nav_closed[k2] = structural_open(cx, cy) ? 0 : 1;
        }
        return nav_closed[k2] != 0;
    };
    // The subcell's closure: the goal's and the path's end's subcells and the start's navcell are open; a NAV_WATER
    // navcell is closed by its depth rule, or whole when steep (structural_open closes steep ground too).
    auto closed = [&](int i, int j) {
        if (goal_sub(i, j) || (i / 8 == snav_i && j / 8 == snav_j)) return false;
        if (!water_sub.empty() && cells.inside(i / 8, j / 8) && cells.at(i / 8, j / 8) == NAV_WATER)
            return water_subcell_deep(i, j) || is_steep(i / 8, j / 8);
        return navcell_closed(i / 8, j / 8);
    };
    // The box: its subcells padded by one on each side. The padding is closed, so the flood never leaves the box, and
    // a flood's neighbour is a plain index step. One byte per padded subcell: kClosed (closed), kBlocked (the flood's
    // blocked state: closed, or a tree's or mine's subcell unless it is the goal's, so a unit standing in one walks out
    // of it), kSub (a tree's or mine's subcell, the string pull's own rule). Filled once per box, a navcell at a time.
    constexpr uint8_t kClosed = 1, kBlocked = 2, kSub = 4;
    thread_local std::vector<uint8_t> st;
    // visit[c].from: the padded cell the search reached c from (-1 for the start). visit[c].seen == gen: the current flood reached c.
    // The stamps let a flood start without a fill (from is read only at cells seen by the current flood).
    // seen and from share one record: a flood visits a cell through one cache line, not two.
    struct Visit { uint32_t seen; int32_t from; };
    thread_local std::vector<int32_t> queue;
    thread_local std::vector<Visit> visit;
    thread_local uint32_t gen = 0;
    int lo_i = 0, hi_i = 0, lo_j = 0, hi_j = 0, W = 0, H = 0, PW = 0;
    size_t np = 0;     // padded cells
    int goal_p = 0, mend_p = 0; // the requested goal's and the path's end's padded cells
    int near_c = -1;   // the nearest open subcell the last flood reached to the goal (padded), -1 if none
    long long near_d = -1;
    auto pidx = [&](int i, int j) { return (i - lo_i + 1) + (j - lo_j + 1) * PW; };
    auto fill = [&]() {
        st.assign(np, static_cast<uint8_t>(kClosed | kBlocked));
        const bool water_on = !water_sub.empty();
        const bool tree_on = !tree_sub[cls].empty(), mine_on = !mine_sub[cls].empty();
        for (int cy = lo_j / 8; cy <= hi_j / 8; ++cy) {
            for (int cx = lo_i / 8; cx <= hi_i / 8; ++cx) {
                const size_t nv = static_cast<size_t>(cy) * static_cast<size_t>(res) + static_cast<size_t>(cx);
                const bool water = water_on && cells.inside(cx, cy) && cells.at(cx, cy) == NAV_WATER;
                const bool nav_cl = water ? false : navcell_closed(cx, cy);
                const bool steep = water && is_steep(cx, cy);
                const uint64_t ww = water ? water_sub[nv] : 0u;
                const uint64_t sub = (tree_on ? tree_sub[cls][nv] : 0u) | (mine_on ? mine_sub[cls][nv] : 0u);
                const bool snav = cx == snav_i && cy == snav_j;
                const int x0 = std::max(lo_i, cx * 8), x1 = std::min(hi_i, cx * 8 + 7);
                const int y0 = std::max(lo_j, cy * 8), y1 = std::min(hi_j, cy * 8 + 7);
                // The same values as goal_sub and the closure tests per subcell, worked out as bit masks over the
                // navcell's 64 subcells: the goal subcells inside it are exempt (their closure is off, and their
                // bit in `sub` does not block them), and the start's navcell is open.
                uint64_t ex_mask = 0;
                for (int g = 0; g < 2; ++g) {
                    const int gx = g ? mi : gi, gy = g ? mj : gj;
                    if (gx / 8 == cx && gy / 8 == cy) ex_mask |= 1ull << ((gy & 7) * 8 + (gx & 7));
                }
                const uint64_t cl_all = snav ? 0ull : (water ? (ww | (steep ? ~0ull : 0ull)) : (nav_cl ? ~0ull : 0ull));
                const uint64_t cl_eff = cl_all & ~ex_mask;
                const uint64_t bl_all = cl_eff | (sub & ~ex_mask);
                uint8_t *sp = st.data();
                for (int y = y0; y <= y1; ++y) {
                    uint8_t *cell = sp + pidx(x0, y);
                    for (int x = x0; x <= x1; ++x, ++cell) {
                        const uint64_t m = 1ull << ((y & 7) * 8 + (x & 7));
                        *cell = static_cast<uint8_t>(((cl_eff & m) ? kClosed : 0) | ((bl_all & m) ? kBlocked : 0) | ((sub & m) ? kSub : 0));
                    }
                }
            }
        }
    };
    // Floods the box from src (a step c -> nb is allowed iff c is blocked or nb is open). Stops at the goal's subcell.
    auto flood = [&](int src) {
        if (visit.size() < np) visit.resize(np, Visit{0u, 0});
        if (++gen == 0) { // the stamps wrapped: clear them
            for (Visit &v : visit) v.seen = 0u;
            gen = 1;
        }
        // A cell is queued once per flood, so the queue holds at most np cells: a flat buffer with a head and a tail.
        if (queue.size() < np) queue.resize(np);
        int32_t *q = queue.data();
        const uint8_t *sp = st.data();
        Visit *vs = visit.data();
        size_t head = 0, tail = 0;
        vs[src].seen = gen;
        vs[src].from = -1;
        q[tail++] = src;
        if (src == goal_p) return;
        // The flood ends where it discovers the goal's subcell: each cell's from is set before the cell is pushed, so the
        // chain from the goal is complete, and the pad loop takes the goal as reached (its nearest-subcell scan unused).
        while (head < tail) {
            const int c = q[head++];
            const bool cb = (sp[c] & kBlocked) != 0;
            const int nb[4] = {c + 1, c - 1, c + PW, c - PW};
            for (int d = 0; d < 4; ++d) {
                const int nc = nb[d];
                if (sp[nc] & kClosed) continue; // a structure's navcell is never entered
                if (vs[nc].seen == gen) continue;
                if (!cb && (sp[nc] & kBlocked)) continue; // a step leaves open ground only into open ground
                vs[nc].seen = gen;
                vs[nc].from = c;
                q[tail++] = nc;
                if (nc == goal_p) return;
            }
        }
    };
    // Whether the last flood, which ran to its end, reached a subcell on an edge of the box that is not an edge of the map
    // (the region may go on past it).
    auto clipped_edge = [&]() {
        if (lo_i > 0)
            for (int j = lo_j; j <= hi_j; ++j)
                if (visit[static_cast<size_t>(pidx(lo_i, j))].seen == gen) return true;
        if (hi_i < S - 1)
            for (int j = lo_j; j <= hi_j; ++j)
                if (visit[static_cast<size_t>(pidx(hi_i, j))].seen == gen) return true;
        if (lo_j > 0)
            for (int i = lo_i; i <= hi_i; ++i)
                if (visit[static_cast<size_t>(pidx(i, lo_j))].seen == gen) return true;
        if (hi_j < S - 1)
            for (int i = lo_i; i <= hi_i; ++i)
                if (visit[static_cast<size_t>(pidx(i, hi_j))].seen == gen) return true;
        return false;
    };
    // The nearest open subcell the last flood reached to the goal: the least squared distance, ties by box index (the
    // row-major first: the scan is row-major, and the first of equal distances is kept).
    auto nearest = [&]() {
        near_d = -1;
        near_c = -1;
        for (int j = lo_j; j <= hi_j; ++j) {
            const long long dz = j - gj;
            for (int i = lo_i; i <= hi_i; ++i) {
                const int p = pidx(i, j);
                if (visit[static_cast<size_t>(p)].seen != gen || (st[static_cast<size_t>(p)] & kBlocked)) continue;
                const long long dx = i - gi, d = dx * dx + dz * dz;
                if (near_c < 0 || d < near_d) {
                    near_d = d;
                    near_c = p;
                }
            }
        }
    };
    // The box grows by pad subcells around the start, the goal and the path, doubling while the start's region runs past
    // its edges and the nearest open subcell found so far is not yet provably the nearest: a subcell outside the box lies
    // at least the box's edge distance from the goal, so a nearer one found inside it is the nearest overall. The goal's
    // region may be closed in (unreachable): the nearest reached subcell can then lie outside a box that was too small.
    bool reached_goal = false, reached_end = false;
    for (int pad = TREE_BOX_PAD;; pad *= 2) {
        lo_i = std::max(0, lo0_i - pad);
        hi_i = std::min(S - 1, hi0_i + pad);
        lo_j = std::max(0, lo0_j - pad);
        hi_j = std::min(S - 1, hi0_j + pad);
        W = hi_i - lo_i + 1;
        H = hi_j - lo_j + 1;
        if (static_cast<long long>(W) * H > TREE_BFS_CELLS) {
            ++tree_sub_fallbacks;
            return;
        }
        PW = W + 2;
        np = static_cast<size_t>(PW) * static_cast<size_t>(H + 2);
        goal_p = pidx(gi, gj);
        mend_p = pidx(mi, mj);
        fill();
        flood(pidx(si, sj));
        reached_goal = visit[static_cast<size_t>(goal_p)].seen == gen;
        reached_end = visit[static_cast<size_t>(mend_p)].seen == gen;
        if (reached_goal || reached_end || !clipped_edge()) break;
        nearest();
        long long dl = LLONG_MAX; // the distance from the goal to the nearest box edge that is not the map's
        if (lo_i > 0) dl = std::min<long long>(dl, gi - (lo_i - 1));
        if (hi_i < S - 1) dl = std::min<long long>(dl, (hi_i + 1) - gi);
        if (lo_j > 0) dl = std::min<long long>(dl, gj - (lo_j - 1));
        if (hi_j < S - 1) dl = std::min<long long>(dl, (hi_j + 1) - gj);
        if (near_d >= 0 && near_d < dl * dl) break;
    }
    // The goal the search reached: the requested one (as 0.28's MakeGoalReachable leaves a reachable goal), else the
    // navcell path's end (the goal RTS0's MakeGoalReachable chose, when the path reaches it), else the nearest reached
    // subcell to the requested goal (MakeGoalReachable on the 1 m grid).
    int target = goal_p;
    if (!reached_goal) {
        target = mend_p;
        if (!reached_end) {
            // Neither the goal nor the navcell end is reached: no tree re-plan. The nearest reached subcell would end
            // the leg at a shore point that the unit's clearance bars, so the walk never gets past it (G168).
            ++tree_sub_fallbacks;
            return;
        }
    }
    // The box cells from the start to the target.
    thread_local std::vector<int> chain;
    chain.clear();
    for (int c = target; c != -1; c = visit[static_cast<size_t>(c)].from) chain.push_back(c);
    std::reverse(chain.begin(), chain.end());
    const int m = static_cast<int>(chain.size()) - 1;
    auto cell_centre = [&](int c, float &x, float &z) {
        x = (static_cast<float>(c % PW - 1 + lo_i) + 0.5f) / k - map_half;
        z = (static_cast<float>(c / PW - 1 + lo_j) + 0.5f) / k - map_half;
    };
    // Vertex i of the chain: the goal itself where the search reached it, else the centre of its subcell.
    auto vertex = [&](int i, float &x, float &z) {
        if (i == m && reached_goal) {
            x = rx;
            z = rz;
        } else if (i == m && reached_end) {
            x = out.xs[out.count - 1];
            z = out.zs[out.count - 1];
        } else {
            cell_centre(chain[static_cast<size_t>(i)], x, z);
        }
    };
    // A straight leg from (ax, az) to (bx, bz) that crosses no blocked subcell once it has left the blocked ground
    // it starts on (the rule of the search above; the goal's subcell is open). The box's subcells are read from st; a
    // subcell outside the box (the walk may touch one) is read as the structure and sub-cell rules give it.
    auto leg_clear = [&](float ax, float az, float bx, float bz) {
        bool left = false, ok = true;
        walk_grid(S, (ax + map_half) * k, (az + map_half) * k, (bx + map_half) * k, (bz + map_half) * k,
                [&](int x, int y) {
                    bool cl, sb;
                    if (x >= lo_i && x <= hi_i && y >= lo_j && y <= hi_j) {
                        const uint8_t s = st[static_cast<size_t>(pidx(x, y))];
                        cl = (s & kClosed) != 0;
                        sb = (s & kSub) != 0;
                    } else {
                        cl = closed(x, y);
                        sb = sub_blocked(cls, x, y);
                    }
                    if (cl) { // a structure's navcell: the leg is refused
                        ok = false;
                        return false;
                    }
                    if ((x == gi && y == gj) || !sb) {
                        left = true;
                        return true;
                    }
                    if (left) ok = false;
                    return ok;
                });
        return ok;
    };
    // String pulling at 1 m: from each anchor the farthest vertex of the chain the leg reaches before the first that it
    // does not (the next cell is always a leg of the search itself).
    thread_local std::vector<std::pair<float, float>> verts;
    verts.clear();
    float ax = sx, az = sz;
    int i = 0;
    while (i < m) {
        int best = i + 1;
        for (int j = i + 2; j <= m; ++j) {
            float jx, jz;
            vertex(j, jx, jz);
            if (!leg_clear(ax, az, jx, jz)) break;
            best = j;
        }
        float bx, bz;
        vertex(best, bx, bz);
        verts.emplace_back(bx, bz);
        ax = bx;
        az = bz;
        i = best;
    }
    if (m == 0) {
        float x, z;
        vertex(0, x, z);
        verts.emplace_back(x, z);
    }
    if (verts.size() > static_cast<size_t>(WaypointPath::MAX_WP)) {
        ++tree_sub_fallbacks;
        return;
    }
    out.clear();
    for (const auto &v : verts) out.push_back(v.first, v.second);
    out.cur = 0;
    gx = verts.back().first; // the path's end: the goal reached, or the nearest reachable subcell's centre
    gz = verts.back().second;
    ++tree_sub_replans;
    out.tree_replanned = true; // C1 tree watch: this leg was re-planned round a tree's subcell
}

inline bool NavGrid::find_path(float sx, float sz, float tx, float tz, WaypointPath &out, uint8_t pass, bool navcells_only) const {
    const bool found = find_path_planned(sx, sz, tx, tz, out, pass, navcells_only);
    out.tree_off = navcells_only; // set after the plan (its out.clear() reset it); the walk's later re-plans read it
    return found;
}

inline bool NavGrid::find_path_planned(float sx, float sz, float tx, float tz, WaypointPath &out, uint8_t pass, bool navcells_only) const {
    if (pass & PASS_LARGE) {
        bool narrow_gap = false;
        if (find_path_on(sx, sz, tx, tz, out, pass, &narrow_gap, navcells_only)) return true;
        // A path planned on the nodes round a gap too narrow for the class that does not reach the
        // goal ends where MakeGoalReachable moves it, the nearest node of the start's region
        // (HierarchicalPathfinder.cpp:689-719), as 0.28's long path for the class does: it has no
        // other class to fall back on. The default class's path would take the unit into the gap it
        // was planned round, where its steps are refused at the mouth (Simulation::large_step_barred).
        if (narrow_gap) return false;
        return find_path_on(sx, sz, tx, tz, out, pass & ~PASS_LARGE, nullptr, navcells_only);
    }
    if (!(pass & PASS_SHIP_CLEARANCE)) return find_path_on(sx, sz, tx, tz, out, pass, nullptr, navcells_only);
    return find_path_on(sx, sz, tx, tz, out, pass, nullptr, navcells_only) ||
           find_path_on(sx, sz, tx, tz, out, PASS_SHIP, nullptr, navcells_only);
}

inline bool NavGrid::find_path_on(float sx, float sz, float tx, float tz, WaypointPath &out, uint8_t pass, bool *narrow_gap,
        bool navcells_only) const {
    float gx = tx, gz = tz;
    bool reached = find_path_cells(sx, sz, gx, gz, out, pass);
    if (reached) {
        const int mines = avoid_mine_subcells(sx, sz, gx, gz, out, pass);
        if (mines == 2) return false;
        // C1: the trees' subcells, after the mines' (avoid_tree_subcells).
        // The tree search ranks to the navcell path's end (gx, gz), not the requested point. That end is the requested goal
        // unless find_path_cells moved it: the G163 snap of a point inside an obstruction, the water branch's shore point
        // for a water goal (point_goal_snaps is false for those, so the requested point would end the walk in the water),
        // or a region move. A walk to a water point stopped at the shore must not be extended into the water.
        if (!navcells_only) avoid_tree_subcells(sx, sz, gx, gz, out, pass, gx, gz);
    }
    if ((pass & PASS_SHIP) || cells.blank()) return reached;
    const int cls = tile_class(pass);
    if (!tile_class_kept(cls)) return reached;
    bool fine = false;
    if (tile_paths_on()) {
        // The class's clearance, grown by half a sample so a leg that clips a steep tile's corner
        // between two samples counts too.
        const float c = tile_clearance(cls) + steep_sample_pad();
        fine = near_steep_tile(gx, gz, c) || path_near_steep_tile(sx, sz, out, c) || path_through_obstruction_near_steep(sx, sz, out, cls);
        // A path that ends short of the goal (MakeGoalReachable moved it, or JPS stopped at the
        // navcell nearest it) when the goal's tile region is the start's: navcells steep by their
        // averaged slope can close a pass whose tiles are level, and 0.28's MakeGoalReachable, on
        // navcells no coarser than the tiles, leaves the goal where it is. A point goal the G163 end
        // move snapped is not such a path: 0.28 leaves a reachable point goal where it is, so the
        // snapped end is no reason for the tile planner.
        if (!fine && !point_goal_snaps(tx, tz, pass) && (out.count == 0 || out.xs[out.count - 1] != tx || out.zs[out.count - 1] != tz)) {
            const uint32_t from = tile_region_at(sx, sz, tile_snap_reach(), cls);
            fine = from != 0 && from == tile_region_at(tx, tz, tile_snap_reach(), cls);
        }
    }
    // A "large" path through a gap between two structures too narrow for the class: 0.28's long
    // path never takes one (Rasterize.cpp:29-48), and the unit could not walk it
    // (Simulation::large_step_barred).
    bool gap = false;
    if (!fine && cls == 1) fine = gap = path_through_narrow_gap(sx, sz, out);
    if (!fine) return reached;
    // The tiles take the goal as given: their own MakeGoalReachable moves it, not the navcells'.
    const WaypointPath navcell = out;
    const bool navcell_reached = reached;
    WaypointPath tiles;
    bool tiles_reached = false;
    if (!find_path_tiles(sx, sz, tx, tz, tiles, tiles_reached, cls)) return reached;
    // G171: ImprovePathWaypoints (find_path_tiles) tests each shortcut it takes with tile_line_clear,
    // but not the first leg from the unit to its first node, the leg into the goal, or any leg of a
    // path with at most two jump points (a collinear shortcut in the middle is the same line as its
    // jump segments). A leg of those that enters a tree's clearance (GAIA grown by the class
    // clearance, not the subcells' clearance plus 1 m) would be walked into a tree the steering
    // must go round, and the navcell path was just re-planned round the trees. Keep the navcell
    // path when the first or last such leg crosses (tile_path_crosses_tree_clearance); middle legs
    // the thinning did test already passed tile_line_clear at the same clearance. Skipped for the
    // narrow-gap case, where the navcell path takes the gap the tile path was planned round, and
    // with navcells_only, where the walk asked for the tree-blind navcell path.
    if (!navcells_only && !gap && tile_path_crosses_tree_clearance(sx, sz, tiles, cls)) {
        out = navcell;
        return navcell_reached;
    }
    out = tiles;
    if (narrow_gap) *narrow_gap = gap;
    return tiles_reached;
}

// find_path_cells moves a point goal on an impassable navcell to the passable navcell it snaps to (the
// G163 end move, below). This is that test, for a goal. A water goal of a land unit is moved by the
// water branch first, so it is not counted here.
inline bool NavGrid::point_goal_snaps(float tx, float tz, uint8_t pass) const {
    if (cells.blank()) return false;
    const auto [cx, cy] = world_to_cell(tx, tz);
    if (is_passable_for(cx, cy, pass)) return false;
    if (!(pass & PASS_SHIP) && cells.inside(cx, cy) && cells.at(cx, cy) == NAV_WATER) return false;
    for (int dy = -3; dy <= 3; ++dy)
        for (int dx = -3; dx <= 3; ++dx)
            if (is_passable_for(cx + dx, cy + dy, pass)) return true;
    return false;
}

inline bool NavGrid::find_path_cells(float sx, float sz, float &tx, float &tz, WaypointPath &out, uint8_t pass) const {
    out.clear();
    if (cells.blank()) {
        out.push_back(tx, tz);
        return false;
    }
    auto [scx, scy] = world_to_cell(sx, sz);
    auto [tcx, tcy] = world_to_cell(tx, tz);

    // An end on an impassable navcell moves to the passable navcell nearest its own point, within
    // 3 navcells: LongPathfinder.cpp:749-754 does so for the start
    // (HierarchicalPathfinder::FindNearestPassableNavcell) and MakeGoalReachable for the goal
    // (HierarchicalPathfinder.cpp:715-720). The first navcell a scan meets could lie across a wall:
    // a unit pressed on it would start its path on the far side, and a walk to its face would end
    // there. Ring r holds no navcell nearer than r - 0.5; ties keep the scan order.
    auto snap_to_passable = [&](int &cx, int &cy, float px, float pz) {
        if (is_passable_for(cx, cy, pass)) return;
        const auto [fx, fy] = world_to_navcells(px, pz);
        float best = -1.0f;
        int bx = cx, by = cy;
        for (int r = 1; r <= 3; ++r) {
            const float ring = static_cast<float>(r) - 0.5f;
            if (best >= 0.0f && ring * ring >= best) break;
            for (int dy = -r; dy <= r; ++dy) {
                for (int dx = -r; dx <= r; ++dx) {
                    if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                    if (!is_passable_for(cx + dx, cy + dy, pass)) continue;
                    const float ex = static_cast<float>(cx + dx) + 0.5f - fx;
                    const float ey = static_cast<float>(cy + dy) + 0.5f - fy;
                    const float d = ex * ex + ey * ey;
                    if (best < 0.0f || d < best) { best = d; bx = cx + dx; by = cy + dy; }
                }
            }
        }
        cx = bx;
        cy = by;
    };
    snap_to_passable(scx, scy, sx, sz);
    // MakeGoalReachable: a goal out at sea becomes the nearest navcell the unit can stand
    // on, so a land unit stops at the shore instead of wading into deep water.
    if (!(pass & PASS_SHIP) && cells.inside(tcx, tcy) && cells.at(tcx, tcy) == NAV_WATER) {
        int best = -1, bx = tcx, by = tcy;
        for (int r = 1; r < res && (best < 0 || r * r <= best); ++r) {
            for (int dy = -r; dy <= r; ++dy) {
                for (int dx = -r; dx <= r; ++dx) {
                    if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                    const int d = dx * dx + dy * dy;
                    if ((best < 0 || d < best) && is_passable_for(tcx + dx, tcy + dy, pass)) {
                        best = d; bx = tcx + dx; by = tcy + dy;
                    }
                }
            }
        }
        if (best >= 0) {
            tcx = bx; tcy = by;
            tx = ((static_cast<float>(tcx) + 0.5f) / static_cast<float>(res)) * (map_half * 2.0f) - map_half;
            tz = ((static_cast<float>(tcy) + 0.5f) / static_cast<float>(res)) * (map_half * 2.0f) - map_half;
        }
    }
    // The end moves with its navcell, as 0 A.D.'s MakeGoalReachable does for an unreachable point goal
    // (HierarchicalPathfinder::CreatePointGoalAt, HierarchicalPathfinder.cpp:681-718): the last
    // waypoint is the centre of the passable navcell the goal snapped to, not the point inside the
    // obstruction. Otherwise a unit walks to the obstruction and stalls there, its path ending on
    // ground it cannot enter.
    const int goal_cx = tcx, goal_cy = tcy;
    snap_to_passable(tcx, tcy, tx, tz);
    if (tcx != goal_cx || tcy != goal_cy) {
        tx = ((static_cast<float>(tcx) + 0.5f) / static_cast<float>(res)) * (map_half * 2.0f) - map_half;
        tz = ((static_cast<float>(tcy) + 0.5f) / static_cast<float>(res)) * (map_half * 2.0f) - map_half;
    }
    // A goal still on impassable cells (land for a ship) is no reason to fail either:
    // MakeGoalReachable below moves it to the nearest navcell the unit can reach.
    if (!is_passable_for(scx, scy, pass) || (!regions_ready_ && !is_passable_for(tcx, tcy, pass))) {
        out.push_back(tx, tz);
        return false;
    }
    // HierarchicalPathfinder.cpp:689 MakeGoalReachable — if the goal region is not
    // connected to the start, snap to the nearest navcell in the start global region
    // instead of failing (0 A.D. never leaves the unit with an island goal).
    {
        const uint8_t rpass = pass;
        if (regions_ready_ && !reachable(scx, scy, tcx, tcy, rpass)) {
            const uint16_t want = global_region_at(scx, scy, rpass);
            int best_d = 1 << 30;
            int bx = tcx, by = tcy;
            bool found = false;
            // The nearest navcell of the start's region, ties going to the lowest row, then the
            // lowest column. Rings of growing Chebyshev radius r round the goal hold only cells at
            // least r away, so the search stops once r * r passes the best distance: a target
            // across a channel costs a few rings, not the whole grid.
            if (want != 0) {
                const int max_r = std::max(std::max(tcx, res - 1 - tcx), std::max(tcy, res - 1 - tcy));
                for (int r = 1; r <= max_r && (!found || r * r <= best_d); ++r) {
                    for (int y = std::max(0, tcy - r); y <= std::min(res - 1, tcy + r); ++y) {
                        const int dy = y - tcy;
                        const bool edge_row = dy == -r || dy == r;
                        for (int x = std::max(0, tcx - r); x <= std::min(res - 1, tcx + r);
                                x += (edge_row || x == tcx + r) ? 1 : std::max(1, tcx + r - x)) {
                            const int dx = x - tcx;
                            if (!edge_row && dx != -r && dx != r) continue;
                            const int d = dx * dx + dy * dy;
                            if (d > best_d || (d == best_d && (y > by || (y == by && x > bx)))) continue;
                            if (global_region_at(x, y, rpass) != want) continue;
                            best_d = d;
                            bx = x;
                            by = y;
                            found = true;
                        }
                    }
                }
            }
            if (!found) {
                out.push_back(tx, tz);
                return false;
            }
            tcx = bx;
            tcy = by;
            tx = ((static_cast<float>(tcx) + 0.5f) / static_cast<float>(res)) * (map_half * 2.0f) - map_half;
            tz = ((static_cast<float>(tcy) + 0.5f) / static_cast<float>(res)) * (map_half * 2.0f) - map_half;
        }
    }
    if (scx == tcx && scy == tcy) {
        out.push_back(tx, tz);
        return true;
    }

    const int W = res;
    const int H = res;
    std::vector<std::pair<int,int>> path_cells;
    path_cells.reserve(64);
    const bool reached = search_cells_jps(scx, scy, tcx, tcy, pass, path_cells);
    if (!reached && path_cells.size() < 2) {
        out.clear();
        out.push_back(tx, tz);
        return false;
    }
    string_pull(path_cells, pass);

    // LongPathfinder::ComputeJPSPath leaves the start navcell out (the unit already stands in it:
    // heading for its centre first would walk a unit given a new path every tick back and forth
    // there for good) and ends the path at the goal itself, not the goal navcell's centre.
    size_t k = 1;
    for (; k < path_cells.size(); ++k) {
        if (out.count >= WaypointPath::MAX_WP - 1) break;
        const auto &p = path_cells[k];
        float wx = ((static_cast<float>(p.first) + 0.5f) / static_cast<float>(W)) * (map_half * 2.0f) - map_half;
        float wz = ((static_cast<float>(p.second) + 0.5f) / static_cast<float>(H)) * (map_half * 2.0f) - map_half;
        out.push_back(wx, wz);
    }
    if (reached && out.count > 0) {
        if (k == path_cells.size()) {
            out.xs[out.count - 1] = tx;
            out.zs[out.count - 1] = tz;
        } else {
            out.push_back(tx, tz); // cut short at MAX_WP: still end at the goal
        }
    }
    out.cur = 0;
    return reached;
}

// ---------------------------------------------------------------------------
// NavGrid::find_path_tiles — find_path_on's pass on the nodes half a tile apart, for a land path
// that comes near steep ground: 0.28's LongPathfinder::ComputeJPSPath on the passability grid units
// move on.
// ---------------------------------------------------------------------------
template <class Hit>
inline bool NavGrid::segment_box_hits(float ax, float az, float bx, float bz, float r, Hit &&hit) const {
    if (!hit(std::min(ax, bx) - r, std::min(az, bz) - r, std::max(ax, bx) + r, std::max(az, bz) + r)) return false;
    const float dx = bx - ax, dz = bz - az;
    const float step = 0.5f * map_half / static_cast<float>(sample_tiles());
    const int n = std::max(1, static_cast<int>(std::ceil(std::sqrt(dx * dx + dz * dz) / step)));
    // In runs of eight samples, as nav::detail::segment_near_steep: a run whose box (grown by r)
    // hits nothing is passed over whole.
    constexpr int RUN = 8;
    for (int k0 = 1; k0 <= n; k0 += RUN) {
        const int k1 = std::min(n, k0 + RUN - 1);
        const float t0 = static_cast<float>(k0) / static_cast<float>(n), t1 = static_cast<float>(k1) / static_cast<float>(n);
        const float x0 = ax + dx * t0, z0 = az + dz * t0, x1 = ax + dx * t1, z1 = az + dz * t1;
        if (!hit(std::min(x0, x1) - r, std::min(z0, z1) - r, std::max(x0, x1) + r, std::max(z0, z1) + r)) continue;
        for (int k = k0; k <= k1; ++k) {
            const float t = static_cast<float>(k) / static_cast<float>(n);
            const float x = ax + dx * t, z = az + dz * t;
            if (hit(x - r, z - r, x + r, z + r)) return true;
        }
    }
    return false;
}

inline bool NavGrid::segment_near_steep_tile(float ax, float az, float bx, float bz, float r) const {
    if (!any_steep_tile()) return false;
    return segment_box_hits(ax, az, bx, bz, r, [this](float x0, float z0, float x1, float z1) { return steep_tile_in(x0, z0, x1, z1); });
}

inline bool NavGrid::path_near_steep_tile(float sx, float sz, const WaypointPath &path, float c) const {
    float x = sx, z = sz;
    for (int k = std::max(path.cur, 0); k < path.count; ++k) {
        if (segment_near_steep_tile(x, z, path.xs[k], path.zs[k], c)) return true;
        x = path.xs[k];
        z = path.zs[k];
    }
    return false;
}

inline bool NavGrid::path_through_obstruction_near_steep(float sx, float sz, const WaypointPath &path, int cls) const {
    const float c = tile_clearance(cls), g = c + TILE_HAIR, reach = 2.0f * c + steep_sample_pad();
    float x = sx, z = sz;
    for (int k = std::max(path.cur, 0); k < path.count; ++k) {
        const float ax = x, az = z, bx = path.xs[k], bz = path.zs[k];
        if (any_tile_obs_near_segment(ax, az, bx, bz, g, [&](const TileObstruction &o) {
                if (!tile_obs_segment_hit(cls == 1 ? tile_obs_crowd_grown(o) : o, ax, az, bx, bz, g)) return false;
                // Its box, grown by twice the clearance (and half a steep sample, as the lines' tests).
                const float au = std::fabs(o.ux), av = std::fabs(o.uz);
                const float ex = o.hu * au + o.hv * av + reach, ez = o.hu * av + o.hv * au + reach;
                return steep_tile_in(o.x - ex, o.z - ez, o.x + ex, o.z + ez);
            }))
            return true;
        x = bx;
        z = bz;
    }
    return false;
}

inline bool NavGrid::path_through_narrow_gap(float sx, float sz, const WaypointPath &path) const {
    if (tile_obs_[TILE_OBS_STRUCTURES].size() < 2) return false;
    // A walk that comes within the large clearance of a crowded structure's grown box (tile_obs_crowd_grown)
    // is one the node pass plans round: a walk through the gap between two such structures is one the
    // navcells would take through it.
    const float g = TILE_PATH_CLEARANCE_LARGE + TILE_HAIR;
    float x = sx, z = sz;
    for (int k = std::max(path.cur, 0); k < path.count; ++k) {
        const float ax = x, az = z, bx = path.xs[k], bz = path.zs[k];
        if (any_tile_obs_near_segment(ax, az, bx, bz, g, [&](const TileObstruction &o) {
                return o.crowd && tile_obs_segment_hit(tile_obs_crowd_grown(o), ax, az, bx, bz, g);
            }))
            return true;
        x = bx;
        z = bz;
    }
    return false;
}

inline bool NavGrid::tile_line_clear(float ax, float az, float bx, float bz, int cls, bool live) const {
    const float c = tile_clearance(cls);
    // The terrain is tested every quarter tile, so the margin it keeps off is half a sample wider
    // than the clearance: no point between two samples comes within the clearance.
    if (segment_box_hits(ax, az, bx, bz, c + steep_sample_pad(), [&](float x0, float z0, float x1, float z1) {
            return steep_tile_in(x0, z0, x1, z1) || water_cell_in(x0, z0, x1, z1, live);
        }))
        return false;
    return !segment_hits_tile_obs(ax, az, bx, bz, c, cls);
}

inline bool NavGrid::find_path_tiles(float sx, float sz, float tx, float tz, WaypointPath &out, bool &reached, int cls) const {
    if (!tile_class_kept(cls)) return false;
    const int nw = node_res();
    auto at = [&](int i, int j) { return static_cast<size_t>(j) * static_cast<size_t>(nw) + static_cast<size_t>(i); };
    // The nodes a unit of the class can stand on, as update_tile_open keeps them when it has; else
    // the terrain's alone, as it is now.
    const bool cached = tile_open_cached(), live = !cached;
    const std::vector<uint8_t> &open_nodes = tile_open_[cls];
    const std::vector<uint32_t> &regions = tile_region_[cls];
    auto open = [&](int i, int j) {
        if (i < 0 || j < 0 || i >= nw || j >= nw) return false;
        if (cached) return open_nodes[at(i, j)] != 0;
        return node_terrain_open(i, j, cls, true);
    };
    // LongPathfinder::ComputeJPSPath starts from the passable navcell nearest the unit
    // (FindNearestPassableNavcell), and a goal on an impassable one moves to the passable one nearest
    // it, by find_path_cells' rule (snap_tile). MakeGoalReachable then moves a goal the unit can't
    // walk to, to the node of the start's region nearest the goal's (the regions update_tile_open
    // keeps), ties going to the lowest row, then column, as find_path_cells does; without the regions
    // JPS ends on the explored node nearest the goal when it can't get there.
    int si = node_of(sx), sj = node_of(sz);
    const int si0 = si, sj0 = sj;
    if (!snap_tile(si, sj, sx, sz, tile_snap_reach(), open)) return false;
    const bool start_moved = si != si0 || sj != sj0;
    const uint32_t want = cached ? regions[at(si, sj)] : 0;
    const int gi0 = node_of(tx), gj0 = node_of(tz);
    int gi = gi0, gj = gj0;
    if (!snap_tile(gi, gj, tx, tz, tile_snap_reach(), open) && !want) return false;
    if (want && (!open(gi, gj) || regions[at(gi, gj)] != want)) {
        int best_d = -1, bi = gi0, bj = gj0;
        auto consider = [&](int i, int j) {
            const int di = i - gi0, dj = j - gj0, d = di * di + dj * dj;
            if (best_d >= 0 && (d > best_d || (d == best_d && (j > bj || (j == bj && i > bi))))) return;
            best_d = d;
            bi = i;
            bj = j;
        };
        // The start's region is first walked from the start, up to TILE_POCKET_NODES nodes: a pocket
        // (a gap between trees on a ridge) is then known whole, and a far goal costs no rings out to
        // it. A larger region seeds best_d with the nodes walked, so the rings round the goal stop
        // sooner; they find the same node, since every node walked is one of the region's.
        bool whole = false;
        {
            thread_local std::vector<uint8_t> seen;
            thread_local std::vector<std::pair<int, int>> todo;
            if (seen.size() != regions.size()) seen.assign(regions.size(), 0);
            todo.clear();
            if (todo.capacity() < TILE_POCKET_NODES + 4) todo.reserve(TILE_POCKET_NODES + 4);
            todo.emplace_back(si, sj);
            seen[at(si, sj)] = 1;
            size_t k = 0;
            for (; k < todo.size() && todo.size() <= TILE_POCKET_NODES; ++k) {
                const auto [i, j] = todo[k];
                consider(i, j);
                const int ni[4] = {i - 1, i + 1, i, i}, nj[4] = {j, j, j - 1, j + 1};
                for (int n = 0; n < 4; ++n) {
                    if (ni[n] < 0 || nj[n] < 0 || ni[n] >= nw || nj[n] >= nw) continue;
                    const size_t a = at(ni[n], nj[n]);
                    if (seen[a] || regions[a] != want) continue;
                    seen[a] = 1;
                    todo.emplace_back(ni[n], nj[n]);
                }
            }
            whole = k == todo.size();
            for (const auto &[i, j] : todo) seen[at(i, j)] = 0;
        }
        const int max_r = whole ? 0 : std::max(std::max(gi0, nw - 1 - gi0), std::max(gj0, nw - 1 - gj0));
        for (int r = 1; r <= max_r && (best_d < 0 || r * r <= best_d); ++r)
            for (int dj = -r; dj <= r; ++dj)
                for (int di = -r; di <= r; di += (dj == -r || dj == r || di == r) ? 1 : 2 * r) {
                    const int i = gi0 + di, j = gj0 + dj;
                    if (i < 0 || j < 0 || i >= nw || j >= nw) continue;
                    if (regions[at(i, j)] == want) consider(i, j);
                }
        if (best_d < 0) return false;
        gi = bi;
        gj = bj;
    }
    // The path ends at the goal itself when it stays on its own node and a unit of the class can
    // stand there; else on the node it moved to.
    const float c = tile_clearance(cls);
    const bool goal_moved = gi != gi0 || gj != gj0 || !open(gi, gj) || !point_terrain_open(tx, tz, cls, live) ||
            segment_hits_tile_obs(tx, tz, tx, tz, c, cls);
    thread_local nav::JpsBuffers jps;
    thread_local std::vector<std::pair<int, int>> path_cells;
    thread_local std::vector<std::pair<float, float>> pts, kept;
    if (path_cells.capacity() < 256) path_cells.reserve(256);
    if (pts.capacity() < 256) pts.reserve(256);
    if (kept.capacity() < 256) kept.reserve(256);
    // The search scans the open nodes' bits when they are kept (nav::JpsBits).
    nav::JpsBits bits;
    if (cached) {
        bits.row_words = bits.col_words = node_words();
        bits.rows = tile_bits_rows_[cls].data();
        bits.cols = tile_bits_cols_[cls].data();
    }
    reached = nav::compute_jps_path(nw, nw, open, si, sj, gi, gj, path_cells, nullptr, &jps, cached ? &bits : nullptr);
    nav::jps_debug_keep(jps, nw, -map_half, -map_half, node_step());
    if (path_cells.empty()) return false;

    // The jump points after the start node, the last one replaced by the goal when the path gets
    // there (ComputeJPSPath leaves out the navcell the unit starts on). A start that moved keeps
    // its node, which the unit walks to first.
    pts.clear();
    if (start_moved) pts.emplace_back(node_pos(si), node_pos(sj));
    for (size_t k = 1; k < path_cells.size(); ++k) pts.emplace_back(node_pos(path_cells[k].first), node_pos(path_cells[k].second));
    if (reached && !goal_moved) {
        if (path_cells.size() > 1) pts.back() = {tx, tz};
        else pts.emplace_back(tx, tz);
    } else if (pts.empty()) {
        pts.emplace_back(node_pos(si), node_pos(sj));
    }
    // LongPathfinder::ImprovePathWaypoints, from the goal back: a waypoint stays only when the line
    // from the last one kept to the one before it isn't clear (CheckLineMovement: tile_line_clear);
    // one on a line with those two goes without a test ("mostly straight"), and the one nearest the
    // start always stays. One test a waypoint, so the pass is linear in the path's length.
    kept.clear();
    const size_t m = pts.size();
    kept.push_back(pts[m - 1]);
    if (m >= 2) {
        std::pair<float, float> prev = pts[m - 1];
        for (size_t k = m - 2; k >= 1; --k) {
            const std::pair<float, float> &curr = pts[k], &ahead = pts[k - 1];
            const float cross = (ahead.first - curr.first) * (curr.second - prev.second) - (ahead.second - curr.second) * (curr.first - prev.first);
            if (std::fabs(cross) > 1e-4f && !tile_line_clear(prev.first, prev.second, ahead.first, ahead.second, cls, live)) {
                prev = curr;
                kept.push_back(curr);
            }
        }
        kept.push_back(pts[0]);
    }
    std::reverse(kept.begin(), kept.end());
    // As find_path_cells: a path cut short at MAX_WP still ends where the search ended it.
    out.clear();
    for (size_t k = 0; k < kept.size(); ++k) {
        if (out.count == WaypointPath::MAX_WP - 1 && k + 1 < kept.size()) {
            out.push_back(kept.back().first, kept.back().second);
            break;
        }
        out.push_back(kept[k].first, kept[k].second);
    }
    out.cur = 0;
    return true;
}

// ---------------------------------------------------------------------------
// NavGrid::search_cells_jps — Jump Point Search (nav/long_pathfinder.h), the
// port of 0 A.D. LongPathfinder::ComputeJPSPath. Emits start, jump points and
// goal; on failure, the path to the closest explored cell (iBest/jBest).
// ---------------------------------------------------------------------------
inline bool NavGrid::search_cells_jps(int scx, int scy, int tcx, int tcy, uint8_t pass,
        std::vector<std::pair<int,int>> &path_cells) const {
    thread_local nav::JpsBuffers jps; // find_path runs on the worker threads: one search state each
    bool reached;
    // The closed search (PASS_MINE_CLOSED) takes the general path: the fast one reads the arrays and
    // doesn't know which partly covered navcells are closed.
    if (!(pass & PASS_SHIP) && !((pass & PASS_MINE_CLOSED) && mine_any()) && cells.w == res && cells.h == res &&
            (steep_cells_.blank() || (steep_cells_.w == res && steep_cells_.h == res))) {
        // is_passable_for for a land class, read straight from the arrays: the search tests cells
        // inside the grid only, millions of times on a long path across a big map.
        const uint8_t *cd = cells.data.data();
        const uint8_t *st = steep_cells_.blank() ? nullptr : steep_cells_.data.data();
        const uint8_t *lc = (pass & PASS_LARGE) && large_closed.size() == cells.data.size() ? large_closed.data() : nullptr;
        const size_t w = static_cast<size_t>(res);
        auto passable = [cd, st, lc, w](int x, int y) {
            const size_t i = static_cast<size_t>(y) * w + static_cast<size_t>(x);
            const uint8_t c = cd[i];
            return (c == NAV_PASSABLE || c == NAV_SHALLOW) && !(st && st[i]) && !(lc && lc[i]);
        };
        reached = nav::compute_jps_path(res, res, passable, scx, scy, tcx, tcy, path_cells, nullptr, &jps);
    } else {
        auto passable = [this, pass](int x, int y) { return is_passable_for(x, y, pass); };
        reached = nav::compute_jps_path(res, res, passable, scx, scy, tcx, tcy, path_cells, nullptr, &jps);
    }
    const float cell = map_half * 2.0f / static_cast<float>(res);
    nav::jps_debug_keep(jps, res, -map_half + 0.5f * cell, -map_half + 0.5f * cell, cell);
    return reached;
}

} // namespace rtso

#endif // RTSO_GRID_NAV_H
