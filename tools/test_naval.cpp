// Naval tests: merchant ships trading dock to dock (0 A.D. Trader.js, Market.js
// TradeType "land naval", Market.js CalculateTraderGain, the Carthaginian ship bonus).
//   g++ -std=c++20 -O2 -Isrc tools/test_naval.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//       src/network/net_host.cpp -lpthread -o /tmp/test_naval && /tmp/test_naval
// Private access only to put a test AI straight into the town phase.
#define private public
#include "simulation/simulation.h"
#undef private
#include <algorithm>
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

static void fresh(Simulation &s, float (*height)(float, float) = height_at) {
	s.set_sim_seed(7);
	s.reset();
	s.set_map_half(HALF);
	std::vector<float> h((size_t)RES * RES);
	for (int j = 0; j < RES; ++j)
		for (int i = 0; i < RES; ++i)
			h[(size_t)j * RES + i] = height(-HALF + 2.0f * HALF * i / (RES - 1), -HALF + 2.0f * HALF * j / (RES - 1));
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

	// Market TradeType: docks are "land naval", markets "land", so ships trade at
	// docks and caravans at both.
	CHECK(s.can_trade_at(0, *bw, UNIT_MERCHANT_SHIP));
	CHECK(s.can_trade_at(0, *bw, UNIT_TRADER));
	CHECK(s.can_trade_at(0, *bm, UNIT_TRADER));
	CHECK(!s.can_trade_at(0, *bm, UNIT_MERCHANT_SHIP));
	// The sea docks share a sea, the lake dock doesn't.
	CHECK(s.dock_sea(*bw) != 0 && s.dock_sea(*bw) == s.dock_sea(*be));
	CHECK(s.dock_sea(*bl) != 0 && s.dock_sea(*bl) != s.dock_sea(*bw));
	CHECK(s.has_trade_route(0, UNIT_MERCHANT_SHIP));
	CHECK(s.has_trade_route(0, UNIT_TRADER)); // the market and a dock

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

	// A merchant ship in the sea takes the farthest pair of docks it can trade at (the route an
	// AI's idle trader picks, order_trade_auto) and is paid on each arrival.
	const uint32_t ship = s.spawn_unit(-40.0f, 8.0f, 0, UNIT_MERCHANT_SHIP);
	s.order_trade_auto(&ship, 1);
	run(s, 240.0f);
	const int earned = total(s, 0);
	std::printf("Carthaginian merchant ship earned %d in 240 s\n", earned);
	CHECK(earned >= cart_ship);
	CHECK(earned % cart_ship == 0); // only whole dock-to-dock deliveries, never the lake
	const auto &u = s.units();
	for (size_t i = 0; i < u.count; ++i) {
		if (u.id[i] == ship) {
			CHECK(u.action_state[i] == ACTION_TRADE);
			auto [cx, cy] = s.nav_grid().world_to_cell(u.pos_x[i], u.pos_z[i]);
			CHECK(s.nav_grid().is_passable_for(cx, cy, PASS_SHIP)); // stayed on the water
		}
	}

	// A caravan on the south shore trades between its market and the dock on the
	// same shore, never the docks across the sea or on the lake.
	static Simulation c;
	fresh(c);
	c.spawn_building_world(-48.0f, 20.0f, BLD_DOCK, 0);
	const uint32_t south = c.spawn_building_world(48.0f, -20.0f, BLD_DOCK, 0);
	c.spawn_building_world(44.0f, 56.0f, BLD_DOCK, 0);
	const uint32_t cmarket = c.spawn_building_world(-40.0f, -40.0f, BLD_MARKET, 0);
	c.spawn_building_world(5.0f, -45.0f, BLD_TOWN_CENTER, 0); // territory, or both decay to Gaia
	const uint32_t caravan = c.spawn_unit(-30.0f, -40.0f, 0, UNIT_TRADER);
	c.order_trade_auto(&caravan, 1);
	run(c, 240.0f);
	const auto route = c.trader_routes_.find(caravan);
	CHECK(route != c.trader_routes_.end());
	if (route != c.trader_routes_.end())
		CHECK(std::min(route->second.first, route->second.second) == std::min(cmarket, south) &&
		      std::max(route->second.first, route->second.second) == std::max(cmarket, south));
	const int caravan_gain = c.calculate_trade_gain(*bld(c, cmarket), *bld(c, south), 0, UNIT_TRADER).trader;
	const int caravan_earned = total(c, 0);
	std::printf("caravan between market and dock earned %d in 240 s (%d a trip)\n", caravan_earned, caravan_gain);
	CHECK(caravan_earned >= caravan_gain && caravan_earned % caravan_gain == 0);

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

// tradeManager.js trainMoreTraders: a merchant ship Petra has as a transport (not trained for
// trade) is switched to trading on a 5th turn below the trader target, before any is trained.
static void test_petra_transport_switches_to_trade() {
	static Simulation s;
	fresh(s);
	s.set_player_active(1, true, true);
	s.set_player_resources(1, 2000, 2000, 2000, 2000);
	s.players_[1].phase = 2;
	s.spawn_building_world(-48.0f, 20.0f, BLD_DOCK, 1);
	s.spawn_building_world(48.0f, -20.0f, BLD_DOCK, 1);
	s.spawn_building_world(-40.0f, 40.0f, BLD_TOWN_CENTER, 1);
	const uint32_t ship = s.spawn_unit(-40.0f, 8.0f, 1, UNIT_MERCHANT_SHIP);
	s.petra_pid_ = 1;
	const bool transport = !s.petra_trade_ship(ship);
	bool traded_early = false;
	for (int k = 0; k < 40; ++k) {
		run(s, 0.5f);
		s.petra_pid_ = 1;
		if (!s.petra_trade_ship(ship) && s.units().action_state[s.lookup_unit(ship)] == ACTION_TRADE) traded_early = true;
		if (s.petra_trade_ship(ship)) break;
	}
	run(s, 4.0f);
	s.petra_pid_ = 1;
	const bool switched = s.petra_trade_ship(ship);
	const bool trading = s.units().action_state[s.lookup_unit(ship)] == ACTION_TRADE;
	std::printf("Petra transport to trade: a transport first %d, traded before the switch %d, switched %d, trading %d\n", transport,
	            traded_early, switched, trading);
	CHECK(transport && !traded_early && switched && trading);
}

// tradeManager.js updateTrader: a land trader whose land has no route asks for a ship
// (navalManager.requireTransport) to the nearer end of the best route on another land. A second
// trader there joins the same crossing while the ship boards.
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
	const uint32_t second = s.spawn_unit(-26.0f, -40.0f, 1, UNIT_TRADER);
	s.spawn_unit(-30.0f, -10.0f, 1, UNIT_TRIREME);
	bool planned = false, joined = false;
	for (int t = 0; t < 40 && !(planned && joined); ++t) {
		run(s, 1.0f);
		for (const auto &tp : s.petra_transports_) {
			if (tp.owner != PETRA_TRANSPORT_TRADER) continue;
			const auto has = [&](uint32_t id) { return std::find(tp.passenger_ids.begin(), tp.passenger_ids.end(), id) != tp.passenger_ids.end(); };
			planned |= has(trader);
			joined |= has(trader) && has(second);
		}
	}
	bool routed = false;
	for (int t = 0; t < 120 && !routed; ++t) {
		run(s, 1.0f);
		routed = s.trader_routes_.count(trader) && s.units_.action_state[s.lookup_unit(trader)] == ACTION_TRADE;
	}
	std::printf("Petra trader on the other land: transport %s, second trader on board %s, trading %s\n", planned ? "yes" : "no",
	            joined ? "yes" : "no", routed ? "yes" : "no");
	CHECK(planned);
	CHECK(joined);
	CHECK(routed);
}

// navalManager.js transportShips and getBestShip: a merchantman with no trade route is a
// transport (Carthage's village-phase transport is its merchantman), so it carries a land
// trader across, and isn't counted or routed as a trader while it does.
static uint16_t tmpl_of(const char *id);
static void test_petra_merchantman_transport() {
	static Simulation s;
	fresh(s);
	s.players_[1].civ = CIV_CARTHAGINIANS;
	s.set_player_active(1, true, true);
	s.set_player_resources(1, 5000, 5000, 5000, 5000);
	s.players_[1].phase = 2;
	s.spawn_building_world(-30.0f, 40.0f, BLD_TOWN_CENTER, 1);
	s.spawn_building_world(-50.0f, 52.0f, BLD_MARKET, 1);
	s.spawn_building_world(30.0f, 52.0f, BLD_MARKET, 1);
	const uint32_t trader = s.spawn_unit(-30.0f, -40.0f, 1, UNIT_TRADER);
	const uint32_t ship = s.spawn_unit(-30.0f, -10.0f, 1, UNIT_MERCHANT_SHIP, 0.0f, tmpl_of("cart/ship_merchant"));
	bool planned = false;
	for (int t = 0; t < 40 && !planned; ++t) {
		run(s, 1.0f);
		for (const auto &tp : s.petra_transports_)
			if (tp.owner == PETRA_TRANSPORT_TRADER && tp.ship_id == ship &&
			    std::find(tp.passenger_ids.begin(), tp.passenger_ids.end(), trader) != tp.passenger_ids.end())
				planned = true;
	}
	bool routed = false;
	for (int t = 0; t < 120 && !routed; ++t) {
		run(s, 1.0f);
		routed = s.trader_routes_.count(trader) && s.units_.action_state[s.lookup_unit(trader)] == ACTION_TRADE;
	}
	std::printf("Petra merchantman as a transport: carries the trader %s, trader trading %s, ship routed %s\n", planned ? "yes" : "no",
	            routed ? "yes" : "no", s.trader_routes_.count(ship) ? "yes" : "no");
	CHECK(planned);
	CHECK(routed);
	CHECK(!s.trader_routes_.count(ship)); // no sea route here: it stays a transport
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
	s.set_explore_map(true); // boats only move on to fish their player has seen
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
	t.set_explore_map(true);
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
	// Per-civ GarrisonHolder/Max: Briton arrow ships hold 40.
	const uint32_t brit_arrow = s.spawn_unit(-30.0f, 0.0f, 0, UNIT_TRIREME, 0.0f, tmpl_of("brit/ship_arrow"));
	CHECK(s.get_transport_capacity(brit_arrow) == 40);

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

	// Warships (template_unit_ship_warship_arrow/_ram) eject Civilian Infantry Dog: a healer
	// drowns there but swims from a scout ship (template_unit_ship's list); a cavalry hero
	// (Hero Cavalry, not Infantry) drowns.
	{
		auto up = [&](uint32_t id) { const int32_t i = idx(s, id); return i >= 0 && s.units().alive[i]; };
		const uint16_t heal_t = tmpl_of("athen/support_healer_b");
		auto sink = [&](float x, const char *ship_t, uint32_t a, uint32_t b) {
			const uint32_t sh = s.spawn_unit(x, 13.0f, 0, UNIT_TRIREME, 0.0f, tmpl_of(ship_t));
			uint32_t party[2] = {a, b};
			s.order_board_transport(party, b ? 2 : 1, sh);
			run(s, 3.0f);
			CHECK(s.get_transport_passenger_count(sh) == (b ? 2u : 1u));
			s.units_.hp[idx(s, sh)] = 0.0f;
			s.units_.alive[idx(s, sh)] = 0;
			run(s, 0.1f);
		};
		const uint32_t h1 = s.spawn_unit(-80.0f, 17.5f, 0, UNIT_HEALER, 0.0f, heal_t);
		const uint32_t hero = s.spawn_unit(-79.0f, 17.5f, 0, UNIT_HERO, 0.0f, tmpl_of("mace/hero_alexander_iii"));
		sink(-80.0f, "athen/ship_arrow", h1, hero);
		CHECK(!up(h1) && !up(hero));
		const uint32_t h2 = s.spawn_unit(-100.0f, 17.5f, 0, UNIT_HEALER, 0.0f, heal_t);
		sink(-100.0f, "athen/ship_scout", h2, 0);
		CHECK(up(h2));
	}

	// EjectClassesOnDestroy is "Civilian Infantry Healer Dog": a 0.28 civilian swims ashore, an
	// old female citizen (FemaleCitizen Support only) drowns, and a loaded ram (Siege) sinks
	// with its own passengers (EjectOrKill kills a holder's units when it dies out of the world).
	{
		const uint16_t civ_t = tmpl_of("athen/support_civilian"), fem_t = tmpl_of("athen/support_female_citizen");
		const uint32_t ship4 = s.spawn_unit(-60.0f, 13.0f, 0, UNIT_TRIREME, 0.0f, tmpl_of("athen/ship_arrow"));
		const uint32_t civ = s.spawn_unit(-60.0f, 17.5f, 0, UNIT_FEMALE_CITIZEN, 0.0f, civ_t);
		const uint32_t fem = s.spawn_unit(-61.0f, 17.5f, 0, UNIT_FEMALE_CITIZEN, 0.0f, fem_t);
		const uint32_t ram4 = s.spawn_unit(-59.0f, 17.5f, 0, UNIT_SIEGE_RAM);
		uint32_t crew4[2] = {s.spawn_unit(-59.0f, 19.0f, 0, UNIT_SPEARMAN), s.spawn_unit(-58.0f, 19.0f, 0, UNIT_SPEARMAN)};
		s.order_board_transport(crew4, 2, ram4);
		CHECK(s.get_transport_passenger_count(ram4) == 2);
		const uint32_t party4[] = {civ, fem, ram4};
		s.order_board_transport(party4, 3, ship4);
		run(s, 3.0f);
		CHECK(s.get_transport_passenger_count(ship4) == 3);
		s.units_.hp[idx(s, ship4)] = 0.0f;
		s.units_.alive[idx(s, ship4)] = 0;
		run(s, 0.1f);
		CHECK(s.units().alive[idx(s, civ)] && !s.units().alive[idx(s, fem)] && !s.units().alive[idx(s, ram4)]);
		CHECK(!s.units().alive[idx(s, crew4[0])] && !s.units().alive[idx(s, crew4[1])]);
		CHECK(s.get_transport_passengers(ram4) == nullptr);
		// A fishing boat (template_unit_ship's List) takes a civilian, not an old female citizen.
		const int32_t ifb = idx(s, fisher);
		const uint32_t civ2 = s.spawn_unit(30.0f, 20.0f, 0, UNIT_FEMALE_CITIZEN, 0.0f, civ_t);
		const uint32_t fem2 = s.spawn_unit(31.0f, 20.0f, 0, UNIT_FEMALE_CITIZEN, 0.0f, fem_t);
		CHECK(s.can_board(ifb, idx(s, civ2)) && !s.can_board(ifb, idx(s, fem2)));
		// Relics ride warships and merchant ships (List Relic), not fishing boats.
		const uint32_t relic = s.spawn_unit(32.0f, 20.0f, 0, UNIT_CATAFALQUE);
		CHECK(s.can_board(ia, idx(s, relic)) && s.can_board(im, idx(s, relic)) && !s.can_board(ifb, idx(s, relic)));
	}

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
	// ... its class clearance off deep water (CheckUnitPlacement's expanded grid).
	CHECK(!s.near_deep_water(s.units().pos_x[iar], s.units().pos_z[iar], s.unit_clearance_at((size_t)iar)));
	// Order.PickupUnit: a walker who can reach the ship from where it lies is waited for (LOADING).
	{
		const float sx = s.units().pos_x[idx(s, ship3)], sz = s.units().pos_z[idx(s, ship3)];
		const uint32_t walker = s.spawn_unit(40.0f, 26.0f, 0, UNIT_ARCHER);
		s.order_board_transport(&walker, 1, ship3);
		run(s, 12.0f);
		const float moved = std::hypot(s.units().pos_x[idx(s, ship3)] - sx, s.units().pos_z[idx(s, ship3)] - sz);
		std::printf("  pickup of a walker who can reach the ship: ship moved %.2f, walker %s\n", moved,
				aboard(s, ship3, walker) ? "aboard" : "ashore");
		CHECK(moved < 1.5f && aboard(s, ship3, walker)); // pushed a little, not sailed off
		s.order_disembark_all(ship3);
	}
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
// until the ship comes within LoadingRange. The AI's walk keeps its point out at sea, and fails three
// obstructed turns after the unit reaches the end of its path (PathResult's short-path hack), not 35.
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
	int ai_short = -1, ai_done = -1; // ticks: the AI walker's path walked and its step stopped, its walk ended
	for (int t = 0; t < (int)(15.0f / Simulation::FIXED_STEP); ++t) {
		s.tick(Simulation::FIXED_STEP);
		waded = waded || deep(walker) || deep(ai_walker) || (!aboard(s, ship, boarder) && deep(boarder));
		if (ai_short < 0 && s.walk_falls_short((size_t)idx(s, ai_walker)) && s.units_.motion[idx(s, ai_walker)].failed > 0)
			ai_short = t;
		if (ai_done < 0 && !s.unit_has_order(ai_walker)) ai_done = t;
	}
	const int32_t w = idx(s, walker);
	const float ai_wait = (float)(ai_done - ai_short) * Simulation::FIXED_STEP;
	std::printf("  walk to sea stopped at (%.1f, %.1f); the AI's walk ended %.2f s after it stopped at the end of its path; boarder waits at z %.1f\n",
			s.units().pos_x[w], s.units().pos_z[w], ai_wait, s.units().pos_z[idx(s, boarder)]);
	CHECK(!waded);
	CHECK(std::fabs(s.units().pos_x[w] + 20.0f) < 2.0f && s.units().pos_z[w] > 10.0f && !s.unit_has_order(walker));
	CHECK(s.units().pos_z[idx(s, ai_walker)] > 10.0f);
	CHECK(ai_short >= 0 && ai_done > ai_short && ai_wait <= 0.8f);
	// Its own walk done, the ship comes in to the beach for the waiting unit (UnitAI
	// PushOrderAfterForced("PickupUnit")), which boards.
	run(s, 10.0f);
	std::printf("  the ship came for the boarder: %s\n", aboard(s, ship, boarder) ? "aboard" : "still ashore");
	CHECK(aboard(s, ship, boarder));
}

