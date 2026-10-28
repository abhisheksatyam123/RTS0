// Map triggers: the Trigger.js / Timer.js engine (src/simulation/triggers.h) and
// the scripts ported from 0 A.D. (flood_triggers.js, extinct_volcano_triggers.js,
// polar_sea_triggers.js, scenarios/treasure_islands.js).
//
// Checks
//   1. OnInterval triggers fire on time and stop when disabled (Timer.js).
//   2. Flood, Rising: warning before each rise, half a 0 A.D. metre per 2.4 s
//      step from 260 s, a 2.5 m rise in all by 1580 s; the 2 m home islands stay
//      dry over the first metre; Shallow never rises; Deep starts a metre up.
//   3. Extinct Volcano: Gaia towers start garrisoned, the warning comes a minute
//      before the Game Setup rise time, then the water climbs a metre at a time.
//   4. Rising water drowns units, siege, animals, structures, treasures and resources, but not
//      ships or shipwrecks.
//   5. Saving mid-rise and loading keeps the lockstep hash and the trigger
//      state; both copies stay in sync while the water keeps rising; two
//      identical runs write byte-identical saves.
//   6. Polar Sea: the axe and basket techs are disabled; from 5 minutes, waves of
//      1-3 violent arctic wolves spawn at every trigger point and attack the three
//      nearest player units in turn; the next wave comes 2-4 minutes later.
//   7. Treasure Islands: the intro message, a real treasure pickup counted through
//      OnTreasureCollected, the messages near the goal, the 18-18 battle message on
//      its interval, and 19 treasures winning the match 5 s later.
//   8. Survival of the Fittest: plateaus with the civic centres, the treasure seekers
//      down in the valley (invulnerable), economic structures disabled, treasures on
//      one of the B/C/D sets every 3-5 minutes, Gaia waves from each player's A point
//      attack-walking to its civic centre from 4-6 minutes, and losing the civic centre
//      defeating its player (the seeker goes, the waves stop coming for it).
//
//  10. OnAttackDetected (AttackDetection.js AttackAlert): a hit on a player's entity by
//      another player fires once; hits within 160 m stay suppressed for 60 s, a livestock
//      alert is overridden by a regular one, the suppression survives a save.
//
//   9. OnRange (TriggerPoint.js RegisterRangeTrigger): a query per trigger point reports
//      the counted player's units that come into range and leave it, keeps its
//      collection through a save, and stops when its trigger is disabled.
//
//   g++ -std=c++20 -O2 -Isrc tools/test_triggers.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//       src/network/net_host.cpp -lpthread -o /tmp/test_triggers && /tmp/test_triggers
// Private access only to register a test trigger and read the trigger state.
#include <map>
#include <set>
#define private public
#include "simulation/simulation.h"
#undef private
#include "network/turn_manager.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace rtso;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; std::printf("  FAIL " __VA_ARGS__); std::printf("\n"); } } while (0)

static constexpr uint32_t SEED = 7;
static constexpr int PLAYERS = 2;
static constexpr float MAP_HALF = 128.0f;
// rms_generator.cpp kTriggerMapHeight: the rising-water maps' heights are 0 A.D. metres (HEIGHT_PER_METRE).
static constexpr float M = 0.25f;

static void build(Simulation &s, MapType map, int water_mode = Simulation::WATER_LEVEL_RISING, int rise_min = 25) {
	s.reset();
	s.set_sim_seed(SEED);
	s.set_waves_enabled(false);
	s.generate_random_map(map, SEED, PLAYERS, MAP_HALF);
	s.set_victory_condition(VICTORY_ENDLESS);
	s.set_water_level_mode(water_mode);
	s.set_sea_level_rise_minutes(rise_min);
}

static void run_to(Simulation &s, float seconds) {
	// A finished match stops ticking (and its trigger clock).
	while (!s.game_over_ && !s.victory_ && s.trigger_state().time_ms < (uint32_t)std::lround(seconds * 1000.0f))
		s.tick(Simulation::FIXED_STEP);
}

static bool near(float a, float b) { return std::fabs(a - b) < 1e-3f; }

// Of the nav cells within `r` of player 0's civic centre, those land units walk on dry (not
// water or wadeable shallows), as a share of the cells that aren't under a structure.
static float dry_share_round_cc(Simulation &s, float r) {
	float cx = 0.0f, cz = 0.0f;
	for (const auto &b : s.buildings())
		if (b.team == 0 && b.type == BLD_TOWN_CENTER) { cx = b.x; cz = b.z; }
	int dry = 0, all = 0;
	for (float z = cz - r; z <= cz + r; z += 0.5f)
		for (float x = cx - r; x <= cx + r; x += 0.5f) {
			if ((x - cx) * (x - cx) + (z - cz) * (z - cz) > r * r) continue;
			auto [nx, ny] = s.nav_grid_.world_to_cell(x, z);
			const uint8_t c = s.nav_grid_.cells.at(nx, ny);
			if (c == NAV_OBSTRUCTED) continue;
			++all;
			dry += c == NAV_PASSABLE;
		}
	return all ? (float)dry / (float)all : 0.0f;
}

static void check_interval() {
	static Simulation s;
	s.reset();
	// OnInterval every second after half a second: fires at 0.5, 1.5, 2.5 s.
	const uint16_t t = s.triggers_.register_trigger(TRIGGER_ON_INTERVAL, TRIGGER_ACTION_FLOOD_WARNING, true, 1000, 500);
	// Trigger.js RegisterTrigger: a name registered before is refused, so this adds no second timer.
	CHECK(s.triggers_.register_trigger(TRIGGER_ON_INTERVAL, TRIGGER_ACTION_FLOOD_WARNING, true, 300, 0) == t &&
	      s.trigger_state().timers.size() == 1, "a second registration of one trigger went through");
	run_to(s, 3.0f);
	CHECK(s.trigger_state().notifications.size() == 3, "interval trigger: %zu firings in 3 s, expected 3", s.trigger_state().notifications.size());
	s.triggers_.disable_trigger(t);
	run_to(s, 6.0f);
	CHECK(s.trigger_state().notifications.size() == 3, "disabled interval trigger kept firing (%zu)", s.trigger_state().notifications.size());
	CHECK(s.trigger_state().timers.empty(), "disabled interval trigger left %zu timers", s.trigger_state().timers.size());
	run_to(s, 13.0f);
	CHECK(s.trigger_state().notifications.empty(), "notifications not expired after 10 s");
	if (!failures) std::printf("OnInterval fires on time, stops when disabled, notifications expire: ok\n");
}

static void check_flood() {
	const int before = failures;
	static Simulation s;
	build(s, MAP_FLOOD);
	const float h0 = s.water_height();
	run_to(s, 245.0f);
	CHECK(s.time_notifications_for(0).empty(), "flood warning shown before 245.6 s");
	run_to(s, 246.0f);
	CHECK(s.time_notifications_for(0).size() == 1, "no flood warning 14.4 s before the first rise");
	run_to(s, 259.9f);
	CHECK(near(s.water_height(), h0), "flood water moved before 260 s (%.3f -> %.3f)", h0, s.water_height());
	run_to(s, 260.1f);
	CHECK(near(s.water_height(), h0 + 0.5f * M), "first flood step: %.3f, expected %.3f", s.water_height(), h0 + 0.5f * M);
	run_to(s, 262.5f);
	CHECK(near(s.water_height(), h0 + 1.0f * M), "first flood rise: %.3f, expected %.3f", s.water_height(), h0 + 1.0f * M);
	// flood.js's home islands stand 2 m high: the first metre leaves them dry (the map's 0 m is
	// the nav water line), so Petra keeps room to build until the second rise.
	const float dry = dry_share_round_cc(s, 8.0f);
	CHECK(dry > 0.8f, "home island %.0f%% dry after the first metre", 100.0f * dry);
	run_to(s, 1500.0f);
	CHECK(near(s.water_height(), h0 + 1.0f * M), "flood water moved between rises (%.3f)", s.water_height());
	run_to(s, 1580.0f);
	CHECK(dry_share_round_cc(s, 8.0f) < 0.2f, "home island %.0f%% dry after the second rise", 100.0f * dry_share_round_cc(s, 8.0f));
	CHECK(near(s.water_height(), h0 + 5.0f * M), "second flood rise: %.3f, expected %.3f", s.water_height(), h0 + 5.0f * M);
	CHECK(s.trigger_state().timers.empty(), "flood left %zu timers after the last rise", s.trigger_state().timers.size());

	static Simulation shallow, deep;
	build(shallow, MAP_FLOOD, Simulation::WATER_LEVEL_SHALLOW);
	build(deep, MAP_FLOOD, Simulation::WATER_LEVEL_DEEP);
	run_to(shallow, 300.0f);
	run_to(deep, 1.0f);
	CHECK(near(shallow.water_height(), h0), "shallow flood rose to %.3f", shallow.water_height());
	CHECK(near(deep.water_height(), h0 + 1.0f * M), "deep flood at %.3f, expected %.3f", deep.water_height(), h0 + 1.0f * M);
	if (failures == before) std::printf("Flood: warning, 2.4 s steps from 260 s, home islands dry until +5 m by 1580 s; shallow and deep: ok\n");
}

static void check_volcano() {
	const int before = failures;
	static Simulation s;
	build(s, MAP_EXTINCT_VOLCANO, Simulation::WATER_LEVEL_RISING, 2);
	const float h0 = s.water_height();
	run_to(s, 1.0f);
	// extinct_volcano.js's land stands 1 m above the water: dry ground, not wadeable shallows.
	int shallow = 0, cells = 0;
	for (int y = 0; y < s.nav_grid_.cells.h; ++y)
		for (int x = 0; x < s.nav_grid_.cells.w; ++x) {
			const uint8_t c = s.nav_grid_.cells.at(x, y);
			cells += c != NAV_WATER;
			shallow += c == NAV_SHALLOW;
		}
	CHECK(shallow * 10 < cells, "%d of %d land cells are shallows at the start", shallow, cells);
	int towers = 0, full = 0;
	for (const auto &b : s.buildings()) {
		if (b.team != GAIA_PLAYER || b.wrecked) continue;
		CHECK(b.type != BLD_TOWER, "a Gaia defense tower: 0.28's palisades_fort is a sentry tower");
		if (b.type != BLD_SENTRY_TOWER) continue;
		++towers;
		const int cap = s.get_garrison_capacity(b.id);
		CHECK(cap == 3, "a Wooden Tower holds %d, 0.28's sentry tower 3", cap);
		if (cap > 0 && s.get_garrison_count(b.id) == cap) ++full;
	}
	CHECK(towers > 0, "Extinct Volcano has no Gaia towers");
	for (const auto &b : s.buildings())
		if (b.team == GAIA_PLAYER && b.type == BLD_SENTRY_TOWER) CHECK(b.max_capture_points == 0.0f, "palisades_fort has <Capturable disable=\"\"/>");
	CHECK(full == towers, "%d of %d Gaia towers garrisoned", full, towers);
	run_to(s, 59.0f);
	CHECK(s.time_notifications_for(0).empty(), "volcano warning before the last minute");
	run_to(s, 61.0f);
	CHECK(s.time_notifications_for(1).size() == 1, "no volcano warning a minute before the rise");
	run_to(s, 119.9f);
	CHECK(near(s.water_height(), h0), "volcano water moved before the rise time (%.3f)", s.water_height());
	run_to(s, 120.1f);
	CHECK(near(s.water_height(), h0 + 1.0f * M), "first volcano step: %.3f, expected %.3f", s.water_height(), h0 + 1.0f * M);
	run_to(s, 149.0f);
	CHECK(near(s.water_height(), h0 + 1.0f * M), "volcano stepped again within 30 s");
	run_to(s, 181.0f);
	CHECK(near(s.water_height(), h0 + 2.0f * M), "second volcano step by 181 s: %.3f, expected %.3f", s.water_height(), h0 + 2.0f * M);
	if (failures == before) std::printf("Extinct Volcano: %d Gaia towers garrisoned, warning a minute ahead, a metre per 30-60 s: ok\n", towers);
}

