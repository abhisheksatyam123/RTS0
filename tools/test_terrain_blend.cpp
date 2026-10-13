// Terrain blend tests: port of 0 A.D. AlphaMapCalculator and CPatchRData::BuildBlends
// (src/presentation/terrain_blend.h).
//
// g++ -std=c++20 -O2 -Isrc tools/test_terrain_blend.cpp -o /tmp/test_terrain_blend
#include "presentation/terrain_blend.h"

#include <cstdio>
#include <vector>

using namespace rtso::terrain_blend;

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

// The blends build() gives the middle tile of a 3x3 map.
static std::vector<const uint8_t *> middle_blends(const std::vector<uint8_t> &out) {
	std::vector<const uint8_t *> r;
	for (int k = 0; k < MAX_BLENDS; ++k) {
		const uint8_t *px = &out[((size_t)1 * 3 * MAX_BLENDS + (size_t)1 * MAX_BLENDS + k) * 4];
		if (px[3]) r.push_back(px);
	}
	return r;
}

int main() {
	// Every shape with a neighbour that has the terrain fits one of 0 A.D.'s alpha maps.
	int bad = 0;
	for (int m = 0; m < 255; ++m) {
		uint8_t s[8];
		for (int k = 0; k < 8; ++k) s[k] = (m >> k) & 1;
		unsigned flags = 0;
		const int am = calculate(s, flags);
		if (am < 0 || am >= ALPHA_MAP_BAD) ++bad;
	}
	CHECK(bad == 0, "all 255 neighbour shapes map to a real alpha map");
	{
		const uint8_t all[8] = {1, 1, 1, 1, 1, 1, 1, 1};
		unsigned f = 0;
		CHECK(calculate(all, f) == -1, "no neighbour with the terrain: nothing drawn");
	}

	// A tile surrounded by a higher-priority terrain: the blend circle, unturned.
	{
		const std::vector<uint8_t> t = {1, 1, 1, 1, 0, 1, 1, 1, 1};
		const auto b = middle_blends(build(3, t.data()));
		CHECK(b.size() == 1 && b[0][0] == 1 && b[0][1] == 0 && b[0][2] == 0, "island tile takes blendcircle");
	}
	// A higher terrain along one side (the -x column): the edge map, turned a quarter.
	{
		const std::vector<uint8_t> t = {1, 0, 0, 1, 0, 0, 1, 0, 0};
		const auto b = middle_blends(build(3, t.data()));
		CHECK(b.size() == 1 && b[0][1] == 2 && b[0][2] == ROTATE_90, "terrain along one side takes blendedge");
		// The other side: 0 A.D. finds the flipped edge first, then turns it.
		const std::vector<uint8_t> u = {0, 0, 1, 0, 0, 1, 0, 0, 1};
		const auto c = middle_blends(build(3, u.data()));
		CHECK(c.size() == 1 && c[0][1] == 2 && c[0][2] == (FLIP_U | ROTATE_270), "the opposite side flips and turns it");
	}
	// Lower-priority neighbours draw nothing over the tile.
	{
		const std::vector<uint8_t> t = {0, 0, 0, 0, 1, 0, 0, 0, 0};
		CHECK(middle_blends(build(3, t.data())).empty(), "a tile above its neighbours keeps its own terrain");
	}
	// Eight different higher terrains: the highest MAX_BLENDS, lowest first.
	{
		const std::vector<uint8_t> t = {1, 2, 3, 4, 0, 5, 6, 7, 8};
		const auto b = middle_blends(build(3, t.data()));
		bool ordered = b.size() == (size_t)MAX_BLENDS;
		for (size_t k = 0; ordered && k < b.size(); ++k) ordered = b[k][0] == 9 - MAX_BLENDS + k;
		CHECK(ordered, "blends sorted by priority, the lowest past MAX_BLENDS left out");
	}
	// Map edges count as not having the terrain (0 A.D. skips tiles off the map).
	{
		const std::vector<uint8_t> t = {1, 1, 0, 0};
		const auto out = build(2, t.data());
		const uint8_t *px = &out[((size_t)1 * 2 * MAX_BLENDS) * 4]; // tile (0, 1)
		CHECK(px[3] == 255 && px[0] == 1, "corner tile blends the row beside it");
	}

	std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
	return failures ? 1 : 0;
}
