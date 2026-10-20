#include "minimap_view.h"
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/time.hpp>
#include "display_colors.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/rect2.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/geometry2d.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <vector>
#include "user_config.h"

using namespace godot;

void RtsoMiniMapView::_bind_methods() {
	ADD_SIGNAL(MethodInfo("minimap_clicked", PropertyInfo(Variant::VECTOR2, "world_pos")));
	ADD_SIGNAL(MethodInfo("flare_requested", PropertyInfo(Variant::VECTOR2, "world_pos")));
	ADD_SIGNAL(MethodInfo("minimap_order", PropertyInfo(Variant::VECTOR2, "world_pos")));
	ClassDB::bind_method(D_METHOD("add_ping", "world_pos", "color"), &RtsoMiniMapView::add_ping, DEFVAL(Color(1.0f, 0.85f, 0.2f)));
	ClassDB::bind_method(D_METHOD("toggle_territory"), &RtsoMiniMapView::toggle_territory);
	ClassDB::bind_method(D_METHOD("toggle_resources"), &RtsoMiniMapView::toggle_resources);
	ClassDB::bind_method(D_METHOD("toggle_fow"), &RtsoMiniMapView::toggle_fow);
	ClassDB::bind_method(D_METHOD("cycle_shroud"), &RtsoMiniMapView::cycle_shroud);
	ClassDB::bind_method(D_METHOD("toggle_diplomacy"), &RtsoMiniMapView::toggle_diplomacy);
}

RtsoMiniMapView::RtsoMiniMapView() {
	// Inside the minimap panel at session.xml's 50%-512 (minimap_panel_view.h).
	set_anchor(SIDE_LEFT, 0.5f);
	set_anchor(SIDE_RIGHT, 0.5f);
	set_anchor(SIDE_TOP, 1.0f);
	set_anchor(SIDE_BOTTOM, 1.0f);
	set_offset(SIDE_LEFT, -504.0f);
	set_offset(SIDE_RIGHT, -314.0f);
	set_offset(SIDE_TOP, -204.0f);
	set_offset(SIDE_BOTTOM, -14.0f);
	set_custom_minimum_size(Vector2(190, 190));
}

RtsoMiniMapView::~RtsoMiniMapView() {}

static constexpr uint64_t kFlareLifetimeMs = 6000; // MiniMap.xml flare_lifetime_seconds

// The part of segment a-b inside the map (CMiniMap CropPointsByCircle on a circular
// map; the square on a square one). False when none of it is.
static bool crop_segment(Vector2 &a, Vector2 &b, float half, bool round) {
	const Vector2 d = b - a;
	float t0 = 0.0f, t1 = 1.0f;
	if (round) {
		// |a + t d|^2 = half^2
		const float A = d.dot(d), B = 2.0f * a.dot(d), C = a.dot(a) - half * half;
		if (A < 1e-8f) return C < 0.0f;
		const float disc = B * B - 4.0f * A * C;
		if (disc <= 0.0f) return false;
		const float s = std::sqrt(disc);
		t0 = std::max(t0, (-B - s) / (2.0f * A));
		t1 = std::min(t1, (-B + s) / (2.0f * A));
	} else {
		// Liang-Barsky against [-half, half]^2.
		const float p[4] = {-d.x, d.x, -d.y, d.y};
		const float q[4] = {a.x + half, half - a.x, a.y + half, half - a.y};
		for (int i = 0; i < 4; ++i) {
			if (std::fabs(p[i]) < 1e-8f) {
				if (q[i] < 0.0f) return false;
				continue;
			}
			const float r = q[i] / p[i];
			if (p[i] < 0.0f) t0 = std::max(t0, r);
			else t1 = std::min(t1, r);
		}
	}
	if (t0 >= t1) return false;
	const Vector2 a0 = a;
	a = a0 + d * t0;
	b = a0 + d * t1;
	return true;
}

void RtsoMiniMapView::add_ping(Vector2 world_pos, Color col) {
	Vector2 sz = get_size();
	float half_map = sim ? sim->map_half() : 64.0f;
	float nx = (world_pos.x + half_map) / (half_map * 2.0f);
	float nz = (world_pos.y + half_map) / (half_map * 2.0f);
	Vector2 mpos(nx * sz.x, nz * sz.y);

	MapPing p;
	p.mini_pos = mpos;
	p.start_ms = Time::get_singleton()->get_ticks_msec();
	p.color = col;
	pings.push_back(p);
	queue_redraw();
}

static constexpr uint64_t kPingDurationMs = 50000; // default.cfg gui.session.minimap.pingduration
static constexpr uint64_t kBlinkDurationMs = 1700; // gui.session.minimap.blinkduration

