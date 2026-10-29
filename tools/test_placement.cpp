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
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace rtso;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static constexpr float HALF = 64.0f;
static constexpr int RES = 65;

// A sea across the middle of the map (|z| < 16.5), land north and south of it, the shores
// sloping so a dock can stand half in the water (0.28 CheckShorePlacement).
static float height_at(float, float z) { return std::clamp(std::fabs(z) - 16.5f, -3.0f, 3.0f); }

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
	s.set_explore_map(true); // the seated players test placement rules, not the shroud
}

// The cell a building of `type` centred on (x, z) is placed at.
static Simulation::PlacementResult at(const Simulation &s, uint8_t player, float x, float z, uint8_t type) {
	const float half = building_type_info(type).footprint * Simulation::GRID * 0.5f;
	return s.check_placement(player, (int)std::floor((x - half) / Simulation::GRID),
			(int)std::floor((z - half) / Simulation::GRID), type);
}

static float seconds_until(Simulation &s, uint32_t bid, bool (*done)(const Simulation::Building &)) {
	for (int t = 0; t < 20 * 600; ++t) {
		for (const auto &b : s.buildings())
			if (b.id == bid && done(b)) return t * Simulation::FIXED_STEP;
		s.tick(Simulation::FIXED_STEP);
	}
	return -1.0f;
}

// Foundation.js / Repairable.js: n builders work at n^0.7 / n each, and a repair
// takes RepairTimeRatio (2) x the build time for the missing health.
static void test_build_and_repair() {
	static Simulation s;
	const float build_time = civ_building_type_info(s.player_civ(0), BLD_HOUSE).build_time; // athen: the big house
	float took[2] = {};
	for (int k = 0; k < 2; ++k) {
		const int builders = k ? 3 : 1;
		fresh(s);
		s.spawn_building_world(-40.0f, 40.0f, BLD_TOWN_CENTER, 0, 1.0f);
		const uint32_t house = s.spawn_building_world(-30.0f, 50.0f, BLD_HOUSE, 0, 0.0f);
		std::vector<uint32_t> ids;
		for (int b = 0; b < builders; ++b) ids.push_back(s.spawn_unit(-31.0f + b, 52.5f, 0, UNIT_FEMALE_CITIZEN));
		s.order_build_or_repair(ids.data(), ids.size(), house);
		// Timed from the first work: the builders walk up to Builder range first.
		seconds_until(s, house, [](const Simulation::Building &b) { return b.build_progress > 0.0f; });
		took[k] = seconds_until(s, house, [](const Simulation::Building &b) { return b.build_progress >= 1.0f; });
	}
	const float speedup = took[0] / took[1];
	std::printf("  house: %.1f s with one builder (template %.0f s), %.1f s with three (x%.2f, 0 A.D. x%.2f)\n",
	            took[0], build_time, took[1], speedup, std::pow(3.0f, 0.7f));
	CHECK(std::fabs(took[0] - build_time) < 1.5f);
	CHECK(std::fabs(speedup - std::pow(3.0f, 0.7f)) < 0.15f);

	// Repair half a house's health with one builder: 0.5 x 2 x build time.
	fresh(s);
	s.spawn_building_world(-40.0f, 40.0f, BLD_TOWN_CENTER, 0, 1.0f);
	const uint32_t house = s.spawn_building_world(-30.0f, 50.0f, BLD_HOUSE, 0, 1.0f);
	for (auto &b : s.buildings_) if (b.id == house) b.hp = b.max_hp * 0.5f;
	const uint32_t woman = s.spawn_unit(-31.0f, 52.5f, 0, UNIT_FEMALE_CITIZEN);
	s.order_build_or_repair(&woman, 1, house);
	const float repair = seconds_until(s, house, [](const Simulation::Building &b) { return b.hp >= b.max_hp; });
	std::printf("  house repair from half health: %.1f s (0 A.D. %.0f s)\n", repair, build_time);
	CHECK(std::fabs(repair - build_time) < 1.5f);
}

