// RtsoMain's input handling (0 A.D. gui/session/input.js and gui/session/hotkeys/):
// hotkeys, control groups, clicks, drag selection and orders. Split out of main_node.cpp
// (PLAN P4.6); the state it reads and writes is RtsoMain's.

#include "main_node.h"
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
static void pop_from_selection(PackedInt32Array &sel, uint32_t id) {
	const int64_t at = sel.find((int32_t)id);
	if (at >= 0) sel.remove_at(at);
}

void RtsoMain::_unhandled_input(const Ref<InputEvent> &event) {
	// Keys go through named 0 A.D. hotkeys (hotkeys.h, config/default.cfg
	// [hotkey.*]; user overrides in user://config/user.cfg). Same-specificity
	// hotkeys can share a key (B = build menu or fifth trainee), so the context
	// below picks between them, like 0 A.D.'s gui/session/hotkeys/*.xml.
	Ref<InputEventKey> key = event;
	const std::vector<std::string> hk = rtso::hotkeys::fired(key);
	if (!hk.empty()) {
		auto fired = [&](const char *name) { return rtso::hotkeys::has(hk, name); };
		auto ids_ptr = [&]() { return (const uint32_t *)selected_ids.ptr(); };

		if (fired("cancel") || fired("selection.cancel")) {
			// 0 A.D. closeOpenDialogs() closes every dialog at once, then
			// clearSelection() (input.js:1709).
			if (close_open_dialogs()) return;
			if (build_mode) enter_build_mode(false);
			else if (attack_move_mode || patrol_mode || guard_mode || call_to_arms_mode || garrison_mode || repair_mode)
				attack_move_mode = patrol_mode = guard_mode = call_to_arms_mode = garrison_mode = repair_mode = false;
			else clear_selection();
			return;
		}

		// While placing a building, letters pick the building to place
		// (RTS0 [hotkey.rts0.build]); the camera scrolls with the arrows only.
		if (build_mode) {
			static const struct { const char *name; uint8_t type; } kBuildKeys[] = {
				{"rts0.build.civilcentre", rtso::BLD_TOWN_CENTER}, {"rts0.build.house", rtso::BLD_HOUSE},
				{"rts0.build.barracks", rtso::BLD_BARRACKS}, {"rts0.build.storehouse", rtso::BLD_STOREHOUSE},
				{"rts0.build.defensetower", rtso::BLD_TOWER}, {"rts0.build.farmstead", rtso::BLD_FARMSTEAD},
				{"rts0.build.market", rtso::BLD_MARKET}, {"rts0.build.fortress", rtso::BLD_FORTRESS},
				{"rts0.build.gate", rtso::BLD_GATE}, {"rts0.build.temple", rtso::BLD_TEMPLE},
				{"rts0.build.wonder", rtso::BLD_WONDER}, {"rts0.build.dock", rtso::BLD_DOCK},
				{"rts0.build.wall", rtso::BLD_WALL_SEGMENT}, {"rts0.build.corral", rtso::BLD_CORRAL},
			};
			for (const auto &bk : kBuildKeys) {
				if (fired(bk.name)) {
					enter_build_mode(true, bk.type);
					return;
				}
			}
			if (build_mode && !wall_drag_mode) {
			constexpr float ROTATION_STEP = 3.14159265f / 12.0f; // 24 presses make a full turn
			if (fired("session.rotate.cw")) { placement_angle += ROTATION_STEP; return; }
			if (fired("session.rotate.ccw")) { placement_angle -= ROTATION_STEP; return; }
		}

		if (fired("rts0.build.menu")) {
				enter_build_mode(false);
				return;
			}
		}

		if (fired("chat")) {
			toggle_chat();
			return;
		}
		if (fired("teamchat") || fired("privatechat")) {
			if (!chat_window_.is_open()) toggle_chat();
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
		for (int g = 1; g <= 10; ++g) {
			const int grp = g % 10;
			const std::string n = std::to_string(g);
			const bool sel = fired(("selection.group.select." + n).c_str());
			const bool save = fired(("selection.group.save." + n).c_str());
			const bool add = fired(("selection.group.add." + n).c_str());
			if (!sel && !save && !add) continue;
			if (save) {
				selected_groups[grp].clear();
				const bool disjoint = rtso::user_config::get_bool("gui.session.disjointcontrolgroups");
				for (int s = 0; s < selected_ids.size(); ++s) {
					for (int o = 0; o < 10 && disjoint; ++o) {
						if (o == grp) continue;
						const int64_t at = selected_groups[o].find(selected_ids[s]);
						if (at >= 0) selected_groups[o].remove_at(at);
					}
					if (!selected_groups[grp].has(selected_ids[s])) selected_groups[grp].push_back(selected_ids[s]);
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
		for (int j = 1; j <= 4; ++j) {
			const std::string n = std::to_string(j);
			if (fired(("camera.jump.set." + n).c_str())) {
				if (cam_rig) camera_bookmarks_[j - 1] = {true, cam_rig->get_target_pos()};
				return;
			}
			if (fired(("camera.jump." + n).c_str())) {
				if (cam_rig && camera_bookmarks_[j - 1].set) cam_rig->set_target_pos(camera_bookmarks_[j - 1].pos);
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
		if (fired("camera.follow")) {
			if (cam_rig) cam_rig->set_follow_id(selected_ids.is_empty() ? 0 : (uint32_t)selected_ids[0]);
			return;
		}
		if (fired("camera.rallypointfocus")) { // unit_actions.js "focus-rally"
			_on_entity_command("focus-rally");
			return;
		}
		if (fired("camera.lastattackfocus")) {
			if (cam_rig && has_last_attack_) cam_rig->set_target_pos(last_attack_pos_);
			return;
		}

		// Unit commands (0 A.D. [hotkey.session], misc.xml).
		if (fired("session.stop")) {
			_on_entity_command("stop");
			return;
		}
		if (fired("session.unload")) {
			_on_ungarrison_pressed();
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
		if (fired("session.attackmoveunit")) {
			if (!selected_ids.is_empty()) {
				attack_move_mode = true;
				patrol_mode = guard_mode = call_to_arms_mode = garrison_mode = repair_mode = false;
			}
			return;
		}

		// Training: Z X C V B N M , queue the selected building's 1st-8th
		// trainee; Shift trains a batch of 5 (gui.session.batchtrainingsize).
		if (selected_building_id != 0 && command_panel_.roster_list()) {
			for (int q = 1; q <= 8; ++q) {
				if (!fired(("session.queueunit." + std::to_string(q)).c_str())) continue;
				if (q - 1 < command_panel_.roster_list()->get_item_count() && !command_panel_.roster_list()->is_item_disabled(q - 1)) {
					const uint16_t tmpl = (uint16_t)(int)command_panel_.roster_list()->get_item_metadata(q - 1);
					try_train_template(selected_building_id, tmpl, train_batch());
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
		// 0 A.D. hotkeys/misc.xml "silhouettes": Renderer_SetSilhouettesEnabled, for this run only.
		if (fired("silhouettes")) {
			rtso::user_config::set("silhouettes", rtso::user_config::get_bool("silhouettes") ? "false" : "true");
			return;
		}
		// OverlayCounterElapsedTime / RemainingCeasefire Hotkey: the top-right counters.
		if (fired("timeelapsedcounter.toggle")) { toggle_overlay("gui.session.timeelapsedcounter"); return; }
		if (fired("ceasefirecounter.toggle")) { toggle_overlay("gui.session.ceasefirecounter"); return; }

		if (fired("fps.toggle")) {
			if (debug_overlay) debug_overlay->set_visible(!debug_overlay->is_visible());
			return;
		}

		if (fired("screenshot")) {
			Viewport *vp = get_viewport();
			if (vp) {
				Ref<ViewportTexture> tex = vp->get_texture();
				if (tex.is_valid()) {
					Ref<Image> img = tex->get_image();
					if (img.is_valid()) {
						DirAccess::make_dir_recursive_absolute("user://screenshots");
						const String path = "user://screenshots/screenshot" +
								String::num_int64((int64_t)Time::get_singleton()->get_unix_time_from_system()) + ".png";
						img->save_png(path);
						print_line("[RTS0] Saved screenshot to " + path);
					}
				}
			}
			return;
		}
	}

	Ref<InputEventMouseButton> mb = event;
	if (mb.is_valid()) {
		Vector2 mouse_pos = mb->get_position();

		if (mb->get_button_index() == MOUSE_BUTTON_LEFT) {
			if (mb->is_pressed()) {
				// Hold K and click to send a flare (0 A.D. session.flare), or click once
				// the flare button armed it (INPUT_FLARE).
				if (flare_armed_ || rtso::hotkeys::held("session.flare")) {
					flare_armed_ = false;
					const Vector3 click_pos = get_ground_click_pos(mouse_pos);
					trigger_flare(click_pos.x, click_pos.z);
					return;
				}

				if (build_mode) {
					if (current_build_type == rtso::BLD_WALL_SEGMENT || current_build_type == rtso::BLD_PALISADE ||
							current_build_type == rtso::BLD_SIEGE_WALL) {
						// input.js INPUT_BUILDING_WALL_CLICK / _PATHING: a press sets the start, the next press
						// the end; with Shift (session.queue) the end starts the next wall.
						const Vector3 click = get_ground_click_pos(mouse_pos);
						if (!wall_drag_mode) {
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
				sel_rect->set_rect(drag_start, mouse_pos, true);
			} else {
				const bool dbl_click = double_click_pending;
				double_click_pending = false;
				if (build_click_pending) {
					build_click_pending = false;
					int cell_x = build_click_cell_x, cell_y = build_click_cell_y;
					snap_placement_to_edges(current_build_type, cell_x, cell_y);
					const float fp = (float)rtso::building_type_info(current_build_type).footprint;
					const float angle = placement_angle_at(current_build_type, (cell_x + fp * 0.5f) * rtso::Simulation::GRID,
							(cell_y + fp * 0.5f) * rtso::Simulation::GRID);
					// Shift or Alt keeps placing; Alt sends one builder, which leaves the
					// selection (input.js:895 and popOneFromSelection).
					const bool order_one = rtso::hotkeys::held("session.orderone") && !selected_ids.is_empty();
					const bool keep_placing = order_one || rtso::hotkeys::held("session.queue");
					const uint32_t one_builder = order_one ? (uint32_t)selected_ids[0] : 0;
					const uint32_t *builders = order_one ? &one_builder : (const uint32_t *)selected_ids.ptr();
					const size_t builder_n = order_one ? 1 : (size_t)selected_ids.size();
					if (turn_cmds()) {
						post_lockstep(5, builders, builder_n,
								(float)cell_x, (float)cell_y, rtso::Simulation::pack_angle(angle), current_build_type);
						play_civ_voice("build");
						if (order_one) pop_from_selection(selected_ids, one_builder);
						if (!keep_placing) enter_build_mode(false);
					} else if (uint32_t bid = sim->cmd_build(local_player(), cell_x, cell_y, current_build_type, angle)) {
						if (builder_n > 0)
							sim->order_build_or_repair(builders, builder_n, bid);
						play_civ_voice("build");
						if (order_one) pop_from_selection(selected_ids, one_builder);
						if (!keep_placing) enter_build_mode(false);
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

					if (drag_start.distance_to(drag_end) < user_opts_.drag_delta) {
						// Single click selection
						Vector3 click_pos = get_ground_click_pos(mouse_pos);
						auto uview = sim->units();

						// A structure the ray reaches first hides the units behind it.
						float bld_t = 1e30f;
						building_under_mouse(mouse_pos, &bld_t);
						Camera3D *pick_cam = cam_rig ? cam_rig->get_camera() : nullptr;
						const Vector3 ray_o = pick_cam ? pick_cam->project_ray_origin(mouse_pos) : Vector3();
						const Vector3 ray_d = pick_cam ? pick_cam->project_ray_normal(mouse_pos) : Vector3();

						// Check unit hit
						size_t best_uidx = (size_t)-1;
						float best_ud2 = 2.0f * 2.0f;
						for (size_t i = 0; i < uview.count; ++i) {
							if (!uview.alive[i] || uview.team[i] != local_player()) continue;
							if (pick_cam && bld_t < 1e29f) {
								const Vector3 up(uview.pos_x[i], sim->get_terrain_height(uview.pos_x[i], uview.pos_z[i]), uview.pos_z[i]);
								if ((up - ray_o).dot(ray_d) > bld_t + 0.5f) continue;
							}
							float dx = uview.pos_x[i] - click_pos.x;
							float dz = uview.pos_z[i] - click_pos.z;
							float d2 = dx * dx + dz * dz;
							if (d2 < best_ud2) {
								best_ud2 = d2;
								best_uidx = i;
							}
						}

						if (best_uidx != (size_t)-1 && is_ctrl && !dbl_click) {
							// Ctrl+click drops the unit from the selection (selection.remove).
							const int64_t at = selected_ids.find((int32_t)uview.id[best_uidx]);
							if (at >= 0) selected_ids.remove_at(at);
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
								auto matches = [&](size_t i) {
									if (same_tmpl != rtso::CIV_UNIT_TEMPLATE_NONE ? uview.tmpl[i] != same_tmpl : uview.unit_type[i] != same_type)
										return false;
									return !exact || uview.rank[i] == same_rank;
								};
								Camera3D *cam = cam_rig ? cam_rig->get_camera() : nullptr;
								if (!is_shift && !is_ctrl) {
									selected_ids.clear();
								}
								selected_building_id = 0;
								if (cam) {
									const Vector2 vp_size = get_viewport()->get_visible_rect().size;
									for (size_t i = 0; i < uview.count; ++i) {
										if (!uview.alive[i] || uview.team[i] != local_player() || !matches(i))
											continue;
										Vector3 world_pos(uview.pos_x[i], 0, uview.pos_z[i]);
										if (!offscreen && cam->is_position_behind(world_pos)) continue;
										Vector2 screen_pos = cam->unproject_position(world_pos);
										if (offscreen || (screen_pos.x >= 0.0f && screen_pos.x <= vp_size.x &&
											screen_pos.y >= 0.0f && screen_pos.y <= vp_size.y)) {
											int32_t uid = (int32_t)uview.id[i];
											const int64_t at = selected_ids.find(uid);
											if (is_ctrl) {
												if (at >= 0) selected_ids.remove_at(at);
											} else if (at < 0) {
												selected_ids.push_back(uid);
											}
										}
									}
								}
								if (selected_ids.is_empty() && !is_ctrl)
									selected_ids.push_back((int32_t)uview.id[best_uidx]);
							} else if (is_shift) {
								// Shift+click adds the unit (selection.add -> addList keeps it if
								// already selected; Ctrl+click is the removal).
								for (int32_t uid : formation_mates((int32_t)uview.id[best_uidx]))
									if (!selected_ids.has(uid)) selected_ids.push_back(uid);
								selected_building_id = 0;
							} else {
								selected_ids.clear();
								selected_building_id = 0;
								for (int32_t uid : formation_mates((int32_t)uview.id[best_uidx]))
									selected_ids.push_back(uid);
							}
							play_civ_voice("select");
						} else if (uint32_t animal = own_fauna_at(click_pos.x, click_pos.z)) {
							// The player's own livestock (0 A.D. livestock are units).
							const int64_t at = selected_ids.find((int32_t)animal);
							if (is_ctrl) {
								if (at >= 0) selected_ids.remove_at(at);
							} else {
								if (!is_shift) selected_ids.clear();
								selected_building_id = 0;
								if (at < 0) selected_ids.push_back((int32_t)animal);
							}
						} else {
							// Check building hit: any visible part (0 A.D. picks the
							// rendered entity), then the old ground test round its base.
							uint32_t hit_bld = building_under_mouse(mouse_pos, nullptr);
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
								selected_building_id = hit_bld;
								if (!is_shift) selected_ids.clear();
							} else if (!is_ctrl && inspector_.inspect_at(click_pos)) {
								// Another player's unit or structure, an animal or a resource:
								// selected for its details only (0 A.D. EntitySelection).
								selected_ids.clear();
								selected_building_id = 0;
							} else {
								// Clicked empty ground: deselect if shift not pressed
								if (!is_shift) {
									selected_ids.clear();
									selected_building_id = 0;
								}
							}
						}
					} else {
						// Box selection
						Vector2 min_p(std::min(drag_start.x, drag_end.x), std::min(drag_start.y, drag_end.y));
						Vector2 max_p(std::max(drag_start.x, drag_end.x), std::max(drag_start.y, drag_end.y));

						auto uview = sim->units();
						if (!is_shift && !is_ctrl) {
							selected_ids.clear();
						}
						selected_building_id = 0;
						Camera3D *cam = cam_rig ? cam_rig->get_camera() : nullptr;
						if (cam) {
							for (size_t i = 0; i < uview.count; ++i) {
								if (!uview.alive[i] || uview.team[i] != local_player()) continue;
								const rtso::TechClasses cls = rtso::unit_tech_classes(uview.unit_type[i]);
								const bool military = (cls & (rtso::TC_MELEE | rtso::TC_RANGED)) != 0;
								if ((military_only && !military) || (nonmilitary_only && military)) continue;
								if (idle_only && uview.action_state[i] != rtso::ACTION_IDLE) continue;
								// gui.session.woundedunithotkeythreshold = 33 %.
								if (wounded_only && uview.hp[i] >= uview.max_hp[i] * user_opts_.wounded_fraction) continue;
								Vector3 world_pos(uview.pos_x[i], 0, uview.pos_z[i]);
								if (cam->is_position_behind(world_pos)) continue;
								Vector2 screen_pos = cam->unproject_position(world_pos);
								if (screen_pos.x >= min_p.x && screen_pos.x <= max_p.x &&
									screen_pos.y >= min_p.y && screen_pos.y <= max_p.y) {
									for (int32_t uid : formation_mates((int32_t)uview.id[i])) {
										if (is_ctrl) {
											const int64_t at = selected_ids.find(uid);
											if (at >= 0) selected_ids.remove_at(at);
										} else if (!selected_ids.has(uid)) {
											selected_ids.push_back(uid);
										}
									}
								}
							}
							// The player's own livestock in the box (not with the military,
							// idle or wounded filters, which pick soldiers and workers).
							auto fview = sim->fauna();
							for (size_t f = 0; f < fview.count && !military_only && !idle_only && !wounded_only; ++f) {
								if (!sim->fauna_selectable_by(local_player(), fview.id[f])) continue;
								Vector3 world_pos(fview.pos_x[f], 0, fview.pos_z[f]);
								if (cam->is_position_behind(world_pos)) continue;
								Vector2 screen_pos = cam->unproject_position(world_pos);
								if (screen_pos.x < min_p.x || screen_pos.x > max_p.x || screen_pos.y < min_p.y || screen_pos.y > max_p.y)
									continue;
								const int32_t fid = (int32_t)fview.id[f];
								const int64_t at = selected_ids.find(fid);
								if (is_ctrl) {
									if (at >= 0) selected_ids.remove_at(at);
								} else if (at < 0) {
									selected_ids.push_back(fid);
								}
							}
							if (selected_ids.size() > 0) {
								play_civ_voice("select");
							}
						}
					}
				}
			}
		} else if (mb->get_button_index() == MOUSE_BUTTON_RIGHT && mb->is_pressed() && flare_armed_) {
			flare_armed_ = false; // INPUT_FLARE: a right click cancels the flare
			return;
		} else if (mb->get_button_index() == MOUSE_BUTTON_RIGHT && mb->is_pressed()) {
			// A right-click while band-boxing cancels the box and gives no order
			// (input.js INPUT_BANDBOXING: right button resets to INPUT_NORMAL).
			if (dragging && drag_start.distance_to(mouse_pos) > 4.0f) {
				dragging = false;
				double_click_pending = false;
				sel_rect->set_rect(Vector2(), Vector2(), false);
				return;
			}
			if (build_mode) {
				enter_build_mode(false);
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
			// If building selected, right click sets Rally Point!
			if (selected_building_id != 0) {
				if (try_focus_fire(click_pos)) return;
				uint32_t target_res = 0, target_bld = 0;
				auto rview = sim->resources();
				for (size_t r = 0; r < rview.count; ++r) {
					if (!rview.alive[r]) continue;
					float dx = rview.x[r] - click_pos.x;
					float dz = rview.z[r] - click_pos.z;
					if (dx * dx + dz * dz < 2.0f * 2.0f) { target_res = rview.id[r]; break; }
				}
				if (auto_rally) target_bld = selected_building_id;
				for (const auto &b : sim->buildings()) {
					if (target_bld || b.id == selected_building_id) continue;
					float dx = b.x - click_pos.x;
					float dz = b.z - click_pos.z;
					if (dx * dx + dz * dz < 3.0f * 3.0f) { target_bld = b.id; break; }
				}
				// session.queue (Shift) adds a position after the ones set (RallyPoint.AddPosition).
				const bool rally_queued = rtso::hotkeys::held("session.queue");
				if (turn_cmds()) {
					uint32_t extra[2] = {target_res, target_bld};
					post_lockstep(14, extra, 2, click_pos.x, click_pos.z, selected_building_id,
							rally_queued ? rtso::NET_PARAM_QUEUED : 0);
				} else {
					sim->cmd_set_rally_point(local_player(), selected_building_id, click_pos.x, click_pos.z, target_res, target_bld,
							rally_queued);
				}
				play_civ_voice("select");
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
			if (rtso::hotkeys::held("session.orderone")) {
				one_id = sel_ptr[0];
				sel_ptr = &one_id;
				sel_n = 1;
			}

			// 5. Default: Move Order (a player's walk ignores enemies until it arrives)
			auto walk_there = [&]() {
				range_overlays_.add_target_marker(click_pos.x, click_pos.z);
				if (turn_cmds())
					post_lockstep(1, sel_ptr, sel_n, click_pos.x, click_pos.z, 0, qbit);
				else {
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
			// sends it into the player's own corral under the cursor (UnitAI "garrison").
			bool fauna_only = sel_n > 0;
			for (size_t k = 0; k < sel_n && fauna_only; ++k) fauna_only = sim->fauna_selectable_by(local_player(), sel_ptr[k]);
			if (fauna_only) {
				uint32_t corral = 0;
				for (const auto &b : sim->buildings()) {
					if (b.team != local_player() || b.type != rtso::BLD_CORRAL) continue;
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

			// Ctrl over an enemy structure attacks it instead of capturing it (0 A.D.
			// session.attack); Ctrl over anything else is attack-move (session.attackmove).
			if ((rtso::hotkeys::held("session.attack") || rtso::hotkeys::held("session.attackmove")) && !attack_move_mode) {
				if (const uint32_t sid = scenery_at(click_pos.x, click_pos.z); sid != 0) {
					if (turn_cmds())
						post_lockstep(17, sel_ptr, sel_n, 0, 0, sid, rtso::NET_HUNT_SCENERY_NO_CAPTURE);
					else
						sim->order_attack_scenery(sel_ptr, sel_n, sid, false);
					play_civ_voice("attack");
					return;
				}
				for (const auto &b : sim->buildings()) {
					if (b.wrecked || b.team == local_player()) continue;
					const float dx = b.x - click_pos.x, dz = b.z - click_pos.z;
					const float hit_r = b.half_size + 1.2f;
					if (dx * dx + dz * dz > hit_r * hit_r) continue;
					if (turn_cmds())
						post_lockstep(15, sel_ptr, sel_n, 0, 0, b.id, (uint8_t)(qbit | rtso::NET_PARAM_NO_CAPTURE));
					else
						sim->order_attack_building(sel_ptr, sel_n, b.id, queue_shift, false);
					play_civ_voice("attack");
					return;
				}
			}

			// Attack-move: session.attackmove (Ctrl) + right-click, or attack_move_mode
			// toggled. Ctrl on one of our own buildings garrisons instead (session.garrison).
			// The Garrison and Repair buttons arm one click, like holding the modifier.
			const bool force_garrison = garrison_mode || rtso::hotkeys::held("session.garrison");
			const bool force_repair = repair_mode || rtso::hotkeys::held("session.repair");
			garrison_mode = repair_mode = false;
			bool garrison_click = false;
			if (force_garrison) {
				for (const auto &b : sim->buildings()) {
					if (b.team != local_player() && !sim->is_ally(local_player(), b.team)) continue; // own or allied
					const float dx = b.x - click_pos.x, dz = b.z - click_pos.z, hit_r = b.half_size + 1.2f;
					if (dx * dx + dz * dz <= hit_r * hit_r) {
						garrison_click = true;
						break;
					}
				}
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
							(uint8_t)(qbit | (units_only ? rtso::NET_PARAM_UNITS_ONLY : 0)));
				else
					sim->order_attack_move(sel_ptr, sel_n, click_pos.x, click_pos.z, queue_shift, units_only, true);
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
					sim->order_patrol(sel_ptr, sel_n, click_pos.x, click_pos.z, queue_shift);
				play_civ_voice("select");
				patrol_mode = false;
				return;
			}
			if (guard_mode || rtso::hotkeys::held("session.guard")) {
				auto uview = sim->units();
				uint32_t target_ally_id = 0;
				float best_ad2 = 3.0f * 3.0f;
				for (size_t i = 0; i < uview.count; ++i) {
					if (!uview.alive[i] || uview.team[i] != local_player()) continue;
					float dx = uview.pos_x[i] - click_pos.x;
					float dz = uview.pos_z[i] - click_pos.z;
					float d2 = dx * dx + dz * dz;
					if (d2 < best_ad2) {
						best_ad2 = d2;
						target_ally_id = uview.id[i];
					}
				}
				// Guard.js guards structures too: one's own or an ally's under the cursor.
				if (target_ally_id == 0) {
					for (const auto &b : sim->buildings()) {
						if (b.wrecked || (b.team != local_player() && !sim->is_ally(local_player(), b.team))) continue;
						if (std::fabs(b.x - click_pos.x) <= b.half_size && std::fabs(b.z - click_pos.z) <= b.half_size) {
							target_ally_id = b.id;
							break;
						}
					}
				}
				if (target_ally_id != 0) {
					if (turn_cmds())
						post_lockstep(21, sel_ptr, sel_n, 0, 0, target_ally_id, qbit);
					else
						sim->order_guard(sel_ptr, sel_n, target_ally_id, queue_shift);
					play_civ_voice("select");
				}
				guard_mode = false;
				return;
			}

			auto uview = sim->units();

			// 0. Right click with Loaded Transport on Shore/Land (Disembark)
			bool has_loaded_transport = false;
			uint32_t transport_sel_id = 0;
			for (int s = 0; s < selected_ids.size(); ++s) {
				if (sim->is_transport((uint32_t)selected_ids[s]) && sim->get_transport_passenger_count((uint32_t)selected_ids[s]) > 0) {
					has_loaded_transport = true;
					transport_sel_id = (uint32_t)selected_ids[s];
					break;
				}
			}
			if (has_loaded_transport && sim->get_terrain_height(click_pos.x, click_pos.z) >= sim->water_height() + 0.1f) {
				if (one_id != 0) one_id = transport_sel_id;
				if (turn_cmds())
					post_lockstep(19, &transport_sel_id, 1, click_pos.x, click_pos.z);
				else
					sim->order_disembark(transport_sel_id, click_pos.x, click_pos.z);
				play_civ_voice("move");
				return;
			}

			// 1. Right click on Allied War Trireme / Transport (Board Naval Transport)
			uint32_t clicked_transport_id = 0;
			for (size_t i = 0; i < uview.count; ++i) {
				if (!uview.alive[i] || !sim->is_ally(local_player(), uview.team[i]) || uview.unit_type[i] != rtso::UNIT_TRIREME) continue; // own or a mutual ally's
				float dx = uview.pos_x[i] - click_pos.x;
				float dz = uview.pos_z[i] - click_pos.z;
				if (dx * dx + dz * dz <= 3.2f * 3.2f) {
					clicked_transport_id = uview.id[i];
					break;
				}
			}
			if (clicked_transport_id != 0) {
				if (turn_cmds())
					post_lockstep(18, sel_ptr, sel_n, 0, 0, clicked_transport_id);
				else
					sim->order_board_transport(sel_ptr, sel_n, clicked_transport_id);
				play_civ_voice("move");
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
					if (d2 < best_rd2) { best_rd2 = d2; repair_id = uview.id[i]; }
				}
				if (repair_id != 0) {
					if (turn_cmds())
						post_lockstep(16, sel_ptr, sel_n, 0, 0, repair_id, qbit);
					else
						sim->order_build_or_repair(sel_ptr, sel_n, repair_id, queue_shift);
					play_civ_voice("build");
					return;
				}
			}

			// 1c. Healers right-clicking an injured own or allied unit heal it (0 A.D. "heal" cursor).
			{
				// With orderone the first healer of the selection is the one ordered
				// (input.js firstAbleEntity).
				uint32_t first_healer = 0;
				for (int k = 0; k < selected_ids.size() && !first_healer; ++k) {
					for (size_t i = 0; i < uview.count; ++i)
						if (uview.id[i] == (uint32_t)selected_ids[k]) {
							if (rtso::unit_is_healer(uview.tmpl ? uview.tmpl[i] : rtso::CIV_UNIT_TEMPLATE_NONE, uview.unit_type[i])) first_healer = uview.id[i];
							break;
						}
				}
				const bool any_healer = first_healer != 0;
				uint32_t heal_id = 0;
				float best_hd2 = 2.2f * 2.2f;
				for (size_t i = 0; any_healer && i < uview.count; ++i) {
					if (!uview.alive[i] || uview.hp[i] >= uview.max_hp[i]) continue;
					if (uview.team[i] != local_player() && !sim->is_ally(local_player(), uview.team[i])) continue;
					const float dx = uview.pos_x[i] - click_pos.x, dz = uview.pos_z[i] - click_pos.z;
					const float d2 = dx * dx + dz * dz;
					if (d2 < best_hd2) { best_hd2 = d2; heal_id = uview.id[i]; }
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
					const float dx = t.x - click_pos.x, dz = t.z - click_pos.z;
					if (dx * dx + dz * dz < best_td2) { best_td2 = dx * dx + dz * dz; treasure_id = t.id; }
				}
				if (treasure_id != 0) {
					if (turn_cmds())
						post_lockstep(17, sel_ptr, sel_n, 0, 0, treasure_id, (uint8_t)(2 | qbit));
					else
						sim->order_collect_treasure(sel_ptr, sel_n, treasure_id, queue_shift);
					play_civ_voice("gather");
					return;
				}
			}

			// 2. Right click on Enemy Unit (Attack)
			uint32_t clicked_enemy_id = 0;
			float best_ed2 = 2.2f * 2.2f;
			for (size_t i = 0; i < uview.count; ++i) {
				if (!uview.alive[i] || uview.team[i] == local_player()) continue;
				float dx = uview.pos_x[i] - click_pos.x;
				float dz = uview.pos_z[i] - click_pos.z;
				float d2 = dx * dx + dz * dz;
				if (d2 < best_ed2) {
					best_ed2 = d2;
					clicked_enemy_id = uview.id[i];
				}
			}
			if (clicked_enemy_id != 0) {
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
				float dx = fview.pos_x[f] - click_pos.x;
				float dz = fview.pos_z[f] - click_pos.z;
				float d2 = dx * dx + dz * dz;
				if (d2 < best_fd2) {
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
					post_lockstep(17, sel_ptr, sel_n, 0, 0, clicked_fauna_id, herd);
				else if (herd)
					sim->order_herd(sel_ptr, sel_n, clicked_fauna_id);
				else
					sim->order_hunt(sel_ptr, sel_n, clicked_fauna_id);
				play_civ_voice(herd ? "gather" : "attack");
				return;
			}

			// Right click on Gaia scenery (a fence, column, obelisk): capture it if it is Capturable
			// (benches and fences), else attack it (template_structure Health).
			if (const uint32_t sid = scenery_at(click_pos.x, click_pos.z); sid != 0) {
				if (turn_cmds())
					post_lockstep(17, sel_ptr, sel_n, 0, 0, sid, 0);
				else
					sim->order_attack_scenery(sel_ptr, sel_n, sid);
				play_civ_voice("attack");
				return;
			}

			// 3. Right click on Resource Node (Gather)
			auto rview = sim->resources();
			uint32_t clicked_res_id = 0;
			float best_rd2 = 2.2f * 2.2f;
			for (size_t r = 0; r < rview.count; ++r) {
				if (!rview.alive[r] || rview.amount[r] <= 0) continue;
				float dx = rview.x[r] - click_pos.x;
				float dz = rview.z[r] - click_pos.z;
				float d2 = dx * dx + dz * dz;
				if (d2 < best_rd2) {
					best_rd2 = d2;
					clicked_res_id = rview.id[r];
				}
			}
			if (clicked_res_id != 0) {
				if (turn_cmds())
					post_lockstep(4, sel_ptr, sel_n, 0, 0, clicked_res_id, qbit);
				else
					sim->order_gather(sel_ptr, sel_n, clicked_res_id, queue_shift);
				play_civ_voice("gather");
				return;
			}

			// 4. Right click on Building (Attack Enemy vs Build/Repair/Garrison Allied)
			for (const auto &b : sim->buildings()) {
				float dx = b.x - click_pos.x;
				float dz = b.z - click_pos.z;
				float hit_r = b.half_size + 1.2f;
				if (dx * dx + dz * dz <= hit_r * hit_r) {
					// A finished field's grain, whoever owns it (0 A.D. "gather" has no owner check
					// and its specificness 1 beats capture, attack and repair).
					const uint32_t field_grain = b.type == rtso::BLD_FIELD ? sim->field_resource_id(b.id) : 0;
					const bool friendly = b.team == local_player() || sim->is_ally(local_player(), b.team);
					if (force_garrison && friendly && selection_can_garrison(b.id)) {
						// session.garrison is a hotkeyActionCheck: it wins over returning
						// resources and repairing (unit_actions.js "garrison").
						if (turn_cmds())
							post_lockstep(12, sel_ptr, sel_n, 0, 0, b.id, qbit);
						else
							sim->order_garrison(sel_ptr, sel_n, b.id, queue_shift);
						clear_selection();
					} else if (force_repair && friendly && (b.build_progress < 1.0f || b.hp < b.max_hp)) {
						// session.repair likewise wins over returning resources (unit_actions.js "repair").
						if (turn_cmds())
							post_lockstep(16, sel_ptr, sel_n, 0, 0, b.id, qbit);
						else
							sim->order_build_or_repair(sel_ptr, sel_n, b.id, queue_shift);
						play_civ_voice("build");
					} else if (trade_route_click(b, true, queue_shift, nullptr)) {
						// setup-trade-route has specificness 0: it wins over garrison and capture.
					} else if (field_grain && sim->any_can_gather(sel_ptr, sel_n, field_grain)) { // any player's finished field
						if (turn_cmds())
							post_lockstep(4, sel_ptr, sel_n, 0, 0, field_grain, qbit);
						else
							sim->order_gather(sel_ptr, sel_n, field_grain, queue_shift);
						play_civ_voice("gather");
					} else if (b.team != local_player() && sim->is_hostile(local_player(), b.team)) {
						if (turn_cmds())
							post_lockstep(15, sel_ptr, sel_n, 0, 0, b.id, qbit); // capture (0 A.D. default)
						else
							sim->order_attack_building(sel_ptr, sel_n, b.id, queue_shift, true);
						play_civ_voice("attack");
					} else if (b.team != local_player() && !sim->is_ally(local_player(), b.team)) {
						break; // neutral: walk there
					} else if (b.team != local_player()) {
						// An ally's structure: repair it, else garrison (Garrisonable / IsOwnedByMutualAllyOfEntity).
						if (b.build_progress < 1.0f || b.hp < b.max_hp) {
							if (turn_cmds())
								post_lockstep(16, sel_ptr, sel_n, 0, 0, b.id, qbit);
							else
								sim->order_build_or_repair(sel_ptr, sel_n, b.id, queue_shift);
							play_civ_voice("build");
						} else if (selection_can_garrison(b.id)) {
							if (turn_cmds())
								post_lockstep(12, sel_ptr, sel_n, 0, 0, b.id, qbit);
							else
								sim->order_garrison(sel_ptr, sel_n, b.id, queue_shift);
							clear_selection();
						} else {
							break; // nothing to do there: walk
						}
					} else {
						// 0 A.D. "returnresource" comes first: carriers take their load to this dropsite.
						bool carriers = false;
						for (size_t k = 0; k < sel_n && !carriers; ++k) carriers = sim->can_return_resource(sel_ptr[k], b.id);
						if (carriers) {
							if (turn_cmds())
								post_lockstep(34, sel_ptr, sel_n, 0, 0, b.id, qbit);
							else
								sim->order_return_resource(sel_ptr, sel_n, b.id, queue_shift);
							play_civ_voice("gather");
						} else if (b.build_progress < 1.0f || b.hp < b.max_hp || b.type == rtso::BLD_FIELD) { // finished fields: farm
							if (turn_cmds())
								post_lockstep(16, sel_ptr, sel_n, 0, 0, b.id, qbit);
							else
								sim->order_build_or_repair(sel_ptr, sel_n, b.id, queue_shift);
							play_civ_voice("build");
						} else if (b.type == rtso::BLD_WALL_SEGMENT && Input::get_singleton()->is_key_pressed(KEY_CTRL)) {
							// 0 A.D. Upgrade.js wall -> gate (lockstep cmd 26), so replays and peers see it.
							if (turn_cmds())
								post_lockstep(26, nullptr, 0, 0, 0, b.id, 0);
							else
								sim->cmd_upgrade_building(local_player(), b.id);
							play_civ_voice("build");
						} else if (selection_can_garrison(b.id)) {
							if (turn_cmds())
								post_lockstep(12, sel_ptr, sel_n, 0, 0, b.id, qbit);
							else
								sim->order_garrison(sel_ptr, sel_n, b.id, queue_shift);
							clear_selection();
						} else {
							break; // no selected unit is on its GarrisonHolder List: walk
						}
					}
					return;
				}
			}

			walk_there();
		}
	}

	Ref<InputEventMouseMotion> mm = event;
	if (mm.is_valid() && dragging) {
		sel_rect->set_rect(drag_start, mm->get_position(), true);
	}
}

bool RtsoMain::try_place_wall(const Vector3 &end, bool queued) {
	if (!sim) return false;
	// Commands.js "construct-wall": the sim lays the pieces (helpers/Walls.js) and snaps the ends
	// onto the player's wall towers, the same on every peer. Alt hands the whole wall to one
	// builder (popOneFromSelection).
	const bool order_one = rtso::hotkeys::held("session.orderone") && !selected_ids.is_empty();
	const uint32_t one_builder = order_one ? (uint32_t)selected_ids[0] : 0;
	const uint32_t *builders = order_one ? &one_builder : (const uint32_t *)selected_ids.ptr();
	const size_t builder_n = order_one ? 1 : (size_t)selected_ids.size();
	const uint8_t wall_kind = rtso::wallset_kind_of((uint8_t)current_build_type);
	// GuiInterface.js SetWallPlacementPreview: the wall is placeable when its first piece is.
	uint32_t snapped[2] = {0, 0};
	const std::vector<rtso::Simulation::WallPiece> pieces = sim->wall_placement(local_player(), wall_drag_start_pos.x,
			wall_drag_start_pos.z, end.x, end.z, &snapped[0], &snapped[1], wall_kind);
	if (pieces.empty() || sim->check_wall_piece(local_player(), pieces[0], snapped, 2) != rtso::Simulation::PLACE_OK)
		return false;
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
