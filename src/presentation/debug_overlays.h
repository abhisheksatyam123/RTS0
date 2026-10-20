#ifndef RTSO_DEBUG_OVERLAYS_H
#define RTSO_DEBUG_OVERLAYS_H

// The developer overlay's simulation overlays (0 A.D. 0.28
// gui/session/developer_overlay/DeveloperOverlayControlCheckboxes.js, GPL-2.0-or-later):
// Pathfinder overlay (CCmpPathfinder's passability grid: the navcells land units can't
// cross, and the last long and short path searches: LongOverlay's reached tiles and
// VertexPathfinderDebugOverlay's goal and visibility graph), Hierarchical pathfinder overlay (HierarchicalPathfinder's debug overlay: each
// chunk region of navcells in a colour of its own, and white links between the regions an
// edge joins), Obstruction overlay (CCmpObstructionManager::RenderSubmit: static shapes and
// still units blue, moving units magenta, the world bounds yellow), Unit motion overlay
// (g_Selection.SetMotionDebugOverlay, CCmpUnitMotion::RenderPath: each selected unit's
// long path white and short path red, a square at each waypoint), Range overlay
// (CCmpRangeManager::RenderSubmit: each range query's rings, green while enabled and red
// while off, rays to what it matches, and the queries' grid) and Bounding box overlay (Engine.SetBoundingBoxDebugOverlay, CCmpSelectable's debug
// overlays: each selected or hovered entity's model bounds red and its selection box
// green). Drawn whatever the line of sight, as 0 A.D. does, on one ImmediateMesh that
// follows the terrain.

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/directional_light3d.hpp>
#include <godot_cpp/classes/immediate_mesh.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/multi_mesh_instance3d.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/core/memory.hpp>

#include <algorithm>
#include <unordered_map>
#include <cstdint>

#include "../simulation/simulation.h"
#include "../simulation/nav/vertex_pathfinder.h"

namespace rtso {

class RtsoDebugOverlays {
public:
	enum Kind { PATHFINDER, HIER_PATHFINDER, OBSTRUCTION, UNIT_MOTION, RANGE, BOUNDING_BOX, CULL_CAMERA_FRUSTUM, SHADOWS_FRUSTUM, KIND_COUNT };

	void build(godot::Node *parent) {
		using namespace godot;
		mesh_.instantiate();
		inst_ = memnew(MeshInstance3D);
		inst_->set_name("DebugOverlays");
		inst_->set_mesh(mesh_);
		Ref<StandardMaterial3D> mat;
		mat.instantiate();
		mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
		mat->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
		mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
		mat->set_flag(BaseMaterial3D::FLAG_DISABLE_DEPTH_TEST, true);
		mat->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
		inst_->set_material_override(mat);
		inst_->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		parent->add_child(inst_);
	}

	void set(Kind k, bool on) {
		on_[k] = on;
		// The pathfinders keep their last searches only while the overlay shows them.
		if (k == PATHFINDER) {
			nav::jps_debug().on.store(on, std::memory_order_relaxed);
			nav::short_path_debug().on.store(on, std::memory_order_relaxed);
		}
	}
	bool on(Kind k) const { return on_[k]; }
	bool any() const {
		for (bool b : on_)
			if (b) return true;
		return false;
	}
	// Shapes drawn by each overlay last update (tests).
	int drawn(Kind k) const { return drawn_[k]; }
	int range_subdivisions() const { return range_subdivisions_; }
	int range_rays() const { return range_rays_; }
	int region_link_count() const { return region_links_; }
	// The last long path search's tiles and short path search's vertices drawn last update.
	int path_search_tiles() const { return path_tiles_; }
	int short_path_vertexes() const { return short_path_vertexes_; }

	// The units whose paths the unit motion overlay draws (the selection), and the
	// bounding box overlay's boxes (RtsoWorldView::model_boxes of the selected and
	// hovered entities), set before update.
	void set_motion_units(const std::vector<uint32_t> &ids) { motion_units_ = ids; }
	struct Box {
		godot::AABB world;
		godot::Transform3D xf;
		godot::AABB local;
	};
	std::vector<Box> &boxes() { return boxes_; }

