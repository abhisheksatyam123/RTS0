// Tests for src/simulation/nav/geometry.h (port of 0 A.D. Geometry helpers, GPL-2.0-or-later).
// Build: g++ -std=c++20 -O2 -Isrc tools/test_geometry.cpp -o /tmp/test_geometry && /tmp/test_geometry
//
// Oracles are independent double-precision brute force (boundary sampling, Liang-Barsky
// clipping, projection-based SAT); they do not reuse the code under test.

#include "simulation/nav/geometry.h"

#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

using rtso::nav::Vec2;

namespace {

constexpr double TOL = 1e-3;
constexpr int EDGE_SAMPLES = 20000; // boundary samples per edge for distance oracles

struct P {
	double x, y;
};

P operator+(P a, P b) { return P{a.x + b.x, a.y + b.y}; }
P operator-(P a, P b) { return P{a.x - b.x, a.y - b.y}; }
P operator-(P a) { return P{-a.x, -a.y}; }
P operator*(P a, double s) { return P{a.x * s, a.y * s}; }
double dotp(P a, P b) { return a.x * b.x + a.y * b.y; }
double distp(P a, P b) { return std::sqrt(dotp(a - b, a - b)); }

P toP(Vec2 v) { return P{v.x, v.y}; }

// Oracle square: centre, unit axes u/v, half sizes.
struct Sq {
	P c, u, v;
	double hw, hh;
};

Sq makeSq(Vec2 c, Vec2 u, Vec2 v, Vec2 hs)
{
	return Sq{toP(c), toP(u), toP(v), hs.x, hs.y};
}

// Local coordinates (du, dv) of a point relative to the square's centre.
P local(const Sq& s, P p)
{
	P d = p - s.c;
	return P{dotp(d, s.u), dotp(d, s.v)};
}

// Closed-square membership with tolerance eps.
bool insideSolid(const Sq& s, P p, double eps = 0.0)
{
	P l = local(s, p);
	return std::fabs(l.x) <= s.hw + eps && std::fabs(l.y) <= s.hh + eps;
}

// Points on the boundary, sampled densely (corners included).
std::vector<P> boundarySamples(const Sq& s, int perEdge)
{
	P corners[4] = {
		s.c + s.u * s.hw + s.v * s.hh,
		s.c + s.u * s.hw - s.v * s.hh,
		s.c - s.u * s.hw - s.v * s.hh,
		s.c - s.u * s.hw + s.v * s.hh,
	};
	std::vector<P> out;
	out.reserve(static_cast<size_t>(4 * perEdge));
	for (int e = 0; e < 4; ++e) {
		P a = corners[e];
		P b = corners[(e + 1) % 4];
		for (int k = 0; k < perEdge; ++k) {
			double t = static_cast<double>(k) / perEdge;
			out.push_back(a + (b - a) * t);
		}
	}
	return out;
}

// Brute-force min distance from p to the boundary of s.
double bruteBoundaryDist(const std::vector<P>& samples, P p)
{
	double best = 1e300;
	for (const P& q : samples) {
		double dx = p.x - q.x, dy = p.y - q.y;
		double d2 = dx * dx + dy * dy;
		if (d2 < best) best = d2;
	}
	return std::sqrt(best);
}

// Brute-force max distance from p to the boundary of s.
double bruteBoundaryMaxDist(const std::vector<P>& samples, P p)
{
	double best = 0.0;
	for (const P& q : samples) {
		double dx = p.x - q.x, dy = p.y - q.y;
		double d2 = dx * dx + dy * dy;
		if (d2 > best) best = d2;
	}
	return std::sqrt(best);
}

// Liang-Barsky: does segment a-b touch the closed solid square?
bool segHitsSolid(const Sq& s, P a, P b)
{
	P la = local(s, a), lb = local(s, b);
	double dx = lb.x - la.x, dy = lb.y - la.y;
	double p[4] = {-dx, dx, -dy, dy};
	double q[4] = {la.x + s.hw, s.hw - la.x, la.y + s.hh, s.hh - la.y};
	double t0 = 0.0, t1 = 1.0;
	for (int i = 0; i < 4; ++i) {
		if (p[i] == 0.0) {
			if (q[i] < 0.0) return false;
		} else {
			double r = q[i] / p[i];
			if (p[i] < 0.0) {
				if (r > t1) return false;
				if (r > t0) t0 = r;
			} else {
				if (r < t0) return false;
				if (r < t1) t1 = r;
			}
		}
	}
	return true;
}

// Oracle for TestRaySquare semantics: collision only when a is outside and the ray reaches the square.
bool oracleTestRay(const Sq& s, P a, P b)
{
	if (insideSolid(s, a)) return false;
	if (insideSolid(s, b)) return true;
	return segHitsSolid(s, a, b);
}

// Oracle overlap: projection SAT on the four edge normals. Returns the largest axis gap
// (positive = separated). Overlap (touching included) means gap <= 0.
double oracleSquareGap(const Sq& s0, const Sq& s1)
{
	P axes[4] = {s0.u, s0.v, s1.u, s1.v};
	P c0[4], c1[4];
	P sc0[4] = {s0.u * s0.hw + s0.v * s0.hh, s0.u * s0.hw - s0.v * s0.hh, -s0.u * s0.hw - s0.v * s0.hh, -s0.u * s0.hw + s0.v * s0.hh};
	P sc1[4] = {s1.u * s1.hw + s1.v * s1.hh, s1.u * s1.hw - s1.v * s1.hh, -s1.u * s1.hw - s1.v * s1.hh, -s1.u * s1.hw + s1.v * s1.hh};
	for (int i = 0; i < 4; ++i) {
		c0[i] = s0.c + sc0[i];
		c1[i] = s1.c + sc1[i];
	}
	double maxGap = -1e300;
	for (const P& n : axes) {
		double mn0 = 1e300, mx0 = -1e300, mn1 = 1e300, mx1 = -1e300;
		for (int i = 0; i < 4; ++i) {
			double p0 = dotp(c0[i], n), p1 = dotp(c1[i], n);
			mn0 = std::min(mn0, p0); mx0 = std::max(mx0, p0);
			mn1 = std::min(mn1, p1); mx1 = std::max(mx1, p1);
		}
		double gap = std::max(mn1 - mx0, mn0 - mx1);
		maxGap = std::max(maxGap, gap);
	}
	return maxGap;
}

// Corners of the square in CCW order.
std::vector<P> squareCorners(const Sq& s)
{
	return {
		s.c + s.u * s.hw + s.v * s.hh,
		s.c - s.u * s.hw + s.v * s.hh,
		s.c - s.u * s.hw - s.v * s.hh,
		s.c + s.u * s.hw - s.v * s.hh,
	};
}

// Exact distance from point p to segment a-b (double precision, clamped projection).
double pointSegDist(P p, P a, P b)
{
	P ab = b - a;
	double len2 = dotp(ab, ab);
	double t = len2 > 0.0 ? dotp(p - a, ab) / len2 : 0.0;
	t = std::max(0.0, std::min(1.0, t));
	return distp(p, a + ab * t);
}

// Exact gap between two solid convex squares (0 if they overlap).
double exactPolygonGap(const Sq& s0, const Sq& s1)
{
	if (oracleSquareGap(s0, s1) <= 0.0) return 0.0;
	std::vector<P> c0 = squareCorners(s0), c1 = squareCorners(s1);
	double best = 1e300;
	for (int k = 0; k < 4; ++k)
		for (int e = 0; e < 4; ++e) {
			P e0 = c1[e], e1 = c1[(e + 1) % 4];
			best = std::min(best, pointSegDist(c0[k], e0, e1));
			P f0 = c0[e], f1 = c0[(e + 1) % 4];
			best = std::min(best, pointSegDist(c1[k], f0, f1));
		}
	return best;
}

struct Rng {
	std::mt19937 gen{12345u};
	double uni(double lo, double hi) { return std::uniform_real_distribution<double>(lo, hi)(gen); }
};

// Random rotated square. angle < 0 means random angle; otherwise fixed (e.g. 0 for axis-aligned).
struct SqF {
	Vec2 c, u, v, hs;
};

SqF randomSq(Rng& r, double angle = -1.0)
{
	double a = angle < 0.0 ? r.uni(0.0, 2.0 * M_PI) : angle;
	SqF s;
	s.c = Vec2(static_cast<float>(r.uni(-5, 5)), static_cast<float>(r.uni(-5, 5)));
	s.u = Vec2(static_cast<float>(std::cos(a)), static_cast<float>(std::sin(a)));
	s.v = Vec2(static_cast<float>(-std::sin(a)), static_cast<float>(std::cos(a)));
	s.hs = Vec2(static_cast<float>(r.uni(0.3, 2.5)), static_cast<float>(r.uni(0.3, 2.5)));
	return s;
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
		std::printf("PASS  %-30s (%d checks)\n", r.name, r.checks);
	else
		std::printf("FAIL  %-30s %d/%d failed; first: %s\n", r.name, r.fails, r.checks, r.first.c_str());
}

std::string fmt(const char* f, double a = 0, double b = 0, double c = 0, double d = 0)
{
	char buf[512];
	std::snprintf(buf, sizeof(buf), f, a, b, c, d);
	return buf;
}

// ---------------------------------------------------------------------------

Result testPointIsInSquare()
{
	Result r{"PointIsInSquare"};
	// Axis-aligned, hs=(2,1): interior, edge, corner, outside.
	Vec2 u(1, 0), v(0, 1), hs(2, 1);
	expect(r, rtso::nav::PointIsInSquare(Vec2(0, 0), u, v, hs), "centre should be inside");
	expect(r, rtso::nav::PointIsInSquare(Vec2(2, 0), u, v, hs), "right edge midpoint should be inside");
	expect(r, rtso::nav::PointIsInSquare(Vec2(-2, 1), u, v, hs), "corner should be inside");
	expect(r, rtso::nav::PointIsInSquare(Vec2(0, -1), u, v, hs), "bottom edge should be inside");
	expect(r, !rtso::nav::PointIsInSquare(Vec2(2.01f, 0), u, v, hs), "just right of edge should be outside");
	expect(r, !rtso::nav::PointIsInSquare(Vec2(0, 1.01f), u, v, hs), "just above edge should be outside");

	// Rotated 30 degrees: points at distance 1.5 along u are inside; 2.5 along u outside.
	float a = static_cast<float>(M_PI / 6.0);
	Vec2 ru(std::cos(a), std::sin(a)), rv(-std::sin(a), std::cos(a));
	expect(r, rtso::nav::PointIsInSquare(ru * 1.5f, ru, rv, hs), "rotated: inside along u");
	expect(r, !rtso::nav::PointIsInSquare(ru * 2.5f, ru, rv, hs), "rotated: outside along u");
	expect(r, rtso::nav::PointIsInSquare(ru * 1.999f + rv * 0.999f, ru, rv, hs), "rotated: near corner inside");
	expect(r, !rtso::nav::PointIsInSquare(ru * 2.01f + rv * 0.5f, ru, rv, hs), "rotated: beyond edge outside");

	// Random points vs brute-force coordinate test.
	Rng rng;
	for (int i = 0; i < 2000; ++i) {
		SqF s = randomSq(rng);
		Sq o = makeSq(s.c, s.u, s.v, s.hs);
		P p{rng.uni(-8, 8), rng.uni(-8, 8)};
		bool got = rtso::nav::PointIsInSquare(Vec2(static_cast<float>(p.x), static_cast<float>(p.y)) - s.c, s.u, s.v, s.hs);
		bool want = insideSolid(o, p);
		// Skip points within float noise of an edge; those are covered by the edge cases above.
		P l = local(o, p);
		bool nearEdge = std::fabs(std::fabs(l.x) - o.hw) < 1e-4 || std::fabs(std::fabs(l.y) - o.hh) < 1e-4;
		if (!nearEdge)
			expect(r, got == want, "random point membership mismatch");
	}
	return r;
}

Result testHalfBoundingBox()
{
	Result r{"GetHalfBoundingBox"};
	Rng rng;
	for (int i = 0; i < 200; ++i) {
		SqF s = randomSq(rng);
		Vec2 b = rtso::nav::GetHalfBoundingBox(s.u, s.v, s.hs);
		Sq o = makeSq(Vec2(0, 0), s.u, s.v, s.hs);
		std::vector<P> bs = boundarySamples(o, 2000);
		double mx = 0, my = 0;
		for (const P& q : bs) {
			mx = std::max(mx, std::fabs(q.x));
			my = std::max(my, std::fabs(q.y));
		}
		expect(r, std::fabs(mx - b.x) < TOL, fmt("bx mismatch: got %g want %g", b.x, mx));
		expect(r, std::fabs(my - b.y) < TOL, fmt("by mismatch: got %g want %g", b.y, my));
	}
	// Axis-aligned: exact.
	Vec2 bb = rtso::nav::GetHalfBoundingBox(Vec2(1, 0), Vec2(0, 1), Vec2(2, 3));
	expect(r, bb.x == 2.0f && bb.y == 3.0f, "axis-aligned bounding box should equal half size");
	return r;
}

Result testDistanceToSquare()
{
	Result r{"DistanceToSquare"};
	Rng rng;
	for (int i = 0; i < 400; ++i) {
		SqF s = randomSq(rng, (i % 2 == 0) ? 0.0 : -1.0); // half axis-aligned, half random
		Sq o = makeSq(s.c, s.u, s.v, s.hs);
		std::vector<P> bs = boundarySamples(o, EDGE_SAMPLES / 4);
		for (int j = 0; j < 20; ++j) {
			P p{rng.uni(-8, 8), rng.uni(-8, 8)};
			Vec2 pv(static_cast<float>(p.x), static_cast<float>(p.y));
			Vec2 rel = pv - s.c;
			bool inside = insideSolid(o, p);
			double want = bruteBoundaryDist(bs, p);
			double got = rtso::nav::DistanceToSquare(rel, s.u, s.v, s.hs, false);
			expect(r, std::fabs(got - want) < TOL, fmt("boundary dist got %g want %g", got, want));
			double gotZ = rtso::nav::DistanceToSquare(rel, s.u, s.v, s.hs, true);
			double wantZ = inside ? 0.0 : want;
			expect(r, std::fabs(gotZ - wantZ) < TOL, fmt("countInsideAsZero got %g want %g", gotZ, wantZ));
			double gotSq = rtso::nav::DistanceToSquareSquared(rel, s.u, s.v, s.hs, false);
			expect(r, std::fabs(gotSq - want * want) < TOL, fmt("squared got %g want %g", gotSq, want * want));
			double gotSqZ = rtso::nav::DistanceToSquareSquared(rel, s.u, s.v, s.hs, true);
			expect(r, std::fabs(gotSqZ - wantZ * wantZ) < TOL, fmt("squared Z got %g want %g", gotSqZ, wantZ * wantZ));
		}
	}
	// Explicit axis-aligned cases, hs=(2,1).
	Vec2 u(1, 0), v(0, 1), hs(2, 1);
	expect(r, std::fabs(rtso::nav::DistanceToSquare(Vec2(5, 0), u, v, hs) - 3.0f) < 1e-6f, "right of square distance 3");
	expect(r, std::fabs(rtso::nav::DistanceToSquare(Vec2(0, 0), u, v, hs) - 1.0f) < 1e-6f, "centre distance to boundary = 1");
	expect(r, rtso::nav::DistanceToSquare(Vec2(0, 0), u, v, hs, true) == 0.0f, "centre with countInsideAsZero = 0");
	expect(r, std::fabs(rtso::nav::DistanceToSquare(Vec2(5, 5), u, v, hs) - 5.0f) < 1e-5f, "corner-diagonal distance sqrt(9+16)=5");
	return r;
}

Result testNearestPointOnSquare()
{
	Result r{"NearestPointOnSquare"};
	Rng rng;
	for (int i = 0; i < 400; ++i) {
		SqF s = randomSq(rng, (i % 3 == 0) ? 0.0 : -1.0);
		Sq o = makeSq(Vec2(0, 0), s.u, s.v, s.hs);
		std::vector<P> bs = boundarySamples(o, EDGE_SAMPLES / 4);
		for (int j = 0; j < 20; ++j) {
			Vec2 rel(static_cast<float>(rng.uni(-8, 8)), static_cast<float>(rng.uni(-8, 8)));
			P rp = toP(rel);
			Vec2 q = rtso::nav::NearestPointOnSquare(rel, s.u, s.v, s.hs);
			P qp = toP(q);
			// Must lie on the boundary (within float tolerance).
			P ql = local(o, qp);
			bool onBoundary = std::fabs(std::fabs(ql.x) - o.hw) < 1e-3 || std::fabs(std::fabs(ql.y) - o.hh) < 1e-3;
			bool inClosed = std::fabs(ql.x) <= o.hw + 1e-3 && std::fabs(ql.y) <= o.hh + 1e-3;
			expect(r, onBoundary && inClosed, fmt("nearest point not on boundary (%g,%g)", qp.x, qp.y));
			double want = bruteBoundaryDist(bs, rp);
			double got = distp(rp, qp);
			expect(r, std::fabs(got - want) < TOL, fmt("nearest distance got %g want %g", got, want));
		}
	}
	return r;
}

Result testMaxDistanceToSquare()
{
	Result r{"MaxDistanceToSquare"};
	Rng rng;
	for (int i = 0; i < 300; ++i) {
		SqF s = randomSq(rng, (i % 2 == 0) ? 0.0 : -1.0);
		Sq o = makeSq(Vec2(0, 0), s.u, s.v, s.hs);
		std::vector<P> bs = boundarySamples(o, EDGE_SAMPLES / 4);
		for (int j = 0; j < 20; ++j) {
			P p{rng.uni(-8, 8), rng.uni(-8, 8)};
			Vec2 pv(static_cast<float>(p.x), static_cast<float>(p.y));
			bool inside = insideSolid(o, p);
			double want = bruteBoundaryMaxDist(bs, p);
			double got = rtso::nav::MaxDistanceToSquare(pv, s.u, s.v, s.hs, false);
			expect(r, std::fabs(got - want) < TOL, fmt("max dist got %g want %g", got, want));
			double gotZ = rtso::nav::MaxDistanceToSquare(pv, s.u, s.v, s.hs, true);
			double wantZ = inside ? 0.0 : want;
			expect(r, std::fabs(gotZ - wantZ) < TOL, fmt("max dist Z got %g want %g", gotZ, wantZ));
		}
	}
	return r;
}

Result testDistanceSquareToSquare()
{
	Result r{"DistanceSquareToSquare"};
	Rng rng;
	int checkedDisjoint = 0, checkedOverlap = 0;
	for (int i = 0; i < 600; ++i) {
		SqF a = randomSq(rng);
		SqF b = randomSq(rng);
		Sq oa = makeSq(a.c, a.u, a.v, a.hs);
		Sq ob = makeSq(b.c, b.u, b.v, b.hs);
		double gap = oracleSquareGap(oa, ob);
		if (std::fabs(gap) < 2e-3) continue; // avoid near-touching ambiguity

		// relativePos = centre of square 1 relative to centre of square 2.
		Vec2 rel = a.c - b.c;
		float got = rtso::nav::DistanceSquareToSquare(rel, a.u, a.v, a.hs, b.u, b.v, b.hs);
		if (gap <= 0.0) {
			++checkedOverlap;
			expect(r, got == 0.0f, fmt("overlapping squares should give 0, got %g", got));
		} else {
			++checkedDisjoint;
			// Shortest distance is attained with one point on boundary of A; min over those vs solid B.
			// Oracle: for disjoint convex polygons the closest pair lies on a vertex/edge pair,
			// so the exact distance is the min of vertex-to-edge distances in both directions.
			double want = exactPolygonGap(oa, ob);
			expect(r, std::fabs(got - want) < TOL, fmt("disjoint distance got %g want %g", got, want));
		}
	}
	// Explicit: axis-aligned squares gap of 1 along x.
	Vec2 u(1, 0), v(0, 1), hs(1, 1);
	expect(r, std::fabs(rtso::nav::DistanceSquareToSquare(Vec2(3, 0), u, v, hs, u, v, hs) - 1.0f) < 1e-5f, "axis gap of 1");
	expect(r, rtso::nav::DistanceSquareToSquare(Vec2(1.5f, 0.5f), u, v, hs, u, v, hs) == 0.0f, "partial overlap = 0");
	expect(r, rtso::nav::DistanceSquareToSquare(Vec2(0, 0), u, v, Vec2(0.2f, 0.2f), u, v, Vec2(3, 3)) == 0.0f, "contained = 0");
	std::printf("      (DistanceSquareToSquare: %d disjoint, %d overlapping random pairs)\n", checkedDisjoint, checkedOverlap);
	return r;
}

Result testMaxDistanceSquareToSquare()
{
	Result r{"MaxDistanceSquareToSquare"};
	Rng rng;
	int used = 0;
	for (int i = 0; i < 300; ++i) {
		SqF a = randomSq(rng);
		SqF b = randomSq(rng);
		Sq oa = makeSq(a.c, a.u, a.v, a.hs);
		Sq ob = makeSq(b.c, b.u, b.v, b.hs);
		// Vendor MaxDistanceSquareToSquare evaluates corners of A against B with countInsideAsZero=true,
		// so it is only meaningful when no corner of A lies inside B (see report: vendor quirk).
		bool cornerInside = false;
		for (int sx = -1; sx <= 1; sx += 2)
			for (int sy = -1; sy <= 1; sy += 2) {
				P corner = oa.c + oa.u * (sx * oa.hw) + oa.v * (sy * oa.hh);
				if (insideSolid(ob, corner)) cornerInside = true;
			}
		if (cornerInside) continue;
		++used;

		Vec2 rel = a.c - b.c;
		float got = rtso::nav::MaxDistanceSquareToSquare(rel, a.u, a.v, a.hs, b.u, b.v, b.hs);
		std::vector<P> sa = boundarySamples(oa, 200);
		std::vector<P> sb = boundarySamples(ob, 200);
		double want = 0.0;
		for (const P& p : sa)
			for (const P& q : sb) {
				double d = distp(p, q);
				if (d > want) want = d;
			}
		expect(r, std::fabs(got - want) < TOL, fmt("max sq-sq got %g want %g", got, want));
	}
	expect(r, used > 50, "too few usable random cases");
	return r;
}

// TestRaySquare / TestRayAASquare semantics vs oracle, including hand-picked hit/miss cases.
Result testRaySquare()
{
	Result r{"TestRaySquare / TestRayAASquare"};
	Vec2 u(1, 0), v(0, 1), hs(2, 1);
	// Axis-aligned hand cases.
	struct Case { Vec2 a, b; bool want; const char* what; };
	Case cases[] = {
		{Vec2(-5, 0), Vec2(5, 0), true, "pass through centre"},
		{Vec2(-5, 2), Vec2(5, 2), false, "miss above"},
		{Vec2(-5, -1.5f), Vec2(5, -1.5f), false, "miss below"},
		{Vec2(0, 0), Vec2(10, 0), false, "start inside: exit only"},
		{Vec2(-5, 0), Vec2(0, 0), true, "end inside"},
		{Vec2(-5, 0), Vec2(-4, 0), false, "short segment before square"},
		{Vec2(-3, 1.5f), Vec2(3, -1.5f), true, "diagonal across corner region"},
		{Vec2(-3, 1.5f), Vec2(-1, 1.5f), false, "parallel above, not reaching"},
		{Vec2(0, 5), Vec2(0, -5), true, "vertical through"},
		{Vec2(2.5f, 0), Vec2(3, 0), false, "right of square, outside"},
	};
	for (const Case& c : cases) {
		bool got = rtso::nav::TestRaySquare(c.a, c.b, u, v, hs);
		bool gotAA = rtso::nav::TestRayAASquare(c.a, c.b, hs);
		expect(r, got == c.want, std::string("TestRaySquare: ") + c.what);
		expect(r, gotAA == c.want, std::string("TestRayAASquare: ") + c.what);
	}
	// Rotated 45 degrees diamond with vertices at distance sqrt(2) on the axes.
	float a45 = static_cast<float>(M_PI / 4.0);
	Vec2 du(std::cos(a45), std::sin(a45)), dv(-std::sin(a45), std::cos(a45));
	Vec2 dhs(1, 1);
	expect(r, rtso::nav::TestRaySquare(Vec2(-3, 0), Vec2(3, 0), du, dv, dhs), "diamond: horizontal through centre hits");
	expect(r, rtso::nav::TestRaySquare(Vec2(-3, 1.3f), Vec2(3, 1.3f), du, dv, dhs), "diamond: y=1.3 clips the tip, hits");
	expect(r, !rtso::nav::TestRaySquare(Vec2(-3, 1.5f), Vec2(3, 1.5f), du, dv, dhs), "diamond: y=1.5 above tip, misses");

	// Random cases vs oracle (both rotated and axis-aligned).
	Rng rng;
	for (int i = 0; i < 4000; ++i) {
		SqF s = randomSq(rng, (i % 4 == 0) ? 0.0 : -1.0);
		Sq o = makeSq(s.c, s.u, s.v, s.hs);
		Vec2 a(static_cast<float>(rng.uni(-8, 8)), static_cast<float>(rng.uni(-8, 8)));
		Vec2 b(static_cast<float>(rng.uni(-8, 8)), static_cast<float>(rng.uni(-8, 8)));
		bool want = oracleTestRay(o, toP(a), toP(b));
		bool got = rtso::nav::TestRaySquare(a - s.c, b - s.c, s.u, s.v, s.hs);
		expect(r, got == want, fmt("random ray mismatch a=(%g,%g)", a.x, a.y));
		if (i % 4 == 0) {
			// Axis-aligned: centre the square at the origin, compare AA variant too.
			bool gotAA = rtso::nav::TestRayAASquare(a - s.c, b - s.c, s.hs);
			expect(r, gotAA == want, "random AA ray mismatch");
		}
	}
	return r;
}

Result testSquareSquare()
{
	Result r{"TestSquareSquare"};
	Vec2 u(1, 0), v(0, 1);
	// Hand cases.
	expect(r, rtso::nav::TestSquareSquare(Vec2(0, 0), u, v, Vec2(1, 1), Vec2(0, 0), u, v, Vec2(1, 1)), "identical squares overlap");
	expect(r, rtso::nav::TestSquareSquare(Vec2(0, 0), u, v, Vec2(1, 1), Vec2(1.5f, 0), u, v, Vec2(1, 1)), "partial overlap x");
	expect(r, !rtso::nav::TestSquareSquare(Vec2(0, 0), u, v, Vec2(1, 1), Vec2(3, 0), u, v, Vec2(1, 1)), "gap of 1 along x: no overlap");
	expect(r, rtso::nav::TestSquareSquare(Vec2(0, 0), u, v, Vec2(5, 5), Vec2(1, 1), u, v, Vec2(0.5f, 0.5f)), "containment overlaps");
	expect(r, !rtso::nav::TestSquareSquare(Vec2(0, 0), u, v, Vec2(1, 1), Vec2(1, 3), u, v, Vec2(1, 1)), "gap along y: no overlap");
	// Diamond whose left tip is 0.086 away from the square's right edge: no overlap.
	float a45 = static_cast<float>(M_PI / 4.0);
	Vec2 du(std::cos(a45), std::sin(a45)), dv(-std::sin(a45), std::cos(a45));
	expect(r, !rtso::nav::TestSquareSquare(Vec2(0, 0), u, v, Vec2(1, 1), Vec2(2.5f, 0), du, dv, Vec2(1, 1)), "diamond near miss");
	// Diamond whose left tip pokes 0.1 into the square: overlap.
	expect(r, rtso::nav::TestSquareSquare(Vec2(0, 0), u, v, Vec2(1, 1), Vec2(2.3f, 0), du, dv, Vec2(1, 1)), "diamond tip overlap");
	// Rotated square whose corner sits inside an axis-aligned square.
	// Diamond centred (1.3,1.3), radius 0.707: the axis square's corner (1,1) lies inside it.
	expect(r, rtso::nav::TestSquareSquare(Vec2(0, 0), u, v, Vec2(1, 1), Vec2(1.3f, 1.3f), du, dv, Vec2(0.5f, 0.5f)), "rotated corner overlap");
	// Same diamond centred (1.6,1.6): nearest vertex sits on x+y=2.49, so the squares are separated.
	expect(r, !rtso::nav::TestSquareSquare(Vec2(0, 0), u, v, Vec2(1, 1), Vec2(1.6f, 1.6f), du, dv, Vec2(0.5f, 0.5f)), "rotated corner near miss");

	// Random cases vs projection oracle; skip near-touching.
	Rng rng;
	int used = 0;
	for (int i = 0; i < 3000; ++i) {
		SqF a = randomSq(rng);
		SqF b = randomSq(rng);
		Sq oa = makeSq(a.c, a.u, a.v, a.hs);
		Sq ob = makeSq(b.c, b.u, b.v, b.hs);
		double gap = oracleSquareGap(oa, ob);
		if (std::fabs(gap) < 2e-3) continue;
		++used;
		bool want = gap <= 0.0;
		bool got = rtso::nav::TestSquareSquare(a.c, a.u, a.v, a.hs, b.c, b.u, b.v, b.hs);
		expect(r, got == want, fmt("random square overlap mismatch (gap %g)", gap));
	}
	expect(r, used > 1000, "too few random cases");
	return r;
}

Result testPerimeter()
{
	Result r{"GetPerimeterDistance/Coords"};
	int shapes[][2] = {{3, 2}, {1, 1}, {5, 1}, {2, 7}};
	for (auto& sh : shapes) {
		int xm = sh[0], ym = sh[1];
		int quarter = xm + ym;
		for (int k = 0; k < 4 * quarter; ++k) {
			std::pair<int, int> p = rtso::nav::GetPerimeterCoordinates(xm, ym, k);
			bool onPerim = std::abs(p.first) == xm || std::abs(p.second) == ym;
			expect(r, onPerim, fmt("coords not on perimeter for k=%g", k));
			expect(r, rtso::nav::GetPerimeterDistance(xm, ym, p.first, p.second) == k,
			       fmt("round trip failed at k=%g", k));
			std::pair<int, int> wrapped = rtso::nav::GetPerimeterCoordinates(xm, ym, k + 4 * quarter);
			expect(r, wrapped == p, fmt("periodicity failed at k=%g", k));
		}
		expect(r, rtso::nav::GetPerimeterCoordinates(xm, ym, 0) == std::make_pair(xm, 0), "k=0 starts at (x_max,0)");
	}
	return r;
}

Result testDistanceToSegment()
{
	Result r{"DistanceToSegment"};
	Rng rng;
	for (int i = 0; i < 800; ++i) {
		Vec2 a(static_cast<float>(rng.uni(-5, 5)), static_cast<float>(rng.uni(-5, 5)));
		Vec2 b(static_cast<float>(rng.uni(-5, 5)), static_cast<float>(rng.uni(-5, 5)));
		Vec2 p(static_cast<float>(rng.uni(-8, 8)), static_cast<float>(rng.uni(-8, 8)));
		double want = 1e300;
		const int N = 20000;
		for (int k = 0; k <= N; ++k) {
			double t = static_cast<double>(k) / N;
			P q = toP(a) + (toP(b) - toP(a)) * t;
			want = std::min(want, distp(toP(p), q));
		}
		double got = rtso::nav::DistanceToSegment(p, a, b);
		expect(r, std::fabs(got - want) < TOL, fmt("segment distance got %g want %g", got, want));
	}
	// Degenerate segment a == b.
	expect(r, std::fabs(rtso::nav::DistanceToSegment(Vec2(3, 4), Vec2(0, 0), Vec2(0, 0)) - 5.0f) < 1e-5f, "degenerate segment");
	// Perpendicular foot inside: distance to the x-axis segment.
	expect(r, std::fabs(rtso::nav::DistanceToSegment(Vec2(1, 2), Vec2(-5, 0), Vec2(5, 0)) - 2.0f) < 1e-5f, "perpendicular foot");
	// Beyond endpoint b.
	// (8,3) to endpoint (5,0): sqrt(3^2 + 3^2).
	expect(r, std::fabs(rtso::nav::DistanceToSegment(Vec2(8, 3), Vec2(-5, 0), Vec2(5, 0)) - std::sqrt(18.0f)) < 1e-5f, "beyond endpoint b");
	return r;
}

} // namespace

int main()
{
	int totalFails = 0;
	report(testPointIsInSquare(), totalFails);
	report(testHalfBoundingBox(), totalFails);
	report(testDistanceToSquare(), totalFails);
	report(testNearestPointOnSquare(), totalFails);
	report(testMaxDistanceToSquare(), totalFails);
	report(testDistanceSquareToSquare(), totalFails);
	report(testMaxDistanceSquareToSquare(), totalFails);
	report(testRaySquare(), totalFails);
	report(testSquareSquare(), totalFails);
	report(testPerimeter(), totalFails);
	report(testDistanceToSegment(), totalFails);

	if (totalFails == 0) {
		std::printf("ALL PASS\n");
		return 0;
	}
	std::printf("FAILED: %d checks\n", totalFails);
	return 1;
}
