#ifndef RTSO_COMMAND_QUEUE_VIEW_H
#define RTSO_COMMAND_QUEUE_VIEW_H

// P4 stage-2 leaf widget: the command panel (0 A.D. gui/session/selection_panels.js
// Stance, Formation, Research, Construction, Upgrade and Training panels, unit_actions.js
// g_EntityCommands) with the roster list above it and the production queue under it.
// Rebuilds its buttons from the selection each frame; the presses go to the owner.

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/h_flow_container.hpp>
#include <godot_cpp/classes/item_list.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/panel.hpp>
#include <godot_cpp/classes/scroll_container.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/theme.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "../simulation/simulation.h"
#include "hotkeys.h"
#include "l10n.h"
#include "production_queue.h"
#include "user_config.h"

class RtsoCommandPanelHost {
public:
	virtual ~RtsoCommandPanelHost() = default;
	virtual rtso::Simulation *ui_sim() = 0;
	virtual uint8_t local_player() const = 0;
	virtual uint32_t ui_selected_building_id() const = 0;
	virtual const godot::PackedInt32Array &ui_selected_ids() const = 0;
	virtual int ui_batch_size() const = 0; // gui.session.batchtrainingsize
};

class RtsoCommandPanelView {
public:
	static constexpr int ROSTER_ANIMAL = 0x10000; // roster_list metadata flag: a corral animal
	static constexpr int ROSTER_TECH = 0x20000;   // roster_list metadata flag: a training unit's technology

	// owner: signal target (RtsoMain: the stance, formation, tech, build and command presses,
	// _on_roster_item_clicked). parent: the HUD layer the panel and roster go on.
	void build(godot::Object *owner, godot::Node *parent, const godot::Ref<godot::Theme> &theme,
			RtsoCommandPanelHost *host, RtsoProductionQueueHost *queue_host) {
		using namespace godot;
		owner_ = owner;
		host_ = host;
		panel_ = memnew(Panel);
		panel_->set_anchor(SIDE_LEFT, 1.0f);
		panel_->set_anchor(SIDE_RIGHT, 1.0f);
		panel_->set_anchor(SIDE_TOP, 1.0f);
		panel_->set_anchor(SIDE_BOTTOM, 1.0f);
		panel_->set_offset(SIDE_LEFT, -584.0f);
		panel_->set_offset(SIDE_RIGHT, -12.0f);
		panel_->set_offset(SIDE_TOP, -198.0f);
		panel_->set_offset(SIDE_BOTTOM, -10.0f);

		Ref<StyleBoxFlat> cmd_style;
		cmd_style.instantiate();
		cmd_style->set_bg_color(Color(0.10f, 0.11f, 0.15f, 0.94f));
		cmd_style->set_corner_radius_all(3);
		cmd_style->set_border_width_all(2);
		cmd_style->set_border_color(Color(0.90f, 0.75f, 0.31f, 1.0f));
		panel_->add_theme_stylebox_override("panel", cmd_style);
		if (theme.is_valid()) panel_->set_theme(theme);
		parent->add_child(panel_);

		roster_list_ = memnew(ItemList);
		roster_list_->set_anchor(SIDE_LEFT, 1.0f);
		roster_list_->set_anchor(SIDE_RIGHT, 1.0f);
		roster_list_->set_anchor(SIDE_TOP, 1.0f);
		roster_list_->set_anchor(SIDE_BOTTOM, 1.0f);
		roster_list_->set_offset(SIDE_LEFT, -584.0f);
		roster_list_->set_offset(SIDE_RIGHT, -12.0f);
		roster_list_->set_offset(SIDE_TOP, -326.0f);
		roster_list_->set_offset(SIDE_BOTTOM, -202.0f);
		roster_list_->set_max_columns(3);
		roster_list_->set_same_column_width(true);
		roster_list_->set_fixed_column_width(180);
		roster_list_->add_theme_font_size_override("font_size", 11);
		roster_list_->add_theme_stylebox_override("panel", cmd_style);
		if (theme.is_valid()) roster_list_->set_theme(theme);
		roster_list_->set_visible(false);
		roster_list_->connect("item_clicked", Callable(owner_, "_on_roster_item_clicked"));
		parent->add_child(roster_list_);

		title_ = memnew(Label);
		title_->set_position(Vector2(10, 6));
		title_->set_size(Vector2(552, 16));
		title_->add_theme_font_size_override("font_size", 12);
		title_->add_theme_color_override("font_color", Color(1.0f, 0.85f, 0.4f));
		title_->set_text("Commands & Actions");
		panel_->add_child(title_);

		// Stances Bar (Aggressive, Defensive, Stand Ground, Hold, Passive)
		stances_bar_ = memnew(HBoxContainer);
		stances_bar_->set_position(Vector2(10, 24));
		stances_bar_->set_size(Vector2(552, 26));
		stances_bar_->add_theme_constant_override("separation", 6);
		panel_->add_child(stances_bar_);

		// Indexed by UnitStance; laid out in 0 A.D. g_Stances order (violent, aggressive,
		// defensive, passive, standground: UnitAI's selectable stances when a template names
		// none), with selection_panels_helpers.js names and tooltips.
		const char *stance_names[5] = {"Aggressive", "Defensive", "Standground", "Violent", "Passive"};
		const char *stance_tips[5] = {
			"Attack nearby opponents",
			"Attack nearby opponents, chase a short distance and return to the original location",
			"Attack opponents in range, but don't move",
			"Attack nearby opponents, focus on attackers and chase while visible",
			"Flee if attacked"
		};
		// 0 A.D. stance icons (art/textures/ui/session/stances), same order as the names.
		const char *stance_icons[5] = {
			"res://assets/textures/ui/stances/aggressive.png",
			"res://assets/textures/ui/stances/defensive.png",
			"res://assets/textures/ui/stances/standground.png",
			"res://assets/textures/ui/stances/violent.png",
			"res://assets/textures/ui/stances/passive.png"
		};
		const int stance_order[5] = {rtso::STANCE_VIOLENT, rtso::STANCE_AGGRESSIVE, rtso::STANCE_DEFENSIVE,
				rtso::STANCE_PASSIVE, rtso::STANCE_STAND_GROUND};
		for (int s : stance_order) {
			Button *sb = memnew(Button);
			sb->set_custom_minimum_size(Vector2(95, 24));
			sb->set_text(stance_names[s]);
			sb->set_tooltip_text(String(stance_names[s]) + "\n" + stance_tips[s]);
			sb->set_toggle_mode(true); // pressed = some selected unit holds it (IsStanceSelected)
			Ref<Texture2D> s_ico = rtso::load_ui_texture(stance_icons[s]);
			if (s_ico.is_valid()) {
				sb->set_button_icon(s_ico);
				sb->set_expand_icon(true);
			}
			sb->add_theme_font_size_override("font_size", 9);
			sb->connect("pressed", Callable(owner_, "_on_stance_selected").bind(s));
			stances_bar_->add_child(sb);
			stance_buttons_[s] = sb;
		}
		stances_bar_->set_visible(false);

		// Actions & Formations Bar
		actions_bar_ = memnew(HBoxContainer);
		actions_bar_->set_position(Vector2(10, 52));
		actions_bar_->set_size(Vector2(552, 26));
		actions_bar_->add_theme_constant_override("separation", 2);
		panel_->add_child(actions_bar_);

		// Production Bar (Training & Tech)
		// The build menu has 25+ structures: the buttons wrap onto rows and
		// scroll inside the panel instead of running off the screen at 1280x720.
		production_scroll_ = memnew(ScrollContainer);
		production_scroll_->set_position(Vector2(10, 80));
		production_scroll_->set_size(Vector2(552, 40));
		production_scroll_->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
		panel_->add_child(production_scroll_);
		production_bar_ = memnew(HFlowContainer);
		production_bar_->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		production_bar_->add_theme_constant_override("h_separation", 4);
		production_bar_->add_theme_constant_override("v_separation", 2);
		production_scroll_->add_child(production_bar_);

		// Visual 8-Slot Production Queue Bar (ProductionQueue.js parity)
		queue_bar_ = memnew(HBoxContainer);
		queue_bar_->set_position(Vector2(10, 122));
		queue_bar_->set_size(Vector2(552, 54));
		queue_bar_->add_theme_constant_override("separation", 6);
		panel_->add_child(queue_bar_);

		production_queue_.build(owner, queue_bar_, queue_host);
	}

