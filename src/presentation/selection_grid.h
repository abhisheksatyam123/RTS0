#ifndef RTSO_SELECTION_GRID_H
#define RTSO_SELECTION_GRID_H

// Icon grid of entities grouped by template, used for the multi-selection panel
// (0 A.D. gui/session/selection_panels.js g_SelectionPanels.Selection) and the
// garrison panel (g_SelectionPanels.Garrison). Each icon shows the group's
// portrait, its size and the group's summed health; clicking calls
// owner.<method>(group_key). The grid only rebuilds when the groups change.

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/grid_container.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/progress_bar.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/string.hpp>

#include <cstdint>
#include <vector>

#include "../simulation/simulation.h"
#include "top_panel.h" // rtso::load_ui_texture

using namespace godot;

class RtsoSelectionGrid {
public:
	struct Group {
		int key = 0; // group_key()
		uint8_t unit_type = 0;
		uint16_t tmpl = 0;
		int count = 0;
		float hp = 0, max_hp = 0;
	};

	// Template row when the unit has one, else 0x10000 | unit type.
	static int group_key(uint8_t unit_type, uint16_t tmpl) {
		return tmpl != rtso::CIV_UNIT_TEMPLATE_NONE ? (int)tmpl : (0x10000 | unit_type);
	}

	// Groups `ids` (live units of the unit view) by template, in first-seen order.
	static std::vector<Group> group_units(const rtso::Simulation::UnitView &uv, const uint32_t *ids, size_t n) {
		std::vector<Group> groups;
		for (size_t s = 0; s < n; ++s) {
			for (size_t i = 0; i < uv.count; ++i) {
				if (uv.id[i] != ids[s] || !uv.alive[i]) continue;
				const uint16_t tmpl = uv.tmpl ? uv.tmpl[i] : rtso::CIV_UNIT_TEMPLATE_NONE;
				const int key = group_key(uv.unit_type[i], tmpl);
				Group *g = nullptr;
				for (auto &e : groups)
					if (e.key == key) g = &e;
				if (!g) {
					groups.push_back({key, uv.unit_type[i], tmpl, 0, 0, 0});
					g = &groups.back();
				}
				++g->count;
				g->hp += uv.hp[i];
				g->max_hp += uv.max_hp[i];
				break;
			}
		}
		return groups;
	}

	static String group_name(const Group &g) {
		if (g.tmpl != rtso::CIV_UNIT_TEMPLATE_NONE) return String::utf8(rtso::civ_unit_template(g.tmpl).name);
		return String(rtso::unit_type_info(g.unit_type).name);
	}

	// columns: icons per row; icon: icon size in pixels.
	void build(Node *parent, Vector2 pos, Vector2 size, Object *owner, const char *method, int columns, float icon) {
		owner_ = owner;
		method_ = method;
		icon_ = icon;
		grid_ = memnew(GridContainer);
		grid_->set_columns(columns);
		grid_->set_position(pos);
		grid_->set_size(size);
		grid_->add_theme_constant_override("h_separation", 3);
		grid_->add_theme_constant_override("v_separation", 3);
		grid_->set_visible(false);
		parent->add_child(grid_);
	}

	GridContainer *grid() const { return grid_; }

	static String tooltip(const Group &g, const String &tip_suffix) {
		return group_name(g) + " (" + String::num_int64(g.count) + ")\nHealth: " +
				String::num(g.hp, 0) + " / " + String::num(g.max_hp, 0) + "\n" + tip_suffix;
	}

	// Visibility is decided per frame (hide, then show if still wanted) but
	// only applied when it changes: hiding a Button mid-click drops the
	// press, so a hide and show in one frame lost every click.
	void set_visible(bool v) {
		if (grid_ && grid_->is_visible() != v) grid_->set_visible(v);
	}

	// Shows `groups`; `tip_suffix` ends every tooltip (the click help).
	void update(const std::vector<Group> &groups, const String &tip_suffix) {
		if (!grid_) return;
		// Only the groups and their sizes rebuild the icons; health changes
		// every hit and is updated in place, so a click is not lost to a
		// Button replaced between its press and its release.
		uint64_t sig = groups.size();
		for (const auto &g : groups)
			sig = sig * 1000003ull + (uint64_t)g.key * 131ull + (uint64_t)g.count * 7ull;
		if (sig == sig_ && grid_->get_child_count() == (int)groups.size()) {
			for (size_t i = 0; i < groups.size(); ++i) {
				Button *b = Object::cast_to<Button>(grid_->get_child((int)i));
				if (!b) continue;
				const String tip = tooltip(groups[i], tip_suffix);
				if (b->get_tooltip_text() != tip) b->set_tooltip_text(tip);
				for (int c = 0; c < b->get_child_count(); ++c)
					if (ProgressBar *hp = Object::cast_to<ProgressBar>(b->get_child(c))) {
						hp->set_max(groups[i].max_hp > 0 ? groups[i].max_hp : 1.0f);
						hp->set_value(groups[i].hp);
					}
			}
			return;
		}
		sig_ = sig;
		while (grid_->get_child_count() > 0) {
			Node *c = grid_->get_child(0);
			grid_->remove_child(c);
			c->queue_free();
		}
		for (const auto &g : groups) {
			Button *b = memnew(Button);
			b->set_custom_minimum_size(Vector2(icon_, icon_));
			b->set_expand_icon(true);
			b->set_icon_alignment(HORIZONTAL_ALIGNMENT_CENTER);
			Ref<Texture2D> tex = rtso::load_ui_texture(String("res://assets/textures/ui/portraits/") + rtso::unit_type_portrait(g.unit_type) + ".png");
			if (tex.is_valid()) b->set_button_icon(tex);
			else b->set_text(group_name(g).substr(0, 3));
			b->set_tooltip_text(tooltip(g, tip_suffix));
			b->connect("pressed", Callable(owner_, method_).bind(g.key));
			if (g.count > 1) {
				Label *cnt = memnew(Label);
				cnt->set_text(String::num_int64(g.count));
				cnt->add_theme_font_size_override("font_size", 11);
				cnt->add_theme_color_override("font_outline_color", Color(0, 0, 0));
				cnt->add_theme_constant_override("outline_size", 3);
				cnt->set_position(Vector2(icon_ - 16, icon_ - 17));
				cnt->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
				b->add_child(cnt);
			}
			// Group health under the icon (selection_panels.js health bar).
			ProgressBar *hp = memnew(ProgressBar);
			hp->set_show_percentage(false);
			hp->set_position(Vector2(2, icon_ - 4));
			hp->set_size(Vector2(icon_ - 4, 3));
			hp->set_max(g.max_hp > 0 ? g.max_hp : 1.0f);
			hp->set_value(g.hp);
			hp->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
			Ref<StyleBoxFlat> fill;
			fill.instantiate();
			fill->set_bg_color(Color(0.25f, 0.8f, 0.3f));
			hp->add_theme_stylebox_override("fill", fill);
			b->add_child(hp);
			grid_->add_child(b);
		}
	}

private:
	GridContainer *grid_ = nullptr;
	Object *owner_ = nullptr;
	const char *method_ = "";
	float icon_ = 36.0f;
	uint64_t sig_ = ~0ull;
};

#endif
