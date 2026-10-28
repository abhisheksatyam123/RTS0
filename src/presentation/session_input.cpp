// RtsoMain's input handling (0 A.D. gui/session/input.js and gui/session/hotkeys/):
// hotkeys, control groups, clicks, drag selection and orders. Split out of main_node.cpp
// (PLAN P4.6); the state it reads and writes is RtsoMain's.

#include "main_node.h"
#include "fog_mirage.h"
#include "l10n.h"
#include <godot_cpp/classes/color_rect.hpp>
#include <godot_cpp/classes/os.hpp>
#include <unordered_set>
#include <cstring>
#include <godot_cpp/classes/theme.hpp>
#include "popup_safe.h"
#include "simulation/petra_chat.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/script.hpp>
#include "user_config.h"
#include "display_colors.h"
#include <godot_cpp/classes/audio_stream.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/viewport_texture.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/cylinder_mesh.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include "../core/diagnostics/logger.h"
#include "../core/diagnostics/perf_tracker.h"
#include "../core/job_system.h"
#include "../graphics/assets/model_library.h"
#include <sstream>
using namespace godot;
using rtso::sprintf0;

// Drops one unit from the selection after an orderone command
// (0 A.D. gui/session/input.js popOneFromSelection -> g_Selection.removeList).
// The same on structures: a civ's own building whose 0.28 template inherits a generic one's
// SelectionGroupName joins that group on a double-click (brit/crannog and han/civil_centre_court
// with the civic centres, kush/temple_amun and the shrines with the temples, and so on).
static uint8_t structure_selection_group(uint8_t type) {
	switch (type) {
		case rtso::BLD_CRANNOG: case rtso::BLD_IMPERIAL_COURT: case rtso::BLD_CAMPAIGN_CITY:
		case rtso::BLD_CAMPAIGN_CITY_MINOR: return rtso::BLD_TOWN_CENTER;
		case rtso::BLD_TEMPLE_AMUN: case rtso::BLD_TEMPLE_VESTA: case rtso::BLD_TEMPLE_MARS:
		case rtso::BLD_TEMPLE_ISIS: case rtso::BLD_TOPHET: case rtso::BLD_KUSH_SHRINE: case rtso::BLD_SHRINE:
		case rtso::BLD_CAMPAIGN_SANCTUARY: case rtso::BLD_EPIC_TEMPLE: return rtso::BLD_TEMPLE;
		case rtso::BLD_INN: case rtso::BLD_TENT: case rtso::BLD_WAREHOUSE: case rtso::BLD_TAVERN:
		case rtso::BLD_APARTMENT: case rtso::BLD_STALL: case rtso::BLD_HUT: case rtso::BLD_LONGHOUSE:
		case rtso::BLD_PERS_HOUSE_A: case rtso::BLD_PERS_HOUSE_B: case rtso::BLD_APARTMENT_BLOCK: return rtso::BLD_HOUSE;
		case rtso::BLD_TOWER_GREAT: return rtso::BLD_TOWER;
		case rtso::BLD_FORTRESS_OLD: return rtso::BLD_FORTRESS;
		case rtso::BLD_TRADING_POST: return rtso::BLD_MARKET;
		case rtso::BLD_MILL: return rtso::BLD_FARMSTEAD;
		default: return type;
	}
}

// popOneFromSelection for "construct": the first selected unit that can build, 0 if none.
uint32_t RtsoMain::first_selected_builder() const {
	if (!sim) return 0;
	for (int64_t i = 0; i < selected_ids.size(); ++i)
		if (sim->unit_id_builds((uint32_t)selected_ids[i])) return (uint32_t)selected_ids[i];
	return 0;
}

static void pop_from_selection(PackedInt32Array &sel, uint32_t id) {
	const int64_t at = sel.find((int32_t)id);
	if (at >= 0) sel.remove_at(at);
}

// A right-click while band-boxing cancels the box and gives no order (input.js
// INPUT_BANDBOXING: right button resets to INPUT_NORMAL). handleInputBeforeGui runs it, so it
// holds over the panels too, where the box may end.
void RtsoMain::_input(const Ref<InputEvent> &event) {
	Ref<InputEventMouseButton> mb = event;
	if (mb.is_null() || !dragging || !mb->is_pressed() || mb->get_button_index() != MOUSE_BUTTON_RIGHT) return;
	if (drag_start.distance_to(mb->get_position()) <= 4.0f) return;
	dragging = false;
	double_click_pending = false;
	if (sel_rect) sel_rect->set_rect(Vector2(), Vector2(), false);
	get_viewport()->set_input_as_handled();
}

