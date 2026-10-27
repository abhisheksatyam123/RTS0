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
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/texture_rect.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/string.hpp>

#include <cstdint>
#include <vector>

#include "../simulation/simulation.h"
#include "template_portraits.h"
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
		// The garrison panel's extras: the units' owner (its colour behind the icon when
		// `bg` is opaque), whether a click may unload them (else disabled, its icon grey
		// unless `grey` is off) and the tooltip, which replaces the default one.
		uint8_t player = 0xFF;
		bool enabled = true, grey = true;
		Color bg = Color(0, 0, 0, 0);
		String tip;
		// A structure group: its portrait and name (units take theirs from the template).
		Ref<Texture2D> icon;
		String name;
	};

	// selection_panels.js Selection.getMaxNumberOfItems: at most 16 groups show.
	static constexpr size_t kMaxSelectionGroups = 16;

	// A structure type of a civ (selection.js groups structures by template too).
	static int structure_key(uint8_t civ, uint8_t type) { return 0x100000 | ((int)civ << 8) | (int)type; }
	static bool is_structure_key(int key) { return (key & 0x100000) != 0; }

	// identity.selectionGroupName where a support template sets it, grouping every civ's variant
	// (and the house-trained woman with the Civic Centre's); else null.
	static const char *shared_selection_group(uint8_t unit_type) {
		switch (unit_type) {
			case rtso::UNIT_FEMALE_CITIZEN: return "template_unit_support_female_citizen";
			case rtso::UNIT_HEALER: return "template_unit_support_healer";
			case rtso::UNIT_TRADER: return "template_unit_support_trader";
			case rtso::UNIT_FISHING_BOAT: return "template_unit_ship_fishing";
			case rtso::UNIT_MERCHANT_SHIP: return "template_unit_ship_merchant";
			case rtso::UNIT_CATAFALQUE: return "template_unit_catafalque";
			default: return nullptr;
		}
	}

	// selection.js EntityGroups key, selectionGroupName || templateName: the template row when
	// the unit has one, else 0x10000 | unit type; a shared support group keys on its unit type.
	// Every rank shares one group, as each _a and _e template inherits its _b template's
	// SelectionGroupName (GarrisonHolder.UnloadTemplate: "Units with multiple ranks are
	// grouped together").
	static int group_key(uint8_t unit_type, uint16_t tmpl) {
		if (tmpl == rtso::CIV_UNIT_TEMPLATE_NONE || shared_selection_group(unit_type)) return 0x10000 | unit_type;
		return (int)tmpl;
	}

	// A group of the player's own livestock (fauna, not units): its FaunaType.
	static int fauna_key(uint8_t fauna_type) { return 0x20000 | fauna_type; }

	// The garrison panel's groups (EntityGroups): template and owner.
	static int owner_key(uint8_t unit_type, uint16_t tmpl, uint8_t player) {
		return group_key(unit_type, tmpl) | ((int)player << 17);
	}

	// Groups `ids` (live units of the unit view) by template, or template and owner, in
	// first-seen order.
	static std::vector<Group> group_units(const rtso::Simulation::UnitView &uv, const uint32_t *ids, size_t n, bool by_owner = false) {
		std::vector<Group> groups;
		for (size_t s = 0; s < n; ++s) {
			for (size_t i = 0; i < uv.count; ++i) {
				if (uv.id[i] != ids[s]) continue;
				// Garrisoned units and passengers are out of the world (alive 0) but living.
				if (by_owner ? uv.hp[i] <= 0.0f : !uv.alive[i]) break;
				const uint16_t tmpl = uv.tmpl ? uv.tmpl[i] : rtso::CIV_UNIT_TEMPLATE_NONE;
				const int key = by_owner ? owner_key(uv.unit_type[i], tmpl, uv.team[i]) : group_key(uv.unit_type[i], tmpl);
				Group *g = nullptr;
				for (auto &e : groups)
					if (e.key == key) g = &e;
				if (!g) {
					groups.push_back({key, uv.unit_type[i], tmpl, 0, 0, 0, uv.team[i]});
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
		if (!g.name.is_empty()) return g.name;
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
	// A right-click on an icon calls owner.method(event, key) (selection_panels.js onPressRight).
	void set_right_click(const char *method) { right_method_ = method; }

	static String tooltip(const Group &g, const String &tip_suffix) {
		if (!g.tip.is_empty()) return g.tip;
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
			sig = sig * 1000003ull + (uint64_t)g.key * 131ull + (uint64_t)g.count * 7ull + (g.enabled ? 3ull : 0ull) +
					(uint64_t)g.bg.to_rgba32();
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
			Ref<Texture2D> tex = g.icon.is_valid() ? g.icon : rtso::portraits::unit(g.tmpl, g.unit_type);
			if (tex.is_valid() && g.enabled) {
				b->set_button_icon(tex);
			} else if (tex.is_valid()) {
				// A disabled button keeps its look; only the portrait goes grey ("grayscale:").
				TextureRect *icon = memnew(TextureRect);
				icon->set_name("garrisonIcon");
				icon->set_texture(tex);
				icon->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
				icon->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
				icon->set_position(Vector2(3, 3));
				icon->set_size(Vector2(icon_ - 6, icon_ - 6));
				icon->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
				if (g.grey) icon->set_material(grayscale());
				b->add_child(icon);
			} else {
				b->set_text(group_name(g).substr(0, 3));
			}
			b->set_disabled(!g.enabled);
			if (g.bg.a > 0.0f) {
				// guiSelection "color:<player colour> 160", and sprite_disabled = sprite.
				for (const char *state : {"normal", "hover", "pressed", "disabled"}) {
					Ref<StyleBoxFlat> sb;
					sb.instantiate();
					sb->set_bg_color(String(state) == "hover" ? g.bg.lightened(0.2f) : g.bg);
					sb->set_corner_radius_all(2);
					b->add_theme_stylebox_override(state, sb);
				}
			}
			b->set_tooltip_text(tooltip(g, tip_suffix));
			b->connect("pressed", Callable(owner_, method_).bind(g.key));
			if (right_method_) b->connect("gui_input", Callable(owner_, right_method_).bind(g.key));
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
	Ref<ShaderMaterial> grayscale() {
		if (grayscale_.is_null()) {
			Ref<Shader> sh;
			sh.instantiate();
			sh->set_code("shader_type canvas_item;\nvoid fragment() {\n\tvec4 c = texture(TEXTURE, UV);\n"
					"\tfloat g = dot(c.rgb, vec3(0.3, 0.59, 0.11));\n\tCOLOR = vec4(vec3(g), c.a) * COLOR;\n}\n");
			grayscale_.instantiate();
			grayscale_->set_shader(sh);
		}
		return grayscale_;
	}

	GridContainer *grid_ = nullptr;
	Ref<ShaderMaterial> grayscale_; // a member, not a static: it goes before the engine shuts down
	Object *owner_ = nullptr;
	const char *method_ = "";
	const char *right_method_ = nullptr; // called with (event, key) on a right-click
	float icon_ = 36.0f;
	uint64_t sig_ = ~0ull;
};

#endif