	// world: the node the models are drawn under (RtsoWorldView), for the frustums.
	void update(const Simulation *sim, godot::Node *world) {
		if (!inst_ || (!any() && !inst_->is_visible())) return;
		mesh_->clear_surfaces();
		for (int &n : drawn_) n = 0;
		range_subdivisions_ = range_rays_ = region_links_ = path_tiles_ = short_path_vertexes_ = 0;
		inst_->set_visible(sim && any());
		if (!sim || !any()) return;
		sim_ = sim;
		if (on_[PATHFINDER]) pathfinder();
		if (on_[HIER_PATHFINDER]) hier_pathfinder();
		begun_ = false;
		if (on_[PATHFINDER]) short_path_search();
		if (on_[HIER_PATHFINDER]) region_links();
		if (on_[OBSTRUCTION]) obstruction();
		if (on_[UNIT_MOTION]) unit_motion();
		if (on_[RANGE]) range();
		if (on_[BOUNDING_BOX]) bounding_boxes();
		if (on_[CULL_CAMERA_FRUSTUM] && world) cull_camera_frustum(world);
		if (on_[SHADOWS_FRUSTUM] && world) shadows_frustum(world);
		if (begun_) mesh_->surface_end();
		sim_ = nullptr;
	}

	// The cull camera's corners (GameView.cpp m_CullCamera): the locked ones while
	// Lock cull camera holds them, else empty and the live camera is drawn.
	void set_cull_corners(const std::vector<Vector3> &corners) { cull_corners_ = corners; }

private:
	using Color = godot::Color;
	using Vector3 = godot::Vector3;

	// Navcells a land unit (PASS_INFANTRY, 0 A.D.'s "default" class) can't enter, filled red,
	// deep water blue.
	void pathfinder() {
		const NavGrid &g = sim_->nav_grid();
		const float cell = g.map_half * 2.0f / (float)g.res;
		bool open = false;
		for (int cy = 0; cy < g.res; ++cy) {
			for (int cx = 0; cx < g.res; ++cx) {
				if (g.is_passable_for(cx, cy, PASS_INFANTRY)) continue;
				if (!open) {
					mesh_->surface_begin(godot::Mesh::PRIMITIVE_TRIANGLES);
					open = true;
				}
				const Color c = g.cells.at(cx, cy) == NAV_WATER ? Color(0.2f, 0.4f, 1.0f, 0.3f) : Color(1.0f, 0.2f, 0.1f, 0.35f);
				const float x0 = -g.map_half + cx * cell, z0 = -g.map_half + cy * cell;
				const Vector3 a = ground(x0, z0), b = ground(x0 + cell, z0), d = ground(x0, z0 + cell), e = ground(x0 + cell, z0 + cell);
				tri(a, b, e, c);
				tri(a, e, d, c);
				++drawn_[PATHFINDER];
			}
		}
		// LongOverlay: the tiles the last long path search reached, open ones yellow and closed
		// ones green, bluer the later the search reached them (0.28 shades by the step that
		// last touched a tile, n.GetStep() / Steps).
		{
			nav::JpsDebug &d = nav::jps_debug();
			std::lock_guard<std::mutex> lock(d.m);
			const size_t n = d.x.size();
			path_tiles_ = (int)n;
			for (size_t k = 0; k < n; ++k) {
				if (!open) {
					mesh_->surface_begin(godot::Mesh::PRIMITIVE_TRIANGLES);
					open = true;
				}
				const float c = (float)k / (float)std::max<size_t>(n - 1, 1);
				const Color col = d.closed[k] ? Color(0.0f, 1.0f, c, 0.6f) : Color(1.0f, 1.0f, c, 0.6f);
				const float h = d.size * 0.5f;
				const Vector3 a = ground(d.x[k] - h, d.z[k] - h), b = ground(d.x[k] + h, d.z[k] - h),
							  e = ground(d.x[k] + h, d.z[k] + h), f = ground(d.x[k] - h, d.z[k] + h);
				tri(a, b, e, col);
				tri(a, e, f, col);
			}
		}
		if (open) mesh_->surface_end();
	}