void RtsoMiniMapView::ping_entity(uint32_t id) {
	entity_pings_[id] = Time::get_singleton()->get_ticks_msec() + kPingDurationMs; // a new hit restarts it
}

bool RtsoMiniMapView::entity_ping_lit(uint32_t id, uint64_t now_ms) const {
	auto it = entity_pings_.find(id);
	if (it == entity_pings_.end() || now_ms >= it->second) return false;
	const uint64_t since = kPingDurationMs - (it->second - now_ms);
	return (since % kBlinkDurationMs) < kBlinkDurationMs / 2;
}

void RtsoMiniMapView::sync(const rtso::Simulation *s, Vector3 p_cam_pos) {
	sim = s;
	cam_pos = p_cam_pos;

	// Decay pings
	const uint64_t now_ms = Time::get_singleton()->get_ticks_msec();
	pings.erase(std::remove_if(pings.begin(), pings.end(),
		[&](const MapPing &p) { return now_ms - p.start_ms >= kFlareLifetimeMs; }), pings.end());
	if (!entity_pings_.empty()) {
		const uint64_t now = Time::get_singleton()->get_ticks_msec();
		for (auto it = entity_pings_.begin(); it != entity_pings_.end();)
			it = now >= it->second ? entity_pings_.erase(it) : std::next(it);
	}

	queue_redraw();
}

void RtsoMiniMapView::_gui_input(const Ref<InputEvent> &event) {
	Ref<InputEventMouseButton> mb = event;
	if (mb.is_valid()) {
		Vector2 sz = get_size();
		Vector2 mpos = view_xf_.affine_inverse().xform(mb->get_position());
		float half = sim ? sim->map_half() : 64.0f;
		float wx = (mpos.x / sz.x) * (half * 2.0f) - half;
		float wz = (mpos.y / sz.y) * (half * 2.0f) - half;
		// CMiniMap::IsMouseOver: a circular map's minimap is the circle, so a press in a corner misses it;
		// anywhere off the world (a square map's edge frame included) misses too.
		if (sim && mb->is_pressed() && sim->off_world(wx, wz)) return;

		if (mb->get_button_index() == MOUSE_BUTTON_LEFT) {
			dragging = mb->is_pressed();
			emit_signal("minimap_clicked", Vector2(wx, wz));
			accept_event();
		} else if (mb->get_button_index() == MOUSE_BUTTON_RIGHT && mb->is_pressed()) {
			// Right-click orders the selection there or sets the rally point
			// (0 A.D. MiniMap.js onWorldClick); K+click sends a flare.
			emit_signal("minimap_order", Vector2(wx, wz));
			accept_event();
		}
	}

	Ref<InputEventMouseMotion> mm = event;
	if (mm.is_valid() && dragging) {
		Vector2 sz = get_size();
		Vector2 mpos = view_xf_.affine_inverse().xform(mm->get_position());
		float half = sim ? sim->map_half() : 64.0f;
		float wx = (mpos.x / sz.x) * (half * 2.0f) - half;
		float wz = (mpos.y / sz.y) * (half * 2.0f) - half;
		emit_signal("minimap_clicked", Vector2(wx, wz));
		accept_event();
	}
}

// Territory tint upload — closes docs/REMAINING_GAPS.md:148 ("upload
// territory_grid_ as texture, minimap overlay matches sim"). Rasterises
// sim->territory_grid() (cells written by Simulation::rebuild_territory_grids,
// CCmpTerritoryManager's influence flood fill, cell = team+1,
// 0 = unclaimed, same mapping as territory_owner_at simulation.cpp:762-769)
// into a single RGBA8 ImageTexture. The grid bytes are memcmp'd once per
// _draw() and the texture only re-uploaded when ownership actually changes.
// The player's drawn colour: its Game Setup colour (0 A.D. PlayerColor,
// rtso::PLAYER_COLORS), or its diplomacy colour (display_colors.h); Gaia is white.
Color RtsoMiniMapView::player_color(uint8_t pid) const {
	const uint32_t rgb = sim ? rtso::display_colors::rgb(*sim, pid) : 0xFFFFFFu;
	return Color(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, 1.0f);
}

void RtsoMiniMapView::toggle_diplomacy() {
	rtso::display_colors::toggle();
	queue_redraw();
}

Ref<Image> rtso_terrain_layer_image(const String &name); // world_view_node.cpp

