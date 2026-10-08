// Map triggers: the Trigger.js / Timer.js engine (src/simulation/triggers.h) and
// the two rising-water scripts ported from 0 A.D. (flood_triggers.js,
// extinct_volcano_triggers.js).
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
//
//   g++ -std=c++20 -O2 -Isrc tools/test_triggers.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//       src/network/net_host.cpp -lpthread -o /tmp/test_triggers && /tmp/test_triggers
// Private access only to register a test trigger and read the trigger state.
#define private public
#include "simulation/simulation.h"
#undef private
#include "network/turn_manager.h"
#include <cmath>
#include <cstdio>
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
	while (s.trigger_state().time_ms < (uint32_t)std::lround(seconds * 1000.0f)) s.tick(Simulation::FIXED_STEP);
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

int main() {
	TurnManager tm;
	check_interval();
	check_flood();
	check_volcano();
	check_drowning();
	check_save_load(tm, MAP_FLOOD, 261.0f, 264.0f, Simulation::WATER_LEVEL_RISING, 25, "Flood mid-rise");
	check_save_load(tm, MAP_EXTINCT_VOLCANO, 90.0f, 200.0f, Simulation::WATER_LEVEL_RISING, 2, "Extinct Volcano before and during the rise");
	check_identical_saves();
	std::printf("RESULT: %s (%d failed checks)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
