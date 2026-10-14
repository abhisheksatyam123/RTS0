// RTS0 unit pushing test (headless: no SCons, no Godot).
// Checks the port of 0 A.D.'s CCmpUnitMotionManager pushing (Simulation::separation) and of
// idle units as obstructions to a walker's short path (Simulation::avoid_idle_units).
// Build and run:
//   g++ -std=c++20 -O2 -Isrc tools/test_unit_pushing.cpp src/simulation/simulation.cpp \
//     src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//     src/network/net_host.cpp -lpthread -o /tmp/test_unit_pushing && /tmp/test_unit_pushing
//
//  1. A walker crosses a block of idle spearmen: it paths round them, and none of them is pushed.
//  2. Spearmen that appear one by one on the same spot (as a ship's passengers come ashore) spread out.
//  3. A group walks in a crowd and ends spread round its point (a second after it arrives: pushing
//     pressure dampens the pushes of a crowd that has just come together). On the way no two walkers
//     come closer than the distance 0 A.D. treats as overlapping (Push: half the moving pair's
//     MovingSpread of its reach), where the push between them is at its strongest.
//  4. Two groups walking through each other both arrive (within 24 s: a walker obstructed by one
//     already standing starts again from rest, test 11).
//  5. The heavier unit pushes harder: a ram and a spearman standing on each other part mostly by
//     the spearman moving (UnitMotion Weight 50 against 10).
//  6. A walker that runs into a pocket of idle spearmen open towards it gets out and round them:
//     from each corner of its short path it searches on instead of heading back into the pocket.
//  7. Pushing pressure: crowds build it up, it slows a walker down to the floor speed, and it decays
//     once the unit walks alone.
//  8. A hero ordered home again every turn, as Petra orders a hurt hero each time it is hit, gets past
//     an idle rider standing by a barracks. Each new order starts its failed turns again, so its short
//     path searches stay short, and the point it walks to counts at the search box's nearest point to
//     it, inside the barracks: after a search that finds no way, the next one looks 32 m round.
//  9. Two swordsmen walking head-on through each other sidestep: 0 A.D. compares where a pair stands
//     with where it stood when the 200 ms turn began, and nudges a pair that crossed sideways. Over
//     one 50 ms tick two walkers never get far enough past each other to count as crossed.
// 10. UnitMotion Weight by template: chariots (champion and hero) 30, elephants (a hero's too) and
//     siege 50, catafalques 100, others 10.
// 11. UnitMotion Acceleration (PerformMove): a ram gains speed at 4.2 m/s^2 and a spearman at 35, so
//     from rest a ram covers a t^2 / 2 for its first 1.7 s and a spearman is at full speed within
//     0.26 s. Turning past InstantTurnAngle (0.75 rad for siege, 1.5 for infantry) starts it again from
//     rest; a gentler turn keeps cos(angle) of its speed.
// 12. A group sent to one point (no formation) gets a point each round it, as Commands.js "walk" does
//     with DistributeAround: a spiral from 1 m out, rings 1.75 m apart, points 9/pi m apart along a
//     ring (at the crowd scale, PUSH_SCALE), handed out so that no two units would travel less by swapping.
//     A unit on its own walks to the point itself.
#define private public
#include "simulation/simulation.h"
#undef private
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace rtso;

static int failures = 0;
#define CHECK(cond, what) do { if (!(cond)) { std::printf("  [FAIL] %s: %s\n", what, #cond); ++failures; } else std::printf("  [ok]   %s\n", what); } while (0)

static constexpr float DT = 0.05f;

static void setup(Simulation &s) {
	s.set_sim_seed(4242);
	s.reset();
	s.spawn_building_world(-52.0f, -52.0f, BLD_TOWN_CENTER, 0, 1.0f);
	s.spawn_building_world(52.0f, 52.0f, BLD_TOWN_CENTER, 1, 1.0f);
}

static int idx(const Simulation &s, uint32_t id) { return s.lookup_unit(id); }

static float dist(const Simulation &s, int a, int b) {
	return std::hypot(s.units_.pos_x[a] - s.units_.pos_x[b], s.units_.pos_z[a] - s.units_.pos_z[b]);
}

