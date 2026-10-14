// 0 A.D. GarrisonHolder on land siege: siege towers (20) and rams (10) carry
// Support and Infantry, a siege tower fires one arrow per garrisoned Infantry
// (none when empty), and both unload below 10% health or when destroyed.
// Passengers survive save/load and are in the lockstep hash. A tower or ram sets each
// passenger down on its own free spot round the engine (PickSpawnPoint); one with no spot
// stays aboard, or dies when the engine drops below EjectHealth (EjectOrKill). Military Engineers
// cuts siege pack and unpack time by 40%.
//   g++ -std=c++20 -O2 -Isrc tools/test_siege_garrison.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//       src/network/net_host.cpp -lpthread -o /tmp/test_siege_garrison && /tmp/test_siege_garrison
#include "simulation/simulation.h"
#include "network/turn_manager.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>

using namespace rtso;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static void run(Simulation &s, float seconds) {
	for (int t = 0; t < (int)std::lround(seconds / Simulation::FIXED_STEP); ++t) s.tick(Simulation::FIXED_STEP);
}

static uint16_t template_id(const char *id) {
	for (uint16_t i = 0; i < CIV_UNIT_TEMPLATE_COUNT; ++i)
		if (std::strcmp(civ_unit_template(i).id, id) == 0) return i;
	return CIV_UNIT_TEMPLATE_NONE;
}

static int32_t idx_of(const Simulation &s, uint32_t id) {
	auto uv = s.units();
	for (size_t i = 0; i < uv.count; ++i) if (uv.id[i] == id) return (int32_t)i;
	return -1;
}

static bool alive(const Simulation &s, uint32_t id) {
	int32_t i = idx_of(s, id);
	return i >= 0 && s.units().alive[i];
}

static float hp_of(const Simulation &s, uint32_t id) {
	int32_t i = idx_of(s, id);
	return i >= 0 ? s.units().hp[i] : -1.0f;
}

static void fresh(Simulation &s) {
	s.set_sim_seed(9);
	s.reset();
	s.set_team_civ(0, CIV_MACEDONIANS);
	s.spawn_building_world(-50.0f, -50.0f, BLD_TOWN_CENTER, 0, 1.0f);
	s.spawn_building_world(50.0f, 50.0f, BLD_TOWN_CENTER, 1, 1.0f);
	s.set_player_active(1, true, false); // player 1's units follow the test's orders, not Petra's defense
}

// Damage a siege tower with `archers` archers inside deals in `secs` to a sturdy enemy.
static float tower_damage(int archers, float secs) {
	static Simulation s;
	fresh(s);
	const uint16_t st_t = template_id("mace/siege_tower");
	uint32_t tower = s.spawn_unit(0.0f, 0.0f, 0, UNIT_BOLT_SHOOTER, 0.0f, st_t);
	std::vector<uint32_t> ids;
	for (int a = 0; a < archers; ++a) ids.push_back(s.spawn_unit(1.0f, 1.0f, 0, UNIT_ARCHER));
	if (!ids.empty()) s.order_board_transport(ids.data(), ids.size(), tower);
	uint32_t target = s.spawn_unit(10.0f, 0.0f, 1, UNIT_SPEARMAN, 20000.0f);
	s.order_set_stance(&target, 1, STANCE_PASSIVE);
	// Count the arrows the tower looses: whether each one hits depends on 0 A.D.'s
	// projectile spread (DelayedDamage.js), so damage is too noisy a measure.
	std::vector<uint32_t> seen;
	for (int t = 0; t < (int)std::lround(secs / Simulation::FIXED_STEP); ++t) {
		s.tick(Simulation::FIXED_STEP);
		for (const auto &p : s.projectiles())
			if (p.source_id == tower && std::find(seen.begin(), seen.end(), p.id) == seen.end()) seen.push_back(p.id);
	}
	return (float)seen.size();
}

// A crowd of idle units round a ram at (0, 0): a grid 1 wu apart over 12 wu, so every spot the
// search scans is within a unit's collision radii of one of them, except a corridor kept open in
// front (+z) of the engine.
static std::vector<uint32_t> crowd_round_ram(Simulation &s) {
	std::vector<uint32_t> crowd;
	for (float x = -6.0f; x <= 6.0f; x += 1.0f)
		for (float z = -6.0f; z <= 6.0f; z += 1.0f) {
			if (std::fabs(x) < 1.6f && z > 0.5f) continue;
			crowd.push_back(s.spawn_unit(x, z, 0, UNIT_FEMALE_CITIZEN));
		}
	return crowd;
}