	// VertexPathfinderDebugOverlay: the last short path search's goal red (a 0.2 m circle for a
	// point), its vertices yellow 0.5 m circles (open towards their inward quadrant), its edges
	// cyan with an arrowhead at their far end, and its edge squares cyan.
	void short_path_search() {
		nav::ShortPathDebug &d = nav::short_path_debug();
		std::lock_guard<std::mutex> lock(d.m);
		if (!d.searches) return;
		constexpr float kM = 0.25f; // a metre, at 4 m a unit
		const Color red(1.0f, 0.0f, 0.0f, 1.0f), yellow(1.0f, 1.0f, 0.0f, 1.0f), cyan(0.0f, 1.0f, 1.0f, 1.0f);
		const nav::ShortPathGoal &g = d.goal;
		if (g.type == nav::ShortPathGoal::POINT) ring(g.center.x, g.center.y, 0.2f * kM, red);
		else if (g.type == nav::ShortPathGoal::CIRCLE) ring(g.center.x, g.center.y, g.hw, red);
		else box(g.center.x, g.center.y, g.u.x, g.u.y, g.hw, g.hh, red);
		for (const nav::vp::Vertex &v : d.vertexes) {
			float a0 = 0.0f, a1 = 0.0f;
			if (v.quadInward == nav::vp::QUADRANT_BL) { a0 = -0.25f; a1 = 0.50f; }
			else if (v.quadInward == nav::vp::QUADRANT_TR) { a0 = 0.25f; a1 = 1.00f; }
			else if (v.quadInward == nav::vp::QUADRANT_TL) { a0 = -0.50f; a1 = 0.25f; }
			else if (v.quadInward == nav::vp::QUADRANT_BR) { a0 = 0.00f; a1 = 0.75f; }
			if (a0 == a1) ring(v.p.x, v.p.y, 0.5f * kM, yellow);
			else closed_arc(v.p.x, v.p.y, 0.5f * kM, a0 * (float)Math_TAU, a1 * (float)Math_TAU, yellow);
		}
		for (const nav::vp::Edge &e : d.edges) {
			line(e.p0.x, e.p0.y, e.p1.x, e.p1.y, cyan);
			float dx = e.p1.x - e.p0.x, dz = e.p1.y - e.p0.y;
			const float len = godot::Math::sqrt(dx * dx + dz * dz);
			if (len <= 0.0f) continue;
			dx *= kM / 8.0f / len; // 1/8 m along the edge
			dz *= kM / 8.0f / len;
			const float bx = e.p1.x - 2.0f * dx, bz = e.p1.y - 2.0f * dz; // Perpendicular(): (dz, -dx)
			line(bx + dz, bz - dx, bx - dz, bz + dx, cyan);
			line(bx - dz, bz + dx, e.p1.x, e.p1.y, cyan);
		}
		for (const nav::vp::Square &q : d.squares) {
			line(q.p0.x, q.p0.y, q.p0.x, q.p1.y, cyan);
			line(q.p0.x, q.p1.y, q.p1.x, q.p1.y, cyan);
			line(q.p1.x, q.p1.y, q.p1.x, q.p0.y, cyan);
			line(q.p1.x, q.p0.y, q.p0.x, q.p0.y, cyan);
		}
		short_path_vertexes_ = (int)d.vertexes.size();
	}

	// Each passable navcell tinted by its chunk region (NavGrid chunk_regions_, 0 A.D.'s
	// Chunk::m_Regions), a colour hashed from the chunk and the region.
	void hier_pathfinder() {
		const NavGrid &g = sim_->nav_grid();
		if (!g.regions_ready_) return;
		const float cell = g.map_half * 2.0f / (float)g.res;
		bool open = false;
		for (int cy = 0; cy < g.res; ++cy) {
			for (int cx = 0; cx < g.res; ++cx) {
				const uint8_t r = g.chunk_regions_.at(cx, cy);
				if (r == 0) continue;
				if (!open) {
					mesh_->surface_begin(godot::Mesh::PRIMITIVE_TRIANGLES);
					open = true;
				}
				uint32_t h = ((uint32_t)(cy / CHUNK_SIZE) * 131u + (uint32_t)(cx / CHUNK_SIZE)) * 2654435761u ^ (uint32_t)r * 40503u;
				h ^= h >> 15;
				const Color c((float)(h & 0xFF) / 255.0f, (float)((h >> 8) & 0xFF) / 255.0f, (float)((h >> 16) & 0xFF) / 255.0f, 0.3f);
				const float x0 = -g.map_half + cx * cell, z0 = -g.map_half + cy * cell;
				const Vector3 a = ground(x0, z0), b = ground(x0 + cell, z0), d = ground(x0, z0 + cell), e = ground(x0 + cell, z0 + cell);
				tri(a, b, e, c);
				tri(a, e, d, c);
				++drawn_[HIER_PATHFINDER];
			}
		}
		if (open) mesh_->surface_end();
	}

