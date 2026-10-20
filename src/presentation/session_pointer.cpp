// RtsoMain's pointer: the cursor for what is under the mouse, ground and building picks,
// minimap orders and trade route clicks (P4.6, split out of main_node.cpp).

#include "main_node.h"
#include "fog_mirage.h"
#include "l10n.h"
#include "selection_details.h"
#include <godot_cpp/classes/color_rect.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/time.hpp>
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

static Ref<Texture2D> load_tex(const String &path) {
	if (ResourceLoader::get_singleton()->exists(path)) {
		Ref<Texture2D> tex = ResourceLoader::get_singleton()->load(path);
		if (tex.is_valid()) return tex;
	}
	return Ref<Texture2D>();
}

// VideoMode.cpp DEFAULT_CURSOR_NAME "default-arrow" (default-arrow.txt: hotspot 1 1), the
// arrow every page shows; scenes/menu/user_settings.gd sets it at start-up.
void RtsoMain::reset_default_cursor() {
	Input::get_singleton()->set_custom_mouse_cursor(load_tex("res://assets/textures/cursors/default-arrow.png"),
			Input::CURSOR_ARROW, Vector2(1, 1));
}

// unit_actions.js "gather": "action-gather-" + the supply's resourceType.specific.
static const char *gather_cursor(uint8_t type, uint8_t sub) {
	if (sub == rtso::RES_SUB_RUINS) return "action-gather-ruins";
	switch (type) {
		case rtso::RES_WOOD: return "action-gather-tree";
		case rtso::RES_GOLD: return "action-gather-ore";
		case rtso::RES_STONE: return "action-gather-rock";
		default:
			return sub == rtso::RES_SUB_MEAT ? "action-gather-meat"
					: sub == rtso::RES_SUB_GRAIN ? "action-gather-grain"
					: sub == rtso::RES_SUB_FISH ? "action-gather-fish" : "action-gather-fruit";
	}
}

