// P4.6: RtsoMain's debug_* hooks, the queries and steps the Godot e2e tests drive the session through.

#include <algorithm>
#include <cmath>
#include "network/chat_routing.h"
#include "main_node.h"
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
#include "../graphics/assets/unit_template_models.h"
#include <sstream>
using namespace godot;
using rtso::sprintf0;

// music.js states: civ peace tracks for the local player's civ, battle while
// BattleDetection says the player is fighting, victory or defeat at the end
// (with alarm_victory / alarm_defeated).
// Music state, current track and last sound event (headless smoke tests).
// Test hook (single player): sends the local idle workers to the nearest
// resource and centres the camera there, so smoke tests can hear gathering.
void RtsoMain::debug_gather_nearest() {
	if (!sim || net_lockstep()) return;
	std::vector<uint32_t> idle = sim->get_idle_workers(local_player());
	if (idle.empty()) // no idle worker: the selection gathers (e2e_formation_options)
		for (int i = 0; i < selected_ids.size(); ++i) idle.push_back((uint32_t)(int64_t)selected_ids[i]);
	if (idle.empty()) return;
	auto uv = sim->units();
	float x = 0.0f, z = 0.0f;
	for (size_t i = 0; i < uv.count; ++i)
		if (uv.id[i] == idle.front()) { x = uv.pos_x[i]; z = uv.pos_z[i]; break; }
	auto rv = sim->resources();
	uint32_t best = 0;
	float best_d = 1e30f;
	for (size_t r = 0; r < rv.count; ++r) {
		if (!rv.alive[r] || rv.amount[r] <= 0 || rv.subtype[r] == rtso::RES_SUB_FISH) continue;
		const float d = (rv.x[r] - x) * (rv.x[r] - x) + (rv.z[r] - z) * (rv.z[r] - z);
		if (d < best_d) { best_d = d; best = rv.id[r]; }
	}
	if (!best) return;
	if (turn_cmds()) // offline local turns keep it in the replay
		post_lockstep(4, idle.data(), idle.size(), 0, 0, best, 0);
	else
		sim->order_gather(idle.data(), idle.size(), best, false);
	if (cam_rig) cam_rig->set_target_pos(Vector3(x, 0.0f, z));
}

// Test hook (single player): two markets and two traders by the local Town
// Center, the traders selected, then the two right-clicks of setup-trade-route.
Dictionary RtsoMain::debug_trade_route_clicks() {
	Dictionary d;
	if (!sim || net_lockstep()) return d;
	const uint8_t me = local_player();
	float cx = 0.0f, cz = 0.0f;
	for (const auto &b : sim->buildings())
		if (b.team == me && rtso::is_civic_centre(b.type)) { cx = b.x; cz = b.z; break; }
	const uint32_t m1 = sim->spawn_building_world(cx - 30.0f, cz, rtso::BLD_MARKET, me, 1.0f);
	const uint32_t m2 = sim->spawn_building_world(cx + 30.0f, cz + 10.0f, rtso::BLD_MARKET, me, 1.0f);
	clear_selection();
	for (int k = 0; k < 2; ++k)
		selected_ids.push_back((int32_t)sim->spawn_unit(cx - 25.0f, cz + 4.0f * k, me, rtso::UNIT_TRADER));
	const rtso::Simulation::Building *b1 = nullptr, *b2 = nullptr;
	for (const auto &b : sim->buildings()) {
		if (b.id == m1) b1 = &b;
		if (b.id == m2) b2 = &b;
	}
	if (!b1 || !b2) return d;
	String tip;
	const char *cur = nullptr;
	trade_route_click(*b1, false, false, &tip);
	d["hover_first"] = tip;
	d["first"] = trade_route_click(*b1, true, false, nullptr);
	trade_route_click(*b1, false, false, &tip, &cur);
	d["hover_origin"] = tip;
	d["origin_cursor"] = String(cur);
	trade_route_click(*b2, false, false, &tip);
	d["hover_second"] = tip;
	d["second"] = trade_route_click(*b2, true, false, nullptr);
	d["origin_after"] = (int)trade_origin_market_;
	d["m1"] = (int64_t)m1;
	d["m2"] = (int64_t)m2;
	return d;
}

// Test hook: the setup-trade-route tooltip and cursor over a market for the selection.
Dictionary RtsoMain::debug_trade_tip(int64_t market) {
	Dictionary d;
	if (!sim) return d;
	for (const auto &b : sim->buildings())
		if (b.id == (uint32_t)market) {
			String tip;
			const char *cur = nullptr;
			d["ok"] = trade_route_click(b, false, false, &tip, &cur);
			d["tip"] = tip;
			d["cursor"] = String(cur ? cur : "");
		}
	return d;
}

// Test hook: step 0 spawns a spearman guarding another and selects it, step 1
// presses Remove guard, any step reports the guard state and the entity commands.
Dictionary RtsoMain::debug_guard_step(int step) {
	Dictionary d;
	if (!sim || net_lockstep()) return d;
	const uint8_t me = local_player();
	if (step == 0) {
		float cx = 0.0f, cz = 0.0f;
		for (const auto &b : sim->buildings())
			if (b.team == me && rtso::is_civic_centre(b.type)) { cx = b.x; cz = b.z; break; }
		const uint32_t a = sim->spawn_unit(cx - 20.0f, cz, me, rtso::UNIT_SPEARMAN);
		const uint32_t b = sim->spawn_unit(cx - 22.0f, cz, me, rtso::UNIT_SPEARMAN);
		sim->order_guard(&a, 1, b);
		clear_selection();
		selected_ids.push_back((int32_t)a);
	} else if (step == 1) {
		_on_remove_guard_pressed();
	}
	d["guarding"] = !selected_ids.is_empty() && sim->unit_is_guarding((uint32_t)selected_ids[0]);
	update_command_panel();
	PackedStringArray buttons;
	for (Node *bar : {(Node *)command_panel_.commands_row(), (Node *)command_panel_.production_bar()})
		for (int i = 0; i < bar->get_child_count(); ++i)
			if (Button *btn = Object::cast_to<Button>(bar->get_child(i))) buttons.push_back(btn->get_text());
	d["buttons"] = buttons;
	return d;
}

