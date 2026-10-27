// RTS0 building steering test (headless: no SCons, no Godot).
// Checks that land units get around buildings with the short-path steering in move_units
// (src/simulation/nav/building_steering.h, which uses the VertexPathfinder port).
// Build and run:
//   g++ -std=c++20 -O2 -Isrc tools/test_building_steering.cpp src/simulation/simulation.cpp \
//     src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//     src/network/net_host.cpp -lpthread -o /tmp/test_steer && /tmp/test_steer
//
//  1. Guard behind a wall: a guard order moves straight at its target (no long path). The target
//     stands behind a wall of segments, so the guard has to walk around the wall's end.
//  2. Gather around houses: a gather order also moves straight at the wood pile, and a row of
//     houses stands between the villagers and the pile.
//  3. Move through a gap: a move order along a long path through a gap in a row of barracks.
//  4. Gather behind a long wall: the wall is too long for the short-path range, so the gatherer
//     needs the long path move_units requests when buildings block a GATHER/BUILD/RETURN_DROPSITE
//     unit's straight line.
//  5. Walk across fields: fields don't block movement (0 A.D. BlockMovement=false), so a unit
//     walks straight over a row of them instead of around.
//  6. Out of a house cluster: a catapult among houses, whose long path starts on navcells in
//     2-unit gaps it can't stand in, moves on past those waypoints instead of pushing at them for good.
//  7. Walk past a turned barracks: turned 45 degrees, its corners reach 41% further along the axes
//     than an unturned one's sides (nav/footprint.h). A unit walking along that axis must go around
//     the corners, not the unturned square, and a woman finishes a turned house. A long, thin
//     footprint (0 A.D.'s long wall, 36 x 5 m) lies along its width, and a quarter turn swaps its
//     width and depth in every footprint test and in the nav grid. Turned any way, the wall blocks
//     every navcell along it, so no long path goes through it.
//  8. Siege through a gap: a catapult keeps 0 A.D.'s "large" clearance (3 m, unit_building_clearance)
//     from buildings, so it takes a 2-unit (8 m) gap in a long row of houses instead of going around.
//  9. Walk into a building: a move to a point inside a town centre ends beside it, as close as the
//     unit can stand (0 A.D. MakeGoalReachable), instead of pushing at its wall for good.
// 10. Walk given again every tick: a long path leaves out the navcell the unit stands in (0 A.D.
//     LongPathfinder), so a unit given a new path every tick, as Petra sends its scout home, still
//     gets there instead of heading back to its navcell's centre each time.
// 11. Walk to one point: twelve units each sent alone to the same point stop within 12 m (3 units)
//     of it once the others obstruct them (0 A.D. UnitAI DefaultRelaxedMaxRange), instead of
//     pushing for the point for good.
// 12. Ship sent ashore: a point a ship can't reach gives way to the nearest one it can
//     (MakeGoalReachable), so the ship stops at the shore instead of sailing onto the land.
// 13. Walk into a closed gate: an enemy's long path goes through an unlocked gate, which then
//     stops it dead; the walk ends after 35 turns (7 s) of failed movement (0 A.D. UnitMotion
//     MAX_FAILED_MOVEMENTS), instead of pushing at the gate for good.
// 14. Crowds at the shore: ships packed against the shore and soldiers packed at the water's edge
//     push each other apart, but never onto terrain they can't cross (0 A.D. pushing checks
//     CheckMovement), so no ship runs aground and no soldier is pushed out to sea.
// 15. Cliffs: a cliff steeper than 0 A.D.'s MaxTerrainSlope (pathfinder.xml, 1.0 for every land
//     class) splits the map but for one ramp. A swordsman, a horseman and a gatherer all go round
//     by the ramp instead of climbing the cliff, and no house can be placed on it.
// 16. Chases: an attacker paths round water and cliffs as 0 A.D. UnitMotion paths every move, and
//     gives up a target it can't reach (UnitAI MovementUpdate with likelyFailure) instead of wading
//     across the sea after it or climbing a cliff. A player's attack on a unit or a house across the
//     sea ends at the shore; a soldier's own fight with an enemy across a channel is dropped at the
//     water's edge; a player's attack on a unit up a cliff goes round by the ramp and gets there.
// 17. Large units through a narrow gap: chariots, elephant heroes and catafalques take 0 A.D.'s
//     "large" class (3 m clearance) like siege and elephants, by their template's classes. A
//     swordsman walks straight through a 1.2-unit (4.8 m) gap between houses; an elephant hero
//     and a scythed chariot can't fit and walk round the houses instead.
// 18. Walk to a structure's range: a unit sent into a town centre heads for the side it comes
//     from, aiming just outside its clearance, and gets in; builders stop as soon as the foundation
//     is within build range and stay there once it is finished (0 A.D. UnitMotion MoveToTargetRange
//     ends in range, and its goal reaches past the unit's clearance, so any range is reached).
// 19. Mines: a large mine's 13 m square (template_gaia_rock_large Obstruction/Static) blocks
//     movement as a structure's footprint does, and long paths on the navcells it covers whole. A swordsman sent through one walks
//     round it, one sent along a row of them takes the gaps between, and six women sent to gather
//     from one all reach it and work it from round its edge.
// 20. Shore clearance: 0 A.D. grows deep water by each land class's clearance (0.8 m; 3 m for siege
//     and elephants, CCmpPathfinder ExpandImpassableCells). An elephant sent to a spot on the beach
//     2 m off the water stops 3 m off it, where a swordsman gets to the spot, and a clump of elephants
//     on the beach pushes apart along it, never out to within 3 m of the water.
// 21. Trees, bushes and treasures: a 6 m baobab (gaia/tree/baobab_3_mature) blocks movement, so a
//     swordsman sent through it walks round; one sent through a wood of oaks 4 m apart winds
//     between them; women sent to a tree in that wood gather from it; and a soldier sent to a
//     7 m treasure (half-buried barrels) collects it from beside it.
// 22. A circular map's edge: no unit walks, is pushed or spawns past the impassable edge 12 m
//     inside the circle (0.28 MAP_EDGE_TILES) less its class clearance, and no foundation reaches past it.
// 24. Repair in a house cluster: Petra packs houses corner to corner, so the side of a damaged house
//     a woman comes from can be shut by a neighbour touching it there. She repairs it from the
//     nearest open spot she can reach (0 A.D. MoveToTargetRange's goal is the whole range round
//     the house) instead of pushing at the shut one for good.
// 25. Cliff clearance: 0.28 takes each navcell's slope from its 4 m terrain tile and grows steep
//     tiles by the class clearance as it grows deep water. On a heightmap with a tile a unit, a
//     rock (one raised height sample, steep tiles inside a navcell that is not steep) is walked
//     round, never within the walker's clearance; an elephant and a swordsman sent to the foot of a
//     one-tile cliff stop 3 m and 0.8 m off it; a walk round an oak beside the rock always ends.
// 27. Thin ridges: a ridge of steep tiles narrower than a navcell, whose navcells aren't steep (soak:
//     workers and garrisons stood for good at the margin of such cliffs, stacked on one point). 0.28
//     plans long paths on the passability grid units move on, so a walk across it, a return to a
//     storehouse behind it and a garrison into a tower whose near side is its margin all go round
//     its end and get there; so does a return that still holds a path across it (laid before, or in
//     a saved game), which HandleObstructedMove plans again; and a spearman hunting a deer beyond it
//     reaches it along a path with waypoints closer than its reach, and kills it (MoveToTargetRange
//     measures the range to the animal, not to a waypoint).
// 28. A ridge the navcells miss, from the map's edge: a walk, a walk to a dropsite and a gatherer's
//     return across it all go round its one end, 50 units past them, instead of standing against it.
// 29. A pass the navcells close: a ridge across the whole map along a navcell corner line, so every
//     navcell along it is steep, but for a pass one tile wide whose tiles are level. The same three
//     walks take the pass, as 0.28's long path does, instead of stopping on the near side.
// 30. Large units and a one-tile gap: the thin ridge of 27 with a gap one tile wide. A swordsman
//     takes the gap; an elephant and a ram, whose 3 m clearance ("large", ExpandImpassableCells per
//     class) the gap doesn't leave, go round the ridge's end instead of standing in the gap for good.
// 31. Large units and a two-tile gap: 0.28's 3 m expansion of the steep tiles either side leaves 2 m
//     of an 8 m gap open. Elephants, rams and swordsmen given a player's walk on lines through the
//     gap off its middle all take the gap and get to their point, which stays where it was.
// 32. A tree in a one-tile gap: 0.28 rasterizes each tree, grown by the class clearance, onto the
//     long paths' navcells, so a tree in the gap of 30 shuts it. A swordsman and a horseman walking
//     across the ridge go round its end instead of into the tree (or through it); once it is felled,
//     through the gap.
// 33. A structure beside a one-tile gap (soak: map 16 seed 9, a horseman stood for good between steep
//     tiles and a structure's edge): a house across the gap's far end, on a navcell it only partly
//     covers, which stays open. No unit fits between it and the ridge, as 0.28's 1 m navcells show,
//     so walks across the gap go round the ridge's end instead of into the house.
// 34. The nodes after a load (soak: Sahara seed 3, a game saved at minute 0 and loaded went out of
//     sync): RmsGenerator's plant_flora gives palms and bushes their kind after placing them
//     (set_resource_flora), and with it their obstruction size. The nodes take the new size, as a
//     load of the same game does, so both copies hold the same tree list and open nodes.
// 35. A turned structure beside a cliff: a corridor between two walls of steep tiles and a house turned
//     45 degrees in it, its north corner leaving a gap to the north wall. The nodes close the house
//     grown by the class clearance and the steps that cut its corners, not a margin round every
//     face, so a swordsman and a ram walk through gaps the steering lets them through (as 0.28
//     does), instead of round the walls' ends.
// 36. Corner dances: walks on Islands by a Gaul javelineer that never ended, the walker stepping to
//     and fro for good, every turn a free move. Seed 11, from (40, -24) to (0, -55): beside a lake,
//     from one point its line to the point was blocked and it headed for a corner, from the next, 0.05
//     on, the line was clear and it dropped the corner. Seed 3 (every player on one team, as the soak
//     has it): the search from the corner of a wood found no way and the walker went back to where
//     it searched from. 0.28's long path goes round steep ground and trees, which its 1 m navcells
//     hold, and the unit follows its paths' waypoints one by one (CCmpUnitMotion.h:1238-1250,
//     1299-1326, 1503-1507): the short path the steering keeps round them stands in for that, so
//     these walks go round the lake and the wood. Seeds 7 and 4 (one team): walks past trees swung
//     between a tree's corner behind the walker and the spot it left, the way on from that corner still
//     crossing the tree: 0.28 rejects a short path towards the goal whose end is no nearer it
//     (CCmpUnitMotion.h:904-914 RejectFartherPaths, 932-933 and 944-951 when no long waypoint is
//     left), and a corner the unit decides again from is the whole of its path. Seed 1 (one team): the next waypoint of the long path lay inside a bush, and the walker
//     went from one corner of the bush in front of it to the other for good: 0.28's long path never
//     leads into a tree, and HandleObstructedMove drops a long waypoint the unit has come close to
//     (CCmpUnitMotion.h:1416-1426). Every walk ends, the first within 4 of its point and seed 7's
//     within 1; so does the walk from (88, 40) to (-59.83, 29.83) on seed 11.
// 37. Kept round trees, then obstructed: a swordsman beside an oak walks past a barracks, so the
//     short path round the barracks is searched with a tree round it and kept whole (a kept path's
//     memo key, nav::is_kept_path_key). An oak is then put on the leg it follows: the way there is
//     no longer clear, so it drops the path at once and decides again from where it stands, as 0.28's
//     HandleObstructedMove asks for a new short path (CCmpUnitMotion.h:1436-1441), and goes round the
//     oak to its point, never into it.
// 38. A waypoint inside a tree: a swordsman's path to (6, 3) runs through (0.62, 0.25), inside an oak
//     beside another one, where 0.28's long path never leads. Once within 4 of that waypoint it moves
//     on to the next (HandleObstructedMove drops a close long waypoint, CCmpUnitMotion.h:1416-1426)
//     instead of pressing into the oak towards it (or, as the walk of Islands seed 1 in 36 did,
//     walking between the corners of a bush in front of it for good), and gets to its point.
// 39. Straight to a near goal: 0.28 TryGoingStraightToTarget (CCmpUnitMotion.h:1503-1561) drops the
//     long path when the goal is within 24 m (DIRECT_PATH_RANGE) and the straight line to it passes
//     CheckMovement. Spearmen sent past turned houses, on lines that clear every footprint by more
//     than their 0.8 m clearance, walk those lines instead of by the long path's navcell centres
//     round a house's corner. A ram or an elephant on a line that passes within its 3 m of a house
//     fails CheckMovement, so it keeps its path round the house, never into it, and gets there. A
//     line that starts inside a house's clearance fails too, unless the house is the unit's target:
//     0.28 never lets a unit in there, and the line out would cut between that house and a
//     neighbour closer than twice the clearance.
// 40. Past the map's edge: CheckMovement fails a line whose end lies past the edge band, the map's
//     impassable edge grown by the clearance (TestLine's IsInWorld, CCmpObstructionManager.cpp:
//     890-892; CheckLineMovement, CCmpPathfinder.cpp:700-743 and :760-761). A walk that gets within
//     24 m of a point past it keeps its long path and ends where MakeGoalReachable ended that.
// 41. Set down beside a hemmed-in arsenal: CheckUnitPlacement (CCmpPathfinder.cpp:987-1008) tests a
//     spawn spot against the static squares grown by the unit's class Clearance (TestUnitShape,
//     CCmpObstructionManager.cpp:1095-1112), so a ram or an elephant is never set down within 3 m
//     of a structure. Houses stand 1-3 m off an arsenal's front and side, 2-5 m apart; each large
//     unit trained there starts clear of them and gets 15 units away in every direction, round
//     the houses where the gaps between them are too narrow for it.
// 42. Large units at the mouth of a narrow gap: two houses 4, 5 or 6 m apart, square or turned 0.4
//     rad. A ram or an elephant south of them walks first to a point inside both houses' 3 m
//     clearance at the gap's south mouth, or inside the east house's only, beside the mouth; then 8
//     units north. 0.28 lets it into neither (MakeGoalReachable moves such a goal to the nearest
//     navcell the class can stand on, HierarchicalPathfinder.cpp:689-719; PerformMove refuses a step
//     into a static square grown by the clearance, CCmpUnitMotion.h:1302 and :1343, and a push too,
//     CCmpUnitMotion_System.cpp:628-641), so it stops outside them and goes round the houses, never
//     between their facing faces. A third start puts it inside the west house's clearance by the gap
//     (as a house built beside it would): the push that keeps it off that house still moves it, and
//     it does not slide along the house's face through the gap.
// 43. Large units at a house cluster: a 3x3 block of houses 4, 5 or 6 m apart, square or turned 0.4
//     rad, as Petra packs them. A ram or an elephant attacks one of an enemy's houses, or a worker
//     elephant builds a foundation among its own, from 12 units off in each of 8 directions. 0.28
//     lets a large unit into no structure's square grown by its 3 m clearance but its target's
//     (PerformMove skips only the target, CCmpUnitMotion.h:1231-1236, :1302 and :1343; a push that
//     fails CheckMovement is dropped, CCmpUnitMotion_System.cpp:628-641), and MakeGoalReachable
//     (HierarchicalPathfinder.cpp:689-719) moves the point it walks to off those squares. So it
//     never stands inside two other houses' grown squares, and it starts its task at every outer
//     house, each of which has a face open to the outside.
// 44. Large units across a row of houses: nine houses in a row 4, 5 or 6 m apart, square or turned
//     0.4 rad. A ram or an elephant 6 units south of the row is sent 6 units north of it. 0.28's
//     long path for the class closes every structure grown by 3 m (RasterizeRectWithClearance,
//     Rasterize.cpp:29-48), as CheckMovement does (CCmpPathfinder.cpp:971-985), so the path goes
//     round the row's end, never between two houses, and the unit gets there.
// 45. Orders after a load: a game and the same game saved and loaded (or joined by a client that
//     rejoins) must stay hash-identical. A ram's, an elephant's or a worker elephant's attack or build
//     order at the cluster of 43 sets its stand point from the structures round the target
//     (large_stand_point); given before move_units has listed this tick's steering obstacles (a turn's
//     commands, an order a unit's queue hands on), it reads them from what the save holds
//     (Simulation::for_each_structure_square), not from the last tick's list, which a loaded game
//     lacks. The stand point, the unit's place 10 s on and the lockstep hash are the same in both.
// 46. A field's supply and a felled tree in one tick, then a load: a finished field's supply is appended
//     at the start of tick_gather (sync_field_resources), and a tree felled in that tick_gather is
//     removed at its end by moving the last supply, the field's, into its place, so the supply count and
//     the end ids are as before. The nodes' tree list (sync_tile_obstructions) must still drop the tree,
//     as a copy loaded from a save made then lists the trees afresh: both copies hold the same gaia tile
//     obstructions and nodes, and stay hash-identical.
// 47. find_path after a failed narrow-gap re-plan: a ram's long path to a point across a deep channel,
//     which no land unit reaches, runs on the navcells through a 4 m gap between two houses, so the
//     large class plans it again on its nodes (NavGrid::path_through_narrow_gap). With the nodes'
//     regions, MakeGoalReachable moves the goal to the bank and the re-plan gets there round the houses.
//     Without them the re-plan fails, and find_path returns false with the re-plan's own path, as 0.28's
//     long path for the class has no other class to fall back on (HierarchicalPathfinder.cpp:689-719),
//     not the default class's, which goes through the gap.
//  For every scenario the unit bodies must never overlap a footprint that blocks movement by more
//  than MAX_PENETRATION (separation() alone allows deep overlap when a unit walks head-on into a wall).
//  Exit: 0 = PASS, 1 = FAIL. Prints the measurements either way.
// Private access only to turn a standing building (Building::set_rot) and rasterize it, to ask
// direct_walk_clear (scenario 39), to compare a game's nodes and units with its loaded copy's
// (scenarios 34, 45 and 46), and to plan a path on the nodes without their regions (scenario 47).
#define private public
#include "simulation/simulation.h"
#include "simulation/nav/building_steering.h"
#undef private
#include "network/turn_manager.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace rtso;

static constexpr float DT = 0.05f;
static constexpr float MAX_PENETRATION = 0.25f; // world units of body overlap with a footprint

static int g_failures = 0;
static void check(bool ok, const char *what) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", what);
		++g_failures;
	}
}

static void setup(Simulation &s) {
	s.set_sim_seed(4242);
	s.reset();
	// Human seats: Petra would defend its passive targets and walk them home (defenseArmy.js clear).
	for (uint8_t p = 0; p < 2; ++p) s.player(p).active = 1, s.player(p).is_ai = 0;
	// Far-off town centers for both sides keep the match running (tick() stops once it ends).
	s.spawn_building_world(-52.0f, -52.0f, BLD_TOWN_CENTER, 0, 1.0f);
	s.spawn_building_world(52.0f, 52.0f, BLD_TOWN_CENTER, 1, 1.0f);
}

// Deepest overlap of any live team-0 unit's building clearance with any building footprint, turned
// by its rot: its half width along u and half depth along v.
static float max_penetration(const Simulation &s) {
	const Simulation::UnitView u = s.units();
	float worst = 0.0f;
	for (size_t i = 0; i < u.count; ++i) {
		if (!u.alive[i] || u.team[i] != 0) continue;
		const float r = unit_building_clearance(u.tmpl[i], u.unit_type[i]);
		for (const auto &b : s.buildings()) {
			if (b.wrecked || !Simulation::blocks_movement_now(b)) continue; // an uncommitted foundation blocks nothing
			// In the footprint's own axes, u = (cos a, -sin a) and v = (sin a, cos a) (0 A.D.
			// GetObstructionSquare), worked out here rather than taken from nav/footprint.h.
			const float c = std::cos(b.rot), sn = std::sin(b.rot);
			const float dx = u.pos_x[i] - b.x, dz = u.pos_z[i] - b.z;
			const float ex = b.half_size + r - std::fabs(dx * c - dz * sn);
			const float ez = b.half_d + r - std::fabs(dx * sn + dz * c);
			if (ex > 0.0f && ez > 0.0f) worst = std::max(worst, std::min(ex, ez));
		}
	}
	return worst;
}

static int index_of(const Simulation &s, uint32_t id) {
	const Simulation::UnitView u = s.units();
	for (size_t i = 0; i < u.count; ++i)
		if (u.id[i] == id) return static_cast<int>(i);
	return -1;
}

// 1. Guard behind a wall.
static void guard_behind_wall() {
	Simulation s;
	setup(s);
	// Wall segments are 2x2; cells 0 x [-5,4] make a wall at x in [0,2], z in [-10,10].
	for (int cy = -5; cy <= 4; ++cy) s.spawn_building(0, cy, BLD_WALL_SEGMENT, 0, 1.0f);
	const uint32_t target = s.spawn_unit(8.0f, 0.0f, 0, UNIT_SPEARMAN, 150.0f);
	const uint32_t guard = s.spawn_unit(-8.0f, 0.0f, 0, UNIT_SWORDSMAN, 160.0f);
	s.order_guard(&guard, 1, target);
	int arrived = -1;
	float pen = 0.0f;
	for (int t = 0; t < 1200 && arrived < 0; ++t) {
		s.tick(DT);
		pen = std::max(pen, max_penetration(s));
		const int gi = index_of(s, guard), ti = index_of(s, target);
		if (gi < 0 || ti < 0) break;
		const Simulation::UnitView u = s.units();
		const float dx = u.pos_x[gi] - u.pos_x[ti], dz = u.pos_z[gi] - u.pos_z[ti];
		const float reach = s.guard_reach((size_t)gi, (size_t)ti) + 0.2f; // UnitAI guardRange, edge to edge
		if (dx * dx + dz * dz <= reach * reach) arrived = t;
	}
	std::printf("  guard behind wall: arrived after %d ticks (-1 = never in 1200), max penetration %.3f\n", arrived, pen);
	check(arrived >= 0, "guard never reached its target behind the wall");
	check(pen <= MAX_PENETRATION, "guard overlapped a wall segment");
}