void RtsoMain::update_cursor() {
	Viewport *vp = get_viewport();
	if (!sim || !vp) return;
	const char *want = "";
	String tip; // the action's tooltip, shown beside the cursor (0 A.D. cursor tooltips)
	bool tip_orange = false; // coloredText(tooltip, "orange")
	Vector2 hot(1, 1);
	const Vector2 mouse = vp->get_mouse_position();
	const bool over_ui = vp->gui_get_hovered_control() != nullptr;
	if (!over_ui && !build_mode) {
		// input.js updateCursorAndTooltip: never in a replay; an observer's flare has its own cursor.
		if ((flare_armed_ && !turn_mgr_.is_replaying()) || flare_held()) {
			want = spectating() ? "action-observer-flare" : "action-player-flare";
		} else if (selected_building_id != 0 && selected_ids.is_empty()) {
			const Vector3 at = get_ground_click_pos(mouse);
			if (over_own_rally_structure(at) &&
					!(rtso::hotkeys::held("session.autorallypoint") && selected_building_trains_units())) {
				want = "action-unset-rally";
			} else if (over_own_rally_structure(at, false) &&
					!(rtso::hotkeys::held("session.autorallypoint") && selected_building_trains_units())) {
				// "set-rallypoint" refuses a selected structure as its target: no action, no cursor.
			} else if (selected_building_shoots() && rtso::hotkeys::held("session.focusfire")) {
				// unit_actions.js "set-rallypoint" getActionInfo: an attacking structure with
				// session.focusfire held aims ("action-target") instead of setting the rally point.
				want = "action-target";
			} else {
				// unit_actions.js "set-rallypoint" getActionInfo: the cursor of what the trained
				// units will do there (RallyPoint orders: gather, repair, garrison, attack).
				want = "cursor-rally";
				hot = Vector2(5, 31);
				uint32_t res = 0, bld = 0, ent = 0;
				uint8_t rcmd = rtso::RALLY_CMD_AUTO;
				rally_target_at(at, res, bld, &ent, &rcmd);
				const uint8_t me = local_player();
				if (rcmd == rtso::RALLY_CMD_ATTACK_WALK || rcmd == rtso::RALLY_CMD_ATTACK_WALK_UNITS) {
					want = "action-attack-move";
				} else if (rcmd == rtso::RALLY_CMD_ATTACK) {
					want = "action-attack";
				} else if (rcmd == rtso::RALLY_CMD_TREASURE) {
					want = "action-collect-treasure";
				} else if (rcmd == rtso::RALLY_CMD_HUNT) {
					want = "action-gather-meat";
				} else if (rcmd == rtso::RALLY_CMD_TRADE) {
					// unit_actions.js "set-rallypoint" on another market: the route's gain for
					// the trader this market trains, or "too close" with the disabled cursor.
					want = "action-setup-trade-route";
					rtso::Simulation::TradeGain g;
					if (rally_route_gain(bld, g)) {
						if (g.trader > 0) {
							uint8_t to_owner = me;
							for (const auto &b : sim->buildings())
								if (b.id == bld) { to_owner = b.team; break; }
							tip = rtso::tr0("Right-click to establish a default route for new traders.") + "\n" +
									rtso::sprintf0(rtso::tr0("Gain: %(gain)s"), {{"gain", rtso::seldet::trading_gain(g.trader, me, g.market1, to_owner,
											g.market2, me, [this](uint8_t pid) { return player_display_name(pid); })}});
						} else {
							want = "action-setup-trade-route-disabled";
							tip = rtso::tr0("This market is too close to the origin market.");
							tip_orange = true;
						}
					}
				} else if (rcmd == rtso::RALLY_CMD_TURRET) {
					want = "action-garrison";
				} else if (res) {
					auto rv = sim->resources();
					for (size_t r = 0; r < rv.count; ++r) {
						if (rv.id[r] != res) continue;
						want = gather_cursor(rv.type[r], rv.subtype[r]);
						break;
					}
				} else if (bld) {
					for (const auto &b : sim->buildings()) {
						if (b.id != bld) continue;
						if (b.team == me || sim->is_ally(me, b.team)) {
							const bool repairable = b.build_progress < 1.0f || b.hp < b.max_hp || b.type == rtso::BLD_FIELD;
							const int capacity = b.build_progress >= 1.0f ? sim->get_garrison_capacity(b.id) : 0;
							if (repairable && (rtso::hotkeys::held("session.repair") || capacity <= 0)) {
								want = "action-repair";
							} else if (capacity > 0) {
								want = "action-garrison";
								const int occupied = sim->get_garrison_occupied(b.id);
								tip = rtso::sprintf0(rtso::tr0("Current garrison: %(garrisoned)s/%(capacity)s"),
										{{"garrisoned", String::num_int64(occupied)}, {"capacity", String::num_int64(capacity)}});
								tip_orange = occupied >= capacity;
							}
						} else if (sim->is_hostile(me, b.team)) {
							want = b.max_capture_points > 0.0f ? "action-capture" : "action-attack";
						}
						break;
					}
				}
				if (std::string(want) != "cursor-rally") hot = Vector2(1, 1);
			}
		} else if (!selected_ids.is_empty()) {
			const Vector3 at = get_ground_click_pos(mouse);
			const uint8_t me = local_player();
			auto near2 = [&](float x, float z, float r) { return (x - at.x) * (x - at.x) + (z - at.z) * (z - at.z) <= r * r; };
			// input.js determineAction: the entity the hover picked (PickEntityAtPoint), else what
			// lies near the ground point.
			const uint32_t picked = hover_target_;
			auto near_id = [&](uint32_t id, float x, float z, float r) { return picked ? id == picked : near2(x, z, r); };
			// unit_actions.js hotkeyActionCheck in specificness order: session.garrison ("garrison", 20)
			// and session.occupyturret ("occupy-turret", 21) come before session.attackmove (30), all Ctrl.
			const bool hold_garrison = rtso::hotkeys::held("session.garrison");
			const bool hold_turret = rtso::hotkeys::held("session.occupyturret");
			const bool hold_repair = rtso::hotkeys::held("session.repair");
			bool sel_builder = false;
			for (int s = 0; s < selected_ids.size() && !sel_builder; ++s) sel_builder = sim->unit_id_builds((uint32_t)(int64_t)selected_ids[s]);
			bool hotkey_holder = false;
			if (hold_garrison && transport_under(at.x, at.z) != 0) hotkey_holder = true;
			for (const auto &b : sim->buildings()) {
				if (hotkey_holder || !(hold_garrison || hold_turret)) break;
				if (b.wrecked || !near2(b.x, b.z, b.half_size + 1.2f) || !sim->player_can_target_building(me, b.id)) continue;
				if (b.team != me && !sim->is_ally(me, b.team)) continue;
				hotkey_holder = (hold_garrison && selection_can_garrison(b.id)) || (hold_turret && selection_can_occupy_turret(b.id));
				break;
			}
			if (!hotkey_holder && (attack_move_mode || call_to_arms_mode || rtso::hotkeys::held("session.attackmove") ||
					rtso::hotkeys::held("session.attackmoveUnit") || rtso::hotkeys::held("session.calltoarms")))
				want = "action-attack-move";
			else if (patrol_mode || rtso::hotkeys::held("session.patrol")) want = "action-patrol";
			else if (guard_mode) want = "action-guard";
			else if (rtso::hotkeys::held("session.guard"))
				want = guard_target_at(at) ? "action-guard" : selection_guarding() ? "action-remove-guard" : "";
			else if (garrison_mode) want = "action-garrison";
			else if (turret_mode) want = "action-occupy-turret";
			else if (repair_mode) want = "action-repair";
			// An armed action over a target it can't take: its -disabled cursor (unit_actions.js
			// preSelectedActionCheck, "cursor": "action-garrison-disabled" and so on).
			if ((guard_mode || garrison_mode || turret_mode || repair_mode) && !armed_target_ok(at))
				want = guard_mode ? "action-guard-disabled" : garrison_mode ? "action-garrison-disabled"
						: turret_mode ? "action-occupy-turret-disabled" : "action-repair-disabled";
			if (!*want) {
				auto uv = sim->units();
				bool healer = false, builder = false;
				for (int s = 0; s < selected_ids.size() && !(healer && builder); ++s)
					for (size_t i = 0; i < uv.count; ++i)
						if (uv.id[i] == (uint32_t)(int64_t)selected_ids[s]) {
							const uint16_t tm = uv.tmpl ? uv.tmpl[i] : rtso::CIV_UNIT_TEMPLATE_NONE;
							healer = healer || rtso::unit_is_healer(tm, uv.unit_type[i]);
							builder = builder || (rtso::unit_can_build(uv.unit_type[i], tm) && !sim->nonbuilder(uv.id[i]));
							break;
						}
				auto heal_able = [&](uint32_t target) {
					return sim->first_able_healer((const uint32_t *)selected_ids.ptr(), (size_t)selected_ids.size(), target) != 0;
				};
				for (size_t i = 0; i < uv.count && !*want; ++i) {
					if (!uv.alive[i] || !near_id(uv.id[i], uv.pos_x[i], uv.pos_z[i], 2.2f)) continue;
					// PickEntityAtPoint picks only what the player sees: no unit in the fog.
					if (!rtso::los_sees_unit(*sim, me, uv.pos_x[i], uv.pos_z[i], uv.team[i])) continue;
					// "returnresource" on a supply wagon or worker elephant the carrier may drop at.
					if (rtso::unit_is_mobile_dropsite(uv.unit_type[i])) {
						for (int s = 0; s < selected_ids.size() && !*want; ++s) {
							const uint32_t cid = (uint32_t)(int64_t)selected_ids[s];
							if (!sim->can_return_resource(cid, uv.id[i])) continue;
							for (size_t c = 0; c < uv.count; ++c)
								if (uv.id[c] == cid) {
									const uint8_t rt = uv.res_type_carried[c];
									want = rt == rtso::RES_FOOD ? "action-return-food" : rt == rtso::RES_WOOD ? "action-return-wood"
											: rt == rtso::RES_STONE ? "action-return-stone" : "action-return-metal";
									break;
								}
						}
						if (*want) break;
					}
					if (sim->is_hostile(me, uv.team[i])) want = "action-attack";
					// unit_actions "repair": a builder over a damaged own or allied siege engine or ship.
					else if (builder && uv.hp[i] < uv.max_hp[i] && (uv.team[i] == me || sim->is_ally(me, uv.team[i])) &&
							sim->is_repairable_unit(uv.id[i]))
						want = "action-repair";
					// 0 A.D. unit_actions "heal": a healer over a wounded own or allied unit.
					else if (healer && heal_able(uv.id[i]))
						want = "action-heal";
					// unit_actions "garrison" (session.garrison): an own or allied ship, ram or siege tower a
					// selected unit may board. Without the hotkey "move" (12) wins.
					else if (const uint32_t holder = uv.id[i]; hold_garrison && sim->is_ally(me, uv.team[i]) &&
							!selected_ids.has((int32_t)holder) && selection_can_board(holder)) {
						want = "action-garrison";
						const int occupied = sim->get_transport_occupied(holder), capacity = sim->get_transport_capacity(holder);
						tip = rtso::sprintf0(rtso::tr0("Current garrison: %(garrisoned)s/%(capacity)s"),
								{{"garrisoned", String::num_int64(occupied)}, {"capacity", String::num_int64(capacity)}});
						tip_orange = occupied >= capacity;
					}
				}
			}
			if (!*want) {
				// unit_actions "collect-treasure": only for a selection with a TreasureCollector
				// (not dogs, siege engines or catafalques).
				bool collector = false;
				auto cuv = sim->units();
				for (int s = 0; s < selected_ids.size() && !collector; ++s)
					for (size_t i = 0; i < cuv.count; ++i)
						if (cuv.id[i] == (uint32_t)(int64_t)selected_ids[s]) {
							collector = sim->treasure_reach(i) >= 0.0f;
							break;
						}
				for (const auto &t : sim->treasures())
					if (collector && near_id(t.id, t.x, t.z, 2.0f) && sim->los_explored(me, t.x, t.z)) { want = "action-collect-treasure"; break; }
			}
			if (!*want) {
				auto fv = sim->fauna();
				for (size_t f = 0; f < fv.count && !*want; ++f) {
					// A penned animal is garrisoned in its corral: nothing to do with it.
					if (!fv.alive[f] || fv.behavior[f] == rtso::FAUNA_CORRALLED || !near_id(fv.id[f], fv.pos_x[f], fv.pos_z[f], 2.5f)) continue;
					if (!rtso::los_sees_fauna(*sim, me, fv.pos_x[f], fv.pos_z[f], fv.owner[f], fv.fauna_type[f])) continue; // RetainInFog
					// Slaughter for livestock as for game (0 A.D.); Alt herds your own to a corral.
					const bool herd = rtso::fauna_type_info(fv.fauna_type[f]).is_domestic() && fv.owner[f] == local_player() &&
					                  Input::get_singleton()->is_key_pressed(KEY_ALT);
					want = herd ? "action-herd" : "action-gather-meat";
				}
			}
			if (!*want) {
				if (const uint32_t sid = scenery_at(at.x, at.z); sid != 0 && (!picked || sid == picked)) {
					want = scenery_click(sid) == CLICK_CAPTURE ? "action-capture" : "action-attack";
				}
			}
			if (!*want) {
				// Supplies as the player knows them (Fogging mirages; none in unexplored land).
				// unit_actions "gather" needs a selected unit that can gather the supply (else "move").
				std::vector<uint32_t> sel;
				for (int s2 = 0; s2 < selected_ids.size(); ++s2) sel.push_back((uint32_t)(int64_t)selected_ids[s2]);
				const auto gatherable = [&](uint32_t id) {
					return !sim->is_resource_id(id) || sim->any_can_gather(sel.data(), sel.size(), id); // a gone one: its mirage
				};
				if (view) {
					for (const auto &r : view->resource_mirages())
						if (r.amount > 0 && near_id(r.id, r.x, r.z, 2.2f) && gatherable(r.id)) { want = gather_cursor(r.type, r.subtype); break; }
				} else {
					auto rv = sim->resources();
					for (size_t r = 0; r < rv.count && !*want; ++r)
						if (rv.alive[r] && rv.amount[r] > 0 && near_id(rv.id[r], rv.x[r], rv.z[r], 2.2f) && gatherable(rv.id[r]))
							want = gather_cursor(rv.type[r], rv.subtype[r]);
				}
			}
			if (!*want) {
				for (const auto &b : sim->buildings()) {
					if (b.wrecked || !near_id(b.id, b.x, b.z, b.half_size + 1.2f)) continue;
					if (!sim->player_can_target_building(me, b.id)) continue; // never seen: no mirage to pick
					auto garrison_cursor = [&]() {
						want = "action-garrison";
						// unit_actions "garrison" getActionInfo: the holder's slots, orange once full.
						const int occupied = sim->get_garrison_occupied(b.id), capacity = sim->get_garrison_capacity(b.id);
						tip = rtso::sprintf0(rtso::tr0("Current garrison: %(garrisoned)s/%(capacity)s"),
								{{"garrisoned", String::num_int64(occupied)}, {"capacity", String::num_int64(capacity)}});
						tip_orange = occupied >= capacity;
					};
					// determineAction (input.js:261-277) runs every hotkeyActionCheck before the plain
					// actionChecks, as the click does: session.garrison and session.repair come first.
					const bool b_friendly = b.team == me || sim->is_ally(me, b.team);
					const bool b_damaged = b.build_progress < 1.0f || b.hp < b.max_hp;
					if (hold_garrison && b_friendly && selection_can_garrison(b.id)) {
						garrison_cursor();
						break;
					}
					if (hold_repair && b_friendly && b_damaged && sel_builder) {
						want = "action-repair";
						break;
					}
					if (const char *tc = nullptr; trade_route_click(b, false, false, &tip, &tc)) {
						want = tc;
						break;
					}
					// unit_actions "gather" (specificness 1): any player's finished field.
					// unit_actions.js "returnresource": a carrier over a dropsite that takes its load.
					{
						auto cv = sim->units();
						for (int s = 0; s < selected_ids.size() && !*want; ++s) {
							const uint32_t uid = (uint32_t)(int64_t)selected_ids[s];
							if (!sim->can_return_resource(uid, b.id)) continue;
							for (size_t i = 0; i < cv.count; ++i)
								if (cv.id[i] == uid) {
									switch (cv.res_type_carried[i]) {
										case rtso::RES_FOOD: want = "action-return-food"; break;
										case rtso::RES_WOOD: want = "action-return-wood"; break;
										case rtso::RES_STONE: want = "action-return-stone"; break;
										case rtso::RES_GOLD: want = "action-return-metal"; break;
										default: break;
									}
									break;
								}
						}
						if (*want) break;
					}
					const uint32_t grain = b.type == rtso::BLD_FIELD ? sim->field_resource_id(b.id) : 0;
					if (grain && sim->any_can_gather((const uint32_t *)selected_ids.ptr(), selected_ids.size(), grain))
						want = "action-gather-grain";
					// "attack" (9) before "capture" (10); session.capture (C) captures (structure_click).
					else if (b.team != me && sim->is_hostile(me, b.team)) {
						const StructureClick c = structure_click(b.id);
						want = c == CLICK_CAPTURE ? "action-capture" : c == CLICK_ATTACK ? "action-attack" : "";
					}
					// "capture" (10) recaptures an own, allied or neutral structure an enemy holds points of.
					else if (!hold_garrison && !hold_turret && structure_click(b.id) == CLICK_CAPTURE) want = "action-capture";
					else if (b.team != me && !sim->is_ally(me, b.team)) want = ""; // neutral
					// unit_actions "repair" needs a builder; without one "move" wins: no cursor.
					else if (sel_builder && (b.build_progress < 1.0f || (b.type == rtso::BLD_FIELD && b.team == me))) want = "action-build";
					else if (sel_builder && b.hp < b.max_hp) want = "action-repair"; // own or allied
					// unit_actions "garrison" (session.garrison): an own or allied holder whose List takes a
					// selected unit; a wall, gate or outpost has only turret points: "occupy-turret" (21).
					// Without the hotkeys "move" (12) wins: no cursor.
					else if (hold_turret && sim->turret_only_holder(b) && selection_can_garrison(b.id)) {
						want = "action-occupy-turret";
						const int occupied = sim->get_garrison_occupied(b.id), capacity = sim->get_garrison_capacity(b.id);
						tip = rtso::sprintf0(rtso::tr0("Current turrets: %(occupied)s/%(capacity)s"),
								{{"occupied", String::num_int64(occupied)}, {"capacity", String::num_int64(capacity)}});
						tip_orange = occupied >= capacity;
					}
					else if (hold_garrison && selection_can_garrison(b.id)) {
						garrison_cursor();
					}
					// unit_actions "occupy-turret" (session.occupyturret): the rampart tower's points.
					else if (rtso::hotkeys::held("session.occupyturret") && selection_can_occupy_turret(b.id)) {
						want = "action-occupy-turret";
						const int occupied = (int)sim->turret_list(b).size(), capacity = sim->turret_holder_points(b)->count;
						tip = rtso::sprintf0(rtso::tr0("Current turrets: %(occupied)s/%(capacity)s"),
								{{"occupied", String::num_int64(occupied)}, {"capacity", String::num_int64(capacity)}});
						tip_orange = occupied >= capacity;
					}
					break;
				}
			}
		}
	}
	if (cursor_tip_) {
		const Color tip_color = tip_orange ? Color(1.0f, 0.65f, 0.0f) : Color(1.0f, 1.0f, 1.0f);
		if (cursor_tip_->get_theme_color("font_color") != tip_color) cursor_tip_->add_theme_color_override("font_color", tip_color);
		cursor_tip_->set_visible(!tip.is_empty());
		if (!tip.is_empty()) {
			if (cursor_tip_->get_text() != tip) cursor_tip_->set_text(tip);
			cursor_tip_->set_position(mouse + Vector2(24, 20));
		}
	}
	if (cursor_name_ == want) return;
	cursor_name_ = want;
	Input *in = Input::get_singleton();
	if (!*want) {
		reset_default_cursor();
		return;
	}
	Ref<Texture2D> tex = load_tex(String("res://assets/textures/cursors/") + want + ".png");
	if (tex.is_null()) {
		// Cursors not ported yet (-disabled variants, cancel-setup-trade-route) use the base one.
		String base = String(want).trim_suffix("-disabled").replace("action-cancel-", "action-");
		tex = load_tex(String("res://assets/textures/cursors/") + base + ".png");
	}
	in->set_custom_mouse_cursor(tex, Input::CURSOR_ARROW, hot);
}