static void check_drowning() {
	const int before = failures;
	static Simulation s;
	build(s, MAP_FLOOD, Simulation::WATER_LEVEL_SHALLOW);
	run_to(s, 0.5f);
	const Simulation::Building *tc = nullptr;
	for (const auto &b : s.buildings())
		if (b.team == 0 && b.type == BLD_TOWN_CENTER) tc = &b;
	if (!tc) { ++failures; std::printf("  FAIL no Town Center for player 0\n"); return; }
	const float x = tc->x, z = tc->z;
	const uint32_t tc_id = tc->id;
	const uint32_t ship = s.spawn_unit(x + 12.0f, z, 0, UNIT_TRIREME);
	const uint32_t ram = s.spawn_unit(x - 12.0f, z, 0, UNIT_SIEGE_RAM);
	const uint32_t chest = s.spawn_treasure(x, z + 14.0f, TREASURE_METAL);
	const uint32_t wreck = s.spawn_treasure(x, z - 14.0f, TREASURE_SHIPWRECK);
	int organic = 0;
	for (size_t i = 0; i < s.units().count; ++i)
		if (s.units().alive[i] && s.units().team[i] == 0 && !unit_is_ship(s.units().unit_type[i]) && !unit_is_siege_engine(s.units().unit_type[i])) ++organic;
	s.set_water_level(s.get_terrain_height(x, z) + 30.0f, 1.0f);
	int left = 0;
	bool ship_ok = false, ram_ok = false;
	for (size_t i = 0; i < s.units().count; ++i) {
		if (!s.units().alive[i] || s.units().team[i] != 0) continue;
		if (s.units().id[i] == ship) ship_ok = true;
		else if (s.units().id[i] == ram) ram_ok = true;
		else ++left;
	}
	bool tc_wrecked = false;
	for (const auto &b : s.buildings())
		if (b.id == tc_id) tc_wrecked = b.wrecked && b.hp <= 0.0f;
	CHECK(organic > 0 && left == 0, "%d of %d of player 0's units survived the water", left, organic);
	// flood_triggers.js: only Organic entities are Kill()ed, anything else with a Visual becomes
	// an actor; ships, docks and shipwrecks float (pos.y stays at the water).
	CHECK(ship_ok && !ram_ok, "the ship %s, the ram %s", ship_ok ? "floats" : "sank", ram_ok ? "floats" : "is lost");
	CHECK(!s.is_treasure_id(chest) && s.is_treasure_id(wreck), "chest %s, shipwreck %s", s.is_treasure_id(chest) ? "collectable" : "gone",
	      s.is_treasure_id(wreck) ? "floats" : "gone");
	CHECK(tc_wrecked, "Town Center not lost under water");
	run_to(s, 2.0f);
	if (failures == before) std::printf("Rising water drowns %d units, a ram, a chest and the Town Center; the ship and shipwreck float: ok\n", organic);
}

static std::vector<char> read_file(const char *path) {
	std::vector<char> v;
	if (FILE *f = std::fopen(path, "rb")) {
		char buf[65536];
		size_t n;
		while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) v.insert(v.end(), buf, buf + n);
		std::fclose(f);
	}
	return v;
}

static void check_save_load(TurnManager &tm, MapType map, float save_at, float run_until, int mode, int rise_min, const char *name) {
	const int before = failures;
	static Simulation live, loaded;
	const char *path = "/tmp/rts0_test_triggers.sav";
	build(live, map, mode, rise_min);
	run_to(live, save_at);
	if (!live.save_state(path)) { ++failures; std::printf("  FAIL %s: save\n", name); return; }
	build(loaded, map); // Game Setup defaults: the save must bring the real ones back
	if (!loaded.load_state(path)) { ++failures; std::printf("  FAIL %s: load\n", name); return; }
	CHECK(tm.compute_sim_hash(live) == tm.compute_sim_hash(loaded), "%s: hash differs right after loading", name);
	CHECK(loaded.water_level_mode() == mode && loaded.sea_level_rise_minutes() == rise_min,
	      "%s: Game Setup water options lost (%d/%d)", name, loaded.water_level_mode(), loaded.sea_level_rise_minutes());
	CHECK(loaded.trigger_hash() == live.trigger_hash() && loaded.trigger_state().timers.size() == live.trigger_state().timers.size(),
	      "%s: trigger state lost (%zu vs %zu timers)", name, loaded.trigger_state().timers.size(), live.trigger_state().timers.size());
	int diverged = -1;
	for (int t = 0; live.trigger_state().time_ms < (uint32_t)(run_until * 1000.0f); ++t) {
		live.tick(Simulation::FIXED_STEP);
		loaded.tick(Simulation::FIXED_STEP);
		if (tm.compute_sim_hash(live) != tm.compute_sim_hash(loaded)) { diverged = t; break; }
	}
	CHECK(diverged < 0, "%s: loaded game out of sync %d ticks after loading", name, diverged);
	CHECK(near(live.water_height(), loaded.water_height()), "%s: water %.3f vs %.3f", name, live.water_height(), loaded.water_height());
	if (failures == before) std::printf("%s: save at %.0f s, load, in sync to %.0f s (water %.2f): ok\n", name, save_at, run_until, live.water_height());
}

static void check_identical_saves() {
	static Simulation a, b;
	build(a, MAP_EXTINCT_VOLCANO, Simulation::WATER_LEVEL_RISING, 1);
	run_to(a, 75.0f);
	a.save_state("/tmp/rts0_test_triggers_a.sav");
	build(b, MAP_EXTINCT_VOLCANO, Simulation::WATER_LEVEL_RISING, 1);
	run_to(b, 75.0f);
	b.save_state("/tmp/rts0_test_triggers_b.sav");
	const std::vector<char> fa = read_file("/tmp/rts0_test_triggers_a.sav"), fb = read_file("/tmp/rts0_test_triggers_b.sav");
	CHECK(!fa.empty() && fa == fb, "two identical rising-water runs wrote different saves (%zu vs %zu bytes)", fa.size(), fb.size());
	if (!fa.empty() && fa == fb) std::printf("two identical rising-water runs: byte-identical saves (%zu bytes)\n", fa.size());
}

static void check_polar_sea() {
	const int before = failures;
	static Simulation s;
	build(s, MAP_POLAR_SEA);
	const auto &pts = s.trigger_state().points;
	CHECK(!pts.empty(), "Polar Sea has no wolf trigger points");
	s.tick(Simulation::FIXED_STEP); // OnInitGame
	// The map is circular: no wolf point in the corners off the map, where wolves stood still.
	int boxed_in = 0;
	for (const TriggerPoint &p : pts) {
		auto [cx, cy] = s.nav_grid_.world_to_cell(p.x, p.z);
		boxed_in += !s.nav_grid_.is_passable_for(cx, cy, PASS_INFANTRY);
	}
	CHECK(boxed_in == 0, "%d of %zu wolf points on impassable ground", boxed_in, pts.size());
	for (uint8_t t : {TECH_WOODCUTTING_1, TECH_GATHER_LUMBERING_SHARPAXES, TECH_GATHER_LUMBERING_STRONGERAXES, TECH_GATHER_WICKER_BASKETS})
		CHECK(s.research_check(1, t) == Simulation::TECH_ERR_DISABLED, "Polar Sea tech %d not disabled", t);
	CHECK(s.research_check(1, TECH_GATHER_WICKER_BASKETS_MAUR) != Simulation::TECH_ERR_DISABLED, "Polar Sea disabled the Mauryan baskets");
	auto wolves = [&]() {
		int n = 0;
		const auto f = s.fauna();
		for (size_t i = 0; i < f.count; ++i) n += f.alive[i] && f.fauna_type[i] == FAUNA_WOLF_ARCTIC_VIOLENT;
		return n;
	};
	run_to(s, 299.0f);
	CHECK(wolves() == 0, "%d violent wolves before 5 minutes", wolves());
	run_to(s, 301.0f);
	const int w1 = wolves();
	CHECK(w1 >= (int)pts.size() && w1 <= 3 * (int)pts.size(), "first wave: %d wolves at %zu points", w1, pts.size());
	CHECK(s.trigger_state().attack_orders.size() == (size_t)w1, "%zu attack orders for %d wolves", s.trigger_state().attack_orders.size(), w1);
	// Each pack goes for organic player units, the nearest first.
	float d_start = 0.0f, d_later = 0.0f;
	int ordered = 0;
	for (const TriggerAttackOrder &o : s.trigger_state().attack_orders) {
		CHECK(o.count >= 1 && o.count <= 3, "attack order with %u targets", o.count);
		const int32_t u = s.lookup_unit(o.targets[0]);
		CHECK(u >= 0 && s.units_.team[u] < MAX_PLAYERS && (unit_tech_classes(s.units_.unit_type[u]) & TC_ORGANIC),
		      "wolf target %u is not an organic player unit", o.targets[0]);
		const size_t f = s.fauna_id_to_idx_[o.fauna_id];
		CHECK(s.fauna_.behavior[f] == FAUNA_ATTACK && s.fauna_.target_unit_id[f] == o.targets[0], "wolf %u not attacking its first target", o.fauna_id);
		if (u >= 0) d_start += std::hypot(s.units_.pos_x[u] - s.fauna_.pos_x[f], s.units_.pos_z[u] - s.fauna_.pos_z[f]);
		++ordered;
	}
	const std::vector<TriggerAttackOrder> first = s.trigger_state().attack_orders;
	run_to(s, 311.0f);
	int still = 0;
	for (const TriggerAttackOrder &o : first) {
		auto it = s.fauna_id_to_idx_.find(o.fauna_id);
		const int32_t u = s.lookup_unit(o.targets[0]);
		if (it == s.fauna_id_to_idx_.end() || u < 0 || !s.fauna_.alive[it->second]) continue;
		d_later += std::hypot(s.units_.pos_x[u] - s.fauna_.pos_x[it->second], s.units_.pos_z[u] - s.fauna_.pos_z[it->second]);
		++still;
	}
	CHECK(ordered > 0 && (still < ordered || d_later < d_start), "wolves did not close in (%.1f -> %.1f)", d_start, d_later);
	// SpawnWolvesAndAttack reschedules itself 2-4 minutes on.
	bool next = false;
	for (const TriggerTimer &t : s.trigger_state().timers)
		next |= t.action == TRIGGER_ACTION_POLAR_SEA_WAVE && t.time_ms >= 300000 + 120000 && t.time_ms <= 301000 + 240000;
	CHECK(next, "no next wolf wave 2-4 minutes after the first");
	if (failures == before)
		std::printf("Polar Sea: axes and baskets disabled; %d wolves at %zu points at 5:00 attack the nearest units; next wave queued: ok\n", w1, pts.size());
}

static bool shown(const Simulation &s, uint8_t player, const char *text) {
	for (const TimeNotification *n : s.time_notifications_for(player))
		if (std::strstr(n->text, text)) return true;
	return false;
}

static void collected(Simulation &s, uint8_t player, int times) {
	TriggerEventData ev;
	ev.player = player;
	for (int i = 0; i < times; ++i) s.trigger_event(TRIGGER_ON_TREASURE_COLLECTED, ev);
}

static void check_treasure_islands() {
	const int before = failures;
	static Simulation s;
	s.set_sim_seed(SEED);
	s.spawn_initial();
	s.set_waves_enabled(false);
	s.set_victory_condition(VICTORY_ENDLESS); // the default arena has no second player to fight
	s.treasures_.clear(); // the arena's own treasures would count too
	CHECK(s.register_trigger_script("treasure_islands"), "treasure_islands script not registered");
	CHECK(!s.register_trigger_script("no_such_script"), "an unknown script registered");
	run_to(s, 1.9f);
	CHECK(!shown(s, 0, "Collect the treasures"), "intro before 2 s");
	run_to(s, 2.1f);
	CHECK(shown(s, 0, "Collect the treasures") && shown(s, 1, "Collect the treasures") && !shown(s, 2, "Collect the treasures"),
	      "intro not shown to players 1 and 2 only");
	// A real pickup goes through OnTreasureCollected.
	int32_t u = -1;
	for (size_t i = 0; i < s.units_.count; ++i)
		if (s.units_.alive[i] && s.units_.team[i] == 0 && (unit_tech_classes(s.units_.unit_type[i]) & TC_ORGANIC)) { u = (int32_t)i; break; }
	CHECK(u >= 0, "no player 1 unit");
	if (u < 0) return;
	const uint32_t tid = s.spawn_treasure(s.units_.pos_x[u] + 2.0f, s.units_.pos_z[u], TREASURE_FOOD_JARS);
	const uint32_t uid = s.units_.id[u];
	s.order_collect_treasure(&uid, 1, tid);
	for (int t = 0; t < 2000 && !s.treasures().empty(); ++t) s.tick(Simulation::FIXED_STEP);
	CHECK(s.treasures().empty(), "treasure not collected");
	CHECK(s.triggers_.vars[0] == 1, "collected treasure counted %d times", s.triggers_.vars[0]);
	CHECK(shown(s, 0, "You have collected a treasure!") && !shown(s, 1, "You have collected"), "pickup message not player 1's alone");
	// GuiInterface: an observer sees only notifications for everyone; the latest to end comes first.
	CHECK(!shown(s, 0xFF, "You have collected") && !shown(s, 0xFF, "Collect the treasures"), "an observer sees the players' own messages");
	{
		const auto list = s.time_notifications_for(0);
		bool sorted = true;
		for (size_t i = 1; i < list.size(); ++i) sorted &= list[i - 1]->end_ms >= list[i]->end_ms;
		CHECK(list.size() >= 2 && sorted, "notifications not latest-ending first (%zu)", list.size());
	}
	collected(s, 0, 15); // 16: three short of 19
	CHECK(shown(s, 1, "Hurry up!") && shown(s, 0, "Treasures remaining to collect for victory:  %(remainingTreasures)s!\x1fremainingTreasures=3"), "no near-victory messages at 16");
	collected(s, 0, 2);  // 18
	collected(s, 1, 18); // 18 - 18
	CHECK(shown(s, 0, "No winner yet, prepare for battle!") && shown(s, 1, "No winner yet"), "no battle message at 18-18");
	const uint32_t t0 = s.trigger_state().time_ms;
	run_to(s, (float)(t0 + 9500) / 1000.0f);
	CHECK(!shown(s, 0, "Defeat your enemy"), "battle reminder before 10 s");
	run_to(s, (float)(t0 + 10500) / 1000.0f);
	CHECK(shown(s, 0, "Defeat your enemy") && shown(s, 1, "Defeat your enemy"), "no battle reminder 10 s after 18-18");
	CHECK(!s.game_over_, "18-18 ended the match");

	static Simulation w;
	w.set_sim_seed(SEED);
	w.spawn_initial();
	w.set_waves_enabled(false);
	w.set_victory_condition(VICTORY_ENDLESS); // only the script ends this match
	w.register_trigger_script("treasure_islands");
	run_to(w, 3.0f);
	collected(w, 1, 19);
	CHECK(shown(w, 1, "you are victorious!") && shown(w, 0, "you lose!"), "no victory messages at 19");
	run_to(w, 7.9f);
	CHECK(!w.game_over_, "match ended before the 5 s delay");
	run_to(w, 8.1f);
	CHECK(w.game_over_ && (w.winner_mask() & 2) && !(w.winner_mask() & 1), "19 treasures did not win (winners %02x)", w.winner_mask());
	if (failures == before) std::printf("Treasure Islands: intro, pickup counted, near-goal and 18-18 messages, 19 wins after 5 s: ok\n");
}

