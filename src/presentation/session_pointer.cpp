// RtsoMain's pointer: the cursor for what is under the mouse, ground and building picks,
// minimap orders and trade route clicks (P4.6, split out of main_node.cpp).

#include "main_node.h"
#include "l10n.h"
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

void RtsoMain::update_cursor() {
	Viewport *vp = get_viewport();
	if (!sim || !vp) return;
	const char *want = "";
	String tip; // the action's tooltip, shown beside the cursor (0 A.D. cursor tooltips)
	Vector2 hot(1, 1);
	const Vector2 mouse = vp->get_mouse_position();
	const bool over_ui = vp->gui_get_hovered_control() != nullptr;
	if (!over_ui && !build_mode) {
		if (rtso::hotkeys::held("session.flare")) {
			want = "action-flare";
		} else if (selected_building_id != 0 && selected_ids.is_empty()) {
			if (over_own_rally_structure(get_ground_click_pos(mouse))) {
				want = "action-unset-rally";
			} else {
				want = "cursor-rally";
				hot = Vector2(5, 31);
			}
		} else if (!selected_ids.is_empty()) {
			const Vector3 at = get_ground_click_pos(mouse);
			const uint8_t me = local_player();
			auto near2 = [&](float x, float z, float r) { return (x - at.x) * (x - at.x) + (z - at.z) * (z - at.z) <= r * r; };
			if (attack_move_mode || call_to_arms_mode || rtso::hotkeys::held("session.attackmove") ||
					rtso::hotkeys::held("session.attackmoveUnit") || rtso::hotkeys::held("session.calltoarms"))
				want = "action-attack-move";
			else if (patrol_mode || rtso::hotkeys::held("session.patrol")) want = "action-patrol";
			else if (guard_mode || rtso::hotkeys::held("session.guard")) want = "action-guard";
			else if (garrison_mode) want = "action-garrison";
			else if (repair_mode) want = "action-repair";
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
				for (size_t i = 0; i < uv.count && !*want; ++i) {
					if (!uv.alive[i] || !near2(uv.pos_x[i], uv.pos_z[i], 2.2f)) continue;
					if (sim->is_hostile(me, uv.team[i])) want = "action-attack";
					// unit_actions "repair": a builder over a damaged own or allied siege engine or ship.
					else if (builder && uv.hp[i] < uv.max_hp[i] && (uv.team[i] == me || sim->is_ally(me, uv.team[i])) &&
							sim->is_repairable_unit(uv.id[i]))
						want = "action-repair";
					// 0 A.D. unit_actions "heal": a healer over a wounded own or allied unit.
					else if (healer && uv.hp[i] < uv.max_hp[i] && (uv.team[i] == me || sim->is_ally(me, uv.team[i])))
						want = "action-heal";
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
					if (collector && near2(t.x, t.z, 2.0f)) { want = "action-collect-treasure"; break; }
			}
			if (!*want) {
				auto fv = sim->fauna();
				for (size_t f = 0; f < fv.count && !*want; ++f) {
					// A penned animal is garrisoned in its corral: nothing to do with it.
					if (!fv.alive[f] || fv.behavior[f] == rtso::FAUNA_CORRALLED || !near2(fv.pos_x[f], fv.pos_z[f], 2.5f)) continue;
					// Slaughter for livestock as for game (0 A.D.); Alt herds your own to a corral.
					const bool herd = rtso::fauna_type_info(fv.fauna_type[f]).is_domestic() && fv.owner[f] == local_player() &&
					                  Input::get_singleton()->is_key_pressed(KEY_ALT);
					want = herd ? "action-herd" : "action-gather-meat";
				}
			}
			if (!*want) {
				if (const uint32_t sid = scenery_at(at.x, at.z); sid != 0) {
					const int32_t k = sim->scenery_index(sid);
					const bool ctrl = rtso::hotkeys::held("session.attack") || rtso::hotkeys::held("session.attackmove");
					want = k >= 0 && sim->scenery_capturable(sim->scenery()[k]) && !ctrl ? "action-capture" : "action-attack";
				}
			}
			if (!*want) {
				auto rv = sim->resources();
				for (size_t r = 0; r < rv.count && !*want; ++r) {
					if (!rv.alive[r] || rv.amount[r] <= 0 || !near2(rv.x[r], rv.z[r], 2.2f)) continue;
					switch (rv.type[r]) {
						case rtso::RES_WOOD: want = "action-gather-tree"; break;
						case rtso::RES_GOLD: want = "action-gather-ore"; break;
						case rtso::RES_STONE: want = "action-gather-rock"; break;
						default:
							want = rv.subtype[r] == rtso::RES_SUB_MEAT ? "action-gather-meat"
									: rv.subtype[r] == rtso::RES_SUB_GRAIN ? "action-gather-grain"
									: rv.subtype[r] == rtso::RES_SUB_FISH ? "action-gather-fish" : "action-gather-fruit";
					}
				}
			}
			if (!*want) {
				for (const auto &b : sim->buildings()) {
					if (b.wrecked || !near2(b.x, b.z, b.half_size + 1.2f)) continue;
					if (trade_route_click(b, false, false, &tip)) {
						want = "action-setup-trade-route";
						break;
					}
					// 0 A.D. "capture" is the default on enemy structures; session.attack
					// (or attack-move's Ctrl) attacks instead.
					if (b.team == rtso::GAIA_PLAYER && b.type == rtso::BLD_FIELD && sim->field_resource_id(b.id))
						want = "action-gather-grain";
					else if (b.team != me && sim->is_hostile(me, b.team))
						want = (rtso::hotkeys::held("session.attack") || rtso::hotkeys::held("session.attackmove") ||
										  b.max_capture_points <= 0.0f)
								? "action-attack" : "action-capture";
					else if (b.team != me && !sim->is_ally(me, b.team)) want = ""; // neutral
					else if (b.build_progress < 1.0f || (b.type == rtso::BLD_FIELD && b.team == me)) want = "action-build";
					else if (b.hp < b.max_hp) want = "action-repair"; // own or allied (unit_actions "repair")
					// unit_actions "garrison": an own or allied holder whose List takes a selected unit.
					else if (selection_can_garrison(b.id)) want = "action-garrison";
					break;
				}
			}
		}
	}
	if (cursor_tip_) {
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
		in->set_custom_mouse_cursor(Ref<Resource>());
		return;
	}
	Ref<Texture2D> tex = load_tex(String("res://assets/textures/cursors/") + want + ".png");
	in->set_custom_mouse_cursor(tex, Input::CURSOR_ARROW, hot);
}

