// Fog-of-war presentation tests: 0 A.D. Fogging.js/Mirage.js mirages and the
// CLOSTexture LOS bitmap (src/presentation/fog_mirage.cpp).
//
// g++ -std=c++20 -O2 -Isrc tools/test_fog_mirage.cpp src/presentation/fog_mirage.cpp \
//   src/simulation/simulation.cpp src/simulation/rms_generator.cpp src/core/job_system.cpp \
//   -lpthread -o /tmp/test_fog_mirage
#define private public // end_match
#include "simulation/simulation.h"
#undef private
#include "presentation/fog_mirage.h"
#include "simulation/rms_generator.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

using rtso::FogMirages;
using rtso::FoggedBuilding;
using rtso::FoggedResource;
using rtso::LosShading;
using rtso::Simulation;

static int failures = 0;
#define CHECK(cond, msg)                                              \
	do {                                                              \
		if (!(cond)) {                                                \
			std::printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
			++failures;                                               \
		} else {                                                      \
			std::printf("ok   %s\n", msg);                            \
		}                                                             \
	} while (0)

// 4x4 grid over [-8, 8]^2: cell (cx, cz) covers x in [-8 + 4cx, -4 + 4cx).
struct Grid4 {
	uint8_t c[16];
	void fill(uint8_t v) { for (auto &x : c) x = v; }
	void set(int cx, int cz, uint8_t v) { c[cz * 4 + cx] = v; }
};
static float centre(int cell) { return -8.0f + 4.0f * cell + 2.0f; }

static const FoggedBuilding *find_b(const FogMirages &m, uint32_t id) {
	for (const auto &b : m.buildings())
		if (b.id == id) return &b;
	return nullptr;
}
static const FoggedResource *find_r(const FogMirages &m, uint32_t id) {
	for (const auto &r : m.resources())
		if (r.id == id) return &r;
	return nullptr;
}

static FoggedBuilding bld(uint32_t id, int cx, int cz, uint8_t team, float hp) {
	FoggedBuilding b;
	b.id = id;
	b.x = centre(cx);
	b.z = centre(cz);
	b.team = team;
	b.hp = hp;
	b.max_hp = 1000.0f;
	return b;
}