static void check_survival() {
	const int before = failures;
	static Simulation s;
	build(s, MAP_SURVIVAL_OF_THE_FITTEST);
	s.tick(Simulation::FIXED_STEP); // OnInitGame: InitSurvival
	const TriggerState &ts = s.trigger_state();
	int a_points = 0, bcd[3] = {};
	for (const TriggerPoint &p : ts.points) {
		if (p.ref == 'A') { ++a_points; CHECK(p.owner < PLAYERS, "attacker point owned by %u", p.owner); }
		else if (p.ref >= 'B' && p.ref <= 'D') ++bcd[p.ref - 'B'];
	}
	CHECK(a_points == PLAYERS, "%d attacker points for %d players", a_points, PLAYERS);
	CHECK(bcd[0] > 0 && bcd[0] == bcd[1] && bcd[1] == bcd[2], "treasure points %d/%d/%d", bcd[0], bcd[1], bcd[2]);
	CHECK((int)s.treasures().size() == bcd[0], "%zu treasures placed on a set of %d points", s.treasures().size(), bcd[0]);
	CHECK(ts.watched.size() == 2u * PLAYERS, "%zu watched entities", ts.watched.size());
	CHECK(ts.invulnerable.size() == (size_t)PLAYERS, "%zu invulnerable seekers", ts.invulnerable.size());
	// No mines, hunt or extra structures: a civic centre, four infantry and the seeker each.
	int mines = 0;
	const auto res = s.resources();
	for (size_t i = 0; i < res.count; ++i) mines += res.alive[i] && (res.type[i] == RES_GOLD || res.type[i] == RES_STONE);
	CHECK(mines == 0 && s.fauna().count == 0, "%d mines and %zu animals on Survival", mines, s.fauna().count);
	CHECK(s.buildings().size() == (size_t)PLAYERS, "%zu structures at the start", s.buildings().size());
	for (uint8_t b : {BLD_STOREHOUSE, BLD_FARMSTEAD, BLD_FIELD, BLD_CORRAL, BLD_TOWN_CENTER, BLD_DOCK, BLD_WALL_SEGMENT, BLD_PALISADE,
			BLD_SUPER_DOCK, BLD_CRANNOG})
		CHECK(s.building_disabled_by_trigger(0, b) && s.building_disabled_by_trigger(1, b), "structure %d not disabled", b);
	CHECK(!s.building_disabled_by_trigger(0, BLD_HOUSE) && !s.building_disabled_by_trigger(0, BLD_BARRACKS), "houses or barracks disabled");
	// The seeker stands in the valley, below her plateau, and takes no damage.
	const float k = 0.35f;
	uint32_t seeker[PLAYERS] = {}, cc[PLAYERS] = {};
	for (const TriggerWatch &w : ts.watched) (w.building ? cc : seeker)[w.player] = w.id;
	for (int p = 0; p < PLAYERS; ++p) {
		const int32_t u = s.lookup_unit(seeker[p]);
		const auto bit = s.building_id_to_idx_.find(cc[p]);
		CHECK(u >= 0 && bit != s.building_id_to_idx_.end(), "player %d has no seeker or civic centre", p);
		if (u < 0 || bit == s.building_id_to_idx_.end()) return;
		const Simulation::Building &b = s.buildings_[bit->second];
		CHECK(s.get_terrain_height(s.units_.pos_x[u], s.units_.pos_z[u]) < 5.0f * k &&
		      s.get_terrain_height(b.x, b.z) > 25.0f * k, "player %d: seeker at %.1f, civic centre at %.1f", p,
		      s.get_terrain_height(s.units_.pos_x[u], s.units_.pos_z[u]), s.get_terrain_height(b.x, b.z));
	}
	// "nonbuilder|undeletable|": the seeker can't build or be deleted.
	{
		const int32_t u = s.lookup_unit(seeker[0]);
		CHECK(unit_can_build(s.units_.unit_type[u], s.units_.tmpl[u]) && !s.unit_builds((size_t)u), "seeker builds");
		s.cmd_delete(0, &seeker[0], 1);
		CHECK(s.units_.alive[u], "seeker deleted");
	}
	// 0.28's placement obstructors: no foundation in the low centre, while the plateau round a
	// civic centre stays open.
	auto place = [&](uint8_t player, float x, float z, uint8_t type) {
		const float half = building_type_info(type).footprint * Simulation::GRID * 0.5f;
		return s.check_placement(player, (int)std::floor((x - half) / Simulation::GRID), (int)std::floor((z - half) / Simulation::GRID), type);
	};
	CHECK(place(0, 0.0f, 0.0f, BLD_BARRACKS) == Simulation::PLACE_OBSTRUCTED, "barracks in the centre: %d", place(0, 0.0f, 0.0f, BLD_BARRACKS));
	for (int p = 0; p < PLAYERS; ++p) {
		const Simulation::Building &b = s.buildings_[s.building_id_to_idx_.at(cc[p])];
		const float d = std::hypot(b.x, b.z), x = b.x + b.x / d * 10.0f, z = b.z + b.z / d * 10.0f;
		CHECK(place((uint8_t)p, x, z, BLD_HOUSE) != Simulation::PLACE_OBSTRUCTED, "player %d: house behind the civic centre obstructed", p);
	}
	{
		const int32_t u = s.lookup_unit(seeker[0]);
		const float hp = s.units_.hp[u];
		CombatProfile poison{};
		poison.status = 1;
		poison.status_damage = 20.0f;
		poison.status_interval = 0.5f;
		poison.status_time = 5.0f;
		s.apply_status((size_t)u, poison);
		for (int t = 0; t < 20; ++t) s.tick(Simulation::FIXED_STEP);
		CHECK(s.units_.hp[u] == hp, "invulnerable seeker lost %.1f hp to poison", hp - s.units_.hp[u]);
	}
	// The first wave, 4 to 6 minutes in, announced with a countdown.
	uint32_t wave_at = 0, treasures_at = 0;
	for (const TriggerTimer &t : ts.timers) {
		if (t.action == TRIGGER_ACTION_SURVIVAL_WAVE) wave_at = t.time_ms;
		if (t.action == TRIGGER_ACTION_SURVIVAL_TREASURES) treasures_at = t.time_ms;
	}
	CHECK(wave_at >= 240000 && wave_at <= 360000, "first wave at %u ms", wave_at);
	CHECK(treasures_at >= 180000 && treasures_at <= 300000, "next treasures at %u ms", treasures_at);
	CHECK(shown(s, 0, "The first wave will start in %(time)s!"), "no first-wave countdown");
	auto gaia_units = [&]() {
		int n = 0;
		for (size_t i = 0; i < s.units_.count; ++i) n += s.units_.alive[i] && s.units_.team[i] == GAIA_PLAYER;
		return n;
	};
	run_to(s, (float)wave_at / 1000.0f - 1.0f);
	CHECK(gaia_units() == 0, "%d Gaia units before the first wave", gaia_units());
	run_to(s, (float)wave_at / 1000.0f + 0.1f);
	const int w1 = gaia_units();
	CHECK(w1 >= PLAYERS, "first wave: %d attackers", w1);
	CHECK(shown(s, 1, "An enemy wave is attacking!"), "no wave message");
	// Each attacker attack-walks towards its player's civic centre.
	int walking = 0;
	for (size_t i = 0; i < s.units_.count; ++i)
		if (s.units_.alive[i] && s.units_.team[i] == GAIA_PLAYER) walking += s.units_.order_kind[i] == ORDER_WALK_AND_FIGHT;
	CHECK(walking == w1, "%d of %d attackers have an order", walking, w1);
	// Losing the civic centre defeats player 2; its seeker is gone and its waves stop.
	{
		auto bit = s.building_id_to_idx_.find(cc[1]);
		s.buildings_[bit->second].hp = 0.0f;
	}
	run_to(s, (float)s.trigger_state().time_ms / 1000.0f + 2.0f);
	CHECK(s.is_defeated(1) && !s.is_defeated(0), "civic centre loss: defeated %d/%d", s.is_defeated(0), s.is_defeated(1));
	{
		const int32_t u = s.lookup_unit(seeker[1]);
		CHECK(u < 0 || !s.units_.alive[u], "defeated player's seeker still alive");
		CHECK(!s.invulnerable(seeker[1]), "defeated player's seeker still invulnerable");
	}
	// StartAnEnemyWave draws from ["gaia", ...civs]: about one wave in 15 is the Samnite and
	// mercenary champions.
	int gaia_civ = 0, other_civ = 0;
	for (int k = 0; k < 60; ++k) s.trigger_survival_wave();
	for (size_t i = 0; i < s.units_.count; ++i)
		if (s.units_.alive[i] && s.units_.team[i] == GAIA_PLAYER && s.units_.tmpl[i] < CIV_UNIT_TEMPLATE_COUNT)
			(civ_unit_template(s.units_.tmpl[i]).civ == CIV_GAIA ? gaia_civ : other_civ)++;
	CHECK(gaia_civ > 0 && other_civ > 0, "60 waves: %d Gaia-civ attackers, %d of playable civs", gaia_civ, other_civ);
	if (failures == before)
		std::printf("Survival of the Fittest: %d treasure points a set, seekers in the valley and invulnerable, economy disabled, %d attackers in the first wave, defeat on civic centre loss: ok\n", bcd[0], w1);
}

static void tut_cmd(TurnManager &tm, Simulation &s, uint8_t type, uint32_t target, std::vector<uint32_t> ids = {}, uint8_t param = 0) {
	NetCommand c;
	c.client_id = 0;
	c.cmd_type = type;
	c.target_id = target;
	c.entity_ids = std::move(ids);
	c.param = param;
	tm.post_local_command(c);
	for (int i = 0; i < 4; ++i) tm.tick(TurnManager::TURN_DURATION, s);
}

static void tut_ready(TurnManager &tm, Simulation &s) {
	tut_cmd(tm, s, (uint8_t)NetCommandType::NET_CMD_TUTORIAL_READY, 0);
}

static void tut_queued(Simulation &s, uint8_t player, const char *tmpl, uint16_t count) {
	uint32_t cc = 0;
	for (const Simulation::Building &b : s.buildings_)
		if (b.team == player && is_civic_centre(b.type)) { cc = b.id; break; }
	TriggerEventData ev;
	ev.player = player;
	ev.entity = cc;
	ev.value = civ_unit_template_by_id(tmpl);
	ev.count = count;
	s.trigger_event(TRIGGER_ON_TRAINING_QUEUED, ev);
}