// Engine.PickEntityAtPoint: the one entity under the mouse that the viewed player sees, as
// the hover highlights it and a right click acts on it (input.js determineAction): a unit,
// else a structure the ray enters, an animal, a treasure, Gaia scenery, or a resource as the
// player last saw it. 0 over bare ground. The hover leaves treasures and scenery out
// (`with_props` false): RTS0 draws no highlight for them.
uint32_t RtsoMain::entity_under_mouse(Vector2 mouse, bool with_props) const {
	if (!sim) return 0;
	// A unit's own body (about its selection box), not the generous reach a left click
	// selects with: a woman at a tree doesn't hide the tree.
	const size_t u = own_unit_under_mouse(mouse, true, 0.6f);
	if (u != (size_t)-1) return sim->units().id[u];
	if (const uint32_t bid = building_under_mouse(mouse, nullptr, true); bid != 0) return bid;
	const Vector3 at = get_ground_click_pos(mouse);
	auto fv = sim->fauna();
	float best = 2.5f * 2.5f;
	uint32_t pick = 0;
	for (size_t f = 0; f < fv.count; ++f) {
		if (!fv.alive[f] || fv.behavior[f] == rtso::FAUNA_CORRALLED) continue;
		const float dx = fv.pos_x[f] - at.x, dz = fv.pos_z[f] - at.z;
		if (dx * dx + dz * dz >= best) continue;
		if (!rtso::los_sees_fauna(*sim, view_player(), fv.pos_x[f], fv.pos_z[f], fv.owner[f], fv.fauna_type[f])) continue;
		best = dx * dx + dz * dz;
		pick = fv.id[f];
	}
	if (pick) return pick;
	float best_t = 2.0f * 2.0f;
	for (const auto &t : sim->treasures()) {
		if (!with_props) break;
		if (!sim->los_explored(view_player(), t.x, t.z)) continue;
		const float dx = t.x - at.x, dz = t.z - at.z;
		if (dx * dx + dz * dz < best_t) { best_t = dx * dx + dz * dz; pick = t.id; }
	}
	if (pick) return pick;
	if (const uint32_t sid = with_props ? scenery_at(at.x, at.z) : 0; sid != 0) return sid;
	// A tree, mine, bush or shoal as the player last saw it (Fogging.js mirages are
	// hoverable): the one a right click would gather from.
	float best_r = 2.2f * 2.2f;
	if (view) {
		for (const auto &r : view->resource_mirages()) {
			if (r.amount <= 0 || r.subtype == rtso::RES_SUB_GRAIN) continue;
			const float dx = r.x - at.x, dz = r.z - at.z;
			if (dx * dx + dz * dz >= best_r) continue;
			best_r = dx * dx + dz * dz;
			pick = r.id;
		}
	}
	return pick;
}

