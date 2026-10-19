#include "minimap_view.h"
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
#include <godot_cpp/classes/resource_loader.hpp>
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
	for (auto &p : pings) {
		p.timer -= 0.05f;
	}
	pings.erase(std::remove_if(pings.begin(), pings.end(),
		[](const MapPing &p) { return p.timer <= 0.0f; }), pings.end());
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
		Vector2 mpos = mb->get_position();
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
	// 0 A.D. draws a circular map's minimap as a disc (MiniMapTexture m_MapIsCircular): the
	// corners off the world stay empty.
	const bool round = sim && sim->circular_map();
	const Color panel(0.08f, 0.09f, 0.12f, 0.96f), rim(0.90f, 0.75f, 0.31f, 1.0f);
	if (round) {
		draw_circle(sz * 0.5f, std::min(sz.x, sz.y) * 0.5f, panel);
	} else {
		draw_rect(Rect2(Vector2(0, 0), sz), panel, true);
		draw_rect(Rect2(Vector2(0, 0), sz), rim, false, 2.0f);
	}

	if (!sim) return;

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

	// 3-Tier Fog of War shroud (Unexplored black, Explored fog 40%, Visible 100%)
	// matching 0 A.D.'s LOS bitmap (visible=255 / explored=127 / unexplored=0,
	// graphics/LOSTexture.cpp:443-449 composited by MiniMapTexture.cpp:523).
	// CMiniMapTexture draws terrain, territories, then the LOS layer, and the entity
	// points (mirages included) last at full colour, so the shroud never masks them.
	// SHROUD_OFF (observer / replay view) skips it.
	auto draw_shroud = [&]() {
		if (!fow) return;
		float cell_w = sz.x / (float)fow_res;
		float cell_h = sz.y / (float)fow_res;

		const Color black_shroud(0.03f, 0.04f, 0.06f, 1.0f);  // 100% black unexplored
		const Color fog_shroud(0.01f, 0.02f, 0.04f, 0.40f);    // 40% explored fog

		// On a circular map the cells off the world are left undrawn, so the shroud is round too;
		// on a square map the 12 m edge frame is off the world as well (LosIsOffWorld).
		const float cell_world = half_map * 2.0f / (float)fow_res;
		auto off_world = [&](int x, int z) {
			return sim->off_world(-half_map + ((float)x + 0.5f) * cell_world, -half_map + ((float)z + 0.5f) * cell_world);
		};
		for (int z = 0; z < fow_res; ++z) {
			int x = 0;
			while (x < fow_res) {
				uint8_t st = fow[z * fow_res + x];
				if (st == rtso::Simulation::FOW_VISIBLE || off_world(x, z)) {
					x++;
					continue;
				}
				int run_start = x;
				while (x < fow_res && fow[z * fow_res + x] == st && !off_world(x, z)) {
					x++;
				}
				int run_len = x - run_start;
				Color col = (st == rtso::Simulation::FOW_UNEXPLORED) ? black_shroud : fog_shroud;
				draw_rect(Rect2(run_start * cell_w, z * cell_h, run_len * cell_w + 0.5f, cell_h + 0.5f), col, true);
			}
		}
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
	auto in_disc = [&](Vector2 p) {
		const Vector2 d = p - sz * 0.5f;
		const float r = std::min(sz.x, sz.y) * 0.5f;
		return !round || d.length_squared() <= r * r;
	};
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

	// 1b. Smoothed Territory Influence Outlines (0 A.D. Territory System).
	// A circle at each territory root's TerritoryInfluence radius (territory_data.h, before
	// techs): an outline over the texture above, which draws the sim's claimed tiles.
	if (show_territory) {
		for (const auto &b : mirages_.buildings()) {
			if (b.build_progress < 1.0f) continue;
			Vector2 pos = map_to_mini(b.x, b.z);
			const float radius_m = rtso::is_territory_root(b.type) ? rtso::territory_influence_info(b.type).radius : 0.0f;

			if (radius_m > 0.0f) {
				float r_px = (radius_m / (half_map * 2.0f)) * sz.x;
				const Color pc = player_color(b.team);
				const Color territory_fill(pc.r, pc.g, pc.b, 0.14f);
				const Color territory_edge(pc.r, pc.g, pc.b, 0.60f);
				if (!round) {
					draw_circle(pos, r_px, territory_fill);
					draw_arc(pos, r_px, 0.0f, 6.2831853f, 48, territory_edge, 1.5f);
					continue;
				}
				PackedVector2Array ring;
				ring.resize(48);
				for (int i = 0; i < 48; ++i) {
					const float a = 6.2831853f * (float)i / 48.0f;
					ring.set(i, pos + Vector2(std::cos(a), std::sin(a)) * r_px);
				}
				TypedArray<PackedVector2Array> inside = Geometry2D::get_singleton()->intersect_polygons(ring, disc);
				for (int k = 0; k < inside.size(); ++k) draw_colored_polygon(inside[k], territory_fill);
				// The outline, in runs that stay inside the disc.
				PackedVector2Array run;
				for (int i = 0; i <= 48; ++i) {
					const Vector2 q = ring[i % 48];
					if (in_disc(q)) {
						run.push_back(q);
						continue;
					}
					if (run.size() >= 2) draw_polyline(run, territory_edge, 1.5f);
					run.clear();
				}
				if (run.size() >= 2) draw_polyline(run, territory_edge, 1.5f);
			}
		}
	}

	if (shroud_mode != SHROUD_OFF) draw_shroud();

	// 2. Draw Resource Nodes: what the player has seen, live or miraged
	if (show_resources) {
		for (const auto &r : mirages_.resources()) {
			Vector2 pos = map_to_mini(r.x, r.z);
			Color col = Color(0.2f, 0.8f, 0.25f, 0.85f); // Wood
			if (r.type == rtso::RES_GOLD) col = Color(1.0f, 0.85f, 0.15f, 0.95f); // Gold
			else if (r.type == rtso::RES_STONE) col = Color(0.70f, 0.70f, 0.78f, 0.85f); // Stone
			else if (r.type == rtso::RES_FOOD) col = Color(0.95f, 0.45f, 0.2f, 0.85f); // Food

			draw_rect(Rect2(pos - Vector2(1.5f, 1.5f), Vector2(3.0f, 3.0f)), col, true);
		}
		// template_gaia_treasure Minimap: a white (250, 250, 250) dot.
		for (const auto &t : mirages_.treasures()) {
			Color col(0.98f, 0.98f, 0.98f, 0.95f);
			draw_rect(Rect2(map_to_mini(t.x, t.z) - Vector2(1.5f, 1.5f), Vector2(3.0f, 3.0f)), col, true);
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
		draw_rect(Rect2(pos - Vector2(box_sz * 0.5f, box_sz * 0.5f), Vector2(box_sz, box_sz)), col, true);
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
			draw_rect(Rect2(map_to_mini(fv.pos_x[f], fv.pos_z[f]) - Vector2(1.0f, 1.0f), Vector2(2.0f, 2.0f)), fauna_col, true);
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
		draw_rect(Rect2(pos.x - rad * 0.5f, pos.y - rad * 0.5f, rad, rad), col, true);
	}


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
			draw_texture_rect(icon_tex_[which], Rect2(map_to_mini(x, z) - Vector2(icon, icon) * 0.5f, Vector2(icon, icon)),
					false, Color(1, 1, 1, opacity));
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

	// The disc's rim, over everything like the square's frame.
	if (round) draw_arc(sz * 0.5f, std::min(sz.x, sz.y) * 0.5f - 1.0f, 0.0f, 6.2831853f, 96, rim, 2.0f);
}
