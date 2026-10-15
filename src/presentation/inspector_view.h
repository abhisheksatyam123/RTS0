#ifndef RTSO_INSPECTOR_VIEW_H
#define RTSO_INSPECTOR_VIEW_H

// P4 stage-2 leaf widget: the bottom inspector (0 A.D. gui/session/selection_details.js
// and selection_panels.js Selection / Garrison). Owns the portrait, title, the five
// bars, the stats lines, the unload button and the selection and garrison grids; shows
// the selected structure, units or livestock, or the foreign entity a click inspected.

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/color_rect.hpp>
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/text_server.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/panel.hpp>
#include <godot_cpp/classes/progress_bar.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/classes/texture_rect.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/theme.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <algorithm>
#include <initializer_list>
#include <cstdint>
#include <vector>

#include "../simulation/simulation.h"
#include "attack_detection.h"
#include "display_colors.h"
#include "fog_mirage.h"
#include "hotkeys.h"
#include "l10n.h"
#include "selection_details.h"
#include "selection_grid.h"
#include "template_portraits.h"
#include "top_panel.h" // rtso::load_ui_texture

class RtsoInspectorHost {
public:
	virtual ~RtsoInspectorHost() = default;
	virtual rtso::Simulation *ui_sim() = 0;
	virtual uint8_t local_player() const = 0;
	virtual uint8_t view_player() const = 0;
	virtual godot::String player_display_name(uint8_t pid) const = 0;
	// messages.js onClientLeave g_Players[id].offline: a human who left the match.
	virtual bool player_offline(uint8_t pid) const { (void)pid; return false; }
	virtual uint32_t ui_selected_building_id() const = 0;
	virtual const godot::PackedInt32Array &ui_selected_ids() const = 0;
	// Every selected structure, the first being ui_selected_building_id().
	virtual std::vector<uint32_t> ui_selected_structures() const = 0;
};

class RtsoInspectorView {
public:
	enum InspectKind : uint8_t { INSPECT_NONE, INSPECT_UNIT, INSPECT_BUILDING, INSPECT_FAUNA, INSPECT_RESOURCE, INSPECT_TREASURE };

	// owner: signal target (RtsoMain: _on_ungarrison_pressed and the grid clicks).
	// parent: the HUD layer the panel goes on.
	void build(godot::Object *owner, godot::Node *parent, const godot::Ref<godot::Theme> &theme, RtsoInspectorHost *host) {
		using namespace godot;
		host_ = host;
		panel_ = memnew(Panel);
		panel_->set_anchor(SIDE_LEFT, 0.0f);
		panel_->set_anchor(SIDE_RIGHT, 0.0f);
		panel_->set_anchor(SIDE_TOP, 1.0f);
		panel_->set_anchor(SIDE_BOTTOM, 1.0f);
		panel_->set_offset(SIDE_LEFT, 214.0f);
		panel_->set_offset(SIDE_RIGHT, 730.0f);
		panel_->set_offset(SIDE_TOP, -198.0f);
		panel_->set_offset(SIDE_BOTTOM, -10.0f);

		Ref<StyleBoxFlat> insp_style;
		insp_style.instantiate();
		insp_style->set_bg_color(Color(0.10f, 0.11f, 0.15f, 0.94f));
		insp_style->set_corner_radius_all(3);
		insp_style->set_border_width_all(2);
		insp_style->set_border_color(Color(0.90f, 0.75f, 0.31f, 1.0f));
		panel_->add_theme_stylebox_override("panel", insp_style);
		if (theme.is_valid()) panel_->set_theme(theme);
		parent->add_child(panel_);

		portrait_ = memnew(TextureRect);
		portrait_->set_position(Vector2(10, 10));
		portrait_->set_size(Vector2(84, 84));
		portrait_->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
		portrait_->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_COVERED);
		panel_->add_child(portrait_);
		// single_details_area.xml: the iconBorder's clicks (left follows or focuses, right opens the
		// Template Viewer), the rankIcon at 4 4 20 20 and up to five statusEffectsIcons down the
		// right edge (100%-20 4 100%-4 100%), each 16 px every 18 px.
		portrait_->connect("gui_input", Callable(owner, "_on_portrait_gui_input"));
		rank_icon_ = memnew(TextureRect);
		rank_icon_->set_position(Vector2(4, 4));
		rank_icon_->set_size(Vector2(16, 16));
		rank_icon_->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
		rank_icon_->set_mouse_filter(Control::MOUSE_FILTER_PASS);
		rank_icon_->set_visible(false);
		portrait_->add_child(rank_icon_);
		for (int i = 0; i < kStatusIcons; ++i) {
			status_icons_[i] = memnew(TextureRect);
			status_icons_[i]->set_position(Vector2(84 - 20, 4 + i * 18));
			status_icons_[i]->set_size(Vector2(16, 16));
			status_icons_[i]->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
			status_icons_[i]->set_mouse_filter(Control::MOUSE_FILTER_PASS);
			status_icons_[i]->set_visible(false);
			portrait_->add_child(status_icons_[i]);
		}

		title_ = memnew(Label);
		title_->set_position(Vector2(102, 6));
		title_->set_size(Vector2(404, 18));
		title_->add_theme_font_size_override("font_size", 13);
		title_->add_theme_color_override("font_color", Color(1.0f, 0.88f, 0.4f));
		panel_->add_child(title_);

		// selection_details.js's player row: the name on the player colour (alpha 128), the
		// civ emblem in grey behind it and the phase emblems, "Civ — Town Phase" as its tooltip.
		owner_bg_ = memnew(ColorRect);
		owner_bg_->set_position(Vector2(102, 24));
		owner_bg_->set_size(Vector2(404, 15));
		owner_bg_->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		owner_bg_->set_visible(false);
		panel_->add_child(owner_bg_);
		owner_civ_ = memnew(TextureRect);
		owner_civ_->set_position(Vector2(102, 24));
		owner_civ_->set_size(Vector2(15, 15));
		owner_civ_->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
		owner_civ_->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
		owner_civ_->set_modulate(Color(0.6f, 0.6f, 0.6f, 0.8f));
		owner_civ_->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		owner_civ_->set_visible(false);
		panel_->add_child(owner_civ_);
		owner_phase_ = memnew(TextureRect);
		owner_phase_->set_position(Vector2(462, 24));
		owner_phase_->set_size(Vector2(44, 15));
		owner_phase_->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
		owner_phase_->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
		owner_phase_->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		owner_phase_->set_visible(false);
		panel_->add_child(owner_phase_);

		class_ = memnew(Label);
		class_->set_position(Vector2(102, 24));
		class_->set_size(Vector2(404, 15));
		class_->add_theme_font_size_override("font_size", 10);
		class_->add_theme_color_override("font_color", Color(0.7f, 0.75f, 0.85f));
		class_->add_theme_color_override("font_outline_color", Color(0.0f, 0.0f, 0.0f));
		class_->set_mouse_filter(Control::MOUSE_FILTER_PASS);
		panel_->add_child(class_);

		// Helper lambda to setup 5 inspector bars with PNG icons or fallback text-based glyph icon labels
		// (0 A.D. gui/session/selection_details.js icon parity: HP/CP/XP/STA/AURA)
		auto make_insp_bar = [&](ProgressBar *&pbar, Label *&plbl, int y, Color fill_col, const char *glyph_text, const char *icon_path = nullptr) {
			Label *glyph_lbl = memnew(Label);
			glyph_lbl->set_position(Vector2(102, y - 2));
			glyph_lbl->set_size(Vector2(42, 14));
			glyph_lbl->set_text(glyph_text);
			glyph_lbl->add_theme_font_size_override("font_size", 9);
			glyph_lbl->add_theme_color_override("font_color", fill_col);
			panel_->add_child(glyph_lbl);

			if (icon_path && *icon_path) {
				Ref<Texture2D> icon_tex = rtso::load_ui_texture(icon_path);
				if (icon_tex.is_valid()) {
					TextureRect *icon_rect = memnew(TextureRect);
					icon_rect->set_position(Vector2(102, y - 2));
					icon_rect->set_size(Vector2(14, 14));
					icon_rect->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
					icon_rect->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
					icon_rect->set_texture(icon_tex);
					panel_->add_child(icon_rect);
					glyph_lbl->set_visible(false);
				}
			}

			pbar = memnew(ProgressBar);
			pbar->set_position(Vector2(148, y));
			pbar->set_size(Vector2(358, 11));
			pbar->set_show_percentage(false);

			Ref<StyleBoxFlat> bg_sb;
			bg_sb.instantiate();
			bg_sb->set_bg_color(Color(0.10f, 0.12f, 0.16f, 0.85f));
			bg_sb->set_corner_radius_all(2);
			pbar->add_theme_stylebox_override("background", bg_sb);

			Ref<StyleBoxFlat> fg_sb;
			fg_sb.instantiate();
			fg_sb->set_bg_color(fill_col);
			fg_sb->set_corner_radius_all(2);
			pbar->add_theme_stylebox_override("fill", fg_sb);
			panel_->add_child(pbar);

			plbl = memnew(Label);
			plbl->set_position(Vector2(148, y - 1));
			plbl->set_size(Vector2(358, 13));
			plbl->add_theme_font_size_override("font_size", 9);
			plbl->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
			plbl->add_theme_color_override("font_color", Color(0.95f, 0.95f, 0.98f));
			panel_->add_child(plbl);
		};

		// 1. Health Bar (Green)
		make_insp_bar(hp_bar_, hp_label_, 41, Color(0.25f, 0.75f, 0.35f), "HP", "res://assets/textures/ui/icons/insp_health_fg.png");
		// 2. Capture Points Bar (Cyan / Blue)
		make_insp_bar(capture_bar_, capture_label_, 54, Color(0.35f, 0.55f, 0.95f), "CP", "res://assets/textures/ui/icons/insp_capture_bar.png");
		// selection_details.js captureBar[n]: one part per player in their colour at half alpha,
		// the owner's first, over the bar (whose own fill stays empty).
		for (ColorRect *&part : capture_parts_) {
			part = memnew(ColorRect);
			part->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
			part->set_visible(false);
			capture_bar_->add_child(part);
		}
		// 3. Veterancy / Experience XP Bar (Gold / Amber)
		make_insp_bar(xp_bar_, xp_label_, 67, Color(0.95f, 0.75f, 0.20f), "XP", "res://assets/textures/ui/icons/insp_experience_fg.png");
		// 4. Stamina / Energy Bar (Teal / Mint)
		make_insp_bar(stamina_bar_, stamina_label_, 80, Color(0.20f, 0.85f, 0.75f), "STA", "res://assets/textures/ui/icons/insp_stamina_fg.png");
		// 5. Aura Influence Bar (Magenta / Violet) - no public.zip session icon, fallback to text glyph badge
		make_insp_bar(aura_bar_, aura_label_, 93, Color(0.85f, 0.35f, 0.85f), "AURA", nullptr);