	godot::Panel *panel() const { return panel_; }
	godot::HFlowContainer *production_bar() const { return production_bar_; }
	godot::ItemList *roster_list() const { return roster_list_; }
	RtsoProductionQueue &production_queue() { return production_queue_; }
	// The selected training unit whose roster is shown, else 0.
	uint32_t trainer_unit_id() const { return trainer_unit_id_; }

	void update() {
		using namespace godot;
		if (!panel_) return;
		rtso::Simulation *sim = host_->ui_sim();
		const godot::PackedInt32Array &selected_ids = host_->ui_selected_ids();
		const uint32_t selected_building_id = host_->ui_selected_building_id();

		// g_SelectionPanels.Stance: shown when only units are selected, the stances
		// some selected unit holds highlighted (GuiInterface IsStanceSelected).
		if (stances_bar_ && sim) {
			bool held[5] = {};
			bool any = false;
			if (selected_building_id == 0 && !selected_ids.is_empty()) {
				auto uv = sim->units();
				for (int s = 0; s < selected_ids.size(); ++s)
					for (size_t i = 0; i < uv.count; ++i)
						if (uv.id[i] == (uint32_t)selected_ids[s]) {
							if (uv.stance[i] < 5) held[uv.stance[i]] = true;
							any = true;
							break;
						}
			}
			stances_bar_->set_visible(any);
			for (int st = 0; st < 5; ++st)
				if (stance_buttons_[st] && stance_buttons_[st]->is_pressed() != held[st])
					stance_buttons_[st]->set_pressed_no_signal(held[st]);
		}

		// The buttons are built after the current ones each frame, then
		// keep_buttons() keeps whichever set is older when both look and
		// act the same. Replacing them every frame meant a click's press landed on
		// one Button and its release on the next, so pressed never fired.
		const int old_actions = actions_bar_->get_child_count();
		const int old_production = production_bar_->get_child_count();

		if (selected_building_id != 0) {
			const auto &bldgs = sim->buildings();
			const rtso::Simulation::Building *found_b = nullptr;
			for (const auto &b : bldgs) {
				if (b.id == selected_building_id) { found_b = &b; break; }
			}

			// Update 8-slot Visual Production Queue (ProductionQueue.js parity)
			production_queue_.update(found_b);
			update_roster_list(found_b);

			if (found_b && found_b->build_progress >= 1.0f) {
				uint8_t pciv = sim->player_civ(local_player());
				// Units train from the civ's own list only (update_roster_list, 0 A.D. ProductionQueue's
				// per-template Trainer/Entities), never from generic class buttons.

				// Technologies researched here (0 A.D. ProductionQueue "technologies"):
				// one button per tech whose researcher is this building type, greyed
				// out with the reason research_check() gives.
				for (int t = 1; t < rtso::TECH_COUNT; ++t) {
					const rtso::TechInfo &ti = rtso::tech_info((uint8_t)t);
					if (!rtso::building_researches_tech(found_b->type, (uint8_t)t, pciv) || !rtso::tech_available_to_civ((uint8_t)t, pciv)) continue;
					// Researcher.js GetTechnologiesList: a chain shows one slot, its next step; a technology
					// whose superseded one is offered here waits until that one is researched or in progress.
					if (const uint8_t sup = rtso::tech_supersedes((uint8_t)t); sup != rtso::TECH_NONE
							&& rtso::building_researches_tech(found_b->type, sup, pciv) && rtso::tech_available_to_civ(sup, pciv)) {
						const rtso::Simulation::ResearchCheck sc = sim->research_check(local_player(), sup, 0);
						if (sc != rtso::Simulation::TECH_ERR_RESEARCHED && sc != rtso::Simulation::TECH_ERR_IN_PROGRESS) continue;
					}
					rtso::Simulation::ResearchCheck rc = sim->research_check(local_player(), (uint8_t)t, found_b->id);
					if (rc == rtso::Simulation::TECH_ERR_RESEARCHED || rc == rtso::Simulation::TECH_ERR_CIV
					    || rc == rtso::Simulation::TECH_ERR_PAIRED || rc == rtso::Simulation::TECH_ERR_DISABLED) continue;
					Button *btn = memnew(Button);
					String cost;
					auto add_cost = [&](int v, const char *suffix) {
						if (v <= 0) return;
						if (!cost.is_empty()) cost += ",";
						cost += String::num_int64(v) + suffix;
					};
					// Researcher.js GetTechnologyData: the price after this researcher's TechCostMultiplier,
					// team bonuses and the Library of Alexandria, as cmd_research charges it.
					float paid[rtso::RES_COUNT], rtime = 0.0f;
					sim->tech_price(local_player(), (uint8_t)t, found_b->type, paid, rtime);
					add_cost((int)paid[rtso::RES_FOOD], "f");
					add_cost((int)paid[rtso::RES_WOOD], "w");
					add_cost((int)paid[rtso::RES_GOLD], "g");
					add_cost((int)paid[rtso::RES_STONE], "s");
					btn->set_text(String::utf8(ti.name) + (cost.is_empty() ? String() : " (" + cost + ")"));
					if (t == rtso::TECH_PHASE_TOWN || t == rtso::TECH_PHASE_CITY) {
						btn->set_button_icon(rtso::load_ui_texture(t == rtso::TECH_PHASE_TOWN
								? "res://assets/textures/ui/phases/town.png" : "res://assets/textures/ui/phases/city.png"));
						btn->set_icon_alignment(HORIZONTAL_ALIGNMENT_CENTER);
						btn->add_theme_constant_override("icon_max_width", 20);
					}
					// 0.28 Research panel: names, tooltip, cost, requirements, needed resources.
					String tip = String::utf8(ti.name);
					if (ti.desc && *ti.desc) tip += "\n" + String::utf8(ti.desc);
					const rtso::CostTip tech_cost{(int64_t)paid[rtso::RES_FOOD], (int64_t)paid[rtso::RES_WOOD], (int64_t)paid[rtso::RES_STONE],
							(int64_t)paid[rtso::RES_GOLD], 0, (int64_t)std::ceil(rtime)};
					const String tech_cost_tip = rtso::cost_tooltip(tech_cost);
					if (!tech_cost_tip.is_empty()) tip += "\n" + tech_cost_tip;
					String why;
					switch (rc) {
						case rtso::Simulation::TECH_ERR_IN_PROGRESS: why = "Already being researched"; break;
						case rtso::Simulation::TECH_ERR_PHASE:
							why = requires_text(ti.required_phase >= 3 ? "City Phase" : "Town Phase"); break;
						case rtso::Simulation::TECH_ERR_PREREQUISITE: {
							const uint8_t sup = rtso::tech_supersedes((uint8_t)t);
							const bool need_prev = sup != rtso::TECH_NONE && !sim->player(local_player()).has_tech(sup);
							tip += "\n" + requires_text(String::utf8(rtso::tech_info(need_prev ? sup : ti.prerequisite).name));
							btn->set_disabled(true);
							break;
						}
						case rtso::Simulation::TECH_ERR_STRUCTURES:
							why = t == rtso::TECH_PHASE_CITY ? "Requires 3 Town Phase structures"
									: "Requires 5 Village Phase structures"; break;
						case rtso::Simulation::TECH_ERR_COST: why = needed_text(tech_cost); break;
						default: break;
					}
					if (!why.is_empty()) {
						tip += "\n" + owner_->tr(why);
						if (rc != rtso::Simulation::TECH_ERR_COST) btn->set_disabled(true);
					}
					btn->set_tooltip_text(tip);
					btn->connect("pressed", Callable(owner_, "_on_tech_pressed").bind(t));
					production_bar_->add_child(btn);
				}

				// Gate lock (0 A.D. Gate.js): a locked gate stays shut for everyone.
				if (found_b->team == local_player() && rtso::is_gate_type(found_b->type)) {
					Button *btn = memnew(Button);
					btn->set_text(found_b->gate_locked ? "Unlock gate" : "Lock gate");
					btn->set_tooltip_text(found_b->gate_locked ? "Let your and your allies' units through again"
							: "Keep the gate shut, even for your own units");
					btn->connect("pressed", Callable(owner_, "_on_gate_lock_pressed").bind(found_b->gate_locked ? 0 : 1));
					production_bar_->add_child(btn);
				}

				// Dropsite sharing (0 A.D. ResourceDropsite.js): shown once the owner has Diaspora.
				if (found_b->team == local_player() && rtso::Simulation::is_dropsite_type(found_b->type) &&
						sim->player(local_player()).has_tech(rtso::TECH_UNLOCK_SHARED_DROPSITES)) {
					Button *btn = memnew(Button);
					btn->set_text(found_b->dropsite_shared ? "Stop sharing dropsite" : "Share dropsite");
					btn->set_tooltip_text(found_b->dropsite_shared ? "Allies can no longer drop resources here"
							: "Let allies with Diaspora drop resources here");
					btn->connect("pressed", Callable(owner_, "_on_dropsite_sharing_pressed").bind(found_b->dropsite_shared ? 0 : 1));
					production_bar_->add_child(btn);
				}

				// Alert (0 A.D. AlertRaiser.js): civilians take shelter, then go back to work.
				rtso::AlertRaiserInfo alert;
				if (found_b->team == local_player() && found_b->build_progress >= 1.0f &&
						rtso::building_alert_raiser(found_b->type, alert)) {
					const char *who = alert.traders ? "traders" : "women";
					for (int end = 0; end < 2; ++end) {
						Button *btn = memnew(Button);
						btn->set_text(end ? "End alert" : "Raise alert");
						btn->set_tooltip_text(end ? String("Send nearby ") + who + " back to work"
								: String("Send nearby ") + who + " into the nearest structures with room");
						btn->connect("pressed", Callable(owner_, "_on_alert_pressed").bind(end));
						production_bar_->add_child(btn);
					}
				}

				// Autoqueue (0 A.D. ProductionQueue.js): each finished batch is queued again.
				if (found_b->team == local_player() && found_b->build_progress >= 1.0f && roster_list_ &&
						roster_list_->get_item_count() > 0) {
					Button *btn = memnew(Button);
					btn->set_text(found_b->autoqueue ? "Autoqueue: on" : "Autoqueue: off");
					btn->set_tooltip_text(found_b->autoqueue ? "Stop repeating the batches this structure trains"
							: "Queue each batch again when it finishes, paying for it again");
					btn->connect("pressed", Callable(owner_, "_on_autoqueue_pressed").bind(found_b->autoqueue ? 0 : 1));
					production_bar_->add_child(btn);
				}

				// Structure upgrade (0 A.D. Upgrade.js), or cancel one in progress.
				if (found_b->team == local_player()) {
					if (found_b->upgrade_to != rtso::BLD_TYPE_COUNT) {
						Button *btn = memnew(Button);
						btn->set_text(String("Cancel upgrade to ") + rtso::building_type_name(found_b->upgrade_to));
						btn->set_tooltip_text("Stop upgrading and get the resources back");
						btn->connect("pressed", Callable(owner_, "_on_upgrade_pressed").bind(-1));
						production_bar_->add_child(btn);
					} else {
						for (int choice = 0; choice < rtso::building_upgrade_count(found_b->type, pciv); ++choice) {
							const rtso::BuildingUpgradeInfo up = rtso::building_upgrade(found_b->type, pciv, choice);
							const rtso::Simulation::UpgradeCheck check = sim->upgrade_check(local_player(), found_b->id, choice);
							// Not this piece's (only wall_long and palisades_long become gates).
							if (check == rtso::Simulation::UPGRADE_ERR_NONE) continue;
							Button *btn = memnew(Button);
							String cost;
							auto add_cost = [&](int v, const char *suffix) {
								if (v <= 0) return;
								if (!cost.is_empty()) cost += ",";
								cost += String::num_int64(v) + suffix;
							};
							add_cost(up.food_cost, "f");
							add_cost(up.wood_cost, "w");
							add_cost(up.gold_cost, "g");
							add_cost(up.stone_cost, "s");
							btn->set_text(String("Upgrade to ") + String::utf8(rtso::building_type_name(up.to)) + (cost.is_empty() ? String() : " (" + cost + ")"));
							const rtso::CostTip up_cost{up.food_cost, up.wood_cost, up.stone_cost, up.gold_cost, 0, (int64_t)std::ceil(up.time)};
							String tip = String::utf8(up.tooltip ? up.tooltip : "") + "\n" + rtso::cost_tooltip(up_cost);
							String why;
							switch (check) {
								case rtso::Simulation::UPGRADE_ERR_BUSY: why = "Busy: finish or cancel production first"; break;
								case rtso::Simulation::UPGRADE_ERR_PHASE: why = requires_text(up.required_phase >= 3 ? "City Phase" : "Town Phase"); break;
								case rtso::Simulation::UPGRADE_ERR_COST: why = needed_text(up_cost); break;
								case rtso::Simulation::UPGRADE_ERR_LIMIT: why = "Limit reached"; break;
								default: break;
							}
							if (!why.is_empty()) {
								tip += "\n" + owner_->tr(why);
								btn->set_disabled(true);
							}
							btn->set_tooltip_text(tip);
							btn->connect("pressed", Callable(owner_, "_on_upgrade_pressed").bind(choice));
							production_bar_->add_child(btn);
						}
					}
				}
			}
			// The structure's command row (unit_actions.js g_EntityCommands).
			if (found_b && found_b->team == local_player()) {
				const int inside = (int)found_b->garrisoned_units.size();
				if (inside > 0)
					add_entity_command("unload-all", "Unload All", "session.unload",
							"Unload All\nOrder all units to leave the selected entities.", "garrison-out.png", true, inside);
				// isUndeletable: you must hold half the capture points.
				if (found_b->max_capture_points > 0.0f &&
						found_b->capture_points_by_team[found_b->team] < found_b->max_capture_points * 0.5f)
					add_entity_command("delete", "Delete", "",
							"You cannot destroy this entity as you own less than half the capture points", "kill_small_disabled.png", false);
				else
					add_entity_command("delete", "Delete", "session.kill",
							String("Self-Destruct\nDestroy the selected entities.\nUse ") +
									rtso::hotkeys::table().label("session.noconfirmation").c_str() + " to avoid the confirmation dialog.",
							"kill_small.png");
				if (found_b->has_rally_point)
					add_entity_command("focus-rally", "Focus on Rally Point", "camera.rallypointfocus",
							"Focus on Rally Point\nCenter the view on the selected rally point.", "focus-rally.png");
				if (found_b->type == rtso::BLD_MARKET && found_b->build_progress >= 1.0f)
					add_entity_command("select-trading-goods", "Barter & Trade", "session.gui.barter.toggle",
							"Barter & Trade\nOpen the dialog for managing resource trading and bartering.", "economics.png");
			}
		} else {
			// Non-building selected: clear visual queue slots, or show a training unit's (one selected).
			trainer_unit_id_ = 0;
			if (selected_ids.size() != 1 || !update_unit_roster((uint32_t)selected_ids[0])) {
				production_queue_.update(nullptr);
				update_roster_list(nullptr);
			}

			if (!selected_ids.is_empty()) {
				auto uview = sim->units();

				// 0 A.D. gui/session/selection_panels.js g_SelectionPanels.Formation: one button per
				// formation the player has (Player.js GetFormations), disabled with the template's
				// DisabledTooltip unless the selection can form it (Commands.js CanMoveEntsIntoFormation),
				// highlighted when a selected unit is in it (GuiInterface IsFormationSelected).
				static const char *const kFormationIcons[rtso::FORMATION_COUNT] = {
					"null", "line_closed", "box", "column", "phalanx", "wedge", "skirmish", "line_open",
					"column_open", "flank", "battle_line", "ship_line", "ship_column", "flank", "syntagma",
					"testudo", "testudo"};
				const uint32_t *sel_ids = (const uint32_t *)selected_ids.ptr();
				const size_t sel_n = (size_t)selected_ids.size();
				uint32_t in_use = 0;       // formations a selected unit is in (bit 0: a unit in none)
				bool any_can_form = false; // the panel only shows for units that take formations
				for (size_t i = 0; i < sel_n; ++i) {
					for (size_t u = 0; u < uview.count; ++u) {
						if (uview.id[u] != sel_ids[i]) continue;
						in_use |= 1u << (uview.formation[u] < rtso::FORMATION_COUNT ? uview.formation[u] : 0);
						break;
					}
					for (uint8_t f = 1; f < rtso::FORMATION_COUNT && !any_can_form; ++f)
						any_can_form = sim->unit_can_use_formation(sel_ids[i], f);
				}
				const uint32_t available = any_can_form ? sim->player_formation_mask(local_player()) : 0u;
				for (uint8_t f = 0; f < rtso::FORMATION_COUNT; ++f) {
					if (!((available >> f) & 1u)) continue;
					const rtso::FormationTemplate &ft = rtso::formation_template(f);
					const bool enabled = f == rtso::FORMATION_NONE || sim->can_move_into_formation(sel_ids, sel_n, f);
					// The Formation panel's buttons are icons; the name is on the tooltip.
					Button *fb = memnew(Button);
					fb->set_custom_minimum_size(Vector2(26, 26));
					fb->set_clip_text(true);
					String tip = String::utf8(ft.name);
					const char *why = enabled ? ft.tooltip : ft.disabled_tooltip;
					if (why && *why) tip += String("\n") + String::utf8(why);
					fb->set_tooltip_text(tip);
					Ref<Texture2D> f_ico = rtso::load_ui_texture(String("res://assets/textures/ui/formations/") + kFormationIcons[f] + ".png");
					if (f_ico.is_valid()) {
						fb->set_button_icon(f_ico);
						fb->set_expand_icon(true);
						fb->set_icon_alignment(HORIZONTAL_ALIGNMENT_CENTER);
					} else {
						fb->set_text(String::utf8(ft.name));
						fb->set_custom_minimum_size(Vector2(44, 26));
					}
					fb->add_theme_font_size_override("font_size", 8);
					if ((in_use >> f) & 1u) {
						// IsFormationSelected: the formation a selected unit is in is lit.
						fb->add_theme_color_override("font_color", Color(1.0f, 0.85f, 0.2f));
						fb->add_theme_color_override("icon_normal_color", Color(1.0f, 0.85f, 0.2f));
						fb->add_theme_color_override("icon_hover_color", Color(1.0f, 0.9f, 0.4f));
					}
					fb->set_disabled(!enabled);
					fb->connect("pressed", Callable(owner_, "_on_formation_selected").bind((int)f));
					actions_bar_->add_child(fb);
				}

				// Villager Build Menu
				bool has_worker = false;
				for (int i = 0; i < selected_ids.size(); ++i) {
					for (size_t u = 0; u < uview.count; ++u) {
						if (uview.id[u] == (uint32_t)selected_ids[i]) {
							uint8_t ut = uview.unit_type[u];
							if (rtso::unit_can_build(ut, uview.tmpl[u])) {
								has_worker = true;
								break;
							}
						}
					}
				}

				// 0 A.D. Pack / Unpack buttons (gui/session unit_actions.js "pack"): shown when the
				// selection has siege engines with a Pack component, enabled for the state they can go to.
				bool can_pack = false, can_unpack = false;
				for (int i = 0; i < selected_ids.size(); ++i) {
					for (size_t u = 0; u < uview.count; ++u) {
						if (uview.id[u] != (uint32_t)selected_ids[i] || !uview.alive[u]) continue;
						const uint8_t ps = uview.pack_state[u];
						if (ps == rtso::PACK_UNPACKED || ps == rtso::PACK_UNPACKING) can_pack = true;
						if (ps == rtso::PACK_PACKED || ps == rtso::PACK_PACKING) can_unpack = true;
						break;
					}
				}
				if (can_pack || can_unpack) {
					static const struct { const char *label; const char *tip; const char *icon; int pack; } kPackButtons[2] = {
						{"Pack", "Pack the siege engine so it can move (10 s)", "res://assets/textures/ui/archive/icons/pack_fg.png", 1},
						{"Unpack", "Unpack the siege engine so it can attack (10 s)", "res://assets/textures/ui/archive/icons/unpack.png", 0},
					};
					for (const auto &pb : kPackButtons) {
						Button *btn = memnew(Button);
						btn->set_text(pb.label);
						btn->set_tooltip_text(pb.tip);
						Ref<Texture2D> p_ico = rtso::load_ui_texture(pb.icon);
						if (p_ico.is_valid()) {
							btn->set_button_icon(p_ico);
							btn->add_theme_constant_override("icon_max_width", 20);
						}
						btn->set_disabled(pb.pack ? !can_pack : !can_unpack);
						btn->connect("pressed", Callable(owner_, "_on_pack_pressed").bind(pb.pack));
						production_bar_->add_child(btn);
					}
				}

				// 0 A.D. unit upgrades (selection_panels.js "Upgrade"): one button per target the
				// selected units can upgrade to, enabled while any of them can start it, and
				// a cancel button while any of them is upgrading.
				{
					uint16_t targets[8];
					int target_n = 0;
					bool any_upgrading = false;
					for (int i = 0; i < selected_ids.size(); ++i) {
						const uint32_t id = (uint32_t)selected_ids[i];
						if (sim->unit_upgrade_of(id)) any_upgrading = true;
						for (size_t u = 0; u < uview.count; ++u) {
							if (uview.id[u] != id || uview.team[u] != local_player()) continue;
							for (const rtso::CivUnitUpgrade *up = rtso::civ_unit_upgrade_table(); up->from != 0xFFFF; ++up) {
								if (up->from != uview.tmpl[u] || (up->from_rank && uview.rank[u] < up->from_rank)) continue;
								bool seen = false;
								for (int t = 0; t < target_n; ++t) seen = seen || targets[t] == up->to;
								if (!seen && target_n < 8) targets[target_n++] = up->to;
							}
							break;
						}
					}
					for (int t = 0; t < target_n; ++t) {
						const uint16_t to = targets[t];
						const rtso::CivUnitUpgrade *row = nullptr;
						String why = "Already upgrading";
						for (int i = 0; i < selected_ids.size(); ++i) {
							const rtso::Simulation::UpgradeCheck c = sim->unit_upgrade_check(local_player(), (uint32_t)selected_ids[i], to);
							if (c == rtso::Simulation::UPGRADE_OK) { why = String(); break; }
							if (c == rtso::Simulation::UPGRADE_ERR_COST) why = "\x01"; // needed resources, below
							else if (c == rtso::Simulation::UPGRADE_ERR_PHASE) why = requires_text("City Phase");
							else if (c == rtso::Simulation::UPGRADE_ERR_LIMIT) why = "Limit reached";
						}
						for (const rtso::CivUnitUpgrade *up = rtso::civ_unit_upgrade_table(); up->from != 0xFFFF && !row; ++up)
							if (up->to == to) row = up;
						String cost;
						auto add_cost = [&](int v, const char *suffix) {
							if (v <= 0) return;
							if (!cost.is_empty()) cost += ",";
							cost += String::num_int64(v) + suffix;
						};
						add_cost(row->food_cost, "f");
						add_cost(row->wood_cost, "w");
						add_cost(row->gold_cost, "g");
						add_cost(row->stone_cost, "s");
						Button *btn = memnew(Button);
						btn->set_text(String("Upgrade to ") + String::utf8(rtso::civ_unit_template(to).name) + (cost.is_empty() ? String() : " (" + cost + ")"));
						const rtso::CivUnitTemplate &tt = rtso::civ_unit_template(to);
						const rtso::CostTip row_cost{row->food_cost, row->wood_cost, row->stone_cost, row->gold_cost, 0, (int64_t)std::ceil(row->time)};
						String tip = rtso::entity_names(String::utf8(tt.name), String::utf8(tt.specific_name ? tt.specific_name : "")) +
								"\n" + rtso::cost_tooltip(row_cost);
						if (why == "\x01") why = needed_text(row_cost);
						if (!why.is_empty()) {
							tip += "\n" + owner_->tr(why);
							btn->set_disabled(true);
						}
						btn->set_tooltip_text(tip);
						btn->connect("pressed", Callable(owner_, "_on_unit_upgrade_pressed").bind((int)to));
						production_bar_->add_child(btn);
					}
					if (any_upgrading) {
						Button *btn = memnew(Button);
						btn->set_text("Cancel upgrade");
						btn->set_tooltip_text("Stop upgrading and get the resources back");
						btn->connect("pressed", Callable(owner_, "_on_unit_upgrade_pressed").bind(-1));
						production_bar_->add_child(btn);
					}
				}

				// The command row, g_EntityCommands in unit_actions.js order: each shows when
				// its getInfo would (any selected unit it applies to).
				{
					bool any_guarding = false, any_busy = false, any_work = false, any_fighter = false;
					bool any_guard = false, any_can_add_guard = false, any_garrison = false;
					int passengers = 0;
					for (int s = 0; s < selected_ids.size(); ++s) {
						const uint32_t id = (uint32_t)selected_ids[s];
						for (size_t u = 0; u < uview.count; ++u) {
							if (uview.id[u] != id) continue;
							const uint8_t ut = uview.unit_type[u];
							const uint16_t tm = uview.tmpl[u];
							const rtso::TechClasses cls = tm < rtso::CIV_UNIT_TEMPLATE_COUNT ? rtso::civ_unit_template(tm).tech_classes : rtso::unit_tech_classes(ut);
							const bool guarding = sim->unit_is_guarding(id);
							any_guarding = any_guarding || guarding;
							any_busy = any_busy || (sim->unit_has_order(id) && !guarding); // "stop": not idle, not guarding
							any_work = any_work || sim->unit_has_work_order(id);
							// "call-to-arms": Soldier, Warship, Siege or Healer.
							any_fighter = any_fighter || (cls & (rtso::TC_SOLDIER | rtso::TC_WARSHIP | rtso::TC_SIEGE | rtso::TC_HEALER)) != 0;
							// UnitAI CanGuard / CanPatrol: soldiers, warships and siege.
							const bool can_guard = (cls & (rtso::TC_SOLDIER | rtso::TC_WARSHIP | rtso::TC_SIEGE)) != 0;
							any_guard = any_guard || can_guard;
							any_can_add_guard = any_can_add_guard || (can_guard && !guarding);
							any_garrison = any_garrison || !rtso::unit_is_ship(ut); // Garrisonable
							if (sim->is_transport(id)) passengers += sim->get_transport_passenger_count(id);
							break;
						}
					}
					if (passengers > 0)
						add_entity_command("unload-all", "Unload All", "session.unload",
								"Unload All\nOrder all units to leave the selected entities.", "garrison-out.png", true, passengers);
					add_entity_command("delete", "Delete", "session.kill",
							String("Self-Destruct\nDestroy the selected entities.\nUse ") +
									rtso::hotkeys::table().label("session.noconfirmation").c_str() + " to avoid the confirmation dialog.",
							"kill_small.png");
					if (any_busy)
						add_entity_command("stop", "Abort", "session.stop",
								"Abort\nCancel the current orders for the selected units.", "stop.png");
					if (any_fighter)
						add_entity_command("call-to-arms", "Call to Arms", "session.calltoarms",
								"Attack\nSend the selected units on attack move to the specified location after dropping resources.",
								"call-to-arms.png");
					if (any_garrison)
						add_entity_command("garrison", "Garrison", "session.garrison",
								"Garrison\nOrder the selected units to garrison in a structure or another unit.", "garrison.png");
					if (has_worker)
						add_entity_command("repair", "Repair", "session.repair",
								"Repair\nOrder the selected units to repair a structure, ship, or siege engine.", "repair.png");
					if (any_work)
						add_entity_command("back-to-work", "Back to Work", "session.backtowork",
								"Back to Work\nOrder the selected units to resume their work.", "back-to-work.png");
					if (any_can_add_guard)
						add_entity_command("add-guard", "Guard", "session.guard",
								"Guard\nOrder the selected units to guard a structure or unit.", "add-guard.png");
					if (any_guarding)
						add_entity_command("remove-guard", "Abort Guard", "",
								"Abort Guard\nOrder the selected units to stop guarding.", "remove-guard.png");
					if (any_guard)
						add_entity_command("patrol", "Patrol", "session.patrol",
								"Patrol\nOrder to repeatedly go to a point and come back, attacking all enemies along the way.",
								"patrol.png");
				}

				if (has_worker) {
					// Build menu in 0 A.D. order; cost and phase come from the building table.
					static const int b_types[] = {
						rtso::BLD_HOUSE, rtso::BLD_STOREHOUSE, rtso::BLD_FARMSTEAD, rtso::BLD_FIELD,
						rtso::BLD_CORRAL, rtso::BLD_DOCK, rtso::BLD_BARRACKS, rtso::BLD_STABLE,
						rtso::BLD_RANGE, rtso::BLD_SENTRY_TOWER, rtso::BLD_OUTPOST, rtso::BLD_PALISADE,
						rtso::BLD_FORGE, rtso::BLD_MARKET, rtso::BLD_TEMPLE, rtso::BLD_TOWER,
						rtso::BLD_EMBASSY, rtso::BLD_WALL_SEGMENT, rtso::BLD_WALL_TOWER, rtso::BLD_GATE,
						rtso::BLD_FORTRESS, rtso::BLD_ARSENAL, rtso::BLD_ELEPHANT_STABLE, rtso::BLD_THEATER,
						rtso::BLD_WONDER
					};
					static const char *kPhaseNames[4] = {"", "Village", "Town", "City"};
					// Generic structures the civ's 0 A.D. Builder lists name, then the civ's
					// own (0 A.D. structures/<civ>/).
					const uint8_t my_civ = sim->player_civ(local_player());
					std::vector<int> menu;
					for (int bt : b_types)
						if (rtso::civ_can_build(my_civ, (uint8_t)bt)) menu.push_back(bt);
					for (int bt = rtso::CIV_BUILDING_FIRST; bt < rtso::BLD_TYPE_COUNT; ++bt)
						if (rtso::civ_can_build(my_civ, (uint8_t)bt)) menu.push_back(bt);
					// The map script's SetDisabledTemplates (Survival of the Fittest) hides those structures.
					menu.erase(std::remove_if(menu.begin(), menu.end(), [&](int bt) {
						return sim->building_disabled_by_trigger(local_player(), (uint8_t)bt);
					}), menu.end());
					for (int bt : menu) {
						const rtso::BuildingTypeInfo bi = rtso::civ_building_type_info(my_civ, (uint8_t)bt); // the big house
						// The price this player pays (0.28 GetTemplateData: civ, team and technology modifiers).
						float price[rtso::RES_COUNT];
						sim->structure_cost_for(local_player(), (uint8_t)bt, price);
						String cost;
						auto add_cost = [&](int amount, const char *suffix) {
							if (amount <= 0) return;
							if (!cost.is_empty()) cost += ",";
							cost += String::num_int64(amount) + suffix;
						};
						add_cost((int)price[rtso::RES_FOOD], "f");
						add_cost((int)price[rtso::RES_WOOD], "w");
						add_cost((int)price[rtso::RES_GOLD], "g");
						add_cost((int)price[rtso::RES_STONE], "s");
						uint8_t need = rtso::building_required_phase((uint8_t)bt);
						// 0.28's Construction panel: an icon grid, name and cost on the tooltip.
						Button *bb = memnew(Button);
						const String label = String::utf8(bi.name) + (cost.is_empty() ? String() : " (" + cost + ")");
						bb->set_meta("label", label);
						// 0.28's Construction tooltip: names, Identity/Tooltip, cost and time, garrison,
						// population bonus (selection_panels.js Construction setupButton).
						const Dictionary text = structure_template_text(my_civ, bt);
						const String generic = text.get("generic_name", String::utf8(bi.name));
						const String specific = text.get("specific_name", String());
						String tip = specific.is_empty() || specific == generic ? generic : specific + String(" (") + generic + String(")");
						const String identity_tip = text.get("tooltip", String());
						if (!identity_tip.is_empty()) tip += "\n" + identity_tip;
						const rtso::CostTip b_cost{(int64_t)price[rtso::RES_FOOD], (int64_t)price[rtso::RES_WOOD],
								(int64_t)price[rtso::RES_STONE], (int64_t)price[rtso::RES_GOLD], 0,
								(int64_t)std::round(sim->structure_build_time_for(local_player(), (uint8_t)bt))};
						if (!rtso::cost_tooltip(b_cost).is_empty()) tip += "\n" + rtso::cost_tooltip(b_cost);
						const int garrison = rtso::structure_garrison_max((uint8_t)bt, my_civ);
						if (garrison > 0)
							tip += "\n" + rtso::sprintf0(rtso::tr0("%(label)s: %(garrisonLimit)s"),
									{{"label", rtso::tr0("Garrison Limit")}, {"garrisonLimit", String::num_int64(garrison)}});
						const int pop_bonus = sim->structure_pop_bonus((uint8_t)bt, local_player());
						if (pop_bonus > 0)
							tip += "\n" + rtso::sprintf0(rtso::tr0("%(label)s %(bonus)s"),
									{{"label", rtso::tr0("Population Bonus:")}, {"bonus", String::num_int64(pop_bonus)}});
						Ref<Texture2D> b_ico = structure_icon(bi.portrait_id);
						if (b_ico.is_valid()) {
							bb->set_button_icon(b_ico);
							bb->set_expand_icon(true);
							bb->set_custom_minimum_size(Vector2(38, 38));
						} else {
							bb->set_text(label);
						}
						bb->add_theme_font_size_override("font_size", 9);
						if (need > sim->player_phase(local_player())) {
							bb->set_disabled(true);
							tip += "\n" + requires_text(String(kPhaseNames[need]) + " Phase");
						}
						bb->set_tooltip_text(tip);
						bb->connect("pressed", Callable(owner_, "_on_build_btn_pressed").bind(bt));
						production_bar_->add_child(bb);
					}
				}
			}
		}
		// Compact production buttons, so a wrapped build menu fits a few rows.
		for (int i = old_production; i < production_bar_->get_child_count(); ++i)
			if (Button *btn = Object::cast_to<Button>(production_bar_->get_child(i)))
				if (!btn->has_theme_font_size_override("font_size")) btn->add_theme_font_size_override("font_size", 11);
		keep_buttons(actions_bar_, old_actions);
		keep_buttons(production_bar_, old_production);
		// A structure shows its queue under the buttons while something is queued
		// (0.28's Queue panel shows only queued items); otherwise the buttons get
		// that room.
		bool queue_shown = false;
		if (selected_building_id != 0)
			for (const auto &b : sim->buildings())
				if (b.id == selected_building_id) { queue_shown = !b.queue.empty(); break; }
		if (queue_bar_->is_visible() != queue_shown) queue_bar_->set_visible(queue_shown);
		// A builder's construction grid can outgrow the two rows a unit selection
		// has: the panel grows upward to show all of it (0.28 shows the whole
		// Construction panel), up to kMaxGrow, past which it scrolls.
		constexpr float kMaxGrow = 120.0f;
		const float h = queue_shown ? 40.0f : std::clamp(production_bar_->get_combined_minimum_size().y, 96.0f, 96.0f + kMaxGrow);
		const Vector2 area(552, h);
		if (production_scroll_->get_size() != area) production_scroll_->set_size(area);
		const float top = -198.0f - (queue_shown ? 0.0f : h - 96.0f);
		if (panel_->get_offset(SIDE_TOP) != top) panel_->set_offset(SIDE_TOP, top);
	}

private:
	// The selected building's trainable roster for the local player's civ: every
	// civ_unit_template_table() row whose trainers include this building type, with
	// 0 A.D. names and costs after team bonuses; rows above the player's phase are
	// listed but disabled with the phase they need.
	void update_roster_list(const rtso::Simulation::Building *b) {
		using namespace godot;
		rtso::Simulation *sim = host_->ui_sim();
		if (!roster_list_) return;
		const uint8_t pid = local_player();
		if (!b || b->team != pid || b->build_progress < 1.0f) {
			roster_list_->set_visible(false);
			roster_list_key_ = ~0ull;
			return;
		}
		const uint8_t civ = sim->player_civ(pid);
		const uint8_t phase = sim->player_phase(pid);
		// Rebuild when the building, civ, phase, champion unlocks or allies (team bonus prices) change.
		const auto &ps = sim->player(pid);
		const uint64_t unlocks = (ps.has_tech(rtso::TECH_UNLOCK_CHAMPION_INFANTRY) ? 1u : 0u) |
			(ps.has_tech(rtso::TECH_UNLOCK_CHAMPION_CAVALRY) ? 2u : 0u) |
			(ps.has_tech(rtso::TECH_UNLOCK_CHAMPION_CHARIOTS) ? 4u : 0u);
		const uint64_t key = ((uint64_t)b->id << 32) | ((uint64_t)sim->ally_civ_mask(pid) << 12) |
			(unlocks << 8) | ((uint64_t)(civ & 0x3F) << 2) | (uint64_t)(phase & 0x3);
		// A changed option (gui.session.howtoshownames) rebuilds it too.
		if (key == roster_list_key_ && roster_list_gen_ == rtso::user_config::generation()) return;
		roster_list_key_ = key;
		roster_list_gen_ = rtso::user_config::generation();
		roster_list_->clear();
		// A corral trains its civ's animals (structures/<civ>/corral.xml Trainer/Entities);
		// metadata ROSTER_ANIMAL | FaunaType tells the click handler to breed one.
		if (b->type == rtso::BLD_CORRAL) {
			const rtso::CorralTrainable &ct = rtso::CORRAL_TRAINABLE[civ < rtso::CIV_COUNT ? civ : 0];
			for (uint8_t k = 0; k < ct.count; ++k) {
				const uint8_t f = ct.fauna[k];
				const rtso::FaunaTrainCost &c = rtso::fauna_train_cost(f);
				String label = String::utf8(rtso::fauna_type_info(f).name) + " (" + String::num_int64(c.res[0]) + "f)";
				int idx = roster_list_->add_item(label);
				roster_list_->set_item_metadata(idx, ROSTER_ANIMAL | (int)f);
				roster_list_->set_item_tooltip(idx, String::utf8(rtso::fauna_type_info(f).name) + "\nTrain " +
						String::num(sim->livestock_train_time(pid, f), 0) + " s, " +
						String::num_int64(rtso::fauna_type_info(f).food_amount) + " food when slaughtered, " +
						String::num(rtso::fauna_corral_food(f), 0) + " food every " +
						String::num(rtso::CORRAL_TRICKLE_INTERVAL, 0) + " s while penned" +
						(rtso::Simulation::livestock_required_phase(f) > phase
										? String(rtso::Simulation::livestock_required_phase(f) >= 3 ? "\nRequires City Phase" : "\nRequires Town Phase")
										: String()));
				if (rtso::Simulation::livestock_required_phase(f) > phase) roster_list_->set_item_disabled(idx, true);
			}
			roster_list_->set_visible(roster_list_->get_item_count() > 0);
			return;
		}
		// A shrine trains its own civ's Elite Healer (units/{native}/support_healer_e).
		for (uint16_t i : rtso::civ_roster_ids(b->type == rtso::BLD_SHRINE ? b->civ : civ)) {
			const rtso::CivUnitTemplate &u = rtso::civ_unit_template(i);
			if (!(u.trainers & rtso::bld_bit(b->type))) continue;
			// RTS0-only rows (<civ>/rts0_villager) back class training for the AI and old saves;
			// 0 A.D. has no such unit, so players don't see it.
			if (std::strstr(u.id, "/rts0_")) continue;
			String tip;
			const int idx = add_roster_item(pid, i, tip);
			if (u.phase > phase) {
				roster_list_->set_item_disabled(idx, true);
				tip += "\n" + requires_text(u.phase >= 3 ? "City Phase" : "Town Phase");
			} else if (!sim->can_train_at(pid, u.unit_type, b->type, i)) {
				roster_list_->set_item_disabled(idx, true);
				uint8_t gate = sim->train_unlock_tech(pid, u.unit_type, b->type, i);
				if (gate != rtso::TECH_NONE) tip += "\n" + requires_text(String::utf8(rtso::tech_info(gate).name));
			}
			roster_list_->set_item_tooltip(idx, tip);
		}
		roster_list_->set_visible(roster_list_->get_item_count() > 0);
	}

