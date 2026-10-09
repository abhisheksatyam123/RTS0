// Naval tests: merchant ships trading dock to dock (0 A.D. Trader.js, Market.js
// TradeType "naval", Market.js CalculateTraderGain, the Carthaginian ship bonus).
//   g++ -std=c++20 -O2 -Isrc tools/test_naval.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//       src/network/net_host.cpp -lpthread -o /tmp/test_naval && /tmp/test_naval
// Private access only to put a test AI straight into the town phase.
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

// A sea across the middle of the map (|z| < 16) and a small lake in the
// north-east corner that no ship can reach from the sea.
static float height_at(float x, float z) {
	if (std::fabs(z) < 16.0f) return -3.0f;
	const float lx = x - 44.0f, lz = z - 44.0f;
	if (lx * lx + lz * lz < 9.0f * 9.0f) return -3.0f;
	return 3.0f;
}

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
	for (uint8_t p = 0; p < 4; ++p) {
		s.set_player_active(p, true, false);
		s.set_player_resources(p, 0, 0, 0, 0);
	}
	s.set_team_civ(0, CIV_CARTHAGINIANS);
	s.set_team_civ(1, CIV_ATHENIANS);
}

static void run(Simulation &s, float seconds) {
	for (int t = 0; t < (int)(seconds / Simulation::FIXED_STEP); ++t) s.tick(Simulation::FIXED_STEP);
}

static int total(const Simulation &s, uint8_t p) {
	int t = 0;
	for (int r = 0; r < RES_COUNT; ++r) t += s.player_resource(p, (uint8_t)r);
	return t;
}

static const Simulation::Building *bld(const Simulation &s, uint32_t id) {
	for (const auto &b : s.buildings()) if (b.id == id) return &b;
	return nullptr;
}

// globalscripts/Trade.js, written out independently of the sim.
static double expected_gain(double metres, double map_size, double mult) {
	auto curve = [&](double d2) { return d2 / (1.0 + 0.25 * std::sqrt(d2) / map_size); };
	const double norm = std::sqrt(1024.0 / map_size) / curve(10000.0);
	return norm * mult * curve(metres * metres);
}

static void test_dock_trade() {
	static Simulation s;
	fresh(s);
	// Two docks on opposite shores of the sea, one on the lake, and a market.
	const uint32_t west = s.spawn_building_world(-48.0f, 20.0f, BLD_DOCK, 0);
	const uint32_t east = s.spawn_building_world(48.0f, -20.0f, BLD_DOCK, 0);
	const uint32_t lake = s.spawn_building_world(44.0f, 56.0f, BLD_DOCK, 0);
	const uint32_t market = s.spawn_building_world(-40.0f, -40.0f, BLD_MARKET, 0);
	const Simulation::Building *bw = bld(s, west), *be = bld(s, east), *bl = bld(s, lake), *bm = bld(s, market);
	CHECK(bw && be && bl && bm);
	if (!bw || !be || !bl || !bm) return;

	// Market TradeType: ships trade at docks, caravans at markets.
	CHECK(s.can_trade_at(0, *bw, UNIT_MERCHANT_SHIP));
	CHECK(!s.can_trade_at(0, *bw, UNIT_TRADER));
	CHECK(s.can_trade_at(0, *bm, UNIT_TRADER));
	CHECK(!s.can_trade_at(0, *bm, UNIT_MERCHANT_SHIP));
	// The sea docks share a sea, the lake dock doesn't.
	CHECK(s.dock_sea(*bw) != 0 && s.dock_sea(*bw) == s.dock_sea(*be));
	CHECK(s.dock_sea(*bl) != 0 && s.dock_sea(*bl) != s.dock_sea(*bw));
	CHECK(s.has_trade_route(0, UNIT_MERCHANT_SHIP));
	CHECK(!s.has_trade_route(0, UNIT_TRADER)); // one market only

	// Gain: Trade.js with Trader/GainMultiplier 0.75 and Carthage's +25% on ships only.
	const double map_size = 2.0 * HALF * 4.0;
	const double metres = std::hypot((double)bw->x - be->x, (double)bw->z - be->z) * 4.0;
	const int cart_ship = s.calculate_trade_gain(*bw, *be, 0, UNIT_MERCHANT_SHIP).trader;
	const int cart_land = s.calculate_trade_gain(*bw, *be, 0, UNIT_TRADER).trader;
	const int athen_ship = s.calculate_trade_gain(*bw, *be, 1, UNIT_MERCHANT_SHIP).trader;
	CHECK(cart_ship == (int)std::lround(expected_gain(metres, map_size, 0.75 * 1.25)));
	CHECK(cart_land == (int)std::lround(expected_gain(metres, map_size, 0.75)));
	CHECK(athen_ship == cart_land);
	std::printf("dock route %.0f m: Carthaginian ship %d, caravan %d, Athenian ship %d\n", metres, cart_ship, cart_land, athen_ship);

	// A merchant ship in the sea finds the route itself and is paid on each arrival.
	const uint32_t ship = s.spawn_unit(-40.0f, 8.0f, 0, UNIT_MERCHANT_SHIP);
	const uint32_t caravan = s.spawn_unit(-30.0f, -40.0f, 0, UNIT_TRADER);
	run(s, 240.0f);
	const int earned = total(s, 0);
	std::printf("Carthaginian merchant ship earned %d in 240 s\n", earned);
	CHECK(earned >= cart_ship);
	CHECK(earned % cart_ship == 0); // only whole dock-to-dock deliveries, never the lake
	// The caravan has no second market, so it never trades at a dock.
	const auto &u = s.units();
	for (size_t i = 0; i < u.count; ++i) {
		if (u.id[i] == caravan) CHECK(u.action_state[i] != ACTION_TRADE);
		if (u.id[i] == ship) {
			CHECK(u.action_state[i] == ACTION_TRADE);
			auto [cx, cy] = s.nav_grid().world_to_cell(u.pos_x[i], u.pos_z[i]);
			CHECK(s.nav_grid().is_passable_for(cx, cy, PASS_SHIP)); // stayed on the water
		}
	}

	// An enemy's dock ends the route (Trader.js CanTrade).
	static Simulation t;
	fresh(t);
	const uint32_t a = t.spawn_building_world(-48.0f, 20.0f, BLD_DOCK, 0);
	const uint32_t b = t.spawn_building_world(48.0f, -20.0f, BLD_DOCK, 1);
	t.spawn_unit(-40.0f, 8.0f, 0, UNIT_MERCHANT_SHIP);
	CHECK(t.is_hostile(0, 1));
	CHECK(!t.has_trade_route(0, UNIT_MERCHANT_SHIP));
	run(t, 60.0f);
	CHECK(total(t, 0) == 0 && total(t, 1) == 0);
	(void)a; (void)b;
}

