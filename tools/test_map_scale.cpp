// RTS0 map-size test (headless: no SCons, no Godot).
// Checks that the nav, fog-of-war and territory grids follow the map size, so
// 0 A.D. map sizes from Tiny (128 units) to Giant (512 units) work end to end.
// Build and run:
//   g++ -std=c++20 -O2 -Isrc tools/test_map_scale.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp -o /tmp/test_map_scale && /tmp/test_map_scale
//
//  1. grid_res_for_map keeps 2m cells: 64 cells on the default 128m map, 256 on 512m.
//  2. Every random map type generates at every 0 A.D. size with matching nav and
//     FoW grids, and paths between town centres stay on the map. On the land
//     maps modelled on 0 A.D. (Mainland onwards) every base reaches every other
//     (by sea on the naval maps).
//  3. Map JSON loading (reset, then set_map_half) re-initialises the nav grid for
//     the new extent, so a path across a 256m map reaches its far corner.
//  4. A unit near the far corner of a Giant map lights up the FoW there.
//  4b. The FoW viewer (a multiplayer joiner's slot) gets its own fog, shared within its team.
//  4c. Explored and revealed map options.
//  5. Generation is deterministic: the same seed gives the same map at every size.
//  6. Stronghold keeps teammates together.
//  6b. Team Placement (the 21 random maps whose 0.28 JSON lists PlayerPlacements): each
//      offered pattern places every player, keeps teammates together and every base
//      reachable, and the setup option survives the game-start attributes.
//  7. Shallow water is walkable and sailable; deep water is ships only.
//  8. Red Sea and Hellas are painted from 0 A.D.'s heightmap images.
//  9. English Channel splits the players by sea; Fortress walls every base.
// 10. Random maps paint 0 A.D. per-tile terrains (water, forest floor, road) and the
//     map JSON tile encoding decodes; bad tiles are refused.
// 10b. Random maps plant their biome's rmbiome trees and fruit (FloraKind), forests
//     and stragglers alike, and the map JSON plant names resolve. Each tree holds its
//     template's wood (savanna's baobabs 600, india's banyans 600).
// 10c. Maps from 0 A.D. scripts with their own terrains paint and plant only those
//     (script_terrains.h), with their templates' wood, unless game setup overrides the biome.
//  Exit: 0 = PASS, 1 = FAIL.
#include "simulation/simulation.h"
#include "simulation/resource_counts_data.h"
#include "simulation/rms_generator.h"
#include "network/net_types.h"
#include "simulation/script_terrains.h"

#include <algorithm>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <set>
#include <vector>

using namespace rtso;

static int g_failures = 0;
#define CHECK(cond, ...)                                    \
	do {                                                    \
		if (!(cond)) {                                      \
			std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
			std::printf(__VA_ARGS__);                       \
			std::printf("\n");                              \
			++g_failures;                                   \
		}                                                   \
	} while (0)

// Whether land (or ship) units at a can reach b, by the nav grid's region
// labels: find_path itself returns the nearest reachable spot instead of
// failing (HierarchicalPathfinder MakeGoalReachable). Starts and goals are
// snapped to a passable cell within 4, as find_path does.
static std::pair<int, int> snap_cell(const NavGrid &g, std::pair<int, int> c, uint8_t pass) {
	for (int r = 0; r <= 4; ++r)
		for (int dy = -r; dy <= r; ++dy)
			for (int dx = -r; dx <= r; ++dx)
				if (g.is_passable_for(c.first + dx, c.second + dy, pass)) return {c.first + dx, c.second + dy};
	return c;
}

static bool can_reach(const NavGrid &g, float ax, float az, float bx, float bz, uint8_t pass) {
	const auto a = snap_cell(g, g.world_to_cell(ax, az), pass), b = snap_cell(g, g.world_to_cell(bx, bz), pass);
	return g.is_passable_for(a.first, a.second, pass) && g.is_passable_for(b.first, b.second, pass) &&
	       g.reachable(a.first, a.second, b.first, b.second, pass);
}

// Ship regions bordering the land that units at (x, z) can walk to.
static std::vector<uint16_t> shore_ship_regions(const NavGrid &g, float x, float z) {
	const auto s = snap_cell(g, g.world_to_cell(x, z), PASS_INFANTRY);
	std::vector<uint16_t> out;
	if (!g.is_passable_for(s.first, s.second, PASS_INFANTRY)) return out;
	const uint16_t land = g.global_region_at(s.first, s.second, PASS_INFANTRY);
	for (int y = 0; y < g.res; ++y)
		for (int cx = 0; cx < g.res; ++cx) {
			if (!g.is_passable_for(cx, y, PASS_SHIP)) continue;
			bool shore = false;
			for (int dy = -1; dy <= 1 && !shore; ++dy)
				for (int dx = -1; dx <= 1 && !shore; ++dx)
					shore = g.is_passable_for(cx + dx, y + dy, PASS_INFANTRY) &&
					        g.global_region_at(cx + dx, y + dy, PASS_INFANTRY) == land;
			if (!shore) continue;
			const uint16_t r = g.global_region_at(cx, y, PASS_SHIP);
			if (std::find(out.begin(), out.end(), r) == out.end()) out.push_back(r);
		}
	return out;
}

static const int kSizes[] = {128, 192, 256, 320, 384, 448, 512};

static uint64_t map_fingerprint(const Simulation &sim) {
	uint64_t h = 1469598103934665603ull;
	auto mix = [&h](const void *p, size_t n) {
		const unsigned char *b = (const unsigned char *)p;
		for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
	};
	mix(sim.terrain_heights().data(), sim.terrain_heights().size() * sizeof(float));
	auto r = sim.resources();
	mix(r.x, r.count * sizeof(float));
	mix(r.z, r.count * sizeof(float));
	mix(sim.nav_grid().cells.data.data(), sim.nav_grid().cells.data.size());
	return h;
}

