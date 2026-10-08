#ifndef RTSO_NAV_RASTERIZE_H
#define RTSO_NAV_RASTERIZE_H

// Rasterization of a rotated rectangle, expanded by a clearance, onto a navcell grid.
//
// Port of 0 A.D. SimRasterize::RasterizeRectWithClearance, GPL-2.0-or-later (Copyright (C) 2025 Wildfire Games).
//   Vendor: src/0ad/simulation2/helpers/Rasterize.cpp:29-130 and Rasterize.h:52-54 (REFERENCE ONLY).
//   Fixed -> float, CFixedVector2D -> Vec2, i16 -> int, Span list -> per-cell callback.
//   The row walk, corner tests and skip/break logic are kept; the vendor line is cited at each step.
// Header-only; depends on geometry.h (DistanceToSquareSquared, GetHalfBoundingBox).
//
// Rule (vendor): navcell (i,j) is covered iff all four of its corners lie within the clearance-expanded
// rectangle, i.e. the cell lies entirely inside the rounded rectangle (the rectangle grown by a disc
// of radius R). Corners are rounded, not squared. A cell whose centre is inside but whose corners are
// not is NOT covered.
//
// Deliberate deviations from the vendor:
//   - Cell lattice is anchored at grid_origin (vendor anchors at world (0,0)). Cell (i,j) covers
//     [origin.x + i*cell_size, +cell_size] x [origin.y + j*cell_size, +cell_size].
//   - Output is mark(i,j) per covered cell, clipped to 0 <= i < w, 0 <= j < h. Vendor returns unclipped spans.
//     The row walk starts at the clipped range; this is safe because the covered cells of a row form one
//     contiguous run (the expanded shape is convex), so a walk that begins inside the run still finds all of it.
//   - The vendor adds Pathfinding.h:160 CLEARANCE_EXTENSION_RADIUS (1.0 world unit) to the clearance
//     (Rasterize.cpp:48). This port keeps that value as kClearanceExtensionRadius. In 0 A.D. a navcell is
//     1 unit, so it is one navcell there. RTS0 navcells are 2.0 units, so here it is half a navcell.
//     It is kept because it is less than one cell, so grid-aligned footprints are unchanged, and it lets
//     off-grid footprint-2 buildings always cover a navcell. Off-grid footprint-1 buildings still cover none
//     in about 22% of placements (rounded corners), so Simulation::rebuild_nav_grid falls back to the centre
//     navcell. tools/test_rasterize.cpp checks these properties.

#include "geometry.h"

#include <cmath>

namespace rtso {
namespace nav {

// Vendor Pathfinding.h:160 CLEARANCE_EXTENSION_RADIUS = fixed::FromInt(1), in world units.
inline constexpr float kClearanceExtensionRadius = 1.0f;

namespace detail {

// Maps a floored or ceiled grid coordinate to [0, n]. NaN maps to 0.
inline int clamp_cell_index(float f, int n)
{
	if (!(f > 0.0f))
		return 0;
	if (f >= static_cast<float>(n))
		return n;
	return static_cast<int>(f);
}

} // namespace detail

// Calls mark(i, j) once for each navcell (i,j) inside the w x h grid that is fully covered by the
// rectangle with centre `center`, axes `u` and `v` (unit length, orthogonal), half sizes `halfSize`,
// grown by `clearance` (plus kClearanceExtensionRadius). clearance must be >= 0.
// `grid_origin` is the world position of the grid's (0,0) corner; `cell_size` is the navcell side.
// Vendor: Rasterize.cpp:29-130.
template <class Fn>
void rasterize_rect_with_clearance(Vec2 center, Vec2 u, Vec2 v, Vec2 halfSize,
	float clearance, float cell_size, Vec2 grid_origin, int w, int h, Fn &&mark)
{
	// Vendor :48 — the extension is added to the clearance.
	const float radius = clearance + kClearanceExtensionRadius;
	// Vendor :64 — squared, so the corner tests below compare squared distances.
	const float radius2 = radius * radius;

	// Vendor :52-54 — bounding box of the expanded rectangle.
	const Vec2 expandedHalf(halfSize.x + radius, halfSize.y + radius);
	const Vec2 halfBound = GetHalfBoundingBox(u, v, expandedHalf);

	// Vendor :55-58 — candidate cell range [i0,i1) x [j0,j1), clipped to the grid.
	// Vendor rounds toward -inf for the lower bound and toward +inf for the upper bound.
	const int i0 = detail::clamp_cell_index(std::floor((center.x - halfBound.x - grid_origin.x) / cell_size), w);
	const int j0 = detail::clamp_cell_index(std::floor((center.y - halfBound.y - grid_origin.y) / cell_size), h);
	const int i1 = detail::clamp_cell_index(std::ceil((center.x + halfBound.x - grid_origin.x) / cell_size), w);
	const int j1 = detail::clamp_cell_index(std::ceil((center.y + halfBound.y - grid_origin.y) / cell_size), h);

	// Vendor :60-61 — empty bounds.
	if (i1 <= i0 || j1 <= j0)
		return;

	// Vendor :88-114 — is the lattice corner (gi,gj) outside the expanded rectangle?
	// Distance to the un-expanded rectangle, zero inside, compared with the squared radius.
	auto corner_outside = [&](int gi, int gj) {
		const Vec2 p(grid_origin.x + cell_size * static_cast<float>(gi) - center.x,
			grid_origin.y + cell_size * static_cast<float>(gj) - center.y);
		return DistanceToSquareSquared(p, u, v, halfSize, true) > radius2;
	};

	// Vendor :68-129 — one row at a time.
	for (int j = j0; j < j1; ++j) {
		// Vendor :76-79
		int spanI0 = 0;
		int spanI1 = 0;
		bool previousInside = false;
		bool skipNextCell = false;

		for (int i = i0; i < i1; ++i) {
			// Vendor :82-86
			if (skipNextCell) {
				skipNextCell = false;
				continue;
			}

			// Vendor :88-95 — bottom-right corner (i+1, j). If it is outside, the next cell's
			// bottom-left corner is the same point, so the next cell cannot be covered either.
			if (corner_outside(i + 1, j)) {
				if (previousInside)
					break;
				skipNextCell = true;
				continue;
			}

			// Vendor :97-104 — top-right corner (i+1, j+1). Same reasoning for the next cell's top-left.
			if (corner_outside(i + 1, j + 1)) {
				if (previousInside)
					break;
				skipNextCell = true;
				continue;
			}

			// Vendor :106-118 — start of a run: the left corners must be inside too.
			if (!previousInside) {
				if (corner_outside(i, j))
					continue;
				if (corner_outside(i, j + 1))
					continue;

				previousInside = true;
				spanI0 = i;
			}

			// Vendor :120
			spanI1 = i + 1;
		}

		// Vendor :124-128 — emit the run [spanI0, spanI1) of row j.
		if (previousInside) {
			for (int i = spanI0; i < spanI1; ++i)
				mark(i, j);
		}
	}
}

} // namespace nav
} // namespace rtso

#endif // RTSO_NAV_RASTERIZE_H
