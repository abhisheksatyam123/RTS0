#include "minimap_view.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/rect2.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <cstring>

using namespace godot;

void RtsoMiniMapView::_bind_methods() {
	ADD_SIGNAL(MethodInfo("minimap_clicked", PropertyInfo(Variant::VECTOR2, "world_pos")));
	ADD_SIGNAL(MethodInfo("flare_requested", PropertyInfo(Variant::VECTOR2, "world_pos")));
	ClassDB::bind_method(D_METHOD("add_ping", "world_pos", "color"), &RtsoMiniMapView::add_ping, DEFVAL(Color(1.0f, 0.85f, 0.2f)));
	ClassDB::bind_method(D_METHOD("toggle_territory"), &RtsoMiniMapView::toggle_territory);
	ClassDB::bind_method(D_METHOD("toggle_resources"), &RtsoMiniMapView::toggle_resources);
	ClassDB::bind_method(D_METHOD("toggle_fow"), &RtsoMiniMapView::toggle_fow);
	ClassDB::bind_method(D_METHOD("cycle_shroud"), &RtsoMiniMapView::cycle_shroud);
	ClassDB::bind_method(D_METHOD("toggle_diplomacy"), &RtsoMiniMapView::toggle_diplomacy);
}

RtsoMiniMapView::RtsoMiniMapView() {
	set_anchor(SIDE_LEFT, 0.0f);
	set_anchor(SIDE_RIGHT, 0.0f);
	set_anchor(SIDE_TOP, 1.0f);
	set_anchor(SIDE_BOTTOM, 1.0f);
	set_offset(SIDE_LEFT, 14.0f);
	set_offset(SIDE_RIGHT, 204.0f);
	set_offset(SIDE_TOP, -204.0f);
	set_offset(SIDE_BOTTOM, -14.0f);
	set_custom_minimum_size(Vector2(190, 190));
}

RtsoMiniMapView::~RtsoMiniMapView() {}

void RtsoMiniMapView::add_ping(Vector2 world_pos, Color col) {
	Vector2 sz = get_size();
	float half_map = sim ? sim->map_half() : 64.0f;
	float nx = (world_pos.x + half_map) / (half_map * 2.0f);
	float nz = (world_pos.y + half_map) / (half_map * 2.0f);
	Vector2 mpos(nx * sz.x, nz * sz.y);

	MapPing p;
	p.mini_pos = mpos;
	p.timer = 3.0f;
	p.color = col;
	pings.push_back(p);
	queue_redraw();
}

void RtsoMiniMapView::sync(const rtso::Simulation *s, Vector3 p_cam_pos) {
	sim = s;
	cam_pos = p_cam_pos;

	// Decay pings
	for (auto &p : pings) {
		p.timer -= 0.05f;
	}
	pings.erase(std::remove_if(pings.begin(), pings.end(),
		[](const MapPing &p) { return p.timer <= 0.0f; }), pings.end());

	queue_redraw();
}

void RtsoMiniMapView::_gui_input(const Ref<InputEvent> &event) {
	Ref<InputEventMouseButton> mb = event;
	if (mb.is_valid()) {
		Vector2 sz = get_size();
		Vector2 mpos = mb->get_position();
		float half = sim ? sim->map_half() : 64.0f;
		float wx = (mpos.x / sz.x) * (half * 2.0f) - half;
		float wz = (mpos.y / sz.y) * (half * 2.0f) - half;

		if (mb->get_button_index() == MOUSE_BUTTON_LEFT) {
			dragging = mb->is_pressed();
			emit_signal("minimap_clicked", Vector2(wx, wz));
			accept_event();
		} else if (mb->get_button_index() == MOUSE_BUTTON_RIGHT && mb->is_pressed()) {
			// Right-click on minimap adds tactical ping + broadcasts a flare
			// to session peers (main_node handles flare_requested -> NMT_CHAT).
			add_ping(Vector2(wx, wz), Color(1.0f, 0.4f, 0.2f));
			emit_signal("flare_requested", Vector2(wx, wz));
			accept_event();
		}
	}

	Ref<InputEventMouseMotion> mm = event;
	if (mm.is_valid() && dragging) {
		Vector2 sz = get_size();
		Vector2 mpos = mm->get_position();
		float half = sim ? sim->map_half() : 64.0f;
		float wx = (mpos.x / sz.x) * (half * 2.0f) - half;
		float wz = (mpos.y / sz.y) * (half * 2.0f) - half;
		emit_signal("minimap_clicked", Vector2(wx, wz));
		accept_event();
	}
}