// A bank where the water deepens fast: the navcells below z = 16 count as deep water, while the
// ground rises above the water line at 15.8.
static float bank_at(float, float z) { return std::clamp((std::fabs(z) - 15.5f) * 0.5f, -3.0f, 3.0f); }

// CheckUnitPlacement tests a spawn point on the grid grown by the unit's class clearance: a unit
// trained by a building on the bank, or set ashore by a ship, stands its clearance (0.8 m, 3 m for
// elephants and siege) off deep water, not at its edge.
static void test_spawn_clearance() {
	static Simulation s;
	fresh(s, bank_at);
	// A footprint 0.5 across 1.5 units up the bank, its rally point out at sea: the spots in front
	// of it lie at the edge of the deep water.
	float x = 0.0f, z = 0.0f;
	const bool found = s.pick_spawn_point_box(0.0f, 17.5f, 0.0f, 0.5f, 0.5f, 0.0f, true, 0.0f, 0.0f,
			UNIT_ELEPHANT, 0, x, z);
	std::printf("  an elephant trained on the bank spawns at (%.2f, %.2f)\n", x, z);
	CHECK(found && !s.near_deep_water(x, z, CLEARANCE_LARGE));
	const uint32_t ship = s.spawn_unit(0.0f, 13.0f, 0, UNIT_TRIREME, 0.0f, tmpl_of("athen/ship_arrow"));
	const uint32_t elephant = s.spawn_unit(30.0f, 30.0f, 0, UNIT_ELEPHANT);
	const size_t is = (size_t)idx(s, ship);
	float ex = 0.0f, ez = 0.0f, fx = 0.0f, fz = 0.0f;
	const bool ashore = s.ship_unload_point(is, ex, ez, (size_t)idx(s, elephant));
	const bool any = s.ship_unload_point(is, fx, fz, s.units().count);
	std::printf("  set ashore at (%.2f, %.2f), an elephant at (%.2f, %.2f)\n", fx, fz, ex, ez);
	CHECK(any && !s.near_deep_water(fx, fz, CLEARANCE_DEFAULT));
	CHECK(ashore && !s.near_deep_water(ex, ez, CLEARANCE_LARGE));
}

// Shallow water everywhere (0.3 under the nav water line, under 0 A.D.'s MaxWaterDepth 2 m) but the
// north-east corner.
static float shallows_at(float x, float z) { return (x > 40.0f && z > 40.0f) ? 1.0f : 0.0f; }

// A siege tower standing in the shallows sets its passengers down round it: PickSpawnPoint asks only
// the passenger's passability (CheckUnitPlacement), which wades shallow water, so a tower by a shore
// empties on every Unload, not only on its dry side.
static void test_siege_unloads_into_shallows() {
	static Simulation s;
	fresh(s, shallows_at);
	const uint32_t tower = s.spawn_unit(0.0f, 0.0f, 0, UNIT_BOLT_SHOOTER, 0.0f, tmpl_of("mace/siege_tower"));
	std::vector<uint32_t> ids;
	for (int a = 0; a < 3; ++a) ids.push_back(s.spawn_unit(1.0f, 0.5f, 0, UNIT_ARCHER));
	s.order_board_transport(ids.data(), ids.size(), tower);
	CHECK(s.get_transport_passenger_count(tower) == 3);
	s.order_disembark_all(tower);
	std::printf("  a siege tower in the shallows unloads: %d left aboard\n", (int)s.get_transport_passenger_count(tower));
	CHECK(s.get_transport_passenger_count(tower) == 0);
	for (uint32_t id : ids)
		CHECK(idx(s, id) >= 0 && s.units().alive[(size_t)idx(s, id)]);
}

// A wall on a strip of dry land too narrow to stand on beside it (navcells round it deep water).
static float g_strip_hu = 0.0f, g_strip_hv = 0.0f;
static float strip_at(float x, float z) {
	return (std::fabs(x) < g_strip_hu + 0.3f && std::fabs(z - 30.0f) < g_strip_hv + 0.3f) ? 3.0f : -3.0f;
}

// Turretable.LeaveTurret (Turretable.js 110-126): a unit comes down onto the holder's free spawn spot
// (PositionHelper.GetSpawnPosition, Footprint.PickSpawnPoint): beside a wall on a shore, on the land,
// not past the wall into the sea. With no free spot a "leave-turret" or an unload fails and the unit
// stays up; a forced leave (its point gone, the wall destroyed) puts it on the wall's centre.
static void test_turret_leave_spot() {
	static Simulation s;
	fresh(s);
	const uint32_t wall = s.spawn_building_world(0.0f, 17.5f, BLD_WALL_SEGMENT, 0, 1.0f);
	const auto &w = s.buildings_[s.building_id_to_idx_.at(wall)];
	std::vector<uint32_t> men;
	for (int k = 0; k < 4; ++k) {
		men.push_back(s.spawn_unit(-1.0f + k, 20.0f, 0, UNIT_ARCHER));
		CHECK(s.garrison_unit_now(men.back(), wall) && s.turret_holder_of(men.back()) == wall);
	}
	s.cmd_leave_turret(0, men.data(), men.size());
	int dry = 0;
	for (uint32_t id : men) {
		const size_t i = (size_t)idx(s, id);
		const float x = s.units().pos_x[i], z = s.units().pos_z[i];
		const auto [gx, gy] = s.nav_grid().world_to_cell(x, z);
		if (s.turret_holder_of(id) == 0 && !s.near_deep_water(x, z, CLEARANCE_DEFAULT) &&
				s.nav_grid().is_passable_for(gx, gy, PASS_INFANTRY) && std::fabs(x) <= w.hu + 2.0f) ++dry;
	}
	std::printf("  4 archers down off a wall on the shore: %d beside it on dry land\n", dry);
	CHECK(dry == 4);

	static Simulation probe;
	fresh(probe);
	const uint32_t pw = probe.spawn_building_world(0.0f, 30.0f, BLD_WALL_SEGMENT, 0, 1.0f);
	g_strip_hu = std::max(probe.buildings_[probe.building_id_to_idx_.at(pw)].hu, probe.buildings_[probe.building_id_to_idx_.at(pw)].hv);
	g_strip_hv = std::min(probe.buildings_[probe.building_id_to_idx_.at(pw)].hu, probe.buildings_[probe.building_id_to_idx_.at(pw)].hv);
	static Simulation t;
	fresh(t, strip_at);
	const uint32_t w2 = t.spawn_building_world(0.0f, 30.0f, BLD_WALL_SEGMENT, 0, 1.0f);
	const uint32_t a = t.spawn_unit(0.0f, 30.0f, 0, UNIT_ARCHER);
	CHECK(t.garrison_unit_now(a, w2) && t.turret_holder_of(a) == w2);
	t.cmd_leave_turret(0, &a, 1);
	t.cmd_unload(0, w2, &a, 1);
	std::printf("  an archer on a wall with nowhere to step down: %s\n", t.turret_holder_of(a) == w2 ? "stays up" : "came down");
	CHECK(t.turret_holder_of(a) == w2);
}