// input.js INPUT_NORMAL mousemotion and INPUT_BANDBOXING: setHighlightList([PickEntityAtPoint])
// while the mouse is over the world, and the band box's preferred entities while dragging.
void RtsoMain::update_highlight() {
	PackedInt32Array ids;
	Viewport *vp = get_viewport();
	if (sim && vp && view && !sim->cinema_playing()) {
		const Vector2 mouse = vp->get_mouse_position();
		if (dragging && drag_start.distance_to(mouse) >= user_opts_.drag_delta) {
			const std::vector<int32_t> boxed = band_box_ids(drag_start, mouse);
			for (int32_t id : boxed) ids.push_back(id);
			if (boxed.empty())
				for (uint32_t id : band_box_structures(drag_start, mouse)) ids.push_back((int32_t)id);
		} else if (!dragging && !build_mode && !wall_drag_mode && vp->gui_get_hovered_control() == nullptr) {
			Camera3D *cam = cam_rig ? cam_rig->get_camera() : nullptr;
			const Transform3D cam_xf = cam ? cam->get_global_transform() : Transform3D();
			const uint64_t now = Time::get_singleton()->get_ticks_msec();
			if (mouse == hover_mouse_ && cam_xf == hover_cam_ && now - hover_at_ms_ < kHoverRepickMs) {
				ids = hover_pick_; // nothing moved: the last pick stands
			} else {
			hover_mouse_ = mouse;
			hover_cam_ = cam_xf;
			hover_at_ms_ = now;
			hover_target_ = entity_under_mouse(mouse, true);
			if (const uint32_t pick = entity_under_mouse(mouse, false)) ids.push_back((int32_t)pick);
			hover_pick_ = ids;
			}
		}
	}
	// session.js updateAdditionalHighlight: PageDown rings the units guarding the selection,
	// PageUp the entities the selection guards.
	if (sim && view && (!selected_ids.is_empty() || selected_building_id != 0)) {
		const bool guarding = rtso::hotkeys::held("session.highlightguarding");
		const bool guarded = rtso::hotkeys::held("session.highlightguarded");
		if (guarding || guarded) {
			auto add = [&](int32_t id) {
				if (id != 0 && !ids.has(id) && !selected_ids.has(id)) ids.push_back(id);
			};
			auto uv = sim->units();
			for (size_t i = 0; i < uv.count && guarding; ++i)
				if (uv.alive[i]) {
					const uint32_t t = sim->unit_guard_target(uv.id[i]);
					if (t != 0 && (selected_ids.has((int32_t)t) || t == selected_building_id)) add((int32_t)uv.id[i]);
				}
			for (int s = 0; s < selected_ids.size() && guarded; ++s) add((int32_t)sim->unit_guard_target((uint32_t)selected_ids[s]));
		}
	}
	if (ids != highlight_ids_) {
		highlight_ids_ = ids;
		if (view) view->set_highlight_ids(ids);
	}
}