void RtsoMain::_unhandled_input(const Ref<InputEvent> &event) {
	// input.js handleInputBeforeGui / handleInputAfterGui: nothing while a cinema path plays.
	if (cinema_view_.playing()) return;
	// Keys go through named 0 A.D. hotkeys (hotkeys.h, config/default.cfg
	// [hotkey.*]; user overrides in user://config/user.cfg). Same-specificity
	// hotkeys can share a key (B = build menu or fifth trainee), so the context
	// below picks between them, like 0 A.D.'s gui/session/hotkeys/*.xml.
	Ref<InputEventKey> key = event;
	std::vector<std::string> hk = rtso::hotkeys::fired(key, true);
	if (key.is_valid() && key->is_echo()) {
		// A held key repeats only the 0.28 actions bound on="KeyDown" (hotkeys/misc.xml,
		// training.xml) and placement rotation (input.js "hotkeydown"); Delete is left out
		// so a held key can't stack confirmations.
		static const char *const kRepeats[] = {"selection.cancel", "selection.idlebuilder", "selection.idlewarrior",
			"selection.idleunit", "session.backtowork", "session.leaveturret", "session.stop", "session.unload",
			"session.unloadturrets", "session.queueunit.1", "session.queueunit.2", "session.queueunit.3",
			"session.queueunit.4", "session.queueunit.5", "session.queueunit.6", "session.queueunit.7",
			"session.queueunit.autoqueueon", "session.queueunit.autoqueueoff", "session.rotate.cw", "session.rotate.ccw"};
		std::vector<std::string> kept;
		for (const std::string &n : hk)
			for (const char *r : kRepeats)
				if (n == r) { kept.push_back(n); break; }
		hk.swap(kept);
	}
	if (!hk.empty()) {
		auto fired = [&](const char *name) { return rtso::hotkeys::has(hk, name); };
		auto ids_ptr = [&]() { return (const uint32_t *)selected_ids.ptr(); };

		if (fired("cancel") || fired("selection.cancel")) {
			// misc.xml: Escape is both hotkeys and both act on one press: cancel runs
			// closeOpenDialogs(), selection.cancel clearSelection() (input.js:1739), which
			// ends placement or else empties the selection, and drops the armed action.
			if (fired("cancel")) close_open_dialogs();
			if (fired("selection.cancel")) {
				if (build_mode) enter_build_mode(false);
				else {
					clear_selection();
					inspector_.clear(); // g_Selection.reset() drops an inspected entity too
				}
				attack_move_mode = patrol_mode = guard_mode = call_to_arms_mode = garrison_mode = turret_mode = repair_mode = false;
			}
			return;
		}

		// While placing a building, letters pick the building to place
		// (RTS0 [hotkey.rts0.build], unbound by default as 0.28 has no such keys).
		if (build_mode) {
			static const struct { const char *name; uint8_t type; } kBuildKeys[] = {
				{"rts0.build.civilcentre", rtso::BLD_TOWN_CENTER}, {"rts0.build.house", rtso::BLD_HOUSE},
				{"rts0.build.barracks", rtso::BLD_BARRACKS}, {"rts0.build.storehouse", rtso::BLD_STOREHOUSE},
				{"rts0.build.defensetower", rtso::BLD_TOWER}, {"rts0.build.farmstead", rtso::BLD_FARMSTEAD},
				{"rts0.build.market", rtso::BLD_MARKET}, {"rts0.build.fortress", rtso::BLD_FORTRESS},
				{"rts0.build.temple", rtso::BLD_TEMPLE},
				{"rts0.build.wonder", rtso::BLD_WONDER}, {"rts0.build.dock", rtso::BLD_DOCK},
				{"rts0.build.wall", rtso::BLD_WALL_SEGMENT}, {"rts0.build.corral", rtso::BLD_CORRAL},
			};
			for (const auto &bk : kBuildKeys) {
				if (fired(bk.name)) {
					enter_build_mode(true, bk.type);
					return;
				}
			}
			// Walls have no rotation (input.js rotates only INPUT_BUILDING_PLACEMENT).
			if (build_mode && !wall_drag_mode && current_build_type != rtso::BLD_WALL_SEGMENT &&
					current_build_type != rtso::BLD_PALISADE && current_build_type != rtso::BLD_SIEGE_WALL) {
			constexpr float ROTATION_STEP = 3.14159265f / 12.0f; // 24 presses make a full turn
			if (fired("session.rotate.cw")) { placement_angle += ROTATION_STEP; return; }
			if (fired("session.rotate.ccw")) { placement_angle -= ROTATION_STEP; return; }
		}

		if (fired("rts0.build.menu")) {
				enter_build_mode(false);
				return;
			}
		}

		// Chat.openPage: each chat hotkey opens the window afresh on its addressee.
		if (fired("chat")) {
			chat_open_page();
			return;
		}
		if (fired("teamchat") || fired("privatechat")) {
			if (!chat_open_page()) return;
			if (fired("teamchat")) chat_window_.select_team();
			else chat_window_.select_last_private();
			return;
		}

		// Control groups (0 A.D. input.js:852-873 and performGroup, input.js:1606):
		// N selects group N, Ctrl+N saves the selection as group N, Shift+N adds
		// group N to the selection (performGroup "add"), and pressing N twice within
		// 500 ms centres the camera on the group. With gui.session.disjointcontrolgroups
		// ("Single", the default) a unit saved to one group leaves the others
		// (EntityGroupsContainer.addEntities); "Multiple" keeps it in them.
		// input.js handles selection.group.* only in INPUT_NORMAL: not while placing, with an
		// action or flare armed, band-boxing, holding the right button or batch training.
		for (int g = 1; g <= 10 && input_normal(); ++g) {
			const int grp = g % 10;
			const std::string n = std::to_string(g);
			const bool sel = fired(("selection.group.select." + n).c_str());
			const bool save = fired(("selection.group.save." + n).c_str());
			const bool add = fired(("selection.group.add." + n).c_str());
			if (!sel && !save && !add) continue;
			if (save) {
				selected_groups[grp].clear();
				const bool disjoint = rtso::user_config::get_bool("gui.session.disjointcontrolgroups");
				// Every selected entity, structures too (EntityGroupsContainer.addEntities).
				PackedInt32Array members = selected_ids;
				for (uint32_t b : selected_structures()) members.push_back((int32_t)b);
				for (int s = 0; s < members.size(); ++s) {
					for (int o = 0; o < 10 && disjoint; ++o) {
						if (o == grp) continue;
						const int64_t at = selected_groups[o].find(members[s]);
						if (at >= 0) selected_groups[o].remove_at(at);
					}
					if (!selected_groups[grp].has(members[s])) selected_groups[grp].push_back(members[s]);
				}
				return;
			}
			if (add) {
				perform_group(grp, GROUP_ADD);
				return;
			}
			const uint64_t now_ms = Time::get_singleton()->get_ticks_msec();
			const bool snap = grp == last_group_pressed_ && now_ms - last_group_press_ms_ < 500;
			last_group_pressed_ = grp;
			last_group_press_ms_ = now_ms;
			perform_group(grp, snap ? GROUP_SNAP : GROUP_SELECT);
			return;
		}

		// Idle unit finders (0 A.D. findIdleUnit, input.js:1659, classes from
		// gui/session/hotkeys/misc.xml and MiniMapIdleWorkerButton.js).
		if (fired("selection.idleworker")) { find_idle_unit(IDLE_WORKER); return; }
		if (fired("selection.idlewarrior")) { find_idle_unit(IDLE_WARRIOR); return; }
		if (fired("selection.idleunit")) { find_idle_unit(IDLE_ANY); return; }
		if (fired("selection.idlebuilder")) { find_idle_unit(IDLE_BUILDER); return; }

		if (fired("session.gui.diplomacy.toggle")) { toggle_diplomacy(); return; }
		if (fired("session.diplomacycolors")) { toggle_diplomacy_colors(); return; }
		if (fired("session.gui.matchsettings.toggle") || fired("session.gui.objectives.toggle")) { toggle_objectives(); return; }
		if (fired("session.gui.tutorial.toggle")) { tutorial_panel_.toggle(); return; }
		if (fired("session.gui.barter.toggle")) {
			_toggle_session_dialog("BarterDialog");
			return;
		}
		// gui/session/hotkeys/training.xml: turnAutoQueueOn / turnAutoQueueOff.
		if (fired("session.queueunit.autoqueueon")) { _on_autoqueue_pressed(1); return; }
		if (fired("session.queueunit.autoqueueoff")) { _on_autoqueue_pressed(0); return; }
		if (fired("session.flareactivate")) { _on_flare_button_pressed(); return; } // MiniMap.xml flareButton hotkey
		if (fired("structree")) {
			if (Node *tree = get_node_or_null(NodePath("TechTreeDialog"))) tree->call("toggle");
			return;
		}
		if (fired("civinfo")) {
			if (Node *tree = get_node_or_null(NodePath("TechTreeDialog"))) tree->call("toggle_civinfo");
			return;
		}
		if (fired("session.gui.toggle")) {
			if (ui_layer) ui_layer->set_visible(!ui_layer->is_visible());
			return;
		}
		if (fired("summary")) {
			close_open_dialogs(); // MenuButtons Summary.onPress: closeOpenDialogs first
			toggle_summary();
			return;
		}
		if (fired("session.gui.menu.toggle")) {
			_on_menu_button_pressed();
			return;
		}
		if (fired("pause")) {
			// Menu.js initButton: the Pause entry's own hotkey closes the menu first.
			if (Node *menu = get_node_or_null(NodePath("PauseMenu"))) menu->call("_close");
			toggle_pause();
			return;
		}
		// 0 A.D. misc.xml: quicksave/quickload are disabled in networked games.
		if (fired("quicksave")) {
			quicksave();
			return;
		}
		if (fired("quickload")) {
			quickload();
			return;
		}
		if (time_warp_ && fired("session.timewarp.rewind")) { // TimeWarp.rewind
			time_warp_rewind();
			return;
		}

		// Camera (0 A.D. [hotkey.camera], gui/session/hotkeys/camera.xml).
		for (int j = 1; j <= 10; ++j) {
			const std::string n = std::to_string(j);
			if (fired(("camera.jump.set." + n).c_str())) {
				if (cam_rig) camera_bookmarks_[j - 1] = {true, cam_rig->get_target_pos()};
				return;
			}
			if (fired(("camera.jump." + n).c_str())) {
				// jumpCamera: already within gui.session.camerajump.threshold (metres) of the
				// bookmark, jump back to where the camera was before the last jump.
				if (cam_rig && camera_bookmarks_[j - 1].set) {
					const Vector3 pos = camera_bookmarks_[j - 1].pos, pivot = cam_rig->get_target_pos();
					const float threshold = (float)std::atof(rtso::user_config::get("gui.session.camerajump.threshold").c_str()) / 4.0f; // 1 unit = 4 m
					cam_rig->set_follow_id(0);
					if (camera_jump_last_.set && std::fabs(pivot.x - pos.x) < threshold && std::fabs(pivot.z - pos.z) < threshold) {
						cam_rig->set_target_pos(camera_jump_last_.pos);
					} else {
						camera_jump_last_ = {true, pivot};
						cam_rig->set_target_pos(pos);
					}
				}
				return;
			}
		}
		if (fired("camera.reset")) {
			if (cam_rig) cam_rig->reset_camera();
			return;
		}
		if (fired("camera.togglebirdseyeview")) {
			if (cam_rig) cam_rig->toggle_birds_eye();
			return;
		}
		static const char *kSpeedKinds[3] = { "scroll", "rotate", "zoom" };
		for (int s = 0; s < 3; ++s)
			for (int inc = 0; inc < 2; ++inc)
				if (fired((std::string("camera.") + kSpeedKinds[s] + (inc ? ".speed.increase" : ".speed.decrease")).c_str())) {
					if (cam_rig) cam_rig->change_speed(s, inc != 0);
					return;
				}
		if (fired("camera.follow")) { // setCameraFollow: a unit is followed, a structure looked at
			const uint32_t first = !selected_ids.is_empty() ? (uint32_t)selected_ids[0] : selected_building_id;
			if (cam_rig) cam_rig->set_follow_id(0);
			follow_focus(first, false);
			return;
		}
		if (fired("camera.rallypointfocus")) { // unit_actions.js "focus-rally"
			_on_entity_command("focus-rally");
			return;
		}
		if (fired("camera.lastattackfocus")) {
			// focusAttack: follow the attacked unit while it lives, else look at the spot.
			if (cam_rig && has_last_attack_) {
				cam_rig->set_follow_id(0);
				follow_focus(last_attack_unit_, false);
				if (cam_rig->follow_id() == 0) cam_rig->set_target_pos(last_attack_pos_);
			}
			return;
		}

		// Unit commands (0 A.D. [hotkey.session], misc.xml).
		if (fired("session.toggledefaultformation")) {
			toggle_default_formation();
			return;
		}
		if (fired("session.stop")) {
			_on_entity_command("stop");
			return;
		}
		// misc.xml: U (unload, unloadturrets and leaveturret) unloads the selected holders,
		// empties their turret points and takes selected units off theirs, all on one press.
		const bool unload = fired("session.unload"), leave_turret = fired("session.leaveturret"),
				unload_turrets = fired("session.unloadturrets");
		if (unload || leave_turret || unload_turrets) {
			if (unload) _on_ungarrison_pressed();
			if (unload_turrets) _on_entity_command("unload-all-turrets"); // unit_actions "unload-all-turrets"
			if (leave_turret) _on_entity_command("leave-turret");
			return;
		}
		// Delete (0 A.D. session.kill, unit_actions "delete"): DeleteSelectionConfirmation
		// asks first unless session.noconfirmation (Shift) is held.
		if (fired("session.kill")) {
			_on_entity_command("delete");
			return;
		}
		// Back to work (session.backtowork): resume the last gather or build.
		if (fired("session.backtowork")) {
			_on_entity_command("back-to-work");
			return;
		}
		auto apply_stance = [&](uint8_t st) {
			if (selected_ids.is_empty()) return;
			if (turn_cmds())
				post_lockstep(7, ids_ptr(), selected_ids.size(), 0, 0, 0, st);
			else
				sim->order_set_stance(ids_ptr(), selected_ids.size(), st);
		};
		if (fired("rts0.stance.aggressive")) { apply_stance(rtso::STANCE_AGGRESSIVE); return; }
		if (fired("rts0.stance.defensive")) { apply_stance(rtso::STANCE_DEFENSIVE); return; }
		if (fired("rts0.stance.standground")) { apply_stance(rtso::STANCE_STAND_GROUND); return; }
		if (fired("rts0.stance.violent")) { apply_stance(rtso::STANCE_VIOLENT); return; }
		if (fired("rts0.stance.passive")) { apply_stance(rtso::STANCE_PASSIVE); return; }
		// session.attackmoveUnit is only a modifier held with a right-click (unit_actions.js),
		// read where the order is given; a press arms nothing.
		if (fired("session.attackmoveunit")) return;

		// Training: Z X C V B N M queue the selected building's 1st-7th trainee;
		// Shift trains a batch of 5 (gui.session.batchtrainingsize). training.xml
		// wires only queueunit.1-7, so Comma (queueunit.8) does nothing, as in 0.28.
		if (selected_building_id != 0 && command_panel_.roster_list()) {
			for (int q = 1; q <= 7; ++q) {
				if (!fired(("session.queueunit." + std::to_string(q)).c_str())) continue;
				if (q - 1 < command_panel_.roster_list()->get_item_count() && !command_panel_.roster_list()->is_item_disabled(q - 1)) {
					const uint16_t tmpl = (uint16_t)(int)command_panel_.roster_list()->get_item_metadata(q - 1);
					add_training(false, selected_building_id, tmpl);
				}
				return;
			}
		}

		if (fired("rts0.build.menu")) {
			enter_build_mode(!build_mode, rtso::BLD_HOUSE);
			return;
		}

		if (fired("rts0.debug.battle500")) { spawn_large_battle(500); return; }
		if (fired("rts0.debug.battle1000")) { spawn_large_battle(1000); return; }
		if (fired("rts0.debug.battle2000")) { spawn_large_battle(2000); return; }

		// gui/session/hotkeys/misc.xml session.showstatusbars -> toggleStatusBars().
		if (fired("session.showstatusbars")) {
			if (view) view->set_show_all_status_bars(!view->show_all_status_bars());
			return;
		}
		// 0 A.D. hotkeys/misc.xml: the toggles save the choice (ConfigDB_CreateAndSaveValue).
		auto toggle_overlay = [this](const char *key) {
			rtso::user_config::set(key, rtso::user_config::get_bool(key) ? "false" : "true");
			rtso::user_config::save();
			refresh_user_options();
		};
		if (fired("session.devcommands.toggle")) { toggle_dev_overlay(); return; }
		if (fired("session.toggleattackrange")) { toggle_overlay("gui.session.attackrange"); return; }
		if (fired("session.toggleaurasrange")) { toggle_overlay("gui.session.aurasrange"); return; }
		if (fired("session.togglehealrange")) { toggle_overlay("gui.session.healrange"); return; }
		// 0 A.D. hotkeys/misc.xml "silhouettes": toggleConfigBool saves it to user.cfg (session.js:840).
		if (fired("silhouettes")) { toggle_overlay("silhouettes"); return; }
		// MenuButtons.js Lobby (hotkey "lobby"): the lobby over the game when a lobby client exists.
		if (fired("lobby")) {
			open_lobby_page();
			return;
		}
		// OverlayCounterElapsedTime / RemainingCeasefire Hotkey: the top-right counters.
		// OverlayCounter.js toggle: ConfigDB_CreateValue, for this run only (not saved).
		auto toggle_counter = [this](const char *key) {
			rtso::user_config::set(key, rtso::user_config::get_bool(key) ? "false" : "true");
			refresh_user_options();
		};
		if (fired("timeelapsedcounter.toggle")) { toggle_counter("gui.session.timeelapsedcounter"); return; }
		if (fired("ceasefirecounter.toggle")) { toggle_counter("gui.session.ceasefirecounter"); return; }

		// fps.toggle, realtime.toggle, screenshot and bigscreenshot are global
		// (scenes/menu/user_settings.gd), as in 0.28.
	}

	Ref<InputEventMouseButton> mb = event;
	if (mb.is_valid()) {
		Vector2 mouse_pos = mb->get_position();
		// input.js: a left or right click in the game world closes the session menu (g_Menu.close),
		// and still does what it does.
		if (mb->get_button_index() == MOUSE_BUTTON_LEFT || mb->get_button_index() == MOUSE_BUTTON_RIGHT)
			if (CanvasLayer *menu = Object::cast_to<CanvasLayer>(get_node_or_null(NodePath("PauseMenu"))); menu && menu->is_visible())
				_on_menu_button_pressed();

		// input.js INPUT_UNIT_POSITION_START / INPUT_UNIT_POSITION: with the right button held a
		// left press does nothing, and the left release drops the coming right-click order.
		if (mb->get_button_index() == MOUSE_BUTTON_LEFT && right_line_.active && !dragging) {
			if (!mb->is_pressed()) right_line_ = RightLine();
			return;
		}
		if (mb->get_button_index() == MOUSE_BUTTON_LEFT) {
			if (mb->is_pressed()) {
				// Hold K and click to send a flare (0 A.D. session.flare, INPUT_NORMAL only), or
				// click once the flare button armed it (INPUT_FLARE).
				if (flare_armed_ || (flare_held() && input_normal())) {
					flare_armed_ = false;
					const Vector3 click_pos = get_ground_click_pos(mouse_pos);
					trigger_flare(click_pos.x, click_pos.z);
					return;
				}

				if (!build_mode && any_armed_mode() && run_armed_click(mb)) return;
				if (build_mode) {
					if (current_build_type == rtso::BLD_WALL_SEGMENT || current_build_type == rtso::BLD_PALISADE ||
							current_build_type == rtso::BLD_SIEGE_WALL) {
						// input.js INPUT_BUILDING_WALL_CLICK / _PATHING: a press sets the start, the next press
						// the end; with Shift (session.queue) the end starts the next wall.
						const Vector3 click = get_ground_click_pos(mouse_pos);
						if (!wall_drag_mode) {
							// input.js:1095-1101: the start is taken only where its tower could stand
							// (or on one of the player's towers to snap onto).
							if (sim) {
								const uint8_t kind = rtso::wallset_kind_of((uint8_t)current_build_type);
								const uint8_t civ = sim->player_civ(local_player());
								const rtso::WallSetInfo &ws = rtso::wallset_info(kind, civ);
								const rtso::Simulation::WallPiece tower{ws.tower, click.x, click.z, 0.0f, ws.tower_half, civ};
								if (!sim->wall_snap_tower(local_player(), click.x, click.z, kind) &&
										sim->check_wall_piece(local_player(), tower) != rtso::Simulation::PLACE_OK)
									return;
							}
							wall_drag_mode = true;
							wall_drag_start_pos = click;
							return;
						}
						const bool queued = rtso::hotkeys::held("session.queue");
						if (!try_place_wall(click, queued)) {
							session_audio_.play_invalid_placement();
							wall_fail_tip_ = rtso::tr0("Cannot build wall here!");
							wall_fail_mouse_ = mouse_pos;
							return;
						}
						if (queued) {
							wall_drag_start_pos = click;
						} else {
							wall_drag_mode = false;
							for (auto *wg : wall_ghosts)
								if (wg) wg->set_visible(false);
							enter_build_mode(false);
						}
						return;
					}
					// The site is fixed at the press; the release places it (input.js INPUT_BUILDING_CLICK).
					auto [cell_x, cell_y] = mouse_cell(mouse_pos);
					build_click_pending = true;
					build_click_dragged_ = false;
					build_click_screen = mouse_pos;
					build_click_ground = get_ground_click_pos(mouse_pos);
					build_click_cell_x = cell_x;
					build_click_cell_y = cell_y;
					return;
				}
				// Godot flags the second press of a double-click (0 A.D. ev.clicks == 2,
				// input.js:893); consumed on release below.
				// A third press soon after a double-click is 0 A.D.'s triple-click
				// (ev.clicks == 3: the exact template and rank).
				{
					const uint64_t now = Time::get_singleton()->get_ticks_msec();
					if (mb->is_double_click()) {
						click_count_ = 2;
						last_double_click_ms_ = now;
						last_double_click_pos_ = mouse_pos;
					} else if (last_double_click_ms_ != 0 && now - last_double_click_ms_ < 500 &&
							mouse_pos.distance_to(last_double_click_pos_) < 8.0f) {
						click_count_ = 3;
						last_double_click_ms_ = 0;
					} else {
						click_count_ = 1;
					}
				}
				double_click_pending = click_count_ >= 2;
				dragging = true;
				drag_start = mouse_pos;
				if (sim) {
					// selection.js addList: Control all units selects anyone visible.
					const size_t hit = click_pick_unit(mouse_pos, sim->controls_all(local_player()) || observer_all_view());
					// input.js:859-862: a double or triple click keeps the first click's unit,
					// unless that one missed.
					if (click_count_ == 1 || press_pick_unit_ == 0) press_pick_unit_ = hit == (size_t)-1 ? 0 : sim->units().id[hit];
				}
				sel_rect->set_rect(drag_start, mouse_pos, true);
			} else {
				const bool dbl_click = double_click_pending;
				double_click_pending = false;
				// selection.js addList plays the first added entity's sound; removeList none.
				const PackedInt32Array before_sel = selected_ids;
				const std::vector<uint32_t> before_structs = selected_structures();
				if (build_click_pending) {
					build_click_pending = false;
					int cell_x = build_click_cell_x, cell_y = build_click_cell_y;
					snap_placement_to_edges(current_build_type, cell_x, cell_y);
					const float fp = (float)rtso::building_type_info(current_build_type).footprint;
					const float angle = placement_angle_at(current_build_type, (cell_x + fp * 0.5f) * rtso::Simulation::GRID,
							(cell_y + fp * 0.5f) * rtso::Simulation::GRID);
					// tryPlaceBuilding (input.js:306-347): Shift queues the build for the builders
					// and keeps placing while units stay selected; Alt sends the first selected
					// builder, which leaves the selection (popOneFromSelection), else everyone.
					const bool queued = rtso::hotkeys::held("session.queue");
					const bool push_front = !queued && rtso::hotkeys::held("session.pushorderfront");
					const uint32_t one_builder = rtso::hotkeys::held("session.orderone") ? first_selected_builder() : 0;
					const bool order_one = one_builder != 0;
					const uint32_t *builders = order_one ? &one_builder : (const uint32_t *)selected_ids.ptr();
					const size_t builder_n = order_one ? 1 : (size_t)selected_ids.size();
					auto after_place = [&]() {
						play_civ_voice("build");
						if (order_one) pop_from_selection(selected_ids, one_builder);
						if (!queued || selected_ids.is_empty()) enter_build_mode(false);
					};
					// tryPlaceBuilding: an invalid preview sends nothing and plays
					// invalid_building_placement; placement goes on (INPUT_BUILDING_PLACEMENT).
					if (turn_cmds() && sim->check_placement(local_player(), cell_x, cell_y, current_build_type, angle) != rtso::Simulation::PLACE_OK) {
						session_audio_.play_invalid_placement();
					} else if (turn_cmds()) {
						const uint32_t flags = (queued ? rtso::NET_BUILD_QUEUED : 0) | (push_front ? rtso::NET_BUILD_PUSH_FRONT : 0);
						post_lockstep(5, builders, builder_n,
								(float)cell_x, (float)cell_y, rtso::Simulation::pack_angle(angle) | flags, current_build_type);
						after_place();
					} else if (uint32_t bid = sim->cmd_build(local_player(), cell_x, cell_y, current_build_type, angle)) {
						if (builder_n > 0) {
							sim->set_push_front_order(push_front);
							sim->order_build_or_repair(builders, builder_n, bid, queued);
							sim->set_push_front_order(false);
						}
						after_place();
					} else {
						session_audio_.play_invalid_placement();
					}
					return;
				}
				if (wall_drag_mode) return; // the start is set: the next press sets the end (input.js INPUT_BUILDING_WALL_PATHING)
				if (dragging) {
					dragging = false;
					sel_rect->set_rect(Vector2(), Vector2(), false);
					Vector2 drag_end = mouse_pos;
					// 0 A.D. [hotkey.selection] modifiers (input.js handleInputBeforeGui):
					// Shift adds, Ctrl removes, Alt keeps military only (Alt+Y non-military),
					// I idle only, O wounded only, Alt on a double-click reaches offscreen.
					const bool is_shift = rtso::hotkeys::held("selection.add");
					const bool is_ctrl = rtso::hotkeys::held("selection.remove");
					const bool nonmilitary_only = rtso::hotkeys::held("selection.nonmilitaryonly");
					const bool military_only = !nonmilitary_only && rtso::hotkeys::held("selection.militaryonly");
					const bool idle_only = rtso::hotkeys::held("selection.idleonly");
					const bool wounded_only = rtso::hotkeys::held("selection.woundedonly");
					const bool offscreen = rtso::hotkeys::held("selection.offscreen");
					// selection.js addList: while someone else's entity is the sole selection (the
					// inspector), nothing is added; removeList drops only that entity.
					if (selected_ids.is_empty() && selected_building_id == 0 && inspector_.kind() != RtsoInspectorView::INSPECT_NONE &&
							!sim->controls_all(local_player()) && !observer_all_view() && (is_shift || is_ctrl)) {
						if (is_ctrl && !is_shift && drag_start.distance_to(drag_end) < user_opts_.drag_delta) {
							const uint8_t kind = inspector_.kind();
							const uint32_t id = inspector_.id();
							const bool same = inspector_.inspect_at(get_ground_click_pos(mouse_pos)) && inspector_.kind() == kind && inspector_.id() == id;
							if (same) inspector_.clear();
							else inspector_.inspect(kind, id);
						}
						return;
					}

					if (drag_start.distance_to(drag_end) < user_opts_.drag_delta) {
						// Single click selection
						Vector3 click_pos = get_ground_click_pos(mouse_pos);
						auto uview = sim->units();

						// input.js: the entity hit on mouse-down is kept, so a unit that walked away
						// before the release is still picked; else the one under the release.
						size_t best_uidx = (size_t)-1;
						const bool control_all = sim->controls_all(local_player()) || observer_all_view();
						if (press_pick_unit_ != 0)
							for (size_t i = 0; i < uview.count; ++i)
								if (uview.id[i] == press_pick_unit_ && uview.alive[i] &&
										(uview.team[i] == local_player() || control_all)) best_uidx = i;
						if (best_uidx == (size_t)-1) {
							// input.js picks what the hover highlights (PickEntityAtPoint): a tree,
							// structure or animal right under the cursor beats a unit beside it.
							const uint32_t hov = entity_under_mouse(mouse_pos, false);
							bool hov_unit = hov == 0;
							for (size_t i = 0; i < uview.count && !hov_unit; ++i) hov_unit = uview.id[i] == hov;
							if (hov_unit) best_uidx = click_pick_unit(mouse_pos, control_all);
							// input.js: a double-click on someone else's unit picks the player's own of
							// its template (PickSimilarPlayerEntities names no player), maybe none.
							if (hov_unit && best_uidx == (size_t)-1 && dbl_click) {
								best_uidx = click_pick_unit(mouse_pos, true);
								if (best_uidx != (size_t)-1) inspector_.clear();
							}
						}
						// input.js: clicking any entity but the followed one stops the camera following.
						if (best_uidx != (size_t)-1 && cam_rig && cam_rig->follow_id() != uview.id[best_uidx]) cam_rig->set_follow_id(0);

						if (best_uidx != (size_t)-1 && is_ctrl && !dbl_click) {
							// Ctrl+click drops the unit, and its formation, from the selection
							// (selection.remove: removeList's addFormationMembers).
							for (int32_t uid : formation_mates((int32_t)uview.id[best_uidx])) {
								const int64_t at = selected_ids.find(uid);
								if (at >= 0) selected_ids.remove_at(at);
							}
						} else if (best_uidx != (size_t)-1) {
							if (dbl_click) {
								// Double-click selects the player's units of the clicked one's template
								// (any rank), triple-click its exact template and rank, on screen or on
								// the whole map with selection.offscreen; Ctrl removes them instead
								// (0 A.D. gui/session/input.js:1009-1042, selectionGroupName).
								const uint8_t same_type = uview.unit_type[best_uidx];
								const uint16_t same_tmpl = uview.tmpl[best_uidx];
								const uint8_t same_rank = uview.rank[best_uidx];
								const bool exact = click_count_ >= 3;
								// identity.selectionGroupName: template_unit_support_female_citizen, _healer,
								// _trader, ship_fishing, ship_merchant and the catafalque group every civ's
								// (and the house-trained woman's) template on a double-click.
								const bool group = !exact && RtsoSelectionGrid::shared_selection_group(same_type);
								auto matches = [&](size_t i) {
									if (group) return uview.unit_type[i] == same_type;
									if (same_tmpl != rtso::CIV_UNIT_TEMPLATE_NONE ? uview.tmpl[i] != same_tmpl : uview.unit_type[i] != same_type)
										return false;
									return !exact || uview.rank[i] == same_rank;
								};
								Camera3D *cam = cam_rig ? cam_rig->get_camera() : nullptr;
								// addList / removeList change only the units they name: selected
								// structures stay under Shift or Ctrl (selection.js).
								if (!is_shift && !is_ctrl) {
									selected_ids.clear();
									selected_building_id = 0;
								}
								if (cam) {
									const Vector2 vp_size = get_viewport()->get_visible_rect().size;
									for (size_t i = 0; i < uview.count; ++i) {
										if (!uview.alive[i] || uview.team[i] != (observer_all_view() ? uview.team[best_uidx] : local_player()) || !matches(i))
											continue;
										// The unit on the ground, or a ship on the sea, as drawn.
										float wy = sim->get_terrain_height(uview.pos_x[i], uview.pos_z[i]);
										if (rtso::unit_is_ship(uview.unit_type[i]) && sim->water_height() > -0.5f)
											wy = std::max(wy, sim->water_height() + rtso::NAV_WATER_MARGIN);
										Vector3 world_pos(uview.pos_x[i], wy, uview.pos_z[i]);
										if (!offscreen && cam->is_position_behind(world_pos)) continue;
										Vector2 screen_pos = cam->unproject_position(world_pos);
										if (offscreen || (screen_pos.x >= 0.0f && screen_pos.x <= vp_size.x &&
											screen_pos.y >= 0.0f && screen_pos.y <= vp_size.y)) {
											// addList / removeList take whole formations.
											for (int32_t uid : formation_mates((int32_t)uview.id[i])) {
												const int64_t at = selected_ids.find(uid);
												if (is_ctrl) {
													if (at >= 0) selected_ids.remove_at(at);
												} else if (at < 0) {
													selected_ids.push_back(uid);
												}
											}
										}
									}
								}
								if (selected_ids.is_empty() && !is_ctrl && (uview.team[best_uidx] == local_player() || control_all))
									selected_ids.push_back((int32_t)uview.id[best_uidx]);
							} else if (is_shift) {
								// Shift+click adds the unit (selection.add -> addList keeps it if
								// already selected; Ctrl+click is the removal).
								for (int32_t uid : formation_mates((int32_t)uview.id[best_uidx]))
									if (!selected_ids.has(uid)) selected_ids.push_back(uid);
							} else {
								selected_ids.clear();
								selected_building_id = 0;
								for (int32_t uid : formation_mates((int32_t)uview.id[best_uidx]))
									selected_ids.push_back(uid);
							}
							play_select_added(before_sel, before_structs);
						} else if (uint32_t animal = own_fauna_at(click_pos.x, click_pos.z)) {
							// The player's own livestock (0 A.D. livestock are units). A double-click takes
							// the player's animals of its kind on screen, or everywhere with
							// selection.offscreen (input.js PickSimilarPlayerEntities).
							std::vector<int32_t> picked{(int32_t)animal};
							if (dbl_click) {
								auto fv = sim->fauna();
								uint8_t kind = 0xFF;
								for (size_t f = 0; f < fv.count; ++f)
									if (fv.id[f] == animal) kind = fv.fauna_type[f];
								Camera3D *cam = cam_rig ? cam_rig->get_camera() : nullptr;
								const Vector2 vp_size = get_viewport()->get_visible_rect().size;
								for (size_t f = 0; f < fv.count && cam; ++f) {
									if (fv.id[f] == animal || fv.fauna_type[f] != kind || !sim->fauna_selectable_by(local_player(), fv.id[f]))
										continue;
									const Vector3 wp(fv.pos_x[f], sim->get_terrain_height(fv.pos_x[f], fv.pos_z[f]), fv.pos_z[f]);
									if (!offscreen && cam->is_position_behind(wp)) continue;
									const Vector2 sp = cam->unproject_position(wp);
									if (offscreen || (sp.x >= 0.0f && sp.x <= vp_size.x && sp.y >= 0.0f && sp.y <= vp_size.y))
										picked.push_back((int32_t)fv.id[f]);
								}
							}
							if (!is_ctrl && !is_shift) {
								selected_ids.clear();
								selected_building_id = 0;
							}
							for (int32_t id : picked) {
								const int64_t at = selected_ids.find(id);
								if (is_ctrl) {
									if (at >= 0) selected_ids.remove_at(at);
								} else if (at < 0) {
									selected_ids.push_back(id);
								}
							}
						} else {
							// Check building hit: any visible part (0 A.D. picks the
							// rendered entity), then the old ground test round its base.
							// Control all units: anyone's structure (selection.js addList force).
							uint32_t hit_bld = building_under_mouse(mouse_pos, nullptr, sim->controls_all(local_player()));
							// input.js: a double-click on someone else's structure picks the player's own
							// of its selection group (PickSimilarPlayerEntities), maybe none.
							bool foreign_bld = false;
							if (hit_bld == 0 && dbl_click && !is_ctrl) {
								hit_bld = building_under_mouse(mouse_pos, nullptr, true);
								foreign_bld = hit_bld != 0;
								if (foreign_bld) inspector_.clear();
							}
							for (const auto &b : sim->buildings()) {
								if (hit_bld != 0) break;
								if (b.team != local_player()) continue;
								float dx = b.x - click_pos.x;
								float dz = b.z - click_pos.z;
								float hit_r = b.half_size + 0.6f;
								if (dx * dx + dz * dz <= hit_r * hit_r) {
									hit_bld = b.id;
									break;
								}
							}
							if (hit_bld != 0) {
								if (cam_rig && cam_rig->follow_id() != hit_bld) cam_rig->set_follow_id(0);
								// selection.js: Shift adds the structure, Ctrl removes it, a plain click
								// selects it alone. A double-click takes every one of its selection group
								// on screen (all of them with selection.offscreen), foundations too; a
								// triple-click its exact template, so finished ones or, on a foundation,
								// foundations (input.js PickSimilarPlayerEntities: matchRank false by
								// SelectionGroupName, else the template name, "foundation|" included).
								std::vector<uint32_t> structs = selected_structures();
								std::vector<uint32_t> picked;
								if (!foreign_bld) picked.push_back(hit_bld);
								if (dbl_click) {
									const bool exact = click_count_ >= 3;
									auto group_of = [&](const rtso::Simulation::Building &b) -> int64_t {
										const bool foundation = b.build_progress < 1.0f;
										if (exact)
											return ((int64_t)b.civ << 40) | ((int64_t)b.type << 32) | ((int64_t)foundation << 31) |
													(int64_t)std::lround(b.half_size * 100.0f);
										// rome/siege_wall_tower has its own group, the siege wall pieces another.
										if (rtso::is_siege_wall_tower(b.type, b.half_size)) return 0x10000 | b.type;
										return structure_selection_group(b.type);
									};
									int64_t group = -1;
									for (const auto &b : sim->buildings())
										if (b.id == hit_bld) group = group_of(b);
									Camera3D *cam = cam_rig ? cam_rig->get_camera() : nullptr;
									const Vector2 vp_size = get_viewport()->get_visible_rect().size;
									for (const auto &b : sim->buildings()) {
										if (b.id == hit_bld || b.team != local_player() || b.wrecked || !cam || group_of(b) != group)
											continue;
										const Vector3 wp(b.x, sim->get_terrain_height(b.x, b.z), b.z);
										if (!offscreen && cam->is_position_behind(wp)) continue;
										const Vector2 sp = cam->unproject_position(wp);
										if (offscreen || (sp.x >= 0.0f && sp.x <= vp_size.x && sp.y >= 0.0f && sp.y <= vp_size.y))
											picked.push_back(b.id);
									}
								}
								if (is_ctrl) {
									for (uint32_t id : picked) structs.erase(std::remove(structs.begin(), structs.end(), id), structs.end());
								} else if (is_shift) {
									for (uint32_t id : picked)
										if (std::find(structs.begin(), structs.end(), id) == structs.end()) structs.push_back(id);
								} else {
									selected_ids.clear();
									structs = picked;
								}
								set_selected_structures(structs);
								play_select_added(before_sel, before_structs);
							} else if (!is_ctrl && !(is_shift && (!selected_ids.is_empty() || selected_building_id != 0)) &&
									inspector_.inspect_at(click_pos)) {
								// Another player's unit or structure, an animal or a resource:
								// selected for its details only (0 A.D. EntitySelection; selection.js addList
								// skips an unowned entity while something is selected, so Shift keeps it).
								if (cam_rig && cam_rig->follow_id() != inspector_.id()) cam_rig->set_follow_id(0);
								selected_ids.clear();
								selected_building_id = 0;
								// A double-click picks the player's own entities of its template
								// (PickSimilarPlayerEntities): none of an animal, a tree or a treasure.
								if (dbl_click && !is_shift) inspector_.clear();
								else play_inspected_select();
							} else {
								// Clicked empty ground: deselect unless selection.add or .remove is held
								// (input.js:963-975).
								if (!is_shift && !is_ctrl) {
									selected_ids.clear();
									selected_building_id = 0;
									// resetIdleUnit: the next idle-unit hotkey starts again at the first.
									last_idle_unit_ = 0;
									idle_class_index_ = 0;
								}
							}
						}
					} else {
						// Box selection
						Vector2 min_p(std::min(drag_start.x, drag_end.x), std::min(drag_start.y, drag_end.y));
						Vector2 max_p(std::max(drag_start.x, drag_end.x), std::max(drag_start.y, drag_end.y));

						const std::vector<int32_t> boxed = band_box_ids(drag_start, drag_end);
						// input.js: with no unit in the box, its defensive structures, else any of the
						// player's structures.
						const std::vector<uint32_t> boxed_structs = boxed.empty() ? band_box_structures(drag_start, drag_end)
								: std::vector<uint32_t>();
						if (!boxed_structs.empty()) {
							std::vector<uint32_t> structs = selected_structures();
							if (is_ctrl) {
								for (uint32_t id : boxed_structs) structs.erase(std::remove(structs.begin(), structs.end(), id), structs.end());
							} else if (is_shift) {
								for (uint32_t id : boxed_structs)
									if (std::find(structs.begin(), structs.end(), id) == structs.end()) structs.push_back(id);
							} else {
								selected_ids.clear();
								structs = boxed_structs;
							}
							set_selected_structures(structs);
							play_select_added(before_sel, before_structs);
							return;
						}
						if (!is_shift && !is_ctrl) {
							selected_ids.clear();
							selected_building_id = 0;
						}
						for (int32_t uid : boxed) {
							const int64_t at = selected_ids.find(uid);
							if (is_ctrl) {
								if (at >= 0) selected_ids.remove_at(at);
							} else if (at < 0) {
								selected_ids.push_back(uid);
							}
						}
						play_select_added(before_sel, before_structs);
					}
				}
			}
		} else if (mb->get_button_index() == MOUSE_BUTTON_RIGHT && !mb->is_pressed()) {
			// input.js INPUT_UNIT_POSITION_START / INPUT_UNIT_POSITION: the order fires on release.
			if (right_line_.active) finish_right_line(mb);
			return;
		} else if (mb->get_button_index() == MOUSE_BUTTON_RIGHT && mb->is_pressed() && flare_armed_) {
			flare_armed_ = false; // INPUT_FLARE: a right click cancels the flare
			return;
		} else if (mb->get_button_index() == MOUSE_BUTTON_RIGHT && mb->is_pressed() && !replaying_right_ && flare_held() &&
				input_normal()) {
			// input.js INPUT_NORMAL: with session.flare held, a press of any button sends a flare.
			const Vector3 click_pos = get_ground_click_pos(mouse_pos);
			trigger_flare(click_pos.x, click_pos.z);
			return;
		} else if (mb->get_button_index() == MOUSE_BUTTON_RIGHT && mb->is_pressed()) {
			// input.js INPUT_PRESELECTEDACTION: a right press only drops the armed action, and
			// its release gives no order.
			if (!replaying_right_ && any_armed_mode()) {
				attack_move_mode = patrol_mode = guard_mode = call_to_arms_mode = garrison_mode = turret_mode = repair_mode = false;
				return;
			}
			// A right-click while band-boxing cancels the box (_input, before the GUI).
			if (build_mode) {
				enter_build_mode(false);
				return;
			}
			// input.js INPUT_NORMAL right button: wait for the release, which gives the usual order
			// (a structure's rally point too) or, after a drag, spreads the units along the drawn line.
			if (!replaying_right_ && (!selected_ids.is_empty() || selected_building_id != 0) && orders_allowed()) {
				right_line_ = RightLine();
				right_line_.active = true;
				right_line_.start = mouse_pos;
				return;
			}
			Vector3 click_pos = get_ground_click_pos(mouse_pos);

			// Right-clicking the selected structure itself drops its rally point
			// (unit_actions.js "unset-rallypoint", lockstep command 14 param 1), unless
			// session.autorallypoint (Ctrl) sets it on a structure that trains units, so
			// what it trains goes inside ("set-rallypoint" on itself, Trainer autoGarrison).
			const bool auto_rally = over_own_rally_structure(click_pos, false) && rtso::hotkeys::held("session.autorallypoint") &&
					selected_building_trains_units();
			if (over_own_rally_structure(click_pos) && !auto_rally) {
				if (turn_cmds())
					post_lockstep(14, nullptr, 0, 0, 0, selected_building_id, 1);
				else
					sim->cmd_unset_rally_point(local_player(), selected_building_id);
				return;
			}
			// unit_actions.js "set-rallypoint" getActionInfo: a selected structure is no target
			// (unless session.autorallypoint), and with no rally point to drop nothing happens.
			if (selected_ids.is_empty() && over_own_rally_structure(click_pos, false) && !auto_rally) return;
			// A selected structure alone: a right-click sets its rally point. With units also
			// selected (Shift+click), their orders come first (unit_actions.js: the units'
			// actions are checked before "set-rallypoint").
			if (selected_building_id != 0 && selected_ids.is_empty()) {
				if (try_focus_fire(click_pos)) return;
				uint32_t target_res = 0, target_bld = 0, target_ent = 0;
				uint8_t rally_cmd = rtso::RALLY_CMD_AUTO;
				rally_target_at(click_pos, target_res, target_bld, &target_ent, &rally_cmd);
				if (auto_rally) target_bld = selected_building_id, rally_cmd = rtso::RALLY_CMD_AUTO;
				// A market too close to this one: the "trade" rally action is disabled (no gain).
				if (rally_cmd == rtso::RALLY_CMD_TRADE) {
					rtso::Simulation::TradeGain g;
					if (rally_route_gain(target_bld, g) && g.trader <= 0) return;
				}
				// session.queue (Shift) adds a position after the ones set (RallyPoint.AddPosition).
				const bool rally_queued = rtso::hotkeys::held("session.queue");
				// unit_actions.js "set-rallypoint": every selected structure with a RallyPoint.
				for (uint32_t sb : selected_structures()) {
					if (sb != selected_building_id && !building_trains_units(sb)) continue;
					// A trade route starts at each structure itself: only a market sets one up.
					const uint8_t cmd_here = rally_cmd == rtso::RALLY_CMD_TRADE && sb != selected_building_id ? (uint8_t)rtso::RALLY_CMD_AUTO : rally_cmd;
					if (turn_cmds()) {
						uint32_t extra[4] = {target_res, auto_rally ? sb : target_bld, target_ent, cmd_here};
						post_lockstep(14, extra, 4, click_pos.x, click_pos.z, sb, rally_queued ? rtso::NET_PARAM_QUEUED : 0);
					} else {
						sim->cmd_set_rally_point(local_player(), sb, click_pos.x, click_pos.z, target_res, auto_rally ? sb : target_bld,
								rally_queued, cmd_here, target_ent);
					}
				}
				// unit_actions.js set-rallypoint: no sound.
				return;
			}

			if (selected_ids.is_empty()) return;

			// Shift queues the order behind the units' current ones (0 A.D.
			// gui/session/input.js:1345 session.queue); without it the order replaces them.
			const bool queue_shift = rtso::hotkeys::held("session.queue");
			const uint8_t qbit = queue_shift ? rtso::NET_PARAM_QUEUED : 0;
			const uint32_t *sel_ptr = (const uint32_t *)selected_ids.ptr();
			size_t sel_n = selected_ids.size();
			// Alt orders only one unit, which then leaves the selection (0 A.D.
			// gui/session/input.js popOneFromSelection, session.orderone).
			uint32_t one_id = 0;
			struct PopOne {
				PackedInt32Array &sel;
				const uint32_t &id;
				~PopOne() {
					if (id != 0) pop_from_selection(sel, id);
				}
			} pop_one{ selected_ids, one_id };
			const uint32_t *all_ptr = sel_ptr;
			const size_t all_n = sel_n;
			if (rtso::hotkeys::held("session.orderone")) {
				one_id = sel_ptr[0];
				sel_ptr = &one_id;
				sel_n = 1;
			}
			// popOneFromSelection: with Alt the action checks look at the whole selection, and the
			// order goes to the first selected unit able to carry it out.
			auto able_any = [&](auto &&able) {
				const uint32_t *p = one_id ? all_ptr : sel_ptr;
				const size_t n = one_id ? all_n : sel_n;
				for (size_t k = 0; k < n; ++k)
					if (able(p[k])) return true;
				return false;
			};
			auto pick_one = [&](auto &&able) {
				for (size_t k = 0; one_id && k < all_n; ++k)
					if (able(all_ptr[k])) { one_id = all_ptr[k]; return; }
			};
			auto builds = [&](uint32_t id) { return sim->unit_id_builds(id); };

			// 5. Default: Move Order (a player's walk ignores enemies until it arrives)
			auto walk_there = [&]() {
				range_overlays_.add_target_marker(click_pos.x, click_pos.z);
				if (turn_cmds())
					post_lockstep(1, sel_ptr, sel_n, click_pos.x, click_pos.z, 0, qbit);
				else {
					apply_default_formation(sel_ptr, sel_n);
					sim->order_walk(sel_ptr, sel_n, click_pos.x, click_pos.z, queue_shift, true);
					sim->order_fauna_walk(local_player(), sel_ptr, sel_n, click_pos.x, click_pos.z);
				}
				play_civ_voice("move");
			};
			// unit_actions.js "move" hotkeyActionCheck: session.move walks there whatever is
			// under the cursor (an enemy, a resource, a structure).
			if (rtso::hotkeys::held("session.move")) {
				walk_there();
				return;
			}

			// Owned livestock alone (0 A.D. livestock are units): a right-click walks it, or
			// sends it into the player's own or a mutual ally's corral under the cursor (UnitAI "garrison").
			bool fauna_only = sel_n > 0;
			for (size_t k = 0; k < sel_n && fauna_only; ++k) fauna_only = sim->fauna_selectable_by(local_player(), sel_ptr[k]);
			if (fauna_only) {
				uint32_t corral = 0;
				for (const auto &b : sim->buildings()) {
					if (!sim->is_ally(b.team, local_player()) || b.type != rtso::BLD_CORRAL) continue;
					const float dx = b.x - click_pos.x, dz = b.z - click_pos.z, hit_r = b.half_size + 1.2f;
					if (dx * dx + dz * dz <= hit_r * hit_r) corral = b.id;
				}
				if (!corral)
					range_overlays_.add_target_marker(click_pos.x, click_pos.z);
				if (turn_cmds()) {
					if (corral) post_lockstep(12, sel_ptr, sel_n, 0, 0, corral, qbit);
					else post_lockstep(1, sel_ptr, sel_n, click_pos.x, click_pos.z, 0, qbit);
				} else {
					sim->order_fauna_walk(local_player(), sel_ptr, sel_n, click_pos.x, click_pos.z, corral);
				}
				return;
			}

			// unit_actions.js "attack" (allowCapture false) or "capture" on structure bid or scenery sid.
			const auto send_structure_click = [&](StructureClick c, uint32_t bid, uint32_t sid) {
				if (c == CLICK_NONE) return false;
				const bool capture = c == CLICK_CAPTURE;
				// Alt (orderone): the first selected unit able to attack or capture it.
				if (sid) {
					pick_one([&](uint32_t id) {
						auto v = sim->units();
						for (size_t i = 0; i < v.count; ++i)
							if (v.id[i] == id) return v.alive[i] && v.attack_damage[i] > 0.0f;
						return false;
					});
				} else {
					pick_one([&](uint32_t id) {
						return capture ? sim->any_can_capture_building(&id, 1, bid) : sim->any_can_damage_building(&id, 1, bid);
					});
				}
				if (sid) {
					if (turn_cmds())
						post_lockstep(17, sel_ptr, sel_n, 0, 0, sid, (uint8_t)(qbit | (capture ? 0 : rtso::NET_HUNT_SCENERY_NO_CAPTURE)));
					else
						sim->order_attack_scenery(sel_ptr, sel_n, sid, capture, queue_shift);
				} else {
					if (turn_cmds())
						post_lockstep(15, sel_ptr, sel_n, 0, 0, bid, (uint8_t)(qbit | (capture ? 0 : rtso::NET_PARAM_NO_CAPTURE)));
					else
						sim->order_attack_building(sel_ptr, sel_n, bid, queue_shift, capture);
				}
				play_civ_voice("attack");
				return true;
			};
			// unit_actions.js hotkeyActionCheck in specificness order: session.attack (F, "attack" 9)
			// and session.capture (C, "capture" 10) on an enemy structure or Gaia scenery come before
			// garrison (20) and attack-move (30, Ctrl); without them the actionCheck pass below
			// attacks too (structure_click).
			if ((rtso::hotkeys::held("session.attack") || rtso::hotkeys::held("session.capture")) &&
					!attack_move_mode && !rtso::hotkeys::held("session.calltoarms")) {
				if (const uint32_t sid = scenery_at(click_pos.x, click_pos.z); sid != 0) {
					if (send_structure_click(scenery_click(sid), 0, sid)) return;
				}
				for (const auto &b : sim->buildings()) {
					if (b.wrecked) continue; // own structures too: C recaptures (Capturable.CanCapture)
					const float dx = b.x - click_pos.x, dz = b.z - click_pos.z;
					const float hit_r = b.half_size + 1.2f;
					if (dx * dx + dz * dz > hit_r * hit_r) continue;
					const StructureClick c = structure_click(b.id);
					if ((c == CLICK_ATTACK && rtso::hotkeys::held("session.attack")) ||
							(c == CLICK_CAPTURE && rtso::hotkeys::held("session.capture"))) {
						send_structure_click(c, b.id, 0);
						return;
					}
					break;
				}
			}

			// Attack-move: session.attackmove (Ctrl) + right-click, or attack_move_mode
			// toggled. Ctrl on one of our own buildings garrisons instead (session.garrison).
			// The Garrison and Repair buttons arm the next left click (run_armed_click), like the modifier.
			const bool force_garrison = garrison_mode || rtso::hotkeys::held("session.garrison");
			const bool force_repair = repair_mode || rtso::hotkeys::held("session.repair");
			// "occupy-turret": the Occupy Turret button arms it (preSelectedActionCheck, ahead of
			// everything); session.occupyturret comes after "garrison" (specificness 21 to 20).
			const bool armed_turret = turret_mode;
			const bool force_turret = turret_mode || rtso::hotkeys::held("session.occupyturret");
			garrison_mode = turret_mode = repair_mode = false;
			bool garrison_click = false;
			uint32_t turret_target = 0;
			if (force_garrison || force_turret) {
				for (const auto &b : sim->buildings()) {
					if (b.team != local_player() && !sim->is_ally(local_player(), b.team)) continue; // own or allied
					const float dx = b.x - click_pos.x, dz = b.z - click_pos.z, hit_r = b.half_size + 1.2f;
					if (dx * dx + dz * dz <= hit_r * hit_r) {
						// "garrison" (20) and "occupy-turret" (21) come before "attack-move" (30) when
						// their hotkeyActionCheck finds something to do; otherwise Ctrl attack-moves.
						garrison_click = armed_turret || (force_garrison && selection_can_garrison(b.id)) ||
								(force_turret && selection_can_occupy_turret(b.id));
						turret_target = b.id;
						break;
					}
				}
				// A ship, ram or siege tower a selected unit may board ("garrison" takes any GarrisonHolder).
				if (!garrison_click && force_garrison && transport_under(click_pos.x, click_pos.z) != 0) garrison_click = true;
			}
			// The armed Occupy Turret takes this click whatever lies under it: the holder's points,
			// or nothing (action-occupy-turret-disabled).
			if (armed_turret) {
				if (turret_target && selection_can_occupy_turret(turret_target)) {
					// unit_actions.js "occupy-turret": order_garrison (said before a unit at
					// the holder boards and leaves the world)
					play_civ_voice("garrison");
					if (turn_cmds())
						post_lockstep(12, sel_ptr, sel_n, 0, 0, turret_target, (uint8_t)(qbit | rtso::NET_PARAM_OCCUPY_TURRET));
					else
						sim->order_garrison(sel_ptr, sel_n, turret_target, queue_shift, true);
				}
				return;
			}
			// session.calltoarms + right-click: Commands.js call-to-arms (drop loads, then attack-walk).
			// unit_actions.js: session.attackmoveUnit fights units only (targetClasses Unit); call-to-arms
			// captures only with session.capture.
			const bool units_only = rtso::hotkeys::held("session.attackmoveUnit");
			if (call_to_arms_mode || (!garrison_click && rtso::hotkeys::held("session.calltoarms"))) {
				call_to_arms_mode = false;
				range_overlays_.add_target_marker(click_pos.x, click_pos.z);
				const bool capture = rtso::hotkeys::held("session.capture");
				if (turn_cmds())
					post_lockstep((uint8_t)rtso::NetCommandType::NET_CMD_CALL_TO_ARMS, sel_ptr, sel_n, click_pos.x, click_pos.z, 0,
							(uint8_t)(qbit | (units_only ? rtso::NET_PARAM_UNITS_ONLY : 0) | (capture ? rtso::NET_PARAM_CAPTURE : 0)));
				else
					sim->order_call_to_arms(sel_ptr, sel_n, click_pos.x, click_pos.z, queue_shift, units_only, capture);
				play_civ_voice("attack");
				return;
			}
			if (attack_move_mode || (!garrison_click && (rtso::hotkeys::held("session.attackmove") || units_only))) {
				range_overlays_.add_target_marker(click_pos.x, click_pos.z);
				if (turn_cmds())
					post_lockstep(10, sel_ptr, sel_n, click_pos.x, click_pos.z, 0,
							(uint8_t)(qbit | rtso::NET_PARAM_NO_CAPTURE | (units_only ? rtso::NET_PARAM_UNITS_ONLY : 0)));
				else
					sim->order_attack_move(sel_ptr, sel_n, click_pos.x, click_pos.z, queue_shift, units_only, false);
				play_civ_voice("attack");
				attack_move_mode = false;
				return;
			}
			// Patrol and guard: the command button arms them, or hold P / G while
			// right-clicking (0 A.D. session.patrol / session.guard modifiers).
			if (patrol_mode || rtso::hotkeys::held("session.patrol")) {
				if (turn_cmds())
					post_lockstep(11, sel_ptr, sel_n, click_pos.x, click_pos.z, 0, qbit);
				else
					{
						apply_default_formation(sel_ptr, sel_n);
						sim->order_patrol(sel_ptr, sel_n, click_pos.x, click_pos.z, queue_shift);
					}
				// unit_actions.js PlaySound("order_patrol"): 0.28's units have no such sound group.
				patrol_mode = false;
				return;
			}
			// session.guard held: guard what is under the cursor (an own or allied unit or
			// structure), else stop guarding (remove-guard, specificness 41), else the click is the
			// usual order (hotkeyActionCheck fails). An armed Guard only guards.
			if (guard_mode || rtso::hotkeys::held("session.guard")) {
				const uint32_t target = guard_target_at(click_pos);
				if (target != 0) {
					if (turn_cmds())
						post_lockstep(21, sel_ptr, sel_n, 0, 0, target, qbit);
					else
						sim->order_guard(sel_ptr, sel_n, target, queue_shift);
					// PlaySound("order_guard"): no such sound group in 0.28's units either.
					guard_mode = false;
					return;
				}
				if (!guard_mode && selection_guarding()) {
					_on_remove_guard_pressed();
					return;
				}
				if (guard_mode) {
					guard_mode = false;
					return;
				}
			}

			auto uview = sim->units();
			// input.js determineAction: the actions below test the one entity under the cursor
			// (PickEntityAtPoint, as the hover highlights it); over bare ground, what lies near the
			// click point.
			const uint32_t picked = entity_under_mouse(mouse_pos);
			const auto near_pick = [&](uint32_t id, float d2, float best_d2) {
				return picked != 0 ? id == picked : d2 < best_d2;
			};

			// A loaded ship right-clicked on land just moves (unit_actions.js has no unload-on-click
			// action): passengers leave through Unload All, the garrison icons or session.unload.

			// 0. returnresource (specificness 2) on a supply wagon or worker elephant the carriers
			// may drop at (ResourceDropsite on template_unit_support_wagon / _elephant).
			{
				uint32_t drop_id = 0;
				float best = 2.2f * 2.2f;
				for (size_t i = 0; i < uview.count; ++i) {
					if (!uview.alive[i] || !rtso::unit_is_mobile_dropsite(uview.unit_type[i])) continue;
					const float dx = uview.pos_x[i] - click_pos.x, dz = uview.pos_z[i] - click_pos.z;
					if (!near_pick(uview.id[i], dx * dx + dz * dz, best)) continue;
					const uint32_t wagon = uview.id[i];
					if (!able_any([&](uint32_t id) { return sim->can_return_resource(id, wagon); })) continue;
					best = dx * dx + dz * dz;
					drop_id = uview.id[i];
				}
				if (drop_id != 0) {
					pick_one([&](uint32_t id) { return sim->can_return_resource(id, drop_id); });
					if (turn_cmds())
						post_lockstep(34, sel_ptr, sel_n, 0, 0, drop_id, qbit);
					else
						sim->order_return_resource(sel_ptr, sel_n, drop_id, queue_shift);
					play_civ_voice("gather");
					return;
				}
			}

			// 1. session.garrison + right click on an own or allied ship, ram or siege tower (board it).
			// unit_actions.js "garrison" takes any GarrisonHolder: warships, rams and siege towers.
			// Its specificness 20 loses to "repair" (11) and "move" (12): a plain click walks there.
			const uint32_t clicked_transport_id = transport_under(click_pos.x, click_pos.z);
			auto board = [&]() {
				play_civ_voice("garrison"); // unit_actions.js "garrison": order_garrison, before a unit alongside boards
				if (turn_cmds())
					post_lockstep(18, sel_ptr, sel_n, 0, 0, clicked_transport_id, qbit);
				else
					sim->order_board_transport(sel_ptr, sel_n, clicked_transport_id, queue_shift);
			};
			if (clicked_transport_id != 0 && force_garrison) {
				board();
				return;
			}

			// 1b. Builders right-clicking a damaged own or allied siege engine or ship repair it
			// (0 A.D. unit_actions "repair": Repairable units, Builder.js CanRepair).
			{
				bool any_builder = false;
				for (int k = 0; k < selected_ids.size() && !any_builder; ++k)
					for (size_t i = 0; i < uview.count; ++i)
						if (uview.id[i] == (uint32_t)selected_ids[k]) {
							any_builder = rtso::unit_can_build(uview.unit_type[i], uview.tmpl ? uview.tmpl[i] : rtso::CIV_UNIT_TEMPLATE_NONE) && !sim->nonbuilder(uview.id[i]);
							break;
						}
				uint32_t repair_id = 0;
				float best_rd2 = 2.2f * 2.2f;
				for (size_t i = 0; any_builder && i < uview.count; ++i) {
					if (!uview.alive[i] || uview.hp[i] >= uview.max_hp[i] || !sim->is_repairable_unit(uview.id[i])) continue;
					if (uview.team[i] != local_player() && !sim->is_ally(local_player(), uview.team[i])) continue;
					const float dx = uview.pos_x[i] - click_pos.x, dz = uview.pos_z[i] - click_pos.z;
					const float d2 = dx * dx + dz * dz;
					if (near_pick(uview.id[i], d2, best_rd2)) { best_rd2 = d2; repair_id = uview.id[i]; }
				}
				if (repair_id != 0) {
					pick_one(builds);
					if (turn_cmds())
						post_lockstep(16, sel_ptr, sel_n, 0, 0, repair_id, qbit);
					else
						sim->order_build_or_repair(sel_ptr, sel_n, repair_id, queue_shift);
					play_civ_voice("repair");
					return;
				}
			}

			// 1c. Healers right-clicking an injured own or allied unit heal it (0 A.D. "heal" cursor):
			// only a unit a selected healer may heal (Heal.js CanHeal: a Human, not the healer itself).
			{
				// With orderone the first able healer of the selection is the one ordered
				// (input.js firstAbleEntity).
				uint32_t first_healer = 0;
				uint32_t heal_id = 0;
				float best_hd2 = 2.2f * 2.2f;
				for (size_t i = 0; i < uview.count; ++i) {
					if (!uview.alive[i] || uview.hp[i] >= uview.max_hp[i]) continue;
					if (uview.team[i] != local_player() && !sim->regards_as_ally(local_player(), uview.team[i])) continue;
					const float dx = uview.pos_x[i] - click_pos.x, dz = uview.pos_z[i] - click_pos.z;
					const float d2 = dx * dx + dz * dz;
					if (!near_pick(uview.id[i], d2, best_hd2)) continue;
					const uint32_t h = sim->first_able_healer((const uint32_t *)selected_ids.ptr(), (size_t)selected_ids.size(), uview.id[i]);
					if (h == 0) continue;
					best_hd2 = d2;
					heal_id = uview.id[i];
					first_healer = h;
				}
				if (heal_id != 0) {
					if (one_id != 0) one_id = first_healer;
					if (turn_cmds())
						post_lockstep(32, sel_ptr, sel_n, 0, 0, heal_id, qbit);
					else
						sim->order_heal(sel_ptr, sel_n, heal_id, queue_shift);
					play_civ_voice("heal");
					return;
				}
			}

			// 1d. A Gaia treasure: collect it (0 A.D. unit_actions "collect-treasure").
			{
				uint32_t treasure_id = 0;
				float best_td2 = 2.0f * 2.0f;
				for (const auto &t : sim->treasures()) {
					if (!sim->treasure_seen_by(local_player(), t)) continue; // none in the shroud
					const float dx = t.x - click_pos.x, dz = t.z - click_pos.z;
					if (near_pick(t.id, dx * dx + dz * dz, best_td2)) { best_td2 = dx * dx + dz * dz; treasure_id = t.id; }
				}
				if (treasure_id != 0) {
					if (turn_cmds())
						post_lockstep(17, sel_ptr, sel_n, 0, 0, treasure_id, (uint8_t)(2 | qbit));
					else
						sim->order_collect_treasure(sel_ptr, sel_n, treasure_id, queue_shift);
					play_civ_voice("collect_treasure");
					return;
				}
			}

			// 2. Right click on Enemy Unit (Attack)
			uint32_t clicked_enemy_id = 0;
			float best_ed2 = 2.2f * 2.2f;
			for (size_t i = 0; i < uview.count; ++i) {
				// unit_actions "attack" needs an enemy (Attack.js CanAttack IsEnemy): an ally or a
				// neutral unit gets "move", a walk to the click.
				if (!uview.alive[i] || !sim->is_hostile(local_player(), uview.team[i])) continue;
				// PickEntityAtPoint: only units the player sees (none in the fog or the shroud).
				if (!rtso::los_sees_unit(*sim, local_player(), uview.pos_x[i], uview.pos_z[i], uview.team[i])) continue;
				float dx = uview.pos_x[i] - click_pos.x;
				float dz = uview.pos_z[i] - click_pos.z;
				float d2 = dx * dx + dz * dz;
				if (near_pick(uview.id[i], d2, best_ed2)) {
					best_ed2 = d2;
					clicked_enemy_id = uview.id[i];
				}
			}
			if (clicked_enemy_id != 0) {
				pick_one([&](uint32_t id) { return sim->unit_id_can_attack(id, clicked_enemy_id); });
				if (turn_cmds())
					post_lockstep(3, sel_ptr, sel_n, 0, 0, clicked_enemy_id, qbit);
				else
					sim->order_attack(sel_ptr, sel_n, clicked_enemy_id, queue_shift);
				play_civ_voice("attack");
				return;
			}

			// 3. Right click on Wildlife Fauna (Hunting vs Herding into Corral)
			auto fview = sim->fauna();
			uint32_t clicked_fauna_id = 0;
			uint8_t clicked_fauna_type = 0;
			float best_fd2 = 2.5f * 2.5f;
			size_t best_f = 0;
			for (size_t f = 0; f < fview.count; ++f) {
				if (!fview.alive[f]) continue;
				if (!rtso::los_sees_fauna(*sim, local_player(), fview.pos_x[f], fview.pos_z[f], fview.owner[f], fview.fauna_type[f])) continue;
				float dx = fview.pos_x[f] - click_pos.x;
				float dz = fview.pos_z[f] - click_pos.z;
				float d2 = dx * dx + dz * dz;
				if (near_pick(fview.id[f], d2, best_fd2)) {
					best_fd2 = d2;
					clicked_fauna_id = fview.id[f];
					clicked_fauna_type = fview.fauna_type[f];
					best_f = f;
				}
			}
			if (clicked_fauna_id != 0) {
				const auto &finfo = rtso::fauna_type_info(clicked_fauna_type);
				// 0 A.D.: a right-click on livestock slaughters it, whoever owns it. RTS0 can't select
				// animals, so Alt+right-click on your own livestock herds it to your corral instead.
				uint8_t herd = finfo.is_domestic() && fview.behavior[best_f] != rtso::FAUNA_CORRALLED &&
				               fview.owner[best_f] == local_player() && Input::get_singleton()->is_key_pressed(KEY_ALT) ? 1 : 0;
				if (turn_cmds())
					post_lockstep(17, sel_ptr, sel_n, 0, 0, clicked_fauna_id, herd ? herd : qbit); // Shift queues a hunt
				else if (herd)
					sim->order_herd(sel_ptr, sel_n, clicked_fauna_id);
				else
					sim->order_hunt(sel_ptr, sel_n, clicked_fauna_id, queue_shift);
				play_civ_voice(herd ? "gather" : "attack");
				return;
			}

			// Right click on Gaia scenery (a fence, column, obelisk): "attack" (9) before "capture" (10);
			// Capturable scenery (benches and fences) is captured with session.capture.
			if (const uint32_t sid = scenery_at(click_pos.x, click_pos.z); sid != 0 && near_pick(sid, 0.0f, 1.0f)) {
				send_structure_click(scenery_click(sid), 0, sid);
				return;
			}

			// 3. Right click on Resource Node (Gather): the supply as the player knows it (G/input.js
			// picks Fogging mirages too), the hover's list. One used up in the fog is gone from the
			// sim: its units gather the same kind near where it was (FINDINGNEWTARGET).
			auto rview = sim->resources();
			uint32_t clicked_res_id = 0;
			const rtso::FoggedResource *gone = nullptr;
			float best_rd2 = 2.2f * 2.2f;
			const auto live_supply = [&](uint32_t id) {
				for (size_t r = 0; r < rview.count; ++r)
					if (rview.id[r] == id) return rview.alive[r] && rview.amount[r] > 0;
				return false;
			};
			if (view) {
				for (const auto &r : view->resource_mirages()) {
					if (r.amount <= 0 || r.subtype == rtso::RES_SUB_GRAIN) continue; // fields: the structure's click
					const float dx = r.x - click_pos.x, dz = r.z - click_pos.z;
					if (!near_pick(r.id, dx * dx + dz * dz, best_rd2)) continue;
					best_rd2 = dx * dx + dz * dz;
					clicked_res_id = r.id;
					gone = &r;
				}
				if (clicked_res_id != 0 && live_supply(clicked_res_id)) gone = nullptr;
			} else {
				for (size_t r = 0; r < rview.count; ++r) {
					if (!rview.alive[r] || rview.amount[r] <= 0) continue;
					float dx = rview.x[r] - click_pos.x;
					float dz = rview.z[r] - click_pos.z;
					float d2 = dx * dx + dz * dz;
					if (near_pick(rview.id[r], d2, best_rd2)) {
						best_rd2 = d2;
						clicked_res_id = rview.id[r];
					}
				}
			}
			if (gone) {
				const uint32_t kind = (uint32_t)gone->type | ((uint32_t)gone->subtype << 8);
				if (turn_cmds())
					post_lockstep((uint8_t)rtso::NetCommandType::NET_CMD_GATHER_NEAR, sel_ptr, sel_n, gone->x, gone->z, kind, qbit);
				else
					sim->order_gather_near(sel_ptr, sel_n, gone->x, gone->z, gone->type, gone->subtype, queue_shift);
				play_civ_voice("gather");
				return;
			}
			// unit_actions "gather" needs a unit that can gather it; else the click falls through (move).
			auto gathers = [&](uint32_t res) { return [&, res](uint32_t id) { return sim->any_can_gather(&id, 1, res); }; };
			if (clicked_res_id != 0 && !able_any(gathers(clicked_res_id))) clicked_res_id = 0;
			if (clicked_res_id != 0) {
				pick_one(gathers(clicked_res_id));
				if (turn_cmds())
					post_lockstep(4, sel_ptr, sel_n, 0, 0, clicked_res_id, qbit);
				else
					sim->order_gather(sel_ptr, sel_n, clicked_res_id, queue_shift);
				play_civ_voice("gather");
				return;
			}

			// 4. Right click on Building (Attack Enemy vs Build/Repair/Garrison Allied)
			for (const auto &b : sim->buildings()) {
				if (!sim->player_can_target_building(local_player(), b.id)) continue; // never seen: no mirage
				float dx = b.x - click_pos.x;
				float dz = b.z - click_pos.z;
				float hit_r = b.half_size + 1.2f;
				if (picked != 0 ? b.id == picked : dx * dx + dz * dz <= hit_r * hit_r) {
					// A finished field's grain, whoever owns it (0 A.D. "gather" has no owner check
					// and its specificness 1 beats capture, attack and repair).
					const uint32_t field_grain = b.type == rtso::BLD_FIELD ? sim->field_resource_id(b.id) : 0;
					const bool friendly = b.team == local_player() || sim->is_ally(local_player(), b.team);
					auto occupy_turret = [&]() {
						play_civ_voice("garrison"); // unit_actions.js "occupy-turret": order_garrison
						if (turn_cmds())
							post_lockstep(12, sel_ptr, sel_n, 0, 0, b.id, (uint8_t)(qbit | rtso::NET_PARAM_OCCUPY_TURRET));
						else
							sim->order_garrison(sel_ptr, sel_n, b.id, queue_shift, true);
					};
					if (force_garrison && friendly && selection_can_garrison(b.id)) {
						// session.garrison is a hotkeyActionCheck: it wins over returning
						// resources and repairing (unit_actions.js "garrison", order_garrison: said
						// before a unit at the door goes inside and leaves the world).
						play_civ_voice("garrison");
						if (turn_cmds())
							post_lockstep(12, sel_ptr, sel_n, 0, 0, b.id, qbit);
						else
							sim->order_garrison(sel_ptr, sel_n, b.id, queue_shift);
					} else if (force_turret && friendly && selection_can_occupy_turret(b.id)) {
						occupy_turret();
					} else if (force_repair && friendly && (b.build_progress < 1.0f || b.hp < b.max_hp) && able_any(builds)) {
						// session.repair likewise wins over returning resources (unit_actions.js "repair").
						pick_one(builds);
						if (turn_cmds())
							post_lockstep(16, sel_ptr, sel_n, 0, 0, b.id, qbit);
						else
							sim->order_build_or_repair(sel_ptr, sel_n, b.id, queue_shift);
						// unit_actions.js "repair": order_build on a foundation, else order_repair
						play_civ_voice(b.build_progress < 1.0f ? "build" : "repair");
					} else if (trade_route_click(b, true, queue_shift, nullptr)) {
						// setup-trade-route has specificness 0: it wins over garrison and capture.
					} else if (field_grain && able_any(gathers(field_grain))) { // any player's finished field
						pick_one(gathers(field_grain));
						if (turn_cmds())
							post_lockstep(4, sel_ptr, sel_n, 0, 0, field_grain, qbit);
						else
							sim->order_gather(sel_ptr, sel_n, field_grain, queue_shift);
						play_civ_voice("gather");
					} else if (b.team != local_player() && sim->is_hostile(local_player(), b.team) &&
							structure_click(b.id) != CLICK_NONE) {
						// "attack" (9, allowCapture false) before "capture" (10); C captures.
						send_structure_click(structure_click(b.id), b.id, 0);
					} else if (structure_click(b.id) == CLICK_CAPTURE && !able_any([&](uint32_t id) {
								   return sim->can_return_resource(id, b.id);
							   })) {
						// "capture" (10) on an own, allied or neutral structure an enemy holds points of
						// (Capturable.CanCapture): recapture, before "repair" (11) and "move" (12).
						send_structure_click(CLICK_CAPTURE, b.id, 0);
					} else if (b.team != local_player() && !sim->is_ally(local_player(), b.team)) {
						break; // neutral: walk there
					} else if (b.team != local_player()) {
						// An ally's structure: return resources to its shared dropsite ("returnresource", 2,
						// Diaspora), else repair it, else walk there ("move", 12, beats "garrison", 20,
						// which needs session.garrison).
						auto returns_here = [&](uint32_t id) { return sim->can_return_resource(id, b.id); };
						if (able_any(returns_here)) {
							pick_one(returns_here);
							if (turn_cmds())
								post_lockstep(34, sel_ptr, sel_n, 0, 0, b.id, qbit);
							else
								sim->order_return_resource(sel_ptr, sel_n, b.id, queue_shift);
							play_civ_voice("gather");
						} else if ((b.build_progress < 1.0f || b.hp < b.max_hp) && able_any(builds)) { // "repair" needs a builder
							pick_one(builds);
							if (turn_cmds())
								post_lockstep(16, sel_ptr, sel_n, 0, 0, b.id, qbit);
							else
								sim->order_build_or_repair(sel_ptr, sel_n, b.id, queue_shift);
							play_civ_voice(b.build_progress < 1.0f ? "build" : "repair");
						} else {
							break; // nothing to do there: walk
						}
					} else {
						// 0 A.D. "returnresource" comes first: carriers take their load to this dropsite.
						auto returns_here = [&](uint32_t id) { return sim->can_return_resource(id, b.id); };
						if (able_any(returns_here)) {
							pick_one(returns_here);
							if (turn_cmds())
								post_lockstep(34, sel_ptr, sel_n, 0, 0, b.id, qbit);
							else
								sim->order_return_resource(sel_ptr, sel_n, b.id, queue_shift);
							play_civ_voice("gather");
						} else if ((b.build_progress < 1.0f || b.hp < b.max_hp || b.type == rtso::BLD_FIELD) && able_any(builds)) {
							// finished fields: farm; "repair" needs a builder, else the click walks there
							pick_one(builds);
							if (turn_cmds())
								post_lockstep(16, sel_ptr, sel_n, 0, 0, b.id, qbit);
							else
								sim->order_build_or_repair(sel_ptr, sel_n, b.id, queue_shift);
							play_civ_voice(b.build_progress < 1.0f ? "build" : b.type == rtso::BLD_FIELD ? "gather" : "repair");
						} else {
							break; // "move" (12) beats "garrison" (20) without session.garrison: walk
						}
					}
					return;
				}
			}

			walk_there();
		}
	}

	Ref<InputEventMouseMotion> mm = event;
	if (mm.is_valid() && right_line_.active && sim) {
		// positionUnitsFreehandSelectionMouseMove: a point at least 1 m (0.25) from the last.
		const Vector2 at = mm->get_position();
		if (!right_line_.drawing && right_line_.start.distance_to(at) >= user_opts_.drag_delta) {
			right_line_.drawing = true;
			const Vector3 g = get_ground_click_pos(right_line_.start);
			right_line_.pts.push_back(Vector2(g.x, g.z));
		}
		if (right_line_.drawing) {
			const Vector3 g = get_ground_click_pos(at);
			if (right_line_.pts.empty() || right_line_.pts.back().distance_squared_to(Vector2(g.x, g.z)) >= 0.25f * 0.25f)
				right_line_.pts.push_back(Vector2(g.x, g.z));
		}
	}
	if (mm.is_valid() && dragging) {
		sel_rect->set_rect(drag_start, mm->get_position(), true);
	}
	// input.js INPUT_BUILDING_PLACEMENT mousemotion: placing a structure (not a wall) that the
	// player can no longer afford stops placement on the next mouse move.
	if (mm.is_valid() && build_mode && sim && current_build_type != rtso::BLD_WALL_SEGMENT &&
			current_build_type != rtso::BLD_PALISADE && current_build_type != rtso::BLD_SIEGE_WALL) {
		float cost[rtso::RES_COUNT];
		sim->structure_cost_for(local_player(), (uint8_t)current_build_type, cost);
		const auto &ps = sim->player(local_player());
		for (int r = 0; r < rtso::RES_COUNT; ++r)
			if (ps.res[r] < cost[r]) {
				enter_build_mode(false);
				break;
			}
	}
}