	// HierarchicalPathfinder::AddDebugEdges: a white line between the centres of every two
	// regions an edge joins (navcells side by side across a chunk border, FindEdges), each end
	// pushed 1 m inwards. A region's centre is its navcell nearest the mean of its navcells
	// (Chunk::RegionCenter).
	void region_links() {
		const NavGrid &g = sim_->nav_grid();
		if (!g.regions_ready_) return;
		const int res = g.res, cpa = (res + CHUNK_SIZE - 1) / CHUNK_SIZE;
		const float cell = g.map_half * 2.0f / (float)res;
		auto key = [&](int i, int j, uint8_t r) {
			return ((uint32_t)(j / CHUNK_SIZE) * (uint32_t)cpa + (uint32_t)(i / CHUNK_SIZE)) << 8 | r;
		};
		struct Centre {
			int64_t si = 0, sj = 0, n = 0;
			uint64_t best = UINT64_MAX;
			int bi = 0, bj = 0;
		};
		std::unordered_map<uint32_t, Centre> centres;
		for (int j = 0; j < res; ++j)
			for (int i = 0; i < res; ++i)
				if (const uint8_t r = g.chunk_regions_.at(i, j)) {
					Centre &c = centres[key(i, j, r)];
					c.si += i % CHUNK_SIZE;
					c.sj += j % CHUNK_SIZE;
					++c.n;
				}
		for (int j = 0; j < res; ++j)
			for (int i = 0; i < res; ++i)
				if (const uint8_t r = g.chunk_regions_.at(i, j)) {
					Centre &c = centres[key(i, j, r)];
					const int64_t di = (int64_t)(i % CHUNK_SIZE) * c.n - c.si, dj = (int64_t)(j % CHUNK_SIZE) * c.n - c.sj;
					const uint64_t d = (uint64_t)(di * di + dj * dj);
					if (d < c.best) {
						c.best = d;
						c.bi = i;
						c.bj = j;
					}
				}
		std::vector<uint64_t> pairs;
		auto edge = [&](int i0, int j0, int i1, int j1) {
			const uint8_t r0 = g.chunk_regions_.at(i0, j0), r1 = g.chunk_regions_.at(i1, j1);
			if (!r0 || !r1) return;
			const uint64_t a = key(i0, j0, r0), b = key(i1, j1, r1);
			pairs.push_back(std::min(a, b) << 32 | std::max(a, b));
		};
		for (int j = 0; j < res; ++j)
			for (int i = 0; i < res; ++i) {
				if ((i + 1) % CHUNK_SIZE == 0 && i + 1 < res) edge(i, j, i + 1, j);
				if ((j + 1) % CHUNK_SIZE == 0 && j + 1 < res) edge(i, j, i, j + 1);
			}
		std::sort(pairs.begin(), pairs.end());
		pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());
		const Color white(1.0f, 1.0f, 1.0f, 1.0f);
		for (const uint64_t p : pairs) {
			const Centre &c0 = centres[(uint32_t)(p >> 32)], &c1 = centres[(uint32_t)p];
			float ax = -g.map_half + (c0.bi + 0.5f) * cell, az = -g.map_half + (c0.bj + 0.5f) * cell;
			float bx = -g.map_half + (c1.bi + 0.5f) * cell, bz = -g.map_half + (c1.bj + 0.5f) * cell;
			const float dx = bx - ax, dz = bz - az, len = godot::Math::sqrt(dx * dx + dz * dz);
			if (len > 0.0f) {
				const float px = dx / len * 0.25f, pz = dz / len * 0.25f; // 1 m, at 4 m a unit
				ax += px;
				az += pz;
				bx -= px;
				bz -= pz;
			}
			line(ax, az, bx, bz, white);
		}
		region_links_ = (int)pairs.size();
	}

