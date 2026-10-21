// 0 A.D. Trainer on units (unit_trainer_data.h): Athenian triremes train marines and Cretan
// archers, Persian triremes train cavalry after Equine Transports, Cyrus II trains Immortals.
// The queue pays, reserves population, spawns beside the trainer, refunds on cancel and on
// the trainer's death, and survives save/load with the lockstep hash.
//   g++ -std=c++20 -O2 -Isrc tools/test_unit_trainers.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//       src/network/net_host.cpp -lpthread -o /tmp/test_unit_trainers && /tmp/test_unit_trainers
#include "simulation/simulation.h"
#include "network/turn_manager.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace rtso;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static uint16_t tmpl_of(const char *id) {
	for (uint16_t t = 0; t < CIV_UNIT_TEMPLATE_COUNT; ++t)
		if (std::strcmp(civ_unit_template(t).id, id) == 0) return t;
	return CIV_UNIT_TEMPLATE_NONE;
}

static void fresh(Simulation &s, uint8_t civ) {
	s.set_sim_seed(33);
	s.reset();
	s.set_player_civ(civ);
	s.set_player_resources(0, 5000, 5000, 5000, 5000);
	s.spawn_building_world(-40.0f, -40.0f, BLD_TOWN_CENTER, 0, 1.0f);
	for (int h = 0; h < 6; ++h) s.spawn_building_world(-40.0f + 6.0f * h, 40.0f, BLD_HOUSE, 0, 1.0f);
}

static void run(Simulation &s, float seconds) {
	for (int t = 0; t < (int)std::lround(seconds / Simulation::FIXED_STEP); ++t) s.tick(Simulation::FIXED_STEP);
}

static int count_tmpl(const Simulation &s, uint16_t tmpl) {
	int n = 0;
	auto v = s.units();
	for (size_t i = 0; i < v.count; ++i) n += v.alive[i] && v.tmpl[i] == tmpl;
	return n;
}

static bool has(const std::vector<uint16_t> &v, uint16_t t) { return std::find(v.begin(), v.end(), t) != v.end(); }

