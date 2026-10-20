// P4.6: RtsoMain's command panel and selection details handlers (unit_commands.js, selection_panels.js): train, build, research, upgrade, stances, formations, garrison, delete and the other entity commands.

#include <algorithm>
#include "network/chat_routing.h"
#include "main_node.h"
#include "fog_mirage.h"
#include "l10n.h"
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/classes/color_rect.hpp>
#include <godot_cpp/classes/os.hpp>
#include <unordered_set>
#include <cstring>
#include <godot_cpp/classes/theme.hpp>
#include "popup_safe.h"
#include "simulation/petra_chat.h"
#include "simulation/rms_generator.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/script.hpp>
#include "user_config.h"
#include "obstruction_snap.h"
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

// Tests: select the local player's first structure or first live unit.
// unit_actions.js "garrison" actionCheck: some selected unit may garrison in this own or allied
// structure (its GarrisonHolder List, IsOwnedByMutualAllyOfEntity). Room is not checked.
bool RtsoMain::selection_can_garrison(uint32_t building_id) const {
	if (!sim) return false;
	for (int s = 0; s < selected_ids.size(); ++s)
		if (sim->can_garrison_in((uint32_t)(int64_t)selected_ids[s], building_id)) return true;
	return false;
}

// unit_actions.js "garrison" on an entity: an own or mutually allied ship, ram or siege tower
// (GarrisonHolder) under the cursor that some selected unit may board; 0 if none.
uint32_t RtsoMain::transport_under(float x, float z) const {
	if (!sim || selected_ids.is_empty()) return 0;
	auto uv = sim->units();
	for (size_t i = 0; i < uv.count; ++i) {
		if (!uv.alive[i] || !sim->is_ally(local_player(), uv.team[i]) || selected_ids.has((int32_t)uv.id[i])) continue;
		const float dx = uv.pos_x[i] - x, dz = uv.pos_z[i] - z;
		if (dx * dx + dz * dz <= 3.2f * 3.2f && selection_can_board(uv.id[i])) return uv.id[i];
	}
	return 0;
}