// Placement rotation (0 A.D. placementSupport.angle): cmd_build keeps the angle it is
// given, lockstep carries it in 1/65536ths of a turn, and docks face the open water
// (Commands.js GetDockAngle).
static void test_rotation() {
	static Simulation s;
	constexpr float PI = 3.14159265f;
	auto same_angle = [](float a, float b) {
		const float d = std::remainder(a - b, 2.0f * PI);
		return std::fabs(d) < 0.01f;
	};
	CHECK(same_angle(Simulation::unpack_angle(Simulation::pack_angle(1.25f)), 1.25f));
	CHECK(Simulation::pack_angle(Simulation::DEFAULT_BUILDING_ROT) == 0); // older commands keep the default
	fresh(s);
	s.spawn_building_world(-40.0f, 40.0f, BLD_TOWN_CENTER, 0, 1.0f);
	s.rebuild_territory();
	const uint32_t house = s.cmd_build(0, -16, 24, BLD_HOUSE, 0.5f * PI);
	const uint32_t plain = s.cmd_build(0, -24, 24, BLD_HOUSE);
	// The sea is south of player 0's shore: a dock there faces -z (0 A.D. angle pi),
	// whatever angle it was asked for.
	const auto &dinfo = building_type_info(BLD_DOCK);
	const float dhalf = dinfo.footprint * Simulation::GRID * 0.5f;
	const uint32_t dock = s.cmd_build(0, (int)std::floor((-40.0f - dhalf) / Simulation::GRID),
			(int)std::floor((18.0f - dhalf) / Simulation::GRID), BLD_DOCK, 0.0f);
	CHECK(house && plain && dock);
	for (const auto &b : s.buildings()) {
		if (b.id == house) CHECK(same_angle(b.rot, 0.5f * PI));
		if (b.id == plain) CHECK(same_angle(b.rot, Simulation::DEFAULT_BUILDING_ROT));
		if (b.id == dock) {
			std::printf("  dock on the north shore faces %.2f rad (sea to the south: pi)\n", b.rot);
			CHECK(same_angle(b.rot, PI));
		}
	}
	CHECK(std::isnan(s.dock_angle(-30.0f, 50.0f, dhalf))); // inland: no shoreline in reach
}

