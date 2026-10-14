#ifndef RTSO_INSPECTOR_VIEW_H
#define RTSO_INSPECTOR_VIEW_H

// P4 stage-2 leaf widget: the bottom inspector (0 A.D. gui/session/selection_details.js
// and selection_panels.js Selection / Garrison). Owns the portrait, title, the five
// bars, the stats lines, the unload button and the selection and garrison grids; shows
// the selected structure, units or livestock, or the foreign entity a click inspected.

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/label.hpp>
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
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "../simulation/simulation.h"
#include "attack_detection.h"
#include "hotkeys.h"
#include "l10n.h"
#include "selection_grid.h"
#include "top_panel.h" // rtso::load_ui_texture

class RtsoInspectorHost {
public:
	virtual ~RtsoInspectorHost() = default;
	virtual rtso::Simulation *ui_sim() = 0;
	virtual uint8_t local_player() const = 0;
	virtual uint8_t view_player() const = 0;
	virtual godot::String player_display_name(uint8_t pid) const = 0;
	virtual uint32_t ui_selected_building_id() const = 0;
	virtual const godot::PackedInt32Array &ui_selected_ids() const = 0;
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

		title_ = memnew(Label);
		title_->set_position(Vector2(102, 6));
		title_->set_size(Vector2(404, 18));
		title_->add_theme_font_size_override("font_size", 13);
		title_->add_theme_color_override("font_color", Color(1.0f, 0.88f, 0.4f));
		panel_->add_child(title_);

		class_ = memnew(Label);
		class_->set_position(Vector2(102, 24));
		class_->set_size(Vector2(404, 15));
		class_->add_theme_font_size_override("font_size", 10);
		class_->add_theme_color_override("font_color", Color(0.7f, 0.75f, 0.85f));
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
		// 3. Veterancy / Experience XP Bar (Gold / Amber)
		make_insp_bar(xp_bar_, xp_label_, 67, Color(0.95f, 0.75f, 0.20f), "XP", "res://assets/textures/ui/icons/insp_experience_fg.png");
		// 4. Stamina / Energy Bar (Teal / Mint)
		make_insp_bar(stamina_bar_, stamina_label_, 80, Color(0.20f, 0.85f, 0.75f), "STA", "res://assets/textures/ui/icons/insp_stamina_fg.png");
		// 5. Aura Influence Bar (Magenta / Violet) - no public.zip session icon, fallback to text glyph badge
		make_insp_bar(aura_bar_, aura_label_, 93, Color(0.85f, 0.35f, 0.85f), "AURA", nullptr);

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