void RtsoMain::rally_target_at(const Vector3 &at, uint32_t &res, uint32_t &bld, uint32_t *ent, uint8_t *cmd) const {
	res = bld = 0;
	uint32_t ent_id = 0;
	uint8_t command = rtso::RALLY_CMD_AUTO;
	struct Out { uint32_t *e; uint8_t *c; uint32_t &ev; uint8_t &cv; ~Out() { if (e) *e = ev; if (c) *c = cv; } } out{ent, cmd, ent_id, command};
	if (!sim) return;
	// unit_actions.js "set-rallypoint": walk, or attack-walk with session.attackmove held
	// (units only with session.attackmoveUnit); a target below gives its own command.
	if (rtso::hotkeys::held("session.attackmoveUnit")) command = rtso::RALLY_CMD_ATTACK_WALK_UNITS;
	else if (rtso::hotkeys::held("session.attackmove")) command = rtso::RALLY_CMD_ATTACK_WALK;
	float best = 2.0f * 2.0f;
	auto rv = sim->resources();
	for (size_t r = 0; r < rv.count; ++r) {
		if (!rv.alive[r] || rv.amount[r] <= 0) continue;
		const float dx = rv.x[r] - at.x, dz = rv.z[r] - at.z, d2 = dx * dx + dz * dz;
		if (d2 < best) { best = d2; res = rv.id[r]; }
	}
	if (res) { command = rtso::RALLY_CMD_AUTO; return; }
	// A treasure: collect it ("collect-treasure").
	float best_t = 2.0f * 2.0f;
	for (const auto &t : sim->treasures()) {
		const float dx = t.x - at.x, dz = t.z - at.z, d2 = dx * dx + dz * dz;
		if (d2 < best_t) { best_t = d2; ent_id = t.id; }
	}
	if (ent_id) { command = rtso::RALLY_CMD_TREASURE; return; }
	const uint8_t me = local_player();
	// An enemy unit in sight: attack it ("attack").
	auto uv = sim->units();
	float best_u = 1.2f * 1.2f;
	for (size_t i = 0; i < uv.count; ++i) {
		if (!uv.alive[i] || !sim->is_hostile(me, uv.team[i])) continue;
		const float dx = uv.pos_x[i] - at.x, dz = uv.pos_z[i] - at.z, d2 = dx * dx + dz * dz;
		if (d2 >= best_u || !rtso::los_sees_unit(*sim, view_player(), uv.pos_x[i], uv.pos_z[i], uv.team[i])) continue;
		best_u = d2;
		ent_id = uv.id[i];
	}
	if (ent_id) { command = rtso::RALLY_CMD_ATTACK; return; }
	const std::vector<uint32_t> mine = selected_structures();
	float best_b = 1e9f;
	for (const auto &b : sim->buildings()) {
		if (b.wrecked || std::find(mine.begin(), mine.end(), b.id) != mine.end()) continue;
		const float dx = b.x - at.x, dz = b.z - at.z, d2 = dx * dx + dz * dz, hit_r = b.half_size + 1.2f;
		if (d2 <= hit_r * hit_r && d2 < best_b) { best_b = d2; bld = b.id; }
	}
	if (!bld) return;
	const rtso::Simulation::Building *t = nullptr;
	const rtso::Simulation::Building *from = nullptr;
	for (const auto &b : sim->buildings()) {
		if (b.id == bld) t = &b;
		if (b.id == selected_building_id) from = &b;
	}
	if (!t) return;
	command = rtso::RALLY_CMD_AUTO;
	const bool friendly = t->team == me || sim->is_ally(me, t->team);
	// Occupy a turret point of an own or allied wall or tower that has no garrison ("occupy-turret").
	if (friendly && t->build_progress >= 1.0f && sim->get_garrison_capacity(t->id) <= 0 && sim->turret_holder_points(*t)) {
		command = rtso::RALLY_CMD_TURRET;
		return;
	}
	// From a market to another that isn't an enemy's: new traders trade between them ("trade").
	auto market = [](uint8_t type) { return rtso::is_land_market(type) || type == rtso::BLD_DOCK; };
	if (from && market(from->type) && market(t->type) && t->build_progress >= 1.0f && !sim->is_hostile(me, t->team) &&
			(from->type == rtso::BLD_DOCK) == (t->type == rtso::BLD_DOCK))
		command = rtso::RALLY_CMD_TRADE;
}

bool RtsoMain::selection_can_board(uint32_t holder) const {
	if (!sim || !sim->is_transport(holder)) return false;
	for (int s = 0; s < selected_ids.size(); ++s)
		if (sim->can_board_unit(holder, (uint32_t)(int64_t)selected_ids[s])) return true;
	return false;
}

// unit_actions.js "occupy-turret" actionCheck: some selected unit may occupy a turret point of
// this own or mutually allied holder (Turretable). Room is not checked.
bool RtsoMain::selection_can_occupy_turret(uint32_t building_id) const {
	if (!sim) return false;
	for (int s = 0; s < selected_ids.size(); ++s)
		if (sim->can_occupy_turret_in((uint32_t)(int64_t)selected_ids[s], building_id)) return true;
	return false;
}

void RtsoMain::_on_ungarrison_pressed() {
	if (selected_building_id != 0) {
		// unloadAll: every selected structure empties.
		for (uint32_t id : selected_structures()) {
			if (turn_cmds())
				post_lockstep(13, nullptr, 0, 0, 0, id, 0);
			else
				sim->cmd_unload(local_player(), id, nullptr, 0);
		}
	} else if (!selected_ids.is_empty()) {
		for (int s = 0; s < selected_ids.size(); ++s) {
			if (sim->is_transport((uint32_t)selected_ids[s])) {
				uint32_t tid = (uint32_t)selected_ids[s];
				// An ally's transport: unload-all-by-owner, only our own units.
				uint8_t holder_owner = 0xFF;
				sim->holder_contents(tid, &holder_owner);
				if (holder_owner != local_player()) {
					if (turn_cmds())
						post_lockstep(13, nullptr, 0, 0, 0, tid, 0);
					else
						sim->cmd_unload(local_player(), tid, nullptr, 0);
					continue;
				}
				if (turn_cmds())
					post_lockstep(13, &tid, 1, 0, 0, 0, 1);
				else
					sim->order_disembark_all(tid);
			}
		}
	}
}

