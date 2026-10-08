#ifndef RTSO_TOP_PANEL_H
#define RTSO_TOP_PANEL_H

// P4 stage-2 leaf widget: Top HUD Bar (0 A.D. gui/session/top_panel.js parity).
// Owns civ & phase emblem badges, 4 resource counters (food, wood, metal/gold,
// stone), population gauge, idle worker trigger button, match clock, and wave /
// wonder countdown banner.

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/texture_rect.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include <cstdint>

#include "../simulation/simulation.h"

#ifndef RTSO_UI_LOAD_TEXTURE
#define RTSO_UI_LOAD_TEXTURE
namespace rtso {
inline godot::Ref<godot::Texture2D> load_ui_texture(const godot::String &path) {
	if (godot::ResourceLoader::get_singleton()->exists(path)) {
		godot::Ref<godot::Texture2D> tex = godot::ResourceLoader::get_singleton()->load(path);
		if (tex.is_valid()) return tex;
	}
	return godot::Ref<godot::Texture2D>();
}
} // namespace rtso
#endif

class RtsoTopPanel {
public:
	// owner: signal target object (RtsoMain, for _on_idle_worker_clicked).
	// top_hbox: parent HBoxContainer inside top_hud_panel.
	// initial_civ: current player civ ID.
	void build(godot::Object *owner, godot::HBoxContainer *top_hbox, uint8_t initial_civ) {
		using namespace godot;

		// 1. Civ Emblem Badge
		civ_emblem_ = memnew(TextureRect);
		civ_emblem_->set_custom_minimum_size(Vector2(38, 38));
		civ_emblem_->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
		civ_emblem_->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
		civ_emblem_->set_texture(rtso::load_ui_texture(rtso::civ_info(initial_civ).emblem_path));
		top_hbox->add_child(civ_emblem_);

		// 2. Phase Emblem Badge (Village, Town, City)
		phase_emblem_ = memnew(TextureRect);
		phase_emblem_->set_custom_minimum_size(Vector2(38, 38));
		phase_emblem_->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
		phase_emblem_->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
		phase_emblem_->set_texture(rtso::load_ui_texture("res://assets/textures/ui/phases/village.png"));
		top_hbox->add_child(phase_emblem_);

		// 3. Resource Items (Food, Wood, Gold, Stone)
		const char *res_paths[4] = {
			"res://assets/textures/ui/icons/food.png",
			"res://assets/textures/ui/icons/wood.png",
			"res://assets/textures/ui/icons/metal.png",
			"res://assets/textures/ui/icons/stone.png"
		};
		for (int i = 0; i < 4; ++i) {
			HBoxContainer *rc = memnew(HBoxContainer);
			rc->add_theme_constant_override("separation", 4);

			res_icons_[i] = memnew(TextureRect);
			res_icons_[i]->set_custom_minimum_size(Vector2(26, 26));
			res_icons_[i]->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
			res_icons_[i]->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
			res_icons_[i]->set_texture(rtso::load_ui_texture(res_paths[i]));
			rc->add_child(res_icons_[i]);

			res_labels_[i] = memnew(Label);
			res_labels_[i]->set_custom_minimum_size(Vector2(95, 26));
			res_labels_[i]->add_theme_font_size_override("font_size", 13);
			res_labels_[i]->add_theme_color_override("font_color", Color(0.95f, 0.95f, 0.98f));
			rc->add_child(res_labels_[i]);

			top_hbox->add_child(rc);
		}

		// 4. Population Item
		HBoxContainer *pop_box = memnew(HBoxContainer);
		pop_box->add_theme_constant_override("separation", 4);
		pop_icon_ = memnew(TextureRect);
		pop_icon_->set_custom_minimum_size(Vector2(26, 26));
		pop_icon_->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
		pop_icon_->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
		pop_icon_->set_texture(rtso::load_ui_texture("res://assets/textures/ui/icons/population.png"));
		pop_box->add_child(pop_icon_);

		pop_label_ = memnew(Label);
		pop_label_->set_custom_minimum_size(Vector2(75, 26));
		pop_label_->add_theme_font_size_override("font_size", 13);
		pop_box->add_child(pop_label_);
		top_hbox->add_child(pop_box);

		// 5. Idle Worker Button
		idle_worker_btn_ = memnew(Button);
		idle_worker_btn_->set_custom_minimum_size(Vector2(90, 30));
		idle_worker_btn_->set_text("Idle: 0");
		idle_worker_btn_->add_theme_font_size_override("font_size", 12);
		idle_worker_btn_->connect("pressed", Callable(owner, "_on_idle_worker_clicked"));
		top_hbox->add_child(idle_worker_btn_);

		// 6. Clock & Wave
		clock_label_ = memnew(Label);
		clock_label_->set_custom_minimum_size(Vector2(65, 26));
		clock_label_->add_theme_font_size_override("font_size", 13);
		clock_label_->add_theme_color_override("font_color", Color(1.0f, 0.85f, 0.4f));
		clock_label_->set_text("00:00");
		top_hbox->add_child(clock_label_);

		wave_label_ = memnew(Label);
		wave_label_->set_custom_minimum_size(Vector2(140, 26));
		wave_label_->add_theme_font_size_override("font_size", 12);
		wave_label_->add_theme_color_override("font_color", Color(0.95f, 0.6f, 0.5f));
		top_hbox->add_child(wave_label_);
	}