		// resourceSection: "Wood:" beside a bar of the supply left, "amount / max" (or "∞") on it.
		// It takes the first bar row under the health bar that nothing else uses.
		resource_name_ = memnew(Label);
		resource_name_->set_size(Vector2(44, 14));
		resource_name_->add_theme_font_size_override("font_size", 9);
		resource_name_->add_theme_color_override("font_color", Color(0.95f, 0.8f, 0.35f));
		resource_name_->set_visible(false);
		panel_->add_child(resource_name_);
		resource_bar_ = memnew(ProgressBar);
		resource_bar_->set_name("resourceBar");
		resource_bar_->set_size(Vector2(358, 11));
		resource_bar_->set_show_percentage(false);
		{
			Ref<StyleBoxFlat> bg_sb, fg_sb;
			bg_sb.instantiate();
			bg_sb->set_bg_color(Color(0.10f, 0.12f, 0.16f, 0.85f));
			bg_sb->set_corner_radius_all(2);
			fg_sb.instantiate();
			fg_sb->set_bg_color(Color(0.85f, 0.65f, 0.2f));
			fg_sb->set_corner_radius_all(2);
			resource_bar_->add_theme_stylebox_override("background", bg_sb);
			resource_bar_->add_theme_stylebox_override("fill", fg_sb);
		}
		resource_bar_->set_visible(false);
		panel_->add_child(resource_bar_);
		resource_stats_ = memnew(Label);
		resource_stats_->set_size(Vector2(358, 13));
		resource_stats_->add_theme_font_size_override("font_size", 9);
		resource_stats_->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
		resource_stats_->add_theme_color_override("font_color", Color(0.95f, 0.95f, 0.98f));
		resource_stats_->set_visible(false);
		panel_->add_child(resource_stats_);

		stats_ = memnew(Label);
		stats_->set_position(Vector2(102, 108));
		stats_->set_size(Vector2(404, 38));
		stats_->add_theme_font_size_override("font_size", 10);
		stats_->add_theme_color_override("font_color", Color(0.85f, 0.88f, 0.95f));
		panel_->add_child(stats_);

		extra_ = memnew(Label);
		extra_->set_position(Vector2(10, 100));
		extra_->set_size(Vector2(88, 48));
		extra_->add_theme_font_size_override("font_size", 9);
		extra_->add_theme_color_override("font_color", Color(0.4f, 0.95f, 0.6f));
		panel_->add_child(extra_);

		ungarrison_btn_ = memnew(Button);
		ungarrison_btn_->set_position(Vector2(10, 150));
		ungarrison_btn_->set_size(Vector2(88, 26));
		ungarrison_btn_->set_text("Ungarrison (Z)");
		ungarrison_btn_->add_theme_font_size_override("font_size", 10);
		ungarrison_btn_->connect("pressed", Callable(owner, "_on_ungarrison_pressed"));
		ungarrison_btn_->set_visible(false);
		panel_->add_child(ungarrison_btn_);

		// selection_details.js tooltips: the portrait (name, auras), the title (a group's
		// garrison size, carried resources and loot) and the stats (attack, resistance...).
		for (Control *c : std::initializer_list<Control *>{portrait_, title_, stats_, extra_, aura_label_})
			c->set_mouse_filter(Control::MOUSE_FILTER_PASS);