// unit_actions.js g_EntityCommands "remove-guard": the selected guards stop guarding
// (lockstep command 37).
void RtsoMain::_on_remove_guard_pressed() {
	if (!sim || selected_ids.is_empty()) return;
	std::vector<uint32_t> ids;
	for (int s = 0; s < selected_ids.size(); ++s)
		if (sim->unit_is_guarding((uint32_t)selected_ids[s])) ids.push_back((uint32_t)selected_ids[s]);
	if (ids.empty()) return;
	if (turn_cmds())
		post_lockstep((uint8_t)rtso::NetCommandType::NET_CMD_REMOVE_GUARD, ids.data(), ids.size());
	else
		sim->order_remove_guard(ids.data(), ids.size());
}

// g_EntityCommands "call-to-arms": arms ACTION_CALLTOARMS, so the next left click
// sends the selection there (drop loads, then attack-move; lockstep command 38).
void RtsoMain::_on_call_to_arms_pressed() {
	if (selected_ids.is_empty()) return;
	call_to_arms_mode = true;
	attack_move_mode = patrol_mode = guard_mode = garrison_mode = turret_mode = repair_mode = false;
}

// g_EntityCommands execute() for the command row, and the hotkeys that run them.
void RtsoMain::_on_entity_command(const String &name) {
	if (!sim) return;
	const uint32_t *ids = (const uint32_t *)selected_ids.ptr();
	const size_t n = selected_ids.size();
	auto arm = [&](bool &mode) {
		if (n == 0) return;
		attack_move_mode = patrol_mode = guard_mode = call_to_arms_mode = garrison_mode = turret_mode = repair_mode = false;
		mode = true;
	};
	if (name == "delete") {
		request_delete();
	} else if (name == "autoqueue-on" || name == "autoqueue-off") { // turnAutoQueueOn / Off
		_on_autoqueue_pressed(name == "autoqueue-on" ? 1 : 0);
	} else if (name == "share-dropsite") { // set-dropsite-sharing: shared when any selected one is locked
		bool any_locked = false;
		for (uint32_t id : selected_structures())
			for (const auto &b : sim->buildings())
				if (b.id == id) {
					any_locked = any_locked || (rtso::Simulation::is_dropsite_type(b.type) && !b.dropsite_shared);
					break;
				}
		_on_dropsite_sharing_pressed(any_locked ? 1 : 0);
	} else if (name == "stop") { // stopUnits
		if (n == 0) return;
		if (turn_cmds())
			post_lockstep(2, ids, n);
		else {
			sim->cmd_stop(ids, n);
			sim->order_fauna_stop(local_player(), ids, n);
		}
	} else if (name == "back-to-work") { // backToWork
		if (n == 0) return;
		if (turn_cmds())
			post_lockstep(31, ids, n);
		else
			sim->order_back_to_work(ids, n);
		play_civ_voice("gather");
	} else if (name == "call-to-arms") {
		_on_call_to_arms_pressed();
	} else if (name == "garrison") {
		arm(garrison_mode);
	} else if (name == "occupy-turret") {
		arm(turret_mode);
	} else if (name == "unload-all-turrets") { // unloadAllTurrets, on every selected structure
		const std::vector<uint32_t> structs = selected_structures();
		for (const auto &b : sim->buildings()) {
			if (b.turret_units.empty() || std::find(structs.begin(), structs.end(), b.id) == structs.end()) continue;
			if (turn_cmds()) post_lockstep(13, nullptr, 0, 0, 0, b.id, 3);
			else sim->cmd_unload_turrets(local_player(), b.id);
		}
	} else if (name == "repair") {
		arm(repair_mode);
	} else if (name == "add-guard") {
		arm(guard_mode);
	} else if (name == "patrol") {
		arm(patrol_mode);
	} else if (name == "remove-guard") {
		_on_remove_guard_pressed();
	} else if (name == "unload-all") {
		_on_ungarrison_pressed();
	} else if (name == "leave-turret") {
		// unit_actions.js leaveTurretPoints: every selected unit on a turret point steps down.
		std::vector<uint32_t> ids;
		for (int s = 0; s < selected_ids.size(); ++s)
			if (sim->turret_holder_of((uint32_t)(int64_t)selected_ids[s])) ids.push_back((uint32_t)(int64_t)selected_ids[s]);
		if (ids.empty()) return;
		if (turn_cmds()) post_lockstep(13, ids.data(), ids.size(), 0, 0, 0, 2);
		else sim->cmd_leave_turret(local_player(), ids.data(), ids.size());
	} else if (name == "select-trading-goods") {
		_toggle_session_dialog("BarterDialog");
	} else if (name == "focus-rally") {
		// The first rally point, else the structure itself.
		for (const auto &b : sim->buildings())
			if (b.id == selected_building_id && cam_rig) {
				cam_rig->set_target_pos(b.has_rally_point ? Vector3(b.rally_x, 0, b.rally_z) : Vector3(b.x, 0, b.z));
				break;
			}
	}
}