// Test hook: a finished structure of `btype` next to the Civic Centre, selected;
// returns the units it offers and the production buttons it shows.
PackedStringArray RtsoMain::debug_train_buttons(int btype) {
	PackedStringArray out;
	if (!sim || net_lockstep()) return out;
	const uint8_t me = local_player();
	float cx = 0.0f, cz = 0.0f;
	for (const auto &b : sim->buildings())
		if (b.team == me && rtso::is_civic_centre(b.type)) { cx = b.x; cz = b.z; break; }
	const uint32_t id = sim->spawn_building_world(cx + 24.0f, cz + 24.0f, (uint8_t)btype, me, 1.0f);
	clear_selection();
	selected_building_id = id;
	update_command_panel();
	// The units it trains (the civ's roster list), then its other buttons.
	if (command_panel_.roster_list() && command_panel_.roster_list()->is_visible())
		for (int i = 0; i < command_panel_.roster_list()->get_item_count(); ++i) out.push_back(command_panel_.roster_label(i));
	// Icon buttons (research, construction) by their label; a research pair's two buttons.
	auto add = [&out](Node *n) {
		if (Button *btn = Object::cast_to<Button>(n)) out.push_back(btn->has_meta("label") ? String(btn->get_meta("label")) : btn->get_text());
	};
	for (int i = 0; i < command_panel_.production_bar()->get_child_count(); ++i) {
		Node *n = command_panel_.production_bar()->get_child(i);
		add(n);
		if (!Object::cast_to<Button>(n))
			for (int j = 0; j < n->get_child_count(); ++j) add(n->get_child(j));
	}
	for (int i = 0; i < command_panel_.commands_row()->get_child_count(); ++i) add(command_panel_.commands_row()->get_child(i));
	return out;
}

// Test hook: selects the local player's first unit that can build (step one
// call) and reports the command button whose text starts with `prefix`: its
// instance id, global rect and whether build placement is active.
Dictionary RtsoMain::debug_command_button(const String &prefix) {
	Dictionary d;
	if (!sim) return d;
	if (selected_ids.is_empty()) {
		auto uview = sim->units();
		for (size_t i = 0; i < uview.count; ++i) {
			if (!uview.alive[i] || uview.team[i] != local_player()) continue;
			if (uview.unit_type[i] != rtso::UNIT_FEMALE_CITIZEN && uview.unit_type[i] != rtso::UNIT_VILLAGER) continue;
			clear_selection();
			selected_ids.push_back((int32_t)uview.id[i]);
			break;
		}
	}
	d["build_mode"] = build_mode;
	for (int k = 0; k < 2 && !d.has("id"); ++k) {
	Node *bar = k == 0 ? (Node *)command_panel_.commands_row() : (Node *)command_panel_.production_bar();
	for (int i = 0; i < bar->get_child_count(); ++i) {
		Button *btn = Object::cast_to<Button>(bar->get_child(i));
		if (!btn) continue;
		const String label = btn->has_meta("label") ? String(btn->get_meta("label")) : btn->get_text();
		if (!label.begins_with(prefix)) continue;
		d["id"] = (int64_t)btn->get_instance_id();
		d["rect"] = btn->get_global_rect();
		d["disabled"] = btn->is_disabled();
		break;
	}
	}
	return d;
}

// Test hook: clears the selection and returns the screen point of the local
// Civic Centre's roof (its centre, a little above the ground) and its id, plus
// what is selected now.
Dictionary RtsoMain::debug_civic_centre_roof() {
	Dictionary d;
	d["selected_building"] = (int64_t)selected_building_id;
	if (!sim || !cam_rig || !cam_rig->get_camera()) return d;
	for (const auto &b : sim->buildings()) {
		if (b.team != local_player() || !rtso::is_civic_centre(b.type)) continue;
		const auto model = rtso::ModelLibrary::get("buildings", String(rtso::civ_building_model(sim->team_civ(b.team), b.type)));
		const float height = model.valid ? model.y_offset + model.aabb.size.y * (model.scale > 0.05f ? model.scale : 1.0f) : 3.0f;
		const Vector3 roof(b.x, sim->get_terrain_height(b.x, b.z) + height * 0.8f, b.z);
		d["height"] = height;

		cam_rig->set_target_pos(Vector3(b.x, 0.0f, b.z));
		d["id"] = (int64_t)b.id;
		d["roof"] = cam_rig->get_camera()->unproject_position(roof);
		d["ground_under_roof"] = get_ground_click_pos(cam_rig->get_camera()->unproject_position(roof));
		d["centre"] = Vector2(b.x, b.z);
		d["base_radius"] = b.half_size + 0.6f; // the ground test round the base
		break;
	}
	return d;
}

// Test hook: with select_all, selects every unit the local player owns; reports
// the selection size and the selection panel's group icons (global rects).
// "unit": the first own unit selected; "multi": all own units; "building": the first own
// structure; "field": the first own finished field (spawning one if there is none); "resource" /
// "fauna": the first tree or mine / animal, inspected as a click would; a number: that unit.
void RtsoMain::debug_inspect_entity(int64_t id) {
	if (!sim) return;
	clear_selection();
	// The click at its position (inspect_at reads the structures as the viewer knows them).
	for (const auto &b : sim->buildings())
		if (b.id == (uint32_t)id) {
			inspector_.inspect_at(Vector3(b.x, 0.0f, b.z));
			return;
		}
	auto uv = sim->units();
	for (size_t i = 0; i < uv.count; ++i)
		if (uv.id[i] == (uint32_t)id) {
			inspector_.inspect_at(Vector3(uv.pos_x[i], 0.0f, uv.pos_z[i]));
			return;
		}
}

