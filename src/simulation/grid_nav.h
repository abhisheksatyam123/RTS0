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

#include <cstdint>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <utility>
#include <unordered_map>

#include "nav/long_pathfinder.h"

namespace rtso {

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
    Grid<uint8_t> dirty; // 0=clean, 1=needs hierarchical rebuild
    // A circular map's passable edge (Simulation::past_map_edge), 0 on a square map. The navcells whose
    // centre lies past it are closed (Simulation::rebuild_nav_grid); find_path_tiles' nodes take the
    // edge at their own spacing, as units do (point_terrain_open).
    float edge_r = 0.0f;

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
    static int tile_class(uint8_t pass) { return (pass & PASS_LARGE) ? 1 : 0; }
    static float tile_clearance(int cls) { return cls ? TILE_PATH_CLEARANCE_LARGE : TILE_PATH_CLEARANCE; }
    // How far, in world units, the nodes' tests reach past a clearance, so that a square or a
    // rectangle that only touches a node's (a float's rounding either way) closes it.
    static constexpr float TILE_HAIR = 1e-3f;

    // A static obstruction the tile pass keeps units off (CCmpObstructionManager::Rasterize: the
    // static shapes that BlockPathfinding): a rectangle centred on (x, z), hu along u = (ux, uz) and
    // hv along v = (-uz, ux), u = (1, 0) when uz is 0 (nav/footprint.h). key names it; a list is
    // sorted by key (Simulation::sync_tile_obstructions).
    struct TileObstruction {
        uint64_t key = 0;
        float x = 0.0f, z = 0.0f, ux = 1.0f, uz = 0.0f, hu = 0.0f, hv = 0.0f;
        bool operator==(const TileObstruction &o) const {
            return key == o.key && x == o.x && z == o.z && ux == o.ux && uz == o.uz && hu == o.hu && hv == o.hv;
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
        return true;
    }

