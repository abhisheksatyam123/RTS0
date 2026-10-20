#ifndef RTSO_COMMAND_QUEUE_VIEW_H
#define RTSO_COMMAND_QUEUE_VIEW_H

// P4 stage-2 leaf widget: the command panel (0 A.D. gui/session/selection_panels.js
// Stance, Formation, Research, Construction, Upgrade and Training panels, unit_actions.js
// g_EntityCommands) with the roster list above it and the production queue under it.
// Rebuilds its buttons from the selection each frame; the presses go to the owner.

#include <godot_cpp/classes/base_button.hpp>
#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/color_rect.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/texture_rect.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <godot_cpp/classes/h_flow_container.hpp>
#include <godot_cpp/classes/item_list.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/panel.hpp>
#include <godot_cpp/classes/scroll_container.hpp>
#include <godot_cpp/classes/style_box_empty.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/theme.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/script.hpp>
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
#include <map>
#include <unordered_map>
#include <vector>

#include "../simulation/entity_limits_data.h"
#include "../simulation/simulation.h"
#include "hotkeys.h"
#include "l10n.h"
#include "template_portraits.h"
#include "production_queue.h"
#include "selection_details.h"
#include "user_config.h"

class RtsoCommandPanelHost {
public:
	virtual ~RtsoCommandPanelHost() = default;
	virtual rtso::Simulation *ui_sim() = 0;
	virtual uint8_t local_player() const = 0;
	virtual uint32_t ui_selected_building_id() const = 0;
	virtual const godot::PackedInt32Array &ui_selected_ids() const = 0;
	virtual int ui_batch_size() const = 0; // gui.session.batchtrainingsize
	// The units a held Shift batch of this template holds so far (input.js g_NumberOfBatches
	// x the batch size), 0 when none is being built.
	virtual int ui_pending_batch(uint16_t tmpl) const = 0;
	// getBuildingsWhichCanTrainEntity: the selected finished structures that train it
	// (`holder` among them).
	virtual int ui_trainer_count(uint16_t tmpl, uint32_t holder) const { return 1; }
	virtual std::vector<uint32_t> ui_selected_structures() const = 0;
	// The structure's Trainer has entities (autoqueue-on / autoqueue-off).
	virtual bool ui_trains_units(uint32_t id) const { return false; }
	// The Garrison panel shows (a selected holder has units inside): it wins over Formation.
	virtual bool ui_garrison_panel_shown() const { return false; }
	// g_IsObserver: an observer, a defeated player or a replay viewer.
	virtual bool ui_observing() const { return false; }
};

class RtsoCommandPanelView {
public:
	// 0 A.D. AutoFormation.js: gui.session.defaultformation names the formation walk and
	// patrol orders put units into; the null formation (or anything unknown) turns it off.
	static uint8_t default_formation() {
		const std::string name = rtso::user_config::get("gui.session.defaultformation");
		for (uint8_t f = 1; f < rtso::FORMATION_COUNT; ++f)
			if (name == rtso::formation_template(f).path) return f;
		return rtso::FORMATION_NONE;
	}
	static constexpr int ROSTER_ANIMAL = 0x10000; // roster_list metadata flag: a corral animal
	// Where refresh_roster_state puts a training cell's Cost and batch lines in its base tooltip.
	static constexpr const char *COST_SLOT = "\x01" "cost";
	static constexpr const char *BATCH_SLOT = "\x01" "batch";
	static constexpr int ROSTER_TECH = 0x20000;   // roster_list metadata flag: a training unit's technology

	// owner: signal target (RtsoMain: the stance, formation, tech, build and command presses,
	// _on_roster_item_clicked). parent: the HUD layer the panel and roster go on.
	void build(godot::Object *owner, godot::Node *parent, const godot::Ref<godot::Theme> &theme,
			RtsoCommandPanelHost *host, RtsoProductionQueueHost *queue_host) {
		using namespace godot;
		owner_ = owner;
		host_ = host;
		panel_ = memnew(Panel);
		panel_->set_anchor(SIDE_LEFT, 0.5f);
		panel_->set_anchor(SIDE_RIGHT, 0.5f);
		panel_->set_anchor(SIDE_TOP, 1.0f);
		panel_->set_anchor(SIDE_BOTTOM, 1.0f);
		// session.xml, in the 1024 px group centred at the bottom: the 402 x 166 commands
		// panel (unitCommands, 50%+110 .. 50%+512) and the 206 x 166 supplemental panel
		// (supplementalSelectionDetails, 50%-316 .. 50%-110) either side of the details.
		panel_->set_offset(SIDE_LEFT, 110.0f);
		panel_->set_offset(SIDE_RIGHT, 110.0f + kRightW);
		panel_->set_offset(SIDE_TOP, -10.0f - kPanelH);
		panel_->set_offset(SIDE_BOTTOM, -10.0f);

		Ref<StyleBoxFlat> cmd_style;
		cmd_style.instantiate();
		cmd_style->set_bg_color(Color(0.10f, 0.11f, 0.15f, 0.94f));
		cmd_style->set_corner_radius_all(3);
		cmd_style->set_border_width_all(2);
		cmd_style->set_border_color(Color(0.90f, 0.75f, 0.31f, 1.0f));
		panel_->add_theme_stylebox_override("panel", cmd_style);
		if (theme.is_valid()) panel_->set_theme(theme);
		// session/selection_panels*.xml: the panel's buttons use sessionToolTipBottom (tooltips.gd);
		// the stance, formation, alert, command and barter ones override it with the bold variant.
		panel_->set_meta("tooltip_style", "session_bottom");
		left_panel_ = memnew(Panel);
		left_panel_->set_anchor(SIDE_LEFT, 0.5f);
		left_panel_->set_anchor(SIDE_RIGHT, 0.5f);
		left_panel_->set_anchor(SIDE_TOP, 1.0f);
		left_panel_->set_anchor(SIDE_BOTTOM, 1.0f);
		left_panel_->set_offset(SIDE_LEFT, -110.0f - kLeftW);
		left_panel_->set_offset(SIDE_RIGHT, -110.0f);
		left_panel_->set_offset(SIDE_TOP, -10.0f - kPanelH);
		left_panel_->set_offset(SIDE_BOTTOM, -10.0f);
		left_panel_->add_theme_stylebox_override("panel", cmd_style);
		if (theme.is_valid()) left_panel_->set_theme(theme);
		left_panel_->set_meta("tooltip_style", "session_bottom_bold");
		parent->add_child(left_panel_);
		parent->add_child(panel_);

		roster_list_ = memnew(ItemList);
		// 0.28's Training panel (training_panel.xml at 6 6): a grid of portraits
		// (rowLength 10) at the top of the commands panel, with the count each click
		// would train under it.
		roster_list_->set_position(Vector2(6, 6));
		roster_list_->set_size(Vector2(kGridW, kTrainH));
		roster_list_->set_max_columns(10);
		roster_list_->set_same_column_width(true);
		roster_list_->set_fixed_column_width(36);
		roster_list_->set_icon_mode(ItemList::ICON_MODE_TOP);
		roster_list_->set_fixed_icon_size(Vector2i(32, 32));
		roster_list_->add_theme_font_size_override("font_size", 11);
		roster_list_->add_theme_constant_override("h_separation", 3);
		roster_list_->add_theme_constant_override("v_separation", 1);
		{
			Ref<StyleBoxEmpty> none;
			none.instantiate();
			roster_list_->add_theme_stylebox_override("panel", none);
			roster_list_->add_theme_stylebox_override("focus", none);
		}
		if (theme.is_valid()) roster_list_->set_theme(theme);
		roster_list_->set_visible(false);
		roster_list_->set_allow_rmb_select(true); // a right click opens the Template Viewer
		roster_list_->connect("item_clicked", Callable(owner_, "_on_roster_item_clicked"));
		roster_list_->connect("gui_input", Callable(owner_, "_on_roster_gui_input"));
		roster_list_->set_meta("tooltip_style", "session_bottom");
		panel_->add_child(roster_list_);

		// stance_panel.xml: five 38 px icons along the supplemental panel's bottom.
		stances_bar_ = memnew(HBoxContainer);
		stances_bar_->set_position(Vector2(6, kPanelH - 41.0f));
		stances_bar_->set_size(Vector2(kLeftW - 12.0f, 38));
		stances_bar_->add_theme_constant_override("separation", 1);
		stances_bar_->set_meta("tooltip_style", "session_bottom_bold");
		left_panel_->add_child(stances_bar_);

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
			// stance_panel.xml: a 38 px icon button, no caption; the name is in the tooltip.
			sb->set_name(stance_names[s]);
			sb->set_custom_minimum_size(Vector2(38, 38));
			sb->set_tooltip_text(String(stance_names[s]) + "\n" + stance_tips[s]);
			sb->set_toggle_mode(true); // pressed = some selected unit holds it (IsStanceSelected)
			Ref<Texture2D> s_ico = rtso::load_ui_texture(stance_icons[s]);
			if (s_ico.is_valid()) {
				sb->set_button_icon(s_ico);
				sb->set_expand_icon(true);
			}
			sb->connect("pressed", Callable(owner_, "_on_stance_selected").bind(s));
			stances_bar_->add_child(sb);
			stance_buttons_[s] = sb;
		}
		stances_bar_->set_visible(false);

		// formation_panel.xml: the formations from the supplemental panel's top left.
		actions_bar_ = memnew(HFlowContainer);
		actions_bar_->set_position(Vector2(6, 6));
		actions_bar_->set_size(Vector2(kLeftW - 12.0f, kPanelH - 50.0f));
		actions_bar_->add_theme_constant_override("h_separation", 2);
		actions_bar_->add_theme_constant_override("v_separation", 2);
		actions_bar_->set_meta("tooltip_style", "session_bottom_bold");
		left_panel_->add_child(actions_bar_);

		// Production Bar (Training & Tech)
		// The build menu has 25+ structures: the buttons wrap onto rows and
		// scroll inside the panel instead of running off the screen at 1280x720.
		production_scroll_ = memnew(ScrollContainer);
		production_scroll_->set_position(Vector2(6, 6));
		production_scroll_->set_size(Vector2(kGridW, kPanelH - 12.0f));
		production_scroll_->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
		panel_->add_child(production_scroll_);
		production_bar_ = memnew(HFlowContainer);
		production_bar_->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		production_bar_->add_theme_constant_override("h_separation", 1);
		production_bar_->add_theme_constant_override("v_separation", 1);
		production_scroll_->add_child(production_bar_);

		// queue_panel.xml: the production queue is a 56 px strip on top of the
		// commands panel (4 -56 100% 0).
		queue_bar_ = memnew(HBoxContainer);
		queue_bar_->set_position(Vector2(4, -56));
		queue_bar_->set_size(Vector2(kRightW - 4.0f, 54));
		queue_bar_->add_theme_constant_override("separation", 6);
		panel_->add_child(queue_bar_);

		production_queue_.build(owner, queue_bar_, queue_host);