Dictionary RtsoMain::debug_inspector(const String &what) {
	if (!sim) return Dictionary();
	if (what == "current") { // the details of the selection as it stands
		inspector_.update();
		return inspector_.debug_state();
	}
	clear_selection();
	inspector_.clear();
	auto uview = sim->units();
	if (what.is_valid_int()) {
		selected_ids.push_back((int32_t)what.to_int());
	} else if (what == "unit" || what == "multi") {
		for (size_t i = 0; i < uview.count; ++i)
			if (uview.alive[i] && uview.team[i] == local_player() && (what == "multi" || uview.unit_type[i] != rtso::UNIT_FEMALE_CITIZEN)) {
				selected_ids.push_back((int32_t)uview.id[i]);
				if (what == "unit") break;
			}
	} else if (what.begins_with("building:")) { // a structure by id
		selected_building_id = (uint32_t)what.substr(9).to_int();
	} else if (what == "building") {
		for (const auto &b : sim->buildings())
			if (b.team == local_player() && rtso::is_civic_centre(b.type)) { selected_building_id = b.id; break; }
	} else if (what == "field") {
		// The player's first finished field; the test spawns one beside the Civic Center first.
		for (const auto &b : sim->buildings())
			if (b.team == local_player() && b.type == rtso::BLD_FIELD && b.build_progress >= 1.0f) { selected_building_id = b.id; break; }
		if (selected_building_id == 0)
			for (const auto &b : sim->buildings())
				if (b.team == local_player() && rtso::is_civic_centre(b.type)) {
					sim->spawn_building_world(b.x + 12.0f, b.z + 12.0f, rtso::BLD_FIELD, local_player(), 1.0f);
					break;
				}
	} else if (what == "resource") {
		auto rv = sim->resources();
		for (size_t r = 0; r < rv.count; ++r)
			if (rv.alive[r] && rv.amount[r] > 0 && rv.type[r] == rtso::RES_WOOD) { inspector_.inspect(RtsoInspectorView::INSPECT_RESOURCE, rv.id[r]); break; }
	} else if (what == "relic") {
		// A Gaia catafalque beside the player's Civic Center, inspected.
		uint32_t relic = sim->relics().empty() ? 0 : sim->relics().front().id;
		if (!relic)
			for (const auto &b : sim->buildings())
				if (b.team == local_player() && rtso::is_civic_centre(b.type)) { relic = sim->spawn_relic(b.x - 14.0f, b.z + 14.0f); break; }
		if (relic) inspector_.inspect(RtsoInspectorView::INSPECT_UNIT, relic);
	} else if (what == "livestock") {
		auto fv = sim->fauna();
		for (size_t f = 0; f < fv.count; ++f)
			if (sim->fauna_selectable_by(local_player(), fv.id[f])) selected_ids.push_back((int32_t)fv.id[f]);
	} else if (what == "fauna") {
		auto fv = sim->fauna();
		for (size_t f = 0; f < fv.count; ++f)
			if (fv.alive[f] && fv.food_amount[f] > 0 && fv.behavior[f] != rtso::FAUNA_CORRALLED) { inspector_.inspect(RtsoInspectorView::INSPECT_FAUNA, fv.id[f]); break; }
	}
	inspector_.update();
	return inspector_.debug_state();
}

// Test hook: the local player's stock of resource `res` (0 wood, 1 metal, 2 stone, 3 food).
void RtsoMain::debug_set_resource(int res, float amount) {
	if (sim && res >= 0 && res < rtso::RES_COUNT) sim->player(local_player()).res[res] = amount;
}

// Test hook: whether a structure is being placed.
bool RtsoMain::debug_placing() const { return build_mode; }

bool RtsoMain::debug_structure_outlined(int64_t id) const { return view && view->structure_outlined((uint32_t)id); }

// Test hook: a unit carries `amount` of resource `res` (0 wood, 1 metal, 2 stone, 3 food).
void RtsoMain::debug_set_carry(int64_t unit_id, int res, int amount) {
	if (sim) sim->debug_set_carry((uint32_t)unit_id, (uint8_t)res, amount);
}

Dictionary RtsoMain::debug_selection_grid(bool select_all) {
	Dictionary d;
	if (!sim) return d;
	if (select_all) {
		clear_selection();
		auto uview = sim->units();
		for (size_t i = 0; i < uview.count; ++i)
			if (uview.alive[i] && uview.team[i] == local_player()) selected_ids.push_back((int32_t)uview.id[i]);
	}
	d["selected"] = selected_ids.size();
	Array icons;
	if (GridContainer *g = inspector_.selection_grid().grid()) {
		for (int i = 0; i < g->get_child_count(); ++i)
			if (Button *b = Object::cast_to<Button>(g->get_child(i))) {
				Dictionary e;
				e["rect"] = b->get_global_rect();
				e["tip"] = b->get_tooltip_text();
				e["id"] = (int64_t)b->get_instance_id();
				e["visible"] = b->is_visible_in_tree();
				icons.push_back(e);
			}
	}
	d["icons"] = icons;
	return d;
}

