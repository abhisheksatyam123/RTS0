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
#include <sstream>

using namespace godot;

using rtso::sprintf0;


// 0.28 CTurnManager::QuickSave keeps the quicksave in memory for this match only; the
// simulation writes its state to a file, so it goes outside user://saves (out of the save
// and load dialogs) and is deleted when the match ends.
static const char *const kQuicksavePath = "user://quicksave.rts0sav";
static const char *const kBackgroundSavePath = "user://saves/background.rts0sav";

RtsoMain::RtsoMain() {
	sim = new rtso::Simulation();
}

RtsoMain::~RtsoMain() {
	net_host_.stop();
	delete sim;
}

void RtsoMain::_notification(int p_what) {
	if (p_what == NOTIFICATION_WM_SIZE_CHANGED) {
		apply_safe_area_insets();
	}
	// 0 A.D. "pauseonfocusloss" (main.cpp Frame: isUnfocusedSP): while the window has
	// no focus a single-player game's simulation doesn't advance; it carries on when
	// focus returns, with no pause overlay.
	if (p_what == NOTIFICATION_APPLICATION_FOCUS_OUT) app_unfocused_ = true;
	if (p_what == NOTIFICATION_APPLICATION_FOCUS_IN) app_unfocused_ = false;
	// A phone may close a game left in the background to free memory: a single-player
	// match is saved first, to Load Game's "background" save (one, overwritten each time).
	// No thumbnail: the screen may already be gone.
	if (p_what == NOTIFICATION_APPLICATION_PAUSED && sim && !net_lockstep()) {
		DirAccess::make_dir_recursive_absolute(ProjectSettings::get_singleton()->globalize_path("user://saves"));
		if (sim->save_state(ProjectSettings::get_singleton()->globalize_path(kBackgroundSavePath).utf8().get_data()))
			write_save_metadata(kBackgroundSavePath, "Saved when the game went to the background");
	}
	if (p_what == NOTIFICATION_EXIT_TREE) {
		// The MenuAudio autoload outlives the match: stop driving the session's tick.
		if (SceneTree *exiting = get_tree()) {
			if (Node *ma = exiting->get_root()->get_node_or_null(NodePath("MenuAudio")))
				ma->set("session_music_tick", Callable());
		}
		write_replay(true); // leaving the match keeps its replay, with its summary (endGame)
		if (quicksaved_) DirAccess::remove_absolute(ProjectSettings::get_singleton()->globalize_path(kQuicksavePath));
		// Menus get the normal arrow back (update_cursor sets action cursors).
		if (!cursor_name_.empty()) reset_default_cursor();
		// The Render Debug Mode is the session's; the menus draw normally.
		if (render_debug_mode_ && get_viewport()) get_viewport()->set_debug_draw(Viewport::DEBUG_DRAW_DISABLED);
		if (SceneTree *t = get_tree()) {
			rtso::hide_node_popups(t->get_root());
		} else {
			rtso::hide_node_popups(this);
		}
	}
}

void RtsoMain::play_voice(const char *path) {
	if (!voice_player || !path) return;
	++voices_played_;
	String p = path;
	String final_path = p;
	if (!final_path.begins_with("res://")) {
		if (final_path.begins_with("audio/")) {
			final_path = "res://assets/sounds/" + final_path.substr(6);
		} else if (final_path.begins_with("assets/sounds/")) {
			final_path = "res://" + final_path;
		} else {
			final_path = "res://" + final_path;
		}
	}
	if (!ResourceLoader::get_singleton()->exists(final_path)) {
		if (ResourceLoader::get_singleton()->exists(p)) {
			final_path = p;
		} else if (ResourceLoader::get_singleton()->exists("res://" + p)) {
			final_path = "res://" + p;
		}
	}
	if (ResourceLoader::get_singleton()->exists(final_path)) {
		Ref<AudioStream> s = ResourceLoader::get_singleton()->load(final_path);
		if (s.is_valid()) {
			voice_player->set_stream(s);
			voice_player->play();
		}
	}
}

// Test hook: the selection, the selected structure's rally point, and every own
// unit with its template, rank, idleness, whether it is a Civilian,
// world position and screen position.
// The top-right counters (user_settings.gd): match time, sim speed, ceasefire left.
Dictionary RtsoMain::get_overlay_times() const {
	Dictionary d;
	d["elapsed"] = match_elapsed_time();
	d["speed"] = game_speed;
	d["ceasefire"] = sim ? sim->ceasefire_remaining() : 0.0f;
	return d;
}

// CounterManager.onPress: a click on a resource or population counter cycles
// the order of the allies' stats in their tooltips.
void RtsoMain::_on_top_counter_gui_input(const Ref<InputEvent> &event) {
	Ref<InputEventMouseButton> mb = event;
	if (mb.is_null() || !mb->is_pressed() || mb->get_button_index() != MOUSE_BUTTON_LEFT) return;
	RtsoTopPanel::cycle_order();
	top_panel_.invalidate();
	if (sim) top_panel_.update(sim, local_player(), match_elapsed_time(), spectating() || turn_mgr_.is_replaying());
}

Dictionary RtsoMain::get_audio_state() const {
	static const char *kStates[] = {"off", "menu", "peace", "battle", "victory", "defeat", "custom"};
	Dictionary d;
	d["music_state"] = kStates[music_.state()];
	d["track"] = music_.current_track();
	d["music_position"] = music_.current_position();
	PackedStringArray playlist;
	for (const std::string &t : music_.playlist_from_current()) playlist.push_back(String::utf8(t.c_str()));
	d["playlist"] = playlist;
	d["last_sound"] = session_audio_.last_played();
	d["in_battle"] = session_audio_.in_battle();
	d["work_sounds"] = session_audio_.work_sounds_played();
	d["workers_at_work"] = session_audio_.workers_at_work();
	d["workers_in_earshot"] = session_audio_.workers_in_earshot();
	d["listener"] = session_audio_.listener();
	d["combat_sounds"] = session_audio_.combat_sounds_played();
	d["combat_culled"] = session_audio_.combat_sounds_culled();
	d["voices"] = view ? view->spatial_voices_playing() : 0;
	d["voices_dropped"] = view ? view->spatial_sounds_dropped() : 0;
	// Ambient.js: the one looping dayscape track and its level.
	Array ambient;
	for (int i = 0; i < get_child_count(); ++i) {
		AudioStreamPlayer *p = Object::cast_to<AudioStreamPlayer>(get_child(i));
		if (!p || p->get_bus() != StringName("Ambient")) continue;
		Dictionary a;
		a["path"] = p->get_stream().is_valid() ? p->get_stream()->get_path() : String();
		a["db"] = p->get_volume_db();
		a["playing"] = p->is_playing();
		ambient.push_back(a);
	}
	d["ambient"] = ambient;
	return d;
}

void RtsoMain::update_music_state() {
	if (!sim) return;
	// session.js initializeMusic: the civ tracks of the player this client starts as,
	// stored once (an observer or a replay viewer, player -1, keeps the default list).
	const bool replay = turn_mgr_.is_replaying();
	const uint8_t seat = replay ? (uint8_t)0xFF : seat_player();
	const uint8_t civ = seat < rtso::MAX_PLAYERS ? sim->player_civ(seat) : (uint8_t)0xFE;
	if (civ != music_civ_) {
		music_civ_ = civ;
		music_.store_civ_tracks(civ);
		if (music_.state() != rtso::RtsoMusic::OFF) {
			const auto keep = music_.state();
			music_.set_state(rtso::RtsoMusic::OFF);
			music_.set_state(keep);
		}
	}
	// Player.OnGlobalPlayerDefeated: this client's player hears "defeated" when it
	// falls, "defeated_ally" when an ally does and "defeated_enemy" when an enemy
	// does, unless it has already won (template_player has no "won" group).
	uint32_t defeated = 0;
	for (uint8_t p = 0; p < rtso::MAX_PLAYERS; ++p)
		if (sim->is_defeated(p)) defeated |= 1u << p;
	if (music_primed_ && seat < rtso::MAX_PLAYERS) {
		for (uint8_t p = 0; p < rtso::MAX_PLAYERS; ++p) {
			if (!((defeated & ~defeated_heard_) >> p & 1)) continue;
			if (p == seat) session_audio_.play_defeat();
			else if (sim->is_ally(seat, p)) session_audio_.play_defeated_ally();
			else if (!sim->is_victory_for(seat)) session_audio_.play_defeated_enemy();
		}
	}
	defeated_heard_ = defeated;
	music_primed_ = true;
	// session.js onSimulationUpdate: battle music only while not an observer (a replay
	// viewer, an observer, or a player who has lost or won); VICTORY and DEFEAT only
	// for this client's own player (the music doesn't follow the viewed player).
	const bool observer = replay || seat >= rtso::MAX_PLAYERS || spectating();
	rtso::RtsoMusic::State want = !observer && session_audio_.in_battle() ? rtso::RtsoMusic::BATTLE : rtso::RtsoMusic::PEACE;
	if (seat < rtso::MAX_PLAYERS && sim->is_victory_for(seat)) want = rtso::RtsoMusic::VICTORY;
	else if (seat < rtso::MAX_PLAYERS && sim->is_defeated(seat)) want = rtso::RtsoMusic::DEFEAT;
	if (want != music_.state()) music_.set_state(want);
}

void RtsoMain::play_spatial_sfx(const char *path, Vector3 pos) {
	if (view) {
		view->play_spatial_sound_path(path, pos);
		return;
	}
	if (!spatial_sfx_player) return;
	if (ResourceLoader::get_singleton()->exists(path)) {
		Ref<AudioStream> s = ResourceLoader::get_singleton()->load(path);
		if (s.is_valid()) {
			spatial_sfx_player->set_position(pos);
			spatial_sfx_player->set_stream(s);
			spatial_sfx_player->play();
		}
	}
}

void RtsoMain::play_sfx(const char *path) {
	if (!sfx_player) return;
	if (ResourceLoader::get_singleton()->exists(path)) {
		Ref<AudioStream> s = ResourceLoader::get_singleton()->load(path);
		if (s.is_valid()) {
			sfx_player->set_stream(s);
			sfx_player->play();
		}
	}
}

void RtsoMain::play_alarm(const char *path) {
	if (!alarm_player) return;
	if (ResourceLoader::get_singleton()->exists(path)) {
		Ref<AudioStream> s = ResourceLoader::get_singleton()->load(path);
		if (s.is_valid()) {
			alarm_player->set_stream(s);
			alarm_player->play();
		}
	}
}

// Tactical flare: broadcast to session peers over NMT_CHAT (approximation of
// 0 A.D.'s dedicated flare message; receive side parses "[Flare] x,z" in the
// onChat callback registered in _ready()). Local ping already added by caller.
void RtsoMain::_on_flare_requested(Vector2 world_pos) {
	if (!net_lockstep()) return;
	std::string wire = "[Flare] " + std::to_string(world_pos.x) + "," + std::to_string(world_pos.y);
	net_host_.sendChat(wire);
}

// MiniMapFlareButton.onPress (and session.flareactivate): INPUT_FLARE, so the
// next left click on the map or minimap sends a flare; never in a replay.
void RtsoMain::_on_flare_button_pressed() {
	if (turn_mgr_.is_replaying() || !input_normal()) return; // onPress: only from INPUT_NORMAL
	flare_armed_ = true;
}