static void check_tutorials() {
	const int before = failures;
	static Simulation s, loaded;
	TurnManager tm;
	s.set_sim_seed(SEED);
	s.spawn_initial();
	s.set_waves_enabled(false);
	s.set_player_active(0, true, false); // seated as Game Setup does: you, and Petra on the enemy
	s.set_player_active(1, true, true);
	CHECK(s.register_trigger_script("introductory_tutorial"), "introductory_tutorial not registered");
	CHECK(s.tutorial_id() == TUTORIAL_INTRODUCTORY, "tutorial id %d", s.tutorial_id());
	CHECK(tutorial_goal_count(TUTORIAL_INTRODUCTORY) == 27 && tutorial_goal_count(TUTORIAL_ECONOMY) == 32, "goal counts");
	for (int t = 0; t < 4; ++t) tm.tick(TurnManager::TURN_DURATION, s);
	CHECK(s.tutorial_goal_index() == 0 && s.tutorial_ready_button(), "intro starts on goal 0 with Ready (%d)", s.tutorial_goal_index());
	CHECK(s.victory_condition() == VICTORY_ENDLESS, "tutorial has a victory condition");
	CHECK(!s.petra_runs_for(1), "Petra still plays the tutorial's enemy");
	const float x = s.units_.pos_x[0], z = s.units_.pos_z[0];
	const uint32_t fruit = s.spawn_resource_world(x + 6.0f, z, RES_FOOD, 200, RES_SUB_NONE);
	const uint32_t tree = s.spawn_resource_world(x - 6.0f, z, RES_WOOD, 200);
	tut_cmd(tm, s, 4, fruit); // gathering before Ready does nothing
	CHECK(s.tutorial_goal_index() == 0, "a gather order skipped the Ready button");
	tut_ready(tm, s);
	CHECK(s.tutorial_goal_index() == 1 && !s.tutorial_ready_button(), "Ready did not open goal 1 (%d)", s.tutorial_goal_index());
	tut_cmd(tm, s, 4, tree); // goal 1 wants berries
	CHECK(s.tutorial_goal_index() == 1, "wood advanced the berries goal");
	// A gather on a gone supply's mirage names its type (type | subtype << 8), not an entity.
	tut_cmd(tm, s, (uint8_t)NetCommandType::NET_CMD_GATHER_NEAR, RES_WOOD | (RES_SUB_NONE << 8));
	CHECK(s.tutorial_goal_index() == 1, "a wood mirage advanced the berries goal");
	tut_cmd(tm, s, (uint8_t)NetCommandType::NET_CMD_GATHER_NEAR, RES_FOOD | (RES_SUB_NONE << 8));
	CHECK(s.tutorial_goal_index() == 2, "a berries mirage: at %d", s.tutorial_goal_index());
	tut_cmd(tm, s, 4, tree);
	CHECK(s.tutorial_goal_index() == 3, "gather goals: at %d, want 3", s.tutorial_goal_index());
	tut_queued(s, 1, "spart/infantry_spearman_b", 5); // the enemy's training is not the player's
	CHECK(s.tutorial_goal_index() == 3, "the enemy's training advanced the goal");
	{
		// Trainer.js raises OnTrainingQueued before the batch joins the queue: ResetQueue refunds
		// what was queued before it and turns autoqueue off; the wrong single hoplite still trains.
		uint32_t cc = 0;
		for (const Simulation::Building &b : s.buildings_)
			if (b.team == 0 && is_civic_centre(b.type)) { cc = b.id; break; }
		s.set_player_resources(0, 5000, 5000, 5000, 5000);
		const uint32_t warned = s.tutorial_warning_count();
		auto queue_size = [&]() { for (const Simulation::Building &b : s.buildings_) if (b.id == cc) return b.queue.size(); return (size_t)99; };
		s.cmd_set_autoqueue(0, cc, true);
		CHECK(s.cmd_train(0, cc, UNIT_SPEARMAN, 1), "a single spearman queued");
		CHECK(queue_size() == 1 && s.tutorial_warning() == TUTORIAL_WARN_BATCH_HOTKEY, "the wrong batch stays: %zu queued", queue_size());
		CHECK(s.cmd_train(0, cc, UNIT_SPEARMAN, 1) && queue_size() == 1, "a second single one refunds the first: %zu queued", queue_size());
		bool autoq = true;
		for (const Simulation::Building &b : s.buildings_) if (b.id == cc) autoq = b.autoqueue;
		CHECK(!autoq, "ResetQueue turns autoqueue off");
		CHECK(s.tutorial_warning_count() == warned + 2, "each warning counts: %u sent", s.tutorial_warning_count() - warned);
		s.tutorial_reset_queue(cc);
	}
	tut_queued(s, 0, "spart/infantry_spearman_b", 1);
	CHECK(s.tutorial_goal_index() == 3 && s.tutorial_warning() == TUTORIAL_WARN_BATCH_HOTKEY, "single hoplite: warning %d", s.tutorial_warning());
	tut_queued(s, 0, "spart/infantry_javelineer_b", 5);
	CHECK(s.tutorial_warning() == TUTORIAL_WARN_CLICK_HOPLITE, "skirmisher batch: warning %d", s.tutorial_warning());
	tut_queued(s, 0, "spart/infantry_spearman_b", 5);
	CHECK(s.tutorial_goal_index() == 4 && s.tutorial_warning() == TUTORIAL_WARN_NONE, "hoplite batch: at %d", s.tutorial_goal_index());

	// Save mid-tutorial: the goal and its registrations come back.
	const char *path = "/tmp/rts0_test_tutorial.sav";
	CHECK(s.save_state(path), "tutorial save");
	loaded.set_sim_seed(SEED);
	loaded.spawn_initial();
	CHECK(loaded.load_state(path), "tutorial load");
	CHECK(loaded.tutorial_id() == TUTORIAL_INTRODUCTORY && loaded.tutorial_goal_index() == 4 && tm.compute_sim_hash(loaded) == tm.compute_sim_hash(s),
	      "tutorial state lost on load (goal %d)", loaded.tutorial_goal_index());
	CHECK(loaded.trigger_hash() == s.trigger_hash(), "trigger hash differs after load");
	CHECK(loaded.tutorial_ready_button() == s.tutorial_ready_button(), "Ready %d after load, %d before", loaded.tutorial_ready_button(), s.tutorial_ready_button());

	// Economy walkthrough: five Ready goals, then gathering, hunting, a rally point.
	static Simulation e;
	TurnManager te;
	e.set_sim_seed(SEED);
	e.spawn_initial();
	e.set_waves_enabled(false);
	CHECK(e.register_trigger_script("starting_economy_walkthrough"), "starting_economy_walkthrough not registered");
	for (int t = 0; t < 4; ++t) te.tick(TurnManager::TURN_DURATION, e);
	for (int i = 0; i < 5; ++i) tut_ready(te, e);
	CHECK(e.tutorial_goal_index() == 5 && !e.tutorial_ready_button(), "economy: at %d after five Ready", e.tutorial_goal_index());
	const float ex = e.units_.pos_x[0], ez = e.units_.pos_z[0];
	const uint32_t efruit = e.spawn_resource_world(ex + 6.0f, ez, RES_FOOD, 200, RES_SUB_NONE);
	const uint32_t etree = e.spawn_resource_world(ex - 6.0f, ez, RES_WOOD, 200);
	tut_cmd(te, e, 4, efruit);
	tut_cmd(te, e, 4, etree);
	tut_cmd(te, e, 17, 1, {}, 0); // hunt
	CHECK(e.tutorial_goal_index() == 8, "economy gather goals: at %d, want 8", e.tutorial_goal_index());
	uint32_t cc = 0;
	for (const Simulation::Building &b : e.buildings_)
		if (b.team == 0 && is_civic_centre(b.type)) { cc = b.id; break; }
	tut_cmd(te, e, 14, 0, {efruit, cc}); // a rally point on berries
	CHECK(e.tutorial_goal_index() == 8 && e.tutorial_warning() == TUTORIAL_WARN_RALLY_TREE, "rally on berries: warning %d", e.tutorial_warning());
	tut_cmd(te, e, 14, 0, {etree, cc});
	CHECK(e.tutorial_goal_index() == 9, "rally on the tree: at %d", e.tutorial_goal_index());
	tut_queued(e, 0, "athen/infantry_spearman_b", 1);
	CHECK(e.tutorial_warning() == TUTORIAL_WARN_HOLD_SHIFT, "single hoplite: warning %d", e.tutorial_warning());
	tut_queued(e, 0, "athen/infantry_spearman_b", 5);
	CHECK(e.tutorial_goal_index() == 10, "hoplite batch: at %d", e.tutorial_goal_index());
	// 0.28's order: the storehouse, then the rally point removed, then a batch of civilians.
	auto fire = [&](uint8_t event, uint32_t value, uint32_t count = 0) {
		TriggerEventData ev;
		ev.player = 0;
		ev.value = value;
		ev.count = count;
		e.trigger_event(event, ev);
	};
	fire(TRIGGER_ON_TRAINING_FINISHED, 0);
	fire(TRIGGER_ON_PLAYER_COMMAND, TRIGGER_CMD_CONSTRUCT, BLD_STOREHOUSE);
	fire(TRIGGER_ON_STRUCTURE_BUILT, BLD_STOREHOUSE);
	CHECK(e.tutorial_goal_index() == 13, "storehouse built: at %d", e.tutorial_goal_index());
	tut_queued(e, 0, "athen/support_civilian", 5);
	CHECK(e.tutorial_goal_index() == 13, "civilians trained before the rally point went: at %d", e.tutorial_goal_index());
	fire(TRIGGER_ON_PLAYER_COMMAND, TRIGGER_CMD_UNSET_RALLY);
	CHECK(e.tutorial_goal_index() == 14, "rally point removed: at %d", e.tutorial_goal_index());
	tut_queued(e, 0, "athen/support_civilian", 1);
	CHECK(e.tutorial_warning() == TUTORIAL_WARN_HOLD_SHIFT_FEMALE, "single civilian: warning %d", e.tutorial_warning());
	tut_queued(e, 0, "athen/support_civilian", 5);
	CHECK(e.tutorial_goal_index() == 15, "civilian batch: at %d", e.tutorial_goal_index());
	fire(TRIGGER_ON_TRAINING_FINISHED, 0);
	CHECK(e.tutorial_goal_index() == 16, "civilians trained: at %d", e.tutorial_goal_index());
	{
		// Every "%(hotkey)s" of a line is the player's key.
		const TutorialGoal *g = tutorial_goal(TUTORIAL_ECONOMY, 9);
		CHECK(g && g->lines[1].hotkey && std::strcmp(g->lines[1].hotkey, "session.batchtrain") == 0, "batch line without its hotkey");
	}
	if (failures == before) std::printf("tutorials: Ready, gather, training and rally goals advance; state survives a save: ok\n");
}

static void check_triggers_demo() {
	// scenarios/triggers_demo.js: listeners on eight events, the yes-no dialog at 10, 15 and
	// 20 s and never again, and OnRange round the A point.
	const int before = failures;
	static Simulation s;
	s.reset();
	s.set_sim_seed(SEED);
	s.spawn_initial();
	s.set_waves_enabled(false);
	s.set_victory_condition(VICTORY_ENDLESS);
	s.add_trigger_point('A', 0.0f, 0.0f, 0);
	CHECK(s.register_trigger_script("triggers_demo"), "triggers_demo not registered");
	run_to(s, 1.0f);
	CHECK(s.trigger_state().triggers.size() == 12, "init, nine listeners, the interval and the range: %zu",
	      s.trigger_state().triggers.size());
	CHECK(s.trigger_state().range_queries.size() == 1, "one range query for the one A point");
	CHECK(s.take_trigger_dialogs().empty(), "no dialog before 10 s");
	run_to(s, 10.5f);
	const auto first = s.take_trigger_dialogs();
	CHECK(first.size() == 1 && first[0].players == 0xFF && first[0].button1 == "Sure" && first[0].button2 == "Rather not",
	      "the yes-no dialog at 10 s for every player");
	run_to(s, 30.0f);
	CHECK(s.take_trigger_dialogs().size() == 2, "two more at 15 and 20 s, then the interval is disabled");
	if (failures == before) std::printf("triggers_demo: listeners, three yes-no dialogs, OnRange: ok\n");
}

static void check_pickup_test() {
	// scenarios/pickup_test_map_triggers.js: at 200 ms four rams and six arrow ships, 95 archers
	// sent to board them (one more once the first boards the erratic ship); the close ram fills.
	const int before = failures;
	static Simulation s;
	s.reset();
	s.set_sim_seed(SEED);
	s.spawn_initial();
	s.set_waves_enabled(false);
	s.set_victory_condition(VICTORY_ENDLESS);
	for (int k = 0; k < 13; ++k) s.add_trigger_point('A', -30.0f + 5.0f * (float)k, 25.0f + (float)(k % 3) * 4.0f, 1);
	s.add_trigger_point('B', -20.0f, 40.0f, 0);
	s.add_trigger_point('C', 20.0f, 40.0f, 0);
	CHECK(s.register_trigger_script("pickup_test_map_triggers"), "pickup_test_map_triggers not registered");
	const uint16_t archer = civ_unit_template_by_id("athen/infantry_archer_b");
	const uint16_t ram = civ_unit_template_by_id("brit/siege_ram");
	const uint16_t ship = civ_unit_template_by_id("athen/ship_arrow");
	auto count = [&](uint16_t tmpl) {
		int n = 0;
		for (size_t i = 0; i < s.units_.count; ++i) n += s.units_.tmpl[i] == tmpl && s.units_.team[i] == 0;
		return n;
	};
	run_to(s, 0.15f);
	CHECK(count(ram) == 0 && count(ship) == 0, "nothing before 200 ms");
	run_to(s, 0.3f);
	CHECK(count(ram) == 4 && count(ship) == 6, "%d rams, %d ships at 200 ms", count(ram), count(ship));
	CHECK(count(archer) == 95, "%d archers at 200 ms, want 95", count(archer));
	uint32_t first_ram = 0;
	for (size_t i = 0; i < s.units_.count; ++i)
		if (s.units_.tmpl[i] == ram && (!first_ram || s.units_.id[i] < first_ram)) first_ram = s.units_.id[i];
	run_to(s, 40.0f);
	CHECK(s.get_transport_passenger_count(first_ram) == 5, "UnitsIntoRamClose: %zu aboard, want 5",
	      s.get_transport_passenger_count(first_ram));
	if (failures == before) std::printf("pickup_test_map: rams, ships and 95 archers at 200 ms, the close ram fills: ok\n");
}