Dictionary RtsoMain::debug_selection_state() {
	Dictionary d;
	if (!sim) return d;
	d["selected"] = selected_ids;
	d["building"] = (int64_t)selected_building_id;
	for (const auto &b : sim->buildings())
		if (b.id == selected_building_id && b.has_rally_point) d["rally"] = Vector2(b.rally_x, b.rally_z);
	Camera3D *cam = cam_rig ? cam_rig->get_camera() : nullptr;
	Array units;
	auto uview = sim->units();
	for (size_t i = 0; i < uview.count; ++i) {
		// Own units, and any selected one (another player's, under Control all units).
		if (!uview.alive[i] || (uview.team[i] != local_player() && !selected_ids.has((int32_t)uview.id[i]))) continue;
		Dictionary u;
		u["id"] = (int64_t)uview.id[i];
		u["tmpl"] = (int64_t)uview.tmpl[i];
		u["rank"] = (int64_t)uview.rank[i];
		u["has_rank"] = sim->unit_has_rank(i);
		u["xp"] = (int64_t)uview.xp[i];
		u["pos"] = Vector2(uview.pos_x[i], uview.pos_z[i]);
		u["formation"] = (int64_t)sim->formation_of_unit(uview.id[i]);
		u["idle"] = uview.action_state[i] == rtso::ACTION_IDLE;
		const rtso::TechClasses tc = uview.tmpl[i] < rtso::CIV_UNIT_TEMPLATE_COUNT ? rtso::civ_unit_template(uview.tmpl[i]).tech_classes
				: rtso::unit_tech_classes(uview.unit_type[i]);
		u["civilian"] = (bool)(tc & rtso::TC_CIVILIAN);
		u["target_resource"] = (int64_t)uview.target_resource_id[i];
		const std::vector<rtso::UnitOrder> *oq = sim->unit_order_queue(uview.id[i]);
		u["queued_orders"] = (int64_t)(oq ? oq->size() : 0);
		u["order_kind"] = (int64_t)sim->unit_order(uview.id[i]).kind;
		if (oq && !oq->empty()) u["queue_front_kind"] = (int64_t)oq->front().kind;
		if (cam) {
			const Vector3 wp(uview.pos_x[i], sim->get_terrain_height(uview.pos_x[i], uview.pos_z[i]), uview.pos_z[i]);
			u["behind"] = cam->is_position_behind(wp);
			u["screen"] = cam->unproject_position(wp);
		}
		units.push_back(u);
	}
	d["units"] = units;
	d["viewport"] = get_viewport()->get_visible_rect().size;
	// The resource nearest the first selected unit, and where it is on screen.
	if (cam && !selected_ids.is_empty()) {
		float ux = 0, uz = 0;
		for (size_t i = 0; i < uview.count; ++i)
			if (uview.id[i] == (uint32_t)selected_ids[0]) { ux = uview.pos_x[i]; uz = uview.pos_z[i]; }
		auto rv = sim->resources();
		float best = 1e30f;
		for (size_t r = 0; r < rv.count; ++r) {
			// A field's grain is hovered as the field (the supply pick skips it), and a
			// unit other than the selected ones standing over a supply is picked before it.
			if (!rv.alive[r] || rv.subtype[r] == rtso::RES_SUB_GRAIN) continue;
			const float dd = (rv.x[r] - ux) * (rv.x[r] - ux) + (rv.z[r] - uz) * (rv.z[r] - uz);
			if (dd >= best) continue;
			bool covered = false;
			for (size_t i = 0; i < uview.count && !covered; ++i) {
				const float ex = uview.pos_x[i] - rv.x[r], ez = uview.pos_z[i] - rv.z[r];
				covered = uview.alive[i] && ex * ex + ez * ez < 3.0f * 3.0f && !selected_ids.has((int32_t)uview.id[i]);
			}
			if (covered) continue;
			if (dd < best) {
				best = dd;
				d["nearest_resource"] = (int64_t)rv.id[r];
				d["nearest_resource_screen"] = cam->unproject_position(Vector3(rv.x[r], sim->get_terrain_height(rv.x[r], rv.z[r]), rv.z[r]));
			}
		}
	}
	return d;
}

// Test hook: the command panel's buttons after a refresh (text, tooltip,
// disabled, instance id).
Array RtsoMain::debug_command_row() {
	Array out;
	update_command_panel();
	auto add = [&out](Node *n, bool paired) {
		Button *btn = Object::cast_to<Button>(n);
		if (!btn) return;
		Dictionary e;
		e["text"] = btn->has_meta("label") ? String(btn->get_meta("label")) : btn->get_text();
		e["tip"] = btn->get_tooltip_text();
		e["disabled"] = btn->is_disabled();
		e["id"] = (int64_t)btn->get_instance_id();
		e["paired"] = paired; // one of a research pair (selection_panels.js Research)
		e["corners"] = btn->find_child("corners", false, false) != nullptr; // guiSelection
		Label *cnt = Object::cast_to<Label>(btn->find_child("unitUpgradeCount", false, false));
		e["count"] = cnt ? cnt->get_text().to_int() : 0;
		e["progress"] = btn->has_meta("progress") ? (int)btn->get_meta("progress") : -1; // pixels left
		e["grey"] = btn->has_meta("grey");
		e["masked"] = btn->find_child("resourcesMask", false, false) != nullptr; // resourcesToAlphaMask
		for (int k = 0; k < btn->get_child_count(); ++k)
			if (TextureRect *ic = Object::cast_to<TextureRect>(btn->get_child(k)))
				if (ic->get_material().is_valid()) e["grey"] = true;
		out.push_back(e);
	};
	for (int i = 0; i < command_panel_.production_bar()->get_child_count(); ++i) {
		Node *n = command_panel_.production_bar()->get_child(i);
		add(n, false);
		if (!Object::cast_to<Button>(n))
			for (int j = 0; j < n->get_child_count(); ++j) add(n->get_child(j), true);
	}
	// unit_commands.xml: the entity commands along the details panel's bottom.
	for (int i = 0; i < command_panel_.commands_row()->get_child_count(); ++i) add(command_panel_.commands_row()->get_child(i), false);
	return out;
}

// Test hook: where debug_spawn_units puts its row of `count` (the first unit's offset from
// the player's Civic Centre). 16 m out on both axes, unless that row has water within 4 m:
// on a small coastal map that may be sea or its shore, where a siege tower finds nowhere to
// unload and walkers can't reach it. Then the nearest row with none, still 12 m or more off
// the Civic Centre and clear of trees and mines; with none, the usual one. Structures a
// test placed nearby are its own business. Ships keep the usual row.
Vector2 RtsoMain::debug_spawn_offset(const String &template_id, int count, int player) const {
	if (!sim) return Vector2(-16.0f, -16.0f);
	const uint16_t t = rtso::civ_unit_template_by_id(template_id.utf8().get_data());
	if (t >= rtso::CIV_UNIT_TEMPLATE_COUNT) return Vector2(-16.0f, -16.0f);
	const uint8_t me = player < 0 ? local_player() : (uint8_t)player;
	float cx = 0.0f, cz = 0.0f;
	for (const auto &b : sim->buildings())
		if (b.team == me && rtso::is_civic_centre(b.type)) { cx = b.x; cz = b.z; break; }
	const rtso::NavGrid &nav = sim->nav_grid();
	if (rtso::unit_is_ship(rtso::civ_unit_template(t).unit_type) || nav.cells.blank()) return Vector2(-16.0f, -16.0f);
	auto dry = [&](float x, float z) {
		const auto [gx, gy] = nav.world_to_cell(x, z);
		if (!nav.cells.inside(gx, gy)) return false;
		const uint8_t c = nav.cells.at(gx, gy);
		return c != rtso::NAV_WATER && c != rtso::NAV_SHALLOW && sim->get_terrain_height(x, z) >= sim->water_height() + 0.15f;
	};
	const auto res = sim->resources();
	auto clear_row = [&](float ox, float oz, bool moved) {
		if (std::max(std::fabs(ox), std::fabs(oz)) < 12.0f) return false;
		for (float dz = -4.0f; dz <= 4.0f; dz += 2.0f)
			for (float dx = -4.0f; dx <= 3.0f * (count - 1) + 4.0f; dx += 2.0f)
				if (!dry(cx + ox + dx, cz + oz + dz)) return false;
		if (moved) // nor into a wood or a mine the usual row is clear of
			for (size_t r = 0; r < res.count; ++r)
				if (res.alive[r] && res.x[r] > cx + ox - 4.0f && res.x[r] < cx + ox + 3.0f * (count - 1) + 4.0f &&
						std::fabs(res.z[r] - (cz + oz)) < 4.0f)
					return false;
		return true;
	};
	for (int k = 0; k <= 22; ++k)
		for (int dz = 0; dz <= std::min(k, 10); ++dz)
			if (clear_row(-16.0f + (float)(k - dz), -16.0f + (float)dz, k > 0)) return Vector2(-16.0f + (float)(k - dz), -16.0f + (float)dz);
	return Vector2(-16.0f, -16.0f);
}