void RtsoMain::_on_minimap_clicked(Vector2 world_pos) {
	if (flare_armed_ || flare_held()) { // MiniMap.js onWorldClick: the flare wins
		flare_armed_ = false;
		trigger_flare(world_pos.x, world_pos.y);
		return;
	}
	// MiniMap.js onWorldClick: an armed ground action (call to arms, attack-walk, patrol) runs at
	// the clicked point and stays armed under Shift or Alt; otherwise the camera moves there.
	if ((attack_move_mode || patrol_mode || call_to_arms_mode) && !selected_ids.is_empty()) {
		const bool keep = rtso::hotkeys::held("session.queue") || rtso::hotkeys::held("session.orderone");
		const bool modes[3] = { attack_move_mode, patrol_mode, call_to_arms_mode };
		if (minimap_unit_order(world_pos)) {
			attack_move_mode = patrol_mode = call_to_arms_mode = false;
			if (keep && !selected_ids.is_empty()) attack_move_mode = modes[0], patrol_mode = modes[1], call_to_arms_mode = modes[2];
			return;
		}
	}
	// Garrison, repair, guard and occupy-turret need an entity, so on the minimap they are
	// "none": the click ends them (unless Shift or Alt is held) and leaves the camera.
	if ((garrison_mode || repair_mode || guard_mode || turret_mode) && !selected_ids.is_empty()) {
		if (!rtso::hotkeys::held("session.queue") && !rtso::hotkeys::held("session.orderone"))
			garrison_mode = repair_mode = guard_mode = turret_mode = false;
		return;
	}
	if (cam_rig) {
		cam_rig->set_target_pos(Vector3(world_pos.x, 0.0f, world_pos.y));
	}
}

// The sender's displayed colour (Ownership on flare_target_marker, MiniMap.flare's player colour).
Color RtsoMain::player_flare_color(uint8_t pid) const {
	// An observer's flare is Gaia's (unit_actions.js renderAndPlayFlare owner 0; MiniMap.flare
	// "Observers flare in white").
	if (!sim || pid >= rtso::MAX_PLAYERS) return Color(1.0f, 1.0f, 1.0f);
	const uint32_t rgb = rtso::display_colors::rgb(*sim, pid);
	return Color(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, 1.0f);
}

// input.js triggerFlareAction: one flare per g_FlareCooldown (3 s), shown in the
// world and on the minimap and sent to the other players.
void RtsoMain::trigger_flare(float x, float z) {
	const double now = Time::get_singleton()->get_ticks_msec() / 1000.0;
	if (now < last_flare_time_ + 3.0) return;
	last_flare_time_ = now;
	render_flare(x, z, own_seat(), String()); // the sender's seat, not a viewed player
	_on_flare_requested(Vector2(x, z));
}

// unit_actions.js renderAndPlayFlare, for the sender and every receiver: the
// flare_target_marker (Decay DelayTime 6 s) and minimap flare in the sender's colour, the
// minimap one lasting gui.session.flarelifetime, the flare sound and the chat line.
void RtsoMain::render_flare(float x, float z, uint8_t seat, const String &guid) {
	const Color c = player_flare_color(seat);
	range_overlays_.add_flare(x, z, c, 6.0f);
	if (minimap_view) minimap_view->add_ping(Vector2(x, z), c, user_opts_.flare_time);
	play_alarm("res://assets/audio/interface/alarm/alarmally_1.ogg");
	// ChatMessageFormatSimulation.flare: gui.session.notifications.flare
	// "never", "observer" (only while observing) or "always".
	const std::string when = rtso::user_config::get("gui.session.notifications.flare");
	if (!(when == "always" || (when == "observer" && chat_local_observer()))) return;
	// colorizePlayernameByGUID: an observer's name in white.
	const String name = seat < rtso::MAX_PLAYERS ? colored_player_name(seat) : chat_user(guid).replace("[color=#b3b3b3]", "[color=#ffffff]");
	chat_window_.append_line(sprintf0(tr("%(icon)s%(player)s has sent a flare."),
			{{"icon", "[img=16x16]res://assets/textures/ui/archive/icons/focus-flare.png[/img]"},
					{"player", name}}) + "\n",
			"flare:" + String::num(x) + "," + String::num(z), tr("Click to focus on the flare's location."));
}

void RtsoMain::_on_idle_worker_clicked() {
	cycle_idle_worker();
}

void RtsoMain::cycle_idle_worker() {
	find_idle_unit(IDLE_WORKER);
}

// Under-attack alerts (0 A.D. AttackDetection.js AttackAlert, called for every
// Attacked message): a hit on one of the local player's entities by another
// player runs 0 A.D.'s suppression rules (attack_detection.h) and may notify.
void RtsoMain::detect_attacks(const std::vector<rtso::Simulation::AttackedEvent> &attacks) {
	if (!sim) return;
	const uint8_t me = local_player();
	if (me != attack_watch_player_) {
		// New match seat (load, net start): forget suppressed attacks.
		attack_watch_player_ = me;
		attack_detection_.clear();
	}
	for (const auto &a : attacks) {
		// Attacks against other players, or dealt by myself, are not registered.
		if (a.target_owner != me || a.attacker_owner == me) continue;
		// AttackDetection.OnGlobalAttacked: every hit entity pings the minimap, before suppression.
		if (minimap_view) minimap_view->ping_entity(a.target_id);
		// AttackDetection.js: an attack on livestock is a low-priority event (targetIsDomesticAnimal).
		bool livestock = false;
		if (!a.target_is_building) {
			const auto fv = sim->fauna();
			for (size_t f = 0; f < fv.count; ++f)
				if (fv.id[f] == a.target_id) { livestock = rtso::fauna_type_info(fv.fauna_type[f]).is_domestic(); break; }
		}
		if (attack_detection_.report(a.x, a.z, match_elapsed_time(), livestock)) {
			notify_attack(Vector3(a.x, 0, a.z), a.attacker_owner, a.target_is_building != 0, a.target_id);
			// messages.js "attack": a followed player's attacked entity takes the camera and the selection.
			if (follow_player_ && followed_view() == (int)me) follow_focus(a.target_id, true);
		}
	}
}

// 0 A.D. messages.js "attack" notification: remember the spot for
// camera.lastattackfocus, play the "attacked" sound group
// (alarm_attackplayer, or alarm_attacked_gaia for Gaia) and add a chat line.
void RtsoMain::notify_attack(Vector3 pos, uint8_t attacker, bool structure, uint32_t target) {
	has_last_attack_ = true;
	last_attack_pos_ = pos;
	last_attack_unit_ = structure ? 0 : target;
	const bool gaia = attacker == rtso::GAIA_PLAYER;
	session_audio_.play_group("interface/alarm", gaia ? "alarm_attacked_gaia" : "alarmattackplayer", 0.25f);
	if (!chat_window_.is_ready() || !user_opts_.attack_notice) return;
	// ChatMessageFormatSimulation.attack: the attacker's name in its colour, livestock worded
	// apart (targetIsDomesticAnimal); a click runs focusAttack, following an attacked unit.
	bool livestock = false;
	if (!structure && sim) {
		auto fv = sim->fauna();
		for (size_t f = 0; f < fv.count; ++f)
			if (fv.id[f] == target) { livestock = rtso::fauna_type_info(fv.fauna_type[f]).is_domestic(); break; }
	}
	const String msg = livestock ? tr("%(icon)sYour livestock has been attacked by %(attacker)s!")
								 : tr("%(icon)sYou have been attacked by %(attacker)s!");
	chat_window_.append_line(sprintf0(msg, {{"icon", "[img=16x16]res://assets/textures/ui/archive/icons/focus-attacked.png[/img]"},
											 {"attacker", colored_player_name(attacker)}}) + "\n",
			"attack:" + String::num_int64(structure ? 0 : target) + ":" + String::num(pos.x) + "," + String::num(pos.z),
			tr("Click to focus on the attacked unit."));
}

// colorizePlayernameByID: the name in the player's displayed colour.
String RtsoMain::colored_player_name(uint8_t pid) const {
	const uint32_t rgb = sim ? rtso::display_colors::rgb(*sim, pid) : 0xFFFFFFu;
	return "[color=#" + String::num_int64(rgb, 16).lpad(6, "0") + "]" + player_display_name(pid) + "[/color]";
}

// Context cursors (0 A.D. gui/session/unit_actions.js "cursor" of each action
// and input.js updateCursorAndTooltip, art/textures/cursors/action-*.png): the
// cursor shows what a right-click would do with the current selection.
// unit_actions.js "unset-rallypoint" getActionInfo: the target is the selected
// structure itself and it has a rally point.
// unit_actions.js "focus-fire": right-clicking an enemy while holding session.focusfire
// (F) with a structure that shoots arrows selected sends Commands.js "focus-fire"
// (BuildingAI.AddFocusTarget; Shift queues it) instead of setting the rally point
// there. Without the hotkey the click sets the rally point, as in 0.28. Lockstep command 50.
bool RtsoMain::try_focus_fire(const Vector3 &at) {
	if (!rtso::hotkeys::held("session.focusfire")) return false;
	const uint8_t me = local_player();
	const auto bit = std::find_if(sim->buildings().begin(), sim->buildings().end(),
			[&](const rtso::Simulation::Building &b) { return b.id == selected_building_id; });
	if (bit == sim->buildings().end() || bit->team != me || !rtso::building_ai_info(bit->type).shoots()) return false;
	uint32_t target = 0;
	auto uview = sim->units();
	float best = 2.2f * 2.2f;
	for (size_t i = 0; i < uview.count; ++i) {
		if (!uview.alive[i] || !sim->is_hostile(me, uview.team[i])) continue;
		const float dx = uview.pos_x[i] - at.x, dz = uview.pos_z[i] - at.z, d2 = dx * dx + dz * dz;
		if (d2 < best) { best = d2; target = uview.id[i]; }
	}
	for (const auto &b : sim->buildings()) {
		if (target) break;
		if (b.wrecked || b.team >= rtso::MAX_PLAYERS || !sim->is_hostile(me, b.team)) continue;
		const float dx = b.x - at.x, dz = b.z - at.z, hit_r = b.half_size + 1.2f;
		if (dx * dx + dz * dz <= hit_r * hit_r) target = b.id;
	}
	if (!target) return false;
	const bool queued = rtso::hotkeys::held("session.queue");
	// input.js: pushFront = session.pushorderfront held (unbound by default).
	const bool push_front = rtso::hotkeys::held("session.pushorderfront");
	const uint32_t ids[1] = {selected_building_id};
	if (turn_cmds())
		post_lockstep((uint8_t)rtso::NetCommandType::NET_CMD_FOCUS_FIRE, ids, 1, 0, 0, target,
				(queued ? rtso::NET_PARAM_QUEUED : 0) | (push_front ? rtso::NET_PARAM_PUSH_FRONT : 0));
	else if (!sim->cmd_focus_fire(me, ids, 1, target, queued, push_front))
		return false;
	return true;
}

bool RtsoMain::over_own_rally_structure(const Vector3 &at, bool need_rally) const {
	if (!sim || selected_building_id == 0 || !selected_ids.is_empty()) return false;
	for (const auto &b : sim->buildings()) {
		if (b.id != selected_building_id) continue;
		if (b.team != local_player() || (need_rally && !b.has_rally_point)) return false;
		// Inside the turned footprint (u axis = (ux, uz), half extents hu, hv).
		const float dx = at.x - b.x, dz = at.z - b.z;
		const float u = dx * b.ux + dz * b.uz, v = -dx * b.uz + dz * b.ux;
		const float hu = b.hu > 0.0f ? b.hu : b.half_size, hv = b.hv > 0.0f ? b.hv : b.half_size;
		return std::fabs(u) <= hu && std::fabs(v) <= hv;
	}
	return false;
}