bool RtsoMiniMapView::ensure_terrain_texture(bool round) {
	if (!sim) return false;
	const auto &heights = sim->terrain_heights();
	const int hres = sim->terrain_res();
	if (heights.empty() || hres <= 1) return false;
	// 0 A.D. rebuilds the texture when the terrain is marked dirty; RTS0 hashes the
	// heights and tiles at most twice a second (the minimap's own update rate).
	const uint64_t now = Time::get_singleton()->get_ticks_msec();
	if (terrain_tex_.is_valid() && now - terrain_checked_ms_ < 500) return true;
	terrain_checked_ms_ = now;
	const auto &tt = sim->terrain_tiles();
	uint64_t key = 1469598103934665603ull;
	auto mix = [&](const void *p, size_t n) {
		const uint8_t *b = (const uint8_t *)p;
		for (size_t i = 0; i < n; ++i) key = (key ^ b[i]) * 1099511628211ull;
	};
	for (size_t i = 0; i < heights.size(); i += 7) mix(&heights[i], sizeof(float)); // a sample keeps the check cheap
	mix(tt.tiles.data(), tt.tiles.size());
	const float water = sim->water_height();
	mix(&water, sizeof(water));
	mix(&round, sizeof(round));
	if (terrain_tex_.is_valid() && key == terrain_key_) return true;
	terrain_key_ = key;

	// CTerrainTextureEntry::GetBaseColor: the texture's smallest mip level.
	std::vector<Color> base(tt.names.size(), Color(0.46f, 0.42f, 0.33f));
	for (size_t k = 0; k < tt.names.size(); ++k) {
		Ref<Image> img = rtso_terrain_layer_image(String::utf8(tt.names[k].c_str()));
		if (img.is_null() || img->get_format() != Image::FORMAT_RGBA8) continue;
		const int last = img->get_mipmap_count();
		const PackedByteArray d = img->get_data();
		const int64_t off = img->get_mipmap_offset(last);
		if (off + 3 < d.size()) base[k] = Color(d[off] / 255.0f, d[off + 1] / 255.0f, d[off + 2] / 255.0f);
	}
	const float half = sim->map_half();
	const int n = tt.empty() ? hres - 1 : tt.res;
	const float step = (half * 2.0f) / (float)(hres - 1);
	auto height_at = [&](float x, float z) {
		const float fx = Math::clamp((x + half) / step, 0.0f, (float)(hres - 1));
		const float fz = Math::clamp((z + half) / step, 0.0f, (float)(hres - 1));
		const int x0 = std::min((int)fx, hres - 2), z0 = std::min((int)fz, hres - 2);
		const float tx = fx - x0, tz = fz - z0;
		const float a = heights[z0 * hres + x0] * (1 - tx) + heights[z0 * hres + x0 + 1] * tx;
		const float b = heights[(z0 + 1) * hres + x0] * (1 - tx) + heights[(z0 + 1) * hres + x0 + 1] * tx;
		return a * (1 - tz) + b * tz;
	};
	// pathfinder.xml default MaxWaterDepth (2 m; 1 m = 0.25 height units, grid_nav.h).
	const float shallow = 2.0f * 0.25f;
	const bool has_water = water > -0.5f;
	const float tile = (half * 2.0f) / (float)n;
	PackedByteArray px;
	px.resize((int64_t)n * n * 4);
	uint8_t *out = px.ptrw();
	for (int j = 0; j < n; ++j) {
		for (int i = 0; i < n; ++i) {
			const float x0 = -half + i * tile, z0 = -half + j * tile;
			uint8_t *o = out + ((size_t)j * n + i) * 4;
			if (round && sim->off_world(x0 + 0.5f * tile, z0 + 0.5f * tile)) {
				o[0] = o[1] = o[2] = o[3] = 0;
				continue;
			}
			const float avg = 0.25f * (height_at(x0, z0) + height_at(x0 + tile, z0) + height_at(x0, z0 + tile) + height_at(x0 + tile, z0 + tile));
			Color c;
			if (has_water && avg < water && avg > water - shallow) c = Color(112 / 255.0f, 152 / 255.0f, 192 / 255.0f); // shallow water
			else if (has_water && avg < water) c = Color(80 / 255.0f, 120 / 255.0f, 160 / 255.0f); // a constant colour on every map
			else {
				// val = (heightmap >> 8) / 3 + 170, the heightmap at 92 units per metre.
				const float metres = std::max(0.0f, height_at(x0, z0) / 0.25f);
				const float val = std::min(255.0f, std::floor(std::floor(metres * 92.0f / 256.0f) / 3.0f) + 170.0f);
				c = tt.empty() ? Color(0.46f, 0.42f, 0.33f) : base[tt.at(i, j) < base.size() ? tt.at(i, j) : 0];
				c = Color(c.r * val / 255.0f, c.g * val / 255.0f, c.b * val / 255.0f);
			}
			o[0] = (uint8_t)(c.r * 255.0f + 0.5f);
			o[1] = (uint8_t)(c.g * 255.0f + 0.5f);
			o[2] = (uint8_t)(c.b * 255.0f + 0.5f);
			o[3] = 255;
		}
	}
	Ref<Image> img = Image::create_from_data(n, n, false, Image::FORMAT_RGBA8, px);
	if (img.is_null()) return terrain_tex_.is_valid();
	if (terrain_tex_.is_valid() && terrain_tex_->get_width() == n) terrain_tex_->set_image(img);
	else terrain_tex_ = ImageTexture::create_from_image(img);
	return true;
}

