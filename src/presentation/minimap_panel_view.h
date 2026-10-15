#ifndef RTSO_MINIMAP_PANEL_VIEW_H
#define RTSO_MINIMAP_PANEL_VIEW_H

// 0 A.D. 0.28 gui/session/minimap (GPL-2.0-or-later): the three buttons round
// the minimap (MiniMap.xml) with their 0.28 sprites and texture masks, so only
// the drawn corner takes the click and the rest reaches the minimap:
//   MiniMapIdleWorkerButton: bottom right, "Find idle worker", the idle count
//     over it, disabled with none (FindIdleUnits);
//   MiniMapDiplomacyColorsButton: bottom left, toggles diplomacy colours, its
//     sprite on or off with them;
//   MiniMapFlareButton: top left, arms the flare (INPUT_FLARE): the next left
//     click on the map or the minimap sends one, a right click cancels;
//     disabled in a replay, the observer sprite for observers.
// Behind the minimap: the viewed player's civ background
// (session/icons/bkg/background_circle_<civ>.png, gaia when observing) and
// session/minimap_circle_modern.png (MiniMapPanel.setCivBackgroundTexture).

#include <godot_cpp/classes/bit_map.hpp>
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/texture_button.hpp>
#include <godot_cpp/classes/texture_rect.hpp>
#include <godot_cpp/variant/callable.hpp>

#include <functional>
#include <string>

#include "l10n.h"

namespace rtso {

class RtsoMiniMapPanel {
public:
	enum Button { IDLE, DIPLOMACY, FLARE, COUNT };
	static constexpr const char *kDir = "res://assets/textures/ui/archive/";

	// parent: the HUD layer, after the minimap (190 px at 14 px from the bottom
	// left); the panel is MiniMap.xml's 200 px with the minimap at 8 10.
	// load: the HUD's texture loader (rtso::load_ui_texture).
	template <typename Load>
	void build(godot::Node *parent, godot::Object *owner, Load load) {
		using namespace godot;
		panel_ = memnew(Control);
		panel_->set_name("MiniMapPanel");
		panel_->set_meta("tooltip_style", "session_bottom"); // minimap_panel.xml sessionToolTipBottom
		panel_->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		panel_->set_anchor(SIDE_LEFT, 0.5f);
		panel_->set_anchor(SIDE_RIGHT, 0.5f);
		panel_->set_anchor(SIDE_TOP, 1.0f);
		panel_->set_anchor(SIDE_BOTTOM, 1.0f);
		panel_->set_offset(SIDE_LEFT, -512.0f); // session.xml: 50%-512 .. 50%-312
		panel_->set_offset(SIDE_RIGHT, -312.0f);
		panel_->set_offset(SIDE_TOP, -204.0f - 10.0f);
		panel_->set_offset(SIDE_BOTTOM, -204.0f - 10.0f + 200.0f);
		parent->add_child(panel_);
		struct Def { const char *name, *normal, *over, *disabled, *method; float x, y; };
		const Def defs[COUNT] = {
			{"idleWorkerButton", "minimap-idle.png", "minimap-idle-highlight.png", "minimap-idle-disabled.png",
					"_on_idle_worker_clicked", 200 - 119, 200 - 121},
			{"diplomacyColorsButton", "minimap-diplomacy-off.png", "minimap-diplomacy-off-highlight.png", "",
					"toggle_diplomacy_colors", 3, 200 - 121},
			{"flareButton", "minimap-player-flare.png", "minimap-player-flare-highlight.png", "minimap-flare-disabled.png",
					"_on_flare_button_pressed", 2, 4},
		};
		for (int i = 0; i < COUNT; ++i) {
			TextureButton *b = memnew(TextureButton);
			b->set_name(defs[i].name);
			b->set_position(Vector2(defs[i].x, defs[i].y));
			b->set_size(Vector2(115, 115));
			b->set_ignore_texture_size(true);
			b->set_stretch_mode(TextureButton::STRETCH_SCALE);
			b->set_focus_mode(Control::FOCUS_NONE);
			Ref<Texture2D> normal = load(String(kDir) + defs[i].normal);
			b->set_texture_normal(normal);
			b->set_texture_hover(load(String(kDir) + defs[i].over));
			if (defs[i].disabled[0]) b->set_texture_disabled(load(String(kDir) + defs[i].disabled));
			b->set_click_mask(mask(normal)); // mouse_event_mask="texture:..."
			b->connect("pressed", Callable(owner, defs[i].method));
			panel_->add_child(b);
			buttons_[i] = b;
		}
		diplomacy_on_ = load(String(kDir) + "minimap-diplomacy-on.png");
		diplomacy_on_over_ = load(String(kDir) + "minimap-diplomacy-on-highlight.png");
		diplomacy_off_ = buttons_[DIPLOMACY]->get_texture_normal();
		diplomacy_off_over_ = buttons_[DIPLOMACY]->get_texture_hover();
		player_flare_ = buttons_[FLARE]->get_texture_normal();
		player_flare_over_ = buttons_[FLARE]->get_texture_hover();
		observer_flare_ = load(String(kDir) + "minimap-observer-flare.png");
		observer_flare_over_ = load(String(kDir) + "minimap-observer-flare-highlight.png");
		// totalNumberIdleWorkers: "85% 89% 97% 98%", sans-bold-stroke-14, white, bottom right.
		count_ = memnew(Label);
		count_->set_name("totalNumberIdleWorkers");
		count_->set_position(Vector2(170, 178));
		count_->set_size(Vector2(24, 18));
		count_->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_RIGHT);
		count_->set_vertical_alignment(VERTICAL_ALIGNMENT_BOTTOM);
		count_->add_theme_font_size_override("font_size", 14);
		count_->add_theme_color_override("font_color", Color(1, 1, 1));
		count_->add_theme_color_override("font_outline_color", Color(0, 0, 0));
		count_->add_theme_constant_override("outline_size", 4);
		count_->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		panel_->add_child(count_);
	}