int main() {
	setvbuf(stdout, nullptr, _IONBF, 0);
	const uint16_t athen_arrow = tmpl_of("athen/ship_arrow"), athen_ram = tmpl_of("athen/ship_ram");
	const uint16_t marine = tmpl_of("athen/champion_marine_dock"), archer = tmpl_of("athen/infantry_archer_b_dock");
	const uint16_t pers_arrow = tmpl_of("pers/ship_arrow"), axe = tmpl_of("pers/cavalry_axeman_b");
	const uint16_t jav = tmpl_of("pers/cavalry_javelineer_b"), cyrus = tmpl_of("pers/hero_cyrus_ii");
	const uint16_t immortal = tmpl_of("pers/champion_infantry");
	CHECK(athen_arrow != CIV_UNIT_TEMPLATE_NONE && marine != CIV_UNIT_TEMPLATE_NONE && pers_arrow != CIV_UNIT_TEMPLATE_NONE);
	CHECK(cyrus != CIV_UNIT_TEMPLATE_NONE && immortal != CIV_UNIT_TEMPLATE_NONE && axe != CIV_UNIT_TEMPLATE_NONE);

	// 1. Data: who trains what.
	CHECK(Simulation::unit_is_trainer(athen_arrow) && Simulation::unit_is_trainer(athen_ram));
	CHECK(Simulation::unit_is_trainer(pers_arrow) && Simulation::unit_is_trainer(cyrus));
	CHECK(!Simulation::unit_is_trainer(marine) && !Simulation::unit_is_trainer(tmpl_of("rome/ship_arrow")));
	CHECK(tech_info(TECH_EQUINE_TRANSPORTS).researcher == BLD_DOCK && tech_info(TECH_EQUINE_TRANSPORTS).required_phase == 3);
	CHECK(tech_available_to_civ(TECH_EQUINE_TRANSPORTS, CIV_PERSIANS) && !tech_available_to_civ(TECH_EQUINE_TRANSPORTS, CIV_ATHENIANS));

	static Simulation s;

	// 2. An Athenian trireme trains marines (BatchTimeModifier 0.7) and they step out beside it.
	{
		fresh(s, CIV_ATHENIANS);
		s.player(0).phase = 3;
		const uint32_t ship = s.spawn_unit(10.0f, 10.0f, 0, UNIT_TRIREME, 0.0f, athen_arrow);
		// Both need Iphicratean Reforms (Identity/Requirements "-phase_town iphicratean_reforms").
		CHECK(s.unit_trainables(0, athen_arrow).empty());
		CHECK(!s.cmd_train_from_unit(0, ship, marine));
		// The trireme researches it itself (Researcher/Technologies), as the dock does.
		const std::vector<uint8_t> techs = Simulation::unit_researchables(athen_arrow);
		CHECK(techs.size() == 1 && techs[0] == TECH_IPHICRATEAN_REFORMS);
		CHECK(Simulation::unit_researchables(tmpl_of("pers/ship_arrow")).empty());
		const float w0 = s.player(0).wood() + s.player(0).food() + s.player(0).gold() + s.player(0).stone();
		CHECK(s.cmd_research_from_unit(0, ship, TECH_IPHICRATEAN_REFORMS));
		CHECK(!s.cmd_research_from_unit(0, ship, TECH_IPHICRATEAN_REFORMS)); // in progress
		CHECK(s.research_check(0, TECH_IPHICRATEAN_REFORMS) == Simulation::TECH_ERR_IN_PROGRESS);
		CHECK(s.player(0).wood() + s.player(0).food() + s.player(0).gold() + s.player(0).stone() < w0);
		run(s, tech_info(TECH_IPHICRATEAN_REFORMS).research_time + 1.0f);
		CHECK(s.player(0).has_tech(TECH_IPHICRATEAN_REFORMS));
		CHECK(!s.unit_queue(ship));
		const auto can = s.unit_trainables(0, athen_arrow);
		CHECK(has(can, marine) && has(can, archer) && can.size() == 2);
		CHECK(!s.cmd_train_from_unit(1, ship, marine)); // not the owner
		CHECK(!s.cmd_train_from_unit(0, ship, axe));    // not on its list
		const float food = s.player(0).food(), gold = s.player(0).gold();
		CHECK(s.cmd_train_from_unit(0, ship, archer, 3));
		CHECK(s.unit_queue(ship) && s.unit_queue(ship)->size() == 1);
		CHECK(s.player(0).food() < food || s.player(0).gold() < gold);
		const ProductionOrder o = s.unit_queue(ship)->front();
		const float single = civ_template_unit_info(archer, civ_unit_template(archer).unit_type).train_time;
		CHECK(o.count == 3 && o.duration < 3.0f * single && o.duration > 2.0f * single); // 3^0.7 = 2.16
		const int pop0 = s.pop_current(0);
		run(s, 1.0f + Simulation::FIXED_STEP); // the queue's first second starts it (ProgressTimeout)
		CHECK(s.pop_current(0) == pop0 + 3); // the started batch holds its population
		run(s, o.duration + 1.0f);
		CHECK(count_tmpl(s, archer) == 3);
		CHECK(!s.unit_queue(ship));
		CHECK(s.pop_current(0) == pop0 + 3);
		auto v = s.units();
		for (size_t i = 0; i < v.count; ++i)
			if (v.alive[i] && v.tmpl[i] == archer) {
				const float d = std::hypot(v.pos_x[i] - 10.0f, v.pos_z[i] - 10.0f);
				CHECK(d > 1.0f && d < 8.0f);
			}
		std::printf("athenian trireme trained 3 archers in %.1f s\n", o.duration);
	}

	// 3. Cancel refunds; the trainer's death refunds the rest of its queue.
	{
		fresh(s, CIV_ATHENIANS);
		s.player(0).phase = 3;
		s.player(0).techs.set(TECH_IPHICRATEAN_REFORMS);
		const uint32_t ship = s.spawn_unit(10.0f, 10.0f, 0, UNIT_TRIREME, 0.0f, athen_ram);
		const float f0 = s.player(0).food(), g0 = s.player(0).gold(), w0 = s.player(0).wood();
		CHECK(s.cmd_train_from_unit(0, ship, marine));
		CHECK(s.cmd_train_from_unit(0, ship, archer, 2));
		CHECK(s.cmd_cancel_unit_train(0, ship, 1));
		CHECK(s.unit_queue(ship)->size() == 1);
		CHECK(!s.cmd_cancel_unit_train(0, ship, 1));
		run(s, 1.0f);
		s.cmd_delete(0, &ship, 1);
		run(s, 1.0f);
		CHECK(!s.unit_queue(ship));
		CHECK(s.player(0).food() == f0 && s.player(0).gold() == g0 && s.player(0).wood() == w0);
		CHECK(count_tmpl(s, marine) == 0);
	}

	// 4. Persian triremes train cavalry only once Equine Transports is in; Cyrus II trains Immortals.
	{
		fresh(s, CIV_PERSIANS);
		s.player(0).phase = 3;
		const uint32_t ship = s.spawn_unit(10.0f, 10.0f, 0, UNIT_TRIREME, 0.0f, pers_arrow);
		CHECK(s.unit_trainables(0, pers_arrow).empty());
		CHECK(s.unit_trainables(0, pers_arrow, true).size() == 2);
		CHECK(!s.cmd_train_from_unit(0, ship, axe));
		s.player(0).techs.set(TECH_EQUINE_TRANSPORTS);
		CHECK(has(s.unit_trainables(0, pers_arrow), axe) && has(s.unit_trainables(0, pers_arrow), jav));
		CHECK(s.cmd_train_from_unit(0, ship, jav, 2));

		const uint32_t hero = s.spawn_unit(-10.0f, 10.0f, 0, (UnitType)civ_unit_template(cyrus).unit_type, 0.0f, cyrus);
		// pers/champion_infantry: "-phase_city unlock_champion_infantry".
		CHECK(!has(s.unit_trainables(0, cyrus), immortal));
		s.player(0).techs.set(TECH_UNLOCK_CHAMPION_INFANTRY);
		CHECK(has(s.unit_trainables(0, cyrus), immortal));
		CHECK(s.cmd_train_from_unit(0, hero, immortal));

		// 5. Save/load keeps both queues and the lockstep hash.
		run(s, 3.0f);
		TurnManager tm;
		const char *path = "/tmp/rts0_test_unit_trainers.sav";
		CHECK(s.save_state(path));
		static Simulation l;
		fresh(l, CIV_PERSIANS);
		CHECK(l.load_state(path));
		CHECK(l.unit_queue(ship) && l.unit_queue(ship)->front().count == 2 && l.unit_queue(ship)->front().started);
		CHECK(l.unit_queue(hero) && l.unit_queue(hero)->front().tmpl == immortal);
		CHECK(tm.compute_sim_hash(s) == tm.compute_sim_hash(l));
		run(s, 60.0f);
		run(l, 60.0f);
		CHECK(count_tmpl(s, jav) == 2 && count_tmpl(s, immortal) == 1);
		CHECK(count_tmpl(l, jav) == 2 && count_tmpl(l, immortal) == 1);
		CHECK(tm.compute_sim_hash(s) == tm.compute_sim_hash(l));
	}

	// 6. Lockstep: NET_CMD_TRAIN_FROM_UNIT trains (param 0) and cancels (param 1).
	{
		fresh(s, CIV_ATHENIANS);
		s.player(0).phase = 3;
		s.player(0).techs.set(TECH_IPHICRATEAN_REFORMS);
		const uint32_t ship = s.spawn_unit(10.0f, 10.0f, 0, UNIT_TRIREME, 0.0f, athen_arrow);
		TurnManager tm;
		NetCommand c{};
		c.cmd_type = (uint8_t)NetCommandType::NET_CMD_TRAIN_FROM_UNIT;
		c.target_id = ship;
		c.target_x = (float)marine;
		c.target_z = 2.0f;
		tm.post_local_command(c);
		NetCommand x{};
		x.cmd_type = (uint8_t)NetCommandType::NET_CMD_TRAIN_FROM_UNIT;
		x.target_id = ship;
		x.target_x = (float)archer;
		tm.post_local_command(x);
		for (int t = 0; t < 20; ++t) tm.tick(TurnManager::TURN_DURATION, s);
		CHECK(s.unit_queue(ship) && s.unit_queue(ship)->size() == 2 && s.unit_queue(ship)->front().count == 2);
		NetCommand k{};
		k.cmd_type = (uint8_t)NetCommandType::NET_CMD_TRAIN_FROM_UNIT;
		k.target_id = ship;
		k.target_x = 1.0f;
		k.param = 1;
		tm.post_local_command(k);
		for (int t = 0; t < 20; ++t) tm.tick(TurnManager::TURN_DURATION, s);
		CHECK(s.unit_queue(ship) && s.unit_queue(ship)->size() == 1);
	}

	// 7. ProductionQueue OnGarrisonedStateChanged: a garrisoned trainer pauses its batch, and the
	// timer runs on once it is out again.
	{
		fresh(s, CIV_PERSIANS);
		s.player(0).phase = 3;
		s.player(0).techs.set(TECH_UNLOCK_CHAMPION_INFANTRY);
		const uint32_t cc = s.spawn_building_world(-20.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
		const uint32_t hero = s.spawn_unit(-10.0f, 0.0f, 0, (UnitType)civ_unit_template(cyrus).unit_type, 0.0f, cyrus);
		CHECK(s.cmd_train_from_unit(0, hero, immortal));
		s.order_garrison(&hero, 1, cc);
		run(s, 5.0f);
		const float t0 = s.unit_queue(hero) ? s.unit_queue(hero)->front().timer : -1.0f;
		run(s, 60.0f);
		const float t1 = s.unit_queue(hero) ? s.unit_queue(hero)->front().timer : -1.0f;
		std::printf("garrisoned Cyrus: batch timer %.2f -> %.2f in 60 s, %d Immortals\n", t0, t1, count_tmpl(s, immortal));
		CHECK(t0 >= 0.0f && t1 == t0 && count_tmpl(s, immortal) == 0);
		s.cmd_unload(0, cc, &hero, 1);
		run(s, 60.0f);
		CHECK(count_tmpl(s, immortal) == 1);
	}

	// 5. A trainer unit's queue works in whole seconds, as a structure's does, and autoqueues
	//    (ProductionQueue.js EnableAutoQueue on any entity with a ProductionQueue).
	{
		fresh(s, CIV_ATHENIANS);
		s.player(0).phase = 3;
		s.player(0).techs.set(TECH_IPHICRATEAN_REFORMS);
		const uint32_t ship = s.spawn_unit(10.0f, 10.0f, 0, UNIT_TRIREME, 0.0f, athen_arrow);
		CHECK(Simulation::unit_trains_units(athen_arrow) && !Simulation::unit_trains_units(marine));
		CHECK(!s.cmd_set_autoqueue(1, ship, true)); // not the owner
		CHECK(s.cmd_set_autoqueue(0, ship, true) && s.unit_autoqueue(ship));
		CHECK(s.cmd_train_from_unit(0, ship, archer, 2));
		const float t = s.unit_queue(ship)->front().duration;
		int ticks = 0;
		while (count_tmpl(s, archer) < 2 && ticks < 4000) { s.tick(Simulation::FIXED_STEP); ++ticks; }
		CHECK(ticks == (int)std::lround(std::ceil(t) / Simulation::FIXED_STEP));
		CHECK(s.unit_queue(ship) && s.unit_queue(ship)->size() == 1 && s.unit_queue(ship)->front().count == 2); // queued again
		run(s, 2.0f * std::ceil(t) + 1.0f);
		std::printf("autoqueued trireme: %d archers, batch of 2 in %.2f s (%.0f s whole)\n", count_tmpl(s, archer), t, std::ceil(t));
		CHECK(count_tmpl(s, archer) >= 6);
		CHECK(s.cmd_set_autoqueue(0, ship, false) && !s.unit_autoqueue(ship));
		run(s, std::ceil(t) + 1.0f);
		const int n = count_tmpl(s, archer);
		run(s, 2.0f * std::ceil(t));
		CHECK(count_tmpl(s, archer) == n && !s.unit_queue(ship));
		// Saved and loaded with the queue's clock and the autoqueue flag.
		CHECK(s.cmd_set_autoqueue(0, ship, true));
		CHECK(s.cmd_train_from_unit(0, ship, archer, 1));
		run(s, 0.4f);
		static Simulation l;
		CHECK(s.save_state("/tmp/test_unit_trainers_aq.sav") && l.load_state("/tmp/test_unit_trainers_aq.sav"));
		CHECK(l.unit_autoqueue(ship) && l.unit_queues().at(ship).clock == s.unit_queues().at(ship).clock);
	}

	std::printf(failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", failures);
	return failures ? 1 : 0;
}
