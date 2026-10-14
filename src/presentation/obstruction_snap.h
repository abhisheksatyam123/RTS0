#pragma once

// Snap to edges for building placement: 0 A.D. 0.28 simulation/helpers/ObstructionSnap.js
// (getPosition) over the edges Engine.GetEdgesOfStaticObstructionsOnScreenNearTo gives
// (source/simulation2/scripting/JSInterface_Simulation.cpp). Pure geometry on RTS0 world units;
// the caller collects the nearby structures and fits the result to a building cell.
//
// Angles follow 0 A.D.'s Position rotation (0 faces +z, pi/2 faces +x), which is RTS0's
// Building::rot, and vectors turn with 0 A.D.'s Rotate: (x cos + y sin, y cos - x sin).

#include <algorithm>
#include <cmath>
#include <vector>

namespace rtso {
namespace snap {

inline constexpr float kMetre = 0.25f; // one 0 A.D. metre in RTS0 units (1 unit = 4 m)
// default.cfg gui.session.snaptoedgesdistancethreshold
inline constexpr float kDistanceThreshold = 15.0f * kMetre;
inline constexpr float kMinimalDistanceToSnap = 5.0f * kMetre; // ObstructionSnap.MinimalDistanceToSnap
inline constexpr float kPadding = 0.05f * kMetre;               // getPadding for "cw" edges
inline constexpr float kMinHalfSize = 2.0f * kMetre;             // smaller obstructions give no edges
inline constexpr float kEps = 1e-3f;

struct Vec {
	float x = 0.0f, y = 0.0f;
};
inline Vec operator-(Vec a, Vec b) { return {a.x - b.x, a.y - b.y}; }
inline Vec operator+(Vec a, Vec b) { return {a.x + b.x, a.y + b.y}; }
inline Vec operator*(Vec a, float s) { return {a.x * s, a.y * s}; }
inline float dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y; }
inline Vec normalized(Vec a) {
	const float l = std::sqrt(dot(a, a));
	return l > 0.0f ? a * (1.0f / l) : a;
}
inline Vec rotate(Vec v, float a) {
	const float s = std::sin(a), c = std::cos(a);
	return {v.x * c + v.y * s, v.y * c - v.x * s};
}

// A structure's static obstruction: centre, half width (local x) and half depth (local z), rotation.
struct Rect {
	float x, z, half_w, half_d, rot;
};

struct Edge {
	Vec begin, end, normal;
	float angle = 0.0f;
	bool ccw = false; // a paired edge turned round (snapped from its inside, no padding)
	float signed_distance = 0.0f, offset_distance = 0.0f;
};

inline float distance_to_segment(Vec p, Vec a, Vec b) {
	const Vec ab = b - a;
	const float len2 = dot(ab, ab);
	float t = len2 > 0.0f ? dot(p - a, ab) / len2 : 0.0f;
	t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
	const Vec d = p - (a + ab * t);
	return std::sqrt(dot(d, d));
}

// GetEdgesOfStaticObstructionsOnScreenNearTo: each side of each obstruction within the threshold
// of (x, z), with its outward normal.
inline std::vector<Edge> edges_near(const std::vector<Rect> &rects, float x, float z,
		float threshold = kDistanceThreshold) {
	std::vector<Edge> out;
	const Vec pos{x, z};
	for (const Rect &r : rects) {
		if (r.half_w <= 0.0f || r.half_d <= 0.0f || std::max(r.half_w, r.half_d) <= kMinHalfSize) continue;
		Vec corners[4] = {{-r.half_w, -r.half_d}, {-r.half_w, r.half_d}, {r.half_w, r.half_d}, {r.half_w, -r.half_d}};
		for (Vec &c : corners) c = rotate(c, r.rot) + Vec{r.x, r.z};
		for (int i = 0; i < 4; ++i) {
			const Vec a = corners[i], b = corners[(i + 1) % 4];
			if (distance_to_segment(pos, a, b) > threshold) continue;
			const Vec d = b - a;
			Edge e;
			e.begin = a;
			e.end = b;
			e.angle = r.rot;
			e.normal = normalized(Vec{-d.y, d.x}); // -(next - corner).Perpendicular()
			out.push_back(e);
		}
	}
	return out;
}

// getValidEdges
inline std::vector<Edge> valid_edges(const std::vector<Edge> &all, Vec pos, float max_side) {
	std::vector<Edge> out;
	for (Edge e : all) {
		const float signed_distance = dot(e.normal, pos) - dot(e.normal, e.begin);
		if (signed_distance < -kMinimalDistanceToSnap - max_side || signed_distance > kMinimalDistanceToSnap + max_side)
			continue;
		const Vec dir1 = normalized(e.begin - e.end), dir2 = dir1 * -1.0f;
		float offset = std::max(dot(dir1, pos) - dot(dir1, e.begin), dot(dir2, pos) - dot(dir2, e.end));
		if (offset > kMinimalDistanceToSnap + max_side) continue;
		if (offset < 0.0f) offset = 0.0f;
		e.signed_distance = signed_distance;
		e.offset_distance = offset;
		out.push_back(e);
	}
	return out;
}

// compareEdges: negative when a is the better base.
inline float compare_edges(const Edge &a, const Edge &b) {
	const bool behind_a = a.signed_distance < -kEps, behind_b = b.signed_distance < -kEps;
	const float score_a = std::fabs(a.signed_distance) + a.offset_distance;
	const float score_b = std::fabs(b.signed_distance) + b.offset_distance;
	if (std::fabs(score_a - score_b) < kEps) {
		if (behind_a != behind_b) return (float)behind_a - (float)behind_b;
		if (!behind_a) return a.offset_distance - b.offset_distance;
		return -a.signed_distance + b.signed_distance;
	}
	return score_a - score_b;
}

struct Result {
	float x, z, angle;
};

// getPosition: the template's centre and angle flush against the nearest edge, then against a
// perpendicular one if there is one. False when no edge is near enough.
inline bool snap_position(const std::vector<Edge> &all, float half_w, float half_d, float x, float z, float angle,
		Result &out) {
	const float max_side = std::max(half_w, half_d);
	Vec pos{x, z};
	std::vector<Edge> edges = valid_edges(all, pos, max_side);
	if (edges.empty()) return false;
	Edge base = edges[0];
	for (const Edge &e : edges)
		if (compare_edges(e, base) < 0.0f) base = e;
	constexpr float kPi = 3.14159265358979f;
	for (int dir = 0; dir < 4; ++dir) {
		const float candidate = base.angle + dir * kPi / 2.0f;
		float diff = std::fabs(candidate - angle);
		diff = std::min(diff, kPi * 2.0f - diff);
		if (diff < kPi / 4.0f + kEps) {
			angle = candidate;
			break;
		}
	}
	// getNearestSizeAlongNormal
	const Vec front = rotate(Vec{0.0f, 1.0f}, angle);
	const bool facing = std::fabs(dot(front, base.normal)) > std::cos(kPi / 4.0f);
	const float size_to_base = facing ? half_d : half_w, size_to_paired = facing ? half_w : half_d;
	const auto padding = [](const Edge &e) { return e.ccw ? 0.0f : kPadding; };

	const float distance = dot(base.normal, pos) - dot(base.normal, base.begin);
	pos = pos - base.normal * (distance - size_to_base - padding(base));
	edges = valid_edges(all, pos, max_side);
	if (edges.size() > 1) {
		std::vector<Edge> paired;
		for (const Edge &e : edges) {
			if (std::fabs(dot(base.normal, e.normal)) > kEps) continue;
			Edge flipped = e;
			flipped.begin = e.end;
			flipped.end = e.begin;
			flipped.normal = e.normal * -1.0f;
			flipped.signed_distance = -e.signed_distance;
			flipped.ccw = true;
			paired.push_back(e);
			paired.push_back(flipped);
		}
		if (!paired.empty()) {
			Edge second = paired[0];
			for (const Edge &e : paired)
				if (compare_edges(e, second) < 0.0f) second = e;
			const float dist = dot(second.normal, pos) - dot(second.normal, second.begin);
			pos = pos - second.normal * (dist - size_to_paired - padding(second));
		}
	}
	out = {pos.x, pos.y, angle};
	return true;
}

} // namespace snap
} // namespace rtso
