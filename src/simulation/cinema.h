#ifndef RTSO_CINEMA_H
#define RTSO_CINEMA_H

// Cinematic camera paths, ported from 0 A.D. 0.28 (GPL-2.0-or-later):
//   source/simulation2/components/CCmpCinemaManager.cpp (the path queue and its timing)
//   source/simulation2/helpers/CinemaPath.cpp           (easing and where the camera looks)
//   source/maths/NUSpline.cpp                           (TNSpline: the timed Hermite spline)
//   source/graphics/MapReader.cpp ReadPaths             (a map's <Paths>, via tools/port_0ad_maps.py)
//
// CinemaState is simulation state (Simulation::cinema_, saved from SAVE_VERSION_CINEMA):
// the paths a map or script added, the queue of paths to play and how far into it the
// match is. The camera itself is presentation: cinema_camera() turns a path and the time
// into it into the eye and the point it looks at, the way CCinemaPath::Play does.

#include "core/math/det_math.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace rtso {

// One <Node> of a path: a position (world units) and the seconds since the spline's
// previous node (MapReader adds up the deltatime of nodes without one, per spline).
struct CinemaNode {
	float x = 0.0f, y = 0.0f, z = 0.0f;
	float dt = 0.0f; // ignored on the first node
};
static_assert(sizeof(CinemaNode) == 16, "CinemaNode is saved raw: keep it free of padding");

// CCinemaData m_Mode, m_Style.
enum CinemaMode : uint8_t { CINEMA_EASE_IN = 0, CINEMA_EASE_OUT, CINEMA_EASE_INOUT, CINEMA_EASE_OUTIN };
enum CinemaStyle : uint8_t { CINEMA_STYLE_DEFAULT = 0, CINEMA_STYLE_GROWTH, CINEMA_STYLE_EXPO, CINEMA_STYLE_CIRCLE, CINEMA_STYLE_SINE };

inline uint8_t cinema_mode_from_name(const char *s) {
	if (!std::strcmp(s, "ease_in")) return CINEMA_EASE_IN;
	if (!std::strcmp(s, "ease_out")) return CINEMA_EASE_OUT;
	if (!std::strcmp(s, "ease_outin")) return CINEMA_EASE_OUTIN;
	return CINEMA_EASE_INOUT; // "ease_inout", and 0 A.D.'s fallback for an unknown mode
}
inline uint8_t cinema_style_from_name(const char *s) {
	if (!std::strcmp(s, "growth")) return CINEMA_STYLE_GROWTH;
	if (!std::strcmp(s, "expo")) return CINEMA_STYLE_EXPO;
	if (!std::strcmp(s, "circle")) return CINEMA_STYLE_CIRCLE;
	if (!std::strcmp(s, "sine")) return CINEMA_STYLE_SINE;
	return CINEMA_STYLE_DEFAULT;
}

struct CinemaPathInfo {
	char name[32] = {};
	float timescale = 1.0f;          // camera seconds per simulation second (render side only)
	uint8_t mode = CINEMA_EASE_INOUT;
	uint8_t style = CINEMA_STYLE_DEFAULT;
	uint8_t look_at_target = 0;      // orientation="target"
	uint8_t reserved = 0;            // CINEMA_PATH_DELETED: DeletePath ran while it was queued
};
constexpr uint8_t CINEMA_PATH_DELETED = 1;
static_assert(sizeof(CinemaPathInfo) == 40, "CinemaPathInfo is saved raw: keep it free of padding");

struct CinemaPath {
	CinemaPathInfo info;
	std::vector<CinemaNode> nodes;   // the camera's spline
	std::vector<CinemaNode> targets; // what it looks at, when info.look_at_target

	// TNSpline MaxDistance of the position spline, the path's GetDuration().
	float duration_s() const {
		float d = 0.0f;
		for (size_t i = 1; i < nodes.size(); ++i) d += nodes[i].dt;
		return d;
	}
	uint32_t duration_ms() const { return (uint32_t)std::lround(duration_s() * 1000.0f); }
};

// CCmpCinemaManager.
struct CinemaState {
	uint8_t enabled = 0;      // a queue is playing (m_Enabled)
	uint8_t map_revealed = 0; // the reveal-map state before playing began
	uint8_t reserved[2] = {};
	uint32_t elapsed_ms = 0;      // m_ElapsedTime: since the queue started
	uint32_t path_elapsed_ms = 0; // m_CurrentPathElapsedTime: into the path at the front
	uint32_t total_ms = 0;        // m_TotalTime: the queued paths' durations
	std::vector<CinemaPath> paths;
	std::vector<uint16_t> queue;  // indices into paths, front first

