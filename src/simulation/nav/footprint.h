#ifndef RTSO_NAV_FOOTPRINT_H
#define RTSO_NAV_FOOTPRINT_H

// Building footprints: rectangles centred on (x, z) and turned by the building's 0 A.D. Position
// angle, like the static squares of CCmpObstructionManager, whose axes are u = (cos a, -sin a) and
// v = (sin a, cos a) (0 faces +z, pi/2 faces +x), with the template's Obstruction/Static width along
// u and depth along v.
//   Vendor: src/0ad/simulation2/components/CCmpObstruction.cpp GetObstructionSquare and
//   src/0ad/simulation2/helpers/Geometry.h GetHalfBoundingBox (REFERENCE ONLY).
//
// One helper for every footprint test (nav raster, steering, separation, reach, placement), so
// they all agree on where a turned building stands. A footprint type B has the fields x, z, ux and
// uz (the u axis from footprint_axis; v is u turned a quarter, (-uz, ux)) and hu and hv, its half
// extents along u and v.
//
// A rectangle looks the same every half turn, and every quarter turn with its width and depth
// swapped, so the angle counts modulo pi/2 and footprint_axis says when to swap (hu is then the
// half depth, hv the half width). An angle within FOOTPRINT_SNAP of a quarter turn snaps to the
// exact axes u = (1, 0), v = (0, 1). Every helper takes its axis-aligned branch on those
// (uz == 0), with the same float operations as the axis-aligned code it replaced, so unturned
// square buildings give bit-identical results.

#include "simulation/nav/geometry.h"

#include <algorithm>
#include <cmath>

namespace rtso {
namespace nav {

inline constexpr float FOOTPRINT_SNAP = 1e-3f; // radians

// The u axis of a footprint turned by `rot`, reduced to under a quarter turn, and whether the
// quarter turns taken off number an odd count (width and depth then lie along v and u).
inline void footprint_axis(float rot, float& ux, float& uz, bool& swapped)
{
	constexpr float QUARTER = 1.57079632679f;
	float t = std::fmod(rot, QUARTER);
	if (t < 0.0f) t += QUARTER;
	float quarters = std::round((rot - t) / QUARTER);
	if (!(t >= FOOTPRINT_SNAP && t <= QUARTER - FOOTPRINT_SNAP)) { // NaN snaps too
		if (t > QUARTER - FOOTPRINT_SNAP) quarters += 1.0f; // just short of the next quarter turn
		ux = 1.0f;
		uz = 0.0f;
	} else {
		ux = std::cos(t);
		uz = -std::sin(t);
	}
	swapped = std::fabs(std::fmod(quarters, 2.0f)) == 1.0f; // false for NaN
}

inline void footprint_axis(float rot, float& ux, float& uz)
{
	bool swapped;
	footprint_axis(rot, ux, uz, swapped);
}

template <class B>
bool footprint_aligned(const B& b) { return b.uz == 0.0f; }

template <class B>
Vec2 footprint_u(const B& b) { return b.uz == 0.0f ? Vec2(1.0f, 0.0f) : Vec2(b.ux, b.uz); }

template <class B>
Vec2 footprint_v(const B& b) { return b.uz == 0.0f ? Vec2(0.0f, 1.0f) : Vec2(-b.uz, b.ux); }

// The larger half size of the footprint's axis-aligned bounding box (GetHalfBoundingBox).
template <class B>
float footprint_extent(const B& b)
{
	if (b.uz == 0.0f) return std::max(b.hu, b.hv);
	const float ax = std::fabs(b.ux), az = std::fabs(b.uz);
	return std::max(b.hu * ax + b.hv * az, b.hu * az + b.hv * ax);
}

// (px, pz) relative to the footprint's centre, along u and v.
template <class B>
Vec2 footprint_local(const B& b, float px, float pz)
{
	const float dx = px - b.x, dz = pz - b.z;
	if (b.uz == 0.0f) return Vec2(dx, dz);
	return Vec2(dx * b.ux + dz * b.uz, dz * b.ux - dx * b.uz);
}

// A direction along u and v, in world axes.
template <class B>
Vec2 footprint_dir(const B& b, float du, float dv)
{
	if (b.uz == 0.0f) return Vec2(du, dv);
	return Vec2(du * b.ux - dv * b.uz, du * b.uz + dv * b.ux);
}

// The world point at (lu, lv) along u and v from the footprint's centre.
template <class B>
Vec2 footprint_world(const B& b, float lu, float lv)
{
	if (b.uz == 0.0f) return Vec2(b.x + lu, b.z + lv);
	const Vec2 d = footprint_dir(b, lu, lv);
	return Vec2(b.x + d.x, b.z + d.y);
}

// How far (px, pz) lies outside the footprint along u and along v (0 on an axis it spans), so
// gap.x * gap.x + gap.y * gap.y is the squared distance to the footprint (0 inside).
template <class B>
Vec2 footprint_gap(const B& b, float px, float pz)
{
	const Vec2 l = footprint_local(b, px, pz);
	return Vec2(std::max(0.0f, std::fabs(l.x) - b.hu), std::max(0.0f, std::fabs(l.y) - b.hv));
}

// Whether the footprint grown by `r` on every side holds (px, pz), edges excluded.
template <class B>
bool footprint_contains(const B& b, float px, float pz, float r)
{
	const Vec2 l = footprint_local(b, px, pz);
	return std::fabs(l.x) < b.hu + r && std::fabs(l.y) < b.hv + r;
}

// If the footprint grown by `r` holds (px, pz), moves it to the nearest edge of that grown rectangle
// and returns true.
template <class B>
bool footprint_push_out(const B& b, float r, float& px, float& pz)
{
	Vec2 l = footprint_local(b, px, pz);
	const float hu = b.hu + r, hv = b.hv + r;
	if (!(std::fabs(l.x) < hu && std::fabs(l.y) < hv)) return false;
	if (hu - std::fabs(l.x) <= hv - std::fabs(l.y)) l.x = l.x < 0.0f ? -hu : hu;
	else l.y = l.y < 0.0f ? -hv : hv;
	const Vec2 w = footprint_world(b, l.x, l.y);
	px = w.x;
	pz = w.y;
	return true;
}

// Whether the footprint overlaps the axis-aligned box [x0, x1] x [z0, z1] (touching does not
// count): the separating axis test over both shapes' axes.
template <class B>
bool footprint_overlaps_box(const B& b, float x0, float z0, float x1, float z1)
{
	const float hx = 0.5f * (x1 - x0), hz = 0.5f * (z1 - z0);
	const float cx = 0.5f * (x0 + x1), cz = 0.5f * (z0 + z1);
	if (b.uz == 0.0f) return std::fabs(b.x - cx) < b.hu + hx && std::fabs(b.z - cz) < b.hv + hz;
	const float ax = std::fabs(b.ux), az = std::fabs(b.uz);
	// The footprint's bounding box against the box, then the box against the footprint's axes.
	if (std::fabs(b.x - cx) >= b.hu * ax + b.hv * az + hx || std::fabs(b.z - cz) >= b.hu * az + b.hv * ax + hz) return false;
	const Vec2 l = footprint_local(b, cx, cz);
	return std::fabs(l.x) < b.hu + hx * ax + hz * az && std::fabs(l.y) < b.hv + hx * az + hz * ax;
}

} // namespace nav
} // namespace rtso

#endif // RTSO_NAV_FOOTPRINT_H