int main() {
	// 1. Grid resolution.
	CHECK(Simulation::grid_res_for_map(64.0f) == 64, "128m map -> %d", Simulation::grid_res_for_map(64.0f));
	CHECK(Simulation::grid_res_for_map(96.0f) == 96, "192m map -> %d", Simulation::grid_res_for_map(96.0f));
	CHECK(Simulation::grid_res_for_map(256.0f) == 256, "512m map -> %d", Simulation::grid_res_for_map(256.0f));
	CHECK(Simulation::grid_res_for_map(10.0f) == Simulation::MIN_GRID_RES, "tiny clamps up");
	CHECK(Simulation::grid_res_for_map(2000.0f) == Simulation::MAX_GRID_RES, "huge clamps down");
	CHECK(Simulation::grid_res_for_map(100.0f) % CHUNK_SIZE == 0, "res is whole chunks");

	// 2. Every map type at every size.
	int paths = 0, truncated = 0, steep_mines = 0;
	for (int mt = 0; mt < MAP_TYPE_COUNT; ++mt) {
		for (int size : kSizes) {
			Simulation sim;
			const int players = (size >= 384) ? 4 : 2;
			sim.generate_random_map((MapType)mt, 1234, players, size * 0.5f);
			const int res = Simulation::grid_res_for_map(size * 0.5f);
			CHECK(sim.fow_res() == res, "%s %d: fow_res %d != %d", map_type_info(mt).id, size, sim.fow_res(), res);
			CHECK(sim.nav_grid().res == res, "%s %d: nav res %d != %d", map_type_info(mt).id, size, sim.nav_grid().res, res);
			CHECK(std::fabs(sim.nav_grid().map_half - size * 0.5f) < 1e-3f, "%s %d: nav map_half %.1f",
			      map_type_info(mt).id, size, sim.nav_grid().map_half);

			std::vector<std::pair<float, float>> tcs;
			for (const auto &b : sim.buildings())
				if (b.type == BLD_TOWN_CENTER && b.team < MAX_PLAYERS) tcs.push_back({b.x, b.z}); // not Danubius' Gaia villages
			// wall_demo.js places walls alone, no player entities (test_wall_demo checks them).
			CHECK((int)tcs.size() >= 2 || mt == MAP_WALL_DEMO, "%s %d: %zu town centres", map_type_info(mt).id, size, tcs.size());
			// new_rms_test.js places the player bases alone: no resources or animals.
			if (mt == MAP_NEW_RMS_TEST)
				CHECK(sim.resources().count == 0 && sim.fauna().count == 0 && (int)tcs.size() == players,
				      "new_rms_test %d: %zu resources, %zu animals, %zu town centres", size, sim.resources().count, sim.fauna().count, tcs.size());
			for (size_t i = 0; i < tcs.size(); ++i) {
				for (size_t j = 0; j < tcs.size(); ++j) {
					if (i == j) continue;
					// Start and end just outside the TC footprint.
					float sx = tcs[i].first + 6.0f, sz = tcs[i].second + 6.0f;
					float tx = tcs[j].first + 6.0f, tz = tcs[j].second + 6.0f;
					WaypointPath wp;
					sim.nav_grid().find_path(sx, sz, tx, tz, wp, PASS_INFANTRY);
					const bool reached = can_reach(sim.nav_grid(), sx, sz, tx, tz, PASS_INFANTRY);
					++paths;
					// Every land map must connect every base on foot (Continental
					// once split into one island per player); on naval maps the
					// bases share a sea instead. Survival of the Fittest walls each base
					// in on purpose (0 A.D.'s script: only the Gaia waves cross).
					if (!map_is_naval(mt) && mt != MAP_SURVIVAL_OF_THE_FITTEST)
						CHECK(reached, "%s %d: no land path from base %zu to base %zu", map_type_info(mt).id, size, i, j);
					if (map_is_naval(mt) && !reached) {
						const auto ra = shore_ship_regions(sim.nav_grid(), sx, sz), rb = shore_ship_regions(sim.nav_grid(), tx, tz);
						bool shared = false;
						for (uint16_t r : ra) shared |= std::find(rb.begin(), rb.end(), r) != rb.end();
						CHECK(shared, "%s %d: bases %zu and %zu share no sea", map_type_info(mt).id, size, i, j);
					}
					if (wp.count >= WaypointPath::MAX_WP - 1) ++truncated;
					// Island maps are allowed to be unreachable on foot; the path
					// must still lie inside the map.
					for (int k = 0; k < wp.count; ++k) {
						CHECK(std::fabs(wp.xs[k]) <= size * 0.5f && std::fabs(wp.zs[k]) <= size * 0.5f,
						      "%s %d: waypoint (%.1f, %.1f) off map", map_type_info(mt).id, size, wp.xs[k], wp.zs[k]);
					}
				}
			}
			// 2b. Every mine and bush stands on, and every tree beside, a terrain tile land
			// units can stand on (0.28's passability: corner heights over the tile within the
			// max slope; RmsGenerator::keep_resources_passable), the bases' mines always.
			const auto &H = sim.terrain_heights();
			const int hr = sim.terrain_res(), tr = hr - 1;
			const float tile = (float)size / (float)tr;
			auto steep = [&](float x, float z) {
				const int i = std::clamp((int)std::floor((x + size * 0.5f) / tile), 0, tr - 1);
				const int j = std::clamp((int)std::floor((z + size * 0.5f) / tile), 0, tr - 1);
				const float a = H[(size_t)j * hr + i], b = H[(size_t)j * hr + i + 1], c = H[(size_t)(j + 1) * hr + i], d = H[(size_t)(j + 1) * hr + i + 1];
				return (std::max(std::max(a, b), std::max(c, d)) - std::min(std::min(a, b), std::min(c, d))) / tile > LAND_MAX_TERRAIN_SLOPE;
			};
			const auto rv = sim.resources();
			int on_steep = 0;
			for (size_t r = 0; r < rv.count; ++r) {
				if (!rv.alive[r] || (rv.type[r] != RES_GOLD && rv.type[r] != RES_STONE)) continue;
				if (!steep(rv.x[r], rv.z[r])) continue;
				++on_steep;
				for (const auto &t : tcs)
					CHECK(std::hypot(rv.x[r] - t.first, rv.z[r] - t.second) > 16.0f, "%s %d: a base mine at (%.1f, %.1f) on a cliff tile",
					      map_type_info(mt).id, size, rv.x[r], rv.z[r]);
			}
			steep_mines += on_steep;
			// 2c. Nothing starts in a circular map's impassable edge ring (rmgen MAP_BORDER_WIDTH).
			if (sim.circular_map()) {
				int ringed = 0;
				for (size_t r = 0; r < rv.count; ++r) ringed += rv.alive[r] && sim.past_map_edge(rv.x[r], rv.z[r]);
				const auto fv = sim.fauna();
				for (size_t f = 0; f < fv.count; ++f) ringed += fv.alive[f] && sim.past_map_edge(fv.pos_x[f], fv.pos_z[f]);
				CHECK(ringed == 0, "%s %d: %d animals and resources start past the circle's edge", map_type_info(mt).id, size, ringed);
			}
		}
	}
	std::printf("paths between town centres: %d, hit the %d-waypoint cap: %d; mines on cliff tiles: %d\n", paths, WaypointPath::MAX_WP, truncated, steep_mines);

	// 3. JSON map flow (main_node.cpp load_map): reset, set_map_half, terrain,
	// then buildings inside begin/end_bulk_spawn.
	{
		auto load = [](Simulation &sim, bool bulk) {
			sim.reset();
			sim.set_map_half(128.0f);
			std::vector<float> hm(64 * 64, 2.0f);
			for (int z = 0; z < 64; ++z)
				for (int x = 40; x < 48; ++x) hm[z * 64 + x] = (z < 20) ? -3.0f : 2.0f; // a lake
			sim.set_terrain_data(hm, 64, 0.0f);
			if (bulk) sim.begin_bulk_spawn();
			for (int i = 0; i < 60; ++i)
				sim.spawn_building_world(-100.0f + (float)(i % 10) * 9.0f, -40.0f + (float)(i / 10) * 9.0f,
				                         (i % 7 == 0) ? BLD_TOWER : BLD_HOUSE, (uint8_t)(i % 2), 1.0f);
			if (bulk) sim.end_bulk_spawn();
		};
		Simulation sim, one_by_one;
		load(sim, true);
		load(one_by_one, false);
		CHECK(sim.nav_grid().cells.data == one_by_one.nav_grid().cells.data, "bulk spawn changes the nav grid");
		int water = 0;
		for (uint8_t c : sim.nav_grid().cells.data) water += (c == NAV_WATER);
		CHECK(water > 0, "lake missing from nav grid after bulk spawn");
		WaypointPath wp;
		bool ok = sim.nav_grid().find_path(-120.0f, -120.0f, 120.0f, 120.0f, wp, PASS_INFANTRY);
		CHECK(ok && wp.count > 0, "path across a 256m map failed");
		if (wp.count > 0) {
			float ex = wp.xs[wp.count - 1], ez = wp.zs[wp.count - 1];
			CHECK(std::hypot(ex - 120.0f, ez - 120.0f) < 2.0f, "path ends at (%.1f, %.1f), not (120, 120)", ex, ez);
		}
		CHECK(sim.fow_res() == 128, "256m map fow_res %d", sim.fow_res());
	}

	// 4. FoW at the far corner of a Giant map.
	{
		Simulation sim;
		sim.generate_random_map(MAP_RANDOM_CONTINENTAL, 99, 2, 256.0f);
		const float x = 230.0f, z = 230.0f;
		CHECK(sim.fow_state_at(x, z) != Simulation::FOW_VISIBLE, "corner visible before any unit");
		sim.spawn_unit(x, z, 0, UNIT_SPEARMAN);
		for (int i = 0; i < 10; ++i) sim.tick(Simulation::FIXED_STEP);
		CHECK(sim.fow_state_at(x, z) == Simulation::FOW_VISIBLE, "unit at (%.0f, %.0f) does not reveal its cell", x, z);
		CHECK(sim.fow_state_at(x - 40.0f, z - 40.0f) != Simulation::FOW_VISIBLE, "cell 56m away visible");
	}

	// 4b. Per-player fog: the viewer (a joiner's slot) sees through its own units
	// and its game-setup team's, and player 0's (hashed) view is unaffected.
	{
		Simulation sim;
		sim.generate_random_map(MAP_MAINLAND, 7, 4, 128.0f);
		const float x = -80.0f, z = 80.0f; // inside Mainland's circle
		sim.spawn_unit(x, z, 2, UNIT_SPEARMAN);
		sim.spawn_unit(-x, -z, 3, UNIT_SPEARMAN);
		sim.set_player_team(2, 5);
		sim.set_player_team(3, 5);
		sim.player(2).techs.set(TECH_UNLOCK_SHARED_LOS); // Cartography: allies' vision
		sim.set_fow_viewer(2);
		for (int i = 0; i < 10; ++i) sim.tick(Simulation::FIXED_STEP);
		CHECK(sim.fow_state_at(x, z, 2) == Simulation::FOW_VISIBLE, "player 2 can't see its own unit");
		CHECK(sim.fow_state_at(-x, -z, 2) == Simulation::FOW_VISIBLE, "player 2 can't see its team-mate's unit");
		CHECK(sim.fow_state_at(x, z, 0) != Simulation::FOW_VISIBLE, "player 0 sees player 2's unit");
		CHECK(sim.fow_state_at(x, z, 1) != Simulation::FOW_VISIBLE, "player 1 sees player 2's unit");
		CHECK(sim.is_entity_visible_to_viewer(-x, -z, 3, 2), "team-mate's unit hidden from player 2");
		CHECK(sim.fow_grid(6)[0] == Simulation::FOW_UNEXPLORED, "player 6 (not the viewer) has explored cells");
	}

	// 4c. Game-setup visibility: "Explored map" knows the terrain but not what moves there;
	// "Revealed map" shows everyone everything without the per-cell fog work.
	{
		Simulation sim;
		sim.generate_random_map(MAP_MAINLAND, 7, 4, 128.0f);
		sim.spawn_unit(-80.0f, 80.0f, 2, UNIT_SPEARMAN); // inside Mainland's circle
		sim.set_explore_map(true);
		sim.set_fow_viewer(2);
		for (int i = 0; i < 10; ++i) sim.tick(Simulation::FIXED_STEP);
		CHECK(sim.fow_state_at(80.0f, -80.0f, 0) == Simulation::FOW_EXPLORED, "explored map: far cell not explored for player 0");
		CHECK(sim.fow_state_at(80.0f, -80.0f, 2) == Simulation::FOW_EXPLORED, "explored map: far cell not explored for the viewer");
		CHECK(sim.fow_state_at(-80.0f, 80.0f, 2) == Simulation::FOW_VISIBLE, "explored map: viewer can't see its own unit");
		CHECK(!sim.is_entity_visible_to_viewer(-80.0f, 80.0f, 2, 0), "explored map shows player 0 an enemy unit");
		CHECK(sim.fow_state_at(120.0f, -120.0f, 2) == Simulation::FOW_UNEXPLORED, "explored map explores off the circular map");
	}
	{
		Simulation sim;
		sim.generate_random_map(MAP_MAINLAND, 7, 4, 128.0f);
		sim.set_reveal_map(true);
		sim.set_fow_viewer(3);
		for (int i = 0; i < 10; ++i) sim.tick(Simulation::FIXED_STEP);
		// Mainland is circular: everything inside the circle (m_LosStateRevealed), nothing off it.
		const int res = sim.fow_res();
		const float cell = 2.0f * sim.map_half() / (float)res;
		bool all0 = true, all3 = true;
		for (int i = 0; i < res * res; ++i) {
			const float cx = -sim.map_half() + ((float)(i % res) + 0.5f) * cell, cz = -sim.map_half() + ((float)(i / res) + 0.5f) * cell;
			const uint8_t want = sim.off_world(cx, cz) ? Simulation::FOW_UNEXPLORED : Simulation::FOW_VISIBLE;
			all0 &= sim.fow_grid(0)[i] == want;
			all3 &= sim.fow_grid(3)[i] == want;
		}
		CHECK(all0 && all3, "revealed map: some in-world cell is not visible, or an off-world one is");
		CHECK(sim.map_scale_stats().fow_calls > 0, "fow never ticked");
	}

	// 5. Determinism.
	for (int size : kSizes) {
		Simulation a, b;
		a.generate_random_map(MAP_DANUBIUS, 77, 2, size * 0.5f);
		b.generate_random_map(MAP_DANUBIUS, 77, 2, size * 0.5f);
		CHECK(map_fingerprint(a) == map_fingerprint(b), "size %d: same seed, different map", size);
	}

	// 6. Stronghold packs teammates together (game-setup teams).
	{
		const uint8_t teams[4] = {1, 2, 1, 2};
		Simulation sim;
		sim.generate_random_map(MAP_STRONGHOLD, 99, 4, 128.0f, teams);
		float tx[8] = {}, tz[8] = {};
		bool has[8] = {};
		for (const auto &b : sim.buildings())
			if (b.type == BLD_TOWN_CENTER && b.team < 8) { has[b.team] = true; tx[b.team] = b.x; tz[b.team] = b.z; }
		CHECK(has[0] && has[1] && has[2] && has[3], "stronghold: a player has no town centre");
		const float mate = std::hypot(tx[0] - tx[2], tz[0] - tz[2]), foe = std::hypot(tx[0] - tx[1], tz[0] - tz[1]);
		CHECK(mate < foe, "stronghold: teammates %.1f apart, enemies %.1f", mate, foe);
		CHECK(mate >= 17.0f, "stronghold: teammates' bases overlap (%.1f apart)", mate);
	}

	// 6b. Team Placement (0 A.D. PlayerPlacement on every random map whose JSON lists it).
	int placement_maps = 0;
	for (int type = 0; type < MAP_TYPE_COUNT; ++type) {
		const MapType mt = (MapType)type;
		const uint8_t offered = map_team_placements(mt);
		if (!offered) continue;
		++placement_maps;
		const uint8_t teams[4] = {1, 2, 1, 2};
		const char *name = map_type_info(mt).id;
		std::set<std::vector<int>> layouts;
		int patterns = 0;
		for (int tp = TEAM_PLACEMENT_RADIAL; tp < TEAM_PLACEMENT_COUNT; ++tp) {
			if (!((offered >> tp) & 1)) continue;
			++patterns;
			Simulation sim;
			sim.set_team_placement(tp);
			sim.generate_random_map(mt, 31, 4, 128.0f, teams);
			float tx[8] = {}, tz[8] = {};
			bool has[8] = {};
			for (const auto &b : sim.buildings())
				if (is_civic_centre(b.type) && b.team < 8) { has[b.team] = true; tx[b.team] = b.x; tz[b.team] = b.z; }
			CHECK(has[0] && has[1] && has[2] && has[3], "%s placement %d: a player has no civic centre", name, tp);
			auto d = [&](int a, int b) { return std::hypot(tx[a] - tx[b], tz[a] - tz[b]); };
			const float mate = std::max(d(0, 2), d(1, 3));
			const float foe = std::min({d(0, 1), d(0, 3), d(2, 1), d(2, 3)});
			if (tp == TEAM_PLACEMENT_RANDOM_GROUP || tp == TEAM_PLACEMENT_RIVER) {
				// groupPlayersByArea: of the spots, teammates get the pairing
				// with the least distance between allies.
				const float pairs = d(0, 2) + d(1, 3);
				CHECK(pairs <= d(0, 1) + d(2, 3) + 1.0f && pairs <= d(0, 3) + d(1, 2) + 1.0f,
				      "%s placement %d: teammates %.1f apart in all, another pairing is closer", name, tp, pairs);
			} else if (tp != TEAM_PLACEMENT_RADIAL) {
				CHECK(mate < foe, "%s placement %d: teammates %.1f apart, nearest enemies %.1f", name, tp, mate, foe);
			}
			// Every base reaches every other on foot, except on the island maps.
			if (!map_is_naval(mt))
				for (int a = 1; a < 4; ++a)
					CHECK(can_reach(sim.nav_grid(), tx[0], tz[0], tx[a], tz[a], PASS_INFANTRY),
					      "%s placement %d: player %d can't walk to player 1", name, tp, a + 1);
			std::vector<int> l;
			for (int p = 0; p < 4; ++p) { l.push_back((int)tx[p]); l.push_back((int)tz[p]); }
			layouts.insert(l);
		}
		CHECK((int)layouts.size() == patterns, "%s: the %d placements gave %zu layouts", name, patterns, layouts.size());
	}
	CHECK(placement_maps == 25, "%d random maps offer Team Placement", placement_maps);
	{
		GameSetupParams p, q;
		p.map_id = "random_frontier";
		p.team_placement = TEAM_PLACEMENT_RIVER;
		CHECK(parse_game_start_full(format_game_start_full(p), q) && q.team_placement == TEAM_PLACEMENT_RIVER,
		      "Team Placement does not survive the game-start attributes");
	}

	// 7. Shallow water (0 A.D. pathfinder.xml MaxWaterDepth 2): land units wade
	// through it, ships sail it; deeper water stays ships only.
	{
		Simulation sim;
		sim.generate_random_map(MAP_RHINE_MARSHLANDS, 5, 2, 128.0f);
		const auto &g = sim.nav_grid();
		int shallow = 0, deep = 0;
		bool shallow_ok = true;
		for (int y = 0; y < g.res; ++y)
			for (int x = 0; x < g.res; ++x) {
				const uint8_t c = g.cells.at(x, y);
				if (c == NAV_SHALLOW) {
					++shallow;
					shallow_ok &= g.is_passable_for(x, y, PASS_INFANTRY) && g.is_passable_for(x, y, PASS_SHIP);
				}
				deep += c == NAV_WATER;
			}
		CHECK(shallow > 0, "rhine_marshlands: no shallow water");
		CHECK(deep == 0, "rhine_marshlands: %d deep water cells", deep);
		CHECK(shallow_ok, "shallow water is not walkable and sailable");
		Simulation sea;
		sea.generate_random_map(MAP_RANDOM_ARCHIPELAGO, 5, 2, 128.0f);
		bool deep_ok = true;
		int sea_deep = 0;
		for (int y = 0; y < sea.nav_grid().res; ++y)
			for (int x = 0; x < sea.nav_grid().res; ++x)
				if (sea.nav_grid().cells.at(x, y) == NAV_WATER) {
					++sea_deep;
					deep_ok &= !sea.nav_grid().is_passable_for(x, y, PASS_INFANTRY) && sea.nav_grid().is_passable_for(x, y, PASS_SHIP);
				}
		CHECK(sea_deep > 0 && deep_ok, "deep water must be ships only (%d cells)", sea_deep);
	}

	// 8. Heightmap maps (rmgen LoadHeightmapImage / HeightmapPainter).
	for (int size : {128, 512}) {
		const float half = size * 0.5f;
		Simulation sim;
		sim.generate_random_map(MAP_RED_SEA, 3, 4, half);
		const float wh = sim.water_height();
		// red_sea.png pixel (111, 145) is mid Red Sea, (162, 102) inland Arabia.
		auto at_px = [&](float px, float py) {
			return sim.get_terrain_height(-half + px / 255.0f * size, -half + py / 255.0f * size);
		};
		CHECK(at_px(111.0f, 145.0f) < wh, "red_sea %d: the Red Sea is dry (%.2f)", size, at_px(111.0f, 145.0f));
		CHECK(at_px(162.0f, 102.0f) > wh + 0.3f, "red_sea %d: Arabia is under water (%.2f)", size, at_px(162.0f, 102.0f));
		Simulation hel;
		hel.generate_random_map(MAP_HELLAS, 3, 4, half);
		int land = 0, cells = 0;
		float top = -1e9f;
		for (float h : hel.terrain_heights()) {
			++cells;
			land += h > hel.water_height() + 0.3f;
			top = std::max(top, h);
		}
		// The land-ratio checks pick mostly land; heights reach 100 m x (size + 1) / 321.
		CHECK(land > cells / 2, "hellas %d: only %d of %d cells are land", size, land, cells);
		CHECK(top > hel.water_height() + 3.0f, "hellas %d: flat (top %.1f)", size, top);
	}

	// 9. Naval and walled maps.
	{
		Simulation sim;
		sim.generate_random_map(MAP_ENGLISH_CHANNEL, 11, 2, 128.0f);
		std::vector<std::pair<float, float>> tcs;
		for (const auto &b : sim.buildings())
			if (b.type == BLD_TOWN_CENTER) tcs.push_back({b.x + 6.0f, b.z + 6.0f});
		CHECK(tcs.size() == 2 && !can_reach(sim.nav_grid(), tcs[0].first, tcs[0].second, tcs[1].first, tcs[1].second, PASS_INFANTRY),
		      "english_channel: the channel can be crossed on foot");
		Simulation fort;
		fort.generate_random_map(MAP_FORTRESS, 11, 4, 128.0f);
		int walls = 0, gates = 0, towers = 0, kinds[BLD_TYPE_COUNT] = {};
		for (const auto &b : fort.buildings()) {
			walls += b.type == BLD_WALL_SEGMENT;
			gates += b.type == BLD_GATE;
			towers += b.type == BLD_WALL_TOWER;
			++kinds[b.type];
		}
		// The Spahbod: 10 long walls, 2 gates, 8 towers and 4 corner towers each.
		CHECK(walls == 4 * 10 && gates == 4 * 2 && towers == 4 * 12, "fortress: %d walls, %d gates, %d towers", walls, gates, towers);
		CHECK(kinds[BLD_MARKET] == 4 && kinds[BLD_BARRACKS] == 4 && kinds[BLD_STABLE] == 4 && kinds[BLD_TEMPLE] == 4,
		      "fortress: each player gets a market, barracks, stable and temple");
		// The gates (unlocked) let units out of the walls.
		std::vector<std::pair<float, float>> ftc;
		for (const auto &b : fort.buildings())
			if (b.type == BLD_TOWN_CENTER) ftc.push_back({b.x + 6.0f, b.z + 6.0f});
		CHECK(ftc.size() == 4 && can_reach(fort.nav_grid(), ftc[0].first, ftc[0].second, ftc[1].first, ftc[1].second, PASS_INFANTRY),
		      "fortress: no way out through the gates");
	}

	// 10. Per-tile terrain (0 A.D. CTerrain tiles).
	for (int size : kSizes) {
		Simulation sim;
		sim.generate_random_map(MAP_RANDOM_ARCHIPELAGO, 12, 2, size * 0.5f);
		const auto &t = sim.terrain_tiles();
		CHECK(t.res == size && t.tiles.size() == (size_t)size * size && !t.names.empty(), "size %d: %d tiles a side", size, t.res);
		const TerrainBiome &tb = terrain_biome(sim.current_map_biome().c_str());
		auto name_at = [&](float x, float z) { const int i = sim.terrain_tile_at(x, z); return i < 0 ? std::string() : t.names[i]; };
		int under_water = 0, water_ok = 0, forest = 0, forest_ok = 0, roads = 0;
		for (int z = -size / 2 + 1; z < size / 2; z += 2)
			for (int x = -size / 2 + 1; x < size / 2; x += 2)
				if (sim.get_terrain_height((float)x + 0.5f, (float)z + 0.5f) < sim.water_height() - 0.05f) {
					++under_water;
					water_ok += name_at((float)x + 0.5f, (float)z + 0.5f) == tb.water;
				}
		const auto rv = sim.resources();
		for (size_t i = 0; i < rv.count; ++i)
			if (rv.alive[i] && rv.type[i] == RES_WOOD) {
				++forest;
				const std::string n = name_at(rv.x[i], rv.z[i]);
				forest_ok += n == tb.forest[0] || (tb.forest[1] && n == tb.forest[1]) ||
				             n == tb.cliff[0] || (tb.cliff[1] && n == tb.cliff[1]) || n == tb.hill[0] || (tb.hill[1] && n == tb.hill[1]) ||
				             n == tb.shore || n == tb.water || n == tb.road_wild || n == tb.road; // base woodlines stand in the city patch
			}
		for (const auto &b : sim.buildings())
			if (b.type == BLD_TOWN_CENTER) roads += name_at(b.x, b.z) == tb.road;
		CHECK(under_water > 0 && water_ok == under_water, "size %d: %d of %d tiles under water painted %s", size, water_ok, under_water, tb.water);
		CHECK(forest > 0 && forest_ok >= forest * 9 / 10, "size %d: %d of %d trees stand on forest floor", size, forest_ok, forest);
		CHECK(roads == 2, "size %d: %d civic centres on the road terrain", size, roads);
		// Names in rmgen paint order (the renderer's blend priority): main terrain under
		// water, roads, cliffs and forests.
		auto index_of = [&](const char *n) {
			const auto it = n ? std::find(t.names.begin(), t.names.end(), std::string(n)) : t.names.end();
			return it == t.names.end() ? -1 : (int)(it - t.names.begin());
		};
		const int main_i = index_of(tb.main[0]);
		bool ordered = main_i >= 0;
		for (const char *n : {tb.water, tb.road, tb.cliff[0], tb.forest[0]})
			if (index_of(n) >= 0 && n && std::strcmp(n, tb.main[0]) != 0) ordered = ordered && index_of(n) > main_i;
		CHECK(ordered, "size %d: the main terrain has the lowest blend priority", size);
		Simulation again;
		again.generate_random_map(MAP_RANDOM_ARCHIPELAGO, 12, 2, size * 0.5f);
		CHECK(again.terrain_tiles().tiles == t.tiles && again.terrain_tiles().names == t.names, "size %d: terrain differs for one seed", size);
	}
	// 10b. Biome flora (rmbiome tree1-tree5 and fruitBush).
	for (int biome = 0; biome < BIOME_COUNT; ++biome) {
		Simulation sim;
		sim.set_biome_override(biome);
		sim.generate_random_map(MAP_MAINLAND, 7, 2, 128.0f);
		const BiomeFlora *f = biome_flora(sim.current_map_biome().c_str());
		CHECK(f != nullptr, "biome %s has 0 A.D. flora", sim.current_map_biome().c_str());
		if (!f) continue;
		bool allowed[FLORA_COUNT] = {};
		for (const auto &o : f->tree12) { allowed[o[0]] = allowed[o[1]] = true; }
		for (const auto &o : f->tree45) { allowed[o[0]] = allowed[o[1]] = true; }
		for (uint8_t k : f->tree3) allowed[k] = true;
		allowed[FLORA_DEFAULT] = false;
		int trees = 0, trees_ok = 0, fruit = 0, fruit_ok = 0;
		bool seen[FLORA_COUNT] = {};
		const auto rv = sim.resources();
		for (size_t i = 0; i < rv.count; ++i) {
			if (!rv.alive[i] || rv.subtype[i] != RES_SUB_NONE) continue;
			if (rv.type[i] == RES_WOOD) { ++trees; trees_ok += allowed[rv.flora[i]]; seen[rv.flora[i]] = true; }
			// The fruit bush, or a straggler slot that is a fruit tree (temperate's apple).
			if (rv.type[i] == RES_FOOD) { ++fruit; fruit_ok += rv.flora[i] == f->fruit || (allowed[rv.flora[i]] && flora_is_fruit(rv.flora[i])); }
		}
		int kinds = 0;
		for (bool b : seen) kinds += b;
		CHECK(trees > 0 && trees_ok == trees, "%s: %d of %d trees are the biome's", f->biome, trees_ok, trees);
		CHECK(fruit > 0 && fruit_ok == fruit, "%s: %d of %d fruit bushes are the biome's", f->biome, fruit_ok, fruit);
		CHECK(kinds >= 1, "%s: trees of %d kinds", f->biome, kinds);
		Simulation again;
		again.set_biome_override(biome);
		again.generate_random_map(MAP_MAINLAND, 7, 2, 128.0f);
		const auto av = again.resources();
		CHECK(av.count == rv.count && std::equal(rv.flora, rv.flora + rv.count, av.flora), "%s: flora differs for one seed", f->biome);
		// Each tree holds the wood of one of its slots' templates (tree*_wood; 0: TREE_WOOD_SUPPLY).
		std::set<int32_t> woods;
		auto add_wood = [&](uint16_t w) { woods.insert(w ? (int32_t)w : TREE_WOOD_SUPPLY); };
		for (const auto &o : f->tree12_wood) { add_wood(o[0]); add_wood(o[1]); }
		for (const auto &o : f->tree45_wood) { add_wood(o[0]); add_wood(o[1]); }
		for (uint16_t w : f->tree3_wood) add_wood(w);
		int odd_wood = 0;
		for (size_t i = 0; i < rv.count; ++i)
			if (rv.alive[i] && rv.type[i] == RES_WOOD && rv.subtype[i] == RES_SUB_NONE) odd_wood += !woods.count(rv.amount[i]);
		CHECK(odd_wood == 0, "%s: %d trees hold wood no slot's template gives", f->biome, odd_wood);
	}
	// 10b''''. 0.28's Autumn and Nubia biomes paint their rmbiome terrains.
	for (int biome : {BIOME_AUTUMN, BIOME_NUBIA}) {
		Simulation sim;
		sim.set_biome_override(biome);
		sim.generate_random_map(MAP_MAINLAND, 7, 2, 128.0f);
		const char *name = biome == BIOME_AUTUMN ? "autumn" : "nubia";
		const auto &names = sim.terrain_tiles().names;
		CHECK(sim.current_map_biome() == name && std::count(names.begin(), names.end(), std::string(terrain_biome(name).main[0])) == 1,
		      "%s: Mainland paints the biome's main terrain", name);
	}
	// 10b'. Savanna: tree1 is gaia/tree/baobab (ResourceSupply/Max 600), tree2 acacia (200) and
	// tree4/tree5 bush_tropic (50) and bush_temperate (100).
	{
		Simulation sim;
		sim.set_biome_override(BIOME_SAVANNA);
		sim.generate_random_map(MAP_MAINLAND, 7, 2, 128.0f);
		const auto rv = sim.resources();
		int baobabs = 0, baobab_600 = 0, acacias = 0, acacia_200 = 0, bushes = 0, bush_ok = 0;
		for (size_t i = 0; i < rv.count; ++i) {
			if (rv.type[i] != RES_WOOD || rv.subtype[i] != RES_SUB_NONE) continue;
			if (flora_family(rv.flora[i]) == FLORA_BAOBAB) { ++baobabs; baobab_600 += rv.amount[i] == 600; }
			if (flora_family(rv.flora[i]) == FLORA_ACACIA) { ++acacias; acacia_200 += rv.amount[i] == 200; }
			if (flora_family(rv.flora[i]) == FLORA_BUSH) { ++bushes; bush_ok += rv.amount[i] == 50 || rv.amount[i] == 100; }
		}
		CHECK(baobabs > 0 && baobab_600 == baobabs, "savanna: %d of %d baobabs hold 600 wood", baobab_600, baobabs);
		CHECK(acacias > 0 && acacia_200 == acacias, "savanna: %d of %d acacias hold 200 wood", acacia_200, acacias);
		CHECK(bushes > 0 && bush_ok == bushes, "savanna: %d of %d bushes hold 50 or 100 wood", bush_ok, bushes);
		// ResourceSupply/MaxGatherers: baobab 9, the bushes 4 (mixins/bush.xml), acacia 8.
		int mg_bad = 0;
		for (size_t i = 0; i < rv.count; ++i) {
			if (rv.type[i] != RES_WOOD || rv.subtype[i] != RES_SUB_NONE) continue;
			const int mg = sim.resource_max_gatherers(rv.id[i]);
			const uint8_t fam = flora_family(rv.flora[i]);
			mg_bad += fam == FLORA_BAOBAB ? mg != 9 : fam == FLORA_BUSH || fam == FLORA_TROPIC_BUSH ? mg != 4
					: fam == FLORA_ACACIA ? mg != 8 : 0;
		}
		CHECK(mg_bad == 0, "savanna: %d trees with the wrong gatherer limit", mg_bad);
		// set_resource_amount: a tree's amount; a fruit bush's regrowth Max follows it.
		const uint32_t tree = sim.spawn_resource_world(0.0f, 0.0f, RES_WOOD, TREE_WOOD_SUPPLY);
		sim.set_resource_amount(tree, 600);
		const auto v2 = sim.resources();
		for (size_t i = 0; i < v2.count; ++i)
			if (v2.id[i] == tree) CHECK(v2.amount[i] == 600, "set_resource_amount sets a tree's wood (%d)", v2.amount[i]);
	}
	// 10b''. India: tree1 banyan 600, tree2 strangler 500, tree3 teak 500.
	{
		Simulation sim;
		sim.set_biome_override(BIOME_INDIA);
		sim.generate_random_map(MAP_MAINLAND, 7, 2, 128.0f);
		const auto rv = sim.resources();
		int big = 0;
		for (size_t i = 0; i < rv.count; ++i)
			big += rv.type[i] == RES_WOOD && rv.subtype[i] == RES_SUB_NONE && (rv.amount[i] == 600 || rv.amount[i] == 500);
		CHECK(big > 0, "india: %d trees hold banyan, strangler or teak wood", big);
	}
	// 10b'''. Large quarries: template_gaia_rock_large's MaxGatherers is 24, but the temperate
	// and alpine biomes' stoneLarge (gaia/rock/temperate_large_02) takes 48.
	for (int b : {BIOME_TEMPERATE, BIOME_OASIS}) {
		Simulation sim;
		sim.set_biome_override(b);
		sim.generate_random_map(MAP_MAINLAND, 7, 2, 128.0f);
		const auto rv = sim.resources();
		const int want = b == BIOME_TEMPERATE ? 48 : 24;
		int quarries = 0, ok = 0;
		for (size_t i = 0; i < rv.count; ++i)
			if (rv.alive[i] && rv.type[i] == RES_STONE && rv.subtype[i] == RES_SUB_NONE && rv.amount[i] == LARGE_MINE_SUPPLY) {
				++quarries;
				ok += sim.resource_max_gatherers(rv.id[i]) == want;
			}
		CHECK(quarries > 0 && ok == quarries, "%s: %d of %d large quarries take %d gatherers",
				b == BIOME_TEMPERATE ? "temperate" : "oasis", ok, quarries, want);
	}
	// 10c. Per-script terrains and plants.
	int scripts = 0, patch_scripts = 0, patch_painted = 0;
	for (int type = 0; type < MAP_TYPE_COUNT; ++type) {
		const ScriptTerrains *st = script_terrains(map_type_info((uint8_t)type).id);
		if (!st) continue;
		++scripts;
		Simulation sim;
		sim.generate_random_map((MapType)type, 5, 2, 64.0f);
		const TerrainBiome &b = st->terrain;
		std::set<std::string> own;
		auto add = [&](const char *const *list, int n) { for (int k = 0; k < n; ++k) if (list[k]) own.insert(list[k]); };
		add(b.main, 4); add(b.forest, 2); add(b.tier, 4); add(b.cliff, 2); add(b.hill, 2); add(b.dirt, 2);
		for (const char *n : {b.road, b.road_wild, b.shore_blend, b.shore, b.water})
			if (n) own.insert(n);
		for (const HeightBand &band : st->bands) add(band.terrain, 2);
		for (const AreaLayers &area : st->areas)
			for (const auto &layer : area.layer) add(layer, 6);
		std::set<std::string> unpatched = own;
		for (const PatchSet &patch : st->patches)
			for (const auto &layer : patch.layer) add(layer, 8);
		int foreign = 0;
		for (const auto &n : sim.terrain_tiles().names) foreign += !own.count(n);
		CHECK(foreign == 0, "%s paints %d terrains outside its script's set", st->map_id, foreign);
		if (own.size() > unpatched.size()) {
			// createLayeredPatches / createPatches: a name only a patch layer gives shows up.
			patch_scripts++;
			for (const auto &n : sim.terrain_tiles().names)
				if (!unpatched.count(n)) { patch_painted++; break; }
		}
		bool allowed[FLORA_COUNT] = {};
		for (const auto &o : st->flora.tree12) { allowed[o[0]] = allowed[o[1]] = true; }
		for (const auto &o : st->flora.tree45) { allowed[o[0]] = allowed[o[1]] = true; }
		for (uint8_t k : st->flora.tree3) allowed[k] = true;
		allowed[FLORA_DEFAULT] = true; // an empty set leaves the viewer's pick
		const auto rv = sim.resources();
		int odd = 0;
		for (size_t i = 0; i < rv.count; ++i)
			if (rv.type[i] == RES_WOOD && rv.subtype[i] == RES_SUB_NONE) odd += !allowed[rv.flora[i]];
		CHECK(odd == 0, "%s plants %d trees outside its script's set", st->map_id, odd);
		std::set<int32_t> woods;
		auto add_wood = [&](uint16_t w) { woods.insert(w ? (int32_t)w : TREE_WOOD_SUPPLY); };
		for (const auto &o : st->flora.tree12_wood) { add_wood(o[0]); add_wood(o[1]); }
		for (const auto &o : st->flora.tree45_wood) { add_wood(o[0]); add_wood(o[1]); }
		for (uint16_t w : st->flora.tree3_wood) add_wood(w);
		int odd_wood = 0;
		for (size_t i = 0; i < rv.count; ++i)
			// A cleared tree (clear_resources_near: Jebel Barkal's city) is dead, with no wood left.
			if (rv.alive[i] && rv.type[i] == RES_WOOD && rv.subtype[i] == RES_SUB_NONE) odd_wood += !woods.count(rv.amount[i]);
		CHECK(odd_wood == 0, "%s: %d trees hold wood no slot's template gives", st->map_id, odd_wood);
		Simulation chosen;
		chosen.set_biome_override(BIOME_SAVANNA);
		chosen.generate_random_map((MapType)type, 5, 2, 64.0f);
		const bool savanna = std::count(chosen.terrain_tiles().names.begin(), chosen.terrain_tiles().names.end(), std::string(terrain_biome("savanna").main[0])) == 1;
		// 0.28 offers a biome only on maps whose SupportedBiomes list it; elsewhere the script keeps its own.
		if (map_supported_biomes(type) >> BIOME_SAVANNA & 1u) CHECK(savanna, "%s with the savanna biome chosen paints savanna", st->map_id);
		else CHECK(chosen.terrain_tiles().names == sim.terrain_tiles().names, "%s ignores a biome 0.28 does not offer", st->map_id);
	}
	CHECK(scripts >= 30, "%d maps have their 0 A.D. script's own terrains", scripts);
	CHECK(patch_scripts > 0 && patch_painted == patch_scripts, "%d of %d scripts with patch-only terrains paint them",
	      patch_painted, patch_scripts);
	{
		// paintTerrainBasedOnHeight bands: Aegean Sea's corals under the sea, Arctic Summer's
		// polar cliffs from 4 m and snow from 15 m.
		auto count = [](MapType type, const char *name, float lo_m, float hi_m) {
			Simulation sim;
			sim.generate_random_map(type, 7, 2, 256.0f);
			const auto &t = sim.terrain_tiles();
			int in_band = 0, named = 0;
			const float half = sim.map_half(), cell = 2.0f * half / (float)t.res;
			for (int tz = 0; tz < t.res; ++tz)
				for (int tx = 0; tx < t.res; ++tx) {
					const float h = sim.get_terrain_height(-half + ((float)tx + 0.5f) * cell, -half + ((float)tz + 0.5f) * cell);
					const float m = (h - sim.water_height()) / HEIGHT_PER_METRE;
					if (m < lo_m || m >= hi_m) continue;
					++in_band;
					named += t.names[t.tiles[(size_t)tz * (size_t)t.res + (size_t)tx]] == name;
				}
			return std::make_pair(named, in_band);
		};
		const auto coral = count(MAP_AEGEAN_SEA, "medit_sea_coral_deep", -3.0f, -2.0f);
		CHECK(coral.second > 0 && coral.first * 10 >= coral.second * 9, "aegean_sea: %d of %d tiles at -3..-2 m are coral",
		      coral.first, coral.second);
		const auto snow = count(MAP_ARCTIC_SUMMER, "polar_snow_rocks", 15.0f, 100.0f);
		const auto ice = count(MAP_ARCTIC_SUMMER, "polar_ice", 15.0f, 100.0f);
		CHECK(snow.second > 0 && (snow.first + ice.first) * 10 >= snow.second * 9, "arctic_summer: %d of %d tiles over 15 m are snow",
		      snow.first + ice.first, snow.second);
		// Script areas (LayeredPainter): tiles only an area's layers name.
		auto tiles_named = [](MapType type, const char *name) {
			Simulation sim;
			sim.generate_random_map(type, 7, 2, 128.0f);
			const auto &t = sim.terrain_tiles();
			int n = 0;
			for (uint8_t v : t.tiles) n += t.names[v] == name;
			return n;
		};
		CHECK(tiles_named(MAP_CYCLADIC_ARCHIPELAGO, "medit_sea_coral_plants") > 0, "cycladic_archipelago: islands ring with coral");
		CHECK(tiles_named(MAP_DODECANESE, "LavaTest06") > 0, "dodecanese: the volcano's lava rim");
		CHECK(tiles_named(MAP_ARCTIC_SUMMER, "polar_ice_cracked") > 0, "arctic_summer: hilltops of cracked ice");
	}
	{
		Simulation sim;
		sim.set_biome_override(BIOME_TEMPERATE);
		sim.generate_random_map(MAP_MAINLAND, 7, 2, 128.0f);
		const auto rv = sim.resources();
		bool oak = false, pine = false, aleppo = false;
		for (size_t i = 0; i < rv.count; ++i) {
			const uint8_t fam = flora_family(rv.flora[i]);
			oak |= fam == FLORA_OAK; pine |= fam == FLORA_PINE; aleppo |= fam == FLORA_ALEPPO_PINE;
		}
		CHECK(oak && (pine || aleppo), "temperate mixes oak (tree1/tree2) and pine or maritime pine (tree4/tree5) forests");
		// temperate.json's tree4/tree5 is pine + pine_maritime, pine + pine or pine_maritime + pine_maritime.
		int pairs_seen = 0;
		for (uint32_t seed = 1; seed <= 12 && pairs_seen != 7; ++seed) {
			Simulation m;
			m.set_biome_override(BIOME_TEMPERATE);
			m.generate_random_map(MAP_MAINLAND, seed, 2, 128.0f);
			const auto r = m.resources();
			bool p = false, a = false;
			for (size_t i = 0; i < r.count; ++i) {
				p |= flora_family(r.flora[i]) == FLORA_PINE;
				a |= flora_family(r.flora[i]) == FLORA_ALEPPO_PINE;
			}
			pairs_seen |= p && a ? 1 : p ? 2 : a ? 4 : 0;
		}
		CHECK(pairs_seen == 7, "temperate seeds give all three pine pairings (seen mask %d)", pairs_seen);
		// createStragglerTrees' tree3 is gaia/fruit/apple: a 400-food fruit tree, not wood.
		int apples = 0, apple_wood = 0;
		for (size_t i = 0; i < rv.count; ++i) {
			if (!rv.alive[i] || rv.flora[i] != FLORA_FRUIT_APPLE) continue;
			if (rv.type[i] == RES_FOOD && rv.amount[i] == 400) ++apples; else ++apple_wood;
		}
		std::printf("  temperate stragglers: %d apple trees (400 food), %d wrong\n", apples, apple_wood);
		CHECK(apples > 0 && apple_wood == 0, "temperate straggler apples are food supplies");
		CHECK(flora_kind_by_name("aleppo_pine") == FLORA_ALEPPO_PINE && flora_kind_by_name("teak") == FLORA_TEAK &&
		      flora_kind_by_name("dead") == FLORA_DEAD, "0 A.D. species families resolve");
		CHECK(flora_kind_by_name("pine") == FLORA_PINE && flora_kind_by_name("berry") == FLORA_BERRY &&
		      flora_kind_by_name("tree") == FLORA_DEFAULT && flora_kind_by_name("food") == FLORA_DEFAULT &&
		      flora_kind_by_name("stone") == FLORA_COUNT && flora_is_fruit(FLORA_DATE) && !flora_is_fruit(FLORA_PALM),
		      "map JSON plant names resolve");
		// 0.28 templates with their own model (flora_data.h) resolve to their own kind, keeping
		// their family for game logic.
		CHECK(flora_kind_by_name("tree/elm") == FLORA_TREE_ELM && flora_family(FLORA_TREE_ELM) == FLORA_BROADLEAF &&
		      flora_kind_by_name("fruit/fig") == FLORA_FRUIT_FIG && flora_is_fruit(FLORA_FRUIT_FIG) &&
		      flora_family(FLORA_FRUIT_FIG) == FLORA_APPLE && !flora_is_fruit(FLORA_TREE_DATE_PALM) &&
		      flora_kind_by_name("tree/no_such_tree") == FLORA_COUNT && flora_family(FLORA_COUNT) == FLORA_DEFAULT,
		      "template flora kinds resolve");
		CHECK(flora_obstruction_m(FLORA_OAK, false) == 1.6f && flora_obstruction_m(FLORA_TREE_BAOBAB_3_MATURE, false) == 6.0f &&
		      flora_obstruction_m(FLORA_BANYAN, false) == 6.0f && flora_obstruction_m(FLORA_FRUIT_FIG, true) == 5.0f &&
		      flora_obstruction_m(FLORA_DEFAULT, true) == 2.5f && flora_obstruction_m(FLORA_DEFAULT, false) == 1.6f,
		      "tree and fruit obstructions are their 0.28 templates' (1.6 m trees, 2.5 m fruit, 6 m baobab)");
		const uint32_t id = sim.spawn_resource_world(0.0f, 0.0f, RES_WOOD, 100, RES_SUB_NONE, 1, FLORA_BAMBOO);
		CHECK(sim.resource_max_gatherers(id) == 1, "a JSON tree keeps its MaxGatherers");
		sim.set_resource_flora(id, 200);
		const auto v2 = sim.resources();
		for (size_t i = 0; i < v2.count; ++i)
			if (v2.id[i] == id) CHECK(v2.flora[i] == FLORA_DEFAULT, "an out-of-range FloraKind falls back to the default");
	}
	{
		// 0.28's per-map fish passes (latium.js, gulf_of_bothnia.js, corsica.js).
		auto fish = [](MapType mt) {
			static Simulation s;
			s.reset();
			s.generate_random_map(mt, 7, 2, 128.0f);
			const auto rv = s.resources();
			int n = 0;
			for (size_t i = 0; i < rv.count; ++i) n += rv.subtype[i] == RES_SUB_FISH;
			return n;
		};
		const int latium = fish(MAP_LATIUM), gulf = fish(MAP_GULF_OF_BOTHNIA), corsica = fish(MAP_CORSICA);
		CHECK(latium >= 40, "latium: %d fish, 0.28 places scaleByMapSize(45, 65) single fish", latium);
		CHECK(gulf >= 100, "gulf_of_bothnia: %d fish, 0.28 places 4 * scaleByMapSize(5, 30) groups of 2-3", gulf);
		CHECK(corsica >= 250, "corsica: %d fish, 0.28 places 50 and 70 groups of 2-3 per player", corsica);
	}
	{
		// Every map with a 0 A.D. script has at least the mines, fruit and hunt its script
		// places outside the bases (resource_counts_data.h), on a Medium map for 2 players.
		for (const MapResourceCounts &rc : kMapResourceCounts) {
			MapType mt = MAP_TYPE_COUNT;
			for (int t = 0; t < MAP_TYPE_COUNT; ++t)
				if (std::strcmp(map_type_info((uint8_t)t).id, rc.map_id) == 0) mt = (MapType)t;
			CHECK(mt != MAP_TYPE_COUNT, "%s: resource counts for a map RTS0 doesn't have", rc.map_id);
			if (mt == MAP_TYPE_COUNT) continue;
			static Simulation s;
			s.reset();
			s.generate_random_map(mt, 7, 2, 128.0f);
			const auto rv = s.resources();
			int metal = 0, stone = 0, fruit = 0, hunt = 0;
			for (size_t i = 0; i < rv.count; ++i) {
				if (rv.subtype[i] == RES_SUB_RUINS) continue;
				metal += rv.type[i] == RES_GOLD;
				stone += rv.type[i] == RES_STONE;
				fruit += rv.type[i] == RES_FOOD && rv.subtype[i] == RES_SUB_NONE;
			}
			const auto fv = s.fauna();
			for (size_t i = 0; i < fv.count; ++i) hunt += fauna_type_info(fv.fauna_type[i]).food_amount > 0;
			auto want = [&](const uint16_t (&g)[4][3]) { return map_resource_count(g, 256.0f, 2); };
			const int want_metal = want(rc.metal_large) + want(rc.metal_small), want_stone = want(rc.stone_large) + want(rc.stone_small);
			CHECK(metal * 4 >= want_metal * 3 && stone * 4 >= want_stone * 3 && fruit * 4 >= want(rc.fruit) * 3 && hunt * 4 >= want(rc.hunt) * 3,
			      "%s: %d metal, %d stone, %d fruit, %d hunt; its 0.28 script places %d, %d, %d, %d outside the bases", rc.map_id,
			      metal, stone, fruit, hunt, want_metal, want_stone, want(rc.fruit), want(rc.hunt));
		}
	}
	{
		// "AQAAAQAA" is the (run - 1, name) pairs (1, 0) (0, 1) (0, 0): 0, 0, 1, 0.
		auto t = Simulation::terrain_tiles_from_rle(2, {"a", "b"}, "AQAAAQAA");
		CHECK(t.res == 2 && t.tiles == std::vector<uint8_t>({0, 0, 1, 0}), "terrain RLE decodes (%d tiles)", (int)t.tiles.size());
		CHECK(Simulation::terrain_tiles_from_rle(2, {"a"}, "AQAAAQAA").empty(), "an index past the names is refused");
		CHECK(Simulation::terrain_tiles_from_rle(3, {"a", "b"}, "AQAAAQAA").empty(), "too few tiles are refused");
		Simulation sim;
		Simulation::TerrainTiles bad;
		bad.res = 2;
		bad.names = {"a"};
		bad.tiles = {0, 0, 0};
		sim.set_terrain_tiles(bad);
		CHECK(sim.terrain_tiles().empty(), "set_terrain_tiles refuses a short tile array");
	}

	if (g_failures) {
		std::printf("FAIL (%d)\n", g_failures);
		return 1;
	}
	std::printf("PASS\n");
	return 0;
}