	void clear() { *this = CinemaState(); }
	int find(const char *name) const {
		for (size_t i = 0; i < paths.size(); ++i)
			if (paths[i].info.reserved != CINEMA_PATH_DELETED && !std::strncmp(paths[i].info.name, name, sizeof(paths[i].info.name))) return (int)i;
		return -1;
	}
};

namespace cinema {

// TNSpline over a path's nodes: RNSpline::BuildSpline velocities, then TNSpline::Smooth
// (three rounds of SNSpline::Smooth and Constrain) three times, as CCinemaPath's
// constructor does. 0 A.D. keeps positions and times in fixed point; floats here.
struct Spline {
	struct Node { float p[3]; float v[3]; float dist; };
	std::vector<Node> n;
	float max_distance = 0.0f;

	explicit Spline(const std::vector<CinemaNode> &nodes) {
		for (size_t i = 0; i < nodes.size() && i < 128; ++i) { // MAX_SPLINE_NODES
			if (!n.empty()) {
				n.back().dist = nodes[i].dt;
				max_distance += nodes[i].dt;
			}
			n.push_back(Node{{nodes[i].x, nodes[i].y, nodes[i].z}, {0, 0, 0}, 0.0f});
		}
		build();
	}

	static void sub(const float *a, const float *b, float *o) { for (int k = 0; k < 3; ++k) o[k] = a[k] - b[k]; }
	static float len(const float *a) { return std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]); }
	static void normalize(float *a) {
		const float l = len(a);
		if (l > 0.0f) for (int k = 0; k < 3; ++k) a[k] /= l;
	}
	// RNSpline::GetStartVelocity / GetEndVelocity (equations 14 and 15).
	void start_velocity(int i, float *o) const {
		const int c = (int)n.size();
		if (i >= c - 1 || i < 0) { o[0] = o[1] = o[2] = 0.0f; return; }
		for (int k = 0; k < 3; ++k)
			o[k] = ((n[i + 1].p[k] - n[i].p[k]) * 3.0f * (1.0f / n[i].dist) - n[i + 1].v[k]) * 0.5f;
	}
	void end_velocity(int i, float *o) const {
		const int c = (int)n.size();
		if (i >= c || i < 1) { o[0] = o[1] = o[2] = 0.0f; return; }
		for (int k = 0; k < 3; ++k)
			o[k] = ((n[i].p[k] - n[i - 1].p[k]) * 3.0f * (1.0f / n[i - 1].dist) - n[i - 1].v[k]) * 0.5f;
	}
	void build() {
		const int c = (int)n.size();
		if (c < 2) return;
		if (c > 2) {
			for (int i = 1; i < c - 1; ++i) {
				float next[3], prev[3];
				sub(n[i + 1].p, n[i].p, next);
				sub(n[i - 1].p, n[i].p, prev);
				normalize(next);
				normalize(prev);
				for (int k = 0; k < 3; ++k) n[i].v[k] = next[k] - prev[k];
				normalize(n[i].v);
			}
		}
		float v[3];
		start_velocity(0, v);
		std::memcpy(n[0].v, v, sizeof v);
		end_velocity(c - 1, v);
		std::memcpy(n[c - 1].v, v, sizeof v);
		if (c == 2) return;
		for (int r = 0; r < 3; ++r)
			for (int s = 0; s < 3; ++s) {
				smooth();
				constrain();
			}
	}
	// SNSpline::Smooth (equation 12).
	void smooth() {
		const int c = (int)n.size();
		if (c < 3) return;
		float old_vel[3], new_vel[3], ev[3], sv[3];
		start_velocity(0, old_vel);
		for (int i = 1; i < c - 1; ++i) {
			end_velocity(i, ev);
			start_velocity(i, sv);
			for (int k = 0; k < 3; ++k)
				new_vel[k] = (ev[k] * n[i].dist + sv[k] * n[i - 1].dist) * (1.0f / (n[i - 1].dist + n[i].dist));
			std::memcpy(n[i - 1].v, old_vel, sizeof old_vel);
			std::memcpy(old_vel, new_vel, sizeof new_vel);
		}
		end_velocity(c - 1, ev);
		std::memcpy(n[c - 1].v, ev, sizeof ev);
		std::memcpy(n[c - 2].v, old_vel, sizeof old_vel);
	}
	// TNSpline::Constrain (equation 13).
	void constrain() {
		const int c = (int)n.size();
		if (c < 3) return;
		for (int i = 1; i < c - 1; ++i) {
			float d0[3], d1[3];
			sub(n[i].p, n[i - 1].p, d0);
			sub(n[i + 1].p, n[i].p, d1);
			const float r0 = len(d0) / n[i - 1].dist, r1 = len(d1) / n[i].dist;
			const float f = 4.0f * r0 * r1 / ((r0 + r1) * (r0 + r1));
			for (int k = 0; k < 3; ++k) n[i].v[k] *= f;
		}
	}
	// RNSpline::GetPosition, time 0..1 over the whole spline.
	void position(float time, float *out) const {
		const int c = (int)n.size();
		if (c < 2) { out[0] = out[1] = out[2] = 0.0f; return; }
		time = time < 0.0f ? 0.0f : time > 1.0f ? 1.0f : time;
		const float distance = time * max_distance;
		float cur = 0.0f;
		int i = 0;
		while (cur + n[i].dist < distance && i < c - 2) {
			cur += n[i].dist;
			++i;
		}
		const Node &a = n[i], &b = n[i + 1];
		if ((a.p[0] == b.p[0] && a.p[1] == b.p[1] && a.p[2] == b.p[2]) || a.dist < 1e-7f) {
			std::memcpy(out, b.p, sizeof b.p);
			return;
		}
		const float t = (distance - cur) / a.dist, t2 = t * t, t3 = t2 * t;
		// GetPositionOnCubic: the Hermite basis, velocities scaled by the segment's time.
		const float h0 = 2 * t3 - 3 * t2 + 1, h1 = -2 * t3 + 3 * t2, h2 = t3 - 2 * t2 + t, h3 = t3 - t2;
		for (int k = 0; k < 3; ++k)
			out[k] = h0 * a.p[k] + h1 * b.p[k] + (h2 * a.v[k] + h3 * b.v[k]) * a.dist;
	}
};