// The local player's structure whose box the mouse ray enters first: its
// turned footprint (hu, hv) from the ground up to a height that grows with its
// size, so a click on a roof selects it and not the ground behind it. With
// `any_player`, any structure the viewer sees, live or as a mirage
// (Engine.PickEntityAtPoint).
uint32_t RtsoMain::building_under_mouse(Vector2 mouse_pos, float *hit_t, bool any_player) const {
	if (!sim || !cam_rig || !cam_rig->get_camera()) return 0;
	Camera3D *cam = cam_rig->get_camera();
	const Vector3 o = cam->project_ray_origin(mouse_pos);
	const Vector3 d = cam->project_ray_normal(mouse_pos);
	uint32_t best = 0;
	float best_t = 1e30f;
	// Built on the first structure the ray could hit, not for every pick.
	std::unordered_set<uint32_t> seen;
	bool seen_built = false;
	auto is_seen = [&](uint32_t id) {
		if (!seen_built) {
			seen_built = true;
			if (view)
				for (const auto &fb : view->seen_buildings())
					if (!fb.wrecked) seen.insert(fb.id);
		}
		return seen.count(id) != 0;
	};
	for (const auto &b : sim->buildings()) {
		if (any_player ? b.wrecked : b.team != local_player()) continue;
		const float hu = (b.hu > 0.0f ? b.hu : b.half_size) + 0.3f;
		const float hv = (b.hv > 0.0f ? b.hv : b.half_size) + 0.3f;
		const float base = sim->get_terrain_height(b.x, b.z);
		// The ray in the structure's frame: u along (ux, uz), v across it.
		const float px = o.x - b.x, pz = o.z - b.z;
		const float po[3] = {px * b.ux + pz * b.uz, o.y, -px * b.uz + pz * b.ux};
		const float pd[3] = {d.x * b.ux + d.z * b.uz, d.y, -d.x * b.uz + d.z * b.ux};
		auto slab = [&](float top, float &t0) {
			const float lo[3] = {-hu, base - 0.5f, -hv}, hi[3] = {hu, top, hv};
			t0 = 0.0f;
			float t1 = 1e30f;
			for (int a = 0; a < 3; ++a) {
				if (std::fabs(pd[a]) < 1e-6f) {
					if (po[a] < lo[a] || po[a] > hi[a]) return false;
					continue;
				}
				float ta = (lo[a] - po[a]) / pd[a], tb = (hi[a] - po[a]) / pd[a];
				if (ta > tb) std::swap(ta, tb);
				t0 = std::max(t0, ta);
				t1 = std::min(t1, tb);
				if (t0 > t1) return false;
			}
			return true;
		};
		// A loose box first (taller than any model), so most structures cost no model lookup.
		float t0 = 0.0f;
		if (!slab(base + 64.0f, t0) || t0 >= best_t) continue;
		if (any_player && !is_seen(b.id)) continue;
		// The model's own height (ModelLibrary bounds), else one from its size.
		float height = std::clamp(std::max(hu, hv) * 1.2f, 1.5f, 8.0f);
		const auto model = rtso::ModelLibrary::get("buildings", String(rtso::civ_building_model(sim->team_civ(b.team), b.type)));
		if (model.valid && model.aabb.size.y > 0.1f)
			height = model.y_offset + model.aabb.size.y * (model.scale > 0.05f ? model.scale : 1.0f);
		const bool hit = slab(base + height, t0);
		if (hit && t0 < best_t) {
			best_t = t0;
			best = b.id;
		}
	}
	if (hit_t) *hit_t = best_t;
	return best;
}