bool RtsoMain::selected_building_trains_units() const { return building_trains_units(selected_building_id); }

bool RtsoMain::building_trains_units(uint32_t id) const {
	if (!sim || id == 0) return false;
	for (const auto &b : sim->buildings()) {
		if (b.id != id) continue;
		const uint8_t owner = sim->player_civ(b.team);
		for (uint8_t civ : {b.civ, owner})
			for (uint16_t i : rtso::civ_roster_ids(civ))
				if (rtso::Simulation::structure_trains(b, owner, i)) return true;
		return false;
	}
	return false;
}


// The pause hotkey and the replay's Pause button. Networked, the pause holds
// every peer's turns (0 A.D. NMT_CLIENT_PAUSED).
// 0 A.D. session hotkeys quicksave/quickload: one quicksave slot, not offered
// in networked games or while watching a replay; a short notice says what happened.
void RtsoMain::show_notice(const String &text) {
	if (!banner_label || !banner_panel || hotkey_paused_) return;
	banner_label->set_text(text);
	banner_panel->set_visible(true);
	notice_until_ = Time::get_singleton()->get_ticks_msec() + 2000;
}

void RtsoMain::quicksave() {
	if (net_lockstep() || turn_mgr_.is_replaying() || !sim) return;
	const bool ok = sim->save_state(ProjectSettings::get_singleton()->globalize_path(kQuicksavePath).utf8().get_data());
	if (ok) {
		// session.js getSavedGameData: QuickSave keeps only the control groups, so a
		// quickload leaves the camera where it is.
		Array groups;
		for (int g = 0; g < 10; ++g) groups.push_back(selected_groups[g]);
		quicksave_gui_ = Dictionary();
		quicksave_gui_["groups"] = groups;
		// The quicksave is the whole simulation, mirages included.
		save_fog_memory(quicksave_gui_);
		quicksaved_ = true;
	}
	log_render(ok ? "Quicksaved game" : "Quicksave failed", !ok);
}

void RtsoMain::quickload() {
	if (net_lockstep() || turn_mgr_.is_replaying()) return;
	if (!quicksaved_) {
		log_render("Cannot quickload game - no game was quicksaved", true);
		return;
	}
	const bool ok = load_world(kQuicksavePath, quicksave_gui_);
	log_render(ok ? "Quickloaded game" : "Quickload failed", !ok);
}

// CLogger::WriteMessage with "render": a line in the top-left overlay; the oldest goes
// when a 21st comes.
void RtsoMain::log_render(const String &text, bool error) {
	log_lines_.push_back({text, error, Time::get_singleton()->get_ticks_msec() + 10000});
	if (log_lines_.size() > 20) log_lines_.erase(log_lines_.begin());
	update_log_messages(true);
}

void RtsoMain::update_log_messages(bool changed) {
	if (!log_label_ || (log_lines_.empty() && !changed)) return;
	const uint64_t now = Time::get_singleton()->get_ticks_msec();
	const size_t before = log_lines_.size();
	log_lines_.erase(std::remove_if(log_lines_.begin(), log_lines_.end(), [now](const LogLine &l) { return l.until_ms <= now; }),
			log_lines_.end());
	if (!changed && log_lines_.size() == before) return;
	String txt;
	for (const LogLine &l : log_lines_) {
		if (!txt.is_empty()) txt += "\n";
		const String line = l.text.replace("[", "[lb]");
		txt += l.error ? "[color=#ff0000]" + line + "[/color]" : line;
	}
	log_label_->set_text(txt);
	log_label_->set_visible(!log_lines_.empty());
}

// NetworkStatusOverlay.onNetStatusMessage: the overlay shows any status but
// "active". Lockstep stands in for 0 A.D.'s loading handshake: the first turn
// that needs every player's batch waits for those still loading the map, and
// they are the clients 0 A.D. lists in NMT_CLIENTS_LOADING. Once the link to
// the host is lost (messages.js handleNetStatusMessage) the match is over for
// this client: dialogs close, the overlay says why (network.js
// getDisconnectReason) and stays until Exit.
void RtsoMain::update_net_warnings() {
	if (!net_warnings_label_) return;
	const uint64_t now = (uint64_t)Time::get_singleton()->get_ticks_msec();
	const bool on = rtso::user_config::get_bool("overlay.netwarnings") && !net_disconnected_;
	if (on && now - net_warnings_checked_ms_ >= 1000) {
		net_warnings_checked_ms_ = now;
		auto warn = [&](const std::string &guid, const String &text) { net_warnings_[guid] = {now, text}; };
		const bool client = net_host_.getRole() == rtso::NetRole::Client;
		if (client) {
			// NetClient::CheckServerConnection.
			const uint32_t silent = net_host_.hostSilentMs();
			const uint32_t rtt = net_host_.getStats().rtt_ms;
			if (silent > rtso::NETWORK_WARNING_TIMEOUT_MS)
				warn("server", rtso::sprintf0(rtso::tr0("Losing connection to server (%(seconds)ss)"),
						{{"seconds", String::num_int64((silent + 999) / 1000)}}));
			else if (rtt > rtso::NETWORK_BAD_PING_MS)
				warn("server", rtso::sprintf0(rtso::tr0("Bad connection to server (%(milliseconds)sms)"),
						{{"milliseconds", String::num_int64(rtt)}}));
		}
		// NetServer::CheckClientConnections, as every client hears it
		// (NMT_CLIENT_TIMEOUT, NMT_CLIENT_PERFORMANCE); not about ourselves.
		const uint32_t me = client ? net_host_.getLocalPeerId() : 0;
		for (const auto &e : net_host_.clientPerformance()) {
			if (e.client == 0 || e.client == me) continue;
			const std::string guid = rtso::peer_guid(e.client);
			const String who = chat_user(String(guid.c_str()));
			if (e.silent_ms > rtso::NETWORK_WARNING_TIMEOUT_MS)
				warn(guid, rtso::sprintf0(rtso::tr0("%(player)s losing connection (%(seconds)ss)"),
						{{"player", who}, {"seconds", String::num_int64((e.silent_ms + 999) / 1000)}}));
			else if (e.rtt_ms > rtso::NETWORK_BAD_PING_MS)
				warn(guid, rtso::sprintf0(rtso::tr0("Bad connection to %(player)s (%(milliseconds)sms)"),
						{{"player", who}, {"milliseconds", String::num_int64(e.rtt_ms)}}));
		}
	}
	// getNetworkWarnings: drop the old ones and those of clients gone.
	String text;
	for (auto it = net_warnings_.begin(); it != net_warnings_.end();) {
		bool gone = it->first != "server";
		for (const auto &e : net_host_.playerAssignment())
			if (rtso::peer_guid(e.client) == it->first && e.status != rtso::PlayerAssignmentMsg::LEFT) gone = false;
		// Before Start there is no player assignment yet: the connected clients.
		if (is_net_pregame())
			for (const auto &p : net_host_.getPeers())
				if (p.connected && rtso::peer_guid(p.id) == it->first) gone = false;
		if (!on || now > it->second.first + rtso::NETWORK_WARNING_SHOW_MS || gone) {
			it = net_warnings_.erase(it);
			continue;
		}
		++it;
	}
	if (net_warnings_.count("server")) text = net_warnings_["server"].second;
	for (const auto &w : net_warnings_)
		if (w.first != "server") text += String(text.is_empty() ? "" : "\n") + w.second.second;
	net_warnings_label_->set_text("[right]" + text + "[/right]");
	net_warnings_label_->set_visible(!text.is_empty());
	// SetupWindow.js registers netwarn too: before Start the warnings go over
	// the (opaque) setup page and the Connecting box, then back on the session UI.
	Node *home = ui_layer;
	if (is_net_pregame()) {
		if (!net_warnings_layer_) {
			net_warnings_layer_ = memnew(CanvasLayer);
			net_warnings_layer_->set_name("NetWarnings");
			net_warnings_layer_->set_layer(22); // the setup page is 20, the Connecting box 21
			add_child(net_warnings_layer_);
		}
		home = net_warnings_layer_;
	}
	if (home && net_warnings_label_->get_parent() != home) net_warnings_label_->reparent(home, false);
	if (!is_net_pregame() && net_warnings_layer_ && net_warnings_label_->get_parent() != net_warnings_layer_) {
		net_warnings_layer_->queue_free();
		net_warnings_layer_ = nullptr;
	}
}

