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
//                 elephants, chariots, wagons, catafalques), Clearance 3 m. Trees grown by that
//                 clearance close more of a wood (Simulation::forest_cell_closed), and those
//                 navcells (large_closed) block it too. Like PASS_SHIP_CLEARANCE, only long paths
//                 use it, falling back to the default class when it finds no way; regions stay
//                 the default class's.
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
        if (!dirty.blank()) dirty.fill(0);
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
// NavGrid::search_cells_jps — Jump Point Search (nav/long_pathfinder.h), the
// port of 0 A.D. LongPathfinder::ComputeJPSPath. Emits start, jump points and
// goal; on failure, the path to the closest explored cell (iBest/jBest).
// ---------------------------------------------------------------------------
inline bool NavGrid::search_cells_jps(int scx, int scy, int tcx, int tcy, uint8_t pass,
        std::vector<std::pair<int,int>> &path_cells) const {
    auto passable = [this, pass](int x, int y) { return is_passable_for(x, y, pass); };
    return nav::compute_jps_path(res, res, passable, scx, scy, tcx, tcy, path_cells);
}

} // namespace rtso

#endif // RTSO_GRID_NAV_H
