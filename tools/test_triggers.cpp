// Map triggers: the Trigger.js / Timer.js engine (src/simulation/triggers.h) and
// the scripts ported from 0 A.D. (flood_triggers.js, extinct_volcano_triggers.js,
// polar_sea_triggers.js, scenarios/treasure_islands.js).
//
// Checks
//   1. OnInterval triggers fire on time and stop when disabled (Timer.js).
//   2. Flood, Rising: warning before each rise, half a 0 A.D. metre per 2.4 s
//      step from 260 s, a 2.5 m rise in all by 1580 s; Shallow never rises;
//      Deep starts a metre up.
//   3. Extinct Volcano: Gaia towers start garrisoned, the warning comes a minute
//      before the Game Setup rise time, then the water climbs a metre at a time.
//   4. Rising water drowns units, animals, structures and resources, but not
//      ships or siege.
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
//   g++ -std=c++20 -O2 -Isrc tools/test_triggers.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//       src/network/net_host.cpp -lpthread -o /tmp/test_triggers && /tmp/test_triggers
// Private access only to register a test trigger and read the trigger state.
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
// rms_generator.cpp kTriggerMapHeight: the rising-water maps' heights are 0 A.D. metres x 0.5.
static constexpr float M = 0.5f;

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

static void check_interval() {
	static Simulation s;
	s.reset();
	// OnInterval every second after half a second: fires at 0.5, 1.5, 2.5 s.
	const uint16_t t = s.triggers_.register_trigger(TRIGGER_ON_INTERVAL, TRIGGER_ACTION_FLOOD_WARNING, true, 1000, 500);
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
	run_to(s, 1500.0f);
	CHECK(near(s.water_height(), h0 + 1.0f * M), "flood water moved between rises (%.3f)", s.water_height());
	run_to(s, 1580.0f);
	CHECK(near(s.water_height(), h0 + 5.0f * M), "second flood rise: %.3f, expected %.3f", s.water_height(), h0 + 5.0f * M);
	CHECK(s.trigger_state().timers.empty(), "flood left %zu timers after the last rise", s.trigger_state().timers.size());

	static Simulation shallow, deep;
	build(shallow, MAP_FLOOD, Simulation::WATER_LEVEL_SHALLOW);
	build(deep, MAP_FLOOD, Simulation::WATER_LEVEL_DEEP);
	run_to(shallow, 300.0f);
	run_to(deep, 1.0f);
	CHECK(near(shallow.water_height(), h0), "shallow flood rose to %.3f", shallow.water_height());
	CHECK(near(deep.water_height(), h0 + 1.0f * M), "deep flood at %.3f, expected %.3f", deep.water_height(), h0 + 1.0f * M);
	if (failures == before) std::printf("Flood: warning, 2.4 s steps from 260 s, +2.5 m by 1580 s; shallow and deep: ok\n");
}

