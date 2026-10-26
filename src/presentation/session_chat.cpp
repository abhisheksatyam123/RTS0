// P4.1 / P4.6: RtsoMain's in-game chat handling (0 A.D. gui/session/chat/ChatWindow.js + ChatBounce.js parity).
// Manages the chat overlay, network chat serialization, addressee filtering, cheats, and network console commands.

#include <algorithm>
#include <sstream>
#include <vector>
#include "main_node.h"
#include "network/chat_routing.h"
#include "l10n.h"
#include "user_config.h"
#include "display_colors.h"
#include "popup_safe.h"
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/line_edit.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>

using namespace godot;
using rtso::sprintf0;

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
	// Cheats.js reads the number with +parameter, so "1.5" and "1e3" count.
	if (cheat->numeric && !parameter.is_empty() && !parameter.is_valid_float()) {
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
	int64_t number = cheat->numeric && !parameter.is_empty() ? (int64_t)parameter.to_float() : 0;
	uint32_t extra = (uint32_t)std::max(cheat->resource, 0);
	if (cheat->action == S::CHEAT_CONVERT_UNITS || cheat->action == S::CHEAT_DEFEAT_PLAYER)
		number = number < 0 ? -1 : (number == 0 ? rtso::GAIA_PLAYER : number - 1);
	// The selection, a selected structure first (createunits and brainiac read selected[0]).
	std::vector<uint32_t> ids;
	if (selected_building_id) ids.push_back(selected_building_id);
	for (int i = 0; i < selected_ids.size(); ++i) ids.push_back((uint32_t)selected_ids[i]);
	if (cheat->action == S::CHEAT_RESEARCH) {
		// "brainiac N [top|bottom]" researches the selected structure's Nth research icon
		// (Researcher.js GetTechnologiesList: a chain shows its next step, a pair is one
		// entry, "top" by default), else a tech by its 0 A.D. name.
		extra = rtso::TECH_NONE;
		const rtso::Simulation::Building *rb = nullptr; // the selected structure
		for (const auto &bb : sim->buildings())
			if (selected_building_id && bb.id == selected_building_id) rb = &bb;
		const String first = parameter.get_slice(" ", 0), half = parameter.get_slice(" ", 1);
		if (rb && first.is_valid_float()) {
			const uint8_t pciv = sim->player_civ(me);
			std::vector<std::pair<uint8_t, uint8_t>> entries; // top, bottom (TECH_NONE unless paired)
			for (int t = 1; t < rtso::TECH_COUNT; ++t) {
				if (!rtso::building_researches_tech(rb->type, (uint8_t)t, sim->structure_template_civ(*rb)) || !rtso::tech_available_to_civ((uint8_t)t, pciv)) continue;
				if (const uint8_t sup = rtso::tech_supersedes((uint8_t)t); sup != rtso::TECH_NONE
						&& rtso::building_researches_tech(rb->type, sup, sim->structure_template_civ(*rb)) && rtso::tech_available_to_civ(sup, pciv)) {
					const rtso::Simulation::ResearchCheck sc = sim->research_check(me, sup, 0);
					if (sc != rtso::Simulation::TECH_ERR_RESEARCHED && sc != rtso::Simulation::TECH_ERR_IN_PROGRESS) continue;
				}
				const rtso::Simulation::ResearchCheck rc = sim->research_check(me, (uint8_t)t, rb->id);
				if (rc == rtso::Simulation::TECH_ERR_RESEARCHED || rc == rtso::Simulation::TECH_ERR_IN_PROGRESS || rc == rtso::Simulation::TECH_ERR_CIV
						|| rc == rtso::Simulation::TECH_ERR_PAIRED || rc == rtso::Simulation::TECH_ERR_DISABLED) continue;
				const uint8_t partner = rtso::tech_pair_partner((uint8_t)t);
				auto paired = std::find_if(entries.begin(), entries.end(), [&](const auto &e) { return partner != rtso::TECH_NONE && e.first == partner; });
				if (paired != entries.end()) paired->second = (uint8_t)t;
				else entries.push_back({(uint8_t)t, rtso::TECH_NONE});
			}
			const double nth = first.to_float();
			if (nth >= 1.0 && nth <= (double)entries.size()) {
				const auto &e = entries[(size_t)nth - 1];
				extra = half == "bottom" && e.second != rtso::TECH_NONE ? e.second : e.first;
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
		} else if (specs && is_net_pregame()) {
			// Before Start the setup's observers are the assignment's unassigned clients.
			for (uint32_t c = 1; c < rtso::NET_SETUP_CLIENTS; ++c)
				if (((net_setup_.clients >> c) & 1) && net_setup_.slot_of(c) < 0)
					net_host_.kickPeer(net_host_.peerByName(net_setup_.names[c]), ban);
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
		if (networked && is_net_pregame()) {
			// g_PlayerAssignments before Start: the setup's clients, seated or watching.
			for (uint32_t c = 0; c < rtso::NET_SETUP_CLIENTS; ++c)
				if ((net_setup_.clients >> c) & 1) {
					const int slot = net_setup_.slot_of(c);
					users.emplace_back(slot >= 0 ? (uint32_t)slot : 1000u + c, chat_user("peer-" + String::num_int64(c)));
				}
		} else if (networked) {
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
		if (is_net_pregame()) chat_window_.clear_history();
		else chat_window_.clear();
		return true;
	}
	return false;
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


// messages.js handlePlayerAssignmentsMessage (onClientJoin, onClientLeave) and
// the kicked message, as ChatMessageFormatNetwork words them.
void RtsoMain::post_network_chat() {
	auto who = [&](const rtso::PlayerAssignmentMsg::Entry &e) {
		// colorizePlayernameByGUID: "Unknown Player" for a client without a name.
		String name = (e.name.empty() ? tr("Unknown Player") : String::utf8(e.name.c_str())).replace("[", "[lb]");
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
	// messages.js colorizePlayernameByGUID: the player's colour as Diplomacy Colors
	// show it, white for an observer, "Unknown Player" without a name.
	if (name.is_empty()) name = pl < rtso::MAX_PLAYERS ? player_display_name(pl) : tr("Unknown Player");
	char hex[8] = "ffffff";
	if (sim && pl < rtso::MAX_PLAYERS) std::snprintf(hex, sizeof(hex), "%06x", (unsigned)(rtso::display_colors::rgb(*sim, pl) & 0xFFFFFF));
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
		// line to one observer reaches no other. A seat whose game is over keeps
		// its player tag, which chat_tag_reaches lets observers and players reach.
		const String tag = e.player >= rtso::MAX_PLAYERS ? "[Observer-" + String::num_int64(e.client) + "]"
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

