// Civ roster wiring tests: spawned and trained units carry their civ's 0 A.D.
// template, heroes train with their auras, civ bonuses come from data, and the
// template survives a save/load round trip.
//   g++ -std=c++20 -O2 -Isrc tools/test_civ_units.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp -lpthread -o /tmp/test_civ_units \
//       && /tmp/test_civ_units
#include "simulation/simulation.h"
#include <cstdio>
#include <cstring>

using namespace rtso;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static int32_t index_of(const Simulation &s, uint32_t uid) {
	auto v = s.units();
	for (size_t i = 0; i < v.count; ++i)
		if (v.id[i] == uid) return (int32_t)i;
	return -1;
}

static uint16_t find_template(uint8_t civ, const char *name) {
	for (uint16_t i = civ_roster_begin(civ); i < civ_roster_end(civ); ++i)
		if (std::strcmp(civ_unit_template(i).name, name) == 0) return i;
	return CIV_UNIT_TEMPLATE_NONE;
}

int main() {
	static Simulation s;
	s.set_sim_seed(5);
	s.set_team_civ(0, CIV_ATHENIANS);
	s.set_team_civ(1, CIV_HAN);
	s.reset();

	// Spawning a class gives the owner's civ unit with 0 A.D. stats.
	uint32_t hop = s.spawn_unit(0.0f, 0.0f, 0, UNIT_SPEARMAN);
	int32_t hi = index_of(s, hop);
	CHECK(hi >= 0);
	uint16_t ht = s.unit_template(hop);
	CHECK(ht != CIV_UNIT_TEMPLATE_NONE);
	CHECK(std::strcmp(civ_unit_template(ht).name, "Athenian Hoplite") == 0);
	CHECK(s.units().max_hp[hi] == civ_unit_template(ht).hp);

	uint32_t han = s.spawn_unit(4.0f, 0.0f, 1, UNIT_SPEARMAN);
	CHECK(civ_unit_template(s.unit_template(han)).civ == CIV_HAN);

	// An explicit hp still overrides the template (start-of-match armies).
	uint32_t tough = s.spawn_unit(0.0f, 4.0f, 0, UNIT_SWORDSMAN, 160.0f);
	CHECK(s.units().max_hp[index_of(s, tough)] == 160.0f);

	// Civ unit bonuses are in the data: Spartan women have 0 A.D.'s +40% health.
	s.set_team_civ(2, CIV_SPARTANS);
	uint32_t sw = s.spawn_unit(8.0f, 0.0f, 2, UNIT_FEMALE_CITIZEN);
	uint32_t aw = s.spawn_unit(8.0f, 4.0f, 0, UNIT_FEMALE_CITIZEN);
	CHECK(s.units().max_hp[index_of(s, sw)] > s.units().max_hp[index_of(s, aw)] * 1.3f);

	// Britons' Woad Warriors: infantry +5% speed over the same template elsewhere.
	s.set_team_civ(3, CIV_BRITONS);
	s.set_team_civ(4, CIV_GAULS);
	uint32_t bsp = s.spawn_unit(12.0f, 0.0f, 3, UNIT_SPEARMAN);
	uint32_t gsp = s.spawn_unit(12.0f, 4.0f, 4, UNIT_SPEARMAN);
	CHECK(civ_unit_template(s.unit_template(bsp)).speed > civ_unit_template(s.unit_template(gsp)).speed * 1.04f);

	// Training a hero by template: Pericles, with his own aura. A fresh match, so
	// the extra players above don't end it early.
	static Simulation m;
	m.set_sim_seed(6);
	m.set_team_civ(0, CIV_ATHENIANS);
	m.reset();
	uint16_t peri = find_template(CIV_ATHENIANS, "Pericles");
	CHECK(peri != CIV_UNIT_TEMPLATE_NONE);
	m.set_player_resources(0, 5000, 5000, 5000, 5000);
	uint32_t tc = m.spawn_building(-20, -20, BLD_TOWN_CENTER, 0, 1.0f);
	for (int i = 0; i < 4; ++i) m.spawn_building(-30 + i * 3, -30, BLD_HOUSE, 0, 1.0f);
	CHECK(m.cmd_train_template(tc, peri));
	CHECK(!m.cmd_train_template(tc, find_template(CIV_HAN, "Infantry Crossbowman"))); // other civ's unit
	const float food_before = m.player(0).food();
	CHECK(m.cmd_train(tc, UNIT_FEMALE_CITIZEN));
	CHECK(m.player(0).food() < food_before);
	// Pericles trains in 0 A.D.'s 50 s; look for him as soon as he is out.
	bool got_pericles = false;
	for (int i = 0; i < 20 * 70 && !got_pericles; ++i) {
		m.tick(Simulation::FIXED_STEP);
		auto v = m.units();
		for (size_t u = 0; u < v.count; ++u)
			if (v.team[u] == 0 && v.tmpl[u] == peri) got_pericles = true;
	}
	CHECK(got_pericles);

	// Classes a civ lacks are refused (Athenians field no elephants).
	CHECK(!m.cmd_train(tc, UNIT_ELEPHANT));

	// Building health takes 0 A.D.'s civ structure bonus (Hellenic Architecture +10%).
	uint32_t ath_house = s.spawn_building(10, 10, BLD_HOUSE, 0, 1.0f);
	s.set_team_civ(5, CIV_PERSIANS);
	uint32_t per_house = s.spawn_building(14, 10, BLD_HOUSE, 5, 1.0f);
	float ath_hp = 0, per_hp = 0;
	for (const auto &b : s.buildings()) {
		if (b.id == ath_house) ath_hp = b.max_hp;
		if (b.id == per_house) per_hp = b.max_hp;
	}
	CHECK(ath_hp > per_hp * 1.09f && ath_hp < per_hp * 1.11f);

	// Templates survive save/load.
	const char *path = "/tmp/rtso_test_civ_units.sav";
	CHECK(s.save_state(path));
	uint16_t han_tmpl = s.unit_template(han);
	static Simulation r;
	CHECK(r.load_state(path));
	CHECK(r.unit_template(hop) == ht);
	CHECK(r.unit_template(han) == han_tmpl);
	std::remove(path);

	if (failures == 0) std::printf("ALL PASS\n");
	return failures == 0 ? 0 : 1;
}