	// onHotkeyChange: each tooltip leads with its hotkey (colorizeHotkey).
	void set_hotkeys(const godot::String &idle, const godot::String &diplomacy, const godot::String &flare) {
		keys_[IDLE] = idle;
		keys_[DIPLOMACY] = diplomacy;
		keys_[FLARE] = flare;
		observer_ = -1;
		if (buttons_[IDLE]) buttons_[IDLE]->set_tooltip_text(lead(IDLE) + tr0("Find idle worker\nNumber of idle workers."));
		if (buttons_[DIPLOMACY]) buttons_[DIPLOMACY]->set_tooltip_text(lead(DIPLOMACY) + tr0("Toggle Diplomacy Colors"));
	}

	// minimapBackgroundTexture ("4 4 100%-4 100%-4") and the background circle ("4 6 100%-4
	// 100%-6"), added before the minimap so it draws over them.
	template <typename Load>
	void build_background(godot::Node *parent, Load load) {
		using namespace godot;
		back_ = memnew(Control);
		back_->set_name("MiniMapBackground");
		back_->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		back_->set_anchor(SIDE_LEFT, 0.5f);
		back_->set_anchor(SIDE_RIGHT, 0.5f);
		back_->set_anchor(SIDE_TOP, 1.0f);
		back_->set_anchor(SIDE_BOTTOM, 1.0f);
		back_->set_offset(SIDE_LEFT, -512.0f); // session.xml: 50%-512 .. 50%-312
		back_->set_offset(SIDE_RIGHT, -312.0f);
		back_->set_offset(SIDE_TOP, -204.0f - 10.0f);
		back_->set_offset(SIDE_BOTTOM, -204.0f - 10.0f + 200.0f);
		parent->add_child(back_);
		auto image = [&](const char *name, Vector2 pos, Vector2 size) {
			TextureRect *t = memnew(TextureRect);
			t->set_name(name);
			t->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
			t->set_stretch_mode(TextureRect::STRETCH_SCALE);
			t->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
			t->set_position(pos);
			t->set_size(size);
			back_->add_child(t);
			return t;
		};
		civ_back_ = image("minimapBackgroundTexture", Vector2(4, 4), Vector2(192, 192));
		image("minimapCircle", Vector2(4, 6), Vector2(192, 188))->set_texture(load(String(kDir) + "minimap_circle_modern.png"));
		load_ = [load](const String &p) { return load(p); };
	}