bool RtsoMain::try_place_wall(const Vector3 &end, bool queued) {
	if (!sim) return false;
	// Commands.js "construct-wall": the sim lays the pieces (helpers/Walls.js) and snaps the ends
	// onto the player's wall towers, the same on every peer. Alt hands the whole wall to one
	// builder (popOneFromSelection).
	const uint32_t one_builder = rtso::hotkeys::held("session.orderone") ? first_selected_builder() : 0;
	const bool order_one = one_builder != 0;
	const uint32_t *builders = order_one ? &one_builder : (const uint32_t *)selected_ids.ptr();
	const size_t builder_n = order_one ? 1 : (size_t)selected_ids.size();
	const uint8_t wall_kind = rtso::wallset_kind_of((uint8_t)current_build_type);
	// GuiInterface.js SetWallPlacementPreview: the wall is placeable when its first piece is.
	uint32_t snapped[2] = {0, 0};
	const std::vector<rtso::Simulation::WallPiece> pieces = sim->wall_placement(local_player(), wall_drag_start_pos.x,
			wall_drag_start_pos.z, end.x, end.z, &snapped[0], &snapped[1], wall_kind);
	// tryPlaceWall (input.js:384-403): no segment (a wall no longer than a tower) posts nothing
	// and still ends the placement.
	if (pieces.empty()) return true;
	if (sim->check_wall_piece(local_player(), pieces[0], snapped, 2) != rtso::Simulation::PLACE_OK)
		return false;
	// hasWallSegment: the preview holds the placeable pieces up to the first blocked one; with
	// only a tower among them nothing is sent and no voice plays, and placement still ends.
	{
		const rtso::WallSetInfo ws = rtso::wallset_info(wall_kind, sim->team_civ(local_player()));
		bool segment = false;
		for (const auto &p : pieces) {
			if (sim->check_wall_piece(local_player(), p, snapped, 2) != rtso::Simulation::PLACE_OK) break;
			if (!(p.type == ws.tower && p.half_w == ws.tower_half)) { segment = true; break; }
		}
		if (!segment) return true;
	}
	if (turn_cmds()) {
		post_lockstep((uint8_t)rtso::NetCommandType::NET_CMD_BUILD_WALL, builders, builder_n, end.x, end.z,
				rtso::Simulation::pack_wall_point(wall_drag_start_pos.x, wall_drag_start_pos.z),
				(uint8_t)(wall_kind | (queued ? rtso::NET_PARAM_QUEUED : 0)));
	} else if (sim->cmd_build_wall(local_player(), wall_drag_start_pos.x, wall_drag_start_pos.z, end.x, end.z, builders,
					   builder_n, queued, wall_kind) == 0) {
		return false;
	}
	play_civ_voice("build");
	if (order_one) pop_from_selection(selected_ids, one_builder);
	return true;
}