static void test_building_mirages() {
	FogMirages m;
	Grid4 g;
	g.fill(Simulation::FOW_EXPLORED);
	g.set(0, 0, Simulation::FOW_VISIBLE);

	// Frame 1: enemy house in sight; enemy barracks in explored fog, never seen.
	m.begin(g.c, 4, 8.0f);
	FoggedBuilding seen_house = bld(1, 0, 0, 1, 900.0f);
	seen_house.has_resistance = true; // as update() fills it for a structure in sight
	seen_house.resistance[0] = 5.0f;
	m.add_building(seen_house, false);
	m.add_building(bld(2, 2, 2, 1, 1000.0f), false);
	m.add_building(bld(3, 3, 3, 0, 1000.0f), true); // own, in fog
	m.end();
	CHECK(find_b(m, 1) && !find_b(m, 1)->miraged, "enemy structure in sight shows live");
	CHECK(!find_b(m, 2), "enemy structure built in the fog stays hidden until seen");
	CHECK(find_b(m, 3) && !find_b(m, 3)->miraged, "own structure in the fog shows live");

	// Frame 2: the house's cell falls into fog; meanwhile it took damage.
	g.set(0, 0, Simulation::FOW_EXPLORED);
	m.begin(g.c, 4, 8.0f);
	m.add_building(bld(1, 0, 0, 1, 300.0f), false);
	m.add_building(bld(2, 2, 2, 1, 1000.0f), false);
	m.add_building(bld(3, 3, 3, 0, 1000.0f), true);
	m.end();
	const FoggedBuilding *h = find_b(m, 1);
	CHECK(h && h->miraged, "fogged enemy structure becomes a mirage");
	CHECK(h && h->hp == 900.0f, "mirage keeps the last seen hit points, not live ones");
	CHECK(h && h->has_resistance && h->resistance[0] == 5.0f, "mirage keeps the last seen resistance (ResistanceMirage)");

	// Frame 3: the house is destroyed out of sight; its mirage stays.
	m.begin(g.c, 4, 8.0f);
	m.add_building(bld(2, 2, 2, 1, 1000.0f), false);
	m.end();
	CHECK(find_b(m, 1) && find_b(m, 1)->miraged, "mirage of a destroyed structure stays in the fog");

	// Frame 4: the viewer sees the spot again; the mirage is gone.
	g.set(0, 0, Simulation::FOW_VISIBLE);
	m.begin(g.c, 4, 8.0f);
	m.add_building(bld(2, 2, 2, 1, 1000.0f), false);
	m.end();
	CHECK(!find_b(m, 1), "mirage of a destroyed structure clears once seen again");
	CHECK(m.building_mirage_count() == 0, "no memory left for the destroyed structure");

	// Frame 5: unexplored cells never show anything, mirage or live.
	Grid4 dark;
	dark.fill(Simulation::FOW_UNEXPLORED);
	m.begin(dark.c, 4, 8.0f);
	m.add_building(bld(2, 2, 2, 1, 1000.0f), false);
	m.end();
	CHECK(!find_b(m, 2), "unexplored enemy structure is hidden");

	// Captured structure: the viewer keeps the old owner until it looks again.
	Grid4 g2;
	g2.fill(Simulation::FOW_VISIBLE);
	FogMirages c;
	c.begin(g2.c, 4, 8.0f);
	c.add_building(bld(7, 1, 1, 1, 1000.0f), false);
	c.end();
	g2.set(1, 1, Simulation::FOW_EXPLORED);
	c.begin(g2.c, 4, 8.0f);
	c.add_building(bld(7, 1, 1, 2, 1000.0f), false);
	c.end();
	CHECK(find_b(c, 7) && find_b(c, 7)->team == 1, "mirage keeps the owner it was seen with");
	// Capturable is mirrored too (Fogging.js componentsToMirage): the inspector shows these.
	FogMirages cp;
	Grid4 g3;
	g3.fill(Simulation::FOW_VISIBLE);
	FoggedBuilding held = bld(8, 2, 1, 1, 1000.0f);
	held.capture_points = 2500.0f;
	held.max_capture_points = 2500.0f;
	cp.begin(g3.c, 4, 8.0f);
	cp.add_building(held, false);
	cp.end();
	g3.set(2, 1, Simulation::FOW_EXPLORED);
	held.capture_points = 400.0f;
	cp.begin(g3.c, 4, 8.0f);
	cp.add_building(held, false);
	cp.end();
	CHECK(find_b(cp, 8) && find_b(cp, 8)->capture_points == 2500.0f && find_b(cp, 8)->max_capture_points == 2500.0f,
			"mirage keeps the capture points it was seen with");
	c.begin(g2.c, 4, 8.0f);
	c.add_building(bld(7, 1, 1, 0, 1000.0f), true);
	c.end();
	CHECK(find_b(c, 7) && !find_b(c, 7)->miraged && c.building_mirage_count() == 0,
			"structure captured by the viewer shows live and drops its mirage");
}

static void test_resource_mirages() {
	FogMirages m;
	Grid4 g;
	g.fill(Simulation::FOW_UNEXPLORED);
	g.set(1, 1, Simulation::FOW_VISIBLE);
	g.set(2, 2, Simulation::FOW_EXPLORED);

	auto res = [](uint32_t id, int cx, int cz, int amount) {
		FoggedResource r;
		r.id = id;
		r.x = centre(cx);
		r.z = centre(cz);
		r.amount = amount;
		r.type = rtso::RES_GOLD;
		return r;
	};
	m.begin(g.c, 4, 8.0f);
	m.add_resource(res(10, 1, 1, 1000));
	m.add_resource(res(11, 2, 2, 500));
	m.add_resource(res(12, 3, 3, 500));
	m.end();
	CHECK(find_r(m, 10) && !find_r(m, 10)->miraged, "resource in sight shows live");
	CHECK(find_r(m, 11), "untouched resource in explored fog shows (RetainInFog)");
	CHECK(!find_r(m, 12), "resource in unexplored land is hidden");

	// The gold mine drops into fog and gets mined out by an enemy.
	g.set(1, 1, Simulation::FOW_EXPLORED);
	m.begin(g.c, 4, 8.0f);
	m.add_resource(res(10, 1, 1, 100));
	m.add_resource(res(11, 2, 2, 20));
	m.end();
	CHECK(find_r(m, 10) && find_r(m, 10)->amount == 1000, "fogged resource keeps the last seen amount");
	CHECK(find_r(m, 11) && find_r(m, 11)->amount == 500, "explored resource does not leak gathering");
	m.begin(g.c, 4, 8.0f);
	m.add_resource(res(11, 2, 2, 20));
	m.end();
	CHECK(find_r(m, 10) && find_r(m, 10)->miraged, "exhausted resource stays as a mirage in the fog");
	g.set(1, 1, Simulation::FOW_VISIBLE);
	m.begin(g.c, 4, 8.0f);
	m.add_resource(res(11, 2, 2, 20));
	m.end();
	CHECK(!find_r(m, 10), "exhausted resource vanishes once seen again");
}