// unit_actions.js "delete": everything selected that may be deleted (not a
// structure you hold less than half the capture points of), after
// DeleteSelectionConfirmation unless session.noconfirmation is held.
void RtsoMain::request_delete() {
	delete_pending_.clear();
	for (int s = 0; s < selected_ids.size(); ++s) delete_pending_.push_back(selected_ids[s]);
	const std::vector<uint32_t> structs = selected_structures();
	for (const auto &b : sim->buildings())
		if (std::find(structs.begin(), structs.end(), b.id) != structs.end() && b.team == local_player() && !(b.max_capture_points > 0.0f &&
				b.capture_points_by_team[b.team] < b.max_capture_points * 0.5f))
			delete_pending_.push_back((int32_t)b.id);
	if (delete_pending_.is_empty()) return;
	if (rtso::hotkeys::held("session.noconfirmation")) {
		_on_delete_confirmed();
		return;
	}
	if (!delete_dialog_) {
		delete_dialog_ = memnew(ConfirmationDialog);
		delete_dialog_->set_title(tr("Delete"));
		delete_dialog_->set_text(tr("Destroy everything currently selected?"));
		delete_dialog_->get_ok_button()->set_text(tr("Yes"));
		delete_dialog_->get_cancel_button()->set_text(tr("No"));
		delete_dialog_->connect("confirmed", Callable(this, "_on_delete_confirmed"));
		ui_layer->add_child(delete_dialog_);
	}
	delete_dialog_->popup_centered();
}

void RtsoMain::_on_delete_confirmed() {
	if (!sim || delete_pending_.is_empty()) return;
	std::vector<uint32_t> ids;
	for (int s = 0; s < delete_pending_.size(); ++s) ids.push_back((uint32_t)(int64_t)delete_pending_[s]);
	delete_pending_.clear();
	if (turn_cmds())
		post_lockstep(30, ids.data(), ids.size());
	else
		sim->cmd_delete(local_player(), ids.data(), ids.size());
	clear_selection();
}

void RtsoMain::_on_stance_selected(int stance) {
	if (!selected_ids.is_empty()) {
		if (turn_cmds())
			post_lockstep(7, (const uint32_t *)selected_ids.ptr(), selected_ids.size(), 0, 0, 0, (uint8_t)stance);
		else
			sim->order_set_stance((const uint32_t *)selected_ids.ptr(), selected_ids.size(), (uint8_t)stance);
	}
}

void RtsoMain::_on_formation_selected(int formation) {
	if (!selected_ids.is_empty()) {
		if (turn_cmds())
			post_lockstep(8, (const uint32_t *)selected_ids.ptr(), selected_ids.size(), 0, 0, 0, (uint8_t)formation);
		else
			sim->order_set_formation((const uint32_t *)selected_ids.ptr(), selected_ids.size(), (uint8_t)formation);
	}
}

void RtsoMain::_on_train_pressed(int utype) {
	try_train_unit((rtso::UnitType)utype);
}

