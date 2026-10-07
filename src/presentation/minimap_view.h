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
#include <cstdint>
#include <vector>
#include "../simulation/simulation.h"

using namespace godot;

class RtsoMiniMapView : public Control {
	GDCLASS(RtsoMiniMapView, Control);

	struct MapPing {
		Vector2 mini_pos;
		float timer;
		Color color;
	};

	const rtso::Simulation *sim = nullptr;
	Vector3 cam_pos;
	bool dragging = false;
	bool show_territory = true;
	bool show_resources = true;
	bool show_diplo_palette = false; // ally/enemy palette per DiplomacyDialog stances (UI_PARITY Minimap bullet)

	// 3-state shroud view. 0 A.D. rasterises a 3-level LOS bitmap
	// (visible=255 / explored=127 / unexplored=0, graphics/LOSTexture.cpp:443-449)
	// and composites it over minimap terrain (graphics/MiniMapTexture.cpp:523);
	// RTS0 mirrors those states in _draw(). The toggle rides the minimap panel
	// button row, parity with 0 A.D. gui/session/minimap/MiniMapPanel.js:8-11
	// (diplomacy/idle/flare buttons) and the enabled/disabled sprite swap of
	// gui/session/minimap/MiniMapDiplomacyColorsButton.js:24-33.
	enum ShroudMode {
		SHROUD_ON = 0, // normal: 3-tier shroud masks terrain AND entities
		SHROUD_TERRAIN = 1, // terrain-only: shroud over the terrain layer only
		SHROUD_OFF = 2, // shroud off (observer / replay view)
	};
	int shroud_mode = SHROUD_ON;
	std::vector<MapPing> pings;

	// Territory tint overlay — docs/REMAINING_GAPS.md:148 "upload territory_grid_
	// as texture, minimap overlay matches sim". RGBA8 ImageTexture rasterised from
	// sim->territory_grid() (64x64 NavGrid res, cell = team+1, 0 = unclaimed;
	// simulation.h:327 accessor / simulation.cpp:762-769 territory_owner_at) and
	// rebuilt only when the grid bytes change. Drawn under the smoothed circle
	// outlines in _draw(); see ensure_territory_texture() in minimap_view.cpp.
	Ref<ImageTexture> territory_tex_;
	std::vector<uint8_t> territory_tex_cache_;
	bool ensure_territory_texture();

protected:
	static void _bind_methods();

public:
	RtsoMiniMapView();
	~RtsoMiniMapView();

	void _draw() override;
	void _gui_input(const Ref<InputEvent> &event) override;
	void sync(const rtso::Simulation *s, Vector3 p_cam_pos);
	void add_ping(Vector2 world_pos, Color col = Color(1.0f, 0.85f, 0.2f));
	void toggle_territory() { show_territory = !show_territory; queue_redraw(); }
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
	void toggle_diplomacy() { show_diplo_palette = !show_diplo_palette; queue_redraw(); }
};

#endif