// A task walk's line is tested on every navcell it passes through, as CheckLineMovement walks
// them: one that clips a deep-water navcell between its ends crosses it, where a Bresenham line
// from navcell to navcell stepped round that cell.
static void test_line_clips_water() {
	static Simulation s;
	fresh(s);
	NavGrid &g = s.nav_grid_;
	const float cell = 2.0f * g.map_half / g.res;
	const auto [bx, by] = g.world_to_cell(-20.0f, 40.0f); // dry land north of the sea
	auto wx = [&](float c) { return -g.map_half + cell * c; };
	const float x0 = wx(bx + 0.5f), z0 = wx(by + 0.5f), x1 = wx(bx + 2.5f), z1 = wx(by + 1.5f);
	const bool before = s.line_crosses_impassable(x0, z0, x1, z1);
	g.cells.at(bx + 1, by) = NAV_WATER;
	const bool after = s.line_crosses_impassable(x0, z0, x1, z1), back = s.line_crosses_impassable(x1, z1, x0, z0);
	std::printf("  a line clipping one water navcell: crosses %d (both ways %d), %d before the water\n", after, back, before);
	CHECK(!before && after && back);
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
	run(s, 18.0f); // the dock search runs every fifth naval turn (10 s), and a field laid first takes a turn's one foundation
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
	for (int k = 0; k < 12; ++k) t.spawn_unit(-40.0f + (float)(k % 6) * 2.0f, 50.0f + (float)(k / 6) * 2.0f, 1, UNIT_VILLAGER); // workers: the ships queue isn't paused
	for (int k = 0; k < 4; ++k) t.spawn_resource_world(-40.0f + 10.0f * k, 0.0f, RES_FOOD, FISH_FOOD_SUPPLY, RES_SUB_FISH);
	run(t, 90.0f);
	int fishing = 0;
	for (size_t i = 0; i < t.units().count; ++i)
		if (t.units().alive[i] && t.units().team[i] == 1 && t.units().unit_type[i] == UNIT_FISHING_BOAT &&
		    (t.units().action_state[i] == ACTION_GATHER || t.units().action_state[i] == ACTION_RETURN_DROPSITE)) ++fishing;
	std::printf("Petra navy: %d fishing boats (%d fishing), %d warships\n", count_units(t, 1, UNIT_FISHING_BOAT), fishing, count_units(t, 1, UNIT_TRIREME));
	CHECK(count_units(t, 1, UNIT_FISHING_BOAT) == 1 && fishing == 1);
	CHECK(count_units(t, 1, UNIT_TRIREME) == 0);

	// navalManager.js decides at the start: a sea that began with over 1000 fish keeps its
	// boat after it is fished below 1000, until it is empty.
	static Simulation u;
	fresh(u);
	u.set_player_active(1, true, true);
	u.set_player_resources(1, 5000, 5000, 5000, 5000);
	u.spawn_building_world(-30.0f, 40.0f, BLD_TOWN_CENTER, 1);
	u.spawn_building_world(-48.0f, 20.0f, BLD_DOCK, 1);
	for (int k = 0; k < 12; ++k) u.spawn_unit(-40.0f + (float)(k % 6) * 2.0f, 50.0f + (float)(k / 6) * 2.0f, 1, UNIT_VILLAGER); // workers: the ships queue isn't paused
	for (int k = 0; k < 2; ++k) u.spawn_resource_world(-40.0f + 10.0f * k, 0.0f, RES_FOOD, FISH_FOOD_SUPPLY, RES_SUB_FISH);
	run(u, 2.0f); // the first AI turn decides the boats
	for (size_t r = 0; r < u.resources_.count; ++r)
		if (u.resources_.subtype[r] == RES_SUB_FISH) u.resources_.amount[r] = 300;
	run(u, 90.0f);
	std::printf("Petra navy: %d fishing boats on a sea fished down to 600\n", count_units(u, 1, UNIT_FISHING_BOAT));
	CHECK(count_units(u, 1, UNIT_FISHING_BOAT) == 1);
}

// attackPlan.js overseas: an attack on the other land waits for a transport ship,
// which navalManager.js trains (getBestShip "transport"), then sails across.
// transportPlan.js getBoardingPos with avoidEnnemy: an army lands on the shore of the
// target's land nearest the ship and target outside enemy territory, not on the beach
// below an enemy Civic Centre.
static void test_petra_landing_spot() {
	static Simulation s;
	fresh(s);
	s.set_victory_condition(VICTORY_ENDLESS);
	s.set_player_active(1, true, true);
	s.spawn_building_world(20.0f, -30.0f, BLD_TOWN_CENTER, 0);
	run(s, 2.0f);
	s.petra_pid_ = 1;
	const uint16_t land = s.petra_land_at(20.0f, -40.0f);
	auto [cx, cz] = s.nav_grid_.world_to_cell(0.0f, 0.0f);
	const uint16_t sea = s.nav_grid_.global_region_at(cx, cz, PASS_SHIP);
	CHECK(s.territory_owner_at(20.0f, -18.0f) == 0);
	float nx = 0.0f, nz = 0.0f, ax = 0.0f, az = 0.0f;
	CHECK(s.petra_boarding_pos(land, sea, 20.0f, 0.0f, 20.0f, -40.0f, false, nx, nz));
	CHECK(s.petra_boarding_pos(land, sea, 20.0f, 0.0f, 20.0f, -40.0f, true, ax, az));
	std::printf("Petra landing spot: (%.1f, %.1f) nearest, (%.1f, %.1f) outside enemy land\n", nx, nz, ax, az);
	CHECK(std::fabs(nx - 20.0f) < 3.0f && nz < -15.0f && nz > -20.0f); // the beach below the target
	CHECK(s.territory_owner_at(ax, az) != 0 && az < -15.0f && az > -20.0f); // a beach outside the enemy's land
	CHECK(s.petra_land_at(ax, az) == land);
}

// attackPlan.js chooseTarget/updatePreparation: with a base of ours on the target's land the
// plan rallies there and needs no ship for the attack itself; its army on the home land is
// ferried to the rally point, and marches from there.
static void test_petra_attack_from_far_base() {
	static Simulation s;
	fresh(s);
	s.set_victory_condition(VICTORY_ENDLESS);
	s.set_player_active(1, true, true);
	s.set_player_resources(1, 5000, 5000, 5000, 5000);
	s.players_[1].phase = 2;
	s.spawn_building_world(-30.0f, 40.0f, BLD_TOWN_CENTER, 1);
	s.spawn_building_world(-48.0f, 20.0f, BLD_DOCK, 1);
	s.spawn_building_world(-36.0f, -44.0f, BLD_MILITARY_COLONY, 1); // a base on the south land
	const uint32_t target = s.spawn_building_world(30.0f, -40.0f, BLD_OUTPOST, 0);
	std::vector<uint32_t> army;
	for (int k = 0; k < 6; ++k) army.push_back(s.spawn_unit(-36.0f + 2.0f * k, 30.0f, 1, UNIT_SPEARMAN));
	s.petra_army_ids_[1] = army;
	run(s, 1.0f);
	const uint16_t overseas = s.petra_plan_overseas(-30.0f, 40.0f, target, true);
	std::printf("Petra attack from a far base: overseas %u from any base, %u from the first\n",
	            (unsigned)overseas, (unsigned)s.petra_plan_overseas(-30.0f, 40.0f, target));
	CHECK(overseas == 0);
	s.petra_attack_mgr_.kind = PETRA_ATTACK_ATTACK;
	s.petra_attack_mgr_.target_size = 6;
	s.petra_attack_mgr_.min_size = 6;
	s.petra_attack_mgr_.rushes_set = 1;
	s.petra_attack_plan_.state = Simulation::AttackPlan::MUSTER;
	s.petra_attack_plan_.target_building_id = target;
	s.petra_attack_plan_.state_timer = 0.0f;
	s.petra_attack_plan_.forced = true;
	bool completing = false;
	for (int k = 0; k < 240 && !completing; ++k) {
		run(s, 1.0f);
		completing = s.petra_attack_plan_.completing;
	}
	const auto &plan = s.petra_attack_plan_;
	std::printf("Petra attack from a far base: completing %s, rally (%.0f, %.0f), %zu in the squad, ferry %s\n",
	            completing ? "yes" : "no", plan.staging_x, plan.staging_z, plan.squad.size(),
	            s.petra_transport_for(PETRA_TRANSPORT_ATTACK) ? "yes" : "no");
	CHECK(completing && plan.staging_z < -16.0f);
	int south = 0;
	bool marched = false;
	for (int k = 0; k < 300 && !(marched && south >= 4); ++k) {
		run(s, 1.0f);
		marched |= s.petra_started_attacks_.size() > 0 || s.petra_attack_plan_.state == Simulation::AttackPlan::WALKING;
		south = 0;
		for (uint32_t uid : army) {
			const int32_t i = idx(s, uid);
			if (i >= 0 && s.units().alive[i] && s.units().pos_z[i] < -16.0f) ++south;
		}
	}
	std::printf("Petra attack from a far base: %d of 6 ferried to the south land, marched %s\n", south, marched ? "yes" : "no");
	CHECK(south >= 4 && marched);
}

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
	slot.petra_attack_plan_.forced = true; // six spearmen: no plan's classes are full (mustStart)
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
		// Support workers: the starting villagers and the civilians Petra trains (#578).
		if (!s.units().alive[i] || s.units().team[i] != 1 ||
		    unit_gather_class(s.units().unit_type[i], s.units().tmpl[i]) != GATHER_SUPPORT) continue;
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

// queueplanBuilding.js buildOverseaDock: with no room for a civic centre, Petra lays a dock on an
// ally's island across the sea (not on an enemy's, and not where it has a dock already).
static void test_petra_oversea_dock() {
	static Simulation s;
	auto dock_south = [&]() {
		int n = 0;
		for (const auto &b : s.buildings())
			if (b.team == 1 && b.type == BLD_DOCK && b.hp > 0.0f && b.z < 0.0f) ++n;
		return n;
	};
	int placed[3] = {};
	for (int c = 0; c < 3; ++c) {
		fresh(s);
		s.set_victory_condition(VICTORY_ENDLESS);
		s.set_player_active(1, true, true);
		s.set_player_resources(1, 5000, 5000, 5000, 5000);
		s.players_[1].phase = 2;
		const uint8_t stance = c == 1 ? Simulation::DIPLO_ENEMY : Simulation::DIPLO_ALLY;
		s.set_diplomacy(1, 2, stance);
		s.set_diplomacy(2, 1, stance);
		s.spawn_building_world(-30.0f, 40.0f, BLD_TOWN_CENTER, 1);
		s.spawn_building_world(-48.0f, 20.0f, BLD_DOCK, 1);
		s.spawn_building_world(30.0f, -40.0f, BLD_TOWN_CENTER, 2);
		s.spawn_unit(-30.0f, 30.0f, 1, UNIT_VILLAGER); // a builder (hasBuilder)
		if (c == 2) s.spawn_building_world(40.0f, -20.0f, BLD_DOCK, 1);
		s.petra_pid_ = 1;
		const int before = dock_south();
		const int wood = s.player_resource(1, RES_WOOD);
		s.petra_build_oversea_dock();
		placed[c] = dock_south() - before;
		if (c == 0) {
			const Simulation::Building *d = nullptr;
			for (const auto &b : s.buildings())
				if (b.team == 1 && b.type == BLD_DOCK && b.z < 0.0f) d = &b;
			std::printf("Petra oversea dock: on the ally's island at (%.1f, %.1f), wood %d -> %d\n",
			            d ? d->x : 0.0f, d ? d->z : 0.0f, wood, s.player_resource(1, RES_WOOD));
			CHECK(d && d->build_progress < 1.0f && s.player_resource(1, RES_WOOD) < wood);
		}
	}
	std::printf("Petra oversea dock: ally %d, enemy %d, dock already there %d\n", placed[0], placed[1], placed[2]);
	CHECK(placed[0] == 1 && placed[1] == 0 && placed[2] == 0);
}

// sicilia_nomad: Petra starts aboard a ship with too little stone for a civic centre.
// buildFirstBase counts the passengers where their ship is, lays a dock out on the nearest
// shore (findDockPosition with proximity), createTransportIfNeeded lands them there, and once
// the dock stands they gather from it (basesManager.js noBase) for the civic centre.
static void test_petra_start_at_sea() {
	static Simulation s;
	fresh(s);
	s.set_victory_condition(VICTORY_ENDLESS);
	s.set_player_active(1, true, true);
	s.set_player_resources(1, 300, 450, 300, 200);
	const uint32_t ship = s.spawn_unit(10.0f, 0.0f, 1, UNIT_TRIREME);
	std::vector<uint32_t> crew;
	for (int k = 0; k < 8; ++k) crew.push_back(s.spawn_unit(10.0f, 0.0f, 1, UNIT_VILLAGER));
	s.order_board_transport(crew.data(), crew.size(), ship);
	for (int k = 0; k < 6; ++k) s.spawn_resource_world(-6.0f + 3.0f * k, -34.0f, RES_STONE, 2000);
	for (int k = 0; k < 6; ++k) s.spawn_resource_world(-6.0f + 3.0f * k, -44.0f, RES_WOOD, 600);
	const size_t aboard = s.get_transport_passenger_count(ship);
	const Simulation::Building *dock = nullptr, *cc = nullptr;
	float landed = -1.0f;
	for (int k = 0; k < 300 && !cc; ++k) {
		run(s, 1.0f);
		if (landed < 0.0f && s.get_transport_passenger_count(ship) == 0) landed = (float)k;
		dock = cc = nullptr;
		for (const auto &b : s.buildings()) {
			if (b.team != 1) continue;
			if (b.type == BLD_DOCK) dock = &b;
			if (b.type == BLD_TOWN_CENTER) cc = &b;
		}
	}
	std::printf("Petra start at sea: %zu aboard, ashore at %.0fs, dock at (%.1f, %.1f) built %.0f%%, civic centre %s, stone %d\n",
	            aboard, landed, dock ? dock->x : 0.0f, dock ? dock->z : 0.0f, dock ? 100.0f * dock->build_progress : 0.0f,
	            cc ? "laid out" : "none", s.player_resource(1, RES_STONE));
	CHECK(aboard == 8);
	CHECK(landed >= 0.0f);
	CHECK(dock && dock->build_progress >= 1.0f);
	CHECK(cc != nullptr);
}

