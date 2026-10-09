// 0 A.D. ProductionQueue.js / Trainer.js / Researcher.js: batch training, technologies in the
// shared queue, population blocking, autoqueue, refunds, save/load and lockstep.
//   g++ -std=c++20 -O2 -Isrc tools/test_production_queue.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//       src/network/net_host.cpp -lpthread -o /tmp/test_production_queue && /tmp/test_production_queue
#include "simulation/simulation.h"
#include "network/turn_manager.h"
#include <cmath>
#include <cstdio>
#include <string>

using namespace rtso;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static uint32_t fresh(Simulation &s) {
	s.set_sim_seed(5);
	s.reset();
	s.set_player_resources(0, 20000, 20000, 20000, 20000);
	return s.spawn_building_world(-30.0f, -30.0f, BLD_BARRACKS, 0, 1.0f);
}

static void run(Simulation &s, float seconds) {
	for (int t = 0; t < (int)(seconds / Simulation::FIXED_STEP); ++t) s.tick(Simulation::FIXED_STEP);
}

static const Simulation::Building &bld(const Simulation &s, uint32_t id) {
	for (const auto &b : s.buildings()) if (b.id == id) return b;
	static Simulation::Building none{};
	return none;
}

static int count_units(const Simulation &s, uint8_t team, uint8_t type) {
	auto u = s.units();
	int n = 0;
	for (size_t i = 0; i < u.count; ++i) if (u.alive[i] && u.team[i] == team && u.unit_type[i] == type) ++n;
	return n;
}

static float total_res(const Simulation &s) {
	float t = 0.0f;
	for (int r = 0; r < RES_COUNT; ++r) t += s.player(0).res[r];
	return t;
}

