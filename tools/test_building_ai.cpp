// 0 A.D. BuildingAI.js: structure arrows. DefaultArrowCount + garrisoned
// GarrisonArrowClasses x GarrisonArrowMultiplier (capped at MaxArrowCount) per
// Attack/Ranged/RepeatTime, fired over ten rounds after PrepareTime, each at a random
// target weighted towards PreferredClasses (Human), with the template's damage and range.
//   g++ -std=c++20 -O2 -Isrc tools/test_building_ai.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//       src/network/net_host.cpp -lpthread -o /tmp/test_building_ai && /tmp/test_building_ai
#include "simulation/simulation.h"
#include <cmath>
#include <cstdio>
#include <set>

using namespace rtso;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

static Simulation::Building *find_bld(Simulation &s, uint32_t id) {
	for (const auto &b : s.buildings())
		if (b.id == id) return const_cast<Simulation::Building *>(&b);
	return nullptr;
}

static int32_t idx_of(const Simulation &s, uint32_t id) {
	auto uv = s.units();
	for (size_t i = 0; i < uv.count; ++i) if (uv.id[i] == id) return (int32_t)i;
	return -1;
}

// Arrows a structure launched, counted as new projectiles with it as their source.
struct ArrowCounter {
	std::set<uint32_t> seen;
	std::vector<Projectile> fired;
	int step(Simulation &s, uint32_t bld) {
		int n = 0;
		for (const Projectile &p : s.projectiles())
			if (p.source_id == bld && seen.insert(p.id).second) { fired.push_back(p); ++n; }
		return n;
	}
};

static void fresh(Simulation &s, uint32_t seed = 7) {
	s.set_sim_seed(seed);
	s.reset();
	s.set_player_resources(0, 5000, 5000, 5000, 5000);
}

// A foe that stays put and never fights back from where it stands.
static uint32_t foe(Simulation &s, float x, float z, UnitType t = UNIT_SPEARMAN) {
	uint32_t u = s.spawn_unit(x, z, 1, t, 100000.0f);
	s.order_set_stance(&u, 1, STANCE_STAND_GROUND);
	s.cmd_stop(&u, 1);
	return u;
}

static void garrison(Simulation &s, uint32_t *ids, size_t n, uint32_t bld) {
	s.order_garrison(ids, n, bld);
	for (int t = 0; t < 900 && s.get_garrison_count(bld) < (int)n; ++t) s.tick(0.1f);
}

