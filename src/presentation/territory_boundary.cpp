// Derived from 0 A.D., GPL-2.0-or-later (Copyright (C) Wildfire Games).
// See territory_boundary.h for the 0 A.D. sources this ports.
#include "territory_boundary.h"

#include <cmath>

namespace rtso {

std::vector<TerritoryBoundary> compute_territory_boundaries(const uint8_t *grid, int w, int h) {
	std::vector<TerritoryBoundary> boundaries;
	if (!grid || w <= 0 || h <= 0) return boundaries;

	std::vector<uint8_t> processed((size_t)w * (size_t)h, 0);
	auto get = [&](int i, int j) -> uint8_t { return grid[(size_t)j * w + i]; };

	static const float edge_x[4] = { 0.5f, 1.0f, 0.5f, 0.0f };
	static const float edge_y[4] = { 0.0f, 0.5f, 1.0f, 0.5f };
	enum : uint8_t { TILE_BOTTOM = 0, TILE_RIGHT = 1, TILE_TOP = 2, TILE_LEFT = 3 };

	const int maxi = w - 1;
	const int maxj = h - 1;
	for (int j = 0; j < h; ++j) {
		for (int i = 0; i < w; ++i) {
			const uint8_t owner = get(i, j);
			if (!owner) continue;
			// Start on a cell whose bottom edge is a border, not yet traced.
			const bool eligible = (j == 0 || owner != get(i, j - 1));
			if (processed[(size_t)j * w + i] || !eligible) continue;

			boundaries.push_back(TerritoryBoundary());
			boundaries.back().owner = owner;
			std::vector<BoundaryPoint> &points = boundaries.back().points;

			const uint8_t dir = TILE_BOTTOM;
			uint8_t cdir = dir;
			int ci = i, cj = j;
			// Walk the outline keeping the owner's cells on the left; at each
			// step turn clockwise round a convex corner, carry straight on, or
			// turn counter-clockwise inside a concave one.
			while (true) {
				points.push_back({ (float)ci + edge_x[cdir], (float)cj + edge_y[cdir] });
				switch (cdir) {
					case TILE_BOTTOM:
						processed[(size_t)cj * w + ci] = 1;
						if (ci < maxi && cj > 0 && get(ci + 1, cj - 1) == owner) {
							++ci;
							--cj;
							cdir = TILE_LEFT;
						} else if (ci < maxi && get(ci + 1, cj) == owner) {
							++ci;
						} else {
							cdir = TILE_RIGHT;
						}
						break;
					case TILE_RIGHT:
						if (ci < maxi && cj < maxj && get(ci + 1, cj + 1) == owner) {
							++ci;
							++cj;
							cdir = TILE_BOTTOM;
						} else if (cj < maxj && get(ci, cj + 1) == owner) {
							++cj;
						} else {
							cdir = TILE_TOP;
						}
						break;
					case TILE_TOP:
						if (ci > 0 && cj < maxj && get(ci - 1, cj + 1) == owner) {
							--ci;
							++cj;
							cdir = TILE_RIGHT;
						} else if (ci > 0 && get(ci - 1, cj) == owner) {
							--ci;
						} else {
							cdir = TILE_LEFT;
						}
						break;
					case TILE_LEFT:
						if (ci > 0 && cj > 0 && get(ci - 1, cj - 1) == owner) {
							--ci;
							--cj;
							cdir = TILE_TOP;
						} else if (cj > 0 && get(ci, cj - 1) == owner) {
							--cj;
						} else {
							cdir = TILE_BOTTOM;
						}
						break;
				}
				if (ci == i && cj == j && cdir == dir) break;
			}
		}
	}
	return boundaries;
}

void smooth_points_average(std::vector<BoundaryPoint> &points) {
	const size_t n = points.size();
	if (n < 2) return;
	std::vector<BoundaryPoint> out(n);
	for (size_t i = 0; i < n; ++i) {
		const BoundaryPoint &a = points[(i + n - 1) % n];
		const BoundaryPoint &b = points[i];
		const BoundaryPoint &c = points[(i + 1) % n];
		out[i] = { (a.x + b.x + c.x) / 3.0f, (a.y + b.y + c.y) / 3.0f };
	}
	points.swap(out);
}

namespace {
struct V2 {
	float x, y;
	V2 operator+(V2 o) const { return { x + o.x, y + o.y }; }
	V2 operator-(V2 o) const { return { x - o.x, y - o.y }; }
	V2 operator*(float s) const { return { x * s, y * s }; }
	float length() const { return std::sqrt(x * x + y * y); }
	V2 normalized() const {
		const float l = length();
		return l > 0.0f ? V2{ x / l, y / l } : V2{ 0.0f, 0.0f };
	}
};

BoundaryPoint evaluate_spline(float t, V2 a0, V2 a1, V2 a2, V2 a3, float offset) {
	const V2 p = a0 * (t * t * t) + a1 * (t * t) + a2 * t + a3;
	const V2 dp = (a0 * (3 * t * t) + a1 * (2 * t) + a2).normalized();
	return { p.x + dp.y * -offset, p.y + dp.x * offset };
}
} // namespace

void interpolate_points_rns(std::vector<BoundaryPoint> &points, float offset, int segment_samples) {
	const size_t n = points.size();
	if (n < 1 || segment_samples <= 0) return;
	std::vector<BoundaryPoint> out;
	out.reserve(n * (size_t)segment_samples);
	auto at = [&](size_t k) { return V2{ points[k % n].x, points[k % n].y }; };
	for (size_t i = 0; i < n; ++i) {
		const V2 p0 = at(i + n - 1);
		const V2 p1 = at(i);
		const V2 p2 = at(i + 1);
		const V2 p3 = at(i + 2);
		// GPG4 "Nonuniform Splines", as SimRender does.
		const float l1 = (p2 - p1).length();
		const V2 s0 = (p1 - p0).normalized();
		const V2 s1 = (p2 - p1).normalized();
		const V2 s2 = (p3 - p2).normalized();
		const V2 v1 = (s0 + s1).normalized() * l1;
		const V2 v2 = (s1 + s2).normalized() * l1;
		const V2 a0 = p1 * 2 + p2 * -2 + v1 + v2;
		const V2 a1 = p1 * -3 + p2 * 3 + v1 * -2 + v2 * -1;
		const V2 a2 = v1;
		const V2 a3 = p1;
		for (int s = 0; s < segment_samples; ++s)
			out.push_back(evaluate_spline((float)s / (float)segment_samples, a0, a1, a2, a3, offset));
	}
	points.swap(out);
}

} // namespace rtso