// defenseArmy.js assignUnit -> navalManager.requireTransport: defenders on another land than
// the foe they are sent against are ferried over, instead of walking at the shore.
static void test_petra_defenders_ferried() {
	static Simulation s;
	fresh(s);
	s.set_victory_condition(VICTORY_ENDLESS);
	s.set_player_active(1, true, true);
	s.set_player_resources(1, 5000, 5000, 5000, 5000);
	s.players_[1].phase = 2;
	s.spawn_building_world(-30.0f, 40.0f, BLD_TOWN_CENTER, 1);
	s.spawn_building_world(-48.0f, 20.0f, BLD_DOCK, 1);
	// A second civic centre on the south land, which an enemy squad attacks.
	const uint32_t south_cc = s.spawn_building_world(-30.0f, -40.0f, BLD_TOWN_CENTER, 1);
	s.spawn_unit(-40.0f, 10.0f, 1, UNIT_TRIREME);
	std::vector<uint32_t> army;
	// assignDefenders keeps up to 12 units with no army on their land at home as a backup: the
	// ones past those are ferried.
	for (int k = 0; k < 18; ++k) army.push_back(s.spawn_unit(-36.0f + 2.0f * (k % 6), 30.0f + 2.0f * (k / 6), 1, UNIT_SPEARMAN));
	s.petra_army_ids_[1] = army;
	std::sort(s.petra_army_ids_[1].begin(), s.petra_army_ids_[1].end());
	s.petra_attack_mgr_.rushes_set = 1;
	const uint16_t south = s.petra_land_at(-30.0f, -40.0f), north = s.petra_land_at(-30.0f, 40.0f);
	run(s, 2.0f);
	// Enough of them that the trireme, a defender too (assignDefenders takes warships), can't
	// meet the need alone.
	for (int k = 0; k < 10; ++k) {
		const uint32_t foe = s.spawn_unit(-22.0f + 2.0f * (k % 5), -34.0f - 2.0f * (k / 5), 0, UNIT_SPEARMAN);
		s.order_attack_building(&foe, 1, south_cc, false, false);
	}
	bool planned = false;
	int landed = 0;
	for (int k = 0; k < 150 && landed < 2; ++k) {
		run(s, 1.0f);
		for (const auto &p : s.petra_transport_plans()) planned |= p.owner == PETRA_TRANSPORT_DEFENSE;
		landed = 0;
		for (uint32_t uid : army) {
			const int32_t i = idx(s, uid);
			if (i >= 0 && s.units().alive[i] && s.petra_land_at(s.units().pos_x[i], s.units().pos_z[i]) == south) ++landed;
		}
	}
	std::printf("Petra defenders ferried: lands %d -> %d, defense transport %s, %d of 18 spearmen on the south land\n",
	            (int)north, (int)south, planned ? "planned" : "never planned", landed);
	CHECK(north != south);
	CHECK(planned);
	CHECK(landed >= 2);
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
	s.spawn_unit(-40.0f, 10.0f, 1, UNIT_TRIREME);
	s.spawn_unit(-20.0f, 10.0f, 1, UNIT_TRIREME);
	// A ship for the attack besides the two the builders' crossings may take.
	s.spawn_unit(-30.0f, 4.0f, 1, UNIT_TRIREME);
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
	for (int k = 0; k < 360 && !plan_of(PETRA_TRANSPORT_BUILDERS); ++k) run(s, 1.0f);
	CHECK(plan_of(PETRA_TRANSPORT_BUILDERS) != nullptr);
	// Several plans per owner (navalManager.js requireTransport/splitTransport): a second
	// builders' crossing starts on the free ship instead of waiting for the first.
	std::vector<uint32_t> probe;
	probe.push_back(s.spawn_unit(-44.0f, 52.0f, 1, UNIT_VILLAGER));
	const size_t nplans = s.petra_transport_plans().size();
	CHECK(s.petra_start_transport(probe, -30.0f, 40.0f, -30.0f, -40.0f, 0, PETRA_TRANSPORT_BUILDERS));
	// Undo the probe, leaving the ships and units for the attack below.
	if (s.petra_transport_plans().size() > nplans) {
		uint32_t probe_ship = s.petra_transport_plans().back().ship_id;
		s.cmd_stop(&probe_ship, 1);
		s.petra_transports_.pop_back();
	}
	s.cmd_stop(probe.data(), probe.size());
	// The target, placed now: Petra's soldiers would capture it while the builders cross.
	const uint32_t house = s.spawn_building_world(30.0f, -40.0f, BLD_OUTPOST, 0); // an outpost: no decay on neutral land
	s.petra_attack_mgr_.kind = PETRA_ATTACK_ATTACK;
	s.petra_attack_mgr_.target_size = 6;
	s.petra_attack_mgr_.min_size = 6;
	s.petra_attack_mgr_.rushes_set = 1;
	// A fresh plan: Petra may already muster its own, with the soldiers it trained meanwhile.
	s.petra_attack_plan_ = Simulation::AttackPlan();
	s.petra_attack_plan_.state = Simulation::AttackPlan::MUSTER;
	s.petra_attack_plan_.target_building_id = house;
	s.petra_attack_plan_.state_timer = 0.0f;
	// Forced (as an ally's call for help): it sets off with what it has, whatever siege class
	// the plan rotation would still have it wait for.
	s.petra_attack_plan_.forced = true;
	// The army is the six given above: drop the batches Petra queues for the attack, which
	// the plan would wait for (updatePreparation waits for its units in training), here and
	// on each second below (trainMoreUnits keeps filling its ranged and cavalry classes).
	auto drop_attack_batches = [&] {
		for (auto &b : s.buildings_)
			if (b.team == 1)
				b.queue.erase(std::remove_if(b.queue.begin(), b.queue.end(),
				                             [](const ProductionOrder &o) { return o.petra_plan == PETRA_ORDER_ATTACK; }),
				              b.queue.end());
	};
	drop_attack_batches();
	bool both = false, shared = false;
	std::vector<uint32_t> landed;
	// The most workers (not the army's soldiers) on the south land at once: once the civic centre
	// stands, Petra may ship a builder home again for a resource the new base lacks. The crew is
	// whoever is idle first, the women it trained meanwhile as well as the starting villagers.
	int villagers_south = 0;
	for (int k = 0; k < 120; ++k) {
		// Petra may replace the plan with its own (it picks the same target): that one is forced too.
		if (s.petra_attack_plan_.state == Simulation::AttackPlan::MUSTER) {
			drop_attack_batches();
			s.petra_attack_plan_.forced = true;
		}
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
		int now_south = 0;
		for (size_t i = 0; i < s.units().count; ++i)
			if (s.units().alive[i] && s.units().team[i] == 1 && s.petra_is_worker(i) &&
			    s.petra_land_at(s.units().pos_x[i], s.units().pos_z[i]) == south) ++now_south;
		villagers_south = std::max(villagers_south, now_south);
	}
	std::printf("Petra concurrent transports: both under way at once %s, %zu of 6 spearmen landed, at most %d workers on the south land at once\n",
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

// navalManager.checkEvents: a transport ship sunk while sailing sets its infantry down on the
// nearest shore; those not on the destination's land ask for another transport, which takes
// them across instead of the plan ending.
static void test_petra_transport_sunk_at_sea() {
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
	for (int k = 0; k < 120 && !(s.petra_transport_plans().size() == 1 && s.petra_transport_plans()[0].state == PetraTransportPlan::SAILING); ++k)
		run(s, 0.5f);
	const bool sailing = s.petra_transport_plans().size() == 1 && s.petra_transport_plans()[0].state == PetraTransportPlan::SAILING;
	const size_t aboard = s.get_transport_passenger_count(first);
	// Sunk off the north shore it left: the spearmen swim back to it.
	const int32_t fi = idx(s, first);
	s.units_.pos_x[fi] = -30.0f;
	s.units_.pos_z[fi] = 13.0f;
	s.cmd_delete(1, &first, 1);
	uint32_t took = 0;
	int landed = 0;
	for (int k = 0; k < 200; ++k) {
		run(s, 1.0f);
		if (!s.petra_transport_plans().empty() && s.petra_transport_plans()[0].ship_id) took = s.petra_transport_plans()[0].ship_id;
		landed = 0;
		for (uint32_t uid : crew) {
			const int32_t i = idx(s, uid);
			if (i >= 0 && s.units().alive[i] && s.units().pos_z[i] < -16.0f) ++landed;
		}
		if (landed >= (int)aboard && aboard) break;
	}
	std::printf("Petra transport sunk at sea: sailing %s with %zu aboard, plan moved to ship %s, %d landed\n", sailing ? "yes" : "no",
	            aboard, took == second ? "two" : "none", landed);
	CHECK(sailing && aboard >= 2);
	CHECK(took == second && landed >= (int)aboard);
}

// An islet in the sea before the south shore.
static float islet_height_at(float x, float z) {
	const float ix = x + 30.0f, iz = z;
	if (ix * ix + iz * iz < 5.0f * 5.0f) return 3.0f;
	return height_at(x, z);
}

// transportPlan.js onSailing: units unloaded on another land than the destination go back
// aboard and the ship sails for another landing point on the right land.
static void test_petra_transport_wrong_land() {
	static Simulation s;
	fresh(s, islet_height_at);
	s.set_victory_condition(VICTORY_ENDLESS);
	s.set_player_active(1, true, true);
	s.set_player_resources(1, 5000, 5000, 5000, 5000);
	s.spawn_building_world(-30.0f, 40.0f, BLD_TOWN_CENTER, 1);
	s.spawn_building_world(-48.0f, 20.0f, BLD_DOCK, 1);
	const uint32_t ship = s.spawn_unit(-20.0f, 12.0f, 1, UNIT_TRIREME);
	std::vector<uint32_t> crew;
	for (int k = 0; k < 4; ++k) crew.push_back(s.spawn_unit(-24.0f + 2.0f * k, 26.0f, 1, UNIT_SPEARMAN));
	run(s, 0.5f);
	const uint16_t south = s.petra_land_at(-20.0f, -40.0f), islet = s.petra_land_at(-30.0f, 0.0f);
	CHECK(south && islet && south != islet);
	CHECK(s.petra_start_transport(crew, -20.0f, 30.0f, -20.0f, -40.0f, 0, PETRA_TRANSPORT_ATTACK));
	for (int k = 0; k < 120 && !(s.petra_transport_plans().size() == 1 && s.petra_transport_plans()[0].state == PetraTransportPlan::SAILING); ++k)
		run(s, 0.5f);
	const size_t aboard = s.get_transport_passenger_count(ship);
	// The ship stops beside the islet: it sets its passengers down there.
	const int32_t si = idx(s, ship);
	s.units_.pos_x[si] = -30.0f;
	s.units_.pos_z[si] = -7.5f;
	s.cmd_stop(&ship, 1);
	int on_islet = 0, landed = 0;
	for (int k = 0; k < 150; ++k) {
		run(s, 1.0f);
		landed = 0;
		for (uint32_t uid : crew) {
			const int32_t i = idx(s, uid);
			if (i < 0 || !s.units().alive[i]) continue;
			const uint16_t land = s.petra_land_at(s.units().pos_x[i], s.units().pos_z[i]);
			landed += land == south;
			if (k > 20) on_islet += land == islet; // the recovery takes a turn or two
		}
		if (landed >= (int)aboard && aboard) break;
	}
	std::printf("Petra transport set down on the wrong land: %zu aboard, %d landed on the south land, %d unit-seconds left on the islet\n",
	            aboard, landed, on_islet);
	CHECK(aboard >= 2);
	CHECK(landed >= (int)aboard && on_islet == 0);
}

// navalManager.checkEvents: survivors of a ship sunk while sailing, set down on two lands (the
// north shore and the islet), each ask for a transport (requireTransport) and all reach the
// destination.
static void test_petra_sunk_survivors_two_lands() {
	static Simulation s;
	fresh(s, islet_height_at);
	s.set_victory_condition(VICTORY_ENDLESS);
	s.set_player_active(1, true, true);
	s.set_player_resources(1, 5000, 5000, 5000, 5000);
	s.spawn_building_world(-30.0f, 40.0f, BLD_TOWN_CENTER, 1);
	s.spawn_building_world(-48.0f, 20.0f, BLD_DOCK, 1);
	const uint32_t first = s.spawn_unit(-20.0f, 12.0f, 1, UNIT_TRIREME);
	s.spawn_unit(30.0f, 8.0f, 1, UNIT_TRIREME);
	s.spawn_unit(34.0f, 8.0f, 1, UNIT_TRIREME);
	std::vector<uint32_t> crew;
	for (int k = 0; k < 4; ++k) crew.push_back(s.spawn_unit(-24.0f + 2.0f * k, 26.0f, 1, UNIT_SPEARMAN));
	run(s, 0.5f);
	const uint16_t south = s.petra_land_at(-20.0f, -40.0f), islet = s.petra_land_at(-30.0f, 0.0f), north = s.petra_land_at(-30.0f, 30.0f);
	CHECK(s.petra_start_transport(crew, -20.0f, 30.0f, -20.0f, -40.0f, 0, PETRA_TRANSPORT_ATTACK));
	for (int k = 0; k < 120 && !(s.petra_transport_plans().size() == 1 && s.petra_transport_plans()[0].state == PetraTransportPlan::SAILING); ++k)
		run(s, 0.5f);
	const size_t aboard = s.get_transport_passenger_count(first);
	const int32_t fi = idx(s, first);
	s.units_.pos_x[fi] = -30.0f;
	s.units_.pos_z[fi] = 13.0f;
	s.cmd_delete(1, &first, 1);
	s.tick(Simulation::FIXED_STEP);
	// Two swam to the islet, the others back to the north shore.
	int n = 0;
	for (uint32_t uid : crew) {
		const int32_t i = idx(s, uid);
		if (i < 0 || !s.units().alive[i]) continue;
		const bool to_islet = n++ < 2;
		s.units_.pos_x[i] = to_islet ? -30.0f + (float)n : -30.0f + (float)n;
		s.units_.pos_z[i] = to_islet ? 0.0f : 28.0f;
		s.units_.target_x[i] = s.units_.pos_x[i];
		s.units_.target_z[i] = s.units_.pos_z[i];
	}
	int on_islet = 0, on_north = 0;
	for (uint32_t uid : crew) {
		const int32_t i = idx(s, uid);
		if (i < 0 || !s.units().alive[i]) continue;
		const uint16_t land = s.petra_land_at(s.units().pos_x[i], s.units().pos_z[i]);
		on_islet += land == islet;
		on_north += land == north;
	}
	// Each counts once it stands on the south land (as workers they may be ferried home again).
	std::vector<uint32_t> reached;
	for (int k = 0; k < 240 && reached.size() < crew.size(); ++k) {
		run(s, 0.5f);
		for (uint32_t uid : crew) {
			const int32_t i = idx(s, uid);
			if (i >= 0 && s.units().alive[i] && s.petra_land_at(s.units().pos_x[i], s.units().pos_z[i]) == south &&
			    std::find(reached.begin(), reached.end(), uid) == reached.end())
				reached.push_back(uid);
		}
	}
	const int landed = (int)reached.size();
	std::printf("Petra survivors on two lands: %zu aboard, %d on the islet, %d on the north shore, %d landed south\n", aboard, on_islet,
	            on_north, landed);
	CHECK(aboard == 4 && on_islet == 2 && on_north == 2);
	CHECK(landed == 4);
}

// transportPlan.js never leaves units aboard: an idle ship of Petra's carrying units outside any
// transport plan (one boarded after its plan gave up) sets them down on the shore.
static void test_petra_laden_ship_lands() {
	static Simulation s;
	fresh(s);
	s.set_victory_condition(VICTORY_ENDLESS);
	s.set_player_active(1, true, true);
	s.set_player_resources(1, 5000, 5000, 5000, 5000);
	s.spawn_building_world(-30.0f, 40.0f, BLD_TOWN_CENTER, 1);
	const uint32_t ship = s.spawn_unit(-30.0f, 0.0f, 1, UNIT_TRIREME);
	uint32_t pair[2];
	for (int k = 0; k < 2; ++k) pair[k] = s.spawn_unit(-30.0f, 0.0f, 1, UNIT_SPEARMAN);
	s.order_board_transport(pair, 2, ship);
	const size_t aboard = s.get_transport_passenger_count(ship);
	float ashore = -1.0f;
	for (int k = 0; k < 120 && ashore < 0.0f; ++k) {
		run(s, 1.0f);
		if (s.get_transport_passenger_count(ship) == 0) ashore = (float)k;
	}
	std::printf("Petra laden ship outside a plan: %zu aboard, ashore at %.0fs\n", aboard, ashore);
	CHECK(aboard == 2);
	CHECK(ashore >= 0.0f);
}

// DeathDamage.js: a fire ship ignited (Upgrade/Ignition to ship_fire_fire, mixins/fireship.xml)
// burns down at RegenRate -200 and, dying, deals Crush 500 over 22 m (x3 vs Ship and Building)
// to enemies only, with quadratic falloff. Before ignition it neither burns nor blasts. Other ships and
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
	// The Ptolemaic Juggernaut's parent is template_unit_ship_warship_siege: MaxDistance 24, not 12.
	const uint32_t siege = s.spawn_unit(-30.0f, -12.0f, 0, UNIT_TRIREME, 0.0f, tmpl_of("cart/ship_siege"));
	const uint32_t jugg = s.spawn_unit(-30.0f, -18.0f, 0, UNIT_TRIREME, 0.0f, tmpl_of("ptol/champion_juggernaut"));
	const float r_war = s.treasure_reach((size_t)s.lookup_unit(war));
	const float r_siege = s.treasure_reach((size_t)s.lookup_unit(siege));
	const float r_jugg = s.treasure_reach((size_t)s.lookup_unit(jugg));
	std::printf("ship treasure reach: warship %.2f, siege ship %.2f, juggernaut %.2f\n", r_war, r_siege, r_jugg);
	CHECK(r_siege > r_war);
	CHECK(r_jugg == r_siege);
}

// template_gaia_treasure: a shipwreck floating at sea blocks movement (Obstruction/Static,
// BlockMovement), so a ship sailing past steers round it instead of through it.
static void test_ship_round_wreck() {
	static Simulation s;
	fresh(s);
	const uint32_t wreck = s.spawn_treasure(-10.0f, 0.0f, TREASURE_SHIPWRECK);
	const uint32_t ship = s.spawn_unit(-30.0f, 0.0f, 0, UNIT_TRIREME);
	s.cmd_move(&ship, 1, 10.0f, 0.0f);
	s.ensure_gaia_obstacles();
	Simulation::SteerObstacle w{};
	for (const auto &o : s.gaia_obstacles_)
		if (o.id == wreck) w = o;
	const nav::Vec2 u = nav::footprint_u(w), v = nav::footprint_v(w);
	float worst = 1e9f; // the ship centre's least clearance outside the wreck's square (negative: inside)
	int arrived = -1;
	for (int t = 0; t < 900 && arrived < 0; ++t) {
		s.tick(Simulation::FIXED_STEP);
		const int i = s.lookup_unit(ship);
		const auto un = s.units();
		const nav::Vec2 p = nav::Vec2(un.pos_x[i], un.pos_z[i]) - nav::Vec2(w.x, w.z);
		worst = std::min(worst, std::max(std::fabs(p.dot(u)) - w.hu, std::fabs(p.dot(v)) - w.hv));
		if (std::hypot(un.pos_x[i] - 10.0f, un.pos_z[i]) < 1.0f) arrived = t;
	}
	std::printf("ship past a floating wreck: arrived after %d ticks, closest %.2f outside its square (%.1f x %.1f)\n",
	            arrived, worst, 2.0f * w.hu, 2.0f * w.hv);
	CHECK(arrived >= 0);
	CHECK(worst > 0.0f);
	// Sent to collect it, the ship still sails up to it.
	const float wood0 = s.player(0).wood();
	s.order_collect_treasure(&ship, 1, wreck);
	run(s, 30.0f);
	CHECK(s.player(0).wood() > wood0);
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

// Ships drop off at any Naval dropsite (ResourceGatherer.js): the Briton crannog, a civic
// centre on the shore, takes a fishing boat's catch as a dock does.
static void test_crannog_takes_fish() {
	static Simulation s;
	fresh(s);
	s.set_team_civ(0, CIV_BRITONS);
	s.spawn_building_world(-48.0f, 20.0f, BLD_CRANNOG, 0);
	const uint32_t fish = s.spawn_resource_world(-36.0f, 6.0f, RES_FOOD, FISH_FOOD_SUPPLY, RES_SUB_FISH);
	const uint32_t boat = s.spawn_unit(-44.0f, 10.0f, 0, UNIT_FISHING_BOAT);
	s.order_gather(&boat, 1, fish);
	run(s, 90.0f);
	const int food = s.player_resource(0, RES_FOOD);
	std::printf("crannog: %d food landed by a fishing boat in 90 s\n", food);
	CHECK(food > 0 && gather_target(s, boat) == fish);
}

// UnitAI FindNearbyResource: a boat whose fish runs out looks for the next one within 64 m
// (17.9 units) and goes idle rather than sail to one farther off, or to one in unexplored
// water (CheckTargetVisible).
static void test_next_fish_range() {
	static Simulation s;
	fresh(s);
	s.set_explore_map(true);
	s.spawn_building_world(-48.0f, 20.0f, BLD_DOCK, 0);
	const uint32_t small = s.spawn_resource_world(-36.0f, 6.0f, RES_FOOD, 16, RES_SUB_FISH);
	const uint32_t far = s.spawn_resource_world(-12.0f, 4.0f, RES_FOOD, FISH_FOOD_SUPPLY, RES_SUB_FISH);
	const uint32_t boat = s.spawn_unit(-40.0f, 8.0f, 0, UNIT_FISHING_BOAT);
	s.order_gather(&boat, 1, small);
	run(s, 40.0f);
	std::printf("next fish: fish 24 units off taken: %d\n", (int)(gather_target(s, boat) == far));
	CHECK(subtype_of(s, small) == nullptr && gather_target(s, boat) == 0);

	// A fish 16 units off is in range, but nobody has seen its water (a fishing boat sees 30 m).
	for (bool explored : {false, true}) {
		static Simulation t;
		fresh(t);
		t.set_explore_map(explored);
		t.spawn_building_world(-48.0f, 20.0f, BLD_DOCK, 0);
		const uint32_t first = t.spawn_resource_world(-36.0f, 6.0f, RES_FOOD, 16, RES_SUB_FISH);
		const uint32_t hidden = t.spawn_resource_world(-25.0f, -5.0f, RES_FOOD, FISH_FOOD_SUPPLY, RES_SUB_FISH);
		const uint32_t b = t.spawn_unit(-40.0f, 8.0f, 0, UNIT_FISHING_BOAT);
		t.order_gather(&b, 1, first);
		run(t, 40.0f);
		std::printf("next fish: explored %d, boat moved on to the fish 16 units off: %d\n", (int)explored, (int)(gather_target(t, b) == hidden));
		CHECK((gather_target(t, b) == hidden) == explored);
	}
}

// entityExtend.js gatherTreasure: a land collector only takes treasures on its own land
// (getLandAccess), so a wreck out at sea is left to ships.
static void test_petra_land_skips_wreck() {
	static Simulation s;
	fresh(s);
	s.set_player_active(1, true, true);
	s.petra_pid_ = 1;
	const uint32_t worker = s.spawn_unit(0.0f, 22.0f, 1, UNIT_VILLAGER);
	s.spawn_treasure(0.0f, 4.0f, TREASURE_SHIPWRECK);
	Simulation::PetraHuntScan scan = s.petra_hunt_scan();
	const int32_t wi = s.lookup_unit(worker);
	CHECK(!s.petra_gather_treasure((size_t)wi, scan));
	const uint32_t barrel = s.spawn_treasure(14.0f, 26.0f, TREASURE_FOOD_BARREL);
	scan = s.petra_hunt_scan();
	CHECK(s.petra_gather_treasure((size_t)s.lookup_unit(worker), scan));
	const auto o = s.unit_order(worker);
	std::printf("Petra land treasure: order %d target %u (barrel %u)\n", (int)o.kind, o.target, barrel);
	CHECK(o.kind == ORDER_COLLECT_TREASURE && o.target == barrel);
}

// worker.js startFishing: Petra's idle boat takes the fish nearest its dock, not the one
// nearest itself; with no fish left on its sea (live whales aside) the boat is deleted.
static void test_petra_fishing() {
	static Simulation s;
	fresh(s);
	s.set_player_active(1, true, true);
	s.spawn_building_world(-48.0f, 20.0f, BLD_DOCK, 1);
	const uint32_t by_dock = s.spawn_resource_world(-36.0f, 6.0f, RES_FOOD, FISH_FOOD_SUPPLY, RES_SUB_FISH);
	const uint32_t by_boat = s.spawn_resource_world(24.0f, 4.0f, RES_FOOD, FISH_FOOD_SUPPLY, RES_SUB_FISH);
	const uint32_t boat = s.spawn_unit(20.0f, 8.0f, 1, UNIT_FISHING_BOAT);
	s.petra_pid_ = 1;
	s.petra_manage_naval(2.0f);
	const uint32_t picked = gather_target(s, boat);
	std::printf("Petra fishing: picked fish %u (by the dock %u, by the boat %u)\n", picked, by_dock, by_boat);
	CHECK(picked == by_dock);

	for (size_t r = 0; r < s.resources_.count; ++r)
		if (s.resources_.subtype[r] == RES_SUB_FISH) s.resources_.amount[r] = 0;
	// filters.js isFishable skips what can move: a live whale left on the sea is no fish for Petra.
	s.spawn_fauna(0.0f, -4.0f, FAUNA_WHALE_FIN);
	s.cmd_stop(&boat, 1);
	s.petra_manage_naval(2.0f);
	const int32_t bi = s.lookup_unit(boat);
	std::printf("Petra fishing: boat on an empty sea %s\n", bi >= 0 && s.units().alive[bi] ? "kept" : "deleted");
	CHECK(bi < 0 || !s.units().alive[bi]);
}

// defenseManager.js checkEvents: an idle or gathering fishing boat that is attacked moves out
// of its attacker's range (ranged range + 15 m).
static void test_petra_fishing_boat_backs_off() {
	static Simulation s;
	fresh(s);
	s.set_victory_condition(VICTORY_ENDLESS);
	s.set_player_active(1, true, true);
	s.spawn_building_world(-30.0f, 40.0f, BLD_TOWN_CENTER, 1);
	s.spawn_building_world(-48.0f, 20.0f, BLD_DOCK, 1);
	s.spawn_resource_world(18.0f, 4.0f, RES_FOOD, FISH_FOOD_SUPPLY, RES_SUB_FISH); // else Petra deletes the idle boat
	const uint32_t boat = s.spawn_unit(20.0f, 8.0f, 1, UNIT_FISHING_BOAT, 1.0e5f);
	const uint32_t ship = s.spawn_unit(24.0f, 8.0f, 0, UNIT_TRIREME);
	const int32_t si = s.lookup_unit(ship);
	const float reach = s.units_.attack_range[si] + 15.0f * 0.28f;
	s.order_attack(&ship, 1, boat);
	// Petra's next turn after the first hit: the boat leaves its fish for a spot out of reach.
	const int32_t bi = s.lookup_unit(boat);
	bool away = false;
	float d = 0.0f;
	for (int k = 0; k < 40 && !away; ++k) {
		run(s, 0.1f);
		away = s.units_.order_kind[bi] == ORDER_WALK;
		const float dx = s.units_.order_x[bi] - s.units().pos_x[si], dz = s.units_.order_z[bi] - s.units().pos_z[si];
		d = std::sqrt(dx * dx + dz * dz);
	}
	std::printf("Petra fishing boat attacked: walks off %s, to %.1f from the warship (its reach + 15 m: %.1f)\n", away ? "yes" : "no", d, reach);
	CHECK(away && d >= reach);
}

// startingStrategy.js configFirstBase: on a starting land under 25000 m² Petra keeps to one
// field and lays a corral as well (saveSpace, maxFields 1, needCorral); a finished market
// lifts the field cap.
static void test_petra_small_land_fields() {
	static Simulation s;
	s.set_sim_seed(7);
	s.reset();
	s.set_map_half(HALF);
	std::vector<float> h((size_t)RES * RES);
	for (int j = 0; j < RES; ++j)
		for (int i = 0; i < RES; ++i) {
			const float x = -HALF + 2.0f * HALF * i / (RES - 1), z = -HALF + 2.0f * HALF * j / (RES - 1);
			h[(size_t)j * RES + i] = x * x + z * z < 16.0f * 16.0f ? 3.0f : -3.0f; // an island of about 13000 m²
		}
	s.set_terrain_data(h, RES, 0.0f);
	s.begin_bulk_spawn();
	s.end_bulk_spawn();
	s.set_player_active(0, true, false);
	s.set_player_active(1, true, true);
	s.set_team_civ(1, CIV_ATHENIANS);
	s.set_player_resources(1, 5000, 5000, 5000, 5000);
	s.spawn_building_world(0.0f, 0.0f, BLD_TOWN_CENTER, 1);
	s.spawn_building_world(-8.0f, 0.0f, BLD_FIELD, 1);
	s.spawn_unit(6.0f, 6.0f, 1, UNIT_FEMALE_CITIZEN); // a builder (hasBuilder)
	s.petra_pid_ = 1;
	auto count = [&](uint8_t type) {
		int n = 0;
		for (const auto &b : s.buildings()) n += b.team == 1 && b.type == type && b.hp > 0.0f;
		return n;
	};
	auto manage = [&] {
		for (auto &a : s.petra_queues_.accounts) a = {5000, 5000, 5000, 5000};
		s.petra_class_topo_pid_ = -1; // a new AI turn: the classified topology is looked up again
		s.petra_manage_fields();
	};
	manage();
	manage();
	std::printf("Petra small land: %d fields, %d corrals\n", count(BLD_FIELD), count(BLD_CORRAL));
	CHECK(count(BLD_FIELD) == 1);
	CHECK(count(BLD_CORRAL) == 1);
	s.spawn_building_world(8.0f, 0.0f, BLD_MARKET, 1);
	manage();
	CHECK(count(BLD_FIELD) == 2);
}

// startingStrategy.js configFirstBase: a starting land under 25000 m² with under 800 food in
// stock and in reach needs fish (popForDock 1): its dock goes down at once, not at 16
// population, and its dock and ships queues stay unpaused. navalManager.js wants two fishing
// boats on the seas it touches. All of it is decided once and kept through a save (v88).
static float islet_height(float x, float z) { return x * x + z * z < 16.0f * 16.0f ? 3.0f : -3.0f; }

static void test_petra_need_fish() {
	static Simulation s;
	fresh(s, islet_height);
	s.set_player_active(1, true, true);
	s.set_player_resources(1, 100, 1000, 0, 0);
	s.spawn_building_world(0.0f, 0.0f, BLD_TOWN_CENTER, 1);
	for (int k = 0; k < 4; ++k) s.spawn_unit(-4.0f + 2.0f * (float)k, 8.0f, 1, UNIT_VILLAGER);
	for (int k = 0; k < 3; ++k) s.spawn_resource_world(-20.0f + 20.0f * (float)k, -24.0f, RES_FOOD, FISH_FOOD_SUPPLY, RES_SUB_FISH);
	int docks = 0;
	for (int t = 0; t < 40 && !docks; ++t) {
		run(s, 1.0f);
		for (const auto &b : s.buildings()) docks += b.team == 1 && b.type == BLD_DOCK;
	}
	auto [cx, cz] = s.nav_grid_.world_to_cell(0.0f, -24.0f);
	const uint16_t sea = s.nav_grid_.global_region_at(cx, cz, PASS_SHIP);
	const PetraKept k = s.petra_kept_;
	std::printf("Petra on a food-poor islet: needFish %s, dock %s at population %d, %d fishing boats wanted on its sea\n",
	            k.need_fish ? "yes" : "no", docks ? "placed" : "not placed", s.pop_current(1), PetraKept::at(k.wanted_fish, sea));
	CHECK(k.decided && k.need_fish && !k.need_farm && k.need_corral);
	CHECK(docks == 1 && s.pop_current(1) < 16);
	CHECK(PetraKept::at(k.wanted_fish, sea) == 2);

	const char *path = "/tmp/rtso_test_need_fish.sav";
	CHECK(s.save_state(path));
	static Simulation t;
	fresh(t, islet_height);
	CHECK(t.load_state(path));
	std::remove(path);
	const PetraKept &l = t.petra_kept_;
	CHECK(l.decided && l.need_fish == k.need_fish && l.need_corral == k.need_corral && l.wanted_fish == k.wanted_fish &&
	      l.wanted_transport == k.wanted_transport && l.target_traders == k.target_traders);
}

// worker.js startGathering asks navalManager.requireTransport for a supply near one of our
// bases on another land: a woodcutter on an island with no dropsite of ours sails home, and
// with wood only by the overseas base, north workers cross to cut it there.
static void test_petra_workers_across_water() {
	static Simulation s;
	fresh(s);
	s.set_victory_condition(VICTORY_ENDLESS);
	s.set_player_active(1, true, true);
	s.set_player_resources(1, 0, 0, 0, 0);
	s.players_[1].phase = 2;
	s.spawn_building_world(-30.0f, 40.0f, BLD_TOWN_CENTER, 1);
	s.spawn_building_world(-48.0f, 20.0f, BLD_DOCK, 1);
	s.spawn_unit(-40.0f, 10.0f, 1, UNIT_TRIREME);
	for (int k = 0; k < 8; ++k) s.spawn_resource_world(-44.0f + 3.0f * k, 54.0f, RES_WOOD, 600);
	// A woodcutter stranded on the south land, no dropsite of ours there.
	const uint32_t tree = s.spawn_resource_world(30.0f, -44.0f, RES_WOOD, 600);
	const uint32_t w = s.spawn_unit(30.0f, -40.0f, 1, UNIT_FEMALE_CITIZEN);
	s.order_gather(&w, 1, tree);
	const uint16_t north = s.petra_land_at(-30.0f, 40.0f), south = s.petra_land_at(30.0f, -40.0f);
	bool planned = false;
	float home_at = -1.0f;
	for (int k = 0; k < 240 && home_at < 0.0f; ++k) {
		run(s, 1.0f);
		for (const auto &p : s.petra_transport_plans())
			planned |= p.owner == PETRA_TRANSPORT_WORKERS && std::find(p.passenger_ids.begin(), p.passenger_ids.end(), w) != p.passenger_ids.end();
		const int32_t i = idx(s, w);
		if (i >= 0 && s.petra_land_at(s.units().pos_x[i], s.units().pos_z[i]) == north) home_at = (float)k;
	}
	std::printf("Petra workers across water: stranded woodcutter %s, home at %.0fs\n", planned ? "booked a crossing" : "never booked", home_at);
	CHECK(north && south && north != south);
	CHECK(planned && home_at >= 0.0f);

	// Wood only by the overseas base: north workers cross to it.
	static Simulation t;
	fresh(t);
	t.set_victory_condition(VICTORY_ENDLESS);
	t.set_player_active(1, true, true);
	t.set_player_resources(1, 0, 0, 0, 0);
	t.players_[1].phase = 2;
	t.spawn_building_world(-30.0f, 40.0f, BLD_TOWN_CENTER, 1);
	t.spawn_building_world(30.0f, -40.0f, BLD_TOWN_CENTER, 1);
	t.spawn_building_world(-48.0f, 20.0f, BLD_DOCK, 1);
	t.spawn_unit(-40.0f, 10.0f, 1, UNIT_TRIREME);
	for (int k = 0; k < 6; ++k) t.spawn_unit(-40.0f + 2.0f * k, 52.0f, 1, UNIT_FEMALE_CITIZEN);
	for (int k = 0; k < 8; ++k) t.spawn_resource_world(18.0f + 3.0f * k, -52.0f, RES_WOOD, 600);
	bool crossing = false;
	for (int k = 0; k < 240; ++k) {
		run(t, 1.0f);
		for (const auto &p : t.petra_transport_plans()) crossing |= p.owner == PETRA_TRANSPORT_WORKERS;
	}
	int south_cutters = 0;
	for (size_t i = 0; i < t.units().count; ++i) {
		if (!t.units().alive[i] || t.units().team[i] != 1 || t.units().unit_type[i] != UNIT_FEMALE_CITIZEN) continue;
		if (t.petra_land_at(t.units().pos_x[i], t.units().pos_z[i]) != south) continue;
		auto it = t.res_id_to_idx_.find(t.units().target_resource_id[i]);
		south_cutters += it != t.res_id_to_idx_.end() && t.resources_.type[it->second] == RES_WOOD;
	}
	std::printf("Petra workers across water: crossing %s, %d cutting wood by the south base, wood %d\n",
	            crossing ? "yes" : "no", south_cutters, t.player_resource(1, RES_WOOD));
	CHECK(crossing && south_cutters >= 1 && t.player_resource(1, RES_WOOD) > 0);
}

// queueplanBuilding.js findDockPosition: Petra's dock goes on its own shore of the sea, inside
// or near its territory, where the wood (counted twice), stone and metal within 80 m are thickest.
static void test_petra_dock_position() {
	static Simulation s;
	fresh(s);
	s.set_player_active(1, true, true);
	s.players_[1].phase = 2;
	s.spawn_building_world(0.0f, 36.0f, BLD_TOWN_CENTER, 1);
	for (int k = 0; k < 6; ++k) s.spawn_resource_world(14.0f + 2.0f * k, 22.0f, RES_WOOD, 300);
	s.petra_pid_ = 1;
	s.petra_class_topo_pid_ = -1;
	const uint16_t north = s.petra_land_at(0.0f, 36.0f);
	uint16_t sea = 0;
	{
		auto [cx, cz] = s.nav_grid_.world_to_cell(0.0f, 0.0f);
		sea = s.nav_grid_.global_region_at(cx, cz, PASS_SHIP);
	}
	int cx = 0, cz = 0;
	const bool found = s.petra_find_dock_position(north, sea, cx, cz);
	const int foot = building_type_info(BLD_DOCK).footprint;
	const float x = (cx + 0.5f * foot) * Simulation::GRID, z = (cz + 0.5f * foot) * Simulation::GRID;
	std::printf("Petra dock position: %s at (%.0f, %.0f)\n", found ? "found" : "none", x, z);
	CHECK(found);
	CHECK(z > 10.0f && z < 24.0f);  // the north shore
	CHECK(x > 8.0f && x < 30.0f);   // by the wood, not straight below the civic centre
	// template_gaia_fish BlockFoundation false: a shoal on the site doesn't stop the dock, and
	// Foundation.js Commit deletes it (DeleteUponConstruction).
	const uint32_t shoal = s.spawn_resource_world(x, z, RES_FOOD, FISH_FOOD_SUPPLY, RES_SUB_FISH);
	CHECK(s.check_placement(1, cx, cz, BLD_DOCK) == Simulation::PLACE_OK);
	const uint32_t bid = found ? s.spawn_building(cx, cz, BLD_DOCK, 1, 0.0f) : 0;
	const Simulation::Building *b = bid ? bld(s, bid) : nullptr;
	CHECK(b && s.dock_sea(*b) == sea);
	if (b) {
		Simulation::Building &mb = s.buildings_[s.building_id_to_idx_.at(bid)];
		mb.committed = false;
		CHECK(s.try_commit_foundation(mb));
		bool left = false;
		for (size_t r = 0; r < s.resources_.count; ++r) left = left || (s.resources_.id[r] == shoal && s.resources_.alive[r]);
		std::printf("Petra dock position: the shoal on the site is %s once building starts\n", left ? "left" : "gone");
		CHECK(!left);
	}
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
	run(s, 0.5f);
	const float burnt = f0 - hp(fire);
	std::printf("fire ship: %.1f hp burnt in 0.5 s\n", burnt);
	CHECK(burnt > 90.0f && burnt < 110.0f);
	s.set_unit_hp(fire, 1.0f);
	run(s, 1.0f);
	CHECK(!s.units().alive[idx(s, fire)]);
	std::printf("fire ship blast: enemy %.0f -> %.0f, own %.0f -> %.0f, far %.0f -> %.0f\n",
			e0, hp(enemy), o0, hp(own), x0, hp(far_enemy));
	CHECK(hp(enemy) < e0 - 100.0f);       // close: most of 500
	CHECK(hp(own) == o0);                 // no FriendlyFire
	CHECK(hp(far_enemy) == x0);           // 16 units away is outside 22 m (x0.6)

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

	// What the blast kills is a kill like any other (Health.js KilledBy with the fire ship as the
	// attacker): its owner takes the loot, and a structure it razes leaves the territory grid.
	const uint32_t fire2 = s.spawn_unit(30.0f, 12.0f, 0, UNIT_TRIREME, 0.0f, tmpl_of("brit/ship_fire"));
	CHECK(s.cmd_upgrade_units(0, &fire2, 1, tmpl_of("brit/ship_fire_fire")));
	run(s, 0.1f);
	const uint32_t prey = s.spawn_unit(33.0f, 12.0f, 1, UNIT_TRIREME);
	const uint32_t dock = s.spawn_building_world(30.0f, 17.0f, BLD_DOCK, 1, 1.0f);
	uint32_t pair[2] = {fire2, prey};
	s.order_set_stance(pair, 2, STANCE_PASSIVE);
	s.set_unit_hp(prey, 1.0f);
	s.buildings_[s.building_id_to_idx_.at(dock)].hp = 1.0f;
	const float wood0 = s.player(0).res[RES_WOOD];
	s.set_unit_hp(fire2, 1.0f);
	run(s, 1.0f);
	CHECK(!s.units().alive[idx(s, prey)]);
	CHECK(s.buildings_[s.building_id_to_idx_.at(dock)].wrecked);
	std::printf("fire ship blast loot: wood %.0f -> %.0f\n", wood0, s.player(0).res[RES_WOOD]);
	CHECK(s.player(0).res[RES_WOOD] > wood0);
	CHECK(s.territory_owner_at(30.0f, 17.0f) != 1);
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
// transportPlan.js:82: a crossing asked for with no free ship is planned all the same
// (needTransportShips) and takes the first transport ship that comes.
static void test_petra_crossing_waits_for_ship() {
	static Simulation s;
	fresh(s);
	s.set_player_active(1, true, true);
	s.petra_pid_ = 1;
	const uint16_t north = s.petra_land_at(-30.0f, 40.0f), south = s.petra_land_at(-30.0f, -40.0f);
	const uint16_t sea = s.petra_topo().sea_between(north, south);
	std::vector<uint32_t> crew;
	for (int k = 0; k < 3; ++k) crew.push_back(s.spawn_unit(-34.0f + 2.0f * k, 44.0f, 1, UNIT_VILLAGER));
	const bool planned = s.petra_start_transport(crew, -30.0f, 44.0f, -30.0f, -40.0f, sea, PETRA_TRANSPORT_BUILDERS);
	const uint32_t first = s.petra_transport_plans().empty() ? 1u : s.petra_transport_plans().back().ship_id;
	const uint32_t ship = s.spawn_unit(-30.0f, 4.0f, 1, UNIT_TRIREME);
	s.petra_manage_transport(Simulation::FIXED_STEP);
	const uint32_t later = s.petra_transport_plans().empty() ? 0u : s.petra_transport_plans().back().ship_id;
	std::printf("crossing with no ship: sea %u, planned %s, ship %u, then %u (trireme %u)\n", sea, planned ? "yes" : "no", first, later, ship);
	CHECK(sea && planned && first == 0 && later == ship);
	s.petra_transports_.clear();
}

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
		// 0.28 popPhase2 is 100 at a 200 population limit.
		s.set_pop_cap(200);
		for (int k = 0; k < 120; ++k) s.spawn_unit(-40.0f + (float)(k % 15) * 2.0f, 50.0f + (float)(k / 15) * 2.0f, 1, UNIT_VILLAGER);
		// Phasing to the city phase, the temple and the forge go down first (checkPhaseRequirements
		// counts Town foundations), and a dock at 7 s (no longer held to a 10 s search), so the Cothon follows some
		// 16.5 s in.
		run(s, 20.0f);
		const Simulation::Building *cothon = nullptr;
		for (const auto &b : s.buildings()) if (b.team == 1 && b.type == BLD_SUPER_DOCK) cothon = &b;
		const Simulation::Building *db = bld(s, dock);
		std::printf("Petra Cothon (%s): %s\n", civ_info(civ).name, cothon ? "laid out" : "none");
		if (civ == CIV_CARTHAGINIANS) CHECK(cothon && db && s.dock_sea(*cothon) == s.dock_sea(*db));
		else CHECK(cothon == nullptr);
	}
}

// A worker on the far shore carries its load only to a dropsite on its own land: with the only
// storehouse across the sea she finds none and RETURNINGRESOURCE finishes the order (she stays by
// her tree, idle and still carrying, free for a new task); sent to gather again once one stands
// on her side, she drops off there.
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
	CHECK(s.units().action_state[idx(s, woman)] != ACTION_RETURN_DROPSITE);
	s.spawn_building_world(0.0f, -40.0f, BLD_STOREHOUSE, 0, 1.0f);
	s.order_gather(&woman, 1, tree);
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

// Land north of z 24 and a headland down the middle (|x| < 6) to z -36: the sea south of it joins
// its two sides.
static float headland_at(float x, float z) {
	return z > 24.0f || (std::fabs(x) < 6.0f && z > -36.0f) ? 3.0f : -3.0f;
}

// Ships sail round land on every task, as 0 A.D. UnitMotion paths every move on the ship's
// passability class: a fishing boat to its fish, a merchant ship between docks and a warship to
// its target, each across the headland, never stepping ashore.
static void test_ships_sail_round_land() {
	static Simulation s;
	fresh(s, headland_at);
	s.set_victory_condition(VICTORY_ENDLESS);
	s.set_reveal_map(true); // the warship's target stays in sight
	const uint32_t west = s.spawn_building_world(-30.0f, 28.0f, BLD_DOCK, 0);
	const uint32_t east = s.spawn_building_world(30.0f, 28.0f, BLD_DOCK, 0);
	const uint32_t fish = s.spawn_resource_world(24.0f, 0.0f, RES_FOOD, FISH_FOOD_SUPPLY, RES_SUB_FISH);
	const uint32_t boat = s.spawn_unit(-24.0f, 0.0f, 0, UNIT_FISHING_BOAT);
	const uint32_t trader = s.spawn_unit(-30.0f, 16.0f, 0, UNIT_MERCHANT_SHIP);
	const uint32_t tri = s.spawn_unit(-40.0f, -8.0f, 0, UNIT_TRIREME);
	const uint32_t prey = s.spawn_unit(44.0f, -8.0f, 1, UNIT_FISHING_BOAT);
	s.order_gather(&boat, 1, fish);
	s.order_trade(&trader, 1, west, east);
	s.order_attack(&tri, 1, prey);
	const uint32_t ships[3] = {boat, trader, tri};
	int ashore[3] = {0, 0, 0}, east_side[3] = {0, 0, 0};
	for (int t = 0; t < (int)(150.0f / Simulation::FIXED_STEP); ++t) {
		s.tick(Simulation::FIXED_STEP);
		for (int k = 0; k < 3; ++k) {
			const int32_t i = s.lookup_unit(ships[k]);
			if (i < 0 || !s.units().alive[i]) continue;
			auto [cx, cy] = s.nav_grid().world_to_cell(s.units().pos_x[i], s.units().pos_z[i]);
			ashore[k] += !s.nav_grid().is_passable_for(cx, cy, PASS_SHIP);
			east_side[k] |= s.units().pos_x[i] > 8.0f;
		}
	}
	const int32_t pi = s.lookup_unit(prey);
	const bool prey_hit = pi < 0 || !s.units().alive[pi] || s.units().hp[pi] < s.units().max_hp[pi];
	std::printf("ships round the headland: ticks ashore boat %d, merchant %d, warship %d; food %d, trade %d, prey %s\n",
			ashore[0], ashore[1], ashore[2], s.player_resource(0, RES_FOOD), total(s, 0) - s.player_resource(0, RES_FOOD),
			prey_hit ? "hit" : "untouched");
	CHECK(ashore[0] == 0 && ashore[1] == 0 && ashore[2] == 0);
	CHECK(east_side[0] && east_side[1] && east_side[2]);
	CHECK(s.player_resource(0, RES_FOOD) > 0); // the boat landed fish at a dock
	CHECK(total(s, 0) > s.player_resource(0, RES_FOOD)); // the merchant ship was paid at a dock
	CHECK(prey_hit);
}

// pathfinder.xml: warships ("ship", Clearance 10 m) keep a navcell of water between them and the
// shore on their way round the headland (grid_nav.h PASS_WARSHIP), where a fishing boat
// ("ship-small", 3 m) may sail beside it. Through a channel two navcells wide, which the clearance
// closes, a warship still gets through on the plain ship path.
static float channel_at(float x, float z) {
	return std::fabs(x) < 20.0f && std::fabs(z) > 2.5f ? 3.0f : -3.0f;
}

static void test_warships_keep_off_shore() {
	static Simulation s;
	fresh(s, headland_at);
	const uint32_t tri = s.spawn_unit(-40.0f, -8.0f, 0, UNIT_TRIREME);
	const uint32_t boat = s.spawn_unit(-40.0f, -12.0f, 0, UNIT_FISHING_BOAT);
	s.cmd_move(&tri, 1, 40.0f, -8.0f);
	s.cmd_move(&boat, 1, 40.0f, -12.0f);
	const uint32_t ships[2] = {tri, boat};
	int by_shore[2] = {0, 0}, ashore[2] = {0, 0};
	bool there[2] = {false, false};
	for (int t = 0; t < (int)(90.0f / Simulation::FIXED_STEP); ++t) {
		s.tick(Simulation::FIXED_STEP);
		for (int k = 0; k < 2; ++k) {
			const int32_t i = s.lookup_unit(ships[k]);
			if (i < 0) continue;
			auto [cx, cy] = s.nav_grid().world_to_cell(s.units().pos_x[i], s.units().pos_z[i]);
			ashore[k] += !s.nav_grid().is_passable_for(cx, cy, PASS_SHIP);
			// A navcell beside land: one of its eight neighbours isn't ship water.
			bool beside = false;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx) beside |= !s.nav_grid().is_passable_for(cx + dx, cy + dy, PASS_SHIP);
			by_shore[k] += beside;
			there[k] |= s.units().pos_x[i] > 36.0f;
		}
	}
	std::printf("warships keep off the shore: ticks beside the shore warship %d, fishing boat %d; ashore %d, %d; both round: %s\n",
			by_shore[0], by_shore[1], ashore[0], ashore[1], there[0] && there[1] ? "yes" : "no");
	CHECK(by_shore[0] == 0 && ashore[0] == 0 && ashore[1] == 0);
	CHECK(there[0] && there[1]);

	fresh(s, channel_at);
	const uint32_t tri2 = s.spawn_unit(-40.0f, 0.0f, 0, UNIT_TRIREME);
	s.cmd_move(&tri2, 1, 40.0f, 0.0f);
	bool through = false;
	int aground = 0;
	for (int t = 0; t < (int)(90.0f / Simulation::FIXED_STEP) && !through; ++t) {
		s.tick(Simulation::FIXED_STEP);
		const int32_t i = s.lookup_unit(tri2);
		if (i < 0) break;
		auto [cx, cy] = s.nav_grid().world_to_cell(s.units().pos_x[i], s.units().pos_z[i]);
		aground += !s.nav_grid().is_passable_for(cx, cy, PASS_SHIP);
		through = s.units().pos_x[i] > 30.0f;
	}
	std::printf("warship through a narrow channel: %s, ticks aground %d\n", through ? "through" : "stuck", aground);
	CHECK(through && aground == 0);
}

