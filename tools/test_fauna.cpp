// 0 A.D. fauna (templates/gaia/fauna_*.xml, UnitAI.js animal stances): the 61-species table,
// hunting (units kill an animal, then gather its carcass), stances (skittish and passive animals
// flee when hit, defensive ones attack inside range + vision / 2, aggressive ones inside vision,
// passive-defensive ones only fight back), soldiers fighting back, and herding for any player.
//   g++ -std=c++20 -O2 -Isrc tools/test_fauna.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//       src/network/net_host.cpp -lpthread -o /tmp/test_fauna && /tmp/test_fauna
#define private public
#include "simulation/simulation.h"
#include "simulation/entity_limits_data.h"
#undef private
#include "network/turn_manager.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

using namespace rtso;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static void run(Simulation &s, float seconds) {
	for (int t = 0; t < (int)std::lround(seconds / Simulation::FIXED_STEP); ++t) s.tick(Simulation::FIXED_STEP);
}

static void fresh(Simulation &s) {
	s.set_sim_seed(5);
	s.reset();
	for (uint8_t p = 0; p < 2; ++p) s.players_[p].active = 1, s.players_[p].is_ai = 0; // no Petra builders
	s.spawn_building_world(-50.0f, -50.0f, BLD_TOWN_CENTER, 0, 1.0f);
	s.spawn_building_world(50.0f, 50.0f, BLD_TOWN_CENTER, 1, 1.0f);
}

static int32_t fidx(const Simulation &s, uint32_t id) {
	auto it = s.fauna_id_to_idx_.find(id);
	return it == s.fauna_id_to_idx_.end() ? -1 : (int32_t)it->second;
}
static bool f_alive(const Simulation &s, uint32_t id) { int32_t f = fidx(s, id); return f >= 0 && s.fauna_.alive[f]; }
static int32_t uidx(const Simulation &s, uint32_t id) { return s.lookup_unit(id); }

// Meat left on the map within r of (x, z).
static int meat_near(const Simulation &s, float x, float z, float r) {
	int total = 0;
	for (size_t i = 0; i < s.resources_.count; ++i) {
		if (s.resources_.subtype[i] != RES_SUB_MEAT) continue;
		float dx = s.resources_.x[i] - x, dz = s.resources_.z[i] - z;
		if (dx * dx + dz * dz <= r * r) total += s.resources_.amount[i];
	}
	return total;
}