// The shortest distance between two of `ids`, and the mean distance to each one's nearest.
static void spacing(const Simulation &s, const std::vector<uint32_t> &ids, float &min_pair, float &mean_nn) {
	min_pair = 1e9f;
	float sum = 0.0f;
	for (uint32_t a : ids) {
		float best = 1e9f;
		for (uint32_t b : ids)
			if (a != b) best = std::min(best, dist(s, idx(s, a), idx(s, b)));
		min_pair = std::min(min_pair, best);
		sum += best;
	}
	mean_nn = sum / (float)ids.size();
}

static bool all_done(const Simulation &s, const std::vector<uint32_t> &ids) {
	for (uint32_t id : ids)
		if (s.units_.has_order[idx(s, id)]) return false;
	return true;
}

int main() {
	std::printf("unit pushing\n");
	{
		auto s = std::make_unique<Simulation>();
		setup(*s);
		std::vector<uint32_t> block;
		for (int k = 0; k < 25; ++k) block.push_back(s->spawn_unit(-2.8f + 1.4f * (k % 5), -2.8f + 1.4f * (k / 5), 0, UNIT_SPEARMAN, 150.0f));
		for (int t = 0; t < 100; ++t) s->tick(DT);
		std::vector<float> bx, bz;
		for (uint32_t id : block) bx.push_back(s->units_.pos_x[idx(*s, id)]), bz.push_back(s->units_.pos_z[idx(*s, id)]);
		const uint32_t w = s->spawn_unit(-12.0f, 0.0f, 0, UNIT_SWORDSMAN, 150.0f);
		s->order_walk(&w, 1, 12.0f, 0.0f);
		int arrived = -1;
		float moved = 0.0f, closest = 1e9f;
		for (int t = 1; t <= 600 && arrived < 0; ++t) {
			s->tick(DT);
			const int wi = idx(*s, w);
			for (size_t b = 0; b < block.size(); ++b) {
				const int j = idx(*s, block[b]);
				moved = std::max(moved, std::hypot(s->units_.pos_x[j] - bx[b], s->units_.pos_z[j] - bz[b]));
				closest = std::min(closest, dist(*s, wi, j));
			}
			if (!s->units_.has_order[wi]) arrived = t;
		}
		const int wi = idx(*s, w);
		std::printf("  walker through an idle block: done after %d ticks at (%.2f, %.2f), block moved %.3f, closest %.2f\n",
		            arrived, s->units_.pos_x[wi], s->units_.pos_z[wi], moved, closest);
		CHECK(arrived > 0 && std::hypot(s->units_.pos_x[wi] - 12.0f, s->units_.pos_z[wi]) < 1.0f, "a walker gets past an idle block to its point");
		CHECK(moved < 0.05f, "and pushes none of the idle spearmen");
		CHECK(closest >= 2.0f * unit_collision_radius(UNIT_SPEARMAN), "and never walks through one");
	}
	{
		auto s = std::make_unique<Simulation>();
		setup(*s);
		std::vector<uint32_t> ids;
		for (int t = 0; t < 15 * 40 + 300; ++t) {
			if (t % 40 == 0 && ids.size() < 15) ids.push_back(s->spawn_unit(5.0f, 0.0f, 0, UNIT_SPEARMAN, 150.0f));
			s->tick(DT);
		}
		float min_pair, mean_nn;
		spacing(*s, ids, min_pair, mean_nn);
		std::printf("  15 spearmen appearing on one spot: nearest %.2f on average, %.2f at least\n", mean_nn, min_pair);
		CHECK(min_pair >= 1.8f * unit_collision_radius(UNIT_SPEARMAN), "units appearing on one spot spread out");
	}
	{
		auto s = std::make_unique<Simulation>();
		setup(*s);
		std::vector<uint32_t> ids;
		for (int k = 0; k < 30; ++k) ids.push_back(s->spawn_unit(-20.0f + 1.4f * (k % 6), -3.5f + 1.4f * (k / 6), 0, UNIT_SWORDSMAN, 150.0f));
		s->order_walk(ids.data(), ids.size(), 20.0f, 0.0f);
		int done = -1;
		float walking_min = 1e9f;
		for (int t = 1; t <= 800 && done < 0; ++t) {
			s->tick(DT);
			if (t >= 20 && t <= 120) {
				float min_pair, mean_nn;
				spacing(*s, ids, min_pair, mean_nn);
				walking_min = std::min(walking_min, min_pair);
			}
			if (all_done(*s, ids)) done = t;
		}
		for (int t = 0; t < 20; ++t) s->tick(DT);
		float min_pair, mean_nn, far = 0.0f;
		spacing(*s, ids, min_pair, mean_nn);
		for (uint32_t id : ids) far = std::max(far, std::hypot(s->units_.pos_x[idx(*s, id)] - 20.0f, s->units_.pos_z[idx(*s, id)]));
		std::printf("  30 walking 40 units: done after %d ticks, %.2f apart at least on the way, %.2f a second later, %.2f from the point at most\n",
		            done, walking_min, min_pair, far);
		CHECK(done > 0 && done <= 300, "a group walk ends");
		// A walking crowd closes up as in 0 A.D.: its rear carries less pressure, so walks faster, and the
		// pushes between walkers mostly cancel under MinimalForce until a pair comes within half its
		// MovingSpread distance (CCmpUnitMotion_System.cpp Push), 0.96 for two swordsmen. Where the
		// minimum lands above that depends on the walk speed (with walk speeds scaled from 0.9 to 1.3
		// it ranges from 0.99 to 1.38), so the check is 0 A.D.'s bound, not the bodies' 1.1. Walking
		// on to their own points round the target (DistributeAround, test 12) their ways converge, and a
		// pair can pass a hair inside it (0.95 against 0.96).
		const float c = unit_push_clearance(UNIT_SWORDSMAN);
		const float overlap = ((c + c) * (5.0f / 7.0f) * 1.4f + 4.0f * PUSH_SCALE) * 0.4f * 0.5f;
		std::printf("  walkers' overlap distance %.2f\n", overlap);
		CHECK(walking_min >= 0.97f * overlap, "walkers don't overlap on the way");
		CHECK(min_pair >= 2.0f * unit_collision_radius(UNIT_SWORDSMAN) && far < 7.0f, "and stand apart round their point");
	}
	{
		auto s = std::make_unique<Simulation>();
		setup(*s);
		std::vector<uint32_t> g1, g2;
		for (int k = 0; k < 16; ++k) g1.push_back(s->spawn_unit(-15.0f + 1.4f * (k % 4), -2.1f + 1.4f * (k / 4), 0, UNIT_SWORDSMAN, 150.0f));
		for (int k = 0; k < 16; ++k) g2.push_back(s->spawn_unit(11.0f + 1.4f * (k % 4), -2.1f + 1.4f * (k / 4), 0, UNIT_ARCHER, 150.0f));
		s->order_walk(g1.data(), g1.size(), 15.0f, 0.0f);
		s->order_walk(g2.data(), g2.size(), -15.0f, 0.0f);
		int done = -1, pressure = 0;
		for (int t = 1; t <= 800 && done < 0; ++t) {
			s->tick(DT);
			for (uint32_t id : g1) pressure = std::max<int>(pressure, s->units_.motion[idx(*s, id)].pressure);
			if (all_done(*s, g1) && all_done(*s, g2)) done = t;
		}
		float far1 = 0.0f, far2 = 0.0f;
		for (uint32_t id : g1) far1 = std::max(far1, std::hypot(s->units_.pos_x[idx(*s, id)] - 15.0f, s->units_.pos_z[idx(*s, id)]));
		for (uint32_t id : g2) far2 = std::max(far2, std::hypot(s->units_.pos_x[idx(*s, id)] + 15.0f, s->units_.pos_z[idx(*s, id)]));
		std::printf("  two groups crossing: done after %d ticks, %.2f and %.2f from their points at most, pressure up to %d\n",
		            done, far1, far2, pressure);
		CHECK(done > 0 && done <= 480 && far1 < 6.0f && far2 < 6.0f, "two groups walk through each other");
		CHECK(pressure > 10, "and the crowd's pushing pressure slows them");
	}
	{
		auto s = std::make_unique<Simulation>();
		setup(*s);
		const uint32_t ram = s->spawn_unit(0.0f, 0.0f, 0, UNIT_SIEGE_RAM, 0.0f);
		const uint32_t man = s->spawn_unit(0.3f, 0.0f, 0, UNIT_SPEARMAN, 0.0f);
		for (int t = 0; t < 100; ++t) s->tick(DT);
		const int r = idx(*s, ram), m = idx(*s, man);
		const float ram_moved = std::hypot(s->units_.pos_x[r], s->units_.pos_z[r]);
		const float man_moved = std::hypot(s->units_.pos_x[m] - 0.3f, s->units_.pos_z[m]);
		std::printf("  ram and spearman on one spot: ram moved %.2f, spearman %.2f, %.2f apart\n", ram_moved, man_moved, dist(*s, r, m));
		CHECK(man_moved > 3.0f * ram_moved, "the heavier unit pushes harder");
		CHECK(dist(*s, r, m) >= unit_collision_radius(UNIT_SIEGE_RAM) + unit_collision_radius(UNIT_SPEARMAN), "and they part");
	}
	{
		auto s = std::make_unique<Simulation>();
		setup(*s);
		std::vector<uint32_t> pocket; // a wall at x = 2 and two arms reaching west from its ends
		for (int k = -2; k <= 2; ++k) pocket.push_back(s->spawn_unit(2.0f, 1.5f * k, 0, UNIT_SPEARMAN, 150.0f));
		for (int k = 1; k <= 3; ++k) {
			pocket.push_back(s->spawn_unit(2.0f - 1.5f * k, 3.0f, 0, UNIT_SPEARMAN, 150.0f));
			pocket.push_back(s->spawn_unit(2.0f - 1.5f * k, -3.0f, 0, UNIT_SPEARMAN, 150.0f));
		}
		for (int t = 0; t < 100; ++t) s->tick(DT);
		const uint32_t w = s->spawn_unit(-12.0f, 0.0f, 0, UNIT_SWORDSMAN, 150.0f);
		s->order_walk(&w, 1, 12.0f, 0.0f);
		int arrived = -1;
		float closest = 1e9f;
		for (int t = 1; t <= 800 && arrived < 0; ++t) {
			s->tick(DT);
			const int wi = idx(*s, w);
			for (uint32_t id : pocket) closest = std::min(closest, dist(*s, wi, idx(*s, id)));
			if (!s->units_.has_order[wi]) arrived = t;
		}
		const int wi = idx(*s, w);
		std::printf("  walker into a pocket: done after %d ticks at (%.2f, %.2f), closest %.2f\n",
		            arrived, s->units_.pos_x[wi], s->units_.pos_z[wi], closest);
		CHECK(arrived > 0 && std::hypot(s->units_.pos_x[wi] - 12.0f, s->units_.pos_z[wi]) < 1.0f, "a walker gets out of a pocket of idle units to its point");
		CHECK(closest >= 2.0f * unit_collision_radius(UNIT_SPEARMAN), "and never walks through one");
	}
	{
		// A lone walker under pressure 200 walks at the floor speed (1.5 m/s at the rosters' scale);
		// with nobody pushing it the pressure decays by 0.6 a turn and it walks at full speed again.
		auto s = std::make_unique<Simulation>();
		setup(*s);
		const uint32_t w = s->spawn_unit(-10.0f, 0.0f, 0, UNIT_SWORDSMAN, 150.0f);
		s->order_walk(&w, 1, 30.0f, 0.0f);
		const int wi = idx(*s, w);
		s->units_.motion[wi].pressure = 200;
		const float speed = s->unit_walk_speed(wi);
		s->tick(DT); // from rest: the floor speed is reached within the first tick (test 11)
		const float x0 = s->units_.pos_x[wi];
		s->tick(DT);
		const float slow = (s->units_.pos_x[wi] - x0) / DT;
		for (int t = 0; t < 60; ++t) s->tick(DT);
		const float x1 = s->units_.pos_x[wi];
		s->tick(DT);
		const float fast = (s->units_.pos_x[wi] - x1) / DT;
		std::printf("  lone walker under pressure 200: %.2f a second (walk speed %.2f), pressure %d after 3 s, then %.2f a second\n",
		            slow, speed, (int)s->units_.motion[wi].pressure, fast);
		CHECK(std::fabs(slow - 1.5f * FORMATION_METRE) < 0.01f, "pressure slows a walker to the floor speed");
		CHECK(s->units_.motion[wi].pressure == 0, "and decays when nobody pushes it");
		CHECK(std::fabs(fast - speed) < 0.01f, "after which it walks at full speed");
	}
	{
		// The layout of test_petra's regicide match where the hero stuck: its Civic Center, a barracks
		// beside it and a rider of its own standing at the barracks' corner, in the hero's way home.
		auto s = std::make_unique<Simulation>();
		s->set_sim_seed(4242);
		s->reset();
		s->spawn_building_world(-24.89f, -24.89f, BLD_TOWN_CENTER, 0, 1.0f);
		s->spawn_building_world(52.0f, 52.0f, BLD_TOWN_CENTER, 1, 1.0f);
		s->spawn_building_world(-18.88f, -18.88f, BLD_BARRACKS, 0, 1.0f);
		s->spawn_unit(-16.054f, -16.237f, 0, UNIT_CAVALRY, 150.0f);
		const uint32_t hero = s->spawn_unit(-13.779f, -14.946f, 0, UNIT_HERO, 150.0f);
		const int hi = idx(*s, hero);
		const float d0 = std::hypot(s->units_.pos_x[hi] + 24.89f, s->units_.pos_z[hi] + 24.89f);
		for (int t = 0; t < 400; ++t) {
			if (t % 4 == 0) s->order_walk(&hero, 1, -24.89f, -24.89f);
			s->tick(DT);
		}
		const float d1 = std::hypot(s->units_.pos_x[hi] + 24.89f, s->units_.pos_z[hi] + 24.89f);
		std::printf("  hero ordered home every turn past an idle rider: %.1f from its Civic Center, %.1f after 20 s\n", d0, d1);
		CHECK(d1 < 4.0f, "a walker ordered again every turn gets past an idle unit by a structure");
	}
	{
		auto s = std::make_unique<Simulation>();
		setup(*s);
		uint32_t a = s->spawn_unit(-10.0f, 0.0f, 0, UNIT_SWORDSMAN, 0.0f);
		uint32_t b = s->spawn_unit(10.0f, 0.0f, 0, UNIT_SWORDSMAN, 0.0f);
		s->cmd_move(&a, 1, 10.0f, 0.0f);
		s->cmd_move(&b, 1, -10.0f, 0.0f);
		float apart = 0.0f;
		int done = -1;
		for (int t = 0; t < 400 && done < 0; ++t) {
			s->tick(DT);
			const int ia = idx(*s, a), ib = idx(*s, b);
			apart = std::max(apart, std::fabs(s->units_.pos_z[ia] - s->units_.pos_z[ib]));
			if (!s->units_.has_order[ia] && !s->units_.has_order[ib]) done = t;
		}
		std::printf("  two swordsmen head-on: %.2f apart sideways at most, both arrived after %d ticks\n", apart, done);
		CHECK(apart > 0.3f, "walkers that pass through each other are nudged sideways");
		CHECK(done > 0 && done <= 120, "and both arrive");
	}
	{
		auto tmpl = [](const char *id) {
			for (uint16_t t = 0; t < CIV_UNIT_TEMPLATE_COUNT; ++t)
				if (std::strcmp(civ_unit_template(t).id, id) == 0) return t;
			return (uint16_t)CIV_UNIT_TEMPLATE_NONE;
		};
		const float chariot = unit_push_weight(tmpl("brit/champion_chariot"), UNIT_CHARIOT);
		const float hero_chariot = unit_push_weight(tmpl("pers/hero_darius_i"), UNIT_HERO);
		const float hero_elephant = unit_push_weight(tmpl("maur/hero_chandragupta"), UNIT_HERO);
		const float catafalque = unit_push_weight(tmpl("athen/catafalque"), UNIT_CATAFALQUE);
		const float spearman = unit_push_weight(tmpl("athen/infantry_spearman_b"), UNIT_SPEARMAN);
		std::printf("  weights: chariot %.0f, hero chariot %.0f, hero elephant %.0f, catafalque %.0f, spearman %.0f, ram %.0f\n",
		            chariot, hero_chariot, hero_elephant, catafalque, spearman, (float)unit_push_weight(CIV_UNIT_TEMPLATE_NONE, UNIT_SIEGE_RAM));
		CHECK(chariot == 30.0f && hero_chariot == 30.0f, "chariots weigh 30 (mixins/chariot)");
		CHECK(hero_elephant == 50.0f && unit_push_weight(CIV_UNIT_TEMPLATE_NONE, UNIT_SIEGE_RAM) == 50.0f, "elephants and siege 50");
		CHECK(catafalque == 100.0f && spearman == 10.0f, "catafalques 100, foot soldiers 10");
		// unit_push_weight reads the roster's classes only for heroes: every row's weight must
		// match the rule its classes give.
		int wrong = 0;
		for (uint16_t t = 0; t < CIV_UNIT_TEMPLATE_COUNT; ++t) {
			const CivUnitTemplate &row = civ_unit_template(t);
			const TechClasses c = row.tech_classes;
			const int want = row.unit_type == UNIT_CATAFALQUE ? 100
					: (c & (TC_SIEGE | TC_ELEPHANT)) != 0 || row.unit_type == UNIT_WAGON ? 50 : (c & TC_CHARIOT) != 0 ? 30 : 10;
			if (unit_push_weight(t, row.unit_type) != want) {
				std::printf("  %s: weight %d, its classes say %d\n", row.id, unit_push_weight(t, row.unit_type), want);
				++wrong;
			}
		}
		CHECK(wrong == 0, "every roster row weighs what its classes say");
	}
	{
		auto s = std::make_unique<Simulation>();
		setup(*s);
		uint32_t ram = s->spawn_unit(-20.0f, 10.0f, 0, UNIT_SIEGE_RAM, 0.0f);
		uint32_t spear = s->spawn_unit(-20.0f, -10.0f, 0, UNIT_SPEARMAN, 0.0f);
		const int ir = idx(*s, ram), is = idx(*s, spear);
		const float a_ram = unit_motion(s->units_.tmpl[ir], UNIT_SIEGE_RAM).acceleration;
		const float a_spear = unit_motion(s->units_.tmpl[is], UNIT_SPEARMAN).acceleration;
		const float v_ram = s->unit_move_speed(ir), v_spear = s->unit_move_speed(is);
		s->cmd_move(&ram, 1, 20.0f, 10.0f);
		s->cmd_move(&spear, 1, 20.0f, -10.0f);
		auto speed = [&](int i) { return std::hypot(s->units_.vel_x[i], s->units_.vel_z[i]); };
		// Distance from rest after t seconds at acceleration a up to speed v.
		auto from_rest = [](float a, float v, float t) {
			const float ta = std::min(t, v / a);
			return 0.5f * a * ta * ta + v * (t - ta);
		};
		for (int t = 0; t < 10; ++t) s->tick(DT);
		const float ram_05 = s->units_.pos_x[ir] + 20.0f, spear_05 = s->units_.pos_x[is] + 20.0f;
		for (int t = 0; t < 10; ++t) s->tick(DT);
		const float ram_10 = s->units_.pos_x[ir] + 20.0f, ram_v10 = speed(ir);
		for (int t = 0; t < 30; ++t) s->tick(DT);
		const float ram_v25 = speed(ir);
		std::printf("  acceleration: ram %.2f, spearman %.2f units/s^2; after 0.5 s the ram has gone %.3f (%.3f expected,"
		            " %.3f at full speed), the spearman %.3f (%.3f, %.3f); the ram at 1 s %.3f (%.3f) at %.2f of %.2f\n",
		            a_ram, a_spear, ram_05, from_rest(a_ram, v_ram, 0.5f), v_ram * 0.5f, spear_05,
		            from_rest(a_spear, v_spear, 0.5f), v_spear * 0.5f, ram_10, from_rest(a_ram, v_ram, 1.0f), ram_v10, v_ram);
		CHECK(std::fabs(a_ram - 4.2f * 0.356f) < 0.01f && std::fabs(a_spear - 35.0f * 0.356f) < 0.01f,
		      "rams accelerate at 4.2 m/s^2 and spearmen at 35, at the speed scale");
		CHECK(std::fabs(ram_05 - from_rest(a_ram, v_ram, 0.5f)) < 0.02f && std::fabs(ram_10 - from_rest(a_ram, v_ram, 1.0f)) < 0.03f,
		      "a ram from rest covers a t^2 / 2");
		CHECK(std::fabs(spear_05 - from_rest(a_spear, v_spear, 0.5f)) < 0.03f, "a spearman reaches full speed within 0.26 s");
		CHECK(ram_v10 < 0.7f * v_ram && std::fabs(ram_v25 - v_ram) < 1e-3f, "the ram reaches full speed after 1.7 s");
		// Turn back: past InstantTurnAngle, so both start again from rest.
		const float rx = s->units_.pos_x[ir], sx = s->units_.pos_x[is];
		s->cmd_move(&ram, 1, -20.0f, 10.0f);
		s->cmd_move(&spear, 1, -20.0f, -10.0f);
		s->tick(DT);
		std::printf("  turned back: the ram moved %.4f in the first tick at %.3f, the spearman %.4f at %.3f\n",
		            rx - s->units_.pos_x[ir], speed(ir), sx - s->units_.pos_x[is], speed(is));
		CHECK(std::fabs(speed(ir) - a_ram * DT) < 1e-3f && std::fabs(speed(is) - a_spear * DT) < 1e-3f,
		      "turning back starts a unit again from rest");
		// A gentle turn: 30 degrees off a full-speed walk keeps cos(30) of the speed.
		for (int t = 0; t < 40; ++t) s->tick(DT);
		const float v0 = speed(ir);
		s->cmd_move(&ram, 1, s->units_.pos_x[ir] - 10.0f * std::cos(0.5236f), s->units_.pos_z[ir] + 10.0f * std::sin(0.5236f));
		s->tick(DT);
		const float want = std::min(v_ram, v0 * std::cos(0.5236f) + a_ram * DT);
		std::printf("  a 30 degree turn at %.3f: %.3f after one tick (%.3f expected)\n", v0, speed(ir), want);
		CHECK(std::fabs(v0 - v_ram) < 1e-3f && std::fabs(speed(ir) - want) < 2e-3f, "a turn short of InstantTurnAngle keeps cos(angle) of the speed");
	}
	{
		auto s = std::make_unique<Simulation>();
		setup(*s);
		std::vector<uint32_t> group;
		for (int k = 0; k < 10; ++k) group.push_back(s->spawn_unit(-20.0f + 1.5f * (k % 2), -7.0f + 1.5f * k, 0, UNIT_SPEARMAN, 0.0f));
		std::vector<std::pair<float, float>> from;
		for (uint32_t id : group) from.emplace_back(s->units_.pos_x[idx(*s, id)], s->units_.pos_z[idx(*s, id)]);
		s->cmd_move(group.data(), group.size(), 10.0f, 5.0f);
		// Ring radii in metres: 1 (3 points), 2.75 (7 points).
		int ring1 = 0, ring2 = 0, other = 0, swaps = 0;
		std::vector<std::pair<float, float>> to;
		for (uint32_t id : group) {
			const int i = idx(*s, id);
			to.emplace_back(s->units_.target_x[i], s->units_.target_z[i]);
			const float r = std::hypot(s->units_.target_x[i] - 10.0f, s->units_.target_z[i] - 5.0f) / PUSH_SCALE;
			(std::fabs(r - 1.0f) < 0.01f ? ring1 : std::fabs(r - 2.75f) < 0.01f ? ring2 : other)++;
		}
		auto d2 = [](std::pair<float, float> a, std::pair<float, float> b) {
			const int64_t dx = (int64_t)std::ceil((a.first - b.first) / PUSH_SCALE);
			const int64_t dz = (int64_t)std::ceil((a.second - b.second) / PUSH_SCALE);
			return dx * dx + dz * dz;
		};
		for (size_t a = 0; a < to.size(); ++a)
			for (size_t b = a + 1; b < to.size(); ++b)
				if (d2(to[b], from[a]) + d2(to[a], from[b]) < d2(to[a], from[a]) + d2(to[b], from[b])) ++swaps;
		std::printf("  group of 10 to one point: %d on the first ring, %d on the second, %d elsewhere; %d swaps would cut travel\n",
		            ring1, ring2, other, swaps);
		CHECK(ring1 == 3 && ring2 == 7 && other == 0, "a group spreads on DistributeAround's spiral");
		CHECK(swaps == 0, "and no two units would travel less by swapping points");
		uint32_t lone = s->spawn_unit(20.0f, 20.0f, 0, UNIT_SPEARMAN, 0.0f);
		s->cmd_move(&lone, 1, 25.0f, 25.0f);
		CHECK(s->units_.target_x[idx(*s, lone)] == 25.0f && s->units_.target_z[idx(*s, lone)] == 25.0f, "a unit on its own walks to the point");
	}
	std::printf(failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", failures);
	return failures ? 1 : 0;
}
