#ifndef RTSO_RESEARCH_PROGRESS_VIEW_H
#define RTSO_RESEARCH_PROGRESS_VIEW_H

// 0 A.D. 0.28 gui/session ResearchProgress.js and ResearchProgress.xml
// (GPL-2.0-or-later): a column of up to ten buttons on the right, under the top
// panel, one per technology the viewed player is researching (GuiInterface
// GetStartedResearch: every started technology, in a structure's queue or a unit's).
// Each shows the technology's icon, a dark slider over the part still to research
// (queueProgressSlider) and the time remaining as m:ss; its tooltip names the
// technology (getEntityNames). A started one that is not progressing (behind another
// item, or its unit garrisoned) is paused: a greyed icon, researchPausedIcon and
// "This item is paused.". A click selects the researcher and moves the camera to it.

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/color_rect.hpp>
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/texture_rect.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include <algorithm>
#include <cmath>

#include "../simulation/simulation.h"
#include "production_queue.h"
#include "../simulation/tech_tooltip_data.h"

namespace rtso {

class RtsoResearchProgress {
public:
	static constexpr int kButtons = 10;  // ResearchProgress.xml repeat count
	static constexpr float kSize = 38.0f;
	static constexpr float kMargin = 4.0f; // ResearchProgressButton.prototype.Margin

	// parent: the HUD layer; owner gets `method`(slot) when a button is pressed.
	void build(godot::Node *parent, godot::Object *owner, const char *method, float top) {
		using namespace godot;
		column_ = memnew(Control);
		column_->set_name("ResearchProgress");
		column_->set_meta("tooltip_style", "session_bottom"); // research_progress.xml sessionToolTipBottom
		column_->set_anchor(SIDE_LEFT, 1.0f);
		column_->set_anchor(SIDE_RIGHT, 1.0f);
		column_->set_offset(SIDE_LEFT, -48.0f); // "100%-48 40 100%-10 420"
		column_->set_offset(SIDE_RIGHT, -10.0f);
		column_->set_offset(SIDE_TOP, top);
		column_->set_offset(SIDE_BOTTOM, top + kButtons * (kSize + kMargin) + kMargin);
		column_->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		parent->add_child(column_);
		for (int i = 0; i < kButtons; ++i) {
			Slot &s = slots_[i];
			s.button = memnew(Button);
			s.button->set_name(String("ResearchStartedButton") + String::num_int64(i));
			s.button->set_position(Vector2(0, kMargin + (kMargin + kSize) * i));
			s.button->set_size(Vector2(kSize, kSize));
			s.button->set_visible(false);
			s.button->connect("pressed", Callable(owner, method).bind(i));
			column_->add_child(s.button);
			s.icon = memnew(TextureRect);
			s.icon->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
			s.icon->set_stretch_mode(TextureRect::STRETCH_SCALE);
			s.icon->set_position(Vector2(3, 3));
			s.icon->set_size(Vector2(32, 32));
			s.icon->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
			s.button->add_child(s.icon);
			s.slider = memnew(ColorRect);
			s.slider->set_color(Color(0, 0, 0, 0.55f));
			s.slider->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
			s.button->add_child(s.slider);
			s.time = memnew(Label);
			s.time->set_position(Vector2(0, 0));
			s.time->set_size(Vector2(kSize, kSize));
			s.time->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
			s.time->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
			s.time->add_theme_font_size_override("font_size", 12);
			s.time->add_theme_color_override("font_outline_color", Color(0, 0, 0, 1));
			s.time->add_theme_constant_override("outline_size", 3);
			s.time->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
			s.button->add_child(s.time);
			// researchPausedIcon: stretched:session/icons/stop.png, "3 3 35 35".
			s.paused = memnew(TextureRect);
			s.paused->set_texture(load_ui_texture("res://assets/textures/ui/archive/icons/stop.png"));
			s.paused->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
			s.paused->set_stretch_mode(TextureRect::STRETCH_SCALE);
			s.paused->set_position(Vector2(3, 3));
			s.paused->set_size(Vector2(32, 32));
			s.paused->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
			s.paused->set_visible(false);
			s.button->add_child(s.paused);
		}
		Ref<Shader> sh;
		sh.instantiate();
		sh->set_code("shader_type canvas_item;\nvoid fragment() {\n\tvec4 c = texture(TEXTURE, UV);\n"
				"\tfloat g = dot(c.rgb, vec3(0.3, 0.59, 0.11));\n\tCOLOR = vec4(vec3(g), c.a) * COLOR;\n}\n");
		grayscale_.instantiate();
		grayscale_->set_shader(sh);
	}