// input.js OnTrainMouseWheel: Shift+wheel over the trainable units changes the batch
// size by 1 / gui.session.scrollbatchratio per notch.
void RtsoMain::_on_roster_gui_input(const Ref<InputEvent> &event) {
	Ref<InputEventMouseButton> mb = event;
	if (mb.is_null() || !mb->is_pressed() || !rtso::hotkeys::held("session.batchtrain")) return;
	const int dir = mb->get_button_index() == MOUSE_BUTTON_WHEEL_UP ? 1 : mb->get_button_index() == MOUSE_BUTTON_WHEEL_DOWN ? -1 : 0;
	if (!dir) return;
	const double ratio = rtso::user_config::get_number("gui.session.scrollbatchratio");
	user_opts_.batch_scroll += dir / (float)(ratio > 0.0 ? ratio : 1.0);
	if (!(user_opts_.batch_scroll >= 1.0f)) user_opts_.batch_scroll = 1.0f;
	user_opts_.batch_size = std::max(1, (int)std::lround(user_opts_.batch_scroll));
	if (command_panel_.roster_list()) command_panel_.roster_list()->accept_event();
}

void RtsoMain::_on_roster_item_clicked(int index, Vector2 at, int mouse_button) {
	(void)at;
	if (!command_panel_.roster_list() || index < 0 || index >= command_panel_.roster_list()->get_item_count()) return;
	command_panel_.roster_list()->deselect_all();
	const int meta = (int)command_panel_.roster_list()->get_item_metadata(index);
	if (mouse_button == MOUSE_BUTTON_RIGHT && !(meta & RtsoCommandPanelView::ROSTER_ANIMAL) && sim && ui_layer) {
		// onPressRight showTemplateDetails, disabled rows too.
		Ref<Script> viewer = ResourceLoader::get_singleton()->load("res://scenes/reference/template_viewer.gd");
		const bool tech = meta & RtsoCommandPanelView::ROSTER_TECH;
		if (viewer.is_valid())
			viewer->call("open", ui_layer, String(tech ? "tech" : "unit"), tech ? (meta & 0xFF) : meta, (int)sim->player_civ(local_player()));
		return;
	}
	if (mouse_button != MOUSE_BUTTON_LEFT || command_panel_.roster_list()->is_item_disabled(index)) return;
	if (meta & RtsoCommandPanelView::ROSTER_ANIMAL) {
		try_breed(selected_building_id, (uint8_t)(meta & 0xFF), train_batch());
		return;
	}
	if (selected_building_id == 0 && command_panel_.trainer_unit_id() != 0) {
		if (meta & RtsoCommandPanelView::ROSTER_TECH) {
			const uint8_t tech = (uint8_t)(meta & 0xFF);
			if (turn_cmds())
				post_lockstep((uint8_t)rtso::NetCommandType::NET_CMD_TRAIN_FROM_UNIT, nullptr, 0, (float)tech, 0, command_panel_.trainer_unit_id(), 2);
			else
				sim->cmd_research_from_unit(local_player(), command_panel_.trainer_unit_id(), tech);
			return;
		}
		add_training(true, command_panel_.trainer_unit_id(), (uint16_t)meta);
		return;
	}
	add_training(false, selected_building_id, (uint16_t)meta);
}

// A unit's Trainer (triremes, Cyrus II): lockstep NET_CMD_TRAIN_FROM_UNIT.
void RtsoMain::try_train_from_unit(uint32_t unit_id, uint16_t tmpl, int count) {
	if (unit_id == 0 || tmpl >= rtso::CIV_UNIT_TEMPLATE_COUNT) return;
	if (turn_cmds()) {
		post_lockstep((uint8_t)rtso::NetCommandType::NET_CMD_TRAIN_FROM_UNIT, nullptr, 0, (float)tmpl, (float)count, unit_id, 0);
		play_civ_voice("select");
		return;
	}
	if (sim->cmd_train_from_unit(local_player(), unit_id, tmpl, count))
		play_civ_voice("select");
}

// Corral training (0 A.D. Trainer at the corral): the lockstep Train command with no unit
// type and the animal in target_x.
void RtsoMain::try_breed(uint32_t building_id, uint8_t ftype, int count) {
	if (building_id == 0) return;
	if (turn_cmds()) {
		post_lockstep(6, nullptr, 0, (float)ftype, (float)count, building_id, (uint8_t)rtso::UNIT_TYPE_NONE);
		return;
	}
	sim->cmd_breed_livestock(local_player(), building_id, ftype, count);
}

