// Tribute, barter and trade tests (0 A.D. Player.js TributeResource, Barter.js,
// Market.js CalculateTraderGain, Trader.js, globalscripts/Trade.js).
//   g++ -std=c++20 -O2 -Isrc tools/test_trade.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//       src/network/net_host.cpp -lpthread -o /tmp/test_trade && /tmp/test_trade
#define private public // complete_research
#include "simulation/simulation.h"
#undef private
#include "network/turn_manager.h"
#include <cmath>
#include <cstdio>
#include <vector>

using namespace rtso;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

// Four human slots, so no Petra spends the stocks under test.
static void fresh(Simulation &s) {
	s.set_sim_seed(11);
	s.reset();
	for (uint8_t p = 0; p < 4; ++p) {
		s.set_player_active(p, true, false);
		s.set_player_resources(p, 1000, 1000, 1000, 1000);
	}
	s.set_map_half(128.0f); // a 1024 m map, 0 A.D.'s "normal" size
}

static void run(Simulation &s, float seconds) {
	for (int t = 0; t < (int)(seconds / Simulation::FIXED_STEP); ++t) s.tick(Simulation::FIXED_STEP);
}

static int total(const Simulation &s, uint8_t p) {
	int t = 0;
	for (int r = 0; r < RES_COUNT; ++r) t += s.player_resource(p, (uint8_t)r);
	return t;
}

// globalscripts/Trade.js, written out independently of the sim.
static double expected_gain(double metres, double map_size, double mult) {
	auto curve = [&](double d2) { return d2 / (1.0 + 0.25 * std::sqrt(d2) / map_size); };
	const double norm = std::sqrt(1024.0 / map_size) / curve(10000.0);
	return norm * mult * curve(metres * metres);
}

static void test_tribute() {
	static Simulation s;
	fresh(s);
	// Any player to any player, several resources at once.
	const int amounts[RES_COUNT] = {100, 0, 50, 25}; // wood, gold, stone, food
	CHECK(s.cmd_tribute(2, 3, amounts));
	CHECK(s.player_resource(2, RES_WOOD) == 900 && s.player_resource(2, RES_STONE) == 950 && s.player_resource(2, RES_FOOD) == 975);
	CHECK(s.player_resource(3, RES_WOOD) == 1100 && s.player_resource(3, RES_STONE) == 1050 && s.player_resource(3, RES_FOOD) == 1025);
	// All or nothing: one resource short refuses the whole tribute.
	const int too_much[RES_COUNT] = {10, 5000, 0, 0};
	s.take_session_notices();
	CHECK(!s.cmd_tribute(2, 3, too_much));
	CHECK(s.player_resource(2, RES_WOOD) == 900 && s.player_resource(2, RES_GOLD) == 1000);
	{
		// SubtractResourcesOrNotify: the sender hears "Insufficient resources - 4000 metal".
		const auto notes = s.take_session_notices();
		CHECK(notes.size() == 1 && notes[0].kind == Simulation::SessionNotice::TEXT &&
		      notes[0].tech == Simulation::SessionNotice::TEXT_INSUFFICIENT_RESOURCES && notes[0].player == 2 &&
		      notes[0].amounts[RES_GOLD] == 4000 && notes[0].amounts[RES_WOOD] == 0);
	}
	const int negative[RES_COUNT] = {10, -5, 0, 0};
	CHECK(!s.cmd_tribute(2, 3, negative));
	const int nothing[RES_COUNT] = {};
	CHECK(!s.cmd_tribute(2, 3, nothing));
	CHECK(!s.cmd_tribute(2, 2, RES_FOOD, 10));
	CHECK(!s.cmd_tribute(2, 8, RES_FOOD, 10));
	CHECK(!s.cmd_tribute(2, 3, RES_COUNT, 10));
	// Both players must be in the match: slot 5 is not seated.
	CHECK(!s.cmd_tribute(2, 5, RES_FOOD, 10));
	CHECK(!s.cmd_tribute(5, 2, RES_FOOD, 10));
	// Player 0's wrapper and Petra's wrapper go through the same command.
	CHECK(s.send_tribute(1, RES_GOLD, 100));
	CHECK(s.gold() == 900 && s.player_resource(1, RES_GOLD) == 1100);
	s.set_player_active(1, true, true);
	CHECK(s.ai_send_tribute(RES_GOLD, 100)); // Petra player 1 -> player 0
	CHECK(s.gold() == 1000 && s.player_resource(1, RES_GOLD) == 1000);
}

static bool near(float a, float b) { return std::fabs(a - b) < 1e-3f; }

