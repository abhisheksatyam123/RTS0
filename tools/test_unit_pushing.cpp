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
//     0.26 s. Turning past InstantTurnAngle (0.75 rad for siege, 1.5 for infantry) stops it: it turns
//     on the spot at its TurnRate (4 rad/s for siege, 14 for infantry) until within that angle, so a
//     ram sent back the way it came stands for 0.6 s and a spearman for 0.12 s, then starts again from
//     rest; a gentler turn keeps cos(angle) of its speed. A unit attacking another faces it, and a
//     trained one faces away from the structure that trained it.
// 12. A group sent to one point (no formation) gets a point each round it, as Commands.js "walk" does
//     with DistributeAround: a spiral from 1 m out, rings 1.75 m apart, points 9/pi m apart along a
//     ring (at the crowd scale, PUSH_SCALE), handed out so that no two units would travel less by swapping.
//     A unit on its own walks to the point itself.
// 13. HandleObstructedMove on a long path: a walker obstructed within max(ShortPathSearchRange / 3,
//     8 m) of its next long waypoint drops it for the one after; one further off plans its whole path
//     again from where it stands on its third failed turn; within 16 m of its goal it does neither.
//     The pressure speed floor is the template walk speed when that is slower than 1.5 m/s, so a
//     unit slowed by a modifier still walks at the floor.
// 14. Animals push and are pushed like units: 0.28's CCmpUnitMotionManager holds every entity with
//     UnitMotion, and template_unit_fauna.xml keeps template_unit.xml's blocking Obstruction. A
//     walker whose line runs into a standing deer's body is nudged round it, sideways and never
//     back, as one walking into an idle unit's body is, and the deer, standing, is not moved by a
//     walker (Push never mixes moving and idle).
// 15. Two of a player's sheep walking at each other on lines 0.1 apart part sideways, as two walkers
//     do (test 9), and standing animals put on one spot spread out, as units do (test 2), a unit
//     among them.
// 16. An adult elephant (UnitMotion Weight 50, "large" class) pushes a spearman harder than he
//     pushes it, and the pair parts by their clearances. A bull charging a soldier who stands still
//     bites him: pushing never takes a melee pair out of range, as 0.28 measures range between the
//     two obstructions.
// 17. Animals whose obstruction does not block movement (BlockMovement false: chickens, rabbits,
//     sharks, whales) push nobody and nobody pushes them; nor does a penned animal (garrisoned).
//     A pushed animal never goes where it could not step itself (PushAdjust's CheckMovement): it
//     stays off a structure.
// 18. A herd under pushing pressure saved and loaded goes on as the original (0.28 serializes
//     each animal's pushing pressure and failed movements).
// 19. A player's sheep sent onto a standing ram or war elephant ends its walk near its point:
//     UnitAI WALKING finishes a walk obstructed within DefaultRelaxedMaxRange (12 m) of its point,
//     and on MoveFailed, so it doesn't walk in place against the body for good. A herded sheep and
//     its herder push each other as any two walkers do (Push has no exception for an entity and the
//     one it walks to); heading for its corral, the sheep is nudged round its herder standing in its
//     way, and still gets into the corral.
// 20. A boar or a bull charging an archer behind a line of idle spearmen bites it: the soldiers
//     standing round an animal that stands at its target to bite don't push it out of reach.
// 21. A worker taking a full load to a storehouse ringed by its own player's units standing on the
//     drop range (idle spearmen with no order, parked there as 0.28's FINDINGNEWTARGET parks them,
//     or women cutting the trees of a wood round it) walks through them, drops its load and walks
//     back out to its tree, and so does one sent with ReturnResource; none of the idle ring is pushed
//     while they pass.
//     At RTS0's crowd scale the ring closes the drop range, where 0.28's 13 m storehouse needs over
//     20 such units to close it, and the returner stood outside with its load for good.
// 22. A walker that starts just outside an idle unit's square, heading slightly into it, gets round it
//     (Stats' freeze). In Stats' two layouts, a woman beside a town centre with another woman
//     standing at its corner (Continental seed 5), and a woman in her rally batch whose way runs
//     past a woman standing 1.4 ahead, the unit squares at RTS0's crowd scale leave no way out:
//     each corner of her short path lies in a square she can't step into. Before, the walk failed
//     after 7 s and a builder never left the spot, the order standing for good. Now she searches
//     again among smaller squares shaped as 0.28's (both crowd clearances less 0.5 m at the crowd
//     scale, VertexPathfinder.cpp:618-625, never smaller than the two bodies) with a corner offset
//     as small as 0.28's 1/16 m, and walks that path. The same holds for a woman in a slit between
//     such a square and a second woman's or a town centre's, and nowhere does the walker come inside
//     a standing unit's body. A phalanx walking past a standing woman or cavalryman keeps its
//     members out of her body: a member never searches among the smaller squares, as its slot moves
//     every tick and the smaller squares' step test lasts only while its aim stays put (with it,
//     members walked through the standing unit in 15 and 40 of 63 runs).
// 23. A unit or an animal standing in deep water gets out across a steep shore: 0.28's CheckLineMovement
//     lets an entity on impassable navcells cross more of them until it reaches passable ground
//     (Pathfinding.cpp:46-48, 61-75), and deep water and steep ground are alike impassable to it. In a
//     bay whose shore is a cliff, a woman put in the water walks out to her point, and a wolf put in
//     the water comes out to bite a spearman on the shore; sent back in, the woman stops at the cliff's
//     clearance (the way leads off impassable ground, never onto it). Before, the woman stood at the
//     cliff's clearance for good (a unit in the water could not step within it of a steep tile) and the
//     wolf never moved (an animal could step only onto passable navcells): test_petra's wolf, put in
//     the water beside Petra's woman, could be killed only by the civic centre's arrows.
// 24. That way off impassable ground opens only for an animal on impassable terrain, and no animal's
//     step leads past the map's edge less its clearance: 0.28's CheckMovement reads the terrain-only
//     grid, which closes the off-world navcells for every class and grows them by its Clearance
//     (CCmpPathfinder.cpp:700-761, 971-984), and tests structures as shapes. Deer and hares at a
//     circular map's rim, in a sea and on land, two deer fleeing hunters inland, stay inside the
//     edge less their clearance (with the way open to every animal on an impassable navcell they
//     crossed the off-world navcells out of the map's circle; on land the navcells alone let them
//     about 1.2 past the edge). A wolf on a navcell a house's raster closes, beside a deep channel,
//     never steps into the water after a spearman on the far bank (counted as on impassable ground,
//     it swam across).
// 25. Workers west of a wall of Cretan date palm patches (4 m squares on the 4 m tiles), with their
//     storehouse 13-29 m away beyond it, get round the wall (Stats' soak, sahara seed 3: p0's
//     storehouse 1674 and its wood, as they stood at 209 s; 20 of the 28 stuck reports there). A
//     spearman cutting a tree beyond the wall with a full load (tick_gather's return), one that
//     Petra's worker update orders to gather again every 1.6 s (worker.js:236-269, which starts its
//     failed turns again each time), a woman sent with ReturnResource, an empty woman sent to
//     gather a tree beyond the wall, and a spearman a player walks to a point beyond it. 0.28's
//     HandleObstructedMove asks for a short path among all static shapes within 16 m of the goal, its
//     range growing with failed turns, and its long path goes round the trees (1 m navcells); RTS0's
//     steering saw trees within 10 m only, and the walkers pressed into the wall for good (the
//     gatherer took a tree on the near side once MoveFailed came, the walk ended in the notch). The
//     path found is the unit's path (saved): a game saved on the way and loaded goes on the same.
//     A second unit stands far off, as separation(), which keeps units out of the trees, runs only
//     with two or more.
// 26. A worker obstructed within 16 m of its goal, whose way round a long wall leads out of the short
//     path's reach, gets its long path back on the third failed turn. Six women with full loads stand
//     west of a wall whose gap is 40 units off, their storehouse just beyond it, and Petra orders
//     them to gather again every 1.6 s; a woman sent to a palm 3.8 units off beyond a wall whose gap
//     is 30 units off is shut in by wall segments for 1.4 s. A short path among the static shapes
//     that ends at the wall's near face replaced the long path on an early obstructed turn, as
//     0.28's ComputePathToGoal does, but 0.28 asks for a long path on the third (ShouldAlternate-
//     Pathfinder) within 16 m too, and nothing planned one again: a returner stood at the wall for
//     37 s until MoveFailed and Petra's land check sent her to the town centre, and the gatherer gave
//     her palm up. Every woman drops at that storehouse, the gatherer cuts her palm, and a game saved
//     on the way and loaded goes on the same.
// 27-30. Builders that never built in Stats' e2e_human_path runs (main 8f0eb61ab), each scene as the
//     game dumped it: the civ, the village round the civic centre, every unit and structure under its
//     id (the turn a unit searches on and the way a push takes follow its id).
// 27. A worker sent to build from inside the crowd of its player's units standing idle by the civic
//     centre (the soldiers it unloaded, with the women among them: civs 0, 2, 4, 6, 9 and 11) gets out
//     and builds. The crowd shuts every way out at RTS0's crowd scale, among the smaller squares too,
//     and the builder stood in it with its order for good, as 0.28's REPAIR never gives a walk to the
//     site up. Now a builder whose searches find no way from where it stands walks through its own
//     player's units that stand idle with no order, as a returner does (21), and one obstructed at the
//     waypoint of a path round idle units (a path that led it within a standing woman's body, where
//     separation nudged it off each tick) searches on from there, as HandleObstructedMove does.
// 28. A site that waits for a unit on it to leave starts: LeaveFoundation's point is never inside
//     another structure (civ 9: the barracks beside the house site) nor within a standing unit's square
//     at the crowd scale (civ 13: 0.25 from the builder), where the unit's short path found no way and
//     it stood on the site for good, and the site with it.
// 29. A builder standing on its own site, its way off it shut by idle units, commits it from there, as
//     0.28's builder is in range inside the footprint (civ 15's barracks): everyone on it leaves, the
//     builder too, and it comes back to build. Before, the commit was tried only from outside.
// 30. A builder given a gather order drops its path to the site, as StopMoving does (civ 4's market
//     builder kept it as its next waypoint): the gather walk plans its own.
// 31. Spearmen walking across a site that waits for them to leave, as a group and as a box formation,
//     keep their walk to their points beyond it: 0.28 puts LeaveFoundation in front of the walk, which
//     resumes once they are off. A walk is replaced only when LeaveFoundation gave it (its point on one
//     of the rings just off the site that its points lie on) and that point is now taken or shut.
//     Before, one whose point was near another's point (DistributeAround's ring, a formation's shared
//     goal) was sent to the site's edge, ended there, and left its formation.
// 32. A woman standing on the land part of a dock site, its seaward edge the nearest way off, sent to
//     build it or standing there while another builds it: she leaves by land and the dock starts.
//     LeaveFoundation's point for a land unit is on a navcell it can stand on and reach
//     (Order.LeaveFoundation's range goal is planned on reachable navcells); out at sea, her walk ended
//     on the site at the shore and she stood there (the builder since a builder on its site commits
//     it, the woman standing there on main already).
// 33. The same, the land round the dock packed with idle women so that every point off it on land is
//     taken: she is still sent to a point on land, the nearest she can stand on and reach. Before, with
//     no free point, she was sent to the first one, out at sea.
// 34. A house site laid in a crowd of idle women, some of them on it: while it waits, a unit walking
//     off it to its point is not given that walk again (0.28 leaves a LeaveFoundation under way as it
//     is). Before, each was, every turn, its path planned anew each time: 1.3 find_path calls a turn
//     here, twice main's.
// 35. Civ 13's market builder in Stats' e2e_human_path (main 54caf1cf5), as the game dumped it, stood
//     in the crowd its civic centre unloaded for good, in BUILD with its long path and no velocity.
//     Its search found a way down a slit between two idle units' crowd-scale squares, through the
//     body of an archer whose square held it. Walking into that body, separation slid it off the path;
//     its next step entered the square beside the slit and was refused, and it dropped the path,
//     walked back to where it searched and found the same path. Every search found a way, so it never
//     had the stuck memo that lets a builder through its own idle units (27). Now a builder caught in
//     that loop (its search finds the way whose path it dropped, or a step along its path is refused
//     after more than 12 obstructed turns), where only its own idle units are in its way, takes it as
//     no way from there. The dumped crowd, the same with the village round the civic centre, and two
//     random layouts round it (one where the builder walked in place, its failed turns never adding up
//     to a MoveFailed). And builders the rule must leave to 0.28's: one refused among farmers at work
//     and idle archers on its way to a house, and the civ 13 crowd and a random crowd where the units in
//     the way are an ally's (the review of a first form of the rule, which took every refused step
//     along the path as no way: there the first started after 54 s instead of 9, the others never),
//     and a builder whose search finds the way it dropped but whose line to its site crosses an ally's
//     idle units (the rule without its test of that line sealed him into the crowd for good).
// 36. Workers in a palm grove whose storehouse lies 75 m off (Stats' soak, bahrain seed 3, about 1020 s:
//     p3's grove and storehouse, where returners stood with their loads for good) get round the grove and
//     drop their loads, one of them ordered to gather again every 1.6 s as Petra's worker update does. The
//     goal's centre lies beyond 56 m, out of the short path's reach, and RTS0's long path there runs
//     straight through the palms (8 m navcells): obstructed_short_path did nothing, MoveFailed sent the
//     worker at the same storehouse again, and the re-sent one never even got that far. 0.28's long path
//     goes round the trees (1 m navcells); RTS0's stand-in on the third failed turn is a short path toward
//     the goal within 56 m. Every path such a search lays ends in the storehouse's range (within a
//     navcell) or at the storehouse, and a game saved on the way and loaded hashes as the original: one
//     saved while both women walk the paths their far third turns laid, and one saved after.
// 37. Two walls of palms 22 units apart and a goal 40 units off, beyond both: the first far search's box
//     clamps the goal between the walls, which the vertex pathfinder counts as reached, so its path ends
//     about 15 units short. The way on is kept after it (the walk's own path, or the storehouse), so the
//     unit is not taken to have reached the end of a long path that falls short: a walk arrives instead
//     of ending at the second wall, a returner drops its load with its failed turns never past 10, and so
//     does one Petra orders to gather again every 1.6 s, whose path to the storehouse that order keeps.
//     That order drops a path she has walked to its end, as 0.28's StopMoving does: one that ends where
//     she stands, about 38 units short of the storehouse's range, held her at the first wall for as long
//     as the orders came (the guard took it for the storehouse out of reach, and each order reset her
//     failed turns before MoveFailed); she drops her load within 60 s.
//     A walk 65 units off whose own path bends round two fortresses past the first wall takes the
//     sub-goal search instead (HandleObstructedMove's short path to a circle round the next long
//     waypoint, CCmpUnitMotion.h:1416-1442): its third failed turn lays a way round the wall to that
//     circle (radius 1 unit at the third turn; the search reaches 0.28's 20 m, or the waypoint plus 1 m
//     when that is farther) with the rest of the walk's path after it, and the walk arrives. Before, it
//     stood at the wall until MoveFailed.
//     RANGE and STEP call that third failed turn's search alone, on a path set by hand (a spearman 1.5
//     units west of a palm wall, his point 60 units east). With the held path's next waypoint 2.5 to 4
//     units east of him (1 to 2.5 past walls 5 to 9 palms long), the search reaches 0.28's 20 m (5 units)
//     and its path ends on the waypoint's circle with the rest kept, all 9 times; a range of the waypoint
//     plus 1 m alone ends there 3 times. A unit already inside the circle round its next waypoint
//     (obstructed_long_path's re-plan on the same turn can start the path within a unit of it) heads for
//     the circle round the waypoint after it, as HandleObstructedMove pops a long waypoint within
//     skipbeyond (:1421-1426), or, past the last but one, takes the far search, with the point kept after
//     its stretch; without the step it kept its path.
// 38. A spearman Petra sent into its civic centre after a fight on Stats' Islands soak (main 56515ffc3,
//     seed 2, unit 812), the army it fought with standing idle round it: the bay's shore, as the game's
//     navcells laid it, the civic centre beyond the bay, a barracks east of the 20 m of land between
//     them, and 11 of the idle javelineers and spearmen standing in that strip, each under its id in
//     the game. On the shore the water's clearance bars every step but one, into an idle javelineer's
//     square; every search, among the crowd-scale squares and among the smaller ones, finds no way,
//     and GARRISON.APPROACHING keeps its order (UnitAI.js:3272-3289): it stood there with its order for
//     100 s, until Petra moved that javelineer. Now a unit on its way into a structure whose searches
//     find no way from where it stands walks through its own player's units that stand idle with no
//     order, as a builder does (27, 35). It gets in, and the crowd stands as it does without it. The
//     same strip of an ally's units still holds it (0.28's rule): it never walks through their bodies.
//     And the guards a review of a first form of the rule called for: a spearman behind his player's
//     idle block (7 or 12 columns, 3 rows) with 2 rows of an ally's idle units behind it, sent into a
//     civic centre beyond them with a way round both. Sealed, he searched with his own units out of the
//     way and found a way round the ally's rows, but walked that path by 0.28's rule: his next step was
//     refused by his own units, he searched again, found no way, was sealed and sent back at the ally's
//     rows, for good (never in after 300 s, where main takes him round in 10 s and 13.5 s). A walker
//     now follows a path its sealed search found as a sealed one: he gets in within 2 s of main's time
//     and never walks through the ally's bodies, and a builder there, which main's seal held the same
//     way, starts.
#define private public
#include "simulation/simulation.h"
#undef private
#include "network/turn_manager.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
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

// A save file of this run's own in $TMPDIR (or /tmp), so runs side by side don't share one; empty
// if none could be made.
static std::string temp_save_path() {
	const char *tmpdir = std::getenv("TMPDIR");
	std::string tmpl = std::string(tmpdir && *tmpdir ? tmpdir : "/tmp") + "/rts0_test_unit_pushing_XXXXXX";
	std::vector<char> name(tmpl.begin(), tmpl.end());
	name.push_back('\0');
	const int fd = mkstemp(name.data());
	if (fd < 0) return std::string();
	close(fd);
	return std::string(name.data());
}

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

// Test 22: a flat 768 m map with the scene in player 0's own territory (a town centre of player 1
// far off keeps the match going), Athenian women as in Stats' game.
static void freeze_setup(Simulation &s) {
	s.set_sim_seed(5);
	s.reset();
	s.set_map_half(96.0f);
	s.set_team_civ(0, CIV_ATHENIANS);
	s.set_team_civ(1, CIV_SPARTANS);
	s.spawn_building_world(-70.0f, -70.0f, BLD_TOWN_CENTER, 1, 1.0f);
}

struct FreezeRun {
	int moved = -1;            // the tick the walker first got 0.05 from where it started
	int ended = -1;            // the tick its walk ended
	float end_dist = 0.0f;     // how far from its point it ended (or stood at the last tick)
	int built = -1;            // the tick the house it builds was finished
	float body = 1e9f;         // its least distance to a unit it started clear of, over their bodies' radius sum
	float others_moved = 0.0f; // how far any other unit moved
};

// Runs `ticks` ticks at most, until the walker `w` ends its walk to (gx, gz) or, with `house`, until
// it has built that house.
static FreezeRun freeze_run(Simulation &s, uint32_t w, float gx, float gz, uint32_t house, int ticks) {
	FreezeRun r;
	const int w0 = idx(s, w);
	const float x0 = s.units_.pos_x[w0], z0 = s.units_.pos_z[w0];
	const size_t n = s.units_.count;
	std::vector<float> ox(n), oz(n);
	std::vector<uint8_t> clear(n, 0);
	for (size_t k = 0; k < n; ++k) {
		ox[k] = s.units_.pos_x[k];
		oz[k] = s.units_.pos_z[k];
		clear[k] = (int)k != w0 && std::hypot(ox[k] - x0, oz[k] - z0) >=
		           unit_collision_radius(s.units_.unit_type[w0]) + unit_collision_radius(s.units_.unit_type[k]);
	}
	for (int t = 1; t <= ticks; ++t) {
		s.tick(DT);
		const int wi = idx(s, w);
		const float x = s.units_.pos_x[wi], z = s.units_.pos_z[wi];
		for (size_t k = 0; k < n && k < s.units_.count; ++k) {
			if ((int)k == wi) continue;
			r.others_moved = std::max(r.others_moved, std::hypot(s.units_.pos_x[k] - ox[k], s.units_.pos_z[k] - oz[k]));
			if (clear[k])
				r.body = std::min(r.body, std::hypot(s.units_.pos_x[k] - x, s.units_.pos_z[k] - z) /
				                              (unit_collision_radius(s.units_.unit_type[wi]) + unit_collision_radius(s.units_.unit_type[k])));
		}
		if (r.moved < 0 && std::hypot(x - x0, z - z0) > 0.05f) r.moved = t;
		r.end_dist = std::hypot(x - gx, z - gz);
		if (house) {
			if (s.buildings_[s.building_id_to_idx_.at(house)].build_progress >= 1.0f) {
				r.built = t;
				break;
			}
		} else if (!s.units_.has_order[wi]) {
			r.ended = t;
			break;
		}
	}
	return r;
}

