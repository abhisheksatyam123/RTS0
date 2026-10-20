#include <algorithm>
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

// 0 A.D. Trainer lists (civ_unit_template(..).trainers): does a structure of type
// `btype` train any of `civ`'s units of class `utype`?
static bool building_trains_class(uint8_t civ, uint8_t utype, uint8_t btype) {
	for (uint16_t i : rtso::civ_roster_ids(civ)) {
		const rtso::CivUnitTemplate &u = rtso::civ_unit_template(i);
		if (u.unit_type == utype && !(u.flags & rtso::ROSTER_SIEGE_TOWER) && (u.trainers & rtso::bld_bit(btype))) return true;
	}
	return false;
}

RtsoMain::RtsoMain() {
	sim = new rtso::Simulation();
}

RtsoMain::~RtsoMain() {
	net_host_.stop();
	delete sim;
}

void RtsoMain::_notification(int p_what) {
	// 0 A.D. "pauseonfocusloss" (main.cpp Frame: isUnfocusedSP): while the window has
	// no focus a single-player game's simulation doesn't advance; it carries on when
	// focus returns, with no pause overlay.
	if (p_what == NOTIFICATION_APPLICATION_FOCUS_OUT) app_unfocused_ = true;
	if (p_what == NOTIFICATION_APPLICATION_FOCUS_IN) app_unfocused_ = false;
	if (p_what == NOTIFICATION_EXIT_TREE) {
		// The MenuAudio autoload outlives the match: stop driving the session's tick.
		if (SceneTree *exiting = get_tree()) {
			if (Node *ma = exiting->get_root()->get_node_or_null(NodePath("MenuAudio")))
				ma->set("session_music_tick", Callable());
		}
		write_replay(true); // leaving the match keeps its replay, with its summary (endGame)
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
	d["elapsed"] = match_elapsed_time;
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
	if (sim) top_panel_.update(sim, local_player(), match_elapsed_time, spectating() || turn_mgr_.is_replaying());
}

Dictionary RtsoMain::get_audio_state() const {
	static const char *kStates[] = {"off", "menu", "peace", "battle", "victory", "defeat", "custom"};
	Dictionary d;
	d["music_state"] = kStates[music_.state()];
	d["track"] = music_.current_track();
	d["music_position"] = music_.current_position();
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

// 0 A.D. gui/session/Cheats.js executeCheat with the codes of simulation/data/cheats/*.json.
// Player numbers typed after "wololo" and "exodia" are 0 A.D.'s (0 = Gaia, 1 = the first player).
bool RtsoMain::chat_try_cheat(const String &text) {
	using S = rtso::Simulation;
	// resource: the resource for CHEAT_ADD_RESOURCE, the unit list for CHEAT_CREATE_UNITS (1 heroes).
	struct CheatCode { const char *name; uint8_t action; int resource; const char *def; bool numeric; };
	constexpr uint8_t kPlayRetro = 0xFF; // retroMe.json "playRetro": the player's own music
	static const CheatCode kCheats[] = {
		{"i want pizza", S::CHEAT_ADD_RESOURCE, rtso::RES_FOOD, "1000", true},
		{"bring me my axe", S::CHEAT_ADD_RESOURCE, rtso::RES_WOOD, "1000", true},
		{"i see a mountain here", S::CHEAT_ADD_RESOURCE, rtso::RES_STONE, "1000", true},
		{"your money or your life", S::CHEAT_ADD_RESOURCE, rtso::RES_GOLD, "1000", true},
		{"jame jam", S::CHEAT_REVEAL_MAP, 0, "", false},
		{"wololo", S::CHEAT_CONVERT_UNITS, 0, "-1", true},
		{"black death", S::CHEAT_KILL_UNITS, 0, "", false},
		{"exodia", S::CHEAT_DEFEAT_PLAYER, 0, "2", true},
		{"salad bowl", S::CHEAT_CREATE_UNITS, 0, "13", true},
		{"iwanttopwnthem", S::CHEAT_CREATE_UNITS, 1, "1", true}, // createHeros.json
		{"retro me", kPlayRetro, 0, "0_8Bit_Bonus_Track.ogg", false},
		{"back to the future", S::CHEAT_CHANGE_PHASE, 0, "", false},
		{"brainiac", S::CHEAT_RESEARCH, 0, "", false},
		{"gift from the gods", S::CHEAT_META, 0, "100000", true},
		{"the hive master", S::CHEAT_MAX_POPULATION, 0, "", false},
		{"TARDIS", S::CHEAT_CHANGE_MAX_POPULATION, 0, "", false},
		{"i am too busy", S::CHEAT_FAST_ACTIONS, 0, "", false},
	};
	// Cheats.executeCheat: controlsPlayer(Engine.GetPlayerID()), the own seat, not a viewed player.
	const uint8_t me = seat_player();
	if (!sim || turn_mgr_.is_replaying() || !sim->cheats_enabled() || me >= rtso::MAX_PLAYERS || sim->is_defeated(me))
		return false;
	const CheatCode *cheat = nullptr;
	for (const CheatCode &c : kCheats)
		if (text.begins_with(c.name)) { cheat = &c; break; }
	if (!cheat) return false;
	String parameter = text.substr(String(cheat->name).length() + 1).strip_edges();
	if (parameter.is_empty()) parameter = cheat->def;
	if (cheat->numeric && !parameter.is_empty() && !parameter.is_valid_int()) {
		// Cheats.js NoNumericMessage: a chat message from the player, shown only here.
		chat_window_.append_kind(String("<") + colored_player_name(me) + "> " + tr("This cheat requires a numeric parameter.") + "\n",
				RtsoChatWindow::message_bits("", true));
		return false;
	}
	if (cheat->action == kPlayRetro) {
		// Cheat.js playRetro: "play-tracks" with the typed tracks, locked; "off" unlocks.
		std::vector<std::string> tracks;
		if (parameter.to_lower() != "off")
			for (const String &t : parameter.split(" ", false)) tracks.push_back(t.utf8().get_data());
		music_.play_custom(tracks);
		return true;
	}
	int64_t number = cheat->numeric && !parameter.is_empty() ? parameter.to_int() : 0;
	uint32_t extra = (uint32_t)std::max(cheat->resource, 0);
	if (cheat->action == S::CHEAT_CONVERT_UNITS || cheat->action == S::CHEAT_DEFEAT_PLAYER)
		number = number < 0 ? -1 : (number == 0 ? rtso::GAIA_PLAYER : number - 1);
	// The selection, a selected structure first (createunits and brainiac read selected[0]).
	std::vector<uint32_t> ids;
	if (selected_building_id) ids.push_back(selected_building_id);
	for (int i = 0; i < selected_ids.size(); ++i) ids.push_back((uint32_t)selected_ids[i]);
	if (cheat->action == S::CHEAT_RESEARCH) {
		// "brainiac N" researches the selected structure's Nth technology, else a tech by its 0 A.D. name.
		extra = rtso::TECH_NONE;
		int btype = -1; // the selected structure's type
		for (const auto &bb : sim->buildings())
			if (selected_building_id && bb.id == selected_building_id) btype = bb.type;
		if (btype >= 0 && parameter.is_valid_int()) {
			int nth = (int)parameter.to_int();
			for (int t = 1; t < rtso::TECH_COUNT && nth > 0; ++t) {
				const rtso::TechInfo &ti = rtso::tech_info((uint8_t)t);
				if (!rtso::building_researches_tech((uint8_t)btype, (uint8_t)t, sim->player_civ(me)) || !(ti.civ_mask & rtso::civ_bit(sim->player_civ(me)))) continue;
				if (--nth == 0) extra = (uint32_t)t;
			}
		} else {
			const std::string want = parameter.utf8().get_data();
			for (int t = 1; t < rtso::TECH_COUNT; ++t)
				if (want == rtso::tech_info((uint8_t)t).template_name) extra = (uint32_t)t;
		}
	}
	if (cheat->action == S::CHEAT_CREATE_UNITS && !selected_building_id) {
		// Cheat.js createunits: a centre text notice.
		text_notices_.push_back({tr("You need to select a building that trains units."), (uint32_t)std::lround(sim->game_time() * 1000.0f) + 10000});
		return true;
	}
	if (turn_cmds()) post_lockstep(39, ids.data(), ids.size(), (float)extra, 0.0f, (uint32_t)(int32_t)number, cheat->action);
	else sim->cmd_cheat(me, cheat->action, (int32_t)number, extra, ids.data(), ids.size());
	return true;
}

void RtsoMain::chat_send_wire(const std::string &wire) {
	net_host_.sendChat(wire);
}

// 0 A.D. gui/common/network.js g_NetworkCommands, run before the text would
// be sent: /kick and /ban a player by name, /kickspecs and /banspecs every
// observer, /list the connected users, /clear the chat history. Only the
// host (the controller) kicks; a single-player game has nobody to kick.
bool RtsoMain::chat_network_command(const String &text) {
	if (!text.begins_with("/")) return false;
	const int space = text.find(" ");
	const String command = space < 0 ? text : text.substr(0, space);
	const String argument = space < 0 ? String() : text.substr(space + 1).strip_edges();
	auto system = [&](const String &line) { chat_window_.append_line(line + String("\n")); };
	const bool networked = net_lockstep();
	if (command == "/kick" || command == "/ban" || command == "/kickspecs" || command == "/banspecs") {
		const bool ban = command == "/ban" || command == "/banspecs";
		const bool specs = command.ends_with("specs");
		if (networked && !net_host_.isHost()) {
			system(tr("Only the host can kick clients!"));
		} else if (!networked) {
			system(specs ? tr("Offline game! No spectators to kick or ban.") : tr("Offline game! Can not kick or ban."));
		} else if (specs) {
			for (const auto &e : net_host_.playerAssignment())
				if (e.player >= rtso::MAX_PLAYERS && e.client != 0 && e.status != rtso::PlayerAssignmentMsg::LEFT)
					net_host_.kickPeer(net_host_.peerByName(e.name), ban);
		} else {
			net_host_.kickPeer(net_host_.peerByName(argument.utf8().get_data()), ban);
		}
		return true;
	}
	if (command == "/list") {
		// getUsernameList: sorted by player slot, observers last, in their colours.
		std::vector<std::pair<uint32_t, String>> users;
		if (networked) {
			for (const auto &e : net_host_.playerAssignment())
				if (e.status != rtso::PlayerAssignmentMsg::LEFT)
					users.emplace_back(e.player < rtso::MAX_PLAYERS ? e.player : 1000u + e.client,
							chat_user(e.client == turn_mgr_.local_client_id() ? String() : "peer-" + String::num_int64(e.client)));
		} else {
			users.emplace_back(0u, chat_user(""));
		}
		std::stable_sort(users.begin(), users.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
		String names;
		for (const auto &u : users) names += (names.is_empty() ? String() : tr(", ")) + u.second;
		system(sprintf0(tr("Users (%(num)s): %(users)s"), {{"num", String::num_int64((int64_t)users.size())}, {"users", names}}));
		return true;
	}
	if (command == "/clear") {
		chat_window_.clear();
		return true;
	}
	return false;
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
	if (cam_rig) {
		cam_rig->set_target_pos(Vector3(world_pos.x, 0.0f, world_pos.y));
	}
}

// The sender's displayed colour (Ownership on flare_target_marker, MiniMap.flare's player colour).
Color RtsoMain::player_flare_color(uint8_t pid) const {
	if (!sim || pid >= rtso::MAX_PLAYERS) return Color(1.0f, 0.45f, 0.1f);
	const uint32_t rgb = rtso::display_colors::rgb(*sim, pid);
	return Color(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, 1.0f);
}

// input.js triggerFlareAction: one flare per g_FlareCooldown (3 s), shown in the
// world and on the minimap and sent to the other players.
void RtsoMain::trigger_flare(float x, float z) {
	const double now = Time::get_singleton()->get_ticks_msec() / 1000.0;
	if (now < last_flare_time_ + 3.0) return;
	last_flare_time_ = now;
	// The flare_target_marker and minimap flare take the sender's colour.
	const Color c = player_flare_color(own_seat()); // the sender's seat, not a viewed player
	range_overlays_.add_flare(x, z, c, user_opts_.flare_time);
	if (minimap_view) minimap_view->add_ping(Vector2(x, z), c);
	_on_flare_requested(Vector2(x, z));
	play_alarm("res://assets/audio/interface/alarm/alarmally_1.ogg");
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
		if (attack_detection_.report(a.x, a.z, match_elapsed_time, livestock)) {
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

// ChatOverlay.js onPress: an attack line runs focusAttack (follow the attacked unit, else look
// at the spot); a flare line moves the camera to the flare.
void RtsoMain::_on_chat_line_input(const Ref<InputEvent> &event, const String &action) {
	Ref<InputEventMouseButton> mb = event;
	if (!mb.is_valid() || !mb->is_pressed() || mb->get_button_index() != MOUSE_BUTTON_LEFT || !cam_rig) return;
	const PackedStringArray parts = action.split(":");
	if (parts.size() < 2) return;
	const PackedStringArray xz = parts[parts.size() - 1].split(",");
	if (xz.size() != 2) return;
	if (parts[0] == "attack" && parts.size() == 3 && sim) {
		const uint32_t id = (uint32_t)parts[1].to_int();
		auto uv = sim->units();
		for (size_t i = 0; id && i < uv.count; ++i)
			if (uv.id[i] == id && uv.alive[i]) {
				cam_rig->set_follow_id(id); // setCameraFollow
				get_viewport()->set_input_as_handled();
				return;
			}
	}
	cam_rig->set_follow_id(0);
	cam_rig->set_target_pos(Vector3(xz[0].to_float(), 0.0f, xz[1].to_float()));
	get_viewport()->set_input_as_handled();
}

// colorizePlayernameByID: the name in the player's displayed colour.
String RtsoMain::colored_player_name(uint8_t pid) const {
	const uint32_t rgb = sim ? rtso::display_colors::rgb(*sim, pid) : 0xFFFFFFu;
	return "[color=#" + String::num_int64(rgb, 16).lpad(6, "0") + "]" + player_display_name(pid) + "[/color]";
}

// DiplomacyColors.toggle (session.diplomacycolors, the dialog and minimap buttons).
void RtsoMain::toggle_diplomacy_colors() {
	rtso::display_colors::toggle();
	if (minimap_view) minimap_view->queue_redraw();
	if (diplo_dialog_.is_open()) diplo_dialog_.refresh();
}

// 0 A.D. ChatMessageFormatSimulation: who sees which tribute, barter, phase and
// diplomacy line. Observers (and defeated players) see more, as g_IsObserver does.
void RtsoMain::show_trigger_dialogs(const std::vector<rtso::Simulation::TriggerDialog> &dialogs) {
	// messages.js "dialog": a message box with the two buttons, for the players it names.
	// Its "dialog-answer" command only reaches a script's OnPlayerCommand, which no
	// ported script reads, so the answer just closes it.
	const uint8_t me = seat_player(); // messages.js: player == Engine.GetPlayerID(), not the viewed one
	if (!sim || turn_mgr_.is_replaying() || me >= rtso::MAX_PLAYERS) return;
	for (const auto &d : dialogs) {
		if (!((d.players >> me) & 1u)) continue;
		ConfirmationDialog *dlg = memnew(ConfirmationDialog);
		dlg->set_title("");
		dlg->set_text(String::utf8(d.caption.c_str()));
		dlg->set_ok_button_text(String::utf8(d.button1.c_str()));
		dlg->set_cancel_button_text(String::utf8(d.button2.c_str()));
		dlg->get_ok_button()->set_tooltip_text(String::utf8(d.tooltip1.c_str()));
		dlg->get_cancel_button()->set_tooltip_text(String::utf8(d.tooltip2.c_str()));
		dlg->set_exclusive(false);
		dlg->connect("confirmed", callable_mp((Node *)dlg, &Node::queue_free));
		dlg->connect("canceled", callable_mp((Node *)dlg, &Node::queue_free));
		add_child(dlg);
		dlg->popup_centered();
	}
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
				if (rtso::building_trains_template(b.type, b.civ, owner, i)) return true;
		return false;
	}
	return false;
}

static const char *const kQuicksavePath = "user://saves/quicksave.rts0sav";

// 0 A.D. 0.28 simulation/data/settings/game_speeds.json: the session's speed
// dropdown. The Fast Forward speeds are offered only while this client has no
// active player (GameSpeedControl.js): a replay, an observer, or a player who
// has been defeated or has won.
struct GameSpeed {
	float speed;
	const char *title;
	bool fast_forward;
};
static const GameSpeed kGameSpeeds[] = {
	{0.1f, "Turtle (0.1×)", false},
	{0.25f, "Slow (0.25×)", false},
	{0.5f, "Leisurely (0.5×)", false},
	{0.75f, "Relaxed (0.75×)", false},
	{1.0f, "Normal (1×)", false},
	{1.25f, "Fast (1.25×)", false},
	{1.5f, "Very Fast (1.5×)", false},
	{2.0f, "Insane (2×)", false},
	{5.0f, "Fast Forward (5×)", true},
	{10.0f, "Fast Forward (10×)", true},
	{20.0f, "Fast Forward (20×)", true},
};

String RtsoMain::speed_text(float speed) { return String::num(speed, 2) + String::utf8("×"); }

String RtsoMain::speed_title(float speed) {
	for (const GameSpeed &g : kGameSpeeds)
		if (g.speed == speed) return String::utf8(g.title);
	return speed_text(speed);
}

bool RtsoMain::fast_forward_allowed() const { return turn_mgr_.is_replaying() || spectating(); }

Array RtsoMain::speed_choices(bool fast_forward) {
	Array out;
	for (const GameSpeed &g : kGameSpeeds)
		if (fast_forward || !g.fast_forward) out.push_back(g.speed);
	return out;
}

void RtsoMain::_on_speed_button_pressed() {
	if (!speed_btn) return;
	if (!speed_menu_) {
		speed_menu_ = memnew(PopupMenu);
		speed_menu_->connect("id_pressed", Callable(this, "_on_speed_selected"));
		speed_btn->add_child(speed_menu_);
	}
	speed_menu_->clear();
	const Array speeds = speed_choices(fast_forward_allowed());
	for (int i = 0; i < speeds.size(); ++i) {
		speed_menu_->add_radio_check_item(speed_title((float)speeds[i]), i);
		speed_menu_->set_item_metadata(i, speeds[i]);
		speed_menu_->set_item_checked(i, (float)speeds[i] == game_speed);
	}
	speed_menu_->set_position(Vector2i(speed_btn->get_screen_position() + Vector2(0, speed_btn->get_size().y)));
	speed_menu_->popup();
}

void RtsoMain::_on_speed_selected(int id) {
	if (speed_menu_ && id >= 0 && id < speed_menu_->get_item_count())
		set_game_speed((float)speed_menu_->get_item_metadata(speed_menu_->get_item_index(id)));
}

// Offline and in replays only: networked games run at the host's speed.
void RtsoMain::set_game_speed(float speed) {
	if (net_lockstep() || !(speed > 0.0f)) return;
	const Array speeds = speed_choices(fast_forward_allowed());
	game_speed = std::clamp(speed, (float)speeds[0], (float)speeds[speeds.size() - 1]);
	if (speed_btn) speed_btn->set_text(speed_text(game_speed));
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
	if (net_lockstep() || turn_mgr_.is_replaying()) return;
	const bool ok = save_game(kQuicksavePath, "Quicksave");
	log_render(ok ? "Quicksaved game" : "Quicksave failed", !ok);
}

void RtsoMain::quickload() {
	if (net_lockstep() || turn_mgr_.is_replaying()) return;
	if (!FileAccess::file_exists(kQuicksavePath)) {
		log_render("No quicksave to load", true);
		return;
	}
	const bool ok = load_game(kQuicksavePath);
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

// PauseControl.js canPause(explicit): an observer, or a player whose game is
// over, who is not the host cannot pause a multiplayer match (resuming their
// own pause stays allowed). The player an observer views does not count.
// Nor can anyone pause before the match is live (g_IsNetworkedActive: the
// "Waiting for players to connect:" stage), which NetServer would ignore.
bool RtsoMain::can_pause() const {
	if (hotkey_paused_ || !net_lockstep() || turn_mgr_.is_replaying()) return true;
	if (!net_game_started_ || turn_mgr_.current_turn() <= rtso::TurnManager::COMMAND_DELAY) return false;
	return !observing_seat() || net_host_.isHost();
}

void RtsoMain::toggle_pause() {
	if (!can_pause()) return;
	hotkey_paused_ = !hotkey_paused_;
	if (net_lockstep()) turn_mgr_.set_local_paused(hotkey_paused_);
	update_pause_overlay();
	// MenuButtons Pause: its caption follows the pause (registerPauseHandler).
	if (Node *menu = get_node_or_null(NodePath("PauseMenu"))) menu->call("rebuild_pause");
}

// PauseOverlay.rebuild: shown for your own pause and while another client
// pauses; the resume line only for your own; "Paused by" in multiplayer, each
// name in its player's colour (colorizePlayernameByGUID: white for an observer).
void RtsoMain::update_pause_overlay() {
	if (!pause_overlay_) return;
	const bool networked = net_lockstep() && !turn_mgr_.is_replaying();
	String who;
	bool others = false;
	if (networked)
		for (uint32_t c : turn_mgr_.paused_clients()) {
			others = others || c != turn_mgr_.local_client_id();
			String name;
			uint8_t pl = 0xFF;
			for (const auto &e : net_host_.playerAssignment())
				if (e.client == c) {
					if (!e.name.empty()) name = String::utf8(e.name.c_str());
					pl = e.player;
				}
			if (pl == 0xFF) pl = turn_mgr_.player_for_client(c);
			const bool seated = pl < rtso::MAX_PLAYERS;
			if (name.is_empty()) name = seated ? player_display_name(pl) : tr("Unknown Player");
			const uint32_t rgb = seated && sim ? rtso::display_colors::rgb(*sim, pl) : 0xFFFFFFu;
			who += String(who.is_empty() ? "" : ", ") + "[color=#" + String::num_int64(rgb, 16).lpad(6, "0") + "]" +
					name.replace("[", "[lb]") + "[/color]";
		}
	const bool show = hotkey_paused_ || others;
	if (pause_overlay_->is_visible() != show) pause_overlay_->set_visible(show);
	if (!show) return;
	pause_resume_label_->set_visible(hotkey_paused_);
	const String by = networked && !who.is_empty()
			? "[center]" + sprintf0(tr("Paused by %(players)s"), {{"players", who}}) + "[/center]" : String();
	if (pause_by_label_->get_text() != by) pause_by_label_->set_text(by);
}

void RtsoMain::_on_pause_overlay_pressed() {
	if (hotkey_paused_) toggle_pause();
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
		String text = (net_game_started_ ? tr("Connection to the server has been lost.")
				: joined ? tr("Lost connection to the server.") : tr("Failed to connect to the server.")) + "\n" + why;
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
			net_status_title_->set_text(timed_out ? tr("Connection Error") : String());
			net_status_title_->set_visible(timed_out);
		}
		if (net_status_exit_) {
			net_status_exit_->set_text(timed_out ? tr("Close") : tr("Exit"));
			net_status_exit_->set_offset(SIDE_LEFT, timed_out ? -176 : -84);
			net_status_exit_->set_offset(SIDE_RIGHT, timed_out ? -8 : 84);
		}
		if (net_status_faq_) net_status_faq_->set_visible(timed_out);
		net_loading_clients_->set_visible(false);
		net_status_overlay_->set_visible(true);
		if (lobby_status) lobby_status->set_text(net_status_label_->get_text().replace("\n", " "));
		update_net_setup_overlay(); // the setup page gives way at once
		return;
	}
	String status, loading;
	if (client && !net_game_started_ && turn_mgr_.rejoin_syncing() && !net_host_.connectionLost()) {
		status = tr("Synchronizing gameplay with other players…");
	} else if (net_game_started_ && !turn_mgr_.is_replaying() && turn_mgr_.current_turn() <= rtso::TurnManager::COMMAND_DELAY) {
		for (uint32_t c : turn_mgr_.waiting_on()) {
			if (c == turn_mgr_.local_client_id()) continue;
			String name;
			for (const auto &e : net_host_.playerAssignment())
				if (e.client == c && !e.name.empty()) name = String::utf8(e.name.c_str()).replace("[", "[lb]");
			const uint8_t pl = turn_mgr_.player_for_client(c);
			if (name.is_empty()) name = pl < rtso::MAX_PLAYERS ? player_display_name(pl) : String("an observer");
			if (pl < rtso::MAX_PLAYERS && sim)
				name = "[color=#" + String::num_int64(rtso::display_colors::rgb(*sim, pl), 16).lpad(6, "0") + "]" + name + "[/color]";
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

// messages.js handlePlayerAssignmentsMessage (onClientJoin, onClientLeave) and
// the kicked message, as ChatMessageFormatNetwork words them.
void RtsoMain::post_network_chat() {
	auto who = [&](const rtso::PlayerAssignmentMsg::Entry &e) {
		String name = String::utf8(e.name.empty() ? rtso::peer_guid(e.client).c_str() : e.name.c_str()).replace("[", "[lb]");
		if (e.player >= rtso::MAX_PLAYERS || !sim) return name;
		const uint32_t rgb = rtso::display_colors::rgb(*sim, e.player);
		return "[color=#" + String::num_int64(rgb, 16).lpad(6, "0") + "]" + name + "[/color]";
	};
	const auto &now = net_host_.playerAssignment();
	if (net_host_.playerAssignments() != chat_assignments_seen_) {
		const bool first = chat_assignments_seen_ == 0 && chat_assignments_.empty();
		chat_assignments_seen_ = net_host_.playerAssignments();
		auto in = [](const std::vector<rtso::PlayerAssignmentMsg::Entry> &list, uint32_t client) {
			for (const auto &e : list)
				if (e.client == client && e.status != rtso::PlayerAssignmentMsg::LEFT) return &e;
			return (const rtso::PlayerAssignmentMsg::Entry *)nullptr;
		};
		if (!first) {
			for (const auto &e : chat_assignments_)
				if (e.status != rtso::PlayerAssignmentMsg::LEFT && !in(now, e.client))
					chat_window_.append_kind(sprintf0(tr("%(player)s has left the game."), {{"player", who(e)}}) + "\n", CHAT_FILTER_NETWORK);
			for (const auto &e : now)
				if (e.status != rtso::PlayerAssignmentMsg::LEFT && !in(chat_assignments_, e.client))
					chat_window_.append_kind(sprintf0(e.player < rtso::MAX_PLAYERS
							? tr("%(player)s is starting to rejoin the game.") : tr("%(player)s is starting to join the game."),
							{{"player", who(e)}}) + "\n", CHAT_FILTER_NETWORK);
		}
		chat_assignments_ = now;
	}
	post_kick_chat();
	// NMT_REJOINED, once the rejoiner has loaded (ChatMessageFormatNetwork rejoined).
	const auto &rejoined = turn_mgr_.rejoined_clients();
	for (; rejoined_chatted_ < rejoined.size(); ++rejoined_chatted_)
		for (const auto &e : now)
			if (e.client == rejoined[rejoined_chatted_] && e.status != rtso::PlayerAssignmentMsg::LEFT) {
				// ChatMessageFormatNetwork.rejoined: an observer "has joined the game.".
				chat_window_.append_kind(sprintf0(e.player < rtso::MAX_PLAYERS ? tr("%(player)s has rejoined the game.") : tr("%(player)s has joined the game."),
						{{"player", who(e)}}) + "\n", CHAT_FILTER_NETWORK);
				break;
			}
}

// 0 A.D. Menu.toggle: the menu slides open over the running game; only its
// Pause button (and the pages it opens in single player) pause.
void RtsoMain::_on_menu_button_pressed() {
	if (Node *pause = get_node_or_null(NodePath("PauseMenu"))) pause->call("toggle");
}

// Template Viewer for the selection: the selected structure, else the first
// selected unit's roster template (0 A.D. selection_details.js "i" button).
void RtsoMain::_on_template_info_pressed() {
	if (!sim || !ui_layer) return;
	String kind;
	int id = -1, civ = 0;
	if (selected_building_id != 0) {
		for (const auto &b : sim->buildings()) {
			if (b.id != selected_building_id) continue;
			kind = "structure";
			id = b.type;
			civ = b.team == rtso::GAIA_PLAYER ? sim->player_civ(local_player()) : sim->player_civ(b.team);
			break;
		}
	} else if (!selected_ids.is_empty()) {
		auto uv = sim->units();
		const uint32_t want = (uint32_t)(int64_t)selected_ids[0];
		for (size_t i = 0; i < uv.count; ++i) {
			if (uv.id[i] != want) continue;
			if (uv.tmpl && uv.tmpl[i] != rtso::CIV_UNIT_TEMPLATE_NONE) {
				kind = "unit";
				id = uv.tmpl[i];
			}
			break;
		}
	}
	if (kind.is_empty()) {
		show_notice("No details for this selection.");
		return;
	}
	open_template_viewer(kind, id, civ);
}

// selection_panels.js showTemplateDetails: the Template Viewer over the game; a
// single-player game waits while it is open (implicitPause, then resumeGame).
void RtsoMain::open_template_viewer(const String &kind, int id, int civ) {
	Ref<Script> viewer = ResourceLoader::get_singleton()->load("res://scenes/reference/template_viewer.gd");
	if (!viewer.is_valid() || !ui_layer) return;
	Node *v = Object::cast_to<Node>(viewer->call("open", ui_layer, kind, id, civ));
	SceneTree *tree = get_tree();
	if (!v || !tree || net_lockstep() || tree->is_paused()) return;
	tree->set_pause(true);
	v->connect("tree_exited", Callable(tree, "set_pause").bind(false), CONNECT_ONE_SHOT);
}

// A command panel button's right click (selection_panels.js onPressRight showTemplateDetails):
// the Template Viewer for its structure, technology or unit.
void RtsoMain::_on_template_details_input(const Ref<InputEvent> &event, const String &kind, int id, int civ) {
	Ref<InputEventMouseButton> mb = event;
	if (mb.is_null() || !mb->is_pressed() || mb->get_button_index() != MOUSE_BUTTON_RIGHT || !ui_layer) return;
	open_template_viewer(kind, id, civ);
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
		Ref<Script> viewer = ResourceLoader::get_singleton()->load("res://scenes/reference/template_viewer.gd");
		if (viewer.is_valid())
			viewer->call("open", ui_layer, inspector_.portrait_viewer_kind(), inspector_.portrait_viewer_id(), inspector_.portrait_viewer_civ());
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
	set_default_formation(cur == rtso::FORMATION_NONE ? last_default_formation_ : rtso::FORMATION_NONE);
}

void RtsoMain::apply_default_formation(const uint32_t *ids, size_t n) {
	const uint8_t f = RtsoCommandPanelView::default_formation();
	if (f == rtso::FORMATION_NONE || n < 2 || !sim) return;
	// Units already in a formation keep it (the walk reuses its template).
	for (const auto &fc : sim->formations())
		for (uint32_t m : fc.members)
			if (std::find(ids, ids + n, m) != ids + n) return;
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
// go back; Order.CancelPack / CancelUnpack (the opposite order, while in progress).
void RtsoMain::_on_pack_cancel_pressed(int pack) {
	if (!sim || selected_ids.is_empty()) return;
	const uint8_t want = pack ? rtso::PACK_PACKING : rtso::PACK_UNPACKING;
	std::vector<uint32_t> ids;
	auto uv = sim->units();
	for (int s = 0; s < selected_ids.size(); ++s)
		for (size_t i = 0; i < uv.count; ++i)
			if (uv.id[i] == (uint32_t)selected_ids[s]) {
				if (uv.alive[i] && uv.pack_state[i] == want) ids.push_back(uv.id[i]);
				break;
			}
	if (ids.empty()) return;
	if (turn_cmds())
		post_lockstep((uint8_t)rtso::NetCommandType::NET_CMD_PACK, ids.data(), ids.size(), 0, 0, 0, pack ? 0 : 1);
	else
		sim->order_pack(ids.data(), ids.size(), pack == 0);
}

// BarterButton sell press: BarterButtonManager.setSelectedResource.
void RtsoMain::_on_barter_sell_selected(int resource) {
	command_panel_.set_barter_sell(resource);
	update_command_panel();
}

bool RtsoMain::save_game(String path, String description) {
	if (!sim) return false;
	String global_path = ProjectSettings::get_singleton()->globalize_path(path);
	Ref<DirAccess> dir = DirAccess::open("user://");
	if (dir.is_valid()) {
		dir->make_dir_recursive("saves");
	}
	bool res = sim->save_state(global_path.utf8().get_data());
	if (res) {
		// The save and load dialogs list saves by this metadata (0 A.D. gui/savedgames).
		write_save_metadata(path, description);
		write_save_thumbnail(path);
		print_line("[RTS0] Game saved successfully to " + path);
	}
	return res;
}

bool RtsoMain::load_game(String path) {
	if (!sim) return false;
	String global_path = ProjectSettings::get_singleton()->globalize_path(path);
	load_progress(50, "Loading saved game");
	// The replay so far stays valid up to here; what follows starts from the save. Loading
	// leaves this match (0.28 endGame), so its replay keeps its summary.
	stop_replay_recording(true);
	bool success = sim->load_state(global_path.utf8().get_data());
	if (success) {
		selected_ids.clear();
		selected_building_id = 0;
		restore_saved_gui(path);
		// The models the saved world draws load here, behind the loading page (0.28's
		// LDR_ProgressiveLoad), not on the first frame of play.
		if (view) {
			const uint64_t t0 = Time::get_singleton()->get_ticks_msec();
			view->warm_models(sim, [this](int done, int total) {
				load_progress(60 + 35 * done / std::max(1, total), "Loading models");
			});
			print_line(String("[RTS0] Loaded the saved world's models in ") + String::num_int64(Time::get_singleton()->get_ticks_msec() - t0) + " ms");
		}
		print_line("[RTS0] Game loaded successfully from " + path);
		// The game goes on from the save as a new replay that starts from it.
		if (!loading_replay_start_ && !net_lockstep()) begin_recording_from_save(path);
	}
	return success;
}

PackedByteArray RtsoMain::get_fow_grid() const {
	if (!sim) return PackedByteArray();
	const uint8_t* grid = sim->fow_grid(view_player());
	int sz = sim->fow_res() * sim->fow_res();
	PackedByteArray arr;
	arr.resize(sz);
	for (int i = 0; i < sz; ++i) arr.set(i, grid[i]);
	return arr;
}

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
void RtsoMain::set_observer_view(int player) {
	observer_view_gaia_ = player == rtso::GAIA_PLAYER;
	observer_view_ = player >= 0 && player < rtso::MAX_PLAYERS ? (uint8_t)player : rtso::OBSERVER_VIEW;
	clear_selection();
	sync_observer_view_picker();
}

void RtsoMain::sync_observer_view_picker() {
	if (observer_view_picker_) {
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
	top_panel_.update(sim, local_player(), match_elapsed_time, spectating() || turn_mgr_.is_replaying());
	if (sim) {
		const uint8_t me = local_player();
		const int idle = me < rtso::MAX_PLAYERS ? (int)sim->get_idle_workers(me).size() : 0;
		minimap_panel_.update(idle, rtso::display_colors::state().enabled, me >= rtso::MAX_PLAYERS || spectating(),
				turn_mgr_.is_replaying());
		const uint8_t vp = view_player();
		minimap_panel_.set_civ(vp < rtso::MAX_PLAYERS ? rtso::civ_info(sim->player_civ(vp)).code : "gaia");
	}
	// GameSpeedButton: hidden in networked games (g_IsNetworked), shown in replays with Fast Forward.
	if (speed_btn) speed_btn->set_visible(!net_lockstep());
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
	for (size_t i = 0; i < uview.count; ++i)
		if (uview.alive[i] && wanted.count((int32_t)uview.id[i])) living.insert((int32_t)uview.id[i]);
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
	auto live_own = [&](uint32_t id) {
		for (const auto &b : sim->buildings())
			if (b.id == id) return !b.wrecked && b.hp > 0.0f && b.team == local_player();
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
					rtso::building_trains_template(b.type, b.civ, sim->player_civ(b.team), tmpl))
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
	if (mb->is_double_click() || !rtso::hotkeys::held("selection.add")) {
		selected_ids.clear();
		selected_building_id = 0;
	}
	if (!selected_ids.has((int32_t)id)) selected_ids.push_back((int32_t)id);
	if (mb->is_double_click() && cam_rig) cam_rig->set_target_pos(Vector3(uview.pos_x[i], 0, uview.pos_z[i]));
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

// DeveloperOverlay.toggle: only with cheats in a networked game; DeveloperOverlay.sendNotification
// tells the other players ("aichat", shown as the player's chat line).
void RtsoMain::toggle_dev_overlay() {
	if (!sim || (net_lockstep() && !sim->cheats_enabled())) return;
	const bool open = !dev_overlay_.is_open();
	dev_overlay_.set_open(open);
	chat_window_.post_own(tr(open ? "The Developer Overlay was opened." : "The Developer Overlay was closed."));
	refresh_dev_overlay();
}

// GameView.cpp m_CullCamera: freeze the view camera's frustum (outward planes for
// the animation culling, corners for the frustum overlay); cleared on unlock.
void RtsoMain::lock_cull_camera() {
	locked_cull_planes_.clear();
	locked_cull_corners_.clear();
	Camera3D *cam = cam_rig ? cam_rig->get_camera() : nullptr;
	if (!cam || !cam->get_viewport()) return;
	const TypedArray<Plane> planes = cam->get_frustum();
	for (int i = 0; i < planes.size(); ++i) locked_cull_planes_.push_back(planes[i]);
	const Vector2 size = cam->get_viewport()->get_visible_rect().size;
	if (size.x <= 0.0f || size.y <= 0.0f) return;
	const float d_near = cam->get_near(), d_far = cam->get_far();
	const Vector2 at[4] = { Vector2(0, 0), Vector2(size.x, 0), Vector2(0, size.y), Vector2(size.x, size.y) };
	Vector3 corners[8];
	for (int i = 0; i < 4; ++i) {
		const Vector3 o = cam->project_ray_origin(at[i]);
		const Vector3 d = cam->project_ray_normal(at[i]);
		corners[i] = o + d * d_near;
		corners[i + 4] = o + d * d_far;
	}
	for (int i = 0; i < 8; ++i) locked_cull_corners_.push_back(corners[i]);
}

void RtsoMain::sync_cull_camera() {
	if (view) {
		view->set_anim_culling(culling_enabled_);
		view->set_locked_cull_frustum(lock_cull_camera_ ? locked_cull_planes_ : std::vector<Plane>());
	}
	debug_overlays_.set_cull_corners(lock_cull_camera_ ? locked_cull_corners_ : std::vector<Vector3>());
}

// DeveloperOverlayControlCheckboxes.js onPress handlers.
void RtsoMain::_on_dev_command(int command) {
	using D = rtso::RtsoDevOverlay;
	if (!sim) return;
	const bool on = dev_overlay_.checked(command);
	const uint8_t me = local_player();
	auto cheat = [&](uint8_t action, int32_t parameter, const std::vector<uint32_t> &ids) {
		if (turn_cmds()) post_lockstep(39, ids.data(), ids.size(), 0.0f, 0.0f, (uint32_t)parameter, action);
		else sim->cmd_cheat(me, action, parameter, 0, ids.data(), ids.size());
	};
	switch (command) {
		case D::DEV_CONTROL_ALL: // ControlAll: Commands.js "control-all", Player.js SetControlAllUnits
			cheat(rtso::Simulation::CHEAT_DEV_CONTROL_ALL, on ? 1 : 0, {});
			break;
		case D::DEV_CHANGE_PERSPECTIVE: // ChangePerspective: PlayerViewControl.setChangePerspective
			change_perspective_ = on;
			update_observer_view_picker();
			break;
		case D::DEV_SELECTION_STATE: // SelectionEntityState
			dev_overlay_.show_state(on);
			break;
		case D::DEV_PATHFINDER_OVERLAY: // PathfinderOverlay: SetPathfinderDebugOverlay
			debug_overlays_.set(rtso::RtsoDebugOverlays::PATHFINDER, on);
			break;
		case D::DEV_HIER_PATHFINDER_OVERLAY: // HierarchicalPathfinderOverlay: SetPathfinderHierDebugOverlay
			debug_overlays_.set(rtso::RtsoDebugOverlays::HIER_PATHFINDER, on);
			break;
		case D::DEV_OBSTRUCTION_OVERLAY: // ObstructionOverlay: SetObstructionDebugOverlay
			debug_overlays_.set(rtso::RtsoDebugOverlays::OBSTRUCTION, on);
			break;
		case D::DEV_UNIT_MOTION_OVERLAY: // UnitMotionOverlay: SetMotionDebugOverlay on every entity
			debug_overlays_.set(rtso::RtsoDebugOverlays::UNIT_MOTION, on);
			break;
		case D::DEV_RANGE_OVERLAY: // RangeOverlay: SetRangeDebugOverlay
			debug_overlays_.set(rtso::RtsoDebugOverlays::RANGE, on);
			break;
		case D::DEV_BOUNDING_BOX_OVERLAY: // BoundingBoxOverlay: Engine.SetBoundingBoxDebugOverlay
			debug_overlays_.set(rtso::RtsoDebugOverlays::BOUNDING_BOX, on);
			break;
		case D::DEV_REVEAL_MAP: // RevealMap: "reveal-map" with enable
			cheat(rtso::Simulation::CHEAT_DEV_REVEAL_MAP, on ? 1 : 0, {});
			break;
		case D::DEV_RESTRICT_CAMERA: // RestrictCamera: GameView_SetConstrainCameraEnabled
			if (cam_rig) cam_rig->set_constrain_camera(on);
			break;
		case D::DEV_REJOIN_TEST: // ActivateRejoinTest: its SessionMessageBox, OK starting it
			if (on && !rejoin_test_) quit_confirmation("rejoin_test");
			break;
		case D::DEV_TIME_WARP: // EnableTimeWarp: TimeWarp.setEnabled
			set_time_warp(on);
			break;
		case D::DEV_PROMOTE: { // PromoteSelectedUnits: "promote" the selection; checked() is always false
			std::vector<uint32_t> ids;
			for (int k = 0; k < selected_ids.size(); ++k) ids.push_back((uint32_t)(int64_t)selected_ids[k]);
			cheat(rtso::Simulation::CHEAT_DEV_PROMOTE, 0, ids); // "(Cheat - promoted units)" even with nothing selected
			break;
		}
		case D::DEV_ENABLE_CULLING: // EnableCulling: GameView_SetCullingEnabled
			culling_enabled_ = on;
			sync_cull_camera();
			break;
		case D::DEV_LOCK_CULL_CAMERA: // LockCullCamera: GameView_SetLockCullCameraEnabled
			lock_cull_camera_ = on;
			if (on) lock_cull_camera();
			else {
				locked_cull_planes_.clear();
				locked_cull_corners_.clear();
			}
			sync_cull_camera();
			break;
		case D::DEV_DISPLAY_CAMERA_FRUSTUM: // DisplayCameraFrustum: Renderer_SetDisplayFrustumEnabled
			debug_overlays_.set(rtso::RtsoDebugOverlays::CULL_CAMERA_FRUSTUM, on);
			break;
		case D::DEV_DISPLAY_SHADOWS_FRUSTUM: // DisplayShadowsFrustum: Renderer_SetDisplayShadowsFrustumEnabled
			debug_overlays_.set(rtso::RtsoDebugOverlays::SHADOWS_FRUSTUM, on);
			break;
		default:
			break;
	}
	refresh_dev_overlay();
}

// RenderDebugMode.onSelectionChange: Renderer_SetRenderDebugMode. Godot's viewport debug draws
// stand in for 0.28's shader defines: AO shows the ambient occlusion buffer, Alpha the
// overdraw of blended surfaces, and Custom draws albedo only, unlit.
void RtsoMain::_on_render_debug_mode(int index) {
	using D = rtso::RtsoDevOverlay;
	if (index < 0 || index >= D::RENDER_DEBUG_COUNT) return;
	render_debug_mode_ = index;
	static constexpr Viewport::DebugDraw kDraw[D::RENDER_DEBUG_COUNT] = {
		Viewport::DEBUG_DRAW_DISABLED, Viewport::DEBUG_DRAW_SSAO, Viewport::DEBUG_DRAW_OVERDRAW, Viewport::DEBUG_DRAW_UNSHADED,
	};
	if (Viewport *vp = get_viewport()) vp->set_debug_draw(kDraw[index]);
	refresh_dev_overlay();
}

void RtsoMain::activate_rejoin_test() {
	if (rejoin_test_ || !sim || turn_mgr_.is_replaying()) return;
	rejoin_test_ = true;
	refresh_dev_overlay();
	// 0.28 Engine.ActivateRejoinTest: LOGMESSAGERENDER("Rejoin test will activate in %i turns"); the copy joins after the next turn.
	show_notice(String("Rejoin test will activate in %d turns").replace("%d", "1"));
}

// After each turn: the first takes the snapshot and rejoins the copy from it (a fresh
// Simulation loading the save, as a rejoining client loads the host's state); each later
// one replays that turn's commands onto the copy, whose turn manager checks the copy's
// hash against the turn's.
void RtsoMain::rejoin_test_turn() {
	if (!rejoin_test_ || !sim || turn_mgr_.is_replaying() || turn_mgr_.turn_history().empty()) return;
	if (!rejoin_sim_) {
		const String path = ProjectSettings::get_singleton()->globalize_path("user://rejoin_test.rts0sav");
		const std::string p = path.utf8().get_data();
		auto copy = std::make_unique<rtso::Simulation>();
		if (!sim->save_state(p.c_str()) || !copy->load_state(p.c_str())) {
			DirAccess::remove_absolute(path);
			return;
		}
		DirAccess::remove_absolute(path);
		rejoin_sim_ = std::move(copy);
		rejoin_tm_ = std::make_unique<rtso::TurnManager>();
		rejoin_tm_->begin_shadow_replay(turn_mgr_);
		rejoin_from_turn_ = turn_mgr_.current_turn();
		return;
	}
	rejoin_tm_->push_shadow_frame(turn_mgr_.turn_history().back());
	if (!rejoin_tm_->tick(rtso::TurnManager::TURN_DURATION, *rejoin_sim_)) return;
	++rejoin_turns_;
	if (rejoin_tm_->replay_out_of_sync() && !rejoin_oos_turn_) {
		rejoin_oos_turn_ = rejoin_tm_->replay_out_of_sync_turn();
		quit_confirmation("out_of_sync_rejoin_test");
	}
}

Dictionary RtsoMain::get_rejoin_test() const {
	Dictionary d;
	d["active"] = rejoin_test_;
	d["rejoined_at"] = rejoin_from_turn_;
	d["turns"] = rejoin_turns_;
	d["out_of_sync_turn"] = rejoin_oos_turn_;
	return d;
}

// TimeWarp.setEnabled: not in networked games (nor replays); turning it on shows
// TimeWarpMessageBox and starts the snapshots (Engine.EnableTimeWarpRecording(10)).
void RtsoMain::set_time_warp(bool on) {
	if (net_lockstep() || turn_mgr_.is_replaying()) on = false;
	if (on == time_warp_) return;
	time_warp_ = on;
	if (!on) {
		if (time_warp_ff_) game_speed = 1.0f;
		time_warp_ff_ = false;
		for (const String &p : time_warp_snapshots_) DirAccess::remove_absolute(ProjectSettings::get_singleton()->globalize_path(p));
		time_warp_snapshots_.clear();
		return;
	}
	time_warp_turn_ = UINT32_MAX;
	quit_confirmation("time_warp");
}

// CTurnManager time warp: a snapshot of the game every 10 turns, and SetSimRate(20)
// while the fast forward hotkey is held, back to 1 when it is let go.
void RtsoMain::time_warp_tick() {
	if (!time_warp_ || !sim) return;
	const bool ff = rtso::hotkeys::held("session.timewarp.fastforward");
	if (ff != time_warp_ff_) {
		game_speed = ff ? 20.0f : 1.0f; // TimeWarp.resetSpeed: SetSimRate(1), whatever the speed was
		time_warp_ff_ = ff;
	}
	const uint32_t turn = turn_mgr_.current_turn();
	if (turn % 10 != 0 || turn == time_warp_turn_) return;
	time_warp_turn_ = turn;
	DirAccess::make_dir_recursive_absolute(ProjectSettings::get_singleton()->globalize_path("user://timewarp"));
	const String path = "user://timewarp/turn_" + String::num_int64(time_warp_serial_++) + ".rts0sav";
	if (!sim->save_state(ProjectSettings::get_singleton()->globalize_path(path).utf8().get_data())) return;
	time_warp_snapshots_.push_back(path);
	if (time_warp_snapshots_.size() > 100) { // TimeWarp's caption warns of memory: keep the last 100 on disk
		DirAccess::remove_absolute(ProjectSettings::get_singleton()->globalize_path(time_warp_snapshots_.front()));
		time_warp_snapshots_.erase(time_warp_snapshots_.begin());
	}
}

// CTurnManager::RewindTimeWarp: back to the latest snapshot, which is then dropped, so
// rewinding again goes further back. Only the simulation goes back: the control groups
// and selection stay, less what the snapshot doesn't have (0.28's GUI drops entities
// that no longer exist). The game goes on as a new replay from there, where 0.28 keeps
// writing a replay that can no longer be played.
bool RtsoMain::time_warp_rewind() {
	if (!time_warp_ || time_warp_snapshots_.empty()) return false;
	const String path = time_warp_snapshots_.back();
	time_warp_snapshots_.pop_back();
	PackedInt32Array groups[10];
	for (int g = 0; g < 10; ++g) groups[g] = selected_groups[g];
	const PackedInt32Array units = selected_ids;
	const std::vector<uint32_t> structures = selected_structures();
	const bool ok = load_game(path);
	DirAccess::remove_absolute(ProjectSettings::get_singleton()->globalize_path(path));
	time_warp_turn_ = turn_mgr_.current_turn(); // no snapshot of the state just restored
	if (ok) {
		auto exists = [this](int32_t id) { return sim->entity_owner((uint32_t)id) != 0xFF; };
		for (int g = 0; g < 10; ++g) {
			selected_groups[g].clear();
			for (int32_t id : groups[g])
				if (exists(id)) selected_groups[g].push_back(id);
		}
		selected_ids.clear();
		for (int32_t id : units)
			if (exists(id)) selected_ids.push_back(id);
		std::vector<uint32_t> kept;
		for (uint32_t id : structures)
			if (exists((int32_t)id)) kept.push_back(id);
		set_selected_structures(kept);
	}
	return ok;
}

// The checked() handlers while the overlay is open, and DeveloperOverlayEntityState.update:
// the first selected entity's state.
void RtsoMain::refresh_dev_overlay() {
	using D = rtso::RtsoDevOverlay;
	if (!sim) return;
	// ControlAll's checked(): the viewed player's controlsAll; the cheat applies to
	// the local player, which is the viewed one outside observer views.
	const uint8_t me = local_player();
	if (dev_overlay_.is_open()) {
		dev_overlay_.set_checked(D::DEV_CONTROL_ALL, sim->controls_all(me));
		dev_overlay_.set_checked(D::DEV_REVEAL_MAP, sim->reveal_map());
		dev_overlay_.set_checked(D::DEV_RESTRICT_CAMERA, cam_rig ? cam_rig->is_constrain_camera() : true);
		dev_overlay_.set_checked(D::DEV_ENABLE_CULLING, culling_enabled_); // checked(): GameView_GetCullingEnabled
		dev_overlay_.set_checked(D::DEV_LOCK_CULL_CAMERA, lock_cull_camera_); // checked(): GameView_GetLockCullCameraEnabled
		dev_overlay_.set_checked(D::DEV_PROMOTE, false);
		dev_overlay_.set_checked(D::DEV_TIME_WARP, time_warp_);
		dev_overlay_.set_checked(D::DEV_REJOIN_TEST, rejoin_test_); // checked(): this.disabled
		// 0.28 enabled(): !this.disabled && g_InitAttributes.mapType != "random".
		dev_overlay_.set_enabled(D::DEV_REJOIN_TEST, !rejoin_test_ && !random_map_ && !turn_mgr_.is_replaying());
		using O = rtso::RtsoDebugOverlays;
		dev_overlay_.set_checked(D::DEV_PATHFINDER_OVERLAY, debug_overlays_.on(O::PATHFINDER));
		dev_overlay_.set_checked(D::DEV_HIER_PATHFINDER_OVERLAY, debug_overlays_.on(O::HIER_PATHFINDER));
		dev_overlay_.set_checked(D::DEV_OBSTRUCTION_OVERLAY, debug_overlays_.on(O::OBSTRUCTION));
		dev_overlay_.set_checked(D::DEV_UNIT_MOTION_OVERLAY, debug_overlays_.on(O::UNIT_MOTION));
		dev_overlay_.set_checked(D::DEV_RANGE_OVERLAY, debug_overlays_.on(O::RANGE));
		dev_overlay_.set_checked(D::DEV_BOUNDING_BOX_OVERLAY, debug_overlays_.on(O::BOUNDING_BOX));
		dev_overlay_.set_checked(D::DEV_DISPLAY_CAMERA_FRUSTUM, debug_overlays_.on(O::CULL_CAMERA_FRUSTUM));
		dev_overlay_.set_checked(D::DEV_DISPLAY_SHADOWS_FRUSTUM, debug_overlays_.on(O::SHADOWS_FRUSTUM));
		dev_overlay_.set_render_debug(render_debug_mode_);
	}
	if (!dev_overlay_.state_shown()) return;
	// GuiInterface GetSimulationState's keys in its order, players omitted (DeveloperOverlayEntityState.update).
	// mapSize is in 0.28 metres (4 per tile); times in milliseconds, as cmpTimer.
	String text = vformat("simulation: {players:\"<<<omitted>>>\", circularMap:%s, mapSize:%d, timeElapsed:%d",
			sim->circular_map() ? "true" : "false", (int64_t)std::lround(sim->map_half() * 2.0f * 4.0f),
			(int64_t)(match_elapsed_time * 1000.0));
	text += vformat(", ceasefireActive:%s, ceasefireTimeRemaining:%d, cinemaPlaying:%s", sim->ceasefire_active() ? "true" : "false",
			(int64_t)std::lround(sim->ceasefire_remaining() * 1000.0f), sim->cinema_playing() ? "true" : "false");
	{
		// EndGameManager GetVictoryConditions: the victory_conditions/*.json names.
		static constexpr struct { uint8_t bit; const char *name; } kVictory[] = {
			{rtso::VK_CONQUEST, "conquest"}, {rtso::VK_CONQUEST_UNITS, "conquest_units"},
			{rtso::VK_CONQUEST_STRUCTURES, "conquest_structures"}, {rtso::VK_CONQUEST_CIVIC_CENTRES, "conquest_civic_centers"},
			{rtso::VK_WONDER, "wonder"}, {rtso::VK_RELIC, "capture_the_relic"}, {rtso::VK_REGICIDE, "regicide"},
		};
		String vc;
		for (const auto &v : kVictory)
			if (sim->has_victory(v.bit)) vc += String(vc.is_empty() ? "\"" : ", \"") + v.name + "\"";
		// PopulationCapManager: "player", "world" or "team" and its value.
		const char *cap_type = sim->world_pop_cap() ? "world" : sim->team_pop_cap() ? "team" : "player";
		const int cap = sim->world_pop_cap() ? sim->world_pop_cap() : sim->team_pop_cap() ? sim->team_pop_cap() : sim->pop_cap_setting();
		text += vformat(", victoryConditions:[%s], alliedVictory:%s, populationCapType:\"%s\", populationCap:%d}", vc,
				sim->last_man_standing() ? "false" : "true", cap_type, (int64_t)cap);
	}
	const uint32_t first = !selected_ids.is_empty() ? (uint32_t)(int64_t)selected_ids[0] : 0;
	if (first) {
		auto uv = sim->units();
		for (size_t i = 0; i < uv.count; ++i) {
			if (uv.id[i] != first) continue;
			const uint16_t tmpl = uv.tmpl ? uv.tmpl[i] : rtso::CIV_UNIT_TEMPLATE_NONE;
			const char *tid = tmpl < rtso::CIV_UNIT_TEMPLATE_COUNT ? rtso::civ_unit_template(tmpl).id : "";
			text += "\n\nentity: {\n";
			text += vformat("  id:%d\n  template:\"%s\"\n  player:%d\n", (int64_t)uv.id[i], String::utf8(tid ? tid : ""), (int64_t)uv.team[i]);
			text += vformat("  hitpoints:%.1f\n  maxHitpoints:%.1f\n", uv.hp[i], uv.max_hp[i]);
			text += vformat("  position:{x:%.2f, z:%.2f}\n  heading:{x:%.2f, z:%.2f}\n", uv.pos_x[i], uv.pos_z[i],
					uv.heading_x ? uv.heading_x[i] : 0.0f, uv.heading_z ? uv.heading_z[i] : 1.0f);
			text += vformat("  unitType:%d\n  actionState:%d\n  stance:%d\n  rank:%d\n  xp:%d\n  kills:%d\n",
					(int64_t)uv.unit_type[i], (int64_t)uv.action_state[i], (int64_t)uv.stance[i], (int64_t)uv.rank[i],
					(int64_t)(uv.xp ? uv.xp[i] : 0), (int64_t)(uv.kills ? uv.kills[i] : 0));
			text += vformat("  carrying:{type:%d, amount:%d}\n  formation:%d\n", (int64_t)uv.res_type_carried[i],
					(int64_t)uv.carry[i], (int64_t)uv.formation[i]);
			// The rest of the entity's live state (0.28 lists every GetEntityState key).
			text += vformat("  alive:%d\n  velocity:{x:%.2f, z:%.2f}\n  target:{x:%.2f, z:%.2f}\n  targetResource:%d\n",
					(int64_t)uv.alive[i], uv.vel_x[i], uv.vel_z[i], uv.target_x[i], uv.target_z[i], (int64_t)uv.target_resource_id[i]);
			text += vformat("  deathTimer:%.2f\n  statusEffects:%d\n  packState:%d\n  packElapsed:%.2f\n}",
					uv.death_timer ? uv.death_timer[i] : 0.0f, (int64_t)(uv.status_effects ? uv.status_effects[i] : 0),
					(int64_t)(uv.pack_state ? uv.pack_state[i] : 0), uv.pack_elapsed ? uv.pack_elapsed[i] : 0.0f);
			if (tmpl < rtso::CIV_UNIT_TEMPLATE_COUNT) {
				// template: GetTemplateData of the entity's template.
				const rtso::CivUnitTemplate &t = rtso::civ_unit_template(tmpl);
				text += vformat("\n\ntemplate: {name:{generic:\"%s\", specific:\"%s\"}, civ:%d, phase:%d, ",
						String::utf8(t.name ? t.name : ""), String::utf8(t.specific_name ? t.specific_name : ""), (int64_t)t.civ, (int64_t)t.phase);
				text += vformat("cost:{food:%d, wood:%d, stone:%d, metal:%d, population:%d, time:%.1f}, ", (int64_t)t.food_cost,
						(int64_t)t.wood_cost, (int64_t)t.stone_cost, (int64_t)t.gold_cost, (int64_t)t.pop_cost, t.train_time);
				text += vformat("maxHitpoints:%.1f, speed:%.2f, vision:%.2f, ", t.hp, t.speed, t.vision);
				text += vformat("attack:{hack:%.1f, pierce:%.1f, crush:%.1f, range:%.2f, repeatTime:%.2f}, ", t.hack_damage,
						t.pierce_damage, t.crush_damage, t.attack_range, t.attack_cooldown);
				text += vformat("resistance:{hack:%.1f, pierce:%.1f, crush:%.1f}, carryCapacity:%d}", t.hack_armor, t.pierce_armor,
						t.crush_armor, (int64_t)t.carry_capacity);
			}
			break;
		}
	} else if (selected_building_id) {
		for (const auto &b : sim->buildings()) {
			if (b.id != selected_building_id) continue;
			text += "\n\nentity: {\n";
			text += vformat("  id:%d\n  type:%d\n  player:%d\n  hitpoints:%.1f\n  maxHitpoints:%.1f\n", (int64_t)b.id,
					(int64_t)b.type, (int64_t)b.team, b.hp, b.max_hp);
			text += vformat("  position:{x:%.2f, z:%.2f}\n  rotation:%.2f\n  buildProgress:%.2f\n  queue:%d\n", b.x, b.z, b.rot,
					b.build_progress, (int64_t)b.queue.size());
			text += vformat("  garrisoned:%d\n  capturePoints:%.1f\n  maxCapturePoints:%.1f\n}", (int64_t)sim->get_garrison_count(b.id),
					b.capture_points, b.max_capture_points);
			// template: GetTemplateData of the structure's template (its civ's, Gaia's plain one).
			const rtso::BuildingTypeInfo &info = rtso::building_type_info(b.type);
			const Dictionary e = rtso::portraits::structure_entry(b.civ, b.type);
			float cost[rtso::RES_COUNT] = {};
			sim->structure_cost_for(b.team, b.type, cost);
			text += vformat("\n\ntemplate: {template:\"%s\", name:{generic:\"%s\", specific:\"%s\"}, civ:%d, ", e.get("template", String()),
					e.get("generic_name", String(info.name)), e.get("specific_name", String()), (int64_t)b.civ);
			text += vformat("cost:{food:%d, wood:%d, stone:%d, metal:%d, time:%.1f}, ", (int64_t)std::lround(cost[rtso::RES_FOOD]),
					(int64_t)std::lround(cost[rtso::RES_WOOD]), (int64_t)std::lround(cost[rtso::RES_STONE]),
					(int64_t)std::lround(cost[rtso::RES_GOLD]), sim->structure_build_time_for(b.team, b.type));
			text += vformat("maxHitpoints:%.1f, population:{bonus:%d}, garrisonHolder:{max:%d}, capturable:{max:%.1f}}",
					sim->structure_max_hp(b.type, b.team), (int64_t)info.pop_bonus, (int64_t)info.garrison_cap, info.capture_max_points);
			break;
		}
	}
	dev_overlay_.set_state_text(text);
}

// The group's living units (dead ones leave it), then selected or added;
// "snap" also moves the camera to the first.
void RtsoMain::perform_group(int grp, GroupAction action) {
	if (!sim || grp < 0 || grp > 9) return;
	// input.js performGroup: the group's living units, and its structure (one at a time
	// here); a member inside a structure stays in the group and the camera snaps to its
	// holder when nothing else of the group is out.
	PackedInt32Array recall, kept;
	uint32_t recall_building = 0;
	bool have_pos = false;
	Vector3 first_pos, holder_pos;
	bool have_holder = false;
	auto gview = sim->units();
	for (int m = 0; m < selected_groups[grp].size(); ++m) {
		const int32_t gid = selected_groups[grp][m];
		bool found = false;
		for (size_t i = 0; i < gview.count; ++i) {
			if (gview.id[i] != (uint32_t)gid || gview.team[i] != local_player()) continue;
			if (gview.alive[i]) {
				if (!have_pos) first_pos = Vector3(gview.pos_x[i], 0, gview.pos_z[i]);
				have_pos = true;
				recall.push_back(gid);
				found = true;
			} else if (gview.hp[i] > 0.0f) {
				for (const auto &b : sim->buildings())
					if (std::find(b.garrisoned_units.begin(), b.garrisoned_units.end(), (uint32_t)gid) != b.garrisoned_units.end()) {
						if (!have_holder) holder_pos = Vector3(b.x, 0, b.z);
						have_holder = found = true;
						break;
					}
			}
			break;
		}
		if (!found)
			for (const auto &b : sim->buildings())
				if (b.id == (uint32_t)gid && b.team == local_player() && !b.wrecked && b.hp > 0.0f) {
					if (!recall_building) recall_building = b.id;
					if (!have_pos) first_pos = Vector3(b.x, 0, b.z);
					have_pos = found = true;
					break;
				}
		if (found) kept.push_back(gid);
	}
	selected_groups[grp] = kept;
	if (kept.is_empty()) return;
	if (recall.is_empty() && !recall_building) {
		// Everyone is inside: snap to the holder, the selection stays.
		if (action == GROUP_SNAP && cam_rig) cam_rig->set_target_pos(holder_pos);
		return;
	}
	if (action == GROUP_ADD) {
		for (int m = 0; m < recall.size(); ++m)
			if (!selected_ids.has(recall[m])) selected_ids.push_back(recall[m]);
		if (recall_building) selected_building_id = recall_building;
	} else {
		selected_ids = recall;
		selected_building_id = recall_building;
	}
	if (action == GROUP_SNAP && cam_rig && (have_pos || have_holder)) cam_rig->set_target_pos(have_pos ? first_pos : holder_pos);
	play_civ_voice("select");
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

void RtsoMain::enter_build_mode(bool enable, uint8_t btype) {
	build_mode = enable;
	current_build_type = btype;
	build_click_pending = false;
	placement_angle = rtso::Simulation::DEFAULT_BUILDING_ROT; // placementSupport.Reset()
	if (!enable) {
		wall_drag_mode = false;
		for (auto *wg : wall_ghosts) {
			if (wg) wg->set_visible(false);
		}
	}
	update_ghost_mesh();
	if (ghost_mesh) ghost_mesh->set_visible(build_mode && !wall_drag_mode);
}

float RtsoMain::placement_angle_at(uint8_t btype, float x, float z) const {
	if (btype == rtso::BLD_DOCK && sim) {
		const float a = sim->dock_angle(x, z, rtso::building_type_info(btype).footprint * rtso::Simulation::GRID * 0.5f);
		if (!std::isnan(a)) return a;
	}
	return placement_angle;
}

bool RtsoMain::snap_placement_to_edges(uint8_t btype, int &cell_x, int &cell_y, bool hover) {
	// A dock faces the sea (dock_angle): the simulation turns it so whatever the command says.
	if (!sim || btype == rtso::BLD_DOCK) return false;
	// isSnapToEdgesEnabled: the hotkey turns it on, or off when the option is "enabled".
	if (!debug_force_snap_ && rtso::hotkeys::held("session.snaptoedges") == (rtso::user_config::get("gui.session.snaptoedges") == "enabled")) return false;
	// input.js: the hover resets the angle first, to snap to the edge nearest the default facing.
	if (hover) placement_angle = rtso::Simulation::DEFAULT_BUILDING_ROT;
	const float GRID = rtso::Simulation::GRID;
	const int fp = rtso::building_type_info(btype).footprint;
	const float half = fp * GRID * 0.5f;
	const float x = cell_x * GRID + half, z = cell_y * GRID + half;
	// The structures the player has seen (GetEntitiesWithStaticObstructionOnScreen draws mirages too).
	std::vector<rtso::snap::Rect> rects;
	const float reach = rtso::snap::kDistanceThreshold + 2.0f * half + 4.0f * GRID;
	for (const auto &b : sim->buildings()) {
		if (b.wrecked || std::fabs(b.x - x) > reach || std::fabs(b.z - z) > reach) continue;
		if (!sim->los_explored(local_player(), b.x, b.z)) continue;
		rects.push_back({b.x, b.z, b.half_size, b.half_d, b.rot});
	}
	// Mines and ruins have static obstructions too (template_gaia_ore, _rock, _ruins).
	auto rv = sim->resources();
	for (size_t i = 0; i < rv.count; ++i) {
		if (!rv.alive[i] || std::fabs(rv.x[i] - x) > reach || std::fabs(rv.z[i] - z) > reach) continue;
		rtso::nav::Footprint f;
		if (!sim->mine_footprint(i, f) || !sim->los_explored(local_player(), f.x, f.z)) continue;
		rects.push_back({f.x, f.z, f.hu, f.hv, std::atan2(-f.uz, f.ux)}); // snap::rotate turns (1, 0) to (cos, -sin)
	}
	const std::vector<rtso::snap::Edge> edges = rtso::snap::edges_near(rects, x, z);
	rtso::snap::Result r;
	if (!rtso::snap::snap_position(edges, half, rtso::building_half_depth(btype, half), x, z, placement_angle, r)) return false;
	placement_angle = r.angle;
	// RTS0 places on GRID cells: the cell nearest the flush centre that can be built, else the nearest.
	const float fx = r.x / GRID - fp * 0.5f, fz = r.z / GRID - fp * 0.5f;
	int best_x = (int)std::lround(fx), best_y = (int)std::lround(fz);
	float best_d = 1e30f;
	for (int cx = (int)std::floor(fx); cx <= (int)std::floor(fx) + 1; ++cx)
		for (int cy = (int)std::floor(fz); cy <= (int)std::floor(fz) + 1; ++cy) {
			const float d = (cx - fx) * (cx - fx) + (cy - fz) * (cy - fz);
			if (d >= best_d) continue;
			if (sim->check_placement(local_player(), cx, cy, btype, r.angle) != rtso::Simulation::PLACE_OK) continue;
			best_d = d;
			best_x = cx;
			best_y = cy;
		}
	cell_x = best_x;
	cell_y = best_y;
	return true;
}

void RtsoMain::try_train_unit(rtso::UnitType utype) {
	if (!sim || !rtso::civ_can_train(sim->player_civ(local_player()), (uint8_t)utype)) return;
	// The selected structure if it trains the class, else the first finished one
	// that does (0 A.D. trains a unit only where its template's Trainer lists it).
	const uint8_t civ = sim->player_civ(local_player());
	uint32_t bid = 0;
	for (const auto &b : sim->buildings()) {
		if (b.team != local_player() || b.wrecked || b.build_progress < 1.0f) continue;
		if (!building_trains_class(civ, (uint8_t)utype, b.type)) continue;
		if (b.id == selected_building_id) { bid = b.id; break; }
		if (bid == 0) bid = b.id;
	}
	if (bid == 0) return;
	const int count = train_batch();
	if (turn_cmds()) {
		post_lockstep(6, nullptr, 0, 0, (float)count, bid, (uint8_t)utype);
		play_civ_voice("select");
		return;
	}
	if (sim->cmd_train(local_player(), bid, utype, count))
		play_civ_voice("select");
}

int RtsoMain::train_batch() const {
	return rtso::hotkeys::held("session.batchtrain") ? user_opts_.batch_size : 1;
}

int RtsoMain::ui_pending_batch(uint16_t tmpl) const {
	const BatchTraining &bt = batch_training_;
	return bt.active && bt.tmpl == tmpl ? bt.batches * std::max(1, user_opts_.batch_size) : 0;
}

// EntityLimits canBeAddedCount for a template: how many more the player may have, -1 if no limit.
int RtsoMain::template_can_be_added(uint16_t tmpl) const {
	if (!sim || tmpl >= rtso::CIV_UNIT_TEMPLATE_COUNT) return -1;
	const uint8_t cat = rtso::UNIT_LIMITS[tmpl].category;
	if (cat == rtso::LIMIT_NONE || cat >= rtso::LIMIT_CATEGORY_COUNT) return -1;
	const int limit = sim->entity_limit(local_player(), cat);
	return limit < 0 ? -1 : std::max(0, limit - sim->entity_limit_count(local_player(), cat));
}

// GetNeededResources for `count` of a template: whether the player can pay for them all.
bool RtsoMain::can_afford_template(uint16_t tmpl, int count) const {
	if (!sim || tmpl >= rtso::CIV_UNIT_TEMPLATE_COUNT) return false;
	const auto &t = rtso::civ_unit_template(tmpl);
	float cost[rtso::RES_COUNT];
	sim->unit_train_cost(local_player(), tmpl, rtso::civ_template_unit_info(tmpl, t.unit_type), cost);
	const auto &ps = sim->player(local_player());
	for (int r = 0; r < rtso::RES_COUNT; ++r)
		if (ps.res[r] < cost[r] * (float)count) return false;
	return true;
}

// input.js addTrainingToQueue: with session.batchtrain (Shift) held, clicks on the same
// trainee of the same trainer build one batch of N x the batch size, posted when Shift is
// released (or another trainee is clicked); Ctrl takes a batch back off. Without Shift
// one unit is queued at once.
void RtsoMain::add_training(bool from_unit, uint32_t holder, uint16_t tmpl) {
	if (holder == 0 || tmpl >= rtso::CIV_UNIT_TEMPLATE_COUNT) return;
	const bool decrement = rtso::hotkeys::held("selection.remove");
	const int allowed = template_can_be_added(tmpl);
	const int size = std::max(1, user_opts_.batch_size);
	if (rtso::hotkeys::held("session.batchtrain") && (allowed < 0 || allowed > 1)) {
		BatchTraining &bt = batch_training_;
		if (bt.active) {
			if (bt.from_unit == from_unit && bt.holder == holder && bt.tmpl == tmpl) {
				if (decrement) {
					if (--bt.batches <= 0) bt.active = false;
				} else if (allowed < 0 ||
						allowed > bt.batches * size * (from_unit ? 1 : std::max<int>(1, (int)structure_trainers(tmpl, holder).size()))) {
					if (!can_afford_template(tmpl, (bt.batches + 1) * size)) return;
					++bt.batches;
				}
				bt.allowed = allowed;
				return;
			}
			if (!decrement) flush_training_batch();
		}
		if (decrement || !can_afford_template(tmpl, size)) return;
		bt = BatchTraining{true, from_unit, holder, tmpl, 1, allowed};
		return;
	}
	if (from_unit) {
		try_train_from_unit(holder, tmpl, 1);
		return;
	}
	// One unit in each selected structure that trains it, as far as the entity limit allows.
	std::vector<uint32_t> trainers = structure_trainers(tmpl, holder);
	if (allowed >= 0 && (int)trainers.size() > allowed) trainers.resize((size_t)allowed);
	for (uint32_t b : trainers) try_train_template(b, tmpl, 1);
}

// input.js flushTrainingBatch: the batch, cut to what the entity limit still allows.
void RtsoMain::flush_training_batch() {
	BatchTraining &bt = batch_training_;
	if (!bt.active) return;
	bt.active = false;
	int count = bt.batches * std::max(1, user_opts_.batch_size);
	if (bt.from_unit) {
		if (bt.allowed >= 0) count = std::min(count, bt.allowed);
		if (count > 0) try_train_from_unit(bt.holder, bt.tmpl, count);
		return;
	}
	// The batch in every selected structure that trains it; under an entity limit, full
	// batches in as many as it allows and the rest in one more.
	const std::vector<uint32_t> trainers = structure_trainers(bt.tmpl, bt.holder);
	if (bt.allowed >= 0 && bt.allowed < count * (int)trainers.size()) {
		const size_t full = (size_t)(bt.allowed / count);
		for (size_t k = 0; k < full && k < trainers.size(); ++k) try_train_template(trainers[k], bt.tmpl, count);
		const int rest = bt.allowed % count;
		if (rest > 0 && full < trainers.size()) try_train_template(trainers[full], bt.tmpl, rest);
		return;
	}
	for (uint32_t b : trainers) try_train_template(b, bt.tmpl, count);
}

// One queue item of `count` units (0 A.D. batch training: one cost x count, one shorter timer).
void RtsoMain::try_train_template(uint32_t building_id, uint16_t tmpl, int count) {
	if (building_id == 0 || tmpl >= rtso::CIV_UNIT_TEMPLATE_COUNT) return;
	if (turn_cmds()) {
		// input.js addTrainingToQueue / flushTrainingBatch: "pushFront" with session.pushorderfront.
		post_lockstep((uint8_t)rtso::NetCommandType::NET_CMD_TRAIN_TEMPLATE, nullptr, 0, (float)tmpl, (float)count, building_id,
				rtso::hotkeys::held("session.pushorderfront") ? rtso::NET_PARAM_PUSH_FRONT_ORDER : 0);
		play_civ_voice("select");
		return;
	}
	if (sim->cmd_train_template(local_player(), building_id, tmpl, count))
		play_civ_voice("select");
}

// FollowPlayer.xml: a "Follow Player" tick box with that tooltip, shown while an
// observer or replay viewer watches one player (not Observer).
CheckBox *RtsoMain::new_follow_player_box() {
	CheckBox *box = memnew(CheckBox);
	box->set_text("Follow Player");
	box->set_tooltip_text("Follow Player");
	box->set_focus_mode(Control::FOCUS_NONE);
	box->set_pressed_no_signal(follow_player_);
	box->set_visible(false);
	box->connect("toggled", Callable(this, "set_follow_player"));
	return box;
}

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

void RtsoMain::_on_map_selected(int index) {
	PackedStringArray maps = get_available_maps();
	if (index >= 0 && index < maps.size()) {
		load_map(maps[index]);
	}
}

PackedStringArray RtsoMain::get_available_maps() const {
	PackedStringArray maps;
	Ref<DirAccess> dir = DirAccess::open("res://data/maps");
	if (dir.is_valid()) {
		dir->list_dir_begin();
		String fname = dir->get_next();
		PackedStringArray json_maps;
		while (!fname.is_empty()) {
			if (!dir->current_is_dir() && fname.ends_with(".json")) {
				json_maps.push_back(fname.get_basename());
			}
			fname = dir->get_next();
		}
		json_maps.sort();
		for (int i = 0; i < json_maps.size(); ++i) {
			maps.push_back(json_maps[i]);
		}
	}
	if (maps.is_empty()) {
		maps.push_back("acropolis_bay");
		maps.push_back("corinthian_isthmus");
		maps.push_back("atlas_valleys");
		maps.push_back("bactria");
		maps.push_back("battle_for_the_tiber");
	}
	maps.push_back("default");
	maps.push_back("random_continental");
	maps.push_back("random_archipelago");
	maps.push_back("random_oasis");
	maps.push_back("random_highlands");
	maps.push_back("random_valley");
	maps.push_back("random_mediterranean");
	maps.push_back("random_acropolis");
	maps.push_back("random_spartan_valley");
	maps.push_back("random_danubius");
	maps.push_back("random_saharan_oases");
	maps.push_back("random_anatolian_plateau");
	maps.push_back("random_jebel_barkal");
	for (int t = rtso::MAP_MAINLAND; t < rtso::MAP_TYPE_COUNT; ++t)
		maps.push_back(String("random_") + rtso::map_type_info((uint8_t)t).id);
	return maps;
}

bool RtsoMain::load_map(String map_id) {
	uint32_t seed = (uint32_t)Time::get_singleton()->get_ticks_msec();
	if (sim) {
		sim->set_biome_override(-1); // a map picked in game keeps its own biome
		sim->set_nomad(false);
	}
	local_observer_ = false; // a map picked in game is played from slot 1
	return load_map_seeded(map_id, seed);
}

void RtsoMain::apply_map_player_overrides() {
	for (uint8_t p = 0; p < rtso::MAX_PLAYERS; ++p) {
		if (map_civ_[p] >= 0) sim->set_team_civ(p, (uint8_t)map_civ_[p]);
		if (map_has_res_[p]) sim->set_player_resources(p, map_res_[p][0], map_res_[p][1], map_res_[p][3], map_res_[p][2]);
		sim->set_player_pop_cap(p, map_player_pop_limit_[p]);
	}
	if (map_pop_limit_ > 0) sim->set_pop_cap(map_pop_limit_);
	if (map_has_teams_) {
		// A map with LockTeams sets the teams itself (0 A.D. gamesetup locks them).
		sim->set_setup_teams(map_team_);
		sim->apply_team_diplomacy();
	}
}

void RtsoMain::_on_civ_selected(int index) {
	set_civ(index);
}

void RtsoMain::_on_ai_diff_selected(int index) {
	if (!sim || !ai_diff_selector) return;
	sim->set_petra_difficulty((uint8_t)ai_diff_selector->get_item_id(index));
}

void RtsoMain::_on_ai_behavior_selected(int index) {
	if (!sim || !ai_behavior_selector) return;
	sim->set_petra_behavior((uint8_t)ai_behavior_selector->get_item_id(index));
}

String RtsoMain::player_display_name(uint8_t pid) const {
	if (pid >= rtso::MAX_PLAYERS) return pid == rtso::GAIA_PLAYER ? String("Gaia") : String("an observer");
	// 0 A.D. onFinalizeAttributes: a seated client's own name replaces the slot's.
	if (net_lockstep())
		for (const auto &e : net_host_.playerAssignment())
			if (e.player == pid && !e.name.empty()) return String::utf8(e.name.c_str());
	if (!player_names_[pid].is_empty()) return player_names_[pid];
	return "Player " + String::num_int64(pid + 1);
}

// messages.js onClientLeave / onClientJoin: the seat's client left the match
// and nobody (a rejoin under a new id) has taken the seat again.
bool RtsoMain::player_offline(uint8_t pid) const {
	if (!net_lockstep() || !net_game_started_ || pid >= rtso::MAX_PLAYERS || pid >= net_setup_.setup.slot_count) return false;
	const int c = net_setup_.slot_client[pid];
	if (c <= 0 || !turn_mgr_.client_left((uint32_t)c)) return false;
	for (const auto &e : net_host_.playerAssignment())
		if (e.player == pid && !turn_mgr_.client_left(e.client)) return false;
	return true;
}

void RtsoMain::apply_player_names(const Array &names) {
	for (int p = 0; p < rtso::MAX_PLAYERS; ++p)
		player_names_[p] = p < names.size() ? String(names[p]).strip_edges().left(48) : String();
}

void RtsoMain::sync_victory_selector() {
	if (!sim || !victory_selector) return;
	for (int i = 0; i < victory_selector->get_item_count(); ++i) {
		if (victory_selector->get_item_id(i) == (int)sim->victory_condition()) victory_selector->select(i);
	}
}

void RtsoMain::_on_victory_selected(int index) {
	if (!sim || !victory_selector) return;
	sim->set_victory_condition((uint8_t)victory_selector->get_item_id(index));
}

void RtsoMain::_on_voice_button_pressed() {
	use_english_voice_orders_ = !use_english_voice_orders_;
	if (voice_btn)
		voice_btn->set_text(use_english_voice_orders_ ? "Voice: EN" : "Voice: Civ");
}

bool RtsoMain::has_lobby_client() const {
	Node *ls = get_node_or_null(NodePath("/root/LobbySession"));
	return ls && (bool)ls->call("signed_in");
}

void RtsoMain::open_lobby_page() {
	if (!has_lobby_client()) return;
	close_open_dialogs();
	Ref<Script> lobby = ResourceLoader::get_singleton()->load("res://scenes/menu/lobby.gd");
	if (lobby.is_valid()) lobby->call("open_dialog", get_tree());
}

void RtsoMain::_on_lobby_button_pressed() {
	if (!lobby_panel) return;
	lobby_panel->set_visible(!lobby_panel->is_visible());
	if (lobby_panel->is_visible())
		refresh_lobby_rooms();
}

void RtsoMain::refresh_lobby_rooms() {
	if (!lobby_rooms) return;
	lobby_rooms->clear();
	lobby_room_cache_.clear();
	std::string body;
	if (!rtso::lobby_http_get(lobby_http_host_, lobby_http_port_, "/rooms", body)) {
		if (lobby_status)
			lobby_status->set_text("Lobby unreachable - start: python3 tools/lobby_server.py");
		return;
	}
	lobby_room_cache_ = rtso::parse_lobby_rooms(body);
	for (const auto &r : lobby_room_cache_) {
		// The lobby's list carries no address since /rooms/connect; older ones did.
		const String addr = r.host_ip.empty() ? String() : String(r.host_ip.c_str()) + ":" + String::num_int64(r.host_port) + "  ";
		String line = String(r.id.c_str()) + "  " + addr + String::num_int64(r.players) +
		              "/" + String::num_int64(r.max_players) + "  " + String(r.map_id.c_str());
		lobby_rooms->add_item(line);
	}
	if (lobby_status)
		lobby_status->set_text(String::num_int64((int64_t)lobby_room_cache_.size()) + " rooms");
}

// A joiner still in game setup changes its mind: tell the host it now wants
// to watch (or play), as its request at connect did.
void RtsoMain::_on_lobby_observe_toggled(bool on) {
	if (net_lockstep() && !net_host_.isHost() && !net_game_started_) turn_mgr_.request_rejoin(on);
}

void RtsoMain::_start_menu_net() {
	Node *ms = get_node_or_null(NodePath("/root/MatchSettings"));
	if (!ms) return;
	const String mode = ms->get("net_mode");
	if (mode != "host" && mode != "join") return;
	ms->set("net_mode", ""); // once: a later match starts offline unless the lobby says otherwise
	const String lobby = ms->get("net_lobby");
	if (lobby.contains(":")) {
		lobby_http_host_ = lobby.get_slice(":", 0).utf8().get_data();
		lobby_http_port_ = (uint16_t)lobby.get_slice(":", 1).to_int();
	}
	if (lobby_name) lobby_name->set_text(ms->get("net_player_name"));
	if (lobby_host && mode == "join") lobby_host->set_text(ms->get("net_ip"));
	// The lobby's ticket for a game joined from its list, once.
	net_host_.setLobbyTicket(mode == "join" ? String(ms->get("net_lobby_ticket")).utf8().get_data() : "");
	ms->set("net_lobby_ticket", "");
	if (lobby_port) lobby_port->set_text(String::num_int64((int64_t)ms->get("net_port")));
	if (lobby_password) lobby_password->set_text(ms->get("net_password"));
	if (lobby_observe) lobby_observe->set_pressed_no_signal((bool)ms->get("net_observe"));
	// A joiner sees the Connecting box, then the setup page; the developer
	// lobby panel stays behind its button.
	if (lobby_panel) lobby_panel->set_visible(mode == "host");
	if (lobby_btn) lobby_btn->set_visible(true);
	if (mode == "host") _on_lobby_host_pressed();
	else _on_lobby_join_pressed();
	// tools/playtest_godot_mp.py --firewall: each peer drops datagrams from
	// endpoints it never sent to, as a NAT does, so only a hole punch lets
	// the other side in.
	if (OS::get_singleton()->get_cmdline_user_args().has("net_firewall=1")) net_host_.set_simulated_firewall(true);
}

Dictionary RtsoMain::get_lobby_state() const {
	Dictionary d;
	const bool hosting = net_host_.isHost();
	int players = 0;
	if (hosting) {
		players = 1;
		for (const auto &p : net_host_.getPeers())
			if (p.id != 0 && p.connected && p.is_authenticated && !p.is_observer) ++players;
	}
	d["hosting"] = hosting;
	d["players"] = players;
	d["started"] = net_game_started_;
	// For tools/playtest_godot_mp.py: where the match is and whether it agrees.
	d["turn"] = (int64_t)turn_mgr_.current_turn();
	d["seat"] = (int)seat_player();
	d["oos"] = turn_mgr_.has_out_of_sync();
	d["oos_turn"] = (int64_t)turn_mgr_.oos_turn();
	// OutOfSyncNetwork's message: who, whether ours matches the host's, and the files.
	Array oos_players;
	for (const auto &n : turn_mgr_.oos_players()) oos_players.push_back(String::utf8(n.c_str()));
	d["oos_players"] = oos_players;
	d["oos_identical"] = turn_mgr_.oos_matches_host();
	d["oos_dump"] = String::utf8(turn_mgr_.oos_dump_path().c_str());
	d["oos_replay"] = replay_dir_.is_empty() ? String()
			: ProjectSettings::get_singleton()->globalize_path(replay_dir_ + String("commands.rts0rec"));
	bool ai = false;
	for (uint8_t p = 0; sim && p < rtso::MAX_PLAYERS; ++p) ai = ai || (sim->player(p).active && sim->player(p).is_ai);
	d["oos_ai"] = ai;
	d["disconnected"] = net_disconnected_;
	d["setup_page"] = net_setup_overlay_ != nullptr;
	d["disconnect_reason"] = net_host_.disconnectReason();
	d["net_status"] = net_status_overlay_ && net_status_overlay_->is_visible() ? net_status_label_->get_text() : String();
	d["net_status_title"] = net_status_title_ && net_status_title_->is_visible() ? net_status_title_->get_text() : String();
	{
		String buttons;
		for (Button *b : {net_status_exit_, net_status_faq_})
			if (b && b->is_visible()) buttons += (buttons.is_empty() ? "" : ",") + b->get_text();
		d["net_status_buttons"] = buttons;
	}
	d["net_loading"] = net_loading_clients_ && net_loading_clients_->is_visible() ? net_loading_clients_->get_parsed_text() : String();
	d["chat"] = chat_window_.log_text();
	d["net_warnings"] = net_warnings_label_ && net_warnings_label_->is_visible() ? net_warnings_label_->get_parsed_text() : String();
	if (net_warnings_label_)
		if (CanvasLayer *l = Object::cast_to<CanvasLayer>(net_warnings_label_->get_parent())) d["net_warnings_layer"] = (int)l->get_layer();
	return d;
}

Dictionary RtsoMain::get_net_nat_state() const {
	Dictionary d;
	d["stun"] = net_host_.stunState();
	d["public_ip"] = String(net_host_.publicIp().c_str());
	d["public_port"] = (int)net_host_.publicPort();
	d["local_ip"] = String(net_host_.lanIp().c_str());
	d["local_port"] = (int)net_host_.localPort();
	return d;
}

void RtsoMain::net_query_stun(const String &server, int port) {
	if (port <= 0 || port > 65535 || server.is_empty()) return;
	net_host_.queryStun(server.utf8().get_data(), (uint16_t)port);
}

void RtsoMain::net_hole_punch(const String &ip, int port) {
	if (port <= 0 || port > 65535) return;
	net_host_.holePunch(ip.utf8().get_data(), (uint16_t)port);
}

void RtsoMain::net_lobby_auth_result(int peer, const String &ticket, const String &name) {
	net_host_.finishLobbyAuth((uint32_t)peer, ticket.utf8().get_data(), name.utf8().get_data());
}

void RtsoMain::net_retarget_host(const String &ip, int port) {
	if (port <= 0 || port > 65535 || net_game_started_) return;
	net_host_.retargetHost(ip.utf8().get_data(), (uint16_t)port);
}

int64_t RtsoMain::get_turn_hash(int64_t turn) const {
	const auto &history = turn_mgr_.turn_history();
	for (auto it = history.rbegin(); it != history.rend(); ++it)
		if ((int64_t)it->turn_index == turn) return (int64_t)it->sim_state_hash;
	return 0;
}

// 0 A.D. gamesetup player assignment (PlayerAssignmentsController.js): the host
// gives each client a slot, or makes it an observer; a client given an AI slot
// replaces the AI, and one given a slot another client plays swaps with it.
// Unassigned clients take the free human slots in order. Shown only on the
// host until Start.
void RtsoMain::refresh_lobby_seats() {
	if (!lobby_seats) return;
	struct Client { uint32_t id; std::string name; bool watches; };
	std::vector<Client> clients;
	struct Slot { char type; String name; };
	std::vector<Slot> slots;
	if (net_host_.isHost() && !net_game_started_) {
		clients.push_back({0, net_host_.localPlayerName(), lobby_observe && lobby_observe->is_pressed()});
		for (const auto &p : net_host_.getPeers())
			if (p.connected && p.is_authenticated && !p.kick_disconnect_ms && p.id != 0 && p.id < rtso::NET_SETUP_CLIENTS)
				clients.push_back({p.id, p.player_name, p.is_observer});
		if (Node *ms = get_node_or_null(NodePath("/root/MatchSettings"))) {
			Array types = ms->get("slot_types");
			Array names = ms->get("slot_names");
			for (int i = 0; i < 8 && i < types.size(); ++i) {
				const int ty = (int)types[i];
				slots.push_back({ty == 0 ? 'h' : ty == 1 ? 'a' : 'x', i < names.size() ? String(names[i]) : String()});
			}
		}
	}
	if (clients.size() < 2) clients.clear(); // nobody to assign but the host
	for (auto it = seat_choice_.begin(); it != seat_choice_.end();) {
		bool here = false;
		for (const auto &c : clients) here |= c.id == it->first;
		it = here ? std::next(it) : seat_choice_.erase(it);
	}
	for (auto it = seat_seen_.begin(); it != seat_seen_.end();) {
		bool here = false;
		for (const auto &c : clients) here |= c.id == *it;
		it = here ? std::next(it) : seat_seen_.erase(it);
	}
	uint8_t eff[rtso::NET_SETUP_CLIENTS];
	lobby_effective_seats(eff);
	// 0 A.D. onClientJoin: gui.gamesetup.assignplayers decides whether a joiner
	// gets a slot (everyone, only the host's buddies, or nobody until the host
	// assigns one), and a name seen before gets its last slot back when free.
	for (const auto &c : clients) {
		if (c.id == 0 || !seat_seen_.insert(c.id).second || seat_choice_.count(c.id)) continue;
		const std::string opt = rtso::user_config::get("gui.gamesetup.assignplayers");
		bool buddy = false;
		{
			const std::string list = "," + rtso::user_config::get("lobby.buddies") + ",";
			buddy = !c.name.empty() && list.find("," + c.name + ",") != std::string::npos;
		}
		if (opt == "disabled" || (opt == "buddies" && !buddy)) { seat_choice_[c.id] = SEAT_OBSERVER; continue; }
		if (c.watches) continue;
		auto held = [&](int slot) {
			bool h = false;
			for (const auto &o : clients) h |= o.id != c.id && eff[o.id] == slot;
			return h;
		};
		auto last = seat_last_.find(c.name);
		if (last != seat_last_.end() && last->second < slots.size() && slots[last->second].type != 'x' && !held(last->second)) {
			seat_choice_[c.id] = last->second;
			continue;
		}
		// findAssignmentSlot: else the slot the saved game has for that name.
		const String saved = "@" + String::utf8(c.name.c_str()).to_lower();
		for (int i = 0; net_saved_setup() && i < (int)net_saved_seats_.size() && i < (int)slots.size(); ++i)
			if (net_saved_seats_[i] == saved && slots[i].type == 'h' && !held(i)) { seat_choice_[c.id] = i; break; }
	}
	lobby_effective_seats(eff);
	for (const auto &c : clients) seat_last_[c.name] = eff[c.id];
	std::string key;
	for (const auto &c : clients) {
		auto ch = seat_choice_.find(c.id);
		key += std::to_string(c.id) + ":" + c.name + ":" + (c.watches ? "o" : "p") + ":" +
				std::to_string(ch == seat_choice_.end() ? -1 : ch->second) + ":" + std::to_string(eff[c.id]) + ";";
	}
	for (const auto &s : slots) key += std::string(1, s.type) + s.name.utf8().get_data() + ";";
	if (key == lobby_seats_key_) return;
	lobby_seats_key_ = key;
	for (int i = lobby_seats->get_child_count() - 1; i >= 0; --i) {
		Node *n = lobby_seats->get_child(i);
		lobby_seats->remove_child(n);
		n->queue_free();
	}
	if (clients.empty()) return;
	Label *title = memnew(Label);
	title->set_text("Player assignment");
	lobby_seats->add_child(title);
	for (const auto &c : clients) {
		HBoxContainer *row = memnew(HBoxContainer);
		Label *who = memnew(Label);
		who->set_text(c.name.empty() ? String("Player") : String::utf8(c.name.c_str()));
		who->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		row->add_child(who);
		OptionButton *pick = memnew(OptionButton);
		pick->add_item(eff[c.id] < 8 ? "Next free slot (Player " + String::num_int64(eff[c.id] + 1) + ")" : String("Next free slot"), SEAT_AUTO);
		pick->add_item("Observer", SEAT_OBSERVER);
		for (int i = 0; i < (int)slots.size(); ++i) {
			if (slots[i].type == 'x') continue;
			String label = "Player " + String::num_int64(i + 1);
			if (slots[i].type == 'a') label += String(" (AI") + (slots[i].name.is_empty() ? String() : ": " + slots[i].name) + String(")");
			pick->add_item(label, i);
		}
		auto ch = seat_choice_.find(c.id);
		const int chosen = ch != seat_choice_.end() ? ch->second : (c.watches ? SEAT_OBSERVER : SEAT_AUTO);
		const int idx = pick->get_item_index(chosen);
		pick->select(idx >= 0 ? idx : 0);
		pick->set_meta("client", (int)c.id);
		pick->connect("item_selected", Callable(this, "_on_lobby_seat_selected").bind((int)c.id));
		row->add_child(pick);
		lobby_seats->add_child(row);
	}
}

void RtsoMain::_on_lobby_seat_selected(int index, int client) {
	OptionButton *pick = nullptr;
	for (int i = 0; lobby_seats && i < lobby_seats->get_child_count() && !pick; ++i)
		if (Node *row = lobby_seats->get_child(i))
			for (int j = 0; j < row->get_child_count(); ++j)
				if (OptionButton *ob = Object::cast_to<OptionButton>(row->get_child(j)))
					if ((int)ob->get_meta("client", -1) == client) pick = ob;
	if (!pick) return;
	assign_lobby_seat(client, pick->get_item_id(index));
}

void RtsoMain::assign_lobby_seat(int client, int choice) {
	if (client < 0 || client >= (int)rtso::NET_SETUP_CLIENTS) return;
	// 0 A.D. assignPlayer: a slot another client plays is swapped with this
	// client's own slot, or leaves that client unassigned (an observer).
	uint8_t eff[rtso::NET_SETUP_CLIENTS];
	lobby_effective_seats(eff);
	if (choice < 8)
		for (uint32_t other = 0; other < rtso::NET_SETUP_CLIENTS; ++other) {
			if (other == (uint32_t)client || eff[other] != choice) continue;
			bool connected = other == 0;
			for (const auto &p : net_host_.getPeers()) connected |= p.id == other && p.connected;
			if (connected) seat_choice_[other] = eff[client] < 8 ? (int)eff[client] : SEAT_OBSERVER;
		}
	seat_choice_[(uint32_t)client] = choice;
	lobby_seats_key_.clear();
}

// Start: the host's player assignment into NMT_GAME_START (seats= and obs=).
// 0 A.D. game setup assigns observers no player: the host when Observe is
// ticked, a joiner that asked to watch (its rejoin request at connect), and
// whoever the host's player assignment made an observer; a client given an AI
// slot replaces the AI, and if too few human slots are left for the joiners
// the first AI slots become human ones. `commit` (Start) also tells the net
// layer who watches.
void RtsoMain::prepare_lobby_seats(rtso::GameSetupParams &setup, bool commit) {
	const bool host_watches = lobby_observe && lobby_observe->is_pressed();
	setup.observers = host_watches ? 1 : 0;
	for (const auto &p : net_host_.getPeers())
		if (p.connected && p.id != 0 && p.id < rtso::NET_SETUP_CLIENTS && p.is_observer) setup.observers |= (1ull << p.id);
	for (const auto &e : seat_choice_) {
		if (e.first >= rtso::NET_SETUP_CLIENTS) continue;
		const uint64_t bit = 1ull << e.first;
		if (e.second == SEAT_OBSERVER) { setup.observers |= bit; continue; }
		setup.observers &= ~bit;
		if (e.second < 0 || e.second >= setup.slot_count || setup.slot_types[e.second] == 'x') continue;
		// PlayerAssignment.rebuildList: a saved game's AI can't be replaced.
		if (net_saved_setup() && setup.slot_types[e.second] != 'h') continue;
		setup.seats[e.first] = (uint8_t)e.second;
		setup.slot_types[e.second] = 'h';
	}
	if (net_saved_setup()) {
		// Only the save's human seats take players; the rest watch.
		for (uint8_t i = 0; i < setup.slot_count; ++i)
			if (setup.slot_types[i] == 'h' && (i >= net_saved_seats_.size() || net_saved_seats_[i].is_empty())) setup.slot_types[i] = 'x';
		if (commit)
			for (const auto &p : net_host_.getPeers())
				if (p.connected && p.id != 0 && p.id < rtso::NET_SETUP_CLIENTS) net_host_.setPeerObserver(p.id, (setup.observers >> p.id) & 1);
		return;
	}
	int need = (setup.observers & 1) ? 0 : 1;
	for (const auto &p : net_host_.getPeers()) {
		if (!p.connected || p.id == 0) continue;
		const bool watches = p.id < rtso::NET_SETUP_CLIENTS && ((setup.observers >> p.id) & 1);
		if (commit && p.id < rtso::NET_SETUP_CLIENTS) net_host_.setPeerObserver(p.id, watches);
		if (!watches) ++need;
	}
	for (uint8_t i = 0; i < setup.slot_count; ++i)
		if (setup.slot_types[i] == 'h') --need;
	for (uint8_t i = 0; i < setup.slot_count && need > 0; ++i)
		if (setup.slot_types[i] == 'a') { setup.slot_types[i] = 'h'; --need; }
	// Seat the connected clients only, and name each one's slot in seats=, so a
	// client id freed by a leaver takes no slot from a later joiner.
	uint64_t present = 1;
	for (const auto &p : net_host_.getPeers())
		if (p.connected && p.id != 0 && p.id < rtso::NET_SETUP_CLIENTS) present |= 1ull << p.id;
	uint8_t eff[rtso::NET_SETUP_CLIENTS];
	rtso::TurnManager::seat_clients(setup, eff, present);
	for (uint32_t c = 0; c < rtso::NET_SETUP_CLIENTS; ++c)
		if (eff[c] < 8) setup.seats[c] = eff[c];
}

// The slot each client would play if the host pressed Start now (0xFF: watches).
void RtsoMain::lobby_effective_seats(uint8_t out[rtso::NET_SETUP_CLIENTS]) {
	rtso::GameSetupParams setup;
	setup.slot_count = 0;
	if (Node *ms = get_node_or_null(NodePath("/root/MatchSettings"))) {
		Array types = ms->get("slot_types");
		for (int i = 0; i < 8 && i < types.size(); ++i) {
			const int ty = (int)types[i];
			setup.slot_types[i] = ty == 0 ? 'h' : ty == 1 ? 'a' : 'x';
			setup.slot_count++;
		}
	}
	prepare_lobby_seats(setup, false);
	rtso::TurnManager::seat_clients(setup, out);
}

// Host, before Start: send the setup to the clients when it changed (0 A.D.
// gamesetup's NMT_GAME_SETUP on every change). A change to a setting or a
// seat clears every ready flag, as 0.28's ReadyController does, and bumps the
// settings version a client's ready must name to count.
void RtsoMain::host_net_setup_tick(bool force) {
	if (!net_host_.isHost() || !sim) return;
	if (net_game_started_) {
		// A client that connects mid-match (a rejoin) is told the setup too,
		// for the game speed.
		uint64_t now = 0;
		for (const auto &p : net_host_.getPeers())
			if (p.id > 0 && p.id < rtso::NET_SETUP_CLIENTS && p.connected && p.is_authenticated && !p.kick_disconnect_ms) {
				now |= (1ull << p.id);
				if (!((net_setup_.clients >> p.id) & 1)) net_host_.sendGameSetup(rtso::format_net_setup(net_setup_), p.id);
			}
		net_setup_.clients = now | 1;
		return;
	}
	for (const auto &r : net_host_.takeSetupRequests())
		if (const char *why = apply_net_setup_request(r.client, r.request)) net_host_.sendSetupRefusal(r.client, why);
	rtso::NetSetupState n;
	match_settings_setup(n.setup);
	if (net_saved_setup()) n.setup.saved_game = 1; // the clients lock their page too
	// MapSelection.js Random: resolved only at Start (launchGame), so the
	// clients are told "random" and see which map only once it loads.
	if (Node *ms = get_node_or_null(NodePath("/root/MatchSettings")))
		if ((bool)ms->get("net_map_random")) n.setup.map_id = "random";
	uint8_t eff[rtso::NET_SETUP_CLIENTS];
	{
		rtso::GameSetupParams seated = n.setup;
		prepare_lobby_seats(seated, false);
		rtso::TurnManager::seat_clients(seated, eff);
	}
	n.clients = 1;
	n.names[0] = net_host_.localPlayerName();
	for (const auto &p : net_host_.getPeers())
		if (p.id > 0 && p.id < rtso::NET_SETUP_CLIENTS && p.connected && p.is_authenticated && !p.kick_disconnect_ms) {
			n.clients |= (1ull << p.id);
			n.names[p.id] = p.player_name;
		}
	for (uint32_t c = 0; c < rtso::NET_SETUP_CLIENTS; ++c) {
		if (!((n.clients >> c) & 1)) continue;
		if (eff[c] < 8) n.slot_client[eff[c]] = (int8_t)c;
		else n.observers |= (1ull << c);
	}
	std::string key = rtso::format_game_start_full(n.setup) + ";";
	// ReadyController.js onClientJoin / onClientLeave / onPlayerAssignmentsChange:
	// readiness resets when the settings or a seat's client change, not when an
	// observer comes or goes.
	for (int i = 0; i < 8; ++i) key += (char)('0' + n.slot_client[i] + 1);
	n.settings_version = net_setup_.settings_version;
	if (key != net_setup_key_) {
		net_setup_key_ = key;
		++n.settings_version;
		net_host_.clearAllReady();
	}
	for (const auto &p : net_host_.getPeers())
		if (p.id > 0 && p.id < rtso::NET_SETUP_CLIENTS && ((n.clients >> p.id) & 1)) {
			if (p.ready_status == 2) n.stay_ready |= (1ull << p.id);
			else if (p.ready_status == 1 && p.ready_version == n.settings_version) n.ready |= (1ull << p.id);
		}
	const std::string body = rtso::format_net_setup(n);
	n.seq = net_setup_.seq;
	const bool changed = body != net_setup_sent_;
	if (force || changed) {
		net_setup_sent_ = body;
		++n.seq;
		net_host_.sendGameSetup(rtso::format_net_setup(n));
	}
	notify_setup_joins(net_setup_.clients, n.clients);
	post_setup_chat(net_setup_, n);
	net_setup_ = n;
	if (force || changed) emit_signal("net_setup_changed");
}

// ClientKicked.js and ChatMessageFormatNetwork kicked/banned, in the setup and the match.
void RtsoMain::post_kick_chat() {
	const auto &kicks = net_host_.kicks();
	for (; kicks_chatted_ < kicks.size(); ++kicks_chatted_) {
		const auto &k = kicks[kicks_chatted_];
		const String name = String::utf8(k.name.empty() ? k.guid.c_str() : k.name.c_str()).replace("[", "[lb]");
		chat_window_.append_kind(sprintf0(k.ban ? tr("%(username)s has been banned") : tr("%(username)s has been kicked"), {{"username", name}}) + "\n", CHAT_FILTER_NETWORK);
	}
}

// colorizePlayernameByGUID in the setup: the client's name in its slot's colour,
// white while it has no slot.
String RtsoMain::setup_client_name(const rtso::NetSetupState &n, uint32_t client) const {
	String name = client < rtso::NET_SETUP_CLIENTS ? String::utf8(n.names[client].c_str()) : String();
	if (name.is_empty()) name = String::utf8(rtso::peer_guid(client).c_str());
	const int slot = n.slot_of(client);
	const uint32_t rgb = slot >= 0 && n.setup.colors[slot] < rtso::PLAYER_COLOR_COUNT
			? rtso::PLAYER_COLORS[n.setup.colors[slot]] & 0xFFFFFFu : 0xFFFFFFu;
	return "[color=#" + String::num_int64(rgb, 16).lpad(6, "0") + "]" + name.replace("[", "[lb]") + "[/color]";
}

// 0.28 gamesetup ChatMessages: "has joined" / "has left" (ClientConnection.js),
// "* ... is ready!" / "* ... is not ready." for a seated client (ClientReady.js;
// staying ready says nothing). A ready the host's settings change took back
// sends no ready message in 0.28, so it posts no "not ready" line here either.
void RtsoMain::post_setup_chat(const rtso::NetSetupState &before, const rtso::NetSetupState &after) {
	if (before.clients) {
		for (uint32_t c = 1; c < rtso::NET_SETUP_CLIENTS; ++c) {
			const bool was = (before.clients >> c) & 1, is = (after.clients >> c) & 1;
			if (!was && is)
				chat_window_.append_line(sprintf0(tr("%(username)s has joined"),
						{{"username", String::utf8(after.names[c].c_str()).replace("[", "[lb]")}}) + "\n");
			else if (was && !is)
				chat_window_.append_line(sprintf0(tr("%(username)s has left"), {{"username", setup_client_name(before, c)}}) + "\n");
		}
		const bool same_settings = before.settings_version == after.settings_version;
		for (uint32_t c = 1; c < rtso::NET_SETUP_CLIENTS; ++c) {
			if (!((before.clients >> c) & (after.clients >> c) & 1) || after.slot_of(c) < 0) continue;
			const bool stay = (after.stay_ready >> c) & 1;
			const bool was = (before.ready >> c) & 1, is = (after.ready >> c) & 1;
			if (!stay && !was && is)
				chat_window_.append_line("[b]" + sprintf0(tr("* %(username)s is ready!"), {{"username", setup_client_name(after, c)}}) + "[/b]\n");
			else if (!stay && was && !is && same_settings)
				chat_window_.append_line("[b]" + sprintf0(tr("* %(username)s is not ready."), {{"username", setup_client_name(after, c)}}) + "[/b]\n");
		}
	}
	post_kick_chat();
}

Dictionary RtsoMain::get_setup_chat() const {
	Dictionary d;
	d["rev"] = (int64_t)chat_window_.history_rev();
	d["text"] = chat_window_.history();
	return d;
}

// The setup page's chat input: the chat window's send, so /kick, /ban, /list
// and /clear (executeNetworkCommand) run before anything is sent.
void RtsoMain::send_setup_chat(const String &text) {
	chat_window_.send_setup(text);
}

bool RtsoMain::chat_setup_verbatim() { return is_net_pregame(); }

// 0.28 sound.notify.gamesetup.join (off by default): a chime when a client joins
// the game setup; `before` 0 is our first look at it, not a join.
void RtsoMain::notify_setup_joins(uint64_t before, uint64_t after) {
	if (before && (after & ~before) && rtso::user_config::get_bool("sound.notify.gamesetup.join"))
		play_alarm("res://assets/audio/interface/ui/gamesetup_join.ogg");
}

// Host: a client asks to change its own slot ("civ=3", "team=2", "color=5"),
// as the host's own Game Setup could. Returns why it was refused, or nullptr.
const char *RtsoMain::apply_net_setup_request(uint32_t client, const std::string &request) {
	Node *ms = get_node_or_null(NodePath("/root/MatchSettings"));
	const int slot = net_setup_.slot_of(client);
	const size_t eq = request.find('=');
	if (!ms || eq == std::string::npos) return "Unknown request";
	if (slot < 0) return "You have no player slot";
	if (net_saved_setup()) return "The controller loaded a saved game";
	// 0.28 NetServer OnGameSetup: "Only the controller is allowed to send game
	// setup updates"; a client's civ, team and colour are the host's to set.
	if (client != 0) return "Only the controller can change the game setup";
	if ((bool)ms->get("map_locks_players")) return "This map sets every player's civilization, team and color";
	const std::string key = request.substr(0, eq);
	const String sv = String(request.substr(eq + 1).c_str());
	const char *field = key == "civ" ? "slot_civs" : key == "team" ? "slot_teams" : key == "color" ? "slot_colors" : nullptr;
	if (!field || !sv.is_valid_int()) return "Unknown request";
	const int value = (int)sv.to_int();
	if (key == "civ" && value != -1 && !rtso::is_playable_civ(value)) return "Unknown civilization";
	if (key == "team" && (value < 0 || value > 4)) return "Unknown team";
	Array arr = ms->get(field);
	if (slot >= arr.size()) return "You have no player slot";
	if (key == "color") {
		if (value < 0 || value >= rtso::PLAYER_COLOR_COUNT) return "Unknown color";
		// Two players never share a colour.
		Array types = ms->get("slot_types");
		for (int j = 0; j < arr.size(); ++j)
			if (j != slot && (int)arr[j] == value && (j >= types.size() || (int)types[j] != 2))
				return "Another player has that color";
	}
	arr[slot] = value;
	ms->set(field, arr);
	return nullptr;
}

// Client, before Start: take the host's newest setup.
void RtsoMain::client_net_setup_tick() {
	if (net_host_.isHost() || net_game_started_) return;
	for (const auto &why : net_host_.takeSetupRefusals())
		emit_signal("net_setup_refused", String::utf8(why.c_str()));
	rtso::NetSetupState best;
	bool have = false;
	for (const auto &attrs : net_host_.takeGameSetups()) {
		rtso::NetSetupState n;
		if (rtso::parse_net_setup(attrs, n) && n.seq > (have ? best.seq : net_setup_.seq)) {
			best = std::move(n);
			have = true;
		}
	}
	if (!have) return;
	notify_setup_joins(net_setup_.clients, best.clients);
	post_setup_chat(net_setup_, best);
	net_setup_ = std::move(best);
	// 0.28 ReadyController: a change to the settings takes our ready back
	// (GameSettingsChanged.js tells us so).
	if (net_ready_ == 1 && net_ready_version_ != net_setup_.settings_version) {
		net_ready_ = 0;
		chat_window_.append_line(tr("Game settings have been changed") + "\n");
	}
	store_net_setup_in_match_settings(net_setup_.setup);
	emit_signal("net_setup_changed");
}

bool RtsoMain::is_net_pregame() const {
	return net_lockstep() && !net_game_started_;
}

Dictionary RtsoMain::get_net_setup_state() const {
	Dictionary d;
	if (!net_lockstep()) return d; // not a networked match
	const bool host = net_host_.isHost();
	const uint32_t me = host ? 0 : (net_host_.getLocalPeerId() ? net_host_.getLocalPeerId() : 1);
	d["controller"] = host;
	d["my_client"] = (int)me;
	d["my_slot"] = net_setup_.slot_of(me);
	Array slots;
	for (uint8_t i = 0; i < net_setup_.setup.slot_count && i < 8; ++i) {
		Dictionary e;
		const int c = net_setup_.slot_client[i];
		e["client"] = c;
		e["name"] = c >= 0 ? String::utf8(net_setup_.names[c].c_str()) : String();
		// 0.28 ReadyController: 0 not ready, 1 ready, 2 stay ready (the host is).
		e["ready"] = c == 0 ? 2 : c < 0 ? 0 : ((net_setup_.stay_ready >> c) & 1) ? 2 : ((net_setup_.ready >> c) & 1) ? 1 : 0;
		// LobbyGamelistReporter: in the match, who won or was defeated, and
		// which seated client dropped out (the slot is the player id).
		if (net_game_started_ && sim) {
			e["state"] = sim->is_victory_for(i) ? "won" : sim->is_defeated(i) ? "defeated" : "";
			e["offline"] = host && c > 0 && turn_mgr_.client_left((uint32_t)c);
		}
		slots.push_back(e);
	}
	d["slots"] = slots;
	Array observers;
	for (uint32_t c = 0; c < rtso::NET_SETUP_CLIENTS; ++c)
		if (((net_setup_.clients >> c) & 1) && ((net_setup_.observers >> c) & 1)) {
			Dictionary e;
			e["client"] = c;
			e["name"] = String::utf8(net_setup_.names[c].c_str());
			observers.push_back(e);
		}
	d["observers"] = observers;
	d["all_ready"] = net_setup_.all_ready();
	d["can_start"] = host && !net_game_started_ && net_setup_.all_ready();
	d["ready"] = host ? 2 : (int)net_ready_;
	// SavedGameLabel and GameSettingControl EnabledWhenSavedGame.
	d["saved_game"] = net_setup_.setup.saved_game != 0;
	d["locked"] = net_setup_.setup.lock_players != 0 || net_setup_.setup.saved_game != 0;
	d["version"] = (int64_t)net_setup_.seq;
	d["settings_version"] = (int64_t)net_setup_.settings_version;
	return d;
}

// The networked Game Setup (scenes/menu/gamesetup.gd with net_main set) over
// the match until Start, in place of the lobby panel's seat rows and Start.
// Shown once the host is up or has let us in; gone at Start, on a rejoin and
// when the connection drops.
void RtsoMain::update_net_setup_overlay() {
	const bool want = is_net_pregame() && !net_disconnected_ && !turn_mgr_.rejoin_syncing() &&
			(net_host_.isHost() || net_host_.authResult() == rtso::ARC_OK);
	if (want && !net_setup_overlay_ && !net_setup_overlay_unsupported_) {
		Ref<PackedScene> scene = ResourceLoader::get_singleton()->load("res://scenes/menu/gamesetup.tscn");
		Node *gs = scene.is_valid() ? scene->instantiate() : nullptr;
		bool networked = false; // a gamesetup.gd without a networked mode is not shown
		if (gs) {
			const TypedArray<Dictionary> props = gs->get_property_list();
			for (int64_t i = 0; i < props.size() && !networked; ++i)
				networked = String(((Dictionary)props[i]).get("name", "")) == "net_main";
		}
		if (!networked) {
			if (gs) memdelete(gs);
			net_setup_overlay_unsupported_ = true;
		} else {
			gs->set("net_main", this);
			net_setup_layer_ = memnew(CanvasLayer);
			net_setup_layer_->set_layer(20);
			add_child(net_setup_layer_);
			net_setup_layer_->add_child(gs);
			net_setup_overlay_ = gs;
		}
	} else if (!want && net_setup_overlay_) {
		net_setup_layer_->queue_free();
		net_setup_layer_ = nullptr;
		net_setup_overlay_ = nullptr;
	}
	const bool shown = net_setup_overlay_ != nullptr;
	if (lobby_seats) lobby_seats->set_visible(!shown);
	if (lobby_start_btn_) lobby_start_btn_->set_visible(!shown);
}

void RtsoMain::update_net_connect_overlay() {
	const bool client = net_host_.getRole() == rtso::NetRole::Client;
	const bool want = client && !net_game_started_ && !net_disconnected_ && !net_host_.connectionLost() &&
			(net_host_.authResult() < 0 || turn_mgr_.rejoin_syncing());
	if (want && !net_connect_overlay_) {
		Ref<Script> script = ResourceLoader::get_singleton()->load("res://scenes/menu/net_connecting.gd");
		if (script.is_null()) return;
		Control *page = memnew(Control);
		page->set_script(script);
		page->set("net_main", this);
		net_connect_layer_ = memnew(CanvasLayer);
		net_connect_layer_->set_layer(21); // over the setup page (20)
		add_child(net_connect_layer_);
		net_connect_layer_->add_child(page);
		net_connect_overlay_ = page;
	} else if (!want && net_connect_overlay_) {
		net_connect_layer_->queue_free();
		net_connect_layer_ = nullptr;
		net_connect_overlay_ = nullptr;
	}
	if (!net_connect_overlay_) return;
	// gamesetup_mp.js: "Connecting to server..." until the host's handshake,
	// "Registering with server..." until it lets us in; a rejoin then loads
	// the running game (handleAuthenticated).
	const uint8_t why = turn_mgr_.rejoin_reject_reason();
	const char *text = why == 6 ? "This game does not let observers join after it started."
			: why == 7 ? "This game is full: it has as many observers as it allows."
			: why == 3 || why == 4 || why == 5 ? "Could not load the host's game."
			: turn_mgr_.rejoin_syncing() ? "Game has already started, rejoining..."
			: net_host_.hostSoftwareVersion().empty() ? "Connecting to server..."
			: "Registering with server...";
	if (String(net_connect_overlay_->get("text")) != text) net_connect_overlay_->set("text", String(text));
}

// Game Setup's Back in a networked game: leave it for the lobby.
void RtsoMain::leave_net_game() {
	net_host_.stop();
	if (Node *ms = get_node_or_null(NodePath("/root/MatchSettings"))) ms->call("go", "res://scenes/menu/lobby.tscn");
}

void RtsoMain::push_net_setup() {
	host_net_setup_tick(true);
}

void RtsoMain::assign_net_client(int client, int slot) {
	if (!net_host_.isHost() || net_game_started_) return;
	assign_lobby_seat(client, slot < 0 ? SEAT_OBSERVER : slot);
	host_net_setup_tick(false);
}

void RtsoMain::request_net_player_setting(const String &key, int value) {
	if (!is_net_pregame()) return;
	const std::string req = std::string(key.utf8().get_data()) + "=" + std::to_string(value);
	if (net_host_.isHost()) {
		if (const char *why = apply_net_setup_request(0, req)) emit_signal("net_setup_refused", String(why));
		host_net_setup_tick(false);
	} else {
		net_host_.sendSetupRequest(req);
	}
}

void RtsoMain::set_net_ready(int status) {
	if (net_host_.isHost() || !is_net_pregame() || status < 0 || status > 2) return;
	net_ready_ = (uint8_t)status;
	net_ready_version_ = net_setup_.settings_version;
	net_host_.sendReady(net_ready_, net_ready_version_);
}

void RtsoMain::_on_lobby_refresh_pressed() {
	refresh_lobby_rooms();
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
	if (rtso::net_cmd_takes_push_front(cmd_type) && !(param & rtso::NET_PARAM_QUEUED) &&
			rtso::hotkeys::held("session.pushorderfront"))
		c.param |= rtso::NET_PARAM_PUSH_FRONT_ORDER;
	if (spectating()) return; // observers and defeated players order nothing
	if (ids && (cmd_type == (uint8_t)rtso::NetCommandType::NET_CMD_MOVE || cmd_type == (uint8_t)rtso::NetCommandType::NET_CMD_PATROL))
		apply_default_formation(ids, n);
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
			leave.param = rtso::FORMATION_NONE;
			turn_mgr_.post_local_command(leave);
		}
	}
	turn_mgr_.post_local_command(c);
}

// Before Host or Join: what the handshake names (the client sends it at once).
static void apply_net_host_options(rtso::NetHost &net) {
	net.setDuplicatePlayerNames(rtso::user_config::get_bool("network.duplicateplayernames"));
	// Builds whose simulation state differs would fall out of sync: the
	// handshake names the save format too, so the host refuses them up front.
	net.setSoftwareVersion(std::string(rtso::NET_SOFTWARE_VERSION) + " save " +
			std::to_string(rtso::Simulation::SAVE_VERSION));
}

// 0 A.D. 0.28 [network]: the host's late-observer policy, observer limit and
// observer lag (NetServer.cpp, NetServerTurnManager.cpp), and a watcher's
// autocatchup (NetClientTurnManager). Read again at every Host and Join.
void RtsoMain::apply_net_options() {
	std::set<std::string> buddies;
	std::istringstream list(rtso::user_config::get("lobby.buddies"));
	for (std::string name; std::getline(list, name, ',');) {
		for (char &c : name) c = (char)std::tolower((unsigned char)c);
		if (!name.empty()) buddies.insert(name);
	}
	const std::string late = rtso::user_config::get("network.lateobservers");
	turn_mgr_.set_late_observers(late == "buddies" ? rtso::TurnManager::LATE_OBSERVERS_BUDDIES
			: late == "disabled" ? rtso::TurnManager::LATE_OBSERVERS_DISABLED : rtso::TurnManager::LATE_OBSERVERS_EVERYONE, buddies);
	turn_mgr_.set_observer_limit(String(rtso::user_config::get("network.observerlimit").c_str()).to_int());
	turn_mgr_.set_observer_max_lag(String(rtso::user_config::get("network.observermaxlag").c_str()).to_int());
	turn_mgr_.set_auto_catch_up(rtso::user_config::get_bool("network.autocatchup"));
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

void RtsoMain::_on_lobby_host_pressed() {
	rtso::NetConfig cfg;
	cfg.port = (uint16_t)lobby_port->get_text().to_int();
	if (cfg.port == 0) cfg.port = rtso::PS_DEFAULT_PORT;
	net_host_.setLocalPlayerName(lobby_name->get_text().utf8().get_data());
	net_host_.setPassword(lobby_password ? lobby_password->get_text().utf8().get_data() : "");
	kicks_shown_ = 0;
	apply_net_host_options(net_host_);
	// 0 A.D. StartNetworkHost: a host signed in to the lobby has it vouch for
	// every joiner's name (CNetServer useLobbyAuth = HasXmppClient).
	Node *lobby_session = get_node_or_null(NodePath("/root/LobbySession"));
	if (lobby_session && bool(lobby_session->call("signed_in")))
		net_host_.setLobbyAuth([this](uint32_t peer, const std::string &ticket) {
			Node *ls = get_node_or_null(NodePath("/root/LobbySession"));
			if (ls) ls->call("verify_join", (int64_t)peer, String::utf8(ticket.c_str()));
			else net_host_.finishLobbyAuth(peer, ticket, "");
		});
	else
		net_host_.setLobbyAuth(nullptr);
	if (!net_host_.startHost(cfg)) {
		if (lobby_status) lobby_status->set_text("Host bind failed");
		return;
	}
	turn_mgr_.reset();
	turn_mgr_.set_net_host(&net_host_, 0);
	apply_net_options();
	turn_mgr_.clear_game_started();
	net_game_started_ = false; // wait for Start (NMT_GAME_START) before any lockstep turn
	net_oos_shown_turn_ = 0;
	net_setup_ = rtso::NetSetupState();
	net_setup_key_.clear();
	net_setup_sent_.clear();
	net_ready_ = 0;
	std::ostringstream js;
	// No host_ip: the lobby records the address this request comes from.
	js << "{\"id\":\"room-" << cfg.port << "\",\"host_port\":"
	   << cfg.port << ",\"players\":1,\"max_players\":" << (int)cfg.maxPlayers
	   << ",\"map_id\":\"default\"}";
	std::string body;
	// The menu's LobbySession lists a game it hosts itself, with its details;
	// this post would overwrite them with placeholders.
	Node *ls_node = get_node_or_null(NodePath("/root/LobbySession"));
	const bool menu_listed = ls_node && !Dictionary(ls_node->get("room")).is_empty();
	if (!menu_listed) rtso::lobby_http_post_json(lobby_http_host_, lobby_http_port_, "/rooms", js.str(), body);
	if (lobby_status) lobby_status->set_text("Hosting on " + String::num_int64(cfg.port));
	refresh_lobby_rooms();
}

void RtsoMain::_on_lobby_start_pressed() {
	if (!sim) return;
	if (!net_host_.isHost()) {
		if (lobby_status) lobby_status->set_text("Start is host-only - Join waits for host Start");
		return;
	}
	// Seed-sync + full setup broadcast: the host's CURRENT match settings
	// are packed into NMT_GAME_START so the join path applies the identical
	// setup via apply_game_setup. generate_random_map funnels the seed into
	// set_sim_seed, so both RNG streams (RMS + sim ticks) match.
	host_net_setup_tick(false);
	if (!net_setup_.all_ready()) {
		// 0 A.D. gamesetup: Start waits until every player is ready.
		if (lobby_status) lobby_status->set_text("Not every player is ready");
		emit_signal("net_setup_refused", String("Not every player is ready"));
		return;
	}
	if (!net_saved_game_.is_empty()) {
		host_start_saved_game();
		return;
	}
	rtso::GameSetupParams setup;
	match_settings_setup(setup);
	Node *ms = get_node_or_null(NodePath("/root/MatchSettings"));
	if (ms) ms->set("net_map_random", false);
	if (setup.seed == 0 && !(ms && (bool)ms->get("seed_exact"))) setup.seed = (uint32_t)Time::get_singleton()->get_ticks_msec();
	// A Random civ is picked here, so every peer is told the same one.
	for (uint8_t i = 0; i < setup.slot_count && i < 8; ++i)
		if (!rtso::is_playable_civ(setup.civs[i])) setup.civs[i] = rtso::playable_civ(setup.seed / 7u + i * 2654435761u);
	// Every connected joiner needs a human slot to play: if the lobby left too
	// few, turn the first AI slots into human ones (before broadcast, so both
	// peers seat players identically in assign_slot_players).
	{
		prepare_lobby_seats(setup, true);
	}
	// PlayerName onFinalizeAttributes: a seated client's name becomes its slot's
	// PlayerData Name, so the replay and the summary name the real players.
	for (uint32_t c = 0; c < rtso::NET_SETUP_CLIENTS; ++c) {
		const uint8_t slot = setup.seats[c];
		if (slot >= setup.slot_count || slot >= 8 || setup.slot_types[slot] != 'h') continue;
		std::string name = c == 0 ? net_host_.localPlayerName() : std::string();
		for (const auto &p : net_host_.getPeers())
			if (c != 0 && p.id == c && p.connected) name = p.player_name;
		if (!name.empty()) setup.names[slot] = name;
	}
	turn_mgr_.reset();
	turn_mgr_.set_net_host(&net_host_, 0);
	turn_mgr_.assign_slot_players(setup);
	{
		// The joiners wait while the host loads the map (0.28 page_loading).
		rtso::NetHost::KeepAlive keep(net_host_);
		show_net_loading_page(setup.map_id);
		apply_game_setup(setup);
	}
	turn_mgr_.set_game_start_applied_sim(sim);
	turn_mgr_.mark_game_started(sim);
	start_net_speed(setup.speed);
	net_setup_.setup = setup;
	++net_setup_.seq;
	net_host_.note_game_start_sent();
	net_host_.sendGameStart(rtso::format_game_start_full(setup));
	net_game_started_ = true;
	begin_match_recording(&setup);
	print_line("[RTS0][GameStart] HOST broadcast setup: map=" + String(setup.map_id.c_str()) +
	           " seed=" + String::num_int64((int64_t)setup.seed) +
	           " slots=" + String::num_int64(setup.slot_count) +
	           " civ0=" + String::num_int64((int64_t)setup.civs[0]) +
	           " civ1=" + String::num_int64((int64_t)setup.civs[1]) +
	           " types=" + String::num_int64((int64_t)setup.slot_types[0]) +
	           "/" + String::num_int64((int64_t)setup.slot_types[1]) +
	           " res=" + String::num_int64((int64_t)setup.start_res));
	if (lobby_status)
		lobby_status->set_text(String("Started ") + String(setup.map_id.c_str()) +
		                         " seed " + String::num_int64((int64_t)setup.seed));
}

// 0 A.D. GameSpeed in a networked match: the speed set in game setup, the
// same on every peer, with no speed control in the match (set_game_speed
// refuses and the button is hidden, as session.js does when g_IsNetworked).
void RtsoMain::start_net_speed(uint16_t speed) {
	game_speed = std::clamp(speed, (uint16_t)25, (uint16_t)300) / 100.0f;
	if (speed_btn) speed_btn->set_text(speed_text(game_speed));
}

// 0.28 switches every peer to page_loading at "start": the loading page goes
// over the match while it loads the map, following load_progress.
void RtsoMain::show_net_loading_page(const std::string &map_id) {
	Ref<PackedScene> scene = ResourceLoader::get_singleton()->load("res://scenes/menu/loading.tscn");
	if (scene.is_null() || !is_inside_tree()) return;
	Node *page = scene->instantiate();
	page->set("overlay_for", this);
	page->set("overlay_map", String::utf8(map_id.c_str()));
	get_tree()->get_root()->add_child(page);
}

void RtsoMain::apply_net_game_start() {
	if (!sim) return;
	turn_mgr_.set_game_start_applied_sim(sim);
	rtso::GameSetupParams setup;
	if (!turn_mgr_.take_pending_game_start_full(setup)) return;
	if (setup.saved_game) {
		// NMT_SAVED_GAME_START: not our world but the host's, sent as a rejoiner
		// gets it; _process plays on once apply_pending_rejoin has loaded it.
		net_setup_.setup = setup;
		++net_setup_.seq;
		turn_mgr_.request_rejoin(lobby_observe && lobby_observe->is_pressed());
		if (lobby_status) lobby_status->set_text("Loading the host's saved game");
		return;
	}
	turn_mgr_.reset_keep_inbound(); // host batches can arrive in the same poll as GAME_START
	turn_mgr_.cancel_rejoin();
	// The host numbers joiners 1, 2, ... in connect order; that id is our client id.
	uint32_t my_client = 0;
	if (!net_host_.isHost()) my_client = net_host_.getLocalPeerId() ? net_host_.getLocalPeerId() : 1;
	turn_mgr_.set_net_host(&net_host_, my_client);
	turn_mgr_.assign_slot_players(setup);
	{
		// The host waits while this joiner loads the map.
		rtso::NetHost::KeepAlive keep(net_host_);
		show_net_loading_page(setup.map_id);
		apply_game_setup(setup); // after seating, so it selects the local player's units
	}
	turn_mgr_.set_game_start_applied_sim(sim);
	turn_mgr_.note_game_start_applied(sim);
	start_net_speed(setup.speed);
	net_game_started_ = true;
	begin_match_recording(&setup);
	print_line("[RTS0][GameStart] JOINER consumed NMT_GAME_START: map=" + String(setup.map_id.c_str()) +
	           " seed=" + String::num_int64((int64_t)setup.seed) +
	           " slots=" + String::num_int64(setup.slot_count) +
	           " civ0=" + String::num_int64((int64_t)setup.civs[0]) +
	           " civ1=" + String::num_int64((int64_t)setup.civs[1]) +
	           " types=" + String::num_int64((int64_t)setup.slot_types[0]) +
	           "/" + String::num_int64((int64_t)setup.slot_types[1]) +
	           " res=" + String::num_int64((int64_t)setup.start_res));
	if (lobby_status)
		lobby_status->set_text(String("GameStart ") + String(setup.map_id.c_str()) +
		                         " seed " + String::num_int64((int64_t)setup.seed));
}

void RtsoMain::_on_lobby_join_pressed() {
	std::string ip = lobby_host->get_text().utf8().get_data();
	uint16_t port = (uint16_t)lobby_port->get_text().to_int();
	PackedInt32Array sel = lobby_rooms->get_selected_items();
	if (sel.size() > 0) {
		int idx = sel[0];
		if (idx >= 0 && (size_t)idx < lobby_room_cache_.size() && !lobby_room_cache_[(size_t)idx].host_ip.empty()) {
			ip = lobby_room_cache_[(size_t)idx].host_ip;
			port = lobby_room_cache_[(size_t)idx].host_port;
		}
	}
	if (port == 0) port = rtso::PS_DEFAULT_PORT;
	if (ip.empty()) ip = "127.0.0.1";
	net_host_.setLocalPlayerName(lobby_name->get_text().utf8().get_data());
	net_host_.setPassword(lobby_password ? lobby_password->get_text().utf8().get_data() : "");
	kicks_shown_ = 0;
	apply_net_host_options(net_host_);
	if (!net_host_.startClient(ip, port)) {
		if (lobby_status) lobby_status->set_text("Join failed");
		return;
	}
	turn_mgr_.reset();
	turn_mgr_.set_net_host(&net_host_, 1);
	apply_net_options();
	turn_mgr_.clear_game_started();
	net_game_started_ = false; // wait for host's NMT_GAME_START before lockstep turns
	net_oos_shown_turn_ = 0;
	net_setup_ = rtso::NetSetupState();
	net_setup_key_.clear();
	net_setup_sent_.clear();
	net_ready_ = 0;
	// Once connected, ask to rejoin: a host still in its lobby declines and its
	// NMT_GAME_START starts us as usual; a host mid-match sends its world.
	net_host_.onPeerConnected([this](uint32_t) {
		if (!net_host_.isHost() && !net_game_started_)
			turn_mgr_.request_rejoin(lobby_observe && lobby_observe->is_pressed());
	});
	if (lobby_status)
		lobby_status->set_text("Connecting " + String(ip.c_str()) + ":" + String::num_int64(port));
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
		if (!rtso::tech_available_to_civ((uint8_t)t, civ)) continue;
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

void RtsoMain::_on_queue_slot_gui_input(const Ref<InputEvent> &event, int slot_idx) {
	command_panel_.production_queue().handle_gui_input(event, slot_idx, get_viewport());
}

// 0.28 placementTooltip: BackgroundErrorTooltip for a refusal, else
// BackgroundInformationTooltip, beside the cursor; hidden with no message.
void RtsoMain::set_placement_tip(const String &text, bool error) {
	if (!placement_tip_) return;
	placement_tip_->set_visible(!text.is_empty());
	if (text.is_empty()) return;
	if (placement_tip_->get_text() != text) placement_tip_->set_text(text);
	if (error != placement_tip_error_ || !placement_tip_->has_theme_stylebox_override("normal")) {
		placement_tip_error_ = error;
		Ref<StyleBoxFlat> bg;
		bg.instantiate();
		bg->set_bg_color(error ? Color(0.35f, 0.04f, 0.04f, 0.85f) : Color(0.05f, 0.05f, 0.08f, 0.85f));
		bg->set_border_width_all(1);
		bg->set_border_color(error ? Color(0.75f, 0.25f, 0.2f) : Color(0.55f, 0.5f, 0.35f));
		bg->set_content_margin_all(5.0f);
		placement_tip_->add_theme_stylebox_override("normal", bg);
	}
	placement_tip_->set_size(Vector2());
	placement_tip_->set_position(get_viewport()->get_mouse_position() + Vector2(24, 20));
}

// BuildRestrictions.js CheckPlacement's message for a refused placement of `btype` centred at (x, z).
String RtsoMain::placement_failure_text(int result, uint8_t btype, float x, float z) const {
	using S = rtso::Simulation;
	if (!sim || result == S::PLACE_OK) return String();
	const String name = rtso::tr0(String::utf8(rtso::civ_building_type_info(sim->player_civ(local_player()), btype).name));
	auto named = [&](const char *msg) { return rtso::sprintf0(rtso::tr0(msg), {{"name", name}}); };
	switch (result) {
		case S::PLACE_OBSTRUCTED: return named("%(name)s cannot be built on another building or resource");
		case S::PLACE_TERRAIN: return named("%(name)s cannot be built on invalid terrain");
		case S::PLACE_SHORE: return named("%(name)s must be built on a valid shoreline");
		case S::PLACE_UNEXPLORED: return named("%(name)s cannot be built in unexplored area");
		case S::PLACE_TERRITORY: {
			const S::PlacementDetails d = sim->placement_details(local_player(), btype, x, z);
			// markForTranslationWithContext("Territory type", ...) and the template's Territory list.
			const char *found = d.territory_found == 1 ? (d.connected ? "own" : "unconnected own")
					: d.territory_found == 2 ? (d.connected ? "allied" : "unconnected allied")
					: d.territory_found == 4 ? "neutral" : "enemy";
			String valid;
			const char *const kinds[] = {"own", "ally", "neutral", "enemy"};
			for (int i = 0; i < 4; ++i)
				if (d.territory_allowed & (1 << i)) valid += (valid.is_empty() ? String() : rtso::tr0(", ")) + rtso::tr0(kinds[i]);
			return rtso::sprintf0(rtso::tr0("%(name)s cannot be built in %(territoryType)s territory. Valid territories: %(validTerritories)s"),
					{{"name", name}, {"territoryType", rtso::tr0(found)}, {"validTerritories", valid}});
		}
		case S::PLACE_DISTANCE: {
			const S::PlacementDetails d = sim->placement_details(local_player(), btype, x, z);
			const int64_t m = (int64_t)std::lround(d.min_distance_m);
			return rtso::sprintf0(rtso::tr0(m == 1 ? "%(name)s too close to a %(category)s, must be at least %(distance)s meter away"
							: "%(name)s too close to a %(category)s, must be at least %(distance)s meters away"),
					{{"name", name}, {"category", rtso::tr0(d.distance_class ? d.distance_class : "")}, {"distance", String::num_int64(m)}});
		}
		default: return named("%(name)s cannot be built due to unknown error");
	}
}

void RtsoMain::_on_queue_slot_cancel(int slot_idx) {
	command_panel_.production_queue().cancel(slot_idx);
}

void RtsoMain::toggle_chat() {
	if (!chat_window_.is_open() && net_disconnected_) return; // Chat.openPage
	if (!chat_window_.is_open()) close_open_dialogs();
	refresh_chat_tooltips();
	chat_window_.toggle();
}

void RtsoMain::_on_chat_channel_btn_pressed(int channel_idx) {
	chat_window_.select_channel(channel_idx);
}

// colorizePlayernameByGUID: the client's name in its player's colour (grey for an
// observer); "peer-<id>" is the sender's client, empty the local player.
String RtsoMain::chat_user(const String &sender_guid) {
	uint32_t client = turn_mgr_.local_client_id();
	if (sender_guid.begins_with("peer-")) client = (uint32_t)sender_guid.substr(5).to_int();
	// Before Start the setup knows every client's name and slot colour.
	if (is_net_pregame()) return setup_client_name(net_setup_, client);
	String name;
	uint8_t pl = 0xFF;
	if (net_lockstep()) {
		for (const auto &e : net_host_.playerAssignment())
			if (e.client == client) {
				name = String::utf8(e.name.c_str());
				pl = e.player;
			}
		if (pl == 0xFF) pl = turn_mgr_.player_for_client(client);
	} else {
		pl = local_player();
	}
	if (name.is_empty()) name = pl < rtso::MAX_PLAYERS ? player_display_name(pl) : sender_guid;
	char hex[8] = "b3b3b3";
	if (sim && pl < rtso::MAX_PLAYERS) std::snprintf(hex, sizeof(hex), "%06x", (unsigned)(sim->player_color(pl) & 0xFFFFFF));
	return "[color=#" + String(hex) + "]" + name.replace("[", "[lb]") + "[/color]";
}

// network.js reportMismatchingSoftwareVersions: both versions when the host's
// NMT_SERVER_HANDSHAKE named its own, else getDisconnectReason(17).
String RtsoMain::mismatch_detail() const {
	const std::string &host = net_host_.hostSoftwareVersion();
	if (host.empty() || host == net_host_.softwareVersion())
		return tr("Different game engine versions or different mods loaded.");
	return sprintf0(tr("Different engine versions detected client version: %(clientMismatch)s server version: %(serverMismatch)s"),
			{{"clientMismatch", String::utf8(net_host_.softwareVersion().c_str())}, {"serverMismatch", String::utf8(host.c_str())}});
}

uint8_t RtsoMain::chat_player_of(const String &sender_guid) {
	if (!net_lockstep()) return own_seat();
	uint32_t client = turn_mgr_.local_client_id();
	if (sender_guid.begins_with("peer-")) client = (uint32_t)sender_guid.substr(5).to_int();
	return chat_player_of_client(client);
}

uint8_t RtsoMain::chat_player_of_client(uint32_t client) const {
	for (const auto &e : net_host_.playerAssignment())
		if (e.client == client) return e.player;
	return turn_mgr_.player_for_client(client);
}

// ChatAddressees.js AddresseeTypes isAddressee, with the local player as the receiver.
bool RtsoMain::chat_is_addressee(const String &sender_guid, const String &tag) {
	return chat_reaches(chat_player_of(sender_guid), own_seat(), tag.utf8().get_data(),
			net_lockstep() ? turn_mgr_.local_client_id() : UINT32_MAX);
}

// chat_routing.h chat_tag_reaches on this match's diplomacy: the receiver's
// check above, and the host's relay (NetHost::setChatRouter, _ready).
bool RtsoMain::chat_reaches(uint8_t from, uint8_t to, const std::string &tag, uint32_t to_client) const {
	auto stance = [this](uint8_t a, uint8_t b, uint8_t s) { return sim && sim->get_diplomacy(a, b) == s; };
	return rtso::chat_tag_reaches(from, to, tag, rtso::MAX_PLAYERS,
			[&](uint8_t a, uint8_t b) { return stance(a, b, rtso::Simulation::DIPLO_ALLY); },
			[&](uint8_t a, uint8_t b) { return stance(a, b, rtso::Simulation::DIPLO_ENEMY); }, to_client,
			player_finished(from), player_finished(to));
}

// session.js isPlayerObserver: a seat whose game is over (won or defeated).
bool RtsoMain::player_finished(uint8_t pid) const {
	return pid < rtso::MAX_PLAYERS && sim && (sim->is_defeated(pid) || sim->is_victory_for(pid));
}

// session.js g_IsObserver: no seat, or a seat whose game is over. It goes by the
// seat (Engine.GetPlayerID), not the player an observer picked to view.
bool RtsoMain::observing_seat() const {
	if (turn_mgr_.is_replaying()) return local_player() >= rtso::MAX_PLAYERS;
	const uint8_t seat = seat_player();
	return seat >= rtso::MAX_PLAYERS || player_finished(seat);
}

// This client's own seat (Engine.GetPlayerID): what it chats, pauses, resigns
// and is summarised as, whichever player it views. A replay keeps its viewed player.
uint8_t RtsoMain::own_seat() const { return turn_mgr_.is_replaying() ? local_player() : seat_player(); }

bool RtsoMain::chat_local_observer() { return observing_seat(); }

std::vector<std::pair<String, String>> RtsoMain::chat_private_targets() {
	std::vector<std::pair<String, String>> out;
	if (!net_lockstep()) return out;
	const uint32_t mine = turn_mgr_.local_client_id();
	const bool me_obs = chat_local_observer();
	for (const auto &e : net_host_.playerAssignment()) {
		if (e.client == mine) continue;
		const bool obs = e.player >= rtso::MAX_PLAYERS || player_finished(e.player);
		if (me_obs && !obs) continue; // no private message from an observer to a player
		// 0.28 /msg names the addressee's GUID: an observer by its client, so a
		// line to one observer reaches no other.
		const String tag = obs ? "[Observer-" + String::num_int64(e.client) + "]"
		                       : "[Player-" + String::num_int64((int)e.player + 1) + "]";
		out.push_back({tag, chat_user("peer-" + String::num_int64(e.client))});
	}
	// sortGUIDsByPlayerID: players by id, observers after them.
	std::stable_sort(out.begin(), out.end(), [](const auto &a, const auto &b) {
		auto key = [](const String &tag) { return tag.begins_with("[Player-") ? tag.substr(8).to_int() : 1000; };
		return key(a.first) < key(b.first);
	});
	return out;
}

void RtsoMain::_on_chat_send_pressed() {
	chat_window_.send_pressed();
}

void RtsoMain::_on_chat_text_submitted(const String &text) {
	(void)text;
	chat_window_.send_pressed();
}

void RtsoMain::_on_chat_input_gui(const Ref<InputEvent> &event) {
	Ref<InputEventKey> k = event;
	// ChatInput's onTab is plain Tab: Ctrl+Tab stays the summary hotkey.
	if (k.is_valid() && k->is_pressed() && k->get_keycode() == KEY_TAB && !k->is_ctrl_pressed() && !k->is_alt_pressed() && !k->is_meta_pressed())
		chat_window_.tab_complete();
}

std::vector<String> RtsoMain::chat_autocomplete_names() {
	std::vector<String> out;
	if (net_lockstep()) {
		for (const auto &e : net_host_.playerAssignment())
			if (!e.name.empty()) out.push_back(String::utf8(e.name.c_str()));
	} else if (local_player() < rtso::MAX_PLAYERS) {
		out.push_back(player_display_name(local_player()));
	}
	return out;
}

// 0 A.D. MatchSettingsDialog.toggle.
void RtsoMain::toggle_objectives() {
	const bool open = objectives_dialog_.is_open();
	close_open_dialogs();
	if (!open) objectives_dialog_.open(match_settings_description(), match_settings_player_state());
}

// The settings getGameDescription describes: the running match's own (saved with it)
// and the Game Setup ones only the map generator used (in every replay and save).
Dictionary RtsoMain::get_match_settings_info() const {
	Dictionary d;
	if (!sim) return d;
	if (Node *ms = get_node_or_null(NodePath("/root/MatchSettings")))
		for (const char *k : {"map_size", "start_res", "team_placement", "trigger_difficulty", "biome", "rated"}) d[k] = ms->get(k);
	// gamesettings PlayerPlacement.pickRandomItems: a Random pick shows the pattern in play.
	if (sim->played_team_placement()) d["team_placement"] = (int)sim->played_team_placement();
	// gamedescription.js Rated game: rated with two players, else "disabled" in a lobby game.
	int players = 0;
	for (uint8_t p = 0; p < rtso::MAX_PLAYERS; ++p) players += sim->player(p).active ? 1 : 0;
	d["player_count"] = players;
	Node *ls = get_node_or_null(NodePath("/root/LobbySession"));
	d["lobby_game"] = net_lockstep() && ls && bool(ls->call("signed_in"));
	d["map_id"] = String::utf8(sim->current_map_id().c_str());
	d["map_name"] = String::utf8(sim->current_map_name().c_str());
	d["victory_mask"] = (int)sim->victory_conditions(); // rtso::VictoryKind bits
	d["wonder_min"] = (int)std::lround(sim->wonder_duration() / 60.0f);
	d["relic_min"] = (int)std::lround(sim->relic_duration() / 60.0f);
	d["relic_count"] = sim->relic_count();
	d["regicide_garrison"] = sim->regicide_garrison();
	d["lock_teams"] = sim->teams_locked();
	d["last_man_standing"] = sim->last_man_standing();
	d["ceasefire_min"] = (int)std::lround(sim->ceasefire_seconds() / 60.0f);
	d["pop_cap"] = sim->pop_cap_setting(); // negative: World or Team Population
	d["nomad"] = sim->nomad();
	d["disable_treasures"] = sim->disable_treasures();
	d["explore_map"] = sim->explore_map();
	d["ally_view"] = sim->ally_view();
	// The match's setting (g_InitAttributes), not the live state the reveal-map cheats change.
	Node *ms = get_node_or_null(NodePath("/root/MatchSettings"));
	d["reveal_map"] = ms ? bool(ms->get("reveal_map")) : sim->reveal_map();
	d["cheats"] = sim->cheats_enabled();
	return d;
}

String RtsoMain::match_settings_description() const {
	Ref<Script> desc = ResourceLoader::get_singleton()->load("res://scenes/session/game_description.gd");
	return desc.is_valid() ? (String)desc->call("describe", get_match_settings_info()) : String();
}

// MatchSettingsDialog.rebuild: g_PlayerStateMessages for a player who has won or lost.
// It names the local seat (Engine.GetPlayerID()), whoever is viewed: none in a replay or
// for an observer.
String RtsoMain::match_settings_player_state() const {
	if (turn_mgr_.is_replaying()) return String();
	const uint8_t me = seat_player();
	if (!sim || me >= rtso::MAX_PLAYERS) return String();
	if (sim->is_victory_for(me)) return "You have won!";
	if (sim->is_defeated(me)) return "You have been defeated!";
	return String();
}

// session.js closeOpenDialogs: the menu, chat, diplomacy, trade and match
// settings close together; opening one of them closes the others first.
// MiniMapFlareButton.onPress (and session.flareactivate): INPUT_FLARE, so the
// next left click on the map or minimap sends a flare; never in a replay.
void RtsoMain::_on_flare_button_pressed() {
	if (turn_mgr_.is_replaying()) return;
	flare_armed_ = true;
}

// CivIcon.onPress: a left click on the top panel's civ emblem reopens the
// reference page last viewed (the Structure Tree at first).
void RtsoMain::_on_civ_icon_gui_input(const Ref<InputEvent> &event) {
	Ref<InputEventMouseButton> mb = event;
	if (mb.is_null() || !mb->is_pressed() || mb->get_button_index() != MOUSE_BUTTON_LEFT) return;
	if (Node *tree = get_node_or_null(NodePath("TechTreeDialog"))) tree->call("open_last");
}

bool RtsoMain::close_open_dialogs() {
	bool any = false;
	if (Node *menu = get_node_or_null(NodePath("PauseMenu")))
		if (Object::cast_to<CanvasItem>(menu) ? Object::cast_to<CanvasItem>(menu)->is_visible() : bool(menu->get("visible"))) {
			_on_menu_button_pressed();
			any = true;
		}
	if (chat_window_.is_open()) {
		chat_window_.close();
		any = true;
	}
	if (diplo_dialog_.is_open()) {
		diplo_dialog_.close();
		any = true;
	}
	if (objectives_dialog_.is_open()) {
		objectives_dialog_.close();
		any = true;
	}
	if (Node *barter = get_node_or_null(NodePath("BarterDialog")))
		if (bool(barter->call("is_open"))) {
			barter->call("toggle");
			any = true;
		}
	return any;
}

// 0 A.D. messages.js onTutorialReady: "dialog-answer" to the trigger script; the
// last goal's Leave opens the session menu (0 A.D. ends the game there).
void RtsoMain::_on_tutorial_ready_pressed() {
	if (!sim || !sim->tutorial_ready_button()) return;
	// messages.js: on the leave goal the button reads Quit and calls endGame(true).
	if (tutorial_panel_.on_last_goal(*sim)) {
		Ref<Script> q = ResourceLoader::get_singleton()->load("res://scenes/session/quit_confirmation.gd");
		if (q.is_valid()) q->call("end_game", this, true);
		return;
	}
	post_lockstep((uint8_t)rtso::NetCommandType::NET_CMD_TUTORIAL_READY, nullptr, 0, 0.0f, 0.0f, 0, 0);
}

// Top panel IconButtons: DiplomacyDialogButton, TradeDialogButton, MatchSettingsDialogButton.
void RtsoMain::_toggle_session_dialog(String name) {
	if (name == "DiplomacyDialog") toggle_diplomacy();
	else if (name == "Objectives") toggle_objectives();
	else if (Node *n = get_node_or_null(NodePath(name))) {
		if (!(n->has_method("is_open") && bool(n->call("is_open")))) close_open_dialogs();
		n->call("toggle");
	}
}

void RtsoMain::toggle_diplomacy() {
	if (!diplo_dialog_.is_open()) close_open_dialogs();
	diplo_dialog_.toggle();
}

void RtsoMain::_on_diplo_stance_changed(int stance, int player_idx) {
	diplo_dialog_.stance_changed(stance, player_idx);
}

void RtsoMain::_on_diplo_tribute_pressed(int player_idx, int res_type) {
	diplo_dialog_.tribute_pressed(player_idx, res_type);
}

void RtsoMain::_on_diplo_spy_pressed(int player_idx) {
	diplo_dialog_.spy_pressed(player_idx);
}

void RtsoMain::_on_diplo_attack_request_pressed(int player_idx) {
	diplo_dialog_.attack_request_pressed(player_idx);
}