	// A selected unit that trains (0 A.D. Trainer on triremes and Cyrus II): its Trainer list,
	// the ones whose requirements aren't met yet disabled, and its queue. False when the
	// selection is not one of the local player's training units.
	bool update_unit_roster(uint32_t unit_id) {
		using namespace godot;
		rtso::Simulation *sim = host_->ui_sim();
		if (!roster_list_ || unit_id == 0) return false;
		const uint8_t pid = local_player();
		const uint16_t tmpl = sim->unit_template(unit_id);
		if (!rtso::Simulation::unit_is_trainer(tmpl)) return false;
		auto uview = sim->units();
		size_t i = 0;
		while (i < uview.count && uview.id[i] != unit_id) ++i;
		if (i == uview.count || uview.team[i] != pid) return false;
		trainer_unit_id_ = unit_id;
		production_queue_.update_unit(unit_id, sim->unit_queue(unit_id));
		const std::vector<uint16_t> can = sim->unit_trainables(pid, tmpl);
		uint64_t can_mask = 0;
		const std::vector<uint16_t> all = sim->unit_trainables(pid, tmpl, true);
		for (size_t k = 0; k < all.size() && k < 64; ++k)
			if (std::find(can.begin(), can.end(), all[k]) != can.end()) can_mask |= 1ull << k;
		// Researcher on a unit: each technology's research_check result (researched ones drop out).
		const std::vector<uint8_t> techs = rtso::Simulation::unit_researchables(tmpl);
		uint64_t tech_key = 0;
		for (uint8_t t : techs) tech_key = tech_key * 31 + (uint64_t)sim->research_check(pid, t) + 1;
		const uint64_t key = ((uint64_t)unit_id << 32) ^ (can_mask << 8) ^ (tech_key << 20) ^ 0x80ull ^ sim->ally_civ_mask(pid);
		if (key == roster_list_key_) return true;
		roster_list_key_ = key;
		roster_list_->clear();
		for (size_t k = 0; k < all.size(); ++k) {
			String tip;
			const int idx = add_roster_item(pid, all[k], tip);
			if (!(can_mask & (1ull << k))) {
				roster_list_->set_item_disabled(idx, true);
				tip += "\n" + owner_->tr("Requirements not met");
			}
			roster_list_->set_item_tooltip(idx, tip);
		}
		for (uint8_t t : techs) {
			const auto check = sim->research_check(pid, t);
			if (check == rtso::Simulation::TECH_ERR_RESEARCHED) continue;
			const rtso::TechInfo &ti = rtso::tech_info(t);
			String costs;
			auto add_cost = [&](int v, const char *suffix) {
				if (v <= 0) return;
				if (!costs.is_empty()) costs += " ";
				costs += String::num_int64(v) + suffix;
			};
			float paid[rtso::RES_COUNT], rtime = 0.0f;
			sim->tech_price(pid, t, rtso::BLD_TYPE_COUNT, paid, rtime); // as cmd_research_from_unit charges it
			add_cost((int)paid[rtso::RES_FOOD], "f");
			add_cost((int)paid[rtso::RES_WOOD], "w");
			add_cost((int)paid[rtso::RES_STONE], "s");
			add_cost((int)paid[rtso::RES_GOLD], "m");
			const int idx = roster_list_->add_item(String::utf8(ti.name) + " (" + costs + ")");
			roster_list_->set_item_metadata(idx, ROSTER_TECH | (int)t);
			const rtso::CostTip tech_cost{(int64_t)paid[rtso::RES_FOOD], (int64_t)paid[rtso::RES_WOOD], (int64_t)paid[rtso::RES_STONE],
					(int64_t)paid[rtso::RES_GOLD], 0, (int64_t)std::ceil(rtime)};
			String tip = String::utf8(ti.name) + "\n" + String::utf8(ti.desc) + "\n" + rtso::cost_tooltip(tech_cost);
			if (check != rtso::Simulation::TECH_OK && check != rtso::Simulation::TECH_ERR_COST) {
				roster_list_->set_item_disabled(idx, true);
				tip += "\n" + owner_->tr("Requirements not met");
			}
			roster_list_->set_item_tooltip(idx, tip);
		}
		roster_list_->set_visible(roster_list_->get_item_count() > 0);
		return true;
	}