void RtsoMain::update_net_status_overlay() {
	if (net_disconnected_ || !net_status_overlay_) return;
	const bool client = net_host_.getRole() == rtso::NetRole::Client;
	// Before Start too (0.28 reportDisconnect): once the host let us in, the
	// setup page closes for "Lost connection to the server."; a join it refused
	// or never answered reads "Failed to connect to the server.".
	const bool joined = net_game_started_ || net_setup_overlay_ || net_host_.authResult() == rtso::ARC_OK;
	if (client && net_host_.connectionLost()) {
		net_disconnected_ = true;
		finished_asked_ = true; // no quit question over the overlay
		close_open_dialogs();
		String why;
		switch (net_host_.disconnectReason()) {
			case 2: why = tr("The host has ended the game."); break;
			case rtso::NDR_INCORRECT_PROTOCOL_VERSION: why = tr("Incorrect network protocol version."); break;
			case rtso::NDR_SERVER_LOADING: why = tr("Game is loading, please try again later."); break;
			case 6: why = tr("You have been kicked."); break;
			case 7: why = tr("You have been banned."); break;
			case rtso::NDR_PLAYERNAME_IN_USE: why = tr("Player name in use. If you were disconnected, retry in few seconds."); break;
			case rtso::NDR_SERVER_ALREADY_IN_GAME: why = tr("Game has already started, no observers allowed."); break;
			case rtso::NDR_SERVER_FULL: why = tr("Server full."); break;
			case rtso::NDR_LOBBY_AUTH_FAILED: why = tr("Secure lobby authentication failed. Join via lobby."); break;
			case 14: why = tr("Password is invalid."); break;
			case rtso::NDR_INCORRECT_SOFTWARE_VERSION: why = mismatch_detail(); break;
			default: why = tr("An unknown error occurred."); break;
		}
		// NetworkStatusOverlay (in the match): one line break, no title.
		// reportDisconnect (before Start): a blank line, titled "Disconnected".
		String text = net_game_started_ ? tr("Connection to the server has been lost.") + "\n" + why
				: (joined ? tr("Lost connection to the server.") : tr("Failed to connect to the server.")) + "\n\n" + why;
		if (!joined && net_host_.disconnectReason() == 0) {
			// reportConnectionRequestTimeOut, with getRequestTimeOutMessage's
			// address advice only outside the lobby.
			Node *ls = get_node_or_null(NodePath("/root/LobbySession"));
			const bool lobby = ls && bool(ls->call("signed_in"));
			text = tr("Failed to connect to the server.") + " " + tr("The connection request has timed out.") + "\n\n" +
					(lobby ? String() : tr("Please ensure that you entered the correct server name or IP address, and port number.") + "\n\n") +
					tr("If you haven't yet, try to connect again and to different hosts.") + "\n" +
					tr("If the issue persists or occurs regularly, visit the official FAQ for detailed guidance and troubleshooting steps.");
		}
		net_status_label_->set_text(text);
		// The timeout is a "Connection Error" box with Close and Open FAQ.
		const bool timed_out = !joined && net_host_.disconnectReason() == 0;
		if (net_status_title_) {
			net_status_title_->set_text(timed_out ? tr("Connection Error") : net_game_started_ ? String() : tr("Disconnected"));
			net_status_title_->set_visible(!net_game_started_);
		}
		if (net_status_exit_) {
			net_status_exit_->set_text(timed_out ? tr("Close") : tr("Exit"));
			net_status_exit_->set_offset(SIDE_LEFT, timed_out ? -176 : -84);
			net_status_exit_->set_offset(SIDE_RIGHT, timed_out ? -8 : 84);
		}
		if (net_status_faq_) net_status_faq_->set_visible(timed_out);
		net_loading_clients_->set_visible(false);
		net_status_overlay_->set_visible(true);
		if (lobby_status) lobby_status->set_text(net_status_label_->get_text().replace("\n\n", "\n").replace("\n", " "));
		update_net_setup_overlay(); // the setup page gives way at once
		return;
	}
	String status, loading;
	// NetworkStatusOverlay join_syncing: once loaded, until the rejoiner has
	// run the turns that went by while it loaded.
	if (client && net_game_started_ && turn_mgr_.rejoin_catching_up() && !net_host_.connectionLost() &&
			!(net_setup_.seq && net_setup_.setup.saved_game)) {
		status = tr("Synchronizing gameplay with other players…");
	} else if (net_game_started_ && !turn_mgr_.is_replaying() && turn_mgr_.current_turn() <= rtso::TurnManager::COMMAND_DELAY) {
		// NMT_CLIENTS_LOADING: a saved game also waits on the clients still loading it.
		std::set<uint32_t> waiting(turn_mgr_.saved_game_waiting().begin(), turn_mgr_.saved_game_waiting().end());
		for (uint32_t c : turn_mgr_.waiting_on()) waiting.insert(c);
		for (uint32_t c : waiting) {
			if (c == turn_mgr_.local_client_id()) continue;
			String name;
			for (const auto &e : net_host_.playerAssignment())
				if (e.client == c && !e.name.empty()) name = String::utf8(e.name.c_str()).replace("[", "[lb]");
			const uint8_t pl = turn_mgr_.player_for_client(c);
			// colorizePlayernameByGUID: "Unknown Player" without a name, white for an observer.
			const bool seated = pl < rtso::MAX_PLAYERS;
			if (name.is_empty()) name = seated ? player_display_name(pl) : tr("Unknown Player");
			const uint32_t rgb = seated && sim ? rtso::display_colors::rgb(*sim, pl) : 0xFFFFFFu;
			name = "[color=#" + String::num_int64(rgb, 16).lpad(6, "0") + "]" + name + "[/color]";
			loading += (loading.is_empty() ? "" : tr(", ")) + name;
		}
		if (!loading.is_empty()) status = tr("Waiting for players to connect:");
	}
	if (net_status_label_->get_text() != status) net_status_label_->set_text(status);
	const String centred = loading.is_empty() ? String() : "[center]" + loading + "[/center]";
	if (net_loading_clients_->get_text() != centred) net_loading_clients_->set_text(centred);
	net_loading_clients_->set_visible(!loading.is_empty());
	if (net_status_overlay_->is_visible() == status.is_empty()) net_status_overlay_->set_visible(!status.is_empty());
}

// reportConnectionRequestTimeOut's Open FAQ.
void RtsoMain::_on_open_faq_pressed() {
	OS::get_singleton()->shell_open("https://gitea.wildfiregames.com/0ad/0ad/wiki/FAQ#what-shall-i-do-when-joining-multiplayer-matches-fails-with-an-error-message");
}

// disconnectedExitButton: endGame(true), the summary page.
void RtsoMain::_on_disconnected_exit_pressed() {
	// Lost before Start: OK goes back to the lobby, as the setup page's Back does.
	if (!net_game_started_) return leave_net_game();
	Ref<Script> q = ResourceLoader::get_singleton()->load("res://scenes/session/quit_confirmation.gd");
	if (q.is_valid()) q->call("end_game", this, true);
}

// The selection panel portrait (selection_details.js iconBorder): a left click follows the unit
// or looks at the structure (setCameraFollow), or at an animal, tree or treasure; a right click
// opens the Template Viewer (showTemplateDetails).
void RtsoMain::_on_portrait_gui_input(const Ref<InputEvent> &event) {
	Ref<InputEventMouseButton> mb = event;
	if (mb.is_null() || !mb->is_pressed() || !cam_rig) return;
	if (mb->get_button_index() == MOUSE_BUTTON_LEFT) {
		if (const uint32_t id = inspector_.portrait_follow_id()) {
			follow_focus(id, false);
		} else {
			const Vector2 p = inspector_.portrait_position();
			cam_rig->set_follow_id(0);
			cam_rig->set_target_pos(Vector3(p.x, 0, p.y));
		}
	} else if (mb->get_button_index() == MOUSE_BUTTON_RIGHT && ui_layer && !inspector_.portrait_viewer_kind().is_empty()) {
		open_template_viewer(inspector_.portrait_viewer_kind(), inspector_.portrait_viewer_id(), inspector_.portrait_viewer_civ());
	}
}

bool RtsoMain::select_first_own(bool building) {
	if (!sim) return false;
	selected_ids.clear();
	selected_building_id = 0;
	if (building) {
		for (const auto &b : sim->buildings())
			if (b.team == local_player()) { selected_building_id = b.id; return true; }
		return false;
	}
	auto uv = sim->units();
	for (size_t i = 0; i < uv.count; ++i)
		if (uv.alive[i] && uv.team[i] == local_player()) { selected_ids.push_back((int32_t)uv.id[i]); return true; }
	return false;
}

void RtsoMain::_on_formation_gui_input(const Ref<InputEvent> &event, int formation) {
	Ref<InputEventMouseButton> mb = event;
	if (mb.is_null() || !mb->is_pressed() || mb->get_button_index() != MOUSE_BUTTON_RIGHT) return;
	if (formation >= 0 && formation < rtso::FORMATION_COUNT) set_default_formation((uint8_t)formation);
}

void RtsoMain::set_default_formation(uint8_t formation) {
	if (formation != rtso::FORMATION_NONE) last_default_formation_ = formation;
	rtso::user_config::set("gui.session.defaultformation", rtso::formation_template(formation).path);
	rtso::user_config::save();
}

void RtsoMain::toggle_default_formation() {
	const uint8_t cur = RtsoCommandPanelView::default_formation();
	// AutoFormation.js starts lastDefault from the saved default, so turning it off and on
	// again gives back that formation, not Box.
	if (cur != rtso::FORMATION_NONE) last_default_formation_ = cur;
	set_default_formation(cur == rtso::FORMATION_NONE ? last_default_formation_ : rtso::FORMATION_NONE);
}

void RtsoMain::apply_default_formation(const uint32_t *ids, size_t n, bool queued) {
	const uint8_t f = RtsoCommandPanelView::default_formation();
	// Commands.js ProcessCommand passes a queued order no formation: loose units queue their
	// own walks, and formations walk on in theirs.
	if (f == rtso::FORMATION_NONE || n < 2 || !sim || queued) return;
	// GetFormationUnitAIs: units in formations keep their template when they all share one
	// (the walk reuses it); formations of different templates take the default instead.
	uint8_t common = rtso::FORMATION_COUNT;
	bool mixed = false;
	for (const auto &fc : sim->formations())
		for (uint32_t m : fc.members)
			if (std::find(ids, ids + n, m) != ids + n) {
				if (common == rtso::FORMATION_COUNT) common = fc.tmpl;
				else if (fc.tmpl != common) mixed = true;
			}
	if (common != rtso::FORMATION_COUNT && !mixed) return;
	if (turn_cmds()) {
		rtso::NetCommand form;
		form.cmd_type = (uint8_t)rtso::NetCommandType::NET_CMD_FORMATION;
		form.entity_ids.assign(ids, ids + n);
		form.param = f;
		turn_mgr_.post_local_command(form);
	} else {
		sim->order_set_formation(ids, n, f);
	}
}

// The Gaia scenery whose rotated rectangle (with a little slack) holds (x, z), 0 if none.
uint32_t RtsoMain::scenery_at(float x, float z) const {
	if (!sim) return 0;
	for (const auto &sc : sim->scenery()) {
		const float c = std::cos(sc.angle), s = std::sin(sc.angle);
		const float rx = x - sc.x, rz = z - sc.z;
		const float lu = rx * c - rz * s, lv = rx * s + rz * c;
		if (std::fabs(lu) <= sc.half_w + 0.5f && std::fabs(lv) <= sc.half_d + 0.5f) return sc.id;
	}
	return 0;
}

// 0.28 cancelPackUnit: only the selected engines packing (pack 1) or unpacking (pack 0)

void RtsoMain::order_guard_units(uint32_t target_id) {
	if (!sim || selected_ids.is_empty()) return;
	std::vector<uint32_t> ids;
	for (int i = 0; i < selected_ids.size(); ++i) ids.push_back((uint32_t)selected_ids[i]);
	if (turn_cmds())
		post_lockstep(21, ids.data(), ids.size(), 0, 0, target_id);
	else
		sim->order_guard(ids.data(), ids.size(), target_id);
}

void RtsoMain::spawn_large_battle(int total_units) {
	if (!sim) return;
	clear_selection();
	if (banner_panel) banner_panel->set_visible(false);
	stop_replay_recording(); // a debug spawn is not a command, so a replay could not reproduce it
	sim->spawn_war(total_units);
	if (cam_rig) {
		cam_rig->set_target_pos(Vector3(0, 0, 0));
	}
	print_line(String("[RTS0] Spawned massive war simulation: ") + String::num_int64(total_units) + " units across 128m map.");
}

void RtsoMain::update_ghost_mesh() {
	if (!ghost_mesh) return;
	String cat = "buildings";
	const char *fid = rtso::civ_building_foundation(current_build_type);
	auto bentry = rtso::ModelLibrary::get(cat, String(fid));
	if (!bentry.valid) {
		uint8_t pciv = sim ? sim->player_civ(local_player()) : (uint8_t)rtso::CIV_ATHENIANS;
		bentry = rtso::ModelLibrary::get(cat, String(rtso::civ_building_model(pciv, current_build_type)));
	}
	if (bentry.valid) {
		ghost_mesh->set_mesh(bentry.mesh);
		ghost_mesh->set_scale(Vector3(bentry.scale, bentry.scale, bentry.scale));
	} else {
		Ref<BoxMesh> box;
		box.instantiate();
		box->set_size(Vector3(1.8f, 1.8f, 1.8f));
		ghost_mesh->set_mesh(box);
	}
}