// Territory tint upload — closes docs/REMAINING_GAPS.md:148 ("upload
// territory_grid_ as texture, minimap overlay matches sim"). Rasterises
// sim->territory_grid() (simulation.h:327; 64x64 cells written by
// simulation.cpp:728-756 rebuild_territory -> blur_territory, cell = team+1,
// 0 = unclaimed, same mapping as territory_owner_at simulation.cpp:762-769)
// into a single RGBA8 ImageTexture. The grid bytes are memcmp'd once per
// _draw() and the texture only re-uploaded when ownership actually changes.
// The player's Game Setup colour (0 A.D. PlayerColor, rtso::PLAYER_COLORS); Gaia is white.
Color RtsoMiniMapView::player_color(uint8_t pid) const {
	const uint32_t rgb = sim ? sim->player_color(pid) : 0xFFFFFFu;
	return Color(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, 1.0f);
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
	if (territory_tex_cache_.size() != n ||
			std::memcmp(territory_tex_cache_.data(), grid.data.data(), n) != 0) {
		// 0 A.D. TerritoryTexture: each player's land in its Game Setup colour;
		// alpha carries the blurred edge.
		Color tint[rtso::MAX_PLAYERS];
		for (uint8_t p = 0; p < rtso::MAX_PLAYERS; ++p) {
			tint[p] = player_color(p);
			tint[p].a = 0.32f;
		}
		PackedByteArray px;
		px.resize((int64_t)n * 4);
		uint8_t *out = px.ptrw();
		for (size_t i = 0; i < n; ++i) {
			const uint8_t owner = grid.data[i]; // 0 = none, else team+1
			if (owner == 0) {
				out[i * 4 + 0] = 0; out[i * 4 + 1] = 0; out[i * 4 + 2] = 0; out[i * 4 + 3] = 0;
				continue;
			}
			const Color &c = tint[(owner - 1) % rtso::MAX_PLAYERS];
			out[i * 4 + 0] = (uint8_t)(c.r * 255.0f + 0.5f);
			out[i * 4 + 1] = (uint8_t)(c.g * 255.0f + 0.5f);
			out[i * 4 + 2] = (uint8_t)(c.b * 255.0f + 0.5f);
			out[i * 4 + 3] = (uint8_t)(c.a * 255.0f + 0.5f);
		}
		Ref<Image> img = Image::create_from_data(grid.w, grid.h, false, Image::FORMAT_RGBA8, px);
		if (img.is_null()) return territory_tex_.is_valid();
		if (territory_tex_.is_valid()) territory_tex_->set_image(img);
		else territory_tex_ = ImageTexture::create_from_image(img);
		territory_tex_cache_.assign(grid.data.begin(), grid.data.end());
	}
	return territory_tex_.is_valid();
}