// Tests 27-30 and 35: a scene of Stats' dumps on test 22's flat map, player 0 of civ `civ`, each
// structure and unit under its id in the game. With `ally`, player 2 (of the same civ, with a civic
// centre far off) is player 0's ally, and a unit's `team` says whose it is.
struct DumpBld {
	uint32_t id;
	float x, z;
	uint8_t type;
	float progress;
};
struct DumpUnit {
	uint32_t id;
	float x, z;
	UnitType type;
	uint8_t team = 0;
};
static std::unique_ptr<Simulation> dump_scene(uint8_t civ, const std::vector<DumpBld> &blds, const std::vector<DumpUnit> &units,
		bool ally = false) {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(5);
	s->reset();
	s->set_map_half(96.0f);
	s->set_team_civ(0, civ);
	s->set_team_civ(1, CIV_SPARTANS);
	if (ally) {
		s->set_team_civ(2, civ);
		s->set_mutual_ally(0, 2);
	}
	s->next_id_ = 5000;
	s->spawn_building_world(-70.0f, -70.0f, BLD_TOWN_CENTER, 1, 1.0f);
	if (ally) {
		s->next_id_ = 5100;
		s->spawn_building_world(-70.0f, 70.0f, BLD_TOWN_CENTER, 2, 1.0f);
	}
	for (const DumpBld &b : blds) {
		s->next_id_ = b.id;
		s->spawn_building_world(b.x, b.z, b.type, 0, b.progress);
	}
	s->rebuild_nav_grid();
	for (const DumpUnit &u : units) {
		s->next_id_ = u.id;
		s->spawn_unit(u.x, u.z, u.team, u.type);
	}
	s->next_id_ = 6000;
	return s;
}

// Tests 36 and 37: the path a unit holds, to tell the tick its third failed turn laid a new one.
static bool same_path(const WaypointPath &a, const WaypointPath &b) {
	if (a.count != b.count || a.has_goal != b.has_goal) return false;
	for (int k = 0; k < a.count; ++k)
		if (a.xs[k] != b.xs[k] || a.zs[k] != b.zs[k]) return false;
	return true;
}

// Whether unit i is on its third failed turn (ALTERNATE_PATH_TYPE_DELAY) with the centre of what it walks
// to (cx, cz) beyond 56 m, where obstructed_short_path's far-goal and sub-goal searches run.
static bool far_alternate_turn(const Simulation &s, int i, float cx, float cz) {
	return s.units_.motion[i].failed == 3 && std::hypot(cx - s.units_.pos_x[i], cz - s.units_.pos_z[i]) + 0.25f > 14.0f;
}

// How far (x, z) lies from a returner's range round dropsite b (task_range_goal: the footprint grown by
// (1.5 + r) * 2/3, r the unit's radius).
static float drop_range_gap(const Simulation::Building &b, uint8_t unit_type, float x, float z) {
	const float ux = b.uz == 0.0f ? 1.0f : b.ux, uz = b.uz;
	const float grow = (1.5f + unit_collision_radius(unit_type)) * (2.0f / 3.0f);
	const float rx = x - b.x, rz = z - b.z;
	const float a = std::fabs(rx * ux + rz * uz) - (b.hu + grow), c = std::fabs(-rx * uz + rz * ux) - (b.hv + grow);
	return std::hypot(std::max(a, 0.0f), std::max(c, 0.0f));
}

// Test 37's RANGE and STEP: a spearman at (-20, -20) walking to the point 60 units east, a wall of palms
// 1.5 units east of him (z -h..h about him), and the path he holds to his point set by hand (`held`,
// relative to him, its first waypoint next). His third failed turn's search (obstructed_short_path) is
// called alone, and the path he holds after it is returned, relative to him.
static WaypointPath third_turn_path(int h, const std::vector<nav::Vec2> &held) {
	constexpr float OX = -20.0f, OZ = -20.0f;
	auto s = std::make_unique<Simulation>();
	setup(*s);
	for (int k = -h; k <= h; ++k)
		s->spawn_resource_world(1.5f + OX, (float)k + OZ, RES_WOOD, 300, RES_SUB_NONE, 0, FLORA_TREE_CRETAN_DATE_PALM_PATCH);
	const uint32_t w = s->spawn_unit(OX, OZ, 0, UNIT_SPEARMAN, 150.0f);
	s->spawn_unit(OX + 30.0f, OZ + 30.0f, 0, UNIT_FEMALE_CITIZEN, 150.0f);
	s->tick(DT);
	s->order_walk(&w, 1, OX + 60.0f, OZ, false, true);
	const int i = idx(*s, w);
	s->units_.pos_x[i] = OX;
	s->units_.pos_z[i] = OZ;
	WaypointPath &wp = s->unit_paths_[i];
	wp.clear();
	for (const nav::Vec2 &p : held) wp.push_back(p.x + OX, p.y + OZ);
	wp.has_goal = true;
	wp.goal_x = s->units_.target_x[i];
	wp.goal_z = s->units_.target_z[i];
	wp.cur = 0;
	s->units_.waypoint_cur[i] = 0;
	s->units_.waypoint_count[i] = (int16_t)wp.count;
	s->units_.motion[i].failed = 3;
	s->obstructed_short_path((size_t)i, 3);
	WaypointPath out = s->unit_paths_[i];
	for (int k = 0; k < out.count; ++k) {
		out.xs[k] -= OX;
		out.zs[k] -= OZ;
	}
	return out;
}

// Whether `path` ends with held[from..] and the waypoint before that lies on the circle of radius 1 unit
// (the third turn's sub-goal circle) round held[from], within 0.01.
static bool ends_on_circle_with_rest(const WaypointPath &path, const std::vector<nav::Vec2> &held, int from) {
	const int rest = (int)held.size() - from;
	if (path.count <= rest) return false;
	for (int k = 0; k < rest; ++k)
		if (path.xs[path.count - rest + k] != held[from + k].x || path.zs[path.count - rest + k] != held[from + k].y) return false;
	const int e = path.count - rest - 1;
	return std::fabs(std::hypot(path.xs[e] - held[from].x, path.zs[e] - held[from].y) - 1.0f) <= 0.01f;
}

// The tick on which construction of `site` began (its progress above 0) within `ticks`, else -1.
static int build_started(Simulation &s, uint32_t site, int ticks) {
	for (int t = 1; t <= ticks; ++t) {
		s.tick(DT);
		if (s.buildings_[s.building_id_to_idx_.at(site)].build_progress > 0.0f) return t;
	}
	return -1;
}

// Tests 32-33: a shore (deep water south of z 16 on a map of half 64, sloping so a dock can stand half in
// the water), player 0's civic centre inland and a dock site of player 0 laid on the shore near
// x = -20 - 3 * pick, as far inland as it may stand. Returns the dock's id, 0 when it has no place.
static uint32_t shore_dock(Simulation &s, int pick) {
	constexpr int RES = 65;
	constexpr float HALF = 64.0f;
	s.set_sim_seed(7);
	s.reset();
	s.set_map_half(HALF);
	std::vector<float> h((size_t)RES * RES);
	for (int j = 0; j < RES; ++j)
		for (int i = 0; i < RES; ++i)
			h[(size_t)j * RES + i] = std::clamp(std::fabs(-HALF + 2.0f * HALF * j / (RES - 1)) - 16.5f, -3.0f, 3.0f);
	s.set_terrain_data(h, RES, 0.0f);
	s.begin_bulk_spawn();
	s.end_bulk_spawn();
	for (uint8_t p = 0; p < 4; ++p) {
		s.set_player_active(p, true, false);
		s.set_player_resources(p, 0, 0, 0, 0);
	}
	s.set_team_civ(0, CIV_CARTHAGINIANS);
	s.set_team_civ(1, CIV_ATHENIANS);
	s.spawn_building_world(-20.0f, 40.0f, BLD_TOWN_CENTER, 0);
	for (int t = 0; t < 10; ++t) s.tick(Simulation::FIXED_STEP);
	const int foot = building_type_info(BLD_DOCK).footprint;
	for (int z = 63; z >= -64; --z)
		for (int x = -64; x < 64; ++x) {
			const float wx = (x + 0.5f * foot) * Simulation::GRID, wz = (z + 0.5f * foot) * Simulation::GRID;
			if (wz > 10.0f && wz < 24.0f && std::fabs(wx + 20.0f + 3.0f * pick) < 6.0f && s.check_placement(0, x, z, BLD_DOCK) == Simulation::PLACE_OK)
				return s.spawn_building(x, z, BLD_DOCK, 0, 0.0f);
		}
	return 0;
}