// Options page settings the session reads (rtso::user_config, 0 A.D.
// ConfigDB), refreshed whenever the options page changes one.
void RtsoMain::refresh_user_options() {
	namespace uc = rtso::user_config;
	if (user_opts_.generation == uc::generation()) return;
	user_opts_.generation = uc::generation();
	// input.js updateDefaultBatchSize: only a new default resets a scrolled batch.
	const int batch_default = std::clamp((int)uc::get_number("gui.session.batchtrainingsize"), 1, 20);
	if (batch_default != user_opts_.batch_default) {
		user_opts_.batch_default = batch_default;
		user_opts_.batch_scroll = (float)batch_default;
		user_opts_.batch_size = batch_default;
	}
	user_opts_.wounded_fraction = (float)std::clamp(uc::get_number("gui.session.woundedunithotkeythreshold"), 0.0, 100.0) / 100.0f;
	user_opts_.flare_time = (float)std::clamp(uc::get_number("gui.session.flarelifetime"), 0.0, 60.0);
	user_opts_.drag_delta = (float)std::clamp(uc::get_number("gui.session.dragdelta"), 1.0, 200.0);
	user_opts_.attack_notice = uc::get_bool("gui.session.notifications.attack");
	user_opts_.tribute_notice = uc::get_bool("gui.session.notifications.tribute");
	user_opts_.barter_notice = uc::get_bool("gui.session.notifications.barter");
	{
		const std::string phase = uc::get("gui.session.notifications.phase");
		user_opts_.phase_notice = phase == "none" ? 0 : phase == "all" ? 2 : 1;
	}
	chat_window_.set_timestamps(uc::get_bool("chat.timestamp"));
	if (chat_window_.extended() != uc::get_bool("chat.session.extended")) chat_window_.set_extended(uc::get_bool("chat.session.extended"));
	range_overlays_.show_attack = uc::get_bool("gui.session.attackrange");
	range_overlays_.show_auras = uc::get_bool("gui.session.aurasrange");
	range_overlays_.show_heal = uc::get_bool("gui.session.healrange");
	// overlay.fps (fps.toggle, Alt+F, handled on every page by user_settings.gd): the
	// session adds RTS0's frame and tick timings to 0.28's FPS counter.
	if (debug_overlay) debug_overlay->set_visible(uc::get_bool("overlay.fps"));
}

// translateAISettings (gui/common/gamedescription.js): "%(AIdifficulty)s %(AIbehavior)s %(AIname)s".
String RtsoMain::ai_settings_text(uint8_t pid) const {
	if (!sim || pid >= rtso::MAX_PLAYERS || !sim->petra_runs_for(pid)) return String();
	static const char *kDifficulty[] = {"Sandbox", "Very Easy", "Easy", "Medium", "Hard", "Very Hard"};
	static const char *kBehavior[] = {"Random", "Defensive", "Balanced", "Aggressive"};
	const rtso::PetraConfig &c = sim->petra_config_for(pid);
	return rtso::sprintf0(rtso::tr0("%(AIdifficulty)s %(AIbehavior)s %(AIname)s"),
			{{"AIdifficulty", rtso::tr0(kDifficulty[std::min<int>(c.difficulty, 5)])},
			 {"AIbehavior", rtso::tr0(kBehavior[std::min<int>(c.behavior, 3)])}, {"AIname", rtso::tr0("Petra Bot")}});
}

bool RtsoMain::spectating() const {
	if (turn_mgr_.is_replaying() || !sim) return false;
	const uint8_t seat = seat_player();
	return seat >= rtso::MAX_PLAYERS || sim->is_defeated(seat) || sim->is_victory_for(seat);
}

bool RtsoMain::orders_allowed() const {
	if (turn_mgr_.is_replaying() || !sim) return false;
	if (!spectating()) return true;
	const uint8_t seat = seat_player();
	return seat < rtso::MAX_PLAYERS && !sim->is_defeated(seat) && view_player() == seat;
}

// 0 A.D. session "viewPlayer" dropdown (gui/session/top_panel/PlayerViewControl):
// an observer, or a player once defeated or victorious (isPlayerObserver), watches
// the whole map or follows one player's sight and panels. A loss switches the view
// to Observer; a win keeps the player's own (PlayerViewControl.js onPlayerFinished).
// "Observer Mode" shows while the view is Observer or Gaia.
void RtsoMain::update_observer_view_picker() {
	if (!spectating() && !change_perspective_) {
		if (observer_view_picker_) observer_view_picker_->get_parent()->call("set_visible", false);
		return;
	}
	if (!observer_view_picker_) {
		observer_view_picker_ = new_player_view_picker();
		observer_view_picker_->connect("item_selected", Callable(this, "_on_observer_view_selected"));
		observer_mode_label_ = memnew(Label);
		observer_mode_label_->set_text("Observer Mode");
		observer_follow_box_ = new_follow_player_box();
		CanvasLayer *layer = memnew(CanvasLayer);
		layer->set_layer(5);
		add_child(layer);
		layer->add_child(new_player_view_row(observer_view_picker_, observer_mode_label_, observer_follow_box_));
		const uint8_t seat = seat_player();
		if (!spectating()) {
			sync_observer_view_picker(); // Change perspective: the player's own seat
		} else if (seat < rtso::MAX_PLAYERS && sim->is_victory_for(seat) && !sim->is_defeated(seat)) {
			observer_view_ = seat; // the winner keeps watching their own side
			sync_observer_view_picker();
		} else {
			set_observer_view(-1);
		}
	}
	observer_view_picker_->get_parent()->call("set_visible", true);
	if (perspective_pick_ >= 0) {
		const uint8_t seat = seat_player();
		if (perspective_pick_ == (seat < rtso::MAX_PLAYERS ? seat + 1 : 0)) { // the turn has seated us
			perspective_pick_ = -1;
			sync_observer_view_picker();
		}
	}
}

void RtsoMain::_on_observer_view_selected(int index) {
	if (!observer_view_picker_ || index < 0 || index >= observer_view_picker_->get_item_count()) return;
	const int id = observer_view_picker_->get_item_id(index);
	if (change_perspective_) {
		// Engine.SetPlayerID: this client plays the picked player (or watches) from the next turn.
		if (id == kReplayViewGaiaId || !turn_cmds()) {
			sync_observer_view_picker(); // RTS0 can't seat a client as Gaia
			return;
		}
		rtso::NetCommand c;
		c.cmd_type = 39;
		c.param = rtso::Simulation::CHEAT_DEV_PERSPECTIVE;
		c.target_id = id == 0 ? 0xFFu : (uint32_t)(id - 1);
		turn_mgr_.post_local_command(c); // not post_lockstep: an observer may send it too
		perspective_pick_ = id;
	}
	set_observer_view(id == kReplayViewGaiaId ? (int)rtso::GAIA_PLAYER : id - 1);
}

// Gaia sees everything, so its view is the observer's (as in a replay).
// PlayerViewControl.onPlayersFinished: when the viewed player loses, an observer's or a
// replay's view goes to Observer; a winner stays viewed.
void RtsoMain::follow_viewed_player_finish() {
	if (!sim) return;
	int finished = 0, lost = 0;
	for (uint8_t p = 0; p < rtso::MAX_PLAYERS; ++p) {
		if (sim->is_defeated(p)) lost |= 1 << p;
		if (sim->is_defeated(p) || sim->is_victory_for(p)) finished |= 1 << p;
	}
	const int fresh = finished_seen_ < 0 ? 0 : lost & ~finished_seen_;
	finished_seen_ = finished;
	if (!fresh) return;
	if (turn_mgr_.is_replaying()) {
		if (replay_view_ < rtso::MAX_PLAYERS && (fresh & (1 << replay_view_))) set_replay_view(-1);
	} else if (spectating() && observer_view_ < rtso::MAX_PLAYERS && (fresh & (1 << observer_view_))) {
		set_observer_view(-1);
	}
}

void RtsoMain::set_observer_view(int player) {
	observer_view_gaia_ = player == rtso::GAIA_PLAYER;
	observer_view_ = player >= 0 && player < rtso::MAX_PLAYERS ? (uint8_t)player : rtso::OBSERVER_VIEW;
	clear_selection();
	sync_observer_view_picker();
}

void RtsoMain::sync_observer_view_picker() {
	if (observer_view_picker_) {
		rtso::display_colors::state().viewer = view_player(); // onViewedPlayerChange: rebuild in its colours
		refresh_player_view_colors(observer_view_picker_);
		int want = observer_view_gaia_ ? kReplayViewGaiaId : observer_view_ == rtso::OBSERVER_VIEW ? 0 : observer_view_ + 1;
		if (!spectating() && seat_player() < rtso::MAX_PLAYERS) // Change perspective: the seat, or the pick on its way
			want = perspective_pick_ >= 0 ? perspective_pick_ : seat_player() + 1;
		for (int i = 0; i < observer_view_picker_->get_item_count(); ++i)
			if (observer_view_picker_->get_item_id(i) == want) observer_view_picker_->select(i);
	}
	if (observer_mode_label_) observer_mode_label_->set_visible(spectating() && observer_view_ == rtso::OBSERVER_VIEW);
}

std::pair<int, int> RtsoMain::mouse_cell(Vector2 mouse_pos) const {
	Vector3 pos = get_ground_click_pos(mouse_pos);
	int cx = (int)std::floor(pos.x / rtso::Simulation::GRID);
	int cy = (int)std::floor(pos.z / rtso::Simulation::GRID);
	return {cx, cy};
}

void RtsoMain::update_hud() {
	top_panel_.update(sim, local_player(), match_elapsed_time(), spectating() || turn_mgr_.is_replaying());
	if (sim) {
		const bool picker_shown = (replay_view_picker_ && replay_view_picker_->is_visible_in_tree()) ||
				(observer_view_picker_ && observer_view_picker_->is_visible_in_tree());
		top_panel_.set_build_label_view(view_player() < rtso::MAX_PLAYERS, picker_shown);
		if (picker_shown) {
			refresh_player_view_colors(replay_view_picker_);
			refresh_player_view_colors(observer_view_picker_);
		}
		const uint8_t me = local_player();
		const int idle = me < rtso::MAX_PLAYERS ? (int)sim->get_idle_workers(me).size() : 0;
		minimap_panel_.update(idle, rtso::display_colors::state().enabled, me >= rtso::MAX_PLAYERS || spectating(),
				turn_mgr_.is_replaying());
		const uint8_t vp = view_player();
		minimap_panel_.set_civ(vp < rtso::MAX_PLAYERS ? rtso::civ_info(sim->player_civ(vp)).code : "gaia");
	}
	// GameSpeedButton: hidden in networked games (g_IsNetworked), shown in replays with Fast Forward.
	if (speed_btn) speed_btn->set_visible(!net_lockstep());
	top_panel_.set_player_buttons_visible(sim && view_player() < rtso::MAX_PLAYERS);
}

// Selection details (0 A.D. gui/session/selection_details.js): real values
// only. Health always; capture points for capturable structures; experience
// toward the next rank for units (Simulation::promotion_required_xp);
// construction, upgrade or research progress for structures; aura names from
// the unit's roster template; carried resources. Several units show the
// multi-selection icon grid (selection_panels.js "Selection") and a structure
// with a garrison shows the garrison grid ("Garrison").
// EntitySelection.update (gui/session/selection.js): units that died leave the
// selection, so its count and groups follow the battle.
// The player's own unpenned animal nearest (x, z) within a click's reach, or 0.
uint32_t RtsoMain::own_fauna_at(float x, float z) const {
	if (!sim) return 0;
	auto fview = sim->fauna();
	uint32_t best = 0;
	float best_d2 = 1.5f * 1.5f;
	for (size_t f = 0; f < fview.count; ++f) {
		if (!sim->fauna_selectable_by(local_player(), fview.id[f])) continue;
		const float dx = fview.pos_x[f] - x, dz = fview.pos_z[f] - z, d2 = dx * dx + dz * dz;
		if (d2 < best_d2) {
			best_d2 = d2;
			best = fview.id[f];
		}
	}
	return best;
}

