// Territory border lines for the 3D view.
//
// Derived from 0 A.D., GPL-2.0-or-later (Copyright (C) Wildfire Games):
//   CTerritoryBoundaryCalculator::ComputeBoundaries (source/graphics/TerritoryBoundary.cpp),
//   SimRender::SmoothPointsAverage / InterpolatePointsRNS (source/simulation2/helpers/Render.cpp),
//   CCmpTerritoryManager::UpdateBoundaryLines (territorymanager.xml BorderThickness 0.75, BorderSeparation 0.85).
//
// Pure C++ (no Godot) so tools/test_territory_boundary.cpp can drive it.
#ifndef RTSO_TERRITORY_BOUNDARY_H
#define RTSO_TERRITORY_BOUNDARY_H

#include <cstdint>
#include <vector>

namespace rtso {

struct BoundaryPoint {
	float x = 0.0f, y = 0.0f;
};

struct TerritoryBoundary {
	uint8_t owner = 0; // grid value: 0 = unclaimed, else owner id as stored in the grid
	std::vector<BoundaryPoint> points; // closed loop
};

// Every closed outline around a run of equal non-zero cells. Points are in
// cell units (0..w, 0..h) on the cell edges, as ComputeBoundaries emits them
// before scaling by the territory tile size. Each outline runs so that its
// owner's cells lie on its left (0 A.D. traces counter-clockwise).
std::vector<TerritoryBoundary> compute_territory_boundaries(const uint8_t *grid, int w, int h);

// SimRender::SmoothPointsAverage for a closed loop.
void smooth_points_average(std::vector<BoundaryPoint> &points);

// SimRender::InterpolatePointsRNS for a closed loop: a rounded non-uniform
// spline through the points, sampled segment_samples times per segment and
// offset sideways by `offset` (inward, so neighbouring borders sit apart).
void interpolate_points_rns(std::vector<BoundaryPoint> &points, float offset, int segment_samples = 4);

} // namespace rtso

#endif