int main() {
	// 1. The table is 0 A.D.'s: 61 species, RTS0's four old ids first, lookup by template name.
	{
		CHECK(FAUNA_COUNT == 61);
		CHECK(std::strcmp(fauna_type_info(FAUNA_DEER).template_name, "fauna_deer") == 0);
		CHECK(std::strcmp(fauna_type_info(FAUNA_BOAR).template_name, "fauna_boar") == 0);
		CHECK(std::strcmp(fauna_type_info(FAUNA_SHEEP).template_name, "fauna_sheep") == 0);
		CHECK(std::strcmp(fauna_type_info(FAUNA_WOLF).template_name, "fauna_wolf") == 0);
		CHECK(fauna_type_by_name("bear_brown") == FAUNA_BEAR_BROWN);
		CHECK(fauna_type_by_name("fauna_whale_fin") == FAUNA_WHALE_FIN);
		CHECK(fauna_type_by_name("unicorn") == FAUNA_COUNT);
		CHECK(fauna_type_info(FAUNA_DEER).max_hp == 25.0f && fauna_type_info(FAUNA_DEER).food_amount == 100);
		CHECK(fauna_type_info(FAUNA_DEER).stance == FAUNA_STANCE_SKITTISH);
		CHECK(fauna_type_info(FAUNA_BOAR).stance == FAUNA_STANCE_PASSIVE_DEFENSIVE);
		CHECK(fauna_type_info(FAUNA_WOLF_ARCTIC_VIOLENT).stance == FAUNA_STANCE_VIOLENT);
		CHECK(fauna_type_info(FAUNA_SHEEP).is_domestic() && !fauna_type_info(FAUNA_DEER).is_domestic());
		CHECK(fauna_type_info(FAUNA_WHALE_HUMPBACK).is_naval());
		CHECK(fauna_type_info(FAUNA_BEAR_POLAR).is_dangerous() && !fauna_type_info(FAUNA_BOAR).is_dangerous());
		for (int f = 0; f < FAUNA_COUNT; ++f) CHECK(fauna_type_info((uint8_t)f).model_id[0] != 0);
		CHECK(treasure_type_info(TREASURE_SPECIAL_ARGO).amount[0] == 600);
		CHECK(ruins_type_info(0).amount == 500);
	}

	static Simulation s;

	// 2. Hunting: a citizen soldier kills a deer, gets its Loot xp and gathers the carcass.
	{
		fresh(s);
		uint32_t cav = s.spawn_unit(0.0f, 0.0f, 0, UNIT_VILLAGER);
		uint32_t deer = s.spawn_fauna(6.0f, 0.0f, FAUNA_DEER);
		const uint16_t xp0 = s.units_.xp[uidx(s, cav)];
		s.order_hunt(&cav, 1, deer);
		CHECK(s.units_.target_unit_id[uidx(s, cav)] == deer);
		run(s, 20.0f);
		CHECK(!f_alive(s, deer));
		CHECK(fidx(s, deer) < 0); // compacted out of the fauna table
		int32_t c = uidx(s, cav);
		CHECK(std::fabs(s.units_.xp[c] + s.units_.xp_partial[c] - (xp0 + 5)) < 0.01f); // per-hit shares of 5
		const bool gathering = s.units_.action_state[c] == ACTION_GATHER || s.units_.action_state[c] == ACTION_RETURN_DROPSITE;
		CHECK(gathering);
		CHECK(s.units_.carry[c] > 0 || s.units_.res_type_carried[c] == RES_FOOD);
		std::printf("hunt: hunter state %d carry %d\n", s.units_.action_state[c], s.units_.carry[c]);
	}

	// 2a. Health.js TakeDamage: every hit gives its attacker Loot xp x health taken / max
	// health, so two hunters share a deer's 5 xp and a wounded animal already gave some.
	{
		fresh(s);
		uint32_t a = s.spawn_unit(0.0f, 0.0f, 0, UNIT_SPEARMAN);
		uint32_t b = s.spawn_unit(0.0f, 1.0f, 0, UNIT_SPEARMAN);
		uint32_t boar = s.spawn_fauna(3.0f, 0.5f, FAUNA_BOAR);
		const int32_t ai = uidx(s, a), bi = uidx(s, b);
		const float xa0 = s.units_.xp[ai] + s.units_.xp_partial[ai], xb0 = s.units_.xp[bi] + s.units_.xp_partial[bi];
		const uint32_t both[2] = {a, b};
		s.order_hunt(both, 2, boar);
		run(s, 30.0f);
		CHECK(!f_alive(s, boar));
		const float ga = s.units_.xp[uidx(s, a)] + s.units_.xp_partial[uidx(s, a)] - xa0;
		const float gb = s.units_.xp[uidx(s, b)] + s.units_.xp_partial[uidx(s, b)] - xb0;
		const float loot = fauna_type_info(FAUNA_BOAR).loot_xp;
		std::printf("shared hunt xp: %.2f + %.2f of %.0f\n", ga, gb, loot);
		CHECK(ga > 0.0f && gb > 0.0f && std::fabs(ga + gb - loot) < 0.01f);

		// A wounded animal that gets away still gave its share.
		fresh(s);
		uint32_t c = s.spawn_unit(0.0f, 0.0f, 0, UNIT_SPEARMAN);
		uint32_t deer = s.spawn_fauna(2.0f, 0.0f, FAUNA_DEER);
		const int32_t ci = uidx(s, c);
		const float xc0 = s.units_.xp[ci] + s.units_.xp_partial[ci];
		s.order_hunt(&c, 1, deer);
		for (int t = 0; t < 400 && f_alive(s, deer) && s.fauna_.hp[fidx(s, deer)] >= fauna_type_info(FAUNA_DEER).max_hp; ++t)
			s.tick(Simulation::FIXED_STEP);
		s.cmd_stop(&c, 1);
		const int32_t ci2 = uidx(s, c);
		const float gc = s.units_.xp[ci2] + s.units_.xp_partial[ci2] - xc0;
		CHECK(f_alive(s, deer) && gc > 0.0f && gc < fauna_type_info(FAUNA_DEER).loot_xp);
	}

	// 2c. UnitAI FINDINGNEWTARGET: a hunter whose carcass is eaten goes on only to the same
	// template ("type.specific != 'meat' || resourceTemplate == template"), so a deer hunter
	// passes a nearer boar for the next deer.
	{
		fresh(s);
		uint32_t h = s.spawn_unit(0.0f, 0.0f, 0, UNIT_SPEARMAN);
		uint32_t deer = s.spawn_fauna(2.0f, 0.0f, FAUNA_DEER);
		run(s, 0.1f);
		s.order_hunt(&h, 1, deer);
		for (int t = 0; t < 400 && f_alive(s, deer); ++t) s.tick(Simulation::FIXED_STEP);
		CHECK(!f_alive(s, deer));
		CHECK(s.units_.hunt_fauna[uidx(s, h)] == FAUNA_DEER + 1);
		// The others come after the kill, so they don't flee from it.
		const float hx = s.units_.pos_x[uidx(s, h)], hz = s.units_.pos_z[uidx(s, h)];
		uint32_t boar = s.spawn_fauna(hx - 3.0f, hz, FAUNA_BOAR);
		uint32_t deer2 = s.spawn_fauna(hx + 9.0f, hz, FAUNA_DEER);
		for (size_t r = 0; r < s.resources_.count; ++r)
			if (s.resources_.subtype[r] == RES_SUB_MEAT) {
				CHECK(s.resources_.fauna[r] == FAUNA_DEER + 1);
				s.resources_.amount[r] = 1; // the carcass runs out
			}
		uint32_t next = 0;
		for (int t = 0; t < 200 && next == 0; ++t) {
			s.tick(Simulation::FIXED_STEP);
			const uint32_t tu = s.units_.target_unit_id[uidx(s, h)];
			if (tu == boar || tu == deer2) next = tu;
		}
		std::printf("same species: hunter went on to %s\n", next == deer2 ? "the deer" : next == boar ? "the boar" : "nothing");
		CHECK(next == deer2);
	}

	// 2b. Citizen cavalry hunt and butcher meat (template_unit_cavalry: food.meat only, carry 20).
	{
		fresh(s);
		uint32_t cav = s.spawn_unit(-40.0f, -40.0f, 0, UNIT_CAVALRY);
		uint32_t gazelle = s.spawn_fauna(-34.0f, -40.0f, FAUNA_GAZELLE);
		CHECK(s.units_.carry_capacity[uidx(s, cav)] == 20);
		s.order_hunt(&cav, 1, gazelle);
		const float food0 = s.player(0).res[RES_FOOD];
		run(s, 40.0f);
		CHECK(!f_alive(s, gazelle));
		std::printf("cavalry: food %.0f -> %.0f, state %d\n", food0, s.player(0).res[RES_FOOD], s.units_.action_state[uidx(s, cav)]);
		CHECK(s.player(0).res[RES_FOOD] > food0); // butchered and dropped at the town centre
		// ...but not other resources.
		uint32_t tree = s.spawn_resource_world(-38.0f, -36.0f, RES_WOOD, 200);
		s.order_gather(&cav, 1, tree);
		CHECK(s.units_.target_resource_id[uidx(s, cav)] != tree);
	}

	// 3. The deer flees when hit (skittish, vision 0: it doesn't flee on sight).
	{
		fresh(s);
		uint32_t spear = s.spawn_unit(0.0f, 0.0f, 0, UNIT_SPEARMAN);
		uint32_t deer = s.spawn_fauna(3.0f, 0.0f, FAUNA_DEER);
		run(s, 3.0f);
		CHECK(f_alive(s, deer) && s.fauna_.behavior[fidx(s, deer)] != FAUNA_FLEE); // not on sight
		s.fauna_.hp[fidx(s, deer)] = 1000.0f; // survive the first spear
		s.order_hunt(&spear, 1, deer);
		run(s, 3.0f);
		int32_t f = fidx(s, deer);
		CHECK(f >= 0);
		if (f >= 0) {
			CHECK(s.fauna_.hp[f] < 1000.0f);
			const float d = std::hypot(s.fauna_.pos_x[f] - s.units_.pos_x[uidx(s, spear)], s.fauna_.pos_z[f] - s.units_.pos_z[uidx(s, spear)]);
			std::printf("flee: deer behaviour %d, %.1f m from the spearman\n", s.fauna_.behavior[f], d);
			CHECK(s.fauna_.behavior[f] == FAUNA_FLEE || d > 4.0f);
		}
	}

	// 4. A boar (passive-defensive) ignores units until hit, then fights back.
	{
		fresh(s);
		uint32_t spear = s.spawn_unit(0.0f, 0.0f, 0, UNIT_SPEARMAN);
		uint32_t boar = s.spawn_fauna(2.0f, 0.0f, FAUNA_BOAR);
		const float hp0 = s.units_.hp[uidx(s, spear)];
		run(s, 5.0f);
		CHECK(s.units_.hp[uidx(s, spear)] == hp0);
		s.order_hunt(&spear, 1, boar);
		run(s, 30.0f);
		CHECK(!f_alive(s, boar));
		CHECK(s.units_.hp[uidx(s, spear)] < hp0);
		CHECK(meat_near(s, 2.0f, 0.0f, 8.0f) > 0 || s.units_.carry[uidx(s, spear)] > 0);
	}

	// 5. A defensive bear attacks units inside attack range + vision / 2, not further out.
	{
		const auto &bi = fauna_type_info(FAUNA_BEAR_BROWN);
		const float q = std::min(bi.attack_range + bi.vision * 0.5f, bi.vision);
		fresh(s);
		uint32_t far_v = s.spawn_unit(q + 3.0f, 0.0f, 0, UNIT_FEMALE_CITIZEN);
		uint32_t bear = s.spawn_fauna(0.0f, 0.0f, FAUNA_BEAR_BROWN);
		s.fauna_.state_timer[fidx(s, bear)] = 1000.0f; // keep it lingering in place
		const float hp_far = s.units_.hp[uidx(s, far_v)];
		run(s, 4.0f);
		CHECK(s.units_.hp[uidx(s, far_v)] == hp_far);
		CHECK(s.fauna_.behavior[fidx(s, bear)] != FAUNA_ATTACK);
		uint32_t near_v = s.spawn_unit(q - 3.0f, 2.0f, 0, UNIT_FEMALE_CITIZEN);
		const float hp_near = s.units_.hp[uidx(s, near_v)];
		run(s, 6.0f);
		CHECK(s.units_.hp[uidx(s, near_v)] < hp_near || !s.units_.alive[uidx(s, near_v)]);
	}

	// 6. An aggressive wolf attacks inside its vision; an idle soldier fights back and kills it.
	{
		fresh(s);
		uint32_t sword = s.spawn_unit(0.0f, 0.0f, 0, UNIT_SWORDSMAN);
		uint32_t wolf = s.spawn_fauna(15.0f, 0.0f, FAUNA_WOLF_ARCTIC);
		const float hp0 = s.units_.hp[uidx(s, sword)];
		run(s, 40.0f);
		CHECK(!f_alive(s, wolf));
		CHECK(s.units_.hp[uidx(s, sword)] < hp0);
		CHECK(s.units_.alive[uidx(s, sword)]);
		CHECK(meat_near(s, 0.0f, 0.0f, 30.0f) == 0); // wolves are not food in 0 A.D.
	}

	// 7. An animal's bite can kill: a lone woman against a violent wolf.
	{
		fresh(s);
		uint32_t w = s.spawn_unit(0.0f, 0.0f, 1, UNIT_FEMALE_CITIZEN);
		s.order_set_stance(&w, 1, STANCE_PASSIVE);
		s.spawn_fauna(5.0f, 0.0f, FAUNA_WOLF_ARCTIC_VIOLENT);
		run(s, 60.0f);
		CHECK(!s.units_.alive[uidx(s, w)]);
	}

	// 7b. FLEEING runs FleeDistance further than the attacker was (UnitAI.js distanceToFlee),
	// so a deer flees a ranged hunter too, and it turns from any new attacker (FLEEING.Attacked
	// measures the distance to the attack's target, which is the deer itself).
	{
		fresh(s);
		const auto &di = fauna_type_info(FAUNA_DEER);
		uint32_t far_u = s.spawn_unit(-10.0f, 0.0f, 0, UNIT_ARCHER);
		uint32_t near_u = s.spawn_unit(0.0f, 6.0f, 0, UNIT_SPEARMAN);
		uint32_t deer = s.spawn_fauna(0.0f, 0.0f, FAUNA_DEER);
		int32_t f = fidx(s, deer);
		s.fauna_attacked((size_t)f, far_u);
		CHECK(s.fauna_.behavior[f] == FAUNA_FLEE && s.fauna_.target_unit_id[f] == far_u);
		CHECK(std::fabs(s.fauna_.flee_distance[f] - (10.0f + di.flee_distance)) < 0.01f);
		s.fauna_attacked((size_t)f, near_u); // a new attacker: it now flees the spearman
		CHECK(s.fauna_.target_unit_id[f] == near_u);
		s.fauna_attacked((size_t)f, far_u); // and back to the archer, farther or not
		CHECK(s.fauna_.target_unit_id[f] == far_u);
		for (uint32_t u : {far_u, near_u}) s.units_.speed[uidx(s, u)] = 0.0f, s.cmd_stop(&u, 1);
		run(s, 10.0f);
		f = fidx(s, deer);
		const float d = std::hypot(s.fauna_.pos_x[f] - s.units_.pos_x[uidx(s, far_u)], s.fauna_.pos_z[f] - s.units_.pos_z[uidx(s, far_u)]);
		std::printf("flee: deer ran to %.1f from the archer (needed %.1f)\n", d, 10.0f + di.flee_distance);
		CHECK(d >= 10.0f + di.flee_distance - 0.5f && s.fauna_.behavior[f] != FAUNA_FLEE);
	}

	// 7c. Chase leash (ShouldAbandonChase): an animal has no held position, so a hold-ground
	// (passive-defensive) boar approaching its attacker keeps after it well past vision / 2 +
	// attack range of where it started and drops it only once it is out of the boar's vision;
	// then it lingers where it stands.
	{
		fresh(s);
		const auto &bi = fauna_type_info(FAUNA_BOAR);
		CHECK(bi.stance == FAUNA_STANCE_PASSIVE_DEFENSIVE); // respondHoldGround, like defensive
		uint32_t boar = s.spawn_fauna(0.0f, 0.0f, FAUNA_BOAR);
		const float far = bi.vision * 0.5f + bi.attack_range + 2.0f;
		uint32_t u = s.spawn_unit(far, 0.0f, 0, UNIT_SPEARMAN, 1.0e6f);
		s.order_set_stance(&u, 1, STANCE_PASSIVE);
		s.units_.speed[uidx(s, u)] = 0.0f;
		s.fauna_attacked((size_t)fidx(s, boar), u);
		// Past the old held-position leash, inside vision: it keeps approaching.
		run(s, 0.5f);
		CHECK(s.fauna_.behavior[fidx(s, boar)] == FAUNA_ATTACK && s.fauna_.pos_x[fidx(s, boar)] > 0.2f);
		// Out of its vision: it gives up and stays put.
		s.units_.pos_x[uidx(s, u)] = s.fauna_.pos_x[fidx(s, boar)] + bi.vision + 2.0f;
		run(s, 0.2f);
		const size_t b = fidx(s, boar);
		const float gx = s.fauna_.pos_x[b];
		CHECK(s.fauna_.behavior[b] == FAUNA_IDLE_GRAZE);
		run(s, 0.5f);
		CHECK(std::fabs(s.fauna_.pos_x[fidx(s, boar)] - gx) < 0.01f);
		std::printf("leash: boar vision %.1f, gave up at %.1f and lingered\n", bi.vision, bi.vision + 2.0f);
	}

	// 7d. respondChase false: once a passive-defensive boar has reached its attacker, a target
	// that steps out of reach is not chased. At its next swing (ATTACKING OutOfRange) it
	// finishes the order where it stands; a passive-defensive animal targets no one unprovoked.
	{
		fresh(s);
		const auto &bi = fauna_type_info(FAUNA_BOAR);
		uint32_t boar = s.spawn_fauna(0.0f, 0.0f, FAUNA_BOAR);
		uint32_t u = s.spawn_unit(1.5f, 0.0f, 0, UNIT_SPEARMAN, 1.0e6f);
		s.order_set_stance(&u, 1, STANCE_PASSIVE);
		s.units_.speed[uidx(s, u)] = 0.0f;
		s.fauna_attacked((size_t)fidx(s, boar), u);
		run(s, 1.5f);
		CHECK(s.fauna_.behavior[fidx(s, boar)] == FAUNA_ATTACK);
		CHECK(s.units_.hp[uidx(s, u)] < s.units_.max_hp[uidx(s, u)]); // it bit
		const float bx = s.fauna_.pos_x[fidx(s, boar)];
		s.units_.pos_x[uidx(s, u)] = bx + bi.attack_range + 4.0f; // out of reach, in vision
		run(s, bi.attack_cooldown + 0.3f);
		CHECK(s.fauna_.behavior[fidx(s, boar)] == FAUNA_IDLE_GRAZE && s.fauna_.target_unit_id[fidx(s, boar)] == 0);
		CHECK(std::fabs(s.fauna_.pos_x[fidx(s, boar)] - bx) < 0.01f);
		run(s, 1.0f);
		CHECK(s.fauna_.behavior[fidx(s, boar)] != FAUNA_ATTACK);
		// Hit again, it comes back for the attacker (Attacked -> AttackEntityInZone).
		s.fauna_attacked((size_t)fidx(s, boar), u);
		run(s, 0.5f);
		CHECK(s.fauna_.behavior[fidx(s, boar)] == FAUNA_ATTACK && s.fauna_.pos_x[fidx(s, boar)] > bx + 0.2f);
	}

	// 7e. A defensive lion in the same spot does not chase either, but FINDINGNEWTARGET looks
	// again within min(range + vision / 2, vision): a target just out of reach is taken up
	// again, one further out is left alone.
	{
		const auto &li = fauna_type_info(FAUNA_LION);
		CHECK(li.stance == FAUNA_STANCE_DEFENSIVE);
		const float q = std::min(li.attack_range + li.vision * 0.5f, li.vision);
		for (int further = 0; further < 2; ++further) {
			fresh(s);
			uint32_t lion = s.spawn_fauna(0.0f, 0.0f, FAUNA_LION);
			uint32_t u = s.spawn_unit(1.5f, 0.0f, 0, UNIT_SPEARMAN, 1.0e6f);
			s.order_set_stance(&u, 1, STANCE_PASSIVE);
			s.units_.speed[uidx(s, u)] = 0.0f;
			s.fauna_attacked((size_t)fidx(s, lion), u);
			run(s, 1.5f);
			CHECK(s.fauna_.behavior[fidx(s, lion)] == FAUNA_ATTACK);
			const float lx = s.fauna_.pos_x[fidx(s, lion)];
			const float gap = further ? std::min(q + 2.0f, li.vision - 0.5f) : li.attack_range + 3.0f;
			s.units_.pos_x[uidx(s, u)] = lx + gap;
			run(s, li.attack_cooldown + 0.6f);
			if (further) {
				CHECK(s.fauna_.behavior[fidx(s, lion)] != FAUNA_ATTACK);
				CHECK(std::fabs(s.fauna_.pos_x[fidx(s, lion)] - lx) < 0.01f);
			} else {
				CHECK(s.fauna_.behavior[fidx(s, lion)] == FAUNA_ATTACK && s.fauna_.target_unit_id[fidx(s, lion)] == u);
				run(s, 0.5f);
				CHECK(s.fauna_.pos_x[fidx(s, lion)] > lx + 0.2f);
			}
		}
		std::printf("hold ground: lion re-acquires within %.1f (vision %.1f)\n", q, li.vision);
	}

	// 7b. Attack.js timing and resistance: a wolf's first bite waits its PrepareTime (1 s), and
	// each bite is its damage against the victim's full resistance (unit_resistance).
	{
		fresh(s);
		uint32_t man = s.spawn_unit(0.0f, 0.0f, 0, UNIT_SPEARMAN);
		const int32_t u = uidx(s, man);
		s.units_.speed[u] = 0.0f;
		s.units_.attack_damage[u] = 0.0f; // it doesn't fight back here
		s.units_.max_hp[u] = s.units_.hp[u] = 1000.0f;
		uint32_t wolf = s.spawn_fauna(0.8f, 0.0f, FAUNA_WOLF);
		const auto &info = fauna_type_info(FAUNA_WOLF);
		CHECK(info.attack_prepare == 1.0f);
		s.fauna_attacked((size_t)fidx(s, wolf), man);
		CHECK(s.fauna_.behavior[fidx(s, wolf)] == FAUNA_ATTACK);
		run(s, 0.8f);
		CHECK(s.units_.hp[u] == 1000.0f); // still preparing
		float res[3];
		s.unit_resistance((size_t)u, res);
		const float bite = info.hack_damage * powf(0.9f, res[0]) + info.pierce_damage * powf(0.9f, res[1]) +
		                   info.crush_damage * powf(0.9f, res[2]);
		run(s, 0.4f);
		CHECK(std::fabs((1000.0f - s.units_.hp[u]) - std::max(1.0f, bite)) < 0.01f);
		std::printf("wolf bite: first after prepare, %.2f damage (resistance %.1f/%.1f/%.1f)\n",
		            1000.0f - s.units_.hp[u], res[0], res[1], res[2]);
	}

	// 8. Herding works for every player: player 1 herds its own sheep into its own corral.
	{
		fresh(s);
		uint32_t corral0 = s.spawn_building_world(10.0f, -6.0f, BLD_CORRAL, 0, 1.0f);
		uint32_t corral1 = s.spawn_building_world(-14.0f, 10.0f, BLD_CORRAL, 1, 1.0f);
		s.spawn_building_world(-40.0f, 30.0f, BLD_TOWN_CENTER, 1, 1.0f); // its territory keeps corral1 from decaying
		uint32_t herder = s.spawn_unit(0.0f, 0.0f, 1, UNIT_FEMALE_CITIZEN);
		uint32_t sheep = s.spawn_fauna(2.0f, 0.0f, FAUNA_SHEEP_TRAINABLE, 1);
		s.order_herd(&herder, 1, sheep);
		CHECK(s.fauna_.behavior[fidx(s, sheep)] == FAUNA_HERDED);
		run(s, 30.0f);
		int32_t f = fidx(s, sheep);
		CHECK(f >= 0 && s.fauna_.behavior[f] == FAUNA_CORRALLED);
		CHECK(f >= 0 && s.fauna_.corral_id[f] == corral1);
		CHECK(f >= 0 && s.fauna_.corral_id[f] != corral0);
		// A penned animal is out of reach until the corral lets it out; then it is slaughtered.
		s.order_herd(&herder, 1, sheep);
		run(s, 5.0f);
		CHECK(f_alive(s, sheep));
		s.cmd_unload(1, corral1, nullptr, 0);
		s.order_hunt(&herder, 1, sheep); // the herder has wandered off to work meanwhile
		for (int t = 0; t < 1200 && f_alive(s, sheep); ++t) run(s, Simulation::FIXED_STEP);
		CHECK(!f_alive(s, sheep));
	}

	// 8b. Herded livestock is a passive Domestic animal: an attacker that does not kill it
	// makes it flee (UnitAI respondFlee), which ends the herding. Penned livestock stays put.
	// Slaughter reaches 2 m, so an archer walks up to a sheep instead of killing it from afar.
	{
		fresh(s);
		s.spawn_building_world(-14.0f, 10.0f, BLD_CORRAL, 1, 1.0f);
		uint32_t herder = s.spawn_unit(0.0f, 0.0f, 1, UNIT_FEMALE_CITIZEN);
		uint32_t sheep = s.spawn_fauna(2.0f, 0.0f, FAUNA_SHEEP_TRAINABLE, 1);
		s.order_herd(&herder, 1, sheep);
		CHECK(s.fauna_.behavior[fidx(s, sheep)] == FAUNA_HERDED);
		uint32_t foe = s.spawn_unit(8.0f, 0.0f, 0, UNIT_ARCHER);
		s.fauna_attacked((size_t)fidx(s, sheep), foe);
		CHECK(s.fauna_.behavior[fidx(s, sheep)] == FAUNA_FLEE);
		run(s, 3.0f);
		const float d = std::hypot(s.fauna_.pos_x[fidx(s, sheep)] - 8.0f, s.fauna_.pos_z[fidx(s, sheep)]);
		CHECK(d > 8.0f);

		fresh(s);
		uint32_t pen = s.spawn_building_world(-14.0f, 10.0f, BLD_CORRAL, 1, 1.0f);
		uint32_t penned = s.spawn_fauna(-12.0f, 10.0f, FAUNA_SHEEP);
		s.fauna_.behavior[fidx(s, penned)] = FAUNA_CORRALLED;
		s.fauna_.corral_id[fidx(s, penned)] = pen;
		s.fauna_attacked((size_t)fidx(s, penned), foe);
		CHECK(s.fauna_.behavior[fidx(s, penned)] == FAUNA_CORRALLED);
		// A penned animal is garrisoned (GarrisonHolder List Animal): no one can slaughter it
		// over the fence; it heals 1 hp a second (BuffHeal); Unload all lets it out; a corral
		// below half health (EjectHealth 0.5) ejects its animals and takes no more.
		{
			uint32_t butcher = s.spawn_unit(-9.0f, 10.0f, 1, UNIT_FEMALE_CITIZEN);
			s.order_hunt(&butcher, 1, penned);
			CHECK(s.units_.target_unit_id[uidx(s, butcher)] != penned);
			uint32_t herd_ids[1] = {butcher};
			s.order_herd(herd_ids, 1, penned);
			s.fauna_.hp[fidx(s, penned)] -= 5.0f;
			const float hurt = s.fauna_.hp[fidx(s, penned)];
			run(s, 3.0f);
			CHECK(f_alive(s, penned) && s.fauna_.behavior[fidx(s, penned)] == FAUNA_CORRALLED);
			CHECK(std::fabs(s.fauna_.hp[fidx(s, penned)] - (hurt + 3.0f)) < 0.2f);
			CHECK(s.corral_count(pen) == 1);
			s.cmd_unload(0, pen, nullptr, 0); // not the owner
			CHECK(s.corral_count(pen) == 1);
			s.cmd_unload(1, pen, nullptr, 0);
			CHECK(s.corral_count(pen) == 0 && s.fauna_.corral_id[fidx(s, penned)] == 0);
			s.order_hunt(&butcher, 1, penned);
			for (int t = 0; t < 400 && f_alive(s, penned); ++t) run(s, Simulation::FIXED_STEP);
			CHECK(!f_alive(s, penned));

			uint32_t ewe = s.spawn_fauna(-12.0f, 11.0f, FAUNA_SHEEP_TRAINABLE, 1);
			s.fauna_.behavior[fidx(s, ewe)] = FAUNA_CORRALLED;
			s.fauna_.corral_id[fidx(s, ewe)] = pen;
			auto &pb = s.buildings_[s.building_id_to_idx_.at(pen)];
			pb.hp = pb.max_hp * 0.4f;
			run(s, 0.2f);
			CHECK(s.corral_count(pen) == 0 && s.fauna_.behavior[fidx(s, ewe)] != FAUNA_CORRALLED);
			uint32_t shepherd = s.spawn_unit(-10.0f, 13.0f, 1, UNIT_FEMALE_CITIZEN);
			s.order_herd(&shepherd, 1, ewe);
			run(s, 5.0f);
			CHECK(s.corral_count(pen) == 0);
		}

		fresh(s);
		uint32_t archer = s.spawn_unit(0.0f, 0.0f, 0, UNIT_ARCHER);
		uint32_t lamb = s.spawn_fauna(12.0f, 0.0f, FAUNA_SHEEP);
		s.order_hunt(&archer, 1, lamb);
		run(s, 0.5f);
		CHECK(f_alive(s, lamb)); // out of Slaughter range: still walking up
		float closest = 1e9f;
		for (int t = 0; t < 400 && f_alive(s, lamb); ++t) {
			const int32_t a = uidx(s, archer);
			closest = std::hypot(s.units_.pos_x[a] - 12.0f, s.units_.pos_z[a]);
			run(s, Simulation::FIXED_STEP);
		}
		CHECK(!f_alive(s, lamb));
		CHECK(closest < 4.0f);
		std::printf("livestock: herded sheep fled to %.1f from its attacker; archer slaughtered from %.1f\n", d, closest);
	}

	// 9. Animals roam and linger on their own, stay on land and inside the map.
	{
		fresh(s);
		for (int k = 0; k < 20; ++k) s.spawn_fauna(-20.0f + 2.0f * k, 20.0f, (uint8_t)(k % 4 == 3 ? FAUNA_GAZELLE : FAUNA_DEER));
		run(s, 120.0f);
		bool moved = false;
		for (size_t f = 0; f < s.fauna_.count; ++f) {
			CHECK(std::fabs(s.fauna_.pos_x[f]) <= s.map_half_ && std::fabs(s.fauna_.pos_z[f]) <= s.map_half_);
			if (std::fabs(s.fauna_.pos_z[f] - 20.0f) > 0.5f) moved = true;
		}
		CHECK(moved);
	}

	// 10. Save/load keeps fauna and the lockstep hash.
	{
		fresh(s);
		uint32_t cav = s.spawn_unit(0.0f, 0.0f, 0, UNIT_CAVALRY);
		uint32_t boar = s.spawn_fauna(8.0f, 0.0f, FAUNA_BOAR);
		s.spawn_fauna(-8.0f, 6.0f, FAUNA_BEAR_BLACK);
		s.order_hunt(&cav, 1, boar);
		run(s, 2.0f);
		const char *path = "/tmp/rts0_test_fauna.sav";
		CHECK(s.save_state(path));
		static Simulation t;
		CHECK(t.load_state(path));
		CHECK(TurnManager().boot_sim_hash(s) == TurnManager().boot_sim_hash(t));
		run(s, 10.0f);
		run(t, 10.0f);
		CHECK(TurnManager().boot_sim_hash(s) == TurnManager().boot_sim_hash(t));
		CHECK(s.fauna_.count == t.fauna_.count);
		std::remove(path);
	}

	// 11. Fruit bushes (template_gaia_fruit Regrowth 1 food / 6 s) regrow towards their Max,
	// gathered or not; meat and fish don't; an emptied bush is gone; Max survives save/load.
	{
		s.set_sim_seed(5);
		s.reset(); // no town centres: nobody trains gatherers
		uint32_t bush = s.spawn_resource_world(20.0f, -20.0f, RES_FOOD, 200);
		uint32_t meat = s.spawn_resource_world(-20.0f, 20.0f, RES_FOOD, 100, RES_SUB_MEAT);
		auto amount = [&](const Simulation &sim, uint32_t id) {
			auto it = sim.res_id_to_idx_.find(id);
			return it == sim.res_id_to_idx_.end() ? -1 : sim.resources_.amount[it->second];
		};
		s.resources_.amount[s.res_id_to_idx_[bush]] = 150;
		s.resources_.amount[s.res_id_to_idx_[meat]] = 50;
		run(s, 60.0f);
		CHECK(amount(s, bush) == 160);
		CHECK(amount(s, meat) == 50);
		run(s, 400.0f);
		CHECK(amount(s, bush) == 200); // capped at Max
		s.resources_.amount[s.res_id_to_idx_[bush]] = 120;
		const char *path = "/tmp/rts0_test_fruit.sav";
		CHECK(s.save_state(path));
		static Simulation t;
		CHECK(t.load_state(path));
		CHECK(t.resources_.max_amount[t.res_id_to_idx_[bush]] == 200);
		// The bush's timer stopped at Max; it starts again on the first tick that sees it below
		// (CheckTimers), so the fifth food comes a tick after 30 s. The timer survives save/load.
		run(s, 30.1f);
		run(t, 30.1f);
		CHECK(amount(s, bush) == 125 && amount(t, bush) == 125);
		CHECK(TurnManager().boot_sim_hash(s) == TurnManager().boot_sim_hash(t));
		std::remove(path);
		s.resources_.amount[s.res_id_to_idx_[bush]] = 0;
		run(s, 7.0f);
		CHECK(amount(s, bush) == -1);
	}

	// 12. Fauna audit fixes.
	{
		// A whale's carcass is food.fish (template_unit_fauna_hunt_whale: Max 2000, MaxGatherers 5)
		// that fishing ships gather, and it does not regrow like a shoal.
		fresh(s);
		uint32_t whale = s.spawn_fauna(30.0f, 0.0f, FAUNA_WHALE_FIN);
		s.fauna_.hp[fidx(s, whale)] = 0.0f;
		run(s, 0.1f);
		int32_t carcass = -1;
		for (size_t r = 0; r < s.resources_.count; ++r)
			if (std::fabs(s.resources_.x[r] - 30.0f) < 0.5f && std::fabs(s.resources_.z[r]) < 0.5f) carcass = (int32_t)r;
		CHECK(carcass >= 0);
		if (carcass >= 0) {
			CHECK(s.resources_.subtype[carcass] == RES_SUB_FISH && s.resources_.amount[carcass] == 2000);
			CHECK(s.resources_.max_gatherers[carcass] == 5);
			s.resources_.amount[carcass] = 500;
			run(s, 20.0f);
			CHECK(s.resources_.amount[carcass] == 500);
		}

		// Attack/Slaughter is a citizen's and a woman's (PrepareTime 0.9 s); a hero uses his own attack.
		fresh(s);
		uint32_t sheep = s.spawn_fauna(0.0f, 0.0f, FAUNA_SHEEP);
		uint32_t hero = s.spawn_unit(1.0f, 0.0f, 0, UNIT_HERO);
		uint32_t woman = s.spawn_unit(-1.0f, 0.0f, 0, UNIT_FEMALE_CITIZEN);
		s.order_hunt(&hero, 1, sheep);
		s.order_hunt(&woman, 1, sheep);
		CHECK(!s.slaughters_target((size_t)uidx(s, hero)) && s.slaughters_target((size_t)uidx(s, woman)));
		s.cmd_stop(&hero, 1); // the woman alone, for the timing
		run(s, 0.5f);
		CHECK(f_alive(s, sheep)); // the first strike waits PrepareTime
		run(s, 1.0f);
		CHECK(!f_alive(s, sheep));

		// Passive and skittish animals flee a tower's arrows (the structure is the attacker); a
		// boar ignores them, as a structure is a RestrictedClass of its bite.
		fresh(s);
		uint32_t tower = s.spawn_building_world(20.0f, 0.0f, BLD_TOWER, 1, 1.0f);
		uint32_t deer = s.spawn_fauna(10.0f, 0.0f, FAUNA_DEER);
		uint32_t boar = s.spawn_fauna(10.0f, 10.0f, FAUNA_BOAR);
		s.fauna_attacked((size_t)fidx(s, deer), tower);
		s.fauna_attacked((size_t)fidx(s, boar), tower);
		CHECK(s.fauna_.behavior[fidx(s, deer)] == FAUNA_FLEE);
		CHECK(s.fauna_.behavior[fidx(s, boar)] != FAUNA_ATTACK && s.fauna_.behavior[fidx(s, boar)] != FAUNA_FLEE);
		run(s, 1.0f);
		CHECK(s.fauna_.pos_x[fidx(s, deer)] < 9.0f); // running away from the tower

		// COMBAT "Attacked": a melee attacker takes over the fight; a ranged one does not.
		fresh(s);
		boar = s.spawn_fauna(0.0f, 0.0f, FAUNA_BOAR);
		uint32_t a = s.spawn_unit(1.5f, 0.0f, 0, UNIT_SPEARMAN, 1.0e6f);
		uint32_t b = s.spawn_unit(-1.5f, 0.0f, 0, UNIT_SWORDSMAN, 1.0e6f);
		uint32_t c = s.spawn_unit(0.0f, 8.0f, 0, UNIT_ARCHER, 1.0e6f);
		const int32_t bf = fidx(s, boar);
		s.fauna_attacked((size_t)bf, a);
		CHECK(s.fauna_.target_unit_id[bf] == a);
		s.fauna_attacked((size_t)bf, c, false);
		CHECK(s.fauna_.target_unit_id[bf] == a);
		s.fauna_attacked((size_t)bf, b, true);
		CHECK(s.fauna_.target_unit_id[bf] == b);

		// MoveRandomly: roaming describes circles, so an undisturbed deer stays near home.
		fresh(s);
		deer = s.spawn_fauna(0.0f, 0.0f, FAUNA_DEER);
		const float rd = fauna_type_info(FAUNA_DEER).roam_distance;
		float far = 0.0f;
		for (int k = 0; k < 60; ++k) {
			run(s, 5.0f);
			const int32_t f = fidx(s, deer);
			far = std::max(far, std::hypot(s.fauna_.pos_x[f], s.fauna_.pos_z[f]));
		}
		std::printf("roam: deer strayed at most %.1f (RoamDistance %.1f)\n", far, rd);
		CHECK(far > 0.5f * rd && far < 6.0f * rd);
	}

	// 13. Ownership: corral-trained livestock is its trainer's (Trainer.js spawns the
	// *_trainable template owned by the player). Only the owner herds it; anyone's Slaughter
	// takes it (Attack.CanAttack); other attacks only an enemy's; it gives no Loot xp; the
	// owner survives save/load.
	{
		fresh(s);
		// Beside player 1's civic centre: a structure in neutral land decays to Gaia (TerritoryDecay).
		uint32_t pen = s.spawn_building_world(40.0f, 40.0f, BLD_CORRAL, 1, 1.0f);
		s.players_[1].res[RES_FOOD] = 1000;
		s.cmd_breed_livestock(1, pen, FAUNA_SHEEP_TRAINABLE, 1);
		run(s, 70.0f);
		uint32_t mine = 0;
		for (size_t f = 0; f < s.fauna_.count; ++f)
			if (s.fauna_.alive[f] && s.fauna_.fauna_type[f] == FAUNA_SHEEP_TRAINABLE) mine = s.fauna_.id[f];
		CHECK(mine != 0 && s.fauna_owner(mine) == 1);

		// Gaia livestock can't be herded: the order slaughters it instead.
		uint32_t gaia_sheep = s.spawn_fauna(5.0f, 5.0f, FAUNA_SHEEP);
		uint32_t w1 = s.spawn_unit(4.0f, 5.0f, 1, UNIT_FEMALE_CITIZEN);
		s.order_herd(&w1, 1, gaia_sheep);
		CHECK(s.fauna_.behavior[fidx(s, gaia_sheep)] != FAUNA_HERDED);
		CHECK(s.units_.target_unit_id[uidx(s, w1)] == gaia_sheep);
		CHECK(s.fauna_owner(gaia_sheep) == GAIA_PLAYER);

		// Player 0 can't herd player 1's sheep, but its woman's Slaughter takes it.
		const uint32_t sheep1 = s.spawn_fauna(-5.0f, -5.0f, FAUNA_SHEEP_TRAINABLE, 1);
		uint32_t w0 = s.spawn_unit(-4.0f, -5.0f, 0, UNIT_FEMALE_CITIZEN);
		s.order_herd(&w0, 1, sheep1);
		CHECK(s.fauna_.behavior[fidx(s, sheep1)] != FAUNA_HERDED);
		CHECK(s.units_.target_unit_id[uidx(s, w0)] == sheep1);
		// A war dog has no Slaughter: it attacks an enemy's sheep but not its own player's.
		uint32_t dog1 = s.spawn_unit(-6.0f, -5.0f, 1, UNIT_WAR_DOG);
		s.order_hunt(&dog1, 1, sheep1);
		CHECK(s.units_.target_unit_id[uidx(s, dog1)] != sheep1);
		s.players_[1].team = 1;
		s.set_diplomacy(0, 1, Simulation::DIPLO_ENEMY);
		uint32_t dog0 = s.spawn_unit(-6.0f, -4.0f, 0, UNIT_WAR_DOG);
		s.order_hunt(&dog0, 1, sheep1);
		CHECK(s.units_.target_unit_id[uidx(s, dog0)] == sheep1);
		const uint16_t xp0 = s.units_.xp[uidx(s, w0)];
		for (int t = 0; t < 400 && f_alive(s, sheep1); ++t) run(s, Simulation::FIXED_STEP);
		CHECK(!f_alive(s, sheep1));
		CHECK(s.units_.xp[uidx(s, w0)] == xp0 && s.units_.xp[uidx(s, dog0)] == 0); // Loot disabled

		const char *path = "/tmp/rts0_test_fauna_owner.sav";
		CHECK(s.save_state(path));
		static Simulation t;
		CHECK(t.load_state(path));
		CHECK(t.fauna_owner(mine) == 1 && t.fauna_owner(gaia_sheep) == GAIA_PLAYER);
		CHECK(TurnManager().boot_sim_hash(s) == TurnManager().boot_sim_hash(t));
		std::remove(path);
	}

	// 14. Animals keep their 0 A.D. passability class Clearance from structures (pathfinder.xml):
	//     "default" 0.8 m, "large" 3 m for the bull and the adult elephants. A step that comes
	//     closer is refused; a step away is not. Through a 1.2-unit (4.8 m) gap in a row of houses
	//     a fleeing deer runs on, while a bush elephant charging a soldier on the far side is held
	//     back, and neither ever comes closer to a house than its clearance.
	{
		CHECK(fauna_type_info(FAUNA_CATTLE_BULL).is_large() && fauna_type_info(FAUNA_ELEPHANT_AFRICAN_BUSH).is_large());
		CHECK(fauna_type_info(FAUNA_ELEPHANT_ASIAN).is_large() && fauna_type_info(FAUNA_ELEPHANT_NORTH_AFRICAN).is_large());
		int large = 0;
		for (int f = 0; f < FAUNA_COUNT; ++f) large += fauna_type_info((uint8_t)f).is_large();
		CHECK(large == 4); // not the infants, the cows or the trainable cattle
		fresh(s);
		s.spawn_building_world(0.0f, 20.0f, BLD_HOUSE, 0, 1.0f); // half 1: x and z in [-1, 1] round its centre
		s.tick(Simulation::FIXED_STEP);
		const uint32_t deer = s.spawn_fauna(-1.5f, 20.0f, FAUNA_DEER);
		const uint32_t ele = s.spawn_fauna(-2.0f, 20.0f, FAUNA_ELEPHANT_AFRICAN_BUSH);
		CHECK(!s.fauna_can_step(fidx(s, deer), -1.15f, 20.0f) && s.fauna_can_step(fidx(s, deer), -1.25f, 20.0f));
		CHECK(!s.fauna_can_step(fidx(s, ele), -1.7f, 20.0f) && s.fauna_can_step(fidx(s, ele), -1.8f, 20.0f));
		s.fauna_.pos_x[fidx(s, ele)] = -1.5f; // too close already: it may step away, not closer
		CHECK(s.fauna_can_step(fidx(s, ele), -1.6f, 20.0f) && !s.fauna_can_step(fidx(s, ele), -1.4f, 20.0f));
		CHECK(s.fauna_can_step(fidx(s, ele), -1.5f, 20.1f)); // along the wall

		fresh(s);
		// Houses at z = 1 +- (1.6 + 2k): edges at z 0.4 and 1.6, a 1.2-unit gap round the middle of
		// the navcell row z in [0, 2], which no house covers, so the nav grid leaves it open.
		for (int k = 0; k < 6; ++k) {
			s.spawn_building_world(0.0f, 2.6f + 2.0f * k, BLD_HOUSE, 0, 1.0f);
			s.spawn_building_world(0.0f, -0.6f - 2.0f * k, BLD_HOUSE, 0, 1.0f);
		}
		const uint32_t chaser = s.spawn_unit(-9.0f, 1.0f, 0, UNIT_SPEARMAN);
		const uint32_t d2 = s.spawn_fauna(-5.0f, 1.0f, FAUNA_DEER);
		const uint32_t e2 = s.spawn_fauna(-5.0f, 0.95f, FAUNA_ELEPHANT_AFRICAN_BUSH);
		const uint32_t bait = s.spawn_unit(6.0f, 1.0f, 1, UNIT_SPEARMAN);
		for (uint32_t id : {chaser, bait}) s.units_.stance[s.lookup_unit(id)] = STANCE_PASSIVE; // they stay put
		s.tick(Simulation::FIXED_STEP);
		s.fauna_attacked(fidx(s, d2), chaser);
		s.fauna_attacked(fidx(s, e2), bait);
		auto closest = [&](uint32_t id) {
			const int32_t f = fidx(s, id);
			float g = 1e9f;
			for (const auto &b : s.buildings_) {
				if (b.type != BLD_HOUSE) continue;
				const float lx = std::fabs(s.fauna_.pos_x[f] - b.x) - b.hu, lz = std::fabs(s.fauna_.pos_z[f] - b.z) - b.hv;
				g = std::min(g, std::max(lx, lz));
			}
			return g;
		};
		float deer_gap = 1e9f, ele_gap = 1e9f, deer_x = -1e9f, ele_x = -1e9f;
		for (int t = 0; t < 200; ++t) {
			s.tick(Simulation::FIXED_STEP);
			deer_gap = std::min(deer_gap, closest(d2));
			ele_gap = std::min(ele_gap, closest(e2));
			deer_x = std::max(deer_x, s.fauna_.pos_x[fidx(s, d2)]);
			ele_x = std::max(ele_x, s.fauna_.pos_x[fidx(s, e2)]);
		}
		std::printf("fauna clearance: deer reached x %.2f (closest %.2f), bush elephant x %.2f (closest %.2f)\n",
				deer_x, deer_gap, ele_x, ele_gap);
		CHECK(deer_x > 1.0f);                     // through the gap
		CHECK(ele_x < -1.0f - CLEARANCE_LARGE + 0.01f && ele_x > -1.0f - CLEARANCE_LARGE - 0.2f); // held at its clearance
		CHECK(deer_gap >= CLEARANCE_DEFAULT - 1e-4f);
		CHECK(ele_gap >= CLEARANCE_LARGE - 1e-4f);
	}

	// 15. Owned livestock is a unit its owner orders (0 A.D. UnitAI "walk", "stop",
	// "garrison"): it walks where it is sent and feeds there, stops on Stop, goes into its
	// owner's corral, and ignores another player's orders and Gaia's animals ignore all.
	{
		fresh(s);
		uint32_t pen = s.spawn_building_world(40.0f, 40.0f, BLD_CORRAL, 1, 1.0f);
		const uint32_t ewe = s.spawn_fauna(30.0f, 30.0f, FAUNA_SHEEP_TRAINABLE, 1);
		const uint32_t ram = s.spawn_fauna(31.0f, 30.0f, FAUNA_SHEEP_TRAINABLE, 1);
		const uint32_t wild = s.spawn_fauna(32.0f, 30.0f, FAUNA_SHEEP);
		const uint32_t herd[3] = {ewe, ram, wild};
		CHECK(s.fauna_selectable_by(1, ewe) && !s.fauna_selectable_by(0, ewe) && !s.fauna_selectable_by(1, wild));
		s.order_fauna_walk(0, herd, 3, 20.0f, 20.0f); // not player 0's
		CHECK(s.fauna_.behavior[fidx(s, ewe)] != FAUNA_MOVING);
		s.order_fauna_walk(1, herd, 3, 20.0f, 30.0f);
		CHECK(s.fauna_.behavior[fidx(s, ewe)] == FAUNA_MOVING && s.fauna_.behavior[fidx(s, ram)] == FAUNA_MOVING);
		CHECK(s.fauna_.behavior[fidx(s, wild)] != FAUNA_MOVING);
		run(s, 12.0f);
		const float ex = s.fauna_.pos_x[fidx(s, ewe)], ez = s.fauna_.pos_z[fidx(s, ewe)];
		CHECK(std::hypot(ex - 20.0f, ez - 30.0f) < 2.5f && s.fauna_.behavior[fidx(s, ewe)] != FAUNA_MOVING);
		CHECK(std::hypot(s.fauna_.pos_x[fidx(s, wild)] - 20.0f, s.fauna_.pos_z[fidx(s, wild)] - 30.0f) > 6.0f);
		// Vision: player 1 sees round its own ewe, not round Gaia's sheep far from its things.
		run(s, 0.5f);
		CHECK(s.los_visible(1, ex, ez) && !s.los_visible(0, ex, ez));
		{
			const char *path = "/tmp/rts0_test_fauna_vision.sav";
			CHECK(s.save_state(path));
			static Simulation t;
			CHECK(t.load_state(path));
			CHECK(t.los_visible(1, ex, ez));
			CHECK(TurnManager().boot_sim_hash(s) == TurnManager().boot_sim_hash(t));
			std::remove(path);
		}
		// Stop: it feeds where it stands.
		s.order_fauna_walk(1, &ewe, 1, 0.0f, 30.0f);
		run(s, 1.0f);
		s.order_fauna_stop(1, &ewe, 1);
		CHECK(s.fauna_.behavior[fidx(s, ewe)] == FAUNA_IDLE_GRAZE);
		const float stop_x = s.fauna_.pos_x[fidx(s, ewe)];
		run(s, 0.5f);
		CHECK(std::fabs(s.fauna_.pos_x[fidx(s, ewe)] - stop_x) < 0.5f);
		// Garrison: into its owner's corral; penned, it is no longer selectable.
		s.order_fauna_walk(1, herd, 2, 0.0f, 0.0f, pen);
		run(s, 25.0f);
		CHECK(s.fauna_.behavior[fidx(s, ewe)] == FAUNA_CORRALLED && s.corral_count(pen) == 2);
		CHECK(!s.fauna_selectable_by(1, ewe));
		std::printf("livestock orders: ewe walked to (%.1f, %.1f), both penned\n", ex, ez);
	}

	// 13. 0.28 audit (fauna/audit-028-behaviour.md).
	{
		// a. COMBAT APPROACHING walks, even after a fleeing target; CHASING (after a first
		//    ATTACKING) runs after a fleeing one.
		fresh(s);
		const auto &wi = fauna_type_info(FAUNA_WOLF);
		uint32_t u = s.spawn_unit(6.0f, 0.0f, 0, UNIT_SPEARMAN);
		uint32_t wolf = s.spawn_fauna(0.0f, 0.0f, FAUNA_WOLF);
		s.fauna_start_attack((size_t)fidx(s, wolf), u);
		s.tick(Simulation::FIXED_STEP);
		int32_t w = fidx(s, wolf);
		// UnitMotion Acceleration: from rest it gains Acceleration x the tick, up to its walk speed.
		const float first = std::hypot(s.fauna_.vel_x[w], s.fauna_.vel_z[w]);
		CHECK(std::fabs(first - std::min(wi.speed, wi.acceleration * Simulation::FIXED_STEP)) < 0.01f);
		run(s, 0.5f);
		w = fidx(s, wolf);
		const float walk = std::hypot(s.fauna_.vel_x[w], s.fauna_.vel_z[w]);
		CHECK(std::fabs(walk - wi.speed) < 0.01f);
		uint32_t sheep = s.spawn_fauna(-6.0f, 0.0f, FAUNA_SHEEP_TRAINABLE, 1);
		s.fauna_attacked((size_t)fidx(s, sheep), wolf, true);
		CHECK(s.fauna_.behavior[fidx(s, sheep)] == FAUNA_FLEE);
		s.fauna_start_attack((size_t)fidx(s, wolf), sheep);
		run(s, 0.5f);
		w = fidx(s, wolf);
		CHECK(std::fabs(std::hypot(s.fauna_.vel_x[w], s.fauna_.vel_z[w]) - wi.speed) < 0.01f);
		// The wolf is defensive (no respondChase): after its first ATTACKING it gives up instead.
		// An aggressive mastiff that has struck once chases the fleeing sheep at a run.
		const auto &mi = fauna_type_info(FAUNA_DOG_MASTIFF);
		const uint32_t dog = s.spawn_fauna(0.0f, 4.0f, FAUNA_DOG_MASTIFF);
		s.fauna_start_attack((size_t)fidx(s, dog), sheep);
		s.fauna_.flee_distance[fidx(s, dog)] = 1.0f; // COMBAT ATTACKING once: the sheep got away
		run(s, 0.5f);
		const int32_t m = fidx(s, dog);
		const float run_sp = std::hypot(s.fauna_.vel_x[m], s.fauna_.vel_z[m]);
		CHECK(std::fabs(run_sp - mi.speed * mi.run_mult) < 0.01f);
		std::printf("chase: wolf walks at %.2f after a fleeing sheep, a mastiff that struck runs at %.2f\n", walk, run_sp);
	}
	{
		// b. A predator goes for a player's livestock out of its corral (attackfilter: owner > 0).
		fresh(s);
		uint32_t sheep = s.spawn_fauna(3.0f, 0.0f, FAUNA_SHEEP_TRAINABLE, 1);
		uint32_t wolf = s.spawn_fauna(0.0f, 0.0f, FAUNA_WOLF);
		run(s, 0.5f);
		CHECK(s.fauna_.behavior[fidx(s, wolf)] == FAUNA_ATTACK && s.fauna_.target_unit_id[fidx(s, wolf)] == sheep);
		// Bitten, it flees (passive Domestic) with the wolf after it at a run.
		run(s, 2.0f);
		CHECK(f_alive(s, sheep) && s.fauna_.hp[fidx(s, sheep)] < fauna_type_info(FAUNA_SHEEP_TRAINABLE).max_hp);
		CHECK(s.fauna_.behavior[fidx(s, sheep)] == FAUNA_FLEE);
		// One that cannot get away is eaten and leaves its carcass.
		s.fauna_.hp[fidx(s, sheep)] = 1.0f;
		s.fauna_.behavior[fidx(s, sheep)] = FAUNA_IDLE_GRAZE;
		s.fauna_.state_timer[fidx(s, sheep)] = 1000.0f;
		for (int t = 0; t < 200 && f_alive(s, sheep); ++t) s.tick(Simulation::FIXED_STEP);
		CHECK(!f_alive(s, sheep));
		CHECK(meat_near(s, s.fauna_.pos_x[fidx(s, wolf)], s.fauna_.pos_z[fidx(s, wolf)], 6.0f) > 0);
		// Penned livestock is garrisoned and out of reach.
		uint32_t pen = s.spawn_building_world(20.0f, 0.0f, BLD_CORRAL, 1, 1.0f);
		uint32_t ewe = s.spawn_fauna(20.0f, 0.0f, FAUNA_SHEEP_TRAINABLE, 1);
		s.fauna_.behavior[fidx(s, ewe)] = FAUNA_CORRALLED;
		s.fauna_.corral_id[fidx(s, ewe)] = pen;
		uint32_t wolf2 = s.spawn_fauna(22.0f, 0.0f, FAUNA_WOLF);
		run(s, 0.5f);
		CHECK(s.fauna_.target_unit_id[fidx(s, wolf2)] != ewe);
	}
	{
		// c. Identity/Requirements: pigs need the Town Phase, cattle the City Phase.
		fresh(s);
		s.players_[0].res[RES_FOOD] = 5000.0f;
		uint32_t pen = s.spawn_building_world(-40.0f, -40.0f, BLD_CORRAL, 0, 1.0f);
		CHECK(s.cmd_breed_livestock(0, pen, FAUNA_SHEEP_TRAINABLE, 1));
		CHECK(!s.cmd_breed_livestock(0, pen, FAUNA_PIG_TRAINABLE, 1));
		CHECK(!s.cmd_breed_livestock(0, pen, FAUNA_CATTLE_COW_TRAINABLE, 1));
		s.players_[0].phase = 2;
		CHECK(s.cmd_breed_livestock(0, pen, FAUNA_PIG_TRAINABLE, 1));
		CHECK(!s.cmd_breed_livestock(0, pen, FAUNA_CATTLE_COW_TRAINABLE, 1));
		s.players_[0].phase = 3;
		CHECK(s.cmd_breed_livestock(0, pen, FAUNA_CATTLE_COW_TRAINABLE, 1));
	}
	{
		// d. A civic centre takes no animals: herded livestock stops beside it, unpenned.
		fresh(s);
		uint32_t herder = s.spawn_unit(44.0f, 42.0f, 1, UNIT_FEMALE_CITIZEN);
		uint32_t sheep = s.spawn_fauna(45.0f, 42.0f, FAUNA_SHEEP_TRAINABLE, 1);
		s.order_herd(&herder, 1, sheep);
		run(s, 30.0f);
		CHECK(f_alive(s, sheep) && s.fauna_.behavior[fidx(s, sheep)] != FAUNA_CORRALLED);
	}

	// 14. 0.28 audit (fauna/audit-028.md) fixes.
	{
		// e. UnitAI's attack filter takes any entity an enemy owns: a soldier goes for its sheep.
		fresh(s);
		uint32_t u = s.spawn_unit(0.0f, 0.0f, 0, UNIT_SPEARMAN);
		uint32_t sheep = s.spawn_fauna(4.0f, 0.0f, FAUNA_SHEEP_TRAINABLE, 1);
		s.fauna_.behavior[fidx(s, sheep)] = FAUNA_IDLE_GRAZE;
		s.fauna_.state_timer[fidx(s, sheep)] = 1000.0f;
		run(s, 0.5f);
		CHECK(s.units_.target_unit_id[uidx(s, u)] == sheep);
		// Its own livestock, and an ally's, are left alone.
		fresh(s);
		uint32_t u2 = s.spawn_unit(0.0f, 0.0f, 0, UNIT_SPEARMAN);
		uint32_t own = s.spawn_fauna(4.0f, 0.0f, FAUNA_SHEEP_TRAINABLE, 0);
		run(s, 0.5f);
		CHECK(s.units_.target_unit_id[uidx(s, u2)] != own);
		s.set_mutual_ally(0, 1);
		uint32_t allied = s.spawn_fauna(-4.0f, 0.0f, FAUNA_SHEEP_TRAINABLE, 1);
		run(s, 0.5f);
		CHECK(s.units_.target_unit_id[uidx(s, u2)] != allied);
	}
	{
		// f. EntityLimits counts the player's livestock wherever it is, not only when penned.
		fresh(s);
		const int base = s.entity_limit_count(1, LIMIT_ANIMAL);
		s.spawn_fauna(10.0f, 10.0f, FAUNA_SHEEP_TRAINABLE, 1);
		s.spawn_fauna(12.0f, 10.0f, FAUNA_GOAT_TRAINABLE, 1);
		s.spawn_fauna(14.0f, 10.0f, FAUNA_SHEEP_TRAINABLE, 0);
		CHECK(s.entity_limit_count(1, LIMIT_ANIMAL) == base + 2);
		uint32_t pen = s.spawn_building_world(20.0f, 20.0f, BLD_CORRAL, 1, 1.0f);
		uint32_t gaia = s.spawn_fauna(20.0f, 20.0f, FAUNA_SHEEP);
		s.fauna_.behavior[fidx(s, gaia)] = FAUNA_CORRALLED;
		s.fauna_.corral_id[fidx(s, gaia)] = pen;
		CHECK(s.entity_limit_count(1, LIMIT_ANIMAL) == base + 3);
	}
	{
		// g. A captured corral ejects the livestock its new owner may not hold.
		fresh(s);
		uint32_t pen = s.spawn_building_world(20.0f, 20.0f, BLD_CORRAL, 1, 1.0f);
		uint32_t ewe = s.spawn_fauna(20.0f, 20.0f, FAUNA_SHEEP_TRAINABLE, 1);
		s.fauna_.behavior[fidx(s, ewe)] = FAUNA_CORRALLED;
		s.fauna_.corral_id[fidx(s, ewe)] = pen;
		auto *b = &s.buildings_[s.building_id_to_idx_.at(pen)];
		CHECK(b->max_capture_points > 0.0f);
		b->capture_points_by_team.fill(0.0f);
		b->capture_points_by_team[0] = b->max_capture_points;
		s.tick(Simulation::FIXED_STEP);
		b = &s.buildings_[s.building_id_to_idx_.at(pen)];
		CHECK(b->team == 0);
		CHECK(f_alive(s, ewe) && s.fauna_.behavior[fidx(s, ewe)] != FAUNA_CORRALLED);
		CHECK(s.corral_count(pen) == 0);
	}
	{
		// h. GarrisonHolder IsOwnedByMutualAllyOfEntity: livestock walks into an ally's corral.
		fresh(s);
		s.set_mutual_ally(0, 1);
		uint32_t pen = s.spawn_building_world(20.0f, 20.0f, BLD_CORRAL, 0, 1.0f);
		uint32_t ewe = s.spawn_fauna(16.0f, 20.0f, FAUNA_SHEEP_TRAINABLE, 1);
		s.order_fauna_walk(1, &ewe, 1, 20.0f, 20.0f, pen);
		run(s, 10.0f);
		CHECK(f_alive(s, ewe) && s.fauna_.behavior[fidx(s, ewe)] == FAUNA_CORRALLED);
		CHECK(s.corral_count(pen) == 1);
		// An enemy's corral is only walked to.
		fresh(s);
		uint32_t pen2 = s.spawn_building_world(20.0f, 20.0f, BLD_CORRAL, 0, 1.0f);
		uint32_t ewe2 = s.spawn_fauna(16.0f, 20.0f, FAUNA_SHEEP_TRAINABLE, 1);
		s.order_fauna_walk(1, &ewe2, 1, 20.0f, 20.0f, pen2);
		run(s, 10.0f);
		CHECK(f_alive(s, ewe2) && s.fauna_.behavior[fidx(s, ewe2)] != FAUNA_CORRALLED);
	}
	{
		// i. FINDINGNEWTARGET: a bear hunter goes on to the next bear (same template), not a
		// stranger's; and CheckTargetVisible: no animal the hunter can't see now.
		fresh(s);
		uint32_t h = s.spawn_unit(0.0f, 0.0f, 0, UNIT_SPEARMAN);
		uint32_t bear = s.spawn_fauna(6.0f, 0.0f, FAUNA_BEAR_BROWN);
		run(s, 0.5f);
		const int32_t hi = uidx(s, h);
		CHECK(s.find_nearby_prey((size_t)hi, 0.0f, 0.0f, 400.0f) == 0);
		CHECK(s.find_nearby_prey((size_t)hi, 0.0f, 0.0f, 400.0f, FAUNA_BEAR_BROWN + 1) == bear);
		uint32_t far_deer = s.spawn_fauna(-30.0f, 30.0f, FAUNA_DEER);
		run(s, 0.5f);
		CHECK(!s.los_visible(0, -30.0f, 30.0f));
		CHECK(s.find_nearby_prey((size_t)uidx(s, h), -30.0f, 28.0f, 400.0f) != far_deer);
		s.set_reveal_map(true);
		CHECK(s.find_nearby_prey((size_t)uidx(s, h), -30.0f, 28.0f, 400.0f) == far_deer);
	}

	{
		// j. Animals use the long pathfinder (UnitMotion): out of a pocket of houses and round it.
		auto pocket = [](Simulation &sim) {
			// A U of houses open to -x: back row at x = 8, arms along z = +-8 back to x = -2.
			for (int k = -4; k <= 4; ++k) sim.spawn_building_world(8.0f, 2.0f * k, BLD_HOUSE, 0, 1.0f);
			for (int k = 0; k < 5; ++k) {
				sim.spawn_building_world(6.0f - 2.0f * k, 8.0f, BLD_HOUSE, 0, 1.0f);
				sim.spawn_building_world(6.0f - 2.0f * k, -8.0f, BLD_HOUSE, 0, 1.0f);
			}
		};
		fresh(s);
		pocket(s);
		uint32_t ewe = s.spawn_fauna(4.0f, 0.0f, FAUNA_SHEEP_TRAINABLE, 1);
		s.order_fauna_walk(1, &ewe, 1, 16.0f, 0.0f);
		run(s, 40.0f);
		int32_t e = fidx(s, ewe);
		std::printf("pathing: ewe ended at (%.1f, %.1f) for (16, 0)\n", s.fauna_.pos_x[e], s.fauna_.pos_z[e]);
		CHECK(std::hypot(s.fauna_.pos_x[e] - 16.0f, s.fauna_.pos_z[e]) < 1.0f);
		// A wolf in the pocket chases a soldier standing behind it.
		fresh(s);
		pocket(s);
		uint32_t u = s.spawn_unit(16.0f, 0.0f, 1, UNIT_SPEARMAN);
		s.order_set_stance(&u, 1, STANCE_STAND_GROUND);
		uint32_t wolf = s.spawn_fauna(4.0f, 0.0f, FAUNA_WOLF);
		s.fauna_start_attack((size_t)fidx(s, wolf), u);
		float closest = 1e9f;
		for (int t = 0; t < 600 && f_alive(s, wolf); ++t) {
			s.tick(Simulation::FIXED_STEP);
			const int32_t w = fidx(s, wolf), ui = uidx(s, u);
			if (w < 0 || ui < 0) break;
			closest = std::min(closest, std::hypot(s.fauna_.pos_x[w] - s.units_.pos_x[ui], s.fauna_.pos_z[w] - s.units_.pos_z[ui]));
		}
		std::printf("pathing: wolf got within %.1f of the spearman\n", closest);
		CHECK(closest < fauna_type_info(FAUNA_WOLF).attack_range + 1.0f);
	}

	{
		// k. GATHER/COMBAT MovementUpdate likelyFailure: a hunter whose path ends on the shore,
		// short of an animal across deep water, gives the hunt up instead of standing there.
		s.set_sim_seed(7);
		s.reset();
		s.set_map_half(64.0f);
		const int res = 65;
		std::vector<float> h((size_t)res * res);
		for (int j = 0; j < res; ++j)
			for (int k = 0; k < res; ++k) h[(size_t)j * res + k] = std::fabs(-64.0f + 128.0f * j / (res - 1)) < 16.0f ? -3.0f : 3.0f;
		s.set_terrain_data(h, res, 0.0f);
		s.begin_bulk_spawn();
		s.end_bulk_spawn();
		for (uint8_t p = 0; p < 2; ++p) s.set_player_active(p, true, false);
		s.spawn_building_world(-40.0f, -40.0f, BLD_TOWN_CENTER, 0, 1.0f);
		uint32_t u = s.spawn_unit(0.0f, -22.0f, 0, UNIT_SPEARMAN);
		uint32_t pig = s.spawn_fauna(0.0f, 24.0f, FAUNA_PIG);
		s.order_hunt(&u, 1, pig);
		CHECK(s.units_.target_unit_id[uidx(s, u)] == pig);
		run(s, 15.0f);
		const int32_t ui = uidx(s, u);
		std::printf("unreachable hunt: hunter at (%.1f, %.1f), state %d\n", s.units_.pos_x[ui], s.units_.pos_z[ui], s.units_.action_state[ui]);
		CHECK(s.units_.target_unit_id[ui] == 0 && s.units_.action_state[ui] == ACTION_IDLE);
	}

	{
		// l. A hunter on a path stops for the animal, not within its slaughter range of a waypoint:
		// a path whose first waypoint lay 1.9 away left a Petra cavalry hunter standing there for good.
		fresh(s);
		uint32_t cav = s.spawn_unit(0.0f, 0.0f, 0, UNIT_CAVALRY);
		uint32_t deer = s.spawn_fauna(30.0f, 0.0f, FAUNA_DEER);
		s.order_hunt(&cav, 1, deer);
		s.tick(Simulation::FIXED_STEP);
		const int32_t c = uidx(s, cav);
		WaypointPath &wp = s.unit_paths_[c];
		const float x0 = s.units_.pos_x[c], z0 = s.units_.pos_z[c];
		wp.clear();
		wp.xs[0] = x0; wp.zs[0] = z0 + 1.9f;
		wp.xs[1] = x0 + 10.0f; wp.zs[1] = z0 + 1.9f;
		wp.xs[2] = s.units_.target_x[c]; wp.zs[2] = s.units_.target_z[c];
		wp.count = 3;
		wp.cur = 0;
		wp.has_goal = true;
		wp.goal_x = s.units_.target_x[c];
		wp.goal_z = s.units_.target_z[c];
		s.units_.waypoint_cur[c] = 0;
		s.units_.waypoint_count[c] = 3;
		run(s, 2.0f);
		const float mx = s.units_.pos_x[c] - x0, mz = s.units_.pos_z[c] - z0;
		std::printf("hunt path: hunter moved %.1f\n", std::sqrt(mx * mx + mz * mz));
		CHECK(mx * mx + mz * mz > 4.0f);
		run(s, 30.0f);
		CHECK(!f_alive(s, deer));
	}

	// 16. Pass-6 livestock audit against 0.28.
	{
		// a. A walk order is forced (UnitAI.js Walk force: true): attacked, livestock keeps going.
		fresh(s);
		const uint32_t ewe = s.spawn_fauna(10.0f, 0.0f, FAUNA_SHEEP_TRAINABLE, 1);
		uint32_t wolf = s.spawn_fauna(8.0f, 0.0f, FAUNA_WOLF);
		s.order_fauna_walk(1, &ewe, 1, 30.0f, 0.0f);
		s.fauna_attacked((size_t)fidx(s, ewe), wolf, true);
		CHECK(s.fauna_.behavior[fidx(s, ewe)] == FAUNA_MOVING);

		// b. Livestock trained at a corral with a rally point walks to it (Trainer.js rally).
		fresh(s);
		const uint32_t pen = s.spawn_building_world(40.0f, 40.0f, BLD_CORRAL, 1, 1.0f);
		s.cmd_set_rally_point(1, pen, 20.0f, 40.0f);
		s.players_[1].res[RES_FOOD] = 1000;
		s.cmd_breed_livestock(1, pen, FAUNA_SHEEP_TRAINABLE, 1);
		uint32_t bred = 0;
		for (int t = 0; t < 2400 && !bred; ++t) {
			s.tick(Simulation::FIXED_STEP);
			for (size_t f = 0; f < s.fauna_.count; ++f)
				if (s.fauna_.alive[f] && s.fauna_.fauna_type[f] == FAUNA_SHEEP_TRAINABLE) bred = s.fauna_.id[f];
		}
		CHECK(bred != 0 && s.fauna_.behavior[fidx(s, bred)] == FAUNA_MOVING);
		run(s, 15.0f);
		CHECK(bred != 0 && std::hypot(s.fauna_.pos_x[fidx(s, bred)] - 20.0f, s.fauna_.pos_z[fidx(s, bred)] - 40.0f) < 3.0f);

		// c. Defeat hands the player's livestock and carcasses to Gaia (Player.js OnGlobalPlayerDefeated).
		fresh(s);
		const uint32_t sheep = s.spawn_fauna(10.0f, 10.0f, FAUNA_SHEEP_TRAINABLE, 1);
		const uint32_t goat = s.spawn_fauna(12.0f, 10.0f, FAUNA_SHEEP_TRAINABLE, 1);
		s.order_fauna_walk(1, &goat, 1, 30.0f, 10.0f);
		const uint32_t hunter = s.spawn_unit(10.0f, 11.0f, 1, UNIT_FEMALE_CITIZEN);
		s.order_hunt(&hunter, 1, sheep);
		for (int t = 0; t < 400 && f_alive(s, sheep); ++t) run(s, Simulation::FIXED_STEP);
		int carcass = -1;
		for (size_t r = 0; r < s.resources_.count; ++r)
			if (s.resources_.alive[r] && s.resources_.owner[r] == 1) carcass = (int)r;
		CHECK(!f_alive(s, sheep) && carcass >= 0);
		s.defeat_player(1);
		CHECK(s.fauna_owner(goat) == GAIA_PLAYER && s.fauna_.behavior[fidx(s, goat)] != FAUNA_MOVING);
		CHECK(carcass >= 0 && s.resources_.owner[carcass] == GAIA_PLAYER);
		std::printf("livestock audit: walk kept under attack, rally followed, defeat hands animals to Gaia\n");
	}

	// 17. MoveRandomly: roamAngle (+-30 degrees) is drawn on the first roam and kept, its sign
	//     redrawn when the heading comes back round; save/load keeps it.
	{
		fresh(s);
		const uint32_t deer = s.spawn_fauna(0.0f, 0.0f, FAUNA_DEER);
		CHECK(s.fauna_roam_angle_[fidx(s, deer)] == 0.0f);
		int flips = 0;
		float last = 0.0f;
		for (int k = 0; k < 600; ++k) {
			run(s, 1.0f);
			const float a = s.fauna_roam_angle_[fidx(s, deer)];
			if (last != 0.0f && a != last) ++flips;
			if (a != 0.0f) last = a;
		}
		CHECK(std::fabs(std::fabs(last) - 3.14159265f / 6.0f) < 1e-5f);
		std::printf("roam: roamAngle %.3f, %d sign flips in 10 minutes\n", last, flips);
		const char *path = "/tmp/rts0_test_fauna_roam.sav";
		CHECK(s.save_state(path));
		static Simulation t;
		CHECK(t.load_state(path));
		CHECK(t.fauna_roam_angle_[fidx(t, deer)] == s.fauna_roam_angle_[fidx(s, deer)] &&
		      t.fauna_start_angle_[fidx(t, deer)] == s.fauna_start_angle_[fidx(s, deer)]);
		CHECK(TurnManager().boot_sim_hash(s) == TurnManager().boot_sim_hash(t));
		std::remove(path);
	}

	if (failures) {
		std::printf("test_fauna: %d failure(s)\n", failures);
		return 1;
	}
	std::printf("test_fauna: all passed\n");
	return 0;
}
