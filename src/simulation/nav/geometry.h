#ifndef RTSO_NAV_GEOMETRY_H
#define RTSO_NAV_GEOMETRY_H

// 2D geometry helpers for the short-range VertexPathfinder port.
//
// Port of 0 A.D. Geometry helpers, GPL-2.0-or-later (Copyright (C) 2025 Wildfire Games).
//   Vendor: src/0ad/simulation2/helpers/Geometry.h and Geometry.cpp (REFERENCE ONLY).
//   Fixed -> float, CFixedVector2D -> Vec2. Algorithm structure is kept identical;
//   each function cites its vendor location as Geometry.cpp:NN (or Geometry.h:NN).
// Header-only, standard headers only. Used by future src/simulation/nav/vertex_pathfinder.h.

#include <algorithm>
#include <cmath>
#include <utility>

namespace rtso {
namespace nav {

// Minimal 2D float vector. Mirrors the CFixedVector2D operations the vendor code uses.
struct Vec2 {
	float x = 0.0f;
	float y = 0.0f;

	Vec2() = default;
	Vec2(float x_, float y_) : x(x_), y(y_) {}

	Vec2 operator+(const Vec2& o) const { return Vec2(x + o.x, y + o.y); }
	Vec2 operator-(const Vec2& o) const { return Vec2(x - o.x, y - o.y); }
	Vec2 operator-() const { return Vec2(-x, -y); }
	Vec2 operator*(float s) const { return Vec2(x * s, y * s); }
	Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
	Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }

	float dot(const Vec2& o) const { return x * o.x + y * o.y; }

	// Vendor CFixedVector2D::Perpendicular() (maths/FixedVector2D.h, not in tree): (-y, x).
	Vec2 perp() const { return Vec2(-y, x); }

	// Vendor CFixedVector2D::RelativeOrientation(): signed 2D cross product this x o.
	float relativeOrientation(const Vec2& o) const { return x * o.y - y * o.x; }

	float lengthSq() const { return x * x + y * y; }
	float length() const { return std::sqrt(lengthSq()); }