	// One roster row: the 0 A.D. name and cost after team bonuses; `tip` gets its tooltip.
	int add_roster_item(uint8_t pid, uint16_t i, godot::String &tip) {
		using namespace godot;
		rtso::Simulation *sim = host_->ui_sim();
		const rtso::CivUnitTemplate &u = rtso::civ_unit_template(i);
		float cost[rtso::RES_COUNT];
		sim->unit_train_cost(pid, i, rtso::civ_template_unit_info(i, u.unit_type), cost);
		String costs;
		auto add_cost = [&](float v, const char *suffix) {
			if (v <= 0.0f) return;
			if (!costs.is_empty()) costs += " ";
			costs += String::num_int64((int64_t)v) + suffix;
		};
		add_cost(cost[rtso::RES_FOOD], "f");
		add_cost(cost[rtso::RES_WOOD], "w");
		add_cost(cost[rtso::RES_STONE], "s");
		add_cost(cost[rtso::RES_GOLD], "m");
		String label = String::utf8(u.name);
		if (u.flags & rtso::ROSTER_HERO) label = String::utf8("\u2605 ") + label;
		label += costs.is_empty() ? String(" (free)") : " (" + costs + ")";
		int idx = roster_list_->add_item(label);
		roster_list_->set_item_metadata(idx, (int)i);
		// 0.28 Training panel: names, cost (population and time too), health and
		// attack, then the batch line; callers add requirements.
		tip = rtso::entity_names(String::utf8(u.name), String::utf8(u.specific_name ? u.specific_name : ""));
		const rtso::CostTip unit_cost{(int64_t)cost[rtso::RES_FOOD], (int64_t)cost[rtso::RES_WOOD], (int64_t)cost[rtso::RES_STONE],
				(int64_t)cost[rtso::RES_GOLD], (int64_t)u.pop_cost, (int64_t)std::ceil(u.train_time)};
		const String cost_tip = rtso::cost_tooltip(unit_cost);
		if (!cost_tip.is_empty()) tip += "\n" + cost_tip;
		tip += "\n" + rtso::label_details("Health:", String::num_int64((int64_t)u.hp));
		if (u.attack_damage > 0.0f) tip += "\n" + rtso::label_details("Attack:", String::num(u.attack_damage, 1));
		const String batch = rtso::batch_training_string(1, host_->ui_batch_size(), 0);
		if (!batch.is_empty()) tip += "\n" + batch;
		return idx;
	}

