// Capture the Relic relics (template_unit_catafalque.xml): a Gaia catafalque with no Health
// and Capturable CapturePoints 250, RegenRate 10 a second. Units capture it with their
// Capture attack once a second; allies of the owner cannot. It is never an attack target,
// cannot be deleted, does not keep its owner alive in Conquest, and its owner can move it.
//   g++ -std=c++20 -O2 -Isrc tools/test_relics.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//       src/network/net_host.cpp -lpthread -o /tmp/test_relics && /tmp/test_relics
#define private public
#include "simulation/simulation.h"
#undef private
#include "network/turn_manager.h"
#include <cmath>
#include <cstdio>
#include <vector>

using namespace rtso;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static void run(Simulation &s, float seconds) {
	for (int t = 0; t < (int)std::lround(seconds / Simulation::FIXED_STEP); ++t) s.tick(Simulation::FIXED_STEP);
}

static void fresh(Simulation &s) {
	s.set_sim_seed(9);
	s.reset();
	for (uint8_t p = 0; p < 3; ++p) s.players_[p].active = 1, s.players_[p].is_ai = 0;
	s.players_[0].team = 0;
	s.players_[1].team = 1;
	s.players_[2].team = 0; // player 0's ally
	s.set_victory_condition(VICTORY_ENDLESS);
	s.treasures_.clear();
	s.spawn_building_world(-50.0f, -50.0f, BLD_TOWN_CENTER, 0, 1.0f);
	s.spawn_building_world(50.0f, 50.0f, BLD_TOWN_CENTER, 1, 1.0f);
	s.spawn_building_world(50.0f, -50.0f, BLD_TOWN_CENTER, 2, 1.0f);
}

static std::vector<uint32_t> squad(Simulation &s, uint8_t team, int n, float x, float z) {
	std::vector<uint32_t> ids;
	for (int k = 0; k < n; ++k) ids.push_back(s.spawn_unit(x, z + 1.2f * k, team, UNIT_SPEARMAN));
	return ids;
}

static const Simulation::Relic &relic(const Simulation &s) { return s.relics_[0]; }

// A relic of `civ`'s catafalque owned by `owner`, its auras applied on the next 1 s pulse.
static int32_t give_relic(Simulation &s, uint8_t civ, uint8_t owner, float x = 0.0f, float z = 0.0f) {
	const uint32_t id = s.spawn_relic(x, z);
	const int32_t r = s.lookup_unit(id);
	s.units_.tmpl[r] = civ_catafalque_template(civ);
	s.relics_.back().civ = civ;
	s.units_.team[r] = owner;
	return r;
}