		// Multi-selection and garrison icon grids (0 A.D. selection_panels.js).
		selection_grid_.build(panel_, Vector2(102, 60), Vector2(404, 120), owner, "_on_selection_group_clicked", 8, 36.0f);
		selection_grid_.set_right_click("_on_selection_group_gui_input");
		garrison_grid_.build(panel_, Vector2(102, 148), Vector2(404, 34), owner, "_on_garrison_group_clicked", 12, 30.0f);
		// garrison_panel.xml healthGarrison: the garrisoned units' total health, a vertical
		// bar left of the garrison icons (updateGarrisonHealthBar).
		garrison_hp_bar_ = memnew(ProgressBar);
		garrison_hp_bar_->set_name("healthGarrison");
		garrison_hp_bar_->set_position(Vector2(99, 149));
		garrison_hp_bar_->set_size(Vector2(3, 30));
		garrison_hp_bar_->set_fill_mode(ProgressBar::FILL_BOTTOM_TO_TOP);
		garrison_hp_bar_->set_show_percentage(false);
		{
			Ref<StyleBoxFlat> bg_sb;
			bg_sb.instantiate();
			bg_sb->set_bg_color(Color(0.10f, 0.12f, 0.16f, 0.85f));
			garrison_hp_bar_->add_theme_stylebox_override("background", bg_sb);
			Ref<StyleBoxFlat> fg_sb;
			fg_sb.instantiate();
			fg_sb->set_bg_color(Color(0.25f, 0.75f, 0.35f));
			garrison_hp_bar_->add_theme_stylebox_override("fill", fg_sb);
		}
		garrison_hp_bar_->set_visible(false);
		panel_->add_child(garrison_hp_bar_);
		// The Barter panel's place (0.28's left panel, shared with the garrison panel); the
		// command panel fills it.
		barter_host_ = memnew(Control);
		barter_host_->set_name("unitBarterPanel");
		barter_host_->set_position(Vector2(148, 100));
		barter_host_->set_size(Vector2(164, 82));
		barter_host_->set_visible(false);
		panel_->add_child(barter_host_);
	}

	godot::Panel *panel() const { return panel_; }
	RtsoSelectionGrid &selection_grid() { return selection_grid_; }
	// 0 A.D. selects what the player doesn't own too (another player's unit or
	// structure, an animal, a tree or a mine) to show its details, with no commands
	// (EntitySelection; the command panels need the player's own entities).
	uint8_t kind() const { return kind_; }
	uint32_t id() const { return id_; }
	// The portrait's click targets (RtsoMain::_on_portrait_gui_input).
	uint32_t portrait_follow_id() const { return portrait_follow_id_; }
	godot::Vector2 portrait_position() const { return godot::Vector2(portrait_x_, portrait_z_); }
	const godot::String &portrait_viewer_kind() const { return portrait_viewer_kind_; }
	int portrait_viewer_id() const { return portrait_viewer_id_; }
	int portrait_viewer_civ() const { return portrait_viewer_civ_; }
	// A selection of the player's own replaces whatever a click inspected.
	void clear() { kind_ = INSPECT_NONE; }
	// Tests: inspect an entity as a click would, and read back what the panel shows.
	void inspect(uint8_t kind, uint32_t id) {
		kind_ = kind;
		id_ = id;
	}
	godot::Dictionary debug_state() const {
		godot::Dictionary d;
		d["owner"] = class_->get_text();
		d["owner_tooltip"] = class_->get_tooltip_text();
		d["owner_color"] = owner_bg_->is_visible() ? owner_bg_->get_color() : godot::Color(0, 0, 0, 0);
		d["owner_phase"] = owner_phase_->is_visible() && owner_phase_->get_texture().is_valid() ? owner_phase_->get_texture()->get_path() : godot::String();
		d["visible"] = panel_ && panel_->is_visible();
		d["title"] = title_->get_text();
		d["title_tooltip"] = title_->get_tooltip_text();
		d["portrait_tooltip"] = portrait_->get_tooltip_text();
		d["portrait"] = portrait_->get_texture().is_valid() ? portrait_->get_texture()->get_path() : godot::String();
		d["rank_icon"] = rank_icon_->is_visible() && rank_icon_->get_texture().is_valid() ? rank_icon_->get_texture()->get_path() : godot::String();
		d["rank_tooltip"] = rank_icon_->get_tooltip_text();
		godot::Array statuses;
		for (int i = 0; i < kStatusIcons; ++i)
			if (status_icons_[i]->is_visible()) {
				godot::Dictionary st;
				st["icon"] = status_icons_[i]->get_texture().is_valid() ? status_icons_[i]->get_texture()->get_path() : godot::String();
				st["tooltip"] = status_icons_[i]->get_tooltip_text();
				statuses.push_back(st);
			}
		d["statuses"] = statuses;
		d["health"] = hp_label_->get_text();
		d["xp"] = xp_label_->is_visible() ? xp_label_->get_text() : godot::String();
		d["stats"] = stats_->get_text();
		godot::Array capture_parts;
		for (godot::ColorRect *r : capture_parts_)
			if (r->is_visible()) {
				godot::Dictionary part;
				part["color"] = r->get_color();
				part["width"] = r->get_size().x;
				capture_parts.push_back(part);
			}
		d["capture_parts"] = capture_parts;
		d["xp_tooltip"] = xp_bar_->get_tooltip_text();
		d["stats_tooltip"] = stats_->get_tooltip_text();
		d["extra"] = extra_->get_text();
		d["resource"] = resource_bar_->is_visible() ? resource_name_->get_text() + " " + resource_stats_->get_text() : godot::String();
		d["extra_tooltip"] = extra_->get_tooltip_text();
		return d;
	}

	// What a click on (x, z) inspects when it hits nothing of the player's own: another
	// player's unit, an animal, another player's structure or a resource, nearest first by
	// that order, and only what the player sees (units, animals) or has explored.
	bool inspect_at(const Vector3 &click_pos) {
		rtso::Simulation *sim = host_->ui_sim();
		if (!sim) return false;
		const uint8_t vp = host_->view_player();
		const bool all = sim->reveal_map() || vp >= rtso::MAX_PLAYERS;
		auto seen = [&](float x, float z, uint8_t team) { return all || sim->is_entity_visible_to_viewer(x, z, team, vp); };
		auto explored = [&](float x, float z) { return all || sim->fow_state_at(x, z, vp) != rtso::Simulation::FOW_UNEXPLORED; };
		auto pick = [&](uint8_t kind, uint32_t id) {
			kind_ = kind;
			id_ = id;
			return true;
		};
		const float cx = click_pos.x, cz = click_pos.z;
		{
			auto uv = sim->units();
			size_t best = (size_t)-1;
			float best_d2 = 1.4f * 1.4f;
			for (size_t i = 0; i < uv.count; ++i) {
				if (!uv.alive[i] || uv.team[i] == host_->local_player() || !seen(uv.pos_x[i], uv.pos_z[i], uv.team[i])) continue;
				const float dx = uv.pos_x[i] - cx, dz = uv.pos_z[i] - cz, d2 = dx * dx + dz * dz;
				if (d2 < best_d2) { best_d2 = d2; best = i; }
			}
			if (best != (size_t)-1) return pick(INSPECT_UNIT, uv.id[best]);
		}
		{
			auto fv = sim->fauna();
			size_t best = (size_t)-1;
			float best_d2 = 1.6f * 1.6f;
			for (size_t f = 0; f < fv.count; ++f) {
				if (!fv.alive[f] || fv.behavior[f] == rtso::FAUNA_CORRALLED) continue;
				if (!seen(fv.pos_x[f], fv.pos_z[f], fv.owner ? fv.owner[f] : rtso::GAIA_PLAYER)) continue;
				const float dx = fv.pos_x[f] - cx, dz = fv.pos_z[f] - cz, d2 = dx * dx + dz * dz;
				if (d2 < best_d2) { best_d2 = d2; best = f; }
			}
			if (best != (size_t)-1) return pick(INSPECT_FAUNA, fv.id[best]);
		}
		// Structures as the viewer knows them (Fogging.js): live in sight, the mirage in the
		// fog (it stays selectable after the structure is gone), nothing if never seen.
		mirages_.update(*sim, vp);
		for (const auto &m : mirages_.buildings()) {
			if (m.team == host_->local_player() || m.wrecked) continue;
			const rtso::Simulation::Building *b = nullptr;
			for (const auto &lb : sim->buildings())
				if (lb.id == m.id) { b = &lb; break; }
			const float hr = (b && b->hu > 0.0f ? std::max(b->hu, b->hv) : m.half_size) + 0.4f;
			const float dx = m.x - cx, dz = m.z - cz;
			if (dx * dx + dz * dz <= hr * hr) return pick(INSPECT_BUILDING, m.id);
		}
		{
			auto rv = sim->resources();
			size_t best = (size_t)-1;
			float best_d2 = 1.3f * 1.3f;
			for (size_t r = 0; r < rv.count; ++r) {
				if (!rv.alive[r] || rv.subtype[r] == rtso::RES_SUB_GRAIN || !explored(rv.x[r], rv.z[r])) continue;
				const float dx = rv.x[r] - cx, dz = rv.z[r] - cz, d2 = dx * dx + dz * dz;
				if (d2 < best_d2) { best_d2 = d2; best = r; }
			}
			if (best != (size_t)-1) return pick(INSPECT_RESOURCE, rv.id[best]);
		}
		{
			uint32_t best = 0;
			float best_d2 = 1.3f * 1.3f;
			for (const auto &t : sim->treasures()) {
				if (!explored(t.x, t.z)) continue;
				const float dx = t.x - cx, dz = t.z - cz, d2 = dx * dx + dz * dz;
				if (d2 < best_d2) { best_d2 = d2; best = t.id; }
			}
			if (best != 0) return pick(INSPECT_TREASURE, best);
		}
		kind_ = INSPECT_NONE;
		return false;
	}

	// False when the inspected entity is gone or out of sight.
	godot::GridContainer *garrison_grid_view() const { return garrison_grid_.grid(); }
	godot::Control *barter_host() const { return barter_host_; }

	bool fill_inspected_details() {
		rtso::Simulation *sim = host_->ui_sim();
		if (!sim || kind_ == INSPECT_NONE) return false;
		const uint8_t vp = host_->view_player();
		const bool all = sim->reveal_map() || vp >= rtso::MAX_PLAYERS;
		auto bar = [](ProgressBar *pb, Label *lbl, bool on, float value = 0.0f, float max = 1.0f, const String &text = String()) {
			pb->set_visible(on);
			lbl->set_visible(on);
			if (!on) return;
			pb->set_max(max > 0.0f ? max : 1.0f);
			pb->set_value(value);
			lbl->set_text(text);
		};
		auto health = [&](float hp, float max) {
			bar(hp_bar_, hp_label_, max > 0.0f, hp, max, rtso::seldet::health(hp, max));
		};
		bar(capture_bar_, capture_label_, false);
		bar(xp_bar_, xp_label_, false);
		bar(aura_bar_, aura_label_, false);
		extra_->set_text("");
		hide_resource_bar();
		clear_tooltips();

		if (kind_ == INSPECT_UNIT) {
			auto uv = sim->units();
			size_t u = 0;
			for (; u < uv.count; ++u)
				if (uv.id[u] == id_) break;
			if (u == uv.count || !uv.alive[u] || (!all && !sim->is_entity_visible_to_viewer(uv.pos_x[u], uv.pos_z[u], uv.team[u], vp)))
				return false;
			const uint8_t utype = uv.unit_type[u];
			const uint16_t tmpl = uv.tmpl ? uv.tmpl[u] : rtso::CIV_UNIT_TEMPLATE_NONE;
			const auto &uinfo = tmpl != rtso::CIV_UNIT_TEMPLATE_NONE ? rtso::civ_template_unit_info(tmpl, utype) : rtso::unit_type_info(utype);
			portrait_->set_texture(rtso::portraits::unit(tmpl, utype));
			title_->set_text(unit_name(uv, u, uinfo.name));
			set_owner(*sim, uv.team[u]);
			health(uv.hp[u], uv.max_hp[u]);
			fill_unit_details(*sim, u, title_->get_text());
			set_unit_extras(*sim, u);
			return true;
		}
		if (kind_ == INSPECT_FAUNA) {
			auto fv = sim->fauna();
			size_t f = 0;
			for (; f < fv.count; ++f)
				if (fv.id[f] == id_) break;
			const uint8_t owner = f < fv.count && fv.owner ? fv.owner[f] : rtso::GAIA_PLAYER;
			if (f == fv.count || !fv.alive[f] || fv.behavior[f] == rtso::FAUNA_CORRALLED ||
					(!all && !sim->is_entity_visible_to_viewer(fv.pos_x[f], fv.pos_z[f], owner, vp)))
				return false;
			const auto &fi = rtso::fauna_type_info(fv.fauna_type[f]);
			portrait_->set_texture(rtso::portraits::fauna(fv.fauna_type[f]));
			title_->set_text(String::utf8(fi.name));
			set_owner(*sim, owner);
			health(fv.hp[f], fv.max_hp[f]);
			set_statuses(*sim, rtso::Simulation::STATUS_ON_FAUNA, fv.id[f]);
			set_portrait_target(false, 0, fv.pos_x[f], fv.pos_z[f], String(), -1, 0);
			portrait_->set_tooltip_text(portrait_tooltip(title_->get_text(), rtso::portraits::fauna_entry(fv.fauna_type[f]), String(), String(), false));
			// An animal's Attack/Melee and Resistance as tooltips.js shows a unit's.
			rtso::Simulation::UnitDetails d;
			d.damage[0] = fi.hack_damage;
			d.damage[1] = fi.pierce_damage;
			d.damage[2] = fi.crush_damage;
			d.interval = fi.attack_cooldown;
			d.max_range = fi.attack_range;
			d.resistance[0] = fi.hack_armor;
			d.resistance[1] = fi.pierce_armor;
			d.resistance[2] = fi.crush_armor;
			// getSpeedTooltip: walk and run, in 0 A.D. metres a second (RTS0 walks at x0.356).
			d.walk = fi.speed / 0.356f;
			d.run = d.walk * (fi.run_mult > 0.0f ? fi.run_mult : 1.0f);
			const String attack = rtso::seldet::attack_tooltip(d,
					godot::Dictionary(rtso::portraits::fauna_entry(fv.fauna_type[f]).get("viewer", godot::Dictionary())).get("attacks", godot::Array())),
					resistance = rtso::seldet::resistance_tooltip(d.resistance);
			stats_->set_text(rtso::seldet::join_lines({body_lines(attack), body_lines(resistance)}));
			stats_->set_tooltip_text(rtso::seldet::join_lines({attack, resistance, rtso::seldet::speed_tooltip(d)}));
			// resourceSection: the meat it yields, "Food:" and 100 / 100 on the bar.
			if (fv.food_amount[f] > 0)
				show_resource_bar(rtso::RES_FOOD, fv.food_amount[f], std::max(fi.food_amount, fv.food_amount[f]), false);
			return true;
		}
		if (kind_ == INSPECT_BUILDING) {
			// Fogging.js AddMiragedEntity: a fogged structure shows its mirage, the Health,
			// Capturable, Foundation and owner it had when last seen.
			mirages_.update(*sim, host_->view_player());
			const rtso::FoggedBuilding *seen = nullptr;
			for (const auto &m : mirages_.buildings())
				if (m.id == id_) { seen = &m; break; }
			if (!seen) return false;
			if (seen->miraged) {
				const auto &binfo = rtso::building_type_info(seen->type);
				portrait_->set_texture(rtso::portraits::structure(seen->civ, seen->type));
				title_->set_text(structure_name(seen->civ, seen->type, binfo.name));
				set_owner(*sim, seen->team);
				health(seen->hp, seen->max_hp);
				if (seen->max_capture_points > 0.0f)
				{
					bar(capture_bar_, capture_label_, true, seen->capture_points, seen->max_capture_points,
							rtso::seldet::capture(seen->capture_points, seen->max_capture_points));
					float pts[rtso::MAX_PLAYERS + 1] = {};
					pts[seen->team < rtso::MAX_PLAYERS ? seen->team : rtso::MAX_PLAYERS] = seen->capture_points;
					set_capture_parts(*sim, seen->team, pts, seen->max_capture_points);
				}
				stats_->set_text(seen->build_progress < 1.0f
						? "Construction: " + String::num((double)(seen->build_progress * 100.0f), 0) + "%" : String());
				stats_->set_tooltip_text(String());
				return true;
			}
			const rtso::Simulation::Building *found = nullptr;
			for (const auto &b : sim->buildings())
				if (b.id == id_) { found = &b; break; }
			if (!found || found->wrecked || found->hp <= 0.0f) return false;
			const auto &binfo = rtso::building_type_info(found->type);
			portrait_->set_texture(rtso::portraits::structure(found->civ, found->type));
			title_->set_text(structure_name(found->civ, found->type, binfo.name));
			set_owner(*sim, found->team);
			health(found->hp, found->max_hp);
			set_structure_extras(*sim, *found);
			if (found->max_capture_points > 0.0f) {
				bar(capture_bar_, capture_label_, true, found->capture_points, found->max_capture_points,
						rtso::seldet::capture(found->capture_points, found->max_capture_points));
				set_structure_capture_parts(*sim, *found);
			}
			rtso::Simulation::BuildWork work;
			String build_tip;
			if (sim->structure_build_work(*found, work)) {
				stats_->set_text(join_spaced(found->build_progress < 1.0f
						? "Construction: " + String::num((double)(found->build_progress * 100.0f), 0) + "%" : rtso::tr0("Repair"),
						rtso::seldet::build_work_caption(work.builders, work.remaining)));
				build_tip = build_work_tooltip(work);
			} else {
				stats_->set_text(supply_lines(*sim, sim->field_resource_id(found->id)));
			}
			stats_->set_tooltip_text(rtso::seldet::join_lines({build_tip, structure_tooltip(*sim, *found)}));
			return true;
		}
		if (kind_ == INSPECT_TREASURE) {
			// selection_details.js: the treasure's name and its Treasure/Resources as the reward.
			const rtso::Simulation::Treasure *found = nullptr;
			for (const auto &t : sim->treasures())
				if (t.id == id_) { found = &t; break; }
			if (!found) return false;
			const auto &ti = rtso::treasure_type_info(found->type);
			static const char *kRes[4] = {"Food", "Wood", "Stone", "Metal"};
			String reward;
			for (int k = 0; k < 4; ++k)
				if (ti.amount[k] > 0) reward += (reward.is_empty() ? "" : ", ") + String::num_int64(ti.amount[k]) + " " + kRes[k];
			portrait_->set_texture(rtso::portraits::treasure(found->type));
			title_->set_text(String::utf8(ti.name));
			set_portrait_target(false, 0, found->x, found->z, String(), -1, 0);
			portrait_->set_tooltip_text(portrait_tooltip(title_->get_text(), rtso::portraits::treasure_entry(found->type), String(),
					rtso::seldet::reward_tooltip(ti.amount), false));
			set_owner(*sim, rtso::GAIA_PLAYER);
			bar(hp_bar_, hp_label_, false);
			stats_->set_text("Reward: " + reward + "\nCollect time: " + String::num((double)ti.collect_time, 1) + " s");
			extra_->set_text("Collect treasures for resources.");
			return true;
		}
		// A resource: its kind and what is left (selection_details.js resourceSupply).
		auto rv = sim->resources();
		size_t r = 0;
		for (; r < rv.count; ++r)
			if (rv.id[r] == id_) break;
		if (r == rv.count || !rv.alive[r] || rv.amount[r] <= 0) return false;
		String title, kind;
		switch (rv.type[r]) {
		case rtso::RES_WOOD: title = flora_title(rv.flora ? rv.flora[r] : 0); kind = "Wood"; break;
		case rtso::RES_GOLD: title = rv.subtype[r] == rtso::RES_SUB_RUINS ? "Ruins" : "Metal Mine"; kind = "Metal"; break;
		case rtso::RES_STONE: title = rv.subtype[r] == rtso::RES_SUB_RUINS ? "Ruins" : "Stone Quarry"; kind = "Stone"; break;
		default:
			title = rv.subtype[r] == rtso::RES_SUB_MEAT ? String("Carcass") : rv.subtype[r] == rtso::RES_SUB_FISH ? String("Fish")
					: flora_title(rv.flora ? rv.flora[r] : rtso::FLORA_BERRY);
			kind = "Food";
		}
		// A tree or fruit: its template's SpecificName under the Tree or Fruit GenericName
		// (gaia/tree and gaia/fruit templates set only SpecificName): "Aleppo Pine (Tree)".
		if (rv.type[r] == rtso::RES_WOOD || (rv.type[r] == rtso::RES_FOOD && rv.subtype[r] != rtso::RES_SUB_MEAT &&
				rv.subtype[r] != rtso::RES_SUB_FISH && rv.subtype[r] != rtso::RES_SUB_GRAIN)) {
			const String generic = rtso::portraits::resource_entry(rv.type[r], rv.subtype[r], rv.flora ? rv.flora[r] : 0).get("generic_name", String());
			if (!generic.is_empty()) title = rtso::entity_names(generic, title);
		}
		portrait_->set_texture(rtso::portraits::resource(rv.type[r], rv.subtype[r], rv.flora ? rv.flora[r] : 0));
		title_->set_text(title);
		set_owner(*sim, rtso::GAIA_PLAYER);
		bar(hp_bar_, hp_label_, false);
		set_portrait_target(false, 0, rv.x[r], rv.z[r], String(), -1, 0);
		portrait_->set_tooltip_text(portrait_tooltip(title, rtso::portraits::resource_entry(rv.type[r], rv.subtype[r], rv.flora ? rv.flora[r] : 0),
				String(), String(), false));
		// resourceSection ("Wood:" and amount / max, ∞ for a field) and the gatherers count
		// (current / max, "Current/max gatherers").
		(void)kind;
		stats_->set_text(supply_lines(*sim, rv.id[r]));
		stats_->set_tooltip_text(rtso::tr0("Current/max gatherers"));
		return true;
	}

	// The player's own selected livestock (0 A.D. livestock are units): one animal's details,
	// or displayMultiple for several (count, summed health, a group per kind). False when
	// the selection holds anything else.
	bool fill_livestock_details(bool &show_grid) {
		rtso::Simulation *sim = host_->ui_sim();
		const PackedInt32Array &selected_ids = host_->ui_selected_ids();
		if (!sim || selected_ids.is_empty()) return false;
		for (int s = 0; s < selected_ids.size(); ++s)
			if (!sim->fauna_selectable_by(host_->local_player(), (uint32_t)selected_ids[s])) return false;
		const uint8_t keep_kind = kind_;
		const uint32_t keep_id = id_;
		kind_ = INSPECT_FAUNA;
		id_ = (uint32_t)selected_ids[0];
		const bool ok = fill_inspected_details();
		kind_ = keep_kind;
		id_ = keep_id;
		if (!ok || selected_ids.size() < 2) return ok;
		auto fv = sim->fauna();
		std::vector<RtsoSelectionGrid::Group> groups;
		for (int s = 0; s < selected_ids.size(); ++s)
			for (size_t f = 0; f < fv.count; ++f) {
				if (fv.id[f] != (uint32_t)selected_ids[s]) continue;
				const int key = RtsoSelectionGrid::fauna_key(fv.fauna_type[f]);
				RtsoSelectionGrid::Group *g = nullptr;
				for (auto &e : groups)
					if (e.key == key) g = &e;
				if (!g) {
					groups.emplace_back();
					g = &groups.back();
					g->key = key;
					g->icon = rtso::portraits::fauna(fv.fauna_type[f]);
					g->name = String::utf8(rtso::fauna_type_info(fv.fauna_type[f]).name);
					g->player = fv.owner ? fv.owner[f] : rtso::GAIA_PLAYER;
				}
				++g->count;
				g->hp += fv.hp[f];
				g->max_hp += fv.max_hp[f];
				break;
			}
		float hp = 0.0f, max_hp = 0.0f;
		for (auto &g : groups) {
			hp += g.hp;
			max_hp += g.max_hp;
			g.tip = rtso::entity_names(RtsoSelectionGrid::group_name(g), String()) + "\n" +
					rtso::tr0("Click to select only these, Ctrl+click or right-click to deselect them.");
		}
		if (groups.size() > RtsoSelectionGrid::kMaxSelectionGroups) groups.resize(RtsoSelectionGrid::kMaxSelectionGroups);
		title_->set_text(String::num_int64(selected_ids.size()) + " units selected");
		title_->set_tooltip_text("");
		portrait_->set_tooltip_text("");
		clear_owner();
		class_->set_text(String::num_int64(groups.size()) + (groups.size() == 1 ? " type" : " types"));
		hp_bar_->set_max(max_hp > 0.0f ? max_hp : 1.0f);
		hp_bar_->set_value(hp);
		hp_label_->set_text(rtso::seldet::health(hp, max_hp));
		for (int i = 0; i < kStatusIcons; ++i) status_icons_[i]->set_visible(false);
		hide_resource_bar();
		stats_->set_text("");
		stats_->set_tooltip_text("");
		extra_->set_text("");
		selection_grid_.update(groups, "");
		show_grid = true;
		return true;
	}

	void update() {
		rtso::Simulation *sim = host_->ui_sim();
		const PackedInt32Array &selected_ids = host_->ui_selected_ids();
		const uint32_t selected_building_id = host_->ui_selected_building_id();
		if (!panel_ || !sim) return;
		auto show_bar = [](ProgressBar *bar, Label *lbl, bool v) {
			bar->set_visible(v);
			lbl->set_visible(v);
		};
		auto set_bar = [&](ProgressBar *bar, Label *lbl, float value, float max, const String &text) {
			show_bar(bar, lbl, true);
			bar->set_max(max > 0.0f ? max : 1.0f);
			bar->set_value(value);
			lbl->set_text(text);
		};
		auto armour_text = [](float hack, float pierce, float crush) {
			return "Armor: " + String::num(hack, 0) + " hack, " + String::num(pierce, 0) + " pierce, " + String::num(crush, 0) + " crush";
		};
		// The stamina bar has no 0 A.D. counterpart.
		show_bar(stamina_bar_, stamina_label_, false);
		hide_resource_bar();
		garrison_hp_bar_->set_visible(false); // fill_garrison_grid shows it
		clear_tooltips();
		stats_->set_position(Vector2(102, 108));
		stats_->set_size(Vector2(404, 38));
		stats_->set_autowrap_mode(TextServer::AUTOWRAP_OFF);
		// Each grid shows if a branch below asks for it; the guard applies the
		// result once, whichever way update_inspector returns.
		bool show_selection_grid = false, show_garrison_grid = false;
		struct GridVisibility {
			RtsoSelectionGrid &sel, &gar;
			bool &show_sel, &show_gar;
			~GridVisibility() {
				sel.set_visible(show_sel);
				gar.set_visible(show_gar);
			}
		} grid_visibility{selection_grid_, garrison_grid_, show_selection_grid, show_garrison_grid};

		// selection_details.js displayMultiple: several structures, or structures with units.
		if (const std::vector<uint32_t> structs = host_->ui_selected_structures();
				!structs.empty() && structs.size() + (size_t)selected_ids.size() > 1) {
			fill_multiple(*sim, structs);
			show_selection_grid = true;
			panel_->set_visible(true);
			return;
		}

		if (selected_building_id != 0) {
			const rtso::Simulation::Building *found_b = nullptr;
			for (const auto &b : sim->buildings()) {
				if (b.id == selected_building_id) { found_b = &b; break; }
			}

			if (found_b) {
				const auto &binfo = rtso::building_type_info(found_b->type);
				portrait_->set_texture(rtso::portraits::structure(found_b->civ, found_b->type));
				title_->set_text(structure_name(found_b->civ, found_b->type, binfo.name));
				const uint8_t owner = found_b->team;
				set_owner(*sim, owner, found_b->garrison_cap > 0
						? " - Garrison: " + String::num_int64(found_b->garrisoned_units.size()) + " / " + String::num_int64(found_b->garrison_cap)
						: String());

				set_bar(hp_bar_, hp_label_, found_b->hp, found_b->max_hp, rtso::seldet::health(found_b->hp, found_b->max_hp));
				set_structure_extras(*sim, *found_b);

				if (found_b->max_capture_points > 0.0f) {
					set_bar(capture_bar_, capture_label_, found_b->capture_points, found_b->max_capture_points,
							rtso::seldet::capture(found_b->capture_points, found_b->max_capture_points));
					set_structure_capture_parts(*sim, *found_b);
				} else
					show_bar(capture_bar_, capture_label_, false);

				rtso::Simulation::BuildWork work;
				const bool building_work = sim->structure_build_work(*found_b, work);
				if (found_b->build_progress < 1.0f) {
					set_bar(xp_bar_, xp_label_, found_b->build_progress, 1.0f,
							join_spaced("Construction: " + String::num((double)(found_b->build_progress * 100.0f), 0) + "%",
									rtso::seldet::build_work_caption(work.builders, work.remaining)));
					if (building_work) xp_bar_->set_tooltip_text(build_work_tooltip(work));
				} else if (found_b->upgrade_to != rtso::BLD_TYPE_COUNT && found_b->upgrade_duration > 0.0f) {
					set_bar(xp_bar_, xp_label_, found_b->upgrade_timer, found_b->upgrade_duration,
							"Upgrading to " + String(rtso::building_type_name(found_b->upgrade_to)) + " (" +
							String::num(found_b->upgrade_duration - found_b->upgrade_timer, 0) + " s left)");
				} else if (const uint8_t rt = rtso::Simulation::researching(*found_b); rt != rtso::TECH_NONE && found_b->queue.front().duration > 0.0f) {
					const rtso::ProductionOrder &po = found_b->queue.front();
					set_bar(xp_bar_, xp_label_, po.timer, po.duration,
							"Researching " + String::utf8(rtso::tech_info(rt).name) + " (" +
							String::num(po.duration - po.timer, 0) + " s left)");
				} else if (building_work && work.repair) {
					// selection_details.js: a damaged structure shows its repairers and the time left.
					set_bar(xp_bar_, xp_label_, found_b->hp, found_b->max_hp,
							join_spaced(rtso::tr0("Repair"), rtso::seldet::build_work_caption(work.builders, work.remaining)));
					xp_bar_->set_tooltip_text(build_work_tooltip(work));
				} else {
					show_bar(xp_bar_, xp_label_, false);
				}
				show_bar(aura_bar_, aura_label_, false);

				String status;
				if (rtso::is_gate_type(found_b->type))
					status = found_b->gate_locked ? "Gate: locked" : (found_b->gate_open ? "Gate: open" : "Gate: closed");
				else if (!found_b->queue.empty())
					status = "Production: " + String::num_int64(found_b->queue.size()) + " / " + String::num_int64((int64_t)rtso::PRODUCTION_QUEUE_MAX) +
							" in queue" + (found_b->autoqueue ? " (autoqueue on)" : "") + " (click a slot to cancel)";
				if (const rtso::Simulation::StructureDetails sd = sim->structure_details(*found_b); sd.shoots)
					status += String(status.is_empty() ? "" : "\n") + "Attack: " + String::num_int64(sd.arrows) + " arrows of " +
							String::num(sd.damage[1], 0) + " pierce every " + String::num(sd.repeat_time, 1) + " s, range " +
							String::num(sd.max_range * rtso::AttackDetection::kMetresPerUnit, 0) + " m";
				// A field's resourceSection: "Food: ∞" and its gatherers.
				stats_->set_text(rtso::seldet::join_lines({status, supply_lines(*sim, sim->field_resource_id(found_b->id))}));
				// Beside the Barter panel when it shows.
				const bool barter = barter_host_ && barter_host_->is_visible();
				if (barter) {
					stats_->set_position(Vector2(318, 108));
					stats_->set_size(Vector2(188, 38));
					stats_->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
				}
				stats_->set_tooltip_text(structure_tooltip(*sim, *found_b));
				// Animals penned in a corral (GarrisonHolder List Animal) unload with the same button.
				const int penned = found_b->type == rtso::BLD_CORRAL ? sim->corral_count(found_b->id) : 0;
				String extra = found_b->has_rally_point ? "Rally point set" : "";
				if (penned > 0)
					extra += String(extra.is_empty() ? "" : "\n") + "Penned animals: " + String::num_int64(penned) + " / " +
							String::num_int64(rtso::CORRAL_GARRISON_MAX);
				extra_->set_text(extra);

				// Garrison panel; U unloads all we may (Commands.js unload / unload-all-by-owner).
				const std::vector<uint32_t> mine = sim->unloadable_by(host_->local_player(), found_b->id);
				show_garrison_grid = fill_garrison_grid(*sim, found_b->id);
				ungarrison_btn_->set_text("Unload all (" + String(rtso::hotkeys::table().label("session.unload").c_str()) + ")");
				ungarrison_btn_->set_visible(!mine.empty() || (penned > 0 && owner == host_->local_player()));
				panel_->set_visible(true);
				return;
			}
		}

		if (selected_ids.is_empty()) {
			ungarrison_btn_->set_visible(false);
			if (kind_ != INSPECT_NONE && !fill_inspected_details()) kind_ = INSPECT_NONE;
			panel_->set_visible(kind_ != INSPECT_NONE);
			return;
		}
		if (fill_livestock_details(show_selection_grid)) {
			ungarrison_btn_->set_visible(false);
			panel_->set_visible(true);
			return;
		}
		auto uview = sim->units();
		const uint32_t *ids = (const uint32_t *)selected_ids.ptr();
		const auto groups = RtsoSelectionGrid::group_units(uview, ids, selected_ids.size());
		float hp_sum = 0, hp_max_sum = 0;
		for (const auto &g : groups) {
			hp_sum += g.hp;
			hp_max_sum += g.max_hp;
		}
		set_bar(hp_bar_, hp_label_, hp_sum, hp_max_sum, rtso::seldet::health(hp_sum, hp_max_sum));
		show_bar(capture_bar_, capture_label_, false);

		if (selected_ids.size() == 1 && !groups.empty()) {
			size_t u = 0;
			for (; u < uview.count; ++u)
				if (uview.id[u] == (uint32_t)selected_ids[0]) break;
			if (u == uview.count) {
				panel_->set_visible(false);
				return;
			}
			const uint8_t utype = uview.unit_type[u];
			const uint16_t tmpl = uview.tmpl ? uview.tmpl[u] : rtso::CIV_UNIT_TEMPLATE_NONE;
			const auto &uinfo = tmpl != rtso::CIV_UNIT_TEMPLATE_NONE ? rtso::civ_template_unit_info(tmpl, utype) : rtso::unit_type_info(utype);
			portrait_->set_texture(rtso::portraits::unit(tmpl, utype));

			// 0 A.D. ranks: Basic, Advanced, Elite (Promotion.js).
			const String name = unit_name(uview, u, uinfo.name);
			title_->set_text(name);
			const uint8_t owner = uview.team[u];
			set_owner(*sim, owner, " - Kills: " + String::num_int64(uview.kills[u]));

			// Experience toward the next rank (Promotion.js GetRequiredXp after technologies).
			if (const float need = sim->promotion_required_xp(u); need > 0.0f) {
				set_bar(xp_bar_, xp_label_, (float)uview.xp[u], need, rtso::seldet::experience(uview.xp[u], need));
			} else {
				show_bar(xp_bar_, xp_label_, false);
			}

			String auras;
			if (tmpl != rtso::CIV_UNIT_TEMPLATE_NONE) {
				for (int16_t a : rtso::civ_unit_template(tmpl).auras) {
					if (a < 0) continue;
					if (!auras.is_empty()) auras += ", ";
					auras += String::utf8(rtso::civ_hero_aura(a).name);
				}
			}
			if (!auras.is_empty()) set_bar(aura_bar_, aura_label_, 1.0f, 1.0f, "Auras: " + auras);
			else show_bar(aura_bar_, aura_label_, false);

			fill_unit_details(*sim, u, name);
			set_unit_extras(*sim, u);

			String extra;
			uint32_t route_a = 0, route_b = 0;
			int route_leg = 0;
			rtso::Simulation::TradeGain g;
			if (rtso::unit_is_trader(uview.unit_type[u]) && sim->trader_route(uview.id[u], route_a, route_b, route_leg) &&
					sim->trader_goods(uview.id[u], g)) {
				// selection_details.js: a trader shows its goods and their total gain only while it
				// carries goods (trader.goods.amount), heading to market a on leg 0, else b.
				const rtso::Simulation::Building *ma = nullptr, *mb = nullptr;
				for (const auto &bb : sim->buildings()) {
					if (bb.id == (route_leg == 0 ? route_b : route_a)) ma = &bb;
					if (bb.id == (route_leg == 0 ? route_a : route_b)) mb = &bb;
				}
				if (ma && mb) {
					// resourceCarrying for traders: the goods and the total gain, "Gain: 30 (Alice), 5 (Bob)"
					// as the tooltip (getTradingTooltip: market1 pays the arrival market's owner, market2
					// the starting market's).
					const uint8_t goods = uview.res_type_carried[u];
					extra = rtso::seldet::resource_name(goods) + " " + String::num_int64(g.trader + g.market1 + g.market2);
					const uint8_t owner = uview.team[u];
					const struct { int gain; uint8_t owner; } markets[] = {{g.market1, mb->team}, {g.market2, ma->team}};
					String primary = String::num_int64(g.trader);
					for (const auto &m : markets)
						if (m.gain && m.owner == owner) primary += rtso::tr0("+") + String::num_int64(m.gain);
					auto gain_of = [&](const String &gain, uint8_t pid) {
						return rtso::sprintf0(rtso::tr0("%(gain)s (%(player)s)"), {{"gain", gain}, {"player", host_->player_display_name(pid)}});
					};
					String gains = gain_of(primary, owner);
					for (const auto &m : markets)
						if (m.gain && m.owner != owner) gains += rtso::tr0(", ") + gain_of(String::num_int64(m.gain), m.owner);
					extra_->set_tooltip_text(rtso::sprintf0(rtso::tr0("Gain: %(gain)s"), {{"gain", gains}}));
				}
			} else if (!rtso::unit_is_trader(uview.unit_type[u]) && uview.carry[u] > 0 && uview.res_type_carried[u] < rtso::RES_COUNT) {
				// resourceCarrying: the resource and "amount / max".
				extra = rtso::seldet::resource_name(uview.res_type_carried[u]) + " " +
						rtso::seldet::amount_of(uview.carry[u], sim->unit_details(u).carry_max);
			}
			if (sim->is_transport((uint32_t)selected_ids[0])) {
				const size_t p_count = sim->get_transport_passenger_count((uint32_t)selected_ids[0]);
				extra = "Passengers: " + String::num_int64(p_count) + " / " + String::num_int64(sim->get_transport_capacity((uint32_t)selected_ids[0]));
				ungarrison_btn_->set_text("Unload all (" + String(rtso::hotkeys::table().label("session.unload").c_str()) + ")");
				ungarrison_btn_->set_visible(!sim->unloadable_by(host_->local_player(), (uint32_t)selected_ids[0]).empty());
				show_garrison_grid = fill_garrison_grid(*sim, (uint32_t)selected_ids[0]);
			} else {
				ungarrison_btn_->set_visible(false);
			}
			extra_->set_text(extra);
		} else {
			portrait_->set_texture(groups.empty() ? Ref<Texture2D>()
					: rtso::portraits::unit(groups.front().tmpl, groups.front().unit_type));
			title_->set_text(String::num_int64(selected_ids.size()) + " units selected");
			// displayMultiple's numberOfUnits tooltip: garrison size, carried resources, loot.
			{
				rtso::seldet::Amounts carrying, loot;
				int64_t garrison_size = 0;
				for (int s = 0; s < selected_ids.size(); ++s) {
					size_t i = 0;
					for (; i < uview.count; ++i)
						if (uview.id[i] == (uint32_t)selected_ids[s]) break;
					if (i == uview.count || !uview.alive[i]) continue;
					// Garrisonable.size (ships have none) plus a holder's GarrisonHolder.occupiedSlots.
					garrison_size += (rtso::unit_is_ship(uview.unit_type[i]) ? 0 : 1) + (int64_t)sim->get_transport_passenger_count(uview.id[i]);
					const rtso::LootInfo l = sim->unit_details(i).loot;
					loot.res[rtso::RES_FOOD] += l.food;
					loot.res[rtso::RES_WOOD] += l.wood;
					loot.res[rtso::RES_STONE] += l.stone;
					loot.res[rtso::RES_GOLD] += l.metal;
					loot.xp += l.xp;
					if (uview.carry[i] > 0 && uview.res_type_carried[i] < rtso::RES_COUNT) {
						carrying.res[uview.res_type_carried[i]] += uview.carry[i];
						loot.res[uview.res_type_carried[i]] += uview.carry[i];
					}
				}
				title_->set_tooltip_text(rtso::seldet::multiple_tooltip(garrison_size, carrying, loot));
			}
			clear_owner();
			class_->set_text(String::num_int64(groups.size()) + (groups.size() == 1 ? " type" : " types"));
			show_bar(xp_bar_, xp_label_, false);
			show_bar(aura_bar_, aura_label_, false);
			stats_->set_text("");
			extra_->set_text("");
			ungarrison_btn_->set_visible(false);
			selection_grid_.update(selection_groups(*sim, uview, groups), "");
			show_selection_grid = true;
		}
		panel_->set_visible(true);
	}