Vector3 RtsoMain::get_ground_click_pos(Vector2 mouse_pos) const {
	if (!cam_rig || !cam_rig->get_camera()) return Vector3();
	Camera3D *cam = cam_rig->get_camera();
	Vector3 ray_origin = cam->project_ray_origin(mouse_pos);
	Vector3 ray_dir = cam->project_ray_normal(mouse_pos);
	if (std::abs(ray_dir.y) < 0.0001f) return Vector3();
	const float t_plane = -ray_origin.y / ray_dir.y;
	// The ground the player sees is the heightmap, not y = 0 (0 A.D.'s
	// GetTerrainCursorPosition): walk the ray down until it passes under the
	// terrain, then bisect that step. Without a heightmap, or when the ray
	// never meets it, the y = 0 plane stands in.
	if (sim && ray_dir.y < 0.0f && !sim->terrain_heights().empty()) {
		constexpr float STEP = 0.5f;
		const float t_end = std::max(t_plane, 0.0f) + 400.0f;
		float prev = 0.0f;
		for (float t = 0.0f; t <= t_end; t += STEP) {
			const Vector3 p = ray_origin + ray_dir * t;
			if (p.y > sim->get_terrain_height(p.x, p.z)) {
				prev = t;
				continue;
			}
			float lo = prev, hi = t;
			for (int i = 0; i < 12; ++i) {
				const float mid = 0.5f * (lo + hi);
				const Vector3 m = ray_origin + ray_dir * mid;
				(m.y > sim->get_terrain_height(m.x, m.z) ? lo : hi) = mid;
			}
			const Vector3 hit = ray_origin + ray_dir * hi;
			return Vector3(hit.x, sim->get_terrain_height(hit.x, hit.z), hit.z);
		}
	}
	return ray_origin + ray_dir * t_plane;
}

