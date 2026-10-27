// Gathering details checked against 0 A.D. (A27 simulation/components):
//   - Player.js AddResource has no cap: a player past 9999 keeps what it drops off.
//   - UnitAI REPAIR has no ReturnResource: a builder with a full load keeps building, and
//     ConstructionFinished drops the load at the finished structure when it takes it.
//   - Athenian Silver Owls (phase_town_athen / phase_city_athen): Workers metal.ore x1.1 at
//     Town and again at City; stone untouched.
//   - Loot.js GetResources / GetXp floor the modified amounts.
//   - ResourceSupply Change timers run per supply: a fruit bush's Regrowth ticks 6 s after its
//     timer started, not on a map-wide clock.
//   - ResourceGatherer.StartGathering fixes the gather rate: a field's diminishing returns are
//     those when the farmer started, until she stops gathering, and its timer starts afresh.
//   - Han rice paddies take 3 farmers; the Ptolemaic team food trickle reaches allies without buildings.
//   g++ -std=c++20 -O2 -Isrc tools/test_gather_parity.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//       src/network/net_host.cpp -lpthread -o /tmp/test_gather_parity && /tmp/test_gather_parity
#define private public
#include "simulation/simulation.h"
#include "simulation/nav/footprint.h"
#undef private
#include <cmath>
#include <cstdio>
#include <vector>

using namespace rtso;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static void run(Simulation &s, float seconds) {
	for (int k = 0; k < (int)(seconds / Simulation::FIXED_STEP); ++k) s.tick(Simulation::FIXED_STEP);
}

static void fresh(Simulation &s) {
	s.set_sim_seed(3);
	s.reset();
	for (uint8_t p = 0; p < 2; ++p) s.set_player_active(p, true, false);
	s.set_player_team(0, 0);
	s.set_player_team(1, 1);
}

// Seconds a woman standing at a full mine takes to fill her basket.
static float fill_time(uint8_t civ, uint8_t phase, uint8_t res) {
	static Simulation s;
	fresh(s);
	s.player(0).civ = (Civilization)civ;
	s.player(0).phase = phase;
	const uint32_t mine = s.spawn_resource_world(0.0f, 0.0f, res, 5000);
	const uint32_t woman = s.spawn_unit(1.2f, 0.0f, 0, UNIT_FEMALE_CITIZEN);
	s.order_gather(&woman, 1, mine);
	const int32_t w = s.lookup_unit(woman);
	const int32_t cap = s.carry_cap((size_t)w, res);
	int ticks = 0;
	while (s.units_.carry[w] < cap && ticks < 20000) { s.tick(Simulation::FIXED_STEP); ++ticks; }
	return ticks * Simulation::FIXED_STEP;
}

