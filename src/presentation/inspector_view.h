#ifndef RTSO_INSPECTOR_VIEW_H
#define RTSO_INSPECTOR_VIEW_H

// P4 stage-2 leaf widget: the bottom inspector (0 A.D. gui/session/selection_details.js
// and selection_panels.js Selection / Garrison). Owns the portrait, title, the five
// bars, the stats lines, the unload button and the selection and garrison grids; shows
// the selected structure, units or livestock, or the foreign entity a click inspected.

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/color_rect.hpp>
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/font_variation.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/text_server.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/panel.hpp>
#include <godot_cpp/classes/progress_bar.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/classes/texture_rect.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/theme.hpp>
#include <godot_cpp/classes/theme_db.hpp>
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
	// The selected structures, ships, rams and siege towers that hold units (Garrison.getItems).
	virtual std::vector<uint32_t> ui_selected_garrison_holders() const = 0;
};

class RtsoInspectorView {
	// session.xml selectionDetails: 228 x 204 at the middle of the bottom group.
	// single_details_area.xml (4 4 100%-4 100%-44): the 96 px portrait, the bars right of
	// it, the names and the player band under them, the command row below.
	static constexpr float kW = 228.0f, kH = 204.0f;
	static constexpr float kTextW = kW - 8.0f, kBarX = 118.0f, kBarW = kW - 6.0f - kBarX;
	static constexpr float kRows[5] = {10.0f, 26.0f, 42.0f, 58.0f, 74.0f};
	// single_details_area.xml in panel coordinates (detailsAreaSingle at 4 4): the names, the
	// player band, and the bar sections (x 96 to 100%, sectionPosTop/Middle/Bottom at 2, 12, 35),
	// each a 14 px caption row over a 7 px bar (1 14 100% 21).
	static constexpr float kNamesY = 4 + 96 + 3, kNamesH = 34, kBandY = 4 + 96 + 38, kBandH = 24;
	static constexpr float kSectionX = 4 + 96, kSectionW = kW - 4 - kSectionX;
	static constexpr float kSectionTop = 4 + 2, kSectionMiddle = 4 + 12, kSectionBottom = 4 + 35;
	static inline const godot::Vector2 kStatsAt{104.0f, 86.0f}, kStatsSize{kW - 108.0f, 14.0f};

public:
	enum InspectKind : uint8_t { INSPECT_NONE, INSPECT_UNIT, INSPECT_BUILDING, INSPECT_FAUNA, INSPECT_RESOURCE, INSPECT_TREASURE };

	// owner: signal target (RtsoMain: _on_ungarrison_pressed and the grid clicks).
	// parent: the HUD layer the panel goes on.
	void build(godot::Object *owner, godot::Node *parent, const godot::Ref<godot::Theme> &theme, RtsoInspectorHost *host) {
		using namespace godot;
		host_ = host;
		panel_ = memnew(Panel);
		panel_->set_anchor(SIDE_LEFT, 0.5f);
		panel_->set_anchor(SIDE_RIGHT, 0.5f);
		panel_->set_anchor(SIDE_TOP, 1.0f);
		panel_->set_anchor(SIDE_BOTTOM, 1.0f);
		panel_->set_offset(SIDE_LEFT, -kW / 2.0f);
		panel_->set_offset(SIDE_RIGHT, kW / 2.0f);
		panel_->set_offset(SIDE_TOP, -10.0f - kH);
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
		portrait_->set_position(Vector2(4, 4));
		portrait_->set_size(Vector2(96, 96));
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
			status_icons_[i]->set_position(Vector2(96 - 20, 4 + i * 18));
			status_icons_[i]->set_size(Vector2(16, 16));
			status_icons_[i]->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
			status_icons_[i]->set_mouse_filter(Control::MOUSE_FILTER_PASS);
			status_icons_[i]->set_visible(false);
			portrait_->add_child(status_icons_[i]);
		}

		bold_.instantiate();
		bold_->set_base_font(ThemeDB::get_singleton()->get_fallback_font());
		bold_->set_variation_embolden(0.7f);
		// statsArea (0 96 100% 100%): the names (0 3 100% 37), then the player band (0 38 100% 62).
		// primary (PrimaryNameCentered, sans-bold-stroke-13, gold) on the top half, or the whole
		// height without a secondary; secondary (SecondaryNameCentered, sans-stroke-12, white) at 50%-1.
		title_ = memnew(Label);
		title_->set_position(Vector2(4, kNamesY));
		title_->set_size(Vector2(kTextW, kNamesH));
		title_->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
		title_->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
		title_->set_clip_text(true);
		title_->add_theme_font_override("font", bold_);
		title_->add_theme_font_size_override("font_size", 13);
		title_->add_theme_color_override("font_color", Color(1.0f, 0.84f, 0.0f));
		title_->add_theme_color_override("font_outline_color", Color(0, 0, 0));
		title_->add_theme_constant_override("outline_size", 2);
		panel_->add_child(title_);
		secondary_ = memnew(Label);
		secondary_->set_name("secondary");
		secondary_->set_position(Vector2(4, kNamesY + kNamesH / 2.0f - 1.0f));
		secondary_->set_size(Vector2(kTextW, kNamesH / 2.0f + 1.0f));
		secondary_->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
		secondary_->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
		secondary_->set_clip_text(true);
		secondary_->add_theme_font_size_override("font_size", 12);
		secondary_->add_theme_color_override("font_color", Color(1.0f, 1.0f, 1.0f));
		secondary_->add_theme_color_override("font_outline_color", Color(0, 0, 0));
		secondary_->add_theme_constant_override("outline_size", 2);
		secondary_->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		secondary_->set_visible(false);
		panel_->add_child(secondary_); // not title_'s child: clip_text clips children too

		// selection_details.js's player band (0 38 100% 62 in statsArea): playerCivIcon
		// (50%-64 50%-10 50%+64 50%+10, "cropped:1.0, 0.15625 center:grayscale:" emblem), the
		// player colour at alpha 128 over it, phaseEmblems across the band, then the name, with
		// "Civ — Town Phase" as its tooltip.
		owner_civ_ = memnew(TextureRect);
		owner_civ_->set_name("playerCivIcon");
		owner_civ_->set_position(Vector2(4 + kTextW / 2.0f - 64.0f, kBandY + kBandH / 2.0f - 10.0f));
		owner_civ_->set_size(Vector2(128, 20));
		owner_civ_->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
		owner_civ_->set_stretch_mode(TextureRect::STRETCH_SCALE);
		{
			Ref<Shader> sh;
			sh.instantiate();
			sh->set_code("shader_type canvas_item;\nvoid fragment() {\n"
					"\tvec4 c = texture(TEXTURE, vec2(UV.x, 0.5 + (UV.y - 0.5) * 0.15625));\n"
					"\tfloat g = dot(c.rgb, vec3(0.3, 0.59, 0.11));\n\tCOLOR = vec4(vec3(g), c.a) * COLOR;\n}\n");
			civ_strip_.instantiate();
			civ_strip_->set_shader(sh);
		}
		owner_civ_->set_material(civ_strip_);
		owner_civ_->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		owner_civ_->set_visible(false);
		panel_->add_child(owner_civ_);
		owner_bg_ = memnew(ColorRect);
		owner_bg_->set_position(Vector2(4, kBandY));
		owner_bg_->set_size(Vector2(kTextW, kBandH));
		owner_bg_->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		owner_bg_->set_visible(false);
		panel_->add_child(owner_bg_);
		owner_phase_ = memnew(TextureRect);
		owner_phase_->set_name("phaseEmblems");
		owner_phase_->set_position(Vector2(4, kBandY));
		owner_phase_->set_size(Vector2(kTextW, kBandH));
		owner_phase_->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
		owner_phase_->set_stretch_mode(TextureRect::STRETCH_SCALE);
		owner_phase_->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		owner_phase_->set_visible(false);
		panel_->add_child(owner_phase_);

		class_ = memnew(Label);
		class_->set_position(Vector2(4, kBandY + kBandH / 2.0f - 10.0f));
		class_->set_size(Vector2(kTextW, 20));
		class_->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
		class_->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
		class_->set_clip_text(true);
		class_->add_theme_font_size_override("font_size", 11);
		class_->add_theme_color_override("font_color", Color(0.7f, 0.75f, 0.85f));
		class_->add_theme_color_override("font_outline_color", Color(0.0f, 0.0f, 0.0f));
		class_->set_mouse_filter(Control::MOUSE_FILTER_PASS);
		panel_->add_child(class_);