// CCinemaPath's DistStylePtr with CCinemaData's defaults (m_Growth 1, m_GrowthCount 0).
inline float ease_style(uint8_t style, float t) {
	switch (style) {
		case CINEMA_STYLE_GROWTH: return det::pow(t, 1.0f);
		case CINEMA_STYLE_EXPO: return t == 0.0f ? t : det::pow(1.0f, 10.0f * (t - 1.0f));
		case CINEMA_STYLE_CIRCLE: return -(std::sqrt(1.0f - t * t) - 1.0f);
		case CINEMA_STYLE_SINE: return 1.0f - det::cos(t * 3.14159265358979f / 2.0f);
		default: return t;
	}
}
// DistModePtr, with m_Switch 1.
inline float ease(uint8_t mode, uint8_t style, float t) {
	constexpr float sw = 1.0f;
	auto in = [style](float x) { return ease_style(style, x); };
	auto out = [&](float x) { return 1.0f - in(1.0f - x); };
	switch (mode) {
		case CINEMA_EASE_IN: return in(t);
		case CINEMA_EASE_OUT: return out(t);
		case CINEMA_EASE_OUTIN: return t < sw ? out(t / sw) * sw : in((t - sw) / sw) * sw + sw;
		default: return t < sw ? in(t / sw) * sw : out((t - sw) / sw) * sw + sw;
	}
}

// CCinemaPath::Play and MoveToPointAt: the camera eye and the point it looks at,
// elapsed_s camera seconds into the path. False past the path's end (Validate),
// where 0 A.D. leaves the camera as it was.
inline bool camera_at(const CinemaPath &path, const Spline &pos_spline, const Spline &target_spline,
		float elapsed_s, float eye[3], float look[3]) {
	const float duration = pos_spline.max_distance;
	if (pos_spline.n.size() < 2 || duration <= 0.0f || elapsed_s > duration || elapsed_s < 0.0f) return false;
	pos_spline.position(ease(path.info.mode, path.info.style, elapsed_s / duration), eye);
	if (path.info.look_at_target && !target_spline.n.empty()) {
		if (target_spline.n.size() >= 2 && elapsed_s <= target_spline.max_distance)
			target_spline.position(elapsed_s / target_spline.max_distance, look);
		else
			std::memcpy(look, target_spline.n.back().p, sizeof(float) * 3);
	} else {
		// No rotation splines in 0.28 (MapReader's TODO): the camera keeps 0 A.D.'s
		// identity orientation, facing +z.
		look[0] = eye[0];
		look[1] = eye[1];
		look[2] = eye[2] + 1.0f;
	}
	return true;
}

} // namespace cinema
} // namespace rtso

#endif
