// Tribute, barter and trade tests (0 A.D. Player.js TributeResource, Barter.js,
// Market.js CalculateTraderGain, Trader.js, globalscripts/Trade.js).
//   g++ -std=c++20 -O2 -Isrc tools/test_trade.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//       src/network/net_host.cpp -lpthread -o /tmp/test_trade && /tmp/test_trade
#include "simulation/simulation.h"
#include "network/turn_manager.h"
#include <cmath>
#include <cstdio>

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
	CHECK(!s.cmd_tribute(2, 3, too_much));
	CHECK(s.player_resource(2, RES_WOOD) == 900 && s.player_resource(2, RES_GOLD) == 1000);
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
	CHECK(s.barter_price_diff(RES_WOOD) == -2.0f && s.barter_price_diff(RES_STONE) == 2.0f);
	// Only 100 or 500 (DEAL_AMOUNT, BATCH_SIZE deals); the same pair is now dearer.
	CHECK(!s.cmd_barter(0, RES_WOOD, RES_STONE, 200));
	CHECK(!s.cmd_barter(0, RES_WOOD, RES_WOOD, 100));
	CHECK(s.barter_quote(RES_WOOD, RES_STONE, 500) == (int)std::lround(88.0 / 112.0 * 500.0));
	CHECK(s.cmd_barter(0, RES_WOOD, RES_STONE, 500));
	CHECK(s.barter_price_diff(RES_WOOD) == -12.0f && s.barter_price_diff(RES_STONE) == 12.0f);
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
	CHECK(s.barter_quote(RES_STONE, RES_FOOD, 100) == (int)std::lround(102.0 / 110.0 * 100.0));
	// Can't sell what you don't have.
	s.set_player_resources(1, 1000, 50, 1000, 1000);
	CHECK(!s.cmd_barter(1, RES_WOOD, RES_FOOD, 100));
	CHECK(s.player_resource(1, RES_WOOD) == 50);

	// Prices drift back 0.5% every 5 s (Barter.js ProgressTimeout).
	run(s, 5.05f);
	CHECK(s.barter_price_diff(RES_WOOD) == -11.5f && s.barter_price_diff(RES_STONE) == 11.5f);
	run(s, 5.0f);
	CHECK(s.barter_price_diff(RES_WOOD) == -11.0f);

	// The overall difference never passes +-99%: the dynamic part clamps at 89.
	s.set_player_resources(0, 100000, 100000, 100000, 100000);
	for (int i = 0; i < 30; ++i) CHECK(s.cmd_barter(0, RES_FOOD, RES_GOLD, 500));
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
	// At war, player 1's market is no trade partner and player 0 has no route.
	CHECK(!s.has_trade_route(0));
	s.set_diplomacy(0, 1, Simulation::DIPLO_NEUTRAL);
	CHECK(s.has_trade_route(0) && s.can_trade_at(0, *find_bld(s, m1)));
	CHECK(!s.has_trade_route(2)); // player 2 is still at war with player 1

	// A twin world without the trader separates trade income from other trickles.
	static Simulation c;
	fresh(c);
	c.spawn_building_world(-80.0f, 0.0f, BLD_MARKET, 0, 1.0f);
	c.spawn_building_world(80.0f, 0.0f, BLD_MARKET, 1, 1.0f);
	c.set_diplomacy(0, 1, Simulation::DIPLO_NEUTRAL);

	const uint32_t trader = s.spawn_unit(-74.0f, 4.0f, 0, UNIT_TRADER);
	const Simulation::TradeGain g = s.calculate_trade_gain(*find_bld(s, m0), *find_bld(s, m1), 0);
	CHECK(g.trader > 0 && g.market1 > 0);
	// The idle trader finds the route by itself and walks it.
	run(s, 240.0f);
	run(c, 240.0f);
	const int p0_gain = total(s, 0) - total(c, 0), p1_gain = total(s, 1) - total(c, 1);
	std::printf("  trader: +%d for player 0, +%d for player 1 (per leg %d/%d/%d)\n",
	            p0_gain, p1_gain, g.trader, g.market1, g.market2);
	// Each paid leg gives player 0 the trader gain plus its market's bonus
	// (trader + market1 or trader + market2) and player 1 the other bonus.
	CHECK(p0_gain >= 2 * (g.trader + g.market1));
	CHECK(p1_gain >= 2 * g.market1);
	CHECK(p0_gain % (g.trader + g.market1) == 0 && p1_gain % g.market1 == 0);
	CHECK(p0_gain / (g.trader + g.market1) == p1_gain / g.market1); // one bonus per leg each

	// Declaring war on the other market's owner ends the route.
	s.set_diplomacy(0, 1, Simulation::DIPLO_ENEMY);
	run(s, 1.0f);
	auto u = s.units();
	bool idle = false;
	for (size_t i = 0; i < u.count; ++i)
		if (u.id[i] == trader) idle = u.action_state[i] == ACTION_IDLE && u.carry[i] == 0;
	CHECK(idle);
	run(c, 1.0f);
	const int frozen = total(s, 0) - total(c, 0);
	run(s, 60.0f);
	run(c, 60.0f);
	CHECK(total(s, 0) - total(c, 0) == frozen); // no route, no income
}

int main() {
	test_tribute();
	test_barter();
	test_trade_gain();
	test_trader_route();
	std::printf(failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", failures);
	return failures ? 1 : 0;
}