	// 0 A.D.'s "Requires %(technology)s" line with the requirement's translated name.
	godot::String requires_text(const godot::String &what) const {
		using namespace godot;
		return rtso::sprintf0(owner_->tr("Requires %(technology)s"), {{"technology", owner_->tr(what)}});
	}

	// getNeededResourcesTooltip for the local player's stock, called once the sim
	// refused the cost; a cost multiplier the shown cost leaves out (a researcher's
	// TechCostMultiplier) still gets the label.
	godot::String needed_text(const rtso::CostTip &cost) const {
		using namespace godot;
		rtso::Simulation *sim = host_->ui_sim();
		const auto &ps = sim->player(local_player());
		const String need = rtso::needed_resources_tooltip(cost, ps.res[rtso::RES_FOOD], ps.res[rtso::RES_WOOD],
				ps.res[rtso::RES_STONE], ps.res[rtso::RES_GOLD]);
		return need.is_empty() ? rtso::tr0("Insufficient resources:").trim_suffix(":") : need;
	}

	// One g_EntityCommands button (unit_actions.js): the hotkey and title on the
	// tooltip's first line, the body below, the icon, and a count where it has one.
	void add_entity_command(const char *name, const char *label, const char *hotkey, const godot::String &tip,
			const char *icon, bool enabled = true, int count = 0) {
		using namespace godot;
		Button *btn = memnew(Button);
		const String text = count > 0 ? String(label) + " (" + String::num_int64(count) + ")" : String(label);
		btn->set_meta("label", text);
		const std::string key = (hotkey && *hotkey) ? rtso::hotkeys::table().label(hotkey) : std::string();
		btn->set_tooltip_text(key.empty() ? tip : "[" + String(key.c_str()) + "] " + tip);
		Ref<Texture2D> ico = rtso::load_ui_texture(String("res://assets/textures/ui/archive/icons/") + icon);
		if (ico.is_valid()) {
			// 0.28's command row is icons, with the count (Unload All) on the icon.
			btn->set_button_icon(ico);
			btn->set_expand_icon(true);
			btn->set_custom_minimum_size(Vector2(32, 32));
			btn->set_text(count > 0 ? String::num_int64(count) : String());
			btn->add_theme_font_size_override("font_size", 10);
		} else {
			btn->set_text(text);
		}
		btn->set_disabled(!enabled);
		btn->connect("pressed", Callable(owner_, "_on_entity_command").bind(String(name)));
		production_bar_->add_child(btn);
	}