	void obstruction() {
		const Color still(0.0f, 0.0f, 1.0f, 1.0f), moving(1.0f, 0.0f, 1.0f, 1.0f), bounds(1.0f, 1.0f, 0.0f, 1.0f);
		for (const auto &b : sim_->buildings()) {
			if (b.wrecked) continue;
			box(b.x, b.z, b.ux, b.uz, b.hu, b.hv, still);
			++drawn_[OBSTRUCTION];
		}
		auto rv = sim_->resources();
		for (size_t r = 0; r < rv.count; ++r) {
			if (!rv.alive[r]) continue;
			nav::Footprint f;
			if (sim_->mine_footprint(r, f)) box(f.x, f.z, f.ux, f.uz, f.hu, f.hv, still);
			else {
				const float h = sim_->resource_block_half(r);
				box(rv.x[r], rv.z[r], 1.0f, 0.0f, h, h, still);
			}
			++drawn_[OBSTRUCTION];
		}
		auto uv = sim_->units();
		for (size_t i = 0; i < uv.count; ++i) {
			if (!uv.alive[i]) continue;
			const float h = unit_collision_radius(uv.unit_type[i]);
			const bool mv = uv.vel_x[i] * uv.vel_x[i] + uv.vel_z[i] * uv.vel_z[i] > 1e-6f;
			box(uv.pos_x[i], uv.pos_z[i], 1.0f, 0.0f, h, h, mv ? moving : still);
			++drawn_[OBSTRUCTION];
		}
		const float m = sim_->map_half();
		if (sim_->circular_map()) ring(0.0f, 0.0f, m, bounds);
		else box(0.0f, 0.0f, 1.0f, 0.0f, m, m, bounds);
	}

	// CCmpUnitMotion::RenderPath: the path from the unit through each waypoint, with a 1 m
	// square on each (ConstructSquareOnGround); the long path white, the short path red.
	void unit_motion() {
		const Color long_path(1.0f, 1.0f, 1.0f, 1.0f), short_path(1.0f, 0.0f, 0.0f, 1.0f);
		constexpr float kSquare = 0.5f * 0.25f; // half of 1 m, at 4 m a unit
		auto uv = sim_->units();
		if (motion_units_.empty()) return;
		for (size_t i = 0; i < uv.count; ++i) {
			if (!uv.alive[i] || std::find(motion_units_.begin(), motion_units_.end(), uv.id[i]) == motion_units_.end()) continue;
			const float *xs = nullptr, *zs = nullptr;
			const int n = sim_->unit_waypoints_left(i, &xs, &zs);
			float px = uv.pos_x[i], pz = uv.pos_z[i];
			for (int k = 0; k < n; ++k) {
				line(px, pz, xs[k], zs[k], long_path);
				box(xs[k], zs[k], 1.0f, 0.0f, kSquare, kSquare, long_path);
				px = xs[k];
				pz = zs[k];
			}
			// The short path leads to the next long-path waypoint, or the goal itself.
			const float ax = n > 0 ? xs[0] : uv.target_x[i], az = n > 0 ? zs[0] : uv.target_z[i];
			float wx, wz;
			if (sim_->unit_short_waypoint(i, ax, az, &wx, &wz)) {
				line(uv.pos_x[i], uv.pos_z[i], wx, wz, short_path);
				line(wx, wz, ax, az, short_path);
				box(wx, wz, 1.0f, 0.0f, kSquare, kSquare, short_path);
				box(ax, az, 1.0f, 0.0f, kSquare, kSquare, short_path);
			}
			++drawn_[UNIT_MOTION];
		}
	}