bool RtsoMiniMapView::ensure_territory_texture() {
	if (!sim) return false;
	const auto &grid = sim->territory_grid();
	if (grid.blank() || grid.w <= 0 || grid.h <= 0 || grid.data.empty()) {
		territory_tex_.unref();
		territory_tex_cache_.clear();
		return false;
	}
	const size_t n = grid.data.size();
	const uint64_t colors = rtso::display_colors::key(*sim);
	if (territory_tex_cache_.size() != n || territory_colors_ != colors ||
			std::memcmp(territory_tex_cache_.data(), grid.data.data(), n) != 0) {
		territory_colors_ = colors;
		// CTerritoryTexture::GenerateBitmap: each player's colour, opaque only in a
		// fringe inside its borders. Border cells (and unclaimed ones) take alphaMax,
		// a cheap blur spreads it inward losing alphaFalloff a tile, then the
		// alphaMax cells are cleared, leaving a gap on the border line itself.
		// 0 A.D.'s territory tiles are 8 m; RTS0's cells fall off in proportion.
		const int alpha_max = 0xC0;
		const float cell_m = sim->map_half() * 2.0f * 4.0f / (float)grid.w;
		const int falloff = std::max(1, (int)std::lround(0x20 * cell_m / 8.0f));
		const int w = grid.w, h = grid.h;
		auto owner_at = [&](int i, int j) { return grid.data[(size_t)j * w + i]; };
		PackedByteArray px;
		px.resize((int64_t)n * 4);
		uint8_t *out = px.ptrw();
		for (int j = 0; j < h; ++j) {
			for (int i = 0; i < w; ++i) {
				const uint8_t v = owner_at(i, j); // 0 = none, else team+1
				const Color c = v ? player_color((uint8_t)((v - 1) % rtso::MAX_PLAYERS)) : Color(1, 1, 1);
				uint8_t *o = out + ((size_t)j * w + i) * 4;
				o[0] = (uint8_t)(c.r * 255.0f);
				o[1] = (uint8_t)(c.g * 255.0f);
				o[2] = (uint8_t)(c.b * 255.0f);
				const bool edge = v == 0 || (i > 0 && owner_at(i - 1, j) != v) || (i < w - 1 && owner_at(i + 1, j) != v) ||
						(j > 0 && owner_at(i, j - 1) != v) || (j < h - 1 && owner_at(i, j + 1) != v);
				o[3] = edge ? alpha_max : 0;
			}
		}
		auto blur = [&](int start, int step, int count) {
			int a = 0;
			for (int k = 0; k < count; ++k) {
				uint8_t &al = out[(size_t)(start + k * step) * 4 + 3];
				a = std::max(a - falloff, (int)al);
				al = (uint8_t)a;
			}
		};
		for (int j = 0; j < h; ++j) {
			blur(j * w, 1, w);
			blur(j * w + w - 1, -1, w);
		}
		for (int i = 0; i < w; ++i) {
			blur(i, w, h);
			blur((h - 1) * w + i, -w, h);
		}
		for (size_t k = 0; k < n; ++k)
			if (out[k * 4 + 3] == alpha_max) out[k * 4 + 3] = 0;
		Ref<Image> img = Image::create_from_data(grid.w, grid.h, false, Image::FORMAT_RGBA8, px);
		if (img.is_null()) return territory_tex_.is_valid();
		if (territory_tex_.is_valid()) territory_tex_->set_image(img);
		else territory_tex_ = ImageTexture::create_from_image(img);
		territory_tex_cache_.assign(grid.data.begin(), grid.data.end());
	}
	return territory_tex_.is_valid();
}

