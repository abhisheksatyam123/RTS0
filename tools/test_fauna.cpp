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

	// 7b. FLEEING runs FleeDistance further than the attacker was (UnitAI.js distanceToFlee),
	// so a deer flees a ranged hunter too, and it only turns from a nearer new attacker.
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
		s.fauna_attacked((size_t)f, near_u); // nearer: it now flees the spearman
		CHECK(s.fauna_.target_unit_id[f] == near_u);
		s.fauna_attacked((size_t)f, far_u); // farther: no change
		CHECK(s.fauna_.target_unit_id[f] == near_u);
		for (uint32_t u : {far_u, near_u}) s.units_.speed[uidx(s, u)] = 0.0f, s.cmd_stop(&u, 1);
		run(s, 10.0f);
		f = fidx(s, deer);
		const float d = std::hypot(s.fauna_.pos_x[f] - s.units_.pos_x[uidx(s, near_u)], s.fauna_.pos_z[f] - s.units_.pos_z[uidx(s, near_u)]);
		std::printf("flee: deer ran to %.1f from the spearman (needed %.1f)\n", d, 6.0f + di.flee_distance);
		CHECK(d >= 6.0f + di.flee_distance - 0.5f && s.fauna_.behavior[f] != FAUNA_FLEE);
	}

	// 7c. Hold ground (CheckTargetDistanceFromHeldPosition): a boar drops a target that is
	// more than vision / 2 + attack range from where it was attacked.
	{
		fresh(s);
		const auto &bi = fauna_type_info(FAUNA_BOAR);
		const float leash = bi.vision * 0.5f + bi.attack_range;
		uint32_t boar = s.spawn_fauna(0.0f, 0.0f, FAUNA_BOAR);
		uint32_t u = s.spawn_unit(1.5f, 0.0f, 0, UNIT_SPEARMAN, 1.0e6f);
		s.order_set_stance(&u, 1, STANCE_PASSIVE);
		s.fauna_attacked((size_t)fidx(s, boar), u);
		run(s, 1.0f);
		CHECK(s.fauna_.behavior[fidx(s, boar)] == FAUNA_ATTACK);
		s.units_.pos_x[uidx(s, u)] = leash + 1.0f; // out of the leash
		s.cmd_stop(&u, 1);
		s.units_.speed[uidx(s, u)] = 0.0f;
		run(s, 0.5f);
		CHECK(s.fauna_.behavior[fidx(s, boar)] != FAUNA_ATTACK);
		std::printf("leash: boar leash %.1f, gave up %s\n", leash, s.fauna_.behavior[fidx(s, boar)] != FAUNA_ATTACK ? "yes" : "no");
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

	// 8b. Herded livestock is a passive Domestic animal: an attacker that does not kill it
	// makes it flee (UnitAI respondFlee), which ends the herding. Penned livestock stays put.
	// Slaughter reaches 2 m, so an archer walks up to a sheep instead of killing it from afar.
	{
		fresh(s);
		s.spawn_building_world(-14.0f, 10.0f, BLD_CORRAL, 1, 1.0f);
		uint32_t herder = s.spawn_unit(0.0f, 0.0f, 1, UNIT_FEMALE_CITIZEN);
		uint32_t sheep = s.spawn_fauna(2.0f, 0.0f, FAUNA_SHEEP);
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

	if (failures) {
		std::printf("test_fauna: %d failure(s)\n", failures);
		return 1;
	}
	std::printf("test_fauna: all passed\n");
	return 0;
}