		// unit_commands.xml (0 100%-40 100% 100%-4 in the 228 px details panel): the
		// g_EntityCommands icons, centred along the details panel's bottom.
		commands_row_ = memnew(HBoxContainer);
		commands_row_->set_name("unitCommandPanel");
		commands_row_->set_anchor(SIDE_LEFT, 0.5f);
		commands_row_->set_anchor(SIDE_RIGHT, 0.5f);
		commands_row_->set_anchor(SIDE_TOP, 1.0f);
		commands_row_->set_anchor(SIDE_BOTTOM, 1.0f);
		commands_row_->set_offset(SIDE_LEFT, -110.0f);
		commands_row_->set_offset(SIDE_RIGHT, 110.0f);
		commands_row_->set_offset(SIDE_TOP, -10.0f - 40.0f);
		commands_row_->set_offset(SIDE_BOTTOM, -10.0f - 4.0f);
		commands_row_->set_alignment(BoxContainer::ALIGNMENT_CENTER);
		commands_row_->add_theme_constant_override("separation", 1);
		commands_row_->set_meta("tooltip_style", "session_bottom_bold");
		if (theme.is_valid()) commands_row_->set_theme(theme);
		parent->add_child(commands_row_);
	}

	godot::Panel *panel() const { return panel_; }
	godot::Panel *left_panel() const { return left_panel_; }
	godot::HBoxContainer *commands_row() const { return commands_row_; }
	godot::HFlowContainer *production_bar() const { return production_bar_; }
	void set_barter_sell(int r) { barter_sell_ = r; } // BarterButtonManager.setSelectedResource
	void set_barter_host(godot::Control *c) {
		barter_host_ = c;
		if (c) c->set_meta("tooltip_style", "session_bottom_bold");
	}
	godot::Control *barter_host() const { return barter_host_; }
	godot::ItemList *roster_list() const { return roster_list_; }
	godot::String roster_label(int idx) const { return idx >= 0 && idx < (int)roster_labels_.size() ? roster_labels_[idx] : godot::String(); }
	RtsoProductionQueue &production_queue() { return production_queue_; }
	// The selected training unit whose roster is shown, else 0.
	uint32_t trainer_unit_id() const { return trainer_unit_id_; }

	void update() {
		using namespace godot;
		if (!panel_) return;
		rtso::Simulation *sim = host_->ui_sim();
		const godot::PackedInt32Array &selected_ids = host_->ui_selected_ids();
		const uint32_t selected_building_id = host_->ui_selected_building_id();
		// unit_commands.js g_IsObserver: an observer gets every panel of the selection's owner
		// (playerStates[entStates[0].player]), its buttons disabled (controlsPlayer).
		panel_player_ = 0xFF;
		read_only_ = false;
		// unit_commands.js playerState.controlsAll (Control all units): every panel of the
		// selection's owner, and pressable.
		const bool control_all = sim && !host_->ui_observing() && sim->controls_all(host_->local_player());
		if (sim && (host_->ui_observing() || control_all)) {
			uint8_t owner = 0xFF;
			if (selected_building_id != 0) {
				for (const auto &b : sim->buildings())
					if (b.id == selected_building_id) { owner = b.team; break; }
			} else if (!selected_ids.is_empty()) {
				auto uv = sim->units();
				for (size_t i = 0; i < uv.count; ++i)
					if (uv.id[i] == (uint32_t)selected_ids[0]) { owner = uv.team[i]; break; }
			}
			if (owner < rtso::MAX_PLAYERS) {
				panel_player_ = owner;
				read_only_ = !control_all;
			}
		}
		update_barter_panel();

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
		const int old_commands = commands_row_->get_child_count();
		commands_base_ = old_commands;

		if (selected_building_id != 0) {
			const auto &bldgs = sim->buildings();
			const rtso::Simulation::Building *found_b = nullptr;
			for (const auto &b : bldgs) {
				if (b.id == selected_building_id) { found_b = &b; break; }
			}

			// Update 8-slot Visual Production Queue (ProductionQueue.js parity); with several
			// structures selected, their queues merged (Queue.getItems).
			const std::vector<uint32_t> structs = host_->ui_selected_structures();
			if (structs.size() > 1) {
				std::vector<const rtso::Simulation::Building *> producers;
				for (uint32_t id : structs)
					for (const auto &b : bldgs)
						if (b.id == id) producers.push_back(&b);
				production_queue_.update_multi(producers);
			} else {
				production_queue_.update(found_b);
			}
			update_roster_list(found_b);

			if (found_b && found_b->build_progress >= 1.0f) {
				uint8_t pciv = sim->player_civ(local_player());
				// Units train from the civ's own list only (update_roster_list, 0 A.D. ProductionQueue's
				// per-template Trainer/Entities), never from generic class buttons.

				// Technologies researched here (0 A.D. ProductionQueue "technologies"):
				// one button per tech whose researcher is this building type, greyed
				// out with the reason research_check() gives.
				// Research.getItems: one researcher lists its technologies; several selected merge
				// theirs, the ones not upgrading and with the shortest queue first, dropping a
				// technology already listed at the same price, a structure's whole list skipped when
				// it would pass 10 slots. Each button researches at its own structure.
				std::vector<const rtso::Simulation::Building *> researchers;
				for (uint32_t id : structs.size() > 1 ? structs : std::vector<uint32_t>{found_b->id})
					for (const auto &b : bldgs)
						if (b.id == id && b.team == local_player() && b.build_progress >= 1.0f) researchers.push_back(&b);
				if (researchers.size() > 1)
					std::stable_sort(researchers.begin(), researchers.end(), [](const auto *a, const auto *b) {
						const bool ua = a->upgrade_to != rtso::BLD_TYPE_COUNT, ub = b->upgrade_to != rtso::BLD_TYPE_COUNT;
						if (ua != ub) return !ua;
						return a->queue.size() < b->queue.size();
					});
				struct TechItem { uint8_t tech; const rtso::Simulation::Building *at; rtso::Simulation::ResearchCheck rc; };
				std::vector<TechItem> tech_items;
				auto same_price = [&](uint8_t t, const rtso::Simulation::Building *a, const rtso::Simulation::Building *b) {
					if (a->type == b->type) return true;
					float pa[rtso::RES_COUNT], pb[rtso::RES_COUNT], ta = 0.0f, tb = 0.0f;
					sim->tech_price(local_player(), t, a->type, pa, ta);
					sim->tech_price(local_player(), t, b->type, pb, tb);
					return ta == tb && std::equal(pa, pa + rtso::RES_COUNT, pb);
				};
				size_t slots = 0;
				for (const rtso::Simulation::Building *rb : researchers) {
					std::vector<TechItem> mine;
					size_t pairs = 0;
					for (int t = 1; t < rtso::TECH_COUNT; ++t) {
						if (!rtso::building_researches_tech(rb->type, (uint8_t)t, pciv) || !rtso::tech_available_to_civ((uint8_t)t, pciv)) continue;
						// Researcher.js GetTechnologiesList: a chain shows one slot, its next step; a technology
						// whose superseded one is offered here waits until that one is researched or in progress.
						if (const uint8_t sup = rtso::tech_supersedes((uint8_t)t); sup != rtso::TECH_NONE
								&& rtso::building_researches_tech(rb->type, sup, pciv) && rtso::tech_available_to_civ(sup, pciv)) {
							const rtso::Simulation::ResearchCheck sc = sim->research_check(local_player(), sup, 0);
							if (sc != rtso::Simulation::TECH_ERR_RESEARCHED && sc != rtso::Simulation::TECH_ERR_IN_PROGRESS) continue;
						}
						const rtso::Simulation::ResearchCheck rc = sim->research_check(local_player(), (uint8_t)t, rb->id);
						// Researcher.js GetTechnologiesList: a researched or in-progress tech leaves its slot
						// to its successor.
						if (rc == rtso::Simulation::TECH_ERR_RESEARCHED || rc == rtso::Simulation::TECH_ERR_IN_PROGRESS || rc == rtso::Simulation::TECH_ERR_CIV
								|| rc == rtso::Simulation::TECH_ERR_PAIRED || rc == rtso::Simulation::TECH_ERR_DISABLED) continue;
						if (std::any_of(tech_items.begin(), tech_items.end(),
									[&](const TechItem &it) { return it.tech == t && same_price((uint8_t)t, it.at, rb); })) continue;
						mine.push_back({(uint8_t)t, rb, rc});
					}
					for (const TechItem &a : mine)
						if (const uint8_t partner = rtso::tech_pair_partner(a.tech); partner != rtso::TECH_NONE && a.tech < partner &&
								std::any_of(mine.begin(), mine.end(), [&](const TechItem &b) { return b.tech == partner; })) ++pairs;
					const size_t add = mine.size() - pairs; // a pair is one slot
					if (researchers.size() > 1 && slots + add > 10) continue;
					slots += add;
					tech_items.insert(tech_items.end(), mine.begin(), mine.end());
				}
				std::vector<std::pair<uint8_t, Button *>> tech_buttons;
				for (const TechItem &item : tech_items) {
					const int t = item.tech;
					const rtso::Simulation::Building *rb = item.at;
					const rtso::Simulation::ResearchCheck rc = item.rc;
					const rtso::TechInfo &ti = rtso::tech_info((uint8_t)t);
					// 0.28 Research panel (g_SelectionPanels.Research): an icon button per technology.
					Button *btn = memnew(Button);
					btn->set_meta("label", String::utf8(ti.name));
					btn->set_custom_minimum_size(Vector2(38, 38));
					btn->set_focus_mode(Control::FOCUS_NONE);
					TextureRect *icon = memnew(TextureRect);
					icon->set_name("unitResearchIcon");
					icon->set_position(Vector2(3, 3));
					icon->set_size(Vector2(32, 32));
					icon->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
					icon->set_stretch_mode(TextureRect::STRETCH_SCALE);
					icon->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
					icon->set_texture(rtso::RtsoResearchProgress::icon((uint8_t)t));
					btn->add_child(icon);
					// Researcher.js GetTechnologyData: the price after this researcher's TechCostMultiplier,
					// team bonuses and the Library of Alexandria, as cmd_research charges it.
					float paid[rtso::RES_COUNT], rtime = 0.0f;
					sim->tech_price(local_player(), (uint8_t)t, rb->type, paid, rtime);
					// Names (getEntityNamesFormatted: the civ's specificName first), tooltip, cost,
					// right-click, requirements, needed resources.
					const char *tech_specific = rtso::tech_specific_name((uint8_t)t, pciv);
					String tip = rtso::entity_names_formatted(String::utf8(ti.name), tech_specific ? String::utf8(tech_specific) : String());
					if (ti.desc && *ti.desc) tip += "\n" + String::utf8(ti.desc);
					const rtso::CostTip tech_cost{(int64_t)paid[rtso::RES_FOOD], (int64_t)paid[rtso::RES_WOOD], (int64_t)paid[rtso::RES_STONE],
							(int64_t)paid[rtso::RES_GOLD], 0, (int64_t)std::ceil(rtime)};
					const String tech_cost_tip = rtso::cost_tooltip(tech_cost, true);
					if (!tech_cost_tip.is_empty()) tip += "\n" + tech_cost_tip;
					tip += "\n" + right_click_tooltip();
					on_right_click_details(btn, "tech", t, pciv);
					String why;
					bool reqs_failed = false;
					switch (rc) {
						// selection_panels.js: the tech's own requirementsTooltip ("Unlocked in Town
						// Phase.", "Unlocked in City Phase. Requires “Shipwrights.”").
						case rtso::Simulation::TECH_ERR_PHASE:
							if (const char *req = rtso::tech_requirements_tooltip((uint8_t)t)) why = rtso::objection_font(owner_->tr(String::utf8(req)));
							else why = requires_text(ti.required_phase >= 3 ? "City Phase" : "Town Phase");
							reqs_failed = true;
							break;
						case rtso::Simulation::TECH_ERR_PREREQUISITE: {
							const uint8_t sup = rtso::tech_supersedes((uint8_t)t);
							const bool need_prev = sup != rtso::TECH_NONE && !sim->player(local_player()).has_tech(sup);
							if (const char *req = rtso::tech_requirements_tooltip((uint8_t)t)) why = rtso::objection_font(owner_->tr(String::utf8(req)));
							else why = requires_text(String::utf8(rtso::tech_info(need_prev ? sup : ti.prerequisite).name));
							reqs_failed = true;
							break;
						}
						case rtso::Simulation::TECH_ERR_STRUCTURES:
						{
							// selection_panels.js: the tech's requirementsTooltip, then what is left
							// ("Remaining: 3 entities of class Village").
							const bool city = t == rtso::TECH_PHASE_CITY;
							const int remaining = (city ? 3 : 5) - sim->phase_structures(local_player(), city ? 2 : 1);
							why = owner_->tr(city ? "Requires three Town Structures." : "Requires five Village Structures.");
							if (remaining > 0)
								why += " " + rtso::sprintf0(owner_->tr("Remaining: %(entityCounts)s"),
										{{"entityCounts", rtso::sprintf0(owner_->tr(remaining == 1 ? "%(number)s entity of class %(class)s"
																						: "%(number)s entities of class %(class)s"),
												 {{"number", String::num_int64(remaining)}, {"class", owner_->tr(city ? "Town" : "Village")}})}});
							why = rtso::objection_font(why);
						}
							reqs_failed = true;
							break;
						case rtso::Simulation::TECH_ERR_COST: why = needed_text(tech_cost); break;
						default: break;
					}
					if (!why.is_empty()) tip += "\n" + why;
					// getNeededResourcesTooltip follows whatever requirement failed.
					if (rc != rtso::Simulation::TECH_ERR_COST) {
						const auto &ps = sim->player(local_player());
						const String need = rtso::needed_resources_tooltip(tech_cost, ps.res[rtso::RES_FOOD], ps.res[rtso::RES_WOOD],
								ps.res[rtso::RES_STONE], ps.res[rtso::RES_GOLD], true);
						if (!need.is_empty()) tip += "\n" + need;
					}
					// Requirements not met: "color:0 0 0 127:grayscale:"; resources missing: the
					// resources' alpha mask (red here); both disable the button.
					if (reqs_failed) {
						icon->set_material(grayscale_material());
						icon->set_self_modulate(Color(0.5f, 0.5f, 0.5f));
					} else if (rc == rtso::Simulation::TECH_ERR_COST) {
						ColorRect *mask = memnew(ColorRect);
						mask->set_position(Vector2(3, 3));
						mask->set_size(Vector2(32, 32));
						mask->set_color(Color(0.8f, 0.0f, 0.0f, 0.45f));
						mask->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
						btn->add_child(mask);
					}
					btn->set_disabled(rc != rtso::Simulation::TECH_OK);
					// "Cannot research while upgrading."
					if (rb->upgrade_to != rtso::BLD_TYPE_COUNT) {
						btn->set_disabled(true);
						icon->set_material(grayscale_material());
						icon->set_self_modulate(Color(0.5f, 0.5f, 0.5f));
						tip += "\n" + rtso::objection_font(rtso::tr0("Cannot research while upgrading."));
					}
					btn->set_tooltip_text(tip);
					btn->connect("pressed", Callable(owner_, "_on_tech_pressed_at").bind(t, (int64_t)rb->id));
					tech_buttons.push_back({(uint8_t)t, btn});
				}
				// Pairs (pair_*.json) side by side, top then bottom, with 0.28's pair connector;
				// hovering one crosses out the other (tech_pair_would_be_unavailable).
				for (size_t i = 0; i < tech_buttons.size(); ++i) {
					Button *btn = tech_buttons[i].second;
					if (!btn) continue;
					const uint8_t partner = rtso::tech_pair_partner(tech_buttons[i].first);
					size_t j = i + 1;
					while (j < tech_buttons.size() && !(tech_buttons[j].second && tech_buttons[j].first == partner)) ++j;
					if (partner == rtso::TECH_NONE || j >= tech_buttons.size()) {
						production_bar_->add_child(btn);
						continue;
					}
					Button *other = tech_buttons[j].second;
					tech_buttons[j].second = nullptr;
					Button *top = tech_pair_top(tech_buttons[i].first) ? btn : other;
					Button *bottom = top == btn ? other : btn;
					HBoxContainer *pair = memnew(HBoxContainer);
					pair->set_name("unitResearchPair");
					pair->set_meta("node_name", "unitResearchPair");
					pair->add_theme_constant_override("separation", 1);
					pair->add_child(top);
					pair->add_child(bottom);
					TextureRect *link = memnew(TextureRect);
					link->set_name("unitResearchPairIcon");
					link->set_texture(rtso::load_ui_texture("res://assets/textures/ui/archive/icons/horizontal_pair.png"));
					link->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
					link->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
					link->set_custom_minimum_size(Vector2(16, 22));
					link->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
					link->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
					pair->add_child(link);
					pair->move_child(link, 1);
					for (Button *b : {top, bottom}) {
						TextureRect *cross = memnew(TextureRect);
						cross->set_name("unitResearchUnchosenIcon");
						cross->set_texture(rtso::load_ui_texture("res://assets/textures/ui/archive/icons/tech_pair_would_be_unavailable.png"));
						cross->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
						cross->set_stretch_mode(TextureRect::STRETCH_SCALE);
						cross->set_position(Vector2(3, 3));
						cross->set_size(Vector2(32, 32));
						cross->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
						cross->set_visible(false);
						b->add_child(cross);
					}
					for (Button *b : {top, bottom}) {
						Button *o = b == top ? bottom : top;
						Node *cross = o->get_node_or_null(NodePath("unitResearchUnchosenIcon"));
						if (!cross) continue;
						b->connect("mouse_entered", Callable(cross, "set_visible").bind(true));
						b->connect("mouse_exited", Callable(cross, "set_visible").bind(false));
					}
					production_bar_->add_child(pair);
				}

				// 0.28's Gate panel (g_SelectionPanels.Gate): Lock Gate (lock_locked) and Unlock Gate
				// (lock_unlocked) when any selected entity is a gate, each framed by the corners when
				// some selected gate is in that state.
				// 0.28's Alert panel (g_SelectionPanels.Alert, AlertRaiser.js): Raise (bell_level1)
				// sends nearby civilians or traders into the nearest structures with room, End
				// (bell_level0) sends them back out; shown when any selected entity raises alerts.
				bool any_gate = false, any_locked = false, any_unlocked = false;
				int alert_raisers = 0, civilian_raisers = 0, trader_raisers = 0;
				for (uint32_t id : structs.size() > 1 ? structs : std::vector<uint32_t>{found_b->id})
					for (const auto &b : bldgs) {
						if (b.id != id || b.team != local_player()) continue;
						if (rtso::is_gate_type(b.type)) {
							any_gate = true;
							(b.gate_locked ? any_locked : any_unlocked) = true;
						}
						rtso::AlertRaiserInfo ai;
						if (b.build_progress >= 1.0f && rtso::building_alert_raiser(b.type, ai)) {
							++alert_raisers;
							(ai.traders ? trader_raisers : civilian_raisers) += 1;
						}
						break;
					}
				if (any_gate) {
					for (int lock = 1; lock >= 0; --lock) {
						Button *btn = icon_button(lock ? "Lock Gate" : "Unlock Gate",
								rtso::load_ui_texture(String("res://assets/textures/ui/archive/icons/") + (lock ? "lock_locked.png" : "lock_unlocked.png")));
						btn->set_tooltip_text(rtso::tr0(lock ? "Lock Gate" : "Unlock Gate"));
						if (lock ? any_locked : any_unlocked) add_corners(btn);
						btn->connect("pressed", Callable(owner_, "_on_gate_lock_pressed").bind(lock));
						production_bar_->add_child(btn);
					}
				}
				if (alert_raisers > 0) {
					const bool civilians = civilian_raisers == alert_raisers, traders = trader_raisers == alert_raisers;
					for (int end = 0; end < 2; ++end) {
						Button *btn = icon_button(end ? "End alert" : "Raise alert",
								rtso::load_ui_texture(String("res://assets/textures/ui/archive/icons/") + (end ? "bell_level0.png" : "bell_level1.png")));
						String tip = rtso::tr0(end ? "End the alert." : "Raise an alert!");
						tip += "\n" + rtso::tr0(end ? (civilians ? "Unload nearby Civilians." : traders ? "Unload nearby Traders." : "Unload nearby vulnerable units.")
								: (civilians ? "Alert nearby Civilians to seek refuge." : traders ? "Alert nearby Traders to seek refuge."
															 : "Alert nearby vulnerable units to seek refuge."));
						btn->set_tooltip_text(tip);
						btn->set_meta("tooltip_style", "session_bottom_bold");
						btn->connect("pressed", Callable(owner_, "_on_alert_pressed").bind(end));
						production_bar_->add_child(btn);
					}
				}

				// 0.28's Upgrade panel for a structure (g_SelectionPanels.Upgrade): an icon button per
				// choice (the target's portrait), or "Cancel Upgrading" over the progress overlay
				// while it upgrades. Upgrade.getItems shows nothing when the selection mixes templates.
				bool one_template = true;
				for (uint32_t id : host_->ui_selected_structures())
					for (const auto &o : sim->buildings())
						if (o.id == id) {
							one_template = one_template && o.type == found_b->type && o.civ == found_b->civ;
							break;
						}
				if (found_b->team == local_player() && one_template) {
					const bool upgrading = found_b->upgrade_to != rtso::BLD_TYPE_COUNT;
					for (int choice = 0; choice < rtso::building_upgrade_count(found_b->type, sim->structure_upgrade_civ(*found_b)); ++choice) {
						const rtso::BuildingUpgradeInfo up = rtso::building_upgrade(found_b->type, sim->structure_upgrade_civ(*found_b), choice);
						const rtso::Simulation::UpgradeCheck check = sim->upgrade_check(local_player(), found_b->id, choice);
						// Not this piece's (only wall_long and palisades_long become gates).
						if (check == rtso::Simulation::UPGRADE_ERR_NONE && !upgrading) continue;
						if (upgrading && up.to != found_b->upgrade_to) continue;
						const Dictionary text = structure_template_text(pciv, up.to);
						const String generic = text.get("generic_name", String::utf8(rtso::building_type_name(up.to)));
						const String specific = text.get("specific_name", String());
						Button *btn = icon_button(String::utf8(rtso::building_type_name(up.to)), rtso::portraits::structure(pciv, up.to));
						TextureRect *icon = Object::cast_to<TextureRect>(btn->get_child(0));
						String tip;
						if (upgrading) {
							tip = rtso::tr0("Cancel Upgrading");
							const float progress = found_b->upgrade_duration > 0.0f ? found_b->upgrade_timer / found_b->upgrade_duration : 1.0f;
							const int left_px = (int)std::lround((1.0f - std::clamp(progress, 0.0f, 1.0f)) * 32.0f);
							btn->set_meta("progress", left_px);
							ColorRect *slider = memnew(ColorRect);
							slider->set_name("unitUpgradeProgressSlider");
							slider->set_position(Vector2(3, 3));
							slider->set_size(Vector2(32, (float)left_px));
							slider->set_color(Color(0.0f, 1.0f, 0.0f, 0.5f));
							slider->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
							btn->add_child(slider);
							btn->connect("pressed", Callable(owner_, "_on_upgrade_pressed").bind(-1));
						} else if (check == rtso::Simulation::UPGRADE_ERR_BUSY) {
							tip = rtso::tr0("Cannot upgrade when the entity is training, researching or already upgrading.");
							btn->set_disabled(true);
							icon->set_material(grayscale_material());
							icon->set_self_modulate(Color(0.5f, 0.5f, 0.5f));
						} else {
							// Names, cost, the target's limit, requirements, needed resources, right-click.
							tip = rtso::upgrade_to_text(generic, specific, String::utf8(up.tooltip ? up.tooltip : ""));
							// Every selected structure of this type upgrades: the cost is for all of them
							// (getEntityCostTooltip with unitEntStates.length), the time for one.
							int64_t n_up = 0;
							for (uint32_t id : host_->ui_selected_structures())
								for (const auto &o : sim->buildings())
									if (o.id == id && o.team == local_player() && o.type == found_b->type) ++n_up;
							n_up = std::max<int64_t>(1, n_up);
							const rtso::CostTip up_cost{up.food_cost * n_up, up.wood_cost * n_up, up.stone_cost * n_up, up.gold_cost * n_up, 0,
									(int64_t)std::ceil(up.time)};
							const String cost_tip = rtso::cost_tooltip(up_cost, true);
							if (!cost_tip.is_empty()) tip += "\n" + cost_tip;
							const String limit_tip = limit_tooltip(local_player(), rtso::BUILDING_LIMIT_CATEGORY[up.to]);
							if (!limit_tip.is_empty()) tip += "\n" + limit_tip;
							if (check == rtso::Simulation::UPGRADE_ERR_PHASE) tip += "\n" + requires_text(up.required_phase >= 3 ? "City Phase" : "Town Phase");
							const auto &stock = sim->player(local_player());
							const String up_need = rtso::needed_resources_tooltip(up_cost, stock.res[rtso::RES_FOOD], stock.res[rtso::RES_WOOD],
									stock.res[rtso::RES_STONE], stock.res[rtso::RES_GOLD], true);
							if (!up_need.is_empty()) tip += "\n" + up_need;
							else if (check == rtso::Simulation::UPGRADE_ERR_COST) tip += "\n" + needed_text(up_cost);
							tip += "\n" + right_click_tooltip();
							if (check == rtso::Simulation::UPGRADE_ERR_PHASE || check == rtso::Simulation::UPGRADE_ERR_LIMIT) {
								btn->set_disabled(true);
								icon->set_material(grayscale_material());
								icon->set_self_modulate(Color(0.5f, 0.5f, 0.5f));
							} else if (check == rtso::Simulation::UPGRADE_ERR_COST) {
								btn->set_disabled(true);
								ColorRect *mask = memnew(ColorRect);
								mask->set_position(Vector2(3, 3));
								mask->set_size(Vector2(32, 32));
								mask->set_color(Color(0.8f, 0.0f, 0.0f, 0.45f));
								mask->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
								btn->add_child(mask);
							}
							btn->connect("pressed", Callable(owner_, "_on_upgrade_pressed").bind(choice));
						}
						on_right_click_details(btn, "structure", up.to, pciv);
						btn->set_tooltip_text(tip);
						production_bar_->add_child(btn);
						if (upgrading) break;
					}
				}
			}
			// is-dropsite-shared: an ally's sharable dropsite, once we have Diaspora (always disabled).
			if (found_b && found_b->team != local_player() && local_player() < rtso::MAX_PLAYERS &&
					sim->is_ally(local_player(), found_b->team) && rtso::Simulation::is_dropsite_type(found_b->type) &&
					sim->player(local_player()).has_tech(rtso::TECH_UNLOCK_SHARED_DROPSITES))
				add_entity_command("is-dropsite-shared", found_b->dropsite_shared ? "Shared Dropsite" : "Locked Dropsite", "",
						found_b->dropsite_shared ? "Shared Dropsite\nYou are allowed to use this dropsite."
												 : "Locked Dropsite\nThe use of this dropsite is prohibited.",
						found_b->dropsite_shared ? "unlocked_small.png" : "locked_small.png", false);
			// unload-all / unload-all-turrets on an ally's structure (allowedPlayers Player and Ally):
			// counts and unloads only our own units in it (unload-all-by-owner).
			if (found_b && found_b->team != local_player() && local_player() < rtso::MAX_PLAYERS &&
					sim->is_ally(local_player(), found_b->team)) {
				auto uv = sim->units();
				auto own = [&](const std::vector<uint32_t> &ids) {
					int n = 0;
					for (uint32_t id : ids)
						for (size_t i = 0; i < uv.count; ++i)
							if (uv.id[i] == id) { n += uv.team[i] == local_player(); break; }
					return n;
				};
				if (const int inside = own(found_b->garrisoned_units))
					add_unload_all(sim->turret_only_holder(*found_b), inside);
				if (const int turrets = own(found_b->turret_units))
					add_entity_command("unload-all-turrets", "Unload Turrets", "session.unloadturrets",
							"Unload Turrets\nOrder all units to leave the selected turret points.", "garrison-out.png", true, turrets);
			}
			// The structure's command row (unit_actions.js g_EntityCommands), from every own
			// selected structure as 0.28's getInfo(entStates) reads the whole selection.
			if (found_b && found_b->team == local_player()) {
				std::vector<const rtso::Simulation::Building *> own{found_b};
				for (uint32_t id : host_->ui_selected_structures())
					if (id != found_b->id)
						for (const auto &o : sim->buildings())
							if (o.id == id) {
								if (o.team == local_player() && !o.wrecked) own.push_back(&o);
								break;
							}
				using rtso::tr0;
				auto tip = [](const char *title, const char *body) { return tr0(title) + "\n" + tr0(body); };
				// A corral's penned animals are its garrison too (GarrisonHolder List Animal).
				int inside = 0, turrets = 0;
				bool turret_only = true, deletable = false, market = false, sharable = false, any_locked = false;
				bool rally = false, queue_off = false, queue_on = false;
				for (const auto *o : own) {
					const int n = (int)o->garrisoned_units.size() + (o->type == rtso::BLD_CORRAL ? sim->corral_count(o->id) : 0);
					if (n > 0) {
						inside += n;
						turret_only = turret_only && sim->turret_only_holder(*o);
					}
					turrets += (int)o->turret_units.size();
					// isUndeletable: you must hold half the capture points.
					deletable = deletable || o->max_capture_points <= 0.0f ||
							o->capture_points_by_team[o->team] >= o->max_capture_points * 0.5f;
					rally = rally || o->has_rally_point;
					const bool done = o->build_progress >= 1.0f;
					market = market || (o->type == rtso::BLD_MARKET && done);
					if (rtso::Simulation::is_dropsite_type(o->type) && done) {
						sharable = true;
						any_locked = any_locked || !o->dropsite_shared;
					}
					if (done && ((o == found_b && roster_list_ && roster_list_->get_item_count() > 0) || host_->ui_trains_units(o->id))) {
						queue_off = queue_off || !o->autoqueue;
						queue_on = queue_on || o->autoqueue;
					}
				}
				if (inside > 0)
					add_unload_all(turret_only, inside);
				// "unload-all-turrets": the rampart tower's turret units (a wall's are its garrison above).
				if (turrets > 0)
					add_entity_command("unload-all-turrets", "Unload Turrets", "session.unloadturrets",
							tip("Unload Turrets", "Order all units to leave the selected turret points."), "garrison-out.png", true,
							turrets);
				if (!deletable)
					add_entity_command("delete", "Delete", "",
							tr0("You cannot destroy this entity as you own less than half the capture points"), "kill_small_disabled.png",
							false);
				else
					add_entity_command("delete", "Delete", "session.kill", self_destruct_tooltip(), "kill_small.png");
				if (rally)
					add_entity_command("focus-rally", "Focus on Rally Point", "camera.rallypointfocus",
							tip("Focus on Rally Point", "Center the view on the selected rally point."), "focus-rally.png");
				if (market)
					add_entity_command("select-trading-goods", "Barter & Trade", "session.gui.barter.toggle",
							tip("Barter & Trade", "Open the dialog for managing resource trading and bartering."), "economics.png");
				// share-dropsite: a sharable dropsite whose owner has a mutual ally (no technology: the
				// owner may always set it; Diaspora only lets allies drop there).
				bool mutual_ally = false;
				for (uint8_t p = 0; p < rtso::MAX_PLAYERS && !mutual_ally; ++p)
					mutual_ally = p != local_player() && sim->player(p).active && sim->is_ally(local_player(), p) &&
							sim->is_ally(p, local_player());
				if (sharable && mutual_ally)
					add_entity_command("share-dropsite", any_locked ? "Share Dropsite" : "Lock Dropsite", "",
							any_locked ? tip("Share Dropsite", "Allow allies to use this dropsite, now locked.")
									   : tip("Lock Dropsite", "Prevent allies from using this dropsite, now shared."),
							any_locked ? "locked_small.png" : "unlocked_small.png");
				// autoqueue-on / autoqueue-off: each shows while some trainer is in the other state.
				if (queue_off)
					add_entity_command("autoqueue-on", "Activate Auto-Queue", "session.queueunit.autoqueueon",
							tip("Activate Auto-Queue", "Activate the production auto-queue for the selected structures."),
							"autoqueue-on.png");
				if (queue_on)
					add_entity_command("autoqueue-off", "Deactivate Auto-Queue", "session.queueunit.autoqueueoff",
							tip("Deactivate Auto-Queue", "Deactivate the production auto-queue for the selected structures."),
							"autoqueue-off.png");
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
					"testudo", "testudo", "phalanx"};
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
				// g_SelectionPanels.Formation conflictsWith Garrison.
				const uint32_t available = any_can_form && !host_->ui_garrison_panel_shown() ? sim->player_formation_mask(local_player()) : 0u;
				const uint8_t default_f = default_formation();
				for (uint8_t f = 0; f < rtso::FORMATION_COUNT; ++f) {
					if (!((available >> f) & 1u)) continue;
					// getItems: only the formations some selected unit can take.
					bool someone_can = f == rtso::FORMATION_NONE;
					for (size_t i = 0; i < sel_n && !someone_can; ++i) someone_can = sim->unit_can_use_formation(sel_ids[i], f);
					if (!someone_can) continue;
					const rtso::FormationTemplate &ft = rtso::formation_template(f);
					const bool enabled = f == rtso::FORMATION_NONE || sim->can_move_into_formation(sel_ids, sel_n, f);
					// The Formation panel's buttons are icons; the name is on the tooltip.
					Button *fb = memnew(Button);
					fb->set_custom_minimum_size(Vector2(26, 26));
					fb->set_clip_text(true);
					// setupButton: name, description, the default-formation line, then why not.
					String tip = String::utf8(ft.name);
					if (ft.tooltip && *ft.tooltip) tip += String("\n") + String::utf8(ft.tooltip);
					const bool is_default = f == default_f;
					if (f == rtso::FORMATION_NONE)
						tip += is_default ? "\nDefault formation is disabled." : "\nRight-click to disable the default formation feature.";
					else
						tip += is_default ? "\nThis is the default formation, used for movement orders." : "\nRight-click to set this as the default formation.";
					if (!enabled && ft.disabled_tooltip && *ft.disabled_tooltip)
						tip += String("\n") + String::utf8(ft.disabled_tooltip);
					fb->set_tooltip_text(tip);
					if (is_default) { // countDisplay: the default formation is marked
						Label *mark = memnew(Label);
						mark->set_text(U"\u2605");
						mark->add_theme_font_size_override("font_size", 9);
						mark->add_theme_color_override("font_color", Color(1.0f, 0.85f, 0.2f));
						mark->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
						mark->set_position(Vector2(16, -2));
						fb->add_child(mark);
					}
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
					fb->connect("gui_input", Callable(owner_, "_on_formation_gui_input").bind((int)f));
					actions_bar_->add_child(fb);
				}

				// Villager Build Menu
				bool has_worker = false;
				for (int i = 0; i < selected_ids.size(); ++i) {
					for (size_t u = 0; u < uview.count; ++u) {
						if (uview.id[u] == (uint32_t)selected_ids[i]) {
							uint8_t ut = uview.unit_type[u];
							if (rtso::unit_can_build(ut, uview.tmpl[u]) && !sim->nonbuilder(uview.id[u])) {
								has_worker = true;
								break;
							}
						}
					}
				}

				// 0.28's Pack panel (g_SelectionPanels.Pack): Pack (pack.png) for unpacked engines,
				// Unpack (unpack.png) for packed ones, Cancel Packing / Cancel Unpacking (cancel.png)
				// for those in progress.
				{
					bool pack_btn = false, unpack_btn = false, pack_cancel = false, unpack_cancel = false;
					for (int i = 0; i < selected_ids.size(); ++i) {
						for (size_t u = 0; u < uview.count; ++u) {
							if (uview.id[u] != (uint32_t)selected_ids[i] || !uview.alive[u]) continue;
							const uint8_t ps = uview.pack_state[u];
							pack_btn = pack_btn || ps == rtso::PACK_UNPACKED;
							unpack_btn = unpack_btn || ps == rtso::PACK_PACKED;
							pack_cancel = pack_cancel || ps == rtso::PACK_PACKING;
							unpack_cancel = unpack_cancel || ps == rtso::PACK_UNPACKING;
							break;
						}
					}
					const struct { bool show; const char *tip; const char *icon; const char *method; int pack; } kPack[4] = {
						{pack_btn, "Pack", "pack.png", "_on_pack_pressed", 1},
						{unpack_btn, "Unpack", "unpack.png", "_on_pack_pressed", 0},
						{pack_cancel, "Cancel Packing", "cancel.png", "_on_pack_cancel_pressed", 1},
						{unpack_cancel, "Cancel Unpacking", "cancel.png", "_on_pack_cancel_pressed", 0},
					};
					for (const auto &pb : kPack) {
						if (!pb.show) continue;
						Button *btn = icon_button(pb.tip, rtso::load_ui_texture(String("res://assets/textures/ui/archive/icons/") + pb.icon));
						btn->set_tooltip_text(rtso::tr0(pb.tip));
						btn->connect("pressed", Callable(owner_, pb.method).bind(pb.pack));
						production_bar_->add_child(btn);
					}
				}

				// 0.28's Upgrade panel (g_SelectionPanels.Upgrade): only when every selected unit has
				// the same template, an icon button per target (the target's portrait).
				{
					std::vector<uint32_t> own; // the selected units we own, all of one template
					uint16_t tmpl0 = rtso::CIV_UNIT_TEMPLATE_NONE;
					bool same = true;
					uint16_t targets[8];
					int target_n = 0;
					for (int i = 0; i < selected_ids.size() && same; ++i) {
						const uint32_t id = (uint32_t)selected_ids[i];
						for (size_t u = 0; u < uview.count; ++u) {
							if (uview.id[u] != id) continue;
							const uint16_t t = uview.tmpl ? uview.tmpl[u] : rtso::CIV_UNIT_TEMPLATE_NONE;
							if (own.empty() && tmpl0 == rtso::CIV_UNIT_TEMPLATE_NONE) tmpl0 = t;
							if (t != tmpl0) { same = false; break; }
							if (uview.team[u] != local_player()) break;
							own.push_back(id);
							for (const rtso::CivUnitUpgrade *up = rtso::civ_unit_upgrade_table(); up->from != 0xFFFF; ++up) {
								if (up->from != t || (up->from_rank && uview.rank[u] < up->from_rank)) continue;
								bool seen = false;
								for (int k = 0; k < target_n; ++k) seen = seen || targets[k] == up->to;
								if (!seen && target_n < 8) targets[target_n++] = up->to;
							}
							break;
						}
					}
					if (!same || own.empty()) target_n = 0;
					const auto &ps = sim->player(local_player());
					for (int k = 0; k < target_n; ++k) {
						const uint16_t to = targets[k];
						const rtso::CivUnitUpgrade *row = nullptr;
						for (const rtso::CivUnitUpgrade *up = rtso::civ_unit_upgrade_table(); up->from != 0xFFFF && !row; ++up)
							if (up->from == tmpl0 && up->to == to) row = up;
						if (!row) continue;
						const rtso::CivUnitTemplate &tt = rtso::civ_unit_template(to);
						// upgradingEntStates (to this target) and upgradableEntStates (not upgrading).
						int upgrading = 0, upgradable = 0;
						float progress = 0.0f;
						rtso::Simulation::UpgradeCheck check = rtso::Simulation::UPGRADE_ERR_NONE;
						for (uint32_t id : own) {
							const rtso::Simulation::UnitUpgrade *uu = sim->unit_upgrade_of(id);
							if (uu && uu->to == to) {
								++upgrading;
								progress = std::max(progress, uu->time > 0.0f ? uu->elapsed / uu->time : 1.0f);
							} else if (!uu) {
								++upgradable;
								const rtso::Simulation::UpgradeCheck c = sim->unit_upgrade_check(local_player(), id, to);
								if (check == rtso::Simulation::UPGRADE_ERR_NONE || c == rtso::Simulation::UPGRADE_OK) check = c;
							}
						}
						Button *btn = memnew(Button);
						btn->set_meta("label", String::utf8(tt.name));
						btn->set_custom_minimum_size(Vector2(38, 38));
						btn->set_focus_mode(Control::FOCUS_NONE);
						TextureRect *icon = memnew(TextureRect);
						icon->set_name("unitUpgradeIcon");
						icon->set_position(Vector2(3, 3));
						icon->set_size(Vector2(32, 32));
						icon->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
						icon->set_stretch_mode(TextureRect::STRETCH_SCALE);
						icon->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
						icon->set_texture(rtso::portraits::unit(to, tt.unit_type));
						btn->add_child(icon);
						auto grey = [&]() {
							icon->set_material(grayscale_material());
							icon->set_self_modulate(Color(0.5f, 0.5f, 0.5f));
						};
						auto count_label = [&](int n) {
							if (n < 2) return;
							Label *cnt = memnew(Label);
							cnt->set_name("unitUpgradeCount");
							cnt->set_text(String::num_int64(n));
							cnt->add_theme_font_size_override("font_size", 11);
							cnt->add_theme_color_override("font_outline_color", Color(0, 0, 0));
							cnt->add_theme_constant_override("outline_size", 3);
							cnt->set_position(Vector2(22, 20));
							cnt->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
							btn->add_child(cnt);
						};
						String tip;
						if (upgrading == 0 && upgradable > 0) {
							// "Upgrade to a <specific> (<generic>).", the cost for the whole selection,
							// requirements, the resources the upgradable ones lack, the right-click line.
							tip = rtso::upgrade_to_text(String::utf8(tt.name), String::utf8(tt.specific_name ? tt.specific_name : ""), String());
							const int n_all = (int)own.size();
							const rtso::CostTip all_cost{(int64_t)row->food_cost * n_all, (int64_t)row->wood_cost * n_all,
									(int64_t)row->stone_cost * n_all, (int64_t)row->gold_cost * n_all, 0, (int64_t)std::ceil(row->time)};
							const String cost_tip = rtso::cost_tooltip(all_cost, true);
							if (!cost_tip.is_empty()) tip += "\n" + cost_tip;
							if (to < rtso::CIV_UNIT_TEMPLATE_COUNT) {
								const String limit_tip = limit_tooltip(local_player(), rtso::UNIT_LIMITS[to].category);
								if (!limit_tip.is_empty()) tip += "\n" + limit_tip;
							}
							if (check == rtso::Simulation::UPGRADE_ERR_PHASE) tip += "\n" + requires_text("City Phase");
							const rtso::CostTip need_cost{(int64_t)row->food_cost * upgradable, (int64_t)row->wood_cost * upgradable,
									(int64_t)row->stone_cost * upgradable, (int64_t)row->gold_cost * upgradable, 0, 0};
							const String need = rtso::needed_resources_tooltip(need_cost, ps.res[rtso::RES_FOOD], ps.res[rtso::RES_WOOD],
									ps.res[rtso::RES_STONE], ps.res[rtso::RES_GOLD], true);
							if (!need.is_empty()) tip += "\n" + need;
							tip += "\n" + right_click_tooltip();
							if (check == rtso::Simulation::UPGRADE_ERR_PHASE || check == rtso::Simulation::UPGRADE_ERR_LIMIT) {
								btn->set_disabled(true);
								grey();
							} else if (!need.is_empty()) {
								btn->set_disabled(true);
								ColorRect *mask = memnew(ColorRect);
								mask->set_position(Vector2(3, 3));
								mask->set_size(Vector2(32, 32));
								mask->set_color(Color(0.8f, 0.0f, 0.0f, 0.45f));
								mask->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
								btn->add_child(mask);
							}
							count_label(upgradable);
							btn->connect("pressed", Callable(owner_, "_on_unit_upgrade_pressed").bind((int)to));
						} else if (upgrading > 0) {
							// "Cancel Upgrading" over the progress (unitUpgradeProgressSlider, the
							// leading upgrade's share left, shrinking from the top).
							tip = rtso::tr0("Cancel Upgrading");
							count_label(upgrading);
							const int left_px = (int)std::lround((1.0f - std::min(progress, 1.0f)) * 32.0f);
							btn->set_meta("progress", left_px);
							ColorRect *slider = memnew(ColorRect);
							slider->set_name("unitUpgradeProgressSlider");
							slider->set_position(Vector2(3, 3));
							slider->set_size(Vector2(32, (float)left_px));
							slider->set_color(Color(0.0f, 1.0f, 0.0f, 0.5f));
							slider->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
							btn->add_child(slider);
							btn->connect("pressed", Callable(owner_, "_on_unit_upgrade_pressed").bind(-1));
						} else {
							tip = own.size() > 1
									? rtso::tr0("Cannot upgrade when all entities are training, researching or already upgrading.")
									: rtso::tr0("Cannot upgrade when the entity is training, researching or already upgrading.");
							btn->set_disabled(true);
							grey();
						}
						on_right_click_details(btn, "unit", to, sim->player_civ(local_player()));
						btn->set_tooltip_text(tip);
						production_bar_->add_child(btn);
					}
				}

				// The command row, g_EntityCommands in unit_actions.js order: each shows when
				// its getInfo would (any selected unit it applies to).
				{
					bool any_guarding = false, any_busy = false, any_work = false, any_fighter = false;
					bool any_guard = false, any_can_add_guard = false, any_garrison = false, any_deletable = false;
					bool any_turret = false; // "leave-turret": a selected unit on a turret point
					bool any_turretable = false; // "occupy-turret": a Turretable unit not on one
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
							any_turret = any_turret || sim->turret_holder_of(id) != 0;
							any_turretable = any_turretable || (sim->turret_holder_of(id) == 0 && (cls & rtso::TC_INFANTRY) != 0 && (cls & rtso::TC_RANGED) != 0);
							if (sim->is_transport(id)) passengers += sim->get_transport_passenger_count(id);
							any_deletable = any_deletable || (ut != rtso::UNIT_CATAFALQUE && !sim->undeletable_unit(id)); // isUndeletable
							break;
						}
					}
					if (passengers > 0)
						add_entity_command("unload-all", "Unload All", "session.unload",
								"Unload All\nOrder all units to leave the selected entities.", "garrison-out.png", true, passengers);
					if (any_deletable)
						add_entity_command("delete", "Delete", "session.kill", self_destruct_tooltip(), "kill_small.png");
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
					if (any_turretable)
						add_entity_command("occupy-turret", "Occupy Turret", "session.occupyturret",
								"Occupy Turret\nOrder the selected units to occupy a turret point.", "occupy-turret.png");
					if (any_turret)
						add_entity_command("leave-turret", "Unload", "session.leaveturret",
								"Unload\nOrder the selected units to leave their turret points.", "leave-turret.png");
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
						rtso::BLD_TOWN_CENTER, rtso::BLD_HOUSE, rtso::BLD_STOREHOUSE, rtso::BLD_FARMSTEAD, rtso::BLD_FIELD,
						rtso::BLD_CORRAL, rtso::BLD_DOCK, rtso::BLD_BARRACKS, rtso::BLD_STABLE,
						rtso::BLD_RANGE, rtso::BLD_SENTRY_TOWER, rtso::BLD_OUTPOST, rtso::BLD_PALISADE,
						rtso::BLD_FORGE, rtso::BLD_MARKET, rtso::BLD_TEMPLE, rtso::BLD_TOWER,
						rtso::BLD_EMBASSY, rtso::BLD_WALL_SEGMENT,
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
						uint8_t need = rtso::building_required_phase((uint8_t)bt, my_civ);
						// 0.28's Construction panel: an icon grid, name and cost on the tooltip.
						Button *bb = memnew(Button);
						const String label = String::utf8(bi.name) + (cost.is_empty() ? String() : " (" + cost + ")");
						bb->set_meta("label", label);
						// 0.28's Construction tooltip: names, Identity/Tooltip, cost and time, garrison,
						// population bonus (selection_panels.js Construction setupButton).
						const Dictionary text = structure_template_text(my_civ, bt);
						const String generic = text.get("generic_name", String::utf8(bi.name));
						const String specific = text.get("specific_name", String());
						String tip = rtso::entity_names_formatted(generic, specific);
						auto add_line = [&tip](const String &line) { if (!line.is_empty()) tip += "\n" + line; };
						add_line(classes_tooltip(text));
						add_line(structure_auras_tooltip(text));
						add_line(identity_tooltip(text));
						const rtso::CostTip b_cost{(int64_t)price[rtso::RES_FOOD], (int64_t)price[rtso::RES_WOOD],
								(int64_t)price[rtso::RES_STONE], (int64_t)price[rtso::RES_GOLD], 0,
								(int64_t)std::ceil(sim->structure_build_time_for(local_player(), (uint8_t)bt))};
						if (const rtso::WallSetInfo ws = rtso::wallset_info(rtso::wallset_kind_of((uint8_t)bt), my_civ); ws.segment == bt)
							add_line(wallset_cost_tooltip(ws));
						else
							add_line(rtso::cost_tooltip(b_cost, true));
						add_line(dropsite_tooltip(text));
						const int garrison = rtso::structure_garrison_max((uint8_t)bt, my_civ);
						if (garrison > 0)
							tip += "\n" + rtso::sprintf0(rtso::tr0("%(label)s: %(garrisonLimit)s"),
									{{"label", rtso::header_font(rtso::tr0("Garrison Limit"))}, {"garrisonLimit", String::num_int64(garrison)}});
						if (const float heal = sim->structure_garrison_heal_for((uint8_t)bt, local_player()); garrison > 0 && heal > 0.0f)
							tip += "\n" + rtso::sprintf0(rtso::tr0("%(healRateLabel)s %(value)s %(health)s / %(second)s"),
									{{"healRateLabel", rtso::header_font(rtso::tr0("Heal:"))}, {"value", String::num_int64(std::lround(heal))},
											{"health", rtso::tr0("Health")}, {"second", rtso::tr0("second")}});
						add_line(turrets_tooltip(text));
						const int pop_bonus = sim->structure_pop_bonus((uint8_t)bt, local_player());
						if (pop_bonus > 0)
							tip += "\n" + rtso::sprintf0(rtso::tr0("%(label)s %(bonus)s"),
									{{"label", rtso::header_font(rtso::tr0("Population Bonus:"))}, {"bonus", String::num_int64(pop_bonus)}});
						add_line(right_click_tooltip());
						on_right_click_details(bb, "structure", bt, my_civ);
						add_line(limit_tooltip(local_player(), rtso::BUILDING_LIMIT_CATEGORY[bt]));
						Ref<Texture2D> b_ico = structure_icon(bi.portrait_id);
						if (b_ico.is_valid()) {
							bb->set_button_icon(b_ico);
							bb->set_expand_icon(true);
							bb->set_custom_minimum_size(Vector2(38, 38));
						} else {
							bb->set_text(label);
						}
						bb->add_theme_font_size_override("font_size", 9);
						const bool reqs_failed = need > sim->player_phase(local_player());
						if (reqs_failed) tip += "\n" + requires_text(String(kPhaseNames[need]) + " Phase");
						// selection_panels.js Construction: requirements unmet or the entity limit
						// reached grey the icon; missing resources mask it red and list what is
						// needed; both disable the button.
						const uint8_t limit_cat = rtso::BUILDING_LIMIT_CATEGORY[bt];
						const int limit = limit_cat == rtso::LIMIT_NONE || limit_cat >= rtso::LIMIT_CATEGORY_COUNT ? -1
								: sim->entity_limit(local_player(), limit_cat);
						const bool at_limit = limit >= 0 && sim->entity_limit_count(local_player(), limit_cat) >= limit;
						const auto &ps = sim->player(local_player());
						bool short_of = false;
						for (int r = 0; r < rtso::RES_COUNT; ++r) short_of |= ps.res[r] < price[r];
						if (short_of) tip += "\n" + needed_text(b_cost);
						if (reqs_failed || at_limit) {
							bb->set_self_modulate(Color(0.5f, 0.5f, 0.5f));
							bb->set_meta("grey", true);
						} else if (short_of) {
							ColorRect *mask = memnew(ColorRect);
							mask->set_name("resourcesMask");
							mask->set_position(Vector2(3, 3));
							mask->set_size(Vector2(32, 32));
							mask->set_color(Color(0.8f, 0.0f, 0.0f, 0.45f));
							mask->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
							bb->add_child(mask);
						}
						bb->set_disabled(reqs_failed || at_limit || short_of);
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
		keep_buttons(commands_row_, old_commands);
		for (int st = 0; st < 5; ++st)
			if (stance_buttons_[st] && stance_buttons_[st]->is_disabled() != read_only_) stance_buttons_[st]->set_disabled(read_only_);
		if (read_only_) {
			disable_buttons(actions_bar_);
			disable_buttons(production_bar_);
			disable_buttons(commands_row_);
			if (roster_list_)
				for (int i = 0; i < roster_list_->get_item_count(); ++i) roster_list_->set_item_disabled(i, true);
		}
		// The queue strip shows while something is queued (0.28's Queue panel shows
		// only queued items).
		bool queue_shown = false;
		if (selected_building_id != 0)
			for (const auto &b : sim->buildings())
				if (b.id == selected_building_id) { queue_shown = !b.queue.empty(); break; }
		if (queue_bar_->is_visible() != queue_shown) queue_bar_->set_visible(queue_shown);
		// A builder's construction grid can outgrow the two rows a unit selection
		// has: the panel grows upward to show all of it (0.28 shows the whole
		// Construction panel), up to kMaxGrow, past which it scrolls.
		// With a Training grid on top, the other buttons take the bottom two rows
		// (research_panel.xml: 6 100%-82).
		constexpr float kMaxGrow = 120.0f;
		const bool training = roster_list_ && roster_list_->is_visible();
		const float base = training ? 78.0f : kPanelH - 12.0f;
		const float h = std::clamp(production_bar_->get_combined_minimum_size().y, base, base + kMaxGrow);
		const Vector2 area(kGridW, h);
		const Vector2 at(6, training ? kPanelH - 82.0f : 6.0f);
		if (production_scroll_->get_size() != area) production_scroll_->set_size(area);
		if (production_scroll_->get_position() != at) production_scroll_->set_position(at);
		const float top = -10.0f - kPanelH - (h - base);
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
		uint64_t key = ((uint64_t)b->id << 32) | ((uint64_t)sim->ally_civ_mask(pid) << 12) |
			(unlocks << 8) | ((uint64_t)(civ & 0x3F) << 2) | (uint64_t)(phase & 0x3);
		// getAllTrainableEntitiesFromSelection: every selected finished structure's list, the
		// first structure's first (a corral alone keeps its animals).
		std::vector<const rtso::Simulation::Building *> offering{b};
		if (b->type != rtso::BLD_CORRAL)
			for (uint32_t id : host_->ui_selected_structures())
				for (const auto &o : sim->buildings())
					if (o.id == id && o.id != b->id && o.team == pid && o.build_progress >= 1.0f && !o.wrecked &&
							o.type != rtso::BLD_CORRAL) {
						offering.push_back(&o);
						key = key * 1000003ull + o.id;
					}
		// The tooltips' "Current Count: x, Limit: y." lines follow the counts.
		for (uint8_t c = 0; c < rtso::LIMIT_CATEGORY_COUNT; ++c)
			key = key * 1000003ull + (uint64_t)(sim->entity_limit_count(pid, c) + 1);
		// A changed option (gui.session.howtoshownames) rebuilds it too.
		if (key == roster_list_key_ && roster_list_gen_ == rtso::user_config::generation() &&
				roster_list_batch_ == host_->ui_batch_size()) {
			refresh_roster_state(b);
			return;
		}
		roster_list_key_ = key;
		roster_list_gen_ = rtso::user_config::generation();
		roster_list_batch_ = host_->ui_batch_size(); // Shift+wheel changes the batch line
		clear_roster();
		// A corral trains its civ's animals (structures/<civ>/corral.xml Trainer/Entities);
		// metadata ROSTER_ANIMAL | FaunaType tells the click handler to breed one.
		if (b->type == rtso::BLD_CORRAL) {
			const rtso::CorralTrainable &ct = rtso::CORRAL_TRAINABLE[civ < rtso::CIV_COUNT ? civ : 0];
			for (uint8_t k = 0; k < ct.count; ++k) {
				const uint8_t f = ct.fauna[k];
				const rtso::FaunaTrainCost &c = rtso::fauna_train_cost(f);
				String label = String::utf8(rtso::fauna_type_info(f).name) + " (" + String::num_int64(c.res[0]) + "f)";
				const int idx = add_roster_icon(label, rtso::portraits::fauna(f), ROSTER_ANIMAL | (int)f);
				// The Training panel's tooltip for gaia/fauna_*_trainable: hotkey and names, Tooltip,
				// cost, limit, batch line and requirements; the live part adds the rest.
				const Dictionary gaia = reference_templates().get("gaia", Dictionary());
				const Dictionary text = gaia.get(String("gaia/") + rtso::fauna_type_info(f).template_name, Dictionary());
				const std::string hotkey = rtso::hotkeys::table().label("session.queueunit." + std::to_string(idx + 1));
				String tip = (hotkey.empty() ? String() : rtso::hotkey_tag(String(hotkey.c_str())) + " ") +
						rtso::entity_names_formatted(String(text.get("generic_name", String::utf8(rtso::fauna_type_info(f).name))),
								String(text.get("specific_name", String())));
				auto add_line = [&tip](const String &line) { if (!line.is_empty()) tip += "\n" + line; };
				add_line(identity_tooltip(text));
				tip += "\n" + String(COST_SLOT);
				add_line(limit_tooltip(pid, rtso::LIMIT_ANIMAL));
				tip += "\n" + String(BATCH_SLOT);
				const uint8_t need_phase = rtso::Simulation::livestock_required_phase(f);
				if (need_phase > phase) tip += "\n" + requires_text(need_phase >= 3 ? "City Phase" : "Town Phase");
				set_roster_tip(idx, tip);
				if (rtso::Simulation::livestock_required_phase(f) > phase) roster_list_->set_item_disabled(idx, true);
			}
			finish_roster(b);
			return;
		}
		// units/{native}/... lists (shrines, Kushite camps, Carthaginian embassies, a captured
		// civic centre's civilian) offer the structure's own civ's units.
		std::vector<uint16_t> listed;
		for (const rtso::Simulation::Building *ob : offering)
		for (int pass = 0; pass < 2; ++pass) {
		if (pass == 1 && ob->civ == civ) break;
		for (uint16_t i : rtso::civ_roster_ids(pass == 0 ? ob->civ : civ)) {
			const rtso::CivUnitTemplate &u = rtso::civ_unit_template(i);
			if (!(u.trainers & rtso::bld_bit(ob->type))) continue;
			if (rtso::building_train_civ(ob->type, ob->civ, civ, u) != u.civ) continue;
			// RTS0-only rows (<civ>/rts0_villager) back class training for the AI and old saves;
			// 0 A.D. has no such unit, so players don't see it.
			if (std::strstr(u.id, "/rts0_")) continue;
			if (std::find(listed.begin(), listed.end(), i) != listed.end()) continue;
			listed.push_back(i);
			String tip;
			const int idx = add_roster_item(pid, i, tip);
			if (u.phase > phase) {
				roster_list_->set_item_disabled(idx, true);
				tip += "\n" + requires_text(u.phase >= 3 ? "City Phase" : "Town Phase");
			} else if (!sim->can_train_at(pid, u.unit_type, ob->type, i)) {
				roster_list_->set_item_disabled(idx, true);
				uint8_t gate = sim->train_unlock_tech(pid, u.unit_type, ob->type, i);
				if (gate != rtso::TECH_NONE) tip += "\n" + requires_text(String::utf8(rtso::tech_info(gate).name));
			}
			set_roster_tip(idx, tip);
		}
		}
		finish_roster(b);
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
		if (key == roster_list_key_) {
			refresh_roster_state(nullptr);
			return true;
		}
		roster_list_key_ = key;
		clear_roster();
		for (size_t k = 0; k < all.size(); ++k) {
			String tip;
			const int idx = add_roster_item(pid, all[k], tip);
			if (!(can_mask & (1ull << k))) {
				// getRequirementsTooltip: the phase or technology the unit waits on.
				roster_list_->set_item_disabled(idx, true);
				const rtso::CivUnitTemplate &tu = rtso::civ_unit_template(all[k]);
				const uint8_t gate = rtso::Simulation::unit_trainee_tech(tmpl, all[k]);
				const uint8_t unlock = sim->train_unlock_tech(pid, tu.unit_type, rtso::BLD_TYPE_COUNT, all[k]);
				// The ships' marines and archers: Techs "iphicratean_reforms" (their phase follows from it).
				if (gate != rtso::TECH_NONE && !sim->player(pid).has_tech(gate)) tip += "\n" + requires_text(String::utf8(rtso::tech_info(gate).name));
				else if (unlock != rtso::TECH_NONE) tip += "\n" + requires_text(String::utf8(rtso::tech_info(unlock).name));
				else if (tu.phase > sim->player_phase(pid)) tip += "\n" + requires_text(tu.phase >= 3 ? "City Phase" : "Town Phase");
			}
			set_roster_tip(idx, tip);
		}
		for (uint8_t t : techs) {
			const auto check = sim->research_check(pid, t);
			if (check == rtso::Simulation::TECH_ERR_RESEARCHED || check == rtso::Simulation::TECH_ERR_IN_PROGRESS) continue;
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
			const int idx = add_roster_icon(String::utf8(ti.name) + " (" + costs + ")", rtso::RtsoResearchProgress::icon(t), ROSTER_TECH | (int)t);
			const rtso::CostTip tech_cost{(int64_t)paid[rtso::RES_FOOD], (int64_t)paid[rtso::RES_WOOD], (int64_t)paid[rtso::RES_STONE],
					(int64_t)paid[rtso::RES_GOLD], 0, (int64_t)std::ceil(rtime)};
			// The Research tooltip: names, tooltip, cost, right-click, requirements, needed resources.
			const char *tech_specific = rtso::tech_specific_name(t, sim->player_civ(pid));
			String tip = rtso::entity_names_formatted(String::utf8(ti.name), tech_specific ? String::utf8(tech_specific) : String());
			if (ti.desc && *ti.desc) tip += "\n" + String::utf8(ti.desc);
			const String cost_line = rtso::cost_tooltip(tech_cost, true);
			if (!cost_line.is_empty()) tip += "\n" + cost_line;
			tip += "\n" + right_click_tooltip();
			if (check != rtso::Simulation::TECH_OK && check != rtso::Simulation::TECH_ERR_COST) {
				roster_list_->set_item_disabled(idx, true);
				if (const char *req = rtso::tech_requirements_tooltip(t)) tip += "\n" + rtso::objection_font(owner_->tr(String::utf8(req)));
				else if (check == rtso::Simulation::TECH_ERR_PHASE) tip += "\n" + requires_text(ti.required_phase >= 3 ? "City Phase" : "Town Phase");
				else if (ti.prerequisite != rtso::TECH_NONE) tip += "\n" + requires_text(String::utf8(rtso::tech_info(ti.prerequisite).name));
			}
			const auto &ps = sim->player(pid);
			const String need = rtso::needed_resources_tooltip(tech_cost, ps.res[rtso::RES_FOOD], ps.res[rtso::RES_WOOD],
					ps.res[rtso::RES_STONE], ps.res[rtso::RES_GOLD], true);
			if (!need.is_empty()) tip += "\n" + need;
			set_roster_tip(idx, tip);
		}
		finish_roster(nullptr);
		return true;
	}

	void clear_roster() {
		roster_list_->clear();
		roster_labels_.clear();
		roster_base_tips_.clear();
		roster_state_.clear();
		roster_req_disabled_.clear();
	}

	// A portrait cell; `label` (name and cost) is kept for tests and the debug hooks.
	int add_roster_icon(const godot::String &label, const godot::Ref<godot::Texture2D> &icon, int meta) {
		const int idx = roster_list_->add_item(godot::String(), icon);
		roster_list_->set_item_metadata(idx, meta);
		roster_labels_.push_back(label);
		roster_base_tips_.push_back(godot::String());
		roster_state_.push_back(godot::String());
		return idx;
	}

	void set_roster_tip(int idx, const godot::String &tip) {
		roster_list_->set_item_tooltip(idx, tip);
		if (idx >= 0 && idx < (int)roster_base_tips_.size()) roster_base_tips_[idx] = tip;
	}

	// After a rebuild: which cells their requirements disabled, then the live state.
	void finish_roster(const rtso::Simulation::Building *b) {
		roster_req_disabled_.assign(roster_list_->get_item_count(), false);
		for (int k = 0; k < roster_list_->get_item_count(); ++k) roster_req_disabled_[k] = roster_list_->is_item_disabled(k);
		refresh_roster_state(b);
		roster_list_->set_visible(roster_list_->get_item_count() > 0);
	}

	// selection_panels.js Training setupButton, every frame: getTrainingStatus's count a
	// click would train (the held Shift batch so far plus one more batch while Shift is down,
	// in every selected structure that trains it, cut to the entity limit) under the portrait;
	// the tooltip's Cost line for that many (getEntityCostComponentsTooltipString: resources
	// and population times the count, time ceil(time x GetBatchTime)) and its
	// formatBatchTrainingString line; requirements unmet, the entity limit reached or the
	// trainer upgrading grey and disable it ("Cannot train while upgrading."); resources
	// short for that count mask it red, and getNeededResourcesTooltip follows whatever else
	// is wrong.
	void refresh_roster_state(const rtso::Simulation::Building *b) {
		using namespace godot;
		rtso::Simulation *sim = host_->ui_sim();
		const uint8_t pid = local_player();
		const auto &ps = sim->player(pid);
		const int size = std::max(1, host_->ui_batch_size());
		const bool shift = rtso::hotkeys::held("session.batchtrain");
		// selection_panels.js Training: "Cannot train while upgrading." once every selected
		// structure is upgrading.
		bool upgrading = b && b->upgrade_to != rtso::BLD_TYPE_COUNT;
		if (upgrading)
			for (uint32_t id : host_->ui_selected_structures())
				for (const auto &o : sim->buildings())
					if (o.id == id) { upgrading = upgrading && o.upgrade_to != rtso::BLD_TYPE_COUNT; break; }
		const uint16_t trainer_tmpl = b ? 0 : sim->unit_template(trainer_unit_id_);
		for (int idx = 0; idx < roster_list_->get_item_count() && idx < (int)roster_state_.size(); ++idx) {
			const int meta = (int)roster_list_->get_item_metadata(idx);
			const bool animal = meta & ROSTER_ANIMAL;
			if (meta & ROSTER_TECH || (!animal && meta >= rtso::CIV_UNIT_TEMPLATE_COUNT)) continue;
			// A corral's animal: ROSTER_ANIMAL | FaunaType, trained one Shift batch at a time.
			const uint16_t tmpl = animal ? (uint16_t)(meta & 0xFF) : (uint16_t)meta;
			const uint8_t cat = animal ? (uint8_t)rtso::LIMIT_ANIMAL : rtso::UNIT_LIMITS[tmpl].category;
			const int limit = cat == rtso::LIMIT_NONE || cat >= rtso::LIMIT_CATEGORY_COUNT ? -1 : sim->entity_limit(pid, cat);
			const int allowed = limit < 0 ? -1 : std::max(0, limit - sim->entity_limit_count(pid, cat));
			const int buildings = b && !animal ? std::max(1, host_->ui_trainer_count(tmpl, b->id)) : 1;
			const int pending = animal ? 0 : host_->ui_pending_batch(tmpl);
			int next = pending;
			if (shift && (allowed < 0 || allowed > pending * buildings)) next += size;
			next = std::max(next, 1);
			int full = buildings, rest = 0;
			if (allowed >= 0 && allowed < next * buildings) {
				full = allowed / next;
				rest = allowed % next;
			}
			const int num = full * next + rest;
			const int batch = full > 0 ? next : rest;
			float cost[rtso::RES_COUNT] = {};
			int pop = 0;
			float seconds = 0.0f;
			if (animal) {
				const rtso::FaunaTrainCost &c = rtso::fauna_train_cost((uint8_t)tmpl); // food, wood, stone, metal
				cost[rtso::RES_FOOD] = c.res[0];
				cost[rtso::RES_WOOD] = c.res[1];
				cost[rtso::RES_STONE] = c.res[2];
				cost[rtso::RES_GOLD] = c.res[3];
				seconds = sim->livestock_train_time(pid, (uint8_t)tmpl);
			} else {
				const rtso::CivUnitTemplate &u = rtso::civ_unit_template(tmpl);
				sim->unit_train_cost(pid, tmpl, rtso::civ_template_unit_info(tmpl, u.unit_type), cost);
				pop = u.pop_cost;
				seconds = sim->unit_train_seconds(pid, tmpl);
			}
			const float batch_mult = b ? sim->batch_time_multiplier(pid, b->type, batch, b->civ) : sim->unit_trainer_batch_mult(trainer_tmpl, tmpl, batch);
			const rtso::CostTip total{(int64_t)std::floor(cost[rtso::RES_FOOD] * num), (int64_t)std::floor(cost[rtso::RES_WOOD] * num),
					(int64_t)std::floor(cost[rtso::RES_STONE] * num), (int64_t)std::floor(cost[rtso::RES_GOLD] * num),
					(int64_t)pop * num, (int64_t)std::ceil(seconds * batch_mult)};
			const int64_t costs[4] = {total.food, total.wood, total.stone, total.metal};
			const int res_of[4] = {rtso::RES_FOOD, rtso::RES_WOOD, rtso::RES_STONE, rtso::RES_GOLD};
			String sig = String::num_int64(full) + "," + String::num_int64(next) + "," + String::num_int64(rest) + "," +
					String::num_int64(total.time);
			bool short_of = false;
			for (int r = 0; r < 4; ++r) {
				const int64_t need = costs[r] > ps.res[res_of[r]] ? (int64_t)std::ceil((double)costs[r] - ps.res[res_of[r]]) : 0;
				short_of |= need > 0;
				sig += "," + String::num_int64(need);
			}
			const bool grey = roster_req_disabled_[idx] || allowed == 0 || upgrading;
			const bool red = !grey && short_of;
			sig += String(grey ? "G" : "") + (upgrading ? "U" : "");
			if (sig == roster_state_[idx]) continue;
			roster_state_[idx] = sig;
			roster_list_->set_item_text(idx, num > 1 ? String::num_int64(num) : String());
			roster_list_->set_item_icon_modulate(idx, grey ? Color(0.45f, 0.45f, 0.45f) : red ? Color(1.0f, 0.35f, 0.35f) : Color(1, 1, 1));
			roster_list_->set_item_disabled(idx, grey);
			auto slot = [](const String &line) { return line.is_empty() ? String() : "\n" + line; };
			String tip = roster_base_tips_[idx]
								 .replace("\n" + String(COST_SLOT), slot(rtso::cost_tooltip(total, true)))
								 .replace("\n" + String(BATCH_SLOT), slot(rtso::batch_training_string(full, next, rest)));
			if (short_of) tip += "\n" + needed_text(total);
			if (upgrading) tip += "\n" + rtso::objection_font(rtso::tr0("Cannot train while upgrading."));
			roster_list_->set_item_tooltip(idx, tip);
		}
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
		const int idx = add_roster_icon(label, rtso::portraits::unit(i, u.unit_type), (int)i);
		// 0.28 Training panel: its hotkey and names, cost (population and time too), health
		// and attack, then the batch line; callers add requirements.
		const std::string hotkey = rtso::hotkeys::table().label("session.queueunit." + std::to_string(idx + 1));
		tip = (hotkey.empty() ? String() : rtso::hotkey_tag(String(hotkey.c_str())) + " ") +
				rtso::entity_names_formatted(String::utf8(u.name), String::utf8(u.specific_name ? u.specific_name : ""));
		auto add_line = [&tip](const String &line) { if (!line.is_empty()) tip += "\n" + line; };
		const Dictionary text = unit_template_text(u.id);
		add_line(classes_tooltip(text));
		add_line(rtso::seldet::unit_auras_tooltip((uint16_t)i));
		add_line(identity_tooltip(text));
		tip += "\n" + String(COST_SLOT); // the Cost line for the count a click trains (refresh_roster_state)
		// formatLimitString, formatMatchLimitString (heroes: MatchLimit 1).
		if (i < rtso::CIV_UNIT_TEMPLATE_COUNT) {
			add_line(limit_tooltip(pid, rtso::UNIT_LIMITS[i].category));
			if (rtso::UNIT_LIMITS[i].match_limit == 1)
				add_line(host_->ui_sim()->match_limit_allows(pid, i) ? rtso::tr0("Can be trained only once.")
						: rtso::objection_font(rtso::tr0("Could only be trained once.")));
		}
		// Options' Detailed tooltips (showdetailedtooltips, off by default): health, attack,
		// healer, resistance, garrison, turrets, projectiles, speed and dropsite, from the
		// Template Viewer's lines (template_viewer.gd detailed_tooltip), with the player's
		// technologies and bonuses applied (0.28 GetTemplateData).
		if (rtso::user_config::get_bool("showdetailedtooltips")) {
			const rtso::Simulation::TemplateStatMods m = host_->ui_sim()->template_stat_mods(pid, i);
			Dictionary mods;
			mods["health"] = m.health;
			mods["speed"] = m.speed;
			mods["range_mul"] = m.range_mul;
			mods["range_add"] = m.range_add;
			Array armor, atk_mul, atk_add;
			for (int k = 0; k < 3; ++k) armor.push_back(m.armor[k]);
			for (int k = 0; k < 4; ++k) { atk_mul.push_back(m.atk_mul[k]); atk_add.push_back(m.atk_add[k]); }
			mods["armor"] = armor;
			mods["atk_mul"] = atk_mul;
			mods["atk_add"] = atk_add;
			const String key = String::num_int64(i) + JSON::stringify(mods); // a research changes the lines
			auto it = detailed_tips_.find(key);
			if (it == detailed_tips_.end()) {
				Ref<Script> viewer = ResourceLoader::get_singleton()->load("res://scenes/reference/template_viewer.gd");
				String lines = viewer.is_valid() ? (String)viewer->call("detailed_tooltip", (int)i, mods) : String();
				it = detailed_tips_.emplace(key, lines).first;
			}
			if (!it->second.is_empty()) tip += "\n" + it->second;
		}
		add_line(right_click_tooltip());
		tip += "\n" + String(BATCH_SLOT); // formatBatchTrainingString, live too
		return idx;
	}

	// 0 A.D.'s "Requires %(technology)s" line with the requirement's translated name.
	godot::String requires_text(const godot::String &what) const {
		using namespace godot;
		return rtso::objection_font(rtso::sprintf0(owner_->tr("Requires %(technology)s"), {{"technology", owner_->tr(what)}}));
	}

	// getEntityCostTooltip for a wallset (structures/{civ}/wallset_*): getWallPieceTooltip's
	// "Walls:  [stone] 14 to [stone] 36  [time] 4 to [time] 10" over the short, medium and long
	// pieces, then "Towers:  [stone] 45  [time] 15".
	godot::String wallset_cost_tooltip(const rtso::WallSetInfo &ws) const {
		using namespace godot;
		rtso::Simulation *sim = host_->ui_sim();
		const uint8_t pid = local_player();
		static const struct { int res; const char *icon; } kRes[] = {
				{rtso::RES_FOOD, "food"}, {rtso::RES_WOOD, "wood"}, {rtso::RES_STONE, "stone"}, {rtso::RES_GOLD, "metal"}};
		int64_t lo[5], hi[5];
		for (int k = 0; k < 5; ++k) lo[k] = INT64_MAX, hi[k] = 0;
		bool has[5] = {}, same = true;
		for (int piece = 2; piece >= 0; --piece) { // short, medium, long
			float price[rtso::RES_COUNT];
			const int64_t t = (int64_t)std::ceil(sim->wall_piece_price(pid, ws.segment, 0.5f * ws.lengths[piece], price));
			for (int k = 0; k < 5; ++k) {
				const int64_t v = k < 4 ? (int64_t)price[kRes[k].res] : t;
				if (piece == 2) has[k] = v > 0;
				else if ((v > 0) != has[k]) same = false;
				lo[k] = std::min(lo[k], v);
				hi[k] = std::max(hi[k], v);
			}
		}
		String walls;
		for (int k = 0; k < 5 && same; ++k) {
			if (!has[k]) continue;
			const String icon = rtso::resource_icon(k < 4 ? kRes[k].icon : "time");
			if (!walls.is_empty()) walls += "  ";
			walls += rtso::sprintf0(rtso::tr0("%(resourceIcon)s %(minimum)s to %(resourceIcon)s %(maximum)s"),
					{{"resourceIcon", icon}, {"minimum", String::num_int64(lo[k])}, {"maximum", String::num_int64(hi[k])}});
		}
		float tower[rtso::RES_COUNT];
		const int64_t tower_time = (int64_t)std::ceil(sim->wall_piece_price(pid, ws.tower, ws.tower_half, tower));
		const rtso::CostTip tower_cost{(int64_t)tower[rtso::RES_FOOD], (int64_t)tower[rtso::RES_WOOD], (int64_t)tower[rtso::RES_STONE],
				(int64_t)tower[rtso::RES_GOLD], 0, tower_time};
		return rtso::sprintf0(rtso::tr0("Walls:  %(costs)s"), {{"costs", walls}}) + "\n" +
				rtso::sprintf0(rtso::tr0("Towers:  %(costs)s"), {{"costs", rtso::cost_components(tower_cost, "  ", true)}});
	}

	// getNeededResourcesTooltip for the local player's stock, called once the sim
	// refused the cost; a cost multiplier the shown cost leaves out (a researcher's
	// TechCostMultiplier) still gets the label.
	godot::String needed_text(const rtso::CostTip &cost) const {
		using namespace godot;
		rtso::Simulation *sim = host_->ui_sim();
		const auto &ps = sim->player(local_player());
		const String need = rtso::needed_resources_tooltip(cost, ps.res[rtso::RES_FOOD], ps.res[rtso::RES_WOOD],
				ps.res[rtso::RES_STONE], ps.res[rtso::RES_GOLD], true);
		return need.is_empty() ? rtso::objection_font(rtso::tr0("Insufficient resources:").trim_suffix(":")) : need;
	}

	// "unload-all", or for a wall, gate or outpost (turret points only) "Unload Turrets" as
	// 0.28's unload-all-turrets shows it; the units are its garrison here either way.
	void add_unload_all(bool turret_only, int count) {
		if (turret_only)
			add_entity_command("unload-all", "Unload Turrets", "session.unloadturrets",
					"Unload Turrets\nOrder all units to leave the selected turret points.", "garrison-out.png", true, count);
		else
			add_entity_command("unload-all", "Unload All", "session.unload",
					"Unload All\nOrder all units to leave the selected entities.", "garrison-out.png", true, count);
	}

	// unit_actions.js "delete": Self-Destruct, its body and the colorized noconfirmation line.
	static godot::String self_destruct_tooltip() {
		using rtso::tr0;
		return tr0("Self-Destruct") + "\n" + tr0("Destroy the selected entities.") + "\n" +
				rtso::sprintf0(tr0("Use %(hotkey)s to avoid the confirmation dialog."),
						{{"hotkey", rtso::hotkey_tag(rtso::hotkeys::table().label("session.noconfirmation").c_str())}});
	}

	// One g_EntityCommands button (unit_actions.js): the hotkey and title on the
	// tooltip's first line, the body below, the icon, and a count where it has one.
	void add_entity_command(const char *name, const char *label, const char *hotkey, const godot::String &tip,
			const char *icon, bool enabled = true, int count = 0) {
		using namespace godot;
		// selection_panels.js Command getMaxNumberOfItems: 6, in g_EntityCommands order.
		if (commands_row_->get_child_count() - commands_base_ >= 6) return;
		Button *btn = memnew(Button);
		const String text = count > 0 ? String(label) + " (" + String::num_int64(count) + ")" : String(label);
		btn->set_meta("label", text);
		const std::string key = (hotkey && *hotkey) ? rtso::hotkeys::table().label(hotkey) : std::string();
		// A "Title\nBody" literal is two msgids (translate(title) + "\n" + translate(body)).
		String text_tip = tip;
		const int nl = tip.find("\n");
		if (nl < 0)
			text_tip = rtso::tr0(tip);
		else if (tip.find("\n", nl + 1) < 0)
			text_tip = rtso::tr0(tip.substr(0, nl)) + "\n" + rtso::tr0(tip.substr(nl + 1));
		btn->set_tooltip_text(key.empty() ? text_tip : rtso::hotkey_tag(String(key.c_str())) + " " + text_tip);
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
		btn->set_meta("tooltip_style", "session_bottom_bold"); // g_EntityCommands
		btn->connect("pressed", Callable(owner_, "_on_entity_command").bind(String(name)));
		commands_row_->add_child(btn);
	}

	// Everything that makes a command button look or act differently: its text,
	// tooltip, enabled state, icon, size, overrides and what pressed calls.
	// The pair_*.json "top" technology of its pair.
	static bool tech_pair_top(uint8_t tech) {
		return tech == rtso::TECH_GATHER_WICKER_BASKETS_MAUR || tech == rtso::TECH_TRADITIONAL_ARMY_SELE ||
				tech == rtso::TECH_CIVIL_ENGINEERING_01 || tech == rtso::TECH_CIVIL_SERVICE_01 || tech == rtso::TECH_PHARAONIC_CULT;
	}

	// The "grayscale:" sprite modifier for a disabled icon (held by the view, not a
	// static, so it goes before the engine shuts down).
	godot::Ref<godot::ShaderMaterial> grayscale_material() {
		using namespace godot;
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

	static godot::String button_signature(godot::Node *n) {
		using namespace godot;
		Button *b = Object::cast_to<Button>(n);
		if (!b) {
			// A research pair: its buttons' signatures.
			String s = n ? n->get_class() : String();
			if (n)
				for (int i = 0; i < n->get_child_count(); ++i)
					if (Object::cast_to<Container>(n->get_child(i)) || Object::cast_to<Button>(n->get_child(i)))
						s += String("\x1e") + button_signature(n->get_child(i));
			return s;
		}
		String s = b->get_text() + String("\x1f") + b->get_tooltip_text() + (b->is_disabled() ? "\x1f" "D" : "\x1f" "E");
		if (b->has_meta("progress")) s += String("\x1fg") + String::num_int64((int64_t)b->get_meta("progress"));
		// Child overlays (the corners frame, a red mask, a greyed icon) change with state.
		for (int i = 0; i < b->get_child_count(); ++i) {
			Node *c = b->get_child(i);
			if (Object::cast_to<Container>(c) || Object::cast_to<Button>(c)) continue;
			const String name = c->get_name();
			s += String("\x1fo") + (name.contains("@") ? c->get_class() : name); // auto names differ per frame
			if (CanvasItem *ci = Object::cast_to<CanvasItem>(c)) s += ci->get_material().is_valid() ? "G" : "";
		}
		Ref<Texture2D> ico = b->get_button_icon();
		if (ico.is_valid()) s += String("\x1fi") + (ico->get_path().is_empty() ? String::num_int64(ico->get_instance_id()) : ico->get_path());
		s += String("\x1fm") + String(Variant(b->get_custom_minimum_size()));
		if (b->has_theme_color_override("font_color")) s += String("\x1f" "c") + String(Variant(b->get_theme_color("font_color")));
		if (b->has_theme_font_size_override("font_size")) s += String("\x1f" "f") + String::num_int64(b->get_theme_font_size("font_size"));
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
	static void disable_buttons(godot::Node *n) {
		if (!n) return;
		if (godot::BaseButton *b = godot::Object::cast_to<godot::BaseButton>(n)) b->set_disabled(true);
		for (int i = 0; i < n->get_child_count(); ++i) disable_buttons(n->get_child(i));
	}
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
		// A fresh set built beside a named old one got Godot's unique name; it gets its own back.
		if (!same)
			for (int i = 0; i < bar->get_child_count(); ++i)
				if (Node *n = bar->get_child(i); n->has_meta("node_name")) n->set_name(String(n->get_meta("node_name")));
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

	// data/reference/templates.json (tools/extract_0ad_reference.py), read once.
	static const godot::Dictionary &reference_templates() {
		using namespace godot;
		static Dictionary all;
		static bool loaded = false;
		if (!loaded) {
			loaded = true;
			Ref<FileAccess> f = FileAccess::open("res://data/reference/templates.json", FileAccess::READ);
			if (f.is_valid()) all = Dictionary(JSON::parse_string(f->get_as_text()));
		}
		return all;
	}

	// 0.28's Identity text for a civ's structure (generic_name, specific_name, tooltip,
	// classes, viewer) from templates.json.
	static godot::Dictionary structure_template_text(uint8_t civ, int type) {
		using namespace godot;
		if (civ >= rtso::CIV_COUNT) return Dictionary();
		const Dictionary structures = reference_templates().get("structures", Dictionary());
		const Dictionary by_civ = structures.get(String(rtso::civ_info(civ).code), Dictionary());
		return by_civ.get(String::num_int64(type), Dictionary());
	}

	// The same for a roster unit ("athen/infantry_spearman_b").
	static godot::Dictionary unit_template_text(const char *id) {
		using namespace godot;
		const Dictionary units = reference_templates().get("units", Dictionary());
		return units.get(String::utf8(id), Dictionary());
	}

	// getVisibleEntityClassesFormatted: "Classes: Infantry, Melee", or "".
	static godot::String classes_tooltip(const godot::Dictionary &text) {
		using namespace godot;
		const Array classes = text.get("classes", Array());
		if (classes.is_empty()) return String();
		String list;
		for (int k = 0; k < classes.size(); ++k) {
			if (k) list += rtso::tr0(", ");
			list += rtso::tr0(String(classes[k]));
		}
		return rtso::tr0("Classes:") + " " + list;
	}

	// getEntityTooltip: the template's Identity/Tooltip.
	static godot::String identity_tooltip(const godot::Dictionary &text) {
		return text.get("tooltip", godot::String());
	}

	// getAurasTooltip: "Name: description Range: 30 meters" per aura of a roster unit.
	// getAurasTooltip's line for one aura: "Name: description", then its range.
	static godot::String aura_line(const godot::String &name, const godot::String &description, float radius) {
		using namespace godot;
		String line = rtso::sprintf0(rtso::tr0("%(auralabel)s %(aurainfo)s"),
				{{"auralabel", rtso::sprintf0(rtso::tr0("%(auraname)s:"), {{"auraname", rtso::tr0(name)}})},
				 {"aurainfo", rtso::tr0(description)}});
		if (radius > 0.0f) {
			const int64_t r = (int64_t)std::lround(radius);
			line += " " + rtso::sprintf0(rtso::tr0("%(label)s %(val)s %(unit)s"),
					{{"label", rtso::tr0("Range:")}, {"val", String::num_int64(r)}, {"unit", rtso::tr0(r == 1 ? "meter" : "meters")}});
		}
		return line;
	}

	// The same for a structure, from its 0.28 template's Auras (templates.json).
	static godot::String structure_auras_tooltip(const godot::Dictionary &text) {
		using namespace godot;
		const Array auras = text.get("auras", Array());
		String out;
		for (int k = 0; k < auras.size(); ++k) {
			const Dictionary a = auras[k];
			out += (out.is_empty() ? String() : String("\n")) +
					aura_line(String(a.get("name", "")), String(a.get("description", "")), (float)a.get("radius", 0.0f));
		}
		return out;
	}

	// getResourceDropsiteTooltip: "Dropsite for: Food Wood", a resource's name for its icon.
	static godot::String dropsite_tooltip(const godot::Dictionary &text) {
		using namespace godot;
		const Array types = Dictionary(text.get("viewer", Dictionary())).get("dropsite", Array());
		if (types.is_empty()) return String();
		String icons;
		for (int k = 0; k < types.size(); ++k) {
			if (k) icons += "  ";
			icons += rtso::tr0(String(types[k]).capitalize());
		}
		return rtso::sprintf0(rtso::tr0("%(label)s %(icons)s"), {{"label", rtso::tr0("Dropsite for:")}, {"icons", icons}});
	}

	// getTurretsTooltip: "Turret Positions: 4".
	static godot::String turrets_tooltip(const godot::Dictionary &text) {
		using namespace godot;
		const int turrets = (int)Dictionary(text.get("viewer", Dictionary())).get("turrets", 0);
		if (turrets <= 0) return String();
		return rtso::sprintf0(rtso::tr0("%(label)s: %(turretsLimit)s"),
				{{"label", rtso::tr0("Turret Positions")}, {"turretsLimit", String::num_int64(turrets)}});
	}

	// formatLimitString: "Current Count: 1, Limit: 1." and each LimitChanger the player owns.
	godot::String limit_tooltip(uint8_t pid, uint8_t category) const {
		using namespace godot;
		rtso::Simulation *sim = host_->ui_sim();
		if (category == rtso::LIMIT_NONE || category >= rtso::LIMIT_CATEGORY_COUNT) return String();
		const int limit = sim->entity_limit(pid, category);
		if (limit < 0) return String();
		const int count = sim->entity_limit_count(pid, category);
		String text = rtso::sprintf0(rtso::tr0("Current Count: %(count)s, Limit: %(limit)s."),
				{{"count", String::num_int64(count)}, {"limit", String::num_int64(limit)}});
		if (count >= limit) text = rtso::objection_font(text);
		const uint8_t civ = sim->player_civ(pid);
		for (const rtso::LimitChanger &c : rtso::LIMIT_CHANGERS) {
			if (c.civ != civ || c.category != category || c.bld >= rtso::BLD_TYPE_COUNT || c.value == 0) continue;
			int owned = 0;
			for (const auto &b : sim->buildings())
				owned += b.team == pid && b.type == c.bld && b.hp > 0.0f && !b.wrecked && b.build_progress >= 1.0f;
			if (!owned) continue;
			const int change = owned * c.value;
			text += "\n" + rtso::sprintf0(rtso::tr0(change > 0 ? "%(changer)s enlarges the limit with %(change)s."
					: "%(changer)s lessens the limit with %(change)s."),
					{{"changer", rtso::tr0(String::utf8(rtso::building_type_info(c.bld).name))}, {"change", String::num_int64(change)}});
		}
		return text;
	}

	// getTemplateViewerOnRightClickTooltip; the button's right click opens the Template Viewer.
	static godot::String right_click_tooltip() { return rtso::tr0("Right-click for more information."); }
	// 0.28's Barter panel (g_SelectionPanels.Barter, BarterButtonManager, BarterButton) in
	// the selection details' left panel, on a selected market of ours: per resource a sell
	// button (picks what to sell, the picked one grey with the corners) over a buy button
	// (sells 100, 500 with session.massbarter, for the quoted amount), red where the stock
	// falls short. Rebuilt only when what it shows changes, so a click is never lost.
	void update_barter_panel() {
		using namespace godot;
		if (!barter_host_) return;
		rtso::Simulation *sim = host_->ui_sim();
		const uint32_t bid = host_->ui_selected_building_id();
		const rtso::Simulation::Building *market = nullptr;
		if (sim && bid != 0)
			for (const auto &b : sim->buildings())
				if (b.id == bid) { market = &b; break; }
		const bool show = market && market->team == local_player() && rtso::is_land_market(market->type) &&
				market->build_progress >= 1.0f && sim->can_barter(local_player());
		if (barter_host_->is_visible() != show) barter_host_->set_visible(show);
		if (!show) return;
		const auto &ps = sim->player(local_player());
		static const uint8_t kOrder[] = {rtso::RES_FOOD, rtso::RES_WOOD, rtso::RES_STONE, rtso::RES_GOLD};
		static const char *kNames[] = {"food", "wood", "stone", "metal"};
		const int amount = 100 * (rtso::hotkeys::held("session.massbarter") ? 5 : 1);
		int quote[4];
		bool short_of[4];
		String sig = String::num_int64(barter_sell_) + ":" + String::num_int64(amount);
		for (int i = 0; i < 4; ++i) {
			quote[i] = sim->barter_quote((rtso::ResourceType)barter_sell_, (rtso::ResourceType)kOrder[i], amount);
			short_of[i] = ps.res[kOrder[i]] < (float)amount;
			sig += String(",") + String::num_int64(quote[i]) + (short_of[i] ? "R" : "");
		}
		if (sig == barter_sig_ && barter_host_->get_child_count() > 0) return;
		barter_sig_ = sig;
		while (barter_host_->get_child_count() > 0) {
			Node *c = barter_host_->get_child(0);
			barter_host_->remove_child(c);
			c->queue_free();
		}
		auto make = [&](const char *name, const String &caption, int i, bool selected, bool red) {
			Button *b = memnew(Button);
			b->set_name(name);
			b->set_custom_minimum_size(Vector2(38, 38));
			b->set_focus_mode(Control::FOCUS_NONE);
			TextureRect *icon = memnew(TextureRect);
			icon->set_position(Vector2(3, 3));
			icon->set_size(Vector2(32, 32));
			icon->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
			icon->set_stretch_mode(TextureRect::STRETCH_SCALE);
			icon->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
			icon->set_texture(rtso::load_ui_texture(String("res://assets/textures/ui/icons/") + kNames[i] + ".png"));
			if (selected) { // SelectedModifier "color:0 0 0 100:grayscale:"
				icon->set_material(grayscale_material());
				icon->set_self_modulate(Color(0.6f, 0.6f, 0.6f));
			}
			b->add_child(icon);
			if (red) { // DisabledModifier "color:255 0 0 80:"
				ColorRect *mask = memnew(ColorRect);
				mask->set_position(Vector2(3, 3));
				mask->set_size(Vector2(32, 32));
				mask->set_color(Color(1.0f, 0.0f, 0.0f, 80.0f / 255.0f));
				mask->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
				b->add_child(mask);
			}
			Label *cap = memnew(Label); // resourceText, the top half
			cap->set_name("caption");
			cap->set_text(caption);
			cap->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
			cap->set_size(Vector2(38, 19));
			cap->add_theme_font_size_override("font_size", 11);
			cap->add_theme_color_override("font_outline_color", Color(0, 0, 0));
			cap->add_theme_constant_override("outline_size", 3);
			cap->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
			b->add_child(cap);
			return b;
		};
		int sell_i = 0;
		for (int i = 0; i < 4; ++i)
			if (kOrder[i] == barter_sell_) sell_i = i;
		HBoxContainer *row = memnew(HBoxContainer);
		row->add_theme_constant_override("separation", 2);
		for (int i = 0; i < 4; ++i) {
			const bool selected = i == sell_i;
			VBoxContainer *col = memnew(VBoxContainer);
			col->set_name(String("barter_") + kNames[i]);
			col->add_theme_constant_override("separation", 4);
			const String res_name = owner_->tr(kNames[i]);
			Button *sell = make("sell", rtso::sprintf0(rtso::tr0("-%(amount)s"), {{"amount", String::num_int64(amount)}}), i,
					selected, short_of[i]);
			sell->set_tooltip_text(rtso::sprintf0(rtso::tr0("Sell %(resource)s"), {{"resource", res_name}}));
			sell->connect("pressed", Callable(owner_, "_on_barter_sell_selected").bind((int)kOrder[i]));
			if (selected) {
				TextureRect *corners = memnew(TextureRect);
				corners->set_name("corners");
				corners->set_texture(rtso::load_ui_texture("res://assets/textures/ui/archive/icons/corners.png"));
				corners->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
				corners->set_position(Vector2(3, 3));
				corners->set_size(Vector2(32, 32));
				corners->set_stretch_mode(TextureRect::STRETCH_SCALE);
				corners->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
				sell->add_child(corners);
			}
			col->add_child(sell);
			if (!selected) { // the buy button is hidden under the resource being sold
				Button *buy = make("buy", rtso::sprintf0(rtso::tr0("+%(amount)s"), {{"amount", String::num_int64(quote[i])}}), i,
						false, short_of[sell_i]);
				buy->set_tooltip_text(rtso::sprintf0(rtso::tr0("Buy %(resource)s"), {{"resource", res_name}}));
				buy->connect("pressed", Callable(owner_, "_on_barter_requested").bind(barter_sell_, (int)kOrder[i], amount));
				col->add_child(buy);
			}
			row->add_child(col);
		}
		barter_host_->add_child(row);
	}

	// guiSelection's "stretched:session/icons/corners.png" over a panel button.
	static void add_corners(godot::Button *btn) {
		using namespace godot;
		TextureRect *corners = memnew(TextureRect);
		corners->set_name("corners");
		corners->set_texture(rtso::load_ui_texture("res://assets/textures/ui/archive/icons/corners.png"));
		corners->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
		corners->set_stretch_mode(TextureRect::STRETCH_SCALE);
		corners->set_position(Vector2(3, 3));
		corners->set_size(Vector2(32, 32));
		corners->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		btn->add_child(corners);
	}

	// A 0.28 panel button: 38x38, its icon a child TextureRect (so "grayscale:" greys only
	// the icon), its name in the "label" meta.
	godot::Button *icon_button(const godot::String &label, const godot::Ref<godot::Texture2D> &tex) {
		using namespace godot;
		Button *btn = memnew(Button);
		btn->set_meta("label", label);
		btn->set_custom_minimum_size(Vector2(38, 38));
		btn->set_focus_mode(Control::FOCUS_NONE);
		TextureRect *icon = memnew(TextureRect);
		icon->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
		icon->set_stretch_mode(TextureRect::STRETCH_SCALE);
		icon->set_position(Vector2(3, 3));
		icon->set_size(Vector2(32, 32));
		icon->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		icon->set_texture(tex);
		btn->add_child(icon);
		return btn;
	}

	void on_right_click_details(godot::Control *c, const char *kind, int id, int civ) {
		using namespace godot;
		c->connect("gui_input", Callable(owner_, "_on_template_details_input").bind(String(kind), id, civ));
	}

	godot::Object *owner_ = nullptr;
	RtsoCommandPanelHost *host_ = nullptr;
	godot::Panel *panel_ = nullptr;
	godot::HBoxContainer *stances_bar_ = nullptr;
	godot::Button *stance_buttons_[5] = {}; // indexed by UnitStance
	godot::HFlowContainer *actions_bar_ = nullptr;
	// Wraps its buttons; the scroll keeps a long build menu inside the panel.
	godot::ScrollContainer *production_scroll_ = nullptr;
	godot::HFlowContainer *production_bar_ = nullptr;
	godot::HBoxContainer *queue_bar_ = nullptr;
	godot::Panel *left_panel_ = nullptr;
	godot::HBoxContainer *commands_row_ = nullptr;
	int commands_base_ = 0; // the previous frame's command buttons, before this frame's
	// session.xml's panel sizes.
	static constexpr float kPanelH = 166.0f, kRightW = 402.0f, kLeftW = 206.0f;
	static constexpr float kGridW = kRightW - 10.0f, kTrainH = 78.0f;
	// Every civ roster unit and hero the selected building trains (0 A.D. names,
	// costs after team bonuses), above the command panel. Rebuilt only when
	// roster_list_key_ changes so hover and tooltips survive between frames.
	godot::ItemList *roster_list_ = nullptr;
	uint64_t roster_list_key_ = ~0ull;
	std::vector<godot::String> roster_labels_;    // each cell's "Name (cost)"
	std::vector<godot::String> roster_base_tips_; // each cell's tooltip before the live lines
	std::vector<godot::String> roster_state_;     // the live state last applied (refresh_roster_state)
	std::vector<bool> roster_req_disabled_;       // disabled by its requirements at the rebuild
	int roster_list_batch_ = 0; // the batch size the tooltips' batch line was built for
	std::map<godot::String, godot::String> detailed_tips_; // template + mods -> detailed_tooltip()
	uint32_t roster_list_gen_ = 0; // user_config::generation() it was built at
	uint32_t trainer_unit_id_ = 0;
	int barter_sell_ = rtso::RES_FOOD; // BarterButtonManager.selectedResource
	godot::Control *barter_host_ = nullptr; // in the selection details (0.28's left panel)
	godot::String barter_sig_;
	RtsoProductionQueue production_queue_;
	godot::Ref<godot::ShaderMaterial> grayscale_;

	uint8_t panel_player_ = 0xFF; // an observer's panels: the selection's owner
	bool read_only_ = false;        // and nothing on them can be pressed
	uint8_t local_player() const { return panel_player_ != 0xFF ? panel_player_ : host_->local_player(); }
};

#endif // RTSO_COMMAND_QUEUE_VIEW_H