// Treasure.Reward activates Fogging: a treasure taken out of sight stays in the fog.
static void test_treasure_mirages() {
	FogMirages m;
	Grid4 g;
	g.fill(Simulation::FOW_UNEXPLORED);
	g.set(1, 1, Simulation::FOW_VISIBLE);
	g.set(2, 2, Simulation::FOW_EXPLORED);
	auto tr = [](uint32_t id, int cx, int cz) {
		rtso::FoggedTreasure t;
		t.id = id;
		t.x = centre(cx);
		t.z = centre(cz);
		return t;
	};
	m.begin(g.c, 4, 8.0f);
	m.add_treasure(tr(20, 1, 1));
	m.add_treasure(tr(21, 2, 2));
	m.add_treasure(tr(22, 3, 3));
	m.end();
	CHECK(m.treasures().size() == 2, "treasures in sight and explored fog show, unexplored ones don't");
	// Both are taken; 20 in sight, 21 in the fog.
	m.begin(g.c, 4, 8.0f);
	m.end();
	CHECK(m.treasures().size() == 1 && m.treasures()[0].id == 21 && m.treasures()[0].miraged,
		"a treasure taken in the fog stays as a mirage, one taken in sight is gone");
	g.set(2, 2, Simulation::FOW_VISIBLE);
	m.begin(g.c, 4, 8.0f);
	m.end();
	CHECK(m.treasures().empty(), "the treasure mirage clears once the spot is seen");
	// special_argo.xml RetainInFog false: the Argo shows only in sight and leaves no mirage.
	g.set(2, 2, Simulation::FOW_EXPLORED);
	rtso::FoggedTreasure argo = tr(23, 1, 1);
	argo.type = rtso::TREASURE_SPECIAL_ARGO;
	m.begin(g.c, 4, 8.0f);
	m.add_treasure(argo);
	m.end();
	CHECK(m.treasures().size() == 1, "the Argo in sight shows");
	g.set(1, 1, Simulation::FOW_EXPLORED);
	m.begin(g.c, 4, 8.0f);
	m.add_treasure(argo);
	m.end();
	CHECK(m.treasures().empty(), "the Argo in explored fog is hidden, with no mirage");
	CHECK(!rtso::fauna_retained_in_fog(rtso::FAUNA_SHARK) && rtso::fauna_retained_in_fog(rtso::FAUNA_DEER),
		"fauna_shark is not RetainInFog, other wild animals are");
}