	// Everything that makes a command button look or act differently: its text,
	// tooltip, enabled state, icon, size, overrides and what pressed calls.
	static godot::String button_signature(godot::Node *n) {
		using namespace godot;
		Button *b = Object::cast_to<Button>(n);
		if (!b) return n ? n->get_class() : String();
		String s = b->get_text() + String("\x1f") + b->get_tooltip_text() + (b->is_disabled() ? "\x1fD" : "\x1fE");
		Ref<Texture2D> ico = b->get_button_icon();
		if (ico.is_valid()) s += String("\x1fi") + (ico->get_path().is_empty() ? String::num_int64(ico->get_instance_id()) : ico->get_path());
		s += String("\x1fm") + String(Variant(b->get_custom_minimum_size()));
		if (b->has_theme_color_override("font_color")) s += String("\x1fc") + String(Variant(b->get_theme_color("font_color")));
		if (b->has_theme_font_size_override("font_size")) s += String("\x1ff") + String::num_int64(b->get_theme_font_size("font_size"));
		TypedArray<Dictionary> conns = b->get_signal_connection_list("pressed");
		for (int i = 0; i < conns.size(); ++i) {
			Callable c = Dictionary(conns[i])["callable"];
			s += String("\x1fp") + String(c.get_method()) + String(Variant(c.get_bound_arguments()));
		}
		return s;
	}