void RtsoMiniMapView::_draw() {
	Vector2 sz = get_size();
	// Background Panel
	draw_rect(Rect2(Vector2(0, 0), sz), Color(0.08f, 0.09f, 0.12f, 0.96f), true);
	draw_rect(Rect2(Vector2(0, 0), sz), Color(0.90f, 0.75f, 0.31f, 1.0f), false, 2.0f);

	if (!sim) return;

	float half_map = sim->map_half();
	auto map_to_mini = [&](float x, float z) -> Vector2 {
		float nx = (x + half_map) / (half_map * 2.0f);
		float nz = (z + half_map) / (half_map * 2.0f);
		return Vector2(nx * sz.x, nz * sz.y);
	};

	// Diplomacy colours (0 A.D. session "diplomacy colors" toggle, default.cfg
	// gui.session.diplomacycolors: self 21 55 149, ally 86 180 31, neutral
	// 231 200 5, enemy 150 20 20) replace player colours while enabled.
	auto c8 = [](int r, int g, int b) { return Color(r / 255.0f, g / 255.0f, b / 255.0f); };
	auto diplo_color = [&](uint8_t team, const Color &fallback) -> Color {
		if (!show_diplo_palette || !sim || team >= rtso::MAX_PLAYERS) return fallback;
		if (team == local_player) return c8(21, 55, 149);
		// Every player keeps its own stance row, so read the local player's.
		uint8_t st = sim->get_diplomacy(local_player, team);
		if (st == rtso::Simulation::DIPLO_ALLY) return c8(86, 180, 31);
		if (st == rtso::Simulation::DIPLO_NEUTRAL) return c8(231, 200, 5);
		return c8(150, 20, 20);
	};

	// Entities follow the local player's LOS in every shroud mode (the mode only
	// changes how the shroud is drawn): the sim's per-player LOS via FogMirages.
	mirages_.update(*sim, local_player);
	const uint8_t *fow = mirages_.view().empty() ? nullptr : mirages_.view().data();
	const int fow_res = sim->fow_res();

	// 3-Tier Fog of War shroud (Unexplored black, Explored fog 40%, Visible 100%)
	// matching 0 A.D.'s LOS bitmap (visible=255 / explored=127 / unexplored=0,
	// graphics/LOSTexture.cpp:443-449 composited by MiniMapTexture.cpp:523).
	// Draw order encodes the shroud state (see ShroudMode in minimap_view.h):
	//   SHROUD_ON      -> drawn AFTER entities, masking them (original behaviour)
	//   SHROUD_TERRAIN -> drawn BEFORE entities: shroud hits the terrain layer only
	//   SHROUD_OFF     -> not drawn at all (and no entity masking)
	auto draw_shroud = [&]() {
		if (!fow) return;
		float cell_w = sz.x / (float)fow_res;
		float cell_h = sz.y / (float)fow_res;

		const Color black_shroud(0.03f, 0.04f, 0.06f, 1.0f);  // 100% black unexplored
		const Color fog_shroud(0.01f, 0.02f, 0.04f, 0.40f);    // 40% explored fog

		for (int z = 0; z < fow_res; ++z) {
			int x = 0;
			while (x < fow_res) {
				uint8_t st = fow[z * fow_res + x];
				if (st == rtso::Simulation::FOW_VISIBLE) {
					x++;
					continue;
				}
				int run_start = x;
				while (x < fow_res && fow[z * fow_res + x] == st) {
					x++;
				}
				int run_len = x - run_start;
				Color col = (st == rtso::Simulation::FOW_UNEXPLORED) ? black_shroud : fog_shroud;
				draw_rect(Rect2(run_start * cell_w, z * cell_h, run_len * cell_w + 0.5f, cell_h + 0.5f), col, true);
			}
		}
	};

	// Terrain-only shroud pass (entities are drawn on top, unaffected).
	if (shroud_mode == SHROUD_TERRAIN) draw_shroud();


	// 1. Territory tint raster (territory_grid_ -> ImageTexture upload):
	// drawn over terrain / under the smoothed circle outlines so the cells
	// carry the blurred influence border and the circles keep the crisp edge.
	// Data path: simulation.h:327 territory_grid() -> ensure_territory_texture()
	// (minimap_view.cpp) -> RGBA8 overlay; closes REMAINING_GAPS.md:148.
	if (show_territory && ensure_territory_texture()) {
		draw_texture_rect(territory_tex_, Rect2(Vector2(0, 0), sz), false, Color(1, 1, 1, 1));
	}

	// 1b. Smoothed Territory Influence Outlines (0 A.D. Territory System).
	// Circle-overlay -> territory_grid_ cell mapping (parity note): each circle
	// below stamps the SAME TC(32m)/fortress(28m)/tower(18m) radii that
	// simulation.cpp:735-752 rasterises into influence_grid_ before the 3x3
	// blur_territory (grid_nav.h:501-530) drawn 1:1 by the texture above —
	// circles = hard-radius approximation, texture = blurred sim ground truth.
	if (show_territory) {
		for (const auto &b : mirages_.buildings()) {
			if (b.build_progress < 1.0f) continue;
			Vector2 pos = map_to_mini(b.x, b.z);
			float radius_m = 0.0f;
			if (rtso::is_civic_centre(b.type)) radius_m = 32.0f;
			else if (b.type == rtso::BLD_FORTRESS) radius_m = 28.0f;
			else if (rtso::is_defense_tower(b.type)) radius_m = 18.0f;

			if (radius_m > 0.0f) {
				float r_px = (radius_m / (half_map * 2.0f)) * sz.x;
				const Color pc = player_color(b.team);
				const Color territory_fill(pc.r, pc.g, pc.b, 0.14f);
				const Color territory_edge(pc.r, pc.g, pc.b, 0.60f);
				draw_circle(pos, r_px, territory_fill);
				draw_arc(pos, r_px, 0.0f, 6.2831853f, 48, territory_edge, 1.5f);
			}
		}
	}

	// 2. Draw Resource Nodes: what the player has seen (mirages dimmed)
	if (show_resources) {
		for (const auto &r : mirages_.resources()) {
			Vector2 pos = map_to_mini(r.x, r.z);
			Color col = Color(0.2f, 0.8f, 0.25f, 0.85f); // Wood
			if (r.type == rtso::RES_GOLD) col = Color(1.0f, 0.85f, 0.15f, 0.95f); // Gold
			else if (r.type == rtso::RES_STONE) col = Color(0.70f, 0.70f, 0.78f, 0.85f); // Stone
			else if (r.type == rtso::RES_FOOD) col = Color(0.95f, 0.45f, 0.2f, 0.85f); // Food

			if (mirages_.los_at(r.x, r.z) != rtso::Simulation::FOW_VISIBLE) {
				col.a *= 0.55f;
			}
			draw_rect(Rect2(pos - Vector2(1.5f, 1.5f), Vector2(3.0f, 3.0f)), col, true);
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

		col = diplo_color(b.team, col);
		if (b.miraged) {
			col.a *= 0.55f;
		}
		draw_rect(Rect2(pos - Vector2(box_sz * 0.5f, box_sz * 0.5f), Vector2(box_sz, box_sz)), col, true);
	}

	// 4. Draw Units: own and allied always, others only in direct LOS
	auto units = sim->units();
	for (size_t i = 0; i < units.count; ++i) {
		if (!units.alive[i]) continue;
		if (!rtso::los_sees_unit(*sim, local_player, units.pos_x[i], units.pos_z[i], units.team[i])) {
			continue; // Enemy units invisible outside direct LOS
		}

		Vector2 pos = map_to_mini(units.pos_x[i], units.pos_z[i]);
		Color col = diplo_color(units.team[i], player_color(units.team[i]));
		float rad = (units.unit_type[i] == rtso::UNIT_CAVALRY || units.unit_type[i] == rtso::UNIT_TRADER) ? 3.0f : 2.0f;
		draw_rect(Rect2(pos.x - rad * 0.5f, pos.y - rad * 0.5f, rad, rad), col, true);
	}

	// 5. Normal shroud pass — drawn over the entities so they are masked too
	// (skipped in terrain-only mode, which drew it as a terrain layer above).
	if (shroud_mode == SHROUD_ON) draw_shroud();

	// 6. Draw Tactical Map Pings
	for (const auto &p : pings) {
		float alpha = p.timer / 3.0f;
		float pulse_r = (1.0f - alpha) * 16.0f + 4.0f;
		Color c = p.color;
		c.a = alpha;
		draw_circle(p.mini_pos, pulse_r, Color(c.r, c.g, c.b, alpha * 0.3f));
		draw_arc(p.mini_pos, pulse_r, 0.0f, 6.2831853f, 16, c, 2.0f);
	}

	// 7. Draw Camera Viewport Frustum Box
	Vector2 cam_mini = map_to_mini(cam_pos.x, cam_pos.z);
	draw_rect(Rect2(cam_mini - Vector2(18, 14), Vector2(36, 28)), Color(1.0f, 1.0f, 1.0f, 0.6f), false, 1.5f);
}