int main() {
	const uint16_t st_t = template_id("mace/siege_tower");
	CHECK(st_t != CIV_UNIT_TEMPLATE_NONE);

	static Simulation s;

	// 1. Who is a garrison holder, and its capacity.
	{
		fresh(s);
		uint32_t tower = s.spawn_unit(0.0f, 0.0f, 0, UNIT_BOLT_SHOOTER, 0.0f, st_t);
		uint32_t ram = s.spawn_unit(8.0f, 0.0f, 0, UNIT_SIEGE_RAM);
		uint32_t bolt = s.spawn_unit(16.0f, 0.0f, 0, UNIT_BOLT_SHOOTER);
		CHECK(s.is_transport(tower) && s.get_transport_capacity(tower) == SIEGE_TOWER_GARRISON);
		CHECK(s.is_transport(ram) && s.get_transport_capacity(ram) == SIEGE_RAM_GARRISON);
		CHECK(!s.is_transport(bolt));
		// Per-civ GarrisonHolder/Max: Kushite rams hold 12, the Germanic log ram nobody.
		uint32_t kram = s.spawn_unit(24.0f, 0.0f, 0, UNIT_SIEGE_RAM, 0.0f, template_id("kush/siege_ram"));
		uint32_t gram = s.spawn_unit(32.0f, 0.0f, 0, UNIT_SIEGE_RAM, 0.0f, template_id("germ/siege_ram"));
		uint32_t gcov = s.spawn_unit(40.0f, 0.0f, 0, UNIT_SIEGE_RAM, 0.0f, template_id("germ/siege_ram_covered"));
		CHECK(s.get_transport_capacity(kram) == 12 && !s.is_transport(gram) && s.get_transport_capacity(gcov) == 10);
	}

	// 2. Support and Infantry board; cavalry and siege don't; full is full.
	{
		fresh(s);
		uint32_t tower = s.spawn_unit(0.0f, 0.0f, 0, UNIT_BOLT_SHOOTER, 0.0f, st_t);
		std::vector<uint32_t> ids;
		for (int a = 0; a < 5; ++a) ids.push_back(s.spawn_unit(1.0f, 0.5f, 0, UNIT_ARCHER));
		for (int f = 0; f < 2; ++f) ids.push_back(s.spawn_unit(0.5f, 1.0f, 0, UNIT_FEMALE_CITIZEN));
		uint32_t cav = s.spawn_unit(1.0f, 1.0f, 0, UNIT_CAVALRY);
		uint32_t ram = s.spawn_unit(1.5f, 1.0f, 0, UNIT_SIEGE_RAM);
		ids.push_back(cav);
		ids.push_back(ram);
		s.order_board_transport(ids.data(), ids.size(), tower);
		CHECK(s.get_transport_passenger_count(tower) == 7);
		CHECK(alive(s, cav) && alive(s, ram));
		CHECK(s.siege_tower_arrows((size_t)idx_of(s, tower)) == 5); // female citizens fire no arrows
		// A unit walking up boards on arrival.
		uint32_t late = s.spawn_unit(-12.0f, 0.0f, 0, UNIT_SPEARMAN);
		s.order_board_transport(&late, 1, tower);
		run(s, 6.0f);
		CHECK(!alive(s, late) && s.get_transport_passenger_count(tower) == 8);
		// The ram's ten seats fill and the eleventh soldier stays out.
		uint32_t ram2 = s.spawn_unit(20.0f, 20.0f, 0, UNIT_SIEGE_RAM);
		std::vector<uint32_t> sw;
		for (int k = 0; k < 11; ++k) sw.push_back(s.spawn_unit(21.0f, 20.5f, 0, UNIT_SWORDSMAN));
		s.order_board_transport(sw.data(), sw.size(), ram2);
		CHECK(s.get_transport_passenger_count(ram2) == 10);
		CHECK(alive(s, sw[10]));
		// The List classes are the template's own: a cavalry hero (Alexander) is no Infantry and
		// stays out of a siege tower; an infantry hero (Leonidas) gets in.
		const uint16_t alex = template_id("mace/hero_alexander_iii"), leo = template_id("spart/hero_leonidas");
		CHECK(alex != CIV_UNIT_TEMPLATE_NONE && leo != CIV_UNIT_TEMPLATE_NONE);
		uint32_t t2 = s.spawn_unit(-20.0f, 20.0f, 0, UNIT_BOLT_SHOOTER, 0.0f, st_t);
		uint32_t hc = s.spawn_unit(-19.0f, 20.5f, 0, UNIT_HERO, 0.0f, alex);
		uint32_t hi = s.spawn_unit(-19.0f, 19.5f, 0, UNIT_HERO, 0.0f, leo);
		s.order_board_transport(&hc, 1, t2);
		s.order_board_transport(&hi, 1, t2);
		CHECK(s.get_transport_passenger_count(t2) == 1 && alive(s, hc) && !alive(s, hi));
	}

	// 3. Arrows: none when empty, one per garrisoned Infantry.
	{
		const float d0 = tower_damage(0, 12.0f);
		const float d1 = tower_damage(1, 12.0f);
		const float d5 = tower_damage(5, 12.0f);
		std::printf("siege tower arrows in 12 s: empty %.0f, 1 archer %.0f, 5 archers %.0f\n", d0, d1, d5);
		CHECK(d0 == 0.0f);
		CHECK(d1 > 0.0f);
		CHECK(d5 > d1 * 4.0f && d5 < d1 * 6.0f);
	}

	// 4. Unload below 10% health, unharmed and next to the tower.
	{
		fresh(s);
		uint32_t tower = s.spawn_unit(0.0f, 0.0f, 0, UNIT_BOLT_SHOOTER, 0.0f, st_t);
		std::vector<uint32_t> ids;
		for (int a = 0; a < 4; ++a) ids.push_back(s.spawn_unit(1.0f, 0.5f, 0, UNIT_SPEARMAN));
		s.order_board_transport(ids.data(), ids.size(), tower);
		const float full_hp = hp_of(s, ids[0]);
		run(s, 1.0f);
		CHECK(s.get_transport_passenger_count(tower) == 4);
		int32_t ti = idx_of(s, tower);
		const_cast<float *>(s.units().hp)[ti] = s.units().max_hp[ti] * 0.05f;
		run(s, 0.1f);
		CHECK(s.get_transport_passenger_count(tower) == 0);
		for (uint32_t id : ids) CHECK(alive(s, id) && hp_of(s, id) == full_hp);
	}

	// 5. Unload when destroyed.
	{
		fresh(s);
		uint32_t ram = s.spawn_unit(0.0f, 0.0f, 0, UNIT_SIEGE_RAM);
		std::vector<uint32_t> ids;
		for (int a = 0; a < 3; ++a) ids.push_back(s.spawn_unit(0.5f, 0.5f, 0, UNIT_SWORDSMAN));
		s.order_board_transport(ids.data(), ids.size(), ram);
		CHECK(s.get_transport_passenger_count(ram) == 3);
		// Nearly wrecked, then finished off by enemy soldiers.
		const_cast<float *>(s.units().hp)[idx_of(s, ram)] = 2.0f;
		std::vector<uint32_t> foes;
		for (int k = 0; k < 3; ++k) foes.push_back(s.spawn_unit(1.5f + k, 1.5f, 1, UNIT_SWORDSMAN));
		s.order_attack(foes.data(), foes.size(), ram);
		run(s, 10.0f);
		CHECK(!alive(s, ram));
		int out = 0;
		for (uint32_t id : ids) {
			int32_t i = idx_of(s, id);
			if (i >= 0 && s.units().action_state[i] != ACTION_GARRISONED) ++out;
		}
		CHECK(out == 3); // unloaded, not stuck inside (they may have died fighting since)
		CHECK(s.get_transport_passenger_count(ram) == 0);
	}

	// 6. Save/load keeps the passengers and the hash covers them.
	{
		fresh(s);
		uint32_t tower = s.spawn_unit(0.0f, 0.0f, 0, UNIT_BOLT_SHOOTER, 0.0f, st_t);
		std::vector<uint32_t> ids;
		for (int a = 0; a < 3; ++a) ids.push_back(s.spawn_unit(1.0f, 0.5f, 0, UNIT_ARCHER));
		TurnManager tm;
		const uint32_t h0 = tm.compute_sim_hash(s);
		s.order_board_transport(ids.data(), ids.size(), tower);
		CHECK(tm.compute_sim_hash(s) != h0);
		const char *path = "/tmp/rts0_test_siege_garrison.sav";
		CHECK(s.save_state(path));
		static Simulation l;
		fresh(l);
		CHECK(l.load_state(path));
		CHECK(l.get_transport_passenger_count(tower) == 3);
		CHECK(tm.compute_sim_hash(s) == tm.compute_sim_hash(l));
		l.order_disembark_all(tower);
		CHECK(l.get_transport_passenger_count(tower) == 0 && alive(l, ids[0]));
	}

	// 7. Military Engineers (siege_pack_unpack.json): City phase, Arsenal, 800 food,
	// Siege Engines -40% pack and unpack time; not for civs whose arsenal lacks it.
	{
		const TechInfo &ti = tech_info(TECH_SIEGE_PACK_UNPACK);
		CHECK(ti.food_cost == 800 && ti.research_time == 40.0f && ti.required_phase == 3 && ti.researcher == BLD_ARSENAL);
		CHECK((ti.civ_mask >> CIV_MACEDONIANS) & 1);
		CHECK(!((ti.civ_mask >> CIV_BRITONS) & 1) && !((ti.civ_mask >> CIV_PERSIANS) & 1));
		fresh(s);
		uint32_t cat = s.spawn_unit(0.0f, 0.0f, 0, UNIT_CATAPULT);
		uint32_t foe_cat = s.spawn_unit(30.0f, 30.0f, 1, UNIT_CATAPULT);
		const float before = s.pack_time_s((size_t)idx_of(s, cat));
		s.player(0).phase = 3;
		s.set_player_resources(0, 5000, 5000, 5000, 5000);
		uint32_t arsenal = s.spawn_building_world(-30.0f, -30.0f, BLD_ARSENAL, 0, 1.0f); // in the civic centre's territory
		CHECK(s.cmd_research(0, arsenal, TECH_SIEGE_PACK_UNPACK));
		run(s, ti.research_time + 1.0f);
		CHECK(s.player(0).has_tech(TECH_SIEGE_PACK_UNPACK));
		const float after = s.pack_time_s((size_t)idx_of(s, cat));
		std::printf("catapult pack time: %.1f s, with Military Engineers %.1f s\n", before, after);
		CHECK(std::fabs(after - before * 0.6f) < 1e-4f);
		CHECK(s.pack_time_s((size_t)idx_of(s, foe_cat)) == before); // only the researcher's siege
		// Unpacking now finishes in 3 s instead of 5.
		const uint32_t cats[1] = {cat};
		s.order_pack(cats, 1, false);
		run(s, after - 0.5f);
		CHECK(s.units().pack_state[idx_of(s, cat)] != PACK_UNPACKED);
		run(s, 1.0f);
		CHECK(s.units().pack_state[idx_of(s, cat)] == PACK_UNPACKED);
	}

	// 8. Siege passengers set down on their own free spots (0 A.D. Footprint PickSpawnPoint round the
	// engine): each on a spot of its own, clear of the engine and of the others, outside the engine's
	// footprint box. A ram is 8 m by 12 m, 2 by 3 world units, here with its front toward +z.
	{
		fresh(s);
		uint32_t ram = s.spawn_unit(0.0f, 0.0f, 0, UNIT_SIEGE_RAM);
		std::vector<uint32_t> ids;
		for (int a = 0; a < 6; ++a) ids.push_back(s.spawn_unit(0.5f, 0.5f, 0, UNIT_SWORDSMAN));
		s.order_board_transport(ids.data(), ids.size(), ram);
		CHECK(s.get_transport_passenger_count(ram) == 6);
		s.order_disembark_all(ram);
		CHECK(s.get_transport_passenger_count(ram) == 0);
		const int32_t ri = idx_of(s, ram);
		const float rr = unit_collision_radius(UNIT_SIEGE_RAM), ru = unit_collision_radius(UNIT_SWORDSMAN);
		const float rx = s.units().pos_x[ri], rz = s.units().pos_z[ri];
		int landed = 0, outside = 0;
		float worst = 1e9f; // smallest margin over the radii sum, to the engine and to each other unit
		std::vector<int32_t> at;
		for (uint32_t id : ids) {
			int32_t i = idx_of(s, id);
			if (i < 0 || !s.units().alive[i]) continue;
			++landed;
			at.push_back(i);
			const float dx = s.units().pos_x[i] - rx, dz = s.units().pos_z[i] - rz;
			if (!(std::fabs(dx) < 1.0f && std::fabs(dz) < 1.5f)) ++outside; // half width 1 wu, half depth 1.5 wu
			worst = std::min(worst, std::hypot(dx, dz) - (rr + ru));
		}
		for (size_t a = 0; a < at.size(); ++a)
			for (size_t b = a + 1; b < at.size(); ++b) {
				const float dx = s.units().pos_x[at[a]] - s.units().pos_x[at[b]];
				const float dz = s.units().pos_z[at[a]] - s.units().pos_z[at[b]];
				worst = std::min(worst, std::hypot(dx, dz) - 2.0f * ru);
			}
		std::printf("siege ram unload: %d of 6 landed on their own spots, %d outside the footprint, smallest clearance %.4f wu\n",
			landed, outside, worst);
		CHECK(landed == 6);
		CHECK(outside == landed);
		CHECK(worst >= -1e-4f);
	}

	// 8b. A target to the engine's side (within 6 m) turns the front toward it: the first passenger
	// lands on that side, past the tower's 1.5 wu half width.
	{
		fresh(s);
		uint32_t tower = s.spawn_unit(0.0f, 0.0f, 0, UNIT_BOLT_SHOOTER, 0.0f, st_t);
		std::vector<uint32_t> ids;
		for (int a = 0; a < 4; ++a) ids.push_back(s.spawn_unit(0.5f, 0.5f, 0, UNIT_SWORDSMAN));
		s.order_board_transport(ids.data(), ids.size(), tower);
		CHECK(s.get_transport_passenger_count(tower) == 4);
		s.order_disembark(tower, 1.0f, 0.0f);
		CHECK(s.get_transport_passenger_count(tower) == 0);
		int landed = 0;
		for (uint32_t id : ids) landed += alive(s, id) ? 1 : 0;
		const int32_t first = idx_of(s, ids[0]);
		const float fx = s.units().pos_x[first], fz = s.units().pos_z[first];
		std::printf("siege tower unload toward +x: %d of 4 landed, the first at (%.3f, %.3f) wu from the tower's centre\n",
			landed, fx, fz);
		CHECK(landed == 4);
		CHECK(fx > 1.5f);
	}

	// 9. A crowd round the engine leaves few spots free. A normal unload leaves the passengers with
	// none aboard; below EjectHealth (EjectOrKill, not forced) those without a spot die.
	{
		fresh(s);
		uint32_t ram = s.spawn_unit(0.0f, 0.0f, 0, UNIT_SIEGE_RAM);
		std::vector<uint32_t> ids;
		for (int a = 0; a < 5; ++a) ids.push_back(s.spawn_unit(0.5f, 0.5f, 0, UNIT_SWORDSMAN));
		s.order_board_transport(ids.data(), ids.size(), ram);
		CHECK(s.get_transport_passenger_count(ram) == 5);
		crowd_round_ram(s);
		s.order_disembark_all(ram);
		int landed = 0;
		for (uint32_t id : ids) landed += alive(s, id) ? 1 : 0;
		const size_t aboard = s.get_transport_passenger_count(ram);
		std::printf("siege ram crowded unload: %d of 5 landed, %zu stay aboard\n", landed, aboard);
		CHECK(landed >= 1 && landed < 5);
		CHECK(aboard == (size_t)(5 - landed));
	}
	{
		fresh(s);
		uint32_t ram = s.spawn_unit(0.0f, 0.0f, 0, UNIT_SIEGE_RAM);
		std::vector<uint32_t> ids;
		for (int a = 0; a < 5; ++a) ids.push_back(s.spawn_unit(0.5f, 0.5f, 0, UNIT_SWORDSMAN));
		s.order_board_transport(ids.data(), ids.size(), ram);
		crowd_round_ram(s);
		const int32_t ri = idx_of(s, ram);
		const_cast<float *>(s.units().hp)[ri] = s.units().max_hp[ri] * 0.05f;
		s.tick(Simulation::FIXED_STEP);
		CHECK(s.get_transport_passenger_count(ram) == 0);
		int out = 0, at_centre = 0, on_spot = 0, outside = 0;
		for (uint32_t id : ids) {
			int32_t i = idx_of(s, id);
			if (i < 0 || !s.units().alive[i] || s.units().action_state[i] == ACTION_GARRISONED) continue;
			++out;
			const float dx = s.units().pos_x[i] - s.units().pos_x[ri], dz = s.units().pos_z[i] - s.units().pos_z[ri];
			if (std::hypot(dx, dz) < 1e-4f) ++at_centre;
			else {
				++on_spot;
				if (!(std::fabs(dx) < 1.0f && std::fabs(dz) < 1.5f)) ++outside;
			}
		}
		int dead = 0;
		for (uint32_t id : ids) dead += alive(s, id) ? 0 : 1;
		std::printf("siege ram EjectOrKill in a crowd: %d out on spots, %d at the engine centre, %d killed\n",
			on_spot, at_centre, dead);
		CHECK(on_spot >= 1 && at_centre == 0 && dead >= 1 && out + dead == 5);
		CHECK(outside == on_spot);
	}


	// GarrisonHolder.IsAllowedToBeGarrisoned: a mutual ally's infantry ride your siege tower;
	// an ally unloads only its own, the owner everyone; allies turned enemies get off.
	{
		fresh(s);
		uint32_t tower = s.spawn_unit(0.0f, 0.0f, 0, UNIT_BOLT_SHOOTER, 0.0f, st_t);
		uint32_t mine = s.spawn_unit(1.0f, 0.5f, 0, UNIT_ARCHER);
		std::vector<uint32_t> theirs;
		for (int a = 0; a < 3; ++a) theirs.push_back(s.spawn_unit(0.5f + 0.3f * a, 1.0f, 1, UNIT_ARCHER));
		s.order_board_transport(theirs.data(), theirs.size(), tower);
		CHECK(s.get_transport_passenger_count(tower) == 0); // not allied: refused
		s.set_mutual_ally(0, 1);
		s.order_board_transport(theirs.data(), theirs.size(), tower);
		s.order_board_transport(&mine, 1, tower);
		CHECK(s.get_transport_passenger_count(tower) == 4);
		s.cmd_unload(1, tower, nullptr, 0);
		CHECK(s.get_transport_passenger_count(tower) == 1 && !alive(s, mine));
		for (uint32_t u : theirs) CHECK(alive(s, u));
		// Set down around the tower, they walk back to within its 2 m LoadingRange.
		s.order_board_transport(theirs.data(), theirs.size(), tower);
		run(s, 3.0f);
		CHECK(s.get_transport_passenger_count(tower) == 4);
		s.cmd_unload(0, tower, nullptr, 0);
		CHECK(s.get_transport_passenger_count(tower) == 0);
		s.order_board_transport(theirs.data(), theirs.size(), tower);
		run(s, 3.0f);
		CHECK(s.get_transport_passenger_count(tower) == 3);
		s.set_diplomacy(0, 1, Simulation::DIPLO_ENEMY);
		s.set_diplomacy(1, 0, Simulation::DIPLO_ENEMY);
		run(s, 0.2f);
		CHECK(s.get_transport_passenger_count(tower) == 0);
		int out = 0;
		for (uint32_t u : theirs) out += alive(s, u);
		std::printf("allied passengers: an enemy now, %d of 3 got off alive\n", out);
		CHECK(out == 3);
	}


	// Garrisonable.TotalSize: a ram carrying 10 takes 11 of a fortress's 20 slots.
	{
		fresh(s);
		uint32_t fort = s.spawn_building_world(-20.0f, 20.0f, BLD_FORTRESS, 0, 1.0f);
		uint32_t rams[2];
		for (int r = 0; r < 2; ++r) {
			rams[r] = s.spawn_unit(-20.0f + 4.0f * r, 30.0f, 0, UNIT_SIEGE_RAM);
			std::vector<uint32_t> crew;
			for (int k = 0; k < 10; ++k) crew.push_back(s.spawn_unit(-20.0f + 4.0f * r + 0.2f * k, 31.0f, 0, UNIT_SPEARMAN));
			s.order_board_transport(crew.data(), crew.size(), rams[r]);
			CHECK(s.get_transport_passenger_count(rams[r]) == 10);
		}
		CHECK(s.garrison_unit_now(rams[0], fort));
		CHECK(!s.garrison_unit_now(rams[1], fort)); // 11 + 11 > 20
		uint32_t spare = s.spawn_unit(-24.0f, 30.0f, 0, UNIT_SIEGE_RAM);
		CHECK(s.garrison_unit_now(spare, fort)); // 11 + 1
	}

	std::printf(failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", failures);
	return failures ? 1 : 0;
}