// Foundation.js Commit / UnitAI LeaveFoundation: units standing on a foundation walk off
// it before work starts and then carry on with their order. Enemies make way too
// (WillMoveFromFoundation: a structure has no UnitAI); one that can't move keeps work
// from starting. Once committed, it blocks movement.
static void test_foundation_clearing() {
	static Simulation s;
	auto bld = [&](uint32_t id) -> const Simulation::Building & {
		for (const auto &b : s.buildings()) if (b.id == id) return b;
		return s.buildings().front();
	};
	auto on_site = [&](uint32_t uid, const Simulation::Building &b) {
		const int32_t i = s.lookup_unit(uid);
		return std::fabs(s.units_.pos_x[i] - b.x) < b.half_size && std::fabs(s.units_.pos_z[i] - b.z) < b.half_size;
	};
	fresh(s);
	s.spawn_building_world(-40.0f, 40.0f, BLD_TOWN_CENTER, 0, 1.0f);
	const uint32_t house = s.spawn_building_world(-30.0f, 50.0f, BLD_HOUSE, 0, 0.0f);
	const uint32_t idler = s.spawn_unit(-30.0f, 50.0f, 0, UNIT_SPEARMAN);
	const uint32_t woman = s.spawn_unit(-31.0f, 52.5f, 0, UNIT_FEMALE_CITIZEN);
	s.order_build_or_repair(&woman, 1, house);
	const float took = seconds_until(s, house, [](const Simulation::Building &b) { return b.build_progress >= 1.0f; });
	std::printf("  house with an own unit on the site: built in %.1f s\n", took);
	CHECK(took > 0.0f);
	CHECK(!on_site(idler, bld(house)));

	// A busy unit on the site walks off and then resumes its order (PushOrderFront).
	const uint32_t barracks = s.spawn_building_world(-20.0f, 50.0f, BLD_BARRACKS, 0, 0.0f);
	const uint32_t patroller = s.spawn_unit(-20.0f, 50.5f, 0, UNIT_SPEARMAN);
	s.order_patrol(&patroller, 1, -20.0f, 60.0f);
	const int32_t pi = s.lookup_unit(patroller);
	s.leave_foundation((size_t)pi, bld(barracks));
	const auto *q = s.unit_order_queue(patroller);
	CHECK(s.units_.order_kind[pi] == ORDER_WALK && q && !q->empty() && q->front().kind == ORDER_PATROL);
	for (int t = 0; t < 20 * 5 && on_site(patroller, bld(barracks)); ++t) s.tick(Simulation::FIXED_STEP);
	for (int t = 0; t < 20 && s.units_.order_kind[pi] != ORDER_PATROL; ++t) s.tick(Simulation::FIXED_STEP);
	CHECK(!on_site(patroller, bld(barracks)));
	CHECK(s.units_.order_kind[pi] == ORDER_PATROL);

	// An enemy on the site walks off it too (no Civic Centre here to shoot it).
	fresh(s);
	const uint32_t h2 = s.spawn_building_world(-30.0f, 50.0f, BLD_HOUSE, 0, 0.0f);
	const uint32_t enemy = s.spawn_unit(-30.0f, 50.0f, 1, UNIT_FEMALE_CITIZEN);
	s.order_set_stance(&enemy, 1, STANCE_PASSIVE);
	const uint32_t w2 = s.spawn_unit(-31.0f, 52.5f, 0, UNIT_FEMALE_CITIZEN);
	s.order_set_stance(&w2, 1, STANCE_PASSIVE);
	const int32_t ei = s.lookup_unit(enemy);
	const float enemy_speed = s.units_.speed[ei];
	s.units_.speed[ei] = 0.0f; // one that can't move stays, and work waits
	s.order_build_or_repair(&w2, 1, h2);
	for (int t = 0; t < 20 * 5; ++t) s.tick(Simulation::FIXED_STEP);
	CHECK(on_site(enemy, bld(h2)));
	CHECK(!bld(h2).committed && bld(h2).build_progress == 0.0f);
	const auto [cx, cy] = s.nav_grid().world_to_cell(-30.0f, 50.0f);
	CHECK(s.nav_grid().cells.at(cx, cy) != NAV_OBSTRUCTED);
	s.units_.speed[ei] = enemy_speed;
	for (int t = 0; t < 20 * 10 && !bld(h2).committed; ++t) s.tick(Simulation::FIXED_STEP);
	CHECK(!on_site(enemy, bld(h2)));
	CHECK(bld(h2).committed);
	CHECK(s.nav_grid().cells.at(cx, cy) == NAV_OBSTRUCTED); // committed: units path around it

	// LeaveFoundation goes anywhere clear of the site: a unit whose nearest way off is where
	// another stands idle takes the next free point, so work still starts.
	fresh(s);
	const uint32_t h3 = s.spawn_building_world(-30.0f, 50.0f, BLD_HOUSE, 0, 0.0f);
	const float edge = bld(h3).hv;
	const uint32_t sitter = s.spawn_unit(-30.0f, 50.0f + edge + 0.6f, 0, UNIT_SPEARMAN); // just off the north side
	const uint32_t stuck = s.spawn_unit(-30.0f, 50.0f + edge - 0.4f, 0, UNIT_SPEARMAN);  // on the site, nearest that side
	const uint32_t w3 = s.spawn_unit(-33.0f, 50.0f - edge - 1.5f, 0, UNIT_FEMALE_CITIZEN);
	s.order_build_or_repair(&w3, 1, h3);
	for (int t = 0; t < 20 * 15 && !bld(h3).committed; ++t) s.tick(Simulation::FIXED_STEP);
	std::printf("  house with its nearest exit taken by an idle unit: committed %s\n", bld(h3).committed ? "yes" : "no");
	CHECK(bld(h3).committed && !on_site(stuck, bld(h3)) && !on_site(sitter, bld(h3)));
}