uint32_t RtsoMain::guard_target_at(const Vector3 &at) const {
	if (!sim) return 0;
	const uint8_t me = local_player();
	auto uv = sim->units();
	uint32_t best = 0;
	float best_d2 = 3.0f * 3.0f;
	for (size_t i = 0; i < uv.count; ++i) {
		if (!uv.alive[i] || (uv.team[i] != me && !sim->is_ally(me, uv.team[i])) || selected_ids.has((int32_t)uv.id[i]))
			continue;
		const float dx = uv.pos_x[i] - at.x, dz = uv.pos_z[i] - at.z, d2 = dx * dx + dz * dz;
		if (d2 < best_d2) { best_d2 = d2; best = uv.id[i]; }
	}
	if (!best)
		for (const auto &b : sim->buildings()) {
			if (b.wrecked || (b.team != me && !sim->is_ally(me, b.team))) continue;
			const float dx = b.x - at.x, dz = b.z - at.z, hit_r = b.half_size + 1.2f;
			if (dx * dx + dz * dz <= hit_r * hit_r) { best = b.id; break; }
		}
	// getActionInfo "guard": only when a selected unit may guard it (UnitAI canGuard).
	if (best && !sim->any_can_guard((const uint32_t *)selected_ids.ptr(), (size_t)selected_ids.size(), best)) return 0;
	return best;
}