void RtsoMain::prune_selection() {
	if (!sim || selected_ids.is_empty()) return;
	// selection.js g_MaxSelectionSize: addList stops at 300 entities, the earliest kept.
	constexpr int64_t kMaxSelectionSize = 300;
	if (selected_ids.size() > kMaxSelectionSize) selected_ids.resize(kMaxSelectionSize);
	auto uview = sim->units();
	std::unordered_set<int32_t> wanted(selected_ids.ptr(), selected_ids.ptr() + selected_ids.size());
	std::unordered_set<int32_t> living;
	// selection.js update: with more than one entity selected, one that changed owner (a relic
	// picked up, a unit converted) leaves the selection; a lone one stays.
	const bool owners_only = selected_ids.size() > 1 && !spectating() && !turn_mgr_.is_replaying() &&
			!sim->controls_all(local_player());
	for (size_t i = 0; i < uview.count; ++i)
		if (uview.alive[i] && wanted.count((int32_t)uview.id[i]) && (!owners_only || uview.team[i] == local_player()))
			living.insert((int32_t)uview.id[i]);
	// The player's own livestock stays selected until it dies or is penned.
	for (int32_t id : wanted)
		if (!living.count(id) && sim->fauna_selectable_by(local_player(), (uint32_t)id)) living.insert(id);
	if ((int64_t)living.size() == selected_ids.size()) return;
	PackedInt32Array kept;
	for (int s = 0; s < selected_ids.size(); ++s)
		if (living.count(selected_ids[s])) kept.push_back(selected_ids[s]);
	selected_ids = kept;
}

std::vector<uint32_t> RtsoMain::selected_structures() const {
	std::vector<uint32_t> out;
	if (selected_building_id == 0) return out;
	out.push_back(selected_building_id);
	if (extras_primary_ == selected_building_id)
		for (uint32_t id : extra_structures_)
			if (id != selected_building_id) out.push_back(id);
	return out;
}

void RtsoMain::set_selected_structures(const std::vector<uint32_t> &ids) {
	extra_structures_.clear();
	selected_building_id = ids.empty() ? 0 : ids[0];
	for (size_t k = 1; k < ids.size(); ++k)
		if (std::find(extra_structures_.begin(), extra_structures_.end(), ids[k]) == extra_structures_.end() && ids[k] != ids[0])
			extra_structures_.push_back(ids[k]);
	extras_primary_ = selected_building_id;
}

// Structures leave the selection when destroyed or lost; the next one takes the first place.
// Any other change of selected_building_id (a plain click, a hotkey, a control group) drops the extras.
void RtsoMain::prune_structure_selection() {
	if (!sim) return;
	// Anyone's for an observer's all-players view or Control all units (selection.js force).
	const bool any_owner = observer_all_view() || sim->controls_all(local_player());
	auto live_own = [&](uint32_t id) {
		for (const auto &b : sim->buildings())
			if (b.id == id)
				return !b.wrecked && b.hp > 0.0f && (b.team == local_player() || (any_owner && b.team < rtso::MAX_PLAYERS));
		return false;
	};
	if (selected_building_id != extras_primary_) {
		const bool primary_lost = selected_building_id == 0 && extras_primary_ != 0 && !live_own(extras_primary_);
		if (!primary_lost) extra_structures_.clear();
		extras_primary_ = selected_building_id;
		if (primary_lost) {
			std::vector<uint32_t> rest = extra_structures_;
			rest.erase(std::remove_if(rest.begin(), rest.end(), [&](uint32_t id) { return !live_own(id); }), rest.end());
			set_selected_structures(rest);
		}
		return;
	}
	if (extra_structures_.empty()) return;
	extra_structures_.erase(std::remove_if(extra_structures_.begin(), extra_structures_.end(),
			[&](uint32_t id) { return !live_own(id); }), extra_structures_.end());
}

std::vector<uint32_t> RtsoMain::structure_trainers(uint16_t tmpl, uint32_t holder) const {
	std::vector<uint32_t> out;
	if (!sim || tmpl >= rtso::CIV_UNIT_TEMPLATE_COUNT) return out;
	std::vector<uint32_t> structs = selected_structures();
	if (std::find(structs.begin(), structs.end(), holder) == structs.end()) structs.assign(1, holder);
	const rtso::CivUnitTemplate &u = rtso::civ_unit_template(tmpl);
	const bool control_all = sim->controls_all(local_player()); // anyone's structures (Control all units)
	for (uint32_t id : structs)
		for (const auto &b : sim->buildings()) {
			if (b.id != id) continue;
			if ((b.team == local_player() || control_all) && b.build_progress >= 1.0f && !b.wrecked && b.upgrade_to == rtso::BLD_TYPE_COUNT &&
					rtso::Simulation::structure_trains(b, sim->player_civ(b.team), tmpl))
				out.push_back(id);
			break;
		}
	return out;
}

void RtsoMain::update_inspector() { inspector_.update(); }

// Multi-selection icon click (0 A.D. selection_panels.js Selection.execute):
// select only that group, or drop it with session.deselectgroup (Ctrl).
// selection_panels.js Selection onPressRight: a right-click drops the group.
void RtsoMain::_on_selection_group_gui_input(const Ref<InputEvent> &event, int key) {
	Ref<InputEventMouseButton> mb = event;
	if (mb.is_null() || mb->get_button_index() != MOUSE_BUTTON_RIGHT || mb->is_pressed()) return;
	select_group(key, true);
}

// PanelEntity.js onPress (select it; selection.add keeps the selection) and
// onDoublePress (selectAndMoveTo: select it and move the camera to it).
void RtsoMain::_on_panel_entity_gui_input(const Ref<InputEvent> &event, int slot) {
	Ref<InputEventMouseButton> mb = event;
	if (!sim || mb.is_null() || mb->get_button_index() != MOUSE_BUTTON_LEFT || !mb->is_pressed()) return;
	const uint32_t id = panel_entities_.entity_at(slot);
	if (!id) return;
	auto uview = sim->units();
	size_t i = 0;
	while (i < uview.count && uview.id[i] != id) ++i;
	if (i == uview.count) return;
	if (mb->is_double_click()) {
		// onDoublePress: selectAndMoveTo(getEntityOrHolder(ent)): a garrisoned hero's holder is
		// selected and looked at, a hero in the field is selected and followed.
		uint32_t holder = sim->turret_holder_of(id);
		if (!holder && !uview.alive[i])
			for (const auto &b : sim->buildings()) {
				const std::vector<uint32_t> *g = sim->get_garrisoned_units(b.id);
				if (g && std::find(g->begin(), g->end(), id) != g->end()) { holder = b.id; break; }
			}
		selected_ids.clear();
		selected_building_id = 0;
		for (const auto &b : sim->buildings())
			if (holder && b.id == holder) {
				set_selected_structures({holder});
				if (cam_rig) {
					cam_rig->set_follow_id(0);
					cam_rig->set_target_pos(Vector3(b.x, 0, b.z));
				}
				play_civ_voice("select");
				return;
			}
		selected_ids.push_back((int32_t)id);
		if (cam_rig) cam_rig->set_follow_id(id);
		play_civ_voice("select");
		return;
	}
	if (!rtso::hotkeys::held("selection.add")) {
		selected_ids.clear();
		selected_building_id = 0;
	}
	if (!selected_ids.has((int32_t)id)) selected_ids.push_back((int32_t)id);
	play_civ_voice("select");
}

// ResearchProgressButton.onPress: selectAndMoveTo(researcher).
void RtsoMain::_on_research_progress_pressed(int slot) {
	const uint32_t id = research_progress_.researcher_at(slot);
	if (!sim || !id) return;
	for (const auto &b : sim->buildings()) {
		if (b.id != id) continue;
		selected_ids.clear();
		selected_building_id = id;
		if (cam_rig) cam_rig->set_target_pos(Vector3(b.x, 0, b.z));
		return;
	}
	// A unit researching (a Researcher on a unit); a garrisoned one can't be selected.
	const auto uv = sim->units();
	for (size_t i = 0; i < uv.count; ++i) {
		if (uv.id[i] != id || !uv.alive[i]) continue;
		clear_selection();
		selected_ids.push_back((int32_t)id);
		if (cam_rig) cam_rig->set_target_pos(Vector3(uv.pos_x[i], 0, uv.pos_z[i]));
		return;
	}
}

// The group's living units (dead ones leave it), then selected or added;
// "snap" also moves the camera to the first.
void RtsoMain::perform_group(int grp, GroupAction action) {
	if (!sim || grp < 0 || grp > 9) return;
	// input.js performGroup: the group's living units and structures (addList(toSelect)); a
	// member inside a structure stays in the group and the camera snaps to its holder when
	// nothing else of the group is out.
	PackedInt32Array recall, kept;
	std::vector<uint32_t> recall_buildings;
	// The snap goes to the group's lowest id, or its holder (getEntityOrHolder(toSelect[0])).
	int32_t snap_id = 0;
	Vector3 snap_pos;
	auto gview = sim->units();
	for (int m = 0; m < selected_groups[grp].size(); ++m) {
		const int32_t gid = selected_groups[grp][m];
		bool found = false, foreign = false;
		Vector3 at;
		for (size_t i = 0; i < gview.count; ++i) {
			if (gview.id[i] != (uint32_t)gid) continue;
			// EntityGroupsContainer.update drops only destroyed members: one that changed owner
			// stays in the group (and comes back with it if it returns) but isn't selected.
			if (gview.team[i] != local_player()) {
				foreign = gview.alive[i] || gview.hp[i] > 0.0f;
				break;
			}
			if (gview.alive[i]) {
				at = Vector3(gview.pos_x[i], 0, gview.pos_z[i]);
				recall.push_back(gid);
				found = true;
			} else if (gview.hp[i] > 0.0f) {
				for (const auto &b : sim->buildings())
					if (std::find(b.garrisoned_units.begin(), b.garrisoned_units.end(), (uint32_t)gid) != b.garrisoned_units.end()) {
						at = Vector3(b.x, 0, b.z);
						found = true;
						break;
					}
			}
			break;
		}
		if (!found && !foreign)
			for (const auto &b : sim->buildings())
				if (b.id == (uint32_t)gid && !b.wrecked && b.hp > 0.0f) {
					if (b.team != local_player()) {
						foreign = true;
						break;
					}
					recall_buildings.push_back(b.id);
					at = Vector3(b.x, 0, b.z);
					found = true;
					break;
				}
		if (found) {
			kept.push_back(gid);
			if (snap_id == 0 || gid < snap_id) snap_id = gid, snap_pos = at;
		} else if (foreign) {
			kept.push_back(gid);
		}
	}
	selected_groups[grp] = kept;
	if (snap_id == 0) {
		// Nothing of the group is the player's now: performGroup's g_Selection.reset() still
		// empties the selection (an empty group deselects all).
		if (action != GROUP_ADD) {
			selected_ids.clear();
			set_selected_structures({});
		}
		return;
	}
	// addList(toSelect) brings each unit's formation along (addFormationMembers).
	for (int m = 0, n = recall.size(); m < n; ++m)
		for (int32_t uid : formation_mates(recall[m]))
			if (!recall.has(uid)) recall.push_back(uid);
	if (recall.is_empty() && recall_buildings.empty()) {
		// Everyone is inside: performGroup's g_Selection.reset() still drops the old selection
		// (the members inside can't be picked here), and the snap goes to the holder.
		if (action != GROUP_ADD) {
			selected_ids.clear();
			set_selected_structures({});
		}
		if (action == GROUP_SNAP && cam_rig) cam_rig->set_target_pos(snap_pos);
		return;
	}
	// selection.js addList: the select sound only for what the recall adds, in its first's voice
	// (a plain recall resets the selection first, so all of it is added).
	const PackedInt32Array before_units = action == GROUP_ADD ? selected_ids : PackedInt32Array();
	const std::vector<uint32_t> before_structs = action == GROUP_ADD ? selected_structures() : std::vector<uint32_t>();
	if (action == GROUP_ADD) {
		for (int m = 0; m < recall.size(); ++m)
			if (!selected_ids.has(recall[m])) selected_ids.push_back(recall[m]);
		if (!recall_buildings.empty()) {
			std::vector<uint32_t> structures = selected_structures();
			structures.insert(structures.end(), recall_buildings.begin(), recall_buildings.end());
			set_selected_structures(structures);
		}
	} else {
		selected_ids = recall;
		set_selected_structures(recall_buildings);
	}
	if (action == GROUP_SNAP && cam_rig && snap_id != 0) cam_rig->set_target_pos(snap_pos);
	play_select_added(before_units, before_structs);
}

