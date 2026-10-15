#pragma once
// Terrain blends between tiles, ported from 0 A.D. (GPL-2.0+):
// source/renderer/AlphaMapCalculator.cpp, BlendShapes.h and
// CPatchRData::BuildBlends/AddBlend. Each tile draws its own terrain, then each
// neighbouring terrain of higher priority over it, lowest first, masked by one of
// the 14 alpha maps (assets/textures/terrain/alphamaps/standard, CC BY-SA 3.0)
// chosen, flipped and rotated to fit which of the 8 neighbours have that terrain.
// A tile's priority is its terrain's index in the map's name list
// (Simulation::TerrainTiles keeps the names in priority order).
#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <vector>

namespace rtso::terrain_blend {

// AlphaMapCalculator.h's BLENDMAP_* flags.
enum : uint8_t { FLIP_V = 0x01, FLIP_U = 0x02, ROTATE_90 = 0x04, ROTATE_180 = 0x08, ROTATE_270 = 0x10 };

// CTerrainTextureManager::LoadAlphaMap's order; calculate() returns these indices.
constexpr int ALPHA_MAP_COUNT = 14;
constexpr const char *ALPHA_MAP_NAMES[ALPHA_MAP_COUNT] = {
	"blendcircle", "blendlshape", "blendedge", "blendedgecorner", "blendedgetwocorners", "blendfourcorners",
	"blendtwooppositecorners", "blendlshapecorner", "blendtwocorners", "blendcorner", "blendtwoedges",
	"blendthreecorners", "blendushape", "blendbad"};
constexpr int ALPHA_MAP_BAD = 13;

// Neighbour n of a tile, as {dz, dx} (PatchRData.cpp BlendOffsets); 8 is the tile itself.
constexpr int BLEND_OFFSETS[9][2] = {{0, -1}, {-1, -1}, {-1, 0}, {-1, 1}, {0, 1}, {1, 1}, {1, 0}, {1, -1}, {0, 0}};

namespace detail {

template <int N>
struct Shape {
	uint8_t d[N] = {};
	bool operator==(const Shape &o) const {
		for (int i = 0; i < N; ++i)
			if (d[i] != o.d[i]) return false;
		return true;
	}
	// BlendShape4/BlendShape8 Rotate90, Rotate180, Rotate270, FlipU, FlipV.
	Shape rot(int quarter_turns) const {
		Shape r;
		const int step = N / 4;
		for (int i = 0; i < N; ++i) r.d[(i + step * quarter_turns) % N] = d[i];
		return r;
	}
	Shape flip_u() const {
		Shape r;
		for (int i = 0; i < N; ++i) r.d[(N / 2 - i + N) % N] = d[i];
		return r;
	}
	Shape flip_v() const {
		Shape r;
		for (int i = 0; i < N; ++i) r.d[(N - i) % N] = d[i];
		return r;
	}
};
using Shape4 = Shape<4>;
using Shape8 = Shape<8>;

template <int N>
constexpr Shape<N> shape(std::initializer_list<int> v) {
	Shape<N> s;
	int i = 0;
	for (int x : v) s.d[i++] = (uint8_t)x;
	return s;
}

template <int N>
struct Blend {
	Shape<N> shape;
	int alpha_map;
};

template <int N>
bool match_flipped(const Shape<N> &tmpl, const Shape<N> &s, unsigned &flags) {
	if (s == tmpl) return true;
	if (s == tmpl.flip_u()) { flags |= FLIP_U; return true; }
	if (s == tmpl.flip_v()) { flags |= FLIP_V; return true; }
	return false;
}

template <int N>
bool match(const Shape<N> &tmpl, const Shape<N> &s, unsigned &flags) {
	if (match_flipped(tmpl, s, flags)) return true;
	// A flipped shape turns the opposite way.
	if (match_flipped(tmpl.rot(1), s, flags)) { flags |= flags ? ROTATE_270 : ROTATE_90; return true; }
	if (match_flipped(tmpl.rot(2), s, flags)) { flags |= ROTATE_180; return true; }
	if (match_flipped(tmpl.rot(3), s, flags)) { flags |= flags ? ROTATE_90 : ROTATE_270; return true; }
	return false;
}

template <int N, size_t K>
int lookup(const Blend<N> (&table)[K], const Shape<N> &s, unsigned &flags) {
	for (const Blend<N> &b : table)
		if (match(b.shape, s, flags)) return b.alpha_map;
	return ALPHA_MAP_BAD;
}

inline const Blend<4> BLENDS_1[] = {{shape<4>({1, 0, 0, 0}), 12}};
inline const Blend<4> BLENDS_2[] = {{shape<4>({0, 1, 1, 0}), 7}, {shape<4>({1, 0, 1, 0}), 10}};
inline const Blend<8> BLENDS_2_8[] = {
	{shape<8>({1, 1, 0, 0, 0, 0, 0, 0}), 12}, {shape<8>({1, 0, 0, 0, 0, 1, 0, 0}), 12},
	{shape<8>({0, 1, 0, 1, 0, 0, 0, 0}), 0}, {shape<8>({0, 1, 0, 0, 0, 1, 0, 0}), 0}};
inline const Blend<4> BLENDS_3[] = {{shape<4>({1, 1, 1, 0}), 4}};
inline const Blend<8> BLENDS_3_8[] = {
	{shape<8>({1, 1, 0, 0, 1, 0, 0, 0}), 10}, {shape<8>({1, 1, 0, 0, 0, 0, 0, 1}), 12},
	{shape<8>({1, 1, 1, 0, 0, 0, 0, 0}), 1}, {shape<8>({0, 1, 1, 0, 1, 0, 0, 0}), 7},
	{shape<8>({0, 0, 1, 0, 1, 0, 1, 0}), 4}, {shape<8>({1, 1, 0, 0, 0, 1, 0, 0}), 12},
	{shape<8>({1, 1, 0, 1, 0, 0, 0, 0}), 12}, {shape<8>({0, 0, 1, 0, 1, 0, 0, 1}), 7},
	{shape<8>({1, 0, 0, 1, 0, 1, 0, 0}), 12}, {shape<8>({0, 1, 0, 1, 0, 1, 0, 0}), 0}};
inline const Blend<8> BLENDS_4_8[] = {
	{shape<8>({1, 1, 0, 0, 1, 0, 0, 1}), 10}, {shape<8>({1, 1, 0, 1, 1, 0, 0, 0}), 10},
	{shape<8>({1, 1, 0, 0, 1, 1, 0, 0}), 10}, {shape<8>({1, 1, 0, 1, 0, 0, 0, 1}), 12},
	{shape<8>({0, 1, 1, 0, 1, 1, 0, 0}), 7}, {shape<8>({1, 1, 1, 1, 0, 0, 0, 0}), 1},
	{shape<8>({1, 1, 1, 0, 1, 0, 0, 0}), 3}, {shape<8>({0, 0, 1, 0, 1, 1, 0, 1}), 7},
	{shape<8>({1, 0, 1, 0, 1, 1, 0, 0}), 4}, {shape<8>({1, 1, 1, 0, 0, 1, 0, 0}), 1},
	{shape<8>({1, 1, 0, 1, 0, 1, 0, 0}), 12}, {shape<8>({0, 1, 0, 1, 0, 1, 0, 1}), 0}};
inline const Blend<8> BLENDS_5_8[] = {
	{shape<8>({1, 1, 1, 1, 1, 0, 0, 0}), 2}, {shape<8>({1, 1, 1, 1, 0, 0, 0, 1}), 1},
	{shape<8>({1, 1, 1, 0, 1, 0, 0, 1}), 3}, {shape<8>({1, 1, 1, 0, 1, 0, 1, 0}), 11},
	{shape<8>({1, 1, 1, 0, 0, 1, 0, 1}), 1}, {shape<8>({1, 1, 0, 1, 1, 1, 0, 0}), 10},
	{shape<8>({1, 1, 1, 0, 1, 1, 0, 0}), 3}, {shape<8>({1, 0, 1, 0, 1, 1, 0, 1}), 4},
	{shape<8>({1, 1, 0, 1, 0, 1, 0, 1}), 12}, {shape<8>({0, 1, 1, 0, 1, 1, 0, 1}), 7}};
inline const Blend<8> BLENDS_6_8[] = {
	{shape<8>({1, 1, 1, 1, 1, 1, 0, 0}), 2}, {shape<8>({1, 1, 1, 1, 1, 0, 1, 0}), 8},
	{shape<8>({1, 1, 1, 1, 0, 1, 0, 1}), 1}, {shape<8>({1, 1, 1, 0, 1, 1, 1, 0}), 6},
	{shape<8>({1, 1, 1, 0, 1, 1, 0, 1}), 3}, {shape<8>({1, 1, 0, 1, 1, 1, 0, 1}), 10}};
inline const Blend<8> BLENDS_7_8[] = {{shape<8>({1, 1, 1, 1, 1, 1, 0, 1}), 2}, {shape<8>({1, 1, 1, 1, 1, 1, 1, 0}), 9}};

} // namespace detail

// CAlphaMapCalculator::Calculate: the alpha map (and flip/rotate flags) for a
// blend where lacks[n] is 1 when neighbour n (N, NE, E, SE, S, SW, W, NW) does NOT
// have the blended terrain; -1 when every neighbour lacks it (nothing to draw).
inline int calculate(const uint8_t lacks[8], unsigned &flags) {
	using namespace detail;
	flags = 0;
	Shape8 s8;
	int count = 0;
	for (int i = 0; i < 8; ++i) {
		s8.d[i] = lacks[i] ? 1 : 0;
		count += s8.d[i];
	}
	if (count == 0) return 0;
	if (count == 8) return -1;
	if (count <= 4 && !s8.d[1] && !s8.d[3] && !s8.d[5] && !s8.d[7]) {
		const Shape4 s4 = shape<4>({s8.d[0], s8.d[2], s8.d[4], s8.d[6]});
		switch (count) {
		case 1: return lookup(BLENDS_1, s4, flags);
		case 2: return lookup(BLENDS_2, s4, flags);
		case 3: return lookup(BLENDS_3, s4, flags);
		case 4: return 5;
		}
	}
	switch (count) {
	case 1: return 0;
	case 2: return lookup(BLENDS_2_8, s8, flags);
	case 3: return lookup(BLENDS_3_8, s8, flags);
	case 4: return lookup(BLENDS_4_8, s8, flags);
	case 5: return lookup(BLENDS_5_8, s8, flags);
	case 6: return lookup(BLENDS_6_8, s8, flags);
	case 7: return lookup(BLENDS_7_8, s8, flags);
	}
	return ALPHA_MAP_BAD;
}

// Blends a tile keeps: 0 A.D. draws every higher-priority neighbour terrain, at most
// one for each of its 8 neighbours, so none is left out.
constexpr int MAX_BLENDS = 8;

// CPatchRData::BuildBlends for the whole map: RGBA8 texels, res * MAX_BLENDS
// wide and res high, texel (tx * MAX_BLENDS + k, tz) the k-th blend drawn over tile
// (tx, tz), lowest priority first: r = terrain layer, g = alpha map, b = flags,
// a = 255 (0 past the tile's last blend). tiles[tz * res + tx] is the tile's layer.
inline std::vector<uint8_t> build(int res, const uint8_t *tiles) {
	std::vector<uint8_t> out((size_t)res * (size_t)res * MAX_BLENDS * 4, 0);
	for (int tz = 0; tz < res; ++tz)
		for (int tx = 0; tx < res; ++tx) {
			const uint8_t own = tiles[(size_t)tz * (size_t)res + (size_t)tx];
			// Neighbour terrains above this tile's, with the neighbours that have each.
			uint8_t layer[8];
			uint8_t mask[8];
			int n = 0;
			for (int k = 0; k < 8; ++k) {
				const int x = tx + BLEND_OFFSETS[k][1], z = tz + BLEND_OFFSETS[k][0];
				if (x < 0 || z < 0 || x >= res || z >= res) continue;
				const uint8_t l = tiles[(size_t)z * (size_t)res + (size_t)x];
				if (l <= own) continue;
				int j = 0;
				while (j < n && layer[j] != l) ++j;
				if (j == n) { layer[n] = l; mask[n] = 0; ++n; }
				mask[j] |= (uint8_t)(1u << k);
			}
			// Lowest priority first, keeping the highest MAX_BLENDS.
			int order[8];
			for (int j = 0; j < n; ++j) order[j] = j;
			std::sort(order, order + n, [&](int a, int b) { return layer[a] < layer[b]; });
			const int first = std::max(0, n - MAX_BLENDS);
			uint8_t *px = &out[((size_t)tz * (size_t)res * MAX_BLENDS + (size_t)tx * MAX_BLENDS) * 4];
			for (int j = first; j < n; ++j) {
				uint8_t shape[8];
				for (int k = 0; k < 8; ++k) shape[k] = (mask[order[j]] >> k) & 1 ? 0 : 1;
				unsigned flags = 0;
				const int am = calculate(shape, flags);
				if (am < 0) continue;
				px[0] = layer[order[j]];
				px[1] = (uint8_t)am;
				px[2] = (uint8_t)flags;
				px[3] = 255;
				px += 4;
			}
		}
	return out;
}

} // namespace rtso::terrain_blend
