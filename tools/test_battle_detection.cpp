// 0 A.D. BattleDetection.js: the sim counts the damage each player's entities deal to
// other players (OnGlobalAttacked: not to Gaia, not to themselves), and the detector
// (template_player.xml: 200 ms periods, 12-period record, 0.04 hp/ms) turns it into the
// BATTLE / PEACE state that picks the music. Each hit is also an Attacked event for
// AttackDetection (the under-attack alerts). Melee hits are marked so the session plays
// the attacker's attack_melee sound on them; attack_sound_data.h holds each template's
// 0 A.D. combat sound groups (tools/extract_0ad_attack_sounds.py).
//   g++ -std=c++20 -O2 -Isrc tools/test_battle_detection.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp -lpthread \
//       -o /tmp/test_battle_detection && /tmp/test_battle_detection
#define private public
#include "simulation/simulation.h"
#undef private
#include "presentation/attack_detection.h"
#include <cmath>
#include <cstdio>
#include <cstring>
namespace rtso {
#include "presentation/attack_sound_data.h"
}

using namespace rtso;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static void fresh(Simulation &s) {
	s.set_sim_seed(4);
	s.reset();
	s.set_victory_condition(VICTORY_ENDLESS);
	for (uint8_t p = 0; p < 2; ++p) s.players_[p].active = 1, s.players_[p].is_ai = 0;
	s.spawn_building_world(-50.0f, -50.0f, BLD_TOWN_CENTER, 0, 1.0f);
	s.spawn_building_world(50.0f, 50.0f, BLD_TOWN_CENTER, 1, 1.0f);
}