static void check_unit_chasing() {
	// scenarios/unit_chasing_test_triggers.js: at 3 s 30 chase experiments (the two muskox ones
	// hunt player 1's muskox walking north), nobody promotes, and a pair that loses a unit is
	// removed whole.
	const int before = failures;
	static Simulation s;
	s.reset();
	s.set_sim_seed(SEED);
	s.spawn_initial();
	s.set_waves_enabled(false);
	s.set_victory_condition(VICTORY_ENDLESS);
	CHECK(s.register_trigger_script("unit_chasing_test_triggers"), "unit_chasing_test_triggers not registered");
	run_to(s, 2.9f);
	CHECK(s.triggers_.watched.empty(), "nothing before 3 s");
	run_to(s, 3.1f);
	CHECK(s.triggers_.watched.size() == 60, "%zu entities followed, want 30 pairs", s.triggers_.watched.size());
	const int32_t u = s.lookup_unit(s.triggers_.watched[0].id);
	CHECK(u >= 0 && s.promotion_required_xp((size_t)u) > 10000.0f, "promotions are off: %.0f xp", u >= 0 ? s.promotion_required_xp((size_t)u) : 0.0f);
	run_to(s, 4.0f);
	{
		// fast_on_muskox: the cavalryman goes for the walking muskox.
		int hunters = 0, muskoxen = 0;
		const auto f = s.fauna();
		for (const TriggerWatch &w : s.triggers_.watched) {
			if (w.role != 12 && w.role != 13) continue;
			if (w.building == 2) {
				for (size_t i = 0; i < f.count; ++i)
					muskoxen += f.id[i] == w.id && f.owner[i] == 0 && f.behavior[i] == FAUNA_MOVING;
			} else if (const int32_t h = s.lookup_unit(w.id); h >= 0) {
				hunters += s.units().action_state[h] == ACTION_ATTACK;
			}
		}
		CHECK(hunters == 2 && muskoxen == 2, "fast_on_muskox: %d hunters attacking, %d of player 1's muskoxen walking", hunters, muskoxen);
	}
	run_to(s, 90.0f);
	std::map<uint8_t, int> left;
	for (const TriggerWatch &w : s.triggers_.watched) ++left[w.role];
	bool whole = s.triggers_.watched.size() < 60;
	for (const auto &[role, n] : left) whole &= n == 2;
	CHECK(whole, "pairs go whole: %zu units followed after 90 s", s.triggers_.watched.size());
	if (failures == before) std::printf("unit_chasing_test: 30 experiments at 3 s, no promotions, pairs removed whole (%zu left): ok\n",
	                                    s.triggers_.watched.size());
}

static void check_unit_dancing() {
	// scenarios/unit_dancing_test_triggers.js: 26 experiments at 3 s and again at 151 s with
	// spread off; player 1 sees 5 m; each ends when its dancer dies or stops; player 2 wins at 300 s.
	const int before = failures;
	static Simulation s;
	s.reset();
	s.set_sim_seed(SEED);
	s.spawn_initial();
	s.set_waves_enabled(false);
	s.set_victory_condition(VICTORY_ENDLESS);
	CHECK(s.register_trigger_script("unit_dancing_test_triggers"), "unit_dancing_test_triggers not registered");
	run_to(s, 3.1f);
	CHECK(s.triggers_.watched.size() == 154, "%zu units followed, want 86 dancers and 68 attackers", s.triggers_.watched.size());
	size_t dancer = 0, attacker = 0;
	for (const TriggerWatch &w : s.triggers_.watched) {
		const int32_t u = s.lookup_unit(w.id);
		if (u < 0) continue;
		if (w.player == 1 && !dancer) dancer = (size_t)u;
		if (w.player == 3 && !attacker) attacker = (size_t)u;
	}
	CHECK(dancer && s.unit_vision_at(dancer) < 2.0f, "player 1 sees 5 m: %.2f", dancer ? s.unit_vision_at(dancer) : 0.0f);
	CHECK(attacker && s.unit_vision_at(attacker) > 5.0f, "player 2 keeps its vision: %.2f", attacker ? s.unit_vision_at(attacker) : 0.0f);
	run_to(s, 149.0f);
	const int32_t done = s.triggers_.vars[2] + s.triggers_.vars[3];
	CHECK(done > 0, "experiments finish in the first run: %d killed, %d not", s.triggers_.vars[2], s.triggers_.vars[3]);
	CHECK(s.triggers_.vars[0] == 0, "spread still on");
	run_to(s, 151.5f);
	CHECK(s.triggers_.vars[0] == 1, "spread off for the second run");
	size_t second = 0;
	for (const TriggerWatch &w : s.triggers_.watched) second += w.role >= 26;
	CHECK(second == 154, "second run: %zu units followed", second);
	run_to(s, 300.5f);
	CHECK(s.is_game_over() || s.victory_, "player 2 wins at 300 s");
	if (failures == before) std::printf("unit_dancing_test: two runs of 26, 5 m vision, %d killed and %d not, the end at 300 s: ok\n",
	                                    s.triggers_.vars[2], s.triggers_.vars[3]);
}

static int team_units(const Simulation &s) {
	int n = 0;
	for (size_t i = 0; i < s.units_.count; ++i) n += s.units_.team[i] < 2 && (s.units_.alive[i] || s.units_.action_state[i] == ACTION_GARRISONED);
	return n;
}

static void check_unit_pushing() {
	// scenarios/unit_pushing_test.js: 16 experiments at 4 s (1395 units), player 1 gathering a
	// hundred times faster, player 1 the winner at 300 s.
	const int before = failures;
	static Simulation s;
	s.reset();
	s.set_sim_seed(SEED);
	s.spawn_initial();
	s.set_waves_enabled(false);
	s.set_victory_condition(VICTORY_ENDLESS);
	CHECK(s.register_trigger_script("unit_pushing_test"), "unit_pushing_test not registered");
	run_to(s, 3.9f);
	const int start = team_units(s);
	run_to(s, 4.1f);
	CHECK(team_units(s) - start == 1395, "%d units at 4 s, want 1395", team_units(s) - start);
	const uint16_t reg = civ_unit_template_by_id("athen/infantry_spearman_b");
	CHECK(s.unit_tech_mods(0, UNIT_SPEARMAN, reg).gather[Simulation::GK_WOOD] >= 99.0f, "player 1 gathers a hundred times faster");
	CHECK(s.unit_tech_mods(1, UNIT_SPEARMAN, reg).gather[Simulation::GK_WOOD] < 2.0f, "player 2 does not");
	bool end = false;
	for (const TriggerTimer &t : s.trigger_state().timers) end |= t.action == TRIGGER_ACTION_UNIT_PUSHING && t.time_ms == 300000;
	CHECK(end, "EndGame waits for 300 s");
	if (failures == before) std::printf("unit_pushing_test: 1395 units at 4 s, the gather speed, EndGame at 300 s: ok\n");
}

static void check_unit_motion() {
	// scenarios/unit_motion_integration_test.js: 22 experiments at 3 s (651 units among houses,
	// palisades and trees); 4 s later the enclosed experiment's far palisade goes.
	const int before = failures;
	static Simulation s;
	s.reset();
	s.set_sim_seed(SEED);
	s.spawn_initial();
	s.set_waves_enabled(false);
	s.set_victory_condition(VICTORY_ENDLESS);
	CHECK(s.register_trigger_script("unit_motion_integration_test"), "unit_motion_integration_test not registered");
	run_to(s, 2.9f);
	const int start = team_units(s);
	const size_t bld = s.buildings_.size();
	run_to(s, 3.1f);
	CHECK(team_units(s) - start == 651, "%d units at 3 s, want 651", team_units(s) - start);
	CHECK(s.buildings_.size() > bld + 40, "%zu structures added", s.buildings_.size() - bld);
	const uint32_t gate = (uint32_t)s.triggers_.vars[0];
	auto standing = [&]() {
		const auto it = s.building_id_to_idx_.find(gate);
		return it != s.building_id_to_idx_.end() && !s.buildings_[it->second].wrecked && s.buildings_[it->second].hp > 0.0f;
	};
	CHECK(gate && standing(), "the enclosed palisade stands");
	run_to(s, 7.2f);
	CHECK(!standing(), "the enclosed palisade went at 7 s");
	if (failures == before) std::printf("unit_motion_integration_test: 651 units at 3 s, the palisade gone at 7 s: ok\n");
}

static void check_cinema_demo() {
	// scenarios/cinema_demo.js: a notice at 1 s, the "test" path queued and played at 6 s
	// (CCmpCinemaManager): the map is revealed while it plays, OnCinemaPathEnded and
	// OnCinemaQueueEnded fire when its time is up, and a save mid-path keeps the queue.
	const int before = failures;
	static Simulation s, b;
	auto setup = [](Simulation &sim) {
		sim.reset();
		sim.set_sim_seed(SEED);
		sim.spawn_initial();
		sim.set_waves_enabled(false);
		sim.set_victory_condition(VICTORY_ENDLESS);
		CinemaPath p;
		std::strcpy(p.info.name, "test");
		p.info.look_at_target = 1;
		p.nodes = {{0, 10, 0, 0}, {10, 8, 5, 2}, {20, 6, 0, 3}};
		p.targets = {{5, 0, 5, 0}, {15, 0, 5, 4}};
		sim.add_cinema_path(p);
		sim.add_cinema_path(p); // a second path of the same name is ignored
	};
	setup(s);
	CHECK(s.cinema().paths.size() == 1 && s.cinema().paths[0].duration_ms() == 5000, "one 5 s path");
	CHECK(s.register_trigger_script("cinema_demo"), "cinema_demo not registered");
	run_to(s, 1.1f);
	CHECK(s.trigger_state().notifications.size() == 1 && !s.cinema_playing(), "the notice at 1 s");
	run_to(s, 6.1f);
	CHECK(s.cinema_playing() && s.reveal_map() && s.cinema().queue.size() == 1, "the path plays at 6 s, map revealed");
	// The cinema manager updates before Timer.js: the path the 6 s timer played counts from the next turn.
	CHECK(s.cinema().path_elapsed_ms == 100, "the path counts from the turn after Play (6.05 and 6.1 s): %u ms", s.cinema().path_elapsed_ms);
	run_to(s, 8.0f);
	const char *path = "/tmp/rts0_test_triggers_cinema.sav";
	CHECK(s.save_state(path), "save");
	setup(b);
	CHECK(b.load_state(path), "load");
	CHECK(b.cinema_playing() && b.cinema().path_elapsed_ms == s.cinema().path_elapsed_ms && b.trigger_hash() == s.trigger_hash(),
	      "cinema state differs after loading");
	const uint32_t ended = s.trigger_event_calls(TRIGGER_ON_CINEMA_PATH_ENDED);
	run_to(s, 11.2f);
	CHECK(!s.cinema_playing() && !s.reveal_map() && s.cinema().queue.empty(), "the path ended after 5 s, map hidden again");
	CHECK(s.trigger_event_calls(TRIGGER_ON_CINEMA_PATH_ENDED) == ended + 1 && s.trigger_event_calls(TRIGGER_ON_CINEMA_QUEUE_ENDED) == 1,
	      "OnCinemaPathEnded and OnCinemaQueueEnded once each");
	// The camera: at the first node at the start, the last at the end, looking along the target spline.
	const CinemaPath &cp = s.cinema().paths[0];
	cinema::Spline ps(cp.nodes), ts(cp.targets);
	float eye[3], look[3];
	CHECK(cinema::camera_at(cp, ps, ts, 0.0f, eye, look) && std::fabs(eye[0]) < 1e-4f && std::fabs(eye[1] - 10) < 1e-4f &&
	      std::fabs(look[0] - 5) < 1e-4f, "camera at the first node");
	CHECK(cinema::camera_at(cp, ps, ts, 5.0f, eye, look) && std::fabs(eye[0] - 20) < 1e-3f && std::fabs(look[0] - 15) < 1e-3f,
	      "camera at the last node");
	CHECK(cinema::camera_at(cp, ps, ts, 2.0f, eye, look) && std::fabs(eye[0] - 10) < 1e-3f && std::fabs(eye[2] - 5) < 1e-3f,
	      "camera at the middle node after its 2 s");
	CHECK(!cinema::camera_at(cp, ps, ts, 5.1f, eye, look), "nothing past the end");
	// DeletePath drops the path and every queued copy, the playing one too (m_PathQueue.remove_if):
	// the next path takes over with the time already run, and a deleted name can be added again.
	{
		static Simulation q;
		setup(q);
		CinemaPath other = q.cinema().paths[0];
		std::strcpy(other.info.name, "other");
		q.add_cinema_path(other);
		q.cinema_queue_path("test");
		q.cinema_queue_path("other");
		q.cinema_queue_path("test");
		q.cinema_play();
		for (int i = 0; i < 20; ++i) q.tick(Simulation::FIXED_STEP);
		const uint32_t h0 = q.trigger_hash();
		const uint32_t ended0 = q.trigger_event_calls(TRIGGER_ON_CINEMA_PATH_ENDED);
		q.cinema_delete_path("test");
		CHECK(!q.cinema_has_path("test") && q.cinema().queue.size() == 1 && q.cinema().paths.size() == 1 &&
		      q.cinema().queue[0] == 0 && !std::strcmp(q.cinema().paths[0].info.name, "other") && q.cinema_playing() &&
		      q.cinema().path_elapsed_ms == 1000 && q.trigger_hash() != h0, "the deleted path leaves the queue, playing or not");
		for (int i = 0; i < 81; ++i) q.tick(Simulation::FIXED_STEP);
		CHECK(q.trigger_event_calls(TRIGGER_ON_CINEMA_PATH_ENDED) == ended0 + 1 && q.cinema().queue.empty(),
		      "the next path ends 5 s after the deleted one started");
		q.tick(Simulation::FIXED_STEP);
		CHECK(!q.cinema_playing() && q.trigger_event_calls(TRIGGER_ON_CINEMA_QUEUE_ENDED) == 0, "the emptied queue stops quietly");
		q.cinema_delete_path("other");
		q.add_cinema_path(other);
		CHECK(q.cinema().paths.size() == 1 && q.cinema_has_path("other"), "a deleted name can be added again");
	}
	if (failures == before) std::printf("cinema_demo: notice at 1 s, a 5 s path from 6 s with the map revealed, saved mid-path: ok\n");
}