// session.js updateGroups: a click on a group's button selects it (selection.add
// adds it), a double click snaps to it, a right click breaks it up.
void RtsoMain::_on_control_group_gui_input(const Ref<InputEvent> &event, int button) {
	Ref<InputEventMouseButton> mb = event;
	if (mb.is_null() || !mb->is_pressed()) return;
	const int grp = rtso::RtsoControlGroups::group_of(button);
	if (mb->get_button_index() == MOUSE_BUTTON_RIGHT) {
		selected_groups[grp].clear();
		return;
	}
	if (mb->get_button_index() != MOUSE_BUTTON_LEFT) return;
	perform_group(grp, mb->is_double_click() ? GROUP_SNAP : rtso::hotkeys::held("selection.add") ? GROUP_ADD : GROUP_SELECT);
}

void RtsoMain::_on_selection_group_clicked(int key) {
	select_group(key, rtso::hotkeys::held("session.deselectgroup"));
}

// makePrimarySelectionGroup (keep only the group) or removeFromSelectionGroup (drop it).
void RtsoMain::select_group(int key, bool drop) {
	if (!sim) return;
	// Structure groups (civ and type) keep or drop those structures; keeping one group drops
	// everything else, units included.
	std::vector<uint32_t> structs = selected_structures(), kept_structs;
	for (uint32_t id : structs)
		for (const auto &b : sim->buildings())
			if (b.id == id) {
				const bool in_group = RtsoSelectionGrid::structure_key(b.civ, b.type) == key;
				if (in_group != drop) kept_structs.push_back(id);
				break;
			}
	if (kept_structs != structs) set_selected_structures(kept_structs);
	if (RtsoSelectionGrid::is_structure_key(key)) {
		if (!drop) selected_ids.clear();
		return;
	}
	auto uview = sim->units();
	auto fv = sim->fauna();
	PackedInt32Array kept;
	for (int s = 0; s < selected_ids.size(); ++s) {
		bool found = false;
		for (size_t i = 0; i < uview.count; ++i) {
			if (uview.id[i] != (uint32_t)selected_ids[s]) continue;
			const uint16_t tmpl = uview.tmpl ? uview.tmpl[i] : rtso::CIV_UNIT_TEMPLATE_NONE;
			const bool in_group = RtsoSelectionGrid::group_key(uview.unit_type[i], tmpl) == key;
			if (in_group != drop) kept.push_back(selected_ids[s]);
			found = true;
			break;
		}
		// The player's own livestock, grouped by kind.
		for (size_t f = 0; !found && f < fv.count; ++f) {
			if (fv.id[f] != (uint32_t)selected_ids[s]) continue;
			if ((RtsoSelectionGrid::fauna_key(fv.fauna_type[f]) == key) != drop) kept.push_back(selected_ids[s]);
			break;
		}
	}
	selected_ids = kept;
}

// The garrison panel's holder: the selected structure, or a lone selected ship, ram or
// siege tower.
// unloadTemplate's garrisonHolders: the selected structures and units that hold units.
std::vector<uint32_t> RtsoMain::selected_garrison_holders() const {
	std::vector<uint32_t> out;
	if (!sim) return out;
	for (uint32_t id : selected_structures())
		if (!sim->holder_contents(id).empty()) out.push_back(id);
	for (int s = 0; s < selected_ids.size(); ++s) {
		const uint32_t id = (uint32_t)selected_ids[s];
		if (sim->is_transport(id) && !sim->holder_contents(id).empty()) out.push_back(id);
	}
	return out;
}

bool RtsoMain::ui_garrison_panel_shown() const { return !selected_garrison_holders().empty(); }

// Garrison icon click (selection_panels.js Garrison.execute -> unloadTemplate): every selected
// holder (Commands.js "unload-template" -> GarrisonHolder.UnloadTemplate) unloads one unit of the
// group (template and owner), or all of them with session.unloadtype (Shift).
void RtsoMain::_on_garrison_group_clicked(int key) {
	if (!sim) return;
	const bool all = rtso::hotkeys::held("session.unloadtype");
	auto uview = sim->units();
	for (uint32_t holder : selected_garrison_holders()) {
		const std::vector<uint32_t> mine = sim->unloadable_by(local_player(), holder);
		std::vector<uint32_t> out;
		for (uint32_t gid : mine) {
			for (size_t i = 0; i < uview.count; ++i) {
				if (uview.id[i] != gid) continue;
				const uint16_t tmpl = uview.tmpl ? uview.tmpl[i] : rtso::CIV_UNIT_TEMPLATE_NONE;
				if (RtsoSelectionGrid::owner_key(uview.unit_type[i], tmpl, uview.team[i]) == key) out.push_back(gid);
				break;
			}
			if (!all && !out.empty()) break;
		}
		if (out.empty()) continue;
		if (turn_cmds())
			post_lockstep(13, out.data(), out.size(), 0, 0, holder, 0);
		else
			sim->cmd_unload(local_player(), holder, out.data(), out.size());
	}
}

void RtsoMain::update_command_panel() { command_panel_.update(); }

// The player an observer or replay viewer is watching (GAIA_PLAYER for Gaia), or -1
// (Observer, or playing). FollowPlayer.js hides the box for Observer only: "Following
// gaia can be interesting on scripted maps".
int RtsoMain::followed_view() const {
	if (turn_mgr_.is_replaying())
		return replay_view_gaia_ ? (int)rtso::GAIA_PLAYER : replay_view_ < rtso::MAX_PLAYERS ? (int)replay_view_ : -1;
	if (spectating())
		return observer_view_gaia_ ? (int)rtso::GAIA_PLAYER : observer_view_ < rtso::MAX_PLAYERS ? (int)observer_view_ : -1;
	return -1;
}

void RtsoMain::set_follow_player(bool on) {
	follow_player_ = on;
	for (CheckBox *box : {observer_follow_box_, replay_follow_box_})
		if (box) box->set_pressed_no_signal(on);
}

// messages.js "playercommand": with Follow Player on, each command the watched player
// gives moves the camera to it and selects the units it ordered. Players' commands come
// from the TurnManager; Petra gives its orders in the simulation, which notes them
// (AiCommandEvent), as 0.28's Petra posts commands like a player.
void RtsoMain::follow_player_tick() {
	const int view = followed_view();
	for (CheckBox *box : {observer_follow_box_, replay_follow_box_})
		if (box) box->set_visible(view >= 0);
	const bool on = follow_player_ && view >= 0 && sim;
	turn_mgr_.set_record_applied(on);
	if (sim) sim->set_record_ai_commands(on);
	if (!on) return;
	for (const auto &a : turn_mgr_.take_applied_commands())
		if ((int)a.player == view) follow_command(a.cmd);
	using E = rtso::Simulation::AiCommandEvent;
	using T = rtso::NetCommandType;
	for (const auto &e : sim->take_ai_command_events()) {
		if ((int)e.player != view) continue;
		static constexpr T kType[] = {T::NET_CMD_MOVE, T::NET_CMD_ATTACK_MOVE, T::NET_CMD_PATROL, T::NET_CMD_GATHER,
			T::NET_CMD_ATTACK, T::NET_CMD_BUILD_REPAIR, T::NET_CMD_GARRISON, T::NET_CMD_GUARD, T::NET_CMD_HEAL,
			T::NET_CMD_RETURN_RESOURCE, T::NET_CMD_SETUP_TRADE_ROUTE, T::NET_CMD_STOP, T::NET_CMD_DELETE, T::NET_CMD_TRAIN,
			T::NET_CMD_RESEARCH, T::NET_CMD_NONE};
		static_assert(sizeof(kType) / sizeof(kType[0]) == E::OTHER + 1);
		rtso::NetCommand c;
		c.cmd_type = (uint8_t)kType[std::min<uint8_t>(e.kind, E::OTHER)];
		c.entity_ids = e.ids;
		c.target_id = e.target;
		c.target_x = e.x;
		c.target_z = e.z;
		follow_command(c);
	}
}

void RtsoMain::follow_command(const rtso::NetCommand &c) {
	using rtso::NetCommandType;
	const auto type = (NetCommandType)c.cmd_type;
	if (!cam_rig) return;
	if (type == NetCommandType::NET_CMD_BUILD_REPAIR) {
		// Focus the structure to build.
		for (const auto &b : sim->buildings())
			if (b.id == c.target_id) {
				cam_rig->set_follow_id(0);
				cam_rig->set_target_pos(Vector3(b.x, 0, b.z));
				break;
			}
	} else if (type != NetCommandType::NET_CMD_TRAIN && type != NetCommandType::NET_CMD_RESEARCH &&
			type != NetCommandType::NET_CMD_TRAIN_TEMPLATE && type != NetCommandType::NET_CMD_TRAIN_FROM_UNIT &&
			type != NetCommandType::NET_CMD_RALLY && !c.entity_ids.empty()) {
		// Focus the commanded entities, but keep the focus when training.
		follow_focus(c.entity_ids[0], false);
	}
	// DrawTargetMarker for "walk", "attack-walk" and "patrol".
	if (type == NetCommandType::NET_CMD_MOVE || type == NetCommandType::NET_CMD_ATTACK_MOVE || type == NetCommandType::NET_CMD_PATROL)
		range_overlays_.add_target_marker(c.target_x, c.target_z);
	// Select the units the command affected, and its target.
	if (type == NetCommandType::NET_CMD_RALLY) return;
	clear_selection();
	auto uv = sim->units();
	auto pick = [&](uint32_t id) {
		if (!id) return;
		for (size_t i = 0; i < uv.count; ++i)
			if (uv.id[i] == id) {
				if (uv.alive[i]) selected_ids.push_back((int32_t)id);
				return;
			}
		if (selected_building_id == 0)
			for (const auto &b : sim->buildings())
				if (b.id == id && !b.wrecked) selected_building_id = id;
	};
	for (uint32_t id : c.entity_ids) pick(id);
	pick(c.target_id);
}