		// Multi-selection and garrison icon grids (0 A.D. selection_panels.js).
		selection_grid_.build(panel_, Vector2(102, 60), Vector2(404, 120), owner, "_on_selection_group_clicked", 10, 36.0f);
		selection_grid_.set_right_click("_on_selection_group_gui_input");
		garrison_grid_.build(panel_, Vector2(102, 148), Vector2(404, 34), owner, "_on_garrison_group_clicked", 12, 30.0f);
	}

	godot::Panel *panel() const { return panel_; }
	RtsoSelectionGrid &selection_grid() { return selection_grid_; }
	// 0 A.D. selects what the player doesn't own too (another player's unit or
	// structure, an animal, a tree or a mine) to show its details, with no commands
	// (EntitySelection; the command panels need the player's own entities).
	uint8_t kind() const { return kind_; }
	uint32_t id() const { return id_; }
	// A selection of the player's own replaces whatever a click inspected.
	void clear() { kind_ = INSPECT_NONE; }

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
		for (const auto &b : sim->buildings()) {
			if (b.team == host_->local_player() || b.wrecked || !explored(b.x, b.z)) continue;
			const float hr = (b.hu > 0.0f ? std::max(b.hu, b.hv) : b.half_size) + 0.4f;
			const float dx = b.x - cx, dz = b.z - cz;
			if (dx * dx + dz * dz <= hr * hr) return pick(INSPECT_BUILDING, b.id);
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
		auto owner_text = [&](uint8_t owner) {
			return owner >= rtso::MAX_PLAYERS ? String("Gaia")
					: host_->player_display_name(owner) + " (" + String(rtso::civ_info(sim->player_civ(owner)).name) + ")";
		};
		auto health = [&](float hp, float max) {
			bar(hp_bar_, hp_label_, max > 0.0f, hp, max,
					"Health: " + String::num((double)hp, 0) + " / " + String::num((double)max, 0));
		};
		bar(capture_bar_, capture_label_, false);
		bar(xp_bar_, xp_label_, false);
		bar(aura_bar_, aura_label_, false);
		extra_->set_text("");

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
			portrait_->set_texture(rtso::load_ui_texture("res://assets/textures/ui/portraits/" + String(rtso::unit_type_portrait(utype)) + ".png"));
			static const char *kRank[] = {"", "", "Advanced ", "Elite "};
			title_->set_text(String(kRank[std::min(std::max(1, (int)uv.rank[u]), 3)]) + String::utf8(uinfo.name));
			class_->set_text(owner_text(uv.team[u]));
			health(uv.hp[u], uv.max_hp[u]);
			String stats;
			if (uinfo.attack_damage > 0.0f) stats = "Attack: " + String::num(uinfo.attack_damage, 1) + ", interval " + String::num(uinfo.attack_cooldown, 2) + " s\n";
			stats_->set_text(stats + inspect_armour(uinfo.hack_armor, uinfo.pierce_armor, uinfo.crush_armor));
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
			portrait_->set_texture(Ref<Texture2D>());
			title_->set_text(String::utf8(fi.name));
			class_->set_text(owner_text(owner));
			health(fv.hp[f], fv.max_hp[f]);
			String stats;
			if (fi.attack_damage() > 0.0f) stats = "Attack: " + String::num(fi.attack_damage(), 1) + ", interval " + String::num(fi.attack_cooldown, 2) + " s\n";
			stats_->set_text(stats + inspect_armour(fi.hack_armor, fi.pierce_armor, fi.crush_armor));
			if (fv.food_amount[f] > 0) extra_->set_text("Food: " + String::num_int64(fv.food_amount[f]));
			return true;
		}
		if (kind_ == INSPECT_BUILDING) {
			const rtso::Simulation::Building *found = nullptr;
			for (const auto &b : sim->buildings())
				if (b.id == id_) { found = &b; break; }
			if (!found || found->wrecked || found->hp <= 0.0f) return false;
			const auto &binfo = rtso::building_type_info(found->type);
			portrait_->set_texture(rtso::load_ui_texture("res://assets/textures/ui/portraits/" + String(binfo.portrait_id) + ".png"));
			title_->set_text(String(binfo.name));
			class_->set_text(owner_text(found->team));
			health(found->hp, found->max_hp);
			if (found->max_capture_points > 0.0f)
				bar(capture_bar_, capture_label_, true, found->capture_points, found->max_capture_points,
						"Capture points: " + String::num((double)found->capture_points, 0) + " / " + String::num((double)found->max_capture_points, 0));
			stats_->set_text(found->build_progress < 1.0f
					? "Construction: " + String::num((double)(found->build_progress * 100.0f), 0) + "%" : String());
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
			portrait_->set_texture(Ref<Texture2D>());
			title_->set_text(String::utf8(ti.name));
			class_->set_text("Gaia");
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
		portrait_->set_texture(Ref<Texture2D>());
		title_->set_text(title);
		class_->set_text("Gaia");
		bar(hp_bar_, hp_label_, false);
		stats_->set_text(kind + ": " + String::num_int64(rv.amount[r]) + "\nGatherers: up to " +
				String::num_int64(sim->resource_max_gatherers(rv.id[r])));
		return true;
	}

	// The player's own selected livestock (0 A.D. livestock are units): one animal's details,
	// or a count for several. False when the selection holds anything else.
	bool fill_livestock_details() {
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
		if (ok && selected_ids.size() > 1) title_->set_text(String::num_int64(selected_ids.size()) + " animals selected");
		return ok;
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

		if (selected_building_id != 0) {
			const rtso::Simulation::Building *found_b = nullptr;
			for (const auto &b : sim->buildings()) {
				if (b.id == selected_building_id) { found_b = &b; break; }
			}

			if (found_b) {
				const auto &binfo = rtso::building_type_info(found_b->type);
				portrait_->set_texture(rtso::load_ui_texture("res://assets/textures/ui/portraits/" + String(binfo.portrait_id) + ".png"));
				title_->set_text(String(binfo.name));
				const uint8_t owner = found_b->team;
				String owner_name = owner == rtso::GAIA_PLAYER ? String("Gaia")
						: host_->player_display_name(owner) + " (" + String(rtso::civ_info(sim->player_civ(owner)).name) + ")";
				class_->set_text(owner_name + (found_b->garrison_cap > 0
						? " - Garrison: " + String::num_int64(found_b->garrisoned_units.size()) + " / " + String::num_int64(found_b->garrison_cap)
						: String()));

				set_bar(hp_bar_, hp_label_, found_b->hp, found_b->max_hp,
						"Health: " + String::num((double)found_b->hp, 0) + " / " + String::num((double)found_b->max_hp, 0));

				if (found_b->max_capture_points > 0.0f)
					set_bar(capture_bar_, capture_label_, found_b->capture_points, found_b->max_capture_points,
							"Capture points: " + String::num((double)found_b->capture_points, 0) + " / " + String::num((double)found_b->max_capture_points, 0));
				else
					show_bar(capture_bar_, capture_label_, false);

				if (found_b->build_progress < 1.0f) {
					set_bar(xp_bar_, xp_label_, found_b->build_progress, 1.0f,
							"Construction: " + String::num((double)(found_b->build_progress * 100.0f), 0) + "%");
				} else if (found_b->upgrade_to != rtso::BLD_TYPE_COUNT && found_b->upgrade_duration > 0.0f) {
					set_bar(xp_bar_, xp_label_, found_b->upgrade_timer, found_b->upgrade_duration,
							"Upgrading to " + String(rtso::building_type_name(found_b->upgrade_to)) + " (" +
							String::num(found_b->upgrade_duration - found_b->upgrade_timer, 0) + " s left)");
				} else if (const uint8_t rt = rtso::Simulation::researching(*found_b); rt != rtso::TECH_NONE && found_b->queue.front().duration > 0.0f) {
					const rtso::ProductionOrder &po = found_b->queue.front();
					set_bar(xp_bar_, xp_label_, po.timer, po.duration,
							"Researching " + String::utf8(rtso::tech_info(rt).name) + " (" +
							String::num(po.duration - po.timer, 0) + " s left)");
				} else {
					show_bar(xp_bar_, xp_label_, false);
				}
				show_bar(aura_bar_, aura_label_, false);

				String status;
				if (rtso::is_gate_type(found_b->type))
					status = found_b->gate_locked ? "Gate: locked" : (found_b->gate_open ? "Gate: open" : "Gate: closed");
				else if (!found_b->queue.empty())
					status = "Production: " + String::num_int64(found_b->queue.size()) + " / " + String::num_int64((int64_t)rtso::PRODUCTION_QUEUE_MAX) +
							" in queue" + (found_b->autoqueue ? " (autoqueue on)" : "") + " (right-click a slot to cancel)";
				if (const rtso::BuildingAIInfo &ai = rtso::building_ai_info(found_b->type); ai.shoots())
					status += String(status.is_empty() ? "" : "\n") + "Attack: " + String::num_int64(sim->building_arrow_count(*found_b)) + " arrows of " +
							String::num(ai.pierce, 0) + " pierce every " + String::num(ai.repeat_time, 1) + " s, range " +
							String::num(sim->building_arrow_range(*found_b) * rtso::AttackDetection::kMetresPerUnit, 0) + " m";
				stats_->set_text(status);
				// Animals penned in a corral (GarrisonHolder List Animal) unload with the same button.
				const int penned = found_b->type == rtso::BLD_CORRAL ? sim->corral_count(found_b->id) : 0;
				String extra = found_b->has_rally_point ? "Rally point set" : "";
				if (penned > 0)
					extra += String(extra.is_empty() ? "" : "\n") + "Penned animals: " + String::num_int64(penned) + " / " +
							String::num_int64(rtso::CORRAL_GARRISON_MAX);
				extra_->set_text(extra);

				// Garrison panel: one icon per unit type inside, click to unload one,
				// Shift+click (session.unloadtype) to unload that type, U unloads all. On an ally's
				// structure only our own units inside (Commands.js unload / unload-all-by-owner).
				const std::vector<uint32_t> mine = sim->unloadable_by(host_->local_player(), found_b->id);
				if (!mine.empty()) {
					auto uview = sim->units();
					garrison_grid_.update(RtsoSelectionGrid::group_units(uview, mine.data(), mine.size()),
							"Click to unload one, Shift+click to unload all of this type.");
					show_garrison_grid = true;
				}
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
		if (fill_livestock_details()) {
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
		set_bar(hp_bar_, hp_label_, hp_sum, hp_max_sum,
				"Health: " + String::num((double)hp_sum, 0) + " / " + String::num((double)hp_max_sum, 0));
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
			portrait_->set_texture(rtso::load_ui_texture("res://assets/textures/ui/portraits/" + String(rtso::unit_type_portrait(utype)) + ".png"));

			// 0 A.D. ranks: Basic, Advanced, Elite (Promotion.js).
			const int rank = std::max(1, (int)uview.rank[u]);
			static const char *kRank[] = {"", "", "Advanced ", "Elite "};
			const char *specific = tmpl != rtso::CIV_UNIT_TEMPLATE_NONE ? rtso::civ_unit_template(tmpl).specific_name : nullptr;
			const String name = String(kRank[std::min(rank, 3)]) +
					rtso::entity_names(String::utf8(uinfo.name), String::utf8(specific ? specific : ""));
			title_->set_text(name);
			const uint8_t owner = uview.team[u];
			class_->set_text(host_->player_display_name(owner) + " (" + String(rtso::civ_info(sim->player_civ(owner)).name) +
					") - Kills: " + String::num_int64(uview.kills[u]));

			// Experience toward the next rank (Promotion.js GetRequiredXp after technologies).
			if (const float need = sim->promotion_required_xp(u); need > 0.0f) {
				set_bar(xp_bar_, xp_label_, (float)uview.xp[u], need,
						"Experience: " + String::num_int64(uview.xp[u]) + " / " + String::num(need, 0));
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

			// Attack and armour (gui/common/tooltips.js getAttackTooltip / getArmorTooltip).
			String stats;
			if (uinfo.attack_damage > 0.0f) {
				String dmg;
				auto add = [&](float v, const char *kind) {
					if (v <= 0.0f) return;
					if (!dmg.is_empty()) dmg += ", ";
					dmg += String::num(v, 1) + " " + kind;
				};
				add(uinfo.hack_damage, "hack");
				add(uinfo.pierce_damage, "pierce");
				add(uinfo.crush_damage, "crush");
				if (dmg.is_empty()) dmg = String::num(uinfo.attack_damage, 1);
				stats = "Attack: " + dmg + ", interval " + String::num(uinfo.attack_cooldown, 2) + " s";
				if (uinfo.attack_range > 2.5f) stats += ", range " + String::num(uinfo.attack_range * rtso::AttackDetection::kMetresPerUnit, 0) + " m";
				stats += "\n";
			}
			stats += armour_text(uinfo.hack_armor, uinfo.pierce_armor, uinfo.crush_armor);
			stats_->set_text(stats);

			static const char *kRes[] = {"wood", "metal", "stone", "food"};
			String extra;
			uint32_t route_a = 0, route_b = 0;
			int route_leg = 0;
			if (rtso::unit_is_trader(uview.unit_type[u]) &&
					sim->trader_route(uview.id[u], route_a, route_b, route_leg)) {
				// selection_details.js: a trader shows its goods and their total gain.
				const rtso::Simulation::Building *ma = nullptr, *mb = nullptr;
				for (const auto &bb : sim->buildings()) {
					if (bb.id == route_a) ma = &bb;
					if (bb.id == route_b) mb = &bb;
				}
				if (ma && mb) {
					const auto g = sim->calculate_trade_gain(*ma, *mb, uview.team[u], uview.unit_type[u]);
					const uint8_t goods = uview.res_type_carried[u];
					extra = "Trading " + String(goods < rtso::RES_COUNT ? kRes[goods] : "goods") + ", gain " +
							String::num_int64(g.trader + g.market1 + g.market2) + " (" + String::num_int64(g.trader) +
							" to you)";
				} else {
					extra = "Trade route lost";
				}
			} else if (uview.carry[u] > 0 && uview.res_type_carried[u] < rtso::RES_COUNT)
				extra = "Carrying " + String::num_int64(uview.carry[u]) + " " + kRes[uview.res_type_carried[u]];
			if (sim->is_transport((uint32_t)selected_ids[0])) {
				const size_t p_count = sim->get_transport_passenger_count((uint32_t)selected_ids[0]);
				extra = "Passengers: " + String::num_int64(p_count) + " / " + String::num_int64(sim->get_transport_capacity((uint32_t)selected_ids[0]));
				ungarrison_btn_->set_text("Unload all (" + String(rtso::hotkeys::table().label("session.unload").c_str()) + ")");
				ungarrison_btn_->set_visible(p_count > 0);
			} else {
				ungarrison_btn_->set_visible(false);
			}
			extra_->set_text(extra);
		} else {
			portrait_->set_texture(groups.empty() ? Ref<Texture2D>()
					: rtso::load_ui_texture("res://assets/textures/ui/portraits/" + String(rtso::unit_type_portrait(groups.front().unit_type)) + ".png"));
			title_->set_text(String::num_int64(selected_ids.size()) + " units selected");
			class_->set_text(String::num_int64(groups.size()) + (groups.size() == 1 ? " type" : " types"));
			show_bar(xp_bar_, xp_label_, false);
			show_bar(aura_bar_, aura_label_, false);
			stats_->set_text("");
			extra_->set_text("");
			ungarrison_btn_->set_visible(false);
			selection_grid_.update(groups, "Click to select only these, Ctrl+click or right-click to deselect them.");
			show_selection_grid = true;
		}
		panel_->set_visible(true);
	}

private:
	static String inspect_armour(float hack, float pierce, float crush) {
		return "Armor: " + String::num(hack, 0) + " hack, " + String::num(pierce, 0) + " pierce, " + String::num(crush, 0) + " crush";
	}
	static String flora_title(uint8_t flora) {
		static const char *const kNames[] = {"Tree", "Tree", "Pine", "Date Palm", "Oak", "Baobab", "Acacia", "Bamboo", "Bush",
			"Berry Bush", "Apple Tree", "Date Palm", "Carob", "Poplar", "Fan Palm", "Beech", "Dead Tree", "Cypress",
			"Aleppo Pine", "Fir", "Tropical Bush", "Olive", "Banyan", "Tropical Palm", "Maple", "Juniper", "Teak"};
		return flora < sizeof(kNames) / sizeof(kNames[0]) ? String(kNames[flora]) : String("Tree");
	}

	RtsoInspectorHost *host_ = nullptr;
	uint8_t kind_ = INSPECT_NONE;
	uint32_t id_ = 0; // what kind_ inspects
	godot::Panel *panel_ = nullptr;
	godot::TextureRect *portrait_ = nullptr;
	godot::Label *title_ = nullptr;
	godot::Label *class_ = nullptr;
	godot::ProgressBar *hp_bar_ = nullptr;
	godot::Label *hp_label_ = nullptr;
	godot::ProgressBar *capture_bar_ = nullptr;
	godot::Label *capture_label_ = nullptr;
	godot::ProgressBar *xp_bar_ = nullptr;
	godot::Label *xp_label_ = nullptr;
	godot::ProgressBar *stamina_bar_ = nullptr;
	godot::Label *stamina_label_ = nullptr;
	godot::ProgressBar *aura_bar_ = nullptr;
	godot::Label *aura_label_ = nullptr;
	godot::Label *stats_ = nullptr;
	godot::Label *extra_ = nullptr;
	godot::Button *ungarrison_btn_ = nullptr;
	RtsoSelectionGrid selection_grid_;
	RtsoSelectionGrid garrison_grid_;
};

#endif // RTSO_INSPECTOR_VIEW_H
