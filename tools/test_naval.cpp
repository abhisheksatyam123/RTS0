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
#include <cstring>
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
	CHECK(ships <= s.petra_target_traders() + 1);
}

// tradeManager.js updateTrader: a land trader whose land has no route asks for a ship
// (navalManager.requireTransport) to the nearer end of the best route on another land.
static void test_petra_trader_transport() {
	static Simulation s;
	fresh(s);
	s.set_player_active(1, true, true);
	s.set_player_resources(1, 5000, 5000, 5000, 5000);
	s.players_[1].phase = 2;
	s.spawn_building_world(-30.0f, 40.0f, BLD_TOWN_CENTER, 1);
	s.spawn_building_world(-50.0f, 52.0f, BLD_MARKET, 1);
	s.spawn_building_world(30.0f, 52.0f, BLD_MARKET, 1);
	const uint32_t trader = s.spawn_unit(-30.0f, -40.0f, 1, UNIT_TRADER);
	s.spawn_unit(-30.0f, -10.0f, 1, UNIT_TRIREME);
	bool planned = false;
	for (int t = 0; t < 40 && !planned; ++t) {
		run(s, 1.0f);
		for (const auto &tp : s.petra_transports_)
			if (tp.owner == PETRA_TRANSPORT_TRADER && std::find(tp.passenger_ids.begin(), tp.passenger_ids.end(), trader) != tp.passenger_ids.end())
				planned = true;
	}
	bool routed = false;
	for (int t = 0; t < 120 && !routed; ++t) {
		run(s, 1.0f);
		routed = s.trader_routes_.count(trader) && s.units_.action_state[s.lookup_unit(trader)] == ACTION_TRADE;
	}
	std::printf("Petra trader on the other land: transport %s, trading %s\n", planned ? "yes" : "no", routed ? "yes" : "no");
	CHECK(planned);
	CHECK(routed);
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
	// A boat still on its way doesn't stop the regrowth: only boats at the fish count
	// (ResourceSupply State notGathered reads activeGatherers).
	const uint32_t late = s.spawn_unit(-10.0f, -6.0f, 0, UNIT_FISHING_BOAT);
	s.order_gather(&late, 1, lone);
	run(s, 4.0f);
	subtype_of(s, lone, &lone_amount);
	const int32_t li = s.lookup_unit(late);
	std::printf("fishing: a boat sailing to the untouched fish (at x %.1f), which regrows to %d\n", s.units().pos_x[li], lone_amount);
	CHECK(gather_target(s, late) == lone && lone_amount == 562);

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

static uint16_t tmpl_of(const char *id) {
	for (uint16_t t = 0; t < CIV_UNIT_TEMPLATE_COUNT; ++t)
		if (std::strcmp(civ_unit_template(t).id, id) == 0) return t;
	return CIV_UNIT_TEMPLATE_NONE;
}

static bool aboard(const Simulation &s, uint32_t ship, uint32_t uid) {
	const auto *p = s.get_transport_passengers(ship);
	if (!p) return false;
	for (uint32_t id : *p) if (id == uid) return true;
	return false;
}

static int32_t idx(const Simulation &s, uint32_t id) {
	for (size_t i = 0; i < s.units().count; ++i) if (s.units().id[i] == id) return (int32_t)i;
	return -1;
}

// Ships as 0 A.D. GarrisonHolders (template_unit_ship*.xml): capacity and List per
// template, LoadingRange 10 m with Pickup, unload only onto dry ground beside the
// hull, and EjectClassesOnDestroy when a ship sinks.
static void test_transport() {
	static Simulation s;
	fresh(s);
	// Capacities: warship_arrow 30, warship_siege 50, warship_scout 10, merchant 15,
	// fishing boat 1, fire ship none.
	const uint32_t arrow = s.spawn_unit(-20.0f, 10.0f, 0, UNIT_TRIREME, 0.0f, tmpl_of("athen/ship_arrow"));
	const uint32_t siege = s.spawn_unit(-10.0f, 0.0f, 0, UNIT_TRIREME, 0.0f, tmpl_of("cart/ship_siege"));
	const uint32_t scout = s.spawn_unit(0.0f, 0.0f, 0, UNIT_TRIREME, 0.0f, tmpl_of("athen/ship_scout"));
	const uint32_t fire = s.spawn_unit(10.0f, 0.0f, 0, UNIT_TRIREME, 0.0f, tmpl_of("brit/ship_fire"));
	const uint32_t merchant = s.spawn_unit(20.0f, 0.0f, 0, UNIT_MERCHANT_SHIP);
	const uint32_t fisher = s.spawn_unit(30.0f, 0.0f, 0, UNIT_FISHING_BOAT);
	CHECK(s.get_transport_capacity(arrow) == 30);
	CHECK(s.get_transport_capacity(siege) == 50);
	CHECK(s.get_transport_capacity(scout) == 10);
	CHECK(s.get_transport_capacity(merchant) == 15);
	CHECK(s.get_transport_capacity(fisher) == 1);
	CHECK(!s.is_transport(fire));

	// Lists: cavalry boards a warship or a merchant ship but not a fishing boat;
	// siege boards an arrow ship but not a scout ship.
	const int32_t ia = idx(s, arrow), ism = idx(s, scout), im = idx(s, merchant), iff = idx(s, fisher);
	const uint32_t rider = s.spawn_unit(-20.0f, 17.5f, 0, UNIT_CAVALRY);
	const uint32_t ram = s.spawn_unit(-21.0f, 17.5f, 0, UNIT_SIEGE_RAM);
	const int32_t ir = idx(s, rider), irm = idx(s, ram);
	CHECK(s.can_board(ia, ir) && s.can_board(im, ir) && !s.can_board(iff, ir));
	CHECK(s.can_board(ia, irm) && !s.can_board(ism, irm));

	// Boarding from the beach: the arrow ship lies 10 m off the shore line (z = 16).
	const uint32_t spear = s.spawn_unit(-19.0f, 17.5f, 0, UNIT_SPEARMAN);
	const uint32_t party[] = {spear, rider};
	s.order_board_transport(party, 2, arrow);
	run(s, 3.0f);
	CHECK(aboard(s, arrow, spear) && aboard(s, arrow, rider));

	// Out at sea there is no shore to unload onto: everyone stays aboard.
	s.cmd_move(&arrow, 1, -20.0f, 0.0f);
	run(s, 8.0f);
	s.order_disembark_all(arrow);
	CHECK(s.get_transport_passenger_count(arrow) == 2);

	// Sunk at sea: everyone drowns.
	s.units_.hp[idx(s, arrow)] = 0.0f;
	s.units_.alive[idx(s, arrow)] = 0;
	run(s, 0.1f);
	CHECK(s.get_transport_passengers(arrow) == nullptr);
	CHECK(!s.units().alive[idx(s, spear)] && !s.units().alive[idx(s, rider)]);

	// Sunk beside the beach: the spearman swims ashore, the rider (not in
	// EjectClassesOnDestroy) goes down with the ship.
	const uint32_t ship2 = s.spawn_unit(-40.0f, 13.0f, 0, UNIT_TRIREME, 0.0f, tmpl_of("athen/ship_arrow"));
	const uint32_t spear2 = s.spawn_unit(-40.0f, 17.5f, 0, UNIT_SPEARMAN);
	const uint32_t rider2 = s.spawn_unit(-39.0f, 17.5f, 0, UNIT_CAVALRY);
	const uint32_t party2[] = {spear2, rider2};
	s.order_board_transport(party2, 2, ship2);
	run(s, 2.0f);
	CHECK(s.get_transport_passenger_count(ship2) == 2);
	s.units_.hp[idx(s, ship2)] = 0.0f;
	s.units_.alive[idx(s, ship2)] = 0;
	run(s, 0.1f);
	const int32_t isp2 = idx(s, spear2);
	std::printf("  sunk by the beach: spearman %s at z %.1f\n", isp2 >= 0 && s.units().alive[isp2] ? "ashore" : "drowned", isp2 >= 0 ? s.units().pos_z[isp2] : 0.0f);
	CHECK(isp2 >= 0 && s.units().alive[isp2] && s.get_terrain_height(s.units().pos_x[isp2], s.units().pos_z[isp2]) > 0.15f);
	CHECK(!s.units().alive[idx(s, rider2)]);

	// Pickup: an idle ship sails toward units ordered aboard from inland.
	const uint32_t ship3 = s.spawn_unit(40.0f, -2.0f, 0, UNIT_TRIREME, 0.0f, tmpl_of("athen/ship_arrow"));
	const uint32_t archer = s.spawn_unit(40.0f, 24.0f, 0, UNIT_ARCHER);
	s.order_board_transport(&archer, 1, ship3);
	run(s, 15.0f);
	std::printf("  pickup: ship at z %.1f, archer %s\n", s.units().pos_z[idx(s, ship3)], aboard(s, ship3, archer) ? "aboard" : "ashore");
	CHECK(aboard(s, ship3, archer));
	// Unloading beside the beach puts the archer back on dry ground.
	s.order_disembark_all(ship3);
	const int32_t iar = idx(s, archer);
	CHECK(!aboard(s, ship3, archer) && s.units().alive[iar] && s.get_terrain_height(s.units().pos_x[iar], s.units().pos_z[iar]) > 0.15f);
	CHECK(s.units().pos_z[idx(s, ship3)] < 16.0f); // the ship stayed on the water
	// A full load comes ashore spread out: PickSpawnPoint gives each passenger a spot nobody
	// stands on, so no two bodies overlap as they land.
	uint32_t crew[8];
	for (int k = 0; k < 8; ++k) crew[k] = s.spawn_unit(36.0f + (float)k, 18.0f, 0, UNIT_SPEARMAN);
	s.order_board_transport(crew, 8, ship3);
	run(s, 10.0f);
	CHECK(s.get_transport_passenger_count(ship3) == 8);
	s.order_disembark_all(ship3);
	int overlaps = 0, wet = 0;
	for (int a = 0; a < 8; ++a) {
		const int32_t ia2 = idx(s, crew[a]);
		if (s.get_terrain_height(s.units().pos_x[ia2], s.units().pos_z[ia2]) <= 0.15f) ++wet;
		for (int b = a + 1; b < 8; ++b) {
			const int32_t ib = idx(s, crew[b]);
			const float d = std::hypot(s.units().pos_x[ia2] - s.units().pos_x[ib], s.units().pos_z[ia2] - s.units().pos_z[ib]);
			if (d < 2.0f * unit_collision_radius(UNIT_SPEARMAN) - 1e-3f) ++overlaps;
		}
	}
	std::printf("  8 passengers ashore: %d overlapping pairs, %d in the water\n", overlaps, wet);
	CHECK(overlaps == 0 && wet == 0);
	// A crowded shore: PickSpawnPoint finds no free spot, so the passengers stay aboard (the
	// unforced Eject), and when the ship sinks EjectOrKill drowns the ones it could not eject.
	const uint32_t pair[] = {crew[0], crew[1]};
	s.order_board_transport(pair, 2, ship3);
	run(s, 10.0f);
	CHECK(s.get_transport_passenger_count(ship3) == 2);
	const float sx3 = s.units().pos_x[idx(s, ship3)], sz3 = s.units().pos_z[idx(s, ship3)];
	const float crowd_r = Simulation::SHIP_LOADING_RANGE + unit_collision_radius(UNIT_TRIREME) + 2.0f;
	int crowd = 0;
	for (float dx = -crowd_r; dx <= crowd_r; dx += 0.75f)
		for (float dz = -crowd_r; dz <= crowd_r; dz += 0.75f) {
			if (dx * dx + dz * dz > crowd_r * crowd_r) continue;
			s.spawn_unit(sx3 + dx, sz3 + dz, 0, UNIT_SPEARMAN);
			++crowd;
		}
	s.order_disembark_all(ship3);
	std::printf("  crowded shore (%d units): %zu passengers stay aboard\n", crowd, s.get_transport_passenger_count(ship3));
	CHECK(s.get_transport_passenger_count(ship3) == 2);
	s.order_disembark_single(ship3, crew[0]);
	CHECK(aboard(s, ship3, crew[0]));
	s.units_.hp[idx(s, ship3)] = 0.0f;
	s.units_.alive[idx(s, ship3)] = 0;
	run(s, 0.1f);
	CHECK(s.get_transport_passengers(ship3) == nullptr);
	CHECK(!s.units().alive[idx(s, crew[0])] && !s.units().alive[idx(s, crew[1])]);
	std::printf("transport: capacities, lists, pickup, unloading and sinking match 0 A.D.: %s\n", failures ? "see failures" : "ok");
}

// Deep water is impassable for land units (passability classes): a walk out to sea stops
// on the beach (MakeGoalReachable), a player's or the AI's alike, and a unit boarding a busy ship waits on the shore
// until the ship comes within LoadingRange.
static void test_no_wading() {
	static Simulation s;
	fresh(s);
	auto deep = [&](uint32_t uid) {
		const int32_t i = idx(s, uid);
		auto [cx, cy] = s.nav_grid().world_to_cell(s.units().pos_x[i], s.units().pos_z[i]);
		return s.nav_grid().cells.at(cx, cy) == NAV_WATER;
	};
	const uint32_t walker = s.spawn_unit(-20.0f, 30.0f, 0, UNIT_SPEARMAN);
	s.order_walk(&walker, 1, -20.0f, 0.0f, false, true); // a player's walk
	const uint32_t ai_walker = s.spawn_unit(0.0f, 30.0f, 0, UNIT_SPEARMAN);
	s.cmd_move(&ai_walker, 1, 0.0f, 0.0f); // Petra's walks go through cmd_move
	const uint32_t ship = s.spawn_unit(-40.0f, 0.0f, 0, UNIT_TRIREME, 0.0f, tmpl_of("athen/ship_arrow"));
	s.cmd_move(&ship, 1, -40.0f, -8.0f); // busy: no Pickup
	const uint32_t boarder = s.spawn_unit(-40.0f, 30.0f, 0, UNIT_SPEARMAN);
	s.order_board_transport(&boarder, 1, ship);
	bool waded = false;
	for (int t = 0; t < (int)(15.0f / Simulation::FIXED_STEP); ++t) {
		s.tick(Simulation::FIXED_STEP);
		waded = waded || deep(walker) || deep(ai_walker) || (!aboard(s, ship, boarder) && deep(boarder));
	}
	const int32_t w = idx(s, walker);
	std::printf("  walk to sea stopped at (%.1f, %.1f); boarder waits at z %.1f\n",
			s.units().pos_x[w], s.units().pos_z[w], s.units().pos_z[idx(s, boarder)]);
	CHECK(!waded);
	CHECK(std::fabs(s.units().pos_x[w] + 20.0f) < 2.0f && s.units().pos_z[w] > 10.0f && !s.unit_has_order(walker));
	CHECK(s.units().pos_z[idx(s, ai_walker)] > 10.0f);
	CHECK(!aboard(s, ship, boarder));
	// The ship comes in to the beach: the waiting unit boards.
	s.cmd_move(&ship, 1, -40.0f, 12.0f);
	run(s, 10.0f);
	CHECK(aboard(s, ship, boarder));
}

// A ship moored beside a house on the shore still sails off: its path starts on
// the centre of its own cell, closer to the house than the ship can stand, which
// move_units' crowded-waypoint skip passes over for ships too (seen on an island
// map: Petra's transport sat beside a house for the rest of the game).
static void test_ship_leaves_shore_house() {
	// The spot from the bench: the Tiny continental map of seed 7 is two islands.
	static Simulation s;
	s.reset();
	s.set_sim_seed(7);
	s.set_waves_enabled(false);
	s.generate_random_map(MAP_RANDOM_CONTINENTAL, 7, 2, 64.0f);
	s.spawn_building_world(-15.0f, -15.0f, BLD_HOUSE, 1, 1.0f);
	s.spawn_building_world(-19.0f, -13.0f, BLD_HOUSE, 1, 1.0f);
	const uint32_t ship = s.spawn_unit(-12.439f, -12.439f, 1, UNIT_TRIREME, 0.0f);
	s.cmd_move(&ship, 1, 13.0f, 13.0f);
	run(s, 60.0f);
	const int32_t i = idx(s, ship);
	const float moved = std::hypot(s.units().pos_x[i] + 12.439f, s.units().pos_z[i] + 12.439f);
	std::printf("ship beside a shore house: sailed %.1f toward the other island\n", moved);
	CHECK(moved > 10.0f);
}

static int count_units(const Simulation &s, uint8_t team, uint8_t type) {
	int n = 0;
	for (size_t i = 0; i < s.units().count; ++i)
		if (s.units().alive[i] && s.units().team[i] == team && s.units().unit_type[i] == type) ++n;
	return n;
}

// Petra's navalManager.js: the regions it reads the map with, a dock once its
// population passes popForDock, one fishing boat per sea with fish and no warships.
static void test_petra_navy() {
	static Simulation s;
	fresh(s);
	// Lands and seas (accessibility.js): the sea joins the north and south lands,
	// the lake touches only the north.
	Simulation::PetraNavalTopo topo;
	s.petra_naval_topology(topo);
	const uint16_t north = s.petra_land_at(-30.0f, 40.0f), south = s.petra_land_at(-30.0f, -40.0f);
	auto [scx, scz] = s.nav_grid_.world_to_cell(0.0f, 0.0f);
	auto [lcx, lcz] = s.nav_grid_.world_to_cell(44.0f, 44.0f);
	const uint16_t sea = s.nav_grid_.global_region_at(scx, scz, PASS_SHIP);
	const uint16_t lake = s.nav_grid_.global_region_at(lcx, lcz, PASS_SHIP);
	CHECK(north && south && north != south && sea && lake && sea != lake);
	CHECK(topo.sea_between(north, south) == sea && topo.linked(north, lake) && !topo.linked(south, lake));
	topo.classify(north);
	// The sea is a quarter of the map: a naval map. The lake (~4000 m²) is a naval region too.
	CHECK(topo.naval_map && topo.naval[sea] && topo.naval[lake]);

	// Population 20: no dock yet. Over 25: a dock on a sea of its land.
	s.set_player_active(1, true, true);
	s.set_player_resources(1, 5000, 5000, 5000, 5000);
	s.spawn_building_world(-30.0f, 40.0f, BLD_TOWN_CENTER, 1);
	for (int k = 0; k < 4; ++k) s.spawn_resource_world(-40.0f + 10.0f * k, 0.0f, RES_FOOD, FISH_FOOD_SUPPLY, RES_SUB_FISH);
	for (int k = 0; k < 20; ++k) s.spawn_unit(-40.0f + (float)(k % 10) * 2.0f, 50.0f + (float)(k / 10) * 2.0f, 1, UNIT_VILLAGER);
	auto petra_docks = [&](bool built) {
		int n = 0;
		for (const auto &b : s.buildings()) if (b.team == 1 && b.type == BLD_DOCK && (!built || b.build_progress >= 1.0f)) ++n;
		return n;
	};
	run(s, 6.0f);
	CHECK(petra_docks(false) == 0);
	for (int k = 0; k < 8; ++k) s.spawn_unit(-40.0f + (float)k * 2.0f, 56.0f, 1, UNIT_VILLAGER);
	run(s, 6.0f);
	uint32_t dock = 0;
	for (const auto &b : s.buildings()) if (b.team == 1 && b.type == BLD_DOCK) dock = b.id;
	std::printf("Petra navy: dock %s at population %d\n", dock ? "placed" : "not placed", s.pop_current(1));
	CHECK(dock != 0);
	const Simulation::Building *db = bld(s, dock);
	CHECK(db && s.petra_building_land(*db) == north && (s.dock_sea(*db) == sea || s.dock_sea(*db) == lake));

	// A finished dock on the sea: one fishing boat (targetNumFishers 1 on a large
	// land), out fishing, and no warship while no attack crosses the water.
	static Simulation t;
	fresh(t);
	t.set_player_active(1, true, true);
	t.set_player_resources(1, 5000, 5000, 5000, 5000);
	t.spawn_building_world(-30.0f, 40.0f, BLD_TOWN_CENTER, 1);
	t.spawn_building_world(-48.0f, 20.0f, BLD_DOCK, 1);
	for (int k = 0; k < 4; ++k) t.spawn_resource_world(-40.0f + 10.0f * k, 0.0f, RES_FOOD, FISH_FOOD_SUPPLY, RES_SUB_FISH);
	run(t, 90.0f);
	int fishing = 0;
	for (size_t i = 0; i < t.units().count; ++i)
		if (t.units().alive[i] && t.units().team[i] == 1 && t.units().unit_type[i] == UNIT_FISHING_BOAT &&
		    (t.units().action_state[i] == ACTION_GATHER || t.units().action_state[i] == ACTION_RETURN_DROPSITE)) ++fishing;
	std::printf("Petra navy: %d fishing boats (%d fishing), %d warships\n", count_units(t, 1, UNIT_FISHING_BOAT), fishing, count_units(t, 1, UNIT_TRIREME));
	CHECK(count_units(t, 1, UNIT_FISHING_BOAT) == 1 && fishing == 1);
	CHECK(count_units(t, 1, UNIT_TRIREME) == 0);
}

// attackPlan.js overseas: an attack on the other land waits for a transport ship,
// which navalManager.js trains (getBestShip "transport"), then sails across.
static void test_petra_overseas_attack() {
	static Simulation s;
	fresh(s);
	s.set_victory_condition(VICTORY_ENDLESS);
	s.set_player_active(1, true, true);
	s.set_player_resources(1, 5000, 5000, 5000, 5000);
	s.players_[1].phase = 2;
	s.spawn_building_world(-30.0f, 40.0f, BLD_TOWN_CENTER, 1);
	s.spawn_building_world(-48.0f, 20.0f, BLD_DOCK, 1);
	const uint32_t house = s.spawn_building_world(20.0f, -40.0f, BLD_OUTPOST, 0); // an outpost: no decay on neutral land
	std::vector<uint32_t> army;
	for (int k = 0; k < 6; ++k) army.push_back(s.spawn_unit(-36.0f + 2.0f * k, 30.0f, 1, UNIT_SPEARMAN));
	s.petra_army_ids_[1] = army; // trained for the attack, not as workers (ids rise, so sorted)
	run(s, 1.0f);
	auto &slot = s; // player 1 is PETRA_PLAYER: its state lives in the petra_* members
	slot.petra_attack_mgr_.kind = PETRA_ATTACK_ATTACK;
	slot.petra_attack_mgr_.target_size = 6;
	slot.petra_attack_mgr_.min_size = 6;
	slot.petra_attack_mgr_.rushes_set = 1;
	slot.petra_attack_plan_.state = Simulation::AttackPlan::MUSTER;
	slot.petra_attack_plan_.target_building_id = house;
	slot.petra_attack_plan_.state_timer = 0.0f;
	run(s, 4.0f);
	// Ready, but nothing to sail in: it waits, and a transport ship is in training.
	CHECK(s.petra_attack_plan_.state == Simulation::AttackPlan::MUSTER);
	run(s, 60.0f);
	int ships = count_units(s, 1, UNIT_TRIREME);
	std::printf("Petra overseas attack: %d transport ship(s) trained\n", ships);
	CHECK(ships >= 1);
	int across = 0;
	for (int k = 0; k < 120 && across == 0; ++k) {
		run(s, 1.0f);
		for (uint32_t uid : army) {
			const int32_t i = idx(s, uid);
			if (i >= 0 && s.units().alive[i] && s.units().pos_z[i] < -16.0f) ++across;
		}
	}
	std::printf("Petra overseas attack: %d of 6 spearmen landed on the south shore\n", across);
	CHECK(across >= 1);
	// With nothing left to attack the army comes home (headquarters.js assignEntity):
	// by ship, as navalManager.requireTransport does, not through the sea.
	int waded = 0, home = 0;
	for (int k = 0; k < 240; ++k) {
		run(s, 1.0f);
		for (uint32_t uid : army) {
			const int32_t i = idx(s, uid);
			if (i >= 0 && s.units().alive[i] && std::fabs(s.units().pos_z[i]) < 14.0f) ++waded;
		}
	}
	// Home is the north land, beach included. Two spearmen that captured the house
	// share a navcell and block each other on its centre until #103's path start fix.
	const uint16_t north = s.petra_land_at(-30.0f, 40.0f);
	for (uint32_t uid : army) {
		const int32_t i = idx(s, uid);
		if (i >= 0 && s.units().alive[i] && s.petra_land_at(s.units().pos_x[i], s.units().pos_z[i]) == north) ++home;
	}
	std::printf("Petra overseas attack: house %s, %d spearmen back home, %d wading\n",
	            bld(s, house) && bld(s, house)->team == 1 ? "captured" : "standing", home, waded);
	CHECK(home >= 4 && waded == 0);
}

// headquarters.js findEconomicCCLocation on a naval map: with nothing to settle on
// its own land, Petra lays out a Civic Centre on the other land, a transport ship
// takes builders across (navalManager.requireTransport) and they build it there.
static void test_petra_overseas_base() {
	static Simulation s;
	fresh(s);
	s.set_player_active(1, true, true);
	s.set_player_resources(1, 5000, 5000, 5000, 5000);
	s.players_[1].phase = 2;
	s.spawn_building_world(-30.0f, 40.0f, BLD_TOWN_CENTER, 1);
	s.spawn_building_world(-48.0f, 20.0f, BLD_DOCK, 1);
	for (int k = 0; k < 10; ++k) s.spawn_unit(-40.0f + 2.0f * k, 52.0f, 1, UNIT_VILLAGER);
	// Wood on the south land only.
	for (int k = 0; k < 8; ++k) s.spawn_resource_world(-36.0f + 3.0f * k, -44.0f, RES_WOOD, 600);
	const uint16_t north = s.petra_land_at(-30.0f, 40.0f), south = s.petra_land_at(-30.0f, -40.0f);
	uint32_t cc = 0;
	float placed_at = -1.0f, first_ashore = -1.0f;
	int waded = 0; // land units are not stopped by deep water yet: none may try
	for (int k = 0; k < 360; ++k) {
		run(s, 1.0f);
		for (size_t i = 0; i < s.units().count; ++i)
			if (s.units().alive[i] && s.units().team[i] == 1 && s.units().unit_type[i] == UNIT_VILLAGER &&
			    std::fabs(s.units().pos_z[i]) < 14.0f && s.petra_land_at(s.units().pos_x[i], s.units().pos_z[i]) != north &&
			    s.petra_land_at(s.units().pos_x[i], s.units().pos_z[i]) != south) ++waded; // in the strait, not on a beach
		for (const auto &b : s.buildings())
			if (!cc && b.team == 1 && b.type == BLD_TOWN_CENTER && b.z < -16.0f) { cc = b.id; placed_at = (float)k; }
		if (first_ashore < 0.0f)
			for (size_t i = 0; i < s.units().count; ++i)
				if (s.units().alive[i] && s.units().team[i] == 1 && s.units().unit_type[i] == UNIT_VILLAGER &&
				    s.petra_land_at(s.units().pos_x[i], s.units().pos_z[i]) == south) { first_ashore = (float)k; break; }
	}
	const Simulation::Building *b = cc ? bld(s, cc) : nullptr;
	int south_workers = 0, north_gathering_south = 0;
	for (size_t i = 0; i < s.units().count; ++i) {
		if (!s.units().alive[i] || s.units().team[i] != 1 || s.units().unit_type[i] != UNIT_VILLAGER) continue;
		const uint16_t l = s.petra_land_at(s.units().pos_x[i], s.units().pos_z[i]);
		if (l == south) ++south_workers;
		const uint32_t rid = s.units().target_resource_id[i];
		if (l == north && rid) {
			auto it = s.res_id_to_idx_.find(rid);
			if (it != s.res_id_to_idx_.end() && s.petra_land_at(s.resources_.x[it->second], s.resources_.z[it->second]) == south)
				++north_gathering_south;
		}
	}
	std::printf("Petra overseas base: CC laid out at %.0fs, first builder ashore at %.0fs, built %.0f%%, %d workers on the south land, %d wading\n",
	            placed_at, first_ashore, b ? 100.0f * b->build_progress : 0.0f, south_workers, waded);
	CHECK(north && south && north != south);
	CHECK(cc != 0);
	CHECK(first_ashore >= 0.0f && south_workers >= 2);
	CHECK(b && b->build_progress > 0.3f);
	CHECK(north_gathering_south == 0 && waded == 0);
}

// navalManager.js keeps a transportPlan per ship: an army sailing to attack does
// not wait for the builders bound for a new base, each crossing on its own ship.
static void test_petra_concurrent_transports() {
	static Simulation s;
	fresh(s);
	s.set_victory_condition(VICTORY_ENDLESS);
	s.set_player_active(1, true, true);
	s.set_player_resources(1, 5000, 5000, 5000, 5000);
	s.players_[1].phase = 2;
	s.spawn_building_world(-30.0f, 40.0f, BLD_TOWN_CENTER, 1);
	s.spawn_building_world(-48.0f, 20.0f, BLD_DOCK, 1);
	const uint32_t house = s.spawn_building_world(30.0f, -40.0f, BLD_OUTPOST, 0); // an outpost: no decay on neutral land
	s.spawn_unit(-40.0f, 10.0f, 1, UNIT_TRIREME);
	s.spawn_unit(-20.0f, 10.0f, 1, UNIT_TRIREME);
	std::vector<uint32_t> army;
	for (int k = 0; k < 6; ++k) army.push_back(s.spawn_unit(-36.0f + 2.0f * k, 30.0f, 1, UNIT_SPEARMAN));
	s.petra_army_ids_[1] = army; // trained for the attack, not as workers (ids rise, so sorted)
	for (int k = 0; k < 10; ++k) s.spawn_unit(-40.0f + 2.0f * k, 52.0f, 1, UNIT_VILLAGER);
	// Wood on the south land only: Petra settles there.
	for (int k = 0; k < 8; ++k) s.spawn_resource_world(-36.0f + 3.0f * k, -44.0f, RES_WOOD, 600);
	const uint16_t south = s.petra_land_at(-30.0f, -40.0f);
	auto plan_of = [&](uint8_t owner) -> const PetraTransportPlan * {
		for (const auto &p : s.petra_transport_plans()) if (p.owner == owner) return &p;
		return nullptr;
	};
	// The builders' crossing first; the attack sets out while it is under way.
	for (int k = 0; k < 240 && !plan_of(PETRA_TRANSPORT_BUILDERS); ++k) run(s, 1.0f);
	CHECK(plan_of(PETRA_TRANSPORT_BUILDERS) != nullptr);
	// One plan per owner: a second builders' crossing waits for the first.
	CHECK(!s.petra_start_transport(army, -30.0f, 40.0f, -30.0f, -40.0f, 0, PETRA_TRANSPORT_BUILDERS));
	s.petra_attack_mgr_.kind = PETRA_ATTACK_ATTACK;
	s.petra_attack_mgr_.target_size = 6;
	s.petra_attack_mgr_.min_size = 6;
	s.petra_attack_mgr_.rushes_set = 1;
	s.petra_attack_plan_.state = Simulation::AttackPlan::MUSTER;
	s.petra_attack_plan_.target_building_id = house;
	s.petra_attack_plan_.state_timer = 0.0f;
	bool both = false, shared = false;
	std::vector<uint32_t> landed;
	for (int k = 0; k < 120; ++k) {
		run(s, 1.0f);
		const PetraTransportPlan *a = plan_of(PETRA_TRANSPORT_ATTACK), *b = plan_of(PETRA_TRANSPORT_BUILDERS);
		if (a && b) {
			both = true;
			shared |= a->ship_id == b->ship_id;
			for (uint32_t uid : a->passenger_ids)
				shared |= std::find(b->passenger_ids.begin(), b->passenger_ids.end(), uid) != b->passenger_ids.end();
		}
		for (uint32_t uid : army) {
			const int32_t i = idx(s, uid);
			if (i >= 0 && s.units().alive[i] && s.petra_land_at(s.units().pos_x[i], s.units().pos_z[i]) == south &&
			    std::find(landed.begin(), landed.end(), uid) == landed.end()) landed.push_back(uid);
		}
	}
	int villagers_south = 0;
	for (size_t i = 0; i < s.units().count; ++i)
		if (s.units().alive[i] && s.units().team[i] == 1 && s.units().unit_type[i] == UNIT_VILLAGER &&
		    s.petra_land_at(s.units().pos_x[i], s.units().pos_z[i]) == south) ++villagers_south;
	std::printf("Petra concurrent transports: both under way at once %s, %zu of 6 spearmen landed, %d villagers on the south land\n",
	            both ? "yes" : "no", landed.size(), villagers_south);
	CHECK(both && !shared);
	CHECK(landed.size() >= 4 && villagers_south >= 1);
}


// navalManager.checkEvents: a transport ship sunk while its passengers board leaves
// the plan waiting for another ship, which takes them across instead.
static void test_petra_transport_ship_lost() {
	static Simulation s;
	fresh(s);
	s.set_victory_condition(VICTORY_ENDLESS);
	s.set_player_active(1, true, true);
	s.set_player_resources(1, 5000, 5000, 5000, 5000);
	s.spawn_building_world(-30.0f, 40.0f, BLD_TOWN_CENTER, 1);
	s.spawn_building_world(-48.0f, 20.0f, BLD_DOCK, 1);
	const uint32_t first = s.spawn_unit(-30.0f, 8.0f, 1, UNIT_TRIREME);
	const uint32_t second = s.spawn_unit(30.0f, 8.0f, 1, UNIT_TRIREME);
	std::vector<uint32_t> crew;
	for (int k = 0; k < 4; ++k) crew.push_back(s.spawn_unit(-34.0f + 2.0f * k, 26.0f, 1, UNIT_SPEARMAN));
	run(s, 0.5f);
	CHECK(s.petra_start_transport(crew, -30.0f, 30.0f, -30.0f, -40.0f, 0, PETRA_TRANSPORT_ATTACK));
	CHECK(s.petra_transport_plans().size() == 1 && s.petra_transport_plans()[0].ship_id == first);
	s.cmd_delete(1, &first, 1);
	uint32_t took = 0;
	int landed = 0;
	for (int k = 0; k < 150; ++k) {
		run(s, 1.0f);
		if (!s.petra_transport_plans().empty() && s.petra_transport_plans()[0].ship_id) took = s.petra_transport_plans()[0].ship_id;
		landed = 0;
		for (uint32_t uid : crew) {
			const int32_t i = idx(s, uid);
			if (i >= 0 && s.units().alive[i] && s.units().pos_z[i] < -16.0f) ++landed;
		}
		if (landed == (int)crew.size()) break;
	}
	std::printf("Petra transport ship lost: plan moved to ship %s, %d of 4 landed\n", took == second ? "two" : "none", landed);
	CHECK(took == second && landed >= 3); // it sails with whoever made it aboard
}

// DeathDamage.js: a fire ship ignited (Upgrade/Ignition to ship_fire_fire, mixins/fireship.xml)
// burns down at RegenRate -5 and, dying, deals 500 Fire over 20 m to everyone, its own side
// too, with quadratic falloff. Before ignition it neither burns nor blasts. Other ships and
// structures deal nothing when they die.
// TreasureCollector (0.28 template_unit_ship): ships collect treasures within 12 m, a shipwreck
// out at sea and one on the shore alike; warships too, rams and siege engines on land not.
static void test_ship_treasure() {
	static Simulation s;
	fresh(s);
	const uint32_t merchant = s.spawn_unit(-30.0f, 0.0f, 0, UNIT_MERCHANT_SHIP);
	const uint32_t war = s.spawn_unit(-30.0f, 6.0f, 0, UNIT_TRIREME);
	const uint32_t wreck = s.spawn_treasure(-10.0f, 0.0f, TREASURE_SHIPWRECK);
	const uint32_t shore = s.spawn_treasure(10.0f, 19.0f, TREASURE_SHIPWRECK_DEBRIS); // on land past the water's edge
	const uint32_t ram = s.spawn_unit(10.0f, 30.0f, 0, UNIT_SIEGE_RAM);
	s.order_collect_treasure(&ram, 1, shore);
	CHECK(s.unit_order(ram).kind != ORDER_COLLECT_TREASURE);
	s.order_collect_treasure(&war, 1, wreck);
	CHECK(s.unit_order(war).kind == ORDER_COLLECT_TREASURE);
	s.cmd_stop(&war, 1);
	const float wood0 = s.player(0).wood(), food0 = s.player(0).food();
	s.order_collect_treasure(&merchant, 1, wreck);
	CHECK(s.unit_order(merchant).kind == ORDER_COLLECT_TREASURE);
	run(s, 30.0f);
	std::printf("merchant ship treasure: wood %+.0f\n", s.player(0).wood() - wood0);
	CHECK(s.player(0).wood() - wood0 == 500.0f);
	s.order_collect_treasure(&merchant, 1, shore);
	run(s, 40.0f);
	std::printf("merchant ship shore treasure: food %+.0f\n", s.player(0).food() - food0);
	CHECK(s.player(0).food() - food0 == 200.0f);
	CHECK(s.treasures().empty());
}

// navalManager.js moveApart / entityExtend.js gatherTreasure(water): Petra's idle ship goes
// for the nearest treasure on its own sea, not a nearer one on land.
static void test_petra_ship_treasure() {
	static Simulation s;
	fresh(s);
	s.set_player_active(1, true, true);
	s.spawn_building_world(-40.0f, 40.0f, BLD_TOWN_CENTER, 1);
	const uint32_t tri = s.spawn_unit(0.0f, 10.0f, 1, UNIT_TRIREME);
	const uint32_t wreck = s.spawn_treasure(-10.0f, 0.0f, TREASURE_SHIPWRECK);
	s.spawn_treasure(10.0f, 19.0f, TREASURE_SHIPWRECK_DEBRIS); // nearer, but on land
	s.petra_pid_ = 1;
	s.petra_manage_naval(2.0f); // one naval update (every 2 s)
	const auto o = s.unit_order(tri);
	std::printf("Petra ship treasure: order %d target %u (wreck %u)\n", (int)o.kind, o.target, wreck);
	CHECK(o.kind == ORDER_COLLECT_TREASURE && o.target == wreck);
}

static void test_fire_ship() {
	static Simulation s;
	fresh(s);
	s.set_victory_condition(VICTORY_ENDLESS);
	{
		const uint32_t cold = s.spawn_unit(0.0f, 30.0f, 0, UNIT_TRIREME, 0.0f, tmpl_of("brit/ship_fire"));
		const uint32_t near = s.spawn_unit(3.0f, 30.0f, 1, UNIT_TRIREME);
		uint32_t both[2] = {cold, near};
		s.order_set_stance(both, 1, STANCE_PASSIVE);
		s.order_set_stance(&both[1], 1, STANCE_PASSIVE);
		const float c0 = s.units().hp[idx(s, cold)], n0 = s.units().hp[idx(s, near)];
		run(s, 10.0f);
		CHECK(s.units().hp[idx(s, cold)] == c0); // not burning before ignition
		s.set_unit_hp(cold, 1.0f);
		s.cmd_delete(0, &cold, 1);
		run(s, 1.0f);
		CHECK(s.units().hp[idx(s, near)] == n0); // no blast either
		s.cmd_delete(1, &near, 1);
		run(s, 1.0f);
	}
	const uint32_t fire = s.spawn_unit(0.0f, 12.0f, 0, UNIT_TRIREME, 0.0f, tmpl_of("brit/ship_fire"));
	CHECK(s.cmd_upgrade_units(0, &fire, 1, tmpl_of("brit/ship_fire_fire"))); // Ignition
	run(s, 0.1f);
	CHECK(s.units().tmpl[idx(s, fire)] == tmpl_of("brit/ship_fire_fire"));
	const uint32_t enemy = s.spawn_unit(3.0f, 12.0f, 1, UNIT_TRIREME);
	const uint32_t own = s.spawn_unit(-3.5f, 12.0f, 0, UNIT_TRIREME);
	const uint32_t far_enemy = s.spawn_unit(16.0f, 12.0f, 1, UNIT_TRIREME);
	uint32_t all[4] = {fire, enemy, own, far_enemy};
	s.order_set_stance(all, 4, STANCE_PASSIVE);
	s.order_set_stance(&all[1], 1, STANCE_PASSIVE);
	s.order_set_stance(&all[3], 1, STANCE_PASSIVE);
	auto hp = [&](uint32_t id) { const int32_t i = idx(s, id); return i >= 0 ? s.units().hp[i] : -1.0f; };
	const float f0 = hp(fire), e0 = hp(enemy), o0 = hp(own), x0 = hp(far_enemy);
	run(s, 10.0f);
	const float burnt = f0 - hp(fire);
	std::printf("fire ship: %.1f hp burnt in 10 s\n", burnt);
	CHECK(burnt > 45.0f && burnt < 55.0f);
	s.set_unit_hp(fire, 1.0f);
	run(s, 1.0f);
	CHECK(!s.units().alive[idx(s, fire)]);
	std::printf("fire ship blast: enemy %.0f -> %.0f, own %.0f -> %.0f, far %.0f -> %.0f\n",
			e0, hp(enemy), o0, hp(own), x0, hp(far_enemy));
	CHECK(hp(enemy) < e0 - 100.0f);       // close: most of 500
	CHECK(hp(own) < o0 - 50.0f);          // friendly fire
	CHECK(hp(far_enemy) == x0);           // 16 units away is outside 20 m (x0.6)

	// A trireme that sinks hurts nobody (no DeathDamage in template_unit_ship).
	const uint32_t a = s.spawn_unit(-3.0f, 20.0f, 0, UNIT_TRIREME);
	const uint32_t b = s.spawn_unit(0.0f, 20.0f, 1, UNIT_TRIREME);
	uint32_t ab[2] = {a, b};
	s.order_set_stance(ab, 1, STANCE_PASSIVE);
	s.order_set_stance(&ab[1], 1, STANCE_PASSIVE);
	const float b0 = hp(b);
	s.cmd_delete(0, &a, 1);
	run(s, 1.0f);
	CHECK(hp(b) == b0);
}

// A worker sent to a tree across the north-east lake walks round it (UnitMotion paths
// every move) instead of wading straight through deep water.
static void test_gather_around_lake() {
	static Simulation s;
	fresh(s);
	const uint32_t tree = s.spawn_resource_world(57.0f, 44.0f, RES_WOOD, 200);
	const uint32_t woman = s.spawn_unit(31.0f, 44.0f, 0, UNIT_FEMALE_CITIZEN);
	s.order_gather(&woman, 1, tree);
	bool waded = false, reached = false;
	for (int t = 0; t < (int)(40.0f / Simulation::FIXED_STEP) && !reached; ++t) {
		s.tick(Simulation::FIXED_STEP);
		const int32_t i = idx(s, woman);
		auto [cx, cy] = s.nav_grid().world_to_cell(s.units().pos_x[i], s.units().pos_z[i]);
		waded = waded || s.nav_grid().cells.at(cx, cy) == NAV_WATER;
		reached = s.units().carry[i] > 0;
	}
	std::printf("  gather across the lake: walked round %s, gathering %s\n", waded ? "no" : "yes", reached ? "yes" : "no");
	CHECK(!waded && reached);
}

// navalManager.js buildNavalStructures: from the town phase, with a dock and a market
// and population past popPhase2 + 15, a Carthaginian Petra lays out its Cothon (super
// dock) on the dock's sea; a civ without one in its Builder lists builds none.
static void test_petra_cothon() {
	for (Civilization civ : {CIV_CARTHAGINIANS, CIV_ATHENIANS}) {
		static Simulation s;
		fresh(s);
		s.set_team_civ(1, civ);
		s.set_victory_condition(VICTORY_ENDLESS);
		s.set_player_active(1, true, true);
		s.set_player_resources(1, 5000, 5000, 5000, 5000);
		s.players_[1].phase = 2;
		s.spawn_building_world(-30.0f, 40.0f, BLD_TOWN_CENTER, 1);
		const uint32_t dock = s.spawn_building_world(-48.0f, 20.0f, BLD_DOCK, 1);
		s.spawn_building_world(-10.0f, 50.0f, BLD_MARKET, 1);
		// 0.28 popPhase2 is 100 at the default 200 population limit.
		for (int k = 0; k < 120; ++k) s.spawn_unit(-40.0f + (float)(k % 15) * 2.0f, 50.0f + (float)(k / 15) * 2.0f, 1, UNIT_VILLAGER);
		run(s, 8.0f);
		const Simulation::Building *cothon = nullptr;
		for (const auto &b : s.buildings()) if (b.team == 1 && b.type == BLD_SUPER_DOCK) cothon = &b;
		const Simulation::Building *db = bld(s, dock);
		std::printf("Petra Cothon (%s): %s\n", civ_info(civ).name, cothon ? "laid out" : "none");
		if (civ == CIV_CARTHAGINIANS) CHECK(cothon && db && s.dock_sea(*cothon) == s.dock_sea(*db));
		else CHECK(cothon == nullptr);
	}
}

// A worker on the far shore carries its load only to a dropsite on its own land (0 A.D.
// picks reachable dropsites): with the only storehouse across the sea it waits, and once
// one stands on its side it drops off there.
static void test_dropsite_own_land() {
	static Simulation s;
	fresh(s);
	s.spawn_building_world(0.0f, 40.0f, BLD_STOREHOUSE, 0, 1.0f);
	const uint32_t tree = s.spawn_resource_world(4.0f, -30.0f, RES_WOOD, 500);
	const uint32_t woman = s.spawn_unit(0.0f, -30.0f, 0, UNIT_FEMALE_CITIZEN);
	s.order_gather(&woman, 1, tree);
	bool left_shore = false;
	for (int t = 0; t < (int)(60.0f / Simulation::FIXED_STEP); ++t) {
		s.tick(Simulation::FIXED_STEP);
		left_shore = left_shore || s.units().pos_z[idx(s, woman)] > -22.0f; // the beach is at -16
	}
	const int carried = s.units().carry[idx(s, woman)];
	std::printf("  dropsite across the sea: carrying %d, %s\n", carried, left_shore ? "set off for the beach" : "stayed by the tree");
	CHECK(!left_shore && carried > 0 && total(s, 0) == 0);
	s.spawn_building_world(0.0f, -40.0f, BLD_STOREHOUSE, 0, 1.0f);
	run(s, 20.0f);
	std::printf("  storehouse on her side: stock %d\n", total(s, 0));
	CHECK(total(s, 0) > 0);
}

// A dock takes every resource (template_structure_military_dock ResourceDropsite Types
// food wood stone metal), for a Return Resource order too, not only food.
static void test_dock_takes_all() {
	static Simulation s;
	fresh(s);
	const uint32_t dock = s.spawn_building_world(0.0f, 17.0f, BLD_DOCK, 0, 1.0f);
	const uint32_t woman = s.spawn_unit(0.0f, 24.0f, 0, UNIT_FEMALE_CITIZEN);
	const int32_t w = idx(s, woman);
	s.units_.res_type_carried[w] = RES_WOOD;
	s.units_.carry[w] = 10;
	const bool ok = s.can_return_resource(woman, dock);
	s.order_return_resource(&woman, 1, dock);
	run(s, 15.0f);
	std::printf("  wood to a dock: order %s, stock wood %d\n", ok ? "accepted" : "refused", s.player_resource(0, RES_WOOD));
	CHECK(ok && s.player_resource(0, RES_WOOD) == 10);
}

// attackPlan.js isValidTarget (sameLand) and rushTargetFinder: an enemy structure on a
// plateau no land unit can climb (another land, with no sea to it) is no target. A plan
// sent there parked its army below the cliff and re-targeted every 25 s for ever.
static void test_petra_unreachable_target() {
	static Simulation s;
	s.set_sim_seed(7);
	s.reset();
	s.set_map_half(HALF);
	std::vector<float> h((size_t)RES * RES);
	for (int j = 0; j < RES; ++j)
		for (int i = 0; i < RES; ++i) {
			const float x = -HALF + 2.0f * HALF * i / (RES - 1), z = -HALF + 2.0f * HALF * j / (RES - 1);
			const float mx = x - 30.0f, mz = z + 42.0f; // a sheer-sided mesa on the south land
			h[(size_t)j * RES + i] = mx * mx + mz * mz < 12.0f * 12.0f ? 60.0f : height_at(x, z);
		}
	s.set_terrain_data(h, RES, 0.0f);
	s.begin_bulk_spawn();
	s.end_bulk_spawn();
	for (uint8_t p = 0; p < 4; ++p) s.set_player_active(p, true, false);
	s.set_team_civ(0, CIV_CARTHAGINIANS);
	s.set_team_civ(1, CIV_ATHENIANS);
	s.set_victory_condition(VICTORY_ENDLESS);
	s.set_player_active(1, true, true);
	s.spawn_building_world(-30.0f, -40.0f, BLD_TOWN_CENTER, 1);
	const uint32_t barracks = s.spawn_building_world(30.0f, -42.0f, BLD_BARRACKS, 0); // on the mesa
	const uint32_t house = s.spawn_building_world(0.0f, -50.0f, BLD_HOUSE, 0);         // below it
	// Player 0's own Civic Center, out of the 80 m defended radius of both: its territory
	// connects them, so neither decays (TerritoryDecay.js) and drops out as a target.
	s.spawn_building_world(25.0f, -63.0f, BLD_TOWN_CENTER, 0);
	run(s, 1.0f);
	const uint16_t below = s.petra_land_at(-30.0f, -30.0f), top = s.petra_land_at(30.0f, -42.0f);
	CHECK(below && top && below != top);
	float tx = 0.0f, tz = 0.0f;
	const uint32_t attack = s.find_petra_attack_target(tx, tz);
	const uint32_t rush = s.find_petra_rush_target(tx, tz);
	// An army standing on the mesa's land retargets only up there.
	const uint32_t up_there = s.find_petra_attack_target(tx, tz, top);
	std::printf("Petra attack targets with a barracks on a mesa: attack %s, rush %s, from the mesa %s\n",
	            attack == house ? "the house below" : attack == barracks ? "the mesa barracks" : "none",
	            rush == house ? "the house below" : rush == barracks ? "the mesa barracks" : "none",
	            up_there == barracks ? "the barracks" : "other");
	CHECK(attack == house && rush == house && up_there == barracks);
}

int main() {
	test_transport();
	test_dropsite_own_land();
	test_dock_takes_all();
	test_no_wading();
	test_gather_around_lake();
	test_dock_trade();
	test_fishing();
	test_petra_sea_trade();
	test_petra_trader_transport();
	test_ship_height();
	test_ship_leaves_shore_house();
	test_petra_navy();
	test_petra_overseas_attack();
	test_petra_unreachable_target();
	test_petra_overseas_base();
	test_petra_concurrent_transports();
	test_petra_transport_ship_lost();
	test_petra_cothon();
	test_ship_treasure();
	test_petra_ship_treasure();
	test_fire_ship();
	std::printf(failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", failures);
	return failures ? 1 : 0;
}