// MiniMap.js onWorldClick, right button: determineAction with no entity under the
// cursor, so a structure sets its rally point and units walk there (attack-walk
// with session.attackmove), queued with session.queue.
// MiniMap.js onWorldClick, right button: an armed action or flare is only cancelled;
// otherwise the order determineAction(fromMiniMap) gives at that point.
void RtsoMain::_on_minimap_order(Vector2 world_pos) {
	if (!sim) return;
	if (flare_armed_) { // INPUT_FLARE: a right click cancels it
		flare_armed_ = false;
		return;
	}
	if (flare_held()) {
		trigger_flare(world_pos.x, world_pos.y);
		return;
	}
	if (any_armed_mode()) { // INPUT_PRESELECTEDACTION: a right click cancels it
		attack_move_mode = patrol_mode = guard_mode = call_to_arms_mode = garrison_mode = turret_mode = repair_mode = false;
		return;
	}
	minimap_unit_order(world_pos);
}

// determineAction(fromMiniMap): no entity is under a minimap click, so only the ground actions
// apply: call-to-arms, attack-walk, patrol, walk, or (structures alone) set-rallypoint.
// Returns false when no action applies.
bool RtsoMain::minimap_unit_order(Vector2 world_pos) {
	if (spectating()) return false;
	const bool queued = rtso::hotkeys::held("session.queue");
	const uint8_t qbit = queued ? rtso::NET_PARAM_QUEUED : 0;
	if (selected_ids.is_empty()) {
		if (selected_building_id == 0) return false;
		for (uint32_t sb : selected_structures()) { // unit_actions.js "set-rallypoint": every selected structure
			if (sb != selected_building_id && !building_trains_units(sb)) continue;
			if (turn_cmds()) {
				uint32_t extra[2] = {0, 0};
				post_lockstep(14, extra, 2, world_pos.x, world_pos.y, sb, qbit);
			} else {
				sim->cmd_set_rally_point(local_player(), sb, world_pos.x, world_pos.y, 0, 0, queued);
			}
		}
		return true;
	}
	const uint32_t *sel_ptr = (const uint32_t *)selected_ids.ptr();
	size_t sel_n = selected_ids.size();
	// session.orderone: the first unit only, which then leaves the selection.
	uint32_t one_id = 0;
	if (rtso::hotkeys::held("session.orderone")) {
		one_id = sel_ptr[0];
		sel_ptr = &one_id;
		sel_n = 1;
	}
	const bool units_only = rtso::hotkeys::held("session.attackmoveUnit"); // targetClasses Unit
	// Only the ground actions can be armed here; Guard, Garrison, Repair and Occupy Turret need an entity.
	if (garrison_mode || guard_mode || repair_mode || turret_mode) return false;
	range_overlays_.add_target_marker(world_pos.x, world_pos.y);
	if (call_to_arms_mode || rtso::hotkeys::held("session.calltoarms")) {
		const bool capture = rtso::hotkeys::held("session.capture");
		if (turn_cmds())
			post_lockstep((uint8_t)rtso::NetCommandType::NET_CMD_CALL_TO_ARMS, sel_ptr, sel_n, world_pos.x, world_pos.y, 0,
					(uint8_t)(qbit | (units_only ? rtso::NET_PARAM_UNITS_ONLY : 0) | (capture ? rtso::NET_PARAM_CAPTURE : 0)));
		else
			sim->order_call_to_arms(sel_ptr, sel_n, world_pos.x, world_pos.y, queued, units_only, capture);
		play_civ_voice("attack");
	} else if (attack_move_mode || rtso::hotkeys::held("session.attackmove") || units_only) {
		if (turn_cmds())
			post_lockstep(10, sel_ptr, sel_n, world_pos.x, world_pos.y, 0,
					(uint8_t)(qbit | rtso::NET_PARAM_NO_CAPTURE | (units_only ? rtso::NET_PARAM_UNITS_ONLY : 0)));
		else
			sim->order_attack_move(sel_ptr, sel_n, world_pos.x, world_pos.y, queued, units_only, false);
		play_civ_voice("attack");
	} else if (patrol_mode || rtso::hotkeys::held("session.patrol")) {
		if (turn_cmds())
			post_lockstep(11, sel_ptr, sel_n, world_pos.x, world_pos.y, 0, qbit);
		else {
			apply_default_formation(sel_ptr, sel_n);
			sim->order_patrol(sel_ptr, sel_n, world_pos.x, world_pos.y, queued);
		}
		// "order_patrol" has no sound group in 0.28, so the order is silent.
	} else {
		if (turn_cmds())
			post_lockstep(1, sel_ptr, sel_n, world_pos.x, world_pos.y, 0, qbit);
		else {
			apply_default_formation(sel_ptr, sel_n);
			sim->order_walk(sel_ptr, sel_n, world_pos.x, world_pos.y, queued, true);
		}
		play_civ_voice("move");
	}
	if (one_id != 0) { // input.js popOneFromSelection
		const int64_t at = selected_ids.find((int32_t)one_id);
		if (at >= 0) selected_ids.remove_at(at);
	}
	return true;
}