// CheckFoundation tests a foundation's turned square against the turned squares of the standing
// structures (CCmpObstructionManager::TestStaticShape), and a destroyed structure's rubble has no
// Obstruction: its ground is free to build on again.
static void test_turned_and_freed_ground() {
	static Simulation s;
	constexpr float QUARTER_PI = 0.785398163f;
	const float diagonal = Simulation::DEFAULT_BUILDING_ROT + QUARTER_PI;
	fresh(s);
	s.spawn_building_world(-40.0f, 40.0f, BLD_TOWN_CENTER, 0, 1.0f);
	s.rebuild_territory();
	CHECK(building_type_info(BLD_HOUSE).footprint == 1); // the cells below are one house apart

	// A house turned 45 degrees reaches into the cells beside it.
	CHECK(s.cmd_build(0, -12, 20, BLD_HOUSE, diagonal) != 0);
	CHECK(s.check_placement(0, -11, 20, BLD_HOUSE) == Simulation::PLACE_OBSTRUCTED);
	CHECK(s.check_placement(0, -12, 21, BLD_HOUSE) == Simulation::PLACE_OBSTRUCTED);
	CHECK(s.check_placement(0, -11, 20, BLD_HOUSE, diagonal) == Simulation::PLACE_OBSTRUCTED);
	// Two turned houses on diagonal cells share cells but not ground: the second one fits.
	CHECK(s.check_placement(0, -11, 21, BLD_HOUSE, diagonal) == Simulation::PLACE_OK);
	CHECK(s.cmd_build(0, -11, 21, BLD_HOUSE, diagonal) != 0);
	// An unturned house on a diagonal cell clears the turned one's corners too.
	CHECK(s.check_placement(0, -13, 19, BLD_HOUSE) == Simulation::PLACE_OK);
	// A turned house is kept off water under its corners.
	CHECK(s.check_placement(0, -20, 8, BLD_HOUSE) == Simulation::PLACE_OK);
	CHECK(s.check_placement(0, -20, 8, BLD_HOUSE, diagonal) == Simulation::PLACE_TERRAIN);

	// A deleted foundation and a destroyed house free their ground.
	const uint32_t plot = s.cmd_build(0, -16, 24, BLD_HOUSE);
	const uint32_t house = s.spawn_building(-14, 24, BLD_HOUSE, 0, 1.0f);
	CHECK(plot && house);
	CHECK(s.check_placement(0, -16, 24, BLD_HOUSE) == Simulation::PLACE_OBSTRUCTED);
	CHECK(s.check_placement(0, -14, 24, BLD_HOUSE) == Simulation::PLACE_OBSTRUCTED);
	const uint32_t both[2] = {plot, house};
	s.cmd_delete(0, both, 2);
	for (int k = 0; k < 20; ++k) s.tick(Simulation::FIXED_STEP);
	CHECK(s.check_placement(0, -16, 24, BLD_HOUSE) == Simulation::PLACE_OK);
	CHECK(s.check_placement(0, -14, 24, BLD_HOUSE) == Simulation::PLACE_OK);
	CHECK(s.cmd_build(0, -16, 24, BLD_HOUSE) != 0);
	std::printf("  turned houses: a 45-degree house blocks the cells beside it, another fits on the diagonal; "
	            "a deleted foundation and a destroyed house free their ground\n");
}

// BuildRestrictions.js: own territory cut off from every civic centre (IsTerritoryBlinking)
// takes only what may also go on neutral land ("unconnected own").
static void test_unconnected_territory() {
	static Simulation s;
	fresh(s);
	s.spawn_building_world(-45.0f, 45.0f, BLD_TOWN_CENTER, 0, 1.0f);
	const uint32_t fort = s.spawn_building_world(45.0f, 45.0f, BLD_FORTRESS, 0, 1.0f);
	s.rebuild_territory();
	const bool cut_off = s.territory_owner_at(45.0f, 55.0f) == 0 && !s.territory_connected_at(45.0f, 55.0f);
	std::printf("fortress territory owner %d, connected %d\n", s.territory_owner_at(45.0f, 55.0f),
			(int)s.territory_connected_at(45.0f, 55.0f));
	CHECK(cut_off);
	CHECK(s.territory_connected_at(-45.0f, 55.0f));
	CHECK(at(s, 0, -35.0f, 55.0f, BLD_HOUSE) == Simulation::PLACE_OK);           // connected own
	CHECK(at(s, 0, 52.0f, 58.0f, BLD_HOUSE) == Simulation::PLACE_TERRITORY);     // unconnected own
	CHECK(at(s, 0, 52.0f, 58.0f, BLD_BARRACKS) == Simulation::PLACE_TERRITORY);
	CHECK(at(s, 0, 52.0f, 58.0f, BLD_OUTPOST) == Simulation::PLACE_OK);          // own neutral
	CHECK(at(s, 0, 52.0f, 58.0f, BLD_PALISADE) == Simulation::PLACE_OK);
	// The renderer's per-cell query agrees: the fortress's land blinks, the civic centre's does not.
	const auto &g = s.territory_grid();
	auto cell = [&](float x, float z, int &tx, int &tz) {
		tx = (int)((x + s.map_half()) / (s.map_half() * 2.0f) * (float)g.w);
		tz = (int)((z + s.map_half()) / (s.map_half() * 2.0f) * (float)g.h);
	};
	int fx, fz, cx, cz;
	cell(45.0f, 55.0f, fx, fz);
	cell(-45.0f, 55.0f, cx, cz);
	CHECK(s.territory_blinking_cell(fx, fz));
	CHECK(!s.territory_blinking_cell(cx, cz));
	CHECK(!s.territory_blinking_cell(g.w / 2, 0)); // unclaimed land never blinks
	CHECK(!s.territory_blinking_cell(-1, 0));
	(void)fort;
}