int main() {
	static Simulation s;

	// 1. A Gaia catafalque of some civ, with full capture points.
	fresh(s);
	const uint32_t rid = s.spawn_relic(0.0f, 0.0f);
	const int32_t ri = s.lookup_unit(rid);
	CHECK(ri >= 0 && s.units_.unit_type[ri] == UNIT_CATAFALQUE && s.units_.team[ri] == GAIA_PLAYER);
	CHECK(s.relics_.size() == 1 && relic(s).id == rid && !relic(s).claimed);
	CHECK(relic(s).capture_cp == RELIC_CAPTURE_POINTS * 100);
	CHECK(civ_unit_template(s.units_.tmpl[ri]).flags & ROSTER_RELIC);
	CHECK(civ_unit_template(s.units_.tmpl[ri]).civ == relic(s).civ);
	std::printf("relic: %s\n", civ_unit_template(s.units_.tmpl[ri]).name);

	// 2. Two spearmen (2 x 2.5 a second) never beat the 10 a second regen.
	std::vector<uint32_t> two = squad(s, 0, 2, -4.0f, 0.0f);
	s.order_attack(two.data(), two.size(), rid); // a right click on a relic captures it
	CHECK(s.units_.order_kind[s.lookup_unit(two[0])] == ORDER_CAPTURE_RELIC);
	run(s, 30.0f);
	CHECK(!relic(s).claimed && relic(s).capture_cp == RELIC_CAPTURE_POINTS * 100);
	CHECK(s.units_.hp[s.lookup_unit(rid)] == s.units_.max_hp[s.lookup_unit(rid)]);

	// 3. Six more (8 x 2.5 = 20 a second, net 10) take it in about 25 s.
	std::vector<uint32_t> six = squad(s, 0, 6, 4.0f, -3.0f);
	s.order_attack(six.data(), six.size(), rid);
	run(s, 15.0f);
	CHECK(!relic(s).claimed && relic(s).capture_cp < RELIC_CAPTURE_POINTS * 100);
	std::printf("after 15 s: %d of %d capture points left\n", relic(s).capture_cp / 100, RELIC_CAPTURE_POINTS);
	run(s, 20.0f);
	CHECK(relic(s).claimed && relic(s).owner_team == 0 && s.units_.team[s.lookup_unit(rid)] == 0);
	CHECK(relic(s).capture_cp == RELIC_CAPTURE_POINTS * 100);
	// StatisticsTracker CapturedEntity: a unit captured, of no class and no value (no Cost).
	CHECK(s.stats_[0].units_captured[STAT_UNIT_TOTAL] == 1 && s.stats_[0].units_captured_value == 0);
	CHECK(s.stats_[0].units_captured[STAT_UNIT_INFANTRY] == 0);
	// The captors are done.
	CHECK(s.units_.order_kind[s.lookup_unit(six[0])] != ORDER_CAPTURE_RELIC);

	// 4. Never an attack target, and undeletable.
	{
		std::vector<uint32_t> foes = squad(s, 1, 3, 3.0f, 3.0f);
		run(s, 5.0f);
		const int32_t r = s.lookup_unit(rid);
		CHECK(r >= 0 && s.units_.alive[r] && s.units_.hp[r] == s.units_.max_hp[r]);
		for (uint32_t f : foes) CHECK(s.units_.target_unit_id[s.lookup_unit(f)] != rid);
		s.cmd_delete(0, &rid, 1);
		CHECK(s.lookup_unit(rid) >= 0 && s.units_.alive[s.lookup_unit(rid)]);
		// Player 0's spearmen kill the three foes or the other way round; clear them away.
		for (uint32_t f : foes) {
			const int32_t i = s.lookup_unit(f);
			if (i >= 0) s.units_.alive[i] = 0, s.units_.hp[i] = 0.0f, s.units_.action_state[i] = ACTION_DYING;
		}
		run(s, 1.0f);
	}

	// 5. An ally cannot capture it; the owner can walk it away.
	{
		std::vector<uint32_t> ally = squad(s, 2, 2, -6.0f, 6.0f);
		s.order_attack(ally.data(), ally.size(), rid);
		CHECK(s.units_.order_kind[s.lookup_unit(ally[0])] != ORDER_CAPTURE_RELIC);
		s.order_walk(&rid, 1, -20.0f, 0.0f, false);
		run(s, 20.0f);
		CHECK(relic(s).x < -10.0f);
		std::printf("moved to %.1f, %.1f\n", relic(s).x, relic(s).z);
		for (uint32_t id : ally) s.units_.alive[s.lookup_unit(id)] = 0;
	}

	// 6. An enemy takes it back; captors follow the relic as it moves.
	{
		// Nobody defends it.
		for (uint32_t id : two) s.units_.alive[s.lookup_unit(id)] = 0;
		for (uint32_t id : six) s.units_.alive[s.lookup_unit(id)] = 0;
		const uint32_t lost_before = s.stats_[0].units_lost[STAT_UNIT_TOTAL];
		std::vector<uint32_t> foes = squad(s, 1, 8, -20.0f, 10.0f);
		s.order_attack(foes.data(), foes.size(), rid);
		s.order_walk(&rid, 1, -24.0f, -6.0f, false);
		run(s, 40.0f);
		CHECK(relic(s).claimed && relic(s).owner_team == 1);
		// Capturable.js: LostEntity for the player it was taken from, CapturedEntity for the captor.
		CHECK(s.stats_[1].units_captured[STAT_UNIT_TOTAL] == 1);
		CHECK(s.stats_[0].units_lost[STAT_UNIT_TOTAL] == lost_before + 1);
	}

	// 7. Save and load during a capture keeps the hash.
	{
		std::vector<uint32_t> back = squad(s, 0, 8, relic(s).x + 3.0f, relic(s).z);
		s.order_attack(back.data(), back.size(), rid);
		run(s, 6.0f);
		const char *path = "/tmp/rts0_test_relics.sav";
		CHECK(s.save_state(path));
		static Simulation l;
		CHECK(l.load_state(path));
		run(s, 4.0f);
		run(l, 4.0f);
		CHECK(TurnManager().boot_sim_hash(s) == TurnManager().boot_sim_hash(l));
		CHECK(l.relics_.size() == 1 && l.relics_[0].capture_cp == relic(s).capture_cp);
		std::remove(path);
	}

	// 8. Capture the Relic: Setup.js's one relic when nothing is set, and an owned relic
	// does not keep a player alive in Conquest (catafalques drop ConquestCritical), but does
	// in Conquest Units, whose "Unit+!Animal" query counts it.
	{
		fresh(s);
		s.set_victory_condition(VICTORY_RELIC);
		run(s, 0.1f);
		CHECK(s.relics_.size() == 1 && s.lookup_unit(s.relics_[0].id) >= 0);
		fresh(s);
		s.set_victory_condition(VICTORY_CONQUEST_UNITS);
		const uint32_t r2 = s.spawn_relic(10.0f, 10.0f);
		s.units_.team[s.lookup_unit(r2)] = 1;
		std::vector<uint32_t> p0 = squad(s, 0, 1, -10.0f, -10.0f);
		s.record_.enemies_killed = 1;
		run(s, 1.0f);
		CHECK(!s.is_defeated(1));
		fresh(s);
		s.set_victory_condition(VICTORY_CONQUEST);
		const uint32_t r3 = s.spawn_relic(10.0f, 10.0f);
		s.units_.team[s.lookup_unit(r3)] = 1;
		std::vector<uint32_t> q0 = squad(s, 0, 1, -10.0f, -10.0f);
		for (auto &b : s.buildings_)
			if (b.team == 1 && !b.wrecked) s.destroy_building(b);
		s.stats_[1].units_lost[STAT_UNIT_TOTAL] = 1; // Conquest.js fires on a loss
		s.record_.enemies_killed = 1;
		run(s, 1.0f);
		CHECK(s.is_defeated(1));
	}

	// 8b. CaptureTheRelic.js with allied victory: allies who split the relics between them
	// win together; under Last Man Standing one player must hold them all.
	for (int lms = 0; lms < 2; ++lms) {
		fresh(s);
		s.set_victory_condition(VICTORY_RELIC);
		s.set_relic_count(2);
		s.set_relic_duration(5.0f);
		s.set_last_man_standing(lms != 0);
		run(s, 0.1f);
		CHECK(s.relics_.size() == 2);
		s.units_.team[s.lookup_unit(s.relics_[0].id)] = 0;
		s.units_.team[s.lookup_unit(s.relics_[1].id)] = 2; // player 0's ally
		run(s, 6.0f);
		std::printf("relics split between allies%s: game over %s\n", lms ? " (Last Man Standing)" : "",
		            s.is_game_over() ? "yes" : "no");
		if (lms) CHECK(!s.is_game_over());
		else CHECK(s.is_game_over() && (s.winner_mask_ & 0x5) == 0x5 && !(s.winner_mask_ & 0x2));
	}

	// 8b2. CaptureTheRelic.js:15-21 spawns relicCount on top of the map's own catafalques.
	{
		fresh(s);
		s.set_victory_condition(VICTORY_RELIC);
		s.set_relic_count(2);
		s.spawn_relic(0.0f, 30.0f);
		run(s, 0.1f);
		CHECK(s.relics_.size() == 3);
	}

	// 8c. CaptureTheRelic.js: each relic is a different catafalque, spawned beside a random
	// Gaia entity on land outside every player's territory (TriggerHelper.GetLandSpawnPoints).
	{
		fresh(s);
		for (int k = 0; k < 6; ++k) s.spawn_resource_world(-10.0f + 4.0f * k, 30.0f, RES_WOOD, 200);
		s.spawn_resource_world(-48.0f, -44.0f, RES_WOOD, 200); // inside player 0's territory
		s.set_victory_condition(VICTORY_RELIC);
		s.set_relic_count(5);
		run(s, 0.1f);
		CHECK(s.relics_.size() == 5);
		bool distinct = true, beside = true, neutral = true, passable = true;
		for (size_t a = 0; a < s.relics_.size(); ++a) {
			const auto &rel = s.relics_[a];
			for (size_t b = 0; b < a; ++b) distinct = distinct && s.relics_[b].civ != rel.civ;
			bool near = false;
			for (const auto &p : s.relic_spawn_points())
				near = near || std::hypot(p.first - rel.x, p.second - rel.z) <= 8.5f;
			beside = beside && near;
			neutral = neutral && s.territory_owner_at(rel.x, rel.z) < 0;
			auto [cx, cy] = s.nav_grid_.world_to_cell(rel.x, rel.z);
			passable = passable && s.nav_grid_.is_passable_for(cx, cy, PASS_INFANTRY);
		}
		CHECK(distinct);
		CHECK(beside);
		CHECK(neutral);
		CHECK(passable);
		std::printf("relic placement: %zu relics, distinct %d, beside Gaia %d, neutral %d, passable %d\n",
		            s.relics_.size(), distinct, beside, neutral, passable);
	}

	// 8d. Capturable.js keeps every player's points: two enemies capturing one Gaia relic
	// each take their share, the one holding most gets it, the other keeps what it took and
	// the new owner's regen wins those points back once the fight is over.
	{
		fresh(s);
		const uint32_t id = s.spawn_relic(0.0f, 0.0f);
		std::vector<uint32_t> a = squad(s, 0, 8, -4.0f, -4.0f);
		std::vector<uint32_t> b = squad(s, 1, 6, 4.0f, -3.0f);
		s.order_attack(a.data(), a.size(), id);
		s.order_attack(b.data(), b.size(), id);
		for (int t = 0; t < 120 && !relic(s).claimed; ++t) run(s, 0.5f);
		const auto &cp = relic(s).cp;
		float sum = 0.0f;
		for (float c : cp) sum += c;
		CHECK(relic(s).claimed && relic(s).owner_team == 0);
		CHECK(cp[1] > 0.0f && cp[0] > cp[1]);
		CHECK(std::fabs(sum - (float)RELIC_CAPTURE_POINTS) < 0.01f);
		std::printf("split capture: player 0 holds %.1f, player 1 %.1f, Gaia %.1f\n", cp[0], cp[1], cp[MAX_PLAYERS]);
		const float b_left = cp[1];
		for (uint32_t u : b) s.units_.alive[s.lookup_unit(u)] = 0; // player 1's captors fall
		run(s, 30.0f);
		CHECK(relic(s).cp[1] == 0.0f && relic(s).cp[0] == (float)RELIC_CAPTURE_POINTS);
		CHECK(relic(s).owner_team == 0 && b_left > 0.0f);
	}

	// 9. A save from before relics were catafalques: its relic gets one on the next tick.
	{
		fresh(s);
		s.relics_.push_back({s.alloc_id(), 5.0f, 5.0f, RES_GOLD, 300, false});
		run(s, 0.1f);
		const int32_t r = s.lookup_unit(s.relics_[0].id);
		CHECK(r >= 0 && s.units_.unit_type[r] == UNIT_CATAFALQUE && s.units_.team[r] == GAIA_PLAYER);
		CHECK(s.relics_[0].capture_cp == RELIC_CAPTURE_POINTS * 100);
	}

	// 10. Global relic auras (catafalques/*.json) are player modifiers while the relic is owned:
	// they rescale existing entities, follow the relic to its new owner, and don't stack.
	{
		fresh(s);
		const uint32_t house = s.spawn_building_world(-30.0f, -30.0f, BLD_HOUSE, 0, 1.0f);
		const uint32_t spear = s.spawn_unit(-40.0f, -20.0f, 0, UNIT_SPEARMAN);
		const uint32_t enemy_spear = s.spawn_unit(40.0f, 20.0f, 1, UNIT_SPEARMAN);
		run(s, 1.0f);
		auto bhp = [&](uint32_t id) { return s.buildings_[s.building_id_to_idx_[id]].max_hp; };
		const float house_hp = bhp(house);
		const int32_t su = s.lookup_unit(spear), eu = s.lookup_unit(enemy_spear);
		const float speed = s.units_.speed[su], vision = s.unit_vision_at((size_t)su);
		const float xp = s.promotion_required_xp((size_t)su);

		// Persian Great Builder: structures -10% cost and +5% health.
		const int32_t pr = give_relic(s, CIV_PERSIANS, 0);
		run(s, 1.0f);
		CHECK(std::fabs(bhp(house) - house_hp * 1.05f) < 0.01f);
		CHECK(std::fabs(s.tech_mods(0).bld_cost[BLD_HOUSE][RES_WOOD] - 0.9f) < 1e-5f);
		CHECK(s.tech_mods(1).bld_cost[BLD_HOUSE][RES_WOOD] == 1.0f);
		// A second Persian relic adds nothing (the same aura doesn't stack).
		give_relic(s, CIV_PERSIANS, 0, 3.0f, 0.0f);
		run(s, 1.0f);
		CHECK(std::fabs(bhp(house) - house_hp * 1.05f) < 0.01f);
		// Lost to player 1: player 0's house is back to its own health.
		for (const auto &rel : s.relics_) s.units_.team[s.lookup_unit(rel.id)] = 1;
		run(s, 1.0f);
		CHECK(std::fabs(bhp(house) - house_hp) < 0.01f);
		CHECK(std::fabs(s.tech_mods(1).bld_cost[BLD_HOUSE][RES_WOOD] - 0.9f) < 1e-5f);
		(void)pr;

		// Briton Guerrilla Tactics: soldiers +15% speed and vision.
		s.relics_.clear();
		give_relic(s, CIV_BRITONS, 0);
		run(s, 1.0f);
		CHECK(std::fabs(s.units_.speed[su] - speed * 1.15f) < 1e-4f);
		CHECK(std::fabs(s.unit_vision_at((size_t)su) - vision * 1.15f) < 1e-4f);
		CHECK(s.unit_vision_at((size_t)eu) == vision);

		// Gallic Ambush Slaughter: -25% promotion experience (RequiredXp aura).
		give_relic(s, CIV_GAULS, 0);
		run(s, 1.0f);
		CHECK(std::fabs(s.promotion_required_xp((size_t)su) - xp * 0.75f) < 1e-3f);

		// Kushite Earliest Nubian Queen: the owner's enemies' civilians (women and
		// male citizens alike, 0.28's "Civilian" class) gather 15% slower.
		give_relic(s, CIV_KUSHITES, 0);
		run(s, 1.0f);
		// (Support units that are no Civilian, such as female citizens, keep theirs.)
		const uint16_t civilian1 = civ_default_template(s.civ_of(1), UNIT_FEMALE_CITIZEN);
		CHECK(std::fabs(s.unit_tech_mods(1, UNIT_FEMALE_CITIZEN, civilian1).gather[Simulation::GK_WOOD] - 0.85f) < 1e-5f);
		CHECK(s.tech_mods(1).gather[UNIT_FEMALE_CITIZEN][Simulation::GK_WOOD] == 1.0f);
		CHECK(s.tech_mods(0).gather[UNIT_FEMALE_CITIZEN][Simulation::GK_WOOD] == 1.0f);
		CHECK(s.tech_mods(2).gather[UNIT_FEMALE_CITIZEN][Simulation::GK_WOOD] == 1.0f); // player 0's ally
		CHECK(std::fabs(s.tech_mods(1).gather[UNIT_VILLAGER][Simulation::GK_WOOD] - 0.85f) < 1e-5f);

		// Germanic Sacred Groves: the owner's workers gather wood 20% faster and carry 10 more.
		const int16_t carry0 = s.tech_mods(0).carry[UNIT_VILLAGER][RES_WOOD];
		give_relic(s, CIV_GERMANS, 0);
		run(s, 1.0f);
		CHECK(std::fabs(s.tech_mods(0).gather[UNIT_VILLAGER][Simulation::GK_WOOD] - 1.2f) < 1e-5f);
		CHECK(s.tech_mods(0).carry[UNIT_VILLAGER][RES_WOOD] == carry0 + 10);
		CHECK(std::fabs(s.tech_mods(1).gather[UNIT_VILLAGER][Simulation::GK_WOOD] - 0.85f) < 1e-5f); // Kushite relic only

		// Macedonian Financial Reorganization (+1 metal a second), Seleucid Immigration
		// (+5% population limit), Han Imperial Music Bureau (technologies half price).
		give_relic(s, CIV_MACEDONIANS, 0);
		give_relic(s, CIV_SELEUCIDS, 0);
		give_relic(s, CIV_HAN, 0);
		run(s, 1.0f);
		CHECK(s.tech_mods(0).trickle_add[RES_GOLD] == 1.0f);
		CHECK(std::fabs(s.tech_mods(0).max_pop - 1.05f) < 1e-5f);
		CHECK(std::fabs(s.tech_mods(0).bld_tech_cost[BLD_TOWN_CENTER][RES_FOOD] - 0.5f) < 1e-5f);
		const float gold = s.players_[0].res[RES_GOLD], gold1 = s.players_[1].res[RES_GOLD];
		run(s, 10.0f);
		CHECK(s.players_[0].res[RES_GOLD] - gold >= s.players_[1].res[RES_GOLD] - gold1 + 9.5f);
		// It also raises every owned trickle that lists metal (an empty class list affects all):
		// a wonder's 1 per 2 s becomes 2 per 2 s, so 10 s bring the player 10 + 10.
		s.spawn_building_world(30.0f, -30.0f, BLD_WONDER, 0, 1.0f);
		const float gold_w = s.players_[0].res[RES_GOLD], gold1_w = s.players_[1].res[RES_GOLD];
		run(s, 10.0f);
		const float got = (s.players_[0].res[RES_GOLD] - gold_w) - (s.players_[1].res[RES_GOLD] - gold1_w);
		std::printf("  Financial Reorganization with a wonder: +%.1f metal in 10 s\n", got);
		CHECK(got >= 19.5f && got <= 20.5f);

		// Iberian Mercenary Commander reaches allies too (affectedPlayers Ally).
		give_relic(s, CIV_IBERIANS, 0);
		run(s, 1.0f);
		const uint16_t merc = [] {
			for (uint16_t t = 0; t < CIV_UNIT_TEMPLATE_COUNT; ++t)
				if ((civ_unit_template(t).tech_classes & (TC_CITIZEN | TC_MERCENARY)) == (TC_CITIZEN | TC_MERCENARY)) return t;
			return CIV_UNIT_TEMPLATE_NONE;
		}();
		CHECK(merc != CIV_UNIT_TEMPLATE_NONE);
		if (merc != CIV_UNIT_TEMPLATE_NONE) {
			CHECK(std::fabs(s.tech_mods(0).tmpl[merc].cost[RES_GOLD] - 0.75f) < 1e-5f);
			CHECK(std::fabs(s.tech_mods(2).tmpl[merc].cost[RES_GOLD] - 0.75f) < 1e-5f);
			CHECK(s.tech_mods(1).tmpl[merc].cost[RES_GOLD] == 1.0f);
		}

		// Save and load with relic auras applied keeps the hash.
		const char *path = "/tmp/rts0_test_relic_auras.sav";
		CHECK(s.save_state(path));
		static Simulation t;
		CHECK(t.load_state(path));
		CHECK(TurnManager().boot_sim_hash(s) == TurnManager().boot_sim_hash(t));
		run(s, 5.0f);
		run(t, 5.0f);
		CHECK(TurnManager().boot_sim_hash(s) == TurnManager().boot_sim_hash(t));
		const int32_t tu = t.lookup_unit(spear);
		CHECK(tu >= 0 && std::fabs(t.units_.speed[tu] - speed * 1.15f) < 1e-4f);
		std::remove(path);
	}

	// 11. Kushite Temple at Naqa (a range aura): the owner's temples within 80 m heal their
	// garrison 1 hp/s more.
	{
		fresh(s);
		const uint32_t near = s.spawn_building_world(10.0f, 0.0f, BLD_TEMPLE, 0, 1.0f);
		const uint32_t far = s.spawn_building_world(-40.0f, 30.0f, BLD_TEMPLE, 0, 1.0f);
		const auto &bn = s.buildings_[s.building_id_to_idx_[near]];
		const float base = s.structure_garrison_heal(bn);
		give_relic(s, CIV_KUSHITES, 0);
		CHECK(s.structure_garrison_heal(s.buildings_[s.building_id_to_idx_[near]]) == base + 1.0f);
		CHECK(s.structure_garrison_heal(s.buildings_[s.building_id_to_idx_[far]]) == base);
	}

	if (failures) {
		std::printf("test_relics: %d failure(s)\n", failures);
		return 1;
	}
	std::printf("test_relics: all passed\n");
	return 0;
}