		// Helper lambda to setup 5 inspector bars with PNG icons or fallback text-based glyph icon labels
		// (0 A.D. gui/session/selection_details.js icon parity: HP/CP/XP/STA/AURA)
		auto make_insp_bar = [&](ProgressBar *&pbar, Label *&plbl, float y, Color fill_col, const char *glyph_text, const char *icon_path = nullptr) {
			pbar = memnew(ProgressBar);
			pbar->set_position(Vector2(kBarX, y));
			pbar->set_size(Vector2(kBarW, 11));
			pbar->set_show_percentage(false);

			// The icon (or glyph) left of the bar, its child so it hides with it.
			Label *glyph_lbl = memnew(Label);
			glyph_lbl->set_position(Vector2(-16, -3));
			glyph_lbl->set_size(Vector2(14, 14));
			glyph_lbl->set_clip_text(true);
			glyph_lbl->set_text(glyph_text);
			glyph_lbl->add_theme_font_size_override("font_size", 9);
			glyph_lbl->add_theme_color_override("font_color", fill_col);
			pbar->add_child(glyph_lbl);

			if (icon_path && *icon_path) {
				Ref<Texture2D> icon_tex = rtso::load_ui_texture(icon_path);
				if (icon_tex.is_valid()) {
					TextureRect *icon_rect = memnew(TextureRect);
					icon_rect->set_position(Vector2(-16, -2));
					icon_rect->set_size(Vector2(14, 14));
					icon_rect->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
					icon_rect->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
					icon_rect->set_texture(icon_tex);
					pbar->add_child(icon_rect);
					glyph_lbl->set_visible(false);
				}
			}

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
			plbl->set_position(Vector2(kBarX, y - 1));
			plbl->set_size(Vector2(kBarW, 13));
			plbl->add_theme_font_size_override("font_size", 9);
			plbl->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
			plbl->add_theme_color_override("font_color", Color(0.95f, 0.95f, 0.98f));
			panel_->add_child(plbl);
		};

		// 1. Health Bar (Green)
		make_insp_bar(hp_bar_, hp_label_, kRows[0], Color(0.25f, 0.75f, 0.35f), "HP", "res://assets/textures/ui/icons/insp_health_fg.png");
		// 2. Capture Points Bar (Cyan / Blue)
		make_insp_bar(capture_bar_, capture_label_, kRows[1], Color(0.35f, 0.55f, 0.95f), "CP", "res://assets/textures/ui/icons/insp_capture_bar.png");
		// healthSection and captureSection: healthStats and captureStats (StatsTextRight,
		// sans-stroke-12) in the caption row over a 7 px bar, no icon; layout_sections places them.
		auto section = [&](ProgressBar *bar, Label *caption) {
			for (int i = bar->get_child_count() - 1; i >= 0; --i) {
				Node *child = bar->get_child(i);
				bar->remove_child(child);
				memdelete(child);
			}
			bar->set_size(Vector2(kSectionW - 1.0f, 7));
			stats_caption(caption, HORIZONTAL_ALIGNMENT_RIGHT);
		};
		section(hp_bar_, hp_label_);
		section(capture_bar_, capture_label_);
		// healthSection's and captureTooltip's hover text.
		hp_bar_->set_tooltip_text(rtso::tr0("Health"));
		capture_bar_->set_tooltip_text(rtso::tr0("Capture Points"));
		// selection_details.js captureBar[n]: one part per player in their colour at half alpha,
		// the owner's first, over the bar (whose own fill stays empty).
		for (ColorRect *&part : capture_parts_) {
			part = memnew(ColorRect);
			part->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
			part->set_visible(false);
			capture_bar_->add_child(part);
		}
		// 3. Veterancy / Experience XP Bar (Gold / Amber)
		make_insp_bar(xp_bar_, xp_label_, kRows[2], Color(0.95f, 0.75f, 0.20f), "XP", "res://assets/textures/ui/icons/insp_experience_fg.png");
		// 4. Stamina / Energy Bar (Teal / Mint)
		make_insp_bar(stamina_bar_, stamina_label_, kRows[3], Color(0.20f, 0.85f, 0.75f), "STA", "res://assets/textures/ui/icons/insp_stamina_fg.png");
		// 5. Aura Influence Bar (Magenta / Violet) - no public.zip session icon, fallback to text glyph badge
		make_insp_bar(aura_bar_, aura_label_, kRows[4], Color(0.85f, 0.35f, 0.85f), "AURA", nullptr);

		// resourceSection: "Wood:" beside a bar of the supply left, "amount / max" (or "∞") on it.
		// It takes the first bar row under the health bar that nothing else uses.
		resource_name_ = memnew(Label);
		stats_caption(resource_name_, HORIZONTAL_ALIGNMENT_LEFT);
		resource_name_->set_visible(false);
		panel_->add_child(resource_name_);
		resource_bar_ = memnew(ProgressBar);
		resource_bar_->set_name("resourceBar");
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
		resource_bar_->set_size(Vector2(kSectionW - 1.0f, 7));
		resource_bar_->set_visible(false);
		panel_->add_child(resource_bar_);
		resource_stats_ = memnew(Label);
		stats_caption(resource_stats_, HORIZONTAL_ALIGNMENT_RIGHT);
		resource_stats_->set_visible(false);
		panel_->add_child(resource_stats_);

		stats_ = memnew(Label);
		// The row under the bars (0 63 100% 98): attack and resistance, carried resources.
		stats_->set_position(kStatsAt);
		stats_->set_size(kStatsSize);
		stats_->add_theme_font_size_override("font_size", 9);
		stats_->add_theme_color_override("font_color", Color(0.85f, 0.88f, 0.95f));
		panel_->add_child(stats_);

		// attackAndResistanceStats (97 0 129 32 in the row at 0 63): the defensive stance icon whose
		// instant tooltip is displaySingle's detailed tooltip, hidden when that is empty.
		attack_icon_ = memnew(TextureRect);
		attack_icon_->set_name("attackAndResistanceStats");
		attack_icon_->set_position(Vector2(4 + 97, 4 + 63));
		attack_icon_->set_size(Vector2(32, 32));
		attack_icon_->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
		attack_icon_->set_texture(rtso::load_ui_texture("res://assets/textures/ui/stances/defensive.png"));
		attack_icon_->set_meta("tooltip_style", "session_instant");
		attack_icon_->set_visible(false);
		panel_->add_child(attack_icon_);