static void test_los_bitmap() {
	// 16x16: all visible except an unexplored 4x4 corner and an explored band.
	const int R = 16;
	std::vector<uint8_t> fow(R * R, Simulation::FOW_VISIBLE);
	for (int z = 0; z < 4; ++z)
		for (int x = 0; x < 4; ++x) fow[z * R + x] = Simulation::FOW_UNEXPLORED;
	for (int x = 0; x < R; ++x) fow[12 * R + x] = Simulation::FOW_EXPLORED;
	std::vector<uint8_t> out;
	LosShading::generate_bitmap(fow.data(), R, out);
	// UPSAMPLE pixels per FOW cell each way (0.28's LOS vertices are 1 unit apart).
	const int F = R * LosShading::UPSAMPLE;
	CHECK(out.size() == (size_t)F * F, "the bitmap has two pixels per FOW cell each way");
	auto px = [&](int x, int z) { return (int)out[(size_t)z * F + x]; };
	CHECK(px(16, 16) == 255, "deep visible cell is fully lit (255)");
	CHECK(px(0, 0) < 20, "deep unexplored cell is dark");
	CHECK(px(16, 24) > 127 && px(16, 24) < 255, "explored band is blurred between 127 and 255");
	CHECK(px(7, 7) > px(2, 2) && px(7, 7) < 255, "the fog edge is a soft gradient");
	// 0.28's blur spans 7 LOS vertices (7 units); with 2-unit FOW cells blurred
	// directly it spanned 14. From the unexplored corner's edge (x = 8 pixels) along
	// a visible row, the gradient reaches 255 within about half that.
	int ramp = 0;
	for (int x = 8; x < F && px(x, 4) < 255; ++x) ++ramp;
	int dark = 0;
	for (int x = 7; x >= 0 && px(x, 4) > 0; --x) ++dark;
	CHECK(ramp + dark <= 9, "the fog edge is about 7 pixels (units) wide, as 0.28's");

	// Interpolation reaches the new target within half a second (los_interp delta).
	LosShading s;
	s.set_target(fow.data(), R);
	s.snap();
	std::vector<uint8_t> all(R * R, Simulation::FOW_VISIBLE);
	s.set_target(all.data(), R);
	s.interpolate(0.016f);
	CHECK(s.shown()[0] < 255, "fog lifts gradually, not in one frame");
	for (int i = 0; i < 40; ++i) s.interpolate(0.016f);
	CHECK(s.shown() == s.target(), "fog lift completes within ~0.6 s");
}

static void test_real_sim() {
	Simulation sim;
	sim.set_sim_seed(42);
	sim.spawn_initial();
	sim.set_fow_viewer(0);
	for (int i = 0; i < 25; ++i) sim.tick(0.2f);
	FogMirages m;
	m.update(sim, 0);
	size_t own = 0, other_hidden = 0, other_shown = 0;
	for (const auto &b : sim.buildings()) {
		const bool shown = find_b(m, b.id) != nullptr;
		if (sim.shares_los(0, b.team)) {
			own += shown ? 1 : 0;
			CHECK(shown, "own (or shared-sight ally's) structure is always drawn");
		} else if (!sim.los_explored(0, b.x, b.z)) {
			other_hidden += shown ? 0 : 1;
			if (shown) CHECK(false, "unexplored enemy structure must not be drawn");
		} else if (shown) {
			++other_shown;
		}
	}
	std::printf("     real sim: %zu own structures, %zu hidden enemy, %zu visible enemy, %zu resources drawn of %zu\n",
			own, other_hidden, other_shown, m.resources().size(), sim.resource_count());
	CHECK(own > 0, "real sim: the viewer's town centre is drawn");
	CHECK(other_hidden > 0, "real sim: the far enemy base is hidden at the start");
	CHECK(m.resources().size() < sim.resource_count(), "real sim: unexplored resources are not drawn");

	// The drawn fog is the sim's per-player LOS (#218), the one the sim and Petra use.
	const int G = sim.fow_res();
	const float half = sim.map_half(), cell = 2.0f * half / (float)G;
	bool agrees = m.view().size() == (size_t)G * G;
	size_t seen = 0;
	for (int cz = 0; agrees && cz < G; cz += 3)
		for (int cx = 0; cx < G; cx += 3) {
			const float x = -half + (cx + 0.5f) * cell, z = -half + (cz + 0.5f) * cell;
			const uint8_t want = sim.los_visible(0, x, z) ? Simulation::FOW_VISIBLE
					: sim.los_explored(0, x, z) ? Simulation::FOW_EXPLORED : Simulation::FOW_UNEXPLORED;
			agrees = agrees && m.view()[(size_t)cz * G + cx] == want;
			seen += want == Simulation::FOW_VISIBLE;
		}
	CHECK(agrees && seen > 0, "drawn fog matches Simulation::los_visible / los_explored");
	std::vector<uint8_t> obs;
	rtso::los_view_grid(sim, rtso::OBSERVER_VIEW, obs);
	// The map's edge band stays off the world (LosIsOffWorld), revealed or not.
	bool obs_all = obs.size() == sim.los().cells();
	for (size_t i = 0; obs_all && i < obs.size(); ++i)
		obs_all = obs[i] == (sim.los().off_world((int)i) ? Simulation::FOW_UNEXPLORED : Simulation::FOW_VISIBLE);
	CHECK(obs_all, "an observer sees the whole map");
	FogMirages enemy;
	enemy.update(sim, 1);
	CHECK(enemy.view() != m.view(), "each player draws its own LOS");

	// Cost on the CPU: update() is skipped between sim ticks, so time a forced
	// rebuild (one per sim tick) and the per-frame LOS interpolation.
	const int N = 200;
	auto t0 = std::chrono::steady_clock::now();
	for (int i = 0; i < N; ++i) {
		FogMirages fresh;
		fresh.update(sim, 0);
	}
	auto t1 = std::chrono::steady_clock::now();
	LosShading los;
	los.set_target(m.view().data(), sim.fow_res());
	auto t2 = std::chrono::steady_clock::now();
	for (int i = 0; i < N; ++i) {
		std::vector<uint8_t> tmp;
		LosShading::generate_bitmap(m.view().data(), sim.fow_res(), tmp);
	}
	auto t3 = std::chrono::steady_clock::now();
	auto us = [](auto a, auto b) { return std::chrono::duration<double, std::micro>(b - a).count(); };
	std::printf("     cost: mirage rebuild %.1f us/sim tick, LOS bitmap %.1f us/fog change (%dx%d grid, %zu resources)\n",
			us(t0, t1) / N, us(t2, t3) / N, sim.fow_res(), sim.fow_res(), sim.resource_count());
}