// Test hook: spawns `count` of the roster template `template_id` (e.g.
// "athen/support_female_citizen") for the local player in a row by its Civic Centre,
// 3 m apart from debug_spawn_offset.
PackedInt32Array RtsoMain::debug_spawn_units(const String &template_id, int count, int player, int near) {
	PackedInt32Array out;
	if (!sim) return out;
	const uint16_t t = rtso::civ_unit_template_by_id(template_id.utf8().get_data());
	if (t >= rtso::CIV_UNIT_TEMPLATE_COUNT) return out;
	const uint8_t me = player < 0 ? local_player() : (uint8_t)player;
	const uint8_t by = near < 0 ? me : (uint8_t)near; // whose Civic Centre they spawn by
	float cx = 0.0f, cz = 0.0f;
	for (const auto &b : sim->buildings())
		if (b.team == by && rtso::is_civic_centre(b.type)) { cx = b.x; cz = b.z; break; }
	const Vector2 o = debug_spawn_offset(template_id, count, player);
	for (int i = 0; i < count; ++i)
		out.push_back((int32_t)sim->spawn_unit(cx + o.x + 3.0f * i, cz + o.y, me,
				(rtso::UnitType)rtso::civ_unit_template(t).unit_type, 0.0f, t));
	return out;
}

void RtsoMain::debug_complete_research(int player, int tech) {
	if (sim) sim->debug_complete_research((uint8_t)player, (uint8_t)tech);
}

// Test hook: `count` animals of a FaunaType by the player's Civic Centre, owned by the
// player (trained livestock) or Gaia.
PackedInt32Array RtsoMain::debug_spawn_fauna(int fauna_type, int count, bool owned, float dx, float dz) {
	PackedInt32Array out;
	if (!sim) return out;
	float cx = 0.0f, cz = 0.0f;
	for (const auto &b : sim->buildings())
		if (b.team == local_player() && rtso::is_civic_centre(b.type)) { cx = b.x; cz = b.z; break; }
	for (int i = 0; i < count; ++i)
		out.push_back((int32_t)sim->spawn_fauna(cx + dx + 3.0f * i, cz + dz, (uint8_t)fauna_type,
				owned ? local_player() : rtso::GAIA_PLAYER));
	return out;
}

// Test hooks: a stone mine (template_gaia_rock: 7 x 7 m, 13 x 13 m with 24 gatherers) at
// (x, z) from the player's civic centre; snap_placement_to_edges for a structure centred near
// (x, z) from it with snapping on, after a stale placement angle.
static godot::Vector2 local_cc_pos(const rtso::Simulation &sim, uint8_t player) {
	for (const auto &b : sim.buildings())
		if (b.team == player && rtso::is_civic_centre(b.type)) return godot::Vector2(b.x, b.z);
	return godot::Vector2();
}

int64_t RtsoMain::debug_spawn_mine(float x, float z, int max_gatherers) {
	if (!sim) return 0;
	const Vector2 cc = local_cc_pos(*sim, local_player());
	return sim->spawn_resource_world(cc.x + x, cc.y + z, rtso::RES_STONE, 5000, 0, (uint8_t)max_gatherers);
}

Dictionary RtsoMain::debug_snap_placement(int btype, float x, float z, bool hover, float stale_angle) {
	Dictionary d;
	if (!sim) return d;
	const float GRID = rtso::Simulation::GRID;
	const int fp = rtso::building_type_info((uint8_t)btype).footprint;
	const Vector2 cc = local_cc_pos(*sim, local_player());
	int cx = (int)std::lround((cc.x + x) / GRID - fp * 0.5f), cy = (int)std::lround((cc.y + z) / GRID - fp * 0.5f);
	placement_angle = stale_angle;
	debug_force_snap_ = true;
	d["snapped"] = snap_placement_to_edges((uint8_t)btype, cx, cy, hover);
	debug_force_snap_ = false;
	d["cell"] = Vector2i(cx, cy);
	d["angle"] = placement_angle;
	return d;
}

// Test hooks: put units in a structure at once (a map's <Garrison>) or send them aboard a
// ship, ram or siege tower; select one entity by id.
int RtsoMain::debug_put_inside(const PackedInt32Array &ids, int64_t holder) {
	if (!sim) return 0;
	std::vector<uint32_t> v;
	for (int i = 0; i < ids.size(); ++i) v.push_back((uint32_t)ids[i]);
	if (sim->is_transport((uint32_t)holder)) {
		sim->order_board_transport(v.data(), v.size(), (uint32_t)holder);
		return (int)v.size();
	}
	int n = 0;
	for (uint32_t u : v) n += sim->garrison_unit_now(u, (uint32_t)holder) ? 1 : 0;
	return n;
}

void RtsoMain::debug_select_units(const PackedInt32Array &ids) {
	selected_building_id = 0;
	selected_ids = ids;
}

// A Gaia catafalque at (dx, dz) from the player's Civic Center.
int64_t RtsoMain::debug_spawn_relic(float dx, float dz) {
	if (!sim) return 0;
	for (const auto &b : sim->buildings())
		if (b.team == local_player() && rtso::is_civic_centre(b.type)) return sim->spawn_relic(b.x + dx, b.z + dz);
	return 0;
}

