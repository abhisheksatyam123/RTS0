// Tests for src/simulation/nav/rasterize.h (port of 0 A.D. SimRasterize::RasterizeRectWithClearance,
// GPL-2.0-or-later).
// Build: g++ -std=c++20 -O2 -Isrc tools/test_rasterize.cpp -o /tmp/test_rasterize && /tmp/test_rasterize
//
// Oracle: a brute-force, double-precision restatement of the vendor rule. A navcell is covered iff every
// point of it lies within R = clearance + 1 of the solid rectangle (the vendor adds CLEARANCE_EXTENSION_RADIUS
// = 1, Rasterize.cpp:48). The oracle does not use geometry.h or the vendor's row walk. It checks the four
// corners and a 5x5 lattice per cell, so it does not depend on the convexity argument the row walk uses.
// Grid-aligned building check: the footprint block that Simulation::rebuild_nav_grid used to mark
// (simulation.cpp, footprint loop) is replicated here with the real NavGrid::world_to_cell.

#include "simulation/grid_nav.h"
#include "simulation/nav/rasterize.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <utility>
#include <vector>

using rtso::nav::Vec2;

namespace {

constexpr double kTol = 1e-3;      // float-vs-double ambiguity band for random cases (distance units)
constexpr double kVendorExt = 1.0; // vendor CLEARANCE_EXTENSION_RADIUS, Pathfinding.h:160

// Rounds to float so the oracle sees exactly the values the port receives.
double fl(double v) { return static_cast<double>(static_cast<float>(v)); }

// One rasterization problem. All fields are float-representable.
struct Cfg {
	double cx, cz;          // centre
	double ux, uz, vx, vz;  // axes
	double hw, hh;          // half sizes (un-expanded)
	double clearance;
	double cs;              // navcell side
	double ox, oz;          // world position of the grid's (0,0) corner
	int w, h;
};

Cfg makeCfg(double cx, double cz, double ux, double uz, double vx, double vz, double hw, double hh,
	double clearance, double cs, double ox, double oz, int w, int h)
{
	return Cfg{fl(cx), fl(cz), fl(ux), fl(uz), fl(vx), fl(vz), fl(hw), fl(hh),
		fl(clearance), fl(cs), fl(ox), fl(oz), w, h};
}

enum class Verdict { In, Out, Unsure };

// exact = true: compare squared distance to R^2 with no tolerance (dyadic inputs are exact in double).
Verdict classify(double d2, double R, bool exact)
{
	if (exact)
		return d2 <= R * R ? Verdict::In : Verdict::Out;
	double d = std::sqrt(d2);
	if (d > R + kTol)
		return Verdict::Out;
	if (d <= R - kTol)
		return Verdict::In;
	return Verdict::Unsure;
}

// Squared distance from a world point to the solid rectangle, in double, from its own axes.
double sqDistToSolid(const Cfg& c, double px, double pz)
{
	double dx = px - c.cx, dz = pz - c.cz;
	double pu = dx * c.ux + dz * c.uz;
	double pv = dx * c.vx + dz * c.vz;
	double eu = std::max(std::fabs(pu) - c.hw, 0.0);
	double ev = std::max(std::fabs(pv) - c.hh, 0.0);
	return eu * eu + ev * ev;
}

// Vendor rule for navcell (i,j): covered iff the whole cell is within radius R of the solid rectangle.
// R = clearance + extension; the extension is kVendorExt unless the caller overrides R.
Verdict oracleCellR(const Cfg& c, int i, int j, bool exact, double R)
{
	bool unsure = false;
	for (int a = 0; a <= 4; ++a) {
		for (int b = 0; b <= 4; ++b) {
			double x = c.ox + c.cs * (i + a / 4.0);
			double z = c.oz + c.cs * (j + b / 4.0);
			Verdict v = classify(sqDistToSolid(c, x, z), R, exact);
			if (v == Verdict::Out)
				return Verdict::Out;
			if (v == Verdict::Unsure)
				unsure = true;
		}
	}
	return unsure ? Verdict::Unsure : Verdict::In;
}

Verdict oracleCell(const Cfg& c, int i, int j, bool exact)
{
	return oracleCellR(c, i, j, exact, c.clearance + kVendorExt);
}

// Marks produced by the port, one counter per in-grid cell (index j*w + i).
struct Run {
	std::vector<int> marks;
	int outOfRange = 0;
};

Run runPort(const Cfg& c)
{
	Run r;
	r.marks.assign(static_cast<size_t>(c.w) * static_cast<size_t>(c.h), 0);
	rtso::nav::rasterize_rect_with_clearance(
		Vec2(static_cast<float>(c.cx), static_cast<float>(c.cz)),
		Vec2(static_cast<float>(c.ux), static_cast<float>(c.uz)),
		Vec2(static_cast<float>(c.vx), static_cast<float>(c.vz)),
		Vec2(static_cast<float>(c.hw), static_cast<float>(c.hh)),
		static_cast<float>(c.clearance), static_cast<float>(c.cs),
		Vec2(static_cast<float>(c.ox), static_cast<float>(c.oz)),
		c.w, c.h,
		[&](int i, int j) {
			if (i < 0 || i >= c.w || j < 0 || j >= c.h) {
				++r.outOfRange;
				return;
			}
			++r.marks[static_cast<size_t>(j) * static_cast<size_t>(c.w) + static_cast<size_t>(i)];
		});
	return r;
}

// Cells the port marked, as sorted (i,j) pairs.
std::vector<std::pair<int, int>> portCells(const Cfg& c, int* outOfRange = nullptr)
{
	Run r = runPort(c);
	if (outOfRange) *outOfRange = r.outOfRange;
	std::vector<std::pair<int, int>> out;
	for (int j = 0; j < c.h; ++j)
		for (int i = 0; i < c.w; ++i)
			if (r.marks[static_cast<size_t>(j) * static_cast<size_t>(c.w) + static_cast<size_t>(i)] > 0)
				out.emplace_back(i, j);
	return out;
}

struct Cmp {
	int cells = 0;     // cells with a definite oracle verdict
	int unsure = 0;    // cells skipped because a sample sat within kTol of the boundary
	int covered = 0;   // definite-inside cells (oracle)
	int mismatch = 0;
	int dupes = 0;     // cells marked more than once
	std::string first;
};

Cmp compareCfg(const Cfg& c, const Run& r, bool exact)
{
	Cmp m;
	for (int j = 0; j < c.h; ++j) {
		for (int i = 0; i < c.w; ++i) {
			int got = r.marks[static_cast<size_t>(j) * static_cast<size_t>(c.w) + static_cast<size_t>(i)];
			if (got > 1)
				++m.dupes;
			Verdict v = oracleCell(c, i, j, exact);
			if (v == Verdict::Unsure) {
				++m.unsure;
				continue;
			}
			++m.cells;
			bool want = v == Verdict::In;
			if (want)
				++m.covered;
			if (want != (got > 0)) {
				if (m.mismatch == 0) {
					char buf[256];
					std::snprintf(buf, sizeof(buf), "cell (%d,%d): oracle %s, port %s", i, j,
						want ? "covered" : "not covered", got > 0 ? "covered" : "not covered");
					m.first = buf;
				}
				++m.mismatch;
			}
		}
	}
	return m;
}

struct Result {
	const char* name;
	int checks = 0;
	int fails = 0;
	std::string first;
	explicit Result(const char* n) : name(n) {}
};

void expect(Result& r, bool ok, const std::string& msg)
{
	++r.checks;
	if (!ok) {
		if (r.fails == 0) r.first = msg;
		++r.fails;
	}
}

void report(const Result& r, int& totalFails)
{
	totalFails += r.fails;
	if (r.fails == 0)
		std::printf("PASS  %-36s (%d checks)\n", r.name, r.checks);
	else
		std::printf("FAIL  %-36s %d/%d failed; first: %s\n", r.name, r.fails, r.checks, r.first.c_str());
}

using CellList = std::vector<std::pair<int, int>>;

std::string cellStr(const CellList& cs)
{
	std::string s = "{";
	for (size_t k = 0; k < cs.size(); ++k) {
		if (k) s += ",";
		s += "(" + std::to_string(cs[k].first) + "," + std::to_string(cs[k].second) + ")";
	}
	return s + "}";
}

// Hand-derived expected cells for an exact-match case.
void expectCells(Result& r, const char* what, const Cfg& c, CellList want)
{
	int oor = 0;
	CellList got = portCells(c, &oor);
	std::sort(got.begin(), got.end());
	std::sort(want.begin(), want.end());
	expect(r, oor == 0, std::string(what) + ": mark outside grid");
	expect(r, got == want, std::string(what) + ": got " + cellStr(got) + " want " + cellStr(want));
	// The hand expectation must also agree with the oracle.
	Cmp m = compareCfg(c, runPort(c), true);
	expect(r, m.mismatch == 0 && m.dupes == 0, std::string(what) + " vs oracle: " + m.first);
}

// ---------------------------------------------------------------------------
// Grid for the hand cases: 8x8 navcells of size 2 covering [-8,8]^2.
constexpr int HW_W = 8;
constexpr int HW_H = 8;
constexpr double HW_CS = 2.0;
constexpr double HW_OX = -8.0;
constexpr double HW_OZ = -8.0;

Cfg axisCfg(double cx, double cz, double hw, double hh, double clearance = 0.0)
{
	return makeCfg(cx, cz, 1, 0, 0, 1, hw, hh, clearance, HW_CS, HW_OX, HW_OZ, HW_W, HW_H);
}

// Cell index k in the hand grid covers world [-8 + 2k, -6 + 2k].
Result testHandCases()
{
	Result r{"hand-written edge cases"};

	// The covered region is the solid rectangle grown by R = clearance + 1, with ROUNDED corners
	// (distance <= R), so a navcell must fit inside that rounded shape, not inside a square.

	// Footprint 1, centred on a cell centre (-1,-1): the rectangle is cell 3 itself, so it is covered.
	// Neighbours stick out by more than R.
	expectCells(r, "fp1 aligned, clearance 0", axisCfg(-1, -1, 1, 1), {{3, 3}});

	// Footprint 2, centred on a grid corner (0,0): the rectangle is cells 3..4 on each axis.
	expectCells(r, "fp2 aligned at corner", axisCfg(0, 0, 2, 2), {{3, 3}, {3, 4}, {4, 3}, {4, 4}});

	// Footprint 1 off-grid centre (-0.5,-0.5): only cell 3 lies within R of the rectangle.
	expectCells(r, "fp1 off-grid (-0.5,-0.5)", axisCfg(-0.5, -0.5, 1, 1), {{3, 3}});

	// Footprint 1 off-grid centre (0.5,0.5): only cell 4 lies within R of the rectangle.
	expectCells(r, "fp1 off-grid (0.5,0.5)", axisCfg(0.5, 0.5, 1, 1), {{4, 4}});

	// Footprint 1 centred on the grid corner (0,0): every aligned 2x2 cell touching the origin has a
	// corner at distance sqrt(2) from the rectangle, which is more than R = 1. No cell is covered, so
	// the wiring falls back to the centre navcell.
	expectCells(r, "fp1 centred on grid corner", axisCfg(0, 0, 1, 1), {});

	// Zero half size at a cell centre: a disc of radius 1, and a 2x2 cell reaches sqrt(2) from its
	// centre, so no cell is covered.
	expectCells(r, "zero half size at cell centre", axisCfg(-1, -1, 0, 0), {});

	// Clearance 2 (one cell) on a footprint-2 block: expanded half 5, so cells 2..5 fit on each axis.
	{
		CellList want;
		for (int j = 2; j <= 5; ++j)
			for (int i = 2; i <= 5; ++i)
				want.emplace_back(i, j);
		expectCells(r, "fp2 with clearance 2 (16 cells)", axisCfg(0, 0, 2, 2, 2.0), want);
	}

	// Shape entirely off the grid: nothing marked.
	expectCells(r, "shape fully off grid", axisCfg(100, 0, 1, 1), {});
	expectCells(r, "shape off grid, clearance 5", axisCfg(-40, 40, 2, 2, 5.0), {});

	// Shape far larger than the grid: every cell, each once.
	{
		CellList want;
		for (int j = 0; j < HW_H; ++j)
			for (int i = 0; i < HW_W; ++i)
				want.emplace_back(i, j);
		expectCells(r, "huge shape covers grid", axisCfg(0, 0, 1000, 1000), want);
	}

	// Partly off grid, axis-aligned, checked against the oracle cell by cell.
	{
		Cfg c = axisCfg(-9, -9, 2, 2);
		Run run = runPort(c);
		Cmp m = compareCfg(c, run, true);
		expect(r, m.mismatch == 0 && m.dupes == 0, "partly off grid vs oracle: " + m.first);
	}

	// 45-degree diamond straddling the origin: oracle check.
	{
		double s = std::sqrt(0.5);
		Cfg c = makeCfg(0.3, -0.2, s, s, -s, s, 2.5, 1.5, 0.25, HW_CS, HW_OX, HW_OZ, HW_W, HW_H);
		Run run = runPort(c);
		Cmp m = compareCfg(c, run, false);
		expect(r, m.mismatch == 0 && m.dupes == 0, "45-degree diamond vs oracle: " + m.first);
	}
	return r;
}

// Vendor rule is "whole cell inside", not "centre inside". Half size 0.5 at the grid corner (0,0)
// with R = 1: expanded half 1.5. The centres of cells 3 and 4 (-1 and 1) are inside the expanded
// square, but cells [-2,0] and [0,2] are not fully inside it, so neither is covered.
Result testVendorRuleIsWholeCell()
{
	Result r{"whole-cell rule (not centre)"};
	Cfg c = axisCfg(0, 0, 0.5, 0.5);
	CellList got = portCells(c);
	expect(r, got.empty(), "expected no covered cells, got " + cellStr(got));
	Run run = runPort(c);
	Cmp m = compareCfg(c, run, true);
	expect(r, m.mismatch == 0, "whole-cell case vs oracle: " + m.first);
	return r;
}

// Random rectangles: rotation, size from tiny to large, clearance 0 to several cells, partly or
// fully off the grid, several cell sizes and grid origins. Deterministic seed.
Result testRandomRects()
{
	Result r{"random rects vs oracle"};
	std::mt19937 gen(0x5A17C0DEu);
	auto uni = [&](double lo, double hi) { return std::uniform_real_distribution<double>(lo, hi)(gen); };

	const double cellSizes[] = {2.0, 2.0, 2.0, 1.0, 0.5};
	const int dims[] = {8, 16, 24, 32, 48, 64};
	const double kPi = 3.14159265358979323846;

	int shapes = 0, cellsCompared = 0, unsure = 0, coveredCells = 0, shapesNonEmpty = 0, partlyOff = 0;
	for (int t = 0; t < 3000; ++t) {
		double cs = cellSizes[gen() % 5];
		int w = dims[gen() % 6];
		int h = dims[gen() % 6];
		double extX = w * cs, extZ = h * cs;
		double ox, oz;
		if (gen() % 2) {
			ox = -extX / 2;
			oz = -extZ / 2;
		} else {
			ox = uni(-20, 20);
			oz = uni(-20, 20);
		}
		// Centre from half a grid outside to one and a half grids beyond, so some are fully off-grid.
		double cx = uni(ox - 0.5 * extX, ox + 1.5 * extX);
		double cz = uni(oz - 0.5 * extZ, oz + 1.5 * extZ);

		double ux, uz, vx, vz;
		if (gen() % 5 < 2) {
			// Axis-aligned, exact axes (quarter turns).
			switch (gen() % 4) {
			case 0: ux = 1; uz = 0; break;
			case 1: ux = 0; uz = 1; break;
			case 2: ux = -1; uz = 0; break;
			default: ux = 0; uz = -1; break;
			}
			vx = -uz;
			vz = ux;
		} else {
			double a = uni(0.0, 2.0 * kPi);
			ux = std::cos(a);
			uz = std::sin(a);
			vx = -uz;
			vz = ux;
		}

		// Half sizes log-uniform from 5% of a cell to 60% of the grid extent.
		double hw = std::exp(uni(std::log(0.05 * cs), std::log(0.6 * extX)));
		double hh = std::exp(uni(std::log(0.05 * cs), std::log(0.6 * extZ)));
		double clearance = (gen() % 5 == 0) ? 0.0 : uni(0.0, 4.0 * cs);

		Cfg c = makeCfg(cx, cz, ux, uz, vx, vz, hw, hh, clearance, cs, ox, oz, w, h);
		Run run = runPort(c);
		Cmp m = compareCfg(c, run, false);
		++shapes;
		cellsCompared += m.cells;
		unsure += m.unsure;
		coveredCells += m.covered;
		if (m.covered > 0) ++shapesNonEmpty;
		if (cx - hw < ox || cx + hw > ox + extX || cz - hh < oz || cz + hh > oz + extZ) ++partlyOff;
		expect(r, run.outOfRange == 0, "mark outside grid");
		expect(r, m.dupes == 0, "cell marked more than once");
		expect(r, m.mismatch == 0, "shape " + std::to_string(t) + ": " + m.first);
	}
	expect(r, coveredCells > 20000, "too few covered cells in random set: " + std::to_string(coveredCells));
	expect(r, partlyOff > 500, "too few shapes that extend past the grid");
	std::printf("      (random: %d shapes, %d shapes covered >=1 navcell, %d shapes extend past the grid,\n"
		"       %d cells compared, %d covered by oracle, %d skipped as within %.0e of a boundary)\n",
		shapes, shapesNonEmpty, partlyOff, cellsCompared, coveredCells, unsure, kTol);
	return r;
}

// Exact sweep: axis-aligned, dyadic centres, half sizes, clearances and origins. Float and double
// arithmetic are both exact here, so the port must match the oracle with no tolerance.
Result testExactBoundaries()
{
	Result r{"exact boundary sweep"};
	const double origins[][2] = {{-8, -8}, {-7, -6}};
	const double halves[] = {0.0, 0.5, 1.0, 1.5, 2.0, 3.0};
	const double clears[] = {0.0, 0.5, 1.0, 2.0};
	int cases = 0, cellsChecked = 0, unsure = 0;
	for (const auto& o : origins)
		for (int ci = -9; ci <= 9; ++ci)
			for (int cj = -9; cj <= 9; ++cj)
				for (double hw : halves)
					for (double hh : halves)
						for (double cl : clears) {
							Cfg c = makeCfg(ci, cj, 1, 0, 0, 1, hw, hh, cl, HW_CS, o[0], o[1], HW_W, HW_H);
							Run run = runPort(c);
							Cmp m = compareCfg(c, run, true);
							++cases;
							cellsChecked += m.cells;
							unsure += m.unsure;
							expect(r, m.mismatch == 0 && m.dupes == 0 && run.outOfRange == 0,
								"centre (" + std::to_string(ci) + "," + std::to_string(cj) + ") hs(" +
								std::to_string(hw) + "," + std::to_string(hh) + ") cl " + std::to_string(cl) +
								": " + m.first);
						}
	expect(r, unsure == 0, "exact mode should have no unsure cells");
	std::printf("      (exact: %d cases, %d cells compared)\n", cases, cellsChecked);
	return r;
}

// Grid-aligned buildings: the footprint block that rebuild_nav_grid marked before the port must equal
// the rasterizer's output for every placement the simulation can make (spawn_building, GRID = 2,
// map_half = 64, 64x64 navcells). Placements are cells cx in [-32, 32-footprint], as can_place allows.
Result testGridAlignedBuildings()
{
	Result r{"grid-aligned buildings unchanged"};
	const float GRID = 2.0f; // Simulation::GRID (simulation.h:77)
	rtso::NavGrid nav;       // res 64, map_half 64: the same grid the sim uses
	const float cs = 2.0f * nav.map_half / static_cast<float>(nav.res);
	const Vec2 origin(-nav.map_half, -nav.map_half);

	int placements = 0, cellsCompared = 0;
	for (int footprint = 1; footprint <= 2; ++footprint) {
		for (int cx = -32; cx <= 32 - footprint; ++cx) {
			for (int cz = -32; cz <= 32 - footprint; ++cz) {
				// Simulation::spawn_building (simulation.cpp:578-581) and spawn_building_world (:587-596).
				float x = (cx + footprint * 0.5f) * GRID;
				float z = (cz + footprint * 0.5f) * GRID;
				float half = footprint * GRID * 0.5f;

				// Old rebuild_nav_grid footprint block (simulation.cpp, before the port).
				std::vector<std::pair<int, int>> legacy;
				auto [ccx, ccy] = nav.world_to_cell(x, z);
				int sx = ccx - footprint / 2;
				int sy = ccy - footprint / 2;
				for (int dy = 0; dy < footprint; ++dy)
					for (int dx = 0; dx < footprint; ++dx) {
						int ix = sx + dx, iy = sy + dy;
						if (ix >= 0 && ix < nav.res && iy >= 0 && iy < nav.res)
							legacy.emplace_back(ix, iy);
					}
				std::sort(legacy.begin(), legacy.end());

				// New rebuild_nav_grid call: axis-aligned, clearance 0.
				std::vector<std::pair<int, int>> ported;
				rtso::nav::rasterize_rect_with_clearance(Vec2(x, z), Vec2(1, 0), Vec2(0, 1), Vec2(half, half),
					0.0f, cs, origin, nav.res, nav.res,
					[&](int i, int j) { ported.emplace_back(i, j); });
				std::sort(ported.begin(), ported.end());

				++placements;
				cellsCompared += static_cast<int>(legacy.size());
				expect(r, legacy == ported, "fp" + std::to_string(footprint) + " cell (" + std::to_string(cx) +
					"," + std::to_string(cz) + "): legacy " + cellStr(legacy) + " ported " + cellStr(ported));
			}
		}
	}
	expect(r, placements == 64 * 64 + 63 * 63, "placement count");
	std::printf("      (grid-aligned: %d placements, %d legacy cells matched exactly)\n", placements, cellsCompared);
	return r;
}

// Off-grid buildings (centres in [-60,60], 64x64 grid). Two facts the wiring relies on:
//  - footprint 2 always covers at least one navcell (its central band |x| <= 2, |z| <= 3 contains one);
//  - footprint 1 can cover none (rounded corners). Without the extension, almost none are covered at all.
// The wiring in Simulation::rebuild_nav_grid falls back to the centre navcell when nothing is covered.
Result testOffGridCoverage()
{
	Result r{"off-grid coverage (fallback needed?)"};
	std::mt19937 gen(0x0B5E7u);
	std::uniform_real_distribution<double> uni(-60.0, 60.0);
	int zeroFp1 = 0, zeroFp2 = 0, zeroFp1NoExt = 0, n1 = 0, n2 = 0;
	for (int t = 0; t < 4000; ++t) {
		int footprint = 1 + static_cast<int>(gen() % 2);
		double hs = footprint * 2.0 * 0.5; // Simulation::spawn_building_world: half = footprint*GRID/2
		double x = fl(uni(gen)), z = fl(uni(gen));
		Cfg c = makeCfg(x, z, 1, 0, 0, 1, hs, hs, 0.0, 2.0, -64, -64, 64, 64);
		CellList got = portCells(c);
		int coveredNoExt = 0;
		for (int j = 0; j < c.h; ++j)
			for (int i = 0; i < c.w; ++i)
				if (oracleCellR(c, i, j, false, 0.0) == Verdict::In)
					++coveredNoExt;
		if (footprint == 1) {
			++n1;
			if (got.empty()) ++zeroFp1;
			if (coveredNoExt == 0) ++zeroFp1NoExt;
		} else {
			++n2;
			if (got.empty()) ++zeroFp2;
		}
	}
	expect(r, zeroFp2 == 0, "footprint 2 covered no navcell in " + std::to_string(zeroFp2) + " placements");
	expect(r, zeroFp1NoExt > 0.9 * n1, "without the extension footprint 1 should almost never cover a navcell");
	expect(r, zeroFp1 < zeroFp1NoExt, "the extension should reduce zero-cover footprint-1 placements");
	std::printf("      (off-grid: footprint 1 zero-cover %d/%d with extension, %d/%d without; footprint 2 zero-cover %d/%d)\n",
		zeroFp1, n1, zeroFp1NoExt, n1, zeroFp2, n2);
	return r;
}

} // namespace

int main()
{
	int totalFails = 0;
	report(testHandCases(), totalFails);
	report(testVendorRuleIsWholeCell(), totalFails);
	report(testRandomRects(), totalFails);
	report(testExactBoundaries(), totalFails);
	report(testGridAlignedBuildings(), totalFails);
	report(testOffGridCoverage(), totalFails);

	if (totalFails == 0) {
		std::printf("ALL PASS\n");
		return 0;
	}
	std::printf("FAILED: %d checks\n", totalFails);
	return 1;
}