private:
	void clear_tooltips() {
		for (godot::Control *c : std::initializer_list<godot::Control *>{portrait_, title_, stats_, extra_, aura_label_, xp_bar_}) c->set_tooltip_text("");
		rank_icon_->set_visible(false);
		rank_icon_->set_tooltip_text("");
		for (godot::ColorRect *r : capture_parts_) r->set_visible(false);
		for (godot::TextureRect *icon : status_icons_) icon->set_visible(false);
		set_portrait_target(false, 0, 0.0f, 0.0f, String(), -1, 0);
	}

	// The capture bar's parts: pts[p] for each player p, pts[MAX_PLAYERS] for Gaia; the owner first.
	void set_capture_parts(const rtso::Simulation &sim, uint8_t owner, const float *pts, float max) {
		capture_bar_->set_value(0.0f);
		const float w = capture_bar_->get_size().x, h = capture_bar_->get_size().y;
		const int owner_slot = owner < rtso::MAX_PLAYERS ? owner : rtso::MAX_PLAYERS;
		float x = 0.0f;
		int used = 0;
		auto part = [&](int slot) {
			const float frac = max > 0.0f ? std::clamp(pts[slot] / max, 0.0f, 1.0f) : 0.0f;
			if (frac <= 0.0f || used >= kCaptureParts) return;
			const uint32_t rgb = slot < rtso::MAX_PLAYERS ? rtso::display_colors::rgb(sim, (uint8_t)slot) : sim.player_color(rtso::GAIA_PLAYER);
			godot::ColorRect *r = capture_parts_[used++];
			r->set_color(godot::Color(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, 128.0f / 255.0f));
			r->set_position(godot::Vector2(x, 0.0f));
			r->set_size(godot::Vector2(w * frac, h));
			r->set_visible(true);
			x += w * frac;
		};
		part(owner_slot);
		for (int slot = 0; slot <= rtso::MAX_PLAYERS; ++slot)
			if (slot != owner_slot) part(slot);
		for (int i = used; i < kCaptureParts; ++i) capture_parts_[i]->set_visible(false);
	}
	// Capturable.js GetCapturePoints of a structure. A Gaia structure keeps its points in slot 0.
	void set_structure_capture_parts(const rtso::Simulation &sim, const rtso::Simulation::Building &b) {
		float pts[rtso::MAX_PLAYERS + 1] = {};
		for (int p = 0; p < rtso::MAX_PLAYERS; ++p) pts[p] = b.capture_points_by_team[p];
		if (b.team >= rtso::MAX_PLAYERS) {
			pts[rtso::MAX_PLAYERS] = b.capture_points;
			pts[0] = 0.0f;
		}
		set_capture_parts(sim, b.team, pts, b.max_capture_points);
	}

	// selection_details.js resourceSection of a ResourceSupply (a tree, a mine, a field): "Wood:" and
	// amount / max (∞ for a field), then its gatherers, current / max. Empty without a supply.
	void hide_resource_bar() {
		for (godot::Control *c : std::initializer_list<godot::Control *>{resource_name_, resource_bar_, resource_stats_}) c->set_visible(false);
	}
	// resourceSection: the bar of `res` with "amount / max", full with "∞" when infinite, in the
	// first bar row below the health bar left free (capture and the build/repair row come first).
	void show_resource_bar(uint8_t res, int64_t amount, int64_t max, bool infinite) {
		using namespace godot;
		float y = 54.0f;
		if (capture_bar_->is_visible()) y = 67.0f;
		if (y == 67.0f && xp_bar_->is_visible()) y = 80.0f;
		resource_name_->set_position(Vector2(102, y - 2));
		resource_name_->set_text(rtso::seldet::resource_label(res));
		resource_bar_->set_position(Vector2(148, y));
		resource_bar_->set_max(1.0);
		resource_bar_->set_value(infinite ? 1.0 : max > 0 ? std::clamp((double)amount / (double)max, 0.0, 1.0) : 0.0);
		resource_stats_->set_position(Vector2(148, y - 1));
		resource_stats_->set_text(rtso::seldet::resource_amount(amount, max, infinite));
		for (Control *c : std::initializer_list<Control *>{resource_name_, resource_bar_, resource_stats_}) c->set_visible(true);
	}
	// A resource's (or a field's) supply as the resource bar, and its gatherers, current / max,
	// as the returned line (resourceCarryingText, "Current/max gatherers").
	String supply_lines(const rtso::Simulation &sim, uint32_t res_id) {
		if (!res_id) return String();
		auto rv = sim.resources();
		size_t r = 0;
		for (; r < rv.count; ++r)
			if (rv.id[r] == res_id) break;
		if (r == rv.count || !rv.alive[r]) return String();
		int gatherers = 0;
		auto uv = sim.units();
		for (size_t i = 0; i < uv.count; ++i)
			if (uv.alive[i] && uv.target_resource_id[i] == res_id && uv.action_state[i] == rtso::ACTION_GATHER) ++gatherers;
		show_resource_bar(rv.type[r], rv.amount[r], sim.resource_supply_max(res_id), rv.amount[r] >= rtso::FIELD_FOOD_SUPPLY);
		return rtso::seldet::amount_of(gatherers, sim.resource_max_gatherers(res_id));
	}

	// The primary and secondary names (getEntityNames): a civ's structure from its template
	// ("Agora (Civic Center)"), else the RTS0 name.
	static String structure_name(uint8_t civ, uint8_t type, const char *fallback) {
		const godot::Dictionary e = rtso::portraits::structure_entry(civ, type);
		const String generic = e.get("generic_name", String());
		if (generic.is_empty()) return String(fallback);
		return rtso::entity_names(generic, e.get("specific_name", String()));
	}
	// A unit's names, with " — Packed" on a packed siege engine (selection_details.js).
	static String unit_name(const rtso::Simulation::UnitView &uv, size_t u, const char *generic) {
		const uint16_t tmpl = uv.tmpl ? uv.tmpl[u] : rtso::CIV_UNIT_TEMPLATE_NONE;
		const char *specific = tmpl != rtso::CIV_UNIT_TEMPLATE_NONE ? rtso::civ_unit_template(tmpl).specific_name : nullptr;
		return rtso::entity_names(String::utf8(generic), String::utf8(specific ? specific : ""),
				uv.pack_state && uv.pack_state[u] == rtso::PACK_PACKED);
	}

	static String join_spaced(const String &a, const String &b) { return b.is_empty() ? a : a + String("  ") + b; }
	static String build_work_tooltip(const rtso::Simulation::BuildWork &w) {
		return rtso::seldet::build_work_tooltip(w.repair, w.builders, w.remaining, w.remaining_new);
	}

	// What the portrait's clicks act on: follow_focus(id) for a unit or structure, else the
	// position to look at; the Template Viewer's kind ("unit", "structure"), id and civ.
	void set_portrait_target(bool unit, uint32_t follow_id, float x, float z, const String &viewer_kind, int viewer_id, int viewer_civ) {
		portrait_unit_ = unit;
		portrait_follow_id_ = follow_id;
		portrait_x_ = x;
		portrait_z_ = z;
		portrait_viewer_kind_ = viewer_kind;
		portrait_viewer_id_ = viewer_id;
		portrait_viewer_civ_ = viewer_civ;
	}

	// selection_details.js iconBorder tooltip: the name, getVisibleEntityClassesFormatted,
	// getAurasTooltip, getEntityTooltip, getTreasureTooltip, then what the clicks do.
	static String portrait_tooltip(const String &name, const godot::Dictionary &entry, const String &auras, const String &reward, bool unit) {
		using namespace rtso::seldet;
		const String tooltip = entry.get("tooltip", String());
		return join_lines({name, classes_line(entry.get("classes", godot::Array())), auras,
				tooltip.is_empty() ? String() : rtso::tr0(tooltip), reward, portrait_click_line(unit)});
	}

	// The rank glyph: "%(rank)s Rank" and the rank technology's tooltip (unit_advanced,
	// unit_elite), for templates with an Identity/Rank.
	void set_rank(const godot::Dictionary &entry, uint8_t rank) {
		const godot::Dictionary viewer = entry.get("viewer", godot::Dictionary());
		String name = viewer.get("rank", String());
		if (name.is_empty()) return;
		static const char *const kRank[] = {"Basic", "Advanced", "Elite"};
		if (rank >= 1) name = kRank[std::min<int>(rank, 3) - 1];
		rank_icon_->set_texture(rtso::load_ui_texture("res://assets/textures/ui/archive/icons/ranks/" + name + ".png"));
		const godot::Dictionary techs = rtso::portraits::reference().get("techs", godot::Dictionary());
		const String tech_tip = godot::Dictionary(techs.get("unit_" + name.to_lower(), godot::Dictionary())).get("tooltip", String());
		rank_icon_->set_tooltip_text(rtso::sprintf0(rtso::tr0("%(rank)s Rank"), {{"rank", rtso::tr0(name)}}) +
				(tech_tip.is_empty() ? String() : "\n" + rtso::tr0(tech_tip)));
		rank_icon_->set_visible(rank_icon_->get_texture().is_valid());
	}

	// The statusEffectsIcons: one per stacked status, its icon and receiver tooltip.
	void set_statuses(const rtso::Simulation &sim, uint8_t target_kind, uint32_t id) {
		rtso::Simulation::StatusView st[kStatusIcons];
		const int n = std::min(sim.statuses_on(target_kind, id, st, kStatusIcons), kStatusIcons);
		for (int i = 0; i < kStatusIcons; ++i) {
			status_icons_[i]->set_visible(i < n);
			if (i >= n) continue;
			status_icons_[i]->set_texture(rtso::load_ui_texture(String("res://assets/textures/ui/archive/icons/status_effects/") +
					(st[i].kind == 2 ? "burning" : "poisoned") + ".png"));
			status_icons_[i]->set_tooltip_text(rtso::seldet::status_tooltip(st[i].kind, st[i].damage, st[i].interval, st[i].remaining));
		}
	}

	// A single unit's rank, statuses, portrait tooltip and clicks.
	void set_unit_extras(const rtso::Simulation &sim, size_t u) {
		auto uv = sim.units();
		const uint16_t tmpl = uv.tmpl ? uv.tmpl[u] : rtso::CIV_UNIT_TEMPLATE_NONE;
		const godot::Dictionary entry = rtso::portraits::unit_entry(tmpl);
		set_rank(entry, uv.rank[u]);
		set_statuses(sim, rtso::Simulation::STATUS_ON_UNIT, uv.id[u]);
		set_portrait_target(true, uv.id[u], uv.pos_x[u], uv.pos_z[u], tmpl != rtso::CIV_UNIT_TEMPLATE_NONE ? "unit" : "", tmpl, 0);
		// captureSection for any entity with capturePoints: a relic (catafalque) shows its points
		// per player, the owner's first, and "Capture points: n / 250".
		if (uv.unit_type[u] == rtso::UNIT_CATAFALQUE)
			for (const auto &r : sim.relics()) {
				if (r.id != uv.id[u]) continue;
				const uint8_t owner = uv.team[u];
				const float max = (float)rtso::RELIC_CAPTURE_POINTS;
				capture_bar_->set_visible(true);
				capture_label_->set_visible(true);
				capture_bar_->set_max(max);
				capture_label_->set_text(rtso::seldet::capture(r.cp[owner < rtso::MAX_PLAYERS ? owner : rtso::MAX_PLAYERS], max));
				set_capture_parts(sim, owner, r.cp.data(), max);
				break;
			}
		portrait_->set_tooltip_text(portrait_tooltip(title_->get_text(), entry, aura_label_->get_tooltip_text(), String(), true));
	}

	// A single structure's statuses, portrait tooltip and clicks.
	void set_structure_extras(const rtso::Simulation &sim, const rtso::Simulation::Building &b) {
		set_statuses(sim, rtso::Simulation::STATUS_ON_STRUCTURE, b.id);
		const uint8_t civ = b.team < rtso::MAX_PLAYERS ? sim.player_civ(b.team) : sim.player_civ(host_->local_player());
		set_portrait_target(false, b.id, b.x, b.z, "structure", b.type, civ);
		portrait_->set_tooltip_text(portrait_tooltip(title_->get_text(), rtso::portraits::structure_entry(b.civ, b.type), String(), String(), false));
	}
	// A "%(label)s\n%(details)s" tooltip's details, unindented, for the stats lines.
	static String body_lines(const String &tip) {
		const int nl = tip.find("\n");
		if (nl < 0) return tip;
		String out;
		for (const String &line : tip.substr(nl + 1).split("\n")) {
			if (line.begins_with("    ")) continue; // an attack's status and splash lines stay in the tooltip
			out += String(out.is_empty() ? "" : "\n") + line.strip_edges();
		}
		return out;
	}
	// displaySingle for a unit: the attack and resistance lines, the detailed tooltip
	// (getAttackTooltip, getHealerTooltip, getResistanceTooltip, getGatherTooltip,
	// getSpeedTooltip, getGarrisonTooltip, getPopulationBonusTooltip, getLootTooltip) and the portrait's (name, then getAurasTooltip).
	void fill_unit_details(const rtso::Simulation &sim, size_t u, const String &name) {
		using namespace rtso::seldet;
		const rtso::Simulation::UnitDetails d = sim.unit_details(u);
		auto uv0 = sim.units();
		const godot::Dictionary viewer = rtso::portraits::unit_entry(uv0.tmpl ? uv0.tmpl[u] : rtso::CIV_UNIT_TEMPLATE_NONE).get("viewer", godot::Dictionary());
		// GuiInterface elevationAdaptedRange: for a unit (UnitAI) the Ranged reach in front of it,
		// angle 0, not averaged round it as for a structure.
		float elevated = 0.0f;
		if (d.ranged) {
			float hx = uv0.heading_x ? uv0.heading_x[u] : 0.0f, hz = uv0.heading_z ? uv0.heading_z[u] : 1.0f;
			const float len = std::sqrt(hx * hx + hz * hz);
			if (len > 1e-4f) { hx /= len; hz /= len; } else { hx = 0.0f; hz = 1.0f; }
			elevated = sim.elevation_range_toward(uv0.pos_x[u], uv0.pos_z[u], hx, hz, sim.unit_attack_range_at(u), 0.0f) /
					rtso::COMBAT_DISTANCE_SCALE;
		}
		const String attack = attack_tooltip(d, viewer.get("attacks", godot::Array()), elevated), resistance = resistance_tooltip(d.resistance,
				rtso::unit_is_ship(uv0.unit_type[u]) || sim.unit_combat_at(u).classes.has(rtso::CC_SIEGE));
		stats_->set_text(join_lines({body_lines(attack), body_lines(resistance)}));
		auto uv = sim.units();
		Amounts loot;
		loot.res[rtso::RES_FOOD] = d.loot.food;
		loot.res[rtso::RES_WOOD] = d.loot.wood;
		loot.res[rtso::RES_STONE] = d.loot.stone;
		loot.res[rtso::RES_GOLD] = d.loot.metal;
		loot.xp = d.loot.xp;
		if (uv.carry[u] > 0 && uv.res_type_carried[u] < rtso::RES_COUNT) loot.res[uv.res_type_carried[u]] += uv.carry[u];
		// getGarrisonTooltip for a ship, ram or siege tower (its Garrison Limit; a ship is not
		// Garrisonable) and getPopulationBonusTooltip.
		const int passengers = (int)sim.get_transport_passenger_count(uv.id[u]);
		const String garrison = join_lines({garrison_limit_tooltip(sim.get_transport_capacity(uv.id[u])),
				rtso::unit_is_ship(uv.unit_type[u]) ? String() : garrison_size_tooltip(1, passengers)});
		const uint16_t pop_tmpl = uv.tmpl ? uv.tmpl[u] : rtso::CIV_UNIT_TEMPLATE_NONE;
		const int pop_bonus = pop_tmpl < rtso::CIV_UNIT_TEMPLATE_COUNT ? rtso::unit_pop_bonus(pop_tmpl) : 0;
		// getProjectilesTooltip for a siege tower's BuildingAI (DefaultArrowCount 0,
		// GarrisonArrowMultiplier 1, MaxArrowCount 10): min(max, default + per unit x capacity).
		const String projectiles = sim.is_siege_tower_at(u)
				? projectiles_tooltip(std::min(rtso::SIEGE_TOWER_MAX_ARROWS, sim.get_transport_capacity(uv.id[u])), 0, 1.0f) : String();
		stats_->set_tooltip_text(join_lines({attack, healer_tooltip(d), resistance, gather_tooltip(d), speed_tooltip(d),
				garrison, population_bonus_tooltip(pop_bonus), projectiles, loot_tooltip(loot)}));
		String auras;
		const uint16_t tmpl = uv.tmpl ? uv.tmpl[u] : rtso::CIV_UNIT_TEMPLATE_NONE;
		if (tmpl != rtso::CIV_UNIT_TEMPLATE_NONE)
			for (int16_t a : rtso::civ_unit_template(tmpl).auras) {
				if (a < 0) continue;
				const auto &aura = rtso::civ_hero_aura(a);
				auras = join_lines({auras, aura_line(String::utf8(aura.name), String::utf8(aura.description ? aura.description : ""), aura.radius)});
			}
		portrait_->set_tooltip_text(join_lines({name, auras}));
		aura_label_->set_tooltip_text(auras);
	}
	// displaySingle's detailed tooltip for a structure: its arrows (getAttackTooltip with
	// BuildingAI), resistance and garrison limit (getGarrisonTooltip).
	static String structure_tooltip(const rtso::Simulation &sim, const rtso::Simulation::Building &b) {
		using namespace rtso::seldet;
		// displaySingle's detailedTooltip: attack, resistance, garrison, turrets, population
		// bonus, projectiles, resource trickle and loot.
		float res[3];
		sim.structure_resistance(b, res);
		const rtso::Simulation::StructureDetails d = sim.structure_details(b);
		constexpr float kM = rtso::AttackDetection::kMetresPerUnit;
		// GuiInterface elevationAdaptedRange for BuildingAI: the reach from its arrows' origin there.
		const float elevated = d.shoots ? sim.average_elevation_range(b.x, b.z, d.max_range,
				rtso::civ_building_ai_info(b.civ, b.type).origin_y) * kM : 0.0f;
		const String attack = d.shoots ? structure_attack_tooltip(d.damage, d.min_range * kM, d.max_range * kM, d.repeat_time,
				d.arrows ? d.arrows : d.default_arrows, elevated) : String();
		const rtso::LootInfo l = rtso::structure_loot(b.type, b.civ, b.half_size);
		Amounts loot;
		loot.res[rtso::RES_FOOD] = l.food;
		loot.res[rtso::RES_WOOD] = l.wood;
		loot.res[rtso::RES_STONE] = l.stone;
		loot.res[rtso::RES_GOLD] = l.metal;
		loot.xp = l.xp;
		return join_lines({attack, resistance_tooltip(res, true), garrison_limit_tooltip(b.garrison_cap), garrison_heal_tooltip(d.heal),
				turrets_tooltip(d.turrets), population_bonus_tooltip(d.pop_bonus),
				projectiles_tooltip(d.projectile_limit, d.default_arrows, d.per_unit),
				d.trickles ? trickle_tooltip(d.trickle, rtso::Simulation::TRICKLE_STRUCTURE_INTERVAL) : String(), loot_tooltip(loot)});
	}
	static String flora_title(uint8_t flora) {
		static const char *const kNames[] = {"Tree", "Tree", "Pine", "Date Palm", "Oak", "Baobab", "Acacia", "Bamboo", "Bush",
			"Berry Bush", "Apple Tree", "Date Palm", "Carob", "Poplar", "Fan Palm", "Beech", "Dead Tree", "Cypress",
			"Aleppo Pine", "Fir", "Tropical Bush", "Olive", "Banyan", "Tropical Palm", "Maple", "Juniper", "Teak"};
		if (flora >= rtso::FLORA_TEMPLATE_FIRST && flora < rtso::FLORA_COUNT)
			return String(rtso::flora_template_info(flora - rtso::FLORA_TEMPLATE_FIRST).title);
		return flora < sizeof(kNames) / sizeof(kNames[0]) ? String(kNames[flora]) : String("Tree");
	}

	RtsoInspectorHost *host_ = nullptr;
	uint8_t kind_ = INSPECT_NONE;
	uint32_t id_ = 0; // what kind_ inspects
	godot::Panel *panel_ = nullptr;
	godot::TextureRect *portrait_ = nullptr;
	static constexpr int kStatusIcons = 5;
	godot::TextureRect *rank_icon_ = nullptr;
	godot::TextureRect *status_icons_[kStatusIcons] = {};
	bool portrait_unit_ = false;
	uint32_t portrait_follow_id_ = 0;
	float portrait_x_ = 0.0f, portrait_z_ = 0.0f;
	String portrait_viewer_kind_;
	int portrait_viewer_id_ = -1, portrait_viewer_civ_ = 0;
	godot::Label *title_ = nullptr;
	godot::Label *class_ = nullptr;
	godot::ColorRect *owner_bg_ = nullptr;
	godot::TextureRect *owner_civ_ = nullptr, *owner_phase_ = nullptr;

	// The player row of a single selection: the owner's name (and RTS0's suffix: kills, garrison)
	// centred on their colour, their civ emblem and, for the viewer, its mutual allies and
	// observers, their phase (selection_details.js displaySingle).
	void set_owner(const rtso::Simulation &sim, uint8_t owner, const godot::String &suffix = godot::String()) {
		using namespace godot;
		class_->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
		if (owner >= rtso::MAX_PLAYERS) {
			clear_owner();
			class_->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
			class_->set_text(rtso::tr0("Gaia") + suffix);
			return;
		}
		const uint32_t rgb = rtso::display_colors::rgb(sim, owner);
		owner_bg_->set_color(Color(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, 128.0f / 255.0f));
		owner_bg_->set_visible(true);
		class_->add_theme_color_override("font_color", Color(1.0f, 1.0f, 1.0f));
		class_->add_theme_constant_override("outline_size", 2);
		// selection_details.js: a disconnected player's name is prefixed "[OFFLINE]".
		const godot::String owner_name = host_->player_display_name(owner);
		class_->set_text((host_->player_offline(owner)
				? rtso::sprintf0(rtso::tr0("[OFFLINE] %(player)s"), {{"player", owner_name}}) : owner_name) + suffix);
		const uint8_t civ = sim.player_civ(owner);
		owner_civ_->set_texture(civ < rtso::CIV_COUNT ? rtso::load_ui_texture(rtso::civ_info(civ).emblem_path) : Ref<Texture2D>());
		owner_civ_->set_visible(owner_civ_->get_texture().is_valid());
		String tip = civ < rtso::CIV_COUNT ? String(rtso::civ_info(civ).name) : String();
		// Reveal phases to mutual allies and observers.
		const uint8_t vp = host_->view_player();
		const bool reveal = vp >= rtso::MAX_PLAYERS || vp == owner ||
				(sim.get_diplomacy(vp, owner) == rtso::Simulation::DIPLO_ALLY && sim.get_diplomacy(owner, vp) == rtso::Simulation::DIPLO_ALLY);
		const char *phase = "hidden";
		if (reveal) {
			static const char *kPhase[] = {"village", "town", "city"};
			static const char *kName[] = {"Village Phase", "Town Phase", "City Phase"};
			const int p = std::clamp<int>(sim.player_phase(owner), 1, 3) - 1;
			phase = kPhase[p];
			tip += String::utf8(" — ") + rtso::tr0(kName[p]);
		}
		owner_phase_->set_texture(rtso::load_ui_texture(String("res://assets/textures/ui/archive/panel_phase_emblems_") + phase + ".png"));
		owner_phase_->set_visible(true);
		class_->set_tooltip_text(tip);
	}

	// Rows that are not a player's: a group's type count.
	void clear_owner() {
		owner_bg_->set_visible(false);
		owner_civ_->set_visible(false);
		owner_phase_->set_visible(false);
		class_->set_tooltip_text(godot::String());
		class_->set_horizontal_alignment(godot::HORIZONTAL_ALIGNMENT_LEFT);
		class_->add_theme_color_override("font_color", godot::Color(0.7f, 0.75f, 0.85f));
		class_->add_theme_constant_override("outline_size", 0);
	}
	godot::ProgressBar *hp_bar_ = nullptr;
	godot::ProgressBar *garrison_hp_bar_ = nullptr; // healthGarrison
	godot::Label *hp_label_ = nullptr;
	godot::ProgressBar *capture_bar_ = nullptr;
	static constexpr int kCaptureParts = rtso::MAX_PLAYERS + 1;
	godot::ColorRect *capture_parts_[kCaptureParts] = {};
	godot::Label *capture_label_ = nullptr;
	godot::ProgressBar *xp_bar_ = nullptr;
	godot::Label *xp_label_ = nullptr;
	godot::ProgressBar *stamina_bar_ = nullptr;
	godot::Label *stamina_label_ = nullptr;
	godot::Label *resource_name_ = nullptr;   // resourceLabel
	godot::ProgressBar *resource_bar_ = nullptr;
	godot::Label *resource_stats_ = nullptr;  // resourceStats
	godot::ProgressBar *aura_bar_ = nullptr;
	godot::Label *aura_label_ = nullptr;
	godot::Label *stats_ = nullptr;
	godot::Label *extra_ = nullptr;
	godot::Button *ungarrison_btn_ = nullptr;
	rtso::FogMirages mirages_; // the viewer's structures: live in sight, mirages in the fog
	// displayMultiple for a selection holding structures: the groups (units by template, then
	// structures by civ and type) with their counts and summed health.
	void fill_multiple(const rtso::Simulation &sim, const std::vector<uint32_t> &structs) {
		const godot::PackedInt32Array &selected_ids = host_->ui_selected_ids();
		auto uview = sim.units();
		std::vector<RtsoSelectionGrid::Group> groups = selection_groups(sim, uview,
				RtsoSelectionGrid::group_units(uview, (const uint32_t *)selected_ids.ptr(), selected_ids.size()));
		for (uint32_t id : structs)
			for (const auto &b : sim.buildings()) {
				if (b.id != id) continue;
				const int key = RtsoSelectionGrid::structure_key(b.civ, b.type);
				RtsoSelectionGrid::Group *g = nullptr;
				for (auto &e : groups)
					if (e.key == key) g = &e;
				if (!g) {
					RtsoSelectionGrid::Group ng;
					ng.key = key;
					ng.player = b.team;
					ng.icon = rtso::portraits::structure(b.civ, b.type);
					ng.name = structure_name(b.civ, b.type, rtso::building_type_info(b.type).name);
					groups.push_back(ng);
					g = &groups.back();
				}
				++g->count;
				g->hp += b.hp;
				g->max_hp += b.max_hp;
				break;
			}
		float hp = 0.0f, max_hp = 0.0f;
		int64_t total = 0;
		for (auto &g : groups) {
			hp += g.hp;
			max_hp += g.max_hp;
			total += g.count;
			if (RtsoSelectionGrid::is_structure_key(g.key))
				g.tip = g.name + "\n" + rtso::tr0("Click to select only these, Ctrl+click or right-click to deselect them.");
		}
		auto show_bar = [](godot::ProgressBar *bar, godot::Label *lbl, bool v) {
			bar->set_visible(v);
			lbl->set_visible(v);
		};
		show_bar(hp_bar_, hp_label_, true);
		hp_bar_->set_max(max_hp > 0.0f ? max_hp : 1.0f);
		hp_bar_->set_value(hp);
		hp_label_->set_text(rtso::seldet::health(hp, max_hp));
		// captureMultiple: the selected structures' capture points summed per player, the owner's
		// first ("Capture Points: 2500 / 2500"); hidden when none is capturable.
		float pts[rtso::MAX_PLAYERS + 1] = {}, max_cp = 0.0f;
		uint8_t owner = 0xFF;
		for (uint32_t id : structs)
			for (const auto &b : sim.buildings()) {
				if (b.id != id) continue;
				if (b.max_capture_points > 0.0f) {
					owner = b.team; // selection keeps one owner's structures
					max_cp += b.max_capture_points;
					for (int p = 0; p < rtso::MAX_PLAYERS; ++p) pts[p] += b.capture_points_by_team[p];
					if (b.team >= rtso::MAX_PLAYERS) pts[rtso::MAX_PLAYERS] += b.capture_points;
				}
				break;
			}
		show_bar(capture_bar_, capture_label_, max_cp > 0.0f);
		if (max_cp > 0.0f) {
			const float own = pts[owner < rtso::MAX_PLAYERS ? owner : rtso::MAX_PLAYERS];
			capture_bar_->set_max(max_cp);
			capture_label_->set_text(rtso::seldet::capture(own, max_cp));
			set_capture_parts(sim, owner, pts, max_cp);
		}
		portrait_->set_texture(groups.empty() ? godot::Ref<godot::Texture2D>()
				: groups.front().icon.is_valid() ? groups.front().icon : rtso::portraits::unit(groups.front().tmpl, groups.front().unit_type));
		title_->set_text(godot::String::num_int64(total) + " selected");
		clear_owner();
		class_->set_text(godot::String::num_int64(groups.size()) + (groups.size() == 1 ? " type" : " types"));
		show_bar(xp_bar_, xp_label_, false);
		show_bar(aura_bar_, aura_label_, false);
		stats_->set_text("");
		extra_->set_text("");
		ungarrison_btn_->set_visible(false);
		if (groups.size() > RtsoSelectionGrid::kMaxSelectionGroups) groups.resize(RtsoSelectionGrid::kMaxSelectionGroups);
		selection_grid_.update(groups, "");
	}

	RtsoSelectionGrid selection_grid_;
	RtsoSelectionGrid garrison_grid_;
	godot::Control *barter_host_ = nullptr;

	// 0.28's Selection panel (g_SelectionPanels.Selection setupButton): each group's tooltip
	// is its names, what its units carry (a trader its goods' total gain, while it has goods) and, for an
	// observer, the owner, whose colour then shows behind the icon.
	std::vector<RtsoSelectionGrid::Group> selection_groups(const rtso::Simulation &sim,
			const rtso::Simulation::UnitView &uview, std::vector<RtsoSelectionGrid::Group> groups) {
		const PackedInt32Array &selected_ids = host_->ui_selected_ids();
		const bool observer = host_->local_player() >= rtso::MAX_PLAYERS;
		for (auto &g : groups) {
			rtso::seldet::Amounts carried;
			for (int s = 0; s < selected_ids.size(); ++s) {
				size_t i = 0;
				for (; i < uview.count; ++i)
					if (uview.id[i] == (uint32_t)selected_ids[s]) break;
				if (i == uview.count || !uview.alive[i]) continue;
				const uint16_t tmpl = uview.tmpl ? uview.tmpl[i] : rtso::CIV_UNIT_TEMPLATE_NONE;
				if (RtsoSelectionGrid::group_key(uview.unit_type[i], tmpl) != g.key) continue;
				const uint8_t res = uview.res_type_carried[i];
				if (res >= rtso::RES_COUNT) continue;
				// A trader's carry is its goods' total gain (trader + both markets), as 0.28 adds.
				if (uview.carry[i] > 0) carried.res[res] += uview.carry[i];
			}
			const auto &info = g.tmpl != rtso::CIV_UNIT_TEMPLATE_NONE ? rtso::civ_template_unit_info(g.tmpl, g.unit_type) : rtso::unit_type_info(g.unit_type);
			const char *specific = g.tmpl != rtso::CIV_UNIT_TEMPLATE_NONE ? rtso::civ_unit_template(g.tmpl).specific_name : nullptr;
			g.tip = rtso::entity_names(String::utf8(info.name), String::utf8(specific ? specific : ""));
			if (carried.any()) g.tip += "\n" + rtso::seldet::amounts_list(carried, false);
			if (observer) {
				g.tip += "\n" + rtso::sprintf0(rtso::tr0("Player: %(playername)s"), {{"playername", host_->player_display_name(g.player)}});
				const uint32_t rgb = rtso::display_colors::rgb(sim, g.player);
				g.bg = Color(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, 160.0f / 255.0f);
			}
			g.tip += "\n" + rtso::tr0("Click to select only these, Ctrl+click or right-click to deselect them.");
		}
		if (groups.size() > RtsoSelectionGrid::kMaxSelectionGroups) groups.resize(RtsoSelectionGrid::kMaxSelectionGroups);
		return groups;
	}

	// g_SelectionPanels.Garrison for a structure, ship, ram or siege tower: up to 12 icons,
	// one per template and owner, on the owner's colour. A click unloads one (Shift: all of
	// that type) when we own the holder or the units; others' units show disabled and grey
	// (not grey for an observer). False when nothing is inside.
	bool fill_garrison_grid(const rtso::Simulation &sim, uint32_t holder) {
		using namespace godot;
		uint8_t holder_owner = 0xFF;
		const std::vector<uint32_t> inside = sim.holder_contents(holder, &holder_owner);
		if (inside.empty()) return false;
		auto uview = sim.units();
		std::vector<RtsoSelectionGrid::Group> groups = RtsoSelectionGrid::group_units(uview, inside.data(), inside.size(), true);
		// updateGarrisonHealthBar: every garrisoned unit's health summed, hidden at none.
		float hp = 0.0f, max_hp = 0.0f;
		for (const auto &g : groups) {
			hp += g.hp;
			max_hp += g.max_hp;
		}
		garrison_hp_bar_->set_visible(hp > 0.0f);
		if (hp > 0.0f) {
			garrison_hp_bar_->set_max(max_hp > 0.0f ? max_hp : 1.0f);
			garrison_hp_bar_->set_value(hp);
			garrison_hp_bar_->set_tooltip_text(rtso::seldet::health(hp, max_hp));
		}
		if (groups.size() > 12) groups.resize(12);
		const uint8_t me = host_->local_player();
		const bool observer = me >= rtso::MAX_PLAYERS;
		for (auto &g : groups) {
			g.enabled = !observer && (me == holder_owner || me == g.player);
			g.grey = !observer;
			const uint32_t rgb = rtso::display_colors::rgb(sim, g.player);
			g.bg = Color(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, 160.0f / 255.0f);
			const auto &info = g.tmpl != rtso::CIV_UNIT_TEMPLATE_NONE ? rtso::civ_template_unit_info(g.tmpl, g.unit_type) : rtso::unit_type_info(g.unit_type);
			const char *specific = g.tmpl != rtso::CIV_UNIT_TEMPLATE_NONE ? rtso::civ_unit_template(g.tmpl).specific_name : nullptr;
			const String names = rtso::entity_names(String::utf8(info.name), String::utf8(specific ? specific : ""));
			g.tip = (g.enabled ? rtso::sprintf0(rtso::tr0("Unload %(name)s"), {{"name", names}}) + String("\n") +
							rtso::tr0("Single-click to unload 1. Shift-click to unload all of this type.")
						: names) + String("\n") +
					rtso::sprintf0(rtso::tr0("Player: %(playername)s"), {{"playername", host_->player_display_name(g.player)}});
		}
		garrison_grid_.update(groups, "");
		return true;
	}
};

#endif // RTSO_INSPECTOR_VIEW_H
