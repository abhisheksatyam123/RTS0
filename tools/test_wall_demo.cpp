// 0 A.D. 0.28's Wall Demo random map (maps/random/wall_demo.js, keyword "demo") and its trigger
// script (wall_demo_triggers.js: SetLosRevealAll(-1, true)), with rmgen-common wall_builder.js's
// placers (wall_builder.h, Simulation::place_wall and the rest).
//
// Checks
//   1. g_WallStyles: the 18 styles in loadWallsetsFromCivData's order, with their elements as
//      getWallElement gives them (stone starts and ends are towers, palisade corners are curves,
//      structures 3.5 towers in, outposts 3 out).
//   2. Each placer, style by style at the map's Giant positions, places as many entities as 0.28's
//      script does there (counts worked out from wall_builder.js by hand-porting it to Python):
//      placeWall 24, placeFortress("tiny") 24, placeCircularWall, placePolygonalWall and
//      placeLinearWall the counts below; on a Normal map the last tiny fortress loses the 12
//      pieces past the map's passable edge.
//   3. The generated map at Giant size: every section's total, every style in every section, the
//      civ structures of each style's civ, 90 obelisks, everything Gaia's and inside the passable
//      map, nothing else (no player entities, resources or animals), the same map from the same
//      seed.
//   4. The map is revealed for every player once the game starts (OnInitGame).
//   g++ -std=c++20 -O2 -Isrc tools/test_wall_demo.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//       src/network/net_host.cpp -lpthread -o /tmp/test_wall_demo && /tmp/test_wall_demo
#include "simulation/simulation.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace rtso;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { std::printf("FAIL %s:%d %s: ", __FILE__, __LINE__, #cond); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } } while (0)

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr float kGiantHalf = 256.0f;
constexpr int kStyles = WALL_STYLE_COUNT; // wallStyleList.length

// The style a WallSet piece belongs to, by its WallSetKind and template civ; WALL_STYLE_COUNT
// for a structure.
uint8_t piece_style(const Simulation::Building &b) {
	const bool piece = b.type == BLD_WALL_SEGMENT || b.type == BLD_WALL_TOWER || is_gate_type(b.type) ||
	                   b.type == BLD_PALISADE || b.type == BLD_SIEGE_WALL;
	if (!piece) return WALL_STYLE_COUNT;
	const uint8_t kind = wallset_kind_of(b.type);
	for (uint8_t s = 0; s < WALL_STYLE_COUNT; ++s) {
		const WallStyleInfo &w = wall_style_info(s);
		if (w.kind == kind && w.civ == b.civ) return s;
	}
	return WALL_STYLE_COUNT;
}

// A flat, empty Giant map to run the placers on.
void flat_map(Simulation &s, float half) {
	s.reset();
	s.set_map_half(half);
	const int res = 129;
	s.set_terrain_data(std::vector<float>((size_t)res * res, 0.5f), res, 0.0f);
	s.set_circular_map(false);
}
} // namespace