bool RtsoMain::trade_route_click(const rtso::Simulation::Building &b, bool execute, bool queued, String *tooltip, const char **cursor) {
	if (!sim || selected_ids.is_empty() || b.wrecked || b.build_progress < 1.0f) return false;
	const uint8_t me = local_player();
	auto uv = sim->units();
	std::vector<uint32_t> traders;
	uint8_t kind = rtso::UNIT_TRADER;
	for (int s = 0; s < selected_ids.size(); ++s) {
		const uint32_t id = (uint32_t)(int64_t)selected_ids[s];
		for (size_t i = 0; i < uv.count; ++i) {
			if (uv.id[i] != id) continue;
			// getActionInfo: a market or dock for Organic traders, a dock for ships, not an enemy's.
			if (uv.alive[i] && uv.team[i] == me && rtso::unit_is_trader(uv.unit_type[i]) &&
					sim->can_trade_at(me, b, uv.unit_type[i])) {
				traders.push_back(id);
				kind = uv.unit_type[i];
			}
			break;
		}
	}
	if (traders.empty()) return false;
	std::sort(traders.begin(), traders.end());

	auto building = [&](uint32_t id) -> const rtso::Simulation::Building * {
		for (const auto &o : sim->buildings())
			if (o.id == id && !o.wrecked && sim->can_trade_at(me, o, kind)) return &o;
		return nullptr;
	};
	auto gain_text = [&](const rtso::Simulation::Building &from, const rtso::Simulation::Building &to) {
		const auto g = sim->calculate_trade_gain(from, to, me, kind);
		return rtso::seldet::trading_gain(g.trader, me, g.market1, to.team, g.market2, from.team,
				[this](uint8_t pid) { return player_display_name(pid); });
	};
	if (cursor) *cursor = "action-setup-trade-route";
	auto set_first = [&]() {
		if (tooltip) *tooltip = tr("Right-click to set as origin trade market");
		if (execute) {
			trade_origin_market_ = b.id;
			trade_origin_traders_ = traders;
			play_civ_voice("trade");
		}
		return true;
	};

	// GetTradingDetails reads the first trader's own route: over its markets, "is first" /
	// "is second" with the route's gain. A click there starts a new route from that market
	// (Trader.SetTargetMarket with both markets set).
	uint32_t ma = 0, mb = 0;
	int leg = 0;
	if (sim->trader_route(traders.front(), ma, mb, leg) && ma && mb) {
		const rtso::Simulation::Building *a = building(ma), *d = building(mb);
		if (a && d && (b.id == ma || b.id == mb)) {
			if (tooltip)
				*tooltip = tr(b.id == ma ? "Origin trade market." : "Destination trade market.") + "\n" +
						sprintf0(tr("Gain: %(gain)s"), {{"gain", gain_text(*a, *d)}});
			if (execute) set_first();
			return true;
		}
	}
	const rtso::Simulation::Building *origin = nullptr;
	if (trade_origin_market_ && trade_origin_traders_ == traders) origin = building(trade_origin_market_);
	if (!origin) return set_first();
	if (origin->id == b.id) {
		// "cancel-setup-trade-route": the picked origin, before a destination.
		if (cursor) *cursor = "action-cancel-setup-trade-route";
		if (tooltip) *tooltip = tr("This is the origin trade market.\nRight-click to cancel trade route.");
		if (execute) {
			trade_origin_market_ = 0;
			trade_origin_traders_.clear();
		}
		return true;
	}
	const auto g = sim->calculate_trade_gain(*origin, b, me, kind);
	if (g.trader == 0) {
		if (cursor) *cursor = "action-setup-trade-route-disabled";
		if (tooltip) *tooltip = tr("This market is too close to the origin market.");
		return true;
	}
	if (tooltip)
		*tooltip = tr("Right-click to set as destination trade market.") + "\n" + sprintf0(tr("Gain: %(gain)s"), {{"gain", gain_text(*origin, b)}});
	if (execute) {
		if (turn_cmds())
			post_lockstep((uint8_t)rtso::NetCommandType::NET_CMD_SETUP_TRADE_ROUTE, traders.data(), traders.size(),
					(float)b.id, 0, origin->id, queued ? rtso::NET_PARAM_QUEUED : 0);
		else
			sim->order_trade(traders.data(), traders.size(), origin->id, b.id, queued);
		play_civ_voice("trade");
		trade_origin_market_ = 0;
		trade_origin_traders_.clear();
	}
	return true;
}

// input.js updateCursorAndTooltip / handleInputBeforeGui: session.flare held arms a flare,
// never in a replay (!g_IsReplay).
bool RtsoMain::selected_building_shoots() const {
	if (!sim || selected_building_id == 0 || !selected_ids.is_empty()) return false;
	for (const auto &b : sim->buildings())
		if (b.id == selected_building_id)
			return b.team == local_player() && rtso::building_ai_info(b.type).shoots();
	return false;
}

bool RtsoMain::flare_held() const {
	return !turn_mgr_.is_replaying() && rtso::hotkeys::held("session.flare");
}