// The local player's structure whose box the mouse ray enters first: its
// turned footprint (hu, hv) from the ground up to a height that grows with its
// size, so a click on a roof selects it and not the ground behind it.
uint32_t RtsoMain::building_under_mouse(Vector2 mouse_pos, float *hit_t) const {
	if (!sim || !cam_rig || !cam_rig->get_camera()) return 0;
	Camera3D *cam = cam_rig->get_camera();
	const Vector3 o = cam->project_ray_origin(mouse_pos);
	const Vector3 d = cam->project_ray_normal(mouse_pos);
	uint32_t best = 0;
	float best_t = 1e30f;
	for (const auto &b : sim->buildings()) {
		if (b.team != local_player()) continue;
		const float hu = (b.hu > 0.0f ? b.hu : b.half_size) + 0.3f;
		const float hv = (b.hv > 0.0f ? b.hv : b.half_size) + 0.3f;
		const float base = sim->get_terrain_height(b.x, b.z);
		// The model's own height (ModelLibrary bounds), else one from its size.
		float height = std::clamp(std::max(hu, hv) * 1.2f, 1.5f, 8.0f);
		const auto model = rtso::ModelLibrary::get("buildings", String(rtso::civ_building_model(sim->team_civ(b.team), b.type)));
		if (model.valid && model.aabb.size.y > 0.1f)
			height = model.y_offset + model.aabb.size.y * (model.scale > 0.05f ? model.scale : 1.0f);
		const float top = base + height;
		// The ray in the structure's frame: u along (ux, uz), v across it.
		const float px = o.x - b.x, pz = o.z - b.z;
		const float po[3] = {px * b.ux + pz * b.uz, o.y, -px * b.uz + pz * b.ux};
		const float pd[3] = {d.x * b.ux + d.z * b.uz, d.y, -d.x * b.uz + d.z * b.ux};
		const float lo[3] = {-hu, base - 0.5f, -hv}, hi[3] = {hu, top, hv};
		float t0 = 0.0f, t1 = 1e30f;
		bool hit = true;
		for (int a = 0; a < 3 && hit; ++a) {
			if (std::fabs(pd[a]) < 1e-6f) {
				hit = po[a] >= lo[a] && po[a] <= hi[a];
				continue;
			}
			float ta = (lo[a] - po[a]) / pd[a], tb = (hi[a] - po[a]) / pd[a];
			if (ta > tb) std::swap(ta, tb);
			t0 = std::max(t0, ta);
			t1 = std::min(t1, tb);
			hit = t0 <= t1;
		}
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
void RtsoMain::_on_minimap_order(Vector2 world_pos) {
	if (!sim) return;
	if (rtso::hotkeys::held("session.flare")) {
		trigger_flare(world_pos.x, world_pos.y);
		return;
	}
	const bool queued = rtso::hotkeys::held("session.queue");
	const uint8_t qbit = queued ? rtso::NET_PARAM_QUEUED : 0;
	if (selected_building_id != 0) {
		if (turn_cmds()) {
			uint32_t extra[2] = {0, 0};
			post_lockstep(14, extra, 2, world_pos.x, world_pos.y, selected_building_id, qbit);
		} else {
			sim->cmd_set_rally_point(local_player(), selected_building_id, world_pos.x, world_pos.y, 0, 0, queued);
		}
		return;
	}
	if (selected_ids.is_empty()) return;
	const uint32_t *sel_ptr = (const uint32_t *)selected_ids.ptr();
	const size_t sel_n = selected_ids.size();
	range_overlays_.add_target_marker(world_pos.x, world_pos.y);
	const bool units_only = rtso::hotkeys::held("session.attackmoveUnit"); // targetClasses Unit
	if (rtso::hotkeys::held("session.attackmove") || units_only) {
		if (turn_cmds())
			post_lockstep(10, sel_ptr, sel_n, world_pos.x, world_pos.y, 0,
					(uint8_t)(qbit | (units_only ? rtso::NET_PARAM_UNITS_ONLY : 0)));
		else
			sim->order_attack_move(sel_ptr, sel_n, world_pos.x, world_pos.y, queued, units_only, true);
		play_civ_voice("attack");
		return;
	}
	if (turn_cmds())
		post_lockstep(1, sel_ptr, sel_n, world_pos.x, world_pos.y, 0, qbit);
	else
		sim->order_walk(sel_ptr, sel_n, world_pos.x, world_pos.y, queued, true);
	play_civ_voice("move");
}

bool RtsoMain::trade_route_click(const rtso::Simulation::Building &b, bool execute, bool queued, String *tooltip) {
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

	const rtso::Simulation::Building *origin = nullptr;
	if (trade_origin_market_ && trade_origin_traders_ == traders)
		for (const auto &o : sim->buildings())
			if (o.id == trade_origin_market_ && !o.wrecked && sim->can_trade_at(me, o, kind)) origin = &o;

	auto gain_text = [&](const rtso::Simulation::Building &from, const rtso::Simulation::Building &to) {
		const auto g = sim->calculate_trade_gain(from, to, me, kind);
		String t = String::num_int64(g.trader);
		if (g.market1 || g.market2) t += " (+" + String::num_int64(g.market1 + g.market2) + " to the markets' owners)";
		return t;
	};

	if (!origin || origin->id == b.id) {
		// "set first" / "is first" (Trader.js SetTargetMarket with no first market yet).
		if (tooltip) *tooltip = origin ? String("Origin trade market.") : String("Right-click to set as origin trade market");
		if (execute) {
			trade_origin_market_ = b.id;
			trade_origin_traders_ = traders;
			chat_window_.append_line("Origin trade market set. Right-click another market to start trading.");
			play_civ_voice("trade");
		}
		return true;
	}
	const auto g = sim->calculate_trade_gain(*origin, b, me, kind);
	if (g.trader == 0) {
		if (tooltip) *tooltip = "This market is too close to the origin market.";
		if (execute) chat_window_.append_line("This market is too close to the origin market.");
		return true;
	}
	if (tooltip) *tooltip = "Right-click to set as destination trade market.\nGain: " + gain_text(*origin, b);
	if (execute) {
		if (turn_cmds())
			post_lockstep((uint8_t)rtso::NetCommandType::NET_CMD_SETUP_TRADE_ROUTE, traders.data(), traders.size(),
					(float)b.id, 0, origin->id, queued ? rtso::NET_PARAM_QUEUED : 0);
		else
			sim->order_trade(traders.data(), traders.size(), origin->id, b.id, queued);
		chat_window_.append_line("Trade route set. Gain: " + gain_text(*origin, b));
		play_civ_voice("trade");
		trade_origin_market_ = 0;
		trade_origin_traders_.clear();
	}
	return true;
}