	bool is_ready() const { return res_labels_[0] != nullptr; }

	void set_civ(uint8_t civ_id) {
		if (civ_emblem_) {
			const auto &info = rtso::civ_info(civ_id);
			civ_emblem_->set_texture(rtso::load_ui_texture(info.emblem_path));
		}
	}

	// `me` is the local player whose economy the panel shows.
	void update(rtso::Simulation *sim, uint8_t me, double match_elapsed_time) {
		using namespace godot;
		if (!sim || !res_labels_[0]) return;

		// Dynamic Civ Crest & Phase Badge Emblems
		const auto &cinfo = rtso::civ_info(sim->player_civ(me));
		if (civ_emblem_) {
			civ_emblem_->set_texture(rtso::load_ui_texture(cinfo.emblem_path));
		}
		if (phase_emblem_) {
			uint8_t pphase = sim->player_phase(me);
			if (pphase == 1) phase_emblem_->set_texture(rtso::load_ui_texture("res://assets/textures/ui/phases/village.png"));
			else if (pphase == 2) phase_emblem_->set_texture(rtso::load_ui_texture("res://assets/textures/ui/phases/town.png"));
			else phase_emblem_->set_texture(rtso::load_ui_texture("res://assets/textures/ui/phases/city.png"));
		}

		// Resource counts & rates
		res_labels_[0]->set_text(String::num_int64(sim->food(me)) + " (+" + String::num(sim->food_rate(me), 0) + "/m)");
		res_labels_[1]->set_text(String::num_int64(sim->wood(me)) + " (+" + String::num(sim->wood_rate(me), 0) + "/m)");
		res_labels_[2]->set_text(String::num_int64(sim->gold(me)) + " (+" + String::num(sim->gold_rate(me), 0) + "/m)");
		res_labels_[3]->set_text(String::num_int64(sim->stone(me)) + " (+" + String::num(sim->stone_rate(me), 0) + "/m)");

		// Pop & Idle
		int cur_pop = sim->pop_current(me);
		int max_pop = sim->pop_max(me);
		pop_label_->set_text(String::num_int64(cur_pop) + "/" + String::num_int64(max_pop));
		if (cur_pop >= max_pop) pop_label_->add_theme_color_override("font_color", Color(1.0f, 0.3f, 0.3f));
		else pop_label_->add_theme_color_override("font_color", Color(0.95f, 0.95f, 0.98f));

		auto idle_workers = sim->get_idle_workers(me);
		idle_worker_btn_->set_text("Idle: " + String::num_int64(idle_workers.size()));
		if (!idle_workers.empty()) idle_worker_btn_->add_theme_color_override("font_color", Color(1.0f, 0.85f, 0.2f));
		else idle_worker_btn_->add_theme_color_override("font_color", Color(0.85f, 0.85f, 0.88f));

		// Clock & Wave
		int mins = (int)match_elapsed_time / 60;
		int secs = (int)match_elapsed_time % 60;
		String min_str = (mins < 10 ? "0" : "") + String::num_int64(mins);
		String sec_str = (secs < 10 ? "0" : "") + String::num_int64(secs);
		clock_label_->set_text(min_str + ":" + sec_str);

		String phase_name = (sim->player_phase(me) == 1) ? "Village Phase" : ((sim->player_phase(me) == 2) ? "Town Phase" : "City Phase");
		if (sim->is_wonder_active()) {
			int wmins = (int)sim->wonder_time_remaining() / 60;
			int wsecs = (int)sim->wonder_time_remaining() % 60;
			String wmin_str = (wmins < 10 ? "0" : "") + String::num_int64(wmins);
			String wsec_str = (wsecs < 10 ? "0" : "") + String::num_int64(wsecs);
			wave_label_->set_text("Wonder Victory in " + wmin_str + ":" + wsec_str + " | " + phase_name);
		} else if (sim->ceasefire_active()) {
			int cs = (int)sim->ceasefire_remaining();
			wave_label_->set_text("Ceasefire " + String::num_int64(cs / 60) + ":" + (cs % 60 < 10 ? "0" : "") +
				String::num_int64(cs % 60) + " | " + phase_name);
		} else if (sim->waves_enabled()) {
			wave_label_->set_text("Wave " + String::num_int64(sim->wave_number()) +
				" (" + String::num((double)sim->wave_time_remaining(), 0) + "s) | " + phase_name);
		} else {
			wave_label_->set_text(phase_name);
		}
	}

private:
	godot::TextureRect *civ_emblem_ = nullptr;
	godot::TextureRect *phase_emblem_ = nullptr;
	godot::TextureRect *res_icons_[4] = {nullptr};
	godot::Label *res_labels_[4] = {nullptr};
	godot::TextureRect *pop_icon_ = nullptr;
	godot::Label *pop_label_ = nullptr;
	godot::Button *idle_worker_btn_ = nullptr;
	godot::Label *clock_label_ = nullptr;
	godot::Label *wave_label_ = nullptr;
};

#endif