static void test_barter() {
	static Simulation s;
	fresh(s);
	// No market, no barter (Player.js CanBarter).
	CHECK(!s.can_barter(0));
	CHECK(!s.cmd_barter(0, RES_WOOD, RES_STONE, 100));
	const uint32_t site = s.spawn_building_world(0.0f, 0.0f, BLD_MARKET, 0, 0.5f);
	(void)site;
	CHECK(!s.can_barter(0)); // a foundation does not count
	s.spawn_building_world(20.0f, 0.0f, BLD_MARKET, 0, 1.0f);
	CHECK(s.can_barter(0) && !s.can_barter(1));

	// Fresh prices: sell at 90, buy at 110, so 100 wood buys round(90/110*100) = 82 stone.
	CHECK(s.barter_quote(RES_WOOD, RES_STONE, 100) == 82);
	CHECK(s.cmd_barter(0, RES_WOOD, RES_STONE, 100));
	CHECK(s.wood() == 900 && s.stone() == 1082);
	// Barter.js (0.28): the prices move 2% per 100 received, here 82 stone.
	CHECK(near(s.barter_price_diff(RES_WOOD), -1.64f) && near(s.barter_price_diff(RES_STONE), 1.64f));
	// Only 100 or 500 (DEAL_AMOUNT, BATCH_SIZE deals); the same pair is now dearer.
	CHECK(!s.cmd_barter(0, RES_WOOD, RES_STONE, 200));
	CHECK(!s.cmd_barter(0, RES_WOOD, RES_WOOD, 100));
	const int bought = (int)std::lround(88.36 / 111.64 * 500.0); // 396
	CHECK(s.barter_quote(RES_WOOD, RES_STONE, 500) == bought);
	CHECK(s.cmd_barter(0, RES_WOOD, RES_STONE, 500));
	const float moved = 1.64f + 2.0f * (float)bought / 100.0f; // 9.56
	CHECK(near(s.barter_price_diff(RES_WOOD), -moved) && near(s.barter_price_diff(RES_STONE), moved));
	// Prices are in the lockstep hash and survive save/load (checked before any
	// tick: other per-tick state does not round-trip yet).
	const char *path = "/tmp/rts0_test_trade.sav";
	CHECK(s.save_state(path));
	static Simulation l;
	fresh(l);
	CHECK(l.load_state(path));
	for (uint8_t r = 0; r < RES_COUNT; ++r) CHECK(l.barter_price_diff(r) == s.barter_price_diff(r));
	TurnManager tm;
	CHECK(tm.compute_sim_hash(s) == tm.compute_sim_hash(l));
	CHECK(l.cmd_barter(0, RES_FOOD, RES_GOLD, 100)); // the loaded copy diverges
	CHECK(tm.compute_sim_hash(s) != tm.compute_sim_hash(l));

	// The market is shared: player 1 now sees the moved prices once it has a market.
	s.spawn_building_world(-40.0f, 30.0f, BLD_MARKET, 1, 1.0f);
	CHECK(s.barter_quote(RES_STONE, RES_FOOD, 100) == (int)std::lround((90.0 + moved) / 110.0 * 100.0));
	// Can't sell what you don't have.
	s.set_player_resources(1, 1000, 50, 1000, 1000);
	CHECK(!s.cmd_barter(1, RES_WOOD, RES_FOOD, 100));
	CHECK(s.player_resource(1, RES_WOOD) == 50);

	// Prices drift back 0.5% every 5 s (Barter.js ProgressTimeout).
	run(s, 5.05f);
	CHECK(near(s.barter_price_diff(RES_WOOD), -moved + 0.5f) && near(s.barter_price_diff(RES_STONE), moved - 0.5f));
	run(s, 5.0f);
	CHECK(near(s.barter_price_diff(RES_WOOD), -moved + 1.0f));

	// The overall difference never passes +-99%: the dynamic part clamps at 89.
	// Each deal buys less as the prices part, so the steps shrink on the way.
	s.set_player_resources(0, 100000, 100000, 100000, 100000);
	for (int i = 0; i < 100; ++i) CHECK(s.cmd_barter(0, RES_FOOD, RES_GOLD, 500));
	CHECK(s.barter_price_diff(RES_FOOD) == -89.0f && s.barter_price_diff(RES_GOLD) == 89.0f);
	CHECK(s.market_sell_price(RES_FOOD) > 0.0f && s.market_buy_price(RES_GOLD) < 2.0f);

}

static void test_trade_gain() {
	static Simulation s;
	fresh(s);
	Simulation::Building a{}, b{};
	a.type = b.type = BLD_MARKET;
	a.team = b.team = 0;
	// 100 m on a 1024 m map is the normalisation point: 1 x GainMultiplier 0.75 -> 1.
	b.x = 25.0f;
	CHECK(s.calculate_trade_gain(a, b, 0).trader == 1);
	// 800 m: the gain grows with the square of the distance.
	b.x = 200.0f;
	const int long_route = (int)std::lround(expected_gain(800.0, 1024.0, 0.75));
	CHECK(long_route == 41);
	Simulation::TradeGain g = s.calculate_trade_gain(a, b, 0);
	CHECK(g.trader == long_route && g.market1 == 0 && g.market2 == 0); // one owner: no bonus
	CHECK(s.calculate_trader_gain(a, b, 0) == long_route);
	// Persian Darics: +25% trader gain.
	s.set_team_civ(0, CIV_PERSIANS);
	CHECK(s.calculate_trade_gain(a, b, 0).trader == (int)std::lround(expected_gain(800.0, 1024.0, 0.75 * 1.25)));
	// Carthaginian Commercial Acumen is on merchant ships only (units/cart/ship_merchant).
	s.set_team_civ(0, CIV_CARTHAGINIANS);
	CHECK(s.calculate_trade_gain(a, b, 0).trader == long_route);
	CHECK(s.calculate_trade_gain(a, b, 0, UNIT_MERCHANT_SHIP).trader == (int)std::lround(expected_gain(800.0, 1024.0, 0.75 * 1.25)));
	s.set_team_civ(0, CIV_ATHENIANS);
	// A smaller map pays more for the same distance.
	s.set_map_half(64.0f);
	CHECK(s.calculate_trade_gain(a, b, 0).trader == (int)std::lround(expected_gain(800.0, 512.0, 0.75)));
	s.set_map_half(128.0f);

	// International trade: each market's owner gets 20% of the trader's gain.
	b.team = 1;
	g = s.calculate_trade_gain(a, b, 0);
	CHECK(g.trader == long_route);
	CHECK(g.market1 == (int)std::lround(long_route * 0.2) && g.market2 == g.market1);
	// Han "Silk Road": the Han player's allies' markets get +20% on that bonus.
	s.set_team_civ(2, CIV_HAN);
	s.set_player_team(1, 7);
	s.set_player_team(2, 7);                     // players 1 and 2 are teammates
	s.spawn_unit(60.0f, 60.0f, 2, UNIT_VILLAGER); // player 2 is still in the match
	g = s.calculate_trade_gain(a, b, 0);
	CHECK(g.market1 == (int)std::lround(long_route * 0.2));        // player 0 has no Han ally
	CHECK(g.market2 == (int)std::lround(long_route * 0.2 * 1.2)); // player 1 does
	// Commercial Treaty: +0.1 international bonus on the researcher's markets.
	s.complete_research(0, TECH_TRADE_COMMERCIAL_TREATY);
	g = s.calculate_trade_gain(a, b, 0);
	CHECK(g.market1 == (int)std::lround(long_route * 0.3));
	// Carthage's markets and docks carry 0.3 (Desirable Products); Han's Silk Road
	// technology multiplies its own markets' bonus by 1.2 (after Advanced Handicraft).
	s.set_team_civ(0, CIV_CARTHAGINIANS);
	g = s.calculate_trade_gain(a, b, 0);
	CHECK(g.market1 == (int)std::lround(long_route * 0.4));
	CHECK(tech_info(TECH_SILK_ROAD).prerequisite == TECH_TRADE_GAIN_02 && tech_info(TECH_SILK_ROAD).civ_mask == (1u << CIV_HAN));
	s.set_team_civ(0, CIV_HAN);
	s.complete_research(0, TECH_SILK_ROAD);
	g = s.calculate_trade_gain(a, b, 0);
	CHECK(g.market1 == (int)std::lround(long_route * (0.2 * 1.2 + 0.1))); // Commercial Treaty adds after the multiply
	CHECK(g.market2 == (int)std::lround(long_route * 0.2 * 1.2)); // the ally's bonus is unchanged
}