    // The navcells a line passes through, in order, as Pathfinding::CheckLineMovement (Pathfinding.cpp)
    // walks them: from the navcell holding the start, it leaves each across the edge the line crosses
    // (along y at an exact corner) until it reaches the navcell holding the end. Unlike a Bresenham
    // line it misses no navcell the line touches inside, and never steps diagonally between two.
    // The ends are in navcells (0 to res); visit(x, y) is called for each until it returns false.
    template <class Visit>
    void walk_line(float fx0, float fy0, float fx1, float fy1, Visit &&visit) const {
        int x = std::clamp(static_cast<int>(std::floor(fx0)), 0, res - 1);
        int y = std::clamp(static_cast<int>(std::floor(fy0)), 0, res - 1);
        const int x1 = std::clamp(static_cast<int>(std::floor(fx1)), 0, res - 1);
        const int y1 = std::clamp(static_cast<int>(std::floor(fy1)), 0, res - 1);
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

    // The nodes: node_res() a side, node_step() world units apart, node a at node_pos(a).
    int node_res() const { return 2 * steep_tile_res(); }
    int node_words() const { return (node_res() + 63) / 64; }
    float node_step() const { return map_half / static_cast<float>(std::max(steep_tile_res(), 1)); }
    float node_pos(int a) const { return static_cast<float>(a) * node_step() - map_half; }
    // The node nearest the world coordinate v (x or z).
    int node_of(float v) const {
        return std::clamp(static_cast<int>(std::floor((v + map_half) / node_step() + 0.5f)), 0, std::max(node_res() - 1, 0));
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
        auto tile = [&](float v) { return std::clamp(static_cast<int>(std::floor((v + map_half) * tinv)), 0, tw - 1); };
        auto cell = [&](float v) { return std::clamp(static_cast<int>(std::floor((v + map_half) * winv)), 0, res - 1); };
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
        for (int b = 0; b < nw; ++b) {
            const TileSpan &zb = tile_span_[static_cast<size_t>(b)];
            if (!zb.in) continue;
            const float z = node_pos(b);
            const size_t j0 = static_cast<size_t>(zb.t0) * sw, j1 = static_cast<size_t>(zb.t1) * sw;
            const size_t k0 = static_cast<size_t>(zb.w0) * ww, k1 = static_cast<size_t>(zb.w1) * ww;
            uint8_t *row = open.data() + static_cast<size_t>(b) * static_cast<size_t>(nw);
            for (int a = 0; a < nw; ++a) {
                const TileSpan &xa = tile_span_[static_cast<size_t>(a)];
                if (!xa.in) continue;
                if (edge_r > 0.0f) {
                    const float x = node_pos(a);
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
        auto cell = [&](float v) { return std::clamp(static_cast<int>(std::floor((v + map_half) * inv)), 0, res - 1); };
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

    // Adds (delta 1) or takes off (delta -1) obstruction o's cover of the nodes of class cls, listing
    // in `crossed` each node whose count rose from 0 or fell to it. 0.28 blocks the navcells inside
    // the shape grown by the class Clearance (RasterizeRectWithClearance); a node is covered when the
    // obstruction grown by the clearance (and a hair) holds it. The shape is grown by the clearance
    // alone, as forest_cell_closed grows trees, not also by RasterizeRectWithClearance's
    // CLEARANCE_EXTENSION_RADIUS (1 m): on nodes twice as far apart as 0.28's navcells, that metre
    // would close gaps beside cliffs that 0.28 leaves open. Square with the map, that rectangle
    // (at least a node spacing wide: a narrower one is widened) can't lie across a step between two
    // nodes outside it, nor across the square of four, without holding one of them. A turned
    // rectangle's corner can, so a turned one also covers each node whose square of half a spacing
    // round it the rectangle reaches into: then each point of a step, or of a square of four open
    // nodes, lies in the square of one of them.
    void tile_obs_raster(const TileObstruction &o, int cls, int delta, std::vector<uint32_t> *crossed) {
        const int nw = node_res();
        if (nw <= 0) return;
        const float s = node_step(), hs = 0.5f * s, g = tile_clearance(cls) + TILE_HAIR;
        const bool turned = o.uz != 0.0f;
        float hu = o.hu + g, hv = o.hv + g;
        if (!turned) {
            hu = std::max(hu, hs + TILE_HAIR);
            hv = std::max(hv, hs + TILE_HAIR);
        }
        const float au = std::fabs(o.ux), av = std::fabs(o.uz);
        // The half sizes, along x and z, of the box round the nodes it may cover.
        const float ex = turned ? hu * au + hv * av + hs : hu, ez = turned ? hu * av + hv * au + hs : hv;
        const int a0 = std::max(0, static_cast<int>(std::ceil((o.x - ex + map_half) / s)));
        const int a1 = std::min(nw - 1, static_cast<int>(std::floor((o.x + ex + map_half) / s)));
        const int b0 = std::max(0, static_cast<int>(std::ceil((o.z - ez + map_half) / s)));
        const int b1 = std::min(nw - 1, static_cast<int>(std::floor((o.z + ez + map_half) / s)));
        std::vector<uint16_t> &count = tile_obs_count_[cls];
        for (int b = b0; b <= b1; ++b)
            for (int a = a0; a <= a1; ++a) {
                const float dx = node_pos(a) - o.x, dz = node_pos(b) - o.z;
                bool in;
                if (!turned) {
                    in = std::fabs(dx) < hu && std::fabs(dz) < hv;
                } else {
                    // The node's square against the rectangle: apart along x, z, u or v (the
                    // separating axis test of nav::footprint_overlaps_box).
                    const float lu = dx * o.ux + dz * o.uz, lv = dz * o.ux - dx * o.uz;
                    in = std::fabs(dx) < ex && std::fabs(dz) < ez && std::fabs(lu) < hu + hs * (au + av) &&
                         std::fabs(lv) < hv + hs * (au + av);
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

    // Calls f on the index of each bucket (tile_obs_buckets_) that obstruction o's box, grown by the
    // large clearance, reaches into.
    template <class F>
    void each_tile_obs_bucket(const TileObstruction &o, F &&f) const {
        const int bw = tile_obs_bw_;
        auto bucket = [&](float v) { return std::clamp(static_cast<int>(std::floor((v + map_half) / TILE_OBS_BUCKET)), 0, bw - 1); };
        const float e = std::max(o.hu, o.hv) * 1.4143f + TILE_PATH_CLEARANCE_LARGE + TILE_HAIR;
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
        auto bucket = [&](float v) { return std::clamp(static_cast<int>(std::floor((v + map_half) / TILE_OBS_BUCKET)), 0, bw - 1); };
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
    bool segment_hits_tile_obs(float ax, float az, float bx, float bz, float c) const {
        const float g = c + TILE_HAIR;
        return any_tile_obs_near_segment(ax, az, bx, bz, g, [&](const TileObstruction &o) { return tile_obs_segment_hit(o, ax, az, bx, bz, g); });
    }

    bool tile_open_cached() const {
        const int nw = node_res();
        const size_t n = static_cast<size_t>(nw) * static_cast<size_t>(nw);
        if (!tile_open_ready_ || tile_open_version_ != terrain_version_ || tile_open_res_ != res || tile_open_half_ != map_half ||
                tile_open_edge_r_ != edge_r || tile_cell_water_.size() != cells.data.size())
            return false;
        const size_t nb = static_cast<size_t>(nw) * static_cast<size_t>(node_words());
        for (int cls = 0; cls < TILE_CLASSES; ++cls)
            if (tile_open_[cls].size() != n || tile_region_[cls].size() != n || tile_obs_count_[cls].size() != n ||
                    tile_bits_rows_[cls].size() != nb || tile_bits_cols_[cls].size() != nb)
                return false;
        return true;
    }
    // Works the nodes out (rebuild_regions: all of them; rebuild_dirty_regions: those within the
    // large clearance of a dirty navcell whose deep water changed), as HierarchicalPathfinder::Update
    // redoes the dirty chunks. The steep tiles are the terrain's; the obstructions come and go
    // through set_tile_obstructions.
    void update_tile_open(bool all) {
        const int tw = steep_tile_res();
        if (tw <= res || cells.blank() || !any_steep_tile()) {
            tile_open_ready_ = false;
            for (int cls = 0; cls < TILE_CLASSES; ++cls) {
                tile_open_[cls].clear();
                tile_region_[cls].clear();
                tile_obs_count_[cls].clear();
                tile_bits_rows_[cls].clear();
                tile_bits_cols_[cls].clear();
            }
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
            std::vector<uint8_t> &open = tile_open_[cls];
            tile_closed_.clear();
            tile_opened_.clear();
            for (const uint32_t c : tile_cells_) {
                const int cx = static_cast<int>(c % static_cast<uint32_t>(res)), cy = static_cast<int>(c / static_cast<uint32_t>(res));
                const float x0 = static_cast<float>(cx) * cw - map_half - g, x1 = static_cast<float>(cx + 1) * cw - map_half + g;
                const float z0 = static_cast<float>(cy) * cw - map_half - g, z1 = static_cast<float>(cy + 1) * cw - map_half + g;
                const int a0 = std::max(0, static_cast<int>(std::floor((x0 + map_half) / s)));
                const int a1 = std::min(nw - 1, static_cast<int>(std::ceil((x1 + map_half) / s)));
                const int b0 = std::max(0, static_cast<int>(std::floor((z0 + map_half) / s)));
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
        if (list == old) return;
        if (tile_open_cached()) {
            const int nw = node_res();
            for (int cls = 0; cls < TILE_CLASSES; ++cls) {
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
        if (!tile_open_cached()) return 0;
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
        int x0 = static_cast<int>(std::floor(fx));
        int z0 = static_cast<int>(std::floor(fz));
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
        auto tile = [&](float v) { return std::clamp(static_cast<int>(std::floor((v + map_half) * inv)), 0, tw - 1); };
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
    bool find_path(float sx, float sz, float tx, float tz, WaypointPath &out, uint8_t pass = PASS_INFANTRY) const;
    bool find_path_on(float sx, float sz, float tx, float tz, WaypointPath &out, uint8_t pass) const;
    // find_path_on's search on the navcells alone, before its pass on the terrain tiles. It leaves
    // (tx, tz) where MakeGoalReachable moved the goal (ashore, into the start's region).
    bool find_path_cells(float sx, float sz, float &tx, float &tz, WaypointPath &out, uint8_t pass) const;

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
    float steep_sample_pad() const { return 0.25f * map_half / static_cast<float>(std::max(steep_tile_res(), 1)); }
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

    void clear() { count = 0; cur = 0; has_goal = false; }

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
inline bool NavGrid::find_path(float sx, float sz, float tx, float tz, WaypointPath &out, uint8_t pass) const {
    if (pass & PASS_LARGE)
        return find_path_on(sx, sz, tx, tz, out, pass) || find_path_on(sx, sz, tx, tz, out, pass & ~PASS_LARGE);
    if (!(pass & PASS_SHIP_CLEARANCE)) return find_path_on(sx, sz, tx, tz, out, pass);
    return find_path_on(sx, sz, tx, tz, out, pass) || find_path_on(sx, sz, tx, tz, out, PASS_SHIP);
}

inline bool NavGrid::find_path_on(float sx, float sz, float tx, float tz, WaypointPath &out, uint8_t pass) const {
    float gx = tx, gz = tz;
    const bool reached = find_path_cells(sx, sz, gx, gz, out, pass);
    if ((pass & PASS_SHIP) || cells.blank() || !tile_paths_on()) return reached;
    const int cls = tile_class(pass);
    // The class's clearance, grown by half a sample so a leg that clips a steep tile's corner between
    // two samples counts too.
    const float c = tile_clearance(cls) + steep_sample_pad();
    bool fine = near_steep_tile(gx, gz, c) || path_near_steep_tile(sx, sz, out, c) || path_through_obstruction_near_steep(sx, sz, out, cls);
    // A path that ends short of the goal (MakeGoalReachable moved it, or JPS stopped at the navcell
    // nearest it) when the goal's tile region is the start's: navcells steep by their averaged slope
    // can close a pass whose tiles are level, and 0.28's MakeGoalReachable, on navcells no coarser
    // than the tiles, leaves the goal where it is.
    if (!fine && (out.count == 0 || out.xs[out.count - 1] != tx || out.zs[out.count - 1] != tz)) {
        const uint32_t from = tile_region_at(sx, sz, tile_snap_reach(), cls);
        fine = from != 0 && from == tile_region_at(tx, tz, tile_snap_reach(), cls);
    }
    if (!fine) return reached;
    // The tiles take the goal as given: their own MakeGoalReachable moves it, not the navcells'.
    WaypointPath tiles;
    bool tiles_reached = false;
    if (!find_path_tiles(sx, sz, tx, tz, tiles, tiles_reached, cls)) return reached;
    out = tiles;
    return tiles_reached;
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
    snap_to_passable(tcx, tcy, tx, tz);
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
    const float step = 0.5f * map_half / static_cast<float>(std::max(steep_tile_res(), 1));
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
                if (!tile_obs_segment_hit(o, ax, az, bx, bz, g)) return false;
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

inline bool NavGrid::tile_line_clear(float ax, float az, float bx, float bz, int cls, bool live) const {
    const float c = tile_clearance(cls);
    // The terrain is tested every quarter tile, so the margin it keeps off is half a sample wider
    // than the clearance: no point between two samples comes within the clearance.
    if (segment_box_hits(ax, az, bx, bz, c + steep_sample_pad(), [&](float x0, float z0, float x1, float z1) {
            return steep_tile_in(x0, z0, x1, z1) || water_cell_in(x0, z0, x1, z1, live);
        }))
        return false;
    return !segment_hits_tile_obs(ax, az, bx, bz, c);
}

inline bool NavGrid::find_path_tiles(float sx, float sz, float tx, float tz, WaypointPath &out, bool &reached, int cls) const {
    const int tw = steep_tile_res();
    if (tw <= res || cells.blank()) return false;
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
        const int max_r = std::max(std::max(gi0, nw - 1 - gi0), std::max(gj0, nw - 1 - gj0));
        for (int r = 1; r <= max_r && (best_d < 0 || r * r <= best_d); ++r)
            for (int dj = -r; dj <= r; ++dj)
                for (int di = -r; di <= r; di += (dj == -r || dj == r || di == r) ? 1 : 2 * r) {
                    const int i = gi0 + di, j = gj0 + dj, d = di * di + dj * dj;
                    if (i < 0 || j < 0 || i >= nw || j >= nw) continue;
                    if (best_d >= 0 && (d > best_d || (d == best_d && (j > bj || (j == bj && i > bi))))) continue;
                    if (regions[at(i, j)] != want) continue;
                    best_d = d;
                    bi = i;
                    bj = j;
                }
        if (best_d < 0) return false;
        gi = bi;
        gj = bj;
    }
    // The path ends at the goal itself when it stays on its own node and a unit of the class can
    // stand there; else on the node it moved to.
    const float c = tile_clearance(cls);
    const bool goal_moved = gi != gi0 || gj != gj0 || !open(gi, gj) || !point_terrain_open(tx, tz, cls, live) ||
            segment_hits_tile_obs(tx, tz, tx, tz, c);
    thread_local nav::JpsBuffers jps;
    thread_local std::vector<std::pair<int, int>> path_cells;
    thread_local std::vector<std::pair<float, float>> pts, kept;
    // The search scans the open nodes' bits when they are kept (nav::JpsBits).
    nav::JpsBits bits;
    if (cached) {
        bits.row_words = bits.col_words = node_words();
        bits.rows = tile_bits_rows_[cls].data();
        bits.cols = tile_bits_cols_[cls].data();
    }
    reached = nav::compute_jps_path(nw, nw, open, si, sj, gi, gj, path_cells, nullptr, &jps, cached ? &bits : nullptr);
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
    auto passable = [this, pass](int x, int y) { return is_passable_for(x, y, pass); };
    thread_local nav::JpsBuffers jps; // find_path runs on the worker threads: one search state each
    return nav::compute_jps_path(res, res, passable, scx, scy, tcx, tcy, path_cells, nullptr, &jps);
}

} // namespace rtso

#endif // RTSO_GRID_NAV_H