// template_unit_ship_fishing.xml Harpoon and UnitAI GATHER: a fishing boat ordered onto a
// whale harpoons it, gathers the carcass (food.fish) to a dock, and when the carcass is
// eaten goes on to the next whale (FindNearbyResource takes live whales too).
static void test_whale_hunt() {
	static Simulation s;
	fresh(s);
	s.spawn_building_world(-48.0f, 20.0f, BLD_DOCK, 0);
	const uint32_t w1 = s.spawn_fauna(-30.0f, 4.0f, FAUNA_WHALE_FIN);
	const uint32_t w2 = s.spawn_fauna(-14.0f, 4.0f, FAUNA_WHALE_FIN);
	const uint32_t boat = s.spawn_unit(-40.0f, 8.0f, 0, UNIT_FISHING_BOAT);
	auto f_alive = [&](uint32_t id) {
		auto it = s.fauna_id_to_idx_.find(id);
		return it != s.fauna_id_to_idx_.end() && s.fauna_.alive[it->second];
	};
	auto stop_roaming = [&](uint32_t id) { // keep the second whale where the test put it
		auto it = s.fauna_id_to_idx_.find(id);
		if (it != s.fauna_id_to_idx_.end()) s.fauna_.state_timer[it->second] = 1e6f;
	};
	stop_roaming(w1);
	stop_roaming(w2);
	s.order_hunt(&boat, 1, w1);
	float t = 0.0f;
	while (f_alive(w1) && t < 120.0f) { run(s, 1.0f); t += 1.0f; stop_roaming(w2); }
	CHECK(!f_alive(w1));
	run(s, 3.0f);
	const uint32_t carcass = gather_target(s, boat);
	int32_t left = 0;
	const uint8_t *subp = carcass ? subtype_of(s, carcass, &left) : nullptr;
	const int sub = subp ? *subp : -1;
	std::printf("whale hunt: whale killed in %.0f s, boat gathering carcass %u (fish %d, %d left)\n", t, carcass, sub == RES_SUB_FISH, left);
	CHECK(carcass != 0 && sub == RES_SUB_FISH);
	run(s, 60.0f);
	stop_roaming(w2);
	const int food = s.player_resource(0, RES_FOOD);
	CHECK(food > 0);
	// The carcass runs out: the boat harpoons the next whale, within FindNearbyResource's
	// range of where it was gathering (the first whale fled before it died).
	if (carcass) {
		auto it = s.res_id_to_idx_.find(carcass);
		auto w = s.fauna_id_to_idx_.find(w2);
		if (it != s.res_id_to_idx_.end() && w != s.fauna_id_to_idx_.end()) {
			s.resources_.amount[it->second] = 1;
			const float cx = s.resources_.x[it->second], cz = s.resources_.z[it->second];
			s.fauna_.pos_x[w->second] = cx + (cx > 0.0f ? -6.0f : 6.0f); // in the boat's sight (UnitAI CheckTargetVisible)
			s.fauna_.pos_z[w->second] = cz;
		}
	}
	bool on_w2 = false;
	for (int k = 0; k < 90 && !on_w2; ++k) { // the boat walks back from the dock first
		run(s, 1.0f);
		const int32_t b = s.lookup_unit(boat);
		on_w2 = b >= 0 && (s.units_.target_unit_id[b] == w2 || !f_alive(w2));
	}
	std::printf("whale hunt: %d food landed, then on to the next whale: %d\n", food, on_w2);
	CHECK(on_w2);
}

