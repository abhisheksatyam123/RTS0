// 0 A.D. BuildingAI.js: structure arrows. DefaultArrowCount + garrisoned
// GarrisonArrowClasses x GarrisonArrowMultiplier (capped at MaxArrowCount) per
// Attack/Ranged/RepeatTime, fired over twenty rounds after PrepareTime, each round at the
// first target by PreferredClasses (Human) then distance, with the template's damage and range.
// Of Gaia only units and dangerous animals are shot (SetupGaiaRangeQuery), and only what
// the owner sees (CheckTargetVisible); armour auras naming Structure (Maximus) count.
//   g++ -std=c++20 -O2 -Isrc tools/test_building_ai.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//       src/network/net_host.cpp -lpthread -o /tmp/test_building_ai && /tmp/test_building_ai
#define private public
#include "simulation/simulation.h"
#include "network/turn_manager.h"
#undef private
#include <cstring>
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
	// Two human seats: no Petra sends the foes off to build or gather.
	s.set_player_active(0, true, false);
	s.set_player_active(1, true, false);
	s.set_player_resources(0, 5000, 5000, 5000, 5000);
}

// A foe that stays put and never fights back from where it stands.
static uint32_t foe(Simulation &s, float x, float z, UnitType t = UNIT_SPEARMAN) {
	uint32_t u = s.spawn_unit(x, z, 1, t, 100000.0f);
	s.order_set_stance(&u, 1, STANCE_STAND_GROUND);
	s.cmd_stop(&u, 1);
	return u;
}

static uint16_t template_named(const char *suffix) {
	for (uint16_t t = 0; t < CIV_UNIT_TEMPLATE_COUNT; ++t) {
		const char *id = civ_unit_template(t).id;
		size_t n = std::strlen(id), m = std::strlen(suffix);
		if (n >= m && std::strcmp(id + n - m, suffix) == 0) return t;
	}
	return CIV_UNIT_TEMPLATE_NONE;
}

static int32_t fauna_idx(const Simulation &s, uint32_t id) {
	const auto it = s.fauna_id_to_idx_.find(id);
	return it == s.fauna_id_to_idx_.end() ? -1 : (int32_t)it->second;
}