bool RtsoMain::selection_guarding() const {
	if (!sim) return false;
	for (int s = 0; s < selected_ids.size(); ++s)
		if (sim->unit_is_guarding((uint32_t)selected_ids[s])) return true;
	return false;
}

// unit_actions.js actionCheck of the armed action: whether the click under the cursor is a
// target it takes (else the click does nothing and the action stays armed).
bool RtsoMain::armed_target_ok(const Vector3 &at) const {
	if (!(garrison_mode || turret_mode || repair_mode || guard_mode)) return true; // ground actions
	if (guard_mode) return guard_target_at(at) != 0;
	const uint8_t me = local_player();
	for (const auto &b : sim->buildings()) {
		if (b.wrecked || (b.team != me && !sim->is_ally(me, b.team))) continue;
		const float dx = b.x - at.x, dz = b.z - at.z, hit_r = b.half_size + 1.2f;
		if (dx * dx + dz * dz > hit_r * hit_r) continue;
		if (garrison_mode) return selection_can_garrison(b.id) || transport_under(at.x, at.z);
		if (turret_mode) return selection_can_occupy_turret(b.id);
		return b.build_progress < 1.0f || b.hp < b.max_hp; // repair
	}
	if (garrison_mode) return transport_under(at.x, at.z) != 0;
	if (repair_mode) {
		auto uv = sim->units();
		for (size_t i = 0; i < uv.count; ++i) {
			if (!uv.alive[i]) continue;
			const float dx = uv.pos_x[i] - at.x, dz = uv.pos_z[i] - at.z;
			if (dx * dx + dz * dz <= 2.2f * 2.2f && uv.hp[i] < uv.max_hp[i] &&
					(uv.team[i] == me || sim->is_ally(me, uv.team[i])) && sim->is_repairable_unit(uv.id[i]))
				return true;
		}
	}
	return false;
}

