#ifndef RTSO_MINIMAP_VIEW_H
#define RTSO_MINIMAP_VIEW_H

#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include "../simulation/simulation.h"
#include "fog_mirage.h"

using namespace godot;

class RtsoMiniMapView : public Control {
	GDCLASS(RtsoMiniMapView, Control);

	struct MapPing {
		Vector2 mini_pos;
		uint64_t start_ms; // real time, as CMiniMap's flares
		Color color;
	};

	const rtso::Simulation *sim = nullptr;
	Vector3 cam_pos;
	bool dragging = false;
	bool show_territory = true;
	bool show_resources = true;
	uint8_t local_player = 0; // player whose units read as "own" and whose fog is drawn

	// 3-state shroud view. 0 A.D. rasterises a 3-level LOS bitmap
	// (visible=255 / explored=127 / unexplored=0, graphics/LOSTexture.cpp:443-449)
	// and composites it over minimap terrain (graphics/MiniMapTexture.cpp:523);
	// RTS0 mirrors those states in _draw(). The toggle rides the minimap panel
	// button row, parity with 0 A.D. gui/session/minimap/MiniMapPanel.js:8-11
	// (diplomacy/idle/flare buttons) and the enabled/disabled sprite swap of
	// gui/session/minimap/MiniMapDiplomacyColorsButton.js:24-33.
	enum ShroudMode {
		SHROUD_ON = 0, // normal: 3-tier shroud over terrain and territories, entities on top (CMiniMapTexture)
		SHROUD_TERRAIN = 1, // the same drawing as SHROUD_ON since entities always go over the shroud
		SHROUD_OFF = 2, // shroud off (observer / replay view)
	};
	int shroud_mode = SHROUD_ON;
	// What the local player knows (0 A.D. Fogging.js): the minimap draws enemy
	// structures and resources from this, never from live sim state, whatever
	// the shroud mode (MiniMapTexture draws only entities the player can see).
	rtso::FogMirages mirages_;
	std::vector<MapPing> pings;
	// MT_MinimapPing: entity id -> when its ping ends (ms, real time).
	std::unordered_map<uint32_t, uint64_t> entity_pings_;

	// Territory tint overlay — docs/REMAINING_GAPS.md:148 "upload territory_grid_
	// as texture, minimap overlay matches sim". RGBA8 ImageTexture rasterised from
	// sim->territory_grid() (64x64 NavGrid res, cell = team+1, 0 = unclaimed;
	// simulation.h:327 accessor / simulation.cpp:762-769 territory_owner_at) and
	// rebuilt only when the grid bytes change. Drawn under the smoothed circle
	// outlines in _draw(); see ensure_territory_texture() in minimap_view.cpp.
	Ref<ImageTexture> territory_tex_;
	enum MinimapIcon { ICON_CIVIL_CENTRE, ICON_WONDER, ICON_OTHER, ICON_STONE, ICON_METAL, ICON_RELIC, ICON_COUNT };
	Ref<Texture2D> icon_tex_[ICON_COUNT];
	std::vector<uint8_t> territory_tex_cache_;
	uint64_t territory_colors_ = 0; // display_colors::key the texture was drawn with
	bool ensure_territory_texture();
	// 0 A.D. CMiniMapTexture::RebuildTerrainTexture: each tile's terrain base colour
	// shaded by height, water in two fixed colours. Rebuilt when the terrain changes.
	Ref<ImageTexture> terrain_tex_;
	uint64_t terrain_key_ = 0;
	uint64_t terrain_checked_ms_ = 0;
	bool ensure_terrain_texture(bool round);
	// The LOS layer, re-uploaded when the player's fog changes.
	Ref<ImageTexture> shroud_tex_;
	std::vector<uint8_t> shroud_cache_;
	bool shroud_round_ = false;
	Color player_color(uint8_t pid) const;

protected:
	static void _bind_methods();

public:
	RtsoMiniMapView();
	~RtsoMiniMapView();

	void _draw() override;
	void _gui_input(const Ref<InputEvent> &event) override;
	void sync(const rtso::Simulation *s, Vector3 p_cam_pos);
	// CCmpMinimap MT_MinimapPing: the entity's dot blinks white for
	// gui.session.minimap.pingduration (50 s), blinkduration 1.7 s a cycle.
	void ping_entity(uint32_t id);
	bool entity_ping_lit(uint32_t id, uint64_t now_ms) const;
	size_t entity_ping_count() const { return entity_pings_.size(); }
	void add_ping(Vector2 world_pos, Color col = Color(1.0f, 0.85f, 0.2f));
	void toggle_territory() { show_territory = !show_territory; queue_redraw(); }
	void set_local_player(uint8_t p) { if (p != local_player) { local_player = p; queue_redraw(); } }
	void toggle_resources() { show_resources = !show_resources; queue_redraw(); }
	// Binary legacy API kept bound (docs/UI_PARITY.md still names toggle_fow):
	// maps onto the 3-state cycle as OFF <-> ON.
	void toggle_fow() {
		shroud_mode = (shroud_mode == SHROUD_OFF) ? SHROUD_ON : SHROUD_OFF;
		queue_redraw();
	}
	// Normal -> terrain-only -> off -> normal (hotkey F in main_node.cpp).
	void cycle_shroud() {
		shroud_mode = (shroud_mode + 1) % 3;
		queue_redraw();
	}
	const char *shroud_name() const {
		switch (shroud_mode) {
			case SHROUD_TERRAIN: return "Terr";
			case SHROUD_OFF: return "Off";
			default: return "On";
		}
	}
	// 0 A.D. MiniMapDiplomacyColorsButton: diplomacy colours everywhere (display_colors.h).
	void toggle_diplomacy();
};

#endif
