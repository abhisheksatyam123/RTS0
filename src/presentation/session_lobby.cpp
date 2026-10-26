// P4.6: RtsoMain's multiplayer lobby & pregame net setup (0 A.D. gui/gamesetup/ and NetClient.js parity).
// Handles room browsing, seat assignment, pregame handshake, NAT traversal/STUN, and match start/join lifecycle.

#include <algorithm>
#include <cctype>
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
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/line_edit.hpp>
#include <godot_cpp/classes/item_list.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/panel.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>

using rtso::sprintf0;

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
	// A joiner sees the Connecting box, then the setup page, and the host the
	// setup page: 0.28's top panel has no Lobby button, and the session menu's
	// Lobby entry opens the lobby dialog. The developer lobby panel stays hidden.
	if (lobby_panel) lobby_panel->set_visible(false);
	if (lobby_btn) lobby_btn->set_visible(false);
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
	if (hosting && !net_game_started_) {
		// LobbyGameRegistration.formatClientsForStanza: nbp counts the clients
		// the player assignment seats, not the unassigned ones or a host who watches.
		for (uint8_t i = 0; i < net_setup_.setup.slot_count && i < 8; ++i)
			if (net_setup_.slot_client[i] >= 0) ++players;
	} else if (hosting) {
		// LobbyGamelistReporter.countConnectedPlayers: assignments with a player,
		// so not a host who watches.
		players = turn_mgr_.player_for_client(0) < rtso::MAX_PLAYERS ? 1 : 0;
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
				// LobbyGamelistReporter getObservers: whoever watches now, a late observer too.
				net_setup_.names[p.id] = p.player_name;
				if (p.is_observer) net_setup_.observers |= (1ull << p.id);
				else net_setup_.observers &= ~(1ull << p.id);
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

// gamesetup StatusMessageFormat: "== message" in sans-bold-13.
static String setup_status_line(const String &message) {
	return "[b]" + rtso::sprintf0(rtso::tr0("== %(message)s"), {{"message", message}}) + "[/b]\n";
}

// ClientKicked.js (a status line in the setup) and ChatMessageFormatNetwork kicked/banned
// (the match).
void RtsoMain::post_kick_chat() {
	const auto &kicks = net_host_.kicks();
	for (; kicks_chatted_ < kicks.size(); ++kicks_chatted_) {
		const auto &k = kicks[kicks_chatted_];
		const String name = String::utf8(k.name.empty() ? k.guid.c_str() : k.name.c_str()).replace("[", "[lb]");
		if (is_net_pregame()) {
			chat_window_.append_line(setup_status_line(sprintf0(k.ban ? tr("%(username)s has been banned")
					: tr("%(username)s has been kicked"), {{"username", name}})));
			continue;
		}
		// ChatMessageFormatNetwork.kicked: colorizePlayernameHelper, the player's
		// colour for the player of that name, white for anyone else.
		uint32_t rgb = 0xFFFFFFu;
		for (const auto &e : chat_assignments_)
			if (e.name == k.name && e.player < rtso::MAX_PLAYERS && sim) rgb = rtso::display_colors::rgb(*sim, e.player);
		const String colored = "[color=#" + String::num_int64(rgb, 16).lpad(6, "0") + "]" + name + "[/color]";
		chat_window_.append_kind(sprintf0(k.ban ? tr("%(username)s has been banned") : tr("%(username)s has been kicked"),
				{{"username", colored}}) + String("\n"), CHAT_FILTER_NETWORK);
	}
}

// colorizePlayernameByGUID in the setup: the client's name in its slot's colour,
// white while it has no slot.
// gamesetup ClientChat.js colorizePlayername: the slot colour lightened to at
// least 60% lightness (HSL), so dark colours read on the dark panel; white
// for an unassigned client, and "Unknown Player" for one with no name.
static uint32_t setup_name_rgb(uint32_t rgb) {
	const double r = ((rgb >> 16) & 0xFF) / 255.0, g = ((rgb >> 8) & 0xFF) / 255.0, b = (rgb & 0xFF) / 255.0;
	const double mx = std::max({r, g, b}), mn = std::min({r, g, b});
	double h = 0, sat = 0, l = (mx + mn) / 2;
	if (mx != mn) {
		const double d = mx - mn;
		sat = l > 0.5 ? d / (2 - mx - mn) : d / (mx + mn);
		h = mx == r ? (g - b) / d + (g < b ? 6 : 0) : mx == g ? (b - r) / d + 2 : (r - g) / d + 4;
		h /= 6;
	}
	l = std::max(0.6, l);
	auto hue = [](double p, double q, double t) {
		if (t < 0) t += 1;
		if (t > 1) t -= 1;
		if (t < 1.0 / 6) return p + (q - p) * 6 * t;
		if (t < 0.5) return q;
		if (t < 2.0 / 3) return p + (q - p) * (2.0 / 3 - t) * 6;
		return p;
	};
	double ro = l, go = l, bo = l;
	if (sat != 0) {
		const double q = l < 0.5 ? l * (1 + sat) : l + sat - l * sat, p = 2 * l - q;
		ro = hue(p, q, h + 1.0 / 3);
		go = hue(p, q, h);
		bo = hue(p, q, h - 1.0 / 3);
	}
	auto byte = [](double v) { return (uint32_t)std::lround(std::clamp(v, 0.0, 1.0) * 255); };
	return byte(ro) << 16 | byte(go) << 8 | byte(bo);
}

String RtsoMain::setup_client_name(const rtso::NetSetupState &n, uint32_t client) const {
	String name = client < rtso::NET_SETUP_CLIENTS ? String::utf8(n.names[client].c_str()) : String();
	if (name.is_empty()) name = tr("Unknown Player");
	const int slot = n.slot_of(client);
	const uint32_t rgb = slot >= 0 && n.setup.colors[slot] < rtso::PLAYER_COLOR_COUNT
			? setup_name_rgb(rtso::PLAYER_COLORS[n.setup.colors[slot]] & 0xFFFFFFu) : 0xFFFFFFu;
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
				chat_window_.append_line(setup_status_line(sprintf0(tr("%(username)s has joined"),
						{{"username", String::utf8(after.names[c].c_str()).replace("[", "[lb]")}})));
			else if (was && !is)
				chat_window_.append_line(setup_status_line(sprintf0(tr("%(username)s has left"), {{"username", setup_client_name(before, c)}})));
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
		chat_window_.append_line(setup_status_line(tr("Game settings have been changed")));
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
	d["started"] = net_game_started_;
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
	// A disconnect in the setup (kicked, banned, the host gone): 0.28 keeps the
	// page and its chat behind the "Disconnected" box, with no chat input and
	// no Ready (ChatInputPanel, ReadyButton); Back leaves.
	if (net_disconnected_ && net_setup_overlay_ && !net_game_started_) {
		if (net_status_overlay_ && net_status_overlay_->get_parent() != net_setup_layer_) {
			net_status_overlay_->reparent(net_setup_layer_, false);
			if (net_setup_overlay_->has_method("net_disconnected")) net_setup_overlay_->call("net_disconnected");
		}
		return;
	}
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
			!net_saved_loading_ && (net_host_.authResult() < 0 || turn_mgr_.rejoin_syncing());
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

// Game Setup's Back, the Connecting box's Cancel and the disconnect box's OK
// before Start (SetupWindow.js closePage, gamesetup_mp.js cancelSetup): back to
// the lobby while signed in to it, else to the main menu, where a join the
// host never let in reopens the "Joining an existing game." form.
void RtsoMain::leave_net_game() {
	const int auth = net_host_.authResult();
	const bool not_let_in = net_host_.getRole() == rtso::NetRole::Client &&
			auth != rtso::ARC_OK && auth != rtso::ARC_OK_SAVED_GAME && auth != rtso::ARC_OK_REJOINING;
	net_host_.stop();
	Node *ms = get_node_or_null(NodePath("/root/MatchSettings"));
	if (!ms) return;
	Node *lobby = get_node_or_null(NodePath("/root/LobbySession"));
	if (lobby && bool(lobby->call("signed_in"))) {
		ms->call("go", "res://scenes/menu/lobby.tscn");
		return;
	}
	ms->set("net_reopen", not_let_in ? String("join") : String());
	ms->call("go", "res://scenes/menu/main_menu.tscn");
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


// Before Host or Join: what the handshake names (the client sends it at once).
static void apply_net_host_options(rtso::NetHost &net) {
	net.setDuplicatePlayerNames(rtso::user_config::get_bool("network.duplicateplayernames"));
	// Builds whose simulation state differs would fall out of sync: the
	// handshake names the save format too, so the host refuses them up front.
	net.setSoftwareVersion(std::string(rtso::NET_SOFTWARE_VERSION) + " save " +
			std::to_string(rtso::Simulation::SAVE_VERSION));
	// Mods override the game's data, so the host refuses a client with
	// other mods too (NetProtocol CheckHandshake, in load order).
	std::vector<std::pair<std::string, std::string>> mods;
	Ref<Script> lib = ResourceLoader::get_singleton()->load("res://scenes/menu/mods.gd");
	const Variant v = lib.is_valid() ? lib->call("engine_mods") : Variant();
	if (v.get_type() == Variant::ARRAY) {
		const Array list = v;
		for (int64_t i = 0; i < list.size(); ++i) {
			const Dictionary m = list[i];
			if ((bool)m.get("ignoreInCompatibilityChecks", false)) continue;
			mods.emplace_back(String(m.get("name", "")).utf8().get_data(), String(m.get("version", "")).utf8().get_data());
		}
	}
	net.setEnabledMods(std::move(mods));
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
	chat_window_.clear_history(); // the session's chat starts empty (Chat.js)
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
	game_speed = std::clamp(speed, (uint16_t)10, (uint16_t)2000) / 100.0f; // game_speeds.json's range
	if (speed_btn && speed_btn->get_button_icon().is_null()) speed_btn->set_text(speed_text(game_speed));
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
		// NetClient OnSavedGameStart: the setup gives way to the loading page,
		// which stays until the world has come and loaded.
		net_saved_loading_ = true;
		show_net_loading_page(setup.map_id);
		update_net_setup_overlay();
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
	chat_window_.clear_history(); // the session's chat starts empty (Chat.js)
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