// input.js INPUT_PRESELECTEDACTION left button: run the armed action at the click (as the
// order path of a right click), keep it armed under Shift (session.queue) or Alt (orderone),
// and select nothing. Returns false when the selection is gone, which clears the action.
bool RtsoMain::run_armed_click(const Ref<InputEventMouseButton> &press) {
	if (!sim || selected_ids.is_empty() || !orders_allowed()) {
		attack_move_mode = patrol_mode = guard_mode = call_to_arms_mode = garrison_mode = turret_mode = repair_mode = false;
		return false;
	}
	const bool keep = rtso::hotkeys::held("session.queue") || rtso::hotkeys::held("session.orderone");
	// The -disabled cursor ("none"): nothing happens, and the armed action still ends unless
	// Shift or Alt is held (input.js INPUT_PRESELECTEDACTION).
	if (!armed_target_ok(get_ground_click_pos(press->get_position()))) {
		if (!keep) attack_move_mode = patrol_mode = guard_mode = call_to_arms_mode = garrison_mode = turret_mode = repair_mode = false;
		return true;
	}
	const bool modes[7] = { attack_move_mode, patrol_mode, guard_mode, call_to_arms_mode, garrison_mode, turret_mode, repair_mode };
	Ref<InputEventMouseButton> right = press->duplicate();
	right->set_button_index(MOUSE_BUTTON_RIGHT);
	right->set_button_mask(MOUSE_BUTTON_MASK_RIGHT);
	replaying_right_ = true;
	_unhandled_input(right);
	replaying_right_ = false;
	attack_move_mode = patrol_mode = guard_mode = call_to_arms_mode = garrison_mode = turret_mode = repair_mode = false;
	if (keep && !selected_ids.is_empty()) {
		attack_move_mode = modes[0], patrol_mode = modes[1], guard_mode = modes[2], call_to_arms_mode = modes[3];
		garrison_mode = modes[4], turret_mode = modes[5], repair_mode = modes[6];
	}
	return true;
}