// Cost at 0 A.D. "Giant" size (512 m, 256x256 LOS grid), 8 players.
static void test_giant_cost() {
	Simulation sim;
	sim.generate_random_map(rtso::MAP_MAINLAND, 7, 8, 256.0f);
	sim.set_fow_viewer(0);
	for (int i = 0; i < 25; ++i) sim.tick(0.2f);
	const int N = 50;
	auto t0 = std::chrono::steady_clock::now();
	size_t drawn = 0;
	std::vector<uint8_t> view;
	for (int i = 0; i < N; ++i) {
		FogMirages fresh;
		fresh.update(sim, 0);
		drawn = fresh.resources().size();
		if (i == 0) view = fresh.view();
	}
	auto t1 = std::chrono::steady_clock::now();
	std::vector<uint8_t> tmp;
	for (int i = 0; i < N; ++i) LosShading::generate_bitmap(view.data(), sim.fow_res(), tmp);
	auto t2 = std::chrono::steady_clock::now();
	LosShading los;
	los.set_target(view.data(), sim.fow_res());
	std::vector<uint8_t> all((size_t)sim.fow_res() * sim.fow_res(), Simulation::FOW_VISIBLE);
	los.set_target(all.data(), sim.fow_res());
	auto t3 = std::chrono::steady_clock::now();
	for (int i = 0; i < N; ++i) los.interpolate(0.001f);
	auto t4 = std::chrono::steady_clock::now();
	auto us = [](auto a, auto b) { return std::chrono::duration<double, std::micro>(b - a).count(); };
	std::printf("     giant cost: mirage rebuild %.1f us/sim tick (%zu buildings, %zu resources, %zu drawn), "
			"LOS bitmap %.1f us/fog change, LOS ease %.1f us/frame (%dx%d)\n",
			us(t0, t1) / N, sim.buildings().size(), sim.resource_count(), drawn, us(t1, t2) / N, us(t3, t4) / N,
			sim.fow_res(), sim.fow_res());
	CHECK(sim.fow_res() == 256, "giant map uses a 256-cell LOS grid");
	CHECK(us(t0, t1) / N < 5000.0, "giant map mirage rebuild stays under 5 ms per sim tick");
}