	// bar holds the previous frame's buttons (the first `old_count`) followed by
	// this frame's. Keep the old ones when the two sets match, so a press and its
	// release reach the same Button; otherwise swap in the new ones.
	static void keep_buttons(godot::Control *bar, int old_count) {
		using namespace godot;
		const int total = bar->get_child_count();
		const int fresh = total - old_count;
		bool same = fresh == old_count;
		for (int i = 0; same && i < old_count; ++i)
			same = button_signature(bar->get_child(i)) == button_signature(bar->get_child(old_count + i));
		const int from = same ? old_count : 0, to = same ? total : old_count;
		for (int i = to - 1; i >= from; --i) {
			Node *n = bar->get_child(i);
			bar->remove_child(n);
			memdelete(n);
		}
	}

	// A structure's icon: its portrait, else 0 A.D.'s session portrait from
	// art/textures/ui/session/portraits/structures (the template's Identity/Icon,
	// under its 0.28 file name where that differs from RTS0's portrait id).
	static godot::Ref<godot::Texture2D> structure_icon(const char *portrait_id) {
		using namespace godot;
		Ref<Texture2D> t = rtso::load_ui_texture(String("res://assets/textures/ui/portraits/") + portrait_id + ".png");
		if (t.is_valid()) return t;
		static const char *const kIcons[][2] = {
			{"stable", "stable_01"}, {"palisade", "palisade_wall"}, {"forge", "blacksmith"}, {"wall_tower", "tower"},
			{"arsenal", "siege_workshop"}, {"elephant_stable", "stable_elephant"}, {"embassy", "embassy_celtic"},
			{"amphitheater", "theater"}, {"library", "library_scroll"}, {"military_colony", "military_settlement"},
			{"army_camp", "roman_camp"}, {"ice_house", "yakhchal"}, {"ministry", "imperial_ministry"},
			{"imperial_court", "palace"}, {"tachara", "palace"}, {"pillar", "ashoka_pillar"}, {"prytaneion", "tholos"},
			{"gerousia", "tholos"}, {"syssiton", "syssition"}, {"pyramid_large", "kush_pyramid_big"},
			{"pyramid_small", "kush_pyramid_small"}, {"super_dock", "uber_dock"}, {"temple_amun", "temple_epic"},
			{"temple_mars", "temple_epic"}, {"tower_great", "tower_artillery"}, {"council_chamber", "hall"},
			{"assembly", "hall"}, {"academy", "library_scroll"},
		};
		String file = portrait_id;
		for (const auto &m : kIcons)
			if (file == m[0]) { file = m[1]; break; }
		return rtso::load_ui_texture("res://assets/textures/ui/archive/portraits/structures/" + file + ".png");
	}