// selection_panels_helpers.js setCameraFollow: a unit is followed, a structure looked at.
void RtsoMain::follow_focus(uint32_t id, bool select) {
	if (!sim || !cam_rig || !id) return;
	auto uv = sim->units();
	for (size_t i = 0; i < uv.count; ++i)
		if (uv.id[i] == id && uv.alive[i]) {
			cam_rig->set_follow_id(id);
			if (select) {
				clear_selection();
				selected_ids.push_back((int32_t)id);
			}
			return;
		}
	for (const auto &b : sim->buildings())
		if (b.id == id && !b.wrecked) {
			cam_rig->set_follow_id(0);
			cam_rig->set_target_pos(Vector3(b.x, 0, b.z));
			if (select) {
				clear_selection();
				selected_building_id = id;
			}
			return;
		}
}

void RtsoMain::clear_selection() {
	selected_ids.clear();
	selected_building_id = 0;
}

// Match summary for the setup E2E + future summary screen: what the sim
// actually holds after apply_match_settings / apply_game_setup.
Dictionary RtsoMain::get_match_summary() const {
	Dictionary d;
	if (!sim) return d;
	d["map_id"] = String(sim->current_map_id().c_str());
	d["map_name"] = String(sim->current_map_name().c_str());
	d["seed"] = (int64_t)sim->sim_seed();
	d["ended"] = sim->is_victory() || sim->is_game_over();
	d["victory"] = (int)sim->victory_condition();
	d["petra_diff"] = (int)sim->petra_difficulty();
	d["petra_behavior"] = (int)sim->petra_behavior();
	d["food"] = sim->food(local_player());
	d["wood"] = sim->wood(local_player());
	d["gold"] = sim->gold(local_player());
	d["stone"] = sim->stone(local_player());
	d["player_civ"] = (int)sim->player_civ(local_player());
	d["ai_civ"] = (int)sim->team_civ(1);
	d["units"] = (int64_t)sim->unit_count();
	d["buildings"] = (int64_t)sim->building_count();
	return d;
}

void RtsoMain::post_lockstep(uint8_t cmd_type, const uint32_t *ids, size_t n,
                             float x, float z, uint32_t tid, uint8_t param) {
	rtso::NetCommand c;
	c.cmd_type = cmd_type;
	if (ids && n) c.entity_ids.assign(ids, ids + n);
	c.target_x = x;
	c.target_z = z;
	c.target_id = tid;
	c.param = param;
	// input.js: an order not queued with Shift goes first while session.pushorderfront is held.
	if (rtso::net_cmd_takes_push_front(cmd_type, param) && !(param & rtso::NET_PARAM_QUEUED) &&
			rtso::hotkeys::held("session.pushorderfront"))
		c.param |= rtso::NET_PARAM_PUSH_FRONT_ORDER;
	if (!orders_allowed()) return; // observers and defeated players order nothing
	if (ids && (cmd_type == (uint8_t)rtso::NetCommandType::NET_CMD_MOVE || cmd_type == (uint8_t)rtso::NetCommandType::NET_CMD_PATROL))
		apply_default_formation(ids, n, (param & rtso::NET_PARAM_QUEUED) != 0);
	if (ids && n && order_disbands_formation(cmd_type)) {
		// 0 A.D. AutoFormation.getNull: with gui.session.formationwalkonly every order but
		// walk and patrol carries the null formation, so the units leave their formations
		// first (Commands.js GetFormationUnitAIs). Sent ahead in the same turn.
		std::vector<uint32_t> formed;
		for (const auto &fc : sim->formations())
			for (uint32_t m : fc.members)
				if (std::find(ids, ids + n, m) != ids + n) formed.push_back(m);
		if (!formed.empty()) {
			rtso::NetCommand leave;
			leave.cmd_type = (uint8_t)rtso::NetCommandType::NET_CMD_FORMATION;
			leave.entity_ids = std::move(formed);
			// LeaveFormation(queued): a Shift-queued order lets a busy formation finish first.
			leave.param = (uint8_t)(rtso::FORMATION_NONE | (param & rtso::NET_PARAM_QUEUED));
			turn_mgr_.post_local_command(leave);
		}
	}
	turn_mgr_.post_local_command(c);
}


bool RtsoMain::order_disbands_formation(uint8_t cmd_type) const {
	using T = rtso::NetCommandType;
	switch ((T)cmd_type) {
	case T::NET_CMD_ATTACK: case T::NET_CMD_GATHER: case T::NET_CMD_BUILD: case T::NET_CMD_ATTACK_MOVE:
	case T::NET_CMD_GARRISON: case T::NET_CMD_ATTACK_BUILD: case T::NET_CMD_BUILD_REPAIR: case T::NET_CMD_HUNT:
	case T::NET_CMD_BOARD: case T::NET_CMD_GUARD: case T::NET_CMD_HEAL: case T::NET_CMD_RETURN_RESOURCE:
	case T::NET_CMD_SETUP_TRADE_ROUTE: case T::NET_CMD_CALL_TO_ARMS: case T::NET_CMD_BUILD_WALL:
		return rtso::user_config::get_bool("gui.session.formationwalkonly");
	default:
		return false; // walk and patrol keep (or make) formations
	}
}

std::vector<int32_t> RtsoMain::formation_mates(int32_t uid) const {
	// selection.js addFormationMembers: with gui.session.selectformationasone a unit in a
	// formation is selected with the whole formation, unless selection.singleselection is held.
	if (sim && rtso::user_config::get_bool("gui.session.selectformationasone") &&
			!rtso::hotkeys::held("selection.singleselection"))
		for (const auto &fc : sim->formations())
			if (std::find(fc.members.begin(), fc.members.end(), (uint32_t)uid) != fc.members.end())
				return std::vector<int32_t>(fc.members.begin(), fc.members.end());
	return {uid};
}


void RtsoMain::set_civ(int civ_idx) {
	if (!sim) return;
	if (!rtso::is_playable_civ(civ_idx)) civ_idx = rtso::CIV_ATHENIANS;
	sim->set_player_civ((uint8_t)civ_idx);

	const auto &info = rtso::civ_info(civ_idx);
	top_panel_.set_civ(civ_idx);
	if (Node *ms = get_node_or_null(NodePath("/root/MatchSettings"))) {
		ms->set("player_civ", civ_idx);
	}
	UtilityFunctions::print("[RtsoMain] Switched Civilization to: ", info.name, " (", info.description, ")");
}

Dictionary RtsoMain::get_civ_info(int civ_idx) const {
	Dictionary d;
	const auto &info = rtso::civ_info(civ_idx);
	d["civ"] = (int)info.civ;
	d["code"] = String(info.code);
	d["name"] = String(info.name);
	d["emblem"] = String(info.emblem_path);
	d["desc"] = String(info.description);
	d["metal_mult"] = info.metal_gather_mult;
	d["trade_gain_mult"] = info.trade_gain_mult;
	d["ship_trade_gain_mult"] = info.ship_trade_gain_mult;
	return d;
}

// Structure tree for the local player's civ: buildings and technologies by
// phase, read from the simulation tables (tech_data.h, extracted from 0 A.D.).
Dictionary RtsoMain::get_tech_tree() const {
	Dictionary d;
	Array blds, techs;
	if (!sim) { d["buildings"] = blds; d["techs"] = techs; return d; }
	uint8_t civ = sim->player_civ(local_player());
	d["civ"] = (int)civ;
	d["phase"] = (int)sim->player_phase(local_player());
	for (uint8_t t = 0; t < rtso::BLD_TYPE_COUNT; ++t) {
		if (!rtso::civ_can_build(civ, t)) continue; // other civs' own structures
		const auto &bi = rtso::building_type_info(t);
		Dictionary b;
		b["id"] = (int)t;
		b["name"] = String::utf8(bi.name);
		b["phase"] = (int)rtso::building_required_phase(t, civ);
		blds.push_back(b);
	}
	for (int t = 1; t < rtso::TECH_COUNT; ++t) {
		if (!rtso::civ_structures_offer_tech((uint8_t)t, civ)) continue;
		const auto &ti = rtso::tech_info((uint8_t)t);
		Dictionary e;
		e["id"] = t;
		e["name"] = String::utf8(ti.name);
		e["desc"] = String::utf8(ti.desc ? ti.desc : "");
		e["phase"] = (int)ti.required_phase;
		e["researcher"] = String(rtso::building_type_info(ti.researcher).name);
		float paid[rtso::RES_COUNT], rtime = 0.0f;
		sim->tech_price(local_player(), (uint8_t)t, ti.researcher, paid, rtime); // this player's price
		e["food"] = (int)paid[rtso::RES_FOOD];
		e["wood"] = (int)paid[rtso::RES_WOOD];
		e["metal"] = (int)paid[rtso::RES_GOLD];
		e["stone"] = (int)paid[rtso::RES_STONE];
		e["time"] = rtime;
		// The prerequisite and the technology it supersedes (DeriveTechnologyRequirements).
		String req = ti.prerequisite ? String::utf8(rtso::tech_info(ti.prerequisite).name) : String();
		if (const uint8_t sup = rtso::tech_supersedes((uint8_t)t); sup != rtso::TECH_NONE && sup != ti.prerequisite)
			req += (req.is_empty() ? String() : String(", ")) + String::utf8(rtso::tech_info(sup).name);
		e["prerequisite"] = req;
		e["researched"] = sim->player(local_player()).has_tech((uint8_t)t);
		techs.push_back(e);
	}
	d["buildings"] = blds;
	d["techs"] = techs;
	return d;
}

// ---------------------------------------------------------------------------
// Mobile Viewport Safe Insets (G57)
// ---------------------------------------------------------------------------
void RtsoMain::apply_safe_area_insets() {
	DisplayServer *ds = DisplayServer::get_singleton();
	if (!ds) return;

	Rect2i safe_rect = ds->get_display_safe_area();
	Vector2i win_size = ds->window_get_size();
	if (win_size.x <= 0 || win_size.y <= 0 || safe_rect.size.x <= 0 || safe_rect.size.y <= 0) return;

	float s_left = (float)safe_rect.position.x;
	float s_top = (float)safe_rect.position.y;
	float s_right = std::max(0.0f, (float)(win_size.x - (safe_rect.position.x + safe_rect.size.x)));
	float s_bottom = std::max(0.0f, (float)(win_size.y - (safe_rect.position.y + safe_rect.size.y)));

	set_safe_area_insets(s_left, s_top, s_right, s_bottom);
}

void RtsoMain::set_safe_area_insets(float left, float top, float right, float bottom) {
	safe_inset_left_ = std::max(0.0f, left);
	safe_inset_top_ = std::max(0.0f, top);
	safe_inset_right_ = std::max(0.0f, right);
	safe_inset_bottom_ = std::max(0.0f, bottom);

	if (top_hud_panel) {
		top_hud_panel->set_offset(SIDE_LEFT, 12.0f + safe_inset_left_);
		top_hud_panel->set_offset(SIDE_RIGHT, -12.0f - safe_inset_right_);
		top_hud_panel->set_offset(SIDE_TOP, 8.0f + safe_inset_top_);
		top_hud_panel->set_offset(SIDE_BOTTOM, 54.0f + safe_inset_top_);
	}
	command_panel_.apply_safe_insets(safe_inset_bottom_);
	minimap_panel_.apply_safe_insets(safe_inset_bottom_);
	if (minimap_view) {
		minimap_view->set_offset(SIDE_TOP, -204.0f - safe_inset_bottom_);
		minimap_view->set_offset(SIDE_BOTTOM, -14.0f - safe_inset_bottom_);
	}
	chat_window_.apply_safe_insets(safe_inset_left_, safe_inset_top_);
}