// The first point of a dock site's land part (0.3 in from its edges) whose nearest point off it is at sea.
static bool wet_exit_spot(Simulation &s, const Simulation::Building &b, float &wx, float &wz) {
	for (float lu = -b.hu + 0.3f; lu <= b.hu - 0.3f; lu += 0.25f)
		for (float lv = -b.hv + 0.3f; lv <= b.hv - 0.3f; lv += 0.25f) {
			const nav::Vec2 w = nav::footprint_world(b, lu, lv);
			const auto [gx, gy] = s.nav_grid_.world_to_cell(w.x, w.y);
			if (!s.nav_grid_.is_passable_for(gx, gy, PASS_INFANTRY)) continue;
			float tx, tz;
			s.foundation_exit_point(b, w.x, w.y, unit_collision_radius(UNIT_FEMALE_CITIZEN), tx, tz);
			const auto [ex, ey] = s.nav_grid_.world_to_cell(tx, tz);
			if (s.nav_grid_.is_passable_for(ex, ey, PASS_INFANTRY)) continue;
			wx = w.x;
			wz = w.y;
			return true;
		}
	return false;
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
			if (row.flags & ROSTER_FLYING) continue; // a plane has no obstruction: pushing never weighs it
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
		// Both face their way already (spawned units face +z), so neither turns first.
		for (int i : {ir, is}) {
			s->units_.heading_x[i] = 1.0f;
			s->units_.heading_z[i] = 0.0f;
		}
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
		// Turn back: past InstantTurnAngle, so both stop, turn on the spot at their TurnRate until
		// within it, and start again from rest.
		const float rx = s->units_.pos_x[ir], sx = s->units_.pos_x[is];
		const UnitMotionProfile &mr = unit_motion(s->units_.tmpl[ir], UNIT_SIEGE_RAM);
		const UnitMotionProfile &ms = unit_motion(s->units_.tmpl[is], UNIT_SPEARMAN);
		const float ram_turn = (3.14159265f - mr.instant_turn_angle) / mr.turn_rate;
		const float spear_turn = (3.14159265f - ms.instant_turn_angle) / ms.turn_rate;
		s->cmd_move(&ram, 1, -45.0f, 10.0f);
		s->cmd_move(&spear, 1, -20.0f, -10.0f);
		int ram_still = 0, spear_still = 0; // ticks before each moves
		for (int t = 0; t < 20; ++t) {
			s->tick(DT);
			if (s->units_.pos_x[ir] == rx) ram_still = t + 1;
			if (s->units_.pos_x[is] == sx) spear_still = t + 1;
		}
		const float ram_back = rx - s->units_.pos_x[ir], spear_back = sx - s->units_.pos_x[is];
		std::printf("  turned back: the ram stood %d ticks (%.3f s to turn) and went %.4f in 1 s (%.4f expected),"
		            " the spearman %d (%.3f s) and %.4f (%.4f); headings (%.3f, %.3f) and (%.3f, %.3f)\n",
		            ram_still, ram_turn, ram_back, from_rest(a_ram, v_ram, 1.0f - ram_turn), spear_still, spear_turn,
		            spear_back, from_rest(a_spear, v_spear, 1.0f - spear_turn), s->units_.heading_x[ir],
		            s->units_.heading_z[ir], s->units_.heading_x[is], s->units_.heading_z[is]);
		CHECK(std::fabs(ram_turn - 0.598f) < 0.01f && std::fabs(spear_turn - 0.117f) < 0.01f,
		      "turning back takes a ram 0.6 s and a spearman 0.12 s (TurnRate 4 and 14, InstantTurnAngle 0.75 and 1.5)");
		CHECK(ram_still == (int)(ram_turn / DT) && spear_still == (int)(spear_turn / DT),
		      "a unit stands while it turns past its InstantTurnAngle");
		CHECK(std::fabs(ram_back - from_rest(a_ram, v_ram, 1.0f - ram_turn)) < 0.01f &&
		      std::fabs(spear_back - from_rest(a_spear, v_spear, 1.0f - spear_turn)) < 0.02f,
		      "then starts again from rest");
		CHECK(std::fabs(s->units_.heading_x[ir] + 1.0f) < 1e-5f && std::fabs(s->units_.heading_z[ir]) < 1e-5f &&
		      std::fabs(s->units_.heading_x[is] + 1.0f) < 1e-5f, "and faces its way");
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
		// UnitAI FaceTowardsTarget: an attacker in reach faces its target. Garrisonable.UnLoad: a unit
		// put out of a structure faces away from it.
		auto s = std::make_unique<Simulation>();
		setup(*s);
		uint32_t sword = s->spawn_unit(0.0f, 0.0f, 0, UNIT_SWORDSMAN, 0.0f);
		uint32_t foe = s->spawn_unit(-0.6f, -0.8f, 1, UNIT_SWORDSMAN, 0.0f);
		const int iw = idx(*s, sword);
		s->order_attack(&sword, 1, foe);
		for (int t = 0; t < 4; ++t) s->tick(DT);
		const int jf = idx(*s, foe);
		const float fx = s->units_.pos_x[jf] - s->units_.pos_x[iw], fz = s->units_.pos_z[jf] - s->units_.pos_z[iw];
		const float fl = std::hypot(fx, fz);
		const float face = (s->units_.heading_x[iw] * fx + s->units_.heading_z[iw] * fz) / fl;
		std::printf("  an attacker in reach heads (%.3f, %.3f), its target lies (%.3f, %.3f): cos %.4f\n",
		            s->units_.heading_x[iw], s->units_.heading_z[iw], fx / fl, fz / fl, face);
		CHECK(face > 0.999f, "an attacker in reach faces its target");
		const uint32_t bar = s->spawn_building_world(20.0f, -20.0f, BLD_BARRACKS, 0, 1.0f);
		uint32_t pike = s->spawn_unit(30.0f, -20.0f, 0, UNIT_SPEARMAN, 0.0f);
		s->order_garrison(&pike, 1, bar);
		bool inside = false;
		for (int t = 0; t < 400 && !inside; ++t) {
			s->tick(DT);
			inside = !s->units_.alive[idx(*s, pike)];
		}
		s->cmd_unload(0, bar, &pike, 1);
		const int ip = idx(*s, pike);
		const float ox = s->units_.pos_x[ip] - 20.0f, oz = s->units_.pos_z[ip] + 20.0f, ol = std::hypot(ox, oz);
		const float away = (s->units_.heading_x[ip] * ox + s->units_.heading_z[ip] * oz) / ol;
		std::printf("  unloaded at (%.2f, %.2f), heading (%.3f, %.3f): cos %.4f from the barracks' centre\n",
		            s->units_.pos_x[ip], s->units_.pos_z[ip], s->units_.heading_x[ip], s->units_.heading_z[ip], away);
		CHECK(inside && s->units_.alive[ip] && away > 0.999f, "a unit unloaded from a structure faces away from it");
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
	{
		auto s = std::make_unique<Simulation>();
		setup(*s);
		s->spawn_building_world(20.0f, 0.0f, BLD_BARRACKS, 0, 1.0f);
		uint32_t w = s->spawn_unit(0.0f, 0.5f, 0, UNIT_SPEARMAN, 0.0f);
		s->cmd_move(&w, 1, 40.0f, 0.0f);
		s->tick(DT);
		const int wi = idx(*s, w);
		const WaypointPath &wp = s->unit_paths_[wi];
		auto put = [&](float x, float z) { s->units_.pos_x[wi] = x; s->units_.pos_z[wi] = z; };
		const int count = wp.count;
		const float x0 = wp.xs[0], z0 = wp.zs[0];
		std::printf("  long path round a barracks: %d waypoints, the first (%.2f, %.2f)\n", count, x0, z0);
		CHECK(count >= 2 && wp.cur == 0, "a walk round a barracks has a long path of two waypoints or more");
		// Obstructed 1.5 from its next waypoint (within 8 m): it heads for the one after.
		put(x0 - 1.5f, z0);
		s->obstructed_long_path(wi, 1);
		CHECK(wp.cur == 1 && s->units_.waypoint_cur[wi] == 1, "an obstructed walker drops a waypoint within 8 m");
		// Obstructed off its path, below the barracks: nothing on turns one and two, a new path from
		// where it stands on the third.
		put(0.0f, 0.5f);
		s->cmd_move(&w, 1, 40.0f, 0.0f);
		s->tick(DT);
		const int first_count = wp.count;
		put(2.0f, -3.0f);
		s->obstructed_long_path(wi, 1);
		s->obstructed_long_path(wi, 2);
		const bool kept = wp.cur == 0 && wp.count == first_count && wp.xs[0] == x0 && wp.zs[0] == z0;
		s->obstructed_long_path(wi, 3);
		std::printf("  re-planned from (2, -3): %d waypoints, the first (%.2f, %.2f), the last (%.2f, %.2f)\n",
		            wp.count, wp.xs[0], wp.zs[0], wp.xs[wp.count - 1], wp.zs[wp.count - 1]);
		CHECK(kept, "a walker off its path keeps it for two failed turns");
		CHECK(wp.cur == 0 && wp.zs[0] < 0.0f && s->units_.waypoint_cur[wi] == 0 && s->units_.waypoint_count[wi] == wp.count &&
		      std::hypot(wp.xs[wp.count - 1] - 40.0f, wp.zs[wp.count - 1]) < 0.5f,
		      "and plans its path again from where it stands on the third");
		// Within 16 m of its goal it paths straight there with short searches instead.
		put(0.0f, 0.5f);
		s->cmd_move(&w, 1, 40.0f, 0.0f);
		s->tick(DT);
		put(37.0f, 2.0f);
		const int near_cur = wp.cur, near_count = wp.count;
		s->obstructed_long_path(wi, 3);
		CHECK(wp.cur == near_cur && wp.count == near_count, "within 16 m of its goal it does neither");
		// The pressure floor: a walker slowed below 1.5 m/s by a modifier still walks at the floor.
		s->units_.speed[wi] = 0.3f;
		s->units_.motion[wi].pressure = 200;
		std::printf("  a walker at 0.30 under pressure 200 moves at %.3f (floor %.3f)\n", s->unit_move_speed(wi), 1.5f * FORMATION_METRE);
		CHECK(std::fabs(s->unit_move_speed(wi) - 1.5f * FORMATION_METRE) < 1e-4f, "the pressure floor is the template speed's, not the modified one's");
	}

	// Animals (tests 14-18), north of the ones reset() puts on the map. An animal spawned with a
	// long feeding time stands (LINGERING) for the whole test.
	auto stand = [](Simulation &s, float x, float z, uint8_t type, uint8_t owner = GAIA_PLAYER) {
		const uint32_t id = s.spawn_fauna(x, z, type, owner);
		s.fauna_.state_timer[s.fauna_id_to_idx_.at(id)] = 1e6f;
		return id;
	};
	auto fx = [](const Simulation &s, uint32_t id) { return s.fauna_.pos_x[s.fauna_id_to_idx_.at(id)]; };
	auto fz = [](const Simulation &s, uint32_t id) { return s.fauna_.pos_z[s.fauna_id_to_idx_.at(id)]; };
	auto fdist = [&](const Simulation &s, uint32_t a, uint32_t b) { return std::hypot(fx(s, a) - fx(s, b), fz(s, a) - fz(s, b)); };
	{
		auto s = std::make_unique<Simulation>();
		setup(*s);
		const uint32_t deer = stand(*s, 0.0f, 40.0f, FAUNA_DEER);
		const uint32_t w = s->spawn_unit(-10.0f, 40.2f, 0, UNIT_SWORDSMAN, 150.0f);
		s->order_walk(&w, 1, 10.0f, 40.2f);
		int arrived = -1;
		float closest = 1e9f, aside = 0.0f, deer_moved = 0.0f;
		for (int t = 1; t <= 400 && arrived < 0; ++t) {
			s->tick(DT);
			const int wi = idx(*s, w);
			closest = std::min(closest, std::hypot(s->units_.pos_x[wi] - fx(*s, deer), s->units_.pos_z[wi] - fz(*s, deer)));
			aside = std::max(aside, std::fabs(s->units_.pos_z[wi] - 40.2f));
			deer_moved = std::max(deer_moved, std::hypot(fx(*s, deer), fz(*s, deer) - 40.0f));
			if (!s->units_.has_order[wi]) arrived = t;
		}
		std::printf("  walker into a standing deer: done after %d ticks, closest %.2f, %.2f aside at most, deer moved %.3f\n",
		            arrived, closest, aside, deer_moved);
		CHECK(arrived > 0, "a walker gets past a standing deer");
		// Its line passes 0.2 from the deer's centre: nudged round the deer, it passes further off.
		CHECK(aside > 0.3f && closest > 0.3f, "nudged round it");
		CHECK(deer_moved < 0.01f, "and the standing deer is not pushed by a walker");
	}
	{
		auto s = std::make_unique<Simulation>();
		setup(*s);
		uint32_t a = s->spawn_fauna(-8.0f, 40.0f, FAUNA_SHEEP_TRAINABLE, 0);
		uint32_t b = s->spawn_fauna(8.0f, 40.1f, FAUNA_SHEEP_TRAINABLE, 0);
		s->order_fauna_walk(0, &a, 1, 8.0f, 40.0f);
		s->order_fauna_walk(0, &b, 1, -8.0f, 40.1f);
		float closest = 1e9f, apart = 0.0f;
		int done = -1;
		for (int t = 0; t < 800 && done < 0; ++t) {
			s->tick(DT);
			closest = std::min(closest, fdist(*s, a, b));
			apart = std::max(apart, std::fabs(fz(*s, a) - fz(*s, b)));
			const size_t ia = s->fauna_id_to_idx_.at(a), ib = s->fauna_id_to_idx_.at(b);
			if (s->fauna_.behavior[ia] != FAUNA_MOVING && s->fauna_.behavior[ib] != FAUNA_MOVING) done = t;
		}
		const float miss_a = std::hypot(fx(*s, a) - 8.0f, fz(*s, a) - 40.0f), miss_b = std::hypot(fx(*s, b) + 8.0f, fz(*s, b) - 40.1f);
		std::printf("  two sheep head-on: %.2f apart sideways at most, %.2f at the closest, both stopped after %d ticks, %.2f and %.2f from their points\n",
		            apart, closest, done, miss_a, miss_b);
		CHECK(apart > 0.3f && closest > 0.5f, "walking animals pushing each other part sideways");
		CHECK(done > 0 && miss_a < 1.0f && miss_b < 1.0f, "and both get where they were sent");

		std::vector<uint32_t> heap;
		for (int k = 0; k < 6; ++k) heap.push_back(stand(*s, 0.0f, 50.0f, FAUNA_DEER));
		const uint32_t boar = stand(*s, 10.0f, 50.0f, FAUNA_BOAR);
		const uint32_t pike = s->spawn_unit(10.0f, 50.05f, 0, UNIT_SPEARMAN, 150.0f);
		for (int t = 0; t < 200; ++t) s->tick(DT);
		float min_pair = 1e9f;
		for (size_t i = 0; i < heap.size(); ++i)
			for (size_t j = i + 1; j < heap.size(); ++j) min_pair = std::min(min_pair, fdist(*s, heap[i], heap[j]));
		const int pi = idx(*s, pike);
		const float boar_gap = std::hypot(s->units_.pos_x[pi] - fx(*s, boar), s->units_.pos_z[pi] - fz(*s, boar));
		std::printf("  6 deer put on one spot: %.2f apart at least after 10 s; a spearman put on a boar: %.2f apart\n", min_pair, boar_gap);
		CHECK(min_pair >= 1.0f, "standing animals put on one spot spread out");
		CHECK(boar_gap >= 0.5f + unit_collision_radius(UNIT_SPEARMAN), "and a unit standing on an animal parts from it");
	}
	{
		auto s = std::make_unique<Simulation>();
		setup(*s);
		const uint32_t ele = stand(*s, 0.0f, 40.0f, FAUNA_ELEPHANT_AFRICAN_BUSH);
		const uint32_t man = s->spawn_unit(0.3f, 40.0f, 0, UNIT_SPEARMAN, 150.0f);
		for (int t = 0; t < 100; ++t) s->tick(DT);
		const int m = idx(*s, man);
		const float ele_moved = std::hypot(fx(*s, ele), fz(*s, ele) - 40.0f);
		const float man_moved = std::hypot(s->units_.pos_x[m] - 0.3f, s->units_.pos_z[m] - 40.0f);
		const float gap = std::hypot(s->units_.pos_x[m] - fx(*s, ele), s->units_.pos_z[m] - fz(*s, ele));
		std::printf("  elephant and spearman on one spot: elephant moved %.2f, spearman %.2f, %.2f apart\n", ele_moved, man_moved, gap);
		CHECK(man_moved > 3.0f * ele_moved, "an elephant (Weight 50) pushes a spearman harder than he pushes it");
		CHECK(gap >= 1.4f + unit_collision_radius(UNIT_SPEARMAN), "and they part by their clearances");

		// The bull stops within its reach of the spearman, who neither moves nor strikes back.
		const uint32_t guard = s->spawn_unit(-20.0f, 40.0f, 0, UNIT_SPEARMAN, 150.0f);
		const int gi = idx(*s, guard);
		s->units_.stance[gi] = STANCE_STAND_GROUND;
		s->units_.attack_damage[gi] = 0.0f;
		const uint32_t bull = s->spawn_fauna(-12.0f, 40.0f, FAUNA_CATTLE_BULL);
		s->tick(DT);
		s->fauna_attacked(s->fauna_id_to_idx_.at(bull), guard);
		int bitten = -1;
		for (int t = 1; t <= 400 && bitten < 0; ++t) {
			s->tick(DT);
			if (s->units_.hp[idx(*s, guard)] < 150.0f) bitten = t;
		}
		std::printf("  a bull charging a spearman who stands still bites him after %d ticks\n", bitten);
		CHECK(bitten > 0, "a bull charging a soldier who stands still bites him");
	}
	{
		auto s = std::make_unique<Simulation>();
		setup(*s);
		// A chicken, a whale and a sheep penned in its corral, each with a spearman on top: none of
		// them is pushed, nor pushes him.
		const uint32_t hen = stand(*s, -10.0f, 40.0f, FAUNA_CHICKEN);
		const uint32_t whale = stand(*s, 0.0f, 40.0f, FAUNA_WHALE_HUMPBACK);
		std::vector<uint32_t> men = {s->spawn_unit(-10.1f, 40.0f, 0, UNIT_SPEARMAN, 150.0f), s->spawn_unit(0.1f, 40.0f, 0, UNIT_SPEARMAN, 150.0f)};
		const uint32_t corral = s->spawn_building_world(14.0f, 40.0f, BLD_CORRAL, 0, 1.0f);
		const uint32_t penned = stand(*s, 14.0f, 40.0f, FAUNA_SHEEP_TRAINABLE, 0);
		{
			const size_t f = s->fauna_id_to_idx_.at(penned);
			s->fauna_.behavior[f] = FAUNA_CORRALLED;
			s->fauna_.corral_id[f] = corral;
		}
		// A barracks with a deer against its south side and a spearman standing on the deer's
		// outer side: the push would put the deer into the barracks' clearance.
		const uint32_t bar = s->spawn_building_world(0.0f, 60.0f, BLD_BARRACKS, 0, 1.0f);
		const Simulation::Building &bb = s->buildings_[s->building_id_to_idx_.at(bar)];
		const float edge = bb.z - bb.hv;
		const uint32_t deer = stand(*s, 0.0f, edge - CLEARANCE_DEFAULT - 0.05f, FAUNA_DEER);
		men.push_back(s->spawn_unit(0.0f, edge - CLEARANCE_DEFAULT - 0.1f, 0, UNIT_SPEARMAN, 150.0f));
		std::vector<std::pair<float, float>> at;
		for (uint32_t id : men) at.emplace_back(s->units_.pos_x[idx(*s, id)], s->units_.pos_z[idx(*s, id)]);
		float deepest = 1e9f;
		for (int t = 0; t < 100; ++t) {
			s->tick(DT);
			deepest = std::min(deepest, edge - fz(*s, deer));
		}
		auto moved = [&](size_t k) {
			const int i = idx(*s, men[k]);
			return std::hypot(s->units_.pos_x[i] - at[k].first, s->units_.pos_z[i] - at[k].second);
		};
		const float hen_moved = std::hypot(fx(*s, hen) + 10.0f, fz(*s, hen) - 40.0f), whale_moved = std::hypot(fx(*s, whale), fz(*s, whale) - 40.0f);
		std::printf("  chicken moved %.3f and its spearman %.3f; whale %.3f and its spearman %.3f; deer by a barracks %.3f off it at the closest\n",
		            hen_moved, moved(0), whale_moved, moved(1), deepest);
		CHECK(hen_moved < 1e-4f && moved(0) < 1e-4f, "a chicken (BlockMovement false) neither pushes nor is pushed");
		CHECK(whale_moved < 1e-4f && moved(1) < 1e-4f, "nor does a whale");
		CHECK(s->fauna_.behavior[s->fauna_id_to_idx_.at(penned)] == FAUNA_CORRALLED, "a penned animal stays in its corral");
		CHECK(moved(2) > 0.3f && deepest >= CLEARANCE_DEFAULT - 1e-3f, "a pushed animal stays off a structure, as its own steps do");
	}
	{
		// A flock of a player's sheep sent to one point crowds there, building up pushing pressure.
		auto s = std::make_unique<Simulation>();
		setup(*s);
		std::vector<uint32_t> flock;
		for (int k = 0; k < 12; ++k) flock.push_back(s->spawn_fauna(-10.0f + 1.2f * (k % 4), 36.0f + 1.2f * (k / 4), FAUNA_SHEEP_TRAINABLE, 0));
		for (uint32_t id : flock) s->order_fauna_walk(0, &id, 1, 6.0f, 40.0f);
		for (int t = 0; t < 120; ++t) s->tick(DT);
		const std::string path = temp_save_path();
		CHECK(s->save_state(path.c_str()), "a crowded flock saves");
		auto t2 = std::make_unique<Simulation>();
		CHECK(t2->load_state(path.c_str()), "and loads");
		std::remove(path.c_str());
		const uint32_t h0 = TurnManager().boot_sim_hash(*s), h1 = TurnManager().boot_sim_hash(*t2);
		for (int t = 0; t < 200; ++t) {
			s->tick(DT);
			t2->tick(DT);
		}
		const uint32_t h2 = TurnManager().boot_sim_hash(*s), h3 = TurnManager().boot_sim_hash(*t2);
		// Once every sheep has ended its walk (at its point, or obstructed near it by the others),
		// the standing flock has spread out.
		int walking = 1, t_settled = 200;
		for (; walking && t_settled < 1200; ++t_settled) {
			s->tick(DT);
			walking = 0;
			for (uint32_t id : flock) walking += s->fauna_.behavior[s->fauna_id_to_idx_.at(id)] == FAUNA_MOVING;
		}
		for (int t = 0; t < 100; ++t) s->tick(DT);
		float min_pair = 1e9f;
		for (size_t i = 0; i < flock.size(); ++i)
			for (size_t j = i + 1; j < flock.size(); ++j) min_pair = std::min(min_pair, fdist(*s, flock[i], flock[j]));
		std::printf("  flock of 12 sent to one point: hashes %08x %08x on load, %08x %08x 10 s on; all stopped %.1f s after the load, %.2f apart at least 5 s later\n",
		            h0, h1, h2, h3, t_settled * DT, min_pair);
		CHECK(h0 == h1 && h2 == h3, "a saved flock goes on as before after a load");
		CHECK(!walking && min_pair >= 0.9f, "a flock sent to one point stops, and doesn't stand on itself");
	}
	for (const UnitType body : {UNIT_SIEGE_RAM, UNIT_ELEPHANT}) {
		// A player's sheep sent to the point a ram or a war elephant stands on.
		auto s = std::make_unique<Simulation>();
		setup(*s);
		const uint32_t big = s->spawn_unit(10.0f, 40.0f, 0, body, 1000.0f);
		uint32_t sheep = s->spawn_fauna(0.0f, 40.0f, FAUNA_SHEEP_TRAINABLE, 0);
		s->order_fauna_walk(0, &sheep, 1, 10.0f, 40.0f);
		// Saved and loaded on each of the 8 ticks from when it first gets no nearer its point over 4
		// ticks, near it (a turn or two before its walk ends), each copy ends its walk on the same tick.
		std::vector<std::unique_ptr<Simulation>> copies;
		std::vector<int> copy_ended;
		int ended = -1, saved = 0, first_save = -1;
		float miss = -1.0f, body_moved = -1.0f; // when the walk ends
		float to_go[4] = {10.0f, 10.0f, 10.0f, 10.0f}; // how far it was from its point, 4 ticks back
		const float turn_step = fauna_type_info(FAUNA_SHEEP_TRAINABLE).speed * 4.0f * DT;
		auto copies_walking = [&]() {
			for (int e : copy_ended)
				if (e < 0) return true;
			return false;
		};
		for (int t = 1; t <= 1200 && (ended < 0 || copies_walking()); ++t) {
			s->tick(DT);
			for (size_t k = 0; k < copies.size(); ++k) {
				copies[k]->tick(DT);
				if (copy_ended[k] < 0 && copies[k]->fauna_.behavior[copies[k]->fauna_id_to_idx_.at(sheep)] != FAUNA_MOVING) copy_ended[k] = t;
			}
			const size_t f = s->fauna_id_to_idx_.at(sheep);
			if (ended < 0 && s->fauna_.behavior[f] != FAUNA_MOVING) {
				ended = t;
				const int bi = idx(*s, big);
				miss = std::hypot(fx(*s, sheep) - 10.0f, fz(*s, sheep) - 40.0f);
				body_moved = std::hypot(s->units_.pos_x[bi] - 10.0f, s->units_.pos_z[bi] - 40.0f);
			}
			const float d = std::hypot(fx(*s, sheep) - 10.0f, fz(*s, sheep) - 40.0f), gained = to_go[t % 4] - d;
			to_go[t % 4] = d;
			if (first_save < 0 && ended < 0 && t > 4 && gained < 0.15f * turn_step && d < 3.0f) first_save = t;
			if (first_save >= 0 && ended < 0 && t < first_save + 8) {
				const std::string path = temp_save_path();
				auto copy = std::make_unique<Simulation>();
				if (s->save_state(path.c_str()) && copy->load_state(path.c_str())) {
					++saved;
					copies.push_back(std::move(copy));
					copy_ended.push_back(-1);
				}
				std::remove(path.c_str());
			}
		}
		bool same = saved > 0;
		for (int e : copy_ended) same = same && e == ended;
		std::printf("  saved and loaded on %d ticks as it stood against the body: every copy ended its walk on the same tick: %s\n", saved,
		            same ? "yes" : "no");
		CHECK(saved >= 4 && same, "a sheep walking into a standing body goes on as before after a load");
		std::printf("  sheep sent onto a standing %s: walk ended after %.2f s (-1: still walking at 60 s), %.2f from its point; the %s moved %.3f\n",
		            body == UNIT_ELEPHANT ? "war elephant" : "ram", ended < 0 ? -1.0f : ended * DT, miss, body == UNIT_ELEPHANT ? "elephant" : "ram", body_moved);
		CHECK(ended > 0 && ended * DT < 15.0f && miss <= 3.0f, "a walk onto a standing body ends near its point (UnitAI WALKING, relaxed range)");
		CHECK(body_moved >= 0.0f && body_moved < 0.01f, "and the standing body is not pushed by the walker");
	}
	{
		// A villager herds a sheep that stands beside him to his corral on his other side: the sheep
		// heads for the corral past him as he walks to where it stood. Herding is RTS0's own (0.28
		// has no herding order), and 0.28's Push has no exception for an entity and the one it walks
		// to (CCmpUnitMotion_System.cpp:671-687; specificIgnore, CCmpUnitMotion.h:1231-1237, only
		// keeps the target out of the walker's own obstruction tests), so the two push each other as
		// two walkers do. The sheep's move is to the corral, not to him, so once he stands in its way
		// it is nudged round him, and it still gets into the corral.
		auto s = std::make_unique<Simulation>();
		setup(*s);
		s->spawn_building_world(-12.0f, 40.0f, BLD_CORRAL, 0, 1.0f);
		const uint32_t herder = s->spawn_unit(0.0f, 40.0f, 0, UNIT_VILLAGER, 100.0f);
		const uint32_t sheep = s->spawn_fauna(1.5f, 40.0f, FAUNA_SHEEP_TRAINABLE, 0);
		s->order_herd(&herder, 1, sheep);
		const size_t f = s->fauna_id_to_idx_.at(sheep);
		float off_line = 0.0f, closest = 1e9f;
		int herder_pressure = 0, sheep_pressure = 0, penned = -1;
		for (int t = 1; t <= 600 && penned < 0; ++t) {
			s->tick(DT);
			if (s->fauna_.behavior[f] == FAUNA_CORRALLED) {
				penned = t;
				break;
			}
			off_line = std::max(off_line, std::fabs(s->fauna_.pos_z[f] - 40.0f));
			const int hi = idx(*s, herder);
			closest = std::min(closest, std::hypot(s->units_.pos_x[hi] - s->fauna_.pos_x[f], s->units_.pos_z[hi] - s->fauna_.pos_z[f]));
			herder_pressure = std::max<int>(herder_pressure, s->units_.motion[hi].pressure);
			sheep_pressure = std::max<int>(sheep_pressure, s->fauna_pressure_[f]);
		}
		std::printf("  a herded sheep walking past its herder: %.3f off its line at most, %.2f from him at the closest, pressure up to %d (herder) "
		            "and %d (sheep); penned after %.2f s (-1: not in 30 s)\n",
		            off_line, closest, herder_pressure, sheep_pressure, penned < 0 ? -1.0f : penned * DT);
		CHECK(herder_pressure > 0 && sheep_pressure > 0, "a herded animal and its herder push each other as two walkers do");
		CHECK(off_line > 0.3f, "a herded animal heading for its corral is nudged round its herder standing in its way");
		CHECK(penned > 0, "and it still gets into its corral");
	}
	for (const uint8_t beast : {FAUNA_BOAR, FAUNA_CATTLE_BULL}) {
		// An archer stands 1.5 behind a line of five idle spearmen 1.2 apart, and an animal it hit
		// charges it from the far side of the line.
		auto s = std::make_unique<Simulation>();
		setup(*s);
		std::vector<uint32_t> line;
		for (int k = 0; k < 5; ++k) line.push_back(s->spawn_unit(0.0f, 40.0f + 1.2f * (k - 2), 0, UNIT_SPEARMAN, 150.0f));
		const uint32_t archer = s->spawn_unit(-1.5f, 40.0f, 0, UNIT_ARCHER, 150.0f);
		line.push_back(archer);
		for (uint32_t id : line) {
			const int i = idx(*s, id);
			s->units_.stance[i] = STANCE_STAND_GROUND;
			s->units_.attack_damage[i] = 0.0f;
		}
		const uint32_t animal = s->spawn_fauna(10.0f, 40.3f, beast);
		s->tick(DT);
		s->fauna_attacked(s->fauna_id_to_idx_.at(animal), archer);
		int bitten = -1;
		for (int t = 1; t <= 600 && bitten < 0; ++t) {
			s->tick(DT);
			if (s->units_.hp[idx(*s, archer)] < 150.0f) bitten = t;
		}
		std::printf("  %s charging an archer behind a line of spearmen: bites after %.2f s (-1: not in 30 s)\n", fauna_type_info(beast).name,
		            bitten < 0 ? -1.0f : bitten * DT);
		CHECK(bitten > 0, "an animal charging a unit behind a line of idle soldiers bites it");
	}
	for (const bool working : {false, true}) {
		// A square ring of the player's own units 1.3 units out from a storehouse's footprint, 1.4
		// apart: idle spearmen with no order, or women each cutting one of the trees 1.6 further out.
		// The storehouse stands in player 0's territory (it would decay to gaia outside it).
		auto s = std::make_unique<Simulation>();
		setup(*s);
		const float cx = -40.0f, cz = -32.0f;
		const uint32_t store = s->spawn_building_world(cx, cz, BLD_STOREHOUSE, 0, 1.0f);
		const auto &b = s->buildings_[s->building_id_to_idx_.at(store)];
		const float h = b.hu + 1.3f, step = 1.4f;
		const int per_side = (int)std::lround(2.0f * h / step);
		std::vector<uint32_t> ring;
		uint32_t tree = 0;
		for (int side = 0; side < 4; ++side)
			for (int k = 0; k < per_side; ++k) {
				const float t = -h + step * (float)k;
				const float x = side == 0 ? t : side == 1 ? h : side == 2 ? -t : -h;
				const float z = side == 0 ? -h : side == 1 ? t : side == 2 ? h : -t;
				ring.push_back(s->spawn_unit(cx + x, cz + z, 0, working ? UNIT_FEMALE_CITIZEN : UNIT_SPEARMAN, 150.0f));
				s->units_.stance[idx(*s, ring.back())] = STANCE_STAND_GROUND;
				if (!working) continue;
				if (k % 2 == 0) { // a tree for each pair of women
					const float tt = t + 0.5f * step;
					const float ox = side == 0 ? tt : side == 1 ? h + 1.6f : side == 2 ? -tt : -h - 1.6f;
					const float oz = side == 0 ? -h - 1.6f : side == 1 ? tt : side == 2 ? h + 1.6f : -tt;
					tree = s->spawn_resource_world(cx + ox, cz + oz, RES_WOOD, 5000);
				}
				s->order_gather(&ring.back(), 1, tree);
			}
		for (int t = 0; t < 100; ++t) s->tick(DT);
		std::vector<float> rx, rz;
		for (uint32_t id : ring) rx.push_back(s->units_.pos_x[idx(*s, id)]), rz.push_back(s->units_.pos_z[idx(*s, id)]);
		// A spearman cutting a tree east of the storehouse with a full load (tick_gather's return), and
		// one carrying stone sent there with ReturnResource, as Petra sends an idle full worker.
		const uint32_t far_tree = s->spawn_resource_world(cx + 14.0f, cz + 2.0f, RES_WOOD, 5000);
		const uint32_t cutter = s->spawn_unit(cx + 10.0f, cz + 1.0f, 0, UNIT_SPEARMAN, 150.0f);
		s->order_gather(&cutter, 1, far_tree);
		const int ci = idx(*s, cutter);
		const int32_t cap = s->carry_cap((size_t)ci, RES_WOOD);
		s->units_.res_type_carried[ci] = RES_WOOD;
		s->units_.carry[ci] = cap;
		const uint32_t miner = s->spawn_unit(cx - 1.0f, cz - 10.0f, 0, UNIT_SPEARMAN, 150.0f);
		const int mi = idx(*s, miner);
		s->units_.res_type_carried[mi] = RES_STONE;
		s->units_.carry[mi] = 10;
		s->order_return_resource(&miner, 1, store);
		const int stone0 = s->player_resource(0, RES_STONE);
		int cut_drop = -1, mine_drop = -1, back = -1;
		float moved = 0.0f;
		for (int t = 1; t <= 1200; ++t) {
			s->tick(DT);
			const int c = idx(*s, cutter);
			if (cut_drop < 0 && s->units_.carry[c] < cap) cut_drop = t;
			if (mine_drop < 0 && s->units_.carry[idx(*s, miner)] == 0) mine_drop = t;
			if (cut_drop > 0 && back < 0 && s->units_.action_state[c] == ACTION_GATHER &&
			    s->gather_in_reach((size_t)c, s->res_id_to_idx_.at(far_tree), s->units_.pos_x[c], s->units_.pos_z[c]))
				back = t;
			// Until both have dropped: the one that came with ReturnResource stands idle there after.
			if (cut_drop < 0 || mine_drop < 0)
				for (size_t k = 0; k < ring.size(); ++k) {
					const int j = idx(*s, ring[k]);
					moved = std::max(moved, std::hypot(s->units_.pos_x[j] - rx[k], s->units_.pos_z[j] - rz[k]));
				}
		}
		std::printf("  %s round a storehouse: a full cutter drops after %.2f s and is back at its tree after %.2f s, a "
		            "ReturnResource walker drops after %.2f s (-1: not in 60 s); stone +%d; the ring moved %.3f meanwhile\n",
		            working ? "women cutting a wood" : "idle spearmen", cut_drop < 0 ? -1.0f : cut_drop * DT,
		            back < 0 ? -1.0f : back * DT, mine_drop < 0 ? -1.0f : mine_drop * DT,
		            s->player_resource(0, RES_STONE) - stone0, moved);
		CHECK(cut_drop > 0 && cut_drop * DT < 10.0f && mine_drop > 0 && mine_drop * DT < 10.0f,
		      "a worker with a load gets through its own player's idle units or gatherers ringing the dropsite and drops it");
		CHECK(back > 0 && back * DT < 20.0f, "and walks back out to its supply");
		if (!working) CHECK(moved < 0.05f, "without pushing any of the idle units");
	}
	for (int layout = 0; layout < 2; ++layout)
		for (const bool build : {false, true}) {
			// Stats' two layouts, as his traces had them. 0: Continental seed 5, unit 3 beside the
			// town centre heading for (35, 35), unit 4 standing at the centre's corner; builders go
			// to a house south-west of it. 1: unit 879 heading north past unit 884, standing 1.4
			// ahead, in her rally batch (the others as a rally leaves them, 1.3-1.5 apart); builders
			// go to a house beyond the point.
			auto s = std::make_unique<Simulation>();
			freeze_setup(*s);
			uint32_t w = 0;
			float gx = 0.0f, gz = 0.0f, hx = 0.0f, hz = 0.0f;
			if (layout == 0) {
				s->spawn_building_world(37.335f, 37.335f, BLD_TOWN_CENTER, 0, 1.0f);
				w = s->spawn_unit(39.586f, 36.263f, 0, UNIT_FEMALE_CITIZEN);
				s->spawn_unit(40.867f, 37.040f, 0, UNIT_FEMALE_CITIZEN);
				gx = 35.0f, gz = 35.0f, hx = 36.0f, hz = 32.0f;
			} else {
				w = s->spawn_unit(26.606f, 34.418f, 0, UNIT_FEMALE_CITIZEN);
				s->spawn_unit(26.802f, 35.808f, 0, UNIT_FEMALE_CITIZEN);
				const float batch[8][2] = {{25.0604f, 34.9030f}, {25.8745f, 37.1278f}, {28.2226f, 35.4132f}, {28.2191f, 32.9751f},
				                           {28.2835f, 37.0598f}, {26.0163f, 32.9339f}, {29.7323f, 35.3372f}, {29.7440f, 33.9239f}};
				for (const auto &b : batch) s->spawn_unit(b[0], b[1], 0, UNIT_FEMALE_CITIZEN);
				gx = 26.654f, gz = 39.649f, hx = 26.654f, hz = 41.4f;
			}
			uint32_t house = 0;
			if (build) {
				house = s->spawn_building_world(hx, hz, BLD_HOUSE, 0, 0.0f);
				s->order_build_or_repair(&w, 1, house);
			} else {
				s->order_walk(&w, 1, gx, gz);
			}
			const FreezeRun r = freeze_run(*s, w, gx, gz, house, build ? 800 : 300);
			const char *name = layout == 0 ? "beside a town centre (Stats, Continental seed 5)" : "in her rally batch (Stats, unit 879)";
			std::printf("  a woman %s %s: first moved after %.2f s, %s after %.2f s (-1: not in %d s), %.2f from the point; closest to a "
			            "body %.3f of the radius sum; the others moved %.3f\n", name, build ? "sent to build" : "sent to walk",
			            r.moved < 0 ? -1.0f : r.moved * DT, build ? "house built" : "walk ended", (build ? r.built : r.ended) < 0 ? -1.0f : (build ? r.built : r.ended) * DT,
			            build ? 40 : 15, r.end_dist, r.body, r.others_moved);
			if (build) {
				CHECK(r.built > 0, "a woman sent to build from just outside an idle woman's square gets round it and builds");
			} else {
				CHECK(r.ended > 0 && r.end_dist <= 1.0f, "a woman walking from just outside an idle woman's square gets round it to her point");
			}
			CHECK(r.body >= 1.0f, "and never comes inside a standing unit's body");
			CHECK(r.others_moved < 0.01f, "nor pushes one");
		}
	{
		// A woman just outside the bottom edge of an idle woman's square (`off` below it, `lat` along
		// it from her centre), heading `deg` degrees into it, 5.2 units, with a second woman's square
		// or a town centre's clearance 0.05 below her: a slit too narrow to walk along at the crowd
		// scale. The walk ends at its point and the builder builds her house just beyond it.
		int walks = 0, ended = 0, builds = 0, built = 0;
		float body = 1e9f;
		for (int layout = 0; layout < 2; ++layout)
			for (const bool build : {false, true})
				for (const float off : {0.01f, 0.03f})
					for (const float deg : {20.0f, 90.0f})
						for (const float lat : {-0.6f, 0.0f, 0.6f}) {
							auto s = std::make_unique<Simulation>();
							freeze_setup(*s);
							const float ix = 20.0f, iz = 20.0f, h = 2.0f * unit_push_clearance(UNIT_FEMALE_CITIZEN), off2 = 0.05f;
							const float wx = ix + lat, wz = iz - h - off, a = deg * 3.14159265f / 180.0f;
							const float gx = wx + 5.2f * std::cos(a), gz = wz + 5.2f * std::sin(a);
							if (layout == 1) s->spawn_building_world(wx, wz - off2 - CLEARANCE_DEFAULT - 2.0f, BLD_TOWN_CENTER, 0, 1.0f);
							const uint32_t w = s->spawn_unit(wx, wz, 0, UNIT_FEMALE_CITIZEN);
							s->spawn_unit(ix, iz, 0, UNIT_FEMALE_CITIZEN);
							if (layout == 0) s->spawn_unit(wx + 0.3f, wz - off2 - h, 0, UNIT_FEMALE_CITIZEN);
							uint32_t house = 0;
							if (build) {
								house = s->spawn_building_world(gx + 1.6f * std::cos(a), gz + 1.6f * std::sin(a), BLD_HOUSE, 0, 0.0f);
								s->order_build_or_repair(&w, 1, house);
							} else {
								s->order_walk(&w, 1, gx, gz);
							}
							const FreezeRun r = freeze_run(*s, w, gx, gz, house, build ? 800 : 300);
							body = std::min(body, r.body);
							if (build) ++builds, built += r.built > 0;
							else ++walks, ended += r.ended > 0 && r.end_dist <= 1.0f;
						}
		std::printf("  women in a slit beside an idle woman's square: %d of %d walks ended at their point, %d of %d houses built; "
		            "closest to a body %.3f of the radius sum\n", ended, walks, built, builds, body);
		CHECK(ended == walks && built == builds, "a woman in a slit beside an idle woman's square gets out and on");
		CHECK(body >= 1.0f, "and never comes inside a standing unit's body");
	}
	for (const UnitType standing : {UNIT_FEMALE_CITIZEN, UNIT_CAVALRY}) {
		// A phalanx of 12 spearmen walks about 25 units past one standing unit, set 2.5 units either
		// side of its way in steps of 0.25, in three directions. No member that started clear of the
		// standing unit comes inside its body.
		int runs = 0, inside = 0, done = 0;
		float body = 1e9f;
		for (int k = -10; k <= 10; ++k)
			for (int dir = 0; dir < 3; ++dir) {
				const float lat = 0.25f * (float)k;
				auto s = std::make_unique<Simulation>();
				freeze_setup(*s);
				std::vector<uint32_t> hop;
				for (int m = 0; m < 12; ++m) hop.push_back(s->spawn_unit(10.0f + (m % 4) * 1.6f, 10.0f + (m / 4) * 1.6f, 0, UNIT_SPEARMAN));
				s->order_set_formation(hop.data(), hop.size(), FORMATION_PHALANX);
				for (int t = 0; t < 200; ++t) s->tick(DT);
				const float gx = dir == 0 ? 30.0f : dir == 1 ? 34.0f : 32.0f, gz = dir == 0 ? 34.0f : dir == 1 ? 30.0f : 32.0f;
				const float ux = gx - 12.0f, uz = gz - 12.0f, ul = std::hypot(ux, uz);
				const uint32_t st = s->spawn_unit(22.0f - uz / ul * lat, 22.0f + ux / ul * lat, 0, standing);
				s->order_walk(hop.data(), hop.size(), gx, gz);
				const float rs = unit_collision_radius(UNIT_SPEARMAN) + unit_collision_radius(standing);
				std::vector<uint8_t> clear(hop.size(), 0);
				const int si = idx(*s, st);
				for (size_t m = 0; m < hop.size(); ++m) {
					const int hi = idx(*s, hop[m]);
					clear[m] = std::hypot(s->units_.pos_x[hi] - s->units_.pos_x[si], s->units_.pos_z[hi] - s->units_.pos_z[si]) >= rs;
				}
				float closest = 1e9f;
				int t = 0;
				for (; t < 1000; ++t) {
					s->tick(DT);
					const int sj = idx(*s, st);
					bool any = false;
					for (size_t m = 0; m < hop.size(); ++m) {
						const int hi = idx(*s, hop[m]);
						if (clear[m])
							closest = std::min(closest, std::hypot(s->units_.pos_x[hi] - s->units_.pos_x[sj], s->units_.pos_z[hi] - s->units_.pos_z[sj]) / rs);
						any |= s->units_.has_order[hi];
					}
					if (!any) break;
				}
				++runs;
				inside += closest < 1.0f;
				done += t < 1000;
				body = std::min(body, closest);
			}
		const char *what = standing == UNIT_CAVALRY ? "cavalryman" : "woman";
		std::printf("  a phalanx past a standing %s: %d of %d runs with a member inside its body (closest %.3f of the radius sum), "
		            "%d of %d done in 50 s\n", what, inside, runs, body, done, runs);
		CHECK(inside == 0, standing == UNIT_CAVALRY ? "a phalanx walking past a standing cavalryman keeps out of his body"
		                                            : "a phalanx walking past a standing woman keeps out of her body");
		CHECK(done == runs, "and every run ends");
	}
	{
		// 23. A bay of deep water east of x 20 between z -30 and 30, its shore a cliff (heights 3 to -3
		// over one tile): a woman put in the bay walks out across the cliff's tile to her point, and a
		// wolf put in the bay comes out to bite a spearman standing on the shore (he doesn't fight back).
		auto s = std::make_unique<Simulation>();
		setup(*s);
		constexpr int RES = 129;
		constexpr float HALF = 64.0f;
		s->set_map_half(HALF);
		std::vector<float> h((size_t)RES * RES);
		for (int j = 0; j < RES; ++j)
			for (int i = 0; i < RES; ++i) {
				const float x = -HALF + 2.0f * HALF * i / (RES - 1), z = -HALF + 2.0f * HALF * j / (RES - 1);
				h[(size_t)j * RES + i] = x > 20.0f && std::fabs(z) < 30.0f ? -3.0f : 3.0f;
			}
		s->set_terrain_data(h, RES, 0.0f);
		s->begin_bulk_spawn();
		s->end_bulk_spawn();
		const uint32_t woman = s->spawn_unit(24.5f, 0.0f, 0, UNIT_FEMALE_CITIZEN, 100.0f);
		const uint32_t guard = s->spawn_unit(15.0f, 12.0f, 0, UNIT_SPEARMAN, 150.0f);
		const int gi = idx(*s, guard);
		s->units_.stance[gi] = STANCE_STAND_GROUND;
		s->units_.attack_damage[gi] = 0.0f;
		const uint32_t wolf = s->spawn_fauna(24.5f, 12.0f, FAUNA_WOLF);
		auto deep = [&](float x, float z) {
			const auto [cx, cy] = s->nav_grid_.world_to_cell(x, z);
			return s->nav_grid_.cells.at(cx, cy) == NAV_WATER;
		};
		const int wi0 = idx(*s, woman);
		const bool laid = deep(s->units_.pos_x[wi0], s->units_.pos_z[wi0]) && deep(24.5f, 12.0f) && !deep(15.0f, 12.0f) &&
		                  s->nav_grid_.near_steep_tile(20.5f, 0.0f, 0.0f) && s->nav_grid_.near_steep_tile(20.5f, 12.0f, 0.0f);
		s->tick(DT);
		s->order_walk(&woman, 1, 10.0f, 0.0f);
		s->fauna_start_attack(s->fauna_id_to_idx_.at(wolf), guard);
		int ashore = -1, bitten = -1;
		for (int t = 1; t <= 600 && (ashore < 0 || bitten < 0); ++t) {
			s->tick(DT);
			const int wi = idx(*s, woman);
			if (ashore < 0 && !s->units_.has_order[wi] && std::hypot(s->units_.pos_x[wi] - 10.0f, s->units_.pos_z[wi]) < 0.5f) ashore = t;
			if (bitten < 0 && s->units_.hp[idx(*s, guard)] < 150.0f) bitten = t;
		}
		// Only off: sent back into the bay from the land, she stops at the cliff's clearance.
		s->order_walk(&woman, 1, 24.5f, 0.0f);
		float nearest = 1e9f;
		for (int t = 1; t <= 200; ++t) {
			s->tick(DT);
			const int wi = idx(*s, woman);
			nearest = std::min(nearest, 20.0f - s->units_.pos_x[wi]);
		}
		std::printf("  a bay with a cliff for a shore: a woman in it walked out to her point after %d ticks, a wolf in it bit a "
		            "spearman ashore after %d ticks (-1 = never in 600); sent back in, she came within %.2f of the cliff's tile\n",
		            ashore, bitten, nearest);
		CHECK(laid, "the woman and the wolf start in deep water, the spearman on land, and the shore is steep");
		CHECK(ashore > 0, "a unit in deep water walks out across a steep shore to its point");
		CHECK(bitten > 0, "an animal in deep water comes out across a steep shore to its target");
		CHECK(ashore > 0 && nearest >= s->unit_clearance_at(idx(*s, woman)) - 0.01f,
		      "a unit on land sent into the bay stops at the cliff's clearance");
	}
	{
		// 24a. A circular map (edge 61) whose east half ends at r 46 in deep water. Deer and hares at
		// r 59, in the sea in the east and on land in the west, roam, and a deer on each diagonal, one
		// in the sea and one on land, flees a spearman inland, out towards the rim.
		auto s = std::make_unique<Simulation>();
		setup(*s);
		constexpr int RES = 129;
		constexpr float HALF = 64.0f;
		s->set_map_half(HALF);
		std::vector<float> h((size_t)RES * RES);
		for (int j = 0; j < RES; ++j)
			for (int i = 0; i < RES; ++i) {
				const float x = -HALF + 2.0f * HALF * i / (RES - 1), z = -HALF + 2.0f * HALF * j / (RES - 1);
				h[(size_t)j * RES + i] = x > 0.0f && std::hypot(x, z) > 46.0f ? -3.0f : 3.0f;
			}
		s->set_terrain_data(h, RES, 0.0f);
		s->set_circular_map(true);
		s->begin_bulk_spawn();
		s->end_bulk_spawn();
		const float edge = HALF - MAP_EDGE_DIST;
		const uint32_t hunter = s->spawn_unit(26.0f, 26.0f, 0, UNIT_SPEARMAN, 150.0f);
		const uint32_t hunter2 = s->spawn_unit(-26.0f, -26.0f, 0, UNIT_SPEARMAN, 150.0f);
		s->units_.stance[idx(*s, hunter)] = STANCE_STAND_GROUND;
		s->units_.stance[idx(*s, hunter2)] = STANCE_STAND_GROUND;
		std::vector<uint32_t> herd;
		bool laid = true;
		for (int k = 0; k < 12; ++k) {
			const float a = 0.785398f + 6.283185f * (float)k / 12.0f, x = 59.0f * std::sin(a), z = 59.0f * std::cos(a);
			herd.push_back(s->spawn_fauna(x, z, k % 2 ? FAUNA_RABBIT : FAUNA_DEER));
			const auto [cx, cy] = s->nav_grid_.world_to_cell(x, z);
			laid = laid && (x > 0.0f ? s->nav_grid_.cells.at(cx, cy) == NAV_WATER : s->nav_grid_.is_passable(cx, cy));
		}
		s->tick(DT);
		s->fauna_attacked(s->fauna_id_to_idx_.at(herd[0]), hunter);
		s->fauna_attacked(s->fauna_id_to_idx_.at(herd[6]), hunter2);
		const bool fled = s->fauna_.behavior[s->fauna_id_to_idx_.at(herd[0])] == FAUNA_FLEE &&
		                  s->fauna_.behavior[s->fauna_id_to_idx_.at(herd[6])] == FAUNA_FLEE;
		float out = 0.0f, land_out = 0.0f, moved = 0.0f;
		for (int t = 1; t <= 2400; ++t) {
			s->tick(DT);
			for (size_t k = 0; k < herd.size(); ++k) {
				const auto it = s->fauna_id_to_idx_.find(herd[k]);
				if (it == s->fauna_id_to_idx_.end()) continue;
				const float r = std::hypot(s->fauna_.pos_x[it->second], s->fauna_.pos_z[it->second]);
				out = std::max(out, r);
				if (s->fauna_.pos_x[it->second] < 0.0f) land_out = std::max(land_out, r);
				if (k == 0) moved = std::max(moved, r - 59.0f);
			}
		}
		std::printf("  a circular map's rim: 12 deer and hares in the sea and on land there went %.2f from the centre at most, %.2f "
		            "on land (edge %.2f less clearance %.2f); the deer fleeing outwards in the sea went %.2f out\n", out, land_out, edge,
		            CLEARANCE_DEFAULT, moved);
		CHECK(laid && fled, "the animals start in deep water in the east and on land in the west, and both deer flee the spearmen");
		CHECK(moved > 1.0f, "an animal in deep water flees across it");
		CHECK(out <= edge - CLEARANCE_DEFAULT + 0.01f, "an animal, in deep water or on land, never steps past a circular map's edge");
	}
	{
		// 24b. A deep channel at 20 < x < 32, |z| < 40, and a house at (18, 0): a wolf stands beside
		// the house, outside it, on a navcell the house's raster closes, with the water east of it.
		auto s = std::make_unique<Simulation>();
		setup(*s);
		constexpr int RES = 129;
		constexpr float HALF = 64.0f;
		s->set_map_half(HALF);
		std::vector<float> h((size_t)RES * RES);
		for (int j = 0; j < RES; ++j)
			for (int i = 0; i < RES; ++i) {
				const float x = -HALF + 2.0f * HALF * i / (RES - 1), z = -HALF + 2.0f * HALF * j / (RES - 1);
				h[(size_t)j * RES + i] = x > 20.0f && x < 32.0f && std::fabs(z) < 40.0f ? -0.6f : 0.1f;
			}
		s->set_terrain_data(h, RES, 0.0f);
		s->begin_bulk_spawn();
		s->end_bulk_spawn();
		const uint32_t house = s->spawn_building_world(18.0f, 0.0f, BLD_HOUSE, 0, 1.0f);
		s->tick(DT);
		const auto &b = s->buildings_[s->building_id_to_idx_.at(house)];
		// The point of such a navcell farthest east, 0.5 or more outside the house and its own
		// navcell neither deep water nor in a steep tile's band.
		const float cell = 2.0f * HALF / (float)s->nav_grid_.res;
		float wx = 0.0f, wz = 0.0f;
		bool found = false;
		for (int y = 0; y < s->nav_grid_.res && !found; ++y)
			for (int x = 0; x + 1 < s->nav_grid_.res && !found; ++x) {
				if (s->nav_grid_.cells.at(x, y) != NAV_OBSTRUCTED || s->nav_grid_.cells.at(x + 1, y) != NAV_WATER) continue;
				for (float fy = 0.1f; fy < 0.95f && !found; fy += 0.1f)
					for (float fx = 0.9f; fx > 0.05f && !found; fx -= 0.1f) {
						const float px = -HALF + (x + fx) * cell, pz = -HALF + (y + fy) * cell;
						const float g = std::max(std::fabs(px - b.x) - b.hu, std::fabs(pz - b.z) - b.hv);
						if (g > 0.5f && !s->near_deep_water(px, pz, 0.0f) && !s->nav_grid_.near_steep_tile(px, pz, CLEARANCE_DEFAULT))
							wx = px, wz = pz, found = true;
					}
			}
		int wet = -1, bitten = -1;
		if (found) {
			const uint32_t guard = s->spawn_unit(38.0f, wz, 0, UNIT_SPEARMAN, 150.0f);
			const int gi = idx(*s, guard);
			s->units_.stance[gi] = STANCE_STAND_GROUND;
			s->units_.attack_damage[gi] = 0.0f;
			const uint32_t wolf = s->spawn_fauna(wx, wz, FAUNA_WOLF);
			s->fauna_start_attack(s->fauna_id_to_idx_.at(wolf), guard);
			for (int t = 1; t <= 1200; ++t) {
				s->tick(DT);
				const auto it = s->fauna_id_to_idx_.find(wolf);
				if (it == s->fauna_id_to_idx_.end()) break;
				if (wet < 0 && s->near_deep_water(s->fauna_.pos_x[it->second], s->fauna_.pos_z[it->second], 0.0f)) wet = t;
				if (bitten < 0 && s->units_.hp[idx(*s, guard)] < 150.0f) bitten = t;
			}
		}
		std::printf("  a wolf beside a house at (%.2f, %.2f), a deep channel east of it: in the water after %d ticks, bit the spearman "
		            "across it after %d ticks (-1 = never in 1200)\n", wx, wz, wet, bitten);
		CHECK(found, "the wolf stands on a navcell the house closes, beside deep water");
		CHECK(found && wet < 0, "an animal on a navcell a structure closes never steps into deep water");
	}
	{
		// 25. Sahara seed 3, 209 s: the palms round p0's storehouse 1674 at (85, 29), moved by (OX, OZ) into
		// player 0's territory (setup's town centre at (-52, -52)). The wall runs x 76-81, z 23-33; the
		// ways round are past its ends. The last nine stand round the storehouse, beyond the wall.
		static const float palms[][2] = {
			{76.5f, 24.5f}, {77.5f, 23.5f}, {78.5f, 24.5f}, {79.5f, 25.5f}, {79.5f, 26.5f}, {78.5f, 25.5f}, {80.5f, 26.5f},
			{79.5f, 24.5f}, {79.5f, 27.5f}, {78.5f, 28.5f}, {79.5f, 28.5f}, {78.5f, 29.5f}, {79.5f, 29.5f}, {79.5f, 30.5f},
			{78.5f, 30.5f}, {77.5f, 29.5f}, {78.5f, 31.5f}, {77.5f, 32.5f}, {76.5f, 32.5f}, {76.5f, 31.5f},
			{87.5f, 28.5f}, {87.5f, 34.5f}, {87.5f, 31.5f}, {86.5f, 32.5f}, {85.5f, 31.5f}, {84.5f, 32.5f}, {86.5f, 31.5f},
			{82.5f, 29.5f}, {83.5f, 30.5f}};
		constexpr float OX = -125.0f, OZ = -60.0f;
		enum Run { CUTTER, RETASKED, RETURN_ORDER, GATHER_WALK, SAVED, WALK, RUNS };
		static const char *const names[RUNS] = {"a spearman taking a full load back (tick_gather)",
			"a spearman Petra orders to gather again every 1.6 s", "a woman sent with ReturnResource",
			"an empty woman sent to gather a tree beyond the wall", "the first spearman, saved and loaded after 2 s",
			"a spearman walked to (82, 31.5), beyond the wall"};
		constexpr float WALK_X = 82.0f, WALK_Z = 31.5f;
		// Where each stood: unit 6 in the notch at (77.77, 31.30), unit 1787 at (77.76, 30.22) below it,
		// and two women on the wall's west face.
		static const float start[RUNS][2] = {{77.77f, 31.30f}, {77.76f, 30.22f}, {76.5f, 28.2f}, {76.5f, 30.5f}, {77.77f, 31.30f},
			{77.77f, 31.30f}};
		int done[RUNS];
		float end_gap[RUNS];
		for (int run = 0; run < RUNS; ++run) {
			auto s = std::make_unique<Simulation>();
			setup(*s);
			const uint32_t store = s->spawn_building_world(85.0f + OX, 29.0f + OZ, BLD_STOREHOUSE, 0, 1.0f);
			uint32_t far_tree = 0; // tree 338 at (83.5, 30.5), beside the storehouse
			for (const auto &p : palms)
				far_tree = s->spawn_resource_world(p[0] + OX, p[1] + OZ, RES_WOOD, 300, RES_SUB_NONE, 0, FLORA_TREE_CRETAN_DATE_PALM_PATCH);
			// A woman far off at the town centre (separation() runs only with two units or more).
			s->spawn_unit(-46.0f, -46.0f, 0, UNIT_FEMALE_CITIZEN, 150.0f);
			const bool woman = run == RETURN_ORDER || run == GATHER_WALK;
			const uint32_t w = s->spawn_unit(start[run][0] + OX, start[run][1] + OZ, 0, woman ? UNIT_FEMALE_CITIZEN : UNIT_SPEARMAN, 150.0f);
			const int wi = idx(*s, w);
			const int32_t load = run == RETURN_ORDER ? 10 : run == GATHER_WALK || run == WALK ? 0 : s->carry_cap((size_t)wi, RES_WOOD);
			s->units_.res_type_carried[wi] = RES_WOOD;
			s->units_.carry[wi] = load;
			if (run == RETURN_ORDER) s->order_return_resource(&w, 1, store);
			else if (run == WALK) s->order_walk(&w, 1, WALK_X + OX, WALK_Z + OZ, false, true);
			else s->order_gather(&w, 1, far_tree);
			std::unique_ptr<Simulation> copy;
			const int wood0 = s->player_resource(0, RES_WOOD);
			done[run] = -1;
			for (int t = 1; t <= 1200 && done[run] < 0; ++t) {
				if (run == SAVED && t == 40) {
					const std::string path = temp_save_path();
					copy = std::make_unique<Simulation>();
					const bool ok = !path.empty() && s->save_state(path.c_str()) && copy->load_state(path.c_str());
					std::remove(path.c_str());
					CHECK(ok, "a game with a worker on its way round the palms saves and loads");
					if (!ok) break;
					s = std::move(copy); // the loaded game goes on in its place
				}
				const int i = idx(*s, w);
				if (run == RETASKED && t % 32 == 0 && s->units_.action_state[i] == ACTION_RETURN_DROPSITE) s->order_gather(&w, 1, far_tree);
				s->tick(DT);
				const int j = idx(*s, w);
				if (run == GATHER_WALK ? s->units_.carry[j] > 0 && s->units_.target_resource_id[j] == far_tree
				    : run == WALK      ? s->units_.action_state[j] != ACTION_MOVE &&
				                         std::hypot(s->units_.pos_x[j] - (WALK_X + OX), s->units_.pos_z[j] - (WALK_Z + OZ)) < 1.0f
				                       : s->player_resource(0, RES_WOOD) >= wood0 + load)
					done[run] = t;
			}
			const int j = idx(*s, w);
			end_gap[run] = std::hypot(s->units_.pos_x[j] - (85.0f + OX), s->units_.pos_z[j] - (29.0f + OZ));
			std::printf("  palm wall (Stats, sahara seed 3): %s: %s after %.2f s (-1: not in 60 s), %.1f from the storehouse then\n", names[run],
			            run == GATHER_WALK ? "gathers it" : run == WALK ? "gets there" : "drops the wood",
			            done[run] < 0 ? -1.0f : done[run] * DT, end_gap[run]);
		}
		CHECK(done[CUTTER] > 0 && done[CUTTER] * DT < 30.0f && done[RETURN_ORDER] > 0 && done[RETURN_ORDER] * DT < 30.0f,
		      "a worker with a load west of a palm wall gets round it to the storehouse beyond and drops it");
		CHECK(done[RETASKED] > 0 && done[RETASKED] * DT < 30.0f, "and so does one ordered to gather again every 1.6 s, as Petra does");
		CHECK(done[GATHER_WALK] > 0 && done[GATHER_WALK] * DT < 30.0f, "a gatherer sent to a tree beyond the wall gets round it and cuts that tree");
		CHECK(done[WALK] > 0 && done[WALK] * DT < 30.0f, "a soldier walked to a point beyond the wall gets round it there");
		CHECK(done[SAVED] == done[CUTTER] && end_gap[SAVED] == end_gap[CUTTER], "a game saved on the way round and loaded goes on the same");
	}
	{
		// 26a. Wall segments at x 0-2, z -60 to 60, but for a gap at z 40-44; the storehouse 2.2 units east
		// of the wall, two trees on the west side by the wall, and the women's tree 7 units west.
		constexpr int N = 6;
		constexpr float STORE_X = 4.2f;
		int first_drop[2][N];
		float drop_x[2][N], drop_z[2][N];
		for (int run = 0; run < 2; ++run) {
			auto s = std::make_unique<Simulation>();
			setup(*s);
			for (uint8_t p = 0; p < 2; ++p) s->player(p).active = 1, s->player(p).is_ai = 0;
			for (int cy = -30; cy <= 29; ++cy)
				if (cy != 20 && cy != 21) s->spawn_building(0, cy, BLD_WALL_SEGMENT, 0, 1.0f);
			s->spawn_building_world(STORE_X, 0.0f, BLD_STOREHOUSE, 0, 1.0f);
			s->spawn_resource_world(-1.2f, 2.5f, RES_WOOD, 300);
			s->spawn_resource_world(-1.2f, -2.5f, RES_WOOD, 300);
			const uint32_t tree = s->spawn_resource_world(-7.0f, 0.0f, RES_WOOD, 5000);
			s->spawn_unit(-46.0f, -46.0f, 0, UNIT_FEMALE_CITIZEN, 150.0f);
			uint32_t ws[N];
			for (int k = 0; k < N; ++k) {
				ws[k] = s->spawn_unit(-2.2f - 0.5f * (k % 3), -0.5f + 0.6f * (k / 3), 0, UNIT_FEMALE_CITIZEN, 150.0f);
				const int wi = idx(*s, ws[k]);
				s->units_.res_type_carried[wi] = RES_WOOD;
				s->units_.carry[wi] = 10;
				first_drop[run][k] = -1;
			}
			s->order_gather(ws, N, tree);
			for (int t = 1; t <= 2400; ++t) {
				// The second game is saved and loaded after the first woman's short path to the wall's face
				// and before her third failed turn there.
				if (run == 1 && t == 24) {
					const std::string path = temp_save_path();
					auto copy = std::make_unique<Simulation>();
					const bool ok = !path.empty() && s->save_state(path.c_str()) && copy->load_state(path.c_str());
					std::remove(path.c_str());
					CHECK(ok, "a game with returners at a wall saves and loads");
					if (!ok) break;
					s = std::move(copy);
				}
				int carry0[N];
				for (int k = 0; k < N; ++k) {
					const int i = idx(*s, ws[k]);
					// Petra's worker update: a returner under its Gather order is ordered to gather again.
					if ((t + 3 * k) % 32 == 0 && s->units_.action_state[i] == ACTION_RETURN_DROPSITE && s->units_.carry[i] > 0)
						s->order_gather(&ws[k], 1, tree);
					carry0[k] = s->units_.carry[i];
				}
				s->tick(DT);
				bool all = true;
				for (int k = 0; k < N; ++k) {
					const int i = idx(*s, ws[k]);
					if (first_drop[run][k] < 0 && carry0[k] >= 10 && s->units_.carry[i] < carry0[k] &&
							s->units_.action_state[i] != ACTION_RETURN_DROPSITE) {
						first_drop[run][k] = t;
						drop_x[run][k] = s->units_.pos_x[i];
						drop_z[run][k] = s->units_.pos_z[i];
					}
					all = all && first_drop[run][k] >= 0;
				}
				if (all) break;
			}
		}
		int at_store = 0, last = 0;
		bool same = true;
		for (int k = 0; k < N; ++k) {
			if (first_drop[0][k] >= 0 && std::hypot(drop_x[0][k] - STORE_X, drop_z[0][k]) < 4.0f) ++at_store;
			last = std::max(last, first_drop[0][k] < 0 ? 1 << 20 : first_drop[0][k]);
			same = same && first_drop[1][k] == first_drop[0][k] && (first_drop[0][k] < 0 ||
					(drop_x[1][k] == drop_x[0][k] && drop_z[1][k] == drop_z[0][k]));
		}
		std::printf("  wall with a far gap: %d of %d women drop their first load at the storehouse beyond it, the last after %.2f s\n",
		            at_store, N, last * DT);
		CHECK(at_store == N && last * DT < 40.0f,
		      "workers at a wall whose way round is beyond the short path's reach, their storehouse just beyond it, all drop there");
		CHECK(same, "and a game saved on the way and loaded goes on the same");
	}
	{
		// 26b. Wall segments along x = 0 for z in [-40, 40) but for a gap at z = 30; a palm 3.8 units east of the
		// woman, beyond the wall, its storehouse beside it. Wall segments close round her at tick 2 and
		// their north side goes again at tick 30.
		auto s = std::make_unique<Simulation>();
		setup(*s);
		for (uint8_t p = 0; p < 2; ++p) s->player(p).active = 1, s->player(p).is_ai = 0;
		for (float z = -40.0f; z < 40.0f; z += 2.0f)
			if (std::fabs(z - 30.0f) >= 2.5f) s->spawn_building_world(0.0f, z, BLD_WALL_SEGMENT, 0, 1.0f);
		const uint32_t palm = s->spawn_resource_world(2.0f, 0.0f, RES_WOOD, 300, RES_SUB_NONE, 0, FLORA_TREE_CRETAN_DATE_PALM_PATCH);
		s->spawn_building_world(3.0f, 8.0f, BLD_STOREHOUSE, 0, 1.0f);
		const uint32_t w = s->spawn_unit(-1.8f, 0.0f, 0, UNIT_FEMALE_CITIZEN, 0.0f);
		s->spawn_unit(-40.0f, -40.0f, 0, UNIT_FEMALE_CITIZEN, 0.0f);
		s->order_gather(&w, 1, palm);
		uint32_t north = 0;
		int cut = -1, gave_up = -1;
		for (int t = 1; t <= 1200 && cut < 0; ++t) {
			if (t == 2) {
				north = s->spawn_building_world(-2.0f, 2.0f, BLD_WALL_SEGMENT, 0, 1.0f);
				s->spawn_building_world(-2.0f, -2.0f, BLD_WALL_SEGMENT, 0, 1.0f);
				for (float z = -2.0f; z <= 2.0f; z += 2.0f) s->spawn_building_world(-4.0f, z, BLD_WALL_SEGMENT, 0, 1.0f);
			}
			if (t == 30) {
				const auto it = s->building_id_to_idx_.find(north);
				if (it != s->building_id_to_idx_.end()) s->destroy_building(s->buildings_[it->second]);
			}
			s->tick(DT);
			const int j = idx(*s, w);
			if (s->units_.target_resource_id[j] == palm && s->units_.carry[j] > 0) cut = t;
			if (gave_up < 0 && s->units_.target_resource_id[j] != palm) gave_up = t;
		}
		std::printf("  shut in by a wall with a far gap: the gatherer cuts her palm after %.2f s (-1: not in 60 s), gives it up after %.2f s (-1: never)\n",
		            cut < 0 ? -1.0f : cut * DT, gave_up < 0 ? -1.0f : gave_up * DT);
		CHECK(cut > 0 && cut * DT < 30.0f && gave_up < 0,
		      "a gatherer shut in for a moment by a wall whose way round is beyond the short path's reach keeps to her palm and cuts it");
	}
	// The village round player 0's civic centre in Stats' dumps.
	const DumpBld cc{1, 37.335f, 37.335f, BLD_TOWN_CENTER, 1.0f}, house600{600, 39.0f, 41.0f, BLD_HOUSE, 1.0f};
	const DumpBld barracks700{700, 40.0f, 44.0f, BLD_BARRACKS, 1.0f}, house601{601, 35.0f, 43.0f, BLD_HOUSE, 1.0f};
	const DumpBld house934{934, 37.0f, 43.0f, BLD_HOUSE, 1.0f};
	constexpr UnitType W = UNIT_FEMALE_CITIZEN, A = UNIT_ARCHER;
	{
		// 27. Each builder (the first unit) stands among its player's idle units, all with no order, and
		// is sent to its foundation 6 to 14 units away. The last crowd is one of random layouts round the
		// civ 6 builder, settled for a second first, whose path round the crowd led it within a woman's
		// body.
		struct Crowd {
			const char *name;
			uint8_t civ;
			std::vector<DumpBld> blds;
			std::vector<DumpUnit> units;
			uint32_t site;
			int settle;
		};
		const std::vector<Crowd> crowds = {
			{"civ 0 house", CIV_ATHENIANS, {cc, house600, barracks700, {930, 35.0f, 33.0f, BLD_HOUSE, 0.0f}},
			 {{879, 27.786f, 33.798f, W}, {8, 25.01f, 34.18f, A}, {881, 29.58f, 35.72f, W}, {882, 29.16f, 32.94f, W}, {883, 26.30f, 35.54f, W},
			  {884, 27.81f, 36.17f, W}, {885, 29.32f, 34.38f, W}, {905, 26.63f, 33.89f, A}, {906, 29.03f, 31.53f, A}, {912, 27.55f, 32.36f, A},
			  {913, 27.91f, 35.02f, A}},
			 930, 0},
			{"civ 9 house", CIV_MAURYANS,
			 {cc, house600, barracks700, house601, house934, {992, 35.0f, 33.0f, BLD_STOREHOUSE, 0.0f}, {1038, 37.0f, 33.0f, BLD_HOUSE, 0.0f}},
			 {{911, 27.522f, 33.009f, W}, {910, 27.67f, 33.97f, W}, {912, 26.17f, 34.78f, W}, {913, 29.26f, 35.16f, W}, {914, 26.09f, 32.89f, W},
			  {981, 28.97f, 32.40f, A}, {982, 27.93f, 35.35f, A}, {984, 27.24f, 31.75f, A}, {987, 29.33f, 33.79f, A}},
			 1038, 0},
			{"civ 9 second house", CIV_MAURYANS,
			 {cc, house600, barracks700, house601, house934, {992, 35.0f, 33.0f, BLD_STOREHOUSE, 0.0f}, {1038, 37.0f, 33.0f, BLD_HOUSE, 0.0f},
			  {1063, 39.0f, 33.0f, BLD_HOUSE, 0.0f}},
			 {{913, 29.271f, 35.155f, W}, {8, 29.73f, 37.72f, A}, {910, 27.67f, 33.97f, W}, {981, 28.97f, 32.40f, A}, {982, 27.93f, 35.35f, A},
			  {983, 30.82f, 33.42f, A}, {986, 30.68f, 36.54f, A}, {987, 29.32f, 33.83f, A}, {988, 26.95f, 36.44f, A}, {989, 30.79f, 34.99f, A},
			  {990, 28.63f, 36.63f, A}},
			 1063, 0},
			{"civ 2 market", CIV_PERSIANS, {cc, house600, barracks700, {982, 42.0f, 38.0f, BLD_MARKET, 0.0f}},
			 {{909, 29.698f, 35.842f, W}, {8, 31.07f, 33.81f, A}, {912, 27.72f, 36.57f, W}, {913, 29.17f, 34.37f, W}, {953, 31.13f, 36.36f, A},
			  {954, 27.40f, 34.29f, A}, {956, 28.05f, 38.11f, A}, {957, 28.26f, 35.44f, A}, {959, 31.03f, 34.82f, A}, {961, 29.25f, 37.11f, A}},
			 982, 0},
			{"civ 4 house", CIV_CARTHAGINIANS, {cc, house600, barracks700, {972, 35.0f, 33.0f, BLD_HOUSE, 0.0f}},
			 {{895, 27.775f, 33.193f, W}, {9, 25.08f, 34.15f, A}, {910, 29.15f, 33.03f, W}, {911, 26.30f, 35.49f, W}, {913, 29.26f, 34.43f, W},
			  {954, 27.42f, 34.28f, A}, {957, 28.25f, 35.44f, A}, {958, 28.24f, 31.72f, A}, {959, 26.80f, 31.69f, A}, {960, 26.12f, 33.15f, A}},
			 972, 0},
			{"civ 4 market", CIV_CARTHAGINIANS, {cc, house600, barracks700, {972, 35.0f, 33.0f, BLD_HOUSE, 1.0f}, {1011, 42.0f, 38.0f, BLD_MARKET, 0.0f}},
			 {{911, 26.295f, 35.489f, W}, {9, 25.08f, 34.15f, A}, {895, 28.45f, 33.47f, W}, {912, 27.72f, 36.57f, W}, {952, 26.44f, 37.19f, A},
			  {954, 27.27f, 34.25f, A}, {955, 24.64f, 35.50f, A}, {957, 28.27f, 35.48f, A}, {960, 26.12f, 33.15f, A}},
			 1011, 0},
			{"civ 6 house", CIV_BRITONS, {cc, {943, 35.0f, 33.0f, BLD_HOUSE, 0.0f}},
			 {{896, 28.348f, 34.243f, W}, {8, 28.00f, 31.51f, A}, {9, 27.85f, 32.93f, A}, {910, 29.31f, 35.15f, W}, {911, 27.11f, 35.62f, W},
			  {912, 28.44f, 35.95f, W}, {914, 30.22f, 34.11f, W}, {927, 30.01f, 36.65f, A}, {929, 26.43f, 34.28f, A}, {932, 30.77f, 32.51f, A},
			  {933, 26.31f, 33.28f, A}, {934, 29.37f, 32.77f, A}},
			 943, 0},
			{"civ 11 house", CIV_PTOLEMIES, {cc, {968, 35.0f, 43.0f, BLD_HOUSE, 0.0f}},
			 {{895, 27.890f, 35.144f, W}, {909, 29.20f, 34.10f, W}, {910, 26.81f, 34.05f, W}, {911, 27.46f, 36.60f, W}, {912, 29.40f, 35.50f, W},
			  {913, 25.93f, 35.38f, W}, {926, 27.26f, 32.35f, A}, {928, 28.28f, 33.59f, A}, {929, 29.01f, 36.55f, A}},
			 968, 0},
			{"a civ 6 layout", CIV_BRITONS, {cc, {943, 35.0f, 33.0f, BLD_HOUSE, 0.0f}},
			 {{896, 28.129f, 33.961f, W}, {902, 27.263f, 33.763f, W}, {903, 26.285f, 33.649f, A}, {904, 27.086f, 34.936f, W},
			  {905, 30.184f, 34.799f, A}, {906, 28.087f, 32.062f, W}, {907, 26.981f, 35.754f, A}, {908, 28.285f, 33.031f, W},
			  {909, 29.078f, 34.541f, A}},
			 943, 20},
		};
		int started = 0;
		float crowd_moved = 0.0f;
		for (const Crowd &c : crowds) {
			auto s = dump_scene(c.civ, c.blds, c.units);
			for (int t = 0; t < c.settle; ++t) s->tick(DT);
			const uint32_t builder = c.units[0].id;
			s->order_build_or_repair(&builder, 1, c.site);
			const int t = build_started(*s, c.site, 1200);
			std::printf("  builder %u in the %s crowd: started its foundation after %.2f s (-1: not in 60 s)", builder, c.name,
			            t < 0 ? -1.0f : t * DT);
			if (c.settle == 0) {
				// The crowd stands as it does without the builder, which once ordered neither pushes nor is
				// pushed by it (Push never mixes moving and idle).
				auto still = dump_scene(c.civ, c.blds, std::vector<DumpUnit>(c.units.begin() + 1, c.units.end()));
				for (int k = 0; k < (t < 0 ? 1200 : t); ++k) still->tick(DT);
				float moved = 0.0f;
				for (size_t k = 1; k < c.units.size(); ++k) {
					const int j = idx(*s, c.units[k].id), jj = idx(*still, c.units[k].id);
					moved = std::max(moved, std::hypot(s->units_.pos_x[j] - still->units_.pos_x[jj], s->units_.pos_z[j] - still->units_.pos_z[jj]));
				}
				crowd_moved = std::max(crowd_moved, moved);
				std::printf("; the crowd stands within %.3f of where it stands without the builder", moved);
			}
			std::printf("\n");
			started += t > 0;
		}
		CHECK(started == (int)crowds.size(), "a builder in a crowd of its player's idle units gets out to its site and builds");
		CHECK(crowd_moved < 0.01f, "and walking through them pushes none of the idle crowd aside");
	}
	{
		// 28. A unit stands on a foundation whose builder stands just outside it. Civ 13's storehouse:
		// the nearest point off the site lies 0.25 from the builder. Civ 9's house: the unit walks to
		// the nearest point off it, inside the barracks beside it, when the builder is sent.
		int started = 0, runs = 0;
		for (int scene = 0; scene < 2; ++scene) {
			auto s = scene == 0
					? dump_scene(CIV_HAN, {cc, house600, barracks700, {935, 37.0f, 43.0f, BLD_STOREHOUSE, 0.0f}},
					             {{3, 37.51297f, 41.15369f, W}, {4, 37.28f, 42.49f, W}, {11, 35.85f, 39.67f, UNIT_MINISTER}})
					: dump_scene(CIV_MAURYANS, {cc, house600, barracks700, house601, {934, 37.0f, 43.0f, BLD_HOUSE, 0.0f}},
					             {{3, 35.825f, 41.181f, W}, {2, 37.11f, 41.79f, W}, {4, 37.46f, 43.14f, W}, {10, 33.09f, 41.58f, UNIT_CAVALRY}});
			const uint32_t builder = 3, on_site = 4, site = scene == 0 ? 935 : 934;
			if (scene == 1) s->order_walk(&on_site, 1, 38.98f, 43.137f);
			s->order_build_or_repair(&builder, 1, site);
			const int t = build_started(*s, site, 600);
			const int u = idx(*s, on_site);
			std::printf("  civ %d's %s: started after %.2f s (-1: not in 30 s); the unit that stood on it is at (%.2f, %.2f)\n",
			            scene == 0 ? 13 : 9, scene == 0 ? "storehouse" : "house", t < 0 ? -1.0f : t * DT, s->units_.pos_x[u], s->units_.pos_z[u]);
			++runs;
			started += t > 0;
		}
		CHECK(started == runs, "a unit on a site leaves it for a point it can get to, and the site starts");
	}
	{
		// 29. Civ 15's barracks: its builder stands inside the footprint with an idle woman by the edge
		// it would leave by, an archer standing on the site too.
		auto s = dump_scene(CIV_GERMANS, {cc, house600, {908, 40.0f, 44.0f, BLD_BARRACKS, 0.0f}},
		                    {{2, 38.53499f, 42.54517f, W}, {3, 37.16f, 41.61f, W}, {9, 40.87f, 42.28f, A}, {11, 37.56f, 39.74f, UNIT_CAVALRY}});
		const uint32_t builder = 2;
		s->order_build_or_repair(&builder, 1, 908);
		const int t = build_started(*s, 908, 600);
		const auto &b = s->buildings_[s->building_id_to_idx_.at(908)];
		int on_site = 0;
		for (const uint32_t id : {2u, 9u}) {
			const int i = idx(*s, id);
			on_site += nav::footprint_contains(b, s->units_.pos_x[i], s->units_.pos_z[i], 0.0f);
		}
		std::printf("  a builder standing on its barracks site: started after %.2f s (-1: not in 30 s), %d of the two units still on it\n",
		            t < 0 ? -1.0f : t * DT, on_site);
		CHECK(t > 0 && on_site == 0, "a builder standing on its site has everyone leave it, itself too, and builds");
	}
	{
		// 30. Civ 4's market builder: the line to its stand point crosses the civic centre, so it walks a
		// path; a second after its build order it is sent to gather wood.
		auto s = dump_scene(CIV_CARTHAGINIANS, {cc, house600, barracks700, {972, 35.0f, 33.0f, BLD_HOUSE, 1.0f}, {1011, 42.0f, 38.0f, BLD_MARKET, 0.0f}},
		                    {{911, 26.295f, 35.489f, W}, {9, 25.08f, 34.15f, A}, {895, 28.45f, 33.47f, W}, {912, 27.72f, 36.57f, W},
		                     {952, 26.44f, 37.19f, A}, {954, 27.27f, 34.25f, A}, {955, 24.64f, 35.50f, A}, {957, 28.27f, 35.48f, A},
		                     {960, 26.12f, 33.15f, A}});
		const uint32_t builder = 911;
		s->order_build_or_repair(&builder, 1, 1011);
		for (int t = 0; t < 20; ++t) s->tick(DT);
		const int i = idx(*s, builder);
		const float *xs = nullptr, *zs = nullptr;
		const int build_path = s->unit_waypoints_left((size_t)i, &xs, &zs);
		const uint32_t tree = s->spawn_resource_world(24.0f, 42.0f, RES_WOOD, 200);
		s->order_gather(&builder, 1, tree);
		const int left = s->unit_waypoints_left((size_t)i, &xs, &zs);
		const bool stale = left > 0 || s->unit_paths_[i].has_goal;
		s->tick(DT);
		// Any path it walks now is the gather walk's own, to the tree.
		const auto &wp = s->unit_paths_[idx(*s, builder)];
		const bool to_tree = !wp.has_goal || std::hypot(wp.goal_x - 24.0f, wp.goal_z - 42.0f) <= 2.0f;
		std::printf("  a builder walking a path to its market: %d waypoints left; sent to gather, %d left of the old path; a tick on, its path "
		            "%s\n", build_path, left, to_tree ? "leads to the tree or there is none" : "still leads to the market");
		CHECK(build_path > 0, "the builder walks a path to its site");
		CHECK(!stale && to_tree, "a gather order drops the path to the site");
	}
	{
		// 31. Spearmen sent across a house site to points 14 beyond it, as a group and as a box formation;
		// once one is on the site its builder, waiting beside it, is sent to build.
		int far = 0, units = 0, started = 0, runs = 0;
		for (int formation = 0; formation < 2; ++formation)
			for (int seed = 0; seed < 6; ++seed) {
				auto s = std::make_unique<Simulation>();
				s->set_sim_seed(5);
				s->reset();
				s->set_map_half(96.0f);
				s->set_team_civ(0, CIV_ATHENIANS);
				s->set_team_civ(1, CIV_SPARTANS);
				s->next_id_ = 5000;
				s->spawn_building_world(-70.0f, -70.0f, BLD_TOWN_CENTER, 1, 1.0f);
				s->next_id_ = 100;
				const uint32_t site = s->spawn_building_world(0.0f, 0.0f, BLD_HOUSE, 0, 0.0f);
				s->rebuild_nav_grid();
				s->next_id_ = 200;
				const uint32_t builder = s->spawn_unit(0.0f, -s->buildings_[s->building_id_to_idx_.at(site)].hv - 0.9f, 0, UNIT_FEMALE_CITIZEN);
				const int n = formation ? 9 : 8;
				std::vector<uint32_t> ids;
				for (int k = 0; k < n; ++k)
					ids.push_back(s->spawn_unit(-10.0f - 1.6f * (k % 2), -1.5f + 3.0f * k / (n - 1) + 0.13f * seed, 0, UNIT_SPEARMAN));
				if (formation) s->order_set_formation(ids.data(), ids.size(), FORMATION_BOX);
				for (int t = 0; t < 5; ++t) s->tick(DT);
				const float gx = 14.0f, gz = 0.3f * seed;
				s->cmd_move(ids.data(), ids.size(), gx, gz);
				bool ordered = false;
				for (int t = 1; t <= 800; ++t) {
					for (size_t k = 0; k < ids.size() && !ordered; ++k) {
						const int i = idx(*s, ids[k]);
						if (nav::footprint_contains(s->buildings_[s->building_id_to_idx_.at(site)], s->units_.pos_x[i], s->units_.pos_z[i], 0.0f)) {
							s->order_build_or_repair(&builder, 1, site);
							ordered = true;
						}
					}
					s->tick(DT);
				}
				for (const uint32_t id : ids) {
					const int i = idx(*s, id);
					far += std::hypot(s->units_.pos_x[i] - gx, s->units_.pos_z[i] - gz) > 6.0f;
				}
				units += n;
				started += ordered && s->buildings_[s->building_id_to_idx_.at(site)].build_progress > 0.0f;
				++runs;
			}
		std::printf("  spearmen across a waiting house site, as a group and as a box: %d of %d end more than 6 from their point; the site "
		            "starts in %d of %d runs\n", far, units, started, runs);
		CHECK(far == 0, "a group or formation walking across a site keeps its walk off it to its point");
		CHECK(started == runs, "and the site starts once they are off it");
	}
	{
		// 32. A dock site on a shore (deep water south of z 16): a woman standing on its land part, the
		// sea the nearest way off it, is sent to build it, alone; or stands there while another woman,
		// sent from the land north of it, builds it.
		int started[2] = {0, 0}, runs[2] = {0, 0};
		for (int run = 0; run < 8; ++run) {
			const int pick = run % 4, role = run / 4;
			auto s = std::make_unique<Simulation>();
			const uint32_t dock = shore_dock(*s, pick);
			if (!dock) continue;
			const Simulation::Building &b = s->buildings_[s->building_id_to_idx_.at(dock)];
			float wx = 0.0f, wz = 0.0f;
			if (!wet_exit_spot(*s, b, wx, wz)) continue;
			const uint32_t woman = s->spawn_unit(wx, wz, 0, UNIT_FEMALE_CITIZEN);
			const uint32_t builder = role == 0 ? woman : s->spawn_unit(b.x, b.z + b.hv + 2.0f, 0, UNIT_FEMALE_CITIZEN);
			s->order_build_or_repair(&builder, 1, dock);
			const int t = build_started(*s, dock, 600);
			std::printf("  a woman on a dock site at (%.2f, %.2f), the sea its nearest way off, %s: started after %.2f s (-1: not in 30 s)\n", wx,
			            wz, role == 0 ? "sent to build it" : "idle, another building it", t < 0 ? -1.0f : t * DT);
			started[role] += t > 0;
			++runs[role];
		}
		CHECK(runs[0] == 4 && started[0] == runs[0], "a builder on its dock site leaves it by land, and the dock starts");
		CHECK(runs[1] == 4 && started[1] == runs[1], "and so does a woman standing on it while another builds it");
	}
	{
		// 33. The same shore, the land round the dock packed with idle women 1.1 apart out to 3 off its
		// edges (none within 1.2 of its builder, beside the middle of its side furthest inland): a woman on
		// its land part, the sea her nearest way off it, is sent off it once the builder is in range.
		int runs = 0, sent = 0, wet = 0;
		for (int pick = 0; pick < 4; ++pick) {
			auto s = std::make_unique<Simulation>();
			const uint32_t dock = shore_dock(*s, pick);
			if (!dock) continue;
			const Simulation::Building &b = s->buildings_[s->building_id_to_idx_.at(dock)];
			float wx = 0.0f, wz = 0.0f;
			if (!wet_exit_spot(*s, b, wx, wz)) continue;
			nav::Vec2 bp = nav::footprint_world(b, 0.0f, b.hv + 0.9f);
			for (const nav::Vec2 c : {nav::footprint_world(b, 0.0f, -b.hv - 0.9f), nav::footprint_world(b, b.hu + 0.9f, 0.0f),
					 nav::footprint_world(b, -b.hu - 0.9f, 0.0f)})
				if (c.y > bp.y) bp = c;
			for (float lu = -b.hu - 3.0f; lu <= b.hu + 3.0f + 1e-4f; lu += 1.1f)
				for (float lv = -b.hv - 3.0f; lv <= b.hv + 3.0f + 1e-4f; lv += 1.1f) {
					if (std::fabs(lu) < b.hu + 0.45f && std::fabs(lv) < b.hv + 0.45f) continue;
					const nav::Vec2 w = nav::footprint_world(b, lu, lv);
					const auto [gx, gy] = s->nav_grid_.world_to_cell(w.x, w.y);
					if (!s->nav_grid_.is_passable_for(gx, gy, PASS_INFANTRY) || std::hypot(w.x - bp.x, w.y - bp.y) < 1.2f) continue;
					s->spawn_unit(w.x, w.y, 0, UNIT_FEMALE_CITIZEN);
				}
			for (int t = 0; t < 20; ++t) s->tick(DT);
			const uint32_t woman = s->spawn_unit(wx, wz, 0, UNIT_FEMALE_CITIZEN);
			const uint32_t builder = s->spawn_unit(bp.x, bp.y, 0, UNIT_FEMALE_CITIZEN);
			s->order_build_or_repair(&builder, 1, dock);
			bool walk = false, at_sea = false;
			for (int t = 0; t < 200; ++t) {
				s->tick(DT);
				const int wi = idx(*s, woman);
				if (s->units_.order_kind[wi] != ORDER_WALK || !s->units_.has_order[wi]) continue;
				walk = true;
				const auto [ox, oy] = s->nav_grid_.world_to_cell(s->units_.order_x[wi], s->units_.order_z[wi]);
				if (!s->nav_grid_.is_passable_for(ox, oy, PASS_INFANTRY)) at_sea = true;
			}
			std::printf("  a woman on a dock site at (%.2f, %.2f), the land round it crowded: sent off it %d, to a point at sea %d\n", wx, wz, walk,
			            at_sea);
			++runs;
			sent += walk;
			wet += at_sea;
		}
		CHECK(runs == 4 && sent == runs && wet == 0, "a woman on a dock site, the land round it crowded, is sent off it to a point on land");
	}
	{
		// 34. A house site laid in a crowd of idle women 1.3 apart (jittered), some of them on it, its
		// builder just south of it: the find_path calls while it waits, 400 turns at most.
		long long paths = 0;
		int waited = 0;
		for (int seed = 0; seed < 3; ++seed) {
			auto s = std::make_unique<Simulation>();
			s->set_sim_seed(5);
			s->reset();
			s->set_map_half(96.0f);
			s->set_team_civ(0, CIV_ATHENIANS);
			s->set_team_civ(1, CIV_SPARTANS);
			s->next_id_ = 5000;
			s->spawn_building_world(-70.0f, -70.0f, BLD_TOWN_CENTER, 1, 1.0f);
			s->next_id_ = 100;
			const uint32_t site = s->spawn_building_world(0.0f, 0.0f, BLD_HOUSE, 0, 0.0f);
			s->rebuild_nav_grid();
			s->next_id_ = 200;
			const float bx = 0.0f, bz = -s->buildings_[s->building_id_to_idx_.at(site)].hv - 0.9f;
			const uint32_t builder = s->spawn_unit(bx, bz, 0, UNIT_FEMALE_CITIZEN);
			uint64_t r = (uint64_t)seed * 7919 + 17;
			auto jitter = [&r]() {
				r = r * 6364136223846793005ULL + 1442695040888963407ULL;
				return ((float)((r >> 33) & 0xFFFFFF) / (float)0x1000000 - 0.5f) * 0.4f * 1.3f;
			};
			for (float z = -5.0f; z <= 5.0f + 1e-3f; z += 1.3f)
				for (float x = -5.0f; x <= 5.0f + 1e-3f; x += 1.3f) {
					const float jx = x + jitter(), jz = z + jitter();
					if (std::hypot(jx - bx, jz - bz) >= 1.3f) s->spawn_unit(jx, jz, 0, UNIT_FEMALE_CITIZEN);
				}
			for (int t = 0; t < 20; ++t) s->tick(DT);
			s->order_build_or_repair(&builder, 1, site);
			const uint64_t p0 = s->map_scale_stats_.path_calls;
			for (int t = 0; t < 400 && !s->buildings_[s->building_id_to_idx_.at(site)].committed; ++t) {
				s->tick(DT);
				++waited;
			}
			paths += (long long)(s->map_scale_stats_.path_calls - p0);
		}
		std::printf("  a house site waiting in a crowd of idle women: %lld find_path calls in %d turns\n", paths, waited);
		CHECK(waited > 0 && 2 * paths < waited, "a site waiting in a crowd costs under one find_path every two turns");
	}
	{
		// 35. Civ 13's market builder 907, a woman, among the soldiers and women the civic centre unloaded
		// (all idle with no order), sent to the market 10 from her; the house beside them is finished (its
		// id is not in the dump). The first random layout moves each unit up to 0.1 off the dump (there the
		// builder walked in place), the second the builder up to 0.3 and the crowd up to 0.15.
		const DumpBld house{2001, 35.0f, 33.0f, BLD_HOUSE, 1.0f}, market{1008, 42.0f, 38.0f, BLD_MARKET, 0.0f};
		const DumpBld storehouse935{935, 37.0f, 43.0f, BLD_STOREHOUSE, 1.0f};
		const std::vector<DumpUnit> dumped = {
				{907, 30.050f, 33.766f, W}, {9, 30.10f, 33.07f, A}, {910, 30.08f, 34.67f, W}, {953, 31.48f, 34.13f, A},
				{960, 29.11f, 34.38f, A}, {8, 29.47f, 36.21f, A}, {908, 28.75f, 32.36f, W}, {955, 27.99f, 35.28f, A},
				{961, 31.08f, 35.68f, A}, {962, 27.56f, 33.19f, A}};
		struct Crowd {
			const char *name;
			std::vector<DumpBld> blds;
			std::vector<DumpUnit> units;
		};
		const std::vector<Crowd> crowds = {
			{"the dumped crowd", {cc, house, market}, dumped},
			{"the dumped crowd, with the village", {cc, house, market, house600, barracks700, storehouse935}, dumped},
			{"a random layout (the builder walked in place)", {cc, house, market},
			 {{907, 30.059f, 33.792f, W}, {9, 30.148f, 33.121f, A}, {910, 29.991f, 34.702f, W}, {953, 31.391f, 34.117f, A},
			  {960, 29.143f, 34.474f, A}, {8, 29.511f, 36.252f, A}, {908, 28.655f, 32.272f, W}, {955, 27.894f, 35.240f, A},
			  {961, 31.071f, 35.664f, A}, {962, 27.644f, 33.181f, A}}},
			{"another random layout", {cc, house, market},
			 {{907, 29.973f, 33.866f, W}, {9, 30.241f, 33.166f, A}, {910, 30.029f, 34.782f, W}, {953, 31.393f, 34.212f, A},
			  {960, 29.174f, 34.387f, A}, {8, 29.331f, 36.122f, A}, {908, 28.769f, 32.228f, W}, {955, 27.862f, 35.345f, A},
			  {961, 31.186f, 35.740f, A}, {962, 27.662f, 33.231f, A}}},
		};
		int started = 0;
		float crowd_moved = 0.0f;
		for (const Crowd &c : crowds) {
			auto s = dump_scene(CIV_HAN, c.blds, c.units);
			const uint32_t builder = c.units[0].id;
			s->order_build_or_repair(&builder, 1, market.id);
			const int t = build_started(*s, market.id, 1200);
			// The crowd stands as it does without the builder (Push never mixes moving and idle).
			auto still = dump_scene(CIV_HAN, c.blds, std::vector<DumpUnit>(c.units.begin() + 1, c.units.end()));
			for (int k = 0; k < (t < 0 ? 1200 : t); ++k) still->tick(DT);
			float moved = 0.0f;
			for (size_t k = 1; k < c.units.size(); ++k) {
				const int j = idx(*s, c.units[k].id), jj = idx(*still, c.units[k].id);
				moved = std::max(moved, std::hypot(s->units_.pos_x[j] - still->units_.pos_x[jj], s->units_.pos_z[j] - still->units_.pos_z[jj]));
			}
			crowd_moved = std::max(crowd_moved, moved);
			std::printf("  civ 13's market builder %u, %s: started the market after %.2f s (-1: not in 60 s); the crowd stands "
			            "within %.3f of where it stands without the builder\n", builder, c.name, t < 0 ? -1.0f : t * DT, moved);
			started += t > 0;
		}
		CHECK(started == (int)crowds.size(),
		      "a builder caught going round a way through its player's idle units gets out to its site and builds");
		CHECK(crowd_moved < 0.01f, "and walking through them pushes none of the idle crowd aside");

		// Builders the rule leaves alone. A woman sent to a house site 14 east past a field 8 women were
		// sent to farm 15 s before and 14 idle archers (layout 284 of the review's probe): she
		// starts after 8.9 s, as on main; a rule that took every refused step along its path as no way gave
		// her the stuck memo each time an archer or a farmer refused her, her searches every fourth turn,
		// and she started after 54 s. Then the civ 13 crowd with every idle unit an ally's (jittered), and
		// 15 units between a woman and a house site, half of them an ally's: 5 s and 9.5 s, as on main,
		// where that rule never started them.
		const DumpBld site{300, 14.0f, 0.0f, BLD_HOUSE, 0.0f}, field{301, 6.26881838f, 0.796465874f, BLD_FIELD, 1.0f};
		auto fs = dump_scene(CIV_HAN, {site, field}, {}, true);
		fs->next_id_ = 100;
		const uint32_t farm_builder = fs->spawn_unit(0.0f, 0.0f, 0, W);
		const float farmers_at[8][2] = {{7.22947407f, 2.03000164f}, {4.73012781f, 2.23629284f}, {6.61265802f, 1.66949511f},
				{3.28137207f, -0.0247612f}, {6.27909851f, 1.85029984f}, {4.91370487f, -0.182667255f}, {6.91441536f, 3.28786373f},
				{5.58300304f, -1.11421967f}};
		std::vector<uint32_t> farmers;
		for (const auto &f : farmers_at) farmers.push_back(fs->spawn_unit(f[0], f[1], 0, W));
		fs->order_build_or_repair(farmers.data(), farmers.size(), field.id);
		for (int t = 0; t < 300; ++t) fs->tick(DT);
		const float archers_at[14][2] = {{7.54665852f, -2.99619341f}, {3.68731618f, 1.54333115f}, {4.53014183f, 3.39012671f},
				{9.9096117f, 1.23626566f}, {3.79911184f, 2.53909636f}, {8.58837318f, -0.810395718f}, {8.24146843f, 1.84519529f},
				{4.17536736f, -1.9223212f}, {8.86035347f, 2.68578339f}, {2.62931013f, 0.925544739f}, {9.94168758f, -2.74841189f},
				{6.67051172f, -0.172542334f}, {2.23531437f, 3.27467918f}, {2.63995051f, -2.67386174f}};
		for (const auto &a : archers_at) fs->spawn_unit(a[0], a[1], 0, A);
		int farming = 0;
		for (uint32_t id : farmers) farming += fs->units_.action_state[idx(*fs, id)] == ACTION_GATHER;
		fs->order_build_or_repair(&farm_builder, 1, site.id);
		const int t_farm = build_started(*fs, site.id, 1200);
		std::printf("  a builder past %d farmers at work and 14 idle archers: started its house after %.2f s (-1: not in 60 s)\n",
		            farming, t_farm < 0 ? -1.0f : t_farm * DT);
		const std::vector<DumpUnit> allied_c13 = {{907, 29.7820988f, 33.7703094f, W}, {9, 30.215168f, 33.0970039f, A, 2},
				{910, 29.930624f, 34.7252197f, W, 2}, {953, 31.4926891f, 34.1534767f, A, 2}, {960, 29.1862965f, 34.3330307f, A, 2},
				{8, 29.4812298f, 36.1443596f, A, 2}, {908, 28.6876202f, 32.2432938f, W, 2}, {955, 28.0986214f, 35.1471519f, A, 2},
				{961, 31.1826897f, 35.7736664f, A, 2}, {962, 27.5079231f, 33.3359146f, A, 2}};
		const std::vector<DumpUnit> mixed = {{100, 0.0f, 0.0f, W}, {200, 6.55340862f, 1.39690113f, W}, {201, 1.7549727f, 0.754629135f, A},
				{202, 6.95329237f, 0.0975604057f, W}, {203, 1.68127561f, -0.604582787f, A}, {204, 1.66632032f, -2.21708608f, A},
				{205, 3.98458982f, -1.55989265f, A, 2}, {206, 4.76575994f, 2.05195141f, A, 2}, {207, 6.35167074f, -1.43217087f, W, 2},
				{208, 7.33451796f, 0.586462975f, W, 2}, {209, 1.75573742f, -0.207315445f, W}, {210, 5.27122021f, 0.209107161f, A, 2},
				{211, 7.33385706f, 2.45013094f, A}, {212, 2.72495556f, 0.156299591f, W}, {213, 2.98021221f, -0.252498627f, W, 2},
				{214, 3.0578475f, 1.87519169f, A}, {215, 4.16156149f, -0.398954868f, W, 2}};
		auto as = dump_scene(CIV_HAN, {cc, house, market}, allied_c13, true);
		const uint32_t c13_builder = 907;
		as->order_build_or_repair(&c13_builder, 1, market.id);
		const int t_allied = build_started(*as, market.id, 1200);
		auto ms = dump_scene(CIV_HAN, {site}, mixed, true);
		const uint32_t mixed_builder = 100;
		ms->order_build_or_repair(&mixed_builder, 1, site.id);
		const int t_mixed = build_started(*ms, site.id, 1200);
		std::printf("  a builder in the civ 13 crowd of an ally's units: started after %.2f s; among own and allied units: "
		            "after %.2f s (-1: not in 60 s)\n", t_allied < 0 ? -1.0f : t_allied * DT, t_mixed < 0 ? -1.0f : t_mixed * DT);
		CHECK(farming >= 4 && t_farm > 0 && t_farm * DT < 15.0f,
		      "a builder refused among farmers at work and idle archers keeps 0.28's rule and starts within 15 s");
		CHECK(t_allied > 0 && t_mixed > 0, "a builder held by an ally's idle units keeps 0.28's rule and gets to its site");

		// A builder the first test must leave alone (layout 253 of the second review's pockets): a spearman
		// behind his player's idle front layer, with an ally's idle units behind it, sent to a house site 14
		// east. His step toward the site is refused and his search finds the way whose path he dropped, but
		// an ally's unit stands by the line to the site, which the seal does not let through. Sealed on the
		// refused step alone, he walked into the crowd and was still walking in place inside an idle
		// elephant's body 60 s later; left to 0.28's rule he starts after 7.85 s, as on main.
		const DumpBld pocket_site{400, 14.0f, 0.0f, BLD_HOUSE, 0.0f};
		const std::vector<DumpUnit> pocket = {{100, -0.261446446f, 0.100291379f, UNIT_SPEARMAN}, {200, 1.86243105f, -1.03404188f, UNIT_CAVALRY},
				{201, 3.11612272f, 0.150206089f, A}, {202, 2.97701645f, -1.97784579f, UNIT_ELEPHANT},
				{203, 1.91776836f, -3.15397692f, A}, {204, 2.51091957f, 1.82539392f, UNIT_CAVALRY},
				{205, 2.18916273f, 0.618999481f, W}, {206, 1.54490662f, -0.129593611f, UNIT_SPEARMAN},
				{207, 3.26924944f, -0.925747395f, A}, {300, 5.27125311f, 2.70855737f, W, 2},
				{301, 6.17623711f, -2.59730315f, UNIT_SPEARMAN, 2}, {302, 4.62031221f, -0.413892031f, A, 2},
				{303, 5.87819719f, 0.163039684f, W, 2}, {304, 4.82304382f, -2.42948484f, W, 2},
				{305, 6.25483847f, 2.00253892f, W, 2}, {306, 6.21160889f, -0.776330948f, W, 2},
				{307, 4.53921318f, -1.43729925f, W, 2}, {308, 4.5368371f, 1.6525104f, A, 2},
				{309, 4.75416851f, 0.551981926f, UNIT_CAVALRY, 2}};
		auto ps = dump_scene(CIV_HAN, {pocket_site}, pocket, true);
		const uint32_t pocket_builder = 100;
		ps->order_build_or_repair(&pocket_builder, 1, pocket_site.id);
		const int t_pocket = build_started(*ps, pocket_site.id, 1200);
		std::printf("  a builder whose line to his site crosses an ally's idle units: started after %.2f s (-1: not in 60 s)\n",
		            t_pocket < 0 ? -1.0f : t_pocket * DT);
		CHECK(t_pocket > 0, "a builder whose line to its site crosses an ally's idle units is not sealed there, and gets to its site");
	}
	{
		// 36. Bahrain seed 3, about 1020 s: p3's grove (the 20 palms the trace listed, 4 m squares but one
		// of 1.6 m) and its storehouse at (57, 67), moved by an even (OX, OZ) into player 0's territory, so
		// the navcells lie as they did. Two women with full wood loads from where units 5055 and 7683 stood
		// in the grove, 18.6 units from the storehouse's centre; the second is ordered to gather again every
		// 32 ticks while she returns. The second game is saved after 20 s and loaded, the third after 3 s,
		// while both women walk the paths their far third turns laid (the first game checks that they do).
		static const float grove[][3] = {
			{67.5f, 51.5f, 0.5f}, {66.5f, 51.5f, 0.5f}, {68.5f, 51.5f, 0.5f}, {67.5f, 52.5f, 0.5f}, {66.5f, 52.5f, 0.5f},
			{69.5f, 52.5f, 0.5f}, {70.5f, 53.5f, 0.5f}, {71.5f, 52.5f, 0.5f}, {73.5f, 53.5f, 0.5f}, {75.5f, 53.5f, 0.2f},
			{67.5f, 55.5f, 0.5f}, {66.5f, 55.5f, 0.5f}, {69.5f, 55.5f, 0.5f}, {70.5f, 55.5f, 0.5f}, {70.5f, 54.5f, 0.5f},
			{71.5f, 55.5f, 0.5f}, {72.5f, 54.5f, 0.5f}, {74.5f, 54.5f, 0.5f}, {71.5f, 56.5f, 0.5f}, {72.5f, 57.5f, 0.5f}};
		static const float start[2][2] = {{71.11f, 54.83f}, {70.79f, 52.79f}};
		constexpr float OX = -96.0f, OZ = -102.0f;
		constexpr int SAVE_AT[3] = {0, 400, 60}, HASH_AT = 1200, TICKS = 2400;
		int drop[2] = {-1, -1}, installs = 0, short_ends = 0;
		uint32_t hash[3] = {0, 0, 0};
		bool loaded[3] = {true, false, false};
		// At SAVE_AT[2] in the first game: each woman, still full, walks the last path a far third turn laid
		// her, with waypoints left.
		bool mid_far[2] = {false, false};
		WaypointPath far_laid[2];
		for (int run = 0; run < 3; ++run) {
			auto s = std::make_unique<Simulation>();
			setup(*s);
			const uint32_t store = s->spawn_building_world(57.0f + OX, 67.0f + OZ, BLD_STOREHOUSE, 0, 1.0f);
			for (const auto &p : grove)
				s->spawn_resource_world(p[0] + OX, p[1] + OZ, RES_WOOD, 300, RES_SUB_NONE, 0,
						p[2] < 0.3f ? FLORA_TREE_DATE_PALM : FLORA_TREE_CRETAN_DATE_PALM_PATCH);
			// The tree the women are sent to, beyond the grove; a woman far off, as separation() runs only
			// with two units or more.
			const uint32_t tree = s->spawn_resource_world(79.5f + OX, 55.5f + OZ, RES_WOOD, 300, RES_SUB_NONE, 0, FLORA_TREE_DATE_PALM);
			s->spawn_unit(-10.0f, -10.0f, 0, UNIT_FEMALE_CITIZEN, 150.0f);
			uint32_t ws[2];
			int32_t load[2];
			for (int k = 0; k < 2; ++k) {
				ws[k] = s->spawn_unit(start[k][0] + OX, start[k][1] + OZ, 0, UNIT_FEMALE_CITIZEN, 150.0f);
				const int wi = idx(*s, ws[k]);
				s->units_.res_type_carried[wi] = RES_WOOD;
				s->units_.carry[wi] = load[k] = s->carry_cap((size_t)wi, RES_WOOD);
			}
			s->order_gather(ws, 2, tree); // a full worker returns the load first
			WaypointPath prev[2];
			for (int t = 1; t <= (run == 0 ? TICKS : HASH_AT); ++t) {
				if (run > 0 && t == SAVE_AT[run] + 1) {
					const std::string path = temp_save_path();
					auto copy = std::make_unique<Simulation>();
					loaded[run] = !path.empty() && s->save_state(path.c_str()) && copy->load_state(path.c_str());
					std::remove(path.c_str());
					if (!loaded[run]) break;
					s = std::move(copy); // the loaded game goes on in its place
				}
				const int i1 = idx(*s, ws[1]);
				if (t % 32 == 0 && s->units_.action_state[i1] == ACTION_RETURN_DROPSITE) s->order_gather(&ws[1], 1, tree);
				s->tick(DT);
				if (t == HASH_AT) hash[run] = TurnManager().compute_sim_hash(*s);
				if (run != 0) continue;
				const Simulation::Building &b = s->buildings_[s->building_id_to_idx_.at(store)];
				for (int k = 0; k < 2; ++k) {
					const int i = idx(*s, ws[k]);
					if (drop[k] < 0 && s->units_.carry[i] < load[k]) drop[k] = t;
					const WaypointPath &wp = s->unit_paths_[i];
					if (drop[k] < 0 && s->units_.action_state[i] == ACTION_RETURN_DROPSITE && wp.count > 0 &&
							!same_path(wp, prev[k]) && far_alternate_turn(*s, i, b.x, b.z)) {
						// D2: the path ends in the range (within a navcell) or at the storehouse.
						const float lx = wp.xs[wp.count - 1], lz = wp.zs[wp.count - 1];
						++installs;
						far_laid[k] = wp;
						if (drop_range_gap(b, s->units_.unit_type[i], lx, lz) > 2.0f &&
								(lx != s->units_.target_x[i] || lz != s->units_.target_z[i]))
							++short_ends;
					}
					if (t == SAVE_AT[2])
						mid_far[k] = drop[k] < 0 && s->units_.carry[i] == load[k] && far_laid[k].count > 0 &&
								same_path(wp, far_laid[k]) && s->units_.waypoint_cur[i] < wp.count;
					prev[k] = wp;
				}
			}
		}
		std::printf("  palm grove 75 m from the storehouse (Stats, bahrain seed 3): unit 5055's spot drops after %.2f s, 7683's "
		            "(ordered to gather every 1.6 s) after %.2f s (-1: not in 120 s); %d paths laid on far third turns, %d of "
		            "them ending short with no way on; after %.2f s each walks the far path laid her: %d %d; hash after 60 s %08x, "
		            "saved after 20 s and loaded %08x, saved after %.2f s and loaded %08x\n",
		            drop[0] < 0 ? -1.0f : drop[0] * DT, drop[1] < 0 ? -1.0f : drop[1] * DT, installs, short_ends, SAVE_AT[2] * DT,
		            (int)mid_far[0], (int)mid_far[1], hash[0], hash[1], SAVE_AT[2] * DT, hash[2]);
		CHECK(drop[0] > 0 && drop[1] > 0,
		      "workers in a palm grove 75 m from their storehouse, one ordered to gather again every 1.6 s, get round it and drop their loads");
		CHECK(short_ends == 0, "every path a far third turn lays them ends in the storehouse's range or at the storehouse");
		CHECK(loaded[1] && hash[0] == hash[1], "a game saved on the way and loaded hashes as the original 40 s later");
		CHECK(mid_far[0] && mid_far[1] && loaded[2] && hash[0] == hash[2],
		      "and so does one saved while both women walk the paths their far third turns laid");
	}
	{
		// 37. Walls W1 (x = 4) and W2 (x = 26) of 21 palms each, z -10 to 10, 1 unit apart; their point 40
		// units east of the start, moved by (OX, OZ). The first far search, from W1's west face, shifts its
		// box toward the goal by 8.4 units and clamps the goal into it by W2. Player 0's civic centre stands
		// 25 units beyond the storehouse, so the storehouse is the returners' pick and in the territory.
		// SUBGOAL: W1 alone, two fortresses at (18, -8) and (18, 0), and a walk to (65, 0), so the walk's
		// own path (cmd_move's find_path, which does not see the palms) bends round them at (21, 3).
		constexpr float OX = -40.0f, OZ = -40.0f, GX = 40.0f + OX, GZ = OZ, SUB_GX = 65.0f + OX;
		// SPENT: as RESENT, but she starts on a path to the storehouse walked to its end, which ends where she
		// stands (as a search's path does once she has been pushed back from its end).
		enum Run { WALK, RETURNER, RESENT, SPENT, SUBGOAL, RUNS };
		static const char *const names[RUNS] = {"a spearman walked to the point", "a woman taking a full load to a storehouse there",
			"the woman, ordered to gather again every 1.6 s",
			"the woman, ordered to gather again every 1.6 s, holding a path walked to its end where she stands",
			"a spearman walked 65 units, his path bent by two fortresses"};
		int done[RUNS], max_failed[RUNS], first_n[RUNS];
		float first_x[RUNS], first_z[RUNS], end_x[RUNS], end_z[RUNS];
		bool first_round[RUNS];
		// SUBGOAL's first third-turn path: whether it ends with the rest of the path the walk held, and how far
		// the waypoint before that rest (the search's end) lies from the held path's next waypoint.
		bool sub_tail = false;
		float sub_gap = -1.0f;
		int sub_held_n = 0;
		for (int run = 0; run < RUNS; ++run) {
			const float gx = run == SUBGOAL ? SUB_GX : GX;
			auto s = std::make_unique<Simulation>();
			s->set_sim_seed(4242);
			s->reset();
			s->spawn_building_world(65.0f + OX, run == SUBGOAL ? 30.0f + OZ : OZ, BLD_TOWN_CENTER, 0, 1.0f);
			s->spawn_building_world(52.0f, 52.0f, BLD_TOWN_CENTER, 1, 1.0f);
			for (int k = -10; k <= 10; ++k) {
				s->spawn_resource_world(4.0f + OX, (float)k + OZ, RES_WOOD, 300, RES_SUB_NONE, 0, FLORA_TREE_CRETAN_DATE_PALM_PATCH);
				if (run != SUBGOAL)
					s->spawn_resource_world(26.0f + OX, (float)k + OZ, RES_WOOD, 300, RES_SUB_NONE, 0, FLORA_TREE_CRETAN_DATE_PALM_PATCH);
			}
			if (run == SUBGOAL)
				for (float fz : {-8.0f, 0.0f}) s->spawn_building_world(18.0f + OX, fz + OZ, BLD_FORTRESS, 0, 1.0f);
			// The point the way on leads to: the walk's own, or the storehouse's centre.
			end_x[run] = gx;
			end_z[run] = GZ;
			uint32_t store_id = 0;
			if (run == RETURNER || run == RESENT || run == SPENT) {
				store_id = s->spawn_building_world(GX, GZ, BLD_STOREHOUSE, 0, 1.0f);
				const Simulation::Building &b = s->buildings_[s->building_id_to_idx_.at(store_id)];
				end_x[run] = b.x;
				end_z[run] = b.z;
			}
			const uint32_t tree = s->spawn_resource_world(OX - 6.0f, OZ, RES_WOOD, 300); // the gather order's tree, west
			s->spawn_unit(65.0f + OX, 20.0f + OZ, 0, UNIT_FEMALE_CITIZEN, 150.0f); // separation() runs with two units
			const uint32_t w = s->spawn_unit(OX, OZ, 0, run == WALK || run == SUBGOAL ? UNIT_SPEARMAN : UNIT_FEMALE_CITIZEN, 150.0f);
			int32_t load = 0;
			if (run == WALK || run == SUBGOAL) {
				s->order_walk(&w, 1, gx, GZ, false, true);
				if (run == SUBGOAL) sub_held_n = s->unit_paths_[idx(*s, w)].count;
			} else {
				const int wi = idx(*s, w);
				s->units_.res_type_carried[wi] = RES_WOOD;
				s->units_.carry[wi] = load = s->carry_cap((size_t)wi, RES_WOOD);
				s->order_gather(&w, 1, tree); // a full worker returns the load first
				if (run == SPENT) {
					s->tick(DT); // tick_gather turns her back to the storehouse
					const int i = idx(*s, w);
					WaypointPath &wp = s->unit_paths_[i];
					wp.clear();
					wp.push_back(s->units_.pos_x[i], s->units_.pos_z[i]);
					wp.has_goal = true;
					wp.goal_x = s->units_.target_x[i];
					wp.goal_z = s->units_.target_z[i];
					wp.cur = 1;
					s->units_.waypoint_cur[i] = 1;
					s->units_.waypoint_count[i] = 1;
					std::printf("  the path she holds ends %.2f units from the storehouse's range\n",
					            drop_range_gap(s->buildings_[s->building_id_to_idx_.at(store_id)], s->units_.unit_type[i], wp.xs[0], wp.zs[0]));
				}
			}
			done[run] = -1;
			max_failed[run] = 0;
			first_n[run] = 0;
			first_x[run] = first_z[run] = 0.0f;
			first_round[run] = false;
			WaypointPath prev = s->unit_paths_[idx(*s, w)];
			for (int t = 1; t <= 1800 && done[run] < 0; ++t) {
				const int i0 = idx(*s, w);
				if ((run == RESENT || run == SPENT) && t % 32 == 0 && s->units_.action_state[i0] == ACTION_RETURN_DROPSITE)
					s->order_gather(&w, 1, tree);
				const int prev_cur = s->units_.waypoint_cur[i0];
				s->tick(DT);
				const int i = idx(*s, w);
				max_failed[run] = std::max<int>(max_failed[run], s->units_.motion[i].failed);
				if (run == WALK || run == SUBGOAL ? std::hypot(s->units_.pos_x[i] - gx, s->units_.pos_z[i] - GZ) <= Simulation::ARRIVE_DIST
				                                  : s->units_.carry[i] < load)
					done[run] = t;
				// The first path a far third turn lays that goes round W1 (a waypoint past its end) and has a
				// way on after it.
				const WaypointPath &wp = s->unit_paths_[i];
				if (first_n[run] == 0 && wp.count >= 2 && !same_path(wp, prev) && far_alternate_turn(*s, i, gx, GZ)) {
					first_n[run] = wp.count;
					first_x[run] = wp.xs[wp.count - 1];
					first_z[run] = wp.zs[wp.count - 1];
					for (int k = 0; k + 1 < wp.count; ++k) first_round[run] = first_round[run] || std::fabs(wp.zs[k] - OZ) >= 10.5f;
					const int rest = prev.count - prev_cur; // the held path's waypoints from its next one on
					if (run == SUBGOAL && prev_cur >= 0 && rest >= 1 && wp.count > rest) {
						sub_tail = true;
						for (int k = 0; k < rest; ++k)
							sub_tail = sub_tail && wp.xs[wp.count - rest + k] == prev.xs[prev_cur + k] &&
									wp.zs[wp.count - rest + k] == prev.zs[prev_cur + k];
						const int e = wp.count - rest - 1;
						sub_gap = std::hypot(wp.xs[e] - prev.xs[prev_cur], wp.zs[e] - prev.zs[prev_cur]);
					}
				}
				prev = wp;
			}
			std::printf("  %s: %s: %s after %.2f s (-1: not in 90 s), failed turns at most %d; "
			            "first far path %d waypoints, round W1 %d, ending at (%.2f, %.2f)\n",
			            run == SUBGOAL ? "a palm wall, the point 65 units off" : "palm walls 22 apart, the point 40 units off",
			            names[run], run == WALK || run == SUBGOAL ? "arrives" : "drops the load", done[run] < 0 ? -1.0f : done[run] * DT,
			            max_failed[run], first_n[run], (int)first_round[run], first_x[run] - OX, first_z[run] - OZ);
		}
		std::printf("  the bent walk held a path of %d waypoints; its first third-turn path ends with the rest of it %d, the search's "
		            "end %.3f from the held path's next waypoint (-1: no such path)\n", sub_held_n, (int)sub_tail, sub_gap);
		CHECK(done[WALK] > 0 && done[WALK] * DT <= 60.0f, "a walk to a point beyond two palm walls gets there within 60 s");
		CHECK(done[RETURNER] > 0 && done[RETURNER] * DT <= 60.0f && max_failed[RETURNER] <= 10,
		      "a returner gets its load to a storehouse beyond them within 60 s, never 10 turns obstructed in a row");
		for (int run : {(int)WALK, (int)RETURNER})
			CHECK(first_round[run] && first_x[run] == end_x[run] && first_z[run] == end_z[run],
			      "the first far path round the near wall, cut short by the search box, keeps the way on to the point");
		CHECK(done[RESENT] > 0 && done[RESENT] * DT <= 90.0f, "and so does a returner ordered to gather again every 1.6 s, within 90 s");
		CHECK(done[SPENT] > 0 && done[SPENT] * DT <= 60.0f,
		      "her order drops a path to the storehouse walked to its end short of it, and she drops her load within 60 s");
		CHECK(done[SUBGOAL] > 0 && done[SUBGOAL] * DT <= 60.0f, "a walk 65 units off whose path bends past a palm wall gets there within 60 s");
		CHECK(sub_held_n >= 2 && first_round[SUBGOAL] && sub_tail && std::fabs(sub_gap - 1.0f) <= 0.01f,
		      "its third failed turn lays a way round the wall to the circle round its path's next waypoint, the rest of that path after it");
		// RANGE: the held path's next waypoint d units east of him, past a palm wall of half-height h.
		int range_ok = 0, range_n = 0;
		for (float d : {2.5f, 3.0f, 4.0f})
			for (int h : {2, 3, 4}) {
				const std::vector<nav::Vec2> held = {nav::Vec2(d, 0.0f), nav::Vec2(60.0f, 0.0f)};
				range_ok += ends_on_circle_with_rest(third_turn_path(h, held), held, 0);
				++range_n;
			}
		// STEP: he stands inside the circle round his next waypoint (0.5, 0), with (3, 0) after it, or only
		// his point.
		const std::vector<nav::Vec2> step_sub = {nav::Vec2(0.5f, 0.0f), nav::Vec2(3.0f, 0.0f), nav::Vec2(60.0f, 0.0f)};
		const std::vector<nav::Vec2> step_far = {nav::Vec2(0.5f, 0.0f), nav::Vec2(60.0f, 0.0f)};
		const WaypointPath sub_path = third_turn_path(3, step_sub), far_path = third_turn_path(3, step_far);
		const bool step_sub_ok = ends_on_circle_with_rest(sub_path, step_sub, 1);
		const bool step_far_ok = far_path.count >= 3 && far_path.xs[far_path.count - 1] == 60.0f && far_path.zs[far_path.count - 1] == 0.0f &&
				far_path.xs[far_path.count - 2] > 2.0f;
		std::printf("  a palm wall 1.5 units east, the point 60 units east, the third failed turn's search on a path set by hand: "
		            "the next waypoint 2.5-4 units east, past the wall, %d of %d paths end on its circle with the rest kept; standing "
		            "inside the circle round the next waypoint, %d waypoints to the circle round the one after it (%d), %d "
		            "waypoints to the point by a far stretch ending (%.2f, %.2f) (%d)\n",
		            range_ok, range_n, sub_path.count, (int)step_sub_ok, far_path.count,
		            far_path.count >= 2 ? far_path.xs[far_path.count - 2] : 0.0f, far_path.count >= 2 ? far_path.zs[far_path.count - 2] : 0.0f,
		            (int)step_far_ok);
		CHECK(range_n > 0 && range_ok == range_n,
		      "the search reaches 0.28's 20 m, so a waypoint just past a palm wall gets a way to its circle, the rest kept after it");
		CHECK(step_sub_ok && step_far_ok,
		      "a unit inside the circle round its next waypoint searches to the one after it, or past the last but one takes the far search");
	}
	{
		// 38. The shore round the spearman (Islands, seed 2, at 1316 s): the game's 2-unit navcells, centres x 39
		// to 59 (columns) and z 79 down to 63 (rows); # deep water, ~ shallow, ^ steep, . land. Land beyond.
		static const char *SHORE[] = {
				".....^.....", ".....###...", ".~~~####~..", "########~..", "#######~~..",
				"#######~...", "######~....", "#####~~....", "####~......"};
		constexpr int SHORE_X0 = 39, SHORE_Z0 = 79, SHORE_W = 11, SHORE_H = 9;
		// A navcell's class comes from the height at its centre (NavGrid::set_water_from_heights): deep water
		// below the water's 1.0 less 2 m plus the margin, shallow up to the margin above it. Heights on each
		// side of a centre are the mean of the centres round them, so no tile but a ^ one is steep.
		auto centre = [&](int x, int z) {
			if (x < SHORE_X0 || x > SHORE_X0 + 2 * (SHORE_W - 1) || z > SHORE_Z0 || z < SHORE_Z0 - 2 * (SHORE_H - 1)) return 1.75f;
			const char c = SHORE[(SHORE_Z0 - z) / 2][(x - SHORE_X0) / 2];
			return c == '#' ? 0.5f : c == '~' ? 1.05f : c == '^' ? 4.0f : 1.75f;
		};
		constexpr UnitType J = UNIT_ARCHER, S = UNIT_SPEARMAN; // Gaul javelineers and spearmen
		const std::vector<DumpUnit> strip = {{1292, 55.4050f, 72.2584f, J}, {882, 54.2004f, 73.0709f, J}, {1143, 57.4113f, 72.3859f, J},
				{1294, 57.3094f, 71.0474f, J}, {1228, 58.8601f, 71.7921f, J}, {1185, 55.1814f, 74.3980f, S},
				{1713, 56.9905f, 73.9893f, S}, {1288, 58.6060f, 73.5643f, S}, {908, 58.1683f, 76.5368f, S},
				{880, 56.9448f, 77.5362f, J}, {791, 54.2149f, 77.9698f, S}};
		// The scene: player 0 Gaul, the strip's units player `crowd` (2 an ally), the spearman at (53.87, 71.58)
		// unless `no_walker`.
		auto shore_scene = [&](uint8_t crowd, bool no_walker) {
			auto s = std::make_unique<Simulation>();
			s->set_sim_seed(5);
			s->reset();
			s->set_map_half(128.0f);
			constexpr int RES = 257;
			std::vector<float> h((size_t)RES * RES);
			for (int j = 0; j < RES; ++j)
				for (int i = 0; i < RES; ++i) {
					const int x = i - 128, z = j - 128;
					const bool ox = (x & 1) != 0, oz = (z & 1) != 0;
					h[(size_t)j * RES + i] = ox && oz ? centre(x, z)
					                       : ox      ? 0.5f * (centre(x, z - 1) + centre(x, z + 1))
					                       : oz      ? 0.5f * (centre(x - 1, z) + centre(x + 1, z))
					                                 : 0.25f * (centre(x - 1, z - 1) + centre(x + 1, z - 1) + centre(x - 1, z + 1) +
					                                            centre(x + 1, z + 1));
				}
			s->set_terrain_data(h, RES, 1.0f);
			s->begin_bulk_spawn();
			s->end_bulk_spawn();
			s->set_team_civ(0, CIV_GAULS);
			s->set_team_civ(1, CIV_SPARTANS);
			s->set_team_civ(2, CIV_GAULS);
			s->set_mutual_ally(0, 2);
			s->next_id_ = 5000;
			s->spawn_building_world(-70.0f, -70.0f, BLD_TOWN_CENTER, 1, 1.0f);
			s->next_id_ = 5100;
			s->spawn_building_world(-70.0f, 70.0f, BLD_TOWN_CENTER, 2, 1.0f);
			s->next_id_ = 972;
			s->spawn_building_world(44.0f, 82.0f, BLD_TOWN_CENTER, 0, 1.0f);
			s->next_id_ = 1044;
			s->spawn_building_world(62.0f, 72.0f, BLD_BARRACKS, 0, 1.0f);
			s->rebuild_nav_grid();
			for (const DumpUnit &u : strip) {
				s->next_id_ = u.id;
				s->spawn_unit(u.x, u.z, crowd, u.type);
			}
			if (!no_walker) {
				s->next_id_ = 812;
				s->spawn_unit(53.8745f, 71.5770f, 0, UNIT_SPEARMAN);
			}
			s->next_id_ = 6000;
			return s;
		};
		struct ShoreRun {
			int in = -1;        // the tick it got into the civic centre, -1 not in 30 s
			float body = 1e9f;  // its least distance to a strip unit, over their bodies' radius sum
		};
		auto shore_run = [&](Simulation &s) {
			ShoreRun r;
			const uint32_t w = 812, cc = 972;
			s.order_garrison(&w, 1, cc); // before the first tick: two idle units would push each other
			for (int t = 1; t <= 600; ++t) {
				s.tick(DT);
				const int wi = idx(s, w);
				if (s.units_.action_state[wi] == ACTION_GARRISONED) {
					r.in = t;
					break;
				}
				for (const DumpUnit &u : strip) {
					const int j = idx(s, u.id);
					r.body = std::min(r.body, dist(s, wi, j) / (unit_collision_radius(s.units_.unit_type[wi]) +
							unit_collision_radius(s.units_.unit_type[j])));
				}
			}
			return r;
		};
		auto own = shore_scene(0, false);
		const int w0 = idx(*own, 812);
		auto cell_at = [&](float x, float z) {
			const auto [cx, cy] = own->nav_grid_.world_to_cell(x, z);
			return (int)own->nav_grid_.cells.at(cx, cy);
		};
		const float wx = own->units_.pos_x[w0], wz = own->units_.pos_z[w0];
		const bool laid = cell_at(wx, wz) == NAV_SHALLOW && cell_at(wx, wz + 1.0f) == NAV_WATER &&
				own->land_step_barred(wx, wz, wx, wz + 0.5f, own->unit_clearance_at(w0));
		const ShoreRun r_own = shore_run(*own);
		// The crowd stands as it does without the spearman (Push never mixes moving and idle).
		auto still = shore_scene(0, true);
		for (int k = 0; k < (r_own.in < 0 ? 600 : r_own.in); ++k) still->tick(DT);
		float moved = 0.0f;
		for (const DumpUnit &u : strip) {
			const int j = idx(*own, u.id), jj = idx(*still, u.id);
			moved = std::max(moved, std::hypot(own->units_.pos_x[j] - still->units_.pos_x[jj], own->units_.pos_z[j] - still->units_.pos_z[jj]));
		}
		auto ally = shore_scene(2, false);
		const ShoreRun r_ally = shore_run(*ally);
		std::printf("  a spearman on the bay's shore sent into its civic centre past its idle army: in after %.2f s (-1: not in 30 s), "
		            "the crowd within %.3f of where it stands without him; past an ally's: in after %.2f s, %.2f of the bodies' radius "
		            "sum from them at the closest\n", r_own.in < 0 ? -1.0f : r_own.in * DT, moved, r_ally.in < 0 ? -1.0f : r_ally.in * DT,
		            r_ally.body);
		CHECK(laid, "the spearman stands in the shallows, deep water north of him: no step north");
		CHECK(r_own.in > 0, "a unit on its way into a structure, its way shut by its player's idle units, gets through them and in");
		CHECK(r_own.in > 0 && moved < 0.01f, "and walking through them pushes none of the idle crowd aside");
		CHECK(r_ally.body >= 0.99f, "an ally's idle units keep 0.28's rule: the unit never walks through their bodies");

		// Guards (the review of a first form of the rule, which walked a path that its sealed search found by
		// 0.28's rule): on test 22's flat map, a spearman 6 south of a block of his player's idle spearmen
		// (cols x 3, 1.45 apart, centred 4 north of him), 2 rows of an ally's spearmen behind it (2 wider),
		// sent into a civic centre 18 north of him, a way round both on either side. Sealed at the block's
		// edge, he walked into it, was refused by the ally's rows and searched sealed; walked by 0.28's rule
		// the path's next step was refused by his own units, the search from there found no way and the seal
		// sent him back at the ally's rows, for good (never in after 300 s; main, where the seal never takes
		// a garrison walk, takes him round in 10.00 s with 7 columns and 13.50 s with 12). Now he follows that
		// path sealed, and gets in about when main does, never through an ally's body. A builder sent to a
		// house site there, which main's seal holds the same way (never started after 300 s), starts.
		struct MixedRun {
			int in = -1;            // the tick it got in (garrisoned, or the house begun), -1 not in 30 s
			float ally = 1e9f;      // its least distance to an ally's unit, over their bodies' radius sum
		};
		auto mixed_run = [&](int cols, bool build) {
			std::vector<DumpUnit> units;
			const float sp = 1.45f, x0 = -0.5f * sp * (cols - 1), z0 = 4.0f - sp;
			uint32_t id = 1000;
			for (int r = 0; r < 3; ++r)
				for (int k = 0; k < cols; ++k) units.push_back({id++, x0 + k * sp, z0 + r * sp, S});
			for (int r = 0; r < 2; ++r)
				for (int k = 0; k < cols + 2; ++k) units.push_back({id++, x0 + (k - 1) * sp, z0 + (3 + r) * sp, S, 2});
			units.push_back({3000, 0.0f, -6.0f, S});
			const DumpBld target = build ? DumpBld{900, 0.0f, 16.0f, BLD_HOUSE, 0.0f} : DumpBld{900, 0.0f, 18.0f, BLD_TOWN_CENTER, 1.0f};
			auto s = dump_scene(CIV_ATHENIANS, {target}, units, true);
			const uint32_t w = 3000;
			if (build) s->order_build_or_repair(&w, 1, target.id);
			else s->order_garrison(&w, 1, target.id);
			MixedRun r;
			for (int t = 1; t <= 600; ++t) {
				s->tick(DT);
				const int wi = idx(*s, w);
				if (build ? s->buildings_[s->building_id_to_idx_.at(target.id)].build_progress > 0.0f
				          : s->units_.action_state[wi] == ACTION_GARRISONED) {
					r.in = t;
					break;
				}
				for (const DumpUnit &u : units) {
					if (u.team != 2) continue;
					const int j = idx(*s, u.id);
					r.ally = std::min(r.ally, dist(*s, wi, j) / (unit_collision_radius(s->units_.unit_type[wi]) +
							unit_collision_radius(s->units_.unit_type[j])));
				}
			}
			return r;
		};
		const MixedRun m7 = mixed_run(7, false), m12 = mixed_run(12, false), mb = mixed_run(7, true);
		std::printf("  past his idle block and an ally's rows: 7 columns in after %.2f s (main 10.00), %.2f of the bodies' radius sum "
		            "from the ally's units at the closest; 12 columns in after %.2f s (main 13.50), %.2f; a builder started after %.2f s "
		            "(-1: not in 30 s)\n", m7.in < 0 ? -1.0f : m7.in * DT, m7.ally, m12.in < 0 ? -1.0f : m12.in * DT, m12.ally,
		            mb.in < 0 ? -1.0f : mb.in * DT);
		CHECK(m7.in > 0 && m7.in * DT <= 12.0f && m12.in > 0 && m12.in * DT <= 15.5f,
		      "a unit sealed past its idle units, refused by an ally's behind them, gets in within 2 s of main's time");
		CHECK(m7.ally >= 0.99f && m12.ally >= 0.99f, "and never walks through the ally's bodies there");
		CHECK(mb.in > 0, "a builder held so by main's seal gets to its site");
	}
	std::printf(failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", failures);
	return failures ? 1 : 0;
}