int main() {
	// 1. Trainer.js: a batch of 5 costs 5x, takes BuildTime * 5^BatchTimeModifier, is one item
	//    and spawns all 5 at once.
	{
		Simulation s;
		const uint32_t b = fresh(s);
		const float before = s.player(0).res[RES_FOOD];
		CHECK(s.cmd_train(0, b, UNIT_SPEARMAN));
		const float one_cost = before - s.player(0).res[RES_FOOD];
		const float one_time = bld(s, b).queue.back().duration;
		CHECK(one_cost > 0.0f);
		CHECK(s.cmd_train(0, b, UNIT_SPEARMAN, 5));
		const auto &q = bld(s, b).queue;
		CHECK(q.size() == 2 && q.back().count == 5);
		CHECK(std::fabs(before - one_cost - s.player(0).res[RES_FOOD] - 5.0f * one_cost) < 0.01f);
		const float mod = trainer_batch_time_modifier(s.player_civ(0), BLD_BARRACKS);
		CHECK(mod > 0.0f && mod < 1.0f);
		CHECK(std::fabs(q.back().duration - one_time * std::pow(5.0f, mod)) < 0.01f);
		CHECK(std::fabs(s.batch_time_multiplier(0, BLD_BARRACKS, 1) - 1.0f) < 1e-6f);
		run(s, one_time + q.back().duration + 1.0f);
		CHECK(count_units(s, 0, UNIT_SPEARMAN) == 6);
		CHECK(bld(s, b).queue.empty());
		std::printf("batch of 5: %.1f s against %.1f s for one (modifier %.2f)\n", one_time * std::pow(5.0f, mod), one_time, mod);
	}

	// 2. Population: the front batch waits while it doesn't fit (Player.js
	//    TryReservePopulationSlots) and starts once it does; a started batch holds its slots.
	{
		Simulation s;
		const uint32_t b = fresh(s);
		const int room = s.pop_max(0) - s.pop_current(0);
		CHECK(room > 3);
		for (int k = 0; k < room - 3; ++k) s.spawn_unit(10.0f + (k % 10), 10.0f + k / 10, 0, UNIT_SPEARMAN);
		CHECK(s.pop_max(0) - s.pop_current(0) == 3);
		CHECK(s.cmd_train(0, b, UNIT_SPEARMAN, 5));
		const float t = bld(s, b).queue.front().duration;
		run(s, t + 2.0f);
		CHECK(bld(s, b).queue.size() == 1 && !bld(s, b).queue.front().started);
		CHECK(bld(s, b).queue.front().timer == 0.0f);
		s.spawn_building_world(30.0f, 30.0f, BLD_HOUSE, 0, 1.0f);
		run(s, 0.5f);
		CHECK(bld(s, b).queue.front().started);
		const int pop_started = s.pop_current(0);
		run(s, t + 1.0f);
		CHECK(bld(s, b).queue.empty());
		CHECK(s.pop_current(0) == pop_started); // the reserved slots became the units
	}

	// 3. Researcher.js: a technology waits in the same queue behind units, is paid when
	//    queued, counts as in progress, and finishes after the units before it.
	{
		Simulation s;
		s.set_sim_seed(5);
		s.reset();
		s.set_player_resources(0, 20000, 20000, 20000, 20000);
		const uint32_t tc = s.spawn_building_world(-30.0f, -30.0f, BLD_TOWN_CENTER, 0, 1.0f);
		for (int h = 0; h < 5; ++h) s.spawn_building_world(-10.0f + 6.0f * h, 20.0f, BLD_HOUSE, 0, 1.0f);
		CHECK(s.cmd_train(0, tc, UNIT_FEMALE_CITIZEN, 2));
		const float train_t = bld(s, tc).queue.front().duration;
		const float before = total_res(s);
		CHECK(s.research_check(0, TECH_PHASE_TOWN, tc) == Simulation::TECH_OK);
		CHECK(s.cmd_research(0, tc, TECH_PHASE_TOWN));
		CHECK(total_res(s) < before);
		CHECK(bld(s, tc).queue.size() == 2 && bld(s, tc).queue.back().is_tech());
		CHECK(s.tech_queued(0, TECH_PHASE_TOWN));
		CHECK(s.research_check(0, TECH_PHASE_TOWN) == Simulation::TECH_ERR_IN_PROGRESS);
		CHECK(Simulation::researching(bld(s, tc)) == TECH_NONE); // the women go first
		run(s, train_t + 0.5f);
		CHECK(count_units(s, 0, UNIT_FEMALE_CITIZEN) == 2);
		CHECK(Simulation::researching(bld(s, tc)) == TECH_PHASE_TOWN);
		CHECK(!s.is_tech_researched(TECH_PHASE_TOWN));
		run(s, tech_info(TECH_PHASE_TOWN).research_time + 1.0f);
		CHECK(s.is_tech_researched(TECH_PHASE_TOWN));
		CHECK(bld(s, tc).queue.empty());
	}

	// 4. RemoveItem refunds exactly what was paid, a batch and a technology alike.
	{
		Simulation s;
		const uint32_t b = fresh(s);
		const float before = total_res(s);
		CHECK(s.cmd_train(0, b, UNIT_SPEARMAN, 7));
		CHECK(total_res(s) < before);
		CHECK(s.cmd_cancel_train(0, b, 0));
		CHECK(std::fabs(total_res(s) - before) < 0.01f);
		const uint32_t house = s.spawn_building_world(30.0f, -30.0f, BLD_HOUSE, 0, 1.0f);
		uint8_t tech = TECH_NONE;
		for (uint8_t t = 0; t < TECH_COUNT && tech == TECH_NONE; ++t)
			if (s.research_check(0, t, house) == Simulation::TECH_OK) tech = t;
		CHECK(tech != TECH_NONE);
		if (tech != TECH_NONE) {
			CHECK(s.cmd_research(0, house, tech));
			CHECK(total_res(s) < before);
			CHECK(s.cmd_cancel_train(0, house, 0));
			CHECK(std::fabs(total_res(s) - before) < 0.01f);
			CHECK(!s.tech_queued(0, tech));
		}
	}

	// 5. MaxQueueSize 16, and batches beyond 200 or below 1 are refused.
	{
		Simulation s;
		const uint32_t b = fresh(s);
		CHECK(!s.cmd_train(0, b, UNIT_SPEARMAN, 0));
		CHECK(!s.cmd_train(0, b, UNIT_SPEARMAN, 201));
		for (size_t k = 0; k < PRODUCTION_QUEUE_MAX; ++k) CHECK(s.cmd_train(0, b, UNIT_SPEARMAN));
		CHECK(!s.cmd_train(0, b, UNIT_SPEARMAN));
		CHECK(bld(s, b).queue.size() == PRODUCTION_QUEUE_MAX);
	}

	// 6. Autoqueue: a finished batch is queued and paid again; when it can't be paid for,
	//    autoqueuing turns itself off.
	{
		Simulation s;
		const uint32_t b = fresh(s);
		CHECK(!s.cmd_set_autoqueue(1, b, true)); // not the owner
		CHECK(s.cmd_set_autoqueue(0, b, true));
		CHECK(s.cmd_train(0, b, UNIT_SPEARMAN, 2));
		const float t = bld(s, b).queue.front().duration;
		run(s, 3.0f * t + 1.0f);
		CHECK(count_units(s, 0, UNIT_SPEARMAN) >= 6);
		CHECK(bld(s, b).queue.size() == 1 && bld(s, b).queue.front().count == 2);
		CHECK(bld(s, b).autoqueue);
		s.set_player_resources(0, 0, 0, 0, 0);
		const int have = count_units(s, 0, UNIT_SPEARMAN);
		run(s, 2.0f * t + 1.0f);
		CHECK(count_units(s, 0, UNIT_SPEARMAN) == have + 2);
		CHECK(!bld(s, b).autoqueue);
		CHECK(bld(s, b).queue.empty());
	}

	// 7. Save/load keeps batches, technologies, paid costs, started flags and autoqueue.
	{
		Simulation s;
		s.set_sim_seed(5);
		s.reset();
		s.set_player_resources(0, 20000, 20000, 20000, 20000);
		const uint32_t tc = s.spawn_building_world(-30.0f, -30.0f, BLD_TOWN_CENTER, 0, 1.0f);
		for (int h = 0; h < 5; ++h) s.spawn_building_world(-10.0f + 6.0f * h, 20.0f, BLD_HOUSE, 0, 1.0f);
		CHECK(s.cmd_train(0, tc, UNIT_FEMALE_CITIZEN, 3));
		CHECK(s.cmd_research(0, tc, TECH_PHASE_TOWN));
		CHECK(s.cmd_set_autoqueue(0, tc, true));
		run(s, 1.0f);
		const std::string path = "/tmp/rts0_test_production_queue.sav";
		CHECK(s.save_state(path.c_str()));
		Simulation l;
		CHECK(l.load_state(path.c_str()));
		const auto &a = bld(s, tc).queue, &c = bld(l, tc).queue;
		CHECK(a.size() == 2 && c.size() == 2);
		if (a.size() == 2 && c.size() == 2) {
			CHECK(c[0].count == 3 && c[0].started && c[0].timer == a[0].timer);
			CHECK(c[1].tech == TECH_PHASE_TOWN && !c[1].started);
			for (int r = 0; r < RES_COUNT; ++r) CHECK(c[0].paid[r] == a[0].paid[r] && c[1].paid[r] == a[1].paid[r]);
		}
		CHECK(bld(l, tc).autoqueue);
		CHECK(s.pop_current(0) == l.pop_current(0));
		run(s, 40.0f);
		run(l, 40.0f);
		TurnManager th;
		CHECK(th.boot_sim_hash(s) == th.boot_sim_hash(l));
		CHECK(count_units(s, 0, UNIT_FEMALE_CITIZEN) == count_units(l, 0, UNIT_FEMALE_CITIZEN));
		std::remove(path.c_str());
	}

	// 8. Lockstep: batch size in target_z of cmds 6 and 25, autoqueue as cmd 44.
	{
		Simulation s;
		const uint32_t b = fresh(s);
		TurnManager tm;
		NetCommand c;
		c.client_id = 0;
		c.cmd_type = (uint8_t)NetCommandType::NET_CMD_TRAIN;
		c.target_id = b;
		c.param = UNIT_SPEARMAN;
		c.target_z = 4;
		tm.post_local_command(c);
		NetCommand d;
		d.client_id = 0;
		d.cmd_type = (uint8_t)NetCommandType::NET_CMD_AUTOQUEUE;
		d.target_id = b;
		d.param = 1;
		tm.post_local_command(d);
		NetCommand e; // an older sender's train: target_z 0 trains one
		e.client_id = 0;
		e.cmd_type = (uint8_t)NetCommandType::NET_CMD_TRAIN;
		e.target_id = b;
		e.param = UNIT_SPEARMAN;
		tm.post_local_command(e);
		for (int i = 0; i < 4; ++i) tm.tick(TurnManager::TURN_DURATION, s);
		const auto &q = bld(s, b).queue;
		CHECK(d.cmd_type == 44);
		CHECK(q.size() >= 2 && q[0].count == 4 && q[1].count == 1);
		CHECK(bld(s, b).autoqueue);
	}

	// 9. Conscription (barracks_batch_training) adds -0.1 to the Barracks' BatchTimeModifier;
	//    Persians get -0.1 per phase for Stables instead (phase_town_pers, phase_city_pers).
	{
		Simulation s;
		const uint32_t b = fresh(s);
		s.player(0).phase = 3;
		const float base = s.batch_time_multiplier(0, BLD_BARRACKS, 5);
		const float mod = trainer_batch_time_modifier(s.player_civ(0), BLD_BARRACKS);
		CHECK(tech_info(TECH_BARRACKS_BATCH_TRAINING).researcher == BLD_BARRACKS);
		CHECK(s.research_check(0, TECH_BARRACKS_BATCH_TRAINING, b) == Simulation::TECH_OK);
		CHECK(s.cmd_research(0, b, TECH_BARRACKS_BATCH_TRAINING));
		run(s, tech_info(TECH_BARRACKS_BATCH_TRAINING).research_time + 1.0f);
		CHECK(s.is_tech_researched(TECH_BARRACKS_BATCH_TRAINING));
		CHECK(std::fabs(base - std::pow(5.0f, mod)) < 1e-4f);
		CHECK(std::fabs(s.batch_time_multiplier(0, BLD_BARRACKS, 5) - std::pow(5.0f, mod - 0.1f)) < 1e-4f);
		CHECK(std::fabs(s.batch_time_multiplier(0, BLD_STABLE, 5) - std::pow(5.0f, trainer_batch_time_modifier(s.player_civ(0), BLD_STABLE))) < 1e-4f);

		Simulation p;
		fresh(p);
		p.set_player_civ(CIV_PERSIANS);
		const float smod = trainer_batch_time_modifier(CIV_PERSIANS, BLD_STABLE);
		p.player(0).phase = 1;
		CHECK(std::fabs(p.batch_time_multiplier(0, BLD_STABLE, 5) - std::pow(5.0f, smod)) < 1e-4f);
		p.player(0).phase = 3;
		CHECK(std::fabs(p.batch_time_multiplier(0, BLD_STABLE, 5) - std::pow(5.0f, smod - 0.2f)) < 1e-4f);
		CHECK(!tech_available_to_civ(TECH_STABLE_BATCH_TRAINING, CIV_PERSIANS));
	}

	if (failures) {
		std::printf("FAIL: %d checks\n", failures);
		return 1;
	}
	std::printf("PASS test_production_queue\n");
	return 0;
}