void RtsoMain::_on_build_btn_pressed(int btype) {
	enter_build_mode(true, (uint8_t)btype);
}

void RtsoMain::_on_tech_pressed(int tech_id) {
	if (selected_building_id != 0) _on_tech_pressed_at(tech_id, selected_building_id);
}

// A Research button researches at its own researchFacilityId (Research.getItems).
void RtsoMain::_on_tech_pressed_at(int tech_id, int64_t building_id) {
	if (building_id == 0) return;
	if (turn_cmds())
		// selection_panels_helpers.js addResearchToQueue: "pushFront" (target_x 1).
		post_lockstep(9, nullptr, 0, rtso::hotkeys::held("session.pushorderfront") ? 1.0f : 0.0f, 0, (uint32_t)building_id,
				(uint8_t)tech_id);
	else
		sim->cmd_research(local_player(), (uint32_t)building_id, (uint8_t)tech_id, rtso::hotkeys::held("session.pushorderfront"));
}

// The selected structures of the primary one's type, primary first: 0.28's selection
// panels (Upgrade, Gate, Queue, AlertRaiser) send their command for every selected entity
// that has the component, and RTS0's per-type components follow the structure type.
std::vector<uint32_t> RtsoMain::selected_structures_like_primary() const {
	std::vector<uint32_t> out;
	const std::vector<uint32_t> structs = selected_structures();
	int type = -1;
	for (const auto &b : sim->buildings())
		if (b.id == selected_building_id) type = b.type;
	for (uint32_t id : structs)
		for (const auto &b : sim->buildings())
			if (b.id == id) {
				if ((int)b.type == type) out.push_back(id);
				break;
			}
	return out;
}

// 0 A.D. Upgrade.js through lockstep command 26: `choice` is which of
// building_upgrade()'s choices (target_x), -1 cancels and refunds (param 1). Every
// selected structure of the same type upgrades (g_SelectionPanels.Upgrade).
void RtsoMain::_on_upgrade_pressed(int choice) {
	if (selected_building_id == 0) return;
	for (uint32_t id : selected_structures_like_primary()) {
		if (turn_cmds())
			post_lockstep((uint8_t)rtso::NetCommandType::NET_CMD_UPGRADE_BUILDING, nullptr, 0, choice < 0 ? 0.0f : (float)choice, 0,
					id, choice < 0 ? 1 : 0);
		else if (choice < 0)
			sim->cmd_cancel_upgrade(local_player(), id);
		else
			sim->cmd_upgrade_building(local_player(), id, choice);
	}
}

// 0 A.D. Gate.js LockGate / UnlockGate through lockstep command 27, on every selected gate.
void RtsoMain::_on_gate_lock_pressed(int lock) {
	if (selected_building_id == 0) return;
	const std::vector<uint32_t> structs = selected_structures();
	bool any = false;
	for (const auto &b : sim->buildings()) {
		if (!rtso::is_gate_type(b.type) || std::find(structs.begin(), structs.end(), b.id) == structs.end()) continue;
		const uint32_t id = b.id;
		any = any || (b.team == local_player() && b.gate_locked != (lock != 0));
		if (turn_cmds())
			post_lockstep((uint8_t)rtso::NetCommandType::NET_CMD_LOCK_GATE, nullptr, 0, 0, 0, id, lock ? 1 : 0);
		else
			sim->cmd_lock_gate(local_player(), id, lock != 0);
	}
	if (any) session_audio_.play_gate_lock(); // Gate.js LockGate / UnlockGate: sel_gate
}

// 0 A.D. ProductionQueue.js EnableAutoQueue / DisableAutoQueue through lockstep command 44,
// on every selected structure that trains.
void RtsoMain::_on_autoqueue_pressed(int on) {
	if (selected_building_id == 0) return;
	for (uint32_t id : selected_structures()) {
		if (!building_trains_units(id)) continue;
		if (turn_cmds())
			post_lockstep((uint8_t)rtso::NetCommandType::NET_CMD_AUTOQUEUE, nullptr, 0, 0, 0, id, on ? 1 : 0);
		else
			sim->cmd_set_autoqueue(local_player(), id, on != 0);
	}
}