// UnitAI GATHER APPROACHING: a walk that fails (MoveFailed, likelyFailure) goes to FINDINGNEWTARGET,
// which takes the nearest resource of the kind other than the one the unit couldn't reach. A woman
// sent to berries across the sea stops at the shore, then gathers the berries on her own side.
static void test_gather_unreachable() {
	static Simulation s;
	fresh(s);
	s.spawn_building_world(-40.0f, 40.0f, BLD_TOWN_CENTER, 0);
	const uint32_t far_bush = s.spawn_resource_world(0.0f, -26.0f, RES_FOOD, 200);
	const uint32_t near_bush = s.spawn_resource_world(8.0f, 28.0f, RES_FOOD, 200);
	const uint32_t woman = s.spawn_unit(0.0f, 30.0f, 0, UNIT_FEMALE_CITIZEN);
	s.order_gather(&woman, 1, far_bush, false, false);
	run(s, 30.0f);
	const int32_t i = idx(s, woman);
	std::printf("  gather across the sea: now on the %s bush, carrying %d\n",
			gather_target(s, woman) == near_bush ? "near" : gather_target(s, woman) == far_bush ? "far" : "no", s.units().carry[i]);
	CHECK(gather_target(s, woman) == near_bush && s.units().carry[i] > 0);
}