int main() {
	// 1. Data: template_structure_civic_civil_centre.xml (6 arrows, Soldier, 8 pierce, 60 m,
	// 4 s), defensive_tower_stone (2 + 2, Infantry, MinRange 10, Origin 15), sentry (2, max 6),
	// fortress (10, 10 pierce), rome/army_camp (5, max 18); walls and docks have no BuildingAI.
	{
		const BuildingAIInfo &cc = building_ai_info(BLD_TOWN_CENTER);
		CHECK(cc.default_arrows == 6 && cc.max_arrows == 0 && cc.garrison_arrow_multiplier == 1.0f);
		CHECK(cc.garrison_arrow_classes == TC_SOLDIER && cc.pierce == 8.0f && cc.hack == 0.0f);
		CHECK(near(cc.max_range, 60.0f * COMBAT_DISTANCE_SCALE) && cc.min_range == 0.0f);
		CHECK(cc.repeat_time == 4.0f && near(cc.prepare_time, 0.4f) && cc.prefers_human);
		const BuildingAIInfo &tw = building_ai_info(BLD_TOWER);
		CHECK(tw.default_arrows == 4 && tw.garrison_arrow_classes == TC_INFANTRY && tw.repeat_time == 3.5f);
		CHECK(near(tw.min_range, 10.0f * COMBAT_DISTANCE_SCALE) && near(tw.origin_y, 15.0f * COMBAT_DISTANCE_SCALE));
		CHECK(building_ai_info(BLD_SENTRY_TOWER).default_arrows == 2 && building_ai_info(BLD_SENTRY_TOWER).max_arrows == 6);
		CHECK(building_ai_info(BLD_FORTRESS).default_arrows == 10 && building_ai_info(BLD_FORTRESS).pierce == 10.0f);
		CHECK(building_ai_info(BLD_ARMY_CAMP).default_arrows == 5 && building_ai_info(BLD_ARMY_CAMP).max_arrows == 18);
		CHECK(!building_ai_info(BLD_WALL_SEGMENT).shoots() && !building_ai_info(BLD_WALL_TOWER).shoots());
		CHECK(!building_ai_info(BLD_DOCK).shoots() && !building_ai_info(BLD_BARRACKS).shoots());
	}

	static Simulation s;

	// 2. GetArrowCount: soldiers in a civic centre add one each, women none; a sentry
	// tower stops at MaxArrowCount; a tower counts Infantry only.
	{
		fresh(s);
		uint32_t cc = s.spawn_building_world(-40.0f, -40.0f, BLD_TOWN_CENTER, 0, 1.0f);
		CHECK(s.building_arrow_count(*find_bld(s, cc)) == 6);
		uint32_t g[4] = {s.spawn_unit(-30.0f, -30.0f, 0, UNIT_SPEARMAN), s.spawn_unit(-30.0f, -31.0f, 0, UNIT_SPEARMAN),
				s.spawn_unit(-30.0f, -32.0f, 0, UNIT_CAVALRY), s.spawn_unit(-30.0f, -33.0f, 0, UNIT_VILLAGER)};
		garrison(s, g, 4, cc);
		CHECK(s.get_garrison_count(cc) == 4);
		std::printf("civic centre with 2 spearmen, a horseman and a woman: %d arrows\n", s.building_arrow_count(*find_bld(s, cc)));
		CHECK(s.building_arrow_count(*find_bld(s, cc)) == 9);

		uint32_t st = s.spawn_building_world(-20.0f, -30.0f, BLD_SENTRY_TOWER, 0, 1.0f);
		uint32_t inf[3] = {s.spawn_unit(-18.0f, -26.0f, 0, UNIT_ARCHER), s.spawn_unit(-18.0f, -27.0f, 0, UNIT_ARCHER),
				s.spawn_unit(-18.0f, -28.0f, 0, UNIT_SPEARMAN)};
		garrison(s, inf, 3, st);
		CHECK(s.get_garrison_count(st) == 3);
		CHECK(s.building_arrow_count(*find_bld(s, st)) == 5);

		uint32_t tw = s.spawn_building_world(-20.0f, -50.0f, BLD_TOWER, 0, 1.0f);
		uint32_t mix[2] = {s.spawn_unit(-18.0f, -46.0f, 0, UNIT_SPEARMAN), s.spawn_unit(-18.0f, -47.0f, 0, UNIT_CAVALRY)};
		garrison(s, mix, 2, tw);
		CHECK(s.get_garrison_count(tw) == 2);
		CHECK(s.building_arrow_count(*find_bld(s, tw)) == 5);
	}

	// 3. FireArrows: nothing before PrepareTime, then exactly GetArrowCount() arrows per
	// RepeatTime spread over the rounds, each doing 8 pierce through the target's armour.
	{
		fresh(s);
		uint32_t cc = s.spawn_building_world(-40.0f, -40.0f, BLD_TOWN_CENTER, 0, 1.0f);
		const float half = find_bld(s, cc)->half_size;
		uint32_t f = foe(s, -40.0f + half + 8.0f, -40.0f);
		ArrowCounter arrows;
		s.tick(Simulation::FIXED_STEP); // the range query sees the foe and sets the timer
		int early = 0;
		for (int t = 0; t < 7; ++t) { s.tick(Simulation::FIXED_STEP); early += arrows.step(s, cc); }
		CHECK(early == 0); // 0.35 s < PrepareTime 0.4 s
		std::vector<int> per_round;
		int total = 0;
		for (int t = 0; t < (int)std::lround(12.0f / Simulation::FIXED_STEP); ++t) {
			s.tick(Simulation::FIXED_STEP);
			const int k = arrows.step(s, cc);
			if (k) per_round.push_back(k);
			total += k;
		}
		std::printf("civic centre, 12 s on one spearman: %d arrows in %zu rounds\n", total, per_round.size());
		CHECK(total == 18); // three RepeatTimes of 6
		CHECK(per_round.size() >= 3);
		int32_t fi = idx_of(s, f);
		float armor[3];
		s.unit_resistance((size_t)fi, armor);
		const float want = 8.0f * std::pow(0.9f, armor[1]);
		bool damage_ok = !arrows.fired.empty();
		for (const Projectile &p : arrows.fired) damage_ok = damage_ok && near(p.damage, want) && p.target_unit_id == s.units().id[fi];
		CHECK(damage_ok);
	}

	// 4. Range: nothing beyond MaxRange from the edge; a tower ignores a foe inside its
	// MinRange (10 m) but shoots one beyond it.
	{
		fresh(s);
		uint32_t cc = s.spawn_building_world(-40.0f, -40.0f, BLD_TOWN_CENTER, 0, 1.0f);
		const float half = find_bld(s, cc)->half_size;
		foe(s, -40.0f + half + building_ai_info(BLD_TOWN_CENTER).max_range + 3.0f, -40.0f);
		ArrowCounter a;
		int n = 0;
		for (int t = 0; t < 120; ++t) { s.tick(Simulation::FIXED_STEP); n += a.step(s, cc); }
		CHECK(n == 0);

		fresh(s);
		s.spawn_building_world(-40.0f, -40.0f, BLD_TOWN_CENTER, 0, 1.0f);
		uint32_t tw = s.spawn_building_world(20.0f, 20.0f, BLD_TOWER, 0, 1.0f);
		const float th = find_bld(s, tw)->half_size;
		uint32_t close = foe(s, 20.0f + th + 1.0f, 20.0f);
		ArrowCounter b;
		n = 0;
		// (Player 1's AI may lay a foundation in range: structures are targets too.)
		auto at_units = [&]() { int k = 0; for (const Projectile &p : b.fired) k += p.target_unit_id != 0; return k; };
		for (int t = 0; t < 120; ++t) { s.tick(Simulation::FIXED_STEP); b.step(s, tw); }
		CHECK(at_units() == 0);
		foe(s, 20.0f, 20.0f + th + 8.0f);
		for (int t = 0; t < 120; ++t) { s.tick(Simulation::FIXED_STEP); b.step(s, tw); }
		n = at_units();
		std::printf("tower: %d arrows at a foe 8 units out, none at one inside MinRange\n", n);
		CHECK(n > 0);
		bool all_far = true;
		for (const Projectile &p : b.fired) all_far = all_far && p.target_unit_id != close;
		CHECK(all_far);
	}

	// 5. PreferredClasses: with a spearman and a ram in range, arrows go to the human
	// about 3 times as often (weights 3 and 1).
	{
		fresh(s, 11);
		uint32_t fort = s.spawn_building_world(-30.0f, -30.0f, BLD_FORTRESS, 0, 1.0f);
		const float half = find_bld(s, fort)->half_size;
		uint32_t man = foe(s, -30.0f + half + 8.0f, -30.0f);
		uint32_t ram = foe(s, -30.0f, -30.0f + half + 8.0f, UNIT_SIEGE_RAM);
		ArrowCounter a;
		for (int t = 0; t < (int)std::lround(70.0f / Simulation::FIXED_STEP); ++t) { s.tick(Simulation::FIXED_STEP); a.step(s, fort); }
		int at_man = 0, at_ram = 0;
		for (const Projectile &p : a.fired) { at_man += p.target_unit_id == man; at_ram += p.target_unit_id == ram; }
		std::printf("fortress over 70 s: %d arrows at the spearman, %d at the ram\n", at_man, at_ram);
		CHECK(at_man + at_ram == (int)a.fired.size() && a.fired.size() >= 180);
		const float share = (float)at_man / (float)(at_man + at_ram);
		CHECK(share > 0.66f && share < 0.84f);
	}

	// 6. Enemy structures in range draw arrows too; walls never shoot.
	{
		fresh(s);
		uint32_t cc = s.spawn_building_world(-40.0f, -40.0f, BLD_TOWN_CENTER, 0, 1.0f);
		const float half = find_bld(s, cc)->half_size;
		uint32_t house = s.spawn_building_world(-40.0f + half + 6.0f, -40.0f, BLD_HOUSE, 1, 1.0f);
		uint32_t wall = s.spawn_building_world(30.0f, 30.0f, BLD_WALL_SEGMENT, 0, 1.0f);
		foe(s, 30.0f, 33.0f);
		ArrowCounter a, w;
		for (int t = 0; t < 100; ++t) { s.tick(Simulation::FIXED_STEP); a.step(s, cc); w.step(s, wall); }
		bool at_house = !a.fired.empty();
		for (const Projectile &p : a.fired) at_house = at_house && p.target_building_id == house;
		CHECK(at_house);
		CHECK(w.fired.empty());
	}

	// 7. Save and load mid-volley: the round, arrows left and timer come back, so the
	// loaded game fires the same arrows.
	{
		fresh(s, 5);
		uint32_t cc = s.spawn_building_world(-40.0f, -40.0f, BLD_TOWN_CENTER, 0, 1.0f);
		const float half = find_bld(s, cc)->half_size;
		foe(s, -40.0f + half + 8.0f, -40.0f);
		for (int t = 0; t < 47; ++t) s.tick(Simulation::FIXED_STEP);
		const Simulation::Building a = *find_bld(s, cc);
		CHECK(a.arrow_timer_on && a.arrow_round > 0);
		const char *path = "/tmp/test_building_ai.sav";
		CHECK(s.save_state(path));
		static Simulation l;
		CHECK(l.load_state(path));
		std::remove(path);
		const Simulation::Building *b = find_bld(l, cc);
		CHECK(b && b->arrow_round == a.arrow_round && b->arrows_left == a.arrows_left && b->arrow_timer_on && near(b->attack_timer, a.attack_timer));
		ArrowCounter x, y;
		for (int t = 0; t < 100; ++t) { s.tick(Simulation::FIXED_STEP); l.tick(Simulation::FIXED_STEP); x.step(s, cc); y.step(l, cc); }
		CHECK(x.fired.size() == y.fired.size());
	}

	std::printf(failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", failures);
	return failures ? 1 : 0;
}