// Petra (tradeManager.js) trades by sea when it has docks on one sea and no land route.
static void test_petra_sea_trade() {
	static Simulation s;
	fresh(s);
	s.set_player_active(1, true, true);
	s.set_player_resources(1, 2000, 2000, 2000, 2000);
	s.players_[1].phase = 2;
	s.spawn_building_world(-48.0f, 20.0f, BLD_DOCK, 1);
	s.spawn_building_world(48.0f, -20.0f, BLD_DOCK, 1);
	s.spawn_building_world(-40.0f, 40.0f, BLD_TOWN_CENTER, 1);
	run(s, 180.0f);
	int ships = 0, trading = 0;
	const auto &u = s.units();
	for (size_t i = 0; i < u.count; ++i) {
		if (!u.alive[i] || u.team[i] != 1 || u.unit_type[i] != UNIT_MERCHANT_SHIP) continue;
		++ships;
		if (u.action_state[i] == ACTION_TRADE) ++trading;
	}
	std::printf("Petra: %d merchant ships, %d trading\n", ships, trading);
	// Every ship launches on the water and trades; Petra trains no more than its trader target.
	CHECK(ships >= 1 && trading == ships);
	CHECK(ships <= s.petra_config().target_num_traders + 1);
}

static const uint8_t *subtype_of(const Simulation &s, uint32_t rid, int32_t *amount = nullptr) {
	const auto r = s.resources();
	for (size_t i = 0; i < r.count; ++i)
		if (r.id[i] == rid) { if (amount) *amount = r.amount[i]; return &r.subtype[i]; }
	return nullptr;
}
static uint32_t gather_target(const Simulation &s, uint32_t uid) {
	const auto &u = s.units();
	for (size_t i = 0; i < u.count; ++i)
		if (u.id[i] == uid) return u.action_state[i] == ACTION_GATHER || u.action_state[i] == ACTION_RETURN_DROPSITE ? u.target_resource_id[i] : 0;
	return 0;
}

