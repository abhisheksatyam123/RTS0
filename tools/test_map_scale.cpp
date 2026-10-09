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
//  6b. Team Placement (Ambush, Frontier): each pattern places every player, keeps
//      teammates together, and the setup option survives the game-start attributes.
//  7. Shallow water is walkable and sailable; deep water is ships only.
//  8. Red Sea and Hellas are painted from 0 A.D.'s heightmap images.
//  9. English Channel splits the players by sea; Fortress walls every base.
//  Exit: 0 = PASS, 1 = FAIL.
#include "simulation/simulation.h"
#include "simulation/rms_generator.h"
#include "network/net_types.h"

#include <algorithm>
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
	int paths = 0, truncated = 0;
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
				if (b.type == BLD_TOWN_CENTER) tcs.push_back({b.x, b.z});
			CHECK((int)tcs.size() >= 2, "%s %d: %zu town centres", map_type_info(mt).id, size, tcs.size());
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
		}
	}
	std::printf("paths between town centres: %d, hit the %d-waypoint cap: %d\n", paths, WaypointPath::MAX_WP, truncated);

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
		const float x = -100.0f, z = 100.0f;
		sim.spawn_unit(x, z, 2, UNIT_SPEARMAN);
		sim.spawn_unit(-x, -z, 3, UNIT_SPEARMAN);
		sim.set_player_team(2, 5);
		sim.set_player_team(3, 5);
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
		sim.spawn_unit(-100.0f, 100.0f, 2, UNIT_SPEARMAN);
		sim.set_explore_map(true);
		sim.set_fow_viewer(2);
		for (int i = 0; i < 10; ++i) sim.tick(Simulation::FIXED_STEP);
		CHECK(sim.fow_state_at(100.0f, -100.0f, 0) == Simulation::FOW_EXPLORED, "explored map: far cell not explored for player 0");
		CHECK(sim.fow_state_at(100.0f, -100.0f, 2) == Simulation::FOW_EXPLORED, "explored map: far cell not explored for the viewer");
		CHECK(sim.fow_state_at(-100.0f, 100.0f, 2) == Simulation::FOW_VISIBLE, "explored map: viewer can't see its own unit");
		CHECK(!sim.is_entity_visible_to_viewer(-100.0f, 100.0f, 2, 0), "explored map shows player 0 an enemy unit");
	}
	{
		Simulation sim;
		sim.generate_random_map(MAP_MAINLAND, 7, 4, 128.0f);
		sim.set_reveal_map(true);
		sim.set_fow_viewer(3);
		for (int i = 0; i < 10; ++i) sim.tick(Simulation::FIXED_STEP);
		const int n = sim.fow_res() * sim.fow_res();
		bool all0 = true, all3 = true;
		for (int i = 0; i < n; ++i) {
			all0 &= sim.fow_grid(0)[i] == Simulation::FOW_VISIBLE;
			all3 &= sim.fow_grid(3)[i] == Simulation::FOW_VISIBLE;
		}
		CHECK(all0 && all3, "revealed map: some cell is not visible");
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

	// 6b. Team Placement (0 A.D. TeamPlacement on Ambush and Frontier).
	for (MapType mt : {MAP_AMBUSH, MAP_FRONTIER}) {
		const uint8_t teams[4] = {1, 2, 1, 2};
		std::set<std::vector<int>> layouts;
		for (int tp = TEAM_PLACEMENT_RADIAL; tp < TEAM_PLACEMENT_COUNT; ++tp) {
			Simulation sim;
			sim.set_team_placement(tp);
			sim.generate_random_map(mt, 31, 4, 128.0f, teams);
			float tx[8] = {}, tz[8] = {};
			bool has[8] = {};
			for (const auto &b : sim.buildings())
				if (b.type == BLD_TOWN_CENTER && b.team < 8) { has[b.team] = true; tx[b.team] = b.x; tz[b.team] = b.z; }
			const char *name = map_type_info(mt).id;
			CHECK(has[0] && has[1] && has[2] && has[3], "%s placement %d: a player has no town centre", name, tp);
			auto d = [&](int a, int b) { return std::hypot(tx[a] - tx[b], tz[a] - tz[b]); };
			const float mate = std::max(d(0, 2), d(1, 3));
			const float foe = std::min({d(0, 1), d(0, 3), d(2, 1), d(2, 3)});
			if (tp == TEAM_PLACEMENT_RANDOM_GROUP) {
				// groupPlayersByArea: of the random spots, teammates get the pairing
				// with the least distance between allies.
				const float pairs = d(0, 2) + d(1, 3);
				CHECK(pairs <= d(0, 1) + d(2, 3) + 1.0f && pairs <= d(0, 3) + d(1, 2) + 1.0f,
				      "%s random group: teammates %.1f apart in all, another pairing is closer", name, pairs);
			} else {
				// On a circle of four the nearest foe is as close as the teammate.
				CHECK(tp == TEAM_PLACEMENT_RADIAL ? mate <= foe + 1.0f : mate < foe,
				      "%s placement %d: teammates %.1f apart, nearest enemies %.1f", name, tp, mate, foe);
			}
			std::vector<int> l;
			for (int p = 0; p < 4; ++p) { l.push_back((int)tx[p]); l.push_back((int)tz[p]); }
			layouts.insert(l);
		}
		CHECK(layouts.size() == 4, "%s: the four placements gave %zu layouts", map_type_info(mt).id, layouts.size());
	}
	{
		GameSetupParams p, q;
		p.map_id = "random_frontier";
		p.team_placement = TEAM_PLACEMENT_STRONGHOLD;
		CHECK(parse_game_start_full(format_game_start_full(p), q) && q.team_placement == TEAM_PLACEMENT_STRONGHOLD,
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
		CHECK(walls >= 4 * 40 && gates == 4 * 4 && towers >= 4 * 8, "fortress: %d walls, %d gates, %d towers", walls, gates, towers);
		CHECK(kinds[BLD_MARKET] == 4 && kinds[BLD_BARRACKS] == 4 && kinds[BLD_STABLE] == 4 && kinds[BLD_TEMPLE] == 4,
		      "fortress: each player gets a market, barracks, stable and temple");
		// The gates (unlocked) let units out of the walls.
		std::vector<std::pair<float, float>> ftc;
		for (const auto &b : fort.buildings())
			if (b.type == BLD_TOWN_CENTER) ftc.push_back({b.x + 6.0f, b.z + 6.0f});
		CHECK(ftc.size() == 4 && can_reach(fort.nav_grid(), ftc[0].first, ftc[0].second, ftc[1].first, ftc[1].second, PASS_INFANTRY),
		      "fortress: no way out through the gates");
	}

	if (g_failures) {
		std::printf("FAIL (%d)\n", g_failures);
		return 1;
	}
	std::printf("PASS\n");
	return 0;
}