// The units go and capture the relic (the "capture" order on a catafalque).
void RtsoMain::debug_capture_relic(const PackedInt32Array &ids, int64_t relic) {
	if (!sim) return;
	std::vector<uint32_t> v;
	for (int i = 0; i < ids.size(); ++i) v.push_back((uint32_t)ids[i]);
	sim->order_capture_relic(v.data(), v.size(), (uint32_t)relic);
}

void RtsoMain::debug_select_structures(const PackedInt32Array &ids) {
	selected_ids.clear();
	std::vector<uint32_t> v;
	for (int i = 0; i < ids.size(); ++i) v.push_back((uint32_t)ids[i]);
	set_selected_structures(v);
}

void RtsoMain::debug_select_entity(int64_t id) {
	if (!sim) return;
	selected_ids.clear();
	selected_building_id = 0;
	for (const auto &b : sim->buildings())
		if (b.id == (uint32_t)id) { set_selected_structures({b.id}); return; }
	set_selected_structures({});
	selected_ids.push_back((int32_t)id);
}

// Test hook: a finished structure of ours `dx`, `dz` from our Civic Center.
// `player`: the owner (the local player by default), still placed by the local Civic Center.
int64_t RtsoMain::debug_spawn_building(int type, float dx, float dz, int player) {
	if (!sim) return 0;
	const uint8_t me = local_player();
	for (const auto &b : sim->buildings())
		if (b.team == me && rtso::is_civic_centre(b.type))
			return sim->spawn_building_world(b.x + dx, b.z + dz, (uint8_t)type, player < 0 ? me : (uint8_t)player, 1.0f);
	return 0;
}

int64_t RtsoMain::debug_civic_centre_id(int player) const {
	if (!sim) return 0;
	for (const auto &b : sim->buildings())
		if (b.team == player && rtso::is_civic_centre(b.type)) return b.id;
	return 0;
}

// Test hook: each player's structures by type name, {player: {name: count}}.
Array RtsoMain::debug_building_looks() const {
	Array out;
	if (!sim || !view) return out;
	const Array rows = view->building_looks_data();
	for (int i = 0; i < rows.size(); ++i) {
		Array row = rows[i];
		for (const auto &b : sim->buildings())
			if (b.id == (uint32_t)(int64_t)row[0]) {
				row.push_back(b.x);
				row.push_back(b.z);
				out.push_back(row);
				break;
			}
	}
	return out;
}

Dictionary RtsoMain::debug_structure_types(bool with_civ) {
	Dictionary d;
	if (!sim) return d;
	for (const auto &b : sim->buildings()) {
		if (b.hp <= 0.0f || b.wrecked) continue;
		const int p = b.team;
		Dictionary per = d.get(p, Dictionary());
		String n = String::utf8(rtso::building_type_info(b.type).name);
		if (with_civ && b.civ < rtso::CIV_COUNT) n += String("/") + rtso::civ_info(b.civ).code; // "Blemmye Camp/kush"
		per[n] = (int)per.get(n, 0) + 1;
		d[p] = per;
	}
	return d;
}

// Test hook: starts the first technology one of the local player's structures can
// research now; {building, tech, name}, empty when none can.
Dictionary RtsoMain::debug_research_any() {
	Dictionary d;
	if (!sim) return d;
	const uint8_t me = local_player();
	std::vector<std::pair<uint32_t, uint8_t>> ids;
	for (const auto &b : sim->buildings())
		if (b.team == me && b.queue.empty()) ids.push_back({b.id, b.type});
	for (const auto &[id, type] : ids)
		for (int t = 1; t < rtso::TECH_COUNT; ++t) {
			if (rtso::tech_info((uint8_t)t).researcher != type) continue;
			if (!sim->cmd_research(me, id, (uint8_t)t)) continue;
			d["building"] = (int64_t)id;
			d["tech"] = t;
			d["name"] = String::utf8(rtso::tech_info((uint8_t)t).name);
			return d;
		}
	return d;
}

// Test hook: destroys the units (Delete's command) without touching the selection,
// as deaths in battle do.
void RtsoMain::debug_delete_units(const PackedInt32Array &ids) {
	if (!sim || ids.is_empty()) return;
	std::vector<uint32_t> v;
	for (int i = 0; i < ids.size(); ++i) v.push_back((uint32_t)ids[i]);
	sim->cmd_delete(local_player(), v.data(), v.size());
}

// Test hook: the units gain experience as from combat.
void RtsoMain::debug_add_xp(const PackedInt32Array &ids, float xp) {
	if (!sim) return;
	for (int i = 0; i < ids.size(); ++i) sim->debug_add_xp((uint32_t)ids[i], xp);
}

// Test hook: which session dialogs are open, the pause overlay's state, and
// the chat log; `chat` in the input line is sent first when given.
Dictionary RtsoMain::debug_dialogs() {
	Dictionary d;
	d["chat"] = chat_window_.is_open();
	d["diplomacy"] = diplo_dialog_.is_open();
	d["objectives"] = objectives_dialog_.is_open();
	d["match_settings_text"] = objectives_dialog_.text();
	d["match_settings_state"] = objectives_dialog_.player_state();
	Node *barter = get_node_or_null(NodePath("BarterDialog"));
	d["barter"] = barter ? bool(barter->call("is_open")) : false;
	d["overlay"] = pause_overlay_ && pause_overlay_->is_visible();
	d["overlay_resume"] = pause_resume_label_ && pause_resume_label_->is_visible();
	d["overlay_by"] = pause_by_label_ ? pause_by_label_->get_text() : String();
	d["chat_log"] = chat_window_.log_text();
	d["chat_overlay"] = (int64_t)chat_window_.overlay_count();
	d["chat_addressee"] = chat_window_.addressee_label();
	d["chat_extended"] = chat_window_.extended();
	d["chat_filters"] = (int64_t)chat_window_.filter_count();
	Array names; // the plain names Tab completes (g_PlayerAssignments)
	for (const String &n : const_cast<RtsoMain *>(this)->chat_autocomplete_names()) names.push_back(RtsoChatWindow::plain(n));
	d["chat_names"] = names;
	d["chat_addressees"] = chat_window_.addressee_list();
	return d;
}