static void check_gallic_fields() {
	// skirmishes/gallic_fields_3p.js: a Roman champion at each B or C point after a minute,
	// attack-walking to its owner's A point; each wave waits 1-3 times longer and grows as much.
	const int before = failures;
	static Simulation s;
	s.set_sim_seed(SEED);
	s.spawn_initial();
	s.set_waves_enabled(false);
	s.set_victory_condition(VICTORY_ENDLESS);
	for (uint8_t p = 0; p < 2; ++p) {
		const float side = p ? 1.0f : -1.0f;
		s.add_trigger_point('A', side * 20.0f, 0.0f, p);
		s.add_trigger_point('B', side * 40.0f, 30.0f, p);
		s.add_trigger_point('C', side * 40.0f, -30.0f, p);
	}
	CHECK(s.register_trigger_script("gallic_fields_3p"), "gallic_fields_3p not registered");
	const uint16_t champion = civ_unit_template_by_id("rome/champion_infantry_swordsman_02"); // gallic_fields_3p.js:4
	CHECK(champion != CIV_UNIT_TEMPLATE_NONE, "no Marian Legionary row");
	auto romans = [&](int *ordered) {
		int n = 0;
		if (ordered) *ordered = 0;
		for (size_t i = 0; i < s.units_.count; ++i)
			if (s.units_.alive[i] && s.units_.team[i] == GAIA_PLAYER && s.units_.tmpl[i] == champion) {
				++n;
				if (ordered) *ordered += s.units_.order_kind[i] != ORDER_NONE;
			}
		return n;
	};
	run_to(s, 59.9f);
	CHECK(romans(nullptr) == 0, "%d Romans before a minute", romans(nullptr));
	run_to(s, 60.1f);
	int ordered = 0;
	CHECK(romans(&ordered) == 2 && ordered == 2, "first wave: %d Romans, %d with orders", romans(nullptr), ordered);
	const int32_t size = s.triggers_.vars[0], wait = s.triggers_.vars[1];
	CHECK(size >= 1 && size <= 3 && wait >= 60000 && wait <= 180000, "next wave of %d after %d ms", size, wait);
	float exact = 0.0f;
	std::memcpy(&exact, &s.triggers_.vars[2], sizeof exact);
	CHECK(exact >= (float)wait && exact < (float)wait + 1.0f, "attackTime keeps its fraction: %.3f ms for %d", exact, wait);
	const int alive = romans(nullptr);
	run_to(s, 60.0f + (float)wait / 1000.0f + 0.1f);
	CHECK(romans(nullptr) >= 2 * size && romans(nullptr) <= alive + 2 * size, "second wave: %d Romans, want %d more", romans(nullptr), 2 * size);
	// Player.js SetState("defeated"): the trigger points pass to Gaia with everything else.
	s.defeat_player(1, 0);
	int gaia_points = 0;
	for (const TriggerPoint &p : s.triggers_.points) gaia_points += p.owner == GAIA_PLAYER;
	CHECK(gaia_points == 3, "%d of the defeated player's 3 trigger points went to Gaia", gaia_points);
	if (failures == before) std::printf("Gallic Fields: a Roman at each point after a minute, %d each after %d ms more: ok\n", size, wait);
}

static void check_death_canyon() {
	const int before = failures;
	static Simulation s;
	s.set_sim_seed(SEED);
	s.spawn_initial();
	s.set_waves_enabled(false);
	CHECK(s.register_trigger_script("death_canyon_invasion_force_triggers"), "death_canyon_invasion_force_triggers not registered");
	run_to(s, 0.5f);
	for (uint8_t t = 0; t < BLD_TYPE_COUNT; ++t) {
		CHECK(s.building_disabled_by_trigger(1, t) && s.building_disabled_by_trigger(3, t), "Invasion Force may build type %d", t);
		CHECK(!s.building_disabled_by_trigger(0, t) && !s.building_disabled_by_trigger(2, t), "a base player may not build type %d", t);
	}
	s.set_player_resources(1, 5000, 5000, 5000, 5000);
	float x = 0.0f, z = 0.0f;
	for (size_t i = 0; i < s.units_.count; ++i)
		if (s.units_.alive[i] && s.units_.team[i] == 1) { x = s.units_.pos_x[i]; z = s.units_.pos_z[i]; break; }
	CHECK(s.cmd_build_wall(1, x + 4.0f, z + 4.0f, x + 12.0f, z + 4.0f, nullptr, 0, false, 0) == 0, "the Invasion Force laid a wall");
	if (failures == before) std::printf("Death Canyon - Invasion Force: the invasion forces build nothing: ok\n");
}

static void check_elephantine() {
	const int before = failures;
	static Simulation s;
	build(s, MAP_ELEPHANTINE);
	int temples = 0, pyramids = 0, towers = 0, full = 0, holders = 0;
	for (const Simulation::Building &b : s.buildings_) {
		if (b.team != GAIA_PLAYER) continue;
		temples += b.type == BLD_TEMPLE || b.type == BLD_TEMPLE_AMUN || b.type == BLD_WONDER;
		pyramids += b.type == BLD_PYRAMID_LARGE;
		if (building_tech_classes(b.type) & TC_TOWER) {
			++towers;
			CHECK(b.max_capture_points == 0.0f, "Kushite tower %u capturable", b.id);
		}
	}
	CHECK(temples == 1 && pyramids == 1, "island temples %d, pyramids %d", temples, pyramids);
	CHECK(towers >= 2, "only %d Kushite towers", towers);
	int gaia_units = 0;
	for (size_t i = 0; i < s.units_.count; ++i) gaia_units += s.units_.alive[i] && s.units_.team[i] == GAIA_PLAYER;
	CHECK(gaia_units >= 8, "only %d Kushites on the island", gaia_units);
	// 0.28's fish: scaleByMapSize(125, 200) groups of 1-2 in the Nile, 12 tiles apart.
	int fish = 0;
	const auto res = s.resources();
	for (size_t i = 0; i < res.count; ++i) fish += res.alive[i] && res.subtype[i] == RES_SUB_FISH;
	CHECK(fish >= 40, "only %d fish in the Nile", fish);
	run_to(s, 1.0f);
	for (const Simulation::Building &b : s.buildings_) {
		if (b.team != GAIA_PLAYER) continue;
		const int cap = s.get_garrison_capacity(b.id);
		if (cap <= 0) continue;
		++holders;
		full += (int)b.garrisoned_units.size() == cap;
	}
	CHECK(holders > 0 && full == holders, "%d of %d Gaia structures garrisoned to capacity", full, holders);
	std::vector<uint32_t> inside;
	for (const Simulation::Building &b : s.buildings_) inside.insert(inside.end(), b.garrisoned_units.begin(), b.garrisoned_units.end());
	// "CitizenSoldier+Infantry" and "Civilian Healer": no other class inside.
	int odd = 0;
	for (uint32_t id : inside) {
		const int32_t u = s.lookup_unit(id);
		const TechClasses c = u >= 0 && s.units_.tmpl[u] < CIV_UNIT_TEMPLATE_COUNT ? civ_unit_template(s.units_.tmpl[u]).tech_classes : 0;
		odd += !((c & TC_HEALER) || ((c & TC_CITIZENSOLDIER) && (c & TC_INFANTRY)) || ((c & TC_CIVILIAN) && (c & TC_WORKER)));
	}
	CHECK(odd == 0, "%d of %zu garrisoned Kushites neither infantry, civilians nor healers", odd, inside.size());
	// GetTemplateNamesByClasses keeps the Noba skirmishers and macemen (merc_e).
	int mercs = 0;
	for (uint32_t id : inside) {
		const int32_t u = s.lookup_unit(id);
		mercs += u >= 0 && s.units_.tmpl[u] < CIV_UNIT_TEMPLATE_COUNT && std::strstr(civ_unit_template(s.units_.tmpl[u]).id, "_merc");
	}
	CHECK(mercs > 0, "no Kushite mercenaries among %zu garrisoned", inside.size());
	int defensive = 0, soldiers = 0;
	for (size_t i = 0; i < s.units_.count; ++i) {
		if (!s.units_.alive[i] || s.units_.team[i] != GAIA_PLAYER) continue;
		if (std::find(inside.begin(), inside.end(), s.units_.id[i]) != inside.end()) continue;
		const uint16_t t = s.units_.tmpl[i];
		if (t >= CIV_UNIT_TEMPLATE_COUNT || !(civ_unit_template(t).tech_classes & TC_SOLDIER)) continue;
		++soldiers;
		defensive += s.units_.stance[i] == STANCE_DEFENSIVE;
	}
	CHECK(soldiers > 0 && defensive == soldiers, "%d of %d island soldiers defensive", defensive, soldiers);
	if (failures == before)
		std::printf("Elephantine: temple, pyramid, %d uncapturable towers, %d Kushites, %d structures garrisoned, %d fish: ok\n", towers, gaia_units, holders, fish);
}