	// 0.28's Identity text for a civ's structure (generic_name, specific_name, tooltip) from
	// data/reference/templates.json (tools/extract_0ad_reference.py), read once.
	static godot::Dictionary structure_template_text(uint8_t civ, int type) {
		using namespace godot;
		static Dictionary structures;
		static bool loaded = false;
		if (!loaded) {
			loaded = true;
			Ref<FileAccess> f = FileAccess::open("res://data/reference/templates.json", FileAccess::READ);
			if (f.is_valid()) structures = Dictionary(JSON::parse_string(f->get_as_text())).get("structures", Dictionary());
		}
		if (civ >= rtso::CIV_COUNT) return Dictionary();
		const Dictionary by_civ = structures.get(String(rtso::civ_info(civ).code), Dictionary());
		return by_civ.get(String::num_int64(type), Dictionary());
	}

	godot::Object *owner_ = nullptr;
	RtsoCommandPanelHost *host_ = nullptr;
	godot::Panel *panel_ = nullptr;
	godot::HBoxContainer *stances_bar_ = nullptr;
	godot::Button *stance_buttons_[5] = {}; // indexed by UnitStance
	godot::HBoxContainer *actions_bar_ = nullptr;
	// Wraps its buttons; the scroll keeps a long build menu inside the panel.
	godot::ScrollContainer *production_scroll_ = nullptr;
	godot::HFlowContainer *production_bar_ = nullptr;
	godot::HBoxContainer *queue_bar_ = nullptr;
	godot::Label *title_ = nullptr;
	// Every civ roster unit and hero the selected building trains (0 A.D. names,
	// costs after team bonuses), above the command panel. Rebuilt only when
	// roster_list_key_ changes so hover and tooltips survive between frames.
	godot::ItemList *roster_list_ = nullptr;
	uint64_t roster_list_key_ = ~0ull;
	uint32_t roster_list_gen_ = 0; // user_config::generation() it was built at
	uint32_t trainer_unit_id_ = 0;
	RtsoProductionQueue production_queue_;

	uint8_t local_player() const { return host_->local_player(); }
};

#endif // RTSO_COMMAND_QUEUE_VIEW_H