int main() {
	// 1. g_WallStyles and getWallElement.
	{
		static const char *kOrder[WALL_STYLE_COUNT] = {"palisade", "athen/stone", "brit/stone", "cart/short", "cart/stone",
			"gaul/stone", "germ/stone", "han/stone", "iber/stone", "kush/stone", "mace/stone", "maur/stone", "pers/stone",
			"ptol/stone", "rome/stone", "rome/siege", "sele/stone", "spart/stone"};
		for (uint8_t st = 0; st < WALL_STYLE_COUNT; ++st) {
			CHECK(std::strcmp(wall_style_info(st).key, kOrder[st]) == 0, "style %d is %s", st, wall_style_info(st).key);
			CHECK(wall_style_by_key(kOrder[st]) == st, "%s", kOrder[st]);
		}
		auto el = [](uint8_t st, const char *name) { return wall_element<double>(st, wall_element_name(name)); };
		const WallElement tower = el(WALL_STYLE_IBER_STONE, "tower");
		CHECK(std::fabs(tower.length - 2.5) < 1e-9 && tower.building == BLD_WALL_TOWER && tower.civ == CIV_IBERIANS, "iber tower");
		CHECK(std::fabs(wall_overlap<double>(WALL_STYLE_IBER_STONE) - 0.5) < 1e-9, "iber overlap %f", wall_overlap<double>(WALL_STYLE_IBER_STONE));
		const WallElement start = el(WALL_STYLE_ATHEN_STONE, "start"), end = el(WALL_STYLE_ATHEN_STONE, "end");
		CHECK(start.building == BLD_WALL_TOWER && end.building == BLD_WALL_TOWER, "stone start/end are towers");
		const WallElement pal_start = el(WALL_STYLE_PALISADE, "start"), pal_in = el(WALL_STYLE_PALISADE, "cornerIn");
		CHECK(pal_start.piece == WALL_PIECE_END && std::fabs(pal_start.angle - 2.5 * kPi) < 1e-9, "palisade start: palisades_end turned");
		CHECK(pal_in.piece == WALL_PIECE_CURVE && std::fabs(pal_in.bend - kPi / 2) < 1e-9 && std::fabs(pal_in.length - 2.0) < 1e-9,
				"palisade cornerIn: the curve");
		const WallElement corner_out = el(WALL_STYLE_ATHEN_STONE, "cornerOut");
		CHECK(corner_out.length == 0.0 && std::fabs(corner_out.indent + 1.75 / 4) < 1e-9 && std::fabs(corner_out.bend + kPi / 2) < 1e-9,
				"stone cornerOut");
		const WallElement house = el(WALL_STYLE_PALISADE, "house"), outpost = el(WALL_STYLE_GAUL_STONE, "outpost");
		CHECK(house.building == BLD_HOUSE && house.civ == CIV_ATHENIANS && std::fabs(house.indent - 0.75 * 3.5) < 1e-9,
				"a palisade style's house is athen/house");
		CHECK(outpost.building == BLD_OUTPOST && outpost.civ == CIV_GAULS && std::fabs(outpost.indent + 2.0 * 3.0) < 1e-9, "gaul outpost");
		const WallElement entry_tower = el(WALL_STYLE_PALISADE, "entryTower"), rome_tower = el(WALL_STYLE_ROME_STONE, "entryTower");
		CHECK(entry_tower.building == BLD_PALISADE && entry_tower.civ == CIV_GAIA, "palisade entryTower: palisades_watchtower");
		CHECK(rome_tower.building == BLD_TOWER && rome_tower.civ == CIV_ROMANS && std::fabs(rome_tower.length - 9.0) < 1e-9,
				"rome entryTower: rome/defense_tower");
		CHECK(el(WALL_STYLE_ROME_SIEGE, "fort").building == BLD_ARMY_CAMP, "siege fort: army camp");
		CHECK(el(WALL_STYLE_ATHEN_STONE, "entry").piece == WALL_PIECE_NONE, "entry has no template");
		const WallElement gap = el(WALL_STYLE_ATHEN_STONE, "gap_3"), turn = el(WALL_STYLE_ATHEN_STONE, "turn_0.25");
		CHECK(!gap.placed() && gap.length == 3.0 && !turn.placed() && std::fabs(turn.bend - kPi / 4) < 1e-9, "gap_ and turn_");
		CHECK(wall_fortress_type("tiny").size() == 24 && wall_fortress_type("giantPalisades").size() > 56 &&
				wall_fortress_type("nope").empty(), "createDefaultFortressTypes");
	}

	// 2. The placers style by style, at wall_demo.js's Giant positions.
	{
		static const int kCircular[WALL_STYLE_COUNT] = {31, 19, 21, 31, 16, 24, 13, 19, 27, 13, 19, 24, 13, 19, 24, 13, 19, 24};
		static const int kPolygonal[WALL_STYLE_COUNT] = {25, 13, 17, 41, 13, 15, 9, 13, 17, 21, 25, 15, 13, 13, 17, 21, 25, 15};
		static const int kLinear[WALL_STYLE_COUNT][2] = {{87, 173}, {35, 67}, {31, 63}, {87, 173}, {31, 61}, {33, 65}, {33, 63},
			{33, 63}, {35, 69}, {33, 65}, {35, 67}, {35, 67}, {35, 67}, {33, 65}, {33, 63}, {35, 69}, {39, 75}, {35, 67}};
		const double size = 2.0 * kGiantHalf, buildable = size - 10.0, col = buildable / kStyles, r = (col - 10.0) / 2.0;
		auto w = [&](double v) { return v - kGiantHalf; };
		const std::vector<WallElementName> custom = wall_element_names({"start", "long", "tower", "tower", "tower", "medium",
				"outpost", "medium", "cornerOut", "medium", "cornerIn", "medium", "house", "end", "entryTower", "start", "short",
				"barracks", "gate", "tower", "medium", "fort", "medium", "end"});
		Simulation s;
		int fails = 0;
		for (uint8_t st = 0; st < WALL_STYLE_COUNT; ++st) {
			flat_map(s, kGiantHalf);
			const double x = 5.0 + st * col;
			const size_t n_wall = s.place_wall(w(x), w(5.0), custom, st, GAIA_PLAYER, kPi / 16 * std::sin(st * kPi / 4));
			const size_t n_tiny = s.place_fortress(w(x + 15.0), w(110.0), "tiny", st, GAIA_PLAYER, st * kPi / 32);
			const size_t n_circ = s.place_circular_wall(w(x + r), w(135.0 + col + r), r, wall_element_names({"tower", "medium", "house"}),
					st, GAIA_PLAYER, st * kPi / 16, kPi / 2 * (st % 3 + 2));
			const size_t n_poly = s.place_polygonal_wall(w(x + r), w(135.0 + 2.0 * col + r),
					r, wall_element_names({"medium", "tower"}), wall_element_name("tower"), st, GAIA_PLAYER, st * kPi / 16, st % 6 + 3, true);
			const double ly = 135.0 + 4.0 * col, max_len = size - ly - 15.0;
			size_t n_lin[2];
			for (int k = 0; k < 2; ++k) {
				const double lx = 5.0 + (st * 2 + k) * buildable / kStyles / 2;
				n_lin[k] = s.place_linear_wall(w(lx), w(ly), w(lx), w(ly + (k + 1) * max_len / 2), wall_element_names({"tower", "medium"}),
						st, GAIA_PLAYER);
			}
			const bool ok = n_wall == 24 && n_tiny == 24 && (int)n_circ == kCircular[st] && (int)n_poly == kPolygonal[st] &&
			                (int)n_lin[0] == kLinear[st][0] && (int)n_lin[1] == kLinear[st][1];
			CHECK(ok, "%s: placeWall %zu, tiny %zu, circular %zu (0.28 %d), polygonal %zu (%d), linear %zu/%zu (%d/%d)",
					wall_style_info(st).key, n_wall, n_tiny, n_circ, kCircular[st], n_poly, kPolygonal[st], n_lin[0], n_lin[1],
					kLinear[st][0], kLinear[st][1]);
			fails += !ok;
			if (st == WALL_STYLE_ATHEN_STONE) {
				// The tiny fortress alone: 4 gates, 8 short walls and 12 towers (4 of them corners).
				flat_map(s, kGiantHalf);
				s.place_fortress(0.0, 0.0, "tiny", st, GAIA_PLAYER, 0.0);
				int gates = 0, shorts = 0, towers = 0;
				for (const auto &b : s.buildings()) {
					gates += b.type == BLD_GATE;
					shorts += b.type == BLD_WALL_SEGMENT && std::fabs(b.half_size - 1.5f) < 1e-3f;
					towers += b.type == BLD_WALL_TOWER;
				}
				CHECK(gates == 4 && shorts == 8 && towers == 12, "athen tiny fortress: %d gates, %d short, %d towers", gates, shorts, towers);
			}
		}
		std::printf("placers at the Giant positions: %d of %d styles as 0.28\n", WALL_STYLE_COUNT - fails, WALL_STYLE_COUNT);
		// A Normal map: the last style's tiny fortress reaches past the passable edge.
		flat_map(s, 128.0f);
		const double col256 = (256.0 - 10.0) / kStyles;
		const size_t n = s.place_fortress(5.0 + 17 * col256 + 15.0 - 128.0, 110.0 - 128.0, "tiny", WALL_STYLE_SPART_STONE,
				GAIA_PLAYER, 17 * kPi / 32);
		CHECK(n == 12, "Normal map, spart tiny fortress: %zu pieces (0.28 12)", n);
	}

	// 3. The map at Giant size.
	{
		Simulation s;
		s.generate_random_map(MAP_WALL_DEMO, 42, 2, kGiantHalf);
		CHECK(!s.circular_map(), "Wall Demo is square (CircularMap false)");
		// Sections in wall_demo.js's order: an obelisk goes down before each fortress, circle and
		// polygon (ids count up from one counter), so the obelisks before a piece say which
		// section, and in it which style, laid it. The linear walls come after the last
		// irregular polygon, above it (wall_demo.js's position.y, tiles from the bottom edge).
		std::vector<uint32_t> obelisk_ids;
		for (const auto &sc : s.scenery()) obelisk_ids.push_back(sc.id);
		std::sort(obelisk_ids.begin(), obelisk_ids.end());
		const double col = (2.0 * kGiantHalf - 10.0) / kStyles;
		const double linear_y = 135.0 + 4.0 * col - 4.0; // the irregular polygons reach up to 9 tiles below it
		auto section_of = [&](const Simulation::Building &b, int &style) {
			const int before = (int)(std::lower_bound(obelisk_ids.begin(), obelisk_ids.end(), b.id) - obelisk_ids.begin());
			style = before > 0 ? (before - 1) % WALL_STYLE_COUNT : -1;
			if (before == 0) return 0;
			if (before == (int)obelisk_ids.size() && b.z + kGiantHalf >= linear_y) return 6;
			return 1 + (before - 1) / WALL_STYLE_COUNT;
		};
		static const char *kSection[7] = {"placeWall", "placeFortress", "placeGenericFortress", "placeCircularWall",
			"placePolygonalWall", "placeIrregularPolygonalWall", "placeLinearWall"};
		int count[7] = {}, styled[7][WALL_STYLE_COUNT] = {}, outside = 0, not_gaia = 0, houses_by_civ = 0, houses = 0;
		int generic_towers[WALL_STYLE_COUNT] = {}, generic_walls[WALL_STYLE_COUNT] = {}, custom_house_civs[CIV_COUNT] = {};
		for (const auto &b : s.buildings()) {
			int by_obelisk = -1;
			const int sec = section_of(b, by_obelisk);
			++count[sec];
			const uint8_t st = piece_style(b);
			if (st < WALL_STYLE_COUNT) ++styled[sec][st];
			if (sec >= 1 && sec <= 5 && st < WALL_STYLE_COUNT)
				CHECK(st == by_obelisk, "%s: a %s piece after %s's obelisk", kSection[sec], wall_style_info(st).key,
						wall_style_info((uint8_t)by_obelisk).key);
			if (sec == 2 && st < WALL_STYLE_COUNT) {
				const bool tower = b.type == BLD_WALL_TOWER || is_wallset_tower_piece(b.type, b.half_size);
				generic_towers[st] += tower;
				generic_walls[st] += !tower;
			}
			outside += std::fabs(b.x) >= kGiantHalf - 3.0f || std::fabs(b.z) >= kGiantHalf - 3.0f;
			not_gaia += b.team != GAIA_PLAYER;
			if (b.type == BLD_HOUSE) {
				++houses;
				if (sec == 0 && b.civ < CIV_COUNT) ++custom_house_civs[b.civ];
			}
		}
		// The custom walls' houses: each style's structures/{civ}/house (athen's for the palisades).
		int want_house_civs[CIV_COUNT] = {};
		for (uint8_t st = 0; st < WALL_STYLE_COUNT; ++st) ++want_house_civs[wall_style_info(st).structure_civ];
		for (int c = 0; c < CIV_COUNT; ++c) houses_by_civ += custom_house_civs[c] == want_house_civs[c];
		std::printf("Wall Demo, Giant: %zu structures, %zu scenery\n", s.buildings().size(), s.scenery().size());
		for (int k = 0; k < 7; ++k) {
			int styles = 0;
			for (int st = 0; st < WALL_STYLE_COUNT; ++st) styles += styled[k][st] > 0;
			std::printf("  %-28s %5d pieces, %d styles\n", kSection[k], count[k], styles);
			CHECK(styles == WALL_STYLE_COUNT, "%s: %d of the styles", kSection[k], styles);
		}
		// wall_demo.js at 512 tiles: 18 x 24 custom walls, 18 x 24 tiny fortresses, 369 circular,
		// 328 polygonal and 2120 linear wall pieces (section 2); the generic fortresses close on
		// at least four points each (a wall or gate and a tower a point), the irregular polygons
		// have their 3 to 8 corner towers and walls between.
		CHECK(count[0] == 432, "placeWall: %d (0.28 432)", count[0]);
		CHECK(count[1] == 432, "placeFortress: %d (0.28 432)", count[1]);
		CHECK(count[2] >= WALL_STYLE_COUNT * 8 && count[2] % 2 == 0, "placeGenericFortress: %d", count[2]);
		CHECK(count[3] == 369, "placeCircularWall: %d (0.28 369)", count[3]);
		CHECK(count[4] == 328, "placePolygonalWall: %d (0.28 328)", count[4]);
		CHECK(count[5] > 3 * (3 + 4 + 5 + 6 + 7 + 8), "placeIrregularPolygonalWall: %d", count[5]);
		CHECK(count[6] == 2120, "placeLinearWall: %d (0.28 2120)", count[6]);
		for (int st = 0; st < WALL_STYLE_COUNT; ++st) // each generic fortress: as many towers as walls and gates
			CHECK(generic_towers[st] >= 4 && generic_towers[st] == generic_walls[st], "%s generic fortress: %d towers, %d walls",
					wall_style_info((uint8_t)st).key, generic_towers[st], generic_walls[st]);
		std::printf("  generic fortress points:");
		for (int st = 0; st < WALL_STYLE_COUNT; ++st) std::printf(" %d", generic_towers[st]);
		std::printf("\n");
		CHECK(outside == 0, "%d pieces outside the passable map", outside);
		CHECK(not_gaia == 0, "%d pieces not Gaia's", not_gaia);
		// A house in each custom wall (structures/{civ}/house, athen's for the palisade style) and
		// one in each circular wall's every part.
		CHECK(houses_by_civ == CIV_COUNT && houses > WALL_STYLE_COUNT, "custom-wall houses of the wrong civ (%d civs right), %d houses",
				houses_by_civ, houses);
		int obelisks = 0;
		for (const auto &sc : s.scenery()) obelisks += sc.type == scenery_type_by_name("obelisk") && sc.owner == GAIA_PLAYER;
		CHECK(obelisks == 5 * WALL_STYLE_COUNT && s.scenery().size() == (size_t)obelisks, "%d obelisks of %zu scenery", obelisks, s.scenery().size());
		CHECK(s.units().count == 0 && s.resources().count == 0 && s.fauna().count == 0, "%zu units, %zu resources, %zu animals",
				s.units().count, s.resources().count, s.fauna().count);
		int structures[BLD_TYPE_COUNT] = {};
		for (const auto &b : s.buildings()) ++structures[b.type];
		std::printf("  fortresses %d, army camps %d, barracks %d, outposts %d, defense towers %d, sentry towers %d, houses %d\n",
				structures[BLD_FORTRESS], structures[BLD_ARMY_CAMP], structures[BLD_BARRACKS], structures[BLD_OUTPOST],
				structures[BLD_TOWER], structures[BLD_SENTRY_TOWER], structures[BLD_HOUSE]);
		CHECK(structures[BLD_FORTRESS] == 16 && structures[BLD_ARMY_CAMP] == 1 && structures[BLD_SENTRY_TOWER] == 1 &&
				structures[BLD_BARRACKS] == WALL_STYLE_COUNT && structures[BLD_OUTPOST] == WALL_STYLE_COUNT &&
				structures[BLD_TOWER] == WALL_STYLE_COUNT - 1, "the custom walls' structures");

		Simulation again;
		again.generate_random_map(MAP_WALL_DEMO, 42, 2, kGiantHalf);
		bool same = again.buildings().size() == s.buildings().size();
		for (size_t k = 0; same && k < s.buildings().size(); ++k) {
			const auto &a = s.buildings()[k], &b = again.buildings()[k];
			same = a.type == b.type && a.x == b.x && a.z == b.z && a.rot == b.rot && a.half_size == b.half_size && a.civ == b.civ;
		}
		CHECK(same, "the same seed lays a different map");

		// 4. wall_demo_triggers.js: SetLosRevealAll(-1, true) as the game starts.
		CHECK(!s.reveal_map(), "revealed before the game starts");
		s.set_fow_viewer(1);
		for (int k = 0; k < 8; ++k) s.tick(Simulation::FIXED_STEP); // tick_fow runs every few ticks
		CHECK(s.reveal_map(), "Wall Demo's map is not revealed");
		bool all = true;
		for (uint8_t p = 0; p < MAX_PLAYERS; ++p)
			for (float x : {-250.0f, 0.0f, 250.0f})
				for (float z : {-250.0f, 0.0f, 250.0f}) all &= s.los_visible(p, x, z) && s.los_explored(p, x, z);
		const int res = s.fow_res();
		// CCmpRangeManager's m_LosStateRevealed leaves the off-world edge hidden (LosIsOffWorld).
		const float cell = 2.0f * kGiantHalf / (float)res;
		for (int c = 0; c < res * res; ++c) {
			const float x = ((float)(c % res) + 0.5f) * cell - kGiantHalf, z = ((float)(c / res) + 0.5f) * cell - kGiantHalf;
			if (s.off_world(x, z)) continue;
			all &= s.fow_grid(0)[c] == Simulation::FOW_VISIBLE && s.fow_grid(1)[c] == Simulation::FOW_VISIBLE;
		}
		CHECK(all, "a cell is hidden from a player");
	}

	if (failures) {
		std::printf("RESULT: FAIL (%d failed checks)\n", failures);
		return 1;
	}
	std::printf("RESULT: PASS\n");
	return 0;
}
