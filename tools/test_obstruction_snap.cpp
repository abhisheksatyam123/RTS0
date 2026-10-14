// Snap to edges tests: 0 A.D. 0.28 ObstructionSnap.getPosition over
// GetEdgesOfStaticObstructionsOnScreenNearTo edges (src/presentation/obstruction_snap.h). Build:
//   g++ -std=c++20 -O2 -Isrc tools/test_obstruction_snap.cpp -o /tmp/test_obstruction_snap

#include "presentation/obstruction_snap.h"

#include <cmath>
#include <cstdio>

using namespace rtso::snap;

static int g_fail = 0;
#define CHECK(cond)                                                       \
	do {                                                                  \
		if (!(cond)) {                                                    \
			std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			++g_fail;                                                     \
		}                                                                 \
	} while (0)

static bool near(float a, float b) { return std::fabs(a - b) < 1e-3f; }

int main() {
	constexpr float kPi = 3.14159265358979f;
	// A 4x4 structure at the origin, unturned.
	const std::vector<Rect> one = {{0.0f, 0.0f, 2.0f, 2.0f, 0.0f}};
	{
		// Each side within the threshold of a point beside it, outward normals.
		const auto e = edges_near(one, 2.5f, 0.0f);
		CHECK(e.size() == 3); // the far side (x = -2) is 4.5 away, past 3.75
		bool right = false;
		for (const Edge &x : e)
			if (near(x.normal.x, 1.0f) && near(x.normal.y, 0.0f)) right = true;
		CHECK(right);
		CHECK(edges_near(one, 20.0f, 0.0f).empty());
		// Obstructions of 2 m or less give no edges.
		CHECK(edges_near({{0.0f, 0.0f, 0.5f, 0.5f, 0.0f}}, 0.6f, 0.0f).empty());
	}
	{
		// A same-size template beside its right side: turned to its axes, pushed flush (plus
		// the 0.05 m padding), and its top lined up with the structure's top from inside.
		Result r{};
		CHECK(snap_position(edges_near(one, 4.5f, 0.3f), 2.0f, 2.0f, 4.5f, 0.3f, 0.3f, r));
		CHECK(near(r.angle, 0.0f));
		CHECK(near(r.x, 4.0f + kPadding));
		CHECK(near(r.z, 0.0f));
	}
	{
		// A turned structure: the template takes its angle, or a quarter turn more when
		// that is within 45 degrees of the template's own.
		const std::vector<Rect> turned = {{0.0f, 0.0f, 2.0f, 2.0f, 0.4f}};
		Result r{};
		CHECK(snap_position(edges_near(turned, 4.0f, 1.0f), 2.0f, 2.0f, 4.0f, 1.0f, 0.6f, r));
		CHECK(near(r.angle, 0.4f));
		CHECK(snap_position(edges_near(turned, 4.0f, 1.0f), 2.0f, 2.0f, 4.0f, 1.0f, 2.0f, r));
		CHECK(near(r.angle, 0.4f + kPi / 2.0f));
		// Flush: the template's centre is one full width from the structure's along the normal.
		const Vec n = rotate(Vec{1.0f, 0.0f}, 0.4f);
		CHECK(std::fabs(dot(Vec{r.x, r.z}, n) - (4.0f + kPadding)) < 1e-2f);
	}
	{
		// Nothing near: no snap.
		Result r{};
		CHECK(!snap_position(edges_near(one, 12.0f, 0.0f), 2.0f, 2.0f, 12.0f, 0.0f, 0.0f, r));
	}
	if (g_fail) return 1;
	std::printf("obstruction snap tests passed\n");
	return 0;
}