// positionUnitsFreehandSelectionMouseUp: a short or no drag gives the usual right-click order
// at the release point; a line of at least 8 m (2.0) with two or more units spreads them evenly
// along it (walk-custom, or attack-walk-custom with session.attackmove), out of formation.
void RtsoMain::finish_right_line(const Ref<InputEventMouseButton> &release) {
	RightLine line = std::move(right_line_);
	right_line_ = RightLine();
	auto replay = [&]() {
		Ref<InputEventMouseButton> press = release->duplicate();
		press->set_pressed(true);
		replaying_right_ = true;
		_unhandled_input(press);
		replaying_right_ = false;
	};
	if (!sim || !line.drawing || !orders_allowed()) {
		replay();
		return;
	}
	float length = 0.0f;
	for (size_t i = 1; i < line.pts.size(); ++i) length += line.pts[i].distance_to(line.pts[i - 1]);
	// Units and owned livestock (UnitAI), by id.
	std::vector<uint32_t> ents;
	std::unordered_map<uint32_t, Vector2> pos;
	auto uv = sim->units();
	auto fv = sim->fauna();
	for (int s = 0; s < selected_ids.size(); ++s) {
		const uint32_t id = (uint32_t)selected_ids[s];
		for (size_t i = 0; i < uv.count; ++i)
			if (uv.id[i] == id && uv.alive[i]) pos[id] = Vector2(uv.pos_x[i], uv.pos_z[i]);
		if (!pos.count(id))
			for (size_t f = 0; f < fv.count; ++f)
				if (fv.id[f] == id && fv.alive[f]) pos[id] = Vector2(fv.pos_x[f], fv.pos_z[f]);
		if (pos.count(id)) ents.push_back(id);
	}
	std::sort(ents.begin(), ents.end());
	if (length < 2.0f || ents.size() < 2) {
		replay();
		return;
	}
	const size_t n = ents.size();
	const float between = length / (float)(n - 1);
	std::vector<Vector2> dist{ line.pts[0] };
	Vector2 p0 = line.pts[0];
	float free_dist = -between;
	for (size_t i = 1; i < line.pts.size(); ++i) {
		const Vector2 p1 = line.pts[i];
		free_dist += line.pts[i - 1].distance_to(p1);
		while (free_dist >= 0.0f) {
			p0 = (p0 - p1).normalized() * free_dist + p1;
			dist.push_back(p0);
			free_dist -= between;
		}
	}
	// Rounding errors can lead to missing or too many points.
	dist.resize(std::min(dist.size(), n));
	while (dist.size() < n) dist.push_back(line.pts.back());
	const Vector2 first = pos[ents.front()], last = pos[ents.back()];
	if (first.distance_to(dist.front()) + last.distance_to(dist.back()) >
			first.distance_to(dist.back()) + last.distance_to(dist.front()))
		std::reverse(dist.begin(), dist.end());

	last_line_targets_.clear();
	last_line_entities_.clear();
	for (size_t k = 0; k < n; ++k) {
		last_line_targets_.push_back(dist[k]);
		last_line_entities_.push_back((int32_t)ents[k]);
	}
	const bool units_only = rtso::hotkeys::held("session.attackmoveUnit");
	const bool attack = units_only || rtso::hotkeys::held("session.attackmove") || attack_move_mode;
	attack_move_mode = false;
	const bool queued = rtso::hotkeys::held("session.queue");
	const uint8_t qbit = queued ? rtso::NET_PARAM_QUEUED : 0;
	if (turn_cmds()) {
		// formation NULL_FORMATION: the units leave their formations first.
		std::vector<uint32_t> formed;
		for (const auto &fc : sim->formations())
			for (uint32_t m : fc.members)
				if (std::find(ents.begin(), ents.end(), m) != ents.end()) formed.push_back(m);
		if (!formed.empty()) {
			rtso::NetCommand leave;
			leave.cmd_type = (uint8_t)rtso::NetCommandType::NET_CMD_FORMATION;
			leave.entity_ids = std::move(formed);
			leave.param = rtso::FORMATION_NONE;
			turn_mgr_.post_local_command(leave);
		}
		for (size_t k = 0; k < n; ++k) {
			rtso::NetCommand c;
			c.cmd_type = attack ? 10 : (uint8_t)rtso::NetCommandType::NET_CMD_MOVE;
			c.entity_ids.assign(1, ents[k]);
			c.target_x = dist[k].x;
			c.target_z = dist[k].y;
			// unit_actions.js "attack-move" sends no allowCapture: UnitAI.DEFAULT_CAPTURE (false).
			c.param = (uint8_t)(qbit | (attack ? rtso::NET_PARAM_NO_CAPTURE : 0) | (attack && units_only ? rtso::NET_PARAM_UNITS_ONLY : 0));
			turn_mgr_.post_local_command(c);
		}
	} else {
		for (size_t k = 0; k < n; ++k) {
			if (attack)
				sim->order_attack_move(&ents[k], 1, dist[k].x, dist[k].y, queued, units_only, false);
			else {
				sim->order_walk(&ents[k], 1, dist[k].x, dist[k].y, queued, true);
				sim->order_fauna_walk(local_player(), &ents[k], 1, dist[k].x, dist[k].y);
			}
		}
	}
	// Target markers at least 5 m (1.25) apart.
	const size_t step = std::max<size_t>(1, (size_t)std::ceil(1.25f / std::max(between, 0.001f)));
	for (size_t k = 0; k < n; k += step) range_overlays_.add_target_marker(dist[k].x, dist[k].y);
	play_civ_voice(attack ? "attack" : "move");
}