	// CCmpRangeManager::RenderSubmit: each range query's max range ring (green while the
	// query is enabled, red while it is off) and its MinRange ring alike, a faint yellow ray
	// from each enabled query's source to every entity it matches, and the queries' grid
	// (m_Subdivision) as blue squares. Queries: each unit's attack query and a healer's heal
	// query (UnitAI), each armed structure's arrow range (BuildingAI) and each range aura
	// (Auras.js).
	void range() {
		const Color enabled(0.0f, 1.0f, 0.0f, 1.0f), disabled(1.0f, 0.0f, 0.0f, 1.0f), ray(1.0f, 1.0f, 0.0f, 0.2f),
				subdiv(0.0f, 0.0f, 1.0f, 1.0f);
		auto uv = sim_->units();
		// The units by grid cell, for the rays.
		const int res = Simulation::unit_grid_res();
		const float m = sim_->map_half(), cell = m * 2.0f / (float)res;
		auto cell_of = [&](float v) { return std::clamp((int)((v + m) / cell), 0, res - 1); };
		std::vector<int32_t> head((size_t)res * res, -1), next(uv.count, -1);
		for (size_t i = 0; i < uv.count; ++i) {
			if (!uv.alive[i]) continue;
			const size_t c = (size_t)cell_of(uv.pos_z[i]) * res + cell_of(uv.pos_x[i]);
			next[i] = head[c];
			head[c] = (int32_t)i;
		}
		const auto &blds = sim_->buildings();
		// Rays to the units (and, for the attack query, structures) between lo and hi of
		// the source at (x, z); hostile: enemies (the attack query), else allies (heal).
		auto rays = [&](float x, float z, uint8_t team, float lo, float hi, bool hostile, size_t self) {
			const int x0 = cell_of(x - hi), x1 = cell_of(x + hi), z0 = cell_of(z - hi), z1 = cell_of(z + hi);
			for (int cz = z0; cz <= z1; ++cz)
				for (int cx = x0; cx <= x1; ++cx)
					for (int32_t j = head[(size_t)cz * res + cx]; j >= 0; j = next[j]) {
						if ((size_t)j == self) continue;
						const bool ok = hostile ? sim_->is_hostile(team, uv.team[j])
												: uv.team[j] == team || sim_->is_ally(team, uv.team[j]);
						if (!ok) continue;
						const float dx = uv.pos_x[j] - x, dz = uv.pos_z[j] - z, d2 = dx * dx + dz * dz;
						if (d2 > hi * hi || d2 < lo * lo) continue;
						line(x, z, uv.pos_x[j], uv.pos_z[j], ray);
						++range_rays_;
					}
			if (!hostile) return;
			for (const auto &b : blds) {
				if (b.wrecked || !sim_->is_hostile(team, b.team)) continue;
				const float dx = b.x - x, dz = b.z - z, d2 = dx * dx + dz * dz;
				if (d2 > hi * hi || d2 < lo * lo) continue;
				line(x, z, b.x, b.z, ray);
				++range_rays_;
			}
		};
		for (size_t i = 0; i < uv.count; ++i) {
			if (!uv.alive[i]) continue;
			const Simulation::RangeQuery q = sim_->unit_range_query(i);
			if (q.max <= 0.0f && q.heal_max <= 0.0f) continue;
			const Color &c = q.enabled ? enabled : disabled;
			if (q.max > 0.0f) {
				ring(uv.pos_x[i], uv.pos_z[i], q.max, c);
				if (q.min > 0.0f) ring(uv.pos_x[i], uv.pos_z[i], q.min, c);
				if (q.enabled) rays(uv.pos_x[i], uv.pos_z[i], uv.team[i], q.min, q.max, true, i);
			}
			if (q.heal_max > 0.0f) {
				ring(uv.pos_x[i], uv.pos_z[i], q.heal_max, c);
				if (q.enabled) rays(uv.pos_x[i], uv.pos_z[i], uv.team[i], 0.0f, q.heal_max, false, i);
			}
			++drawn_[RANGE];
		}
		for (const auto &b : blds) {
			if (b.wrecked || b.build_progress < 1.0f) continue;
			const float r = sim_->building_arrow_range(b);
			if (r <= 0.0f) continue;
			ring(b.x, b.z, r + b.half_size, enabled);
			rays(b.x, b.z, b.team, 0.0f, r + b.half_size, true, (size_t)-1);
			++drawn_[RANGE];
		}
		// Auras.js range auras: one query per aura, whatever it modifies.
		const auto &srcs = sim_->aura_manager().sources;
		for (size_t k = 0; k < srcs.size(); ++k) {
			const AuraSource &s = srcs[k];
			if (!s.alive || s.range <= 0.0f) continue;
			bool seen = false;
			for (size_t e = 0; e < k && !seen; ++e)
				seen = srcs[e].source_id == s.source_id && srcs[e].aura == s.aura && srcs[e].range == s.range;
			if (seen) continue;
			ring(s.x, s.z, s.range, enabled);
			++drawn_[RANGE];
		}
		// Each cell's square, drawn as the grid's lines (neighbours share their edges).
		for (int k = 0; k <= res; ++k) {
			const float v = -m + k * cell;
			line(v, -m, v, m, subdiv);
			line(-m, v, m, v, subdiv);
		}
		range_subdivisions_ = res * res;
	}