// UnitMotion Acceleration (CCmpUnitMotion PerformMove): a fishing boat gains speed at 3.85 m/s^2
// (template_unit_ship 35 x 0.1, x1.1), so it takes 2.6 s to reach full speed. Ships never stop to turn
// (InstantTurnAngle 10): one sent back the way it came keeps cos(180 degrees) of its speed, -1, and
// drifts on for v^2 / 2a before it comes back.
static void test_ships_accelerate() {
	static Simulation s;
	fresh(s);
	uint32_t boat = s.spawn_unit(-40.0f, 0.0f, 0, UNIT_FISHING_BOAT);
	const int32_t i = idx(s, boat);
	const float a = unit_motion(s.units().tmpl[i], UNIT_FISHING_BOAT).acceleration, v = s.unit_move_speed(i);
	s.cmd_move(&boat, 1, 40.0f, 0.0f);
	run(s, 1.0f);
	const float x1 = s.units().pos_x[i] + 40.0f;
	run(s, 2.0f);
	const float v3 = std::hypot(s.units().vel_x[i], s.units().vel_z[i]);
	const float x_back = s.units().pos_x[i];
	s.cmd_move(&boat, 1, -40.0f, 0.0f);
	float furthest = x_back;
	for (int t = 0; t < 160; ++t) {
		s.tick(Simulation::FIXED_STEP);
		furthest = std::max(furthest, s.units().pos_x[i]);
	}
	const float x_end = s.units().pos_x[i];
	std::printf("  fishing boat: %.3f units/s^2, %.3f after 1 s (%.3f expected), %.3f of %.3f at 3 s; sent back, it"
	            " drifted on %.2f (%.2f expected) and is at %.2f after 8 s\n",
	            a, x1, 0.5f * a, v3, v, furthest - x_back, v * v / (2.0f * a), x_end);
	CHECK(std::fabs(a - 35.0f * 0.1f * 1.1f * 0.356f) < 0.01f && std::fabs(x1 - 0.5f * a) < 0.02f && std::fabs(v3 - v) < 1e-3f);
	CHECK(std::fabs(furthest - x_back - v * v / (2.0f * a)) < 0.2f && std::fabs(x_end + 40.0f) < 1.0f);
}