// 0 A.D. set-dropsite-sharing through lockstep command 45, on every selected structure.
void RtsoMain::_on_dropsite_sharing_pressed(int shared) {
	if (selected_building_id == 0) return;
	for (uint32_t id : selected_structures()) {
		if (turn_cmds())
			post_lockstep((uint8_t)rtso::NetCommandType::NET_CMD_SET_DROPSITE_SHARING, nullptr, 0, 0, 0, id, shared ? 1 : 0);
		else
			sim->cmd_set_dropsite_sharing(local_player(), id, shared != 0);
	}
}

// 0 A.D. AlertRaiser alert-raise / alert-end through lockstep command 35, from every
// selected structure that raises alerts (raiseAlert: the selection's AlertRaisers).
void RtsoMain::_on_alert_pressed(int end) {
	if (selected_building_id == 0) return;
	bool alerted = false;
	for (uint32_t id : selected_structures()) {
		rtso::AlertRaiserInfo ai;
		bool raiser = false;
		for (const auto &b : sim->buildings())
			if (b.id == id) raiser = b.team == local_player() && rtso::building_alert_raiser(b.type, ai);
		if (!raiser) continue;
		alerted = true;
		if (net_lockstep())
			post_lockstep((uint8_t)rtso::NetCommandType::NET_CMD_ALERT, nullptr, 0, 0, 0, id, end ? 1 : 0);
		else if (end)
			sim->cmd_end_alert(local_player(), id);
		else
			sim->cmd_raise_alert(local_player(), id);
	}
	if (alerted) session_audio_.play_alert(end != 0); // AlertRaiser alert_raise / alert_end
}

// 0 A.D. unit Upgrade on the selection through lockstep command 43 (`to` = roster
// template, -1 = cancel and refund).
void RtsoMain::_on_unit_upgrade_pressed(int to) {
	if (selected_ids.is_empty()) return;
	const uint32_t *ids = (const uint32_t *)selected_ids.ptr();
	const size_t n = selected_ids.size();
	if (turn_cmds())
		post_lockstep((uint8_t)rtso::NetCommandType::NET_CMD_UPGRADE_UNIT, ids, n, 0, 0, to < 0 ? 0 : (uint32_t)to, to < 0 ? 1 : 0);
	else if (to < 0)
		sim->cmd_cancel_unit_upgrades(local_player(), ids, n);
	else
		sim->cmd_upgrade_units(local_player(), ids, n, (uint16_t)to);
}

// 0 A.D. Pack / Unpack on the selected siege engines through lockstep command 29.
void RtsoMain::_on_pack_pressed(int pack) {
	if (selected_ids.is_empty()) return;
	if (turn_cmds())
		post_lockstep((uint8_t)rtso::NetCommandType::NET_CMD_PACK, (const uint32_t *)selected_ids.ptr(),
				selected_ids.size(), 0, 0, 0, pack ? 1 : 0);
	else
		sim->order_pack((const uint32_t *)selected_ids.ptr(), selected_ids.size(), pack != 0);
}

void RtsoMain::_on_barter_requested(int sell_type, int buy_type, int amount) {
	if (!sim) return;
	if (sell_type < 0 || sell_type >= 4 || buy_type < 0 || buy_type >= 4) return;
	// Lockstep: barter mutates resources + market prices (both hashed) -> cmd_type 23
	// (turn_manager.cpp) applies on every peer at the turn boundary. 0 A.D. Barter.js.
	// The button's click is its only sound: 0.28's barter has no feedback of its own.
	if (turn_cmds())
		post_lockstep(23, nullptr, 0, (float)sell_type, (float)buy_type, (uint32_t)amount, 0);
	else
		sim->cmd_barter(local_player(), (rtso::ResourceType)sell_type, (rtso::ResourceType)buy_type, amount);
}