// Fishing (template_gaia_fish.xml, template_unit_ship_fishing.xml): only boats fish,
// at most 4 per fish, catches go to a dock, and fish regrow while left alone.
static void test_fishing() {
	static Simulation s;
	fresh(s);
	s.spawn_building_world(-48.0f, 20.0f, BLD_DOCK, 0);
	// A Town Center nearer the fish than the dock: a boat can't reach it, so it must not unload there.
	s.spawn_building_world(-30.0f, 24.0f, BLD_TOWN_CENTER, 0);
	const uint32_t fish_a = s.spawn_resource_world(-36.0f, 6.0f, RES_FOOD, FISH_FOOD_SUPPLY, RES_SUB_FISH);
	const uint32_t fish_b = s.spawn_resource_world(-24.0f, 6.0f, RES_FOOD, FISH_FOOD_SUPPLY, RES_SUB_FISH);
	const uint32_t lone = s.spawn_resource_world(30.0f, -6.0f, RES_FOOD, 500, RES_SUB_FISH);
	const uint32_t berries = s.spawn_resource_world(-40.0f, 30.0f, RES_FOOD, 300);
	std::vector<uint32_t> boats;
	for (int k = 0; k < 6; ++k) boats.push_back(s.spawn_unit(-46.0f + 2.0f * k, 10.0f, 0, UNIT_FISHING_BOAT));
	const uint32_t villager = s.spawn_unit(-36.0f, 30.0f, 0, UNIT_VILLAGER);

	// Gather rates: boats only fish, villagers never do.
	s.order_gather(&villager, 1, fish_a);
	CHECK(gather_target(s, villager) == 0);
	s.order_gather(&boats[0], 1, berries);
	CHECK(gather_target(s, boats[0]) == 0);

	s.order_gather(boats.data(), boats.size(), fish_a);
	run(s, 30.0f);
	int on_a = 0, on_b = 0;
	for (uint32_t b : boats) { const uint32_t t = gather_target(s, b); on_a += t == fish_a; on_b += t == fish_b; }
	std::printf("fishing: %d boats on fish A, %d moved on to fish B\n", on_a, on_b);
	CHECK(on_a <= FISH_MAX_GATHERERS && on_a + on_b == 6 && on_b >= 2);

	run(s, 90.0f);
	const int food = s.player_resource(0, RES_FOOD);
	int32_t left_a = 0;
	subtype_of(s, fish_a, &left_a);
	std::printf("fishing: %d food landed at the dock in 120 s, fish A down to %d\n", food, left_a);
	CHECK(food > 0 && left_a < FISH_FOOD_SUPPLY);
	// Nothing caught was dropped at the Town Center: every unit of food came off fish A or B.
	int32_t left_b = 0;
	subtype_of(s, fish_b, &left_b);
	int carried = 0;
	for (size_t i = 0; i < s.units().count; ++i) if (s.units().unit_type[i] == UNIT_FISHING_BOAT) carried += s.units().carry[i];
	// Fish regrow while their boats are away unloading, so gathered food can exceed the supply drop.
	CHECK(food + carried >= (FISH_FOOD_SUPPLY - left_a) + (FISH_FOOD_SUPPLY - left_b));

	// The untouched fish regrows 1 food every 2 s: +60 in 120 s.
	int32_t lone_amount = 0;
	const uint8_t *sub = subtype_of(s, lone, &lone_amount);
	CHECK(sub && *sub == RES_SUB_FISH && lone_amount == 560);

	// A boat whose fish runs out moves on to the nearest fish on its sea.
	static Simulation t;
	fresh(t);
	t.spawn_building_world(-48.0f, 20.0f, BLD_DOCK, 0);
	const uint32_t small = t.spawn_resource_world(-36.0f, 6.0f, RES_FOOD, 16, RES_SUB_FISH);
	const uint32_t next = t.spawn_resource_world(-20.0f, 4.0f, RES_FOOD, FISH_FOOD_SUPPLY, RES_SUB_FISH);
	const uint32_t boat = t.spawn_unit(-40.0f, 8.0f, 0, UNIT_FISHING_BOAT);
	t.order_gather(&boat, 1, small);
	run(t, 40.0f);
	CHECK(subtype_of(t, small) == nullptr); // fished out
	CHECK(gather_target(t, boat) == next);
	(void)lone;
}

// Ships float (0 A.D. Position FloatOnWater): range and launch height come from the water
// surface, not the seabed. A trireme just off the shore wins against a lone archer on the beach.
static void test_ship_height() {
	static Simulation s;
	fresh(s);
	s.set_victory_condition(VICTORY_ENDLESS);
	const uint32_t tri = s.spawn_unit(0.0f, 12.0f, 0, UNIT_TRIREME);
	const uint32_t archer = s.spawn_unit(0.0f, 18.0f, 1, UNIT_ARCHER);
	auto idx = [&](uint32_t id) { for (size_t i = 0; i < s.units().count; ++i) if (s.units().id[i] == id) return (int)i; return -1; };
	CHECK(s.unit_height((size_t)idx(tri)) == s.water_height());
	run(s, 30.0f);
	const int t = idx(tri), a = idx(archer);
	const bool tri_alive = t >= 0 && s.units().alive[t];
	const bool archer_alive = a >= 0 && s.units().alive[a];
	std::printf("ship height: trireme %s, archer %s\n", tri_alive ? "alive" : "sunk", archer_alive ? "alive" : "dead");
	CHECK(tri_alive && !archer_alive);
}

int main() {
	test_dock_trade();
	test_fishing();
	test_petra_sea_trade();
	test_ship_height();
	std::printf(failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", failures);
	return failures ? 1 : 0;
}