namespace {
// Filled rectangles gathered into one triangle array: Godot 4.3's RD canvas
// renderer issues a draw call per draw_rect, and a late-game minimap's dots were
// ~2200 of a frame's ~2450 draw calls on the Mobile renderer.
// Triangles draw in the order added, so the layers stack as before.
struct QuadBatch {
	std::vector<Vector2> pts;
	std::vector<Color> cols;
	void add(const Rect2 &r, const Color &c) {
		const Vector2 a = r.position, b = r.position + r.size;
		pts.insert(pts.end(), { a, Vector2(b.x, a.y), b, Vector2(a.x, b.y) });
		cols.insert(cols.end(), { c, c, c, c });
	}
	void flush(RID item) {
		const int n = (int)pts.size();
		if (n == 0) return;
		PackedVector2Array p;
		PackedColorArray c;
		PackedInt32Array idx;
		p.resize(n);
		c.resize(n);
		idx.resize(n / 4 * 6);
		std::memcpy(p.ptrw(), pts.data(), sizeof(Vector2) * n);
		std::memcpy(c.ptrw(), cols.data(), sizeof(Color) * n);
		int32_t *ix = idx.ptrw();
		for (int q = 0; q < n / 4; ++q) {
			const int32_t v = q * 4;
			ix[q * 6 + 0] = v;
			ix[q * 6 + 1] = v + 1;
			ix[q * 6 + 2] = v + 2;
			ix[q * 6 + 3] = v;
			ix[q * 6 + 4] = v + 2;
			ix[q * 6 + 5] = v + 3;
		}
		RenderingServer::get_singleton()->canvas_item_add_triangle_array(item, idx, p, c);
		pts.clear();
		cols.clear();
	}
};
} // namespace