int main() {
	static Simulation s;

	// 1. The sim counts what each player deals to another player, and nothing for damage
	//    to Gaia or to its own side.
	{
		fresh(s);
		s.note_attacked(1, 0, 2, 1, false, 10.0f);
		s.note_attacked(1, 0, 3, GAIA_PLAYER, false, 50.0f);
		s.note_attacked(1, 0, 4, 0, true, 50.0f);
		s.note_attacked(5, GAIA_PLAYER, 1, 0, false, 50.0f);
		CHECK(s.battle_damage_dealt(0) == 10.0);
		CHECK(s.battle_damage_dealt(1) == 0.0);
	}

	// 2. Two swordsmen fighting: each side's count grows by what it dealt, and a passive
	//    enemy that never strikes back deals nothing.
	{
		fresh(s);
		const uint32_t a = s.spawn_unit(0.0f, 0.0f, 0, UNIT_SWORDSMAN);
		const uint32_t b = s.spawn_unit(1.0f, 0.0f, 1, UNIT_SWORDSMAN);
		s.units_.stance[s.lookup_unit(b)] = STANCE_PASSIVE;
		s.order_attack(&a, 1, b, false);
		const float hp0 = s.units_.hp[s.lookup_unit(b)];
		for (int t = 0; t < 100; ++t) s.tick(Simulation::FIXED_STEP);
		const int32_t bi = s.lookup_unit(b);
		const float lost = bi >= 0 ? hp0 - std::max(0.0f, s.units_.hp[bi]) : hp0;
		CHECK(s.battle_damage_dealt(0) > 0.0);
		CHECK(s.battle_damage_dealt(0) + 1e-3 >= lost);
		CHECK(s.battle_damage_dealt(1) == 0.0);
		std::printf("swordsman dealt %.1f damage in 5 s\n", s.battle_damage_dealt(0));
	}

	// 3. The detector's threshold is 0.04 hp/ms, 40 hp a second: 30 a second stays at
	//    peace, 100 a second reaches BATTLE (alertness 4), and calm brings PEACE back once
	//    alertness falls to 0.
	{
		BattleDetection d;
		double t = 0.0;
		for (int k = 0; k < 50; ++k, t += 0.1) { d.add_damage(3.0f, t); d.update(t); }
		CHECK(!d.in_battle());
		BattleDetection e;
		t = 0.0;
		int ticks_to_battle = -1;
		for (int k = 0; k < 200; ++k, t += 0.1) {
			e.add_damage(10.0f, t);
			e.update(t);
			if (e.in_battle() && ticks_to_battle < 0) ticks_to_battle = k;
		}
		CHECK(e.in_battle());
		for (int k = 0; k < 400; ++k, t += 0.1) e.update(t);
		CHECK(!e.in_battle());
		std::printf("100 hp/s: battle after %.1f s; peace again once calm\n", ticks_to_battle * 0.1);
	}

	// 4. Every hit is also an Attacked event for the session's AttackDetection, with the
	//    attacker, both owners and the target's position; the events add up to the health
	//    lost, arrows name the archer, structures are flagged, and reset() drops them.
	{
		fresh(s);
		const uint32_t sw = s.spawn_unit(0.0f, 0.0f, 0, UNIT_SWORDSMAN);
		const uint32_t d = s.spawn_unit(1.0f, 0.0f, 1, UNIT_SPEARMAN, 2000.0f);
		const uint32_t ar = s.spawn_unit(-10.0f, 8.0f, 0, UNIT_ARCHER);
		const uint32_t d2 = s.spawn_unit(-2.0f, 8.0f, 1, UNIT_SPEARMAN, 2000.0f);
		const uint32_t bar = s.spawn_building_world(-6.0f, -14.0f, BLD_BARRACKS, 1, 1.0f);
		const uint32_t sw2 = s.spawn_unit(-6.0f, -8.0f, 0, UNIT_SWORDSMAN);
		// Standing ground, so the targets stay where they are (passive ones would flee).
		for (uint32_t id : {d, d2}) s.units_.stance[s.lookup_unit(id)] = STANCE_STAND_GROUND;
		auto bhp = [&]() {
			for (const auto &b : s.buildings_)
				if (b.id == bar) return b.hp;
			return 0.0f;
		};
		const float hp0 = s.units_.hp[s.lookup_unit(d)], hp1 = s.units_.hp[s.lookup_unit(d2)], b0 = bhp();
		s.order_attack(&sw, 1, d, false);
		s.order_attack(&ar, 1, d2, false);
		s.order_attack_building(&sw2, 1, bar);
		(void)s.take_attacked_events();
		float melee = 0, arrows = 0, bld = 0;
		bool pos_ok = true, melee_flags = true;
		AttackDetection det;
		int alerts = 0;
		for (int t = 0; t < 200; ++t) {
			s.tick(Simulation::FIXED_STEP);
			for (const auto &e : s.take_attacked_events()) {
				if (e.target_id == d && e.attacker_id == sw) melee += e.damage;
				if (e.target_id == d2 && e.attacker_id == ar) arrows += e.damage;
				if (e.target_id == bar && e.target_is_building && e.attacker_id == sw2) bld += e.damage;
				if (e.target_id == d) pos_ok = pos_ok && std::fabs(e.x - 1.0f) < 1.5f && std::fabs(e.z) < 1.5f;
				// Swords hit in melee, arrows land as projectiles.
				if (e.attacker_id == sw || e.attacker_id == sw2) melee_flags = melee_flags && e.melee == 1;
				if (e.attacker_id == ar) melee_flags = melee_flags && e.melee == 0;
				// main_node's filter: hits on player 1 from someone else.
				if (e.target_owner == 1 && e.attacker_owner != 1 && det.report(e.x, e.z, t * Simulation::FIXED_STEP)) ++alerts;
			}
		}
		const float lost0 = hp0 - s.units_.hp[s.lookup_unit(d)], lost1 = hp1 - s.units_.hp[s.lookup_unit(d2)];
		std::printf("events: melee %.2f/%.2f, arrows %.2f/%.2f, structure %.2f/%.2f, %d alert(s)\n", melee, lost0,
				arrows, lost1, bld, b0 - bhp(), alerts);
		CHECK(lost0 > 0.0f && std::fabs(melee - lost0) < 0.01f);
		CHECK(lost1 > 0.0f && std::fabs(arrows - lost1) < 0.01f);
		CHECK(b0 - bhp() > 0.0f && std::fabs(bld - (b0 - bhp())) < 0.01f);
		CHECK(pos_ok);
		CHECK(melee_flags);
		// All three fights are within SuppressionRange (40 units) of each other: one alert.
		CHECK(alerts == 1);
		s.order_attack(&sw, 1, d, false);
		s.tick(Simulation::FIXED_STEP);
		s.reset();
		CHECK(s.take_attacked_events().empty());
	}

	// 1b. Attack.js: a capture is an Attacked message too ("capture", damage 0), so the
	//     owner's AttackDetection alerts on it; it adds no battle damage.
	{
		fresh(s);
		const uint32_t bar = s.spawn_building_world(-6.0f, -14.0f, BLD_BARRACKS, 1, 1.0f);
		float half = 0.0f;
		for (const auto &b : s.buildings_)
			if (b.id == bar) half = b.half_size;
		const uint32_t sp = s.spawn_unit(-6.0f, -14.0f + half + 1.0f, 0, UNIT_SPEARMAN);
		s.order_attack_building(&sp, 1, bar, false, true);
		(void)s.take_attacked_events();
		int captures = 0;
		AttackDetection det;
		int alerts = 0;
		for (int t = 0; t < 200; ++t) {
			s.tick(Simulation::FIXED_STEP);
			for (const auto &e : s.take_attacked_events())
				if (e.target_id == bar && e.target_is_building && e.attacker_id == sp && e.target_owner == 1 && e.damage == 0.0f) {
					++captures;
					if (det.report(e.x, e.z, t * Simulation::FIXED_STEP)) ++alerts;
				}
		}
		std::printf("capture: %d attacked event(s), %d alert(s)\n", captures, alerts);
		CHECK(captures >= 3 && alerts == 1);
		CHECK(s.battle_damage_dealt(0) == 0.0);
	}

	// 2. Combat sound groups (template Sound/SoundGroups): every listed file ships in
	//    assets/audio/, and the groups follow the templates.
	{
		auto group_is = [](uint8_t g, const char *dir, const char *first) {
			if (g >= ATTACK_SOUND_GROUP_COUNT) return false;
			const AttackSoundGroup &grp = attack_sound_group(g);
			return std::strcmp(grp.dir, dir) == 0 && grp.files[0] && std::strncmp(grp.files[0], first, std::strlen(first)) == 0;
		};
		int missing = 0;
		for (uint8_t g = 0; g < ATTACK_SOUND_GROUP_COUNT; ++g) {
			const AttackSoundGroup &grp = attack_sound_group(g);
			CHECK(grp.files[0] != nullptr && grp.gain > 0.0f && grp.pitch_lo <= grp.pitch_hi);
			for (int i = 0; grp.files[i]; ++i) {
				char path[256];
				std::snprintf(path, sizeof path, "assets/audio/%s/%s", grp.dir, grp.files[i]);
				if (FILE *f = std::fopen(path, "rb")) std::fclose(f);
				else if (++missing <= 3) std::printf("missing %s\n", path);
			}
		}
		CHECK(missing == 0);
		CHECK(ATTACK_SOUND_ROSTER_ROWS == CIV_UNIT_TEMPLATE_COUNT);
		const EntityAttackSounds *gen = generic_attack_sounds();
		CHECK(group_is(gen[UNIT_SWORDSMAN].a, "attack/weapon", "sword_attack"));
		CHECK(group_is(gen[UNIT_SPEARMAN].a, "attack/weapon", "spear_attack"));
		CHECK(group_is(gen[UNIT_ARCHER].b, "attack/weapon", "bow_attack") && group_is(gen[UNIT_ARCHER].c, "attack/impact", "arrow_dirt"));
		CHECK(group_is(gen[UNIT_SIEGE_RAM].a, "attack/siege", "ram_attack"));
		CHECK(gen[UNIT_HEALER].a == ATTACK_SOUND_NONE && gen[UNIT_HEALER].b == ATTACK_SOUND_NONE);
		// Roster rows: a javelineer throws javelin_attack and lands javelin_impact.
		bool jav = false;
		for (uint16_t r = 0; r < CIV_UNIT_TEMPLATE_COUNT; ++r)
			if (std::strcmp(civ_unit_template_table()[r].id, "athen/infantry_javelineer_b") == 0)
				jav = group_is(roster_attack_sounds()[r].b, "attack/weapon", "jav_attack");
		CHECK(jav);
		// Defence towers shoot bow_attack; every structure collapses as template_structure.
		CHECK(group_is(building_attack_sounds(BLD_TOWER).a, "attack/weapon", "bow_attack"));
		CHECK(building_attack_sounds(BLD_HOUSE).a == ATTACK_SOUND_NONE);
		for (uint8_t t = 0; t < BLD_TYPE_COUNT; ++t)
			CHECK(group_is(building_attack_sounds(t).c, "attack/destruction", "building_collapse_large"));
		CHECK(group_is(building_attack_sounds(BLD_TYPE_COUNT + 5).c, "attack/destruction", "building_collapse_large"));
		// Slaughter (women's knife) and the animals' own attacks, in FaunaType order.
		CHECK(group_is(gen[UNIT_FEMALE_CITIZEN].d, "attack/impact", "fleshstab"));
		CHECK(ATTACK_SOUND_FAUNA_ROWS == FAUNA_COUNT);
		CHECK(fauna_attack_sound(fauna_type_by_name("fauna_lion")) != ATTACK_SOUND_NONE &&
				fauna_attack_sound(fauna_type_by_name("fauna_lion")) == fauna_attack_sound(fauna_type_by_name("fauna_tiger")));
		CHECK(group_is(fauna_attack_sound(fauna_type_by_name("fauna_elephant_asian")), "actor/fauna/animal", "elephant_attack"));
		CHECK(fauna_attack_sound(FAUNA_DEER) == ATTACK_SOUND_NONE && fauna_attack_sound(FAUNA_COUNT) == ATTACK_SOUND_NONE);
		// Death groups (Health.js PlaySound("death")): a soldier's by phenotype, a dog's
		// generic animal death, a ship's warship_death, a camel trader's, an animal's own;
		// siege has none.
		CHECK(generic_death_sound(UNIT_SPEARMAN) == ATTACK_SOUND_PHENOTYPE);
		CHECK(group_is(phenotype_death_sound(false), "actor/human/death", "death_"));
		CHECK(group_is(phenotype_death_sound(true), "actor/human/death", "female_death"));
		CHECK(group_is(generic_death_sound(UNIT_WAR_DOG), "actor/fauna/death", "Animal_death_generic"));
		CHECK(group_is(generic_death_sound(UNIT_TRIREME), "actor/ship", "warship_death"));
		CHECK(generic_death_sound(UNIT_SIEGE_RAM) == ATTACK_SOUND_NONE);
		bool camel = false, horse = false;
		for (uint16_t r = 0; r < CIV_UNIT_TEMPLATE_COUNT; ++r) {
			if (std::strcmp(civ_unit_template_table()[r].id, "pers/support_trader") == 0)
				camel = group_is(roster_death_sound(r), "actor/fauna/death", "death_camel");
			if (std::strcmp(civ_unit_template_table()[r].id, "athen/cavalry_javelineer_b") == 0)
				horse = group_is(roster_death_sound(r), "actor/fauna/death", "death_horse");
		}
		CHECK(camel && horse);
		CHECK(group_is(fauna_death_sound(fauna_type_by_name("fauna_elephant_asian")), "actor/fauna/animal", "elephant_death"));
		CHECK(fauna_death_sound(FAUNA_COUNT) == ATTACK_SOUND_NONE);
	}

	// 3. Strikes on animals and animals' bites reach the session's combat sounds: a woman
	//    slaughtering a sheep is a Slaughter strike, a lion's bite is a melee hit by the lion.
	{
		fresh(s);
		const uint32_t w = s.spawn_unit(0.0f, 0.0f, 0, UNIT_FEMALE_CITIZEN);
		const uint32_t sheep = s.spawn_fauna(1.5f, 0.0f, FAUNA_SHEEP);
		s.order_hunt(&w, 1, sheep);
		(void)s.take_fauna_strikes();
		int slaughters = 0;
		for (int t = 0; t < 100 && !slaughters; ++t) {
			s.tick(Simulation::FIXED_STEP);
			for (const auto &e : s.take_fauna_strikes()) slaughters += e.attacker_id == w && e.slaughter;
		}
		CHECK(slaughters == 1);
		const uint32_t sp = s.spawn_unit(20.0f, 20.0f, 0, UNIT_SPEARMAN, 2000.0f);
		const uint32_t lion = s.spawn_fauna(21.0f, 20.0f, FAUNA_LION);
		size_t lf = 0;
		for (size_t f = 0; f < s.fauna_.count; ++f)
			if (s.fauna_.id[f] == lion) lf = f;
		s.fauna_start_attack(lf, sp);
		(void)s.take_attacked_events();
		bool bit = false;
		for (int t = 0; t < 100 && !bit; ++t) {
			s.tick(Simulation::FIXED_STEP);
			for (const auto &e : s.take_attacked_events()) bit = bit || (e.attacker_id == lion && e.target_id == sp && e.melee == 1);
		}
		CHECK(bit);
		s.reset();
		CHECK(s.take_fauna_strikes().empty());
	}

	if (failures) {
		std::printf("test_battle_detection: %d failure(s)\n", failures);
		return 1;
	}
	std::printf("test_battle_detection: all passed\n");
	return 0;
}