// getPreferredEntities for a box with no units: the player's finished defensive structures in it
// (towers, walls, fortresses), else all of its structures there.
std::vector<uint32_t> RtsoMain::band_box_structures(Vector2 a, Vector2 b) const {
	std::vector<uint32_t> defensive, all;
	Camera3D *cam = cam_rig ? cam_rig->get_camera() : nullptr;
	if (!sim || !cam) return all;
	if (rtso::hotkeys::held("selection.militaryonly") || rtso::hotkeys::held("selection.nonmilitaryonly") ||
			rtso::hotkeys::held("selection.idleonly") || rtso::hotkeys::held("selection.woundedonly"))
		return all;
	const Vector2 min_p(std::min(a.x, b.x), std::min(a.y, b.y));
	const Vector2 max_p(std::max(a.x, b.x), std::max(a.y, b.y));
	for (const auto &bl : sim->buildings()) {
		// PickPlayerEntitiesInRect(..., -1) for an observer's all-players view: anyone's.
		if ((observer_all_view() ? bl.team >= rtso::MAX_PLAYERS : bl.team != local_player()) || bl.wrecked || bl.hp <= 0.0f) continue;
		const Vector3 wp(bl.x, sim->get_terrain_height(bl.x, bl.z), bl.z);
		if (cam->is_position_behind(wp)) continue;
		const Vector2 sp = cam->unproject_position(wp);
		if (sp.x < min_p.x || sp.x > max_p.x || sp.y < min_p.y || sp.y > max_p.y) continue;
		all.push_back(bl.id);
		if ((rtso::building_tech_classes(bl.type) & rtso::TC_DEFENSIVE) != 0) defensive.push_back(bl.id);
	}
	return defensive.empty() ? all : defensive;
}

// The local player's units (with their formation mates) and livestock whose screen position
// lies in the box from `a` to `b`, under the held selection filters (input.js
// PickPlayerEntitiesInRect + getPreferredEntities).
std::vector<int32_t> RtsoMain::band_box_ids(Vector2 a, Vector2 b) const {
	std::vector<int32_t> out;
	Camera3D *cam = cam_rig ? cam_rig->get_camera() : nullptr;
	if (!sim || !cam) return out;
	const Vector2 min_p(std::min(a.x, b.x), std::min(a.y, b.y));
	const Vector2 max_p(std::max(a.x, b.x), std::max(a.y, b.y));
	// input.js getPreferredEntities: one filter, the last held of military, non-military,
	// idle and wounded.
	const bool wounded_only = rtso::hotkeys::held("selection.woundedonly");
	const bool idle_only = !wounded_only && rtso::hotkeys::held("selection.idleonly");
	const bool nonmilitary_only = !wounded_only && !idle_only && rtso::hotkeys::held("selection.nonmilitaryonly");
	const bool military_only = !wounded_only && !idle_only && !nonmilitary_only && rtso::hotkeys::held("selection.militaryonly");
	const float water = sim->water_height() > -0.5f ? sim->water_height() + rtso::NAV_WATER_MARGIN : -1e9f;
	auto in_box = [&](float x, float z, bool afloat = false) {
		// The entity as drawn, on the ground or a ship on the sea (PickPlayerEntitiesInRect
		// projects its position).
		const float ground = sim->get_terrain_height(x, z);
		const Vector3 world_pos(x, afloat ? std::max(ground, water) : ground, z);
		if (cam->is_position_behind(world_pos)) return false;
		const Vector2 sp = cam->unproject_position(world_pos);
		return sp.x >= min_p.x && sp.x <= max_p.x && sp.y >= min_p.y && sp.y <= max_p.y;
	};
	auto uview = sim->units();
	for (size_t i = 0; i < uview.count; ++i) {
		// PickPlayerEntitiesInRect(..., g_ViewedPlayer): own units only, Control all units or not
		// (only a click or Shift-click takes another player's, selection.js addList force).
		if (!uview.alive[i]) continue;
		if (observer_all_view() ? uview.team[i] >= rtso::MAX_PLAYERS ||
						!rtso::los_sees_unit(*sim, view_player(), uview.pos_x[i], uview.pos_z[i], uview.team[i])
				: uview.team[i] != local_player())
			continue;
		const rtso::TechClasses cls = rtso::unit_tech_classes(uview.unit_type[i]);
		const bool military = (cls & (rtso::TC_MELEE | rtso::TC_RANGED)) != 0;
		if ((military_only && !military) || (nonmilitary_only && military)) continue;
		if (idle_only && uview.action_state[i] != rtso::ACTION_IDLE) continue;
		// gui.session.woundedunithotkeythreshold = 33 % (input.js isWounded: 100 hp <= max x threshold).
		if (wounded_only && uview.hp[i] > uview.max_hp[i] * user_opts_.wounded_fraction) continue;
		if (!in_box(uview.pos_x[i], uview.pos_z[i], rtso::unit_is_ship(uview.unit_type[i]))) continue;
		for (int32_t uid : formation_mates((int32_t)uview.id[i]))
			if (std::find(out.begin(), out.end(), uid) == out.end()) out.push_back(uid);
	}
	// The player's own livestock in the box (not with the military or idle filters, which pick
	// soldiers and workers; the wounded one takes any Unit, livestock too).
	auto fview = sim->fauna();
	for (size_t f = 0; f < fview.count && !military_only && !idle_only; ++f) {
		if (!sim->fauna_selectable_by(local_player(), fview.id[f])) continue;
		if (wounded_only && fview.hp[f] > fview.max_hp[f] * user_opts_.wounded_fraction) continue;
		if (in_box(fview.pos_x[f], fview.pos_z[f])) out.push_back((int32_t)fview.id[f]);
	}
	return out;
}

// The local player's unit whose drawn box the mouse ray meets first, else the one nearest
// the ground point under the mouse (within `radius`), or SIZE_MAX; a structure the ray
// reaches first hides the units behind it. With
// `any_player`, any unit the viewed player sees (Engine.PickEntityAtPoint).
size_t RtsoMain::own_unit_under_mouse(Vector2 mouse_pos, bool any_player, float radius) const {
	if (!sim) return (size_t)-1;
	const Vector3 click_pos = get_ground_click_pos(mouse_pos);
	auto uview = sim->units();
	float bld_t = 1e30f;
	building_under_mouse(mouse_pos, &bld_t, any_player);
	Camera3D *pick_cam = cam_rig ? cam_rig->get_camera() : nullptr;
	const Vector3 ray_o = pick_cam ? pick_cam->project_ray_origin(mouse_pos) : Vector3();
	const Vector3 ray_d = pick_cam ? pick_cam->project_ray_normal(mouse_pos) : Vector3();
	auto pickable = [&](size_t i) {
		if (!uview.alive[i]) return false;
		return any_player ? rtso::los_sees_unit(*sim, view_player(), uview.pos_x[i], uview.pos_z[i], uview.team[i])
		                  : uview.team[i] == local_player();
	};
	// CCmpSelectable's selection box, as PickEntitiesAtPoint tests it: the ray against each
	// drawn unit's footprint box up to its model height (a soldier on a wall, a tall siege
	// tower or an elephant's body), the nearest along the ray; a structure in front hides it.
	if (pick_cam && view) {
		size_t hit_idx = (size_t)-1;
		float hit_t = std::min(bld_t + 0.5f, 1e30f);
		for (const auto &b : view->unit_pick_boxes()) {
			// The ray in the box's frame: feet at the origin, turned by -yaw.
			const Vector3 o = ray_o - b.base;
			const float c = std::cos(-b.yaw), sn = std::sin(-b.yaw);
			const Vector3 lo(c * o.x + sn * o.z, o.y, -sn * o.x + c * o.z);
			const Vector3 ld(c * ray_d.x + sn * ray_d.z, ray_d.y, -sn * ray_d.x + c * ray_d.z);
			const float mn[3] = { -b.half_w, 0.0f, -b.half_d }, mx[3] = { b.half_w, b.height, b.half_d };
			float t0 = 0.0f, t1 = hit_t;
			bool miss = false;
			for (int a = 0; a < 3 && !miss; ++a) {
				const float org = lo[a], dir = ld[a];
				if (std::abs(dir) < 1e-6f) {
					miss = org < mn[a] || org > mx[a];
					continue;
				}
				float ta = (mn[a] - org) / dir, tb = (mx[a] - org) / dir;
				if (ta > tb) std::swap(ta, tb);
				t0 = std::max(t0, ta);
				t1 = std::min(t1, tb);
				miss = t0 > t1;
			}
			if (miss || t0 >= hit_t) continue;
			size_t i = 0;
			while (i < uview.count && uview.id[i] != b.id) ++i;
			if (i == uview.count || !pickable(i)) continue;
			hit_t = t0;
			hit_idx = i;
		}
		if (hit_idx != (size_t)-1) return hit_idx;
	}
	size_t best_uidx = (size_t)-1;
	float best_ud2 = radius * radius;
	for (size_t i = 0; i < uview.count; ++i) {
		if (!uview.alive[i]) continue;
		// Distance first: it rules out nearly every unit before the costlier checks.
		const float dx = uview.pos_x[i] - click_pos.x, dz = uview.pos_z[i] - click_pos.z;
		const float d2 = dx * dx + dz * dz;
		if (d2 >= best_ud2) continue;
		if (any_player ? !rtso::los_sees_unit(*sim, view_player(), uview.pos_x[i], uview.pos_z[i], uview.team[i])
		               : uview.team[i] != local_player())
			continue;
		if (pick_cam && bld_t < 1e29f) {
			const Vector3 up(uview.pos_x[i], sim->get_terrain_height(uview.pos_x[i], uview.pos_z[i]), uview.pos_z[i]);
			if ((up - ray_o).dot(ray_d) > bld_t + 0.5f) continue;
		}
		{
			best_ud2 = d2;
			best_uidx = i;
		}
	}
	return best_uidx;
}

size_t RtsoMain::click_pick_unit(Vector2 mouse_pos, bool any_player) const {
	if (!sim) return (size_t)-1;
	// The unit nearest the cursor within a unit's reach, whoever owns it (a structure in front
	// hides it): selected only when it is the player's (or anyone's, with any_player), so an
	// enemy, ally or animal standing there is not swapped for one of the player's units nearby.
	const size_t u = own_unit_under_mouse(mouse_pos, true, 2.0f);
	if (u == (size_t)-1) return u;
	return sim->units().team[u] == local_player() || any_player ? u : (size_t)-1;
}