void RtsoMiniMapView::_draw() {
	Vector2 sz = get_size();
	// 0 A.D. draws a circular map's minimap as a disc (MiniMapTexture m_MapIsCircular): the
	// corners off the world stay empty.
	const bool round = sim && sim->circular_map();
	const Color panel(0.08f, 0.09f, 0.12f, 0.96f), rim(0.90f, 0.75f, 0.31f, 1.0f);
	if (round) draw_circle(sz * 0.5f, std::min(sz.x, sz.y) * 0.5f, panel);
	else draw_rect(Rect2(Vector2(0, 0), sz), panel, true);
	if (!sim) {
		if (!round) draw_rect(Rect2(Vector2(0, 0), sz), rim, false, 2.0f);
		return;
	}
	// CMiniMap::GetAngle and m_MapScale: the map turns with the camera so that the
	// view points up, and a square map is drawn 1.414 times smaller so its corners
	// stay inside when turned. Everything on the map is drawn through view_xf_.
	{
		float angle = 0.0f;
		if (const Viewport *vp = get_viewport()) if (const Camera3D *cam = vp->get_camera_3d()) {
			const Vector3 f = -cam->get_global_transform().basis.get_column(2);
			// The minimap's +y is the world's +z: turn the forward (f.x, f.z) to (0, -1).
			if (f.x * f.x + f.z * f.z > 1e-6f) angle = -0.5f * (float)Math_PI - std::atan2(f.z, f.x);
		}
		const float k = round ? 1.0f : 1.0f / 1.414f;
		const Vector2 c = sz * 0.5f;
		view_xf_ = Transform2D(0.0f, c) * Transform2D(angle, Vector2()) * Transform2D(Vector2(k, 0.0f), Vector2(0.0f, k), Vector2()) *
				Transform2D(0.0f, -c);
	}
	draw_set_transform_matrix(view_xf_);
	if (ensure_terrain_texture(round)) draw_texture_rect(terrain_tex_, Rect2(Vector2(0, 0), sz), false);

	float half_map = sim->map_half();
	auto map_to_mini = [&](float x, float z) -> Vector2 {
		float nx = (x + half_map) / (half_map * 2.0f);
		float nz = (z + half_map) / (half_map * 2.0f);
		return Vector2(nx * sz.x, nz * sz.y);
	};


	// Entities follow the local player's LOS in every shroud mode (the mode only
	// changes how the shroud is drawn): the sim's per-player LOS via FogMirages.
	mirages_.update(*sim, local_player);
	const uint8_t *fow = mirages_.view().empty() ? nullptr : mirages_.view().data();
	const int fow_res = sim->fow_res();

	// The shroud: CMiniMapTexture writes the LOS texture (visible 255, explored 127,
	// unexplored 0; graphics/LOSTexture.cpp) into the final texture's alpha over a
	// black background, so explored land is drawn at half brightness and the edges
	// are smooth (the texture is sampled with linear filtering). The entity points
	// (mirages included) go on top at full colour, so the shroud never masks them.
	// SHROUD_OFF (observer / replay view) skips it.
	auto draw_shroud = [&]() {
		if (!fow) return;
		const size_t cells = (size_t)fow_res * (size_t)fow_res;
		// The cells off the world are left clear: a circular map's corners, and on a square
		// map the 12 m edge frame (LosIsOffWorld).
		const float cell_world = half_map * 2.0f / (float)fow_res;
		if (shroud_cache_.size() != cells || shroud_round_ != round || std::memcmp(shroud_cache_.data(), fow, cells) != 0) {
			shroud_cache_.assign(fow, fow + cells);
			shroud_round_ = round;
			PackedByteArray px;
			px.resize((int64_t)cells * 4);
			uint8_t *out = px.ptrw();
			for (int z = 0; z < fow_res; ++z) {
				for (int x = 0; x < fow_res; ++x) {
					const uint8_t st = fow[z * fow_res + x];
					const int los = st == rtso::Simulation::FOW_VISIBLE ? 255 : st == rtso::Simulation::FOW_UNEXPLORED ? 0 : 127;
					const bool off = sim->off_world(-half_map + ((float)x + 0.5f) * cell_world, -half_map + ((float)z + 0.5f) * cell_world);
					uint8_t *o = out + ((size_t)z * fow_res + x) * 4;
					o[0] = 8; o[1] = 10; o[2] = 15;
					o[3] = off ? 0 : (uint8_t)(255 - los);
				}
			}
			Ref<Image> img = Image::create_from_data(fow_res, fow_res, false, Image::FORMAT_RGBA8, px);
			if (img.is_valid()) {
				if (shroud_tex_.is_valid() && shroud_tex_->get_width() == fow_res) shroud_tex_->set_image(img);
				else shroud_tex_ = ImageTexture::create_from_image(img);
			}
		}
		if (shroud_tex_.is_valid()) draw_texture_rect(shroud_tex_, Rect2(Vector2(0, 0), sz), false);
	};


	// 1. Territory tint raster (territory_grid_ -> ImageTexture upload):
	// drawn over terrain / under the smoothed circle outlines so the cells
	// carry the blurred influence border and the circles keep the crisp edge.
	// Data path: simulation.h:327 territory_grid() -> ensure_territory_texture()
	// (minimap_view.cpp) -> RGBA8 overlay; closes REMAINING_GAPS.md:148.
	// A circular map's minimap shader (minimap.fs CIRCULAR_MINIMAP) discards everything
	// past the map's circle, so there the layers below are drawn inside the disc only.
	PackedVector2Array disc;
	if (round) {
		const int n = 96;
		disc.resize(n);
		const Vector2 c = sz * 0.5f;
		const float r = std::min(sz.x, sz.y) * 0.5f;
		for (int i = 0; i < n; ++i) {
			const float a = 6.2831853f * (float)i / (float)n;
			disc.set(i, c + Vector2(std::cos(a), std::sin(a)) * r);
		}
	}
	if (show_territory && ensure_territory_texture()) {
		if (round) {
			PackedVector2Array uv;
			uv.resize(disc.size());
			for (int i = 0; i < disc.size(); ++i) uv.set(i, Vector2(disc[i].x / sz.x, disc[i].y / sz.y));
			draw_polygon(disc, PackedColorArray({Color(1, 1, 1, 1)}), uv, territory_tex_);
		} else {
			draw_texture_rect(territory_tex_, Rect2(Vector2(0, 0), sz), false, Color(1, 1, 1, 1));
		}
	}

	if (shroud_mode != SHROUD_OFF) draw_shroud();

	const RID canvas = get_canvas_item();
	QuadBatch quads;
	// 2. Draw Resource Nodes: what the player has seen, live or miraged
	if (show_resources) {
		for (const auto &r : mirages_.resources()) {
			Vector2 pos = map_to_mini(r.x, r.z);
			// The 0.28 templates' Minimap/Color: template_gaia_tree, _ore, _rock, _fish, _fruit.
			Color col = Color(69 / 255.0f, 107 / 255.0f, 68 / 255.0f);
			if (r.type == rtso::RES_GOLD) col = Color(227 / 255.0f, 159 / 255.0f, 12 / 255.0f);
			else if (r.type == rtso::RES_STONE) col = Color(170 / 255.0f, 170 / 255.0f, 170 / 255.0f);
			else if (r.type == rtso::RES_FOOD) col = r.subtype == rtso::RES_SUB_FISH ? Color(227 / 255.0f, 130 / 255.0f, 141 / 255.0f)
					: Color(155 / 255.0f, 204 / 255.0f, 102 / 255.0f);

			quads.add(Rect2(pos - Vector2(1.5f, 1.5f), Vector2(3.0f, 3.0f)), col);
		}
		// template_gaia_treasure Minimap: a white (250, 250, 250) dot.
		for (const auto &t : mirages_.treasures()) {
			Color col(0.98f, 0.98f, 0.98f, 0.95f);
			quads.add(Rect2(map_to_mini(t.x, t.z) - Vector2(1.5f, 1.5f), Vector2(3.0f, 3.0f)), col);
		}
	}

	// 3. Draw Buildings: own and allied live, others as last seen
	for (const auto &b : mirages_.buildings()) {
		Vector2 pos = map_to_mini(b.x, b.z);
		// 0 A.D. MiniMap: structures in their owner's colour; bigger marks for the
		// civic centre and barracks so bases read at a glance.
		Color col = player_color(b.team);
		float box_sz = 6.0f;

		if (rtso::is_civic_centre(b.type)) {
			box_sz = 9.0f;
		} else if (b.type == rtso::BLD_BARRACKS) {
			box_sz = 7.0f;
		} else if (rtso::is_defense_tower(b.type)) {
			box_sz = 5.0f;
		}

		if (!entity_pings_.empty() && entity_ping_lit(b.id, Time::get_singleton()->get_ticks_msec()))
			col = Color(1, 1, 1); // MiniMapTexture: a pinging entity's vertex is white
		quads.add(Rect2(pos - Vector2(box_sz * 0.5f, box_sz * 0.5f), Vector2(box_sz, box_sz)), col);
	}

	// 3b. Animals: template_unit_fauna.xml Minimap type "food", colour (227, 130, 141), and
	// RetainInFog, so they show wherever the viewer has explored.
	{
		const bool sees_all = rtso::los_sees_all(*sim, local_player);
		const Color fauna_col(227.0f / 255.0f, 130.0f / 255.0f, 141.0f / 255.0f, 1.0f);
		auto fv = sim->fauna();
		for (size_t f = 0; f < fv.count; ++f) {
			if (!fv.alive[f] || fv.behavior[f] == rtso::FAUNA_CORRALLED) continue;
			if (!sees_all && mirages_.los_at(fv.pos_x[f], fv.pos_z[f]) == rtso::Simulation::FOW_UNEXPLORED) continue;
			quads.add(Rect2(map_to_mini(fv.pos_x[f], fv.pos_z[f]) - Vector2(1.0f, 1.0f), Vector2(2.0f, 2.0f)), fauna_col);
		}
	}

	// 4. Draw Units: own and allied always, others only in direct LOS
	auto units = sim->units();
	for (size_t i = 0; i < units.count; ++i) {
		if (!units.alive[i]) continue;
		if (!rtso::los_sees_unit(*sim, local_player, units.pos_x[i], units.pos_z[i], units.team[i])) {
			continue; // Enemy units invisible outside direct LOS
		}

		Vector2 pos = map_to_mini(units.pos_x[i], units.pos_z[i]);
		Color col = player_color(units.team[i]);
		if (!entity_pings_.empty() && entity_ping_lit(units.id[i], Time::get_singleton()->get_ticks_msec())) col = Color(1, 1, 1);
		float rad = (units.unit_type[i] == rtso::UNIT_CAVALRY || units.unit_type[i] == rtso::UNIT_TRADER) ? 3.0f : 2.0f;
		quads.add(Rect2(pos.x - rad * 0.5f, pos.y - rad * 0.5f, rad, rad), col);
	}
	quads.flush(canvas);


	// 5b. Minimap icons (gui.session.minimap.icons.enabled; <Minimap><Icon size="16">):
	// civic centres, wonders, shrines and trading posts, stone and metal mines and
	// catafalques, at the entity as the viewer knows it.
	if (rtso::user_config::get_bool("gui.session.minimap.icons.enabled")) {
		const float icon = 16.0f * (float)rtso::user_config::get_number("gui.session.minimap.icons.sizescale");
		const float opacity = (float)rtso::user_config::get_number("gui.session.minimap.icons.opacity");
		auto draw_icon = [&](int which, float x, float z) {
			if (icon_tex_[which].is_null()) {
				static const char *kIcons[ICON_COUNT] = {"civil_centre", "wonder", "other", "stone", "metal", "relic"};
				icon_tex_[which] = ResourceLoader::get_singleton()->load(
						String("res://assets/textures/ui/archive/icons/minimap/") + kIcons[which] + ".png");
				if (icon_tex_[which].is_null()) return;
			}
			// Upright at the turned position, as CMiniMap draws its icons.
			draw_set_transform_matrix(Transform2D());
			draw_texture_rect(icon_tex_[which], Rect2(view_xf_.xform(map_to_mini(x, z)) - Vector2(icon, icon) * 0.5f, Vector2(icon, icon)),
					false, Color(1, 1, 1, opacity));
			draw_set_transform_matrix(view_xf_);
		};
		if (show_resources)
			for (const auto &r : mirages_.resources())
				if (r.type == rtso::RES_STONE || r.type == rtso::RES_GOLD)
					draw_icon(r.type == rtso::RES_STONE ? ICON_STONE : ICON_METAL, r.x, r.z);
		for (const auto &b : mirages_.buildings()) {
			if (rtso::is_civic_centre(b.type)) draw_icon(ICON_CIVIL_CENTRE, b.x, b.z);
			else if (b.type == rtso::BLD_WONDER) draw_icon(ICON_WONDER, b.x, b.z);
			else if (b.type == rtso::BLD_SHRINE || b.type == rtso::BLD_TRADING_POST || b.type == rtso::BLD_MILL) draw_icon(ICON_OTHER, b.x, b.z);
		}
		for (size_t i = 0; i < units.count; ++i)
			if (units.alive[i] && units.unit_type[i] == rtso::UNIT_CATAFALQUE &&
					rtso::los_sees_unit(*sim, local_player, units.pos_x[i], units.pos_z[i], units.team[i]))
				draw_icon(ICON_RELIC, units.pos_x[i], units.pos_z[i]);
	}

	// 6. Draw Tactical Map Pings
	// gui/session/minimap/MiniMap.xml flare_*: 6 s, fading in over 0.25 s and out over
	// 0.5 s (smoothstep), 16 frames at 10.67 a second of an expanding ring 32 px in
	// radius, a second ring from half way (flare_interleave). 0 A.D. draws the frames
	// from art/textures/animated/minimap-flare, which RTS0 does not have: a drawn ring
	// grows the same way.
	const uint64_t now_ms = Time::get_singleton()->get_ticks_msec();
	auto smooth = [](float x) { x = Math::clamp(x, 0.0f, 1.0f); return x * x * (3.0f - 2.0f * x); };
	for (const auto &p : pings) {
		const float t = (float)(now_ms - p.start_ms) / 1000.0f;
		const float alpha = std::min(smooth(t / 0.25f), smooth((kFlareLifetimeMs / 1000.0f - t) / 0.5f));
		const int step = (int)std::floor(t * 10.67f);
		auto ring = [&](int frame) {
			const float k = (float)(frame % 16 + 1) / 16.0f;
			draw_arc(p.mini_pos, 32.0f * k, 0.0f, 6.2831853f, 24, Color(p.color.r, p.color.g, p.color.b, alpha * (1.0f - 0.6f * k)), 2.0f);
		};
		ring(step);
		if (step >= 8) ring(step - 8);
	}

	// 7. Draw Camera Viewport Frustum Box
	// CMiniMap::DrawViewRect: the screen's corners on the plane at the water height,
	// the edges cropped to the map, in red 2 px wide.
	if (const Viewport *vp = get_viewport()) if (const Camera3D *cam = vp->get_camera_3d()) {
		const Vector2 vs = cam->get_viewport()->get_visible_rect().size;
		const float plane_y = std::max(sim->water_height(), 0.0f);
		const Vector2 corners[4] = {Vector2(0, vs.y), Vector2(vs.x, vs.y), Vector2(vs.x, 0), Vector2(0, 0)};
		Vector2 hit[4];
		for (int k = 0; k < 4; ++k) {
			const Vector3 o = cam->project_ray_origin(corners[k]), d = cam->project_ray_normal(corners[k]);
			// A ray at or above the horizon reaches the far plane instead.
			const float t = d.y < -1e-4f ? (plane_y - o.y) / d.y : (float)cam->get_far();
			const Vector3 w = o + d * std::min(t, (float)cam->get_far());
			hit[k] = Vector2(w.x, w.z);
		}
		for (int k = 0; k < 4; ++k) {
			Vector2 a = hit[k], b = hit[(k + 1) % 4];
			if (!crop_segment(a, b, half_map, round)) continue;
			draw_line(map_to_mini(a.x, a.y), map_to_mini(b.x, b.y), Color(1.0f, 0.3f, 0.3f, 1.0f), 2.0f);
		}
	}

	// The disc's rim, over everything like the square's frame.
	draw_set_transform_matrix(Transform2D());
	if (round) draw_arc(sz * 0.5f, std::min(sz.x, sz.y) * 0.5f - 1.0f, 0.0f, 6.2831853f, 96, rim, 2.0f);
	else draw_rect(Rect2(Vector2(0, 0), sz), rim, false, 2.0f);
}
