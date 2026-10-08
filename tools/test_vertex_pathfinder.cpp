// RTS0 VertexPathfinder test (headless: no SCons, no Godot).
// Differential test for src/simulation/nav/vertex_pathfinder.h, the port of
// 0 A.D. VertexPathfinder::ComputeShortPath.
// Build and run:
//   g++ -std=c++20 -O2 -Isrc tools/test_vertex_pathfinder.cpp -o /tmp/tvp && /tmp/tvp
//
// Checks, over deterministic random scenes (buildings incl. rotated ones, unit clusters,
// impassable terrain, random clearance/range, point and circle/square goals):
//  1. Collision: every segment of every returned path is collision-free against the statics and
//     units expanded by the clearance (geometry.h TestRaySquare), against impassable navcells
//     (open interior), and stays in the search box. Waypoints are never inside an expanded shape.
//  2. Reachability: when the goal is reachable, the path ends on the goal shape; when it is not,
//     the result is false, and any partial path is collision-free and ends no further from the goal.
//  3. Optimality: with heuristic_weight = 1 (exact A*), the path length is within 1e-3 relative of
//     an independent Dijkstra over the same visibility vertices. The oracle below is written
//     separately: it builds the vertex set, visibility and goal handling from the geometry, with
//     its own Liang-Barsky segment tests, and does not call geometry.h or the code under test.
//     The vendor default weight (4/3 or 5/3) is checked for collision-freedom and reported.
//  4. Determinism: the same request twice gives bit-identical waypoints.
//  5. Timing: average microseconds per query for scenes with about 10, 50 and 150 obstructions.
// Exit: 0 = PASS, 1 = FAIL.

#include "simulation/nav/geometry.h"
#include "simulation/nav/vertex_pathfinder.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace rtso::nav;

namespace {

constexpr int W = 64;
constexpr int H = 64;
constexpr float CELL = 2.0f;
const Vec2 ORIGIN(-64.0f, -64.0f);
constexpr double REL_TOL = 1e-3;
constexpr double ABS_TOL = 1e-4;
constexpr double INF = 1e300;
constexpr double SAMPLE_MARGIN = 0.05;

// Scene: statics are buildings, units are unit squares, blocked is W*H impassable navcells.
struct Scene {
	std::vector<ShortPathSquare> statics;
	std::vector<ShortPathSquare> units;
	std::vector<uint8_t> blocked;
	float clearance = 1.0f;
	float bias = 0.0f;
	float range = 20.0f;
	Vec2 start;
	ShortPathGoal goal;