static void check_volcano() {
	const int before = failures;
	static Simulation s;
	build(s, MAP_EXTINCT_VOLCANO, Simulation::WATER_LEVEL_RISING, 2);
	const float h0 = s.water_height();
	run_to(s, 1.0f);
	int towers = 0, full = 0;
	for (const auto &b : s.buildings()) {
		if (b.team != GAIA_PLAYER || b.type != BLD_TOWER || b.wrecked) continue;
		++towers;
		const int cap = s.get_garrison_capacity(b.id);
		if (cap > 0 && s.get_garrison_count(b.id) == cap) ++full;
	}
	CHECK(towers > 0, "Extinct Volcano has no Gaia towers");
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
	CHECK(ship_ok && ram_ok, "water sank a ship (%d) or a ram (%d)", ship_ok, ram_ok);
	CHECK(tc_wrecked, "Town Center not lost under water");
	run_to(s, 2.0f);
	if (failures == before) std::printf("Rising water drowns %d units and the Town Center; the ship and ram float: ok\n", organic);
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
	collected(s, 0, 15); // 16: three short of 19
	CHECK(shown(s, 1, "Hurry up!") && shown(s, 0, "Treasures remaining to collect for victory:  3!"), "no near-victory messages at 16");
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
		s.units_.status_effects[u] |= EFFECT_POISON;
		s.units_.poison_timer[u] = 5.0f;
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
	tut_cmd(tm, s, 4, fruit);
	tut_cmd(tm, s, 4, tree);
	CHECK(s.tutorial_goal_index() == 3, "gather goals: at %d, want 3", s.tutorial_goal_index());
	tut_queued(s, 1, "spart/infantry_spearman_b", 5); // the enemy's training is not the player's
	CHECK(s.tutorial_goal_index() == 3, "the enemy's training advanced the goal");
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
	tut_queued(e, 0, "athen/support_female_citizen", 5);
	CHECK(e.tutorial_goal_index() == 13, "civilians trained before the rally point went: at %d", e.tutorial_goal_index());
	fire(TRIGGER_ON_PLAYER_COMMAND, TRIGGER_CMD_UNSET_RALLY);
	CHECK(e.tutorial_goal_index() == 14, "rally point removed: at %d", e.tutorial_goal_index());
	tut_queued(e, 0, "athen/support_female_citizen", 1);
	CHECK(e.tutorial_warning() == TUTORIAL_WARN_HOLD_SHIFT_FEMALE, "single civilian: warning %d", e.tutorial_warning());
	tut_queued(e, 0, "athen/support_female_citizen", 5);
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
	const uint16_t champion = civ_unit_template_by_id("rome/champion_infantry_swordsman");
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
	const int alive = romans(nullptr);
	run_to(s, 60.0f + (float)wait / 1000.0f + 0.1f);
	CHECK(romans(nullptr) >= 2 * size && romans(nullptr) <= alive + 2 * size, "second wave: %d Romans, want %d more", romans(nullptr), 2 * size);
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
		const uint64_t c = u >= 0 && s.units_.tmpl[u] < CIV_UNIT_TEMPLATE_COUNT ? civ_unit_template(s.units_.tmpl[u]).tech_classes : 0;
		odd += !((c & TC_HEALER) || ((c & TC_CITIZENSOLDIER) && (c & TC_INFANTRY)) || ((c & TC_CIVILIAN) && (c & TC_WORKER)));
	}
	CHECK(odd == 0, "%d of %zu garrisoned Kushites neither infantry, civilians nor healers", odd, inside.size());
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
	int ccs = 0, houses = 0, temples = 0, towers = 0, walls = 0, capturable = 0;
	bool left = false, right = false;
	for (const Simulation::Building &b : s.buildings_) {
		if (b.team != GAIA_PLAYER) continue;
		if (b.type == BLD_TOWN_CENTER) {
			++ccs;
			(s.danubius_is_left(b.x, b.z) ? left : right) = true;
			continue;
		}
		houses += b.type == BLD_HOUSE;
		temples += b.type == BLD_TEMPLE;
		const bool tower = b.type == BLD_SENTRY_TOWER;
		towers += tower;
		walls += b.type == BLD_PALISADE || b.type == BLD_WALL_TOWER;
		if (b.type == BLD_HOUSE || b.type == BLD_TEMPLE || tower) capturable += b.max_capture_points > 0.0f;
	}
	CHECK(ccs == 2 && left && right, "%d Gaia civic centres, one on each bank: %d %d", ccs, left, right);
	CHECK(houses >= 14 && temples >= 3 && towers >= 6, "villages: %d houses, %d temples, %d watchtowers", houses, temples, towers);
	CHECK(capturable == 0, "%d village structures capturable", capturable);
	CHECK(walls > 0, "no palisade round the villages");
	int refs[256] = {};
	for (const TriggerPoint &p : s.triggers_.points) ++refs[p.ref];
	for (char r : {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'}) CHECK(refs[(int)r] > 0, "no trigger point %c", r);
	CHECK(refs['I'] == 1 && refs['C'] <= 200 && refs['D'] <= 200, "I %d, C %d, D %d", refs['I'], refs['C'], refs['D']);
	for (const TriggerPoint &p : s.triggers_.points)
		if (p.ref == 'C' || p.ref == 'D') CHECK(s.danubius_is_left(p.x, p.z) == (p.ref == 'C'), "unload point %c on the wrong bank", p.ref);

	// UngarrisonShipsOrder needs a player's Defensive structure on a bank; give player 1 a
	// sentry tower so the landing doesn't hang on when its Petra builds its first.
	for (const Simulation::Building &b : std::vector<Simulation::Building>(s.buildings_))
		if (b.team == 1 && b.type == BLD_TOWN_CENTER) s.spawn_building_world(b.x * 0.9f, b.z * 0.9f, BLD_SENTRY_TOWER, 1);
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
			const uint64_t c = u >= 0 && s.units_.tmpl[u] < CIV_UNIT_TEMPLATE_COUNT ? civ_unit_template(s.units_.tmpl[u]).tech_classes : 0;
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
	int attackers = 0, ordered = 0;
	for (const TriggerWatch &w : s.triggers_.watched) {
		if (w.role != 3) continue;
		++attackers;
		const int32_t u = s.lookup_unit(w.id);
		ordered += u >= 0 && s.units_.order_kind[u] != ORDER_NONE;
	}
	CHECK(attackers > 0 && ordered > 0, "first wave by 7 minutes: %d attackers, %d under orders", attackers, ordered);
	if (failures == before)
		std::printf("Jebel Barkal: Napata (%d houses, %d CCs, %d fortresses, %d wall pieces, %d gates), %d spawn points, %d patrolling, %d in the first wave: ok\n",
		            houses, ccs, forts, walls, gates, spawns, patrols, attackers);

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

int main() {
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	TurnManager tm;
	check_survival();
	check_save_load(tm, MAP_SURVIVAL_OF_THE_FITTEST, 200.0f, 420.0f, Simulation::WATER_LEVEL_RISING, 25, "Survival of the Fittest across the first wave");
	check_polar_sea();
	check_treasure_islands();
	check_gallic_fields();
	check_save_load(tm, MAP_POLAR_SEA, 305.0f, 330.0f, Simulation::WATER_LEVEL_RISING, 25, "Polar Sea during a wolf wave");
	check_interval();
	check_flood();
	check_volcano();
	check_drowning();
	check_save_load(tm, MAP_FLOOD, 261.0f, 264.0f, Simulation::WATER_LEVEL_RISING, 25, "Flood mid-rise");
	check_save_load(tm, MAP_EXTINCT_VOLCANO, 90.0f, 200.0f, Simulation::WATER_LEVEL_RISING, 2, "Extinct Volcano before and during the rise");
	check_identical_saves();
	check_disabled_buildings_128();
	check_tutorials();
	check_death_canyon();
	check_elephantine();
	check_danubius();
	check_save_load(tm, MAP_DANUBIUS, 290.0f, 400.0f, Simulation::WATER_LEVEL_RISING, 25, "Danubius with soldiers aboard and ships unloading");
	check_jebel_barkal();
	check_save_load(tm, MAP_JEBEL_BARKAL, 280.0f, 430.0f, Simulation::WATER_LEVEL_RISING, 25, "Jebel Barkal across the first attack");
	std::printf("RESULT: %s (%d failed checks)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