int main() {
	// 1. No resource cap for any player.
	{
		static Simulation s;
		fresh(s);
		s.player(1).res[RES_WOOD] = 9995.0f;
		const uint32_t store = s.spawn_building_world(20.0f, 0.0f, BLD_STOREHOUSE, 1, 1.0f);
		const uint32_t woman = s.spawn_unit(24.0f, 0.0f, 1, UNIT_FEMALE_CITIZEN);
		const int32_t w = s.lookup_unit(woman);
		s.units_.res_type_carried[w] = RES_WOOD;
		s.units_.carry[w] = 10;
		s.order_return_resource(&woman, 1, store);
		run(s, 6.0f);
		std::printf("  no cap: player 1 wood %d after dropping 10 at 9995\n", s.player_resource(1, RES_WOOD));
		CHECK(s.player_resource(1, RES_WOOD) == 10005);
	}

	// 2. A builder carrying a full load keeps it while she builds; the finished storehouse takes it.
	{
		static Simulation s;
		fresh(s);
		s.spawn_building_world(-40.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
		const uint32_t site = s.spawn_building_world(20.0f, 0.0f, BLD_STOREHOUSE, 0, 0.0f);
		const uint32_t woman = s.spawn_unit(24.0f, 0.0f, 0, UNIT_FEMALE_CITIZEN);
		const int32_t w = s.lookup_unit(woman);
		s.units_.res_type_carried[w] = RES_WOOD;
		s.units_.carry[w] = s.carry_cap((size_t)w, RES_WOOD);
		const int wood0 = s.player_resource(0, RES_WOOD);
		s.order_build_or_repair(&woman, 1, site);
		run(s, 5.0f);
		const int32_t w2 = s.lookup_unit(woman);
		std::printf("  builder: state %d, carrying %d, x %.1f\n", s.units_.action_state[w2], s.units_.carry[w2], s.units_.pos_x[w2]);
		CHECK(s.units_.action_state[w2] == ACTION_BUILD && s.units_.carry[w2] == s.carry_cap((size_t)w2, RES_WOOD));
		bool done = false;
		for (int k = 0; k < 600 && !done; ++k) {
			run(s, 1.0f);
			for (const auto &b : s.buildings()) if (b.id == site) done = b.build_progress >= 1.0f;
		}
		run(s, 5.0f);
		std::printf("  builder: storehouse finished, wood +%d\n", s.player_resource(0, RES_WOOD) - wood0);
		CHECK(done && s.player_resource(0, RES_WOOD) == wood0 + s.carry_cap((size_t)s.lookup_unit(woman), RES_WOOD));
	}

	// 3. Silver Owls: Athenian women mine metal 10% faster in Town and 21% faster in City,
	// stone at the same rate in every phase; other civs get nothing.
	{
		const float m1 = fill_time(CIV_ATHENIANS, 1, RES_GOLD), m2 = fill_time(CIV_ATHENIANS, 2, RES_GOLD),
				m3 = fill_time(CIV_ATHENIANS, 3, RES_GOLD);
		const float s1 = fill_time(CIV_ATHENIANS, 1, RES_STONE), s3 = fill_time(CIV_ATHENIANS, 3, RES_STONE);
		const float sp1 = fill_time(CIV_SPARTANS, 1, RES_GOLD), sp3 = fill_time(CIV_SPARTANS, 3, RES_GOLD);
		std::printf("  Silver Owls: metal fill %.2f / %.2f / %.2f s by phase, stone %.2f / %.2f s, Spartan metal %.2f / %.2f s\n",
				m1, m2, m3, s1, s3, sp1, sp3);
		CHECK(std::fabs(m1 / m2 - 1.1f) < 0.03f && std::fabs(m1 / m3 - 1.21f) < 0.04f);
		CHECK(std::fabs(s1 - s3) < 0.06f && std::fabs(sp1 - sp3) < 0.06f && std::fabs(sp1 - m1) < 0.06f);
	}

	// 3b. Carthaginian civbonuses cart_stone_01..03: Workers stone.rock x1.25 from the start and
	// again at Town and City (Carthage researches no servants, serfs or slaves); metal untouched.
	{
		const float a1 = fill_time(CIV_ATHENIANS, 1, RES_STONE);
		const float c1 = fill_time(CIV_CARTHAGINIANS, 1, RES_STONE), c2 = fill_time(CIV_CARTHAGINIANS, 2, RES_STONE),
				c3 = fill_time(CIV_CARTHAGINIANS, 3, RES_STONE), cm = fill_time(CIV_CARTHAGINIANS, 3, RES_GOLD);
		const float sm = fill_time(CIV_SPARTANS, 3, RES_GOLD);
		std::printf("  Carthaginian stone: fill %.2f / %.2f / %.2f s by phase (Athenian %.2f), metal %.2f (Spartan %.2f)\n",
				c1, c2, c3, a1, cm, sm);
		CHECK(std::fabs(a1 / c1 - 1.25f) < 0.04f && std::fabs(a1 / c2 - 1.5625f) < 0.06f && std::fabs(a1 / c3 - 1.953f) < 0.08f);
		CHECK(std::fabs(cm - sm) < 0.06f);
		// The selection details show the same rates: a City Carthaginian woman's stone is the
		// Athenian's x1.25^3, an Athenian City woman's metal x1.21.
		static Simulation s;
		fresh(s);
		s.set_team_civ(0, CIV_CARTHAGINIANS);
		s.set_team_civ(1, CIV_ATHENIANS);
		s.player(0).phase = 3;
		s.player(1).phase = 3;
		const uint32_t cw = s.spawn_unit(0.0f, 0.0f, 0, UNIT_FEMALE_CITIZEN), aw = s.spawn_unit(4.0f, 0.0f, 1, UNIT_FEMALE_CITIZEN);
		const auto cd = s.unit_details((size_t)s.lookup_unit(cw)), ad = s.unit_details((size_t)s.lookup_unit(aw));
		std::printf("  panel: Carthaginian stone %.3f, Athenian stone %.3f, metal %.3f / %.3f\n", cd.gather[Simulation::GK_STONE],
				ad.gather[Simulation::GK_STONE], cd.gather[Simulation::GK_METAL], ad.gather[Simulation::GK_METAL]);
		CHECK(std::fabs(cd.gather[Simulation::GK_STONE] / ad.gather[Simulation::GK_STONE] - 1.953125f) < 1e-3f);
		CHECK(std::fabs(ad.gather[Simulation::GK_METAL] / cd.gather[Simulation::GK_METAL] - 1.21f) < 1e-3f);
		// GetEntityState's resourceGatherRates are the cached rates, BaseSpeed modifiers in:
		// an elite spearman's wood is the basic one's x0.7 x0.7 (unit_advanced/unit_elite.json).
		const uint32_t sb = s.spawn_unit(0.0f, 8.0f, 1, UNIT_SPEARMAN), se = s.spawn_unit(4.0f, 8.0f, 1, UNIT_SPEARMAN);
		s.promote_unit((size_t)s.lookup_unit(se), s.units_.tmpl[s.lookup_unit(se)], 3);
		const auto bd = s.unit_details((size_t)s.lookup_unit(sb)), ed = s.unit_details((size_t)s.lookup_unit(se));
		std::printf("  panel: basic spearman wood %.3f, elite %.3f\n", bd.gather[Simulation::GK_WOOD], ed.gather[Simulation::GK_WOOD]);
		CHECK(bd.gather[Simulation::GK_WOOD] > 0.0f && std::fabs(ed.gather[Simulation::GK_WOOD] / bd.gather[Simulation::GK_WOOD] - 0.49f) < 1e-3f);
	}

	// 3c. germ_women's +0.5 m/s is added after the technologies' multiplies (GetTechModifiedProperty):
	// Roman Roads x1.05 on a German civilian scales her base speed, not the bonus.
	{
		static Simulation s;
		fresh(s);
		s.set_team_civ(0, CIV_GERMANS);
		const uint32_t before = s.spawn_unit(0.0f, 0.0f, 0, UNIT_FEMALE_CITIZEN);
		const float add = s.unit_civ_speed_add((size_t)s.lookup_unit(before));
		const float v0 = s.units_.speed[s.lookup_unit(before)];
		s.players_[0].techs.set(TECH_ROMAN_ROADS);
		s.refresh_tech_mods(0);
		const uint32_t after = s.spawn_unit(4.0f, 0.0f, 0, UNIT_FEMALE_CITIZEN);
		const float want = (v0 - add) * 1.05f + add;
		std::printf("  germ_women: add %.3f, speed %.4f -> %.4f / %.4f (want %.4f)\n", add, v0,
				s.units_.speed[s.lookup_unit(before)], s.units_.speed[s.lookup_unit(after)], want);
		CHECK(add > 0.0f && std::fabs(s.units_.speed[s.lookup_unit(before)] - want) < 1e-4f &&
				std::fabs(s.units_.speed[s.lookup_unit(after)] - want) < 1e-4f);
	}

	// 4. Loot is floored after the rank multiplier.
	{
		static Simulation s;
		fresh(s);
		const int food0 = s.player_resource(0, RES_FOOD), metal0 = s.player_resource(0, RES_GOLD);
		LootInfo loot{0, 5, 0, 0, 7};
		s.collect_loot(0, 0, loot, 1.5f, RES_COUNT, 0); // 7.5 -> 7, 10.5 -> 10
		std::printf("  loot: food +%d, metal +%d\n", s.player_resource(0, RES_FOOD) - food0, s.player_resource(0, RES_GOLD) - metal0);
		CHECK(s.player(0).res[RES_FOOD] == (float)food0 + 7.0f && s.player(0).res[RES_GOLD] == (float)metal0 + 10.0f);
	}

	// 5. A fruit bush's regrowth timer starts with the bush (ResourceSupply Init), 1 s into the
	// match here, so its ticks come at 7 s, 13 s, ... (not at 6 s, 12 s).
	{
		static Simulation s;
		fresh(s);
		run(s, 1.0f);
		const uint32_t bush = s.spawn_resource_world(30.0f, 30.0f, RES_FOOD, 200);
		auto amount = [&]() { return s.resources_.amount[s.res_id_to_idx_[bush]]; };
		run(s, 1.0f);
		s.resources_.amount[s.res_id_to_idx_[bush]] = 190;
		run(s, 4.5f); // 6.5 s
		const int at_6_5 = amount();
		run(s, 0.6f); // 7.1 s
		const int at_7_1 = amount();
		run(s, 6.0f); // 13.1 s
		std::printf("  fruit timer: %d at 6.5 s, %d at 7.1 s, %d at 13.1 s\n", at_6_5, at_7_1, amount());
		CHECK(at_6_5 == 190 && at_7_1 == 191 && amount() == 192);
	}

	// 6. A farmer alone on a field keeps her full rate until her load ends, though four more
	// join her (diminishing returns at StartGathering); her next load is slower.
	{
		static Simulation s;
		fresh(s);
		s.spawn_building_world(-55.0f, 30.0f, BLD_TOWN_CENTER, 0, 1.0f); // its territory keeps the farmstead from decaying
		s.spawn_building_world(-30.0f, 10.0f, BLD_FARMSTEAD, 0, 1.0f);
		const uint32_t field = s.spawn_building_world(-20.0f, 10.0f, BLD_FIELD, 0, 1.0f);
		const uint32_t first = s.spawn_unit(-16.0f, 10.0f, 0, UNIT_FEMALE_CITIZEN);
		run(s, 0.1f);
		s.order_build_or_repair(&first, 1, field);
		run(s, 2.0f);
		const int32_t f = s.lookup_unit(first);
		const float alone = s.units_.gather_rate[f];
		std::vector<uint32_t> more;
		for (int k = 0; k < 4; ++k) more.push_back(s.spawn_unit(-16.0f, 12.0f + k, 0, UNIT_FEMALE_CITIZEN));
		s.order_build_or_repair(more.data(), more.size(), field);
		run(s, 2.0f);
		const float later = s.units_.gather_rate[s.lookup_unit(first)];
		const float joined = s.units_.gather_rate[s.lookup_unit(more[0])];
		// Her next load, once she has been to the farmstead.
		float next = 0.0f;
		for (int k = 0; k < 2000 && !(next > 0.0f && next < alone); ++k) {
			run(s, 0.1f);
			next = s.units_.gather_rate[s.lookup_unit(first)];
		}
		std::printf("  field: alone %.3f/s, after four join %.3f/s (theirs %.3f/s), her next load %.3f/s\n",
				alone, later, joined, next);
		CHECK(alone > 0.0f && later == alone && joined < alone && std::fabs(next - joined) < 1e-4f);
	}

	// 7. Diminishing returns count the other farmers only (GetTargetGatherRate runs before
	// AddActiveGatherer): two farmers both work at the full rate, a third at 0.95.
	{
		static Simulation s;
		fresh(s);
		s.spawn_building_world(-55.0f, 30.0f, BLD_TOWN_CENTER, 0, 1.0f);
		s.spawn_building_world(-30.0f, 10.0f, BLD_FARMSTEAD, 0, 1.0f);
		const uint32_t field = s.spawn_building_world(-20.0f, 10.0f, BLD_FIELD, 0, 1.0f);
		std::vector<uint32_t> two = {s.spawn_unit(-16.0f, 10.0f, 0, UNIT_FEMALE_CITIZEN), s.spawn_unit(-16.0f, 11.0f, 0, UNIT_FEMALE_CITIZEN)};
		run(s, 0.1f);
		s.order_build_or_repair(two.data(), two.size(), field);
		run(s, 3.0f);
		const float r0 = s.units_.gather_rate[s.lookup_unit(two[0])], r1 = s.units_.gather_rate[s.lookup_unit(two[1])];
		const uint32_t third = s.spawn_unit(-16.0f, 12.0f, 0, UNIT_FEMALE_CITIZEN);
		s.order_build_or_repair(&third, 1, field);
		run(s, 3.0f);
		const float r2 = s.units_.gather_rate[s.lookup_unit(third)];
		std::printf("  field: two farmers %.3f/s and %.3f/s, a third %.3f/s\n", r0, r1, r2);
		CHECK(r0 > 0.0f && std::fabs(r1 - r0) < 1e-5f && std::fabs(r2 - r0 * 0.95f) < 1e-4f);
	}

	// 8. Wicker Baskets (+5 for Workers) reaches citizen infantry, whose template is a Worker.
	{
		static Simulation s;
		fresh(s);
		const uint32_t spear = s.spawn_unit(0.0f, 0.0f, 0, UNIT_SPEARMAN);
		const uint32_t woman = s.spawn_unit(2.0f, 0.0f, 0, UNIT_FEMALE_CITIZEN);
		const size_t u = (size_t)s.lookup_unit(spear), w = (size_t)s.lookup_unit(woman);
		const int32_t before = s.carry_cap(u, RES_WOOD), wbefore = s.carry_cap(w, RES_WOOD);
		s.player(0).techs.set(TECH_GATHER_CAPACITY_BASKET);
		s.rebuild_all_tech_mods();
		std::printf("  basket: infantry wood %d -> %d, woman %d -> %d (template %d)\n", before, s.carry_cap(u, RES_WOOD),
				wbefore, s.carry_cap(w, RES_WOOD), (int)s.units_.tmpl[u]);
		CHECK(s.units_.tmpl[u] < CIV_UNIT_TEMPLATE_COUNT && s.carry_cap(u, RES_WOOD) == before + 5 &&
		      s.carry_cap(w, RES_WOOD) == wbefore + 5);
	}

	// 9. MaxGatherers (0.28 ResourceSupply.AddGatherer on APPROACHING enter): a unit over a full
	// tree's cap is sent to another tree at once, not after walking to the full one.
	{
		static Simulation s;
		fresh(s);
		s.set_explore_map(true); // FindNearbyResource takes only trees the player has seen
		s.spawn_building_world(40.0f, 20.0f, BLD_TOWN_CENTER, 0, 1.0f);
		const uint32_t full = s.spawn_resource_world(0.0f, 0.0f, RES_WOOD, 200);
		const uint32_t other = s.spawn_resource_world(0.0f, 6.0f, RES_WOOD, 200);
		s.set_resource_max_gatherers(full, 2);
		uint32_t women[3];
		for (int k = 0; k < 3; ++k) women[k] = s.spawn_unit(30.0f, -1.0f + k, 0, UNIT_FEMALE_CITIZEN);
		s.order_gather(women, 3, full);
		run(s, 1.0f);
		int on_full = 0, on_other = 0;
		float other_x = 0.0f;
		for (uint32_t id : women) {
			const int32_t u = s.lookup_unit(id);
			if (s.units_.target_resource_id[u] == full) ++on_full;
			if (s.units_.target_resource_id[u] == other) { ++on_other; other_x = s.units_.pos_x[u]; }
		}
		std::printf("  max gatherers: %d on the full tree, %d sent on from x %.1f\n", on_full, on_other, other_x);
		CHECK(on_full == 2 && on_other == 1 && other_x > 20.0f);
	}

	// 9b. UnitAI FINDINGNEWTARGET with no free supply near: the turned-away worker walks back
	// toward where it was sent (GatherNearPosition) or to a dropsite, not idle where it stood.
	// 9c. The search is for the order's type, not the load: a worker carrying wood sent to a
	// full mine goes on to the free mine beside it.
	{
		static Simulation s;
		fresh(s);
		s.set_explore_map(true);
		s.spawn_building_world(40.0f, 20.0f, BLD_TOWN_CENTER, 0, 1.0f);
		const uint32_t mine = s.spawn_resource_world(0.0f, 0.0f, RES_STONE, 2000);
		s.set_resource_max_gatherers(mine, 1);
		uint32_t men[2];
		for (int k = 0; k < 2; ++k) men[k] = s.spawn_unit(30.0f, -1.0f + k, 0, UNIT_FEMALE_CITIZEN);
		s.order_gather(men, 2, mine);
		run(s, 1.0f);
		int idle_at_start = 0, sent_on = 0;
		for (uint32_t id : men) {
			const int32_t u = s.lookup_unit(id);
			if (s.units_.target_resource_id[u] == mine) continue;
			++sent_on;
			if (s.units_.action_state[u] == ACTION_IDLE && !s.units_.has_order[u]) ++idle_at_start;
		}
		std::printf("  full mine, nothing near: %d turned away, %d idle\n", sent_on, idle_at_start);
		CHECK(sent_on == 1 && idle_at_start == 0);

		static Simulation t;
		fresh(t);
		t.set_explore_map(true);
		t.spawn_building_world(40.0f, 20.0f, BLD_TOWN_CENTER, 0, 1.0f);
		const uint32_t full = t.spawn_resource_world(0.0f, 0.0f, RES_STONE, 2000);
		const uint32_t free_mine = t.spawn_resource_world(0.0f, 8.0f, RES_STONE, 2000);
		t.spawn_resource_world(26.0f, 0.0f, RES_WOOD, 200); // a tree beside the worker
		t.set_resource_max_gatherers(full, 1);
		uint32_t w[2];
		for (int k = 0; k < 2; ++k) w[k] = t.spawn_unit(30.0f, -1.0f + k, 0, UNIT_FEMALE_CITIZEN);
		const int32_t u1 = t.lookup_unit(w[1]);
		t.units_.carry[u1] = 5;
		t.units_.res_type_carried[u1] = RES_WOOD;
		t.order_gather(&w[0], 1, full);
		run(t, 0.5f);
		t.order_gather(&w[1], 1, full);
		run(t, 1.0f);
		const int32_t v = t.lookup_unit(w[1]);
		std::printf("  wood-laden worker turned from a full mine: target %u (free mine %u)\n",
				t.units_.target_resource_id[v], free_mine);
		CHECK(t.units_.target_resource_id[v] == free_mine);
	}

	// 10. 0.28 FindNearestDropsite measures to the footprint (DistanceToPoint): a civic centre
	// whose edge is nearer wins over a storehouse whose centre is nearer.
	{
		static Simulation s;
		fresh(s);
		const uint32_t cc = s.spawn_building_world(0.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
		const Simulation::Building *c = nullptr;
		for (const auto &b : s.buildings()) if (b.id == cc) c = &b;
		const float cc_edge = c->x + std::max(c->hu, c->hv);
		const float wx = cc_edge + 3.0f; // 3 from the civic centre's edge
		const uint32_t store = s.spawn_building_world(wx + (wx - c->x) - 0.5f, 0.0f, BLD_STOREHOUSE, 0, 1.0f);
		const uint32_t woman = s.spawn_unit(wx, 0.0f, 0, UNIT_FEMALE_CITIZEN);
		const int32_t w = s.lookup_unit(woman);
		const Simulation::Building *st = nullptr;
		for (const auto &b : s.buildings()) if (b.id == store) st = &b;
		for (const auto &b : s.buildings()) if (b.id == cc) c = &b;
		const nav::Vec2 gc = nav::footprint_gap(*c, wx, 0.0f), gs = nav::footprint_gap(*st, wx, 0.0f);
		const float dc = std::hypot(gc.x, gc.y), ds = std::hypot(gs.x, gs.y);
		const float cc_centre = std::fabs(wx - c->x), st_centre = std::fabs(st->x - wx);
		s.units_.res_type_carried[w] = RES_WOOD;
		s.units_.carry[w] = s.carry_cap((size_t)w, RES_WOOD);
		s.units_.action_state[w] = ACTION_RETURN_DROPSITE;
		s.tick(Simulation::FIXED_STEP);
		const int32_t w2 = s.lookup_unit(woman);
		std::printf("  dropsite: edge %.1f / %.1f, centre %.1f / %.1f (cc / storehouse); nearest %s, heading to x %.1f\n",
				dc, ds, cc_centre, st_centre, s.nearest_dropsite((size_t)w2) == cc ? "cc" : "storehouse", s.units_.target_x[w2]);
		CHECK(dc < ds && st_centre < cc_centre); // the setup
		CHECK(s.nearest_dropsite((size_t)w2) == cc && s.units_.target_x[w2] == c->x);
	}

	// 11. 0.28 ResourceGatherer has no owner check: player 0's woman farms player 1's field,
	// and a cavalryman (no grain rate) cannot.
	{
		static Simulation s;
		fresh(s);
		const uint32_t field = s.spawn_building_world(0.0f, 0.0f, BLD_FIELD, 1, 1.0f);
		s.spawn_building_world(0.0f, 8.0f, BLD_FARMSTEAD, 0, 1.0f);
		run(s, 0.5f);
		const uint32_t grain = s.field_resource_id(field);
		uint32_t w = s.spawn_unit(3.0f, 3.0f, 0, UNIT_FEMALE_CITIZEN);
		const uint32_t cav = s.spawn_unit(4.0f, 3.0f, 0, UNIT_CAVALRY);
		const bool woman_can = s.any_can_gather(&w, 1, grain), cav_can = s.any_can_gather(&cav, 1, grain);
		const float food0 = s.player(0).res[RES_FOOD];
		s.order_gather(&w, 1, grain);
		run(s, 40.0f);
		const int32_t wi = s.lookup_unit(w);
		const float got = s.player(0).res[RES_FOOD] - food0 + (wi >= 0 ? (float)s.units_.carry[wi] : 0.0f);
		std::printf("  other player's field: %.0f food farmed in 40 s (woman can %d, cavalry can %d)\n", got, woman_can, cav_can);
		CHECK(grain != 0 && woman_can && !cav_can && got > 0.0f);
	}

	// 12. structures/han/field.xml: a rice paddy takes 3 farmers (MaxGatherers 3), not the
	// generic field's 5; the other two are turned away.
	{
		static Simulation s;
		fresh(s);
		s.player(0).civ = CIV_HAN;
		const uint32_t field = s.spawn_building_world(0.0f, 0.0f, BLD_FIELD, 0, 1.0f);
		// The farmstead off the farmers' side: the two turned away walk there to idle (FINDINGNEWTARGET).
		s.spawn_building_world(0.0f, -16.0f, BLD_FARMSTEAD, 0, 1.0f);
		run(s, 0.5f);
		const uint32_t grain = s.field_resource_id(field);
		std::vector<uint32_t> women;
		for (int k = 0; k < 5; ++k) women.push_back(s.spawn_unit(-4.0f + 2.0f * k, 4.0f, 0, UNIT_FEMALE_CITIZEN));
		s.order_gather(women.data(), (int)women.size(), grain);
		run(s, 20.0f);
		int farming = 0;
		for (uint32_t id : women) {
			const int32_t u = s.lookup_unit(id);
			if (u >= 0 && s.units_.target_resource_id[u] == grain) ++farming;
		}
		const auto rit = s.res_id_to_idx_.find(grain);
		const int cap = rit == s.res_id_to_idx_.end() ? -1 : s.resources_.max_gatherers[rit->second];
		std::printf("  Han rice paddy: %d of 5 women farm it (MaxGatherers %d)\n", farming, cap);
		CHECK(farming == 3 && cap == 3);
	}

	// 13. ResourceGatherer.StartGathering sets a fresh timer: the first unit comes 1 / rate after
	// gathering starts, whatever a kill (attack timer -1 s) or a fight (time banked) left behind.
	{
		auto first_unit = [](float leftover) {
			static Simulation s;
			fresh(s);
			const uint32_t bush = s.spawn_resource_world(0.0f, 0.0f, RES_WOOD, 200);
			const uint32_t woman = s.spawn_unit(1.2f, 0.0f, 0, UNIT_FEMALE_CITIZEN);
			const int32_t w = s.lookup_unit(woman);
			s.order_gather(&woman, 1, bush);
			s.units_.attack_timer[w] = leftover;
			int ticks = 0, burst = 0;
			while (s.units_.carry[w] < 1 && ticks < 2000) { s.tick(Simulation::FIXED_STEP); ++ticks; }
			burst = s.units_.carry[w];
			return std::make_pair(ticks, burst);
		};
		const auto clean = first_unit(0.0f), after_kill = first_unit(-1.0f), after_fight = first_unit(5.0f);
		std::printf("  first unit after %d / %d / %d ticks (fresh / after a kill / after a fight), %d / %d / %d at once\n",
				clean.first, after_kill.first, after_fight.first, clean.second, after_kill.second, after_fight.second);
		CHECK(after_kill.first == clean.first && after_fight.first == clean.first);
		CHECK(after_fight.second == 1);
	}

	// 14. ptol_player_teambonus is a player aura (Auras.js): a mutual ally with units but no
	// buildings still gets +1 food a second.
	{
		static Simulation s;
		fresh(s);
		s.set_player_team(1, 0);
		s.player(0).civ = CIV_PTOLEMIES;
		s.player(1).civ = CIV_ATHENIANS;
		s.spawn_building_world(-30.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
		s.spawn_unit(30.0f, 0.0f, 1, UNIT_FEMALE_CITIZEN);
		run(s, 0.5f);
		const float food0 = s.player(1).res[RES_FOOD];
		run(s, 10.0f);
		const float got = s.player(1).res[RES_FOOD] - food0;
		std::printf("  Ptolemaic team bonus: a units-only ally gets %.0f food in 10 s\n", got);
		CHECK(got >= 9.0f && got <= 11.0f);
	}

	// 15. UnitAI ConstructionFinished: the builder of a new storehouse gathers the nearest supply it
	// takes (FindNearbyResource with the dropsite's types), not the kind she gathered last.
	{
		static Simulation s;
		fresh(s);
		s.spawn_building_world(-40.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
		const uint32_t site = s.spawn_building_world(20.0f, 0.0f, BLD_STOREHOUSE, 0, 0.0f);
		const uint32_t mine = s.spawn_resource_world(20.0f, 7.0f, RES_STONE, 5000);
		const uint32_t tree = s.spawn_resource_world(20.0f, -12.0f, RES_WOOD, 200);
		const uint32_t woman = s.spawn_unit(24.0f, 0.0f, 0, UNIT_FEMALE_CITIZEN);
		s.units_.res_type_carried[s.lookup_unit(woman)] = RES_WOOD; // she cut wood last
		s.order_build_or_repair(&woman, 1, site);
		bool done = false;
		for (int k = 0; k < 600 && !done; ++k) {
			run(s, 1.0f);
			for (const auto &b : s.buildings()) if (b.id == site) done = b.build_progress >= 1.0f;
		}
		run(s, 0.5f);
		const uint32_t next = s.units_.target_resource_id[s.lookup_unit(woman)];
		std::printf("  storehouse built: the builder goes to the %s\n", next == mine ? "mine (nearer)" : next == tree ? "tree" : "nothing");
		CHECK(done && next == mine);
	}

	// 15b. FindNearbyResource filters on CanGather before it picks the nearest: fish (land units
	// have no food.fish rate) beside a new farmstead don't hide the berries behind it.
	{
		static Simulation s;
		fresh(s);
		s.spawn_building_world(-40.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
		const uint32_t site = s.spawn_building_world(20.0f, 0.0f, BLD_FARMSTEAD, 0, 0.0f);
		s.spawn_resource_world(20.0f, 5.0f, RES_FOOD, 400, RES_SUB_FISH);
		const uint32_t bush = s.spawn_resource_world(20.0f, -9.0f, RES_FOOD, 200); // berries
		const uint32_t woman = s.spawn_unit(24.0f, 0.0f, 0, UNIT_FEMALE_CITIZEN);
		s.order_build_or_repair(&woman, 1, site);
		bool done = false;
		for (int k = 0; k < 600 && !done; ++k) {
			run(s, 1.0f);
			for (const auto &b : s.buildings()) if (b.id == site) done = b.build_progress >= 1.0f;
		}
		run(s, 0.5f);
		const uint32_t next = s.units_.target_resource_id[s.lookup_unit(woman)];
		std::printf("  farmstead built beside fish: the builder goes to the %s\n", next == bush ? "berries" : next ? "fish" : "nothing");
		CHECK(done && next == bush);
	}

	// 16. UnitAI FINDINGNEWTARGET: a farmer turned away from a full field takes a free field of any
	// player's within 64 m (FindNearbyResource), not her own across the map. Explored, as
	// CheckTargetVisible skips "hidden" supplies.
	{
		static Simulation s;
		fresh(s);
		s.set_explore_map(true);
		const uint32_t full = s.spawn_building_world(0.0f, 0.0f, BLD_FIELD, 0, 1.0f);
		const uint32_t theirs = s.spawn_building_world(0.0f, 10.0f, BLD_FIELD, 1, 1.0f);
		const uint32_t far_own = s.spawn_building_world(0.0f, -70.0f, BLD_FIELD, 0, 1.0f);
		s.spawn_building_world(-8.0f, 4.0f, BLD_FARMSTEAD, 0, 1.0f);
		run(s, 0.5f);
		const uint32_t grain = s.field_resource_id(full), near_grain = s.field_resource_id(theirs), far_grain = s.field_resource_id(far_own);
		std::vector<uint32_t> women;
		for (int k = 0; k < 6; ++k) women.push_back(s.spawn_unit(-5.0f + 2.0f * k, -4.0f, 0, UNIT_FEMALE_CITIZEN));
		s.order_gather(women.data(), (int)women.size(), grain);
		run(s, 15.0f);
		int on_full = 0, on_near = 0, on_far = 0;
		for (uint32_t id : women) {
			const uint32_t t = s.units_.target_resource_id[s.lookup_unit(id)];
			on_full += t == grain; on_near += t == near_grain; on_far += t == far_grain;
		}
		std::printf("  full field: %d farm it, %d moved to player 1's field 10 away, %d to her own 70 away\n", on_full, on_near, on_far);
		CHECK(on_full == 5 && on_near == 1 && on_far == 0);
	}

	// 17. UnitAI Order.Gather returns first only when full of the type she is sent to gather: a
	// woman full of wood sent to berries walks straight there, and StartGathering drops the wood.
	{
		static Simulation s;
		fresh(s);
		s.spawn_building_world(-20.0f, 0.0f, BLD_STOREHOUSE, 0, 1.0f);
		const uint32_t bush = s.spawn_resource_world(10.0f, 0.0f, RES_FOOD, 200);
		const uint32_t w = s.spawn_unit(0.0f, 0.0f, 0, UNIT_FEMALE_CITIZEN);
		const int32_t i = s.lookup_unit(w);
		s.units_.res_type_carried[i] = RES_WOOD;
		s.units_.carry[i] = s.carry_cap((size_t)i, RES_WOOD);
		const float wood0 = s.player(0).res[RES_WOOD];
		s.order_gather(&w, 1, bush);
		bool returned = false;
		for (int k = 0; k < 200; ++k) { run(s, 0.05f); returned |= s.units_.action_state[i] == ACTION_RETURN_DROPSITE; }
		std::printf("  full of wood sent to berries: %s, carrying %d %s\n", returned ? "went home first" : "went straight there",
				s.units_.carry[i], s.units_.res_type_carried[i] == RES_FOOD ? "food" : "other");
		CHECK(!returned && s.units_.res_type_carried[i] == RES_FOOD && s.units_.carry[i] > 0 && s.player(0).res[RES_WOOD] == wood0);
	}

	// 18. Order.Gather on a supply that is gone -> FINDINGNEWTARGET, whose FinishOrder takes the
	// next queued order: a shift-queued tree felled by someone else is skipped, not replaced by
	// the tree beside it.
	{
		static Simulation s;
		fresh(s);
		s.spawn_building_world(0.0f, -6.0f, BLD_STOREHOUSE, 0, 1.0f);
		const uint32_t t1 = s.spawn_resource_world(3.0f, 0.0f, RES_WOOD, 4);
		const uint32_t t2 = s.spawn_resource_world(0.0f, 12.0f, RES_WOOD, 200);
		const uint32_t t3 = s.spawn_resource_world(12.0f, 0.0f, RES_WOOD, 200);
		const uint32_t t4 = s.spawn_resource_world(0.0f, 13.5f, RES_WOOD, 200);
		const uint32_t w = s.spawn_unit(0.0f, 0.0f, 0, UNIT_FEMALE_CITIZEN);
		s.order_gather(&w, 1, t1);
		s.order_gather(&w, 1, t2, true);
		s.order_gather(&w, 1, t3, true);
		s.resources_.amount[s.res_id_to_idx_.at(t2)] = 0; // felled by another player meanwhile
		bool to_t4 = false;
		const int32_t i = s.lookup_unit(w);
		for (int k = 0; k < 400; ++k) { run(s, 0.05f); to_t4 |= s.units_.target_resource_id[i] == t4; }
		std::printf("  queued tree gone: now on %s\n", s.units_.target_resource_id[i] == t3 ? "the next queued tree" :
				s.units_.target_resource_id[i] == t4 ? "the tree beside the gone one" : "something else");
		CHECK(!to_t4 && s.units_.target_resource_id[i] == t3);
	}

	// 19. A right click on a stone mine used up in the fog (its mirage): Order.Gather on the mirage
	// fails where it stood and FINDINGNEWTARGET takes the same type.specific near it (UnitAI.js
	// :2552-2555, :2591-2660), not the tree beside it. With none near, the women walk there.
	{
		static Simulation s;
		fresh(s);
		s.set_explore_map(true);
		s.spawn_resource_world(9.0f, 0.0f, RES_WOOD, 200);
		const uint32_t mine = s.spawn_resource_world(15.0f, 0.0f, RES_STONE, 500);
		uint32_t w[3];
		for (int k = 0; k < 3; ++k) w[k] = s.spawn_unit(0.0f, 2.0f * (float)k, 0, UNIT_FEMALE_CITIZEN);
		s.order_gather_near(w, 3, 8.0f, 0.0f, RES_STONE, RES_SUB_NONE);
		int on_mine = 0;
		for (uint32_t id : w) on_mine += s.units_.target_resource_id[s.lookup_unit(id)] == mine;
		s.order_gather_near(w, 1, 60.0f, 60.0f, RES_STONE, RES_SUB_NONE);
		const int32_t i = s.lookup_unit(w[0]);
		std::printf("  gone mine's mirage: %d of 3 women take the stone mine near it; with none near, order %d to (%.0f, %.0f)\n",
				on_mine, (int)s.units_.order_kind[i], s.units_.order_x[i], s.units_.order_z[i]);
		CHECK(on_mine == 3);
		CHECK(s.units_.order_kind[i] == ORDER_WALK && s.units_.order_x[i] == 60.0f && s.units_.order_z[i] == 60.0f);
	}

	// 20. UnitAI REPAIR on a structure already whole (a queued build finished by others): the
	// builder walks there all the same, and REPAIRING finds it whole: ConstructionFinished, so
	// she gathers by the new storehouse (force becomes autoharvest).
	{
		static Simulation s;
		fresh(s);
		const uint32_t store = s.spawn_building_world(20.0f, 0.0f, BLD_STOREHOUSE, 0, 1.0f);
		const uint32_t mine = s.spawn_resource_world(20.0f, 7.0f, RES_STONE, 5000);
		const uint32_t woman = s.spawn_unit(-10.0f, 0.0f, 0, UNIT_FEMALE_CITIZEN);
		s.order_build_or_repair(&woman, 1, store);
		run(s, 30.0f);
		const int32_t i = s.lookup_unit(woman);
		std::printf("  sent to a finished storehouse: she is at (%.0f, %.0f), gathering %s\n", s.units_.pos_x[i], s.units_.pos_z[i],
				s.units_.target_resource_id[i] == mine ? "the mine" : "nothing");
		CHECK(s.units_.target_resource_id[i] == mine);
	}

	// 21. ConstructionFinished for a builder still on her way: with nothing else to do she walks on
	// to the finished structure (WalkToTarget) rather than stopping where she is.
	{
		static Simulation s;
		fresh(s);
		const uint32_t site = s.spawn_building_world(20.0f, 0.0f, BLD_HOUSE, 0, 0.0f);
		const uint32_t near_w = s.spawn_unit(20.0f, -4.0f, 0, UNIT_FEMALE_CITIZEN);
		const uint32_t far_w = s.spawn_unit(-60.0f, 0.0f, 0, UNIT_FEMALE_CITIZEN);
		s.order_build_or_repair(&near_w, 1, site);
		s.order_build_or_repair(&far_w, 1, site);
		bool done = false;
		for (int k = 0; k < 600 && !done; ++k) {
			run(s, 0.2f);
			for (const auto &b : s.buildings()) if (b.id == site) done = b.build_progress >= 1.0f;
		}
		const int32_t i = s.lookup_unit(far_w);
		const float at_x = s.units_.pos_x[i];
		run(s, 40.0f);
		std::printf("  house built: the builder still on her way was at x %.0f, now at x %.0f\n", at_x, s.units_.pos_x[i]);
		CHECK(done && at_x < 10.0f && s.units_.pos_x[i] > 12.0f);
	}

	// 22. Repairable.js sends ConstructionFinished for a unit too: once the ram is whole its repairer
	// takes a foundation nearby (autocontinue).
	{
		static Simulation s;
		fresh(s);
		const uint32_t ram = s.spawn_unit(0.0f, 0.0f, 0, UNIT_SIEGE_RAM);
		const uint32_t woman = s.spawn_unit(2.5f, 0.0f, 0, UNIT_FEMALE_CITIZEN);
		const int32_t r = s.lookup_unit(ram);
		s.units_.hp[r] = s.units_.max_hp[r] - 5.0f;
		const uint32_t site = s.spawn_building_world(14.0f, 0.0f, BLD_HOUSE, 0, 0.0f);
		s.order_repair_unit(&woman, 1, ram, false, true);
		run(s, 40.0f);
		float progress = 0.0f;
		for (const auto &b : s.buildings()) if (b.id == site) progress = b.build_progress;
		std::printf("  ram repaired to %.0f/%.0f, then the house nearby %.0f%% built\n", s.units_.hp[r], s.units_.max_hp[r], 100.0f * progress);
		CHECK(s.units_.hp[r] >= s.units_.max_hp[r] && progress > 0.05f);
	}

	std::printf(failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", failures);
	return failures ? 1 : 0;
}