		// resourceCarryingText (100%-96 0 100%-33 32) and resourceCarryingIcon (100%-32 0 100% 32)
		// in the row at 0 63: what a unit carries, a trader's gain, builders or repairers with the
		// time left, or a supply's gatherers, the icon carrying the tooltip.
		extra_ = memnew(Label);
		extra_->set_name("resourceCarryingText");
		extra_->set_position(Vector2(4 + kTextW - 96.0f, 4 + 63));
		extra_->set_size(Vector2(63, 32));
		extra_->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_RIGHT);
		extra_->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
		extra_->add_theme_font_size_override("font_size", 10);
		extra_->add_theme_color_override("font_color", Color(1.0f, 1.0f, 1.0f));
		extra_->add_theme_color_override("font_outline_color", Color(0, 0, 0));
		extra_->add_theme_constant_override("outline_size", 2);
		panel_->add_child(extra_);
		carry_icon_ = memnew(TextureRect);
		carry_icon_->set_name("resourceCarryingIcon");
		carry_icon_->set_position(Vector2(4 + kTextW - 32.0f, 4 + 63));
		carry_icon_->set_size(Vector2(32, 32));
		carry_icon_->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
		carry_icon_->set_visible(false);
		panel_->add_child(carry_icon_);

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
		// multiple_details_area.xml (6 6 100%-6 100%-44): the selection icons, the count and
		// the vertical bars in the right 35 px.
		selection_grid_.build(panel_, Vector2(6, 6), Vector2(kW - 47.0f, kH - 50.0f), owner, "_on_selection_group_clicked", 4, 38.0f);
		selection_grid_.set_right_click("_on_selection_group_gui_input");
		selection_grid_.grid()->set_meta("tooltip_style", "session_bold"); // unitSelectionButton: sessionToolTipBold
		// garrison_panel.xml (20 6 100% 100%): in the supplemental panel (move_to_left_panel).
		garrison_grid_.build(panel_, Vector2(20, 6), Vector2(180, 116), owner, "_on_garrison_group_clicked", 4, 38.0f);
		garrison_grid_.grid()->set_meta("tooltip_style", "session_bottom_bold"); // unitGarrisonButton
		// garrison_panel.xml healthGarrison: the garrisoned units' total health, a vertical
		// bar left of the garrison icons (updateGarrisonHealthBar).
		garrison_hp_bar_ = memnew(ProgressBar);
		garrison_hp_bar_->set_name("healthGarrison");
		garrison_hp_bar_->set_position(Vector2(8, 8));
		garrison_hp_bar_->set_size(Vector2(6, 112));
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
		// single_details_area.xml experience (2 2 6 100%-2): a vertical bar on the portrait's left
		// edge. multiple_details_area.xml: numberOfUnits in large text where the portrait goes, the
		// vertical healthMultiple and captureMultiple bars under it.
		// A background with its fill as the first child; set_vbar fills it from the bottom.
		auto make_vbar = [&](Control *parent, const char *name, Vector2 pos, Vector2 size, Color fill_col) {
			ColorRect *b = memnew(ColorRect);
			b->set_name(name);
			b->set_position(pos);
			b->set_size(size);
			b->set_color(Color(0.10f, 0.12f, 0.16f, 0.85f));
			b->set_mouse_filter(Control::MOUSE_FILTER_PASS);
			ColorRect *fill = memnew(ColorRect);
			fill->set_color(fill_col);
			fill->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
			b->add_child(fill);
			b->set_visible(false);
			parent->add_child(b);
			return b;
		};
		xp_vbar_ = make_vbar(portrait_, "experience", Vector2(2, 2), Vector2(4, 92), Color(0.95f, 0.75f, 0.20f));
		count_label_ = memnew(Label);
		count_label_->set_name("numberOfUnits");
		count_label_->set_position(Vector2(kW - 41.0f, 10));
		count_label_->set_size(Vector2(32, 32));
		count_label_->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
		count_label_->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
		count_label_->add_theme_font_size_override("font_size", 20);
		count_label_->add_theme_color_override("font_color", Color(1, 1, 1));
		count_label_->add_theme_color_override("font_outline_color", Color(0, 0, 0));
		count_label_->add_theme_constant_override("outline_size", 4);
		count_label_->set_mouse_filter(Control::MOUSE_FILTER_PASS);
		count_label_->set_visible(false);
		panel_->add_child(count_label_);
		hp_vbar_ = make_vbar(panel_, "healthMultiple", Vector2(kW - 35.0f, 46), Vector2(8, kH - 90.0f), Color(0.25f, 0.75f, 0.35f));
		cap_vbar_ = make_vbar(panel_, "captureMultiple", Vector2(kW - 23.0f, 46), Vector2(8, kH - 90.0f), Color(0, 0, 0, 0));
		for (ColorRect *&part : cap_vbar_parts_) {
			part = memnew(ColorRect);
			part->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
			part->set_visible(false);
			cap_vbar_->add_child(part);
		}
		// The Barter panel's place (0.28's left panel, shared with the garrison panel); the
		// command panel fills it.
		barter_host_ = memnew(Control);
		barter_host_->set_name("unitBarterPanel");
		barter_host_->set_position(Vector2(6, 6));
		barter_host_->set_size(Vector2(194, 116));
		barter_host_->set_visible(false);
		panel_->add_child(barter_host_);
	}

	godot::Panel *panel() const { return panel_; }
	godot::Control *portrait() const { return portrait_; }
	// The garrison and barter panels sit in the commands' supplemental panel (0.28's
	// selection_panels_left).
	void move_to_left_panel(godot::Control *left) {
		if (!left) return;
		garrison_grid_.grid()->reparent(left, false);
		garrison_hp_bar_->reparent(left, false);
		barter_host_->reparent(left, false);
	}
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
		d["title"] = title_full_;
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
		d["xp"] = xp_vbar_->is_visible() ? xp_vbar_->get_tooltip_text() : xp_label_->is_visible() ? xp_label_->get_text() : godot::String();
		d["count"] = count_label_->is_visible() ? count_label_->get_text() : godot::String();
		d["count_tooltip"] = count_label_->get_tooltip_text();
		d["health_multiple"] = hp_vbar_->is_visible() ? hp_vbar_->get_tooltip_text() : godot::String();
		d["capture_multiple"] = cap_vbar_->is_visible() ? cap_vbar_->get_tooltip_text() : godot::String();
		godot::Array cap_multi_parts;
		for (godot::ColorRect *r : cap_vbar_parts_)
			if (cap_vbar_->is_visible() && r->is_visible()) cap_multi_parts.push_back(r->get_size().y);
		d["capture_multiple_parts"] = cap_multi_parts;
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
		d["attack_resistance"] = attack_icon_->is_visible() ? attack_icon_->get_tooltip_text() : godot::String();
		d["extra"] = extra_->get_text();
		d["resource"] = resource_bar_->is_visible() ? resource_name_->get_text() + " " + resource_stats_->get_text() : godot::String();
		d["extra_tooltip"] = carry_icon_->is_visible() ? carry_icon_->get_tooltip_text() : godot::String();
		d["extra_icon"] = carry_icon_->is_visible() && carry_icon_->get_texture().is_valid() ? carry_icon_->get_texture()->get_path() : godot::String();
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
		// Resource supplies and treasures likewise (ResourceSupplyMirage): a supply used up or a
		// treasure taken in the fog stays selectable until the spot is seen again.
		{
			uint32_t best = 0;
			float best_d2 = 1.3f * 1.3f;
			for (const auto &m : mirages_.resources()) {
				if (m.subtype == rtso::RES_SUB_GRAIN) continue;
				const float dx = m.x - cx, dz = m.z - cz, d2 = dx * dx + dz * dz;
				if (d2 < best_d2) { best_d2 = d2; best = m.id; }
			}
			if (best != 0) return pick(INSPECT_RESOURCE, best);
		}
		{
			uint32_t best = 0;
			float best_d2 = 1.3f * 1.3f;
			for (const auto &t : mirages_.treasures()) {
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
		hide_multi();
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
			bar(hp_bar_, hp_label_, max > 0.0f, hp, max, rtso::seldet::health_stats(hp, max));
		};
		bar(capture_bar_, capture_label_, false);
		bar(xp_bar_, xp_label_, false);
		bar(aura_bar_, aura_label_, false);
		clear_carrying();
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
			set_title(unit_name(uv, u, uinfo.name));
			set_owner(*sim, uv.team[u]);
			health(uv.hp[u], uv.max_hp[u]);
			fill_unit_details(*sim, u, title_full_);
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
			set_title(String::utf8(fi.name));
			set_owner(*sim, owner);
			health(fv.hp[f], fv.max_hp[f]);
			set_statuses(*sim, rtso::Simulation::STATUS_ON_FAUNA, fv.id[f]);
			set_portrait_target(false, 0, fv.pos_x[f], fv.pos_z[f], String(), -1, 0);
			portrait_->set_tooltip_text(portrait_tooltip(title_full_, rtso::portraits::fauna_entry(fv.fauna_type[f]), String(), String(), false));
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
			d.acceleration = fi.acceleration / 0.356f; // UnitMotion/Acceleration, as for units
			const String attack = rtso::seldet::attack_tooltip(d,
					godot::Dictionary(rtso::portraits::fauna_entry(fv.fauna_type[f]).get("viewer", godot::Dictionary())).get("attacks", godot::Array())),
					resistance = rtso::seldet::resistance_tooltip(d.resistance);
			stats_->set_text(String());
			rtso::seldet::Amounts loot;
			loot.xp = (int64_t)fi.loot_xp;
			set_detailed_tooltip(rtso::seldet::join_lines({attack, resistance, rtso::seldet::speed_tooltip(d), rtso::seldet::loot_tooltip(loot)}));
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
				set_title(structure_name(seen->civ, seen->type, binfo.name));
				set_owner(*sim, seen->team);
				health(seen->hp, seen->max_hp);
				if (seen->max_capture_points > 0.0f)
				{
					bar(capture_bar_, capture_label_, true, seen->capture_points, seen->max_capture_points,
							rtso::seldet::capture_stats(seen->capture_points, seen->max_capture_points));
					float pts[rtso::MAX_PLAYERS + 1] = {};
					pts[seen->team < rtso::MAX_PLAYERS ? seen->team : rtso::MAX_PLAYERS] = seen->capture_points;
					set_capture_parts(*sim, seen->team, pts, seen->max_capture_points);
				}
				// The builders or repairers and the time left as last seen, and a fogged field's
				// supply and gatherers (FoundationMirage, RepairableMirage, ResourceSupplyMirage).
				if (seen->work) show_build_work(seen->work_repair, seen->work_builders, seen->work_remaining, seen->work_remaining_new);
				const rtso::FoggedResource *field = nullptr;
				if (const uint32_t fid = seen->type == rtso::BLD_FIELD ? sim->field_resource_id(seen->id) : 0)
					for (const auto &m : mirages_.resources())
						if (m.id == fid) { field = &m; break; }
				stats_->set_text(String());
				if (field) show_supply(*sim, field->id, field);
				stats_->set_tooltip_text(String());
				// A mirage keeps only its Resistance of the detailed tooltip's components.
				set_detailed_tooltip(seen->has_resistance ? rtso::seldet::resistance_tooltip(seen->resistance, true) : String());
				// The mirage's portrait: its template's tooltip, a left click to its spot, a right
				// click to the template viewer (selection_details.js displaySingle).
				const uint8_t civ = rtso::is_playable_civ(seen->civ) ? seen->civ
						: seen->team < rtso::MAX_PLAYERS ? sim->player_civ(seen->team) : sim->player_civ(host_->local_player());
				set_portrait_target(false, seen->id, seen->x, seen->z, "structure", seen->type, civ);
				const godot::Dictionary entry = rtso::portraits::structure_entry(seen->civ, seen->type);
				portrait_->set_tooltip_text(portrait_tooltip(title_full_, entry, rtso::seldet::structure_auras_tooltip(entry), String(), false));
				return true;
			}
			const rtso::Simulation::Building *found = nullptr;
			for (const auto &b : sim->buildings())
				if (b.id == id_) { found = &b; break; }
			if (!found || found->wrecked || found->hp <= 0.0f) return false;
			const auto &binfo = rtso::building_type_info(found->type);
			portrait_->set_texture(rtso::portraits::structure(found->civ, found->type));
			set_title(structure_name(found->civ, found->type, binfo.name));
			set_owner(*sim, found->team);
			health(found->hp, found->max_hp);
			set_structure_extras(*sim, *found);
			if (found->max_capture_points > 0.0f) {
				bar(capture_bar_, capture_label_, true, found->capture_points, found->max_capture_points,
						rtso::seldet::capture_stats(found->capture_points, found->max_capture_points));
				set_structure_capture_parts(*sim, *found);
			}
			rtso::Simulation::BuildWork work;
			if (sim->structure_build_work(*found, work)) show_build_work(work.repair, work.builders, work.remaining, work.remaining_new);
			stats_->set_text(String());
			show_supply(*sim, sim->field_resource_id(found->id));
			set_detailed_tooltip(structure_tooltip(*sim, *found));
			return true;
		}
		if (kind_ == INSPECT_TREASURE) {
			// selection_details.js: the treasure's name and its Treasure/Resources as the reward.
			mirages_.update(*sim, host_->view_player());
			const rtso::FoggedTreasure *found = nullptr;
			for (const auto &t : mirages_.treasures())
				if (t.id == id_) { found = &t; break; }
			if (!found) return false;
			const auto &ti = rtso::treasure_type_info(found->type);
			portrait_->set_texture(rtso::portraits::treasure(found->type));
			set_title(String::utf8(ti.name));
			set_portrait_target(false, 0, found->x, found->z, String(), -1, 0);
			portrait_->set_tooltip_text(portrait_tooltip(title_full_, rtso::portraits::treasure_entry(found->type), String(),
					rtso::seldet::reward_tooltip(ti.amount), false));
			set_owner(*sim, rtso::GAIA_PLAYER);
			bar(hp_bar_, hp_label_, false);
			stats_->set_text(String()); // the reward is on the portrait tooltip (getTreasureTooltip)
			return true;
		}
		// A resource: its kind and what is left (selection_details.js resourceSupply), as the
		// viewer last saw it when it is in the fog (ResourceSupplyMirage).
		mirages_.update(*sim, host_->view_player());
		const rtso::FoggedResource *res = nullptr;
		for (const auto &m : mirages_.resources())
			if (m.id == id_) { res = &m; break; }
		if (!res) return false;
		String title, kind;
		switch (res->type) {
		case rtso::RES_WOOD: title = flora_title(res->flora); kind = "Wood"; break;
		case rtso::RES_GOLD: title = res->subtype == rtso::RES_SUB_RUINS ? "Ruins" : "Metal Mine"; kind = "Metal"; break;
		case rtso::RES_STONE: title = res->subtype == rtso::RES_SUB_RUINS ? "Ruins" : "Stone Quarry"; kind = "Stone"; break;
		default:
			title = res->subtype == rtso::RES_SUB_MEAT ? String("Carcass") : res->subtype == rtso::RES_SUB_FISH ? String("Fish")
					: flora_title(res->flora);
			kind = "Food";
		}
		// A tree or fruit: its template's SpecificName under the Tree or Fruit GenericName
		// (gaia/tree and gaia/fruit templates set only SpecificName): "Aleppo Pine (Tree)".
		if (res->type == rtso::RES_WOOD || (res->type == rtso::RES_FOOD && res->subtype != rtso::RES_SUB_MEAT &&
				res->subtype != rtso::RES_SUB_FISH && res->subtype != rtso::RES_SUB_GRAIN)) {
			const String generic = rtso::portraits::resource_entry(res->type, res->subtype, res->flora).get("generic_name", String());
			if (!generic.is_empty()) title = rtso::entity_names(generic, title);
		}
		// A carcass keeps its animal's Identity and Ownership (special/filter/resource.xml).
		const bool carcass = res->subtype == rtso::RES_SUB_MEAT && res->fauna != 0;
		if (carcass) title = String::utf8(rtso::fauna_type_info((uint8_t)(res->fauna - 1)).name);
		portrait_->set_texture(carcass ? rtso::portraits::fauna((uint8_t)(res->fauna - 1)) : rtso::portraits::resource(res->type, res->subtype, res->flora));
		set_title(title);
		set_owner(*sim, carcass && res->owner < rtso::MAX_PLAYERS ? res->owner : rtso::GAIA_PLAYER);
		bar(hp_bar_, hp_label_, false);
		set_portrait_target(false, 0, res->x, res->z, String(), -1, 0);
		portrait_->set_tooltip_text(portrait_tooltip(title, carcass ? rtso::portraits::fauna_entry((uint8_t)(res->fauna - 1))
				: rtso::portraits::resource_entry(res->type, res->subtype, res->flora), String(), String(), false));
		// resourceSection ("Wood:" and amount / max, ∞ for a field) and the gatherers count
		// (current / max, "Current/max gatherers").
		(void)kind;
		stats_->set_text(String());
		show_supply(*sim, res->id, res);
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
			g.tip = rtso::entity_names(RtsoSelectionGrid::group_name(g), String()); // getEntityNames, no click help
		}
		if (groups.size() > RtsoSelectionGrid::kMaxSelectionGroups) groups.resize(RtsoSelectionGrid::kMaxSelectionGroups);
		set_title(String::num_int64(selected_ids.size()) + " units selected");
		// displayMultiple: each animal's Garrisonable size (corrals), no carried resources or Loot.
		title_->set_tooltip_text(rtso::seldet::multiple_tooltip((int64_t)selected_ids.size(), {}, {}));
		portrait_->set_tooltip_text("");
		clear_owner();
		class_->set_text(String::num_int64(groups.size()) + (groups.size() == 1 ? " type" : " types"));
		hp_bar_->set_max(max_hp > 0.0f ? max_hp : 1.0f);
		hp_bar_->set_value(hp);
		hp_label_->set_text(rtso::seldet::health(hp, max_hp));
		show_multi((int64_t)selected_ids.size(), title_->get_tooltip_text(), hp, max_hp);
		for (int i = 0; i < kStatusIcons; ++i) status_icons_[i]->set_visible(false);
		hide_resource_bar();
		stats_->set_text("");
		stats_->set_tooltip_text("");
		clear_carrying();
		selection_grid_.update(groups, "");
		show_grid = true;
		return true;
	}

	void update() {
		update_contents();
		if (panel_) layout_sections();
	}
	void update_contents() {
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
		// The stamina bar has no 0 A.D. counterpart.
		show_bar(stamina_bar_, stamina_label_, false);
		hide_resource_bar();
		hide_multi();
		garrison_hp_bar_->set_visible(false); // fill_garrison_grid shows it
		clear_tooltips();
		stats_->set_position(kStatsAt);
		stats_->set_size(kStatsSize);
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
			show_garrison_grid = fill_garrison_grid(*sim, host_->ui_selected_garrison_holders());
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
				set_title(structure_name(found_b->civ, found_b->type, binfo.name));
				const uint8_t owner = found_b->team;
				// The garrison count is on the garrison panel and the Unload All icon, not here.
				set_owner(*sim, owner, String());

				set_bar(hp_bar_, hp_label_, found_b->hp, found_b->max_hp, rtso::seldet::health_stats(found_b->hp, found_b->max_hp));
				set_structure_extras(*sim, *found_b);

				if (found_b->max_capture_points > 0.0f) {
					set_bar(capture_bar_, capture_label_, found_b->capture_points, found_b->max_capture_points,
							rtso::seldet::capture_stats(found_b->capture_points, found_b->max_capture_points));
					set_structure_capture_parts(*sim, *found_b);
				} else
					show_bar(capture_bar_, capture_label_, false);

				// selection_details.js: a foundation or a damaged structure shows only its builders or
				// repairers and the time left beside the repair icon, with getBuildTimeTooltip or
				// getRepairTimeTooltip; the health bar is its only bar.
				show_bar(xp_bar_, xp_label_, false);
				clear_carrying();
				rtso::Simulation::BuildWork work;
				if (sim->structure_build_work(*found_b, work)) show_build_work(work.repair, work.builders, work.remaining, work.remaining_new);
				show_bar(aura_bar_, aura_label_, false);

				// A field's resourceSection: "Food: ∞" and its gatherers. 0.28 has no free text here: the
				// upgrade and research progress are on the Queue panel, the gate state on its Lock
				// buttons, the arrows on the attack tooltip (selection_details.js displaySingle).
				stats_->set_text(String());
				show_supply(*sim, sim->field_resource_id(found_b->id));
				// Beside the Barter panel when it shows.
				const bool barter = barter_host_ && barter_host_->is_visible();
				if (barter) {
					stats_->set_position(kStatsAt);
					stats_->set_size(kStatsSize);
					stats_->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
				}
				set_detailed_tooltip(structure_tooltip(*sim, *found_b));

				// Garrison panel; Unload All is the command row's icon (g_EntityCommands).
				show_garrison_grid = fill_garrison_grid(*sim, found_b->id);
				ungarrison_btn_->set_visible(false);
				panel_->set_visible(true);
				return;
			}
		}

		if (selected_ids.is_empty()) {
			ungarrison_btn_->set_visible(false);
			if (kind_ != INSPECT_NONE && !fill_inspected_details()) kind_ = INSPECT_NONE;
			// unit_commands.js updateUnitCommands: a mutual ally's structure, ship or siege engine
			// shows its Garrison panel (our units in it may be unloaded from there).
			if ((kind_ == INSPECT_BUILDING || kind_ == INSPECT_UNIT) && host_->local_player() < rtso::MAX_PLAYERS) {
				uint8_t owner = 0xFF;
				sim->holder_contents(id_, &owner);
				if (owner < rtso::MAX_PLAYERS && owner != host_->local_player() && sim->is_ally(host_->local_player(), owner) &&
						sim->is_ally(owner, host_->local_player()))
					show_garrison_grid = fill_garrison_grid(*sim, id_);
			}
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
		set_bar(hp_bar_, hp_label_, hp_sum, hp_max_sum,
				selected_ids.size() == 1 ? rtso::seldet::health_stats(hp_sum, hp_max_sum) : rtso::seldet::health(hp_sum, hp_max_sum));
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
			set_title(name);
			const uint8_t owner = uview.team[u];
			set_owner(*sim, owner, String()); // kills are on the summary screen, not here

			show_bar(xp_bar_, xp_label_, false);

			show_bar(aura_bar_, aura_label_, false); // auras are on the portrait tooltip (getAurasTooltip)

			fill_unit_details(*sim, u, name);
			set_unit_extras(*sim, u);
			if (sim->is_transport((uint32_t)selected_ids[0])) {
				// Passengers are on the garrison panel and the Unload All icon's count.
				ungarrison_btn_->set_visible(false);
				show_garrison_grid = fill_garrison_grid(*sim, (uint32_t)selected_ids[0]);
			} else {
				ungarrison_btn_->set_visible(false);
			}
		} else {
			portrait_->set_texture(groups.empty() ? Ref<Texture2D>()
					: rtso::portraits::unit(groups.front().tmpl, groups.front().unit_type));
			set_title(String::num_int64(selected_ids.size()) + " units selected");
			// displayMultiple's numberOfUnits tooltip: garrison size, carried resources, loot.
			{
				rtso::seldet::Amounts carrying, loot;
				int64_t garrison_size = 0;
				for (int s = 0; s < selected_ids.size(); ++s) {
					size_t i = 0;
					for (; i < uview.count; ++i)
						if (uview.id[i] == (uint32_t)selected_ids[s]) break;
					if (i == uview.count || !uview.alive[i]) continue;
					// Garrisonable.size (1 for every unit, ships included: template_unit.xml) plus a
					// holder's GarrisonHolder.occupiedSlots.
					garrison_size += 1 + (int64_t)sim->get_transport_passenger_count(uview.id[i]);
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
			show_multi((int64_t)selected_ids.size(), title_->get_tooltip_text(), hp_sum, hp_max_sum);
			clear_owner();
			class_->set_text(String::num_int64(groups.size()) + (groups.size() == 1 ? " type" : " types"));
			show_bar(xp_bar_, xp_label_, false);
			show_bar(aura_bar_, aura_label_, false);
			stats_->set_text("");
			clear_carrying();
			ungarrison_btn_->set_visible(false);
			selection_grid_.update(selection_groups(*sim, uview, groups), "");
			show_selection_grid = true;
			// Ships, rams and siege towers with passengers: one merged garrison panel.
			show_garrison_grid = fill_garrison_grid(*sim, host_->ui_selected_garrison_holders());
		}
		panel_->set_visible(true);
	}

private:
	// displaySingle's resourceCarrying for a unit: a trader's goods and total gain while it
	// carries goods (heading to market a on leg 0, else b), what a gatherer carries
	// ("amount / max"), or a damaged Repairable siege engine's or ship's repairers and time left.
	void set_unit_carrying(const rtso::Simulation &sim, size_t u) {
		using namespace godot;
		const auto uview = sim.units();
		uint32_t route_a = 0, route_b = 0;
		int route_leg = 0;
		rtso::Simulation::TradeGain g;
		if (rtso::unit_is_trader(uview.unit_type[u]) && sim.trader_route(uview.id[u], route_a, route_b, route_leg) &&
				sim.trader_goods(uview.id[u], g)) {
			const rtso::Simulation::Building *ma = nullptr, *mb = nullptr;
			for (const auto &bb : sim.buildings()) {
				if (bb.id == (route_leg == 0 ? route_b : route_a)) ma = &bb;
				if (bb.id == (route_leg == 0 ? route_a : route_b)) mb = &bb;
			}
			if (ma && mb) {
				// getTradingTooltip: market1 pays the arrival market's owner, market2 the starting market's.
				const String gains = rtso::seldet::trading_gain(g.trader, uview.team[u], g.market1, mb->team, g.market2, ma->team,
						[&](uint8_t pid) { return host_->player_display_name(pid); });
				set_carrying(res_icon(uview.res_type_carried[u]), String::num_int64(g.trader + g.market1 + g.market2),
						rtso::sprintf0(rtso::tr0("Gain: %(gain)s"), {{"gain", gains}}));
			}
		} else if (!rtso::unit_is_trader(uview.unit_type[u]) && uview.carry[u] > 0 && uview.res_type_carried[u] < rtso::RES_COUNT) {
			set_carrying(res_icon(uview.res_type_carried[u]), rtso::seldet::amount_of(uview.carry[u], sim.unit_details(u).carry_max), String());
		} else if (rtso::Simulation::BuildWork w; sim.unit_repair_work(u, w)) {
			set_carrying("repair", rtso::seldet::build_work_caption(w.builders, w.remaining), build_work_tooltip(w));
		}
	}

	// resourceCarrying: icon "repair" for builders and gatherers, else a resource's own icon.
	void set_carrying(const godot::String &icon, const godot::String &text, const godot::String &tooltip) {
		static const char *const kIcons[] = {"wood", "metal", "stone", "food"}; // ResourceType order
		godot::String path = "res://assets/textures/ui/archive/icons/repair.png";
		for (int r = 0; r < rtso::RES_COUNT; ++r)
			if (icon == kIcons[r]) path = godot::String("res://assets/textures/ui/icons/") + kIcons[r] + ".png";
		carry_icon_->set_texture(rtso::load_ui_texture(path));
		carry_icon_->set_tooltip_text(tooltip);
		carry_icon_->set_visible(true);
		extra_->set_text(text);
	}
	static godot::String res_icon(uint8_t res) {
		static const char *const kIcons[] = {"wood", "metal", "stone", "food"};
		return res < rtso::RES_COUNT ? godot::String(kIcons[res]) : godot::String("repair");
	}
	void clear_carrying() {
		extra_->set_text(godot::String());
		carry_icon_->set_tooltip_text(godot::String());
		carry_icon_->set_visible(false);
	}

	void set_detailed_tooltip(const godot::String &text) {
		attack_icon_->set_tooltip_text(text);
		attack_icon_->set_visible(!text.is_empty());
	}

	// StatsTextLeft / StatsTextRight: sans-stroke-12, white, centred vertically in the 14 px row.
	static void stats_caption(godot::Label *l, godot::HorizontalAlignment align) {
		using namespace godot;
		l->set_size(Vector2(kSectionW, 14));
		l->set_horizontal_alignment(align);
		l->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
		l->set_clip_text(true);
		l->add_theme_font_size_override("font_size", 12);
		l->add_theme_color_override("font_color", Color(1, 1, 1));
		l->add_theme_color_override("font_outline_color", Color(0, 0, 0));
		l->add_theme_constant_override("outline_size", 2);
		l->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	}

	// displaySingle's names: "Primary (Secondary)" splits at its last " (" into the gold primary
	// and the white "(secondary)" under it; title_full_ keeps the whole text.
	void set_title(const godot::String &text) {
		title_full_ = text;
		const int64_t cut = text.ends_with(")") ? text.rfind(" (") : -1;
		const bool two = cut > 0;
		title_->set_text(two ? text.substr(0, cut) : text);
		title_->set_size(godot::Vector2(kTextW, two ? kNamesH / 2.0f : kNamesH));
		secondary_->set_text(two ? text.substr(cut + 1) : godot::String());
		secondary_->set_visible(two);
	}

	// displaySingle's section placement: health on top; capture in the middle under health
	// when a resource bar takes the bottom, else at the bottom; the resource bar at the bottom
	// (top without health); capture alone on top. With all three the capture caption is empty
	// and its points go to the tooltip (showSmallCapture).
	void layout_sections() {
		using namespace godot;
		const bool health = hp_bar_->is_visible(), capture = capture_bar_->is_visible(), resource = resource_bar_->is_visible();
		auto place = [](ProgressBar *bar, std::initializer_list<Label *> captions, float y) {
			for (Label *l : captions) l->set_position(Vector2(kSectionX, y));
			bar->set_position(Vector2(kSectionX + 1.0f, y + 14.0f));
		};
		if (health) place(hp_bar_, {hp_label_}, kSectionTop);
		if (capture) place(capture_bar_, {capture_label_}, !health ? (resource ? kSectionBottom : kSectionTop) : resource ? kSectionMiddle : kSectionBottom);
		if (resource) place(resource_bar_, {resource_name_, resource_stats_}, health ? kSectionBottom : kSectionTop);
		capture_bar_->set_tooltip_text(rtso::tr0("Capture Points"));
		if (health && capture && resource) {
			const PackedStringArray pts = capture_label_->get_text().split(" / ");
			if (pts.size() == 2)
				capture_bar_->set_tooltip_text(rtso::sprintf0(rtso::tr0("%(captureLabel)s %(current)s / %(max)s"),
						{{"captureLabel", rtso::tr0("Capture points:")}, {"current", pts[0]}, {"max", pts[1]}}));
			capture_label_->set_text(String());
		}
	}

	void clear_tooltips() {
		for (godot::Control *c : std::initializer_list<godot::Control *>{portrait_, title_, stats_, extra_, aura_label_, xp_bar_}) c->set_tooltip_text("");
		rank_icon_->set_visible(false);
		rank_icon_->set_tooltip_text("");
		set_detailed_tooltip(String());
		clear_carrying();
		for (godot::ColorRect *r : capture_parts_) r->set_visible(false);
		for (godot::TextureRect *icon : status_icons_) icon->set_visible(false);
		set_portrait_target(false, 0, 0.0f, 0.0f, String(), -1, 0);
	}

	// The capture bar's parts: pts[p] for each player p, pts[MAX_PLAYERS] for Gaia; the owner first.
	static void set_vbar(godot::ColorRect *bar, float value, float max) {
		auto *fill = godot::Object::cast_to<godot::ColorRect>(bar->get_child(0));
		const float frac = max > 0.0f ? std::clamp(value / max, 0.0f, 1.0f) : 0.0f;
		const godot::Vector2 size = bar->get_size();
		fill->set_position(godot::Vector2(0.0f, size.y * (1.0f - frac)));
		fill->set_size(godot::Vector2(size.x, size.y * frac));
	}
	void hide_multi() {
		portrait_->set_visible(true);
		title_->set_visible(true);
		class_->set_visible(true);
		xp_vbar_->set_visible(false);
		count_label_->set_visible(false);
		count_label_->set_tooltip_text("");
		hp_vbar_->set_visible(false);
		cap_vbar_->set_visible(false);
	}
	// displayMultiple: the count in place of the portrait, health (and capture) as vertical bars.
	void show_multi(int64_t count, const godot::String &tip, float hp, float max_hp) {
		portrait_->set_visible(false);
		// multiple_details_area.xml has no names or player band.
		title_->set_visible(false);
		secondary_->set_visible(false);
		class_->set_visible(false);
		count_label_->set_text(godot::String::num_int64(count));
		count_label_->set_tooltip_text(tip);
		count_label_->set_visible(true);
		hp_bar_->set_visible(false);
		hp_label_->set_visible(false);
		capture_bar_->set_visible(false);
		capture_label_->set_visible(false);
		hp_label_->set_text(rtso::seldet::health(hp, max_hp));
		set_vbar(hp_vbar_, hp, max_hp);
		hp_vbar_->set_tooltip_text(hp_label_->get_text());
		hp_vbar_->set_visible(true);
		cap_vbar_->set_visible(false);
	}
	// captureMultiple: one part per player stacked from the bottom, the owner's first.
	void set_multi_capture(const rtso::Simulation &sim, uint8_t owner, const float *pts, float max) {
		const float w = cap_vbar_->get_size().x, h = cap_vbar_->get_size().y;
		const int owner_slot = owner < rtso::MAX_PLAYERS ? owner : rtso::MAX_PLAYERS;
		float y = h;
		int used = 0;
		auto part = [&](int slot) {
			const float frac = max > 0.0f ? std::clamp(pts[slot] / max, 0.0f, 1.0f) : 0.0f;
			if (frac <= 0.0f || used >= kCaptureParts) return;
			const uint32_t rgb = slot < rtso::MAX_PLAYERS ? rtso::display_colors::rgb(sim, (uint8_t)slot) : sim.player_color(rtso::GAIA_PLAYER);
			godot::ColorRect *r = cap_vbar_parts_[used++];
			r->set_color(godot::Color(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, 128.0f / 255.0f));
			y -= h * frac;
			r->set_position(godot::Vector2(0.0f, y));
			r->set_size(godot::Vector2(w, h * frac));
			r->set_visible(true);
		};
		part(owner_slot);
		for (int slot = 0; slot <= rtso::MAX_PLAYERS; ++slot)
			if (slot != owner_slot) part(slot);
		for (int i = used; i < kCaptureParts; ++i) cap_vbar_parts_[i]->set_visible(false);
		set_vbar(cap_vbar_, 0.0f, 1.0f);
		cap_vbar_->set_tooltip_text(rtso::seldet::capture(pts[owner_slot], max, "Capture Points:"));
		cap_vbar_->set_visible(true);
	}
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
		// resourceLabel (StatsTextLeft) and resourceStats (StatsTextRight) share the caption row.
		resource_name_->set_text(rtso::seldet::resource_label(res));
		resource_bar_->set_max(1.0);
		resource_bar_->set_value(infinite ? 1.0 : max > 0 ? std::clamp((double)amount / (double)max, 0.0, 1.0) : 0.0);
		resource_stats_->set_text(rtso::seldet::resource_amount(amount, max, infinite));
		for (Control *c : std::initializer_list<Control *>{resource_name_, resource_bar_, resource_stats_}) c->set_visible(true);
	}
	// A resource's (or a field's) supply as the resource bar, and its gatherers, current / max,
	// as the returned line (resourceCarryingText, "Current/max gatherers").
	// seen: the viewer's copy of the supply; a mirage shows its last-seen amount and gatherers.
	void show_supply(const rtso::Simulation &sim, uint32_t res_id, const rtso::FoggedResource *seen = nullptr) {
		const String gatherers = supply_lines(sim, res_id, seen);
		if (!gatherers.is_empty()) set_carrying("repair", gatherers, rtso::tr0("Current/max gatherers"));
	}
	String supply_lines(const rtso::Simulation &sim, uint32_t res_id, const rtso::FoggedResource *seen = nullptr) {
		if (!res_id) return String();
		if (seen && seen->miraged) {
			show_resource_bar(seen->type, seen->amount, sim.resource_supply_max(res_id), seen->amount >= rtso::FIELD_FOOD_SUPPLY);
			return rtso::seldet::amount_of(seen->gatherers, sim.resource_max_gatherers(res_id));
		}
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
	// resourceCarryingIcon/Text for a foundation or a structure (or unit) being repaired.
	void show_build_work(bool repair, int builders, float remaining, float remaining_new) {
		set_carrying("repair", rtso::seldet::build_work_caption(builders, remaining),
				rtso::seldet::build_work_tooltip(repair, builders, remaining, remaining_new));
	}
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
		// The iconBorder tooltip opens with the primary name only, in namePrimaryBig.
		String primary = name;
		if (const int open = name.rfind(" ("); open > 0 && name.ends_with(")")) primary = name.substr(0, open);
		return join_lines({primary, classes_line(entry.get("classes", godot::Array())), auras,
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
		// displaySingle runs for any unit, its own or not: the experience toward the next rank
		// (Promotion.js GetRequiredXp after technologies) and what it carries.
		xp_vbar_->set_visible(false);
		if (const float need = sim.promotion_required_xp(u); need > 0.0f) {
			set_vbar(xp_vbar_, (float)uv.xp[u], need);
			xp_vbar_->set_tooltip_text(rtso::seldet::experience(uv.xp[u], need));
			xp_vbar_->set_visible(true);
		}
		set_unit_carrying(sim, u);
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
				capture_label_->set_text(rtso::seldet::capture_stats(r.cp[owner < rtso::MAX_PLAYERS ? owner : rtso::MAX_PLAYERS], max));
				set_capture_parts(sim, owner, r.cp.data(), max);
				break;
			}
		portrait_->set_tooltip_text(portrait_tooltip(title_full_, entry, aura_label_->get_tooltip_text(), String(), true));
	}

	// A single structure's statuses, portrait tooltip and clicks.
	void set_structure_extras(const rtso::Simulation &sim, const rtso::Simulation::Building &b) {
		set_statuses(sim, rtso::Simulation::STATUS_ON_STRUCTURE, b.id);
		// ViewerPage.js: the civ of the structure's own template, so a captured barracks lists its builder's units.
		const uint8_t civ = rtso::is_playable_civ(b.civ) ? b.civ
				: b.team < rtso::MAX_PLAYERS ? sim.player_civ(b.team) : sim.player_civ(host_->local_player());
		set_portrait_target(false, b.id, b.x, b.z, "structure", b.type, civ);
		const godot::Dictionary entry = rtso::portraits::structure_entry(b.civ, b.type);
		// A foundation's template (foundation|...) drops the Auras.
		const String auras = b.build_progress < 1.0f ? String() : rtso::seldet::structure_auras_tooltip(entry);
		portrait_->set_tooltip_text(portrait_tooltip(title_full_, entry, auras, String(), false));
	}
	// displaySingle for a unit: the stance icon's detailed tooltip
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
		stats_->set_text(String());
		auto uv = sim.units();
		Amounts loot;
		loot.res[rtso::RES_FOOD] = d.loot.food;
		loot.res[rtso::RES_WOOD] = d.loot.wood;
		loot.res[rtso::RES_STONE] = d.loot.stone;
		loot.res[rtso::RES_GOLD] = d.loot.metal;
		loot.xp = d.loot.xp;
		if (uv.carry[u] > 0 && uv.res_type_carried[u] < rtso::RES_COUNT) loot.res[uv.res_type_carried[u]] += uv.carry[u];
		// getGarrisonTooltip for a ship, ram or siege tower (its Garrison Limit, then its
		// Garrisonable size 1 plus the units inside) and getPopulationBonusTooltip.
		const int passengers = (int)sim.get_transport_passenger_count(uv.id[u]);
		const String garrison = join_lines({garrison_limit_tooltip(sim.get_transport_capacity(uv.id[u])),
				garrison_size_tooltip(1, passengers)});
		const uint16_t pop_tmpl = uv.tmpl ? uv.tmpl[u] : rtso::CIV_UNIT_TEMPLATE_NONE;
		const int pop_bonus = pop_tmpl < rtso::CIV_UNIT_TEMPLATE_COUNT ? rtso::unit_pop_bonus(pop_tmpl) : 0;
		// getProjectilesTooltip for a siege tower's BuildingAI (DefaultArrowCount 0,
		// GarrisonArrowMultiplier 1, MaxArrowCount 10): min(max, default + per unit x capacity).
		const String projectiles = sim.is_siege_tower_at(u)
				? projectiles_tooltip(std::min(rtso::SIEGE_TOWER_MAX_ARROWS, sim.get_transport_capacity(uv.id[u])), 0, 1.0f) : String();
		set_detailed_tooltip(join_lines({attack, healer_tooltip(d), resistance, gather_tooltip(d), speed_tooltip(d),
				garrison, population_bonus_tooltip(pop_bonus), projectiles, loot_tooltip(loot)}));
		const String auras = rtso::seldet::unit_auras_tooltip(uv.tmpl ? uv.tmpl[u] : rtso::CIV_UNIT_TEMPLATE_NONE);
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
		// A foundation (template_foundation: no Attack, BuildingAI, GarrisonHolder, Loot,
		// Population or ResourceTrickle) shows only its Resistance.
		if (b.build_progress < 1.0f) return resistance_tooltip(res, true);
		const rtso::Simulation::StructureDetails d = sim.structure_details(b);
		constexpr float kM = rtso::AttackDetection::kMetresPerUnit;
		// GuiInterface elevationAdaptedRange for BuildingAI: the reach from its arrows' origin there.
		const float elevated = d.shoots ? sim.average_elevation_range(b.x, b.z, d.max_range,
				rtso::civ_building_ai_info(b.civ, b.type).origin_y) * kM : 0.0f;
		const String attack = d.shoots ? structure_attack_tooltip(d.damage, d.min_range * kM, d.max_range * kM, d.repeat_time,
				d.arrows ? d.arrows : d.default_arrows, elevated,
				b.type == rtso::BLD_TOWER_BOLT ? "Bolt" : b.type == rtso::BLD_TOWER_ARTILLERY ? "Stone" : "Bow") : String();
		const rtso::LootInfo l = rtso::structure_loot(b.type, b.civ, b.half_size);
		Amounts loot;
		loot.res[rtso::RES_FOOD] = l.food;
		loot.res[rtso::RES_WOOD] = l.wood;
		loot.res[rtso::RES_STONE] = l.stone;
		loot.res[rtso::RES_GOLD] = l.metal;
		loot.xp = l.xp;
		return join_lines({attack, resistance_tooltip(res, true), garrison_limit_tooltip(sim.turret_only_holder(b) ? 0 : b.garrison_cap), garrison_heal_tooltip(d.heal),
				turrets_tooltip(d.turrets), population_bonus_tooltip(d.pop_bonus),
				projectiles_tooltip(d.projectile_limit, d.default_arrows, d.per_unit),
				d.trickles ? trickle_tooltip(d.trickle, d.trickle_interval) : String(), loot_tooltip(loot)});
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
	godot::Label *title_ = nullptr, *secondary_ = nullptr;
	godot::String title_full_;
	godot::Ref<godot::FontVariation> bold_;
	godot::Ref<godot::ShaderMaterial> civ_strip_; // playerCivIcon's crop and grayscale
	godot::Label *class_ = nullptr;
	godot::ColorRect *owner_bg_ = nullptr;
	godot::TextureRect *owner_civ_ = nullptr, *owner_phase_ = nullptr;

	// The player row of a single selection: the owner's name (and RTS0's suffix: kills, garrison)
	// centred on their colour, their civ emblem and, for the viewer, its mutual allies and
	// observers, their phase (selection_details.js displaySingle).
	void set_owner(const rtso::Simulation &sim, uint8_t owner, const godot::String &suffix = godot::String()) {
		using namespace godot;
		class_->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
		// player: largeCenteredOutlinedText (sans-bold-stroke-14, white).
		class_->add_theme_font_override("font", bold_);
		class_->add_theme_font_size_override("font_size", 14);
		if (owner >= rtso::MAX_PLAYERS) {
			// Gaia keeps its colour band; only the emblem, phase and civ tooltip are blank.
			clear_owner();
			class_->add_theme_font_override("font", bold_);
			class_->add_theme_font_size_override("font_size", 14);
			const uint32_t gaia = sim.player_color(rtso::GAIA_PLAYER);
			owner_bg_->set_color(Color(((gaia >> 16) & 0xFF) / 255.0f, ((gaia >> 8) & 0xFF) / 255.0f, (gaia & 0xFF) / 255.0f, 128.0f / 255.0f));
			owner_bg_->set_visible(true);
			class_->add_theme_color_override("font_color", Color(1.0f, 1.0f, 1.0f));
			class_->add_theme_constant_override("outline_size", 2);
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
			const int p = std::clamp<int>(sim.player_phase(owner), 1, 3) - 1;
			phase = kPhase[p];
			tip += String::utf8(" — ") + rtso::seldet::civ_phase_name(civ, p + 1);
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
		class_->remove_theme_font_override("font");
		class_->add_theme_font_size_override("font_size", 11);
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
	godot::ColorRect *xp_vbar_ = nullptr;     // experience
	godot::Label *count_label_ = nullptr;     // numberOfUnits
	godot::ColorRect *hp_vbar_ = nullptr;     // healthMultiple
	godot::ColorRect *cap_vbar_ = nullptr;    // captureMultiple
	godot::ColorRect *cap_vbar_parts_[kCaptureParts] = {};
	godot::ProgressBar *stamina_bar_ = nullptr;
	godot::Label *stamina_label_ = nullptr;
	godot::Label *resource_name_ = nullptr;   // resourceLabel
	godot::ProgressBar *resource_bar_ = nullptr;
	godot::Label *resource_stats_ = nullptr;  // resourceStats
	godot::ProgressBar *aura_bar_ = nullptr;
	godot::Label *aura_label_ = nullptr;
	godot::Label *stats_ = nullptr;
	godot::TextureRect *attack_icon_ = nullptr; // attackAndResistanceStats
	godot::Label *extra_ = nullptr; // resourceCarryingText
	godot::TextureRect *carry_icon_ = nullptr; // resourceCarryingIcon
	godot::Button *ungarrison_btn_ = nullptr;
	rtso::FogMirages mirages_; // the viewer's structures: live in sight, mirages in the fog
	// displayMultiple for a selection holding structures: the groups (units by template, then
	// structures by civ and type) with their counts and summed health.
	void fill_multiple(const rtso::Simulation &sim, const std::vector<uint32_t> &structs) {
		const godot::PackedInt32Array &selected_ids = host_->ui_selected_ids();
		auto uview = sim.units();
		std::vector<RtsoSelectionGrid::Group> groups = selection_groups(sim, uview,
				RtsoSelectionGrid::group_units(uview, (const uint32_t *)selected_ids.ptr(), selected_ids.size()), false);
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
				g.tip = g.name;
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
			capture_label_->set_text(rtso::seldet::capture(own, max_cp, "Capture Points:"));
		}
		portrait_->set_texture(groups.empty() ? godot::Ref<godot::Texture2D>()
				: groups.front().icon.is_valid() ? groups.front().icon : rtso::portraits::unit(groups.front().tmpl, groups.front().unit_type));
		set_title(godot::String::num_int64(total) + " selected");
		// displayMultiple's numberOfUnits tooltip: the units' Garrisonable size, every holder's
		// occupied slots, carried resources and the summed Loot of units and structures.
		{
			rtso::seldet::Amounts carrying, loot;
			int64_t garrison_size = 0;
			auto add_loot = [&loot](const rtso::LootInfo &l) {
				loot.res[rtso::RES_FOOD] += l.food;
				loot.res[rtso::RES_WOOD] += l.wood;
				loot.res[rtso::RES_STONE] += l.stone;
				loot.res[rtso::RES_GOLD] += l.metal;
				loot.xp += l.xp;
			};
			for (int s = 0; s < selected_ids.size(); ++s) {
				size_t i = 0;
				for (; i < uview.count; ++i)
					if (uview.id[i] == (uint32_t)selected_ids[s]) break;
				if (i == uview.count || !uview.alive[i]) continue;
				garrison_size += 1 + (int64_t)sim.get_transport_passenger_count(uview.id[i]);
				add_loot(sim.unit_details(i).loot);
				if (uview.carry[i] > 0 && uview.res_type_carried[i] < rtso::RES_COUNT) {
					carrying.res[uview.res_type_carried[i]] += uview.carry[i];
					loot.res[uview.res_type_carried[i]] += uview.carry[i];
				}
			}
			for (uint32_t id : structs)
				for (const auto &b : sim.buildings()) {
					if (b.id != id) continue;
					garrison_size += sim.get_garrison_count(b.id);
					add_loot(rtso::structure_loot(b.type, b.civ, b.half_size));
					break;
				}
			title_->set_tooltip_text(rtso::seldet::multiple_tooltip(garrison_size, carrying, loot));
		}
		show_multi(total, title_->get_tooltip_text(), hp, max_hp);
		if (max_cp > 0.0f) set_multi_capture(sim, owner, pts, max_cp);
		clear_owner();
		class_->set_text(godot::String::num_int64(groups.size()) + (groups.size() == 1 ? " type" : " types"));
		show_bar(xp_bar_, xp_label_, false);
		show_bar(aura_bar_, aura_label_, false);
		stats_->set_text("");
		clear_carrying();
		ungarrison_btn_->set_visible(false);
		sort_groups(groups);
		if (groups.size() > RtsoSelectionGrid::kMaxSelectionGroups) groups.resize(RtsoSelectionGrid::kMaxSelectionGroups);
		selection_grid_.update(groups, "");
	}

	RtsoSelectionGrid selection_grid_;
	RtsoSelectionGrid garrison_grid_;
	godot::Control *barter_host_ = nullptr;

	// 0.28's Selection panel (g_SelectionPanels.Selection setupButton): each group's tooltip
	// is its names, what its units carry (a trader its goods' total gain, while it has goods) and, for an
	// observer, the owner, whose colour then shows behind the icon.
	// selection.js getKeys: the panel's groups in sorted "p<player>&<template>" order, so
	// structures/ come before units/ and the 16-icon cap drops the same ones every time.
	static void sort_groups(std::vector<RtsoSelectionGrid::Group> &groups) {
		auto key = [](const RtsoSelectionGrid::Group &g) {
			String t;
			if (RtsoSelectionGrid::is_structure_key(g.key))
				t = String(rtso::portraits::structure_entry((uint8_t)((g.key >> 8) & 0xFF), (uint8_t)(g.key & 0xFF)).get("template", String()));
			else if (g.tmpl != rtso::CIV_UNIT_TEMPLATE_NONE) {
				t = "units/" + String::utf8(rtso::civ_unit_template(g.tmpl).id);
				// The rank's own template: _a (Advanced) sorts before _b, _e (Elite) after.
				const uint8_t rank = RtsoSelectionGrid::key_rank(g.key);
				if (t.ends_with("_b") && (rank == 2 || rank == 3)) t = t.substr(0, t.length() - 1) + (rank == 2 ? "a" : "e");
			}
			else
				t = "units/" + String(rtso::unit_type_info(g.unit_type).name);
			return "p" + String::num_int64(g.player) + "&" + t;
		};
		std::vector<std::pair<String, size_t>> order;
		for (size_t k = 0; k < groups.size(); ++k) order.push_back({key(groups[k]), k});
		std::stable_sort(order.begin(), order.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
		std::vector<RtsoSelectionGrid::Group> sorted;
		for (const auto &o : order) sorted.push_back(groups[o.second]);
		groups.swap(sorted);
	}

	std::vector<RtsoSelectionGrid::Group> selection_groups(const rtso::Simulation &sim,
			const rtso::Simulation::UnitView &uview, std::vector<RtsoSelectionGrid::Group> groups, bool cap = true) {
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
				if (RtsoSelectionGrid::group_key(uview.unit_type[i], tmpl, uview.rank[i]) != g.key) continue;
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
		}
		if (cap) {
			sort_groups(groups);
			if (groups.size() > RtsoSelectionGrid::kMaxSelectionGroups) groups.resize(RtsoSelectionGrid::kMaxSelectionGroups);
		}
		return groups;
	}

	// g_SelectionPanels.Garrison for a structure, ship, ram or siege tower: up to 12 icons,
	// one per template and owner, on the owner's colour. A click unloads one (Shift: all of
	// that type) when we own the holder or the units; others' units show disabled and grey
	// (not grey for an observer). False when nothing is inside.
	bool fill_garrison_grid(const rtso::Simulation &sim, uint32_t holder) {
		return fill_garrison_grid(sim, std::vector<uint32_t>{holder});
	}

	// Garrison.getItems: the units of every selected holder in one EntityGroups, so two
	// towers' archers make one icon.
	bool fill_garrison_grid(const rtso::Simulation &sim, const std::vector<uint32_t> &holders) {
		using namespace godot;
		const uint8_t me = host_->local_player();
		std::vector<uint32_t> inside;
		std::vector<uint32_t> in_own_holder; // units we may unload as the holder's owner
		for (uint32_t holder : holders) {
			uint8_t holder_owner = 0xFF;
			const std::vector<uint32_t> in = sim.holder_contents(holder, &holder_owner);
			inside.insert(inside.end(), in.begin(), in.end());
			if (holder_owner == me) in_own_holder.insert(in_own_holder.end(), in.begin(), in.end());
		}
		if (inside.empty()) return false;
		auto uview = sim.units();
		std::vector<int> own_holder_keys;
		for (uint32_t id : in_own_holder)
			for (size_t i = 0; i < uview.count; ++i)
				if (uview.id[i] == id) {
					own_holder_keys.push_back(RtsoSelectionGrid::owner_key(uview.unit_type[i],
							uview.tmpl ? uview.tmpl[i] : rtso::CIV_UNIT_TEMPLATE_NONE, uview.team[i], uview.rank[i]));
					break;
				}
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
		const bool observer = me >= rtso::MAX_PLAYERS;
		for (auto &g : groups) {
			// controlsPlayer(data.player) || controlsPlayer(entState.player): we own a holder of
			// these units, or the units.
			g.enabled = !observer && (me == g.player ||
					std::find(own_holder_keys.begin(), own_holder_keys.end(), g.key) != own_holder_keys.end());
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