	// setCivBackgroundTexture for the viewed player's civ code ("gaia" when observing).
	void set_civ(const char *code) {
		if (!civ_back_ || civ_code_ == code) return;
		civ_code_ = code;
		civ_back_->set_texture(load_(godot::String(kDir) + "icons/bkg/background_circle_" + godot::String(code) + ".png"));
	}
	godot::String civ_background() const {
		return civ_back_ && civ_back_->get_texture().is_valid() ? civ_back_->get_texture()->get_path() : godot::String();
	}

	// rebuild: the idle count, the diplomacy sprite, the flare button's state.
	void update(int idle, bool diplomacy_colors, bool observer, bool replay) {
		if (!panel_) return;
		buttons_[IDLE]->set_disabled(idle <= 0);
		count_->set_text(idle > 0 ? godot::String::num_int64(idle) : godot::String());
		if (diplomacy_colors != diplomacy_shown_) {
			diplomacy_shown_ = diplomacy_colors;
			buttons_[DIPLOMACY]->set_texture_normal(diplomacy_colors ? diplomacy_on_ : diplomacy_off_);
			buttons_[DIPLOMACY]->set_texture_hover(diplomacy_colors ? diplomacy_on_over_ : diplomacy_off_over_);
		}
		buttons_[FLARE]->set_disabled(replay);
		if ((int)observer != observer_) {
			observer_ = observer;
			buttons_[FLARE]->set_texture_normal(observer ? observer_flare_ : player_flare_);
			buttons_[FLARE]->set_texture_hover(observer ? observer_flare_over_ : player_flare_over_);
			buttons_[FLARE]->set_tooltip_text(lead(FLARE) + tr0(observer ? "Send a flare to other observers." : "Send a flare to your allies."));
		}
	}

	godot::TextureButton *button(int i) const { return i >= 0 && i < COUNT ? buttons_[i] : nullptr; }
	godot::String idle_count_text() const { return count_ ? count_->get_text() : godot::String(); }

private:
	godot::Control *back_ = nullptr;
	godot::TextureRect *civ_back_ = nullptr;
	std::string civ_code_;
	std::function<godot::Ref<godot::Texture2D>(const godot::String &)> load_;
	godot::String lead(int i) const { return keys_[i].is_empty() ? godot::String() : keys_[i] + godot::String(" "); }

	// The sprite's alpha as the click mask.
	static godot::Ref<godot::BitMap> mask(const godot::Ref<godot::Texture2D> &tex) {
		godot::Ref<godot::BitMap> bm;
		if (tex.is_null()) return bm;
		godot::Ref<godot::Image> img = tex->get_image();
		if (img.is_null()) return bm;
		if (img->is_compressed()) img->decompress();
		bm.instantiate();
		bm->create_from_image_alpha(img, 0.1f);
		return bm;
	}

	godot::Control *panel_ = nullptr;
	godot::TextureButton *buttons_[COUNT] = {};
	godot::Label *count_ = nullptr;
	godot::String keys_[COUNT];
	godot::Ref<godot::Texture2D> diplomacy_on_, diplomacy_on_over_, diplomacy_off_, diplomacy_off_over_;
	godot::Ref<godot::Texture2D> player_flare_, player_flare_over_, observer_flare_, observer_flare_over_;
	bool diplomacy_shown_ = false;
	int observer_ = -1;
};

} // namespace rtso

#endif