// rome/army_camp (neutral enemy, 80 m from an ArmyCamp), ptol/mercenary_camp (own neutral,
// 100 m from a MercenaryCamp), kush/camp_noba (own neutral), iber/monument (150 m from a
// Monument), maur/pillar_ashoka (75 m from a Pillar).
static void test_camps() {
	// TerritoryInfluence of the civ's own template: big houses 20 m at 40000, the Iberian tower 38 m.
	CHECK(std::fabs(structure_territory_influence(BLD_HOUSE, CIV_ATHENIANS).radius - 5.0f) < 1e-4f &&
		structure_territory_influence(BLD_HOUSE, CIV_ATHENIANS).weight == 40000);
	CHECK(std::fabs(structure_territory_influence(BLD_HOUSE, CIV_BRITONS).radius - 4.0f) < 1e-4f);
	CHECK(std::fabs(structure_territory_influence(BLD_TOWER, CIV_IBERIANS).radius - 9.5f) < 1e-4f);
	static Simulation s;
	fresh(s);
	s.spawn_building_world(-40.0f, 40.0f, BLD_TOWN_CENTER, 0, 1.0f);
	s.spawn_building_world(40.0f, -40.0f, BLD_TOWN_CENTER, 1, 1.0f);
	s.rebuild_territory();
	CHECK(at(s, 0, -30.0f, 50.0f, BLD_ARMY_CAMP) == Simulation::PLACE_TERRITORY); // own
	CHECK(at(s, 0, 30.0f, 50.0f, BLD_ARMY_CAMP) == Simulation::PLACE_OK);         // neutral
	CHECK(at(s, 0, 50.0f, -52.0f, BLD_ARMY_CAMP) == Simulation::PLACE_OK);        // enemy
	s.spawn_building_world(30.0f, 50.0f, BLD_ARMY_CAMP, 0, 0.0f);
	CHECK(at(s, 0, 50.0f, -52.0f, BLD_ARMY_CAMP) == Simulation::PLACE_OK);        // 100 away
	CHECK(at(s, 0, 50.0f, 40.0f, BLD_ARMY_CAMP) == Simulation::PLACE_DISTANCE);
	CHECK(at(s, 1, 50.0f, 40.0f, BLD_ARMY_CAMP) == Simulation::PLACE_OK);         // only one's own count
	CHECK(at(s, 0, -30.0f, 50.0f, BLD_MERCENARY_CAMP) == Simulation::PLACE_OK);
	CHECK(at(s, 0, 50.0f, 40.0f, BLD_MERCENARY_CAMP) == Simulation::PLACE_OK);
	CHECK(at(s, 0, 50.0f, -52.0f, BLD_MERCENARY_CAMP) == Simulation::PLACE_TERRITORY);
	s.spawn_building_world(-30.0f, 50.0f, BLD_MERCENARY_CAMP, 0, 0.0f);
	CHECK(at(s, 0, -8.0f, 52.0f, BLD_MERCENARY_CAMP) == Simulation::PLACE_DISTANCE);
	CHECK(at(s, 0, 50.0f, 40.0f, BLD_CAMP_NOBA) == Simulation::PLACE_OK);
	CHECK(at(s, 0, 50.0f, 40.0f, BLD_MONUMENT) == Simulation::PLACE_TERRITORY);
	CHECK(at(s, 0, -50.0f, 25.0f, BLD_MONUMENT) == Simulation::PLACE_OK);
	s.spawn_building_world(-50.0f, 25.0f, BLD_MONUMENT, 0, 0.0f);
	CHECK(at(s, 0, -20.0f, 30.0f, BLD_MONUMENT) == Simulation::PLACE_DISTANCE);
	// germ/totem keeps only 100 m from another Monument (122 m here).
	const auto civ0 = s.players_[0].civ;
	s.players_[0].civ = CIV_GERMANS;
	CHECK(at(s, 0, -20.0f, 30.0f, BLD_MONUMENT) == Simulation::PLACE_OK);
	CHECK(at(s, 0, -35.0f, 30.0f, BLD_MONUMENT) == Simulation::PLACE_DISTANCE);
	s.players_[0].civ = civ0;
	// Its own template: 800 health, 150 wood and 100 metal, Resistance 8/20/2, Loot 30 wood 20 metal.
	const BuildingTypeInfo totem = civ_building_type_info(CIV_GERMANS, BLD_MONUMENT);
	CHECK(totem.max_hp == 800.0f && totem.wood_cost == 150 && totem.gold_cost == 100 && totem.stone_cost == 0);
	CHECK(civ_building_type_info(CIV_IBERIANS, BLD_MONUMENT).stone_cost == 100);
	float armour[3];
	structure_armour(BLD_MONUMENT, CIV_GERMANS, armour);
	CHECK(armour[0] == 8.0f && armour[1] == 20.0f && armour[2] == 2.0f);
	structure_armour(BLD_MONUMENT, CIV_IBERIANS, armour);
	CHECK(armour[0] == 20.0f && armour[1] == 30.0f && armour[2] == 3.0f);
	const LootInfo loot = structure_loot(BLD_MONUMENT, CIV_GERMANS, 1.0f);
	CHECK(loot.wood == 30 && loot.metal == 20 && loot.stone == 0);
	// mace/arsenal and maur/elephant_stable are Town phase; everyone else's are City.
	CHECK(building_required_phase(BLD_ARSENAL, CIV_MACEDONIANS) == 2 && building_required_phase(BLD_ARSENAL, CIV_ROMANS) == 3);
	CHECK(building_required_phase(BLD_ELEPHANT_STABLE, CIV_MAURYANS) == 2 &&
		building_required_phase(BLD_ELEPHANT_STABLE, CIV_KUSHITES) == 3);
	CHECK(at(s, 0, -20.0f, 30.0f, BLD_PILLAR) == Simulation::PLACE_OK);
	s.spawn_building_world(-20.0f, 30.0f, BLD_PILLAR, 0, 0.0f);
	CHECK(at(s, 0, -5.0f, 40.0f, BLD_PILLAR) == Simulation::PLACE_DISTANCE);
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
	// Stone walls only in own territory until Athenian Long Walls (own neutral, long_walls.json).
	CHECK(at(s, 0, 30.0f, 50.0f, BLD_WALL_TOWER) == Simulation::PLACE_TERRITORY);
	CHECK(at(s, 0, 30.0f, 50.0f, BLD_WALL_SEGMENT) == Simulation::PLACE_TERRITORY);
	const TechSet techs0 = s.players_[0].techs;
	s.players_[0].techs.set(TECH_LONG_WALLS);
	CHECK(at(s, 0, 30.0f, 50.0f, BLD_WALL_TOWER) == Simulation::PLACE_OK);
	CHECK(at(s, 0, 30.0f, 50.0f, BLD_WALL_SEGMENT) == Simulation::PLACE_OK);
	CHECK(at(s, 0, 40.0f, -50.0f, BLD_WALL_TOWER) == Simulation::PLACE_TERRITORY); // never enemy ground
	CHECK(at(s, 0, 30.0f, 50.0f, BLD_HOUSE) == Simulation::PLACE_TERRITORY);       // walls only
	CHECK(tech_info(TECH_LONG_WALLS).researcher == BLD_PRYTANEION && tech_info(TECH_LONG_WALLS).stone_cost == 400);
	s.players_[0].techs = techs0;

	// Terrain: no land building on water, even in own territory.
	CHECK(at(s, 0, -40.0f, 10.0f, BLD_HOUSE) == Simulation::PLACE_TERRAIN);
	CHECK(at(s, 0, -40.0f, 10.0f, BLD_WALL_SEGMENT) == Simulation::PLACE_TERRAIN); // too deep for a wall

	// Docks: on the shoreline, facing the sea.
	CHECK(at(s, 0, -40.0f, 18.0f, BLD_DOCK) == Simulation::PLACE_OK);
	// brit/crannog: a Civic Center on the shore, own ally or neutral territory, 200 m from
	// the player's other Civic Centers.
	CHECK(at(s, 0, 40.0f, 18.0f, BLD_CRANNOG) == Simulation::PLACE_OK);         // neutral shore
	CHECK(at(s, 0, 40.0f, 40.0f, BLD_CRANNOG) == Simulation::PLACE_SHORE);      // inland
	CHECK(at(s, 0, -40.0f, 18.0f, BLD_CRANNOG) == Simulation::PLACE_DISTANCE);  // next to its Civic Center
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

	// Mines block foundations across their Obstruction/Static: 13 m for a large one, 7 m for a small
	// one or ruins, 22 x 14 m for the 48-gatherer temperate_large_02, turned as the world view draws
	// them. A house whose nearest point is 1.5 units (6 m) from a mine's centre lies inside a large
	// mine's square whichever way it is turned, and outside a small one's.
	{
		auto foot = [&](uint32_t id, nav::Footprint &f) { return s.mine_footprint(s.res_id_to_idx_.at(id), f); };
		nav::Footprint f;
		const uint32_t big = s.spawn_resource_world(-24.5f, 55.0f, RES_STONE, LARGE_MINE_SUPPLY);
		CHECK(foot(big, f) && f.hu == 13.0f / 8.0f && f.hv == 13.0f / 8.0f);
		CHECK(at(s, 0, -27.0f, 55.0f, BLD_HOUSE) == Simulation::PLACE_OBSTRUCTED);
		CHECK(at(s, 0, -29.0f, 55.0f, BLD_HOUSE) == Simulation::PLACE_OK);
		const auto [bcx, bcy] = s.nav_grid_.world_to_cell(-24.5f, 55.0f);
		CHECK(s.nav_grid_.cells.at(bcx, bcy) == NAV_OBSTRUCTED); // and long paths
		const uint32_t small = s.spawn_resource_world(-24.5f, 45.0f, RES_GOLD, MINE_SUPPLY);
		CHECK(foot(small, f) && f.hu == 7.0f / 8.0f && f.hv == 7.0f / 8.0f);
		CHECK(at(s, 0, -27.0f, 45.0f, BLD_HOUSE) == Simulation::PLACE_OK);
		CHECK(at(s, 0, -25.0f, 45.0f, BLD_HOUSE) == Simulation::PLACE_OBSTRUCTED);
		const uint32_t ruins = s.spawn_resource_world(-14.5f, 45.0f, RES_STONE, 500, RES_SUB_RUINS, 1);
		CHECK(foot(ruins, f) && f.hu == 7.0f / 8.0f && f.hv == 7.0f / 8.0f);
		s.set_resource_max_gatherers(big, 48);
		CHECK(foot(big, f) && std::max(f.hu, f.hv) == 22.0f / 8.0f && std::min(f.hu, f.hv) == 14.0f / 8.0f);
		CHECK(!foot(s.spawn_resource_world(-14.5f, 55.0f, RES_WOOD, 200), f)); // trees keep their small square
		// A worked-out mine frees its cells and navcells.
		s.set_resource_amount(big, 0);
		CHECK(at(s, 0, -27.0f, 55.0f, BLD_HOUSE) == Simulation::PLACE_OK);
		CHECK(s.nav_grid_.cells.at(bcx, bcy) == NAV_PASSABLE);
	}

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

	test_build_and_repair();
	test_rotation();
	test_foundation_clearing();
	test_turned_and_freed_ground();
	test_unconnected_territory();
	test_camps();

	std::printf("RESULT: %s (%d)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