static void test_shared_sight_and_winner() {
	using namespace rtso;
	// An ally's units are seen out of our sight only once sight is shared (Diplomacy.js
	// unlock_shared_los, units not RetainInFog); a winner's map is revealed (Player.js).
	static Simulation a;
	a.set_sim_seed(7);
	a.reset();
	a.spawn_building_world(-60.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
	a.spawn_building_world(60.0f, 0.0f, BLD_TOWN_CENTER, 1, 1.0f);
	a.set_mutual_ally(1);
	a.spawn_unit(60.0f, 6.0f, 1, UNIT_SPEARMAN);
	for (int i = 0; i < 10; ++i) a.tick(Simulation::FIXED_STEP);
	const bool before = los_sees_unit(a, 0, 60.0f, 6.0f, 1);
	const bool vis_before = a.is_entity_visible_to_viewer(60.0f, 6.0f, 1, 0);
	FogMirages fm;
	fm.update(a, 0);
	size_t ally_bld_shown = 0;
	for (const auto &b : a.buildings()) ally_bld_shown += b.team == 1 && find_b(fm, b.id) != nullptr;
	a.player(0).techs.set(TECH_UNLOCK_SHARED_LOS);
	for (int i = 0; i < 10; ++i) a.tick(Simulation::FIXED_STEP);
	const bool after = los_sees_unit(a, 0, 60.0f, 6.0f, 1);
	std::printf("ally unit far away: seen %d without Cartography, %d with; unseen ally structures shown %zu\n", before,
			after, ally_bld_shown);
	CHECK(!before && !vis_before && after && ally_bld_shown == 0, "ally sight shared only with Cartography");

	static Simulation w;
	w.set_sim_seed(7);
	w.reset();
	w.spawn_building_world(-50.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
	w.spawn_building_world(50.0f, 0.0f, BLD_TOWN_CENTER, 1, 1.0f);
	for (int i = 0; i < 10; ++i) w.tick(Simulation::FIXED_STEP);
	const bool hidden = !los_sees_all(w, 0) && !los_sees_unit(w, 0, 50.0f, 6.0f, 1);
	w.end_match(1u << 0);
	const bool revealed = los_sees_all(w, 0) && w.fow_state_at(50.0f, 0.0f, 0) == Simulation::FOW_VISIBLE;
	std::printf("winner: enemy base hidden before %d, map revealed after the win %d\n", hidden, revealed);
	CHECK(hidden && revealed, "winner's map revealed");
}

static void test_seen_mask() {
	using namespace rtso;
	// "Explored map": every structure standing at the start is seen (SeeExploredEntities), so an
	// enemy base shows as a mirage from the first turn; the seen bits survive save/load, and a
	// fresh mirage cache (a load, a rejoin) shows what was seen before as a mirage.
	static Simulation s;
	s.set_sim_seed(7);
	s.reset();
	s.set_explore_map(true);
	s.spawn_building_world(-50.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
	const uint32_t enemy = s.spawn_building_world(50.0f, 0.0f, BLD_TOWN_CENTER, 1, 1.0f);
	for (int i = 0; i < 10; ++i) s.tick(Simulation::FIXED_STEP);
	const Simulation::Building *e = nullptr;
	for (const auto &b : s.buildings()) if (b.id == enemy) e = &b;
	FogMirages m;
	m.update(s, 0);
	const FoggedBuilding *f = find_b(m, enemy);
	std::printf("explored map: enemy seen mask %02x, drawn %d as mirage %d\n", e ? e->seen_mask : 0, f != nullptr, f && f->miraged);
	CHECK(e && (e->seen_mask & 1) && f && f->miraged, "explored map shows an unseen enemy base as a mirage");

	static Simulation t;
	t.set_sim_seed(7);
	t.reset();
	t.spawn_building_world(-50.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
	const uint32_t far = t.spawn_building_world(50.0f, 0.0f, BLD_TOWN_CENTER, 1, 1.0f);
	const uint32_t scout = t.spawn_unit(45.0f, 0.0f, 0, UNIT_CAVALRY);
	for (int i = 0; i < 10; ++i) t.tick(Simulation::FIXED_STEP);
	t.cmd_delete(0, &scout, 1);
	for (int i = 0; i < 10; ++i) t.tick(Simulation::FIXED_STEP);
	const char *path = "/tmp/rtso_test_seen_mask.sav";
	CHECK(t.save_state(path), "save");
	static Simulation l;
	CHECK(l.load_state(path), "load");
	std::remove(path);
	FogMirages fresh;
	fresh.update(l, 0);
	const FoggedBuilding *g = find_b(fresh, far);
	std::printf("after load: scouted enemy base drawn %d as mirage %d\n", g != nullptr, g && g->miraged);
	CHECK(g && g->miraged, "a structure seen before the save stays a mirage after the load");
}

int main() {
	test_building_mirages();
	test_resource_mirages();
	test_treasure_mirages();
	test_los_bitmap();
	test_real_sim();
	test_giant_cost();
	test_shared_sight_and_winner();
	test_seen_mask();
	if (failures) {
		std::printf("%d FAILED\n", failures);
		return 1;
	}
	std::printf("all fog/mirage tests passed\n");
	return 0;
}