void RtsoMain::debug_set_rally(int64_t building_id, float x, float z) {
	if (!sim || net_lockstep()) return;
	for (const auto &b : sim->buildings())
		if (b.id == (uint32_t)building_id) {
			sim->cmd_set_rally_point(b.team, b.id, x, z, 0, 0, false);
			return;
		}
}

// Test hook: step 0 places a barracks with a rally point and selects it; step 1
// does what a right-click on the barracks itself does; every step reports the
// rally point and whether the barracks centre / a far spot count as "itself".
Dictionary RtsoMain::debug_unset_rally_step(int step) {
	Dictionary d;
	if (!sim || net_lockstep()) return d;
	const uint8_t me = local_player();
	if (step == 0) {
		float cx = 0.0f, cz = 0.0f;
		for (const auto &b : sim->buildings())
			if (b.team == me && b.type == rtso::BLD_TOWN_CENTER) { cx = b.x; cz = b.z; break; }
		const uint32_t id = sim->spawn_building_world(cx + 24.0f, cz - 24.0f, rtso::BLD_BARRACKS, me, 1.0f);
		sim->cmd_set_rally_point(me, id, cx + 40.0f, cz - 24.0f, 0, 0, false);
		clear_selection();
		selected_building_id = id;
	}
	for (const auto &b : sim->buildings()) {
		if (b.id != selected_building_id) continue;
		if (step == 1 && over_own_rally_structure(Vector3(b.x, 0, b.z)))
			post_lockstep(14, nullptr, 0, 0, 0, selected_building_id, 1);
		d["has_rally"] = b.has_rally_point;
		d["over_self"] = over_own_rally_structure(Vector3(b.x + b.half_size * 0.5f, 0, b.z));
		d["over_far"] = over_own_rally_structure(Vector3(b.x + 30.0f, 0, b.z));
	}
	return d;
}

// Test hook: the selected structure, where it is on screen and its rally point.
Dictionary RtsoMain::debug_selected_building() const {
	Dictionary d;
	if (!sim) return d;
	for (const auto &b : sim->buildings()) {
		if (b.id != selected_building_id) continue;
		d["id"] = (int64_t)b.id;
		d["has_rally"] = b.has_rally_point;
		d["rally_self"] = b.has_rally_point && b.rally_target_bld_id == b.id;
		d["autoqueue"] = b.autoqueue;
		d["queue"] = (int64_t)b.queue.size();
		d["garrisoned"] = (int64_t)b.garrisoned_units.size();
		d["turret_units"] = (int64_t)b.turret_units.size();
		d["food"] = sim->player(b.team < rtso::MAX_PLAYERS ? b.team : 0).res[rtso::RES_FOOD];
		Camera3D *cam = cam_rig ? cam_rig->get_camera() : nullptr;
		if (cam) d["screen"] = cam->unproject_position(Vector3(b.x, sim->get_terrain_height(b.x, b.z), b.z));
	}
	return d;
}

// The selection's Barter panel, for tests: per resource, its sell and buy buttons.
Array RtsoMain::debug_barter_panel() {
	Array out;
	update_command_panel();
	Control *host = command_panel_.barter_host();
	if (!host || !host->is_visible() || host->get_child_count() == 0) return out;
	Node *panel = host->get_child(host->get_child_count() - 1);
	for (int i = 0; i < panel->get_child_count(); ++i) {
		Node *col = panel->get_child(i);
		Dictionary d;
		d["name"] = String(col->get_name()).trim_prefix("barter_");
		for (const char *which : {"sell", "buy"}) {
			Button *b = Object::cast_to<Button>(col->find_child(which, false, false));
			if (!b) continue;
			Dictionary e;
			e["id"] = (int64_t)b->get_instance_id();
			e["tip"] = b->get_tooltip_text();
			Label *cap = Object::cast_to<Label>(b->find_child("caption", false, false));
			e["caption"] = cap ? cap->get_text() : String();
			e["selected"] = b->find_child("corners", false, false) != nullptr;
			bool red = false;
			for (int k = 0; k < b->get_child_count(); ++k) red = red || Object::cast_to<ColorRect>(b->get_child(k)) != nullptr;
			e["red"] = red;
			d[which] = e;
		}
		out.push_back(d);
	}
	return out;
}

// Test hook: the overlay's open state, its boxes, the state text and the camera constraint.
Dictionary RtsoMain::debug_dev_overlay() const {
	using D = rtso::RtsoDevOverlay;
	Dictionary d;
	d["open"] = dev_overlay_.is_open();
	d["reveal"] = dev_overlay_.checked(D::DEV_REVEAL_MAP);
	d["restrict"] = dev_overlay_.checked(D::DEV_RESTRICT_CAMERA);
	d["state_shown"] = dev_overlay_.state_shown();
	d["state"] = dev_overlay_.state_text();
	d["map_revealed"] = sim ? sim->reveal_map() : false;
	d["chat_log"] = chat_window_.log_text();
	d["camera_x"] = cam_rig ? cam_rig->get_target_pos().x : 0.0f;
	int ranks = 0; // the selection's ranks added up
	if (sim) {
		auto v = sim->units();
		for (int k = 0; k < selected_ids.size(); ++k)
			for (size_t i = 0; i < v.count; ++i)
				if (v.alive[i] && v.id[i] == (uint32_t)selected_ids[k]) ranks += v.rank[i];
	}
	d["selection_ranks"] = ranks;
	d["seat"] = (int)seat_player();
	d["view_picker"] = observer_view_picker_ && observer_view_picker_->is_visible_in_tree();
	PackedStringArray commands; // the rows in order
	for (int i = 0; i < D::DEV_COUNT; ++i) commands.push_back(D::kLabels[i]);
	d["commands"] = commands;
	using O = rtso::RtsoDebugOverlays;
	d["pathfinder_drawn"] = debug_overlays_.drawn(O::PATHFINDER);
	d["hier_pathfinder_drawn"] = debug_overlays_.drawn(O::HIER_PATHFINDER);
	d["obstruction_drawn"] = debug_overlays_.drawn(O::OBSTRUCTION);
	d["unit_motion_drawn"] = debug_overlays_.drawn(O::UNIT_MOTION);
	d["range_drawn"] = debug_overlays_.drawn(O::RANGE);
	d["range_subdivisions"] = debug_overlays_.range_subdivisions();
	d["range_rays"] = debug_overlays_.range_rays();
	d["region_links"] = debug_overlays_.region_link_count();
	d["path_search_tiles"] = debug_overlays_.path_search_tiles();
	d["short_path_vertexes"] = debug_overlays_.short_path_vertexes();
	d["bounding_box_drawn"] = debug_overlays_.drawn(O::BOUNDING_BOX);
	d["cull_camera_frustum_drawn"] = debug_overlays_.drawn(O::CULL_CAMERA_FRUSTUM);
	d["shadows_frustum_drawn"] = debug_overlays_.drawn(O::SHADOWS_FRUSTUM);
	d["culling_enabled"] = culling_enabled_;
	d["lock_cull_camera"] = lock_cull_camera_;
	d["structures_shown"] = view ? view->shown_structures() : 0;
	d["control_all"] = sim ? sim->controls_all(local_player()) : false;
	d["render_debug"] = render_debug_mode_;
	d["viewport_debug_draw"] = get_viewport() ? (int)get_viewport()->get_debug_draw() : -1;
	return d;
}

