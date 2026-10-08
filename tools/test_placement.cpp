// Building placement rules (0 A.D. BuildRestrictions.js CheckPlacement):
// obstruction by buildings and resources, land/shore/land-shore terrain, the
// dock shoreline, territory per template, and distance from the player's own
// Civic Centres, towers and fortresses.
//   g++ -std=c++20 -O2 -Isrc tools/test_placement.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp -lpthread -o /tmp/test_placement && /tmp/test_placement
// Private access only to refresh the territory grid without ticking.
#define private public
#include "simulation/simulation.h"
#undef private
#include <cmath>
#include <cstdio>
#include <vector>

using namespace rtso;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static constexpr float HALF = 64.0f;
static constexpr int RES = 65;

// A sea across the middle of the map (|z| < 16), land north and south of it.
static float height_at(float, float z) { return std::fabs(z) < 16.0f ? -3.0f : 3.0f; }

static void fresh(Simulation &s) {
	s.set_sim_seed(7);
	s.reset();
	s.set_map_half(HALF);
	std::vector<float> h((size_t)RES * RES);
	for (int j = 0; j < RES; ++j)
		for (int i = 0; i < RES; ++i)
			h[(size_t)j * RES + i] = height_at(-HALF + 2.0f * HALF * i / (RES - 1), -HALF + 2.0f * HALF * j / (RES - 1));
	s.set_terrain_data(h, RES, 0.0f);
	s.begin_bulk_spawn();
	s.end_bulk_spawn(); // marks the water on the nav grid
	for (uint8_t p = 0; p < 2; ++p) {
		s.set_player_active(p, true, false);
		s.set_player_resources(p, 5000, 5000, 5000, 5000);
		s.players_[p].phase = 3;
	}
}

// The cell a building of `type` centred on (x, z) is placed at.
static Simulation::PlacementResult at(const Simulation &s, uint8_t player, float x, float z, uint8_t type) {
	const float half = building_type_info(type).footprint * Simulation::GRID * 0.5f;
	return s.check_placement(player, (int)std::floor((x - half) / Simulation::GRID),
			(int)std::floor((z - half) / Simulation::GRID), type);
}

int main() {
	static Simulation s;
	fresh(s);
	// Player 0 north of the sea, player 1 south of it.
	s.spawn_building_world(-40.0f, 40.0f, BLD_TOWN_CENTER, 0, 1.0f);
	s.spawn_building_world(40.0f, -40.0f, BLD_TOWN_CENTER, 1, 1.0f);
	s.rebuild_territory();

	// Territory: houses only in own territory.
	CHECK(at(s, 0, -30.0f, 50.0f, BLD_HOUSE) == Simulation::PLACE_OK);
	CHECK(at(s, 0, 30.0f, 50.0f, BLD_HOUSE) == Simulation::PLACE_TERRITORY);   // neutral
	CHECK(at(s, 0, 40.0f, -50.0f, BLD_HOUSE) == Simulation::PLACE_TERRITORY);  // enemy
	// Outposts and palisades also go in neutral territory.
	CHECK(at(s, 0, 30.0f, 50.0f, BLD_OUTPOST) == Simulation::PLACE_OK);
	CHECK(at(s, 0, 30.0f, 50.0f, BLD_PALISADE) == Simulation::PLACE_OK);

	// Terrain: no land building on water, even in own territory.
	CHECK(at(s, 0, -40.0f, 10.0f, BLD_HOUSE) == Simulation::PLACE_TERRAIN);
	CHECK(at(s, 0, -40.0f, 10.0f, BLD_WALL_SEGMENT) == Simulation::PLACE_TERRAIN); // too deep for a wall

	// Docks: on the shoreline, facing the sea.
	CHECK(at(s, 0, -40.0f, 18.0f, BLD_DOCK) == Simulation::PLACE_OK);
	CHECK(at(s, 0, -30.0f, 40.0f, BLD_DOCK) == Simulation::PLACE_SHORE);   // inland
	CHECK(at(s, 0, -40.0f, 6.0f, BLD_DOCK) == Simulation::PLACE_TERRAIN);  // out at sea
	// A dock may stand on an ally's shore but not an enemy's.
	CHECK(at(s, 0, 40.0f, -18.0f, BLD_DOCK) == Simulation::PLACE_TERRITORY);
	s.set_mutual_ally(0, 1);
	CHECK(at(s, 0, 40.0f, -18.0f, BLD_DOCK) == Simulation::PLACE_OK);
	CHECK(at(s, 0, 40.0f, -50.0f, BLD_HOUSE) == Simulation::PLACE_TERRITORY); // houses still not
	s.set_diplomacy(0, 1, Simulation::DIPLO_ENEMY);
	s.set_diplomacy(1, 0, Simulation::DIPLO_ENEMY);

	// Obstruction: other buildings and resources block a foundation; a carcass does not.
	CHECK(at(s, 0, -40.0f, 40.0f, BLD_HOUSE) == Simulation::PLACE_OBSTRUCTED);
	s.spawn_resource_world(-20.0f, 50.0f, RES_WOOD, 200);
	CHECK(at(s, 0, -20.0f, 50.0f, BLD_HOUSE) == Simulation::PLACE_OBSTRUCTED);
	s.spawn_resource_world(-20.0f, 30.0f, RES_FOOD, 100, RES_SUB_MEAT);
	CHECK(at(s, 0, -20.0f, 30.0f, BLD_HOUSE) == Simulation::PLACE_OK);

	// Civic Centres 200 m (50 tiles) apart, measured between footprints, and in
	// own or neutral territory.
	CHECK(at(s, 0, 10.0f, 40.0f, BLD_TOWN_CENTER) == Simulation::PLACE_DISTANCE);
	CHECK(at(s, 0, -50.0f, -50.0f, BLD_TOWN_CENTER) == Simulation::PLACE_OK);
	CHECK(at(s, 1, -50.0f, -50.0f, BLD_TOWN_CENTER) == Simulation::PLACE_OK); // player 1's own CC is 90 away

	// Towers 60 m (15 tiles) from the player's own towers, foundations included.
	CHECK(at(s, 0, -30.0f, 30.0f, BLD_TOWER) == Simulation::PLACE_OK);
	s.spawn_building_world(-30.0f, 30.0f, BLD_TOWER, 0, 0.0f);
	CHECK(at(s, 0, -24.0f, 30.0f, BLD_TOWER) == Simulation::PLACE_DISTANCE);
	CHECK(at(s, 0, -24.0f, 30.0f, BLD_SENTRY_TOWER) == Simulation::PLACE_DISTANCE); // also a Tower
	CHECK(at(s, 0, -50.0f, 50.0f, BLD_TOWER) == Simulation::PLACE_OK);
	CHECK(at(s, 1, -24.0f, 30.0f, BLD_WALL_TOWER) == Simulation::PLACE_TERRITORY); // not the enemy's ground
	CHECK(at(s, 0, -24.0f, 30.0f, BLD_WALL_TOWER) == Simulation::PLACE_OK);         // wall towers aren't Towers

	// cmd_build enforces the rules and charges nothing for a refused site.
	const int wood = s.player_resource(0, RES_WOOD);
	CHECK(s.cmd_build(0, 15, 25, BLD_HOUSE) == 0); // neutral ground
	CHECK(s.player_resource(0, RES_WOOD) == wood);
	CHECK(s.cmd_build(0, -16, 24, BLD_HOUSE) != 0);

	std::printf("RESULT: %s (%d)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