	bool passable(int i, int j) const {
		return i >= 0 && j >= 0 && i < W && j < H && !blocked[static_cast<size_t>(j * W + i)];
	}
	// Expansion of a unit square: clearance minus bias. Statics use the full clearance.
	float unitExpansion() const { return clearance - bias; }
};

struct Rng {
	std::mt19937 gen;
	explicit Rng(uint32_t seed) : gen(seed) {}
	double uni(double lo, double hi) { return std::uniform_real_distribution<double>(lo, hi)(gen); }
	int in(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(gen); }
};

// Independent double-precision helpers used by the oracle and the collision checks.
struct P {
	double x, y;
};
P operator+(P a, P b) { return P{a.x + b.x, a.y + b.y}; }
P operator-(P a, P b) { return P{a.x - b.x, a.y - b.y}; }
P operator*(P a, double s) { return P{a.x * s, a.y * s}; }
double dot(P a, P b) { return a.x * b.x + a.y * b.y; }
double dist(P a, P b) { return std::sqrt(dot(a - b, a - b)); }
P toP(Vec2 v) { return P{v.x, v.y}; }

// Closed segment a-b against a closed axis-aligned rectangle [x0,x1] x [y0,y1] (Liang-Barsky).
bool segHitsRect(P a, P b, double x0, double x1, double y0, double y1) {
	double dx = b.x - a.x, dy = b.y - a.y;
	double p[4] = {-dx, dx, -dy, dy};
	double q[4] = {a.x - x0, x1 - a.x, a.y - y0, y1 - a.y};
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

// A square expanded by e (static clearance, or unit clearance minus bias), in world coordinates.
struct Solid {
	P c, u, v;
	double hw, hh;
};

Solid expanded(const ShortPathSquare& s, double e) {
	return Solid{toP(s.center), toP(s.u), toP(s.v), s.halfSize.x + e, s.halfSize.y + e};
}

// Closed segment against a closed solid square (endpoints inside count as hits).
bool segHitsSolid(const Solid& s, P a, P b) {
	P la{dot(a - s.c, s.u), dot(a - s.c, s.v)};
	P lb{dot(b - s.c, s.u), dot(b - s.c, s.v)};
	return segHitsRect(la, lb, -s.hw, s.hw, -s.hh, s.hh);
}

bool pointInsideSolid(const Solid& s, P p, double eps) {
	return std::fabs(dot(p - s.c, s.u)) < s.hw - eps && std::fabs(dot(p - s.c, s.v)) < s.hh - eps;
}

// Does the closed segment a-b cross the open interior of an impassable navcell?
bool segHitsTerrain(const Scene& s, P a, P b) {
	const double eps = 1e-4;
	int i0 = static_cast<int>(std::floor((std::min(a.x, b.x) - ORIGIN.x) / CELL)) - 1;
	int i1 = static_cast<int>(std::floor((std::max(a.x, b.x) - ORIGIN.x) / CELL)) + 1;
	int j0 = static_cast<int>(std::floor((std::min(a.y, b.y) - ORIGIN.y) / CELL)) - 1;
	int j1 = static_cast<int>(std::floor((std::max(a.y, b.y) - ORIGIN.y) / CELL)) + 1;
	i0 = std::max(i0, 0);
	j0 = std::max(j0, 0);
	i1 = std::min(i1, W - 1);
	j1 = std::min(j1, H - 1);
	for (int j = j0; j <= j1; ++j)
		for (int i = i0; i <= i1; ++i) {
			if (s.passable(i, j)) continue;
			double x0 = ORIGIN.x + i * CELL, z0 = ORIGIN.y + j * CELL;
			if (segHitsRect(a, b, x0 + eps, x0 + CELL - eps, z0 + eps, z0 + CELL - eps)) return true;
		}
	return false;
}

// Search box, from the vendor range rule (vendor 558-579), written again here.
struct Box {
	double x0, x1, z0, z1;
	bool contains(P p, double tol) const {
		return p.x >= x0 - tol && p.x <= x1 + tol && p.y >= z0 - tol && p.y <= z1 + tol;
	}
};

Box searchBox(const Scene& s) {
	const double r = s.range;
	const double sx = s.start.x, sz = s.start.y;
	Box b{sx - r, sx + r, sz - r, sz + r};
	const double dx = s.goal.center.x - sx, dz = s.goal.center.y - sz;
	const double len = std::sqrt(dx * dx + dz * dz);
	if (len > 0.0 && len >= r && r < 46.0 * CELL) {
		const double sh = std::min(len / 2.0, r * 3.0 / 5.0);
		b.x0 += dx * sh / len;
		b.x1 += dx * sh / len;
		b.z0 += dz * sh / len;
		b.z1 += dz * sh / len;
	}
	return b;
}

// Visibility predicate of the oracle: a segment is open if it hits no expanded solid, no
// impassable navcell interior, and stays in the box (the box is convex, so endpoints suffice).
struct Oracle {
	const Scene& s;
	std::vector<Solid> solids;
	Box box;
	double delta;

	explicit Oracle(const Scene& sc) : s(sc), box(searchBox(sc)), delta(CELL / 16.0) {
		for (const auto& st : s.statics) solids.push_back(expanded(st, s.clearance));
		for (const auto& un : s.units) solids.push_back(expanded(un, s.unitExpansion()));
	}

	bool open(P a, P b) const {
		for (const Solid& so : solids)
			if (segHitsSolid(so, a, b)) return false;
		return !segHitsTerrain(s, a, b);
	}

	bool insideAnySolid(P p) const {
		for (const Solid& so : solids)
			if (pointInsideSolid(so, p, 0.0)) return true;
		return false;
	}

	// Goal point seen from u: nearest goal point, clamped into the box by delta.
	P goalPoint(P u) const {
		Vec2 n = s.goal.nearest_point(Vec2(static_cast<float>(u.x), static_cast<float>(u.y)));
		double x = n.x, z = n.y;
		x = std::min(std::max(x, box.x0 + delta), box.x1 - delta);
		z = std::min(std::max(z, box.z0 + delta), box.z1 - delta);
		return P{x, z};
	}

	// Vertex set: start, then the corners of every expanded solid pushed outward by delta,
	// then the convex corners of impassable terrain pushed outward by delta. Only vertices in
	// the box and outside every expanded solid are kept.
	std::vector<P> vertices() const {
		std::vector<P> v;
		v.push_back(toP(s.start));
		for (size_t k = 0; k < solids.size(); ++k) {
			const Solid& so = solids[k];
			const double hw = so.hw + delta, hh = so.hh + delta;
			const P sx[4] = {so.c - so.u * hw + so.v * hh, so.c + so.u * hw + so.v * hh,
			                 so.c + so.u * hw - so.v * hh, so.c - so.u * hw - so.v * hh};
			for (const P& p : sx)
				if (box.contains(p, 0.0) && !insideAnySolid(p)) v.push_back(p);
		}
		for (int j = 0; j < H; ++j)
			for (int i = 0; i < W; ++i) {
				if (s.passable(i, j)) continue;
				const double x = ORIGIN.x + i * CELL, z = ORIGIN.y + j * CELL;
				// Convex corner in the +x,+z direction: the 2x2 block around it is passable apart from this cell.
				if (s.passable(i + 1, j) && s.passable(i, j + 1) && s.passable(i + 1, j + 1))
					pushTerrain(v, P{x + CELL + delta, z + CELL + delta});
				if (s.passable(i - 1, j) && s.passable(i, j + 1) && s.passable(i - 1, j + 1))
					pushTerrain(v, P{x - delta, z + CELL + delta});
				if (s.passable(i + 1, j) && s.passable(i, j - 1) && s.passable(i + 1, j - 1))
					pushTerrain(v, P{x + CELL + delta, z - delta});
				if (s.passable(i - 1, j) && s.passable(i, j - 1) && s.passable(i - 1, j - 1))
					pushTerrain(v, P{x - delta, z - delta});
			}
		return v;
	}

	void pushTerrain(std::vector<P>& v, P p) const {
		if (box.contains(p, 0.0) && !insideAnySolid(p)) v.push_back(p);
	}

	// Optimal length over the visibility graph (goal treated as a moving point), or INF.
	double optimum() const {
		const std::vector<P> nodes = vertices();
		const size_t n = nodes.size();
		std::vector<double> d(n, INF);
		std::vector<uint8_t> done(n, 0);
		// Full visibility matrix, then plain O(V^2) Dijkstra.
		std::vector<uint8_t> vis(n * n, 0);
		for (size_t a = 0; a < n; ++a)
			for (size_t b = a + 1; b < n; ++b) {
				const bool ok = open(nodes[a], nodes[b]);
				vis[a * n + b] = vis[b * n + a] = ok ? 1 : 0;
			}
		d[0] = 0.0;
		for (size_t iter = 0; iter < n; ++iter) {
			size_t u = n;
			for (size_t k = 0; k < n; ++k)
				if (!done[k] && d[k] < INF && (u == n || d[k] < d[u])) u = k;
			if (u == n) break;
			done[u] = 1;
			for (size_t k = 0; k < n; ++k)
				if (!done[k] && vis[u * n + k])
					d[k] = std::min(d[k], d[u] + dist(nodes[u], nodes[k]));
		}
		double best = INF;
		for (size_t u = 0; u < n; ++u) {
			if (d[u] >= INF) continue;
			const P q = goalPoint(nodes[u]);
			if (open(nodes[u], q)) best = std::min(best, d[u] + dist(nodes[u], q));
		}
		return best;
	}
};

// Build a random scene. Returns false if no valid start point was found.
bool buildScene(uint32_t seed, int nStatic, int nUnit, float bias, Scene& s) {
	Rng rng(seed * 2654435761u + 17u);
	s = Scene();
	s.bias = bias;
	s.blocked.assign(static_cast<size_t>(W * H), 0);
	for (int j = 0; j < H; ++j)
		for (int i = 0; i < W; ++i)
			if (i == 0 || j == 0 || i == W - 1 || j == H - 1) s.blocked[static_cast<size_t>(j * W + i)] = 1;

	// Impassable terrain blobs (rectangles of 1-4 navcells), kept off the outer ring.
	const int blobs = rng.in(0, 5);
	for (int b = 0; b < blobs; ++b) {
		const int ci = rng.in(3, W - 5), cj = rng.in(3, H - 5);
		const int wi = rng.in(1, 4), wj = rng.in(1, 4);
		for (int j = cj; j < std::min(cj + wj, H - 1); ++j)
			for (int i = ci; i < std::min(ci + wi, W - 1); ++i)
				s.blocked[static_cast<size_t>(j * W + i)] = 1;
	}

	for (int k = 0; k < nStatic; ++k) {
		ShortPathSquare b;
		b.center = Vec2(static_cast<float>(rng.uni(-50, 50)), static_cast<float>(rng.uni(-50, 50)));
		const bool axis = rng.in(0, 1) == 0;
		if (axis) {
			b.u = Vec2(1.0f, 0.0f);
			b.v = Vec2(0.0f, 1.0f);
		} else {
			const double a = rng.uni(0.0, 2.0 * M_PI);
			b.u = Vec2(static_cast<float>(std::cos(a)), static_cast<float>(std::sin(a)));
			b.v = b.u.perp();
		}
		b.halfSize = Vec2(static_cast<float>(rng.uni(1.0, 4.0)), static_cast<float>(rng.uni(1.0, 4.0)));
		s.statics.push_back(b);
	}

	// Unit squares in clusters around a few centres, half-size 0.5.
	const int clusters = std::max(1, rng.in(1, 3));
	std::vector<Vec2> centres;
	for (int c = 0; c < clusters; ++c)
		centres.push_back(Vec2(static_cast<float>(rng.uni(-45, 45)), static_cast<float>(rng.uni(-45, 45))));
	for (int k = 0; k < nUnit; ++k) {
		const Vec2 c = centres[static_cast<size_t>(k % clusters)];
		ShortPathSquare un;
		un.center = Vec2(c.x + static_cast<float>(rng.uni(-3, 3)), c.y + static_cast<float>(rng.uni(-3, 3)));
		un.u = Vec2(1.0f, 0.0f);
		un.v = Vec2(0.0f, 1.0f);
		un.halfSize = Vec2(0.5f, 0.5f);
		s.units.push_back(un);
	}

	s.clearance = static_cast<float>(rng.uni(0.5, 3.0));
	s.range = static_cast<float>(rng.uni(8.0, 60.0));

	// Start: outside every expanded solid and every impassable navcell, inside the map.
	bool found = false;
	for (int attempt = 0; attempt < 300 && !found; ++attempt) {
		s.start = Vec2(static_cast<float>(rng.uni(-40, 40)), static_cast<float>(rng.uni(-40, 40)));
		bool ok = true;
		for (const auto& st : s.statics) {
			const Solid so = expanded(st, s.clearance);
			if (pointInsideSolid(so, toP(s.start), -SAMPLE_MARGIN)) { ok = false; break; }
		}
		for (const auto& un : s.units) {
			if (!ok) break;
			const Solid so = expanded(un, s.unitExpansion());
			if (pointInsideSolid(so, toP(s.start), -SAMPLE_MARGIN)) ok = false;
		}
		if (ok) {
			const int i = static_cast<int>(std::floor((s.start.x - ORIGIN.x) / CELL));
			const int j = static_cast<int>(std::floor((s.start.y - ORIGIN.y) / CELL));
			if (!s.passable(i, j)) ok = false;
		}
		found = ok;
	}
	if (!found) return false;

	// Goal: point, circle or square, placed within the range around the start.
	const int kind = rng.in(0, 3); // 0,1 point; 2 circle; 3 square
	const float gx = s.start.x + static_cast<float>(rng.uni(-s.range, s.range));
	const float gz = s.start.y + static_cast<float>(rng.uni(-s.range, s.range));
	s.goal.center = Vec2(std::min(std::max(gx, -60.0f), 60.0f), std::min(std::max(gz, -60.0f), 60.0f));
	if (kind <= 1) {
		s.goal.type = ShortPathGoal::POINT;
	} else if (kind == 2) {
		s.goal.type = ShortPathGoal::CIRCLE;
		s.goal.hw = static_cast<float>(rng.uni(0.5, 3.0));
	} else {
		s.goal.type = ShortPathGoal::SQUARE;
		const double a = rng.uni(0.0, 2.0 * M_PI);
		s.goal.u = Vec2(static_cast<float>(std::cos(a)), static_cast<float>(std::sin(a)));
		s.goal.v = s.goal.u.perp();
		s.goal.hw = static_cast<float>(rng.uni(0.5, 3.0));
		s.goal.hh = static_cast<float>(rng.uni(0.5, 3.0));
	}
	return true;
}

bool runQuery(const Scene& s, float weight, std::vector<Vec2>& out) {
	ShortPathRequest req;
	req.start = s.start;
	req.clearance = s.clearance;
	req.range = s.range;
	req.goal = s.goal;
	req.unit_clearance_bias = s.bias;
	req.heuristic_weight = weight;
	const Scene* sc = &s;
	auto pass = [sc](int i, int j) { return sc->passable(i, j); };
	return compute_short_path(req, s.statics, s.units, pass, CELL, ORIGIN, W, H, out);
}

double pathLength(Vec2 start, const std::vector<Vec2>& wp) {
	double len = 0.0;
	P prev = toP(start);
	for (const Vec2& p : wp) {
		len += dist(prev, toP(p));
		prev = toP(p);
	}
	return len;
}

// Returns an empty string if the path is collision-free; otherwise describes the first violation.
std::string checkPath(const Scene& s, const std::vector<Vec2>& wp) {
	const Box box = searchBox(s);
	std::vector<Vec2> pts;
	pts.push_back(s.start);
	pts.insert(pts.end(), wp.begin(), wp.end());
	for (size_t k = 1; k < pts.size(); ++k) {
		const Vec2 a = pts[k - 1], b = pts[k];
		// geometry.h TestRaySquare takes endpoints relative to the square's centre.
		for (const auto& st : s.statics) {
			if (TestRaySquare(a - st.center, b - st.center, st.u, st.v, st.halfSize + Vec2(s.clearance, s.clearance)))
				return "segment crosses a static (expanded by clearance)";
		}
		for (const auto& un : s.units) {
			const float e = s.unitExpansion();
			if (TestRaySquare(a - un.center, b - un.center, un.u, un.v, un.halfSize + Vec2(e, e)))
				return "segment crosses a unit (expanded)";
		}
		if (segHitsTerrain(s, toP(a), toP(b))) return "segment crosses an impassable navcell";
		if (!box.contains(toP(b), 1e-4)) return "waypoint leaves the search box";
		// Endpoint inside an expanded shape: TestRaySquare treats an inside start as clear, so check it here.
		for (const auto& st : s.statics) {
			const Solid so = expanded(st, s.clearance);
			if (pointInsideSolid(so, toP(b), 1e-5)) return "waypoint inside a static";
		}
		for (const auto& un : s.units) {
			const Solid so = expanded(un, s.unitExpansion());
			if (pointInsideSolid(so, toP(b), 1e-5)) return "waypoint inside a unit";
		}
	}
	return "";
}

// The path must end on the goal shape. Vendor 811-812 clamps the goal vertex into the search box
// shrunk by one delta, so a goal that touches that edge can end up to 2 * delta short. The looser
// tolerance applies only in that case.
bool goalSatisfied(const Scene& s, Vec2 last, const Box& box) {
	const double delta = CELL / 16.0;
	const double extent = s.goal.type == ShortPathGoal::POINT ? 0.0
		: (s.goal.type == ShortPathGoal::CIRCLE ? s.goal.hw : s.goal.hw + s.goal.hh);
	const P c = toP(s.goal.center);
	const bool nearEdge = c.x - extent < box.x0 + delta || c.x + extent > box.x1 - delta ||
	                      c.y - extent < box.z0 + delta || c.y + extent > box.z1 - delta;
	const double tol = nearEdge ? 2.0 * delta + 1e-3 : 1e-3;
	const double d = (last - s.goal.nearest_point(last)).length();
	return d <= tol;
}

struct Counter {
	const char* name;
	int checks = 0;
	int fails = 0;
	std::string first;
	explicit Counter(const char* n) : name(n) {}
	void expect(bool ok, const std::string& msg) {
		++checks;
		if (!ok) {
			if (fails == 0) first = msg;
			++fails;
		}
	}
};

std::string fmt(const char* f, double a, double b = 0.0, double c = 0.0) {
	char buf[512];
	std::snprintf(buf, sizeof(buf), f, a, b, c);
	return buf;
}

} // namespace

int main() {
	std::printf("RTS0 VertexPathfinder test (src/simulation/nav/vertex_pathfinder.h)\n");

	Counter collision{"collision"}, reach{"reachability"}, optimal{"optimality"}, determinism{"determinism"};
	int scenes = 0, skipped = 0, reachable = 0, unreachable = 0;
	double maxRelErr = 0.0, maxDefaultRatio = 1.0, sumDefaultRatio = 0.0;
	int defaultCompared = 0, partialPaths = 0;

	const int SCENES = 1500;
	for (int seed = 1; seed <= SCENES; ++seed) {
		// Obstruction counts vary, and every other scene uses the vendor unit bias (0.5).
		const int nStatic = 3 + (seed * 7) % 18;
		const int nUnit = (seed * 5) % 16;
		const float bias = (seed % 2 == 0) ? 0.5f : 0.0f;
		Scene s;
		if (!buildScene(static_cast<uint32_t>(seed), nStatic, nUnit, bias, s)) {
			++skipped;
			continue;
		}
		++scenes;

		std::vector<Vec2> exact, def, exact2, def2;
		const bool okExact = runQuery(s, 1.0f, exact);
		const bool okDef = runQuery(s, 0.0f, def);
		const bool okExact2 = runQuery(s, 1.0f, exact2);
		const bool okDef2 = runQuery(s, 0.0f, def2);

		// 4. Determinism (bit-identical waypoints and flags).
		const bool sameExact = okExact == okExact2 && exact.size() == exact2.size() &&
		                       (exact.empty() || std::memcmp(exact.data(), exact2.data(), exact.size() * sizeof(Vec2)) == 0);
		const bool sameDef = okDef == okDef2 && def.size() == def2.size() &&
		                     (def.empty() || std::memcmp(def.data(), def2.data(), def.size() * sizeof(Vec2)) == 0);
		determinism.expect(sameExact && sameDef, fmt("non-deterministic result for seed %g", seed));

		// 1. Collision, for both weights.
		const std::string ce = checkPath(s, exact);
		collision.expect(ce.empty(), fmt("exact path violates collision (seed %g)", seed) + ce);
		const std::string cd = checkPath(s, def);
		collision.expect(cd.empty(), fmt("default-weight path violates collision (seed %g)", seed) + cd);

		// 3. Oracle.
		Oracle oracle(s);
		const double opt = oracle.optimum();
		const bool oracleReachable = opt < INF;

		if (oracleReachable) {
			++reachable;
			reach.expect(okExact, fmt("reachable goal not reached (seed %g)", seed));
			if (okExact) {
				// An empty path means the start already satisfies the goal (opt is then 0).
				reach.expect(exact.empty() ? opt <= ABS_TOL : goalSatisfied(s, exact.back(), searchBox(s)),
				             fmt("path does not end on goal (seed %g)", seed));
				const double len = pathLength(s.start, exact);
				const double err = std::fabs(len - opt);
				const double rel = opt > ABS_TOL ? err / opt : err;
				maxRelErr = std::max(maxRelErr, rel);
				optimal.expect(rel <= REL_TOL || err <= ABS_TOL,
				               fmt("exact length %g vs oracle %g", len, opt, 0.0));
			}
		} else {
			++unreachable;
			reach.expect(!okExact || exact.empty() || goalSatisfied(s, exact.back(), searchBox(s)),
			             fmt("claims reached but oracle says unreachable (seed %g)", seed));
			if (!exact.empty()) {
				++partialPaths;
				const double dEnd = (exact.back() - s.goal.nearest_point(exact.back())).length();
				const double dStart = (s.start - s.goal.nearest_point(s.start)).length();
				reach.expect(dEnd <= dStart + 1e-3, fmt("partial path ends further from goal (seed %g)", seed));
			}
		}

		// Default weight: reachability agrees and length is within the weighted-A* bound.
		if (okDef != okExact && oracleReachable) reach.expect(false, fmt("default weight misses reachable goal (seed %g)", seed));
		if (okDef && oracleReachable) {
			const double ratio = pathLength(s.start, def) / std::max(opt, ABS_TOL);
			maxDefaultRatio = std::max(maxDefaultRatio, ratio);
			sumDefaultRatio += ratio;
			++defaultCompared;
		}
	}

	// 5. Timing at ~10, ~50 and ~150 obstructions (statics + units), default weight.
	struct Timing {
		int obstructions;
		double us;
		int queries;
	};
	std::vector<Timing> timings;
	for (int target : {10, 50, 150}) {
		double total = 0.0;
		int n = 0;
		for (int q = 0; q < 200; ++q) {
			const int nStatic = (target * 7) / 10;
			const int nUnit = target - nStatic;
			Scene s;
			if (!buildScene(static_cast<uint32_t>(100000 + target * 1000 + q), nStatic, nUnit, 0.5f, s)) continue;
			std::vector<Vec2> out;
			const auto t0 = std::chrono::steady_clock::now();
			runQuery(s, 0.0f, out);
			const auto t1 = std::chrono::steady_clock::now();
			total += std::chrono::duration<double, std::micro>(t1 - t0).count();
			++n;
		}
		timings.push_back(Timing{target, n ? total / n : 0.0, n});
	}

	const Counter* all[] = {&collision, &reach, &optimal, &determinism};
	int totalFails = 0;
	for (const Counter* c : all) {
		totalFails += c->fails;
		if (c->fails == 0)
			std::printf("PASS  %-14s (%d checks)\n", c->name, c->checks);
		else
			std::printf("FAIL  %-14s %d/%d failed; first: %s\n", c->name, c->fails, c->checks, c->first.c_str());
	}
	std::printf("  scenes: %d run, %d skipped (no valid start); reachable %d, unreachable %d, partial paths %d\n",
	            scenes, skipped, reachable, unreachable, partialPaths);
	std::printf("  exact A* (weight 1): max relative length error vs Dijkstra %.2e (tolerance %.0e)\n", maxRelErr, REL_TOL);
	std::printf("  vendor weight (4/3 or 5/3): max length ratio %.3f, mean %.3f over %d reachable queries\n",
	            maxDefaultRatio, defaultCompared ? sumDefaultRatio / defaultCompared : 0.0, defaultCompared);
	for (const Timing& t : timings)
		std::printf("  timing ~%3d obstructions: %8.1f us/query (%d queries, vendor weight)\n", t.obstructions, t.us, t.queries);

	if (totalFails) {
		std::printf("FAIL (%d)\n", totalFails);
		return 1;
	}
	std::printf("PASS\n");
	return 0;
}
