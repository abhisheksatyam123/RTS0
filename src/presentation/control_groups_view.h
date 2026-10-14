#ifndef RTSO_CONTROL_GROUPS_VIEW_H
#define RTSO_CONTROL_GROUPS_VIEW_H

// 0 A.D. 0.28 gui/session session_objects/selection_group_icons.xml and
// session.js updateGroups (GPL-2.0-or-later): a column of ten buttons on the
// left edge, one per control group that has units, numbered 1 to 0 like the
// hotkeys. Each shows the portrait of the group's most common template (the
// costlier on a tie). A click selects the group (selection.add adds it), a
// double click also moves the camera to it (performGroup "snap"), and a right
// click disbands it (performGroup "breakUp").

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/texture_rect.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>

#include <map>

#include "../simulation/simulation.h"
#include "panel_entities_view.h"

namespace rtso {

class RtsoControlGroups {
public:
	static constexpr int kButtons = 10;
	static constexpr float kSize = 38.0f;

	// parent: the HUD layer; owner gets `method`(event, button) on input. Button k
	// (0-9) is the group of hotkey k + 1, the tenth the one of 0: groups[(k + 1) % 10].
	void build(godot::Node *parent, godot::Object *owner, const char *method) {
		using namespace godot;
		column_ = memnew(Control);
		column_->set_name("UnitGroupPanel");
		column_->set_anchor(SIDE_TOP, 0.5f);
		column_->set_anchor(SIDE_BOTTOM, 0.5f);
		column_->set_offset(SIDE_LEFT, 0.0f); // "0 50%-228 38 50%+152"
		column_->set_offset(SIDE_RIGHT, kSize);
		column_->set_offset(SIDE_TOP, -228.0f);
		column_->set_offset(SIDE_BOTTOM, 152.0f);
		column_->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		parent->add_child(column_);
		for (int k = 0; k < kButtons; ++k) {
			Slot &s = slots_[k];
			s.button = memnew(Button);
			s.button->set_name(String("UnitGroupButton") + String::num_int64(k));
			s.button->set_position(Vector2(0, k * (kSize + 1.0f))); // setPanelObjectPosition(button, i, 1)
			s.button->set_size(Vector2(kSize, kSize));
			s.button->set_tooltip_text("Click to select grouped units, double-click to focus the grouped units and right-click to disband the group.");
			s.button->set_visible(false);
			s.button->connect("gui_input", Callable(owner, method).bind(k));
			column_->add_child(s.button);
			s.icon = memnew(TextureRect);
			s.icon->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
			s.icon->set_stretch_mode(TextureRect::STRETCH_SCALE);
			s.icon->set_position(Vector2(3, 3));
			s.icon->set_size(Vector2(32, 32));
			s.icon->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
			s.button->add_child(s.icon);
			Label *l = memnew(Label); // largeCenteredOutlinedText
			l->set_text(String::num_int64(k + 1));
			l->set_size(Vector2(kSize, kSize));
			l->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
			l->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
			l->add_theme_font_size_override("font_size", 16);
			l->add_theme_color_override("font_outline_color", Color(0, 0, 0, 1));
			l->add_theme_constant_override("outline_size", 4);
			l->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
			s.button->add_child(l);
		}
	}

	static int group_of(int button) { return (button + 1) % kButtons; }

	// groups: RtsoMain's selected_groups (unit ids, index = hotkey number % 10).
	void refresh(const Simulation &sim, uint8_t player, const godot::PackedInt32Array *groups) {
		if (!column_) return;
		auto uview = sim.units();
		std::map<uint32_t, size_t> index;
		for (size_t i = 0; i < uview.count; ++i)
			if (uview.alive[i] && uview.team[i] == player) index[uview.id[i]] = i;
		for (int k = 0; k < kButtons; ++k) {
			const godot::PackedInt32Array &g = groups[group_of(k)];
			// The most common template, the costlier on a tie (getCostSum).
			std::map<uint32_t, int> count;
			uint32_t best = UINT32_MAX;
			int best_n = 0, best_cost = -1, total = 0;
			for (int m = 0; m < g.size(); ++m) {
				auto it = index.find((uint32_t)g[m]);
				if (it == index.end()) continue;
				++total;
				const size_t i = it->second;
				const uint16_t tmpl = uview.tmpl ? uview.tmpl[i] : CIV_UNIT_TEMPLATE_NONE;
				const uint32_t key = ((uint32_t)tmpl << 8) | uview.unit_type[i];
				const int n = ++count[key];
				const auto &info = tmpl < CIV_UNIT_TEMPLATE_COUNT ? civ_template_unit_info(tmpl, uview.unit_type[i]) : unit_type_info(uview.unit_type[i]);
				const int cost = info.food_cost + info.wood_cost + info.gold_cost + info.stone_cost;
				if (n > best_n || (n == best_n && cost > best_cost)) {
					best = key;
					best_n = n;
					best_cost = cost;
				}
			}
			Slot &s = slots_[k];
			s.button->set_visible(total > 0);
			if (total > 0 && best != s.icon_key) {
				s.icon_key = best;
				s.icon->set_texture(RtsoPanelEntities::portrait((uint16_t)(best >> 8), (uint8_t)(best & 0xFF)));
			}
		}
	}

private:
	struct Slot {
		godot::Button *button = nullptr;
		godot::TextureRect *icon = nullptr;
		uint32_t icon_key = UINT32_MAX;
	};
	godot::Control *column_ = nullptr;
	Slot slots_[kButtons];
};

} // namespace rtso

#endif