static void garrison(Simulation &s, uint32_t *ids, size_t n, uint32_t bld) {
	const int want = s.get_garrison_count(bld) + (int)n;
	s.order_garrison(ids, n, bld);
	for (int t = 0; t < 900 && s.get_garrison_count(bld) < want; ++t) s.tick(0.1f);
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

		// The tower's List is Support Infantry (template_structure_defensive_tower.xml): the
		// horseman stays out.
		uint32_t tw = s.spawn_building_world(-20.0f, -50.0f, BLD_TOWER, 0, 1.0f);
		uint32_t mix[2] = {s.spawn_unit(-18.0f, -46.0f, 0, UNIT_SPEARMAN), s.spawn_unit(-18.0f, -47.0f, 0, UNIT_CAVALRY)};
		s.order_garrison(mix, 2, tw);
		for (int t = 0; t < 100; ++t) s.tick(0.1f);
		CHECK(s.get_garrison_count(tw) == 1);
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

	// 5. 0.28 FireArrows' target order: PreferredClasses (Human) first, then the nearest;
	// every arrow of a round goes to the first target. With a ram close by and two
	// spearmen further out, all arrows go to the nearer spearman, then to the other
	// spearman once he is dead, never to the ram while a human is in range.
	{
		fresh(s, 11);
		uint32_t fort = s.spawn_building_world(-30.0f, -30.0f, BLD_FORTRESS, 0, 1.0f);
		const float half = find_bld(s, fort)->half_size;
		uint32_t ram = foe(s, -30.0f, -30.0f + half + 3.0f, UNIT_SIEGE_RAM);
		uint32_t near_man = foe(s, -30.0f + half + 6.0f, -30.0f);
		uint32_t far_man = foe(s, -30.0f - half - 9.0f, -30.0f);
		auto keep_alive = [&](uint32_t id) { s.units_.hp[(size_t)idx_of(s, id)] = 1000.0f; };
		ArrowCounter a;
		for (int t = 0; t < (int)std::lround(20.0f / Simulation::FIXED_STEP); ++t) {
			keep_alive(near_man); keep_alive(far_man);
			s.tick(Simulation::FIXED_STEP); a.step(s, fort);
		}
		int at_near = 0;
		for (const Projectile &p : a.fired) at_near += p.target_unit_id == near_man;
		std::printf("fortress over 20 s: %d of %d arrows at the nearer spearman\n", at_near, (int)a.fired.size());
		CHECK(a.fired.size() >= 40 && at_near == (int)a.fired.size());
		s.units_.hp[(size_t)idx_of(s, near_man)] = 0.0f;
		s.units_.alive[(size_t)idx_of(s, near_man)] = 0;
		ArrowCounter c;
		for (int t = 0; t < (int)std::lround(10.0f / Simulation::FIXED_STEP); ++t) {
			keep_alive(far_man);
			s.tick(Simulation::FIXED_STEP); c.step(s, fort);
		}
		int at_far = 0, at_ram = 0;
		for (const Projectile &p : c.fired) { at_far += p.target_unit_id == far_man; at_ram += p.target_unit_id == ram; }
		std::printf("then %d arrows at the other spearman, %d at the ram\n", at_far, at_ram);
		CHECK(!c.fired.empty() && at_far == (int)c.fired.size() && at_ram == 0);
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

	// 8. Gaia: a wolf (a dangerous animal) in range draws arrows and they hurt it; a deer
	// and a Gaia tower do not (OnRangeUpdate keeps animals only if IsDangerousAnimal, and
	// Gaia structures have no UnitAI).
	{
		fresh(s, 3);
		uint32_t cc = s.spawn_building_world(-40.0f, -40.0f, BLD_TOWN_CENTER, 0, 1.0f);
		const float half = find_bld(s, cc)->half_size;
		const uint32_t wolf = s.spawn_fauna(-40.0f + half + 6.0f, -40.0f, FAUNA_WOLF);
		CHECK(fauna_type_info(FAUNA_WOLF).is_dangerous() && fauna_idx(s, wolf) >= 0);
		const float hp0 = s.fauna_.hp[fauna_idx(s, wolf)];
		ArrowCounter a;
		for (int t = 0; t < 60; ++t) { s.tick(Simulation::FIXED_STEP); a.step(s, cc); }
		bool at_wolf = !a.fired.empty();
		for (const Projectile &p : a.fired) at_wolf = at_wolf && p.target_unit_id == wolf;
		std::printf("civic centre at a wolf: %zu arrows, wolf hp %.1f -> %.1f\n", a.fired.size(), hp0,
				fauna_idx(s, wolf) >= 0 ? s.fauna_.hp[fauna_idx(s, wolf)] : 0.0f);
		CHECK(at_wolf);
		CHECK(fauna_idx(s, wolf) < 0 || s.fauna_.hp[fauna_idx(s, wolf)] < hp0 || !s.fauna_.alive[fauna_idx(s, wolf)]);

		fresh(s, 3);
		cc = s.spawn_building_world(-40.0f, -40.0f, BLD_TOWN_CENTER, 0, 1.0f);
		CHECK(!fauna_type_info(FAUNA_DEER).is_dangerous());
		s.spawn_fauna(-40.0f + half + 6.0f, -40.0f, FAUNA_DEER);
		s.spawn_building_world(-40.0f, -40.0f + half + 10.0f, BLD_TOWER, GAIA_PLAYER, 1.0f);
		ArrowCounter d;
		for (int t = 0; t < 60; ++t) { s.tick(Simulation::FIXED_STEP); d.step(s, cc); }
		CHECK(d.fired.empty());
	}

	// 9. CheckTargetVisible: a tower that sees only a tenth as far holds its arrows at a
	// foe in range but out of sight, and shoots once an own unit stands by the foe.
	{
		fresh(s, 4);
		uint32_t tw = s.spawn_building_world(-40.0f, -40.0f, BLD_TOWER, 0, 1.0f);
		const float half = find_bld(s, tw)->half_size;
		s.tech_mods_[0].bld_vision[BLD_TOWER] = 0.1f;
		const float fx = -40.0f + half + 15.0f;
		foe(s, fx, -40.0f);
		ArrowCounter a;
		for (int t = 0; t < 60; ++t) { s.tick(Simulation::FIXED_STEP); a.step(s, tw); }
		CHECK(!s.los_visible(0, fx, -40.0f));
		CHECK(a.fired.empty());
		uint32_t scout = s.spawn_unit(fx - 2.0f, -42.0f, 0, UNIT_CAVALRY, 100000.0f);
		s.order_set_stance(&scout, 1, STANCE_PASSIVE);
		for (int t = 0; t < 60; ++t) { s.tick(Simulation::FIXED_STEP); a.step(s, tw); }
		std::printf("tower with a scout by the foe: %zu arrows\n", a.fired.size());
		CHECK(s.los_visible(0, fx, -40.0f));
		CHECK(!a.fired.empty());
	}

	// 10. Maximus' Shield of Rome: his player's structures get +1 hack, pierce and crush.
	{
		fresh(s, 6);
		s.set_team_civ(0, CIV_ROMANS);
		uint32_t cc = s.spawn_building_world(-40.0f, -40.0f, BLD_TOWN_CENTER, 0, 1.0f);
		float base[3], with[3];
		s.structure_resistance(*find_bld(s, cc), base);
		const uint16_t maximus = template_named("rome/hero_maximus");
		CHECK(maximus != CIV_UNIT_TEMPLATE_NONE);
		s.spawn_unit(40.0f, 40.0f, 0, UNIT_HERO, 0.0f, maximus);
		s.rebuild_auras();
		s.structure_resistance(*find_bld(s, cc), with);
		std::printf("civic centre resistance %.0f/%.0f/%.0f, with Maximus %.0f/%.0f/%.0f\n",
				base[0], base[1], base[2], with[0], with[1], with[2]);
		CHECK(near(with[0], base[0] + 1.0f) && near(with[1], base[1] + 1.0f) && near(with[2], base[2] + 1.0f));
	}

	// 11. Caros' Valiant Defender (iber_hero_caros_1, a garrison aura): the structure or siege
	// tower holding him multiplies GarrisonArrowMultiplier and MaxArrowCount by 1.75, once.
	{
		fresh(s, 8);
		s.set_team_civ(0, CIV_IBERIANS);
		const uint16_t caros = template_named("iber/hero_caros");
		CHECK(caros != CIV_UNIT_TEMPLATE_NONE);
		// A sentry tower (2 arrows, max 6) with three archers: 5; with two archers and Caros:
		// 2 + round(n x 1.75), now capped at round(6 x 1.75) = 11 instead of 6.
		uint32_t st = s.spawn_building_world(-20.0f, -30.0f, BLD_SENTRY_TOWER, 0, 1.0f);
		uint32_t g[2] = {s.spawn_unit(-18.0f, -26.0f, 0, UNIT_ARCHER), s.spawn_unit(-18.0f, -27.0f, 0, UNIT_ARCHER)};
		garrison(s, g, 2, st);
		CHECK(s.building_arrow_count(*find_bld(s, st)) == 4);
		uint32_t hero = s.spawn_unit(-18.0f, -28.0f, 0, UNIT_HERO, 0.0f, caros);
		const bool counts = (s.unit_classes_at((size_t)idx_of(s, hero)) & building_ai_info(BLD_SENTRY_TOWER).garrison_arrow_classes) != 0;
		garrison(s, &hero, 1, st);
		CHECK(s.get_garrison_count(st) == 3);
		const int n = 2 + (counts ? 1 : 0);
		const int want = std::min(2 + (int)std::lround(n * 1.75f), 11);
		std::printf("sentry tower with 2 archers and Caros: %d arrows (want %d)\n", s.building_arrow_count(*find_bld(s, st)), want);
		CHECK(s.building_arrow_count(*find_bld(s, st)) == want);
		CHECK(want > 6); // past the sentry tower's own MaxArrowCount
		// A siege tower carrying four spearmen and Caros: round(n x 1.75) arrows a volley.
		uint32_t tower = s.spawn_unit(20.0f, 20.0f, 0, UNIT_BOLT_SHOOTER, 0.0f, template_named("mace/siege_tower"));
		const int32_t ti = idx_of(s, tower);
		CHECK(ti >= 0 && s.is_siege_tower_at((size_t)ti));
		std::vector<uint32_t> crew;
		for (int k = 0; k < 4; ++k) crew.push_back(s.spawn_unit(25.0f, 20.0f + k, 0, UNIT_SPEARMAN));
		s.transport_passengers_[tower] = crew;
		CHECK(s.siege_tower_arrows((size_t)ti) == 4);
		uint32_t hero2 = s.spawn_unit(25.0f, 26.0f, 0, UNIT_HERO, 0.0f, caros);
		crew.push_back(hero2);
		s.transport_passengers_[tower] = crew;
		const bool inf = (unit_tech_classes(UNIT_HERO) & TC_INFANTRY) != 0;
		std::printf("siege tower with 4 spearmen and Caros: %d arrows\n", s.siege_tower_arrows((size_t)ti));
		CHECK(s.siege_tower_arrows((size_t)ti) == (int)std::lround((4 + (inf ? 1 : 0)) * 1.75f));
		s.transport_passengers_.erase(tower);
		// Auras.js ApplyGarrisonAura has no owner test: an ally's Caros in the tower counts too.
		s.set_player_active(2, true, false);
		s.set_team_civ(2, CIV_IBERIANS);
		s.cmd_set_diplomacy(0, 2, Simulation::DIPLO_ALLY);
		s.cmd_set_diplomacy(2, 0, Simulation::DIPLO_ALLY);
		uint32_t st2 = s.spawn_building_world(-20.0f, 20.0f, BLD_SENTRY_TOWER, 0, 1.0f);
		uint32_t g2[2] = {s.spawn_unit(-18.0f, 24.0f, 0, UNIT_ARCHER), s.spawn_unit(-18.0f, 23.0f, 0, UNIT_ARCHER)};
		garrison(s, g2, 2, st2);
		uint32_t ally_hero = s.spawn_unit(-18.0f, 22.0f, 2, UNIT_HERO, 0.0f, caros);
		garrison(s, &ally_hero, 1, st2);
		CHECK(s.get_garrison_count(st2) == 3);
		std::printf("sentry tower with 2 archers and an ally's Caros: %d arrows (want %d)\n", s.building_arrow_count(*find_bld(s, st2)), want);
		CHECK(s.building_arrow_count(*find_bld(s, st2)) == want);
	}

	// 12. Focus fire (0 A.D. "focus-fire", BuildingAI.AddFocusTarget): with a spearman and a
	// ram in range, a fortress told to focus the ram sends every arrow there; Shift queues the
	// spearman after it. A target out of range is refused, the focus list survives a save, a
	// dead target drops off and the fortress goes back to its own picks, a capture clears the
	// list, and lockstep command 50 carries the order.
	{
		fresh(s, 13);
		uint32_t fort = s.spawn_building_world(-30.0f, -30.0f, BLD_FORTRESS, 0, 1.0f);
		const float half = find_bld(s, fort)->half_size;
		uint32_t man = foe(s, -30.0f + half + 8.0f, -30.0f);
		uint32_t ram = foe(s, -30.0f, -30.0f + half + 8.0f, UNIT_SIEGE_RAM);
		uint32_t far_man = foe(s, 40.0f, 40.0f);
		CHECK(!s.cmd_focus_fire(0, &fort, 1, far_man, false));
		CHECK(!s.cmd_focus_fire(1, &fort, 1, ram, false)); // not player 1's fortress
		CHECK(s.cmd_focus_fire(0, &fort, 1, ram, false));
		CHECK(s.cmd_focus_fire(0, &fort, 1, man, true));
		CHECK((find_bld(s, fort)->focus_targets == std::vector<uint32_t>{ram, man}));
		const char *path = "/tmp/test_building_ai_focus.sav";
		CHECK(s.save_state(path));
		static Simulation l;
		CHECK(l.load_state(path));
		std::remove(path);
		CHECK((find_bld(l, fort)->focus_targets == std::vector<uint32_t>{ram, man}));
		ArrowCounter a;
		for (int t = 0; t < (int)std::lround(20.0f / Simulation::FIXED_STEP); ++t) { s.tick(Simulation::FIXED_STEP); a.step(s, fort); }
		int at_man = 0, at_ram = 0;
		for (const Projectile &p : a.fired) { at_man += p.target_unit_id == man; at_ram += p.target_unit_id == ram; }
		std::printf("focused fortress over 20 s: %d arrows at the ram, %d at the spearman\n", at_ram, at_man);
		CHECK(at_ram > 20 && at_man == 0);
		// The ram dies: its focus entry drops at the next round and the spearman is next.
		s.units_.hp[(size_t)idx_of(s, ram)] = 0.0f;
		s.units_.alive[(size_t)idx_of(s, ram)] = 0;
		ArrowCounter b;
		for (int t = 0; t < (int)std::lround(20.0f / Simulation::FIXED_STEP); ++t) { s.tick(Simulation::FIXED_STEP); b.step(s, fort); }
		int b_man = 0;
		for (const Projectile &p : b.fired) b_man += p.target_unit_id == man;
		CHECK(!b.fired.empty() && b_man == (int)b.fired.size());
		CHECK((find_bld(s, fort)->focus_targets == std::vector<uint32_t>{man}));
		// Lockstep: command 50 with Shift pushes a third target onto the end.
		uint32_t man2 = foe(s, -30.0f - half - 8.0f, -30.0f);
		NetCommand c;
		c.cmd_type = (uint8_t)NetCommandType::NET_CMD_FOCUS_FIRE;
		c.entity_ids = {fort};
		c.target_id = man2;
		c.param = NET_PARAM_QUEUED;
		CHECK((uint8_t)NetCommandType::NET_CMD_FOCUS_FIRE == 50);
		TurnManager tm;
		tm.post_local_command(c);
		for (int t = 0; t < 4; ++t) tm.tick(TurnManager::TURN_DURATION, s);
		CHECK(!find_bld(s, fort)->focus_targets.empty() && find_bld(s, fort)->focus_targets.back() == man2);
		// OnOwnershipChanged empties the list.
		const uint8_t to = 1;
		s.cheat_convert(to, &fort, 1);
		CHECK(find_bld(s, fort)->team == to && find_bld(s, fort)->focus_targets.empty());
	}

	// 13. Siege towers shoot as BuildingAI (0.28: Attack.js leaves entities with BuildingAI
	// to it): an empty tower fires nothing, even at its ordered target; a manned one with
	// no orders shoots the nearest human in range rather than the closer ram, and once
	// ordered onto the ram (UnitAI's target, in range) every volley goes there.
	{
		fresh(s, 17);
		const uint16_t st = template_named("mace/siege_tower");
		uint32_t tower = s.spawn_unit(20.0f, 20.0f, 0, UNIT_BOLT_SHOOTER, 0.0f, st);
		const int32_t ti = idx_of(s, tower);
		CHECK(ti >= 0 && s.is_siege_tower_at((size_t)ti));
		const float r = s.units_.attack_range[(size_t)ti];
		uint32_t ram = foe(s, 20.0f + r * 0.4f, 20.0f, UNIT_SIEGE_RAM);
		uint32_t man = foe(s, 20.0f, 20.0f + r * 0.7f);
		ArrowCounter e;
		s.order_attack(&tower, 1, ram);
		for (int t = 0; t < (int)std::lround(8.0f / Simulation::FIXED_STEP); ++t) { s.tick(Simulation::FIXED_STEP); e.step(s, tower); }
		CHECK(e.fired.empty());
		s.cmd_stop(&tower, 1);
		std::vector<uint32_t> crew;
		for (int k = 0; k < 4; ++k) crew.push_back(s.spawn_unit(25.0f, 20.0f + k, 0, UNIT_SPEARMAN));
		s.transport_passengers_[tower] = crew;
		for (uint32_t c : crew) {
			const int32_t ci = idx_of(s, c);
			s.units_.action_state[(size_t)ci] = ACTION_GARRISONED;
			s.units_.alive[(size_t)ci] = 0;
		}
		auto keep = [&] { s.units_.hp[(size_t)idx_of(s, man)] = 1000.0f; s.units_.hp[(size_t)idx_of(s, ram)] = 1000.0f; };
		ArrowCounter a;
		for (int t = 0; t < (int)std::lround(10.0f / Simulation::FIXED_STEP); ++t) { keep(); s.tick(Simulation::FIXED_STEP); a.step(s, tower); }
		int at_man = 0;
		for (const Projectile &p : a.fired) at_man += p.target_unit_id == man;
		std::printf("idle siege tower with 4 spearmen over 10 s: %d arrows, %d at the spearman\n", (int)a.fired.size(), at_man);
		CHECK(a.fired.size() >= 8 && at_man == (int)a.fired.size());
		s.order_attack(&tower, 1, ram);
		ArrowCounter b;
		for (int t = 0; t < (int)std::lround(10.0f / Simulation::FIXED_STEP); ++t) { keep(); s.tick(Simulation::FIXED_STEP); b.step(s, tower); }
		int at_ram = 0;
		for (const Projectile &p : b.fired) at_ram += p.target_unit_id == ram;
		std::printf("ordered onto the ram: %d of %d arrows at it\n", at_ram, (int)b.fired.size());
		CHECK(!b.fired.empty() && at_ram == (int)b.fired.size());
		s.transport_passengers_.erase(tower);
	}

	// 9. iber/defense_tower.xml: DefaultArrowCount +1 (5) and MinRange 12 m, kept by a captor.
	{
		fresh(s);
		uint32_t tw = s.spawn_building_world(-40.0f, -40.0f, BLD_TOWER, 0, 1.0f);
		s.set_building_civ(tw, CIV_ROMANS);
		CHECK(s.building_arrow_count(*find_bld(s, tw)) == 4);
		s.set_building_civ(tw, CIV_IBERIANS);
		std::printf("Iberian tower: %d arrows\n", s.building_arrow_count(*find_bld(s, tw)));
		CHECK(s.building_arrow_count(*find_bld(s, tw)) == 5);
		CHECK(near(civ_building_ai_info(CIV_IBERIANS, BLD_TOWER).min_range, 12.0f * COMBAT_DISTANCE_SCALE));
		CHECK(near(civ_building_ai_info(CIV_ROMANS, BLD_TOWER).min_range, 10.0f * COMBAT_DISTANCE_SCALE));
	}

	// 10. Attack.js PredictTimeToTarget leads with the structure's own Projectile/Speed: a bolt
	// tower (150) aims less far ahead of a runner than a civic centre (100) would.
	{
		fresh(s);
		uint32_t bt = s.spawn_building_world(-40.0f, -40.0f, BLD_TOWER_BOLT, 0, 1.0f);
		uint32_t cc = s.spawn_building_world(40.0f, 40.0f, BLD_TOWN_CENTER, 0, 1.0f);
		std::printf("lead speeds: bolt tower %.1f, civic centre %.1f u/s\n", s.projectile_speed_of(bt, 0), s.projectile_speed_of(cc, 0));
		CHECK(near(s.projectile_speed_of(bt, 0), 150.0f * COMBAT_DISTANCE_SCALE, 0.01f));
		CHECK(near(s.projectile_speed_of(cc, 0), 100.0f * COMBAT_DISTANCE_SCALE, 0.01f));
	}

	// 11. BuildingAI's enemy query takes an enemy's livestock (IsDangerousAnimal filters only
	// Gaia's); Gaia's sheep and an ally's are left alone.
	{
		fresh(s);
		uint32_t cc = s.spawn_building_world(-40.0f, -40.0f, BLD_TOWN_CENTER, 0, 1.0f);
		uint32_t sheep = s.spawn_fauna(-30.0f, -40.0f, FAUNA_SHEEP_TRAINABLE, 1);
		uint32_t wild_sheep = s.spawn_fauna(-40.0f, -30.0f, FAUNA_SHEEP_TRAINABLE, GAIA_PLAYER);
		ArrowCounter a;
		for (int t = 0; t < (int)std::lround(10.0f / Simulation::FIXED_STEP); ++t) { s.tick(Simulation::FIXED_STEP); a.step(s, cc); }
		int at_sheep = 0, at_wild = 0;
		for (const Projectile &p : a.fired) { at_sheep += p.target_unit_id == sheep; at_wild += p.target_unit_id == wild_sheep; }
		std::printf("civic centre beside an enemy's sheep over 10 s: %d arrows, %d at it, %d at Gaia's\n", (int)a.fired.size(), at_sheep, at_wild);
		CHECK(at_sheep > 0 && at_wild == 0);
		// So does a siege tower's (template_unit_siege_tower has BuildingAI).
		fresh(s);
		uint32_t tower = s.spawn_unit(20.0f, 20.0f, 0, UNIT_BOLT_SHOOTER, 0.0f, template_named("mace/siege_tower"));
		std::vector<uint32_t> crew;
		for (int k = 0; k < 4; ++k) crew.push_back(s.spawn_unit(21.0f, 20.0f + k, 0, UNIT_ARCHER));
		s.order_board_transport(crew.data(), crew.size(), tower);
		uint32_t flock = s.spawn_fauna(26.0f, 20.0f, FAUNA_SHEEP_TRAINABLE, 1);
		ArrowCounter st;
		for (int t = 0; t < (int)std::lround(10.0f / Simulation::FIXED_STEP); ++t) { s.tick(Simulation::FIXED_STEP); st.step(s, tower); }
		int at_flock = 0;
		for (const Projectile &p : st.fired) at_flock += p.target_unit_id == flock;
		std::printf("siege tower beside an enemy's sheep over 10 s: %d arrows, %d at it\n", (int)st.fired.size(), at_flock);
		CHECK(at_flock > 0);
	}

	// 11b. The range query measures to the target's obstruction edge: a war elephant (3 m
	// clearance) whose centre is just past a tower's range is still shot.
	{
		fresh(s);
		uint32_t tw = s.spawn_building_world(0.0f, 0.0f, BLD_TOWER, 0, 1.0f);
		const Simulation::Building &b = *find_bld(s, tw);
		const float edge = b.half_size + s.building_arrow_range(b);
		uint32_t ele = foe(s, edge + CLEARANCE_LARGE * 0.5f, 0.0f, UNIT_ELEPHANT);
		CHECK(s.unit_clearance_at((size_t)idx_of(s, ele)) == CLEARANCE_LARGE);
		ArrowCounter a;
		for (int t = 0; t < (int)std::lround(10.0f / Simulation::FIXED_STEP); ++t) { s.tick(Simulation::FIXED_STEP); a.step(s, tw); }
		std::printf("tower at an elephant %.2f wu past its range: %d arrows\n", CLEARANCE_LARGE * 0.5f, (int)a.fired.size());
		CHECK(!a.fired.empty());
	}

	// 12. Trainer.js {native}: a captured Carthaginian embassy trains its Celtic mercenaries
	// for a Roman captor; a Roman's barracks offers no Carthaginian unit.
	{
		fresh(s);
		s.set_team_civ(0, CIV_ROMANS);
		s.players_[0].phase = 3;
		uint32_t emb = s.spawn_building_world(-40.0f, -40.0f, BLD_EMBASSY, 0, 1.0f);
		s.set_building_civ(emb, CIV_CARTHAGINIANS);
		const uint16_t gaul = template_named("cart/infantry_swordsman_gaul_b");
		CHECK(gaul != CIV_UNIT_TEMPLATE_NONE);
		const bool ok = s.cmd_train_template_at(0, emb, gaul, true, 1);
		std::printf("Roman-held Carthaginian embassy trains a Gallic swordsman: %d\n", (int)ok);
		CHECK(ok);
		uint32_t bk = s.spawn_building_world(40.0f, 40.0f, BLD_BARRACKS, 0, 1.0f);
		CHECK(!s.cmd_train_template_at(0, bk, gaul, false, 1));
	}

	// 13. Alexander's Undefeated (mace_hero_alexander_iii_2: enemy Soldiers and Structures
	// -10% attack damage): a tower near him looses weaker arrows; one out of reach doesn't.
	{
		fresh(s);
		uint32_t near_t = s.spawn_building_world(0.0f, 0.0f, BLD_TOWER, 0, 1.0f);
		uint32_t far_t = s.spawn_building_world(40.0f, 0.0f, BLD_TOWER, 0, 1.0f);
		const float res[3] = {0.0f, 0.0f, 0.0f};
		const float base = s.structure_arrow_damage(*find_bld(s, near_t), res);
		const uint16_t alex = template_named("mace/hero_alexander_iii");
		CHECK(alex != CIV_UNIT_TEMPLATE_NONE);
		s.spawn_unit(0.0f, 5.0f, 1, UNIT_HERO, 0.0f, alex);
		s.rebuild_auras();
		const float with = s.structure_arrow_damage(*find_bld(s, near_t), res);
		std::printf("tower arrow %.2f, near an enemy Alexander %.2f\n", base, with);
		CHECK(near(with, base * 0.9f));
		CHECK(near(s.structure_arrow_damage(*find_bld(s, far_t), res), base));
	}

	std::printf(failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", failures);
	return failures ? 1 : 0;
}
