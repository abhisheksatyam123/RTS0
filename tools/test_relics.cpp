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
		std::vector<uint32_t> foes = squad(s, 1, 8, -20.0f, 10.0f);
		s.order_attack(foes.data(), foes.size(), rid);
		s.order_walk(&rid, 1, -24.0f, -6.0f, false);
		run(s, 40.0f);
		CHECK(relic(s).claimed && relic(s).owner_team == 1);
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
	// does not keep a player alive in Conquest.
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

	// 9. A save from before relics were catafalques: its relic gets one on the next tick.
	{
		fresh(s);
		s.relics_.push_back({s.alloc_id(), 5.0f, 5.0f, RES_GOLD, 300, false});
		run(s, 0.1f);
		const int32_t r = s.lookup_unit(s.relics_[0].id);
		CHECK(r >= 0 && s.units_.unit_type[r] == UNIT_CATAFALQUE && s.units_.team[r] == GAIA_PLAYER);
		CHECK(s.relics_[0].capture_cp == RELIC_CAPTURE_POINTS * 100);
	}

	if (failures) {
		std::printf("test_relics: %d failure(s)\n", failures);
		return 1;
	}
	std::printf("test_relics: all passed\n");
	return 0;
}