// UnitAI Order.Garrison on a ship: an unpacked siege engine packs first; a unit with no room
// walks over and stops beside the ship; an ally's unit walking to board ignores enemies.
static void test_board_rules() {
	std::printf("board rules\n");
	static Simulation s;
	fresh(s);
	const uint32_t arrow = s.spawn_unit(-20.0f, 10.0f, 0, UNIT_TRIREME, 0.0f, tmpl_of("athen/ship_arrow"));
	const uint32_t cat = s.spawn_unit(-20.0f, 17.5f, 0, UNIT_CATAPULT);
	s.units_.pack_state[idx(s, cat)] = PACK_UNPACKED;
	s.order_board_transport(&cat, 1, arrow);
	CHECK(!aboard(s, arrow, cat));
	run(s, 0.4f);
	CHECK(s.units().pack_state[idx(s, cat)] == PACK_PACKING);
	run(s, 20.0f);
	CHECK(aboard(s, arrow, cat));

	const uint32_t scout = s.spawn_unit(20.0f, 10.0f, 0, UNIT_TRIREME, 0.0f, tmpl_of("athen/ship_scout"));
	std::vector<uint32_t> crew;
	for (int k = 0; k < 10; ++k) crew.push_back(s.spawn_unit(18.0f + 0.4f * k, 17.5f, 0, UNIT_SPEARMAN));
	const uint32_t extra = s.spawn_unit(32.0f, 18.0f, 0, UNIT_SPEARMAN);
	crew.push_back(extra);
	s.order_board_transport(crew.data(), crew.size(), scout);
	const float d0 = std::fabs(s.units().pos_x[idx(s, extra)] - 20.0f);
	CHECK(s.units().action_state[idx(s, extra)] == ACTION_MOVE);
	run(s, 12.0f);
	const int32_t ie = idx(s, extra);
	CHECK(s.get_transport_passenger_count(scout) == 10);
	CHECK(s.units().alive[ie] && !aboard(s, scout, extra) && s.units().action_state[ie] == ACTION_IDLE);
	CHECK(std::fabs(s.units().pos_x[ie] - 20.0f) < d0 - 4.0f);

	s.set_mutual_ally(0, 2);
	const uint32_t ship2 = s.spawn_unit(-50.0f, 10.0f, 0, UNIT_TRIREME, 0.0f, tmpl_of("athen/ship_arrow"));
	const uint32_t friend_ = s.spawn_unit(-62.0f, 18.0f, 2, UNIT_SPEARMAN);
	const uint32_t foe = s.spawn_unit(-60.0f, 19.5f, 1, UNIT_SPEARMAN);
	s.order_set_stance(&foe, 1, STANCE_PASSIVE);
	s.order_board_transport(&friend_, 1, ship2);
	// The walk to board is forced: the ally never turns on the enemy beside its path
	// (the ship's own arrows may, once it is aboard).
	bool fought = false;
	for (int k = 0; k < (int)(10.0f / Simulation::FIXED_STEP); ++k) {
		s.tick(Simulation::FIXED_STEP);
		const int32_t fi = idx(s, friend_);
		fought |= fi >= 0 && s.units().action_state[fi] == ACTION_ATTACK;
	}
	CHECK(aboard(s, ship2, friend_));
	CHECK(!fought);
}

int main() {
	test_ships_accelerate();
	test_transport();
	test_dropsite_own_land();
	test_dock_takes_all();
	test_no_wading();
	test_spawn_clearance();
	test_siege_unloads_into_shallows();
	test_turret_leave_spot();
	test_line_clips_water();
	test_gather_around_lake();
	test_dock_trade();
	test_fishing();
	test_petra_sea_trade();
	test_petra_transport_switches_to_trade();
	test_petra_trader_transport();
	test_ship_height();
	test_ship_leaves_shore_house();
	test_ships_sail_round_land();
	test_warships_keep_off_shore();
	test_petra_navy();
	test_petra_overseas_attack();
	test_petra_attack_from_far_base();
	test_petra_landing_spot();
	test_petra_unreachable_target();
	test_petra_overseas_base();
	test_petra_oversea_dock();
	test_petra_start_at_sea();
	test_petra_concurrent_transports();
	test_petra_defenders_ferried();
	test_petra_transport_ship_lost();
	test_petra_transport_sunk_at_sea();
	test_petra_transport_wrong_land();
	test_petra_sunk_survivors_two_lands();
	test_petra_laden_ship_lands();
	test_petra_merchantman_transport();
	test_petra_cothon();
	test_petra_crossing_waits_for_ship();
	test_ship_treasure();
	test_ship_round_wreck();
	test_petra_ship_treasure();
	test_crannog_takes_fish();
	test_next_fish_range();
	test_petra_land_skips_wreck();
	test_petra_fishing();
	test_petra_fishing_boat_backs_off();
	test_petra_small_land_fields();
	test_petra_need_fish();
	test_petra_dock_position();
	test_petra_workers_across_water();
	test_fire_ship();
	test_whale_hunt();
	test_gather_unreachable();
	test_board_rules();
	std::printf(failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", failures);
	return failures ? 1 : 0;
}