	void refresh(const Simulation &sim, uint8_t player) {
		if (!column_) return;
		int k = 0;
		auto show = [&](uint32_t researcher, const ProductionOrder &po, bool paused) {
			if (k == kButtons) return;
			Slot &s = slots_[k++];
			s.researcher = researcher;
			if (s.tech != po.tech || s.is_paused != paused) {
				s.tech = po.tech;
				s.is_paused = paused;
				s.icon->set_texture(icon(po.tech));
				// "stretched:color:0 0 0 127:grayscale:" while paused.
				s.icon->set_material(paused ? godot::Ref<godot::Material>(grayscale_) : godot::Ref<godot::Material>());
				s.icon->set_self_modulate(paused ? godot::Color(0.5f, 0.5f, 0.5f) : godot::Color(1, 1, 1));
				s.paused->set_visible(paused);
				const char *specific = tech_specific_name(po.tech, sim.player_civ(player));
				godot::String tip = entity_names(godot::String::utf8(tech_info(po.tech).name), specific ? godot::String::utf8(specific) : godot::String());
				if (paused) tip += "\n" + tr0("This item is paused.");
				s.button->set_tooltip_text(tip);
			}
			const float progress = po.duration > 0.0f ? std::clamp(po.timer / po.duration, 0.0f, 1.0f) : 0.0f;
			// queueProgressSlider "3 3 37 37": its top moves down as the research advances.
			s.slider->set_position(godot::Vector2(3, 3 + 34.0f * progress));
			s.slider->set_size(godot::Vector2(34, 34.0f * (1.0f - progress)));
			const int left = (int)std::ceil(std::max(po.duration - po.timer, 0.0f));
			s.time->set_text(godot::vformat("%d:%02d", left / 60, left % 60)); // countdown format "m:ss"
			s.button->set_visible(true);
		};
		if (player < MAX_PLAYERS) {
			// The front item progresses; one further back is listed once it has started.
			for (const auto &b : sim.buildings()) {
				if (b.team != player || b.queue.empty() || b.build_progress < 1.0f) continue;
				for (size_t q = 0; q < b.queue.size(); ++q)
					if (b.queue[q].is_tech() && (q == 0 || b.queue[q].timer > 0.0f)) show(b.id, b.queue[q], q != 0);
			}
			const auto uv = sim.units();
			for (const auto &kv : sim.unit_queues()) {
				if (kv.second.player != player) continue;
				bool outside = false; // a garrisoned trainer's production pauses
				for (size_t i = 0; i < uv.count; ++i)
					if (uv.id[i] == kv.first) { outside = uv.alive[i]; break; }
				const auto &orders = kv.second.orders;
				for (size_t q = 0; q < orders.size(); ++q)
					if (orders[q].is_tech() && (q == 0 || orders[q].timer > 0.0f)) show(kv.first, orders[q], q != 0 || !outside);
			}
		}
		for (; k < kButtons; ++k) {
			slots_[k].button->set_visible(false);
			slots_[k].researcher = 0;
		}
	}

	uint32_t researcher_at(int slot) const { return slot >= 0 && slot < kButtons ? slots_[slot].researcher : 0; }

private:
	struct Slot {
		godot::Button *button = nullptr;
		godot::TextureRect *icon = nullptr;
		godot::ColorRect *slider = nullptr;
		godot::Label *time = nullptr;
		godot::TextureRect *paused = nullptr;
		uint32_t researcher = 0;
		uint8_t tech = TECH_NONE;
		bool is_paused = false;
	};
	godot::Ref<godot::ShaderMaterial> grayscale_;

public:
	// The technology's 0.28 icon (data/reference/templates.json techs.<template>.icon).
	static godot::Ref<godot::Texture2D> icon(uint8_t tech) {
		using namespace godot;
		static Dictionary techs;
		static bool loaded = false;
		if (!loaded) {
			loaded = true;
			Ref<FileAccess> f = FileAccess::open("res://data/reference/templates.json", FileAccess::READ);
			if (f.is_valid()) techs = Dictionary(JSON::parse_string(f->get_as_text())).get("techs", Dictionary());
		}
		const char *tmpl = tech_info(tech).template_name;
		const String file = Dictionary(techs.get(String(tmpl ? tmpl : ""), Dictionary())).get("icon", "");
		if (!file.is_empty()) {
			Ref<Texture2D> t = load_ui_texture("res://assets/textures/ui/archive/portraits/technologies/" + file);
			if (t.is_valid()) return t;
		}
		return load_ui_texture(String("res://assets/textures/ui/portraits/") + building_type_info(tech_info(tech).researcher).portrait_id + ".png");
	}

private:
	godot::Control *column_ = nullptr;
	Slot slots_[kButtons];
};

} // namespace rtso

#endif