// 2. Gather around houses.
static void gather_around_houses() {
	Simulation s;
	setup(s);
	s.spawn_building_world(-24.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
	// Houses are 2x2; a touching row at x = -13, z in [-7, 7].
	for (int k = -3; k <= 3; ++k) s.spawn_building_world(-13.0f, 2.0f * k, BLD_HOUSE, 0, 1.0f);
	const uint32_t wood = s.spawn_resource_world(-4.0f, 0.0f, RES_WOOD, 5000);
	std::vector<uint32_t> vills;
	for (int k = 0; k < 3; ++k) vills.push_back(s.spawn_unit(-19.0f, -1.5f + 1.5f * k, 0, UNIT_VILLAGER, 100.0f));
	s.order_gather(vills.data(), vills.size(), wood);
	int arrived = -1;
	float pen = 0.0f;
	for (int t = 0; t < 1200 && arrived < 0; ++t) {
		s.tick(DT);
		pen = std::max(pen, max_penetration(s));
		const Simulation::UnitView u = s.units();
		int at_pile = 0;
		for (uint32_t id : vills) {
			const int i = index_of(s, id);
			if (i < 0) continue;
			const float dx = u.pos_x[i] + 4.0f, dz = u.pos_z[i];
			if (dx * dx + dz * dz <= 3.0f * 3.0f) ++at_pile;
		}
		if (at_pile == (int)vills.size()) arrived = t;
	}
	std::printf("  gather around houses: all 3 at the wood pile after %d ticks (-1 = never in 1200), max penetration %.3f\n", arrived, pen);
	check(arrived >= 0, "villagers never reached the wood pile behind the houses");
	check(pen <= MAX_PENETRATION, "villager overlapped a house");
}

// 4. Gather behind a long wall: too long to get around inside the short-path range, so it needs
// the long path that move_units now requests for GATHER/BUILD/RETURN_DROPSITE.
static void gather_behind_long_wall() {
	Simulation s;
	setup(s);
	// Wall segments at x in [0, 2] from z = -30 to z = 30, with a gap at z in [20, 24].
	for (int cy = -15; cy <= 14; ++cy)
		if (cy != 10 && cy != 11) s.spawn_building(0, cy, BLD_WALL_SEGMENT, 0, 1.0f);
	const uint32_t wood = s.spawn_resource_world(10.0f, 0.0f, RES_WOOD, 5000);
	const uint32_t vill = s.spawn_unit(-8.0f, 0.0f, 0, UNIT_VILLAGER, 100.0f);
	s.order_gather(&vill, 1, wood);
	int arrived = -1;
	float pen = 0.0f;
	for (int t = 0; t < 2400 && arrived < 0; ++t) {
		s.tick(DT);
		pen = std::max(pen, max_penetration(s));
		const int i = index_of(s, vill);
		if (i < 0) break;
		const Simulation::UnitView u = s.units();
		const float dx = u.pos_x[i] - 10.0f, dz = u.pos_z[i];
		if (dx * dx + dz * dz <= 3.0f * 3.0f) arrived = t;
	}
	std::printf("  gather behind long wall: at the wood pile after %d ticks (-1 = never in 2400), max penetration %.3f\n", arrived, pen);
	check(arrived >= 0, "villager never got through the gap in the long wall");
	check(pen <= MAX_PENETRATION, "villager overlapped a wall segment");
}

// 3. Move through a gap.
static void move_through_gap() {
	Simulation s;
	setup(s);
	// Barracks are 4x4. A row at x = 2 from z = -22 to z = 22, with a gap at z in [-2, 2].
	for (int k = -5; k <= 5; ++k) {
		if (k == 0) continue;
		s.spawn_building_world(2.0f, 4.0f * k, BLD_BARRACKS, 0, 1.0f);
	}
	std::vector<uint32_t> ids;
	for (int k = 0; k < 4; ++k) ids.push_back(s.spawn_unit(-10.0f, -14.0f + 1.5f * k, 0, UNIT_SWORDSMAN, 160.0f));
	s.cmd_move(ids.data(), ids.size(), 12.0f, -12.0f);
	int arrived = -1;
	float pen = 0.0f;
	for (int t = 0; t < 1200 && arrived < 0; ++t) {
		s.tick(DT);
		pen = std::max(pen, max_penetration(s));
		const Simulation::UnitView u = s.units();
		int on_far_side = 0;
		for (uint32_t id : ids) {
			const int i = index_of(s, id);
			if (i >= 0 && u.pos_x[i] > 6.0f) ++on_far_side;
		}
		if (on_far_side == (int)ids.size()) arrived = t;
	}
	std::printf("  move through gap: all 4 through after %d ticks (-1 = never in 1200), max penetration %.3f\n", arrived, pen);
	check(arrived >= 0, "units did not get through the gap");
	check(pen <= MAX_PENETRATION, "unit overlapped a barracks");
}

// 5. Walk across fields.
static void walk_across_fields() {
	Simulation s;
	setup(s);
	// A touching column of fields across the straight line from x = -12 to x = 12.
	s.spawn_building_world(0.0f, 0.0f, BLD_FIELD, 0, 1.0f);
	const float half = s.buildings().back().half_size;
	for (int k = 1; k <= 2; ++k) {
		s.spawn_building_world(0.0f, 2.0f * half * k, BLD_FIELD, 0, 1.0f);
		s.spawn_building_world(0.0f, -2.0f * half * k, BLD_FIELD, 0, 1.0f);
	}
	const uint32_t walker = s.spawn_unit(-12.0f, 0.0f, 0, UNIT_SWORDSMAN, 160.0f);
	s.cmd_move(&walker, 1, 12.0f, 0.0f);
	int arrived = -1;
	float max_dev = 0.0f;
	for (int t = 0; t < 1200 && arrived < 0; ++t) {
		s.tick(DT);
		const int i = index_of(s, walker);
		if (i < 0) break;
		const Simulation::UnitView u = s.units();
		max_dev = std::max(max_dev, std::fabs(u.pos_z[i]));
		const float dx = u.pos_x[i] - 12.0f, dz = u.pos_z[i];
		if (dx * dx + dz * dz <= 1.0f) arrived = t;
	}
	std::printf("  walk across fields: arrived after %d ticks (-1 = never in 1200), max sideways %.3f\n", arrived, max_dev);
	check(arrived >= 0, "unit never crossed the fields");
	// Long-path waypoints sit on navcell centres, 1 world unit off the line; going around the
	// fields would take the unit more than 10 units sideways.
	check(max_dev <= 2.0f, "unit walked around the fields instead of across");
}

// 6. Out of a house cluster: Petra builds houses around an idle catapult (test_petra's roster
// army, #136). The catapult's long path starts on navcells in 2-unit gaps between the houses, closer to
// them than a catapult can stand, so it could never reach those waypoints and stayed put.
static void out_of_house_cluster() {
	Simulation s;
	setup(s);
	s.spawn_building_world(-21.0f, -33.0f, BLD_HOUSE, 0, 1.0f);
	s.spawn_building_world(-17.0f, -33.0f, BLD_HOUSE, 0, 1.0f);
	s.spawn_building_world(-25.0f, -31.0f, BLD_HOUSE, 0, 1.0f);
	const uint32_t cat = s.spawn_unit(-22.89f, -35.68f, 0, UNIT_CATAPULT, 0.0f);
	for (int t = 0; t < 20; ++t) s.tick(DT);
	// Foundations placed on top of it: the builder's arrival sends the catapult off each one
	// (Foundation.js Commit, UnitAI LeaveFoundation), and the committed foundation blocks it.
	const uint32_t builder = s.spawn_unit(-21.0f, -40.0f, 0, UNIT_FEMALE_CITIZEN, 0.0f);
	const uint32_t f1 = s.spawn_building_world(-23.0f, -37.0f, BLD_HOUSE, 0, 0.0f);
	s.order_build_or_repair(&builder, 1, f1);
	for (int t = 0; t < 400; ++t) s.tick(DT);
	const uint32_t f2 = s.spawn_building_world(-19.0f, -37.0f, BLD_HOUSE, 0, 0.0f);
	s.order_build_or_repair(&builder, 1, f2);
	for (int t = 0; t < 400; ++t) s.tick(DT);
	s.cmd_move(&cat, 1, 9.0f, -35.0f);
	int arrived = -1;
	float pen = 0.0f;
	for (int t = 0; t < 1200 && arrived < 0; ++t) {
		s.tick(DT);
		pen = std::max(pen, max_penetration(s));
		const int i = index_of(s, cat);
		if (i < 0) break;
		const Simulation::UnitView u = s.units();
		const float dx = u.pos_x[i] - 9.0f, dz = u.pos_z[i] + 35.0f;
		if (dx * dx + dz * dz <= 1.0f) arrived = t;
	}
	std::printf("  out of a house cluster: catapult arrived after %d ticks (-1 = never in 1200), max penetration %.3f\n", arrived, pen);
	check(arrived >= 0, "catapult never left the house cluster");
	check(pen <= MAX_PENETRATION, "catapult overlapped a house");
}

// 7. Walk past a turned barracks.
static void walk_past_turned_building() {
	constexpr float QUARTER_PI = 0.785398163f;
	float ux, uz;
	nav::footprint_axis(Simulation::DEFAULT_BUILDING_ROT, ux, uz);
	check(ux == 1.0f && uz == 0.0f, "the default facing is not axis-aligned");
	nav::footprint_axis(Simulation::unpack_angle(Simulation::pack_angle(2.0f * QUARTER_PI)), ux, uz);
	check(ux == 1.0f && uz == 0.0f, "a quarter turn is not axis-aligned");
	nav::footprint_axis(Simulation::DEFAULT_BUILDING_ROT + QUARTER_PI, ux, uz);
	check(std::fabs(ux - 0.70710678f) < 1e-5f && std::fabs(uz + 0.70710678f) < 1e-5f, "45 degrees gives the wrong axis");

	// 0 A.D.'s long wall: 36 x 5 m, half extents 4.5 and 0.625 RTS0 units.
	Simulation::Building wall{};
	wall.set_extents(4.5f, 0.625f);
	check(nav::footprint_contains(wall, 4.2f, 0.0f, 0.0f) && !nav::footprint_contains(wall, 0.0f, 0.8f, 0.0f),
			"the unturned wall does not lie along x");
	wall.set_rot(Simulation::DEFAULT_BUILDING_ROT + 2.0f * QUARTER_PI);
	check(nav::footprint_aligned(wall) && nav::footprint_extent(wall) == 4.5f, "a quarter-turned wall is not axis-aligned");
	check(nav::footprint_contains(wall, 0.0f, 4.2f, 0.0f) && !nav::footprint_contains(wall, 0.8f, 0.0f, 0.0f),
			"a quarter turn does not swap the wall's width and depth");
	check(nav::footprint_overlaps_box(wall, -0.2f, 4.0f, 0.2f, 4.4f) && !nav::footprint_overlaps_box(wall, 0.7f, -0.2f, 1.0f, 0.2f),
			"the quarter-turned wall's placement box is wrong");
	wall.set_rot(Simulation::DEFAULT_BUILDING_ROT + QUARTER_PI);
	check(std::fabs(nav::footprint_extent(wall) - 5.125f * 0.70710678f) < 1e-4f, "the 45-degree wall's bounds are wrong");
	check(nav::footprint_contains(wall, 2.8f, -2.8f, 0.0f) && !nav::footprint_contains(wall, 2.8f, 2.8f, 0.0f),
			"the 45-degree wall does not lie along its u axis");
	{
		Simulation w;
		setup(w);
		// On a navcell centre: grown by the rasterizer's 1 unit, the wall spans one 2-unit navcell across.
		const uint32_t seg = w.spawn_building_world(-29.0f, -29.0f, BLD_WALL_SEGMENT, 0, 1.0f);
		for (auto &b : w.buildings_)
			if (b.id == seg) {
				b.set_extents(4.5f, 0.625f);
				b.set_rot(Simulation::DEFAULT_BUILDING_ROT + 2.0f * QUARTER_PI);
			}
		w.rebuild_nav_grid();
		auto open = [&](float x, float z) {
			const auto [cx, cy] = w.nav_grid_.world_to_cell(x, z);
			return w.nav_grid_.is_passable_for(cx, cy, PASS_INFANTRY);
		};
		check(!open(-29.0f, -25.5f) && !open(-29.0f, -32.5f) && open(-26.0f, -29.0f) && open(-29.0f, -22.5f),
				"the nav grid does not block the quarter-turned wall along z");
	}
	// Turned off the axes, the wall covers few navcells whole: every navcell along its centre line
	// blocks all the same (0.28 closes a wall whole), so no long path goes through it, and none past
	// its ends does.
	for (const float a : {0.3f, 0.6f, QUARTER_PI, 1.1f, 2.0f}) {
		Simulation w;
		setup(w);
		const uint32_t seg = w.spawn_building_world(-29.0f, -29.0f, BLD_WALL_SEGMENT, 0, 1.0f);
		float rot = 0.0f;
		for (auto &b : w.buildings_)
			if (b.id == seg) {
				b.set_extents(4.5f, 0.625f);
				b.set_rot(Simulation::DEFAULT_BUILDING_ROT + a);
				rot = b.rot;
			}
		w.rebuild_nav_grid();
		// Along its width, u = (cos a, -sin a) (0 A.D. GetObstructionSquare).
		auto open = [&](float t) {
			const auto [cx, cy] = w.nav_grid_.world_to_cell(-29.0f + t * std::cos(rot), -29.0f - t * std::sin(rot));
			return w.nav_grid_.is_passable_for(cx, cy, PASS_INFANTRY);
		};
		bool closed = true;
		for (float t = -4.5f; t <= 4.5f; t += 0.25f) closed = closed && !open(t);
		check(closed, "a navcell along a turned wall is open to long paths");
		check(open(-8.0f) && open(8.0f), "a turned wall blocks navcells past its ends");
	}

	Simulation s;
	setup(s);
	const uint32_t barracks = s.spawn_building_world(0.0f, 0.0f, BLD_BARRACKS, 0, 1.0f);
	const uint32_t house = s.spawn_building_world(0.0f, 16.0f, BLD_HOUSE, 0, 0.0f);
	for (auto &b : s.buildings_)
		if (b.id == barracks || b.id == house) b.set_rot(Simulation::DEFAULT_BUILDING_ROT + QUARTER_PI);
	s.rebuild_nav_grid();
	const auto &turned = s.buildings()[s.building_id_to_idx_.at(barracks)];
	check(nav::footprint_extent(turned) > turned.half_size * 1.41f, "the turned barracks' bounds are too small");
	// For placement: the turned barracks reaches a box past its unturned side, not one in its unturned corner.
	check(nav::footprint_overlaps_box(turned, 2.2f, -0.2f, 2.6f, 0.2f), "the turned barracks misses a box under its corner");
	check(!nav::footprint_overlaps_box(turned, 1.6f, 1.6f, 1.9f, 1.9f), "the turned barracks covers its unturned corner");

	const uint32_t walker = s.spawn_unit(-12.0f, 0.0f, 0, UNIT_SWORDSMAN, 160.0f);
	s.cmd_move(&walker, 1, 12.0f, 0.0f);
	const uint32_t woman = s.spawn_unit(-6.0f, 16.5f, 0, UNIT_FEMALE_CITIZEN, 100.0f);
	s.order_build_or_repair(&woman, 1, house);
	int arrived = -1, built = -1;
	float pen = 0.0f;
	for (int t = 0; t < 1200 && (arrived < 0 || built < 0); ++t) {
		s.tick(DT);
		pen = std::max(pen, max_penetration(s));
		const int i = index_of(s, walker);
		if (i < 0) break;
		const Simulation::UnitView u = s.units();
		const float dx = u.pos_x[i] - 12.0f, dz = u.pos_z[i];
		if (arrived < 0 && dx * dx + dz * dz <= 1.0f) arrived = t;
		if (built < 0 && s.buildings()[s.building_id_to_idx_.at(house)].build_progress >= 1.0f) built = t;
	}
	std::printf("  walk past turned barracks: arrived after %d ticks, house built after %d (-1 = never in 1200), "
			"max penetration %.3f\n", arrived, built, pen);
	check(arrived >= 0, "unit never got past the turned barracks");
	check(built >= 0, "woman never finished the turned house");
	check(pen <= MAX_PENETRATION, "unit overlapped a turned building");
}

// 8. Siege through a gap.
static void siege_through_gap() {
	Simulation s;
	setup(s);
	// Houses are 2x2: a touching row at x = 0 from z = -21 to z = 21, with a gap at z in [-1, 1].
	for (int k = 1; k <= 10; ++k) {
		s.spawn_building_world(0.0f, 2.0f * k, BLD_HOUSE, 0, 1.0f);
		s.spawn_building_world(0.0f, -2.0f * k, BLD_HOUSE, 0, 1.0f);
	}
	const uint32_t cat = s.spawn_unit(-10.0f, 0.5f, 0, UNIT_CATAPULT, 0.0f);
	s.cmd_move(&cat, 1, 10.0f, 0.0f);
	int arrived = -1;
	float pen = 0.0f, max_dev = 0.0f;
	for (int t = 0; t < 1200 && arrived < 0; ++t) {
		s.tick(DT);
		pen = std::max(pen, max_penetration(s));
		const int i = index_of(s, cat);
		if (i < 0) break;
		const Simulation::UnitView u = s.units();
		max_dev = std::max(max_dev, std::fabs(u.pos_z[i]));
		const float dx = u.pos_x[i] - 10.0f, dz = u.pos_z[i];
		if (dx * dx + dz * dz <= 1.0f) arrived = t;
	}
	std::printf("  siege through a gap: catapult arrived after %d ticks (-1 = never in 1200), max sideways %.3f, "
			"max penetration %.3f\n", arrived, max_dev, pen);
	check(arrived >= 0, "catapult never got past the row of houses");
	check(max_dev <= 2.0f, "catapult went around the houses instead of through the gap");
	check(pen <= MAX_PENETRATION, "catapult overlapped a house");
}

// 9. Walk into a building.
static void walk_into_building() {
	Simulation s;
	setup(s);
	s.spawn_building_world(0.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
	const float half = s.buildings().back().half_size;
	const uint32_t cat = s.spawn_unit(-12.0f, 0.3f, 0, UNIT_CATAPULT, 0.0f);
	const uint32_t sword = s.spawn_unit(0.3f, 12.0f, 0, UNIT_SWORDSMAN, 160.0f);
	s.cmd_move(&cat, 1, -half + 0.3f, 0.0f);
	s.cmd_move(&sword, 1, 0.0f, half - 0.5f);
	int idle = -1;
	float pen = 0.0f;
	for (int t = 0; t < 1200 && idle < 0; ++t) {
		s.tick(DT);
		pen = std::max(pen, max_penetration(s));
		const int ci = index_of(s, cat), si = index_of(s, sword);
		if (ci < 0 || si < 0) break;
		const Simulation::UnitView u = s.units();
		if (u.action_state[ci] == ACTION_IDLE && u.action_state[si] == ACTION_IDLE) idle = t;
	}
	const Simulation::UnitView u = s.units();
	const int ci = index_of(s, cat), si = index_of(s, sword);
	std::printf("  walk into a building: both stopped beside the town centre after %d ticks (-1 = never in 1200), "
			"catapult at x %.2f, swordsman at z %.2f, max penetration %.3f\n", idle, ci >= 0 ? u.pos_x[ci] : 0.0f,
			si >= 0 ? u.pos_z[si] : 0.0f, pen);
	check(idle >= 0, "a walk to a point inside a building never ended");
	check(ci >= 0 && u.pos_x[ci] < -half && u.pos_x[ci] > -half - 1.5f, "the catapult stopped away from the town centre's near side");
	check(si >= 0 && u.pos_z[si] > half && u.pos_z[si] < half + 1.0f, "the swordsman stopped away from the town centre's near side");
	check(pen <= MAX_PENETRATION, "a unit overlapped the town centre");
}

static void walk_reissued_every_tick() {
	Simulation s;
	setup(s);
	// Off its navcell's centre, as a unit usually stands.
	const uint32_t scout = s.spawn_unit(0.2f, 0.2f, 0, UNIT_SWORDSMAN, 0.0f);
	int arrived = -1;
	for (int t = 0; t < 600 && arrived < 0; ++t) {
		s.cmd_move(&scout, 1, -30.0f, 0.2f); // as Petra's scout is sent home every tick
		s.tick(DT);
		const int i = index_of(s, scout);
		if (i < 0) break;
		if (std::hypot(s.units().pos_x[i] + 30.0f, s.units().pos_z[i] - 0.2f) < 1.0f) arrived = t;
	}
	std::printf("  walk reissued every tick: arrived after %d ticks (-1 = never in 600)\n", arrived);
	check(arrived >= 0, "a walk given again every tick never arrived");
}

static void walk_to_one_point() {
	Simulation s;
	setup(s);
	std::vector<uint32_t> ids;
	for (int k = 0; k < 12; ++k) ids.push_back(s.spawn_unit(-6.0f + 1.1f * k, -14.0f, 0, UNIT_SWORDSMAN, 0.0f));
	for (uint32_t id : ids) s.cmd_move(&id, 1, 0.0f, 0.0f); // one at a time: all to the same point
	int all_idle = -1;
	float far = 0.0f;
	for (int t = 0; t < 1200 && all_idle < 0; ++t) {
		s.tick(DT);
		bool idle = true;
		for (uint32_t id : ids) {
			const int i = index_of(s, id);
			if (i >= 0 && s.units().action_state[i] != ACTION_IDLE) idle = false;
		}
		if (idle) all_idle = t;
	}
	for (uint32_t id : ids) {
		const int i = index_of(s, id);
		if (i >= 0) far = std::max(far, std::hypot(s.units().pos_x[i], s.units().pos_z[i]));
	}
	std::printf("  walk to one point: all 12 stopped after %d ticks (-1 = never in 1200), farthest %.2f from it\n", all_idle, far);
	check(all_idle >= 0, "units sent to one point kept pushing for it");
	check(far <= 3.0f + 1.0f, "a unit sent to one point stopped beyond 12 m of it");
}

// A sea across the middle of the map (|z| < 16), as tools/test_naval.cpp makes.
static void sea_setup(Simulation &s) {
	setup(s);
	constexpr int RES = 65;
	constexpr float HALF = 64.0f;
	s.set_map_half(HALF);
	std::vector<float> h((size_t)RES * RES);
	for (int j = 0; j < RES; ++j)
		for (int i = 0; i < RES; ++i) {
			const float z = -HALF + 2.0f * HALF * j / (RES - 1);
			h[(size_t)j * RES + i] = std::fabs(z) < 16.0f ? -3.0f : 3.0f;
		}
	s.set_terrain_data(h, RES, 0.0f);
	s.begin_bulk_spawn();
	s.end_bulk_spawn(); // marks the water on the nav grid
}

static void ships_that_cannot_get_there() {
	// A trireme a player sends ashore stops at the shore (MakeGoalReachable).
	Simulation s;
	sea_setup(s);
	const uint32_t ship = s.spawn_unit(0.0f, -5.0f, 0, UNIT_TRIREME, 0.0f);
	s.order_walk(&ship, 1, 0.0f, 30.0f, false, true); // a player's walk
	int idle = -1;
	for (int t = 0; t < 1200 && idle < 0; ++t) {
		s.tick(DT);
		const int i = index_of(s, ship);
		if (i >= 0 && s.units().action_state[i] == ACTION_IDLE) idle = t;
	}
	const int si = index_of(s, ship);
	const float z = si >= 0 ? s.units().pos_z[si] : 99.0f;
	std::printf("  ship sent ashore: stopped after %d ticks (-1 = never in 1200) at z %.2f\n", idle, z);
	check(idle >= 0, "a ship sent ashore never stopped");
	check(z < 16.0f, "a ship sent ashore sailed onto the land");

}

static void crowds_at_the_shore() {
	Simulation s;
	sea_setup(s);
	std::vector<uint32_t> ships, men;
	// Two tight 4 x 3 clumps, one each side of the shore line at z = 16.
	for (int k = 0; k < 12; ++k) {
		const float dx = 0.2f * (float)(k % 4), dz = 0.2f * (float)(k / 4);
		ships.push_back(s.spawn_unit(dx, 14.0f + dz, 0, UNIT_TRIREME, 0.0f));
		men.push_back(s.spawn_unit(20.0f + dx, 17.0f + dz, 0, UNIT_SWORDSMAN, 0.0f));
	}
	int aground = 0, at_sea = 0;
	for (int t = 0; t < 200; ++t) {
		s.tick(DT);
		const Simulation::UnitView u = s.units();
		auto on = [&](uint32_t id, uint8_t pass) {
			const int i = index_of(s, id);
			const auto [cx, cy] = s.nav_grid_.world_to_cell(u.pos_x[i], u.pos_z[i]);
			return s.nav_grid_.is_passable_for(cx, cy, pass);
		};
		for (uint32_t id : ships) aground += !on(id, PASS_SHIP);
		for (uint32_t id : men) at_sea += !on(id, PASS_INFANTRY);
	}
	std::printf("  crowds at the shore: ship-ticks aground %d, soldier-ticks at sea %d (of %d each)\n", aground, at_sea, 200 * 12);
	check(aground == 0, "pushing ran a ship aground");
	check(at_sea == 0, "pushing carried a soldier out to sea");
}

// An enemy walking through a closed gate in a long wall: the long path goes through the gate
// (unlocked gates don't block pathfinding), the gate stops the unit dead, and the walk ends after
// 35 turns of not moving (MoveFailed) instead of pushing at the gate for good.
static void walk_into_closed_gate() {
	Simulation s;
	setup(s);
	s.spawn_building_world(0.0f, 0.0f, BLD_GATE, 1, 1.0f);
	const float gh = s.buildings().back().half_size;
	s.spawn_building_world(0.0f, 0.0f, BLD_WALL_SEGMENT, 1, 1.0f);
	const float wh = s.buildings().back().half_size;
	s.buildings_.pop_back(); // only measuring the segment
	for (float z = gh + wh; z < 60.0f; z += 2.0f * wh) {
		s.spawn_building_world(0.0f, z, BLD_WALL_SEGMENT, 1, 1.0f);
		s.spawn_building_world(0.0f, -z, BLD_WALL_SEGMENT, 1, 1.0f);
	}
	const uint32_t sword = s.spawn_unit(-10.0f, 0.0f, 0, UNIT_SWORDSMAN, 0.0f);
	s.cmd_move(&sword, 1, 10.0f, 0.0f);
	int at_gate = -1, stopped = -1;
	float x = -10.0f;
	for (int t = 0; t < 1200 && stopped < 0; ++t) {
		s.tick(DT);
		const int i = index_of(s, sword);
		if (i < 0) break;
		const Simulation::UnitView u = s.units();
		x = u.pos_x[i];
		if (at_gate < 0 && x > -gh - 0.5f) at_gate = t;
		if (u.action_state[i] == ACTION_IDLE) stopped = t;
	}
	std::printf("  walk into a closed gate: at the gate after %d ticks, walk ended after %d (-1 = never in 1200), x %.2f\n",
			at_gate, stopped, x);
	check(stopped >= 0, "a walk into a closed gate never ended");
	check(at_gate < 0 || stopped < 0 || stopped - at_gate <= 35 * 4 + 20, "a walk stopped by a closed gate ended later than 35 turns");
	check(x < 0.0f, "the unit got through the closed gate");
}

// A cliff along x = 0, 8 units high over 2 (slope 4), and a ramp for z in [16, 34] that climbs
// the same 8 units over 20 (slope 0.4): the top is x > 10 on the ramp, x > 1 elsewhere.
static void cliff_setup(Simulation &s) {
	setup(s);
	constexpr int RES = 65;
	constexpr float HALF = 64.0f;
	s.set_map_half(HALF);
	std::vector<float> h((size_t)RES * RES);
	for (int j = 0; j < RES; ++j)
		for (int i = 0; i < RES; ++i) {
			const float x = -HALF + 2.0f * HALF * i / (RES - 1), z = -HALF + 2.0f * HALF * j / (RES - 1);
			const bool ramp = z >= 16.0f && z <= 34.0f;
			h[(size_t)j * RES + i] = ramp ? 8.0f * std::clamp((x + 10.0f) / 20.0f, 0.0f, 1.0f)
			                              : 8.0f * std::clamp((x + 1.0f) / 2.0f, 0.0f, 1.0f);
		}
	s.set_terrain_data(h, RES, -1.0f);
	s.begin_bulk_spawn();
	s.end_bulk_spawn();
}

static void cliffs() {
	Simulation s;
	cliff_setup(s);
	auto steep_at = [&](float x, float z) {
		const auto [cx, cy] = s.nav_grid_.world_to_cell(x, z);
		return s.nav_grid_.is_steep(cx, cy);
	};
	check(steep_at(0.0f, 0.0f) && !steep_at(0.0f, 25.0f) && !steep_at(-20.0f, 0.0f), "the cliff is not steep or the ramp is");
	struct Walker { uint32_t id; float gx, gz; int arrived = -1, on_cliff = 0; float max_z = -1e9f; const char *what; };
	std::vector<Walker> w;
	w.push_back({s.spawn_unit(-20.0f, 0.0f, 0, UNIT_SWORDSMAN, 0.0f), 20.0f, 0.0f});
	w.back().what = "swordsman";
	w.push_back({s.spawn_unit(-20.0f, -4.0f, 0, UNIT_CAVALRY, 0.0f), 20.0f, -4.0f});
	w.back().what = "horseman";
	s.cmd_move(&w[0].id, 1, w[0].gx, w[0].gz);
	s.cmd_move(&w[1].id, 1, w[1].gx, w[1].gz);
	const uint32_t wood = s.spawn_resource_world(20.0f, -10.0f, RES_WOOD, 5000);
	w.push_back({s.spawn_unit(-20.0f, -10.0f, 0, UNIT_VILLAGER, 100.0f), 20.0f, -10.0f});
	w.back().what = "gatherer";
	s.order_gather(&w[2].id, 1, wood);
	for (int t = 0; t < 2400; ++t) {
		s.tick(DT);
		const Simulation::UnitView u = s.units();
		for (Walker &k : w) {
			const int i = index_of(s, k.id);
			if (i < 0 || k.arrived >= 0) continue;
			k.on_cliff += steep_at(u.pos_x[i], u.pos_z[i]);
			k.max_z = std::max(k.max_z, u.pos_z[i]);
			const float dx = u.pos_x[i] - k.gx, dz = u.pos_z[i] - k.gz;
			if (dx * dx + dz * dz <= 3.0f * 3.0f) k.arrived = t;
		}
	}
	for (const Walker &k : w) {
		std::printf("  cliffs: %s up after %d ticks (-1 = never in 2400), %d ticks on the cliff, furthest z %.1f\n",
				k.what, k.arrived, k.on_cliff, k.max_z);
		check(k.arrived >= 0, "a unit never got up the ramp");
		check(k.on_cliff == 0, "a unit climbed the cliff");
		check(k.max_z >= 16.0f, "a unit got up without taking the ramp");
	}
	check(s.check_placement(0, -1, -8, BLD_HOUSE) == Simulation::PLACE_TERRAIN, "a house can be placed on the cliff");
	check(s.check_placement(0, -12, -8, BLD_HOUSE) != Simulation::PLACE_TERRAIN, "a house can't be placed on flat ground");
}

// 16. Chases.
// A sea across the map, |z| < half_width. Its banks rise 6 over one 2-unit tile, a cliff, unless
// `bank` gives them a slope (height per unit) a unit may stand on.
static void channel_setup(Simulation &s, float half_width, float bank = 0.0f) {
	setup(s);
	constexpr int RES = 65;
	constexpr float HALF = 64.0f;
	s.set_map_half(HALF);
	std::vector<float> h((size_t)RES * RES);
	for (int j = 0; j < RES; ++j)
		for (int i = 0; i < RES; ++i) {
			const float z = -HALF + 2.0f * HALF * j / (RES - 1);
			h[(size_t)j * RES + i] = bank > 0.0f ? std::clamp((std::fabs(z) - half_width + 1.0f) * bank, -3.0f, 3.0f)
			                       : std::fabs(z) < half_width ? -3.0f : 3.0f;
		}
	s.set_terrain_data(h, RES, 0.0f);
	s.begin_bulk_spawn();
	s.end_bulk_spawn();
}

static void chases() {
	auto deep_at = [](const Simulation &s, uint32_t id) {
		const int i = index_of(s, id);
		const auto [cx, cy] = s.nav_grid_.world_to_cell(s.units_.pos_x[i], s.units_.pos_z[i]);
		return s.nav_grid_.cells.inside(cx, cy) && s.nav_grid_.cells.at(cx, cy) == NAV_WATER;
	};
	auto attacking = [](const Simulation &s, uint32_t id, uint32_t target) {
		const int i = index_of(s, id);
		return s.units_.action_state[i] == ACTION_ATTACK &&
				(s.units_.target_unit_id[i] == target || s.units_.target_building_id[i] == target);
	};
	// A player's attack on a unit, then on a house, across the sea (z -16 to 16).
	for (int house = 0; house < 2; ++house) {
		Simulation s;
		channel_setup(s, 16.0f);
		uint32_t target;
		if (house) {
			target = s.spawn_building_world(0.0f, 30.0f, BLD_HOUSE, 1, 1.0f);
		} else {
			target = s.spawn_unit(0.0f, 30.0f, 1, UNIT_SPEARMAN, 0.0f);
			s.order_set_stance(&target, 1, STANCE_PASSIVE);
		}
		const uint32_t a = s.spawn_unit(0.0f, -30.0f, 0, UNIT_SWORDSMAN, 0.0f);
		s.set_reveal_map(true); // the player sees the far shore (a chase into the fog ends, CheckTargetVisible)
		if (house) s.order_attack_building(&a, 1, target);
		else s.order_attack(&a, 1, target);
		int gave_up = -1, wet = 0;
		for (int t = 0; t < 800; ++t) {
			s.tick(DT);
			wet += deep_at(s, a);
			if (gave_up < 0 && !attacking(s, a, target)) gave_up = t;
		}
		const float z = s.units_.pos_z[index_of(s, a)];
		std::printf("  chases: attack on a %s across the sea given up after %d ticks (-1 = never in 800), %d ticks in deep water, stands at z %.1f\n",
				house ? "house" : "unit", gave_up, wet, z);
		check(gave_up >= 0, "an attack across the sea was never given up");
		check(wet == 0, "an attacker waded into the sea after its target");
		check(z > -20.0f && z < -12.0f, "an attacker that gave up is not at the shore");
	}
	// A soldier's own fight with an enemy across a channel (z -6 to 6), well within its sight.
	{
		Simulation s;
		channel_setup(s, 6.0f);
		const uint32_t foe = s.spawn_unit(0.0f, 9.0f, 1, UNIT_SPEARMAN, 0.0f);
		s.order_set_stance(&foe, 1, STANCE_PASSIVE);
		const uint32_t a = s.spawn_unit(0.0f, -9.0f, 0, UNIT_SWORDSMAN, 0.0f);
		const uint64_t paths0 = s.map_scale_stats().path_calls;
		int wet = 0, chased = 0;
		float min_z = 0.0f, max_z = -99.0f;
		for (int t = 0; t < 400; ++t) {
			s.tick(DT);
			wet += deep_at(s, a);
			chased += attacking(s, a, foe);
			const float z = s.units_.pos_z[index_of(s, a)];
			min_z = std::min(min_z, z);
			max_z = std::max(max_z, z);
		}
		const int fi = index_of(s, foe);
		std::printf("  chases: own fight across a channel: %d ticks chasing, %d in deep water, z %.1f to %.1f, foe hp %.0f/%.0f, %llu paths\n",
				chased, wet, min_z, max_z, s.units_.hp[fi], s.units_.max_hp[fi],
				(unsigned long long)(s.map_scale_stats().path_calls - paths0));
		check(chased > 0, "a soldier did not go for an enemy across a channel");
		check(wet == 0, "a soldier waded into a channel after an enemy");
		check(s.units_.hp[fi] == s.units_.max_hp[fi], "a swordsman hit an enemy across a channel");
	}
	// A player's attack on a unit up a cliff with one ramp (cliff_setup).
	{
		Simulation s;
		cliff_setup(s);
		const uint32_t foe = s.spawn_unit(20.0f, 0.0f, 1, UNIT_SPEARMAN, 0.0f);
		s.order_set_stance(&foe, 1, STANCE_PASSIVE);
		const uint32_t a = s.spawn_unit(-20.0f, 0.0f, 0, UNIT_SWORDSMAN, 0.0f);
		s.set_reveal_map(true); // the player sees the cliff top (a chase into the fog ends, CheckTargetVisible)
		s.order_attack(&a, 1, foe);
		int hit = -1, on_cliff = 0;
		for (int t = 0; t < 2400 && hit < 0; ++t) {
			s.tick(DT);
			const int i = index_of(s, a), fi = index_of(s, foe);
			const auto [cx, cy] = s.nav_grid_.world_to_cell(s.units_.pos_x[i], s.units_.pos_z[i]);
			on_cliff += s.nav_grid_.is_steep(cx, cy);
			if (fi < 0 || !s.units_.alive[fi] || s.units_.hp[fi] < s.units_.max_hp[fi]) hit = t;
		}
		std::printf("  chases: attack up the cliff landed after %d ticks (-1 = never in 2400), %d ticks on the cliff\n", hit, on_cliff);
		check(hit >= 0, "an attacker never reached its target up the ramp");
		check(on_cliff == 0, "an attacker climbed the cliff to its target");
	}
}

// 17. Large units through a narrow gap.
static uint16_t roster(const char *id) {
	for (uint16_t k = 0; k < CIV_UNIT_TEMPLATE_COUNT; ++k)
		if (std::strcmp(civ_unit_template(k).id, id) == 0) return k;
	return CIV_UNIT_TEMPLATE_NONE;
}

static void large_units_through_gap() {
	const uint16_t elephant_hero = roster("maur/hero_chandragupta"), foot_hero = roster("athen/hero_pericles");
	const uint16_t chariot_hero = roster("brit/hero_boudicca"), cavalry = roster("athen/cavalry_javelineer_b");
	check(elephant_hero != CIV_UNIT_TEMPLATE_NONE && foot_hero != CIV_UNIT_TEMPLATE_NONE &&
			chariot_hero != CIV_UNIT_TEMPLATE_NONE && cavalry != CIV_UNIT_TEMPLATE_NONE, "roster rows missing");
	check(unit_building_clearance(elephant_hero, UNIT_HERO) == CLEARANCE_LARGE, "an elephant hero is not large");
	check(unit_building_clearance(chariot_hero, UNIT_HERO) == CLEARANCE_LARGE, "a chariot hero is not large");
	check(unit_building_clearance(foot_hero, UNIT_HERO) == CLEARANCE_DEFAULT, "a hero on foot is large");
	check(unit_building_clearance(cavalry, UNIT_CAVALRY) == CLEARANCE_DEFAULT, "cavalry is large");
	for (uint8_t type : {UNIT_CATAPULT, UNIT_SIEGE_RAM, UNIT_BOLT_SHOOTER, UNIT_ELEPHANT, UNIT_WORKER_ELEPHANT,
			UNIT_CHARIOT, UNIT_SCYTHED_CHARIOT, UNIT_CATAFALQUE, UNIT_WAGON})
		check(unit_building_clearance(CIV_UNIT_TEMPLATE_NONE, type) == CLEARANCE_LARGE, "a large unit type is not large");

	struct Walker { uint8_t type; uint16_t tmpl; bool large; const char *name; };
	for (const Walker w : {Walker{UNIT_SWORDSMAN, CIV_UNIT_TEMPLATE_NONE, false, "swordsman"},
			Walker{UNIT_HERO, elephant_hero, true, "elephant hero"},
			Walker{UNIT_SCYTHED_CHARIOT, CIV_UNIT_TEMPLATE_NONE, true, "scythed chariot"}}) {
		Simulation s;
		setup(s);
		// Houses at z = 1 +- (1.6 + 2k): edges at z 0.4 and 1.6, a 1.2-unit gap round the middle of
		// the navcell row z in [0, 2], which no house covers, so long paths may take it.
		for (int k = 0; k < 2; ++k) {
			s.spawn_building_world(0.0f, 2.6f + 2.0f * k, BLD_HOUSE, 0, 1.0f);
			s.spawn_building_world(0.0f, -0.6f - 2.0f * k, BLD_HOUSE, 0, 1.0f);
		}
		const uint32_t id = s.spawn_unit(-6.0f, 1.0f, 0, (UnitType)w.type, 0.0f, w.tmpl);
		s.cmd_move(&id, 1, 6.0f, 1.0f);
		int arrived = -1;
		float pen = 0.0f, max_dev = 0.0f;
		for (int t = 0; t < 1600 && arrived < 0; ++t) {
			s.tick(DT);
			pen = std::max(pen, max_penetration(s));
			const int i = index_of(s, id);
			if (i < 0) break;
			const Simulation::UnitView u = s.units();
			if (std::fabs(u.pos_x[i]) < 1.0f) max_dev = std::max(max_dev, std::fabs(u.pos_z[i] - 1.0f));
			if (std::hypot(u.pos_x[i] - 6.0f, u.pos_z[i] - 1.0f) <= 1.0f) arrived = t;
		}
		std::printf("  large units through a gap: %s arrived after %d ticks (-1 = never in 1600), "
				"%.2f off the gap's middle passing the houses, max penetration %.3f\n", w.name, arrived, max_dev, pen);
		check(arrived >= 0, "a unit never got past the houses");
		if (w.large) check(max_dev > 2.0f, "a large unit squeezed through a gap narrower than its clearance");
		else check(max_dev < 0.4f, "a swordsman went round the houses instead of through the gap");
		check(pen <= MAX_PENETRATION, "a unit overlapped a house");
	}
}

// 18. Walk to a structure's range.
static void walk_to_structure_range() {
	const float from[4][2] = {{-14.0f, 1.0f}, {14.0f, -1.0f}, {1.0f, -14.0f}, {-1.0f, 14.0f}};
	{
		Simulation s;
		setup(s);
		const uint32_t tc = s.spawn_building_world(0.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
		const float half = s.buildings().back().half_size;
		uint32_t ids[4];
		float aim_off[4], last_x[4], last_z[4];
		bool in[4] = {};
		for (int k = 0; k < 4; ++k) {
			ids[k] = s.spawn_unit(from[k][0], from[k][1], 0, UNIT_FEMALE_CITIZEN);
			s.order_garrison(&ids[k], 1, tc);
			const int i = index_of(s, ids[k]);
			// How far outside its clearance the walk aims, on the side it comes from.
			const float ax = s.units_.target_x[i], az = s.units_.target_z[i];
			const float side = k < 2 ? ax * from[k][0] : az * from[k][1];
			const float gap = (k < 2 ? std::fabs(ax) : std::fabs(az)) - half;
			aim_off[k] = side > 0.0f ? gap - unit_building_clearance(s.units_.tmpl[i], s.units_.unit_type[i]) : -1.0f;
			last_x[k] = from[k][0];
			last_z[k] = from[k][1];
		}
		for (int t = 0; t < 600; ++t) {
			s.tick(DT);
			for (int k = 0; k < 4; ++k) {
				const int i = index_of(s, ids[k]);
				if (i >= 0 && s.units().alive[i]) last_x[k] = s.units().pos_x[i], last_z[k] = s.units().pos_z[i];
				else in[k] = true;
			}
		}
		int ok = 0;
		for (int k = 0; k < 4; ++k) {
			// Last seen beyond the town centre's face on the side it came from.
			const bool near_side = k < 2 ? last_x[k] * std::copysign(1.0f, from[k][0]) > half
					: last_z[k] * std::copysign(1.0f, from[k][1]) > half;
			std::printf("  garrison from (%.0f, %.0f): aims %.2f past its clearance, %s, last seen at (%.2f, %.2f)\n",
					from[k][0], from[k][1], aim_off[k], in[k] ? "got in" : "never got in", last_x[k], last_z[k]);
			if (in[k] && near_side && aim_off[k] >= 0.0f && aim_off[k] < 0.1f) ++ok;
		}
		check(ok == 4, "a unit sent into the town centre didn't aim at its own side, just outside its clearance, and get in");
	}
	{
		Simulation s;
		setup(s);
		const uint32_t f = s.spawn_building_world(0.0f, 0.0f, BLD_HOUSE, 0, 0.0f);
		uint32_t ids[4];
		float sx[4], sz[4];
		bool started[4] = {};
		float moved = 0.0f;
		for (int k = 0; k < 4; ++k) {
			ids[k] = s.spawn_unit(from[k][0], from[k][1], 0, UNIT_FEMALE_CITIZEN);
			s.order_build_or_repair(&ids[k], 1, f);
		}
		auto progress = [&]() {
			for (const auto &b : s.buildings())
				if (b.id == f) return b.build_progress;
			return -1.0f;
		};
		int done = -1;
		for (int t = 0; t < 1600 && (done < 0 || t < done + 40); ++t) {
			s.tick(DT);
			if (done < 0 && progress() >= 1.0f) done = t;
			for (int k = 0; k < 4; ++k) {
				const int i = index_of(s, ids[k]);
				if (i < 0) continue;
				const float x = s.units().pos_x[i], z = s.units().pos_z[i];
				if (!started[k] && s.units().action_state[i] == ACTION_BUILD && progress() > 0.0f &&
						s.walk_in_reach((size_t)i, x, z)) {
					started[k] = true;
					sx[k] = x;
					sz[k] = z;
				} else if (started[k]) {
					moved = std::max(moved, std::hypot(x - sx[k], z - sz[k]));
				}
			}
		}
		const int all = started[0] + started[1] + started[2] + started[3];
		std::printf("  build from four sides: %d of 4 builders worked from where they came in range, finished after %d ticks, "
				"moved %.3f at most from there (even after the house was done)\n", all, done, moved);
		check(all == 4 && done >= 0, "a builder never came within build range");
		// Two builders that meet at a corner push each other apart a little (idle units push idle
		// units), still within range.
		check(moved < 0.15f, "a builder walked on past build range or after the house was done");
	}
}

// Deepest overlap of any live team-0 unit's building clearance with a mine's square (mine_footprint).
static float mine_penetration(const Simulation &s) {
	const Simulation::UnitView u = s.units();
	float worst = 0.0f;
	for (size_t r = 0; r < s.resources_.count; ++r) {
		nav::Footprint m;
		if (!s.resources_.alive[r] || s.resources_.amount[r] <= 0 || !s.mine_footprint(r, m)) continue;
		for (size_t i = 0; i < u.count; ++i) {
			if (!u.alive[i] || u.team[i] != 0) continue;
			const float c = unit_building_clearance(u.tmpl[i], u.unit_type[i]);
			const nav::Vec2 l = nav::footprint_local(m, u.pos_x[i], u.pos_z[i]);
			const float ex = m.hu + c - std::fabs(l.x), ez = m.hv + c - std::fabs(l.y);
			if (ex > 0.0f && ez > 0.0f) worst = std::max(worst, std::min(ex, ez));
		}
	}
	return worst;
}

// Deepest overlap of any live team-0 unit's building clearance with a tree's, bush's or
// treasure's square (Simulation::gaia_obstacles_), other than the one it works at.
static float gaia_penetration(const Simulation &s) {
	const Simulation::UnitView u = s.units();
	float worst = 0.0f;
	for (uint32_t k = 0; k < s.gaia_obstacles_.size(); ++k) {
		const auto &o = s.gaia_obstacles_[k];
		if (!s.gaia_obstacle_live(k)) continue;
		for (size_t i = 0; i < u.count; ++i) {
			if (!u.alive[i] || u.team[i] != 0 || s.units_.target_resource_id[i] == o.id || s.units_.target_unit_id[i] == o.id)
				continue;
			const float c = unit_building_clearance(u.tmpl[i], u.unit_type[i]);
			const nav::Vec2 l = nav::footprint_local(o, u.pos_x[i], u.pos_z[i]);
			const float ex = o.hu + c - std::fabs(l.x), ez = o.hv + c - std::fabs(l.y);
			if (ex > 0.0f && ez > 0.0f) worst = std::max(worst, std::min(ex, ez));
		}
	}
	return worst;
}

// 21. Trees, bushes and treasures.
static void trees() {
	{
		Simulation s;
		setup(s);
		s.spawn_resource_world(0.0f, 0.0f, RES_WOOD, 200, RES_SUB_NONE, 0, FLORA_TREE_BAOBAB_3_MATURE);
		const uint32_t id = s.spawn_unit(-8.0f, 0.1f, 0, UNIT_SWORDSMAN, 160.0f);
		s.cmd_move(&id, 1, 8.0f, 0.1f);
		int arrived = -1;
		float pen = 0.0f, dev = 0.0f;
		for (int t = 0; t < 1200 && arrived < 0; ++t) {
			s.tick(DT);
			pen = std::max(pen, gaia_penetration(s));
			const int i = index_of(s, id);
			if (i < 0) break;
			const Simulation::UnitView u = s.units();
			if (std::fabs(u.pos_x[i]) < 0.3f) dev = std::max(dev, std::fabs(u.pos_z[i]));
			if (std::hypot(u.pos_x[i] - 8.0f, u.pos_z[i] - 0.1f) <= 1.0f) arrived = t;
		}
		std::printf("  through a baobab: arrived after %d ticks (-1 = never in 1200), %.2f off its middle passing it, "
				"max penetration %.3f\n", arrived, dev, pen);
		check(arrived >= 0, "a swordsman sent through a baobab never got past it");
		check(dev > 6.0f / 8.0f, "a swordsman walked through a baobab");
		check(pen <= MAX_PENETRATION, "a swordsman overlapped a baobab");
	}
	// A wood of oaks 1 unit (4 m) apart, 1.6 m wide: 2.4 m gaps, wider than an infantryman.
	auto wood = [](Simulation &s) {
		for (int x = -4; x <= 4; ++x)
			for (int z = -4; z <= 4; ++z)
				s.spawn_resource_world((float)x + (z & 1) * 0.5f, (float)z, RES_WOOD, 200, RES_SUB_NONE, 0, FLORA_OAK);
	};
	{
		Simulation s;
		setup(s);
		wood(s);
		const uint32_t id = s.spawn_unit(-8.0f, 0.3f, 0, UNIT_SWORDSMAN, 160.0f);
		s.cmd_move(&id, 1, 8.0f, 0.3f);
		int arrived = -1;
		float pen = 0.0f;
		for (int t = 0; t < 2400 && arrived < 0; ++t) {
			s.tick(DT);
			pen = std::max(pen, gaia_penetration(s));
			const int i = index_of(s, id);
			if (i < 0) break;
			const Simulation::UnitView u = s.units();
			if (std::hypot(u.pos_x[i] - 8.0f, u.pos_z[i] - 0.3f) <= 1.0f) arrived = t;
		}
		std::printf("  through a wood: arrived after %d ticks (-1 = never in 2400), max penetration %.3f\n", arrived, pen);
		check(arrived >= 0, "a swordsman never got through a wood");
		check(pen <= MAX_PENETRATION, "a swordsman walked into a tree");
	}
	{
		Simulation s;
		setup(s);
		wood(s);
		uint32_t tree = 0;
		const Simulation::ResourceView rv = s.resources();
		for (size_t r = 0; r < rv.count; ++r)
			if (rv.x[r] == 0.0f && rv.z[r] == 0.0f) tree = rv.id[r];
		std::vector<uint32_t> women;
		for (int k = 0; k < 4; ++k) women.push_back(s.spawn_unit(-8.0f, -1.5f + 1.0f * k, 0, UNIT_FEMALE_CITIZEN));
		s.order_gather(women.data(), women.size(), tree);
		std::vector<bool> worked(women.size(), false);
		float pen = 0.0f;
		for (int t = 0; t < 1600; ++t) {
			s.tick(DT);
			pen = std::max(pen, gaia_penetration(s));
			for (size_t k = 0; k < women.size(); ++k) {
				const int i = index_of(s, women[k]);
				if (i >= 0 && s.units_.carry[i] > 0 && s.units_.res_type_carried[i] == RES_WOOD) worked[k] = true;
			}
		}
		const int n = (int)std::count(worked.begin(), worked.end(), true);
		std::printf("  gather in a wood: %d of %zu women cut wood, max penetration %.3f\n", n, women.size(), pen);
		check(n == (int)women.size(), "a woman never reached the tree she was sent to");
		check(pen <= MAX_PENETRATION, "a woman walked into a tree");
	}
	{
		Simulation s;
		setup(s);
		const uint32_t chest = s.spawn_treasure(0.0f, 0.0f, TREASURE_FOOD_BARRELS_BURIED);
		const uint32_t id = s.spawn_unit(-6.0f, 0.0f, 0, UNIT_SWORDSMAN, 160.0f);
		s.order_collect_treasure(&id, 1, chest);
		bool taken = false;
		float pen = 0.0f;
		for (int t = 0; t < 800 && !taken; ++t) {
			s.tick(DT);
			taken = !s.is_treasure_id(chest);
			if (!taken) pen = std::max(pen, gaia_penetration(s)); // gaia_obstacles_ drops it next tick
		}
		std::printf("  collect a 7 m treasure: %s, max penetration %.3f\n", taken ? "taken" : "never taken", pen);
		check(taken, "a swordsman never collected a 7 m treasure");
		check(pen <= MAX_PENETRATION, "a swordsman overlapped another obstacle on the way to a treasure");
	}
}

// 19. Mines.
static void mines() {
	{
		Simulation s;
		setup(s);
		s.spawn_resource_world(0.0f, 0.0f, RES_STONE, LARGE_MINE_SUPPLY);
		const uint32_t id = s.spawn_unit(-8.0f, 0.2f, 0, UNIT_SWORDSMAN, 160.0f);
		s.cmd_move(&id, 1, 8.0f, 0.2f);
		int arrived = -1;
		float pen = 0.0f, dev = 0.0f;
		for (int t = 0; t < 1200 && arrived < 0; ++t) {
			s.tick(DT);
			pen = std::max(pen, mine_penetration(s));
			const int i = index_of(s, id);
			if (i < 0) break;
			const Simulation::UnitView u = s.units();
			if (std::fabs(u.pos_x[i]) < 0.5f) dev = std::max(dev, std::fabs(u.pos_z[i]));
			if (std::hypot(u.pos_x[i] - 8.0f, u.pos_z[i] - 0.2f) <= 1.0f) arrived = t;
		}
		std::printf("  through a mine: arrived after %d ticks (-1 = never in 1200), %.2f off its middle passing it, "
				"max penetration %.3f\n", arrived, dev, pen);
		check(arrived >= 0, "a swordsman sent through a mine never got past it");
		check(dev > 13.0f / 8.0f, "a swordsman walked through a mine");
		check(pen <= MAX_PENETRATION, "a swordsman overlapped a mine");
	}
	{
		Simulation s;
		setup(s);
		// Large mines 6 units (24 m) apart along z = 0: whichever way each is turned, at least 1.4
		// units lie between two of them.
		for (int k = -3; k <= 3; ++k) s.spawn_resource_world(6.0f * k, 0.0f, RES_STONE, LARGE_MINE_SUPPLY);
		const uint32_t id = s.spawn_unit(-3.0f, -10.0f, 0, UNIT_SWORDSMAN, 160.0f);
		s.cmd_move(&id, 1, -3.0f, 10.0f);
		int arrived = -1;
		float pen = 0.0f, cross_x = 99.0f, last_z = -10.0f;
		for (int t = 0; t < 1200 && arrived < 0; ++t) {
			s.tick(DT);
			pen = std::max(pen, mine_penetration(s));
			const int i = index_of(s, id);
			if (i < 0) break;
			const Simulation::UnitView u = s.units();
			if (last_z < 0.0f && u.pos_z[i] >= 0.0f) cross_x = u.pos_x[i];
			last_z = u.pos_z[i];
			if (std::hypot(u.pos_x[i] + 3.0f, u.pos_z[i] - 10.0f) <= 1.0f) arrived = t;
		}
		std::printf("  across a row of mines: arrived after %d ticks (-1 = never in 1200), crossed it at x = %.2f, "
				"max penetration %.3f\n", arrived, cross_x, pen);
		check(arrived >= 0, "a swordsman never crossed a row of mines");
		check(std::fabs(cross_x + 3.0f) < 1.0f, "a swordsman went round a row of mines instead of through the gap before it");
		check(pen <= MAX_PENETRATION, "a swordsman overlapped a mine in the row");
	}
	{
		Simulation s;
		setup(s);
		const uint32_t mine = s.spawn_resource_world(0.0f, 0.0f, RES_STONE, LARGE_MINE_SUPPLY);
		std::vector<uint32_t> women;
		for (int k = 0; k < 6; ++k) women.push_back(s.spawn_unit(-8.0f, -2.5f + 1.0f * k, 0, UNIT_FEMALE_CITIZEN));
		s.order_gather(women.data(), women.size(), mine);
		std::vector<bool> worked(women.size(), false);
		float pen = 0.0f;
		for (int t = 0; t < 1200; ++t) {
			s.tick(DT);
			pen = std::max(pen, mine_penetration(s));
			for (size_t k = 0; k < women.size(); ++k) {
				const int i = index_of(s, women[k]);
				if (i >= 0 && s.units_.carry[i] > 0 && s.units_.res_type_carried[i] == RES_STONE) worked[k] = true;
			}
		}
		const int n = (int)std::count(worked.begin(), worked.end(), true);
		std::printf("  gather from a mine: %d of %zu women worked it, max penetration %.3f\n", n, women.size(), pen);
		check(n == (int)women.size(), "a woman never reached the mine she was sent to");
		check(pen <= MAX_PENETRATION, "a gatherer overlapped her mine");
	}
}

// 20. Shore clearance.
static void shore_clearance() {
	// channel_setup(s, 16, 0.75) makes the navcells from z -14 to 14 deep (their centres lie under the
	// sea) and a beach no steeper than 1.5 over a tile, so the water alone keeps units off.
	auto gap = [](const Simulation &s, uint32_t id) { return s.units_.pos_z[index_of(s, id)] - 14.0f; };
	for (int large = 0; large < 2; ++large) {
		Simulation s;
		channel_setup(s, 16.0f, 0.75f);
		if (!large) check(s.near_deep_water(0.0f, 13.9f, 0.0f) && !s.near_deep_water(0.0f, 14.1f, 0.0f), "the sea's edge is not at z 14");
		// The spot (20, 14.5) lies half a unit off the water: within an elephant's 0.75, not a swordsman's 0.2.
		const uint32_t id = s.spawn_unit(-20.0f, 18.0f, 0, large ? UNIT_ELEPHANT : UNIT_SWORDSMAN);
		s.cmd_move(&id, 1, 20.0f, 14.5f);
		float min_gap = 99.0f;
		int idle = -1;
		for (int t = 0; t < 1200 && idle < 0; ++t) {
			s.tick(DT);
			min_gap = std::min(min_gap, gap(s, id));
			if (s.units_.action_state[index_of(s, id)] == ACTION_IDLE) idle = t;
		}
		const int i = index_of(s, id);
		const float c = unit_building_clearance(s.units_.tmpl[i], s.units_.unit_type[i]);
		const float off = std::hypot(s.units_.pos_x[i] - 20.0f, s.units_.pos_z[i] - 14.5f);
		std::printf("  shore clearance: %s sent 2 m off the water stopped after %d ticks (-1 = never in 1200), %.2f from "
				"the spot, %.2f off the water at least (clearance %.2f)\n", large ? "elephant" : "swordsman", idle, off, min_gap, c);
		check(idle >= 0, "a walk to the water's edge never ended");
		check(min_gap >= c - 0.01f, "a land unit came within its clearance of deep water");
		check(off <= (large ? 1.0f : 0.5f), "a land unit sent to the water's edge stopped short of where it could stand");
	}
	{
		Simulation s;
		channel_setup(s, 16.0f, 0.75f);
		std::vector<uint32_t> herd;
		for (int k = 0; k < 6; ++k) herd.push_back(s.spawn_unit(0.2f * (float)(k % 3), 14.9f + 0.2f * (float)(k / 3), 0, UNIT_ELEPHANT));
		float min_gap = 99.0f, spread = 0.0f;
		for (int t = 0; t < 200; ++t) {
			s.tick(DT);
			for (uint32_t id : herd) {
				min_gap = std::min(min_gap, gap(s, id));
				spread = std::max(spread, std::fabs(s.units_.pos_x[index_of(s, id)]));
			}
		}
		std::printf("  shore clearance: a clump of 6 elephants on the beach pushed apart to %.2f along it, %.2f off the water at least\n",
				spread, min_gap);
		check(min_gap >= CLEARANCE_LARGE - 0.01f, "pushing carried an elephant within its clearance of deep water");
		check(spread > 2.0f, "a clump of elephants on the beach never pushed apart");
	}
}

// 22. A circular map's edge (0.28 CCmpPathfinder off-world passability, MAP_EDGE_TILES): no unit
// walks, is pushed or spawns past the circle 12 m inside the map's, less its class clearance, and
// no foundation reaches past it.
static void circle_setup(Simulation &s, bool circular) {
	s.set_sim_seed(4242);
	s.reset();
	for (uint8_t p = 0; p < 2; ++p) s.player(p).active = 1, s.player(p).is_ai = 0;
	constexpr int RES = 65;
	constexpr float HALF = 64.0f;
	s.set_map_half(HALF);
	s.set_terrain_data(std::vector<float>((size_t)RES * RES, 3.0f), RES, 0.0f);
	s.set_circular_map(circular);
	s.begin_bulk_spawn();
	s.spawn_building_world(40.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
	s.spawn_building_world(-40.0f, 0.0f, BLD_TOWN_CENTER, 1, 1.0f);
	s.end_bulk_spawn();
}

static void circular_map_edge() {
	const float edge = 64.0f - MAP_EDGE_DIST; // 61
	{
		Simulation s;
		circle_setup(s, true);
		const auto [cx, cy] = s.nav_grid_.world_to_cell(62.0f, 0.0f);
		const auto [ix, iy] = s.nav_grid_.world_to_cell(56.0f, 0.0f);
		check(!s.nav_grid_.is_passable(cx, cy) && s.nav_grid_.is_passable(ix, iy), "the navcells past a circular map's edge stay passable");
		// Sent past the edge, straight out and to the corner, a swordsman stops at it.
		const float goals[2][2] = {{63.5f, 2.0f}, {60.0f, 60.0f}};
		for (const auto &g : goals) {
			const uint32_t id = s.spawn_unit(48.0f, 10.0f, 0, UNIT_SWORDSMAN);
			s.cmd_move(&id, 1, g[0], g[1]);
			float out = 0.0f;
			int idle = -1;
			for (int t = 0; t < 1200 && idle < 0; ++t) {
				s.tick(DT);
				const int i = index_of(s, id);
				out = std::max(out, std::hypot(s.units_.pos_x[i], s.units_.pos_z[i]));
				if (t > 0 && s.units_.action_state[i] == ACTION_IDLE) idle = t;
			}
			const int i = index_of(s, id);
			const float r = std::hypot(s.units_.pos_x[i], s.units_.pos_z[i]);
			std::printf("  circular map: a swordsman sent to (%.1f, %.1f) stopped after %d ticks (-1 = never in 1200) %.2f from the centre, "
					"%.2f at most (edge %.2f less clearance %.2f)\n", g[0], g[1], idle, r, out, edge, CLEARANCE_DEFAULT);
			check(idle >= 0, "a walk past a circular map's edge never ended");
			check(out <= edge - CLEARANCE_DEFAULT + 0.01f, "a unit walked past a circular map's edge");
			check(r >= edge - CLEARANCE_DEFAULT - 1.0f, "a unit sent past a circular map's edge stopped short of it");
			s.units_.alive[i] = 0;
		}
		// A clump of elephants against the edge is never pushed past it.
		std::vector<uint32_t> herd;
		for (int k = 0; k < 6; ++k) herd.push_back(s.spawn_unit(59.6f - 0.2f * (float)(k / 3), -6.0f + 0.2f * (float)(k % 3), 0, UNIT_ELEPHANT));
		float out = 0.0f;
		for (int t = 0; t < 200; ++t) {
			s.tick(DT);
			for (uint32_t id : herd) {
				const int i = index_of(s, id);
				out = std::max(out, std::hypot(s.units_.pos_x[i], s.units_.pos_z[i]));
			}
		}
		std::printf("  circular map: a clump of 6 elephants at the edge pushed apart, %.2f from the centre at most\n", out);
		check(out <= edge - CLEARANCE_LARGE + 0.01f, "pushing carried an elephant past a circular map's edge");
	}
	// A house reaching past the edge can't be placed there, on a circular map or a square one
	// (CCmpPathfinder UpdateGrid makes the outer MAP_EDGE_TILES impassable on both); one further
	// in is placed on the square map, where the circle doesn't cut its corner.
	const int foot = building_type_info(BLD_HOUSE).footprint;
	const int cell_x = (int)std::floor(60.0f / Simulation::GRID) - foot / 2, cell_y = -foot / 2;
	const int in_x = (int)std::floor((64.0f - MAP_EDGE_DIST) / Simulation::GRID) - foot - 1;
	Simulation::PlacementResult res[2], in_square = Simulation::PLACE_OK;
	for (int circular = 0; circular < 2; ++circular) {
		Simulation s;
		circle_setup(s, circular);
		s.set_explore_map(true); // the edge rule, not the shroud
		res[circular] = s.check_placement(0, cell_x, cell_y, BLD_HOUSE);
		if (!circular) in_square = s.check_placement(0, in_x, cell_y, BLD_HOUSE);
	}
	std::printf("  map edge: a house reaching %.1f out placed %d on a square map, %d on a circular one; reaching %.1f, %d on the square (0 = ok)\n",
			(float)(cell_x + foot) * Simulation::GRID, (int)res[0], (int)res[1], (float)(in_x + foot) * Simulation::GRID, (int)in_square);
	check(res[0] == Simulation::PLACE_OBSTRUCTED, "a house reaching past a square map's edge frame was placed");
	check(in_square == Simulation::PLACE_OK, "a house inside a square map's edge frame couldn't be placed");
	check(res[1] == Simulation::PLACE_OBSTRUCTED, "a house reaching past a circular map's edge was placed");
}

// 23. A wood too thick to walk through closes its navcells to long paths (0 A.D. rasterizes each
// tree grown by the class clearance onto 1 m navcells, so oaks even 4 m apart leave no navcell
// between them), a sparse one doesn't, and felling a lane through the thick one opens it again.
static void forest_cells() {
	Simulation s;
	setup(s);
	auto cell_at = [&](float x, float z) {
		const auto [cx, cy] = s.nav_grid_.world_to_cell(x, z);
		return s.nav_grid_.cells.at(cx, cy);
	};
	std::vector<uint32_t> lane;
	for (int x = -12; x <= 12; ++x)
		for (int z = -12; z <= 12; ++z) {
			const uint32_t id = s.spawn_resource_world(0.5f * (float)x, 0.5f * (float)z, RES_WOOD, 200, RES_SUB_NONE, 0, FLORA_OAK);
			if (std::abs(z) <= 1) lane.push_back(id);
		}
	for (int x = -3; x <= 3; ++x)
		for (int z = -3; z <= 3; ++z)
			s.spawn_resource_world(20.0f + 2.0f * (float)x, 2.0f * (float)z, RES_WOOD, 200, RES_SUB_NONE, 0, FLORA_OAK);
	s.tick(DT);
	const bool thick = cell_at(0.0f, 3.0f) == NAV_OBSTRUCTED, sparse = cell_at(21.0f, 1.0f) == NAV_OBSTRUCTED;
	for (uint32_t id : lane) s.set_resource_amount(id, 0);
	s.tick(DT);
	const bool felled = cell_at(0.0f, 0.0f) == NAV_OBSTRUCTED, rest = cell_at(0.0f, 3.0f) == NAV_OBSTRUCTED;
	std::printf("  woods: a wood of oaks 2 m apart closes its navcells (%d), one 8 m apart doesn't (%d); "
			"a lane felled through the first opens (%d) and the rest stays shut (%d)\n", thick, !sparse, !felled, rest);
	check(thick, "a wood of oaks 2 m apart stayed open to long paths");
	check(!sparse, "a wood of oaks 8 m apart was closed to long paths");
	check(!felled, "a lane felled through a thick wood stayed closed to long paths");
	check(rest, "felling a lane opened the rest of a thick wood");

	// A wood of oaks 6.4 m apart (4.8 m gaps): open to infantry (0.8 m clearance), closed to the
	// large class (3 m: siege, elephants), whose long paths go round it.
	Simulation m;
	setup(m);
	for (int x = -4; x <= 4; ++x)
		for (int z = -4; z <= 4; ++z)
			m.spawn_resource_world(1.6f * (float)x, 1.6f * (float)z, RES_WOOD, 200, RES_SUB_NONE, 0, FLORA_OAK);
	m.tick(DT);
	const auto [wx, wy] = m.nav_grid_.world_to_cell(0.0f, 0.0f);
	const bool inf_open = m.nav_grid_.is_passable_for(wx, wy, PASS_INFANTRY);
	const bool large_open = m.nav_grid_.is_passable_for(wx, wy, PASS_INFANTRY | PASS_LARGE);
	WaypointPath inf, big;
	m.find_path(-12.0f, 0.0f, 12.0f, 0.0f, inf, PASS_INFANTRY);
	const bool big_found = m.find_path(-12.0f, 0.0f, 12.0f, 0.0f, big, PASS_INFANTRY | PASS_LARGE);
	auto off_axis = [](const WaypointPath &p) {
		float d = 0.0f;
		for (int k = 0; k < p.count; ++k) d = std::max(d, std::fabs(p.zs[k]));
		return d;
	};
	std::printf("  large class: a wood with 4.8 m gaps is open to infantry (%d), closed to siege (%d); "
			"paths across stray %.1f (infantry) and %.1f (large) off the line\n", inf_open, !large_open, off_axis(inf), off_axis(big));
	check(inf_open, "a wood with 4.8 m gaps was closed to infantry");
	check(!large_open, "a wood with 4.8 m gaps was open to the large class");
	check(big_found && off_axis(big) > 6.4f, "a large unit's long path crossed a wood it can't fit through");
	check(off_axis(inf) < 6.4f, "an infantryman's long path went round a wood it fits through");
	// With no way round, the large class falls back to the default class's path.
	Simulation c;
	setup(c);
	const float half = c.map_half();
	for (float z = -half + 0.8f; z < half; z += 1.6f)
		for (int x = -2; x <= 2; ++x)
			c.spawn_resource_world(1.6f * (float)x, z, RES_WOOD, 200, RES_SUB_NONE, 0, FLORA_OAK);
	c.tick(DT);
	WaypointPath across;
	const bool across_found = c.find_path(-12.0f, 0.0f, 12.0f, 0.0f, across, PASS_INFANTRY | PASS_LARGE);
	std::printf("  large class: across a wood from edge to edge it takes the infantry path (%d)\n", across_found);
	check(across_found, "a large unit found no path across a wood it can only cross on the default class");
}

// 24. Repair in a house cluster (soak: Petra women stood 60 s or more at a neighbour's corner). The
// two clusters are Petra's from soak matches, round the damaged house at the origin; each woman
// comes from where one stood, and five of them cover every stand point spread (unit id % 5).
static void repair_in_house_cluster() {
	struct Case { const char *what; std::vector<std::pair<float, float>> nb; float from[2]; };
	const Case cases[2] = {
		{"a diagonal row touching its corner", {{2, 2}, {4, 4}, {6, 4}, {6, 6}, {8, 6}, {8, 8}}, {3.23f, 1.25f}},
		{"a corner shut by two neighbours", {{0, -2}, {-2, -2}, {-2, -4}, {-4, -4}, {-6, -6}, {-8, -8}}, {1.0f, -3.23f}},
	};
	for (const Case &c : cases) {
		int repaired = 0;
		for (int k = 0; k < 5; ++k) {
			Simulation s;
			setup(s);
			const uint32_t house = s.spawn_building_world(0.0f, 0.0f, BLD_HOUSE, 0, 1.0f);
			for (const auto &n : c.nb) s.spawn_building_world(n.first, n.second, BLD_HOUSE, 0, 1.0f);
			s.tick(DT);
			float max_hp = 0.0f;
			for (auto &b : s.buildings_) if (b.id == house) { max_hp = b.max_hp; b.hp = max_hp - 20.0f; }
			for (int d = 0; d < k; ++d) s.spawn_unit(-40.0f, -40.0f + 2.0f * (float)d, 0, UNIT_FEMALE_CITIZEN);
			const uint32_t woman = s.spawn_unit(c.from[0], c.from[1], 0, UNIT_FEMALE_CITIZEN);
			s.order_build_or_repair(&woman, 1, house);
			float hp = 0.0f;
			for (int t = 0; t < 400; ++t) {
				s.tick(DT);
				for (const auto &b : s.buildings()) if (b.id == house) hp = b.hp;
				if (hp >= max_hp) break;
			}
			if (hp >= max_hp) ++repaired;
		}
		std::printf("  repair, %s: %d of 5 women repaired the house within 20 s\n", c.what, repaired);
		check(repaired == 5, "a woman never repaired a house whose near side a neighbour shuts");
	}
}

// 25. Cliff clearance. A flat map with heights every unit (a terrain tile a unit, as on generated
// maps), a cliff rising 8 over the tile x 0 to 1, and a rock: the height sample at (-20, -9) raised
// 3, so the four tiles round it are steep and the 2-unit navcell holding them (its corners flat) is not.
static void cliff_clearance_setup(Simulation &s) {
	setup(s);
	constexpr int RES = 129;
	constexpr float HALF = 64.0f;
	s.set_map_half(HALF);
	std::vector<float> h((size_t)RES * RES);
	for (int j = 0; j < RES; ++j)
		for (int i = 0; i < RES; ++i) {
			const float x = -HALF + 2.0f * HALF * i / (RES - 1), z = -HALF + 2.0f * HALF * j / (RES - 1);
			h[(size_t)j * RES + i] = x >= 1.0f ? 8.0f : (x == -20.0f + 1.0f && z == -9.0f) ? 3.0f : 0.0f;
		}
	s.set_terrain_data(h, RES, -1.0f);
	s.begin_bulk_spawn();
	s.end_bulk_spawn();
}

static void cliff_clearance() {
	{
		Simulation s;
		cliff_clearance_setup(s);
		const auto [cx, cy] = s.nav_grid_.world_to_cell(-19.0f, -9.0f);
		check(s.nav_grid_.near_steep_tile(-19.0f, -9.0f, 0.0f) && !s.nav_grid_.is_steep(cx, cy),
				"the rock is not a steep tile in a navcell that is not steep");
		const uint32_t id = s.spawn_unit(-30.0f, -9.0f, 0, UNIT_SWORDSMAN);
		s.cmd_move(&id, 1, -8.0f, -9.0f);
		int arrived = -1, near = 0;
		for (int t = 0; t < 1200 && arrived < 0; ++t) {
			s.tick(DT);
			const int i = index_of(s, id);
			near += s.nav_grid_.near_steep_tile(s.units_.pos_x[i], s.units_.pos_z[i], CLEARANCE_DEFAULT - 0.01f);
			if (std::hypot(s.units_.pos_x[i] + 8.0f, s.units_.pos_z[i] + 9.0f) < 0.5f) arrived = t;
		}
		std::printf("  cliff clearance: a swordsman walked past a rock in %d ticks (-1 = never in 1200), %d ticks within its clearance of it\n",
				arrived, near);
		check(arrived >= 0, "a walk past a rock never got there");
		check(near == 0, "a walker came within its clearance of a steep tile");
	}
	for (int large = 0; large < 2; ++large) {
		Simulation s;
		cliff_clearance_setup(s);
		const uint32_t id = s.spawn_unit(-10.0f, 6.0f, 0, large ? UNIT_ELEPHANT : UNIT_SWORDSMAN);
		s.cmd_move(&id, 1, -0.1f, 6.0f);
		float max_x = -99.0f;
		int idle = -1;
		for (int t = 0; t < 1200 && idle < 0; ++t) {
			s.tick(DT);
			max_x = std::max(max_x, s.units_.pos_x[index_of(s, id)]);
			if (s.units_.action_state[index_of(s, id)] == ACTION_IDLE) idle = t;
		}
		const int i = index_of(s, id);
		const float c = unit_building_clearance(s.units_.tmpl[i], s.units_.unit_type[i]);
		std::printf("  cliff clearance: %s sent to the cliff's foot stopped after %d ticks (-1 = never in 1200) at x %.2f, "
				"%.2f off the cliff at least (clearance %.2f)\n", large ? "elephant" : "swordsman", idle, s.units_.pos_x[i], -max_x, c);
		check(idle >= 0, "a walk to a cliff's foot never ended");
		check(-max_x >= c - 0.01f, "a land unit came within its clearance of a cliff");
		check(s.units_.pos_x[i] >= -c - 0.5f, "a land unit sent to a cliff's foot stopped short of where it could stand");
	}
	{
		// A unit set down within its clearance of the rock may walk on across it, as CheckLineMovement
		// lets a unit on impassable navcells cross more of them; it stuck there before.
		Simulation s;
		cliff_clearance_setup(s);
		const uint32_t id = s.spawn_unit(-20.1f, -9.0f, 0, UNIT_SWORDSMAN);
		const bool inside = s.nav_grid_.near_steep_tile(-20.1f, -9.0f, CLEARANCE_DEFAULT) && !s.nav_grid_.near_steep_tile(-20.1f, -9.0f, 0.0f);
		s.cmd_move(&id, 1, -16.0f, -9.0f);
		int arrived = -1;
		for (int t = 0; t < 600 && arrived < 0; ++t) {
			s.tick(DT);
			const int i = index_of(s, id);
			if (std::hypot(s.units_.pos_x[i] + 16.0f, s.units_.pos_z[i] + 9.0f) < 0.5f) arrived = t;
		}
		std::printf("  cliff clearance: a swordsman inside the rock's clearance (%s) walked across it in %d ticks (-1 = never in 600)\n",
				inside ? "yes" : "no", arrived);
		check(inside, "the swordsman didn't start within its clearance of the rock");
		check(arrived >= 0, "a unit within its clearance of a rock couldn't walk across it");
	}
	{
		// An oak beside the rock's corner (soak: a javelineer on an Islands cliff top jittered for
		// minutes): the short path's corner round the oak lies just off the rock's margin, the step
		// past it inside. The step that reaches the corner ends on it, as PerformMove moves a unit
		// within its step of a waypoint onto it; it was refused whole, the corner was taken as reached
		// and the unit walked to the oak's far corner and back for good. Every walk ends.
		int endless = 0;
		for (int a = 0; a <= 12; ++a)
			for (int b = 0; b <= 12; ++b) {
				Simulation s;
				cliff_clearance_setup(s);
				s.spawn_resource_world(-20.9f + 0.05f * (float)a, -11.0f + 0.05f * (float)b, RES_WOOD, 200, RES_SUB_NONE, 0, FLORA_OAK);
				const uint32_t id = s.spawn_unit(-18.12f, -15.67f, 0, UNIT_SWORDSMAN);
				s.cmd_move(&id, 1, -21.0f, -10.1f);
				int t = 0;
				while (t < 600 && s.units_.action_state[index_of(s, id)] != ACTION_IDLE) {
					s.tick(DT);
					++t;
				}
				endless += t == 600;
			}
		std::printf("  cliff clearance: %d of 169 walks round an oak beside the rock never ended in 600 ticks\n", endless);
		check(endless == 0, "a walk round a tree beside a steep tile never ended");
	}
	// building-land judges slopes per tile too: no house on the rock, one beside it. The cliff cuts
	// the land in two as Petra sees it (petra_access_at); the rock doesn't.
	Simulation s;
	cliff_clearance_setup(s);
	s.set_explore_map(true); // the slope rule, not the shroud
	const int on = (int)s.check_placement(0, -10, -5, BLD_HOUSE), beside = (int)s.check_placement(0, -12, -5, BLD_HOUSE);
	const uint16_t below = s.petra_access_at(-5.0f, 0.0f), above = s.petra_access_at(5.0f, 0.0f);
	const uint16_t west = s.petra_access_at(-25.0f, -9.0f), east = s.petra_access_at(-15.0f, -9.0f);
	std::printf("  cliff clearance: a house on the rock placed %d, beside it %d (2 = terrain, 3 = no territory); land below the "
			"cliff %u, above %u, either side of the rock %u and %u\n", on, beside, below, above, west, east);
	check(on == Simulation::PLACE_TERRAIN, "a house was placed over steep tiles");
	check(beside != Simulation::PLACE_TERRAIN, "a house beside a rock was refused for its terrain");
	check(below && above && below != above, "a cliff didn't cut the land");
	check(west && west == east && west == below, "a rock cut the land");
}

// 26. A tree gap that trees behind it close (soak: Mainland seed 22, a spearman walking to its
// civic centre stood between two corners for minutes). The corner into the gap is a dead end the
// next search turns back from, so the short path takes the way round. Trees as they stood there,
// relative to the spearman.
static void tree_gap_dead_end() {
	Simulation s;
	setup(s);
	static const float trees[][2] = {{0.39f, -1.97f}, {0.92f, -1.24f}, {0.43f, -0.49f}, {-0.46f, -0.37f},
		{2.76f, -2.63f}, {2.92f, -1.74f}, {-2.05f, -5.58f}, {-2.89f, -5.89f}, {-1.52f, -3.75f}, {-1.99f, -2.98f},
		{-2.95f, -2.39f}, {-2.75f, -3.27f}, {-2.15f, -4.48f}, {-2.20f, -3.58f}, {-2.47f, -5.42f}, {2.60f, -1.37f},
		{3.43f, -1.71f}, {-3.84f, -3.64f}, {-0.15f, -2.14f}, {-0.98f, -2.50f}};
	for (const auto &t : trees) s.spawn_resource_world(t[0], t[1], RES_WOOD, 200, RES_SUB_NONE, 0, FLORA_TREE_CYPRESS_WILD);
	s.tick(DT);
	// The short path from where the spearman stood, then from the corner it gives: the second leg
	// must not turn back (it flipped between the two corners for minutes).
	auto steer = [&](nav::Vec2 pos) {
		std::vector<Simulation::SteerObstacle> near;
		ShortPathMemo memo{};
		nav::Vec2 out = pos;
		const nav::Vec2 aim(-0.06f, -9.0f);
		nav::steer_around_obstacles(pos, aim, CLEARANCE_DEFAULT, s.steer_obstacles_, false, near, true,
				[&](nav::Vec2 c, std::vector<Simulation::SteerObstacle> &o) { s.gaia_obstacles_near(c.x, c.y, Simulation::GAIA_STEER_REACH, o); },
				Simulation::GAIA_STEER_REACH, 0, s.nav_grid_, 1u, memo, out);
		return out;
	};
	const nav::Vec2 start(0.09f, 0.21f), w1 = steer(start), w2 = steer(w1);
	const float back = (w1 - start).dot(w2 - w1); // < 0: the way on from the corner turns back
	std::printf("  tree gap: from (%.2f, %.2f) the short path goes to (%.2f, %.2f), then (%.2f, %.2f)\n", start.x, start.y, w1.x, w1.y, w2.x, w2.y);
	check(back >= 0.0f, "the short path turns back at a tree gap that trees behind it close");
}

// 27. Thin ridges. Heights every unit, as in cliff_clearance_setup, and the samples along x = 1 for
// z in [-40, 30] raised 3: the tiles x 0 to 2 there are steep, the navcells holding them (their
// corners at even x, flat) are not, and the way round is past the ridge's north end, 31 units up,
// further than a short path searches. With thick_middle the samples at x = 0 and x = 2 are raised
// too for z in [-4, 4], so the navcells either side of the ridge there are steep: a line across it
// there crosses navcells no land unit can (line_crosses_impassable), and an attacker closing in is
// pathed. With gap tiles, the samples at z = 0 to gap are left flat: a gap that many tiles wide.
static void thin_ridge_setup(Simulation &s, bool thick_middle = false, int gap = 0) {
	setup(s);
	constexpr int RES = 129;
	constexpr float HALF = 64.0f;
	s.set_map_half(HALF);
	std::vector<float> h((size_t)RES * RES);
	for (int j = 0; j < RES; ++j)
		for (int i = 0; i < RES; ++i) {
			const float x = -HALF + 2.0f * HALF * i / (RES - 1), z = -HALF + 2.0f * HALF * j / (RES - 1);
			bool raised = x == 1.0f && z >= -40.0f && z <= 30.0f;
			raised = raised || (thick_middle && (x == 0.0f || x == 2.0f) && z >= -4.0f && z <= 4.0f);
			if (gap > 0 && z >= 0.0f && z <= (float)gap) raised = false;
			h[(size_t)j * RES + i] = raised ? 3.0f : 0.0f;
		}
	s.set_terrain_data(h, RES, -1.0f);
	s.begin_bulk_spawn();
	s.end_bulk_spawn();
}

static void thin_ridges() {
	{
		Simulation s;
		thin_ridge_setup(s);
		const auto [cx, cy] = s.nav_grid_.world_to_cell(1.0f, 0.0f);
		check(s.nav_grid_.near_steep_tile(1.0f, 0.0f, 0.0f) && !s.nav_grid_.is_steep(cx, cy),
				"the ridge is not steep tiles in navcells that are not steep");
		const uint32_t id = s.spawn_unit(-6.0f, 0.0f, 0, UNIT_SWORDSMAN);
		s.cmd_move(&id, 1, 6.0f, 0.0f);
		int arrived = -1, near = 0;
		for (int t = 0; t < 2400 && arrived < 0; ++t) {
			s.tick(DT);
			const int i = index_of(s, id);
			near += s.nav_grid_.near_steep_tile(s.units_.pos_x[i], s.units_.pos_z[i], CLEARANCE_DEFAULT - 0.01f);
			if (std::hypot(s.units_.pos_x[i] - 6.0f, s.units_.pos_z[i]) < 0.5f) arrived = t;
		}
		std::printf("  thin ridge: a walk across it arrived after %d ticks (-1 = never in 2400), %d ticks within its clearance of it\n",
				arrived, near);
		check(arrived >= 0, "a walk across a thin ridge never got round it");
		check(near == 0, "a walker came within its clearance of a thin ridge");
	}
	{
		// RETURNRESOURCE.APPROACHING to a storehouse across the ridge.
		Simulation s;
		thin_ridge_setup(s);
		const uint32_t store = s.spawn_building_world(7.0f, 0.0f, BLD_STOREHOUSE, 0, 1.0f);
		const uint32_t woman = s.spawn_unit(-6.0f, 0.0f, 0, UNIT_FEMALE_CITIZEN);
		const int w = index_of(s, woman);
		s.units_.res_type_carried[w] = RES_WOOD;
		s.units_.carry[w] = 10;
		const int wood0 = s.player_resource(0, RES_WOOD);
		s.order_return_resource(&woman, 1, store);
		int dropped = -1;
		for (int t = 0; t < 2400 && dropped < 0; ++t) {
			s.tick(DT);
			if (s.player_resource(0, RES_WOOD) >= wood0 + 10) dropped = t;
		}
		const int i = index_of(s, woman);
		const float from_store = std::hypot(s.units_.pos_x[i] - 7.0f, s.units_.pos_z[i]);
		std::printf("  thin ridge: a woman returning wood across it dropped it after %d ticks (-1 = never in 2400), last at (%.2f, %.2f), "
				"%.1f from the storehouse\n", dropped, s.units_.pos_x[i], s.units_.pos_z[i], from_store);
		check(dropped >= 0, "a woman never got round a thin ridge to her storehouse");
		// At the storehouse, not at setup()'s town centre (-52, -52), where UnitAI sends her when she
		// can't get round (the nearest other dropsite).
		check(dropped >= 0 && from_store < 4.0f, "a woman returning wood across a thin ridge dropped it elsewhere than at her storehouse");
	}
	{
		// The same return, but the woman still holds the path straight across the ridge that a long
		// path on the navcells alone gave (a game saved before, or a path laid before the tiles were
		// looked at): she walks to the ridge's margin, can't go on, and HandleObstructedMove plans the
		// path again (ShouldAlternatePathfinder) instead of leaving her there for good.
		Simulation s;
		thin_ridge_setup(s);
		const uint32_t store = s.spawn_building_world(7.0f, 0.0f, BLD_STOREHOUSE, 0, 1.0f);
		const uint32_t woman = s.spawn_unit(-6.0f, 0.0f, 0, UNIT_FEMALE_CITIZEN);
		int w = index_of(s, woman);
		s.units_.res_type_carried[w] = RES_WOOD;
		s.units_.carry[w] = 10;
		const int wood0 = s.player_resource(0, RES_WOOD);
		s.order_return_resource(&woman, 1, store);
		s.tick(DT);
		w = index_of(s, woman);
		const float gx = s.units_.target_x[w], gz = s.units_.target_z[w];
		WaypointPath &wp = s.unit_paths_[w];
		wp.clear();
		wp.push_back(-1.0f, 0.0f);
		wp.push_back(3.0f, 0.0f);
		wp.push_back(gx, gz);
		wp.has_goal = true;
		wp.goal_x = gx;
		wp.goal_z = gz;
		s.units_.waypoint_cur[w] = 0;
		s.units_.waypoint_count[w] = (int16_t)wp.count;
		int dropped = -1, at_ridge = -1;
		for (int t = 0; t < 2400 && dropped < 0; ++t) {
			s.tick(DT);
			const int i = index_of(s, woman);
			if (at_ridge < 0 && s.units_.pos_x[i] > -0.6f) at_ridge = t;
			if (s.player_resource(0, RES_WOOD) >= wood0 + 10) dropped = t;
		}
		const int i = index_of(s, woman);
		const float from_store = std::hypot(s.units_.pos_x[i] - 7.0f, s.units_.pos_z[i]);
		std::printf("  thin ridge: a woman holding a path across it reached its margin after %d ticks, dropped the wood after %d "
				"(-1 = never in 2400), last at (%.2f, %.2f), %.1f from the storehouse\n", at_ridge, dropped, s.units_.pos_x[i],
				s.units_.pos_z[i], from_store);
		check(at_ridge >= 0, "a woman holding a path across a thin ridge never walked up to it");
		check(dropped >= 0, "a woman holding a path across a thin ridge never planned it again");
		check(dropped >= 0 && from_store < 4.0f, "a woman holding a path across a thin ridge dropped the wood elsewhere than at her storehouse");
	}
	{
		// GARRISON.APPROACHING a tower whose west face, the side the unit comes from, is the ridge's
		// margin: the point beside it on that side is no place to stand.
		Simulation s;
		thin_ridge_setup(s);
		const float hu = building_type_info(BLD_TOWER).footprint * Simulation::GRID * 0.5f;
		const uint32_t tower = s.spawn_building_world(2.0f + hu, 0.0f, BLD_TOWER, 0, 1.0f);
		const uint32_t id = s.spawn_unit(-6.0f, 0.5f, 0, UNIT_SPEARMAN);
		s.order_garrison(&id, 1, tower);
		const int i0 = index_of(s, id);
		const float aim_x = s.units_.target_x[i0], aim_z = s.units_.target_z[i0];
		int in = -1;
		float last_x = -6.0f, last_z = 0.5f;
		for (int t = 0; t < 2400 && in < 0; ++t) {
			s.tick(DT);
			const int i = index_of(s, id);
			if (i < 0 || !s.units().alive[i]) in = t;
			else last_x = s.units_.pos_x[i], last_z = s.units_.pos_z[i];
		}
		std::printf("  thin ridge: a spearman sent into a tower beside it aimed at (%.2f, %.2f), got in after %d ticks "
				"(-1 = never in 2400), last seen at (%.2f, %.2f)\n", aim_x, aim_z, in, last_x, last_z);
		check(!s.nav_grid_.near_steep_tile(aim_x, aim_z, CLEARANCE_DEFAULT), "a garrison walk aimed at a point within a cliff's margin");
		check(in >= 0, "a unit never got round a thin ridge into the tower beside it");
	}
	{
		// A hunt across the ridge's thick middle: the spearman's line to the deer crosses steep
		// navcells, so it is pathed, and the path goes round the ridge's end on the tiles, its corner
		// waypoints closer together than the spearman's reach. It stops for the deer, not for them.
		Simulation s;
		thin_ridge_setup(s, true);
		const uint32_t deer = s.spawn_fauna(6.0f, 0.5f, FAUNA_DEER);
		s.fauna_.hp[s.fauna_id_to_idx_.at(deer)] = 1.0f; // one blow kills it
		const uint32_t id = s.spawn_unit(-6.0f, 0.5f, 0, UNIT_SPEARMAN);
		s.order_hunt(&id, 1, deer);
		int killed = -1, near = 0, max_path = 0, stalled = 0;
		for (int t = 0; t < 1200 && killed < 0; ++t) {
			s.tick(DT);
			const int i = index_of(s, id);
			near += s.nav_grid_.near_steep_tile(s.units_.pos_x[i], s.units_.pos_z[i], CLEARANCE_DEFAULT - 0.01f);
			max_path = std::max(max_path, (int)s.units_.waypoint_count[i]);
			const auto f = s.fauna_id_to_idx_.find(deer);
			if (f == s.fauna_id_to_idx_.end() || !s.fauna_.alive[f->second] || s.fauna_.hp[f->second] <= 0.0f) {
				killed = t;
				break;
			}
			// Standing still while the deer is well out of a spear's reach: stopped short of it.
			const float dx = s.fauna_.pos_x[f->second] - s.units_.pos_x[i], dz = s.fauna_.pos_z[f->second] - s.units_.pos_z[i];
			const float v2 = s.units_.vel_x[i] * s.units_.vel_x[i] + s.units_.vel_z[i] * s.units_.vel_z[i];
			stalled += v2 < 1e-4f && dx * dx + dz * dz > 3.0f * 3.0f;
		}
		const int i = index_of(s, id);
		std::printf("  thin ridge: a spearman hunting a deer beyond it, along a path of up to %d waypoints, killed it after %d ticks "
				"(-1 = never in 1200), stood still out of reach %d ticks, %d ticks within its clearance of the ridge, "
				"last at (%.2f, %.2f)\n", max_path, killed, stalled, near, s.units_.pos_x[i], s.units_.pos_z[i]);
		check(max_path >= 2, "a hunter across a thick ridge was not pathed");
		check(killed >= 0, "a melee hunter never reached a deer beyond a thin ridge");
		check(stalled < 20, "a melee hunter stopped short of a deer beyond a thin ridge");
		check(near == 0, "a hunter came within its clearance of a thin ridge");
	}
}

// 28. A ridge the navcells miss. Heights every unit (a terrain tile a unit), and a ridge along z, 3
// high on the line x = 1 and flat at x = 0 and x = 2, from the map's south edge to z = 30: the tiles
// either side of it are steep, the 2-unit navcells holding them (their corners all at 0) are not.
// Beyond z = 30 the ground is flat. A long path over the navcells runs straight across the ridge,
// where no land unit may step, and the way round is far beyond a short path's reach.
// 29. A pass the navcells close: the ridge on the line x = 0 instead, a navcell corner line, across
// the whole map but for a pass one tile wide (the height samples at z = 11 and z = 12 left flat).
// Every navcell along the ridge is steep by its corners, the pass among them, so the navcells split
// the map; the tiles of the pass are level, and 0.28's navcells, which take their tiles' slopes,
// leave the pass open.
static void ridge_setup(Simulation &s, bool pass) {
	setup(s);
	constexpr int RES = 129;
	constexpr float HALF = 64.0f;
	s.set_map_half(HALF);
	std::vector<float> h((size_t)RES * RES);
	for (int j = 0; j < RES; ++j)
		for (int i = 0; i < RES; ++i) {
			const float x = -HALF + 2.0f * HALF * i / (RES - 1), z = -HALF + 2.0f * HALF * j / (RES - 1);
			const bool ridge = pass ? x == 0.0f && z != 11.0f && z != 12.0f : x == 1.0f && z < 30.0f;
			h[(size_t)j * RES + i] = ridge ? 3.0f : 0.0f;
		}
	s.set_terrain_data(h, RES, -1.0f);
	s.begin_bulk_spawn();
	s.end_bulk_spawn();
}

// From west of the ridge to east of it: a walk (kind 0), a walk to a dropsite (1, Petra's
// ReturnResource) or a gatherer's return with its load (2, GATHER.RETURNINGRESOURCE). Sets the tick
// it got there (-1: never in 4800) and the ticks spent within its clearance of a steep tile.
static void walk_across_ridge(bool pass, int kind, int &done, int &near) {
	Simulation s;
	ridge_setup(s, pass);
	const uint32_t store = s.spawn_building_world(12.0f, -20.0f, BLD_STOREHOUSE, 0, 1.0f);
	s.spawn_building_world(30.0f, -40.0f, BLD_TOWN_CENTER, 0, 1.0f); // its territory, so it doesn't decay
	uint32_t id = s.spawn_unit(-8.0f, -20.0f, 0, kind ? UNIT_VILLAGER : UNIT_SWORDSMAN, 0.0f);
	int i = index_of(s, id);
	const int wood0 = s.player_resource(0, RES_WOOD);
	if (kind == 0) {
		s.cmd_move(&id, 1, 8.0f, -20.0f);
	} else if (kind == 1) {
		s.units_.carry[i] = 10.0f;
		s.units_.res_type_carried[i] = RES_WOOD;
		s.order_return_resource(&id, 1, store);
	} else {
		const uint32_t tree = s.spawn_resource_world(-10.0f, -20.0f, RES_WOOD, 5000);
		s.order_gather(&id, 1, tree);
	}
	done = -1;
	near = 0;
	for (int t = 0; t < 4800 && done < 0; ++t) {
		s.tick(DT);
		i = index_of(s, id);
		near += s.nav_grid_.near_steep_tile(s.units_.pos_x[i], s.units_.pos_z[i], CLEARANCE_DEFAULT - 0.01f);
		if (kind == 0 ? std::hypot(s.units_.pos_x[i] - 8.0f, s.units_.pos_z[i] + 20.0f) < 0.5f
		              : s.player_resource(0, RES_WOOD) > wood0)
			done = t;
	}
}

static void ridge_from_map_edge_and_pass() {
	static const char *what[] = {"a walk", "a walk to a dropsite", "a gatherer's return"};
	{
		Simulation s;
		ridge_setup(s, false);
		const auto [cx, cy] = s.nav_grid_.world_to_cell(1.0f, -20.0f);
		check(s.nav_grid_.near_steep_tile(1.0f, -20.0f, 0.0f) && !s.nav_grid_.is_steep(cx, cy),
				"the ridge is not steep tiles in navcells that are not steep");
		check(s.petra_access_at(-10.0f, -20.0f) == s.petra_access_at(10.0f, -20.0f), "the ridge cut the land");
	}
	// 28. All three go round the ridge's end, as 0.28's long path, planned on the tiles' slopes, does,
	// and never come within their clearance of a steep tile.
	for (int kind = 0; kind < 3; ++kind) {
		int done, near;
		walk_across_ridge(false, kind, done, near);
		std::printf("  ridge the navcells miss: %s across it got there after %d ticks (-1 = never in 4800), %d ticks within its "
				"clearance of a steep tile\n", what[kind], done, near);
		check(done >= 0, "a unit never got round a ridge its navcells miss");
		check(near == 0, "a unit came within its clearance of a ridge");
	}
	{
		Simulation s;
		ridge_setup(s, true);
		const auto [cx, cy] = s.nav_grid_.world_to_cell(0.5f, 11.5f);
		check(!s.nav_grid_.near_steep_tile(0.5f, 11.5f, CLEARANCE_DEFAULT) && s.nav_grid_.is_steep(cx, cy),
				"the pass is not level tiles in a navcell that is steep");
	}
	// 29. All three go through the pass instead of stopping at the navcell nearest the far side.
	for (int kind = 0; kind < 3; ++kind) {
		int done, near;
		walk_across_ridge(true, kind, done, near);
		std::printf("  pass the navcells close: %s across the ridge got there after %d ticks (-1 = never in 4800), %d ticks "
				"within its clearance of a steep tile\n", what[kind], done, near);
		check(done >= 0, "a unit never took a pass its navcells close");
		check(near == 0, "a unit came within its clearance of the ridge by the pass");
	}
}

// 30. Large units and a one-tile gap in a thin ridge. The gap's tiles leave a swordsman (0.8 m
// clearance) room; an elephant's or a ram's 3 m reaches the steep tiles either side, so 0.28's
// "large" class (ExpandImpassableCells grows impassable terrain by it) has no way through there.
static void large_units_and_one_tile_gap() {
	{
		Simulation s;
		thin_ridge_setup(s, false, 1);
		check(!s.nav_grid_.near_steep_tile(0.5f, 0.5f, CLEARANCE_DEFAULT) && s.nav_grid_.near_steep_tile(0.5f, 0.5f, CLEARANCE_LARGE),
				"the gap is not open to the default class and closed to the large one");
	}
	struct Walker { UnitType type; bool large; const char *name; };
	for (const Walker w : {Walker{UNIT_SWORDSMAN, false, "a swordsman"}, Walker{UNIT_ELEPHANT, true, "an elephant"},
			Walker{UNIT_SIEGE_RAM, true, "a ram"}}) {
		Simulation s;
		thin_ridge_setup(s, false, 1);
		const uint32_t id = s.spawn_unit(-6.0f, 0.5f, 0, w.type);
		s.cmd_move(&id, 1, 6.0f, 0.5f);
		const float c = w.large ? CLEARANCE_LARGE : CLEARANCE_DEFAULT;
		int arrived = -1, near = 0;
		float max_dev = 0.0f;
		for (int t = 0; t < 4800 && arrived < 0; ++t) {
			s.tick(DT);
			const int i = index_of(s, id);
			if (i < 0) break;
			near += s.nav_grid_.near_steep_tile(s.units_.pos_x[i], s.units_.pos_z[i], c - 0.01f);
			max_dev = std::max(max_dev, std::fabs(s.units_.pos_z[i] - 0.5f));
			if (std::hypot(s.units_.pos_x[i] - 6.0f, s.units_.pos_z[i] - 0.5f) < 1.0f) arrived = t;
		}
		std::printf("  one-tile gap: %s arrived after %d ticks (-1 = never in 4800), at most %.1f off the gap's line, %d ticks "
				"within its clearance of the ridge\n", w.name, arrived, max_dev, near);
		check(arrived >= 0, "a unit never got across a thin ridge with a one-tile gap");
		check(near == 0, "a unit came within its clearance of a thin ridge");
		if (w.large) check(max_dev > 20.0f, "a large unit was sent through a gap narrower than its clearance");
		else check(max_dev < 1.0f, "a swordsman went round a thin ridge instead of through its gap");
	}
}

// 31. Large units and a two-tile gap in a thin ridge: the gap's tiles lie at z 0 to 2, and its middle,
// the line z = 1 of tile corners, keeps 1 unit (4 m) off the steep tiles either side, more than the
// large class's 3 m. Lines through the gap at z 0.3, 1 and 1.7: each walker is set down west of the
// ridge on one and given a player's walk (beach_goal) to the point east of it on the same line.
static void large_units_and_two_tile_gap() {
	{
		Simulation s;
		thin_ridge_setup(s, false, 2);
		check(!s.nav_grid_.near_steep_tile(1.0f, 1.0f, CLEARANCE_LARGE) && s.nav_grid_.near_steep_tile(1.0f, 0.5f, CLEARANCE_LARGE) &&
				s.nav_grid_.near_steep_tile(1.0f, 1.5f, CLEARANCE_LARGE),
				"the gap's middle line is not open to the large class, or its tiles' centres are");
	}
	struct Walker { UnitType type; bool large; const char *name; };
	for (const float line : {0.3f, 1.0f, 1.7f})
		for (const Walker w : {Walker{UNIT_ELEPHANT, true, "an elephant"}, Walker{UNIT_SIEGE_RAM, true, "a ram"},
				Walker{UNIT_SWORDSMAN, false, "a swordsman"}}) {
			Simulation s;
			thin_ridge_setup(s, false, 2);
			const uint32_t id = s.spawn_unit(-6.0f, line, 0, w.type);
			s.order_walk(&id, 1, 6.0f, line, false, true);
			const int i0 = index_of(s, id);
			const bool kept = s.units_.target_x[i0] == 6.0f && s.units_.target_z[i0] == line;
			const float c = w.large ? CLEARANCE_LARGE : CLEARANCE_DEFAULT;
			int arrived = -1, near = 0;
			float max_dev = 0.0f;
			for (int t = 0; t < 2400 && arrived < 0; ++t) {
				s.tick(DT);
				const int i = index_of(s, id);
				if (i < 0) break;
				near += s.nav_grid_.near_steep_tile(s.units_.pos_x[i], s.units_.pos_z[i], c - 0.01f);
				max_dev = std::max(max_dev, std::fabs(s.units_.pos_z[i] - 1.0f));
				if (std::hypot(s.units_.pos_x[i] - 6.0f, s.units_.pos_z[i] - line) < 0.5f) arrived = t;
			}
			std::printf("  two-tile gap: %s walking along z = %.1f %s its point, arrived after %d ticks (-1 = never in 2400), "
					"at most %.1f off the gap's middle, %d ticks within its clearance of the ridge\n",
					w.name, line, kept ? "kept" : "lost", arrived, max_dev, near);
			check(kept, "a player's walk through a two-tile gap had its point moved");
			check(arrived >= 0, "a unit never got through a two-tile gap in a thin ridge");
			check(near == 0, "a unit came within its clearance of a thin ridge");
			check(max_dev < 3.0f, "a unit went round a thin ridge instead of through its two-tile gap");
		}
}

// The walks of 32 across the thin ridge with a one-tile gap: a player's walk from (-6, sz) to
// (gx, gz), whose line crosses the ridge by the gap. Sets the tick the unit got there (-1: never in
// 4800), the furthest it went from z = 0 (round the ridge's end: past 30), the ticks it spent within
// its clearance of a steep tile and its deepest overlap with a tree.
static void walk_across_gap(Simulation &s, UnitType type, float sz, float gx, float gz, int &arrived, float &far_z, int &near,
		float &pen) {
	const uint32_t id = s.spawn_unit(-6.0f, sz, 0, type);
	s.order_walk(&id, 1, gx, gz, false, true);
	arrived = -1;
	near = 0;
	far_z = 0.0f;
	pen = 0.0f;
	for (int t = 0; t < 4800 && arrived < 0; ++t) {
		s.tick(DT);
		const int i = index_of(s, id);
		if (i < 0) break;
		pen = std::max(pen, gaia_penetration(s));
		near += s.nav_grid_.near_steep_tile(s.units_.pos_x[i], s.units_.pos_z[i], CLEARANCE_DEFAULT - 0.01f);
		far_z = std::max(far_z, std::fabs(s.units_.pos_z[i]));
		if (std::hypot(s.units_.pos_x[i] - gx, s.units_.pos_z[i] - gz) < 0.5f) arrived = t;
	}
}

// 32. A tree in a one-tile gap: a 1.6 m tree (template_gaia_tree) in the middle of the gap of 30, and
// walks along the gap's line, from (-6, 0.5) to (6, 0.5), and across it, from (-6, 4) to (6, -3).
static void tree_in_one_tile_gap() {
	struct Walker { UnitType type; const char *name; };
	struct Line { float sz, gz; };
	for (const Walker w : {Walker{UNIT_SWORDSMAN, "a swordsman"}, Walker{UNIT_CAVALRY, "a horseman"}})
		for (const Line l : {Line{0.5f, 0.5f}, Line{4.0f, -3.0f}}) {
			Simulation s;
			thin_ridge_setup(s, false, 1);
			const uint32_t tree = s.spawn_resource_world(1.0f, 0.5f, RES_WOOD, 200);
			s.tick(DT);
			const float half = s.resource_block_half(s.res_id_to_idx_.at(tree));
			int arrived, near;
			float far_z, pen;
			walk_across_gap(s, w.type, l.sz, 6.0f, l.gz, arrived, far_z, near, pen);
			std::printf("  tree in a one-tile gap (half size %.2f): %s from (-6, %.1f) to (6, %.1f) arrived after %d ticks (-1 = never "
					"in 4800), at most %.1f from z = 0, %d ticks within its clearance of the ridge, max penetration %.3f\n",
					half, w.name, l.sz, l.gz, arrived, far_z, near, pen);
			check(half + 2.0f * CLEARANCE_DEFAULT > 0.5f, "the tree leaves a unit room beside it in the gap");
			check(arrived >= 0, "a walk across a thin ridge whose gap a tree shuts never got there");
			check(near == 0, "a unit came within its clearance of a thin ridge");
			check(pen <= MAX_PENETRATION, "a unit walked through a tree in a gap in a thin ridge");
			check(far_z > 30.0f, "a unit was sent through a gap a tree shuts");
		}
	{
		// Felled: the gap opens again (Simulation::sync_tile_obstructions at the end of the tick).
		Simulation s;
		thin_ridge_setup(s, false, 1);
		const uint32_t tree = s.spawn_resource_world(1.0f, 0.5f, RES_WOOD, 200);
		s.tick(DT);
		s.resources_.amount[s.res_id_to_idx_.at(tree)] = 0;
		s.tick(DT);
		const bool gone = s.res_id_to_idx_.find(tree) == s.res_id_to_idx_.end();
		int arrived, near;
		float far_z, pen;
		walk_across_gap(s, UNIT_SWORDSMAN, 4.0f, 6.0f, -3.0f, arrived, far_z, near, pen);
		std::printf("  tree in a one-tile gap, felled (%s): a swordsman arrived after %d ticks (-1 = never in 4800), at most %.1f "
				"from z = 0, %d ticks within its clearance of the ridge\n", gone ? "gone" : "still there", arrived, far_z, near);
		check(gone, "a tree with nothing left in it was not removed");
		check(arrived >= 0 && far_z < 5.0f, "a swordsman didn't take a gap once the tree in it was felled");
		check(near == 0, "a unit came within its clearance of a thin ridge");
	}
}

// 33. A structure beside a one-tile gap: a house (2 units a side) at (3.05, -0.3), its west face 0.05
// off the ridge, its north face at z 0.7, across the gap's east end. It covers the navcell x 2 to 4,
// z 0 to 2 only in part, which stays open. Walks to the far side along the gap's line, from (-6, 0.5)
// to (6, 0.5), and across it, from (-6, 3.5) to (6, -2.5).
static void structure_beside_one_tile_gap() {
	struct Walker { UnitType type; const char *name; };
	for (const Walker w : {Walker{UNIT_SWORDSMAN, "a swordsman"}, Walker{UNIT_CAVALRY, "a horseman"}})
		for (const float sz : {0.5f, 3.5f}) {
			Simulation s;
			thin_ridge_setup(s, false, 1);
			s.spawn_building_world(3.05f, -0.3f, BLD_HOUSE, 0, 1.0f);
			const auto [cx, cy] = s.nav_grid_.world_to_cell(2.1f, 0.5f);
			const bool cell_open = s.nav_grid_.cells.at(cx, cy) != NAV_OBSTRUCTED;
			const float gz = sz == 0.5f ? 0.5f : -2.5f;
			const uint32_t id = s.spawn_unit(-6.0f, sz, 0, w.type);
			s.order_walk(&id, 1, 6.0f, gz, false, true);
			int arrived = -1, near = 0;
			float far_z = 0.0f, pen = 0.0f;
			for (int t = 0; t < 4800 && arrived < 0; ++t) {
				s.tick(DT);
				const int i = index_of(s, id);
				if (i < 0) break;
				pen = std::max(pen, max_penetration(s));
				near += s.nav_grid_.near_steep_tile(s.units_.pos_x[i], s.units_.pos_z[i], CLEARANCE_DEFAULT - 0.01f);
				far_z = std::max(far_z, std::fabs(s.units_.pos_z[i]));
				if (std::hypot(s.units_.pos_x[i] - 6.0f, s.units_.pos_z[i] - gz) < 0.5f) arrived = t;
			}
			std::printf("  house beside a one-tile gap (its navcell by the gap %s): %s from (-6, %.1f) to (6, %.1f) arrived after %d "
					"ticks (-1 = never in 4800), at most %.1f from z = 0, %d ticks within its clearance of the ridge, max "
					"penetration %.3f\n", cell_open ? "open" : "closed", w.name, sz, gz, arrived, far_z, near, pen);
			check(cell_open, "the house closes the navcell it only partly covers");
			check(arrived >= 0, "a walk across a thin ridge whose gap a house shuts never got there");
			check(near == 0, "a unit came within its clearance of a thin ridge");
			check(pen <= MAX_PENETRATION, "a unit walked through a house beside a thin ridge");
			check(far_z > 30.0f, "a unit was sent between a house and a thin ridge where it can't fit");
		}
}

// 34. The nodes after a load.
static void nodes_after_load() {
	auto a = std::make_unique<Simulation>(), b = std::make_unique<Simulation>();
	a->reset();
	a->set_sim_seed(3);
	a->set_waves_enabled(false);
	const uint8_t civs[4] = {0, 5, 9, 13};
	for (uint8_t p = 0; p < 4; ++p) a->set_team_civ(p, civs[p]);
	a->generate_random_map(MAP_SAHARA, 3, 4, 128.0f);
	const char *dir = std::getenv("TMPDIR");
	const std::string path = std::string(dir && *dir ? dir : "/tmp") + "/rts0_test_building_steering_nodes.sav";
	const bool ok = a->save_state(path.c_str()) && b->load_state(path.c_str());
	std::remove(path.c_str());
	const NavGrid &x = a->nav_grid_, &y = b->nav_grid_;
	const auto &l = x.tile_obs_[NavGrid::TILE_OBS_GAIA], &m = y.tile_obs_[NavGrid::TILE_OBS_GAIA];
	size_t differ = l.size() == m.size() ? 0 : std::max(l.size(), m.size());
	for (size_t k = 0; k < l.size() && k < m.size(); ++k) differ += l[k] != m[k];
	size_t open_differ[NavGrid::TILE_CLASSES] = {};
	for (int cls = 0; cls < NavGrid::TILE_CLASSES; ++cls) {
		const auto &p = x.tile_open_[cls], &q = y.tile_open_[cls];
		open_differ[cls] = p.size() == q.size() ? 0 : std::max(p.size(), q.size());
		for (size_t k = 0; k < p.size() && k < q.size(); ++k) open_differ[cls] += p[k] != q[k];
	}
	std::printf("  nodes after a load (Sahara, seed 3): %s, nodes kept %d, %zu trees and bushes listed, %zu of them differ in the "
			"loaded copy, open nodes differ at %zu (default) and %zu (large)\n", ok ? "saved and loaded" : "save or load FAILED",
			x.tile_open_cached(), l.size(), differ, open_differ[0], open_differ[1]);
	check(ok, "the game could not be saved and loaded");
	check(x.tile_open_cached() && !l.empty(), "Sahara keeps no nodes or lists no trees on them");
	check(differ == 0, "a load lists the trees and bushes on the nodes otherwise than the game it was saved from");
	check(open_differ[0] == 0 && open_differ[1] == 0, "a load opens other nodes than the game it was saved from");
}

// 35. A turned structure beside a cliff. Heights every unit, as in cliff_clearance_setup; the samples
// along z = -1 and z = north + 1 are raised 6 for x in [-30, 30], so the tiles z -2 to 0 and north to
// north + 2 there are steep: a corridor from z = 0 to z = north. A house (2 units a side) turned 45
// degrees stands in it at x = xc, its north corner gap_m metres south of the north wall and its south
// corner in the south one. A player's walk along the corridor's middle, from x = -10 to x = 10. Sets
// the tick it got there (-1: never in 3000), its furthest from the corridor's middle, its furthest
// along it from x = 0 (round the walls' ends: past 30) and its deepest overlap with the house.
static void walk_past_turned_house(UnitType type, int north, float gap_m, float xc, int &arrived, float &max_dev, float &far_x,
		float &pen) {
	Simulation s;
	setup(s);
	constexpr int RES = 129;
	constexpr float HALF = 64.0f;
	s.set_map_half(HALF);
	std::vector<float> h((size_t)RES * RES, 0.0f);
	for (int j = 0; j < RES; ++j)
		for (int i = 0; i < RES; ++i) {
			const int x = i - 64, z = j - 64;
			if (x >= -30 && x <= 30 && (z == -1 || z == north + 1)) h[(size_t)j * RES + i] = 6.0f;
		}
	s.set_terrain_data(h, RES, -1.0f);
	s.begin_bulk_spawn();
	s.end_bulk_spawn();
	const float rot = Simulation::DEFAULT_BUILDING_ROT + 0.785398163f;
	const float half = building_type_info(BLD_HOUSE).footprint * Simulation::GRID * 0.5f;
	const float corner = half * 1.41421356f; // from its centre to its north corner
	s.spawn_building_world(xc, (float)north - gap_m / 4.0f - corner, BLD_HOUSE, 0, 1.0f, rot);
	s.tick(DT);
	const float mid = 0.5f * (float)north;
	const uint32_t id = s.spawn_unit(-10.0f, mid, 0, type);
	s.order_walk(&id, 1, 10.0f, mid, false, true);
	arrived = -1;
	max_dev = 0.0f;
	far_x = 0.0f;
	pen = 0.0f;
	for (int t = 0; t < 3000 && arrived < 0; ++t) {
		s.tick(DT);
		const int i = index_of(s, id);
		if (i < 0) break;
		pen = std::max(pen, max_penetration(s));
		max_dev = std::max(max_dev, std::fabs(s.units_.pos_z[i] - mid));
		far_x = std::max(far_x, std::fabs(s.units_.pos_x[i]));
		if (std::hypot(s.units_.pos_x[i] - 10.0f, s.units_.pos_z[i] - mid) < 1.0f) arrived = t;
	}
}

static void turned_structure_beside_cliff() {
	struct Case { UnitType type; const char *name; int north; float gap_m; };
	for (const Case c : {Case{UNIT_SWORDSMAN, "a swordsman", 3, 3.5f}, Case{UNIT_SWORDSMAN, "a swordsman", 3, 4.0f},
			Case{UNIT_SIEGE_RAM, "a ram", 5, 9.0f}})
		for (const float xc : {0.0f, 0.13f, 0.26f, 0.39f}) {
			int arrived;
			float max_dev, far_x, pen;
			walk_past_turned_house(c.type, c.north, c.gap_m, xc, arrived, max_dev, far_x, pen);
			std::printf("  turned house beside a cliff: %s past a %.1f m gap (house at x %.2f) arrived after %d ticks (-1 = never in "
					"3000), at most %.1f off the corridor's middle and %.1f from x = 0, max penetration %.3f\n", c.name, c.gap_m, xc,
					arrived, max_dev, far_x, pen);
			check(arrived >= 0, "a unit never got past a turned house beside a cliff");
			check(far_x < 20.0f, "a unit went round a gap between a turned house and a cliff that it fits through");
			check(pen <= MAX_PENETRATION, "a unit walked into a turned house beside a cliff");
		}
}

// 36. A corner dance. Islands (map 61) with civs 6, 7, 8 and 9 and no AI, as the soak plays it, every
// player on one team when `one_team` (as the soak has it: no one shoots the walker). A player's walk
// by a Gaul javelineer (player 1) from (x, z) to (gx, gz). Sets the tick it ended (-1: never in 1200,
// a minute), where the javelineer stood then and how far it walked.
static void corner_dance_walk(uint32_t seed, bool one_team, float x, float z, float gx, float gz, int &ended, float &ex, float &ez,
		float &walked) {
	auto sp = std::make_unique<Simulation>();
	Simulation &s = *sp;
	s.reset();
	s.set_sim_seed(seed);
	s.set_waves_enabled(false);
	const uint8_t civs[4] = {6, 7, 8, 9};
	for (uint8_t p = 0; p < 4; ++p) s.set_team_civ(p, civs[p]);
	s.generate_random_map(MAP_ISLANDS, seed, 4, 128.0f);
	for (uint8_t p = 0; p < 4; ++p) s.set_team_civ(p, civs[p]);
	for (uint8_t p = 0; p < MAX_PLAYERS; ++p) s.set_player_active(p, p < 4, false);
	if (one_team)
		for (uint8_t p = 0; p < 4; ++p) s.set_player_team(p, 0);
	for (int t = 0; t < 20; ++t) s.tick(DT);
	const uint32_t id = s.spawn_unit(x, z, 1, UNIT_ARCHER, 0.0f, civ_unit_template_by_id("gaul/infantry_javelineer_b"));
	s.order_walk(&id, 1, gx, gz);
	ended = -1;
	ex = x;
	ez = z;
	walked = 0.0f;
	for (int t = 0; t < 1200 && ended < 0; ++t) {
		s.tick(DT);
		const int i = index_of(s, id);
		if (i < 0) break;
		walked += std::hypot(s.units_.pos_x[i] - ex, s.units_.pos_z[i] - ez);
		ex = s.units_.pos_x[i];
		ez = s.units_.pos_z[i];
		if (s.units_.order_kind[i] != ORDER_WALK) ended = t;
	}
}

static void corner_dance() {
	// Seed 11's first point lies in a steep pocket by the lake, reachable to within 4 units (the
	// javelineer danced 16 away); seed 7's point is open ground past the trees. The others lie across
	// the sea, so those walks end at the shore their long paths reach.
	struct Case { uint32_t seed; bool one_team; float x, z, gx, gz, near; };
	for (const Case c : {Case{11, false, 40.0f, -24.0f, 0.0f, -55.0f, 4.0f}, Case{11, false, 88.0f, 40.0f, -59.83f, 29.83f, 0.0f},
			Case{7, true, 42.22f, 59.80f, 89.57f, 63.56f, 1.0f}, Case{4, true, 29.60f, -89.09f, -83.41f, 12.11f, 0.0f},
			Case{3, true, -77.32f, -42.95f, -36.98f, 68.04f, 0.0f}, Case{1, true, 6.20f, 4.20f, -39.23f, 26.70f, 0.0f}}) {
		int ended;
		float ex, ez, walked;
		corner_dance_walk(c.seed, c.one_team, c.x, c.z, c.gx, c.gz, ended, ex, ez, walked);
		const float left = std::hypot(c.gx - ex, c.gz - ez);
		std::printf("  corner dance (Islands seed %u%s): a walk from (%.2f, %.2f) to (%.2f, %.2f) ended after %d ticks (-1 = never "
				"in 1200) at (%.2f, %.2f), %.2f from its point, after %.1f units\n", c.seed, c.one_team ? ", one team" : "", c.x, c.z,
				c.gx, c.gz, ended, ex, ez, left, walked);
		check(ended >= 0, "a walk on Islands never ended");
		check(c.near <= 0.0f || left < c.near, "a walk on Islands ended short of the point it can reach");
	}
}

// 37. Kept round trees, then obstructed. A swordsman walks from (-8, 0.3) to (8, 0.3) past a barracks at
// the origin, an oak 1.5 south of where it starts, so the short path round the barracks is searched
// with a tree round the unit and kept whole. Once it has walked some way along a leg of that path, an
// oak goes down 1.2 ahead of it on the leg, out of reach of the obstacles its path was searched among.
static void kept_path_obstructed() {
	Simulation s;
	setup(s);
	s.spawn_building_world(0.0f, 0.0f, BLD_BARRACKS, 0, 1.0f);
	s.spawn_resource_world(-8.0f, -1.2f, RES_WOOD, 200, RES_SUB_NONE, 0, FLORA_OAK);
	const uint32_t id = s.spawn_unit(-8.0f, 0.3f, 0, UNIT_SWORDSMAN);
	s.cmd_move(&id, 1, 8.0f, 0.3f);
	bool kept = false, put = false, dropped = false;
	int arrived = -1;
	float pen = 0.0f;
	for (int t = 0; t < 1200 && arrived < 0; ++t) {
		const int i = index_of(s, id);
		const float x = s.units_.pos_x[i], z = s.units_.pos_z[i];
		const ShortPathMemo m = s.units_.short_path[i];
		kept = kept || nav::is_kept_path_key(m.key, s.short_path_key_);
		float wx, wz;
		if (!put && nav::is_kept_path_key(m.key, s.short_path_key_) && std::hypot(x + 8.0f, z - 0.3f) > 3.5f &&
		    s.unit_short_waypoint((size_t)i, m.aim_x, m.aim_z, &wx, &wz) && std::hypot(wx - x, wz - z) > 2.0f) {
			const float l = std::hypot(wx - x, wz - z), ox = x + (wx - x) / l * 1.2f, oz = z + (wz - z) / l * 1.2f;
			if (std::hypot(ox - m.x, oz - m.z) > 3.5f) {
				s.spawn_resource_world(ox, oz, RES_WOOD, 200, RES_SUB_NONE, 0, FLORA_OAK);
				put = true;
				s.tick(DT);
				// Dropped: no longer this path's kept key (only the waypoint index moving on would keep its base).
				const uint32_t k2 = s.units_.short_path[index_of(s, id)].key;
				dropped = !nav::is_kept_path_key(k2, s.short_path_key_) ||
				          (k2 & ~nav::KEPT_PATH_INDEX_MASK) != (m.key & ~nav::KEPT_PATH_INDEX_MASK);
				pen = std::max(pen, gaia_penetration(s));
				continue;
			}
		}
		s.tick(DT);
		pen = std::max(pen, gaia_penetration(s));
		const int j = index_of(s, id);
		if (s.units_.action_state[j] == ACTION_IDLE) arrived = t;
	}
	const int i = index_of(s, id);
	const float left = std::hypot(s.units_.pos_x[i] - 8.0f, s.units_.pos_z[i] - 0.3f);
	std::printf("  kept round trees, then obstructed: kept its path (%s), an oak put in the way (%s), path dropped at once (%s), "
			"got there after %d ticks (-1 = never in 1200), %.2f from its point, max penetration %.3f\n", kept ? "yes" : "no",
			put ? "yes" : "no", dropped ? "yes" : "no", arrived, left, pen);
	check(kept, "a swordsman beside a tree didn't keep its short path round a barracks");
	check(put, "no oak could be put in the way of the kept short path");
	check(dropped, "a swordsman whose kept short path an oak blocked held on to it");
	check(arrived >= 0 && left < 1.0f, "a walk past an oak put in its way never got there");
	check(pen <= MAX_PENETRATION, "a swordsman walked into an oak put in its way");
}

// 38. A waypoint inside a tree. Oaks at (0, 0) and (0.62, 0.05); a swordsman at (-6, -1.5) walks to
// (6, 3) along a path whose first waypoint, (0.62, 0.25), lies inside the second oak.
static void waypoint_in_a_tree() {
	Simulation s;
	setup(s);
	s.spawn_resource_world(0.0f, 0.0f, RES_WOOD, 200, RES_SUB_NONE, 0, FLORA_OAK);
	s.spawn_resource_world(0.62f, 0.05f, RES_WOOD, 200, RES_SUB_NONE, 0, FLORA_OAK);
	const uint32_t id = s.spawn_unit(-6.0f, -1.5f, 0, UNIT_SWORDSMAN);
	s.cmd_move(&id, 1, 6.0f, 3.0f);
	s.tick(DT);
	int w = index_of(s, id);
	WaypointPath &wp = s.unit_paths_[w];
	wp.clear();
	wp.push_back(0.62f, 0.25f);
	wp.push_back(6.0f, 3.0f);
	s.units_.waypoint_cur[w] = 0;
	s.units_.waypoint_count[w] = (int16_t)wp.count;
	int arrived = -1;
	float pen = 0.0f;
	for (int t = 0; t < 1200 && arrived < 0; ++t) {
		s.tick(DT);
		pen = std::max(pen, gaia_penetration(s));
		const int i = index_of(s, id);
		if (s.units_.action_state[i] == ACTION_IDLE) arrived = t;
	}
	w = index_of(s, id);
	const float left = std::hypot(s.units_.pos_x[w] - 6.0f, s.units_.pos_z[w] - 3.0f);
	std::printf("  a waypoint inside a tree: got there after %d ticks (-1 = never in 1200), %.2f from its point, at (%.2f, %.2f), "
			"max penetration %.3f\n", arrived, left, s.units_.pos_x[w], s.units_.pos_z[w], pen);
	check(arrived >= 0 && left < 1.0f, "a walk whose waypoint lies inside a tree never got there");
	check(pen <= MAX_PENETRATION, "a swordsman walked into an oak round a waypoint inside it");
}

// 39. Straight to a near goal. Five houses at several turns. Each walk is a player's within 6 units
// (24 m). The spearmen's lines clear every footprint by more than their clearance, and RTS0's long
// paths (8 m navcells) took each of them round a house's corner by a navcell centre. The large
// units' lines pass within their clearance of a house, both ends well clear of every house.
static void straight_to_near_goal() {
	// The least distance from the segment to any footprint, sampled every 1/1000 of it.
	auto clearance_of_line = [](const Simulation &s, float x0, float z0, float x1, float z1) {
		float least = 1e9f;
		for (int k = 0; k <= 1000; ++k) {
			const float t = k / 1000.0f;
			for (const auto &b : s.buildings())
				least = std::min(least, nav::footprint_gap(b, x0 + (x1 - x0) * t, z0 + (z1 - z0) * t).length());
		}
		return least;
	};
	struct Walk { UnitType type; float x0, z0, x1, z1; };
	for (const Walk w : {Walk{UNIT_SPEARMAN, 11.85f, -7.74f, 11.99f, -3.25f}, Walk{UNIT_SPEARMAN, 10.92f, -4.24f, 12.67f, -6.24f},
			Walk{UNIT_SPEARMAN, 0.02f, 2.15f, 2.01f, 1.58f}, Walk{UNIT_SPEARMAN, -1.18f, 4.30f, 1.79f, 0.73f},
			Walk{UNIT_SPEARMAN, 12.34f, -4.91f, 7.90f, -2.89f}, Walk{UNIT_SIEGE_RAM, 1.50f, -6.72f, 1.34f, -12.49f},
			Walk{UNIT_SIEGE_RAM, 11.95f, -4.97f, 11.40f, -8.20f}, Walk{UNIT_ELEPHANT, 1.50f, -6.72f, 1.34f, -12.49f}}) {
		Simulation s;
		setup(s);
		struct House { float x, z, rot; };
		for (const House h : {House{0.0f, 0.0f, 0.0f}, House{8.0f, 3.0f, 0.6f}, House{3.0f, -9.0f, 2.0f},
				House{-9.0f, -4.0f, 0.3f}, House{10.0f, -6.0f, 2.6f}})
			s.spawn_building_world(h.x, h.z, BLD_HOUSE, 0, 1.0f, h.rot);
		s.tick(DT);
		const float r = unit_building_clearance(0xffff, w.type);
		const float clear = clearance_of_line(s, w.x0, w.z0, w.x1, w.z1);
		const uint32_t id = s.spawn_unit(w.x0, w.z0, 0, w.type);
		s.order_walk(&id, 1, w.x1, w.z1, false, true);
		const float lx = w.x1 - w.x0, lz = w.z1 - w.z0, len = std::hypot(lx, lz);
		float walked = 0.0f, dev = 0.0f, pen = 0.0f, px = w.x0, pz = w.z0;
		int arrived = -1;
		for (int t = 0; t < 600 && arrived < 0; ++t) {
			s.tick(DT);
			const int i = index_of(s, id);
			if (i < 0) break;
			const float x = s.units_.pos_x[i], z = s.units_.pos_z[i];
			walked += std::hypot(x - px, z - pz);
			px = x;
			pz = z;
			dev = std::max(dev, std::fabs((x - w.x0) * lz - (z - w.z0) * lx) / len);
			pen = std::max(pen, max_penetration(s));
			if (!s.units_.has_order[i] && s.units_.action_state[i] == ACTION_IDLE) arrived = t;
		}
		const char *name = w.type == UNIT_SPEARMAN ? "spearman" : w.type == UNIT_SIEGE_RAM ? "ram" : "elephant";
		std::printf("  %s %.2f units past turned houses (line %.2f off them, clearance %.2f): walked %.2f, at most %.2f off the line, "
				"done after %d ticks, max penetration %.3f\n", name, len, clear, r, walked, dev, arrived, pen);
		check(arrived >= 0 && std::hypot(px - w.x1, pz - w.z1) < 1.0f, "a walk to a near goal past turned houses never got there");
		check(pen <= MAX_PENETRATION, "a unit walked into a turned house on its way to a near goal");
		if (clear > r) {
			check(walked <= len + 0.05f && dev <= 0.05f, "a unit took the long way to a near goal it had a clear line to");
		} else {
			check(w.type != UNIT_SPEARMAN, "a spearman's walk in scenario 39 does not clear the houses");
		}
		if (w.type != UNIT_SPEARMAN) {
			check(clear <= r, "a large unit's walk in scenario 39 clears the houses");
		}
	}
	// A spearman standing inside a house's clearance (no other unit pushes it out), 0.1 off its east
	// face, and a line north along that face, past a second house 0.35 east of the first: the line
	// keeps 0.25 off the second house, more than the 0.2 clearance, so only the first one's square
	// holds it. It fails, as the walk between two houses 1.4 m apart, which 0.28 never makes; it
	// passes when the first house is the spearman's target, and from 0.25 off the face heading away.
	{
		Simulation s;
		setup(s);
		const uint32_t a = s.spawn_building_world(0.0f, 0.0f, BLD_HOUSE, 0, 1.0f, 0.0f);
		float hu = 0.0f, hv = 0.0f;
		for (const auto &b : s.buildings())
			if (b.id == a) hu = b.hu, hv = b.hv;
		s.spawn_building_world(2.0f * hu + 0.35f, hv + 1.0f, BLD_HOUSE, 0, 1.0f, 0.0f);
		const uint32_t id = s.spawn_unit(hu + 0.1f, 0.0f, 0, UNIT_SPEARMAN);
		s.tick(DT);
		const int i = index_of(s, id);
		const float x = s.units_.pos_x[i], z = s.units_.pos_z[i];
		const bool inside = s.direct_walk_clear((size_t)i, x, z, x, z + 2.0f * hv + 2.0f);
		s.units_.target_building_id[i] = a;
		const bool target = s.direct_walk_clear((size_t)i, x, z, x, z + 2.0f * hv + 2.0f);
		s.units_.target_building_id[i] = 0;
		const bool outside = s.direct_walk_clear((size_t)i, hu + 0.25f, z, hu + 3.0f, z - 2.0f);
		std::printf("  a line along a house's face from inside its clearance (%.2f, %.2f): %s; with the house as the target: %s; "
				"from outside the clearance, away from it: %s\n", x, z, inside ? "clear" : "refused", target ? "clear" : "refused",
				outside ? "clear" : "refused");
		check(std::fabs(x - hu - 0.1f) < 0.01f && !inside, "a direct walk started inside a house's clearance and cut between two houses");
		check(target && outside, "a direct walk along a house's face was refused although nothing but the start held it");
	}
}

// 40. Past the map's edge. Ardennes Forest (seed 3, 768 m across, a circular map): a spearman and an
// elephant 7 units inside the map's circle, each sent to a point 0.6 inside it, about 28 m off, so
// the direct walk is tried once within 24 m. Where MakeGoalReachable moves the goal (the long path
// ends short of it), the unit ends at the path's end.
static void walk_past_map_edge() {
	int moved = 0;
	for (const int a : {1, 4, 6, 9})
		for (const UnitType type : {UNIT_SPEARMAN, UNIT_ELEPHANT}) {
			auto s = std::make_unique<Simulation>();
			s->generate_random_map(MAP_ARDENNES_FOREST, 3, 2, 96.0f);
			for (uint8_t p = 0; p < 2; ++p) s->player(p).is_ai = 0;
			const float ang = (float)a * 0.3926991f + 0.1f, r = s->map_half() - 7.0f;
			const float gx = (s->map_half() - 0.6f) * std::cos(ang + 0.03f), gz = (s->map_half() - 0.6f) * std::sin(ang + 0.03f);
			const uint32_t id = s->spawn_unit(r * std::cos(ang), r * std::sin(ang), 0, type);
			s->order_set_stance(&id, 1, STANCE_PASSIVE);
			s->tick(Simulation::FIXED_STEP);
			int i = index_of(*s, id);
			WaypointPath wp;
			s->find_path(s->units_.pos_x[i], s->units_.pos_z[i], gx, gz, wp, s->unit_land_pass((size_t)i));
			const float ex = wp.xs[wp.count - 1], ez = wp.zs[wp.count - 1];
			s->cmd_move(&id, 1, gx, gz);
			for (int t = 0; t < 400; ++t) s->tick(Simulation::FIXED_STEP);
			i = index_of(*s, id);
			const float off = std::hypot(s->units_.pos_x[i] - ex, s->units_.pos_z[i] - ez);
			const bool short_of_goal = std::hypot(ex - gx, ez - gz) > Simulation::ARRIVE_DIST;
			moved += short_of_goal;
			std::printf("  %s sent past the map's edge to (%.2f, %.2f), %.2f away: the long path ends at (%.2f, %.2f), it ended %.2f from there\n",
					type == UNIT_SPEARMAN ? "spearman" : "elephant", gx, gz, std::hypot(gx - r * std::cos(ang), gz - r * std::sin(ang)), ex, ez, off);
			if (short_of_goal) check(off <= 1.0f, "a walk to a point past the map's edge went straight past the end of its long path");
		}
	check(moved >= 4, "scenario 40 no longer has walks whose long path ends short of the goal");
}

// 41. Set down beside a hemmed-in arsenal. A row of five houses 1-3 m off the arsenal's front and
// another off its side, 2-5 m between houses. Each ram and elephant it trains is set down outside
// every structure's square grown by 3 m (pick_spawn_point), and gets within 1 of a point 15 units
// off in each direction in 60 s.
static void large_units_beside_hemmed_in_arsenal() {
	int set_down_in = 0, stuck = 0, runs = 0;
	for (const int face_m : {1, 2, 3})
		for (const int gap_m : {2, 4, 5})
			for (const UnitType type : {UNIT_SIEGE_RAM, UNIT_ELEPHANT})
				for (int dir = 0; dir < 4; ++dir) {
					Simulation s;
					setup(s);
					s.begin_bulk_spawn();
					const uint32_t ars = s.spawn_building_world(0.0f, 0.0f, BLD_ARSENAL, 0, 1.0f, 0.0f);
					s.end_bulk_spawn();
					const Simulation::Building *b = nullptr;
					for (const auto &x : s.buildings()) if (x.id == ars) b = &x;
					const float ahu = b->hu, ahv = b->hv, f = face_m / 4.0f, g = gap_m / 4.0f;
					s.begin_bulk_spawn();
					for (int k = -2; k <= 2; ++k) {
						s.spawn_building_world(k * (2.0f + g), ahv + f + 1.0f, BLD_HOUSE, 0, 1.0f, 0.0f);
						s.spawn_building_world(ahu + f + 1.0f, k * (2.0f + g), BLD_HOUSE, 0, 1.0f, 0.0f);
					}
					s.end_bulk_spawn();
					s.spawn_unit(-40.0f, 40.0f, 0, UNIT_SPEARMAN); // separation() pushes off structures with two units or more
					for (const auto &x : s.buildings()) if (x.id == ars) b = &x;
					float sx = 0.0f, sz = 0.0f;
					const bool found = s.pick_spawn_point(*b, type, 0, sx, sz);
					bool in_band = !found;
					for (const auto &x : s.buildings())
						if (!x.wrecked && nav::PointIsInSquare(nav::Vec2(sx - x.x, sz - x.z), nav::footprint_u(x), nav::footprint_v(x),
								nav::Vec2(x.hu + CLEARANCE_LARGE, x.hv + CLEARANCE_LARGE)))
							in_band = true;
					set_down_in += in_band;
					++runs;
					if (!found) continue;
					const uint32_t id = s.spawn_unit(sx, sz, 0, type);
					s.order_set_stance(&id, 1, STANCE_PASSIVE);
					const float gx = dir == 1 ? 15.0f : dir == 3 ? -15.0f : 0.0f, gz = dir == 0 ? 15.0f : dir == 2 ? -15.0f : 0.0f;
					s.cmd_move(&id, 1, gx, gz);
					bool arrived = false;
					for (int t = 0; t < (int)(60.0f / Simulation::FIXED_STEP) && !arrived; ++t) {
						s.tick(Simulation::FIXED_STEP);
						const int i = index_of(s, id);
						arrived = std::hypot(s.units_.pos_x[i] - gx, s.units_.pos_z[i] - gz) < 1.0f;
					}
					if (in_band || !arrived) {
						const int i = index_of(s, id);
						std::printf("  %s set down at (%.2f, %.2f)%s by an arsenal with houses %d m off and %d m apart, sent to (%.0f, %.0f): "
								"%s at (%.2f, %.2f)\n", type == UNIT_SIEGE_RAM ? "ram" : "elephant", sx, sz, in_band ? " inside a structure's 3 m" : "",
								face_m, gap_m, gx, gz, arrived ? "got there" : "never got there, stopped", s.units_.pos_x[i], s.units_.pos_z[i]);
					}
					stuck += !arrived;
				}
	std::printf("  rams and elephants set down beside a hemmed-in arsenal: %d of %d inside a structure's 3 m, %d never got 15 units away\n",
			set_down_in, runs, stuck);
	check(set_down_in == 0, "a ram or an elephant was set down within its clearance of a structure");
	check(stuck == 0, "a ram or an elephant set down beside a hemmed-in arsenal never got away");
}

// 42. Large units at the mouth of a narrow gap. Two houses side by side, turned by rot, with a gap
// of gap_m metres between their facing faces; u runs across the gap (east), v north. The unit starts
// 8 units south of the gap's middle (mode 0: two-step, mode 1: beside the east house) or inside the
// west house's clearance, 0.1 units off its east face by the gap's south mouth (mode 2). Its centre
// must never come strictly between the facing faces, and the bodies must keep off the houses.
static void large_units_at_narrow_gap_mouth() {
	int between = 0, runs = 0, arrived = 0;
	float worst_pen = 0.0f;
	for (const int gap_m : {4, 5, 6})
		for (const float rot : {0.0f, 0.4f})
			for (int mode = 0; mode < 3; ++mode)
				for (const UnitType type : {UNIT_SIEGE_RAM, UNIT_ELEPHANT})
					for (const float xc : {0.0f, 0.5f, 1.0f, 1.5f})
						for (const float zc : {0.0f, 1.0f}) {
							Simulation s;
							setup(s);
							const float g = gap_m / 4.0f;
							const float ux = std::cos(rot), uz = -std::sin(rot), vx = std::sin(rot), vz = std::cos(rot);
							float hu = 0.0f, hv = 0.0f;
							{
								Simulation probe;
								setup(probe);
								const uint32_t h = probe.spawn_building_world(0.0f, 0.0f, BLD_HOUSE, 0, 1.0f, 0.0f);
								for (const auto &b : probe.buildings())
									if (b.id == h) hu = b.hu, hv = b.hv;
							}
							const float off = hu + 0.5f * g;
							s.begin_bulk_spawn();
							s.spawn_building_world(xc - off * ux, zc - off * uz, BLD_HOUSE, 0, 1.0f, rot);
							s.spawn_building_world(xc + off * ux, zc + off * uz, BLD_HOUSE, 0, 1.0f, rot);
							s.end_bulk_spawn();
							s.spawn_unit(-40.0f, 40.0f, 1, UNIT_SPEARMAN); // separation() pushes off structures with two units or more
							auto at = [&](float lu, float lv, float &x, float &z) { x = xc + lu * ux + lv * vx; z = zc + lu * uz + lv * vz; };
							float sx, sz;
							if (mode == 2) at(-0.5f * g + 0.1f, -hv - 0.2f, sx, sz);
							else at(0.0f, -8.0f, sx, sz);
							const uint32_t id = s.spawn_unit(sx, sz, 0, type);
							s.order_set_stance(&id, 1, STANCE_PASSIVE);
							bool btw = false;
							float pen = 0.0f;
							int ticks = 0;
							auto run = [&](float tx, float tz, float secs) {
								s.cmd_move(&id, 1, tx, tz);
								for (int t = 0; t < (int)(secs / Simulation::FIXED_STEP); ++t, ++ticks) {
									s.tick(Simulation::FIXED_STEP);
									const int i = index_of(s, id);
									const float dx = s.units_.pos_x[i] - xc, dz = s.units_.pos_z[i] - zc;
									const float lu = dx * ux + dz * uz, lv = dx * vx + dz * vz;
									if (std::fabs(lu) < 0.5f * g - 0.02f && std::fabs(lv) < hv) btw = true;
									// One set down inside a clearance is given a second to be pushed out of it.
									if (mode != 2 || ticks >= (int)(1.0f / Simulation::FIXED_STEP)) pen = std::max(pen, max_penetration(s));
								}
							};
							if (mode == 0 || mode == 1) {
								float mx, mz;
								if (mode == 0) at(0.0f, -hv - 0.3f, mx, mz);
								else at(0.5f * g - 0.375f, -hv - 0.3f, mx, mz);
								run(mx, mz, 20.0f);
							}
							float gx, gz;
							at(0.0f, 8.0f, gx, gz);
							run(gx, gz, 30.0f);
							const int i = index_of(s, id);
							const bool arr = std::hypot(s.units_.pos_x[i] - gx, s.units_.pos_z[i] - gz) < 1.0f;
							static const char *modes[] = {"two-step", "beside the east house", "from inside the west house's clearance"};
							if (btw || pen > MAX_PENETRATION)
								std::printf("  %s, %d m gap turned %.1f (at %.1f, %.1f), %s: %s, max penetration %.3f\n",
										type == UNIT_SIEGE_RAM ? "ram" : "elephant", gap_m, rot, xc, zc, modes[mode],
										btw ? "went between the houses" : "kept out of the gap", pen);
							between += btw;
							arrived += arr;
							worst_pen = std::max(worst_pen, pen);
							++runs;
						}
	std::printf("  rams and elephants by 4-6 m gaps between houses: %d of %d went between them, %d got past, max penetration %.3f\n",
			between, runs, arrived, worst_pen);
	check(between == 0, "a ram or an elephant went between two houses less than 6 m apart");
	check(worst_pen <= MAX_PENETRATION, "a ram or an elephant overlapped a house by a narrow gap");
}

// A house's half width (hu) and half depth (hv), unturned.
static void house_half_size(float &hu, float &hv) {
	Simulation probe;
	setup(probe);
	const uint32_t h = probe.spawn_building_world(0.0f, 0.0f, BLD_HOUSE, 0, 1.0f, 0.0f);
	for (const auto &b : probe.buildings())
		if (b.id == h) hu = b.hu, hv = b.hv;
}

// How many of the houses other than `skip` hold (x, z) in their square grown by the large clearance,
// and the deepest it lies in one (the lesser of its depths along the house's two axes).
static int large_bands_holding(const Simulation &s, float x, float z, uint32_t skip, float &deepest) {
	int n = 0;
	for (const auto &b : s.buildings()) {
		if (b.wrecked || b.type != BLD_HOUSE || b.id == skip) continue;
		const float dx = x - b.x, dz = z - b.z;
		const float eu = b.hu + CLEARANCE_LARGE - std::fabs(dx * b.ux + dz * b.uz);
		const float ev = b.hv + CLEARANCE_LARGE - std::fabs(-dx * b.uz + dz * b.ux);
		if (eu > 0.0f && ev > 0.0f) {
			++n;
			deepest = std::max(deepest, std::min(eu, ev));
		}
	}
	return n;
}

// 43. Large units at a house cluster. Nine houses on a 3x3 grid, turned by rot, gap_m metres between
// neighbours' facing faces: player 1's for an attack (a ram, kind 0, or an elephant, kind 1), player
// 0's for a build (a worker elephant, kind 2), with house tk a fresh foundation. The unit starts 12
// units from the block's middle in direction dir and is given the task; the task has started once
// the target has lost hit points or gained build progress, within 90 s. Its centre must never be
// inside two non-target houses' squares grown by 3 m at once, and every outer house's task must start.
static void large_units_at_house_cluster() {
	float hu = 0.0f, hv = 0.0f;
	house_half_size(hu, hv);
	static const char *kinds[] = {"ram attacking", "elephant attacking", "worker elephant building"};
	int runs = 0, in_two = 0, outer_runs = 0, outer_started = 0, started = 0;
	float deepest = 0.0f;
	for (const int gap_m : {4, 5, 6})
		for (const float rot : {0.0f, 0.4f})
			for (int kind = 0; kind < 3; ++kind)
				for (int tk = 0; tk < 9; ++tk)
					for (int dir = 0; dir < 8; ++dir) {
						Simulation s;
						setup(s);
						const float g = gap_m / 4.0f, xc = 0.3f, zc = 0.6f;
						const float ux = std::cos(rot), uz = -std::sin(rot), vx = std::sin(rot), vz = std::cos(rot);
						auto at = [&](float lu, float lv, float &x, float &z) { x = xc + lu * ux + lv * vx; z = zc + lu * uz + lv * vz; };
						const uint8_t owner = kind == 2 ? 0 : 1;
						uint32_t ids[9];
						s.begin_bulk_spawn();
						for (int k = 0; k < 9; ++k) {
							float x, z;
							at((k % 3 - 1) * (2.0f * hu + g), (k / 3 - 1) * (2.0f * hv + g), x, z);
							ids[k] = s.spawn_building_world(x, z, BLD_HOUSE, owner, (kind == 2 && k == tk) ? 0.0f : 1.0f, rot);
						}
						s.end_bulk_spawn();
						for (const auto &b : s.buildings()) const_cast<Simulation::Building &>(b).territory_decay_rate = 0.0f;
						s.spawn_unit(-40.0f, 40.0f, 1, UNIT_SPEARMAN); // separation() pushes off structures with two units or more
						const float ang = dir * 0.78539816f;
						float sx, sz;
						at(12.0f * std::cos(ang), 12.0f * std::sin(ang), sx, sz);
						const UnitType type = kind == 0 ? UNIT_SIEGE_RAM : kind == 1 ? UNIT_ELEPHANT : UNIT_WORKER_ELEPHANT;
						const uint32_t id = s.spawn_unit(sx, sz, 0, type);
						s.order_set_stance(&id, 1, STANCE_PASSIVE);
						const uint32_t target = ids[tk];
						auto find = [&](uint32_t bid) -> const Simulation::Building * {
							for (const auto &b : s.buildings())
								if (b.id == bid) return &b;
							return nullptr;
						};
						const float hp0 = find(target)->hp, prog0 = find(target)->build_progress;
						if (kind == 2) s.order_build_or_repair(&id, 1, target, false, true);
						else s.order_attack_building(&id, 1, target, false, false);
						bool ok = false, two = false;
						float deep = 0.0f;
						for (int t = 0; t < (int)(90.0f / Simulation::FIXED_STEP) && !ok; ++t) {
							s.tick(Simulation::FIXED_STEP);
							const int i = index_of(s, id);
							if (i < 0) break;
							if (large_bands_holding(s, s.units().pos_x[i], s.units().pos_z[i], target, deep) >= 2) two = true;
							const Simulation::Building *b = find(target);
							ok = !b || (kind == 2 ? b->build_progress > prog0 : b->hp < hp0);
						}
						const bool outer = tk != 4;
						if (two || (outer && !ok)) {
							const int i = index_of(s, id);
							std::printf("  %s house %d of a cluster %d m apart turned %.1f, from direction %d: %s%s, ended at (%.2f, %.2f)\n", kinds[kind],
									tk, gap_m, rot, dir, ok ? "started" : "never started", two ? ", stood inside two other houses' 3 m" : "",
									i >= 0 ? s.units().pos_x[i] : 0.0f, i >= 0 ? s.units().pos_z[i] : 0.0f);
						}
						++runs;
						started += ok;
						in_two += two;
						deepest = std::max(deepest, deep);
						if (outer) {
							++outer_runs;
							outer_started += ok;
						}
					}
	std::printf("  rams and elephants at a cluster of houses 4-6 m apart: %d of %d runs inside two other houses' 3 m, outer houses' tasks "
			"started %d of %d (all houses %d of %d), deepest in another house's 3 m %.3f\n",
			in_two, runs, outer_started, outer_runs, started, runs, deepest);
	check(in_two == 0, "a ram or an elephant stood inside two other houses' 3 m clearance in a cluster");
	check(outer_started == outer_runs, "a ram or an elephant never started its task at an outer house of a cluster");
	check(deepest <= 0.05f, "a ram or an elephant went into another house's 3 m clearance in a cluster");
}

// 44. Large units across a row of houses. Nine houses in a row along u, turned by rot, gap_m metres
// between neighbours' facing faces. A ram or an elephant starts 6 units south of the row (v), in front
// of a gap (su 0), of a house's middle (su -0.5) or of a house's side (su 0.37), and is sent 6 units
// north of it. It must get within 1 of that point in 90 s, and its centre must never come strictly
// between two houses' facing faces.
static void large_units_across_house_row() {
	float hu = 0.0f, hv = 0.0f;
	house_half_size(hu, hv);
	int runs = 0, between = 0, arrived = 0;
	for (const int gap_m : {4, 5, 6})
		for (const float rot : {0.0f, 0.4f})
			for (const UnitType type : {UNIT_SIEGE_RAM, UNIT_ELEPHANT})
				for (const float su : {-0.5f, 0.0f, 0.37f})
					for (const float zc : {0.0f, 0.5f}) {
						Simulation s;
						setup(s);
						const float g = gap_m / 4.0f, xc = 0.2f, pitch = 2.0f * hu + g;
						const float ux = std::cos(rot), uz = -std::sin(rot), vx = std::sin(rot), vz = std::cos(rot);
						auto at = [&](float lu, float lv, float &x, float &z) { x = xc + lu * ux + lv * vx; z = zc + lu * uz + lv * vz; };
						s.begin_bulk_spawn();
						for (int k = -4; k <= 4; ++k) {
							float x, z;
							at(k * pitch, 0.0f, x, z);
							s.spawn_building_world(x, z, BLD_HOUSE, 0, 1.0f, rot);
						}
						s.end_bulk_spawn();
						for (const auto &b : s.buildings()) const_cast<Simulation::Building &>(b).territory_decay_rate = 0.0f;
						s.spawn_unit(-40.0f, 40.0f, 1, UNIT_SPEARMAN); // separation() pushes off structures with two units or more
						float sx, sz, gx, gz;
						at((su + 0.5f) * pitch, -6.0f, sx, sz);
						at((su + 0.5f) * pitch, 6.0f, gx, gz);
						const uint32_t id = s.spawn_unit(sx, sz, 0, type);
						s.order_set_stance(&id, 1, STANCE_PASSIVE);
						s.cmd_move(&id, 1, gx, gz);
						bool arr = false, btw = false;
						for (int t = 0; t < (int)(90.0f / Simulation::FIXED_STEP) && !arr; ++t) {
							s.tick(Simulation::FIXED_STEP);
							const int i = index_of(s, id);
							const float dx = s.units().pos_x[i] - xc, dz = s.units().pos_z[i] - zc;
							const float lu = dx * ux + dz * uz, lv = dx * vx + dz * vz;
							if (std::fabs(lv) < hv && std::fabs(lu) < 4.0f * pitch) {
								// m runs from a house's west face: the house spans [0, 2 hu), the gap [2 hu, pitch).
								const float m = std::fmod(lu + 100.0f * pitch + hu, pitch);
								if (m > 2.0f * hu + 0.02f && m < pitch - 0.02f) btw = true;
							}
							arr = std::hypot(s.units().pos_x[i] - gx, s.units().pos_z[i] - gz) < 1.0f;
						}
						if (btw || !arr) {
							const int i = index_of(s, id);
							std::printf("  %s across a row of houses %d m apart turned %.1f (start %.2f along, row at %.1f): %s%s, ended at (%.2f, %.2f)\n",
									type == UNIT_SIEGE_RAM ? "ram" : "elephant", gap_m, rot, su, zc, arr ? "got there" : "never got there",
									btw ? ", went between two houses" : "", s.units().pos_x[i], s.units().pos_z[i]);
						}
						++runs;
						between += btw;
						arrived += arr;
					}
	std::printf("  rams and elephants across a row of houses 4-6 m apart: %d of %d got there, %d went between two houses\n", arrived, runs, between);
	check(between == 0, "a ram or an elephant went between two houses of a row less than 6 m apart");
	check(arrived == runs, "a ram or an elephant never got across a row of houses");
}

// 45. Orders after a load. The cluster of 43, square, its houses 4 or 5 m apart: a ram or an elephant
// (player 0) is to attack one of player 1's houses, or a worker elephant to build one of its own, from 12
// units off in direction dir. The game plays 1 s and is saved, and the save is loaded into a second game.
// Mode 0 gives both the same order before either ticks again, as a turn's commands come before its
// ticks; mode 1 sends the unit 1.5 units on and queues the order behind that walk, so that the queue
// hands it on within a tick, before move_units. The stand point the order sets (large_stand_point, where
// the point by the target lies within another house's 3 m) and, 10 s on, the unit's place and the
// lockstep hash must be the same in the game and its loaded copy.
static void orders_after_load() {
	float hu = 0.0f, hv = 0.0f;
	house_half_size(hu, hv);
	const char *dir_env = std::getenv("TMPDIR");
	const std::string path = std::string(dir_env && *dir_env ? dir_env : "/tmp") + "/rts0_test_building_steering_orders.sav";
	TurnManager tm;
	int runs = 0, moved = 0, point_differ = 0, place_differ = 0, hash_differ = 0, failed = 0;
	for (const int gap_m : {4, 5})
		for (int kind = 0; kind < 3; ++kind)
			for (const int tk : {0, 1, 2, 3, 5})
				for (int mode = 0; mode < 2; ++mode)
					for (int dir = 0; dir < 8; dir += mode == 0 ? 2 : 4) {
						auto ap = std::make_unique<Simulation>(), bp = std::make_unique<Simulation>();
						Simulation &a = *ap, &b = *bp;
						setup(a);
						const float g = gap_m / 4.0f, xc = 0.3f, zc = 0.6f;
						const uint8_t owner = kind == 2 ? 0 : 1;
						uint32_t ids[9];
						a.begin_bulk_spawn();
						for (int k = 0; k < 9; ++k)
							ids[k] = a.spawn_building_world(xc + (k % 3 - 1) * (2.0f * hu + g), zc + (k / 3 - 1) * (2.0f * hv + g), BLD_HOUSE, owner,
									(kind == 2 && k == tk) ? 0.0f : 1.0f, 0.0f);
						a.end_bulk_spawn();
						// No decay, through the setting a save keeps (Building::decay_disabled).
						for (const uint32_t h : ids) a.set_structure_no_decay(h);
						a.spawn_unit(-40.0f, 40.0f, 1, UNIT_SPEARMAN); // separation() pushes off structures with two units or more
						const float ang = dir * 0.78539816f, sx = xc + 12.0f * std::cos(ang), sz = zc + 12.0f * std::sin(ang);
						const UnitType type = kind == 0 ? UNIT_SIEGE_RAM : kind == 1 ? UNIT_ELEPHANT : UNIT_WORKER_ELEPHANT;
						const uint32_t id = a.spawn_unit(sx, sz, 0, type);
						a.order_set_stance(&id, 1, STANCE_PASSIVE);
						for (int t = 0; t < (int)(1.0f / Simulation::FIXED_STEP); ++t) a.tick(Simulation::FIXED_STEP);
						const bool ok = a.save_state(path.c_str()) && b.load_state(path.c_str());
						std::remove(path.c_str());
						if (!ok) {
							++failed;
							continue;
						}
						const uint32_t target = ids[tk];
						auto order = [&](Simulation &x) {
							if (mode == 1) x.order_walk(&id, 1, sx + 1.5f * std::cos(ang), sz + 1.5f * std::sin(ang), false, true);
							if (kind == 2) x.order_build_or_repair(&id, 1, target, mode == 1, true);
							else x.order_attack_building(&id, 1, target, mode == 1, false);
						};
						order(a);
						order(b);
						const int ia = index_of(a, id), ib = index_of(b, id);
						const bool point = ia < 0 || ib < 0 || a.units_.target_x[ia] != b.units_.target_x[ib] ||
								a.units_.target_z[ia] != b.units_.target_z[ib];
						for (int t = 0; t < (int)(10.0f / Simulation::FIXED_STEP); ++t) {
							a.tick(Simulation::FIXED_STEP);
							b.tick(Simulation::FIXED_STEP);
						}
						const int ja = index_of(a, id), jb = index_of(b, id);
						const bool place = ja < 0 || jb < 0 || a.units_.pos_x[ja] != b.units_.pos_x[jb] || a.units_.pos_z[ja] != b.units_.pos_z[jb];
						const bool hash = tm.compute_sim_hash(a) != tm.compute_sim_hash(b);
						if (point || place || hash)
							std::printf("  orders after a load: kind %d, house %d, %d m apart, direction %d, mode %d: stand point (%.3f, %.3f) in the "
									"game, (%.3f, %.3f) in its loaded copy; 10 s on (%.3f, %.3f) and (%.3f, %.3f)%s\n", kind, tk, gap_m, dir, mode,
									ia >= 0 ? a.units_.target_x[ia] : 0.0f, ia >= 0 ? a.units_.target_z[ia] : 0.0f,
									ib >= 0 ? b.units_.target_x[ib] : 0.0f, ib >= 0 ? b.units_.target_z[ib] : 0.0f,
									ja >= 0 ? a.units_.pos_x[ja] : 0.0f, ja >= 0 ? a.units_.pos_z[ja] : 0.0f,
									jb >= 0 ? b.units_.pos_x[jb] : 0.0f, jb >= 0 ? b.units_.pos_z[jb] : 0.0f, hash ? ", hashes differ" : "");
						++runs;
						moved += ja >= 0 && std::hypot(a.units_.pos_x[ja] - sx, a.units_.pos_z[ja] - sz) > 1.0f;
						point_differ += point;
						place_differ += place;
						hash_differ += hash;
					}
	std::printf("  large units' attack and build orders given to a game and its loaded copy: %d runs (%d moved off), the stand point "
			"differs in %d, the place 10 s on in %d, the lockstep hash in %d; %d saves failed\n", runs, moved, point_differ, place_differ,
			hash_differ, failed);
	check(failed == 0, "a game with large units at a house cluster could not be saved and loaded");
	check(point_differ == 0, "an order gave a large unit another stand point in a loaded game than in the game it was saved from");
	check(place_differ == 0 && hash_differ == 0, "a large unit's order took a loaded game out of sync with the game it was saved from");
	check(moved * 2 >= runs, "the units given orders after a load did not move");
}

// 46. A field's supply and a felled tree in one tick, then a load. Six oaks with 1 wood each stand in a
// column 10 units apart, a woman beside each; they fell them one at a time, each woman sent away once
// her oak is felled. Before the tick in which the woman's next load is due, a finished field is placed,
// so that its supply comes at the start of that tick's tick_gather (sync_field_resources) and the tree
// goes at its end (remove_at moves the field's supply into the tree's place). After each tick in which
// both happened, the game is saved and the save loaded into a second game: the two must list the same
// trees, bushes and treasures on the nodes (NavGrid::TILE_OBS_GAIA) and keep the same open nodes and
// obstruction counts in every class, and a ram sent to the felled tree's spot through a 4 m gap between
// two houses must keep them hash-identical for 3 s.
static void field_and_felled_tree_then_load() {
	const char *dir_env = std::getenv("TMPDIR");
	const std::string path = std::string(dir_env && *dir_env ? dir_env : "/tmp") + "/rts0_test_building_steering_field.sav";
	TurnManager tm;
	auto ap = std::make_unique<Simulation>();
	Simulation &a = *ap;
	setup(a);
	std::vector<uint32_t> trees, women;
	for (int k = 0; k < 6; ++k) {
		const float tx = 10.5f, tz = -25.5f + 10.0f * (float)k;
		trees.push_back(a.spawn_resource_world(tx, tz, RES_WOOD, 1));
		women.push_back(a.spawn_unit(tx - 1.0f, tz, 0, UNIT_FEMALE_CITIZEN));
	}
	float hu = 0.0f, hv = 0.0f;
	house_half_size(hu, hv);
	for (int t = 0; t < 4; ++t) a.tick(Simulation::FIXED_STEP);
	const bool nodes = a.nav_grid_.tile_nodes_on();
	int events = 0, fields = 0, failed = 0, list_differ = 0, open_differ = 0, count_differ = 0, hash_differ = 0;
	size_t cur = 0;
	for (int t = 0; t < 4000 && cur < trees.size(); ++t) {
		if (!a.res_id_to_idx_.count(trees[cur])) {
			// She would go on to the next oak (UnitAI's search for the nearest supply of the type): she is
			// sent away, so that each tree is felled by its own woman, one supply at a time.
			a.order_walk(&women[cur], 1, -30.0f, 50.0f, false, true);
			++cur;
			continue;
		}
		const int w = index_of(a, women[cur]);
		if (w < 0) break;
		if (a.units_.action_state[w] != ACTION_GATHER) a.order_gather(&women[cur], 1, trees[cur]);
		const size_t ti = a.res_id_to_idx_.at(trees[cur]);
		const float fx = a.resources_.x[ti], fz = a.resources_.z[ti];
		// Her next load falls due within this tick or the next (attack_timer counts to 1 / gather_rate).
		const bool due = a.units_.action_state[w] == ACTION_GATHER && a.units_.gather_rate[w] > 0.0f &&
				a.units_.attack_timer[w] + 1.5f * Simulation::FIXED_STEP >= 1.0f / a.units_.gather_rate[w];
		const size_t field_supplies = a.field_res_by_bld_.size();
		if (due && fields < 12) {
			a.spawn_building_world(-40.0f + 7.0f * (float)(fields % 6), -10.0f - 7.0f * (float)(fields / 6), BLD_FIELD, 0, 1.0f);
			++fields;
		}
		a.tick(Simulation::FIXED_STEP);
		if (a.res_id_to_idx_.count(trees[cur]) || a.field_res_by_bld_.size() == field_supplies) continue;
		// A field's supply came and the tree went in this tick.
		++events;
		auto bp = std::make_unique<Simulation>();
		Simulation &b = *bp;
		if (!a.save_state(path.c_str()) || !b.load_state(path.c_str())) {
			++failed;
			std::remove(path.c_str());
			continue;
		}
		std::remove(path.c_str());
		const NavGrid &x = a.nav_grid_, &y = b.nav_grid_;
		const auto &l = x.tile_obs_[NavGrid::TILE_OBS_GAIA], &m = y.tile_obs_[NavGrid::TILE_OBS_GAIA];
		size_t ld = l.size() == m.size() ? 0 : std::max(l.size(), m.size());
		for (size_t k = 0; k < l.size() && k < m.size(); ++k) ld += l[k] != m[k];
		size_t od = 0, cd = 0;
		for (int cls = 0; cls < NavGrid::TILE_CLASSES; ++cls) {
			const auto &p = x.tile_open_[cls], &q = y.tile_open_[cls];
			od += p.size() == q.size() ? 0 : std::max(p.size(), q.size());
			for (size_t k = 0; k < p.size() && k < q.size(); ++k) od += p[k] != q[k];
			const auto &c = x.tile_obs_count_[cls], &d = y.tile_obs_count_[cls];
			cd += c.size() == d.size() ? 0 : std::max(c.size(), d.size());
			for (size_t k = 0; k < c.size() && k < d.size(); ++k) cd += c[k] != d[k];
		}
		const size_t listed = l.size(), listed_loaded = m.size();
		// Two houses 4 m apart stand 5 units east of the felled tree's spot, and a ram 8 units past them is
		// sent through their gap to the spot: the large class plans round the gap, too narrow for it, on
		// the nodes (NavGrid::path_through_narrow_gap), where the goal is moved off any tree still listed
		// there (MakeGoalReachable). The game plays on with them. The woman is sent away.
		for (Simulation *s : {&a, &b}) {
			s->spawn_building_world(fx + 5.0f, fz + 0.5f + hv, BLD_HOUSE, 0, 1.0f, 0.0f);
			s->spawn_building_world(fx + 5.0f, fz - 0.5f - hv, BLD_HOUSE, 0, 1.0f, 0.0f);
			const uint32_t ram = s->spawn_unit(fx + 13.0f, fz, 0, UNIT_SIEGE_RAM);
			s->order_walk(&ram, 1, fx, fz, false, true);
			s->order_walk(&women[cur], 1, -30.0f, 50.0f, false, true);
		}
		bool hd = false;
		for (int k = 0; k < (int)(3.0f / Simulation::FIXED_STEP) && !hd; ++k) {
			a.tick(Simulation::FIXED_STEP);
			b.tick(Simulation::FIXED_STEP);
			hd = tm.compute_sim_hash(a) != tm.compute_sim_hash(b);
		}
		std::printf("  a field's supply and a felled tree in one tick (tree %zu at (%.2f, %.2f)): the game lists %zu gaia tile "
				"obstructions, its loaded copy %zu, %zu differ; open nodes differ at %zu, obstruction counts at %zu%s\n", cur, fx, fz,
				listed, listed_loaded, ld, od, cd, hd ? "; the hashes differ within 3 s" : "");
		list_differ += ld != 0;
		open_differ += od != 0;
		count_differ += cd != 0;
		hash_differ += hd;
	}
	std::printf("  fields and felled trees in one tick, then a load: nodes kept %d, %d fields placed, %d such ticks; the gaia tile "
			"lists differ after %d, the open nodes after %d, the obstruction counts after %d, the hashes after %d; %d saves failed\n",
			nodes, fields, events, list_differ, open_differ, count_differ, hash_differ, failed);
	check(nodes, "a level map keeps no nodes for the large class");
	check(events >= 3, "scenario 46 no longer has a tick in which a field's supply comes and a tree goes");
	check(failed == 0, "a game with fields and felled trees could not be saved and loaded");
	check(list_differ == 0 && count_differ == 0 && open_differ == 0,
			"a field's supply and a felled tree in one tick left the nodes otherwise than a load of the same game lists them");
	check(hash_differ == 0, "a field's supply and a felled tree in one tick took a loaded game out of sync with the game it was saved from");
}

// 47. find_path after a failed narrow-gap re-plan. The channel of 16 (|z| < 6, deep water across the map)
// and, south of it, two houses 4 m apart, the gap between them on the line from a ram at (0, -40) to
// (0, 30), across the water. The large class's path on the navcells takes the gap (8 m navcells, which the
// houses don't cover whole), so find_path plans it again on the nodes. First with the nodes' regions, as
// update_tile_open keeps them: MakeGoalReachable (HierarchicalPathfinder.cpp:689-719) moves the goal to
// the nearest node of the start's region, on the bank, and the path gets there round the houses. Then
// with the regions dropped, as before update_tile_open has worked them out: the search on the nodes ends
// where it can get no nearer the goal, short of it, and find_path returns false with that path. Before,
// it fell back to the default class's path, through the gap the re-plan was made to go round.
static void find_path_after_failed_narrow_gap_replan() {
	Simulation s;
	channel_setup(s, 6.0f);
	float hu = 1.0f, hv = 1.0f;
	house_half_size(hu, hv);
	s.spawn_building_world(-0.5f - hu, -20.0f, BLD_HOUSE, 0, 1.0f);
	s.spawn_building_world(0.5f + hu, -20.0f, BLD_HOUSE, 0, 1.0f);
	const uint32_t ram = s.spawn_unit(0.0f, -40.0f, 0, UNIT_SIEGE_RAM);
	s.tick(DT);
	const uint8_t pass = s.unit_land_pass((size_t)index_of(s, ram));
	const float sx = 0.0f, sz = -40.0f, gx = 0.0f, gz = 30.0f;
	auto through_gap = [&](const WaypointPath &p) { return s.nav_grid_.path_through_narrow_gap(sx, sz, p); };
	auto same = [](const WaypointPath &a, const WaypointPath &b) {
		if (a.count != b.count) return false;
		for (int k = 0; k < a.count; ++k)
			if (a.xs[k] != b.xs[k] || a.zs[k] != b.zs[k]) return false;
		return true;
	};
	WaypointPath kept, def;
	const bool kept_found = s.find_path(sx, sz, gx, gz, kept, pass);
	const bool def_found = s.nav_grid_.find_path_on(sx, sz, gx, gz, def, pass & ~PASS_LARGE);
	const float kx = kept.count ? kept.xs[kept.count - 1] : sx, kz = kept.count ? kept.zs[kept.count - 1] : sz;
	// The regions dropped: tile_open_cached() fails, and find_path_tiles searches the nodes as the terrain
	// has them, without MakeGoalReachable's regions.
	s.nav_grid_.tile_open_ready_ = false;
	WaypointPath failed, replan;
	bool gap = false;
	const bool failed_found = s.find_path(sx, sz, gx, gz, failed, pass);
	const bool replan_found = s.nav_grid_.find_path_on(sx, sz, gx, gz, replan, pass, &gap);
	const float fx = failed.count ? failed.xs[failed.count - 1] : sx, fz = failed.count ? failed.zs[failed.count - 1] : sz;
	std::printf("  find_path after a narrow-gap re-plan: with the regions it %s, ending at (%.2f, %.2f)%s; without them it "
			"returns %d with %d waypoints ending at (%.2f, %.2f)%s, the re-plan's own (%d, narrow gap %d)%s; the default class's "
			"path (%d) goes through the gap (%d)\n", kept_found ? "gets there" : "FAILS", kx, kz, through_gap(kept) ? " THROUGH THE GAP" : "",
			(int)failed_found, failed.count, fx, fz, through_gap(failed) ? " THROUGH THE GAP" : "", (int)!replan_found, (int)gap,
			same(failed, replan) ? "" : " (NOT THE SAME)", (int)def_found, (int)through_gap(def));
	check(kept_found && kz < -6.0f && kz > -9.0f && !through_gap(kept),
			"a large path re-planned round a narrow gap did not get to the bank MakeGoalReachable moved its goal to");
	check(through_gap(def), "scenario 47's default-class path no longer goes through the gap (the case is gone)");
	check(!failed_found && failed.count > 0 && gap && !replan_found && same(failed, replan),
			"find_path did not keep the failed narrow-gap re-plan's own path");
	check(!through_gap(failed) && !same(failed, def), "find_path fell back to the default class's path through a gap too narrow for the class");
}

int main() {
	std::printf("RTS0 building steering test (src/simulation/nav/building_steering.h)\n");
	guard_behind_wall();
	gather_around_houses();
	move_through_gap();
	gather_behind_long_wall();
	walk_across_fields();
	out_of_house_cluster();
	walk_past_turned_building();
	siege_through_gap();
	walk_into_building();
	walk_reissued_every_tick();
	walk_to_one_point();
	ships_that_cannot_get_there();
	walk_into_closed_gate();
	crowds_at_the_shore();
	cliffs();
	chases();
	large_units_through_gap();
	walk_to_structure_range();
	mines();
	shore_clearance();
	trees();
	circular_map_edge();
	forest_cells();
	repair_in_house_cluster();
	cliff_clearance();
	tree_gap_dead_end();
	thin_ridges();
	ridge_from_map_edge_and_pass();
	large_units_and_one_tile_gap();
	large_units_and_two_tile_gap();
	tree_in_one_tile_gap();
	structure_beside_one_tile_gap();
	nodes_after_load();
	turned_structure_beside_cliff();
	corner_dance();
	kept_path_obstructed();
	waypoint_in_a_tree();
	straight_to_near_goal();
	walk_past_map_edge();
	large_units_beside_hemmed_in_arsenal();
	large_units_at_narrow_gap_mouth();
	large_units_at_house_cluster();
	large_units_across_house_row();
	orders_after_load();
	field_and_felled_tree_then_load();
	find_path_after_failed_narrow_gap_replan();
	if (g_failures) {
		std::printf("FAIL (%d)\n", g_failures);
		return 1;
	}
	std::printf("PASS\n");
	return 0;
}
