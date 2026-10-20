// Per-player line of sight (los_grid.h, 0 A.D. CCmpRangeManager LOS): sources
// light their own player's cells, allies share them, moves and deaths remove
// exactly what they added, and a save brings the same grid back.
#define private public
#include "simulation/simulation.h"
#undef private
#include "simulation/rms_generator.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

using namespace rtso;

static int failures = 0;
#define CHECK(cond, what)                                              \
	do {                                                               \
		if (cond) std::printf("  [ok]   %s\n", what);                  \
		else { std::printf("  [FAIL] %s: %s\n", what, #cond); ++failures; } \
	} while (0)

static std::unique_ptr<Simulation> make_sim(float half) {
	auto s = std::make_unique<Simulation>();
	s->set_map_half(half);
	for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
		s->players_[p].team = p;
		s->players_[p].active = 1;
	}
	return s;
}

// The counts and bits every live source should give, built from scratch.
static bool matches_fresh(const Simulation &s) {
	LosGrid fresh;
	fresh.init(s.grid_res_, s.map_half_);
	for (const LosSource &src : s.los_unit_src_) { LosSource have; fresh.update(have, src); }
	for (const LosSource &src : s.los_bld_src_) { LosSource have; fresh.update(have, src); }
	for (const LosSource &src : s.los_garrison_src_) { LosSource have; fresh.update(have, src); }
	for (int c = 0; c < (int)fresh.cells(); ++c) {
		if (fresh.visible_bits(c) != s.los().visible_bits(c)) return false;
		for (uint8_t p = 0; p < LosGrid::PLAYERS; ++p)
			if (fresh.count(p, c) != s.los().count(p, c)) return false;
	}
	return true;
}

static void test_own_and_shared_vision() {
	std::printf("own and shared vision\n");
	auto s = make_sim(64.0f);
	const uint32_t id = s->spawn_unit(0.0f, 0.0f, 0, UNIT_VILLAGER);
	s->tick(0.25f); // one LOS update (4 Hz)
	const float vr = unit_vision_range(UNIT_VILLAGER);
	CHECK(s->los_visible(0, 0.0f, 0.0f), "player 0 sees its villager's cell");
	CHECK(s->los_visible(0, vr - 2.0f, 0.0f), "and cells inside its vision range");
	CHECK(!s->los_visible(0, vr + 3.0f, 0.0f), "but not past it");
	CHECK(!s->los_visible(1, 0.0f, 0.0f), "player 1 does not see it");
	s->set_mutual_ally(0, 1);
	CHECK(!s->los_visible(1, 0.0f, 0.0f), "an ally sees nothing of it before Cartography (Player.js UpdateSharedLos)");
	s->players_[1].techs.set(TECH_UNLOCK_SHARED_LOS);
	CHECK(s->los_visible(1, 0.0f, 0.0f), "an ally with Cartography shares the vision");
	CHECK(s->los_explored(1, 0.0f, 0.0f), "and what it explored");
	CHECK(!s->los_visible(2, 0.0f, 0.0f), "a third player still does not");

	const size_t u = s->unit_id_to_idx_.at(id);
	s->units_.pos_x[u] = 40.0f;
	s->tick(0.25f); // one LOS update (4 Hz)
	CHECK(!s->los_visible(0, 0.0f, 0.0f), "the old spot goes dark once the villager walks away");
	CHECK(s->los_explored(0, 0.0f, 0.0f), "but stays explored");
	CHECK(s->los_visible(0, 40.0f, 0.0f), "the new spot is visible");
	CHECK(matches_fresh(*s), "counts equal a fresh build after the move");

	s->units_.alive[u] = 0;
	s->tick(0.25f); // one LOS update (4 Hz)
	bool none = true;
	for (int c = 0; c < (int)s->los().cells(); ++c) none = none && s->los().count(0, c) == 0 && !(s->los().visible_bits(c) & 1);
	CHECK(none, "a dead villager removes every count it added");
}

static void test_buildings_and_capture() {
	std::printf("buildings\n");
	auto s = make_sim(64.0f);
	const uint32_t bid = s->spawn_building_world(-20.0f, -20.0f, BLD_HOUSE, 2, 1.0f);
	const uint32_t fid = s->spawn_building_world(20.0f, 20.0f, BLD_HOUSE, 3, 0.2f);
	(void)fid;
	for (auto &b : s->buildings_) b.hp = b.max_hp = 500.0f; // no civ data loaded here
	s->tick(0.25f); // one LOS update (4 Hz)
	CHECK(s->los_visible(2, -20.0f, -20.0f), "a finished house gives its owner vision");
	CHECK(!s->los_visible(3, 20.0f, 20.0f), "a foundation gives none");
	for (auto &b : s->buildings_)
		if (b.id == bid) { // captured: the new owner holds the capture points too
			b.team = 4;
			std::swap(b.capture_points_by_team[2], b.capture_points_by_team[4]);
		}
	s->tick(0.25f); // one LOS update (4 Hz)
	CHECK(!s->los_visible(2, -20.0f, -20.0f) && s->los_visible(4, -20.0f, -20.0f), "a captured house's vision moves to its new owner");
	CHECK(s->los_explored(2, -20.0f, -20.0f), "the old owner keeps it explored");
	CHECK(matches_fresh(*s), "counts equal a fresh build after the capture");
}

static void test_many_moves_and_save() {
	std::printf("many moves, removals and a save\n");
	auto s = make_sim(128.0f);
	RmsRng rng(7);
	std::vector<uint32_t> ids;
	for (int i = 0; i < 300; ++i)
		ids.push_back(s->spawn_unit(rng.range_float(-120.0f, 120.0f), rng.range_float(-120.0f, 120.0f), (uint8_t)(i % 8),
				(UnitType)(i % 3 == 0 ? UNIT_CAVALRY : UNIT_VILLAGER)));
	for (int t = 0; t < 60; ++t) {
		for (size_t u = 0; u < s->units_.count; ++u) {
			s->units_.pos_x[u] = std::clamp(s->units_.pos_x[u] + rng.range_float(-3.0f, 3.0f), -126.0f, 126.0f);
			s->units_.pos_z[u] = std::clamp(s->units_.pos_z[u] + rng.range_float(-3.0f, 3.0f), -126.0f, 126.0f);
		}
		if (t % 10 == 5) s->units_.remove_at((size_t)rng.range_int(0, (int)s->units_.count - 1));
		s->tick_los();
	}
	CHECK(matches_fresh(*s), "300 wandering units, some removed: counts equal a fresh build");

	const char *path = "/tmp/rts0_test_los.sav";
	CHECK(s->save_state(path), "saves");
	auto b = make_sim(64.0f);
	CHECK(b->load_state(path), "loads into a sim of another size");
	CHECK(b->los_hash() == s->los_hash(), "the loaded LOS hashes the same");
	bool same = b->los().cells() == s->los().cells();
	for (int c = 0; same && c < (int)s->los().cells(); ++c)
		for (uint8_t p = 0; p < LosGrid::PLAYERS; ++p) same = same && b->los().count(p, c) == s->los().count(p, c);
	CHECK(same, "and the same counts");
	b->tick_los();
	CHECK(b->los_hash() == s->los_hash(), "a tick after loading changes nothing");
}

// Vision/Range of the structures' 0.28 templates, x0.28 (vision_data.h).
static void test_structure_vision_ranges() {
	std::printf("structure vision ranges\n");
	auto s = make_sim(64.0f);
	s->spawn_building_world(-30.0f, -30.0f, BLD_TOWN_CENTER, 0, 1.0f);
	s->spawn_building_world(30.0f, -30.0f, BLD_HOUSE, 1, 1.0f);
	s->spawn_building_world(-30.0f, 30.0f, BLD_WALL_SEGMENT, 2, 1.0f);
	s->spawn_building_world(30.0f, 30.0f, BLD_FIELD, 3, 1.0f);
	for (auto &b : s->buildings_) b.hp = b.max_hp = 500.0f; // no civ data loaded here
	s->tick(0.25f); // one LOS update (4 Hz)
	CHECK(structure_vision_range(BLD_TOWN_CENTER) == 25.2f && structure_vision_range(BLD_HOUSE) == 5.6f &&
	      structure_vision_range(BLD_WALL_SEGMENT) == 1.12f && structure_vision_range(BLD_FIELD) == 0.0f,
	      "Civic Centre 90 m, house 20 m, wall 4 m, field 0 (x0.28)");
	CHECK(s->los_visible(0, -30.0f + 23.0f, -30.0f) && !s->los_visible(0, -30.0f + 28.0f, -30.0f),
	      "a Civic Centre sees 25 units, no further");
	CHECK(s->los_visible(1, 30.0f, -30.0f) && s->los_visible(1, 30.0f - 4.0f, -30.0f) && !s->los_visible(1, 30.0f - 9.0f, -30.0f),
	      "a house sees about 6 units");
	CHECK(s->los_visible(2, -30.0f, 30.0f) && !s->los_visible(2, -30.0f + 4.5f, 30.0f), "a wall sees only around its own cell");
	CHECK(!s->los_visible(3, 30.0f, 30.0f), "a field sees nothing");
}

// The fog a player sees is its LOS: plain circles, as CCmpRangeManager's, with no terrain in
// the way (0 A.D. lets a unit see over a hill).
static void test_fog_is_los() {
	std::printf("fog is the line of sight\n");
	auto s = make_sim(64.0f);
	const int res = 65;
	std::vector<float> h((size_t)res * res, 0.0f);
	for (int j = 0; j < res; ++j)
		for (int i = 0; i < res; ++i) {
			const float x = -64.0f + 128.0f * (float)i / (float)(res - 1);
			if (x > 4.0f && x < 8.0f) h[(size_t)j * res + i] = 30.0f; // a steep ridge east of the unit
		}
	s->set_terrain_data(h, res, -5.0f);
	s->spawn_unit(0.0f, 0.0f, 0, UNIT_SPEARMAN);
	s->tick(0.25f); // one LOS and fog update (4 Hz)
	const float vr = s->unit_vision_at(0);
	CHECK(vr > 12.0f, "the spearman sees past the ridge's far side");
	CHECK(s->fow_state_at(vr - 3.0f, 0.0f, 0) == Simulation::FOW_VISIBLE, "the fog shows the cells past the ridge");
	CHECK(s->fow_state_at(vr + 3.0f, 0.0f, 0) == Simulation::FOW_UNEXPLORED, "but nothing past the vision range");
	bool same = true;
	for (float x = -40.0f; x <= 40.0f; x += 2.0f)
		for (float z = -40.0f; z <= 40.0f; z += 2.0f)
			same = same && ((s->fow_state_at(x, z, 0) == Simulation::FOW_VISIBLE) == s->los_visible(0, x, z));
	CHECK(same, "every cell the fog shows is one the LOS sees, and back");
}

static void test_revealed_and_explored_map() {
	std::printf("game setup options\n");
	auto s = make_sim(64.0f);
	s->set_explore_map(true);
	CHECK(s->los_explored(5, 30.0f, 30.0f) && !s->los_visible(5, 30.0f, 30.0f), "Explored map: explored, not visible");
	s->set_reveal_map(true);
	CHECK(s->los_visible(5, 30.0f, 30.0f), "Revealed map: everything visible");
}

static void test_start_territory_explored() {
	std::printf("starting territory explored (InitGame.js ExploreTerritories)\n");
	auto s = make_sim(64.0f);
	s->spawn_building_world(-30.0f, -30.0f, BLD_TOWN_CENTER, 1, 1.0f);
	const auto &tc = s->buildings_.back();
	const float vr = s->building_vision_range(tc);
	const int R = s->grid_res_;
	const float cell = 128.0f / (float)R;
	// A cell of player 1's territory beyond its civic centre's vision.
	float fx = 0.0f, fz = 0.0f;
	bool found = false;
	for (int z = 0; z < R && !found; ++z)
		for (int x = 0; x < R && !found; ++x) {
			const float wx = (x + 0.5f) * cell - 64.0f, wz = (z + 0.5f) * cell - 64.0f;
			if (s->territory_grid_.at(x, z) == 2 && !s->los_.off_world(z * R + x) && std::hypot(wx + 30.0f, wz + 30.0f) > vr + 2.0f * cell) {
				fx = wx; fz = wz; found = true;
			}
		}
	CHECK(found, "the territory reaches past the civic centre's vision");
	s->tick(0.25f);
	CHECK(s->los_explored(1, fx, fz) && !s->los_visible(1, fx, fz), "the owner starts with its whole territory explored");
	CHECK(!s->los_explored(2, fx, fz), "nobody else does");
	CHECK(!s->los_explored(1, 50.0f, 50.0f), "land outside it stays unexplored");
	auto t = make_sim(64.0f);
	t->spawn_building_world(-30.0f, -30.0f, BLD_TOWN_CENTER, 0, 1.0f);
	t->tick(0.25f);
	const int c = t->los_.cell_of(fx, fz);
	CHECK(t->fow_grid_[(size_t)c] == Simulation::FOW_EXPLORED, "and player 0's drawn fog shows it explored");
	t->tick(0.25f);
	t->spawn_building_world(40.0f, 40.0f, BLD_TOWN_CENTER, 0, 1.0f);
	t->tick(0.25f);
	CHECK(t->territory_grid_.data[(size_t)t->los_.cell_of(40.0f, 40.0f - vr - 2.0f * cell)] == 1, "(a later civic centre's territory)");
	CHECK(!t->los_explored(0, 40.0f, 40.0f - vr - 2.0f * cell), "only before the first turn, not for later territory");
}

// 0 A.D. CircularMap: LOS ends at the circle (CCmpRangeManager LosIsOffWorld), so the cells
// outside it stay unseen, unexplored and out of the explored percentage, even with Revealed map.
static void test_circular_map() {
	std::printf("circular map\n");
	CHECK(map_circular(MAP_MAINLAND) && map_circular(MAP_DANUBIUS) && !map_circular(MAP_CORSICA) &&
	      !map_circular(MAP_LATIUM) && !map_circular(MAP_PHOENICIAN_LEVANT) && !map_circular(MAP_RANDOM_CONTINENTAL),
	      "0.28 random maps are circular except corsica, latium and phoenician_levant; RTS0's own are square");
	auto s = make_sim(64.0f);
	s->set_circular_map(true);
	CHECK(s->circular_map() && s->off_world(-62.0f, -62.0f) && !s->off_world(0.0f, -61.0f), "the corners are off the world");
	s->spawn_unit(-60.0f, -60.0f, 0, UNIT_SPEARMAN); // stood in a corner (passability is not this test's)
	s->spawn_unit(0.0f, 0.0f, 0, UNIT_SPEARMAN);
	s->spawn_unit(-40.0f, -40.0f, 0, UNIT_SPEARMAN); // near the edge, inside
	s->tick(0.25f);
	CHECK(s->los_visible(0, 0.0f, 0.0f) && !s->los_visible(0, -62.0f, -62.0f) && !s->los_explored(0, -62.0f, -62.0f),
	      "a unit in the corner lights nothing off the world");
	// LosIsOffWorld: sight ends 8 m (2 units) inside the circle.
	CHECK(s->los_visible(0, -42.0f, -42.0f) && !s->los_visible(0, -44.5f, -44.5f), "but it sees up to 8 m inside the circle's edge");
	CHECK(s->fow_state_at(-62.0f, -62.0f, 0) == Simulation::FOW_UNEXPLORED && s->fow_state_at(-42.0f, -42.0f, 0) == Simulation::FOW_VISIBLE,
	      "the fog matches");
	const size_t world = s->los().in_world_cells(), all = s->los().cells();
	CHECK(world * 100 > all * 70 && world * 100 < all * 78, "about pi/4 x (62/64)^2 of the cells are in the world");
	const char *path = "/tmp/rts0_test_los_circle.sav";
	CHECK(s->save_state(path), "saves");
	auto b = make_sim(64.0f);
	CHECK(b->load_state(path) && b->circular_map() && b->los().in_world_cells() == world, "loads circular");
	CHECK(b->los_hash() == s->los_hash(), "with the same LOS");
	s->set_explore_map(true);
	s->tick(0.25f);
	CHECK(s->fow_state_at(-62.0f, -62.0f, 0) == Simulation::FOW_UNEXPLORED && s->fow_state_at(30.0f, 30.0f, 0) == Simulation::FOW_EXPLORED,
	      "Explored map explores only the circle");
	s->set_reveal_map(true);
	s->tick(0.25f);
	CHECK(s->fow_state_at(-62.0f, -62.0f, 0) == Simulation::FOW_UNEXPLORED && s->fow_state_at(30.0f, 30.0f, 0) == Simulation::FOW_VISIBLE,
	      "Revealed map reveals only the circle (m_LosStateRevealed)");
	// Observers, the defeated and winners see a revealed map: its corners stay black too.
	const int res = s->fow_res();
	auto cell_of = [&](float x, float z) {
		const int cx = std::clamp((int)((x + 64.0f) / 128.0f * res), 0, res - 1), cz = std::clamp((int)((z + 64.0f) / 128.0f * res), 0, res - 1);
		return (size_t)cz * res + cx;
	};
	const uint8_t *obs = s->fow_grid(OBSERVER_VIEW);
	CHECK(obs[cell_of(-63.0f, -63.0f)] == Simulation::FOW_UNEXPLORED && obs[cell_of(0.0f, 0.0f)] == Simulation::FOW_VISIBLE,
	      "an observer's revealed map keeps the corners black");
	CHECK(b->fow_grid(OBSERVER_VIEW)[cell_of(-63.0f, 63.0f)] == Simulation::FOW_UNEXPLORED, "after a load too");

	auto g = make_sim(64.0f);
	g->generate_random_map(MAP_MAINLAND, 3, 2, 64.0f);
	CHECK(g->circular_map(), "Mainland generates circular");
	g->set_circular_map_choice(2);
	g->generate_random_map(MAP_MAINLAND, 3, 2, 64.0f);
	CHECK(!g->circular_map(), "unless the setup asks for a square");
	// A square map's outer 12 m (3 units) are off the world.
	const size_t sq = g->los().in_world_cells(), sq_all = g->los().cells();
	CHECK(sq < sq_all && sq * 100 > sq_all * 85 && g->los().off_world(0) &&
	      g->los().off_world(g->los().cell_of(0.0f, -62.5f)) && !g->los().off_world(g->los().cell_of(0.0f, -58.0f)),
	      "a square map's outer 12 m frame is off the world");
	CHECK(g->past_map_edge(0.0f, 61.5f) && !g->past_map_edge(0.0f, 60.5f) && g->past_map_edge(60.0f, 0.0f, 1.5f) &&
	      g->edge_step_barred(0.0f, 61.2f, 0.0f, 61.6f, 0.0f) && !g->edge_step_barred(0.0f, 61.6f, 0.0f, 61.2f, 0.0f),
	      "and units can't walk into it (they may walk back out)");
}

// VisionSharing.js CheckVisionSharings: a player with units garrisoned in an ally's structure
// sees what the structure sees, Cartography or not, until they leave.
static void test_garrisoned_ally_vision() {
	std::printf("garrisoned ally vision\n");
	auto s = make_sim(64.0f);
	s->set_mutual_ally(0, 1);
	const uint32_t tower = s->spawn_building_world(0.0f, 0.0f, BLD_SENTRY_TOWER, 0, 1.0f);
	const uint32_t guest = s->spawn_unit(3.0f, 0.0f, 1, UNIT_SPEARMAN);
	s->tick(0.25f);
	const Simulation::Building *b = nullptr;
	for (const Simulation::Building &x : s->buildings()) if (x.id == tower) b = &x;
	const float r = b ? s->building_vision_range(*b) : 0.0f;
	const float far_x = -(r - 1.5f);
	CHECK(r > 0.0f && s->los_visible(0, far_x, 0.0f), "the tower's owner sees the far edge of its range");
	CHECK(!s->los_visible(1, far_x, 0.0f), "its ally doesn't, before garrisoning");
	s->order_garrison(&guest, 1, tower);
	for (int t = 0; t < 200 && s->lookup_unit(guest) >= 0 && s->units_.alive[s->lookup_unit(guest)]; ++t) s->tick(0.05f);
	s->tick(0.25f);
	const auto *g = s->get_garrisoned_units(tower);
	bool inside = false;
	if (g) for (uint32_t id : *g) inside |= id == guest;
	CHECK(inside, "the ally's spearman is garrisoned in the tower");
	CHECK(s->los_visible(1, far_x, 0.0f), "the garrisoned ally sees what the tower sees");
	CHECK(!s->los_visible(2, far_x, 0.0f), "a third player still doesn't");
	CHECK(matches_fresh(*s), "counts equal a fresh build with the shared circle");
	s->cmd_unload(0, tower, nullptr, 0);
	for (int t = 0; t < 10; ++t) s->tick(0.25f);
	CHECK(s->lookup_unit(guest) >= 0 && s->units_.alive[s->lookup_unit(guest)] && s->los_garrison_src_.empty(), "once it leaves, the shared circle goes");
	CHECK(matches_fresh(*s), "counts equal a fresh build after unloading");
}

int main() {
	test_own_and_shared_vision();
	test_buildings_and_capture();
	test_many_moves_and_save();
	test_structure_vision_ranges();
	test_fog_is_los();
	test_revealed_and_explored_map();
	test_start_territory_explored();
	test_circular_map();
	test_garrisoned_ally_vision();
	std::printf("%s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