// 0 A.D. TradeDialog: everything the barter and trade panels show for the local
// player. Barter quotes follow BarterButton.update (sell 100, or 500 with
// session.massbarter); trader counts follow GuiInterface.GetTraderNumber, where a
// land trader counts as garrisoned while it rides a merchant ship that is trading.
Dictionary RtsoMain::get_trade_dialog_state() const {
	Dictionary d;
	if (!sim) return d;
	const uint8_t me = local_player();
	const auto &ps = sim->player(me);
	d["player"] = (int)me;
	d["can_barter"] = sim->can_barter(me);
	d["controls"] = !sim->is_defeated(me);
	// Barter.js: 100 per click, 5x with session.massbarter (the only amounts cmd_barter takes).
	const int multiplier = 5;
	const int amount = 100 * (rtso::hotkeys::held("session.massbarter") ? multiplier : 1);
	d["amount"] = amount;
	d["multiplier"] = multiplier;
	PackedInt32Array stock, goods, quotes;
	for (int r = 0; r < rtso::RES_COUNT; ++r) {
		stock.push_back((int)ps.res[r]);
		goods.push_back((int)sim->trading_goods(me)[r]);
	}
	// quotes[sell * RES_COUNT + buy]: what one click buys.
	for (int sell = 0; sell < rtso::RES_COUNT; ++sell)
		for (int buy = 0; buy < rtso::RES_COUNT; ++buy)
			quotes.push_back(sim->barter_quote((rtso::ResourceType)sell, (rtso::ResourceType)buy, amount));
	d["stock"] = stock;
	d["trading_goods"] = goods;
	d["quotes"] = quotes;

	auto uv = sim->units();
	std::unordered_set<uint32_t> trading_ships;
	int land_total = 0, land_trading = 0, ship_total = 0, ship_trading = 0, land_garrisoned = 0;
	for (size_t i = 0; i < uv.count; ++i) {
		if (uv.team[i] != me || uv.hp[i] <= 0.0f || !rtso::unit_is_trader(uv.unit_type[i])) continue;
		if (uv.action_state[i] == rtso::ACTION_DYING) continue;
		const bool trading = uv.alive[i] && uv.action_state[i] == rtso::ACTION_TRADE;
		if (uv.unit_type[i] == rtso::UNIT_MERCHANT_SHIP) {
			++ship_total;
			if (trading) { ++ship_trading; trading_ships.insert(uv.id[i]); }
		} else {
			++land_total;
			if (trading) ++land_trading;
		}
	}
	for (const auto &[ship, passengers] : sim->transport_passengers()) {
		if (!trading_ships.count(ship)) continue;
		for (uint32_t pid : passengers)
			for (size_t i = 0; i < uv.count; ++i)
				if (uv.id[i] == pid) {
					if (uv.unit_type[i] == rtso::UNIT_TRADER && uv.team[i] == me && uv.hp[i] > 0.0f) ++land_garrisoned;
					break;
				}
	}
	Dictionary land, ship;
	land["total"] = land_total;
	land["trading"] = land_trading;
	land["garrisoned"] = land_garrisoned;
	ship["total"] = ship_total;
	ship["trading"] = ship_trading;
	d["land"] = land;
	d["ship"] = ship;
	return d;
}

// TradeButtonManager.setTradingGoods: shares by ResourceType, summing to 100.
// Lockstep command 33 packs one percent byte per resource into target_id.
void RtsoMain::_on_trading_goods_requested(const PackedInt32Array &shares) {
	if (!sim || shares.size() != rtso::RES_COUNT) return;
	uint8_t pct[rtso::RES_COUNT];
	int sum = 0;
	uint32_t packed = 0;
	for (int r = 0; r < rtso::RES_COUNT; ++r) {
		if (shares[r] < 0 || shares[r] > 100) return;
		pct[r] = (uint8_t)shares[r];
		sum += pct[r];
		packed |= (uint32_t)pct[r] << (8 * r);
	}
	if (sum != 100) return;
	if (turn_cmds())
		post_lockstep((uint8_t)rtso::NetCommandType::NET_CMD_SET_TRADING_GOODS, nullptr, 0, 0, 0, packed, 0);
	else
		sim->cmd_set_trading_goods(local_player(), pct);
}