	// Vendor CFixedVector2D::Normalize(). A zero vector is left unchanged.
	void normalize() {
		float l = length();
		if (l > 0.0f) {
			x /= l;
			y /= l;
		}
	}
};

// Checks if a point is inside the given rotated rectangle.
// Points precisely on an edge are considered to be inside.
// Vendor: Geometry.h:43
inline bool PointIsInSquare(const Vec2& point, const Vec2& u, const Vec2& v, const Vec2& halfSize)
{
	return std::fabs(point.dot(u)) <= halfSize.x && std::fabs(point.dot(v)) <= halfSize.y;
}

// Returns (bx, by) such that every point inside the rotated rectangle has
// -bx <= x <= bx and -by <= y <= by.
// Vendor: Geometry.cpp:29
inline Vec2 GetHalfBoundingBox(const Vec2& u, const Vec2& v, const Vec2& halfSize)
{
	return Vec2(
		std::fabs(u.x * halfSize.x) + std::fabs(v.x * halfSize.y),
		std::fabs(u.y * halfSize.x) + std::fabs(v.y * halfSize.y));
}

// Minimum Euclidean distance from point to the boundary of the rotated rectangle.
// If countInsideAsZero is true and the point is inside, returns 0.
// Vendor: Geometry.cpp:37
inline float DistanceToSquare(const Vec2& point, const Vec2& u, const Vec2& v, const Vec2& halfSize, bool countInsideAsZero = false)
{
	// Regions (relative to the square's own axes, 0 = centre):
	//   B, I, G: du < hw;  D, E: dv < hh;  A, C, F, H: corners.
	// By symmetry (absolute values) we only work in the 0-B-C-E quadrant.
	float du = std::fabs(point.dot(u));
	float dv = std::fabs(point.dot(v));

	float hw = halfSize.x;
	float hh = halfSize.y;

	if (du < hw) // regions B, I, G
	{
		if (dv < hh) // region I
			return countInsideAsZero ? 0.0f : std::min(hw - du, hh - dv);
		else
			return dv - hh;
	}
	else if (dv < hh) // regions D, E
	{
		return du - hw; // vertical edges
	}
	else // regions A, C, F, H
	{
		Vec2 distance(du - hw, dv - hh);
		return distance.length();
	}
}

// Same as DistanceToSquare but without sqrt; returns the squared distance.
// Vendor: Geometry.cpp:93
inline float DistanceToSquareSquared(const Vec2& point, const Vec2& u, const Vec2& v, const Vec2& halfSize, bool countInsideAsZero = false)
{
	float du = std::fabs(point.dot(u));
	float dv = std::fabs(point.dot(v));

	float hw = halfSize.x;
	float hh = halfSize.y;

	if (du < hw) // regions B, I, G
	{
		if (dv < hh) // region I
			return countInsideAsZero ? 0.0f : std::min((hw - du) * (hw - du), (hh - dv) * (hh - dv));
		else
			return (dv - hh) * (dv - hh); // horizontal edges
	}
	else if (dv < hh) // regions D, E
	{
		return (du - hw) * (du - hw); // vertical edges
	}
	else // regions A, C, F, H
	{
		return (du - hw) * (du - hw) + (dv - hh) * (dv - hh);
	}
}

// Returns a point on the boundary of the rotated rectangle closest to point.
// Vendor: Geometry.cpp:118
inline Vec2 NearestPointOnSquare(const Vec2& point, const Vec2& u, const Vec2& v, const Vec2& halfSize)
{
	// Regions B, D, E, G are nearest to an edge; A, C, F, H are nearest to a corner.
	// du, dv are the location of the point in the square's coordinate system.
	float du = point.dot(u);
	float dv = point.dot(v);

	float hw = halfSize.x;
	float hh = halfSize.y;

	if (-hw < du && du < hw) // regions B, G; or regions D, E inside the square
	{
		if (-hh < dv && dv < hh && std::fabs(std::fabs(du) - hw) < std::fabs(std::fabs(dv) - hh)) // regions D, E
		{
			if (du >= 0.0f) // E
				return u * hw + v * dv;
			else // D
				return -u * hw + v * dv;
		}
		else // B, G
		{
			if (dv >= 0.0f) // B
				return v * hh + u * du;
			else // G
				return -v * hh + u * du;
		}
	}
	else if (-hh < dv && dv < hh) // regions D, E outside the square
	{
		if (du >= 0.0f) // E
			return u * hw + v * dv;
		else // D
			return -u * hw + v * dv;
	}
	else // regions A, C, F, H
	{
		Vec2 corner;
		if (du < 0.0f) // A, F
			corner -= u * hw;
		else // C, H
			corner += u * hw;
		if (dv < 0.0f) // F, H
			corner -= v * hh;
		else // A, C
			corner += v * hh;

		return corner;
	}
}

// Greatest straight line distance from point to the rotated rectangle.
// Vendor: Geometry.cpp:223
inline float MaxDistanceToSquare(const Vec2& point, const Vec2& u, const Vec2& v, const Vec2& halfSize, bool countInsideAsZero = false)
{
	float hw = halfSize.x;
	float hh = halfSize.y;

	if (std::fabs(point.dot(u)) < hw && std::fabs(point.dot(v)) < hh && countInsideAsZero)
		return 0.0f;

	// The maximum distance from a point to a square is attained at a corner.
	return std::max(std::max(
			(point + u * hw + v * hh).length(),
			(point + u * hw - v * hh).length()),
		std::max(
			(point - u * hw + v * hh).length(),
			(point - u * hw - v * hh).length()));
}

// Greatest straight line distance between two rotated rectangles.
// Vendor: Geometry.cpp:243
// NOTE (kept from vendor): corners are evaluated with countInsideAsZero=true, so a corner of
// square 1 lying inside square 2 contributes 0. For a small square fully inside a big one this
// returns 0 instead of the true maximum (e.g. (1,1) to (-10,-10) for hs 1 inside hs 10 gives
// 15.56). Callers must not rely on it for nested squares; see tools/test_geometry.cpp.
inline float MaxDistanceSquareToSquare(const Vec2& relativePos,
	const Vec2& u1, const Vec2& v1, const Vec2& halfSize1,
	const Vec2& u2, const Vec2& v2, const Vec2& halfSize2)
{
	// The maximum distance between two squares is attained between corners.
	float hw1 = halfSize1.x;
	float hh1 = halfSize1.y;

	return std::max(std::max(
			MaxDistanceToSquare(relativePos + u1 * hw1 + v1 * hh1, u2, v2, halfSize2, true),
			MaxDistanceToSquare(relativePos + u1 * hw1 - v1 * hh1, u2, v2, halfSize2, true)),
		std::max(MaxDistanceToSquare(relativePos - u1 * hw1 + v1 * hh1, u2, v2, halfSize2, true),
			MaxDistanceToSquare(relativePos - u1 * hw1 - v1 * hh1, u2, v2, halfSize2, true)));
}

// Ray a->b hits the rotated rectangle only when it enters from outside
// (points on the edge count as inside). Vendor: Geometry.cpp:259
inline bool TestRaySquare(const Vec2& a, const Vec2& b, const Vec2& u, const Vec2& v, const Vec2& halfSize)
{
	// Cases:
	//   a inside, b inside   -> no collision
	//   a inside, b outside  -> no collision
	//   a outside, b inside  -> collision
	//   a outside, b outside -> separating axis: bounding box test, then side test
	float hw = halfSize.x;
	float hh = halfSize.y;

	float au = a.dot(u);
	float av = a.dot(v);

	if (-hw <= au && au <= hw && -hh <= av && av <= hh)
		return false; // a is inside

	float bu = b.dot(u);
	float bv = b.dot(v);

	if (-hw <= bu && bu <= hw && -hh <= bv && bv <= hh)
		return true; // a is outside, b is inside

	if ((au < -hw && bu < -hw) || (au > hw && bu > hw) || (av < -hh && bv < -hh) || (av > hh && bv > hh))
		return false; // ab is entirely above/below/side the square

	Vec2 abp = (b - a).perp();
	float s0 = abp.dot((u * hw + v * hh) - a);
	float s1 = abp.dot((u * hw - v * hh) - a);
	float s2 = abp.dot((-u * hw - v * hh) - a);
	float s3 = abp.dot((-u * hw + v * hh) - a);
	if (s0 == 0.0f || s1 == 0.0f || s2 == 0.0f || s3 == 0.0f)
		return true; // ray intersects the corner

	bool sign = (s0 < 0.0f);
	if ((s1 < 0.0f) != sign || (s2 < 0.0f) != sign || (s3 < 0.0f) != sign)
		return true; // ray cuts through the square

	return false;
}

// Same as TestRaySquare with u = (1,0), v = (0,1).
// Vendor: Geometry.cpp:308
inline bool TestRayAASquare(const Vec2& a, const Vec2& b, const Vec2& halfSize)
{
	float hw = halfSize.x;
	float hh = halfSize.y;

	if (-hw <= a.x && a.x <= hw && -hh <= a.y && a.y <= hh)
		return false; // a is inside

	if (-hw <= b.x && b.x <= hw && -hh <= b.y && b.y <= hh)
		return true; // a is outside, b is inside

	if ((a.x < -hw && b.x < -hw) || (a.x > hw && b.x > hw) || (a.y < -hh && b.y < -hh) || (a.y > hh && b.y > hh))
		return false; // ab is entirely above/below/side the square

	Vec2 abp = (b - a).perp();
	float s0 = abp.dot(Vec2(hw, hh) - a);
	float s1 = abp.dot(Vec2(hw, -hh) - a);
	float s2 = abp.dot(Vec2(-hw, -hh) - a);
	float s3 = abp.dot(Vec2(-hw, hh) - a);
	if (s0 == 0.0f || s1 == 0.0f || s2 == 0.0f || s3 == 0.0f)
		return true; // ray intersects the corner

	bool sign = (s0 < 0.0f);
	if ((s1 < 0.0f) != sign || (s2 < 0.0f) != sign || (s3 < 0.0f) != sign)
		return true; // ray cuts through the square

	return false;
}

namespace detail {

// Separating axis test (vendor static SquareSAT, Geometry.cpp:341).
// Returns true if the square at the origin is not entirely on one side of the
// line in direction 'axis' passing through 'a'.
inline bool SquareSAT(const Vec2& a, const Vec2& axis, const Vec2& u, const Vec2& v, const Vec2& halfSize)
{
	float hw = halfSize.x;
	float hh = halfSize.y;

	Vec2 p = axis.perp();
	if (p.relativeOrientation(u * hw + v * hh - a) <= 0.0f)
		return true;
	if (p.relativeOrientation(u * hw - v * hh - a) <= 0.0f)
		return true;
	if (p.relativeOrientation(-u * hw - v * hh - a) <= 0.0f)
		return true;
	if (p.relativeOrientation(-u * hw + v * hh - a) <= 0.0f)
		return true;

	return false;
}

} // namespace detail

// Two rotated rectangles overlap (touching counts as overlap).
// Vendor: Geometry.cpp:359 (its own TODO says "test this carefully"; see tools/test_geometry.cpp).
inline bool TestSquareSquare(
	const Vec2& c0, const Vec2& u0, const Vec2& v0, const Vec2& halfSize0,
	const Vec2& c1, const Vec2& u1, const Vec2& v1, const Vec2& halfSize1)
{
	Vec2 corner0a = c0 + u0 * halfSize0.x + v0 * halfSize0.y;
	Vec2 corner0b = c0 - u0 * halfSize0.x - v0 * halfSize0.y;
	Vec2 corner1a = c1 + u1 * halfSize1.x + v1 * halfSize1.y;
	Vec2 corner1b = c1 - u1 * halfSize1.x - v1 * halfSize1.y;

	// Separating axis test: each square against each edge of the other square.
	// DEVIATION from vendor Geometry.cpp:371-386: the v-edge calls (2, 4, 6, 8) have their
	// sign flipped. SquareSAT tests the sign of axis.dot(corner - a), so every call must pass
	// the inward axis of its edge; vendor passes +v for the top edge and -v for the bottom edge,
	// i.e. the wrong way round relative to the u-edge calls. Vendor's own comment on this
	// function says "need to test this carefully". Verified against an independent oracle in
	// tools/test_geometry.cpp (containment and random pairs).
	if (!detail::SquareSAT(corner0a - c1, -u0, u1, v1, halfSize1))
		return false;
	if (!detail::SquareSAT(corner0a - c1, -v0, u1, v1, halfSize1))
		return false;
	if (!detail::SquareSAT(corner0b - c1, u0, u1, v1, halfSize1))
		return false;
	if (!detail::SquareSAT(corner0b - c1, v0, u1, v1, halfSize1))
		return false;
	if (!detail::SquareSAT(corner1a - c0, -u1, u0, v0, halfSize0))
		return false;
	if (!detail::SquareSAT(corner1a - c0, -v1, u0, v0, halfSize0))
		return false;
	if (!detail::SquareSAT(corner1b - c0, u1, u0, v0, halfSize0))
		return false;
	if (!detail::SquareSAT(corner1b - c0, v1, u0, v0, halfSize0))
		return false;

	return true;
}

// Distance travelled walking counter-clockwise from (x_max, 0) to grid point (x, y)
// on the perimeter of [-x_max, x_max] x [-y_max, y_max]. Vendor: Geometry.cpp:391
inline int GetPerimeterDistance(int x_max, int y_max, int x, int y)
{
	if (x_max <= 0 || y_max <= 0)
		return 0;

	int quarter = x_max + y_max;
	if (x == x_max && y >= 0)
		return y;
	if (y == y_max)
		return quarter - x;
	if (x == -x_max)
		return 2 * quarter - y;
	if (y == -y_max)
		return 3 * quarter + x;
	if (x == x_max)
		return 4 * quarter + y;
	return 0;
}

// Inverse of GetPerimeterDistance: grid point reached after walking distance k
// counter-clockwise from (x_max, 0). Vendor: Geometry.cpp:410
inline std::pair<int, int> GetPerimeterCoordinates(int x_max, int y_max, int k)
{
	if (x_max <= 0 || y_max <= 0)
		return std::pair<int, int>(0, 0);

	int quarter = x_max + y_max;
	k %= 4 * quarter;
	if (k < 0)
		k += 4 * quarter;

	if (k < y_max)
		return std::pair<int, int>(x_max, k);
	if (k < quarter + x_max)
		return std::pair<int, int>(quarter - k, y_max);
	if (k < 2 * quarter + y_max)
		return std::pair<int, int>(-x_max, 2 * quarter - k);
	if (k < 3 * quarter + x_max)
		return std::pair<int, int>(k - 3 * quarter, -y_max);
	return std::pair<int, int>(x_max, k - 4 * quarter);
}

// Minimum Euclidean distance from point to the segment a-b. Vendor: Geometry.cpp:431
inline float DistanceToSegment(const Vec2& point, const Vec2& a, const Vec2& b)
{
	// Space #1 (before a) -> distance to a; space #3 (after b) -> distance to b;
	// space #2 (between) -> distance to the line through a and b.
	const Vec2 dir = b - a;
	const float pointDot = dir.dot(point);
	const float aDot = dir.dot(a);
	if (pointDot <= aDot)
		return (point - a).length();
	const float bDot = dir.dot(b);
	if (pointDot >= bDot)
		return (point - b).length();
	Vec2 normal = dir.perp();
	normal.normalize();
	return std::fabs(normal.dot(a) - normal.dot(point));
}

// Shortest distance between two (solid) rotated rectangles.
// relativePos is the position of square 1's centre relative to square 2's centre.
// Vendor: Geometry.cpp:192
inline float DistanceSquareToSquare(const Vec2& relativePos,
	const Vec2& u1, const Vec2& v1, const Vec2& halfSize1,
	const Vec2& u2, const Vec2& v2, const Vec2& halfSize2)
{
	// Two non-colliding squares: the shortest distance equals the distance from a
	// corner of one square to the other square. Take the minimum over all 8 corners.
	// Colliding squares return 0.
	float hw1 = halfSize1.x;
	float hh1 = halfSize1.y;
	float hw2 = halfSize2.x;
	float hh2 = halfSize2.y;
	if (TestRaySquare(relativePos + u1 * hw1 + v1 * hh1, relativePos - u1 * hw1 + v1 * hh1, u2, v2, halfSize2) ||
	    TestRaySquare(relativePos + u1 * hw1 + v1 * hh1, relativePos + u1 * hw1 - v1 * hh1, u2, v2, halfSize2))
		return 0.0f;

	return std::min(std::min(std::min(
			DistanceToSquare(relativePos + u1 * hw1 + v1 * hh1, u2, v2, halfSize2, true),
			DistanceToSquare(relativePos + u1 * hw1 - v1 * hh1, u2, v2, halfSize2, true)),
		std::min(
			DistanceToSquare(relativePos - u1 * hw1 + v1 * hh1, u2, v2, halfSize2, true),
			DistanceToSquare(relativePos - u1 * hw1 - v1 * hh1, u2, v2, halfSize2, true))),
		std::min(std::min(
			DistanceToSquare(relativePos + u2 * hw2 + v2 * hh2, u1, v1, halfSize1, true),
			DistanceToSquare(relativePos + u2 * hw2 - v2 * hh2, u1, v1, halfSize1, true)),
		std::min(
			DistanceToSquare(relativePos - u2 * hw2 + v2 * hh2, u1, v1, halfSize1, true),
			DistanceToSquare(relativePos - u2 * hw2 - v2 * hh2, u1, v1, halfSize1, true))));
}

} // namespace nav
} // namespace rtso

#endif // RTSO_NAV_GEOMETRY_H