// The garrison panel's state, for tests: one entry per icon.
Array RtsoMain::debug_garrison_icons() const {
	Array out;
	GridContainer *grid = inspector_.garrison_grid_view();
	if (!grid || !grid->is_visible()) return out;
	for (int c = 0; c < grid->get_child_count(); ++c) {
		Button *b = Object::cast_to<Button>(grid->get_child(c));
		if (!b) continue;
		Dictionary d;
		d["tip"] = b->get_tooltip_text();
		d["disabled"] = b->is_disabled();
		TextureRect *icon = Object::cast_to<TextureRect>(b->find_child("garrisonIcon", false, false));
		d["grey"] = icon && icon->get_material().is_valid();
		Ref<StyleBoxFlat> sb = b->get_theme_stylebox("normal");
		d["bg"] = sb.is_valid() ? sb->get_bg_color() : Color();
		int count = 1;
		for (int k = 0; k < b->get_child_count(); ++k)
			if (Label *l = Object::cast_to<Label>(b->get_child(k))) count = l->get_text().to_int();
		d["count"] = count;
		d["id"] = (int64_t)b->get_instance_id();
		out.push_back(d);
	}
	return out;
}

Dictionary RtsoMain::debug_placement_tip() const {
	Dictionary d;
	d["visible"] = placement_tip_ && placement_tip_->is_visible();
	d["text"] = placement_tip_ ? placement_tip_->get_text() : String();
	d["error"] = placement_tip_error_;
	return d;
}

// Tests: the action tooltip beside the cursor and the cursor's name.
Dictionary RtsoMain::debug_cursor_tip() {
	update_highlight();
	update_cursor();
	Dictionary d;
	d["visible"] = cursor_tip_ && cursor_tip_->is_visible();
	d["text"] = cursor_tip_ ? cursor_tip_->get_text() : String();
	d["color"] = cursor_tip_ ? cursor_tip_->get_theme_color("font_color") : Color();
	d["cursor"] = String(cursor_name_.c_str());
	return d;
}

// Tests: production queue slot i as the panel shows it.
Dictionary RtsoMain::debug_queue_slot(int i) {
	update_command_panel();
	return command_panel_.production_queue().debug_slot(i);
}

// Test hook: the minimap panel's buttons.
Dictionary RtsoMain::debug_minimap_panel() const {
	Dictionary d;
	const char *names[3] = {"idle", "diplomacy", "flare"};
	for (int i = 0; i < 3; ++i) {
		TextureButton *b = minimap_panel_.button(i);
		Dictionary e;
		e["disabled"] = b ? b->is_disabled() : true;
		e["tooltip"] = b ? b->get_tooltip_text() : String();
		e["texture"] = b && b->get_texture_normal().is_valid() ? b->get_texture_normal()->get_path() : String();
		d[names[i]] = e;
	}
	d["idle_count"] = minimap_panel_.idle_count_text();
	d["civ_background"] = minimap_panel_.civ_background();
	d["flare_armed"] = flare_armed_;
	d["pings"] = minimap_view ? (int64_t)minimap_view->entity_ping_count() : (int64_t)0;
	return d;
}

void RtsoMain::debug_attacked(int64_t target, int64_t attacker_owner) {
	if (!sim) return;
	auto uv = sim->units();
	int64_t u = -1;
	for (size_t i = 0; i < uv.count && u < 0; ++i)
		if (uv.alive[i] && uv.id[i] == (uint32_t)target) u = (int64_t)i;
	rtso::Simulation::AttackedEvent e{};
	e.target_id = (uint32_t)target;
	e.attacker_owner = (uint8_t)attacker_owner;
	if (u >= 0) {
		e.target_owner = uv.team[u];
		e.x = uv.pos_x[u];
		e.z = uv.pos_z[u];
	} else { // an owned animal (livestock)
		const auto fv = sim->fauna();
		size_t f = 0;
		while (f < fv.count && fv.id[f] != (uint32_t)target) ++f;
		if (f == fv.count) return;
		e.target_owner = fv.owner[f];
		e.x = fv.pos_x[f];
		e.z = fv.pos_z[f];
	}
	detect_attacks({e});
}

// The GLB a civ's class model for a unit type loads from (ModelLibrary, aliases included).
String RtsoMain::debug_unit_model_path(int civ, int unit_type) {
	return rtso::ModelLibrary::get("units", String(rtso::civ_unit_model((uint8_t)civ, (uint8_t)unit_type))).path;
}

// The GLB a roster template's own model (unit_template_draw_index) loads from; "" when it
// draws its civ's class model.
String RtsoMain::debug_template_model_path(const String &template_id) {
	const uint16_t t = rtso::civ_unit_template_by_id(template_id.utf8().get_data());
	const char *own = rtso::unit_template_draw_model(rtso::unit_template_draw_index(t));
	return own ? rtso::ModelLibrary::get("units", String(own)).path : String();
}