// Civic centres behind both markets, so neither market stands on neutral land and decays
// to Gaia (TerritoryDecay.js); out of arrow range of the route.
static void root_markets(Simulation &w) {
	w.spawn_building_world(-110.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
	w.spawn_building_world(110.0f, 0.0f, BLD_TOWN_CENTER, 1, 1.0f);
}

static const Simulation::Building *find_bld(const Simulation &s, uint32_t id) {
	for (const auto &b : s.buildings()) if (b.id == id) return &b;
	return nullptr;
}

static void test_trader_route() {
	static Simulation s;
	fresh(s);
	// Player 0's market and player 1's market, 160 units (640 m) apart; neutral, not war.
	const uint32_t m0 = s.spawn_building_world(-80.0f, 0.0f, BLD_MARKET, 0, 1.0f);
	const uint32_t m1 = s.spawn_building_world(80.0f, 0.0f, BLD_MARKET, 1, 1.0f);
	CHECK(find_bld(s, m0) && find_bld(s, m1));
	root_markets(s);
	// At war, player 1's market is no trade partner and player 0 has no route.
	CHECK(!s.has_trade_route(0));
	// CeasefireManager makes enemies neutral: their markets trade until it ends.
	s.set_ceasefire_seconds(s.game_time() + 60.0f);
	CHECK(s.can_trade_at(0, *find_bld(s, m1)));
	s.set_ceasefire_seconds(0.0f);
	CHECK(!s.can_trade_at(0, *find_bld(s, m1)));
	s.set_diplomacy(0, 1, Simulation::DIPLO_NEUTRAL);
	CHECK(s.has_trade_route(0) && s.can_trade_at(0, *find_bld(s, m1)));
	CHECK(!s.has_trade_route(2)); // player 2 is still at war with player 1

	// A twin world without the trader separates trade income from other trickles.
	static Simulation c;
	fresh(c);
	c.spawn_building_world(-80.0f, 0.0f, BLD_MARKET, 0, 1.0f);
	c.spawn_building_world(80.0f, 0.0f, BLD_MARKET, 1, 1.0f);
	root_markets(c);
	c.set_diplomacy(0, 1, Simulation::DIPLO_NEUTRAL);

	const uint32_t trader = s.spawn_unit(-74.0f, 4.0f, 0, UNIT_TRADER);
	const Simulation::TradeGain g = s.calculate_trade_gain(*find_bld(s, m0), *find_bld(s, m1), 0);
	CHECK(g.trader > 0 && g.market1 > 0);
	// Trader.js goods.amount: none until it loads goods at a market.
	Simulation::TradeGain carried;
	CHECK(!s.trader_goods(trader, carried));
	// UnitAI.js: a player's trader trades only when ordered; left idle it stays put.
	run(s, 5.0f);
	run(c, 5.0f);
	{
		auto u = s.units();
		for (size_t i = 0; i < u.count; ++i)
			if (u.id[i] == trader) CHECK(u.action_state[i] == ACTION_IDLE && std::fabs(u.pos_x[i] + 74.0f) < 0.5f);
	}
	s.order_trade(&trader, 1, m0, m1);
	run(s, 235.0f);
	run(c, 235.0f);
	const int p0_gain = total(s, 0) - total(c, 0), p1_gain = total(s, 1) - total(c, 1);
	std::printf("  trader: +%d for player 0, +%d for player 1 (per leg %d/%d/%d)\n",
	            p0_gain, p1_gain, g.trader, g.market1, g.market2);
	// Each paid leg gives player 0 the trader gain plus its market's bonus
	// (trader + market1 or trader + market2) and player 1 the other bonus.
	CHECK(p0_gain >= 2 * (g.trader + g.market1));
	CHECK(p1_gain >= 2 * g.market1);
	CHECK(p0_gain % (g.trader + g.market1) == 0 && p1_gain % g.market1 == 0);
	CHECK(p0_gain / (g.trader + g.market1) == p1_gain / g.market1); // one bonus per leg each
	// On its way it carries goods whose total gain is its carry.
	{
		CHECK(s.trader_goods(trader, carried));
		auto uv = s.units();
		for (size_t i = 0; i < uv.count; ++i)
			if (uv.id[i] == trader) CHECK(uv.carry[i] == carried.trader + carried.market1 + carried.market2);
	}

	// Declaring war on the other market's owner ends the route and drops the goods;
	// 0 A.D. walks the trader back to the other market (Trader.RemoveMarket, TradingCanceled).
	s.set_diplomacy(0, 1, Simulation::DIPLO_ENEMY);
	run(s, 1.0f);
	CHECK(!s.trader_routes_.count(trader)); // the route is over
	CHECK(!s.trader_goods(trader, carried)); // the goods are dropped
	{
		const int32_t i = s.lookup_unit(trader);
		CHECK(i >= 0 && s.units_.carry[i] == 0);
		CHECK(s.units_.action_state[i] == ACTION_MOVE && s.units_.has_order[i]); // walking back
	}
	run(c, 1.0f);
	const int frozen = total(s, 0) - total(c, 0);
	run(s, 60.0f);
	run(c, 60.0f);
	CHECK(total(s, 0) - total(c, 0) == frozen); // no route, no income
	// It walks back to its own market and idles there.
	bool home = false;
	for (int k = 0; k < (int)(180.0f / Simulation::FIXED_STEP) && !home; ++k) {
		s.tick(Simulation::FIXED_STEP);
		const int32_t i = s.lookup_unit(trader);
		if (i < 0) break;
		const float dx = s.units_.pos_x[i] + 80.0f, dz = s.units_.pos_z[i];
		home = s.units_.action_state[i] == ACTION_IDLE && !s.units_.has_order[i] && dx * dx + dz * dz < 64.0f;
	}
	CHECK(home);
}

// P4: 0 A.D. Trader.RemoveMarket + UnitAI TradingCanceled: a trader whose market is
// destroyed walks back to the other market instead of idling where it stands.
static void test_trader_market_destroyed() {
	static Simulation s;
	fresh(s);
	const uint32_t m0 = s.spawn_building_world(-80.0f, 0.0f, BLD_MARKET, 0, 1.0f);
	const uint32_t m1 = s.spawn_building_world(80.0f, 0.0f, BLD_MARKET, 1, 1.0f);
	root_markets(s);
	s.set_diplomacy(0, 1, Simulation::DIPLO_NEUTRAL);
	const uint32_t trader = s.spawn_unit(-74.0f, 4.0f, 0, UNIT_TRADER);
	s.order_trade(&trader, 1, m0, m1);
	auto carry = [&]() {
		const int32_t i = s.lookup_unit(trader);
		return i < 0 ? -1 : s.units_.carry[i];
	};
	for (int k = 0; k < 240 && carry() <= 0; ++k) run(s, 0.5f);
	CHECK(carry() > 0);
	run(s, 15.0f); // mid-leg toward the far market, well away from the near one
	for (auto &b : s.buildings_) if (b.id == m1) { s.destroy_building(b); break; }
	run(s, 1.0f);
	CHECK(!s.trader_routes_.count(trader)); // the route is over, the goods dropped
	{
		const int32_t i = s.lookup_unit(trader);
		CHECK(i >= 0 && s.units_.carry[i] == 0);
		CHECK(s.units_.action_state[i] == ACTION_MOVE && s.units_.has_order[i]); // walking back
	}
	bool home = false;
	for (int k = 0; k < (int)(180.0f / Simulation::FIXED_STEP) && !home; ++k) {
		s.tick(Simulation::FIXED_STEP);
		const int32_t i = s.lookup_unit(trader);
		if (i < 0) break;
		const float dx = s.units_.pos_x[i] + 80.0f, dz = s.units_.pos_z[i];
		home = s.units_.action_state[i] == ACTION_IDLE && !s.units_.has_order[i] && dx * dx + dz * dz < 64.0f;
	}
	CHECK(home); // back at its own market, idle
}

// G10: 0 A.D. Trader.js OnGarrisonedUnitsChanged: traders boarding or leaving a merchant
// ship mid-voyage re-work that leg's gain (each trader aboard adds 20% to all three amounts).
static float sea_height(float x, float z) {
	(void)x;
	return std::fabs(z) < 16.0f ? -3.0f : 3.0f;
}

static void test_ship_trade_gain_mid_voyage() {
	static Simulation s;
	s.set_sim_seed(11);
	s.reset();
	s.set_map_half(64.0f);
	{
		constexpr int RES = 65;
		std::vector<float> h((size_t)RES * RES);
		for (int j = 0; j < RES; ++j)
			for (int i = 0; i < RES; ++i)
				h[(size_t)j * RES + i] = sea_height(-64.0f + 128.0f * i / (RES - 1), -64.0f + 128.0f * j / (RES - 1));
		s.set_terrain_data(h, RES, 0.0f);
		s.begin_bulk_spawn();
		s.end_bulk_spawn();
	}
	for (uint8_t p = 0; p < 4; ++p) {
		s.set_player_active(p, true, false);
		s.set_player_resources(p, 1000, 1000, 1000, 1000);
	}
	s.set_team_civ(1, CIV_CARTHAGINIANS); // its dock's international bonus differs, so the gain's direction shows
	const uint32_t d0 = s.spawn_building_world(-48.0f, 20.0f, BLD_DOCK, 0, 1.0f);
	const uint32_t d1 = s.spawn_building_world(48.0f, -20.0f, BLD_DOCK, 1, 1.0f);
	s.set_diplomacy(0, 1, Simulation::DIPLO_NEUTRAL);
	const uint32_t ship = s.spawn_unit(-44.0f, 10.0f, 0, UNIT_MERCHANT_SHIP);
	s.order_trade(&ship, 1, d0, d1);
	auto carry = [&]() {
		const int32_t i = s.lookup_unit(ship);
		return i < 0 ? -1 : s.units_.carry[i];
	};
	for (int k = 0; k < 600 && carry() <= 0; ++k) run(s, 0.5f);
	CHECK(carry() > 0); // loaded at the first dock with nobody aboard
	Simulation::TradeGain base;
	CHECK(s.trader_goods(ship, base));
	const Simulation::TradeGain leg = s.calculate_trade_gain(*find_bld(s, d0), *find_bld(s, d1), 0, UNIT_MERCHANT_SHIP);
	CHECK(base.trader == leg.trader && base.market1 == leg.market1 && base.market2 == leg.market2);
	auto scaled = [](int v, double m) { return (int)std::floor(m * v + 0.5); };
	auto board_at_ship = [&](void) {
		const int32_t si = s.lookup_unit(ship);
		return s.spawn_unit(s.units_.pos_x[si], s.units_.pos_z[si], 0, UNIT_TRADER);
	};
	const uint32_t t1 = board_at_ship();
	s.order_board_transport(&t1, 1, ship);
	run(s, 0.5f);
	Simulation::TradeGain g1;
	CHECK(s.trader_goods(ship, g1));
	CHECK(g1.trader == scaled(base.trader, 1.2) && g1.market1 == scaled(base.market1, 1.2) &&
			g1.market2 == scaled(base.market2, 1.2));
	{
		const int32_t si = s.lookup_unit(ship);
		CHECK(s.units_.carry[si] == g1.trader + g1.market1 + g1.market2); // the lootable load tracks it
	}
	const uint32_t t2 = board_at_ship();
	s.order_board_transport(&t2, 1, ship);
	run(s, 0.5f);
	Simulation::TradeGain g2;
	CHECK(s.trader_goods(ship, g2));
	CHECK(g2.trader == scaled(base.trader, 1.4) && g2.market1 == scaled(base.market1, 1.4) &&
			g2.market2 == scaled(base.market2, 1.4));
	// A trader killed aboard leaves the same way (GarrisonHolder's holder lets go of it).
	{
		const int32_t p = s.lookup_unit(t2);
		CHECK(p >= 0 && !s.units_.alive[p]);
		s.units_.hp[p] = 0.0f;
		s.units_.action_state[p] = ACTION_DYING;
		s.drop_from_holder(t2);
	}
	run(s, 0.5f);
	Simulation::TradeGain g3;
	CHECK(s.trader_goods(ship, g3));
	CHECK(g3.trader == g1.trader && g3.market1 == g1.market1 && g3.market2 == g1.market2);
	// Boarded traders count toward the next load too (they are aboard, alive 0).
	const uint32_t t3 = board_at_ship();
	s.order_board_transport(&t3, 1, ship);
	run(s, 0.5f);
	const int arrival_leg = s.trader_leg_[ship];
	for (int k = 0; k < 1200 && s.trader_leg_[ship] == arrival_leg; ++k) run(s, 0.5f);
	CHECK(s.trader_leg_[ship] != arrival_leg); // arrived at the far dock and loaded back
	Simulation::TradeGain back;
	CHECK(s.trader_goods(ship, back));
	const Simulation::TradeGain back_leg = s.calculate_trade_gain(*find_bld(s, d1), *find_bld(s, d0), 0, UNIT_MERCHANT_SHIP);
	CHECK(back.trader == scaled(back_leg.trader, 1.4) && back.market1 == scaled(back_leg.market1, 1.4) &&
			back.market2 == scaled(back_leg.market2, 1.4));
}

// G11: 0 A.D. Commands.js "spy-request" bribes any bribable unit of the target player,
// including garrisoned ones (shut in a structure or aboard a ship alike: both keep
// ACTION_GARRISONED with alive 0).
// Commands.js "spy-request" skips a bribable entity that already shares its vision with the
// briber (ShareVisionWith): a merchant ship carrying the briber's units (VisionSharing
// CheckVisionSharings) is no candidate, so with no other the bribe fails.
static void test_bribe_skips_ship_carrying_briber() {
	static Simulation s;
	fresh(s);
	s.spawn_building_world(0.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
	const uint32_t ship = s.spawn_unit(30.0f, 0.0f, 0, UNIT_MERCHANT_SHIP);
	const uint32_t rider = s.spawn_unit(-30.0f, 0.0f, 1, UNIT_FEMALE_CITIZEN);
	s.transport_passengers_[ship].push_back(rider);
	s.set_player_resources(1, 1000, 1000, 1000, 1000);
	s.complete_research(1, TECH_UNLOCK_SPIES);
	const Simulation::BribeResult r = s.cmd_bribe(1, 0);
	std::printf("  bribe when the only merchant ship carries the briber's unit: %s\n", r == Simulation::BRIBE_NO_UNITS ? "no bribable units" : "bribed");
	CHECK(r == Simulation::BRIBE_NO_UNITS);
	s.transport_passengers_[ship].clear();
	CHECK(s.cmd_bribe(1, 0) == Simulation::BRIBE_OK);
}

static void test_bribe_garrisoned_trader() {
	static Simulation s;
	fresh(s);
	const uint32_t tc = s.spawn_building_world(0.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
	const uint32_t trader = s.spawn_unit(4.0f, 0.0f, 0, UNIT_TRADER);
	s.order_garrison(&trader, 1, tc);
	bool inside = false;
	for (int k = 0; k < (int)(30.0f / Simulation::FIXED_STEP) && !inside; ++k) {
		s.tick(Simulation::FIXED_STEP);
		const int32_t i = s.lookup_unit(trader);
		inside = i >= 0 && !s.units_.alive[i] && s.units_.action_state[i] == ACTION_GARRISONED;
	}
	CHECK(inside);
	s.set_player_resources(1, 1000, 1000, 1000, 1000);
	s.complete_research(1, TECH_UNLOCK_SPIES);
	CHECK(s.cmd_bribe(1, 0) == Simulation::BRIBE_OK); // the only trader is shut in the town centre
	CHECK(!s.spies_.empty());
	CHECK(s.player_resource(1, RES_GOLD) == 500); // the spy's 500 metal, paid in gold
	run(s, 0.5f); // the bribed trader shares what it sees without crashing
}
static void test_trading_goods() {
	static Simulation s;
	fresh(s);
	for (uint8_t p = 0; p < MAX_PLAYERS; ++p)
		for (int r = 0; r < RES_COUNT; ++r) CHECK(s.trading_goods(p)[r] == 25);
	uint8_t bad[RES_COUNT] = {50, 50, 10, 0}; // sums to 110
	CHECK(!s.cmd_set_trading_goods(0, bad));
	CHECK(s.trading_goods(0)[RES_WOOD] == 25);
	uint8_t stone_only[RES_COUNT] = {};
	stone_only[RES_STONE] = 100;
	CHECK(!s.cmd_set_trading_goods(MAX_PLAYERS, stone_only));
	CHECK(s.cmd_set_trading_goods(0, stone_only));
	CHECK(s.trading_goods(0)[RES_STONE] == 100 && s.trading_goods(1)[RES_STONE] == 25);

	// A stone-only player's trader brings back nothing but stone, and the other
	// market's owner gets its bonus in stone too.
	s.spawn_building_world(-80.0f, 0.0f, BLD_MARKET, 0, 1.0f);
	s.spawn_building_world(80.0f, 0.0f, BLD_MARKET, 1, 1.0f);
	root_markets(s);
	s.set_diplomacy(0, 1, Simulation::DIPLO_NEUTRAL);
	static Simulation c; // the same world without the trader
	fresh(c);
	c.spawn_building_world(-80.0f, 0.0f, BLD_MARKET, 0, 1.0f);
	c.spawn_building_world(80.0f, 0.0f, BLD_MARKET, 1, 1.0f);
	root_markets(c);
	c.set_diplomacy(0, 1, Simulation::DIPLO_NEUTRAL);
	{
		const uint32_t t = s.spawn_unit(-74.0f, 4.0f, 0, UNIT_TRADER);
		s.order_trade(&t, 1, s.buildings()[0].id, s.buildings()[1].id);
	}
	run(s, 240.0f);
	run(c, 240.0f);
	const int stone = s.player_resource(0, RES_STONE) - c.player_resource(0, RES_STONE);
	std::printf("  trading goods: stone-only trader brought %d stone\n", stone);
	CHECK(stone > 0);
	for (uint8_t r : {RES_FOOD, RES_WOOD, RES_GOLD}) {
		CHECK(s.player_resource(0, r) == c.player_resource(0, r));
		CHECK(s.player_resource(1, r) == c.player_resource(1, r));
	}

	// Saved and hashed: a load keeps the shares, and a change on one peer shows in the hash.
	const char *path = "/tmp/rtso_test_trading_goods.sav";
	CHECK(s.save_state(path));
	static Simulation l;
	fresh(l);
	CHECK(l.load_state(path));
	CHECK(l.trading_goods(0)[RES_STONE] == 100 && l.trading_goods(0)[RES_FOOD] == 0);
	TurnManager tm;
	CHECK(tm.compute_sim_hash(s) == tm.compute_sim_hash(l));
	uint8_t even[RES_COUNT] = {25, 25, 25, 25};
	CHECK(l.cmd_set_trading_goods(0, even));
	CHECK(tm.compute_sim_hash(s) != tm.compute_sim_hash(l));

	// Lockstep command 33 carries one percent byte per ResourceType.
	NetCommand cmd;
	cmd.client_id = 0;
	cmd.cmd_type = (uint8_t)NetCommandType::NET_CMD_SET_TRADING_GOODS;
	uint8_t mix[RES_COUNT] = {};
	mix[RES_FOOD] = 40; mix[RES_WOOD] = 30; mix[RES_GOLD] = 30;
	for (int r = 0; r < RES_COUNT; ++r) cmd.target_id |= (uint32_t)mix[r] << (8 * r);
	TurnManager net;
	net.post_local_command(cmd);
	for (int i = 0; i < 5; ++i) net.tick(TurnManager::TURN_DURATION, l);
	for (int r = 0; r < RES_COUNT; ++r) CHECK(l.trading_goods(0)[r] == mix[r]);
}

// Trader.js: the goods' gain is worked out when they are loaded (CalculateGain in PerformTrade)
// and paid as such on arrival, whatever was researched on the way; Looter.js gives whoever
// kills a trader its goods (calculateCarriedResources: all three gains).
static void test_trader_goods() {
	static Simulation s, c;
	for (Simulation *w : {&s, &c}) {
		fresh(*w);
		w->spawn_building_world(-80.0f, 0.0f, BLD_MARKET, 0, 1.0f);
		w->spawn_building_world(80.0f, 0.0f, BLD_MARKET, 1, 1.0f);
		root_markets(*w);
		w->set_diplomacy(0, 1, Simulation::DIPLO_NEUTRAL);
	}
	const uint32_t m0 = s.buildings()[0].id, m1 = s.buildings()[1].id;
	const uint32_t trader = s.spawn_unit(-74.0f, 4.0f, 0, UNIT_TRADER);
	s.order_trade(&trader, 1, m0, m1);
	auto carry = [&]() {
		const int32_t i = s.lookup_unit(trader);
		return i < 0 ? -1 : s.units_.carry[i];
	};
	// It loads at the first market it reaches, then walks to the other.
	for (int k = 0; k < 240 && carry() <= 0; ++k) { run(s, 0.5f); run(c, 0.5f); }
	const Simulation::TradeGain g = s.calculate_trade_gain(*find_bld(s, m0), *find_bld(s, m1), 0);
	CHECK(carry() == g.trader + g.market1 + g.market2 && g.trader > 0);
	CHECK(s.trader_goods_.count(trader) && s.trader_goods_[trader].market1 == g.market1);
	const int leg = s.trader_leg_[trader];

	// Looter.js: a killer gets the goods' trader gain on top of the trader's own Loot.
	{
		const int32_t i = s.lookup_unit(trader);
		const uint8_t goods = s.units_.res_type_carried[i];
		const int before = s.player_resource(2, goods);
		s.collect_unit_loot(0, 2, (size_t)i);
		const int with_goods = s.player_resource(2, goods) - before;
		s.units_.carry[i] = 0;
		s.collect_unit_loot(0, 2, (size_t)i);
		const int without = s.player_resource(2, goods) - before - with_goods;
		s.units_.carry[i] = g.trader + g.market1 + g.market2;
		s.player(2).res[goods] = (float)before; // keep the twins alike
		std::printf("  trader loot: %d with its goods, %d without (gain %d)\n", with_goods, without, g.trader);
		CHECK(with_goods - without == g.trader + g.market1 + g.market2); // calculateCarriedResources
	}

	// Researched on the way, a trade gain technology doesn't change what these goods pay:
	// the trader's gain and, at player 1's market, player 1's market's bonus (market2Gain).
	s.complete_research(0, TECH_TRADE_GAIN_01);
	c.complete_research(0, TECH_TRADE_GAIN_01);
	const Simulation::TradeGain now = s.calculate_trade_gain(*find_bld(s, m0), *find_bld(s, m1), 0);
	CHECK(now.trader > g.trader);
	const int p0 = total(s, 0) - total(c, 0);
	for (int k = 0; k < 600 && s.trader_leg_[trader] == leg; ++k) { run(s, 0.1f); run(c, 0.1f); }
	CHECK(s.trader_leg_[trader] != leg);
	const int paid = total(s, 0) - total(c, 0) - p0;
	std::printf("  goods loaded before the trade gain tech paid %d (loaded %d+%d, now worth %d+%d)\n", paid, g.trader,
			g.market2, now.trader, now.market2);
	CHECK(paid == g.trader + g.market2);

	// Saved and loaded, the goods on the way keep their amounts.
	const char *path = "/tmp/rtso_test_trader_goods.sav";
	CHECK(s.save_state(path));
	static Simulation l;
	fresh(l);
	CHECK(l.load_state(path));
	CHECK(l.trader_goods_.size() == 1 && l.trader_goods_[trader].trader == s.trader_goods_[trader].trader &&
			l.trader_goods_[trader].market2 == s.trader_goods_[trader].market2);
	TurnManager tm;
	CHECK(tm.compute_sim_hash(s) == tm.compute_sim_hash(l));
	std::remove(path);
}

// Trader.js CalculateGain: each trader aboard a merchant ship adds 20% to all three amounts.
static void test_garrisoned_trader_gain() {
	static Simulation s;
	fresh(s);
	const uint32_t ship = s.spawn_unit(0.0f, 0.0f, 0, UNIT_MERCHANT_SHIP);
	const uint32_t land = s.spawn_unit(4.0f, 0.0f, 0, UNIT_TRADER);
	const uint32_t t1 = s.spawn_unit(6.0f, 0.0f, 0, UNIT_TRADER);
	const uint32_t t2 = s.spawn_unit(8.0f, 0.0f, 0, UNIT_TRADER);
	const uint32_t woman = s.spawn_unit(10.0f, 0.0f, 0, UNIT_FEMALE_CITIZEN);
	const size_t si = (size_t)s.lookup_unit(ship);
	Simulation::TradeGain g;
	g.trader = 100; g.market1 = 21; g.market2 = 7;
	auto same = [](const Simulation::TradeGain &a, int t, int m1, int m2) {
		return a.trader == t && a.market1 == m1 && a.market2 == m2;
	};
	CHECK(same(s.garrisoned_trade_gain(si, g), 100, 21, 7)); // nobody aboard
	s.transport_passengers_[ship] = {woman};
	CHECK(same(s.garrisoned_trade_gain(si, g), 100, 21, 7)); // only traders count
	s.transport_passengers_[ship] = {woman, t1, t2};
	// x1.4, each rounded: 140, 29.4 -> 29, 9.8 -> 10.
	CHECK(same(s.garrisoned_trade_gain(si, g), 140, 29, 10));
	s.transport_passengers_[land] = {t1};
	CHECK(same(s.garrisoned_trade_gain((size_t)s.lookup_unit(land), g), 100, 21, 7)); // land traders have no bonus
	s.transport_passengers_.clear();
}

// UnitAI.js TRADE: a leg whose trader gain rounds to 0 finishes the order.
static void test_zero_gain_route() {
	static Simulation s;
	fresh(s);
	const uint32_t a = s.spawn_building_world(-10.0f, 0.0f, BLD_MARKET, 0, 1.0f);
	const uint32_t b = s.spawn_building_world(10.0f, 0.0f, BLD_MARKET, 0, 1.0f);
	root_markets(s);
	const Simulation::TradeGain g = s.calculate_trade_gain(*find_bld(s, a), *find_bld(s, b), 0);
	std::printf("  markets 20 units apart: trader gain %d\n", g.trader);
	CHECK(g.trader == 0);
	const uint32_t t = s.spawn_unit(-4.0f, 4.0f, 0, UNIT_TRADER);
	s.order_trade(&t, 1, a, b);
	CHECK(s.trader_routes_.count(t)); // the order is taken; arriving with nothing to load ends it
	run(s, 30.0f);
	const int32_t i = s.lookup_unit(t);
	CHECK(i >= 0 && s.units_.action_state[i] == ACTION_IDLE && !s.trader_routes_.count(t));
}

// UnitAI.SetupTradeRoute with no source (a player's market clicks, lockstep command 35 with
// target_x 0) and Trader.SetTargetMarket: the first click makes that market the trader's first
// and walks it there, its Shift-queued walks become the route (expectedRoute), the second click
// trades between them; the first market again cancels (CancelSetupTradeRoute); a click on a
// trading trader's market starts over from it.
static void test_setup_trade_clicks() {
	static Simulation s;
	fresh(s);
	const uint32_t a = s.spawn_building_world(-80.0f, 0.0f, BLD_MARKET, 0, 1.0f);
	const uint32_t b = s.spawn_building_world(80.0f, 0.0f, BLD_MARKET, 1, 1.0f);
	root_markets(s);
	s.set_diplomacy(0, 1, Simulation::DIPLO_NEUTRAL);
	s.set_diplomacy(1, 0, Simulation::DIPLO_NEUTRAL);
	const uint32_t t = s.spawn_unit(-60.0f, 20.0f, 0, UNIT_TRADER);
	auto unit = [&]() { return (size_t)s.lookup_unit(t); };
	uint32_t ma = 9, mb = 9;
	int leg = 0;
	CHECK(!s.trader_route(t, ma, mb, leg));

	TurnManager net;
	auto post = [&](uint32_t market, uint8_t param) {
		NetCommand cmd;
		cmd.client_id = 0;
		cmd.cmd_type = (uint8_t)NetCommandType::NET_CMD_SETUP_TRADE_ROUTE;
		cmd.entity_ids = {t};
		cmd.target_id = market;
		cmd.param = param;
		net.post_local_command(cmd);
		for (int i = 0; i < 5; ++i) net.tick(TurnManager::TURN_DURATION, s);
	};
	post(a, 0);
	CHECK(s.trader_first_market(t) == a && s.trader_route(t, ma, mb, leg) && ma == a && mb == 0);
	CHECK(s.units_.order_kind[unit()] == ORDER_WALK); // WalkToTarget the first market
	// Shift-queued walks are the expected route, not orders; a pushed-front walk is an order.
	s.order_walk(&t, 1, -20.0f, 40.0f, true);
	s.order_walk(&t, 1, 20.0f, 40.0f, true);
	CHECK(!s.order_queues_.count(t) || s.order_queues_[t].empty());
	std::vector<std::pair<float, float>> pts;
	CHECK(s.trader_route_points(t, pts) && pts.size() == 2 && pts[0].first == -20.0f && pts[1].first == 20.0f);
	// The same market again changes nothing; the first market with the cancel flag drops it.
	s.order_setup_trade(&t, 1, a);
	CHECK(s.trader_first_market(t) == a && s.trader_route_points(t, pts) && pts.size() == 2);
	post(a, NET_PARAM_CANCEL_TRADE);
	CHECK(!s.trader_route(t, ma, mb, leg) && !s.trader_route_points(t, pts));

	// Again, then the second market: Order.Trade from a to b along the expected route.
	post(a, 0);
	s.order_walk(&t, 1, 0.0f, 40.0f, true);
	post(b, 0);
	CHECK(s.trader_route(t, ma, mb, leg) && ma == a && mb == b && leg == 0);
	CHECK(s.trader_route_points(t, pts) && pts.size() == 1 && pts[0].second == 40.0f);
	CHECK(s.units_.action_state[unit()] == ACTION_TRADE);
	run(s, 90.0f);
	Simulation::TradeGain g;
	CHECK(s.trader_goods(t, g) && g.trader > 0); // loaded at a, on its way to b

	// A click on its own market starts over: a is its first market again, the goods dropped.
	post(a, 0);
	CHECK(s.trader_first_market(t) == a && !s.trader_goods(t, g) && s.units_.carry[unit()] == 0);
	CHECK(!s.trader_route_points(t, pts));
	// Any other order ends the expected route but keeps the first market (UnitAI.AddOrder).
	s.order_walk(&t, 1, 0.0f, 0.0f, false);
	s.order_walk(&t, 1, 10.0f, 10.0f, true);
	CHECK(s.trader_first_market(t) == a && !s.trader_route_points(t, pts));
	CHECK(s.order_queues_.count(t) && s.order_queues_[t].size() == 1); // a real queued walk now
	// Saved: a load keeps the first market and the hash agrees.
	const char *path = "/tmp/rtso_test_setup_trade.sav";
	CHECK(s.save_state(path));
	static Simulation l;
	fresh(l);
	CHECK(l.load_state(path));
	CHECK(l.trader_first_market(t) == a);
	CHECK(net.compute_sim_hash(s) == net.compute_sim_hash(l));
	std::remove(path);

	// A queued pick while trading ends the Trade order once it comes up: the trader keeps the
	// new first market and walks there.
	s.order_trade(&t, 1, a, b);
	CHECK(s.units_.action_state[unit()] == ACTION_TRADE);
	s.order_setup_trade(&t, 1, b, true);
	CHECK(s.trader_first_market(t) == b);
	run(s, 2.0f);
	CHECK(s.units_.action_state[unit()] != ACTION_TRADE && s.trader_first_market(t) == b);
}

int main() {
	test_zero_gain_route();
	test_tribute();
	test_trading_goods();
	test_barter();
	test_trade_gain();
	test_trader_route();
	test_trader_market_destroyed();
	test_trader_goods();
	test_garrisoned_trader_gain();
	test_ship_trade_gain_mid_voyage();
	test_bribe_garrisoned_trader();
	test_bribe_skips_ship_carrying_briber();
	test_setup_trade_clicks();
	std::printf(failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", failures);
	return failures ? 1 : 0;
}