	// CCmpSelectable::RenderSubmit with ms_EnableDebugOverlays: the visual's bounds red
	// (ConstructBoxOutline of GetBounds) and its selection box green (GetSelectionBox).
	void bounding_boxes() {
		const Color bounds(1.0f, 0.0f, 0.0f, 1.0f), selection(0.0f, 1.0f, 0.0f, 1.0f);
		for (const Box &b : boxes_) {
			obox(godot::Transform3D(), b.world, bounds);
			obox(b.xf, b.local, selection);
		}
	}
	// The 12 edges of box a turned by xf.
	void obox(const godot::Transform3D &xf, const godot::AABB &a, const Color &c) {
		Vector3 p[8];
		for (int i = 0; i < 8; ++i) p[i] = xf.xform(a.get_endpoint(i));
		edges(p, c);
		++drawn_[BOUNDING_BOX];
	}
	// The 12 edges of 8 box corners.
	void edges(const Vector3 p[8], const Color &c) {
		static const int kEdges[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
		for (const auto &e : kEdges) seg(p[e[0]], p[e[1]], c);
	}

	// SceneRenderer.cpp DisplayFrustum: the cull camera's frustum (the locked one
	// while Lock cull camera holds it, else the live view camera's) as 12 edges.
	void cull_camera_frustum(godot::Node *world) {
		using namespace godot;
		const Color c(1.0f, 1.0f, 1.0f, 0.8f);
		Vector3 p[8];
		if (cull_corners_.size() == 8) {
			for (int i = 0; i < 8; ++i) p[i] = cull_corners_[(size_t)i];
		} else {
			Viewport *vp = world->get_viewport();
			Camera3D *cam = vp ? vp->get_camera_3d() : nullptr;
			if (!cam || !camera_slice(cam, cam->get_near(), cam->get_far(), p)) return;
		}
		edges(p, c);
		++drawn_[CULL_CAMERA_FRUSTUM];
	}

	// CShadowMap::RenderDebugBounds: each PSSM cascade's ortho box, fitted round the
	// view frustum's slice in sun space, as 12 edges apiece.
	void shadows_frustum(godot::Node *world) {
		using namespace godot;
		Viewport *vp = world->get_viewport();
		Camera3D *cam = vp ? vp->get_camera_3d() : nullptr;
		Node *scene = world->get_parent();
		DirectionalLight3D *sun = scene ? Object::cast_to<DirectionalLight3D>(scene->get_node_or_null(NodePath("Sun"))) : nullptr;
		if (!cam || !sun || !sun->is_visible_in_tree()) return;
		// The PSSM splits and range live on the light (main.tscn Sun); fall back to
		// the camera's depth when unset.
		auto prop = [&](const char *name, float fallback) {
			godot::Variant v = sun->get(name);
			return v.get_type() == godot::Variant::FLOAT || v.get_type() == godot::Variant::INT ? (float)v : fallback;
		};
		const float max_d = prop("directional_shadow_max_distance", cam->get_far());
		const float splits[5] = { cam->get_near(), max_d * prop("directional_shadow_split_1", 0.1f),
				max_d * prop("directional_shadow_split_2", 0.3f), max_d * prop("directional_shadow_split_3", 0.6f), max_d };
		const Transform3D to_sun = sun->get_global_transform().affine_inverse();
		const Transform3D to_world = sun->get_global_transform();
		static const Color kCascade[4] = { Color(1, 1, 1, 0.8f), Color(0, 1, 1, 0.8f), Color(1, 1, 0, 0.8f), Color(1, 0, 1, 0.8f) };
		for (int s = 0; s < 4; ++s) {
			if (!(splits[s + 1] > splits[s])) continue;
			Vector3 w[8];
			if (!camera_slice(cam, splits[s], splits[s + 1], w)) continue;
			Vector3 lo(1e30f, 1e30f, 1e30f), hi(-1e30f, -1e30f, -1e30f);
			for (int i = 0; i < 8; ++i) {
				const Vector3 l = to_sun.xform(w[i]);
				lo.x = std::min(lo.x, l.x); lo.y = std::min(lo.y, l.y); lo.z = std::min(lo.z, l.z);
				hi.x = std::max(hi.x, l.x); hi.y = std::max(hi.y, l.y); hi.z = std::max(hi.z, l.z);
			}
			Vector3 p[8];
			for (int i = 0; i < 8; ++i)
				p[i] = to_world.xform(Vector3(i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z));
			edges(p, kCascade[s]);
			++drawn_[SHADOWS_FRUSTUM];
		}
	}

	// The 8 world corners of a camera's frustum between two depths: each viewport
	// corner's ray at each depth.
	static bool camera_slice(godot::Camera3D *cam, float d_near, float d_far, Vector3 p[8]) {
		using namespace godot;
		if (!cam || !(d_far > d_near) || !cam->get_viewport()) return false;
		const Vector2 size = cam->get_viewport()->get_visible_rect().size;
		if (size.x <= 0.0f || size.y <= 0.0f) return false;
		const Vector2 at[4] = { Vector2(0, 0), Vector2(size.x, 0), Vector2(0, size.y), Vector2(size.x, size.y) };
		for (int i = 0; i < 4; ++i) {
			const Vector3 o = cam->project_ray_origin(at[i]);
			const Vector3 d = cam->project_ray_normal(at[i]);
			p[i] = o + d * d_near;
			p[i + 4] = o + d * d_far;
		}
		return true;
	}

	Vector3 ground(float x, float z) const { return Vector3(x, sim_->get_terrain_height(x, z) + 0.2f, z); }
	void begin() {
		if (begun_) return;
		mesh_->surface_begin(godot::Mesh::PRIMITIVE_LINES);
		begun_ = true;
	}
	void tri(const Vector3 &a, const Vector3 &b, const Vector3 &c, const Color &col) {
		for (const Vector3 *v : { &a, &b, &c }) {
			mesh_->surface_set_color(col);
			mesh_->surface_add_vertex(*v);
		}
	}
	void seg(const Vector3 &a, const Vector3 &b, const Color &c) {
		begin();
		mesh_->surface_set_color(c);
		mesh_->surface_add_vertex(a);
		mesh_->surface_set_color(c);
		mesh_->surface_add_vertex(b);
	}
	// A straight line, split so it follows the ground.
	void line(float x0, float z0, float x1, float z1, const Color &c) {
		const float len = godot::Math::sqrt((x1 - x0) * (x1 - x0) + (z1 - z0) * (z1 - z0));
		const int n = std::clamp((int)(len / 1.5f), 1, 64);
		Vector3 prev = ground(x0, z0);
		for (int k = 1; k <= n; ++k) {
			const float t = (float)k / n;
			const Vector3 p = ground(x0 + (x1 - x0) * t, z0 + (z1 - z0) * t);
			seg(prev, p, c);
			prev = p;
		}
	}
	// A rectangle centred on (x, z), half extents hu along (ux, uz) and hv across it.
	void box(float x, float z, float ux, float uz, float hu, float hv, const Color &c) {
		const float vx = -uz, vz = ux;
		const float px[4] = { x + ux * hu + vx * hv, x - ux * hu + vx * hv, x - ux * hu - vx * hv, x + ux * hu - vx * hv };
		const float pz[4] = { z + uz * hu + vz * hv, z - uz * hu + vz * hv, z - uz * hu - vz * hv, z + uz * hu - vz * hv };
		for (int k = 0; k < 4; ++k) line(px[k], pz[k], px[(k + 1) % 4], pz[(k + 1) % 4], c);
	}
	void ring(float cx, float cz, float r, const Color &c) {
		const int steps = std::clamp((int)(r * 3.0f), 16, 256);
		Vector3 prev = ground(cx + r, cz);
		for (int k = 1; k <= steps; ++k) {
			const float t = (float)k / steps * (float)Math_TAU;
			const Vector3 p = ground(cx + r * godot::Math::cos(t), cz + r * godot::Math::sin(t));
			seg(prev, p, c);
			prev = p;
		}
	}

	// SimRender::ConstructClosedArcOnGround: from the centre out to the arc from a0 to a1 (radians,
	// measured as sin for x and cos for z) and back.
	void closed_arc(float cx, float cz, float r, float a0, float a1, const Color &c) {
		const int steps = 12;
		Vector3 prev = ground(cx, cz);
		for (int k = 0; k <= steps; ++k) {
			const float t = a0 + (a1 - a0) * (float)k / steps;
			const Vector3 p = ground(cx + r * godot::Math::sin(t), cz + r * godot::Math::cos(t));
			seg(prev, p, c);
			prev = p;
		}
		seg(prev, ground(cx, cz), c);
	}

	bool on_[KIND_COUNT] = {};
	int drawn_[KIND_COUNT] = {};
	std::vector<Vector3> cull_corners_; // Lock cull camera's frustum corners (empty: follow the camera)
	std::vector<uint32_t> motion_units_;
	std::vector<Box> boxes_;
	const Simulation *sim_ = nullptr;
	godot::Ref<godot::ImmediateMesh> mesh_;
	godot::MeshInstance3D *inst_ = nullptr;
	bool begun_ = false;
	int path_tiles_ = 0, short_path_vertexes_ = 0; // the pathfinder overlay's search view last update (tests)
	int region_links_ = 0; // the hierarchical overlay's region links last update (tests)
	int range_subdivisions_ = 0, range_rays_ = 0; // the range overlay's grid squares and rays last update (tests)
};

} // namespace rtso

#endif