static void check_danubius() {
	// danubius.js / danubius_triggers.js on a 256-tile map: a Gallic village on each bank, the
	// garrisons and defenders at the start, two Gaia warships that take soldiers on board and
	// put them ashore, and the villages' own waves.
	const int before = failures;
	static Simulation s;
	build(s, MAP_DANUBIUS);
	int ccs = 0, houses = 0, huts = 0, longhouses = 0, taverns = 0, temples = 0, towers = 0, walls = 0, capturable = 0;
	bool left = false, right = false;
	for (const Simulation::Building &b : s.buildings_) {
		if (b.team != GAIA_PLAYER) continue;
		if (b.type == BLD_TOWN_CENTER) {
			++ccs;
			(s.danubius_is_left(b.x, b.z) ? left : right) = true;
			continue;
		}
		houses += b.type == BLD_HOUSE;
		huts += b.type == BLD_HUT;
		longhouses += b.type == BLD_LONGHOUSE;
		taverns += b.type == BLD_TAVERN;
		temples += b.type == BLD_TEMPLE;
		towers += b.type == BLD_SENTRY_TOWER; // the river towers are gaul/defense_tower
		walls += b.type == BLD_PALISADE || b.type == BLD_WALL_TOWER;
		if (b.type == BLD_HOUSE || b.type == BLD_HUT || b.type == BLD_LONGHOUSE || b.type == BLD_TAVERN || b.type == BLD_TEMPLE ||
		    b.type == BLD_PALISADE)
			capturable += b.max_capture_points > 0.0f;
	}
	CHECK(ccs == 2 && left && right, "%d Gaia civic centres, one on each bank: %d %d", ccs, left, right);
	// fortressDanubiusVillage twice round: a hut, house, longhouse, tavern, temple and two
	// "defense_tower"s each time; below normalMapSize the towers are palisades_watchtower pieces.
	CHECK(houses >= 3 && huts >= 3 && longhouses >= 3 && taverns >= 3 && temples >= 3,
	      "villages: %d houses, %d huts, %d longhouses, %d taverns, %d temples", houses, huts, longhouses, taverns, temples);
	CHECK(towers == 0, "%d Gaia sentry towers: a 256-tile map's villages have palisade watchtowers", towers);
	CHECK(capturable == 0, "%d village structures capturable", capturable);
	CHECK(walls > 0, "no palisade round the villages");
	int refs[256] = {};
	for (const TriggerPoint &p : s.triggers_.points) ++refs[p.ref];
	for (char r : {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'}) CHECK(refs[(int)r] > 0, "no trigger point %c", r);
	CHECK(refs['I'] == 1 && refs['C'] <= 200 && refs['D'] <= 200, "I %d, C %d, D %d", refs['I'], refs['C'], refs['D']);
	for (const TriggerPoint &p : s.triggers_.points)
		if (p.ref == 'C' || p.ref == 'D') CHECK(s.danubius_is_left(p.x, p.z) == (p.ref == 'C'), "unload point %c on the wrong bank", p.ref);

	// UngarrisonShipsOrder needs a player's Defensive structure on a bank: the civic centres
	// are one (template_structure_civic_civil_centre VisibleClasses), so the landings start
	// before any player builds a tower.
	bool left_active = false, right_active = false;
	s.danubius_active_riversides(left_active, right_active);
	CHECK(left_active || right_active, "no bank active from the players' civic centres");
	run_to(s, 1.0f);
	int holders = 0, full = 0;
	for (const Simulation::Building &b : s.buildings_) {
		if (b.team != GAIA_PLAYER || b.type == BLD_PALISADE || b.type == BLD_WALL_TOWER) continue;
		const int cap = s.get_garrison_capacity(b.id);
		if (cap <= 0) continue;
		++holders;
		full += (int)b.garrisoned_units.size() == cap;
	}
	CHECK(holders > 0 && full == holders, "%d of %d Gaia structures garrisoned to capacity", full, holders);
	std::vector<uint32_t> ships;
	for (const TriggerWatch &w : s.triggers_.watched)
		if (w.role >= 2) ships.push_back(w.id);
	CHECK(ships.size() == 2, "%zu Gaia warships at the start", ships.size());
	for (uint32_t id : ships) {
		const int32_t u = s.lookup_unit(id);
		CHECK(u >= 0 && s.units_.alive[u] && s.units_.team[u] == GAIA_PLAYER && unit_is_ship(s.units_.unit_type[u]), "ship %u", id);
		// 0.28 "Trireme": the Gallic arrow ship, never the scout or fire ship.
		CHECK(u >= 0 && s.units_.tmpl[u] < CIV_UNIT_TEMPLATE_COUNT && std::strcmp(civ_unit_template(s.units_.tmpl[u]).id, "gaul/ship_arrow") == 0,
		      "ship %u is %s", id, u >= 0 && s.units_.tmpl[u] < CIV_UNIT_TEMPLATE_COUNT ? civ_unit_template(s.units_.tmpl[u]).id : "?");
	}
	// Houses hold the "Civilian" and "Healer" templates (civilian workers, or the female
	// citizens on rosters without them).
	int house_units = 0, house_others = 0;
	for (const Simulation::Building &b : s.buildings_) {
		if (b.team != GAIA_PLAYER || b.type != BLD_HOUSE) continue;
		for (uint32_t id : b.garrisoned_units) {
			const int32_t u = s.lookup_unit(id);
			const TechClasses c = u >= 0 && s.units_.tmpl[u] < CIV_UNIT_TEMPLATE_COUNT ? civ_unit_template(s.units_.tmpl[u]).tech_classes : 0;
			++house_units;
			house_others += !((c & TC_HEALER) || ((c & TC_CIVILIAN) && (c & TC_WORKER)));
		}
	}
	CHECK(house_units > 0 && house_others == 0, "%d of %d units in Gaia houses neither civilians nor healers", house_others, house_units);
	// The soldiers on the ground (not the outposts' turreted slingers).
	std::vector<uint32_t> inside;
	for (const Simulation::Building &b : s.buildings_) inside.insert(inside.end(), b.garrisoned_units.begin(), b.garrisoned_units.end());
	int defenders = 0, defensive = 0;
	for (size_t i = 0; i < s.units_.count; ++i) {
		if (!s.units_.alive[i] || s.units_.team[i] != GAIA_PLAYER || s.units_.tmpl[i] >= CIV_UNIT_TEMPLATE_COUNT) continue;
		if (std::find(inside.begin(), inside.end(), s.units_.id[i]) != inside.end()) continue;
		if (!(civ_unit_template(s.units_.tmpl[i]).tech_classes & TC_SOLDIER)) continue;
		++defenders;
		defensive += s.units_.stance[i] == STANCE_DEFENSIVE;
	}
	CHECK(defenders >= 2 * 21 && defensive == defenders, "%d of %d village soldiers defensive", defensive, defenders);

	// FillShips at 4-5 minutes: min(30, 2 per minute) aboard each ship that has room.
	run_to(s, 301.0f);
	std::vector<uint32_t> aboard;
	for (const TriggerWatch &w : s.triggers_.watched)
		if (w.role >= 2)
			if (const auto *p = s.get_transport_passengers(w.id)) aboard.insert(aboard.end(), p->begin(), p->end());
	CHECK(!aboard.empty(), "no soldiers aboard the Gaia ships at 5 minutes");
	// UngarrisonShipsOrder at 5-7 minutes, the villages' first waves at 6-8.
	run_to(s, 600.0f);
	int ashore = 0, patrols = 0;
	for (uint32_t id : aboard) {
		const int32_t u = s.lookup_unit(id);
		ashore += u >= 0 && s.units_.alive[u];
	}
	for (size_t i = 0; i < s.units_.count; ++i)
		patrols += s.units_.alive[i] && s.units_.team[i] == GAIA_PLAYER && s.units_.order_kind[i] == ORDER_PATROL;
	CHECK(ashore > 0, "none of the %zu soldiers aboard came ashore by 10 minutes", aboard.size());
	CHECK(patrols > 0, "no Gaia soldiers patrolling by 10 minutes");
	if (failures == before)
		std::printf("Danubius: 2 villages (%d houses, %d temples, %d towers), %d holders garrisoned, %d defenders, %zu aboard, %d ashore, %d patrolling: ok\n",
		            houses, temples, towers, holders, defenders, aboard.size(), ashore, patrols);
}

static void check_jebel_barkal() {
	// jebel_barkal.js / jebel_barkal_triggers.js on a 256-tile map: Napata walled round the
	// foot of the hill, its districts filled with Kushite structures, archers on the hilltop,
	// Gaia patrols through the city from the start and attack waves from 5-7 minutes.
	const int before = failures;
	static Simulation s;
	build(s, MAP_JEBEL_BARKAL);
	int wonders = 0, temples = 0, ccs = 0, forts = 0, houses = 0, capturable_houses = 0, walls = 0, gates = 0;
	for (const Simulation::Building &b : s.buildings_) {
		if (b.team != GAIA_PLAYER) continue;
		wonders += b.type == BLD_WONDER;
		temples += b.type == BLD_TEMPLE || b.type == BLD_TEMPLE_AMUN;
		ccs += b.type == BLD_TOWN_CENTER;
		forts += b.type == BLD_FORTRESS;
		houses += b.type == BLD_HOUSE;
		capturable_houses += b.type == BLD_HOUSE && b.max_capture_points > 0.0f;
		walls += b.type == BLD_WALL_SEGMENT || b.type == BLD_WALL_TOWER;
		gates += b.type == BLD_GATE;
	}
	CHECK(wonders == 1 && temples >= 3, "Napata: %d wonders, %d temples", wonders, temples);
	CHECK(ccs >= 1 && forts >= 1 && houses >= 50, "Napata districts: %d civic centres, %d fortresses, %d houses", ccs, forts, houses);
	CHECK(capturable_houses == 0, "%d Napata houses capturable", capturable_houses);
	CHECK(walls > 0 && gates > 0, "Napata wall: %d pieces, %d gates", walls, gates);
	int refs[256] = {};
	for (const TriggerPoint &p : s.triggers_.points) ++refs[p.ref];
	CHECK(refs['A'] > 0 && refs['B'] > 0, "trigger points A %d, B %d", refs['A'], refs['B']);

	run_to(s, 1.0f);
	int spawns = 0, patrols = 0;
	for (const TriggerWatch &w : s.triggers_.watched) {
		spawns += w.role == 1;
		patrols += w.role == 2;
	}
	CHECK(spawns > 0 && s.triggers_.vars[0] == spawns, "%d spawn points, %d recorded", spawns, (int)s.triggers_.vars[0]);
	CHECK(patrols > 0, "no city patrol at the start");
	int holders = 0, garrisoned = 0;
	for (const Simulation::Building &b : s.buildings_) {
		if (b.team != GAIA_PLAYER || s.get_garrison_capacity(b.id) <= 0) continue;
		++holders;
		garrisoned += !b.garrisoned_units.empty();
	}
	CHECK(holders > 0 && garrisoned > 0, "%d of %d Gaia structures garrisoned", garrisoned, holders);
	// The Grand Temples of Amun are Temples and the Noba/Blemmye camps Embassies to the script:
	// each holds its champions (or infantry) and is a spawn point.
	int amun = 0, amun_full = 0, amun_spawns = 0;
	for (const Simulation::Building &b : s.buildings_) {
		if (b.team != GAIA_PLAYER || b.type != BLD_TEMPLE_AMUN) continue;
		++amun;
		amun_full += (int)b.garrisoned_units.size() == s.get_garrison_capacity(b.id);
		for (const TriggerWatch &w : s.triggers_.watched) amun_spawns += w.role == 1 && w.id == b.id;
	}
	CHECK(amun > 0 && amun_full == amun && amun_spawns == amun, "%d Temples of Amun: %d full, %d spawn points", amun, amun_full, amun_spawns);
	// The defenders placed with the map (the garrisons and patrols come after the stance is set).
	std::vector<uint32_t> inside;
	for (const Simulation::Building &b : s.buildings_) inside.insert(inside.end(), b.garrisoned_units.begin(), b.garrisoned_units.end());
	int soldiers = 0, defensive = 0;
	for (size_t i = 0; i < s.units_.count; ++i) {
		if (!s.units_.alive[i] || s.units_.team[i] != GAIA_PLAYER || s.units_.tmpl[i] >= CIV_UNIT_TEMPLATE_COUNT) continue;
		if (!(civ_unit_template(s.units_.tmpl[i]).tech_classes & TC_SOLDIER)) continue;
		if (s.turret_holder_of(s.units_.id[i]) || std::find(inside.begin(), inside.end(), s.units_.id[i]) != inside.end()) continue;
		bool patrol = false;
		for (const TriggerWatch &w : s.triggers_.watched) patrol |= w.role == 2 && w.id == s.units_.id[i];
		if (patrol) continue;
		++soldiers;
		defensive += s.units_.stance[i] == STANCE_DEFENSIVE;
	}
	CHECK(soldiers > 0 && defensive == soldiers, "%d of %d Napata defenders defensive", defensive, soldiers);

	run_to(s, 299.0f);
	int early = 0;
	for (const TriggerWatch &w : s.triggers_.watched) early += w.role == 3;
	CHECK(early == 0, "%d attackers before 5 minutes", early);
	run_to(s, 421.0f);
	int attackers = 0, ordered = 0, elephants = 0, elephants_on_structures = 0, others_on_units = 0, others_patrolling = 0;
	for (const TriggerWatch &w : s.triggers_.watched) {
		if (w.role != 3) continue;
		++attackers;
		const int32_t u = s.lookup_unit(w.id);
		ordered += u >= 0 && s.units_.order_kind[u] != ORDER_NONE;
		if (u < 0 || s.units_.order_kind[u] != ORDER_PATROL) continue;
		// targetClasses: the elephants "Structure" or "Defensive SiegeEngine Monument Wonder",
		// everyone else "Unit+!Ship".
		const uint8_t classes = (s.units_.order_flags[u] & ORDER_PATROL_MASK) >> ORDER_PATROL_SHIFT;
		if (s.unit_classes_at((size_t)u) & TC_ELEPHANT) {
			++elephants;
			elephants_on_structures += classes == ORDER_PATROL_STRUCTURES || classes == ORDER_PATROL_DEFENSIVE_MONUMENTS;
		} else if (s.formation_group_.empty() || s.formation_group_[u] == 0) {
			++others_patrolling;
			others_on_units += classes == ORDER_PATROL_LAND_UNITS;
		}
	}
	CHECK(attackers > 0 && ordered > 0, "first wave by 7 minutes: %d attackers, %d under orders", attackers, ordered);
	CHECK(elephants_on_structures == elephants, "%d of %d patrolling elephants after structures", elephants_on_structures, elephants);
	CHECK(others_on_units == others_patrolling, "%d of %d other patrolling attackers after units but ships", others_on_units, others_patrolling);
	if (failures == before)
		std::printf("Jebel Barkal: Napata (%d houses, %d CCs, %d fortresses, %d wall pieces, %d gates), %d spawn points, %d patrolling, %d in the first wave (%d elephants after structures): ok\n",
		            houses, ccs, forts, walls, gates, spawns, patrols, attackers, elephants);

	// Game Setup Trigger Difficulty (getDifficulty()): Very Easy builds Napata with no wall
	// and fewer defenders, Very Hard with the Kushite wall and more; the triggers get it.
	auto gaia_soldiers = [](const Simulation &sim) {
		int n = 0;
		for (size_t i = 0; i < sim.units_.count; ++i)
			n += sim.units_.alive[i] && sim.units_.team[i] == GAIA_PLAYER && sim.units_.tmpl[i] < CIV_UNIT_TEMPLATE_COUNT &&
			     (civ_unit_template(sim.units_.tmpl[i]).tech_classes & TC_SOLDIER);
		return n;
	};
	auto wall_pieces = [](const Simulation &sim) {
		int n = 0;
		for (const Simulation::Building &b : sim.buildings_)
			n += b.team == GAIA_PLAYER && (b.type == BLD_WALL_SEGMENT || b.type == BLD_WALL_TOWER || b.type == BLD_GATE || b.type == BLD_PALISADE);
		return n;
	};
	const int before_diff = failures;
	s.set_trigger_difficulty(1);
	build(s, MAP_JEBEL_BARKAL);
	const int easy_soldiers = gaia_soldiers(s), easy_walls = wall_pieces(s);
	CHECK(s.trigger_state().difficulty == 1 && easy_walls == 0, "Very Easy: difficulty %d, %d wall pieces", (int)s.trigger_state().difficulty, easy_walls);
	s.set_trigger_difficulty(5);
	build(s, MAP_JEBEL_BARKAL);
	const int hard_soldiers = gaia_soldiers(s), hard_walls = wall_pieces(s);
	CHECK(s.trigger_state().difficulty == 5 && hard_walls > 0, "Very Hard: difficulty %d, %d wall pieces", (int)s.trigger_state().difficulty, hard_walls);
	CHECK(hard_soldiers > easy_soldiers, "Napata defenders: %d on Very Easy, %d on Very Hard", easy_soldiers, hard_soldiers);
	s.set_trigger_difficulty(3);
	if (failures == before_diff)
		std::printf("Jebel Barkal Trigger Difficulty: Very Easy %d defenders and no wall, Very Hard %d defenders and %d wall pieces: ok\n",
		            easy_soldiers, hard_soldiers, hard_walls);
}

static void check_disabled_buildings_128() {
	// disabled_buildings holds every BuildingType id, past 64 too, through a save.
	const int before = failures;
	static Simulation a, b;
	build(a, MAP_SURVIVAL_OF_THE_FITTEST);
	run_to(a, 2.0f);
	a.triggers_.disabled_buildings[2] |= bld_bit(100);
	a.triggers_.disabled_buildings[2] |= bld_bit(65);
	CHECK(a.triggers_.building_disabled(2, 100) && a.triggers_.building_disabled(2, 65) && !a.triggers_.building_disabled(2, 64),
	      "ids 65 and 100 disabled, 64 not");
	const char *path = "/tmp/rts0_test_triggers_mask.sav";
	CHECK(a.save_state(path), "save");
	build(b, MAP_SURVIVAL_OF_THE_FITTEST);
	CHECK(b.load_state(path), "load");
	CHECK(b.triggers_.building_disabled(2, 100) && b.triggers_.building_disabled(2, 65) && !b.triggers_.building_disabled(2, 64),
	      "ids past 64 lost across a save");
	CHECK(b.trigger_hash() == a.trigger_hash(), "trigger state differs after loading");
	if (failures == before) std::printf("Disabled buildings past id 64 survive a save: ok\n");
}

static void teleport(Simulation &s, uint32_t id, float x, float z) {
	const int32_t u = s.lookup_unit(id);
	if (u >= 0) { s.units_.pos_x[u] = x; s.units_.pos_z[u] = z; }
}

static void check_attack_detected() {
	const int before = failures;
	static Simulation a, b;
	a.reset();
	a.set_victory_condition(VICTORY_ENDLESS);
	a.triggers_.register_trigger(TRIGGER_ON_ATTACK_DETECTED, TRIGGER_ACTION_TRIGGERS_DEMO_LOG, true);
	const uint32_t mine = a.spawn_unit(0.0f, 0.0f, 0, UNIT_SPEARMAN);
	const uint32_t far_one = a.spawn_unit(50.0f, 0.0f, 0, UNIT_SPEARMAN);
	const uint32_t enemy = a.spawn_unit(60.0f, 60.0f, 1, UNIT_SPEARMAN);
	run_to(a, 0.5f);
	auto calls = [](const Simulation &s) { return s.trigger_event_calls(TRIGGER_ON_ATTACK_DETECTED); };
	a.note_attacked(enemy, 1, mine, 0, false, 1.0f);
	a.note_attacked(enemy, 1, mine, 0, false, 1.0f);
	a.note_attacked(mine, 0, enemy, 1, false, 1.0f);   // player 2's alert
	a.note_attacked(mine, 0, far_one, 0, false, 1.0f); // dealt by myself: not registered
	CHECK(calls(a) == 0, "raised at the end of the tick, not mid-combat");
	run_to(a, 1.0f);
	CHECK(calls(a) == 2, "one alert each for players 1 and 2, got %u", calls(a));
	a.note_attacked(enemy, 1, far_one, 0, false, 1.0f); // 200 m away: its own alert
	run_to(a, 1.5f);
	CHECK(calls(a) == 3, "an attack outside SuppressionRange is detected, got %u", calls(a));
	const char *path = "/tmp/rts0_test_triggers_attack.sav";
	CHECK(a.save_state(path), "save");
	b.reset();
	CHECK(b.load_state(path), "load");
	CHECK(b.triggers_.attack_suppressed.size() == 3 && b.trigger_hash() == a.trigger_hash(), "the suppressed attacks survive a save");
	for (Simulation *s : {&a, &b}) {
		s->note_attacked(enemy, 1, mine, 0, false, 1.0f);
		run_to(*s, 30.0f);
	}
	CHECK(calls(a) == 3 && calls(b) == 0, "still suppressed 30 s on, before and after loading: %u/%u", calls(a), calls(b));
	for (Simulation *s : {&a, &b}) {
		run_to(*s, 62.0f);
		s->note_attacked(enemy, 1, mine, 0, false, 1.0f);
		run_to(*s, 62.5f);
	}
	CHECK(calls(a) == 4 && calls(b) == 1, "detected again once SuppressionTime is up: %u/%u", calls(a), calls(b));
	CHECK(b.trigger_hash() == a.trigger_hash(), "loaded copy out of sync");
	// Livestock: a low-priority alert that a regular one nearby overrides.
	TriggerState t;
	CHECK(t.attack_alert(0, 0.0f, 0.0f, true), "a sheep attacked: detected");
	CHECK(!t.attack_alert(0, 5.0f, 0.0f, true), "a second sheep nearby: suppressed");
	CHECK(t.attack_alert(0, 5.0f, 0.0f, false), "a soldier nearby: priority increased, detected");
	CHECK(!t.attack_alert(0, 6.0f, 0.0f, true), "then a sheep again: suppressed");
	CHECK(t.attack_suppressed.size() == 1, "the upgraded alert replaced the sheep's");
	if (failures == before) std::printf("OnAttackDetected: AttackDetection suppression per player, livestock priority, saved: ok\n");
}

static void check_on_range() {
	const int before = failures;
	static Simulation a, b;
	a.reset();
	a.set_victory_condition(VICTORY_ENDLESS);
	a.triggers_.add_point('A', 0.0f, 0.0f);
	a.triggers_.add_point('B', 60.0f, 60.0f);
	// triggers_demo.js: player 1's units (UnitAI) within 40 of the A points.
	const uint16_t t = a.triggers_.register_range_trigger(TRIGGER_ACTION_TRIGGERS_DEMO_RANGE, 'A', 0.0f, 10.0f, 1u << 0, true, true);
	CHECK(a.triggers_.range_queries.size() == 1, "one query for the one A point, got %zu", a.triggers_.range_queries.size());
	const uint32_t u1 = a.spawn_unit(30.0f, 0.0f, 0, UNIT_SPEARMAN);
	a.spawn_unit(2.0f, 0.0f, 1, UNIT_SPEARMAN); // player 2's: not counted
	run_to(a, 0.5f);
	const auto &v = a.triggers_.vars;
	CHECK(v[0] == 0 && v[1] == 0, "nothing in range yet: %d added, %d removed", v[0], v[1]);
	teleport(a, u1, 3.0f, 0.0f);
	run_to(a, 1.0f);
	CHECK(v[0] == 1 && v[2] == 1, "the unit came into range: %d added, %d in range", v[0], v[2]);
	const uint32_t u3 = a.spawn_unit(-4.0f, 4.0f, 0, UNIT_SPEARMAN);
	run_to(a, 1.5f);
	CHECK(v[0] == 2 && v[2] == 2, "a second unit: %d added, %d in range", v[0], v[2]);
	const auto &col = a.triggers_.range_queries[0].collection;
	CHECK(col.size() == 2 && col[0] == u1 && col[1] == u3, "the collection keeps the order they came in");
	const char *path = "/tmp/rts0_test_triggers_range.sav";
	CHECK(a.save_state(path), "save");
	b.reset();
	CHECK(b.load_state(path), "load");
	CHECK(b.trigger_hash() == a.trigger_hash(), "trigger state differs after loading");
	CHECK(b.triggers_.range_queries.size() == 1 && b.triggers_.range_queries[0].collection == col, "the collection survives a save");
	for (Simulation *s : {&a, &b}) {
		teleport(*s, u1, 40.0f, 0.0f);
		run_to(*s, 2.0f);
	}
	CHECK(v[1] == 1 && v[2] == 1 && b.triggers_.vars[1] == 1 && b.triggers_.vars[0] == 2,
	      "only the unit that left fires, before and after loading: %d/%d removed", v[1], b.triggers_.vars[1]);
	CHECK(b.trigger_hash() == a.trigger_hash(), "loaded copy out of sync after the unit left");
	a.triggers_.disable_trigger(t);
	teleport(a, u3, 40.0f, 40.0f);
	run_to(a, 2.5f);
	CHECK(v[1] == 1, "a disabled range trigger kept firing");
	if (failures == before) std::printf("OnRange reports units entering and leaving, through a save, and stops when disabled: ok\n");
}

static void check_engine_events() {
	// Trigger.js: OnConstructionStarted (Foundation.js Commit), OnDiplomacyChanged
	// (Diplomacy.js SetDiplomacyIndex), OnPlayerDefeated and OnPlayerWon (Player.js SetState),
	// each once per change.
	const int before = failures;
	static Simulation s;
	build(s, MAP_MAINLAND);
	for (uint8_t e : {TRIGGER_ON_CONSTRUCTION_STARTED, TRIGGER_ON_DIPLOMACY_CHANGED, TRIGGER_ON_PLAYER_DEFEATED, TRIGGER_ON_PLAYER_WON})
		s.triggers_.register_trigger(e, TRIGGER_ACTION_NONE, true);
	run_to(s, 0.5f);
	const Simulation::Building *cc = nullptr;
	std::set<uint32_t> placed; // the map's own structures, committed from the start
	for (const auto &b : s.buildings_) {
		if (b.team == 0 && is_civic_centre(b.type)) cc = &b;
		if (b.committed) placed.insert(b.id);
	}
	CHECK(cc != nullptr, "no civic centre for player 0");
	if (!cc) return;
	uint32_t builder = 0;
	for (size_t i = 0; i < s.units_.count && !builder; ++i)
		if (s.units_.alive[i] && s.units_.team[i] == 0 && unit_can_build(s.units_.unit_type[i], s.units_.tmpl[i])) builder = s.units_.id[i];
	uint32_t site = 0;
	for (int k = 0; k < 40 && !site; ++k) {
		const float a = (float)k * 0.7f, r = 14.0f + (float)(k / 8) * 3.0f;
		site = s.cmd_build(0, (int)std::floor((cc->x + std::cos(a) * r) / Simulation::GRID), (int)std::floor((cc->z + std::sin(a) * r) / Simulation::GRID), BLD_HOUSE);
	}
	CHECK(site && builder, "no house foundation (%u) or builder (%u)", site, builder);
	if (!site || !builder) return;
	CHECK(s.trigger_event_calls(TRIGGER_ON_CONSTRUCTION_STARTED) == 0, "construction started before anyone built");
	s.order_build_or_repair(&builder, 1, site, false, false);
	run_to(s, 30.0f);
	// Once per foundation that was committed (the AI builds too), the house among them.
	uint32_t committed = 0;
	bool house = false;
	for (const auto &b : s.buildings_) {
		committed += b.committed && !placed.count(b.id);
		house |= b.id == site && b.committed;
	}
	CHECK(house && s.trigger_event_calls(TRIGGER_ON_CONSTRUCTION_STARTED) == committed, "OnConstructionStarted %u times for %u committed foundations (house %d)",
	      s.trigger_event_calls(TRIGGER_ON_CONSTRUCTION_STARTED), committed, house);
	s.cmd_set_diplomacy(0, 1, Simulation::DIPLO_NEUTRAL);
	CHECK(s.trigger_event_calls(TRIGGER_ON_DIPLOMACY_CHANGED) >= 1, "OnDiplomacyChanged %u times", s.trigger_event_calls(TRIGGER_ON_DIPLOMACY_CHANGED));
	s.defeat_player(1);
	s.defeat_player(1);
	CHECK(s.trigger_event_calls(TRIGGER_ON_PLAYER_DEFEATED) == 1, "OnPlayerDefeated %u times", s.trigger_event_calls(TRIGGER_ON_PLAYER_DEFEATED));
	s.end_match(s.side_of(0));
	CHECK(s.trigger_event_calls(TRIGGER_ON_PLAYER_WON) == 1 && s.trigger_event_calls(TRIGGER_ON_PLAYER_DEFEATED) == 1,
	      "after the win: OnPlayerWon %u, OnPlayerDefeated %u", s.trigger_event_calls(TRIGGER_ON_PLAYER_WON), s.trigger_event_calls(TRIGGER_ON_PLAYER_DEFEATED));
	if (failures == before) std::printf("Trigger.js events: ConstructionStarted, DiplomacyChanged, PlayerDefeated and PlayerWon fire once each: ok\n");
}

int main() {
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	TurnManager tm;
	check_survival();
	check_save_load(tm, MAP_SURVIVAL_OF_THE_FITTEST, 200.0f, 420.0f, Simulation::WATER_LEVEL_RISING, 25, "Survival of the Fittest across the first wave");
	check_polar_sea();
	check_treasure_islands();
	check_gallic_fields();
	check_triggers_demo();
	check_pickup_test();
	check_unit_chasing();
	check_unit_dancing();
	check_unit_pushing();
	check_unit_motion();
	check_cinema_demo();
	check_save_load(tm, MAP_POLAR_SEA, 305.0f, 330.0f, Simulation::WATER_LEVEL_RISING, 25, "Polar Sea during a wolf wave");
	check_interval();
	check_on_range();
	check_attack_detected();
	check_flood();
	check_volcano();
	check_drowning();
	check_save_load(tm, MAP_FLOOD, 261.0f, 264.0f, Simulation::WATER_LEVEL_RISING, 25, "Flood mid-rise");
	check_save_load(tm, MAP_EXTINCT_VOLCANO, 90.0f, 200.0f, Simulation::WATER_LEVEL_RISING, 2, "Extinct Volcano before and during the rise");
	check_identical_saves();
	check_disabled_buildings_128();
	check_tutorials();
	check_engine_events();
	check_death_canyon();
	check_elephantine();
	check_danubius();
	check_save_load(tm, MAP_DANUBIUS, 290.0f, 400.0f, Simulation::WATER_LEVEL_RISING, 25, "Danubius with soldiers aboard and ships unloading");
	check_jebel_barkal();
	check_save_load(tm, MAP_JEBEL_BARKAL, 280.0f, 430.0f, Simulation::WATER_LEVEL_RISING, 25, "Jebel Barkal across the first attack");
	std::printf("RESULT: %s (%d failed checks)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
