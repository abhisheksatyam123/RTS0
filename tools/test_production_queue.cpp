// 0 A.D. ProductionQueue.js / Trainer.js / Researcher.js: batch training, technologies in the
// shared queue, population blocking, autoqueue, refunds, save/load and lockstep.
//   g++ -std=c++20 -O2 -Isrc tools/test_production_queue.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//       src/network/net_host.cpp -lpthread -o /tmp/test_production_queue && /tmp/test_production_queue
#include "simulation/simulation.h"
#include "network/turn_manager.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace rtso;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static uint32_t fresh(Simulation &s) {
	s.set_sim_seed(5);
	s.reset();
	s.set_player_resources(0, 20000, 20000, 20000, 20000);
	// Population/Bonus 20: there is no free base. Its territory covers the barracks, which
	// would otherwise decay to Gaia.
	s.spawn_building_world(-30.0f, -56.0f, BLD_TOWN_CENTER, 0, 1.0f);
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
		CHECK(s.training_blocked(0) && !s.training_blocked(1)); // Player.IsTrainingBlocked: the counter flashes
		s.spawn_building_world(30.0f, 30.0f, BLD_HOUSE, 0, 1.0f);
		run(s, 1.1f); // tried again at the queue's next second
		CHECK(bld(s, b).queue.front().started);
		CHECK(!s.training_blocked(0));
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
		run(s, 3.0f * std::ceil(t) + 1.0f); // each autoqueued batch ends on a whole second
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
	// 6b. A batch that spawned in parts autoqueues at the size it was queued at
	//     (ProductionQueue.js originalItem.count), not at its remainder.
	{
		Simulation s;
		const uint32_t b = fresh(s);
		CHECK(s.cmd_set_autoqueue(0, b, true));
		CHECK(s.cmd_train(0, b, UNIT_SPEARMAN, 1));
		const_cast<Simulation::Building &>(bld(s, b)).queue.front().batch = 3; // the last 1 of a batch of 3
		run(s, bld(s, b).queue.front().duration + 0.5f);
		CHECK(count_units(s, 0, UNIT_SPEARMAN) == 1);
		CHECK(bld(s, b).queue.size() == 1 && bld(s, b).queue.front().count == 3 && bld(s, b).queue.front().batch == 0);
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
		// As if 2 of a batch of 5 had spawned before the spot ran out (originalItem.count).
		const_cast<Simulation::Building &>(bld(s, tc)).queue.front().batch = 5;
		const std::string path = "/tmp/rts0_test_production_queue.sav";
		CHECK(s.save_state(path.c_str()));
		Simulation l;
		CHECK(l.load_state(path.c_str()));
		const auto &a = bld(s, tc).queue, &c = bld(l, tc).queue;
		CHECK(a.size() == 2 && c.size() == 2);
		if (a.size() == 2 && c.size() == 2) {
			CHECK(c[0].count == 3 && c[0].batch == 5 && c[0].started && c[0].timer == a[0].timer);
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
		// Trainer/BatchTimeModifier is the structure's own: a captured Athenian dock keeps 0.7.
		CHECK(trainer_batch_time_modifier(CIV_ATHENIANS, BLD_DOCK) != trainer_batch_time_modifier(CIV_PERSIANS, BLD_DOCK));
		CHECK(std::fabs(p.batch_time_multiplier(0, BLD_DOCK, 5, CIV_ATHENIANS) -
		                std::pow(5.0f, trainer_batch_time_modifier(CIV_ATHENIANS, BLD_DOCK))) < 1e-4f);
	}

	// 10. Footprint.PickSpawnPoint: each trained unit spawns on its own free spot around the
	//     footprint, the side facing the rally point first; with no room the rest of the batch
	//     stays queued (already paid) until there is; ejected garrisons spread out the same way.
	{
		auto spread = [](const Simulation &s, uint8_t team, uint8_t type, float &mean_x) {
			auto u = s.units();
			std::vector<size_t> idx;
			for (size_t i = 0; i < u.count; ++i) if (u.alive[i] && u.team[i] == team && u.unit_type[i] == type) idx.push_back(i);
			const float r = unit_collision_radius(type);
			bool apart = true;
			mean_x = 0.0f;
			for (size_t a = 0; a < idx.size(); ++a) {
				mean_x += u.pos_x[idx[a]] / (float)idx.size();
				for (size_t c = a + 1; c < idx.size(); ++c) {
					const float dx = u.pos_x[idx[a]] - u.pos_x[idx[c]], dz = u.pos_z[idx[a]] - u.pos_z[idx[c]];
					if (dx * dx + dz * dz < 4.0f * r * r - 1e-4f) apart = false;
				}
			}
			return apart;
		};
		Simulation s;
		const uint32_t b = fresh(s);
		const Simulation::Building bb = bld(s, b);
		CHECK(s.cmd_train(0, b, UNIT_SPEARMAN, 5));
		const float t = bld(s, b).queue.front().duration;
		for (int k = 0; k < (int)(std::ceil(t) / Simulation::FIXED_STEP) + 1 && count_units(s, 0, UNIT_SPEARMAN) == 0; ++k) s.tick(Simulation::FIXED_STEP);
		float mean_x = 0.0f;
		CHECK(count_units(s, 0, UNIT_SPEARMAN) == 5);
		CHECK(spread(s, 0, UNIT_SPEARMAN, mean_x)); // checked on the tick they appear, before any pushing
		auto u = s.units();
		bool outside = true;
		for (size_t i = 0; i < u.count; ++i)
			if (u.alive[i] && u.unit_type[i] == UNIT_SPEARMAN && std::fabs(u.pos_x[i] - bb.x) < bb.hu && std::fabs(u.pos_z[i] - bb.z) < bb.hv) outside = false;
		CHECK(outside);

		Simulation w;
		const uint32_t wb = fresh(w);
		w.cmd_set_rally_point(0, wb, -60.0f, -30.0f);
		CHECK(w.cmd_train(0, wb, UNIT_SPEARMAN, 5));
		for (int k = 0; k < 2000 && count_units(w, 0, UNIT_SPEARMAN) == 0; ++k) w.tick(Simulation::FIXED_STEP);
		float west = 0.0f;
		CHECK(spread(w, 0, UNIT_SPEARMAN, west));
		CHECK(west < bld(w, wb).x - bld(w, wb).hu); // the rally point's side
		std::printf("spawn points: a batch of 5 on 5 spots, mean x %.1f by the building at %.1f, %.1f toward a rally point to the west\n", mean_x, bb.x, west);

		// No room: a ring of enemy units around the footprint holds the batch.
		Simulation f;
		const uint32_t fb = fresh(f);
		const Simulation::Building fbb = bld(f, fb);
		const float before = total_res(f);
		CHECK(f.cmd_train(0, fb, UNIT_SPEARMAN, 2));
		const float paid = before - total_res(f);
		// The ring closes just before the batch is done: its idle units push each other apart, and
		// within half a second a spot opens at a corner.
		run(f, bld(f, fb).queue.front().duration - 0.1f);
		CHECK(count_units(f, 0, UNIT_SPEARMAN) == 0);
		std::vector<uint32_t> ring;
		for (float x = fbb.x - fbb.hu - 4.0f; x <= fbb.x + fbb.hu + 4.0f; x += 1.0f)
			for (float z = fbb.z - fbb.hv - 4.0f; z <= fbb.z + fbb.hv + 4.0f; z += 1.0f)
				if (std::fabs(x - fbb.x) > fbb.hu || std::fabs(z - fbb.z) > fbb.hv) ring.push_back(f.spawn_unit(x, z, 1, UNIT_FEMALE_CITIZEN));
		for (int k = 0; k < (int)(0.2f / Simulation::FIXED_STEP); ++k) f.tick(Simulation::FIXED_STEP);
		CHECK(count_units(f, 0, UNIT_SPEARMAN) == 0);
		CHECK(bld(f, fb).queue.size() == 1 && bld(f, fb).queue.front().count == 2);
		f.cmd_delete(1, ring.data(), ring.size());
		run(f, 5.0f);
		CHECK(count_units(f, 0, UNIT_SPEARMAN) == 2 && bld(f, fb).queue.empty());
		CHECK(std::fabs(before - paid - total_res(f)) < 0.01f); // paid once, not again
		std::printf("spawn points: a ringed barracks keeps its batch of 2 queued, then trains it once the ring is gone\n");

		// GarrisonHolder.Eject spreads the garrison out too.
		Simulation g;
		fresh(g);
		const uint32_t cc = g.spawn_building_world(30.0f, 30.0f, BLD_TOWN_CENTER, 0, 1.0f);
		std::vector<uint32_t> men;
		for (int k = 0; k < 6; ++k) men.push_back(g.spawn_unit(30.0f + (float)k, 40.0f, 0, UNIT_SPEARMAN));
		g.order_garrison(men.data(), men.size(), cc);
		run(g, 15.0f);
		CHECK(bld(g, cc).garrisoned_units.size() == 6);
		g.order_ungarrison(cc);
		float gx = 0.0f;
		CHECK(count_units(g, 0, UNIT_SPEARMAN) == 6 && spread(g, 0, UNIT_SPEARMAN, gx));

		// A round footprint (brit/civil_centre.xml <Circle>) spawns on rings a gap apart, the first
		// half a gap out, the rally point's side first; the square civic centre's rows are not
		// rings (their corners stand further out than their sides).
		CHECK(building_footprint_round(CIV_BRITONS, BLD_CRANNOG)); // brit/crannog.xml <Circle radius="17"/>
		for (const uint8_t civ : {(uint8_t)CIV_BRITONS, (uint8_t)CIV_ATHENIANS}) {
			Simulation r;
			r.set_player_civ(civ);
			fresh(r);
			const uint32_t rc = r.spawn_building_world(30.0f, 30.0f, BLD_TOWN_CENTER, 0, 1.0f);
			std::vector<uint32_t> ring_men;
			for (int k = 0; k < 6; ++k) ring_men.push_back(r.spawn_unit(30.0f + (float)k, 40.0f, 0, UNIT_SPEARMAN));
			r.order_garrison(ring_men.data(), ring_men.size(), rc);
			run(r, 15.0f);
			CHECK(bld(r, rc).garrisoned_units.size() == 6);
			r.cmd_set_rally_point(0, rc, 0.0f, 30.0f); // due west
			r.order_ungarrison(rc);
			const Simulation::Building rb = bld(r, rc);
			const float gap = 3.0f * unit_collision_radius(UNIT_SPEARMAN);
			const float first = std::max(rb.hu, rb.hv) + 0.5f * gap;
			auto u = r.units();
			float rx = 0.0f;
			int on_rings = 0;
			for (size_t i = 0; i < u.count; ++i) {
				if (!u.alive[i] || u.unit_type[i] != UNIT_SPEARMAN) continue;
				const float d = std::hypot(u.pos_x[i] - rb.x, u.pos_z[i] - rb.z);
				if (std::fabs(d - first) < 1e-3f || std::fabs(d - first - gap) < 1e-3f) ++on_rings;
				rx += (u.pos_x[i] - rb.x) / 6.0f;
			}
			float spread_x = 0.0f;
			CHECK(count_units(r, 0, UNIT_SPEARMAN) == 6 && spread(r, 0, UNIT_SPEARMAN, spread_x));
			CHECK(rx < -rb.hu); // the rally point's side
			CHECK(civ == CIV_BRITONS ? on_rings == 6 : on_rings < 6);
			std::printf("spawn points: %s civic centre unloads 6 west of it, %d on rings %.2f and %.2f out\n",
					civ == CIV_BRITONS ? "a round Briton" : "a square Athenian", on_rings, first, first + gap);
		}
	}

	// 11. With no room outside: a rally point on the trainer itself puts the batch straight
	//     inside (Trainer.js autoGarrison); an unload order leaves the garrison inside
	//     (GetSpawnPosition, not forced); a structure too damaged to hold them kills those
	//     that cannot get out (GarrisonHolder.EjectOrKill).
	{
		auto ringed = [](Simulation &f, uint32_t fb) {
			const Simulation::Building fbb = bld(f, fb);
			std::vector<uint32_t> ring;
			for (float x = fbb.x - fbb.hu - 4.0f; x <= fbb.x + fbb.hu + 4.0f; x += 1.0f)
				for (float z = fbb.z - fbb.hv - 4.0f; z <= fbb.z + fbb.hv + 4.0f; z += 1.0f)
					if (std::fabs(x - fbb.x) > fbb.hu || std::fabs(z - fbb.z) > fbb.hv) ring.push_back(f.spawn_unit(x, z, 1, UNIT_FEMALE_CITIZEN));
			return ring;
		};
		Simulation f;
		const uint32_t fb = fresh(f);
		f.cmd_set_rally_point(0, fb, bld(f, fb).x, bld(f, fb).z, 0, fb);
		CHECK(f.cmd_train(0, fb, UNIT_SPEARMAN, 2));
		run(f, std::ceil(bld(f, fb).queue.front().duration) - 0.5f); // done on a whole second
		ringed(f, fb);
		run(f, 1.0f);
		CHECK(bld(f, fb).queue.empty() && bld(f, fb).garrisoned_units.size() == 2);
		std::printf("autogarrison: a ringed barracks with its rally point on itself takes its batch of %zu inside\n",
				bld(f, fb).garrisoned_units.size());

		ringed(f, fb); // the first ring's idle units have pushed each other apart by now
		f.cmd_unload(0, fb, nullptr, 0);
		f.tick(Simulation::FIXED_STEP);
		CHECK(bld(f, fb).garrisoned_units.size() == 2 && count_units(f, 0, UNIT_SPEARMAN) == 0);

		for (auto &b : const_cast<std::vector<Simulation::Building> &>(f.buildings()))
			if (b.id == fb) b.hp = 1.0f; // below the garrison threshold
		f.tick(Simulation::FIXED_STEP);
		auto u = f.units();
		int dying = 0;
		for (size_t i = 0; i < u.count; ++i) if (u.team[i] == 0 && u.unit_type[i] == UNIT_SPEARMAN && u.action_state[i] == ACTION_DYING) ++dying;
		CHECK(bld(f, fb).garrisoned_units.empty() && count_units(f, 0, UNIT_SPEARMAN) == 0 && dying == 2);
		std::printf("no room: unloading leaves them inside; the structure failing kills the %d that can't get out\n", dying);
	}

	// 12. Population (Player.js, Cost/Population, Population/Bonus): no free base, a Civic
	//     Centre gives 20, a corral nothing, the big house 10 for athen and 5 for the Britons'
	//     small house; heroes cost 0, rams 3; training at the cap queues and pays, then waits.
	{
		Simulation s;
		s.set_sim_seed(5);
		s.reset();
		CHECK(s.pop_max(0) == 0);
		s.spawn_building_world(40.0f, 40.0f, BLD_TOWN_CENTER, 0, 1.0f);
		CHECK(s.pop_max(0) == 20);
		s.spawn_building_world(20.0f, 40.0f, BLD_CORRAL, 0, 1.0f);
		CHECK(s.pop_max(0) == 20);
		s.spawn_building_world(20.0f, 20.0f, BLD_HOUSE, 0, 1.0f);
		CHECK(s.pop_max(0) == 30); // athen/house: template_structure_civic_house_big
		CHECK(civ_building_type_info(CIV_BRITONS, BLD_HOUSE).pop_bonus == 5 &&
		      civ_building_type_info(CIV_BRITONS, BLD_HOUSE).wood_cost == building_type_info(BLD_HOUSE).wood_cost);
		CHECK(civ_building_type_info(CIV_ATHENIANS, BLD_HOUSE).wood_cost == 150);
		CHECK(civ_building_type_info(CIV_CARTHAGINIANS, BLD_APARTMENT).pop_bonus == building_type_info(BLD_APARTMENT).pop_bonus);
		uint16_t hero = CIV_UNIT_TEMPLATE_NONE, ram = CIV_UNIT_TEMPLATE_NONE;
		for (uint16_t t = 0; t < CIV_UNIT_TEMPLATE_COUNT; ++t) {
			if (std::strcmp(civ_unit_template(t).id, "athen/hero_pericles") == 0) hero = t;
			if (std::strcmp(civ_unit_template(t).id, "athen/siege_ram") == 0) ram = t;
		}
		CHECK(hero != CIV_UNIT_TEMPLATE_NONE && ram != CIV_UNIT_TEMPLATE_NONE);
		s.spawn_unit(0.0f, 0.0f, 0, UNIT_HERO, 0.0f, hero);
		CHECK(s.pop_current(0) == 0);
		s.spawn_unit(2.0f, 0.0f, 0, UNIT_SIEGE_RAM, 0.0f, ram);
		CHECK(s.pop_current(0) == 3);
		for (int k = 0; k < 27; ++k) s.spawn_unit(10.0f + (k % 9), -10.0f - k / 9, 0, UNIT_SPEARMAN);
		CHECK(s.pop_current(0) == s.pop_max(0));
		s.set_player_resources(0, 1000, 1000, 1000, 1000);
		// In the Civic Centre's territory: alone it would decay to Gaia (TerritoryDecay.js).
		const uint32_t b = s.spawn_building_world(55.0f, 15.0f, BLD_BARRACKS, 0, 1.0f);
		CHECK(s.cmd_train(0, b, UNIT_SPEARMAN, 2)); // queued and paid at the cap
		CHECK(s.player(0).res[RES_FOOD] < 1000.0f);
		run(s, 30.0f);
		CHECK(bld(s, b).queue.size() == 1 && !bld(s, b).queue.front().started);
		s.spawn_building_world(20.0f, 10.0f, BLD_HOUSE, 0, 1.0f);
		run(s, 30.0f);
		CHECK(bld(s, b).queue.empty() && s.pop_current(0) == 32);
		std::printf("population: CC 20, athen house 10, hero 0, ram 3; a batch at the cap waits for a house\n");
	}

	// 13. ProductionQueue.js OnOwnershipChanged -> ResetQueue also runs when the structure is
	//     destroyed or deleted: every item is refunded, the started batch's population is freed
	//     and the phase it was researching can be queued again elsewhere.
	{
		Simulation s;
		s.set_sim_seed(5);
		s.reset();
		s.set_player_resources(0, 20000, 20000, 20000, 20000);
		const uint32_t tc = s.spawn_building_world(-30.0f, -30.0f, BLD_TOWN_CENTER, 0, 1.0f);
		const uint32_t tc2 = s.spawn_building_world(30.0f, 30.0f, BLD_TOWN_CENTER, 0, 1.0f);
		for (int h = 0; h < 5; ++h) s.spawn_building_world(-10.0f + 6.0f * h, 20.0f, BLD_HOUSE, 0, 1.0f);
		const float before = total_res(s);
		CHECK(s.cmd_train(0, tc, UNIT_FEMALE_CITIZEN, 3));
		CHECK(s.cmd_research(0, tc, TECH_PHASE_TOWN));
		run(s, 1.0f);
		CHECK(bld(s, tc).queue.front().started && s.pop_current(0) == 3);
		s.cmd_delete(0, &tc, 1);
		run(s, 0.5f);
		std::printf("destroyed trainer: resources %.0f of %.0f back, pop %d, town phase queued %d\n",
				total_res(s), before, s.pop_current(0), (int)s.tech_queued(0, TECH_PHASE_TOWN));
		CHECK(bld(s, tc).queue.empty() && std::fabs(total_res(s) - before) < 1.0f);
		CHECK(s.pop_current(0) == 0 && !s.tech_queued(0, TECH_PHASE_TOWN));
		CHECK(s.research_check(0, TECH_PHASE_TOWN, tc2) == Simulation::TECH_OK);
	}

	// 14. ProductionQueue.js ProgressTimeout: the queue works in whole seconds from when it
	//     started, so a lone batch ends on the second after its time is up, and what is left
	//     of a second goes to the next item.
	{
		Simulation s;
		const uint32_t b = fresh(s);
		CHECK(s.cmd_train(0, b, UNIT_SPEARMAN, 5));
		const float t = bld(s, b).queue.front().duration;
		CHECK(std::fabs(t - std::ceil(t)) > 0.05f); // 10 x 5^0.8 = 36.24 s
		int ticks = 0;
		while (!bld(s, b).queue.empty() && ticks < 4000) { s.tick(Simulation::FIXED_STEP); ++ticks; }
		std::printf("batch of 5: %.2f s train time, done after %.2f s\n", t, ticks * Simulation::FIXED_STEP);
		CHECK(ticks == (int)std::lround(std::ceil(t) / Simulation::FIXED_STEP));
		CHECK(count_units(s, 0, UNIT_SPEARMAN) == 5);

		Simulation s2;
		const uint32_t b2 = fresh(s2);
		CHECK(s2.cmd_train(0, b2, UNIT_SPEARMAN, 5));
		CHECK(s2.cmd_train(0, b2, UNIT_SPEARMAN, 5));
		const float both = bld(s2, b2).queue[0].duration + bld(s2, b2).queue[1].duration;
		ticks = 0;
		while (!bld(s2, b2).queue.empty() && ticks < 8000) { s2.tick(Simulation::FIXED_STEP); ++ticks; }
		CHECK(ticks == (int)std::lround(std::ceil(both) / Simulation::FIXED_STEP)); // 73 s, not 74
	}

	// 15. Builder.CanRepair (IsOwnedByAllyOfPlayer): a player who has declared another an ally
	//     builds that player's foundations, whether or not the alliance is returned.
	{
		Simulation s;
		fresh(s);
		s.set_diplomacy(0, 1, Simulation::DIPLO_ALLY);
		s.set_diplomacy(1, 0, Simulation::DIPLO_NEUTRAL);
		const uint32_t house = s.spawn_building_world(-40.0f, -40.0f, BLD_HOUSE, 1, 0.0f);
		const uint32_t woman = s.spawn_unit(-44.0f, -40.0f, 0, UNIT_FEMALE_CITIZEN);
		s.order_build_or_repair(&woman, 1, house);
		run(s, 10.0f);
		std::printf("one-sided ally builds: progress %.2f\n", bld(s, house).build_progress);
		CHECK(bld(s, house).build_progress > 0.05f);
	}

	if (failures) {
		std::printf("FAIL: %d checks\n", failures);
		return 1;
	}
	std::printf("PASS test_production_queue\n");
	return 0;
}
