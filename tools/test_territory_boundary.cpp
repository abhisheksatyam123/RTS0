// Territory border tests: port of 0 A.D. TerritoryBoundary.cpp and SimRender spline helpers
// (src/presentation/territory_boundary.cpp).
//
// g++ -std=c++20 -O2 -Isrc tools/test_territory_boundary.cpp src/presentation/territory_boundary.cpp \
//   -o /tmp/test_territory_boundary
#include "presentation/territory_boundary.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace rtso;

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

static float signed_area(const std::vector<BoundaryPoint> &p) {
	float a = 0.0f;
	for (size_t i = 0; i < p.size(); ++i) {
		const BoundaryPoint &u = p[i], &v = p[(i + 1) % p.size()];
		a += u.x * v.y - v.x * u.y;
	}
	return a * 0.5f;
}

int main() {
	// One owned cell: a diamond through its four edge midpoints.
	{
		std::vector<uint8_t> g(9, 0);
		g[1 * 3 + 1] = 2;
		auto b = compute_territory_boundaries(g.data(), 3, 3);
		CHECK(b.size() == 1 && b[0].owner == 2, "one cell gives one outline with its owner");
		CHECK(b.size() == 1 && b[0].points.size() == 4, "one cell outline has four edge midpoints");
		CHECK(b.size() == 1 && signed_area(b[0].points) > 0.0f, "outline runs counter-clockwise (owner on the left)");
	}
	// Two players side by side, one with a hole: three outlines.
	{
		const int W = 10, H = 6;
		std::vector<uint8_t> g(W * H, 0);
		for (int j = 0; j < H; ++j)
			for (int i = 0; i < W; ++i) g[j * W + i] = i < 5 ? 1 : 2;
		g[2 * W + 2] = 0; // hole in player 1's land
		auto b = compute_territory_boundaries(g.data(), W, H);
		int p1 = 0, p2 = 0;
		for (auto &x : b) (x.owner == 1 ? p1 : p2)++;
		CHECK(b.size() == 3, "two territories and a hole give three outlines");
		CHECK(p1 == 2 && p2 == 1, "the hole is traced as part of its owner's border");
		float outer = 0.0f;
		for (auto &x : b)
			if (x.owner == 1) outer = std::fmax(outer, signed_area(x.points));
		CHECK(std::fabs(outer - 30.0f) < 0.6f, "outer outline encloses the owner's 5x6 cells");
	}
	// Smoothing and the RNS spline keep a closed loop and pull it inward.
	{
		std::vector<uint8_t> g(64 * 64, 0);
		for (int j = 16; j < 48; ++j)
			for (int i = 16; i < 48; ++i)
				if ((i - 32) * (i - 32) + (j - 32) * (j - 32) < 14 * 14) g[j * 64 + i] = 1;
		auto b = compute_territory_boundaries(g.data(), 64, 64);
		CHECK(b.size() == 1, "a disc gives one outline");
		auto pts = b[0].points;
		const float area0 = signed_area(pts);
		smooth_points_average(pts);
		interpolate_points_rns(pts, 0.5f);
		CHECK(pts.size() == b[0].points.size() * 4, "RNS samples four points per segment");
		const float area1 = signed_area(pts);
		CHECK(area1 > 0.0f && area1 < area0, "the border offset moves it inside its territory");
		bool finite = true;
		for (auto &p : pts) finite = finite && std::isfinite(p.x) && std::isfinite(p.y);
		CHECK(finite, "spline points are finite");
	}
	// Cost on a Giant map grid (256x256) with 8 players' land.
	{
		const int G = 256;
		std::vector<uint8_t> g(G * G, 0);
		for (int p = 0; p < 8; ++p) {
			const float cx = 128 + 90 * std::cos(p * 0.785f), cz = 128 + 90 * std::sin(p * 0.785f);
			for (int j = 0; j < G; ++j)
				for (int i = 0; i < G; ++i)
					if ((i - cx) * (i - cx) + (j - cz) * (j - cz) < 30 * 30) g[j * G + i] = (uint8_t)(p + 1);
		}
		const int N = 50;
		auto t0 = std::chrono::steady_clock::now();
		size_t pts = 0;
		for (int k = 0; k < N; ++k) {
			auto b = compute_territory_boundaries(g.data(), G, G);
			for (auto &x : b) {
				smooth_points_average(x.points);
				interpolate_points_rns(x.points, 0.5f);
				pts += x.points.size();
			}
		}
		auto t1 = std::chrono::steady_clock::now();
		std::printf("     giant cost: %.1f us per territory change (%zu border points)\n",
				std::chrono::duration<double, std::micro>(t1 - t0).count() / N, pts / N);
	}
	if (failures) {
		std::printf("%d FAILED\n", failures);
		return 1;
	}
	std::printf("all territory boundary tests passed\n");
	return 0;
}
