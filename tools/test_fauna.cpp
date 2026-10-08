// 0 A.D. fauna (templates/gaia/fauna_*.xml, UnitAI.js animal stances): the 61-species table,
// hunting (units kill an animal, then gather its carcass), stances (skittish and passive animals
// flee when hit, defensive ones attack inside range + vision / 2, aggressive ones inside vision,
// passive-defensive ones only fight back), soldiers fighting back, and herding for any player.
//   g++ -std=c++20 -O2 -Isrc tools/test_fauna.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//       src/network/net_host.cpp -lpthread -o /tmp/test_fauna && /tmp/test_fauna
#define private public
#include "simulation/simulation.h"
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
		CHECK(s.units_.xp[c] == xp0 + 5);
		const bool gathering = s.units_.action_state[c] == ACTION_GATHER || s.units_.action_state[c] == ACTION_RETURN_DROPSITE;
		CHECK(gathering);
		CHECK(s.units_.carry[c] > 0 || s.units_.res_type_carried[c] == RES_FOOD);
		std::printf("hunt: hunter state %d carry %d\n", s.units_.action_state[c], s.units_.carry[c]);
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

	// 8. Herding works for every player: player 1 herds a sheep into its own corral.
	{
		fresh(s);
		uint32_t corral0 = s.spawn_building_world(10.0f, -6.0f, BLD_CORRAL, 0, 1.0f);
		uint32_t corral1 = s.spawn_building_world(-14.0f, 10.0f, BLD_CORRAL, 1, 1.0f);
		uint32_t herder = s.spawn_unit(0.0f, 0.0f, 1, UNIT_FEMALE_CITIZEN);
		uint32_t sheep = s.spawn_fauna(2.0f, 0.0f, FAUNA_SHEEP);
		s.order_herd(&herder, 1, sheep);
		CHECK(s.fauna_.behavior[fidx(s, sheep)] == FAUNA_HERDED);
		run(s, 30.0f);
		int32_t f = fidx(s, sheep);
		CHECK(f >= 0 && s.fauna_.behavior[f] == FAUNA_CORRALLED);
		CHECK(f >= 0 && s.fauna_.corral_id[f] == corral1);
		CHECK(f >= 0 && s.fauna_.corral_id[f] != corral0);
		// Ordering a penned animal slaughters it.
		s.order_herd(&herder, 1, sheep);
		run(s, 20.0f);
		CHECK(!f_alive(s, sheep));
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

	if (failures) {
		std::printf("test_fauna: %d failure(s)\n", failures);
		return 1;
	}
	std::printf("test_fauna: all passed\n");
	return 0;
}
