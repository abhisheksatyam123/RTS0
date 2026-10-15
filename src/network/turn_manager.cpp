#include "turn_manager.h"
#include "net_host.h"
#include <cctype>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <iterator>
#include <filesystem>
#include <cstring>
#include <algorithm>

namespace rtso {

TurnManager::TurnManager() {
	reset();
}

TurnManager::TurnManager(NetHost *host, uint32_t client_id) : net_host_(nullptr) {
	reset();
	set_net_host(host, client_id);
}

TurnManager::~TurnManager() = default;

// Arm the boot-hash capture. Called the moment a session becomes authoritative
// (the host on bind, a joiner when the game layer consumes its NMT_GAME_START), so
// the hash is taken from the world's BOOT state: before any lockstep command or
// sim step has run. That is exactly what a late joiner reproduces when it rebuilds
// turn 0 from the GAME_START resend, which is what makes the turn-0 handshake a
// real proof rather than a number nobody can check.
// The Simulation reference is non-owning and only read on the next tick().
void TurnManager::arm_boot_capture(const Simulation &sim) {
	if (net_host_ && net_host_->getRole() != NetRole::None) {
		sim_boot_hash_ptr_ = &sim;
		if (!boot_hash_captured_) {
			boot_hash_captured_ = true;
			boot_hash_ = compute_sim_hash(sim);
		}
	}
}

// Offline play (no NetHost, or a NetHost that never bound a socket) is ungated;
// only a live session holds the lockstep closed.
bool TurnManager::net_game_started() const {
	if (!net_host_) return true;
	if (net_host_->getRole() == NetRole::None) return true;
	return net_game_started_;
}

void TurnManager::reset_keep_inbound() {
	auto cmds = std::move(turn_remote_cmds_);
	auto batches = std::move(received_batches_);
	auto hashes = std::move(peer_hashes_);
	auto paused = std::move(paused_clients_);
	reset();
	paused_clients_ = std::move(paused);
	turn_remote_cmds_ = std::move(cmds);
	received_batches_ = std::move(batches);
	peer_hashes_ = std::move(hashes);
}

bool TurnManager::lockstep_net_active() const {
	return net_host_ && net_host_->getRole() != NetRole::None && !is_replaying_;
}

static uint64_t rejoin_now_ms();

static constexpr uint8_t CMD_PEER_JOINED = static_cast<uint8_t>(NetCommandType::NET_CMD_PEER_JOINED);
static constexpr uint8_t CMD_PEER_LEFT = static_cast<uint8_t>(NetCommandType::NET_CMD_PEER_LEFT);

// Commands.js ProcessCommand calls the triggers' OnPlayerCommand before it runs the
// command; the tutorials look at gathering, building and rally points.
static void note_player_command(Simulation &sim, uint8_t pl, const NetCommand &c) {
	if (!sim.has_map_triggers()) return;
	uint16_t kind = TRIGGER_CMD_OTHER;
	uint32_t target = c.target_id, other = 0;
	uint8_t hunt = 0;
	uint16_t count = 0;
	const uint8_t param = (uint8_t)(c.param & ~NET_PARAM_QUEUED);
	switch (c.cmd_type) {
		case 4: kind = TRIGGER_CMD_GATHER; break;
		case 17:
			if (param == 0) { kind = TRIGGER_CMD_GATHER; hunt = 1; }
			break;
		case 5: kind = TRIGGER_CMD_CONSTRUCT; target = 0; count = c.param; break;
		case 16: kind = TRIGGER_CMD_REPAIR; break;
		case 14:
			if (param == 1) {
				kind = TRIGGER_CMD_UNSET_RALLY;
			} else {
				kind = TRIGGER_CMD_SET_RALLY;
				target = c.entity_ids.size() > 0 ? c.entity_ids[0] : 0;
				other = c.entity_ids.size() > 1 ? c.entity_ids[1] : 0;
			}
			break;
		case (uint8_t)NetCommandType::NET_CMD_TUTORIAL_READY: kind = TRIGGER_CMD_DIALOG_READY; break;
		default: break;
	}
	sim.note_player_command(pl, kind, target, other, hunt, count);
}

// A train command's batch size (target_z): a whole number 1..200, else one unit.
static int batch_count(float z) {
	return (z >= 1.0f && z <= 200.0f && z == (float)(int)z) ? (int)z : 1;
}

void TurnManager::ensure_lockstep_peers() {
	if (!lockstep_peers_set_) {
		lockstep_peers_.clear();
		lockstep_peers_.push_back(0);
		if (net_host_->isHost()) {
			for (const auto &p : net_host_->getPeers())
				if (p.connected && p.is_authenticated && !p.kick_disconnect_ms && !p.is_observer && p.id != 0 &&
						player_for_client(p.id) < MAX_PLAYERS)
					lockstep_peers_.push_back(p.id);
		} else {
			// Every seated human client (assign_slot_players numbers them 1, 2, ...
			// in slot order, as the host numbers joiners in connect order): their
			// batches reach us through the host's relay.
			for (const auto &cp : client_player_)
				if (cp.first != 0 && cp.second < MAX_PLAYERS) lockstep_peers_.push_back(cp.first);
			if (local_client_id_ != 0 && local_player() < MAX_PLAYERS &&
					std::find(lockstep_peers_.begin(), lockstep_peers_.end(), local_client_id_) == lockstep_peers_.end())
				lockstep_peers_.push_back(local_client_id_);
			std::sort(lockstep_peers_.begin() + 1, lockstep_peers_.end());
		}
		lockstep_peers_set_ = true;
		if (net_host_->isHost()) {
			// A human seat nobody connected to: clients would wait on it forever,
			// so tell them it is empty.
			for (const auto &cp : client_player_)
				if (cp.first != 0 && cp.second < MAX_PLAYERS &&
						std::find(lockstep_peers_.begin(), lockstep_peers_.end(), cp.first) == lockstep_peers_.end()) {
					NetCommand c;
					c.cmd_type = CMD_PEER_LEFT;
					c.target_id = cp.first;
					c.target_x = 0.0f;
					pending_control_.push_back(c);
				}
		}
	}
}

std::vector<uint32_t> TurnManager::waiting_on() {
	std::vector<uint32_t> out;
	const uint32_t turn = current_turn_ + 1;
	if (!lockstep_net_active() || turn <= COMMAND_DELAY) return out;
	ensure_lockstep_peers();
	auto it = received_batches_.find(turn);
	for (uint32_t id : lockstep_peers_) {
		auto since = lockstep_since_.find(id);
		if (since != lockstep_since_.end() && turn < since->second) continue;
		auto until = lockstep_until_.find(id);
		if (until != lockstep_until_.end() && turn > until->second) continue;
		if (it == received_batches_.end() || std::find(it->second.begin(), it->second.end(), id) == it->second.end())
			out.push_back(id);
	}
	for (const auto &o : observer_turn_)
		if (lagging_observer(o.first)) out.push_back(o.first);
	return out;
}

uint64_t TurnManager::waiting_ms() const {
	return stall_since_ms_ ? rejoin_now_ms() - stall_since_ms_ : 0;
}

bool TurnManager::peer_active(uint32_t id) const {
	return std::find(lockstep_peers_.begin(), lockstep_peers_.end(), id) != lockstep_peers_.end() &&
	       lockstep_until_.find(id) == lockstep_until_.end();
}

// Every peer, on the host's word: the lockstep waits for the client's batches up
// to last_turn (the host holds them, and relays them) and drops any after it.
void TurnManager::apply_peer_left(uint32_t client_id, uint32_t last_turn) {
	auto until = lockstep_until_.find(client_id);
	if (until != lockstep_until_.end() && until->second <= last_turn) return;
	lockstep_until_[client_id] = last_turn;
	paused_clients_.erase(client_id); // a paused client that leaves no longer holds the match
	// Its hashes for turns not yet run here are void: a batch it sent just
	// before leaving may never arrive, so the host runs those turns without
	// it (0 A.D. NetServerTurnManager::UninitialiseClient stops checking it).
	left_clients_.insert(client_id);
	// NetServerTurnManager::UninitialiseClient: once every out-of-sync client
	// has left, the host checks hashes again, so a later divergence (say, after
	// the player rejoined) is reported to everyone anew. The last report stays
	// readable until then.
	if (net_host_ && net_host_->isHost() && oos_reported_ && !oos_peers_.empty() &&
			std::all_of(oos_peers_.begin(), oos_peers_.end(), [&](uint32_t p) { return left_clients_.count(p) != 0; }))
		oos_rearmed_ = true;
	for (auto &t : peer_hashes_)
		if (t.first > current_turn_) t.second.erase(client_id);
	for (auto &t : turn_remote_cmds_) {
		if (t.first <= last_turn) continue;
		t.second.erase(std::remove_if(t.second.begin(), t.second.end(),
				[&](const NetCommand &c) { return c.client_id == client_id; }), t.second.end());
	}
	for (auto &t : received_batches_) {
		if (t.first <= last_turn) continue;
		t.second.erase(std::remove(t.second.begin(), t.second.end(), client_id), t.second.end());
	}
}

// A client, from the host's batch as it arrives (before the turn it affects can run).
void TurnManager::apply_peer_control(const NetCommand &c) {
	ensure_lockstep_peers();
	const uint32_t id = c.target_id;
	if (id != 0 && id == local_client_id_ && c.cmd_type == CMD_PEER_JOINED && c.target_z > 0.5f)
		rejoined_clients_.push_back(id); // the rejoiner hears it too
	if (id == 0 || id == local_client_id_) return;
	if (c.cmd_type == CMD_PEER_LEFT) {
		apply_peer_left(id, (uint32_t)c.target_x);
		std::cerr << "[TurnManager] client " << id << " left after turn " << (uint32_t)c.target_x << "\n";
	} else if (c.cmd_type == CMD_PEER_JOINED) {
		if (std::find(lockstep_peers_.begin(), lockstep_peers_.end(), id) == lockstep_peers_.end())
			lockstep_peers_.push_back(id);
		lockstep_since_[id] = (uint32_t)c.target_x;
		lockstep_until_.erase(id);
		left_clients_.erase(id);
		client_player_[id] = c.param;
		if (c.target_z > 0.5f) rejoined_clients_.push_back(id);
		std::cerr << "[TurnManager] client " << id << " takes player " << (unsigned)c.param
		          << " from turn " << (uint32_t)c.target_x << "\n";
	}
}

bool TurnManager::turn_ready(uint32_t turn) {
	if (turn <= COMMAND_DELAY) return true;
	ensure_lockstep_peers();
	auto it = received_batches_.find(turn);
	for (uint32_t id : lockstep_peers_) {
		auto since = lockstep_since_.find(id);
		if (since != lockstep_since_.end() && turn < since->second) continue; // rejoined later
		auto until = lockstep_until_.find(id);
		if (until != lockstep_until_.end() && turn > until->second) continue; // left
		if (it == received_batches_.end() ||
				std::find(it->second.begin(), it->second.end(), id) == it->second.end()) return false;
	}
	for (const auto &o : observer_turn_)
		if (lagging_observer(o.first)) return false;
	return true;
}

// 0 A.D. CNetServerTurnManager::CheckClientsReady: with network.observermaxlag
// set, the host does not start a turn while an observer is more than that
// many turns behind it. An observer counts from its first sync check, once it
// has loaded the world, as 0 A.D. counts it once it finished loading.
bool TurnManager::lagging_observer(uint32_t client) const {
	if (observer_max_lag_ < 0 || !net_host_ || !net_host_->isHost()) return false;
	auto it = observer_turn_.find(client);
	if (it == observer_turn_.end()) return false;
	const NetPeer *p = net_host_->getPeer(client);
	if (!p || !p->connected || !p->is_observer) return false;
	return current_turn_ > it->second + (uint32_t)observer_max_lag_;
}

uint32_t TurnManager::turns_behind() {
	uint32_t n = 0;
	while (n < 64 && received_batches_.count(current_turn_ + n + 1) && turn_ready(current_turn_ + n + 1)) ++n;
	return n;
}

void TurnManager::drop_client(uint32_t client_id) {
	if (client_id == local_client_id_ || client_id == 0) return;
	if (net_host_) ensure_lockstep_peers();
	paused_clients_.erase(client_id);
	observer_turn_.erase(client_id);
	rejoin_loading_.erase(client_id);
	if (peer_active(client_id)) {
		// The last turn whose batch from it is in hand: the lockstep still runs
		// those (every client gets them through the relay), and no later ones.
		uint32_t last = current_turn_;
		for (;;) {
			auto it = received_batches_.find(last + 1);
			if (it == received_batches_.end() ||
					std::find(it->second.begin(), it->second.end(), client_id) == it->second.end()) break;
			++last;
		}
		apply_peer_left(client_id, last);
		NetCommand c;
		c.cmd_type = CMD_PEER_LEFT;
		c.target_id = client_id;
		c.target_x = (float)last;
		pending_control_.push_back(c);
	}
	rejoin_requests_.erase(std::remove(rejoin_requests_.begin(), rejoin_requests_.end(), client_id), rejoin_requests_.end());
	rejoin_outbox_.erase(std::remove_if(rejoin_outbox_.begin() + (std::ptrdiff_t)rejoin_outbox_next_, rejoin_outbox_.end(),
			[&](const auto &e) { return e.first == client_id; }), rejoin_outbox_.end());
	std::cerr << "[TurnManager] client " << client_id << " left at turn " << current_turn_ << "\n";
}

static uint64_t rejoin_now_ms() {
	return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string TurnManager::rejoin_path(const char *tag) const {
	std::string dir = rejoin_dir_;
	if (dir.empty()) {
		std::error_code ec;
		dir = std::filesystem::temp_directory_path(ec).string();
		if (ec || dir.empty()) dir = ".";
	}
	return dir + "/rtso_rejoin_" + tag + "_" + std::to_string(local_client_id_) + ".sav";
}

bool TurnManager::is_observer() const {
	// A late observer, or a client seated with no player at NMT_GAME_START
	// (assigned to watch in game setup, or no human slot left for it).
	if (!net_host_ || net_host_->getRole() == NetRole::None || local_player() < MAX_PLAYERS) return false;
	return rejoined_ || net_game_started_;
}

void TurnManager::request_rejoin(bool observe) {
	if (!net_host_ || net_host_->isHost()) return;
	rejoin_wanted_ = true;
	rejoin_observe_ = observe;
	rejoin_have_header_ = false;
	rejoin_reject_ = 0;
	rejoin_world_.clear();
	rejoin_chunks_.clear();
	rejoin_early_.clear();
	rejoin_received_ = 0;
	rejoin_progress_ms_ = rejoin_now_ms();
	RejoinMsg req;
	req.kind = RejoinMsg::REQUEST;
	req.reason = observe ? 1 : 0;
	std::vector<uint8_t> payload = NetPacketSerializer::frame_rejoin(req);
	std::vector<uint8_t> pkt = NetPacketSerializer::pack_message(NetMessageType::NMT_REJOIN_STATE, 0, payload);
	net_host_->broadcastCommand(pkt);
}

void TurnManager::receive_rejoin(uint32_t author, const RejoinMsg &msg) {
	if (net_host_ && net_host_->isHost()) {
		if (msg.kind == RejoinMsg::LOADED && author != 0) {
			admit_rejoiner(author);
			return;
		}
		if (msg.kind != RejoinMsg::REQUEST || author == 0) return;
		if (msg.reason == 1) observer_requests_.insert(author);
		else observer_requests_.erase(author);
		// Still in game setup: the client asks to watch (0 A.D. assigns it no
		// player), so Start seats it as an observer.
		if (current_turn_ == 0 && !lockstep_peers_set_) net_host_->setPeerObserver(author, msg.reason == 1);
		if (std::find(rejoin_requests_.begin(), rejoin_requests_.end(), author) == rejoin_requests_.end())
			rejoin_requests_.push_back(author);
		return;
	}
	if (author == 0 && msg.kind == RejoinMsg::LOCKSTEP && rejoined_ && rejoin_lockstep_wait_) {
		// The lockstep waits on us from turn L. Turns before it ran without us;
		// any of L..current+COMMAND_DELAY we are already past sending for go
		// out empty now, so nobody waits.
		const uint32_t L = msg.turn;
		rejoin_lockstep_wait_ = false;
		lockstep_since_[local_client_id_] = L;
		if (std::find(lockstep_peers_.begin(), lockstep_peers_.end(), local_client_id_) == lockstep_peers_.end())
			lockstep_peers_.push_back(local_client_id_);
		for (uint32_t t = std::max(L, current_turn_ + 1); t <= current_turn_ + COMMAND_DELAY; ++t) {
			SimulationCommandMsg scm;
			scm.turn = t;
			scm.client_id = local_client_id_;
			std::vector<uint8_t> payload = NetPacketSerializer::frame_simulation_command(scm);
			net_host_->broadcastCommand(NetPacketSerializer::pack_message(NetMessageType::NMT_SIMULATION_COMMAND, t, payload));
			receive_remote_turn_batch(t, local_client_id_, {});
		}
		std::cerr << "[TurnManager] in the lockstep from turn " << L << " (at turn " << current_turn_ << ")\n";
		return;
	}
	if (author != 0 || !rejoin_wanted_ || rejoined_) return; // only the host's answer counts
	if (msg.kind == RejoinMsg::REJECT) {
		// No free seat yet: the host has not noticed the old client leave.
		// Keep waiting; apply_pending_rejoin asks again after REJOIN_RETRY_MS.
		if (msg.reason == 1) { rejoin_progress_ms_ = rejoin_now_ms(); return; }
		rejoin_reject_ = msg.reason ? msg.reason : 255;
		rejoin_wanted_ = false;
	} else if (msg.kind == RejoinMsg::HEADER) {
		if (msg.total == 0 || msg.total > 256u * 1024u * 1024u) return;
		// The header of an earlier request, delivered late, would throw away
		// the transfer that answers the newest one.
		// (rejoin_header_ outlives a repeated request; every T is >= 1.)
		if (msg.turn <= rejoin_header_.turn) return;
		rejoin_header_ = msg;
		rejoin_have_header_ = true;
		rejoin_progress_ms_ = rejoin_now_ms();
		rejoin_world_.assign(msg.total, 0);
		rejoin_chunks_.clear();
		rejoin_received_ = 0;
		std::vector<RejoinMsg> early;
		early.swap(rejoin_early_);
		for (const auto &c : early)
			if (c.turn == msg.turn) take_rejoin_chunk(c);
			else if (c.turn > msg.turn) rejoin_early_.push_back(c);
	} else if (msg.kind == RejoinMsg::CHUNK) {
		if (rejoin_have_header_ && msg.turn == rejoin_header_.turn) {
			take_rejoin_chunk(msg);
		} else if (msg.turn > rejoin_header_.turn &&
				rejoin_early_.size() < 256u * 1024u) {
			rejoin_early_.push_back(msg);
			rejoin_progress_ms_ = rejoin_now_ms();
		}
	}
}

void TurnManager::send_rejoin_msg(uint32_t to, const RejoinMsg &m) {
	std::vector<uint8_t> payload = NetPacketSerializer::frame_rejoin(m);
	std::vector<uint8_t> pkt = NetPacketSerializer::pack_message(NetMessageType::NMT_REJOIN_STATE, 0, payload);
	if (to == 0 && !net_host_->isHost()) net_host_->broadcastCommand(pkt); // a client's one link is the host
	else net_host_->sendCommand(to, pkt);
}

// Host: a rejoining player loaded the world (0 A.D. NetServer OnLoadedGame
// adds it to the turn manager). The lockstep waits on it from L, the turn
// after the batch that tells everyone (CMD_PEER_JOINED).
void TurnManager::admit_rejoiner(uint32_t client_id) {
	auto it = rejoin_loading_.find(client_id);
	if (it == rejoin_loading_.end()) return;
	const uint8_t seat = it->second;
	rejoin_loading_.erase(it);
	ensure_lockstep_peers();
	const uint32_t L = current_turn_ + COMMAND_DELAY + 1;
	if (std::find(lockstep_peers_.begin(), lockstep_peers_.end(), client_id) == lockstep_peers_.end())
		lockstep_peers_.push_back(client_id);
	lockstep_since_[client_id] = L;
	NetCommand c;
	c.cmd_type = CMD_PEER_JOINED;
	c.target_id = client_id;
	c.param = seat;
	c.target_x = (float)L;
	// 0 A.D. NMT_REJOINED: everyone hears "has rejoined the game.", except for
	// the clients of a saved game, which load it at Start as a new game does.
	const bool rejoined = !saved_waiting_.count(client_id);
	c.target_z = rejoined ? 1.0f : 0.0f;
	pending_control_.push_back(c);
	if (rejoined) rejoined_clients_.push_back(client_id);
	RejoinMsg in;
	in.kind = RejoinMsg::LOCKSTEP;
	in.turn = L;
	send_rejoin_msg(client_id, in);
	std::cerr << "[TurnManager] client " << client_id << " loaded; in the lockstep from turn " << L << "\n";
}

void TurnManager::take_rejoin_chunk(const RejoinMsg &msg) {
	if (msg.total > rejoin_world_.size() || msg.bytes.size() > rejoin_world_.size() - msg.total) return;
	if (!rejoin_chunks_.insert(msg.total).second) return; // a resent chunk
	std::memcpy(rejoin_world_.data() + msg.total, msg.bytes.data(), msg.bytes.size());
	rejoin_received_ += msg.bytes.size();
	rejoin_progress_ms_ = rejoin_now_ms();
}

// Host: the match's clients as 0 A.D.'s NMT_PLAYER_ASSIGNMENT lists them. Sent
// again only when something changes (a join, a leave, a seat handed over).
void TurnManager::send_player_assignment() {
	ensure_lockstep_peers();
	std::vector<PlayerAssignmentMsg::Entry> list;
	PlayerAssignmentMsg::Entry me;
	me.client = 0;
	me.name = net_host_->localPlayerName();
	me.player = player_for_client(0);
	me.status = me.player < MAX_PLAYERS ? PlayerAssignmentMsg::PLAYING : PlayerAssignmentMsg::OBSERVER;
	list.push_back(me);
	std::set<uint32_t> listed{0};
	for (const auto &p : net_host_->getPeers()) {
		if (p.id == 0 || !p.connected || !p.is_authenticated || p.kick_disconnect_ms) continue;
		client_names_[p.id] = p.player_name;
		const uint8_t pl = player_for_client(p.id);
		PlayerAssignmentMsg::Entry e;
		e.client = p.id;
		e.name = p.player_name;
		if (pl < MAX_PLAYERS && peer_active(p.id)) { e.player = pl; e.status = PlayerAssignmentMsg::PLAYING; }
		// An observer once the host let it watch (game setup or a served late
		// join); a client still waiting on its rejoin answer is not listed yet.
		else if (pl >= MAX_PLAYERS && client_player_.count(p.id) && p.is_observer) e.status = PlayerAssignmentMsg::OBSERVER;
		else continue; // still joining
		list.push_back(e);
		listed.insert(p.id);
	}
	// Seats whose client has gone (and nobody took over yet).
	for (const auto &cp : client_player_) {
		if (cp.second >= MAX_PLAYERS || listed.count(cp.first)) continue;
		bool held = false;
		for (const auto &e : list) if (e.player == cp.second) held = true;
		if (held) continue;
		auto nm = client_names_.find(cp.first);
		if (nm == client_names_.end()) continue; // a seat nobody ever took
		PlayerAssignmentMsg::Entry e;
		e.client = cp.first;
		e.name = nm->second;
		e.player = cp.second;
		e.status = PlayerAssignmentMsg::LEFT;
		list.push_back(e);
	}
	std::sort(list.begin(), list.end(), [](const auto &a, const auto &b) { return a.client < b.client; });
	if (list == sent_assignment_) return;
	sent_assignment_ = list;
	PlayerAssignmentMsg m;
	m.clients = std::move(list);
	net_host_->sendPlayerAssignment(m);
}

// Host, at a turn boundary (the world is the state after turn T = current_turn_).
void TurnManager::serve_rejoin(uint32_t client_id, Simulation &sim) {
	auto reject = [&](uint8_t reason) {
		RejoinMsg r;
		r.kind = RejoinMsg::REJECT;
		r.reason = reason;
		std::vector<uint8_t> payload = NetPacketSerializer::frame_rejoin(r);
		net_host_->sendCommand(client_id, NetPacketSerializer::pack_message(NetMessageType::NMT_REJOIN_STATE, 0, payload));
	};
	if (!net_game_started() || current_turn_ == 0) { reject(2); return; }
	ensure_lockstep_peers();
	// A client the saved game was started with takes the seat it was given.
	auto saved_seat = saved_seats_.find(client_id);
	const bool saved_client = saved_seat != saved_seats_.end() && !lockstep_since_.count(client_id);
	// A player seated at Start that still plays: its request from game setup
	// came late (a slow load outlasts REJOIN_RETRY_MS, so it asks again). It
	// is in the match already; serving it would drop it from the lockstep.
	if (!saved_client && peer_active(client_id) && !lockstep_since_.count(client_id) && !observer_requests_.count(client_id)) {
		reject(2);
		return;
	}
	// A repeated request (the transfer stalled): start over from this turn.
	lockstep_peers_.erase(std::remove(lockstep_peers_.begin(), lockstep_peers_.end(), client_id), lockstep_peers_.end());
	lockstep_since_.erase(client_id);
	client_player_.erase(client_id);
	rejoin_loading_.erase(client_id);
	rejoin_outbox_.erase(std::remove_if(rejoin_outbox_.begin() + (std::ptrdiff_t)rejoin_outbox_next_, rejoin_outbox_.end(),
			[&](const auto &e) { return e.first == client_id; }), rejoin_outbox_.end());
	// 0 A.D. NetServer (HandleConnect, OnAuthenticate): once the game runs, a
	// client that played and left gets its own player back, matched by name;
	// one that watched and left watches again. Anyone else may only watch, as
	// network.lateobservers allows; it never takes a free seat.
	auto lower = [](std::string n) {
		for (char &ch : n) ch = (char)std::tolower((unsigned char)ch);
		return n;
	};
	const NetPeer *asker = net_host_->getPeer(client_id);
	const std::string name = lower(asker ? asker->player_name : std::string());
	uint8_t seat = 0xFF;
	bool known_observer = false;
	for (const auto &cn : client_names_) {
		if (cn.first == client_id || cn.first == 0 || peer_active(cn.first) || lower(cn.second) != name) continue;
		auto cp = client_player_.find(cn.first);
		if (cp == client_player_.end() || cp->second >= MAX_PLAYERS) { known_observer = true; continue; }
		bool held = false;
		for (const auto &other : client_player_)
			if (other.second == cp->second && peer_active(other.first)) held = true;
		if (!held && (seat == 0xFF || cp->second < seat)) seat = cp->second;
	}
	if (saved_client) {
		seat = observer_requests_.count(client_id) ? 0xFF : saved_seat->second;
		known_observer = true; // seated by the host at Start, not a late observer
	}
	const bool observer = seat == 0xFF;
	if (observer) observer_requests_.insert(client_id);
	else observer_requests_.erase(client_id);
	if (observer && !known_observer && late_observers_ != LATE_OBSERVERS_EVERYONE) {
		// 0 A.D. NetServer: lateobservers "buddies" admits the names in the
		// host's lobby.buddies, "disabled" nobody.
		if (late_observers_ == LATE_OBSERVERS_DISABLED || !buddies_.count(name)) {
			reject(6);
			// 0.28 NetServer: "Game has already started, no observers allowed."
			net_host_->refusePeer(client_id, NDR_SERVER_ALREADY_IN_GAME);
			return;
		}
	}
	if (observer) {
		// 0 A.D. NetServer: the sessions beyond the players may not pass
		// network.observerlimit once the game runs.
		int watching = 0;
		for (const auto &p : net_host_->getPeers())
			if (p.id != client_id && p.connected && p.is_authenticated && p.is_observer) ++watching;
		if (watching + 1 > observer_limit_) { reject(7); net_host_->refusePeer(client_id, NDR_SERVER_FULL); return; }
	}
	const std::string path = rejoin_path("host");
	std::vector<uint8_t> world;
	if (sim.save_state(path.c_str())) {
		std::ifstream in(path, std::ios::binary);
		world.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	}
	std::remove(path.c_str());
	if (world.empty()) { reject(3); return; }

	const uint32_t T = current_turn_;
	client_player_[client_id] = seat;
	lockstep_until_.erase(client_id);
	left_clients_.erase(client_id);
	net_host_->setPeerObserver(client_id, observer);
	// A player joins the lockstep once it has loaded (admit_rejoiner).
	if (!observer) rejoin_loading_[client_id] = seat;
	// 0 A.D. NetServer OnRejoined: the rejoiner is told who is pausing
	// (it keeps the list through its rejoin reset; later changes reach it as
	// they reach everyone).
	for (uint32_t who : paused_clients_) {
		ClientPausedMsg cpm;
		cpm.guid = std::to_string(who);
		cpm.pause = 1;
		std::vector<uint8_t> payload = NetPacketSerializer::frame_client_paused(cpm);
		net_host_->sendCommand(client_id,
				NetPacketSerializer::pack_message(NetMessageType::NMT_CLIENT_PAUSED, current_turn_, payload));
	}

	RejoinMsg hdr;
	hdr.kind = RejoinMsg::HEADER;
	hdr.turn = T;
	hdr.total = (uint32_t)world.size();
	auto &b = hdr.bytes;
	NetSerde::write_u32(b, client_id);
	NetSerde::write_u8(b, seat);
	NetSerde::write_u8(b, (uint8_t)std::min<size_t>(client_player_.size(), 255));
	std::vector<std::pair<uint32_t, uint8_t>> seats(client_player_.begin(), client_player_.end());
	std::sort(seats.begin(), seats.end());
	for (size_t i = 0; i < seats.size() && i < 255; ++i) { NetSerde::write_u32(b, seats[i].first); NetSerde::write_u8(b, seats[i].second); }
	std::vector<uint32_t> active;
	for (uint32_t id : lockstep_peers_) if (peer_active(id)) active.push_back(id);
	NetSerde::write_u8(b, (uint8_t)std::min<size_t>(active.size(), 255));
	for (size_t i = 0; i < active.size() && i < 255; ++i) {
		const uint32_t id = active[i];
		auto since = lockstep_since_.find(id);
		NetSerde::write_u32(b, id);
		NetSerde::write_u32(b, since == lockstep_since_.end() ? 0 : since->second);
	}
	// Every batch already in hand for a turn after T, per sender: the rejoiner
	// never saw these broadcasts, and both sides must run exactly them.
	std::vector<std::vector<uint8_t>> batches;
	std::vector<uint32_t> turns;
	for (const auto &rb : received_batches_) if (rb.first > T) turns.push_back(rb.first);
	std::sort(turns.begin(), turns.end());
	for (uint32_t t : turns) {
		std::vector<uint32_t> senders = received_batches_[t];
		std::sort(senders.begin(), senders.end());
		for (uint32_t sender : senders) {
			SimulationCommandMsg scm;
			scm.turn = t;
			scm.client_id = sender;
			auto it = turn_remote_cmds_.find(t);
			if (it != turn_remote_cmds_.end())
				for (const auto &c : it->second) if (c.client_id == sender) scm.commands.push_back(c);
			batches.push_back(NetPacketSerializer::frame_simulation_command(scm));
		}
	}
	NetSerde::write_u16(b, (uint16_t)std::min<size_t>(batches.size(), 65535));
	for (size_t i = 0; i < batches.size() && i < 65535; ++i) {
		NetSerde::write_u32(b, (uint32_t)batches[i].size());
		b.insert(b.end(), batches[i].begin(), batches[i].end());
	}
	auto send = [&](const RejoinMsg &m) {
		std::vector<uint8_t> payload = NetPacketSerializer::frame_rejoin(m);
		net_host_->sendCommand(client_id, NetPacketSerializer::pack_message(NetMessageType::NMT_REJOIN_STATE, T, payload));
	};
	send(hdr);
	for (size_t off = 0; off < world.size(); off += RejoinMsg::CHUNK_BYTES) {
		RejoinMsg c;
		c.kind = RejoinMsg::CHUNK;
		c.turn = T;
		c.total = (uint32_t)off;
		c.bytes.assign(world.begin() + off, world.begin() + std::min(world.size(), off + RejoinMsg::CHUNK_BYTES));
		rejoin_outbox_.emplace_back(client_id, NetPacketSerializer::pack_message(
				NetMessageType::NMT_REJOIN_STATE, T, NetPacketSerializer::frame_rejoin(c)));
	}
	std::cerr << "[TurnManager] rejoin: client " << client_id << " takes player " << (unsigned)seat
	          << " at turn " << T << " (" << world.size() << " bytes, " << batches.size() << " batches)\n";
}

void TurnManager::start_saved_game(const std::map<uint32_t, uint8_t> &seats, const Simulation *sim) {
	saved_game_ = true;
	saved_seats_.clear();
	saved_waiting_.clear();
	client_player_.clear();
	auto host = seats.find(0);
	client_player_[0] = host == seats.end() ? 0xFF : host->second;
	for (const auto &s : seats) {
		if (s.first == 0) continue;
		saved_seats_[s.first] = s.second;
		saved_waiting_.insert(s.first);
	}
	// Nobody else is in the lockstep until it has loaded (admit_rejoiner).
	set_lockstep_peers({0});
	// Turn 1 is the saved world as sent: every rejoin world is from turn >= 1.
	current_turn_ = 1;
	saved_since_ms_ = rejoin_now_ms();
	mark_game_started(sim);
	// Our batches for the turns up to COMMAND_DELAY ahead, which a match from
	// turn 0 never needs: empty, as nothing was ordered yet.
	for (uint32_t t = current_turn_ + 1; t <= current_turn_ + COMMAND_DELAY; ++t) {
		if (t <= COMMAND_DELAY) continue;
		SimulationCommandMsg scm;
		scm.turn = t;
		scm.client_id = local_client_id_;
		std::vector<uint8_t> payload = NetPacketSerializer::frame_simulation_command(scm);
		net_host_->broadcastCommand(NetPacketSerializer::pack_message(NetMessageType::NMT_SIMULATION_COMMAND, t, payload));
		receive_remote_turn_batch(t, local_client_id_, {});
	}
	std::cerr << "[TurnManager] saved game: waiting for " << saved_waiting_.size() << " client(s) to load\n";
}

// Host: hold the saved game's first turn while a client it started with is
// still loading. One that never asks or left is not waited for.
bool TurnManager::saved_game_hold() {
	if (saved_waiting_.empty()) return false;
	for (auto it = saved_waiting_.begin(); it != saved_waiting_.end();) {
		const NetPeer *p = net_host_ ? net_host_->getPeer(*it) : nullptr;
		auto seat = client_player_.find(*it);
		const bool watching_served = seat != client_player_.end() && seat->second == 0xFF;
		if (!p || !p->connected || lockstep_since_.count(*it) || watching_served) it = saved_waiting_.erase(it);
		else ++it;
	}
	if (!saved_waiting_.empty() && rejoin_now_ms() - saved_since_ms_ > SAVED_GAME_WAIT_MS) {
		std::cerr << "[TurnManager] saved game: starting without " << saved_waiting_.size() << " client(s)\n";
		saved_waiting_.clear();
	}
	return !saved_waiting_.empty();
}

bool TurnManager::apply_pending_rejoin(Simulation &sim) {
	if (!rejoin_wanted_ || rejoined_) return false;
	if (!rejoin_have_header_ || rejoin_received_ != rejoin_world_.size()) {
		// Chunks held behind a lost one are progress: the resend is on its way.
		if (net_host_ && net_host_->held_count(0)) rejoin_progress_ms_ = rejoin_now_ms();
		if (rejoin_now_ms() - rejoin_progress_ms_ > REJOIN_RETRY_MS) {
			std::cerr << "[TurnManager] rejoin transfer stalled; asking the host again\n";
			request_rejoin(rejoin_observe_);
		}
		return false;
	}
	const std::string path = rejoin_path("client");
	{
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		out.write((const char *)rejoin_world_.data(), (std::streamsize)rejoin_world_.size());
	}
	const bool loaded = sim.load_state(path.c_str());
	std::remove(path.c_str());
	if (!loaded) { rejoin_wanted_ = false; rejoin_reject_ = 4; return false; }

	const uint8_t *d = rejoin_header_.bytes.data();
	const size_t n = rejoin_header_.bytes.size();
	size_t off = 0;
	uint32_t me = 0;
	uint8_t seat = 0, count = 0;
	bool ok = NetSerde::read_u32(d, n, off, me) && NetSerde::read_u8(d, n, off, seat) && NetSerde::read_u8(d, n, off, count);
	std::unordered_map<uint32_t, uint8_t> seats;
	for (uint8_t i = 0; ok && i < count; ++i) {
		uint32_t c = 0; uint8_t pl = 0;
		ok = NetSerde::read_u32(d, n, off, c) && NetSerde::read_u8(d, n, off, pl);
		seats[c] = pl;
	}
	std::vector<uint32_t> peers;
	std::unordered_map<uint32_t, uint32_t> since;
	ok = ok && NetSerde::read_u8(d, n, off, count);
	for (uint8_t i = 0; ok && i < count; ++i) {
		uint32_t id = 0, from = 0;
		ok = NetSerde::read_u32(d, n, off, id) && NetSerde::read_u32(d, n, off, from);
		peers.push_back(id);
		if (from) since[id] = from;
	}
	uint16_t nb = 0;
	std::vector<SimulationCommandMsg> batches;
	ok = ok && NetSerde::read_u16(d, n, off, nb);
	for (uint16_t i = 0; ok && i < nb; ++i) {
		uint32_t len = 0;
		ok = NetSerde::read_u32(d, n, off, len) && len <= n - off;
		SimulationCommandMsg scm;
		ok = ok && NetPacketSerializer::parse_simulation_command(d + off, len, scm);
		off += len;
		batches.push_back(std::move(scm));
	}
	if (!ok) { rejoin_wanted_ = false; rejoin_reject_ = 5; return false; }

	// Batches broadcast to us after we connected (for turns after T) survive;
	// anything for T or earlier is history we just loaded.
	const uint32_t T = rejoin_header_.turn;
	reset_keep_inbound();
	for (auto it = received_batches_.begin(); it != received_batches_.end();)
		it = it->first <= T ? received_batches_.erase(it) : std::next(it);
	for (auto it = turn_remote_cmds_.begin(); it != turn_remote_cmds_.end();)
		it = it->first <= T ? turn_remote_cmds_.erase(it) : std::next(it);
	current_turn_ = T;
	local_client_id_ = me;
	client_player_ = std::move(seats);
	lockstep_peers_ = std::move(peers);
	lockstep_peers_set_ = true;
	lockstep_since_ = std::move(since);
	for (const auto &scm : batches) receive_remote_turn_batch(scm.turn, scm.client_id, scm.commands);
	note_game_start_applied(&sim);
	rejoined_ = true;
	rejoin_fast_forward_ = true;
	rejoin_turn_ = T;
	if (seat < MAX_PLAYERS) {
		// 0 A.D. NMT_LOADED_GAME: the host now lets the lockstep wait on us.
		rejoin_lockstep_wait_ = true;
		RejoinMsg loaded;
		loaded.kind = RejoinMsg::LOADED;
		loaded.turn = T;
		send_rejoin_msg(0, loaded);
	}
	rejoin_world_.clear();
	rejoin_world_.shrink_to_fit();
	std::cerr << "[TurnManager] rejoined as client " << me << " (player " << (unsigned)seat << ") at turn " << T << "\n";
	return true;
}

// 0 A.D. gamesetup: observers take no slot, a client the host assigned a human
// slot takes it, and the rest take the free human slots in slot order.
void TurnManager::seat_clients(const GameSetupParams &setup, uint8_t out[8]) {
	bool taken[8] = {};
	bool seated[8] = {};
	for (uint32_t client = 0; client < 8; ++client) {
		const uint8_t slot = setup.seats[client];
		out[client] = 0xFF;
		if ((setup.observers >> client) & 1) seated[client] = true;
		else if (slot < setup.slot_count && slot < 8 && setup.slot_types[slot] == 'h' && !taken[slot]) {
			out[client] = slot;
			taken[slot] = seated[client] = true;
		}
	}
	uint8_t slot = 0;
	for (uint32_t client = 0; client < 8; ++client) {
		if (seated[client]) continue;
		while (slot < setup.slot_count && slot < 8 && (setup.slot_types[slot] != 'h' || taken[slot])) ++slot;
		if (slot < setup.slot_count && slot < 8) { out[client] = slot; taken[slot] = true; }
	}
}

void TurnManager::assign_slot_players(const GameSetupParams &setup) {
	uint8_t seats[8];
	seat_clients(setup, seats);
	client_player_.clear();
	for (uint32_t client = 0; client < 8; ++client) client_player_[client] = seats[client];
}

void TurnManager::reset() {
	client_player_.clear();
	current_turn_ = 0;
	turn_timer_ = 0.0f;
	observer_fast_ = false;
	rejoin_fast_forward_ = false;
	pending_local_cmds_.clear();
	pending_remote_cmds_.clear();
	turn_remote_cmds_.clear();
	peer_hashes_.clear();
	received_batches_.clear();
	lockstep_peers_.clear();
	lockstep_peers_set_ = false;
	waiting_ticks_ = 0;
	turn_history_.clear();
	has_pending_game_start_ = false;
	pending_game_start_ = GameSetupParams();
	is_replaying_ = false;
	replay_frame_idx_ = 0;
	replay_loaded_ = false;
	replay_complete_ = false;
	replay_oos_ = false;
	replay_oos_turn_ = 0;
	replay_expected_hash_ = 0;
	replay_computed_hash_ = 0;
	last_replay_error_.clear();
	oos_detected_ = false;
	oos_turn_ = 0;
	oos_players_.clear();
	oos_reported_ = false;
	oos_rearmed_ = false;
	oos_from_host_ = false;
	oos_dump_pending_ = false;
	oos_dump_written_.clear();
	paused_clients_.clear();
	left_clients_.clear();
	lockstep_until_.clear();
	pending_control_.clear();
	stall_since_ms_ = 0;
	lockstep_since_.clear();
	rejoin_requests_.clear();
	rejoin_loading_.clear();
	saved_game_ = false;
	saved_seats_.clear();
	saved_waiting_.clear();
	saved_since_ms_ = 0;
	rejoin_lockstep_wait_ = false;
	observer_requests_.clear();
	observer_turn_.clear();
	rejoin_observe_ = false;
	sent_assignment_.clear();
	client_names_.clear();
	rejoin_outbox_.clear();
	rejoin_outbox_next_ = 0;
	rejoin_header_ = RejoinMsg();
	rejoin_early_.clear();
	oos_expected_hash_ = 0;
	oos_received_hash_ = 0;
	oos_peers_.clear();
	net_game_started_ = false;
	apply_base_turn_ = 0;
	gated_ticks_ = 0;
	boot_hash_ = 0;
	boot_hash_captured_ = false;
	last_game_start_attributes_.clear();
	join_sync_round_trips_ = 0;
	join_sync_accepted_ = false;
	join_sync_error_ = 0;
	join_sync_turn_ = 0;
	join_sync_hash_ = 0;
	join_sync_mismatch_ = false;
}

void TurnManager::set_net_host(NetHost *net_host, uint32_t client_id) {
	net_host_ = net_host;
	local_client_id_ = client_id;
	if (net_host_) {
		// A HOST owns its world from the moment it binds: it is the authority, it
		// applies its own setup locally at Start (apply_game_setup) and
		// mark_game_started() records that. Only a CLIENT must WAIT for an
		// NMT_GAME_START, because until it does it is ticking its own boot world.
		net_game_started_ = net_host_->isHost();
		boot_hash_captured_ = false;
		net_host_->onPeerDisconnected([this](uint32_t peer_id) {
			if (net_host_ && net_host_->isHost()) this->drop_client(peer_id);
		});
		net_host_->onCommand([this](uint32_t peer_id, const uint8_t *data, size_t size) {
			SimulationCommandMsg scm;
			if (NetPacketSerializer::parse_simulation_command(data, size, scm)) {
				// The host knows who sent it; a client hears the other clients
				// only through the host, which stamps each relayed batch.
				const bool host = net_host_ && net_host_->isHost();
				const uint32_t from = host && peer_id != 0 ? peer_id : (scm.client_id != 0 ? scm.client_id : peer_id);
				if (this->receive_remote_turn_batch(scm.turn, from, scm.commands) && host && from != 0) {
					// 0 A.D. NetServerTurnManager::NotifyFinishedClientCommands: the
					// server forwards every client's commands to all the others.
					scm.client_id = from;
					std::vector<uint8_t> payload = NetPacketSerializer::frame_simulation_command(scm);
					net_host_->broadcastCommand(NetPacketSerializer::pack_message(
							NetMessageType::NMT_SIMULATION_COMMAND, scm.turn, payload), from);
				}
			}
		});

		// G9: the sender id is resolved by NetHost (see NetHost::resolve_sender_id)
		// before this callback runs -- peer_id 0 is ambiguous on the wire, so a
		// bare `peer_id` here is not an authoritative sender id.
		net_host_->onMessage([this](uint32_t peer_id, const NetMessage &msg) {
			// NMT_SIMULATION_COMMAND is NOT handled here: NetHost::dispatchMessage
			// already hands it to the onCommand callback above, and handling it in
			// both places queued every remote command twice.
			if (msg.type == static_cast<uint8_t>(NetMessageType::NMT_SYNC_CHECK)) {
				SyncCheckMsg scm;
				if (NetPacketSerializer::parse_sync_check(msg.payload.data(), msg.payload.size(), scm)) {
					this->receive_sync_check(scm.turn != 0 ? scm.turn : msg.turn, peer_id, scm.hash);
				}
			} else if (msg.type == static_cast<uint8_t>(NetMessageType::NMT_SYNC_ERROR)) {
				SyncErrorMsg sem;
				// Only the host reports an OOS (0 A.D. NetServerTurnManager).
				if (peer_id == 0 && net_host_ && !net_host_->isHost() &&
						NetPacketSerializer::parse_sync_error(msg.payload.data(), msg.payload.size(), sem))
					this->receive_sync_error(sem);
			} else if (msg.type == static_cast<uint8_t>(NetMessageType::NMT_REJOIN_STATE)) {
				RejoinMsg rm;
				if (NetPacketSerializer::parse_rejoin(msg.payload.data(), msg.payload.size(), rm))
					this->receive_rejoin(peer_id, rm);
			} else if (msg.type == static_cast<uint8_t>(NetMessageType::NMT_CLIENT_PAUSED)) {
				ClientPausedMsg cpm;
				if (NetPacketSerializer::parse_client_paused(msg.payload.data(), msg.payload.size(), cpm))
					this->receive_client_paused(peer_id, cpm);
			} else if (msg.type == static_cast<uint8_t>(NetMessageType::NMT_GAME_START)) {
				GameStartMsg gsm;
				if (NetPacketSerializer::parse_game_start(msg.payload.data(), msg.payload.size(), gsm)) {
					GameSetupParams setup;
					if (parse_game_start_full(gsm.init_attributes, setup)) {
						this->has_pending_game_start_ = true;
						this->pending_game_start_ = setup;
						// Keep the verbatim attributes so a late joiner can be
						// answered with a byte-identical resend (P3). The strings
						// agree because parse_game_start_full drops only unknown
						// keys and reorders known ones.
						this->last_game_start_attributes_ = gsm.init_attributes;
					} else {
						std::cerr << "[TurnManager] NMT_GAME_START with bad attributes: "
						          << gsm.init_attributes << "\n";
					}
				}
			} else if (msg.type == static_cast<uint8_t>(NetMessageType::NMT_JOIN_SYNC_STATE)) {
				// One id, two payload shapes (see net_types.h): a 5-byte
				// ROLE_JOINER request, or an 11-byte ROLE_HOST reply. Try the
				// request first, then the reply -- never both, so a message can
				// never be interpreted twice.
				JoinSyncMsg jsm;
				if (NetPacketSerializer::parse_join_sync(msg.payload.data(), msg.payload.size(), jsm)) {
					// ROLE_JOINER: a request. The host answers from NetHost's
					// dispatch branch; nothing to do on this side.
				} else {
					TurnStateSnapshotMsg snap;
					if (!NetPacketSerializer::parse_join_sync_snapshot(msg.payload.data(), msg.payload.size(), snap)) {
						std::cerr << "[TurnManager] NMT_JOIN_SYNC_STATE payload is neither a"
						          << " 5-byte request nor an 11-byte host reply (" << msg.payload.size()
						          << " bytes)\n";
					} else {
						this->receive_join_sync(peer_id, snap);
					}
				}
			} else if (msg.type == static_cast<uint8_t>(NetMessageType::NMT_JOIN_SYNC_CHECK)) {
				JoinSyncCheckMsg c;
				if (NetPacketSerializer::parse_join_sync_check(msg.payload.data(), msg.payload.size(), c))
					this->receive_join_sync_check(peer_id, c);
			}
		});

		// Host half of the late-join resend. NetHost calls this for an admitted
		// peer that asked for setup + turn-0 state (see dispatchMessage).
		net_host_->onJoinSyncRequest([this](uint32_t requester_peer_id, uint32_t requested_turn,
		                                   uint32_t &sync_turn, uint32_t &sync_hash) {
			return this->answer_join_sync(requester_peer_id, requested_turn, sync_turn, sync_hash);
		});
	}
}

void TurnManager::post_local_command(const NetCommand &cmd) {
	if (is_replaying_) return;
	NetCommand c = cmd;
	c.client_id = local_client_id_;
	pending_local_cmds_.push_back(c);
}

void TurnManager::post_remote_command(uint32_t client_id, const NetCommand &cmd) {
	if (is_replaying_) return;
	NetCommand c = cmd;
	c.client_id = client_id;
	pending_remote_cmds_.push_back(c);
}

void TurnManager::post_remote_commands(uint32_t client_id, const std::vector<NetCommand> &cmds) {
	if (is_replaying_) return;
	for (auto c : cmds) {
		c.client_id = client_id;
		pending_remote_cmds_.push_back(c);
	}
}

bool TurnManager::receive_remote_turn_batch(uint32_t turn, uint32_t client_id, const std::vector<NetCommand> &cmds) {
	if (is_replaying_) return false;
	// The host takes no batches from a client without a seat (an observer), so
	// none reach the other clients either.
	if (client_id != 0 && net_host_ && net_host_->isHost() && player_for_client(client_id) >= MAX_PLAYERS) return false;
	auto until = lockstep_until_.find(client_id);
	if (until != lockstep_until_.end() && turn > until->second) return false; // sent after it left
	// One batch per (turn, client): a retransmitted or doubly-delivered batch
	// must not queue its commands twice.
	if (turn <= current_turn_) {
		// Gating makes this unreachable for a lockstep peer (a retransmitted
		// batch for a turn already run is the benign case). Its commands can no
		// longer run on the turn the sender ran them, so do not queue them.
		std::cerr << "[TurnManager] late batch for turn " << turn << " from client " << client_id
		          << " (current " << current_turn_ << ")\n";
		return false;
	}
	auto &got = received_batches_[turn];
	if (std::find(got.begin(), got.end(), client_id) != got.end()) return false;
	got.push_back(client_id);
	if (client_id == 0 && net_host_ && !net_host_->isHost())
		for (const auto &c : cmds)
			if (c.cmd_type == CMD_PEER_JOINED || c.cmd_type == CMD_PEER_LEFT) apply_peer_control(c);
	for (auto c : cmds) {
		c.client_id = client_id;
		turn_remote_cmds_[turn].push_back(c);
	}
	return true;
}

void TurnManager::receive_sync_check(uint32_t turn, uint32_t client_id, uint32_t hash) {
	if (left_clients_.count(client_id)) return;
	peer_hashes_[turn][client_id] = hash;
	if (net_host_ && net_host_->isHost() && client_id != 0) {
		auto cp = client_player_.find(client_id);
		if (cp != client_player_.end() && cp->second >= MAX_PLAYERS) {
			uint32_t &t = observer_turn_[client_id];
			t = std::max(t, turn);
		}
	}
	if (turn <= current_turn_ && turn > 0) {
		for (const auto &f : turn_history_) {
			if (f.turn_index == turn) {
				if (f.sim_state_hash != 0 && f.sim_state_hash != hash)
					note_out_of_sync(turn, f.sim_state_hash, hash, client_id);
				break;
			}
		}
	}
}

uint32_t TurnManager::compute_sim_hash(const Simulation &sim) const {
	// Phase4 HashSerializer parity — comprehensive FNV-1a state hashing for lockstep desync verification.
	// Hashes all unit SoA columns (pos_x, pos_z, hp, action_state, unit_type, team, carry, kills, rank, status_effects),
	// buildings (hp, build_progress, type, team, capture_points), resource nodes (amount, alive, type),
	// the per-player table (stockpiles, researched tech masks, phase, civ, team),
	// active projectiles, relics, territory grid, and market prices.
	uint32_t hash = 2166136261u;
	auto mix_u32 = [&](uint32_t v) { hash = (hash ^ v) * 16777619u; };
	auto mix_float = [&](float f) { uint32_t bits; std::memcpy(&bits, &f, sizeof(float)); mix_u32(bits); };
	auto mix_i32 = [&](int32_t v) { mix_u32((uint32_t)v); };
	auto mix_u8 = [&](uint8_t v) { mix_u32((uint32_t)v); };
	auto mix_u16 = [&](uint16_t v) { mix_u32((uint32_t)v); };

	// 1. Units SoA — hash all unit SoA columns deterministically
	auto uview = sim.units();
	mix_u32((uint32_t)uview.count);
	for (size_t i = 0; i < uview.count; ++i) {
		mix_u8(uview.alive[i]);
		if (!uview.alive[i]) continue;
		mix_u32(uview.id[i]);
		mix_float(uview.pos_x[i]);
		mix_float(uview.pos_z[i]);
		mix_float(uview.vel_x[i]);
		mix_float(uview.vel_z[i]);
		mix_float(uview.target_x[i]);
		mix_float(uview.target_z[i]);
		mix_float(uview.hp[i]);
		mix_float(uview.max_hp[i]);
		mix_u8(uview.action_state[i]);
		mix_u8(uview.unit_type[i]);
		mix_u8(uview.team[i]);
		mix_i32(uview.carry[i]);
		mix_u8(uview.res_type_carried[i]);
		mix_u8(uview.stance[i]);
		mix_u8(uview.formation[i]);
		mix_u16(uview.kills[i]);
		mix_u8(uview.rank[i]);
		mix_u8(uview.status_effects[i]);
		mix_float(uview.death_timer[i]);
		mix_u8(uview.pack_state[i]);
		mix_float(uview.pack_elapsed[i]);
	}

	// 2. Buildings — hp, build_progress, type, team, capture_points, coords, queues
	const auto &blds = sim.buildings();
	mix_u32((uint32_t)blds.size());
	for (const auto &b : blds) {
		mix_u32(b.id);
		mix_float(b.x);
		mix_float(b.z);
		mix_float(b.hp);
		mix_float(b.max_hp);
		mix_float(b.build_progress);
		mix_u8(b.type);
		mix_u8(b.team);
		mix_float(b.capture_points);
		for (float capture : b.capture_points_by_team) mix_float(capture);
		mix_float(b.max_capture_points);
		mix_u32((uint32_t)b.queue.size());
		mix_u32((uint32_t)b.garrisoned_units.size());
		mix_u8(b.is_burning ? 1 : 0);
		mix_u8(b.upgrade_to);
		mix_float(b.upgrade_timer);
		mix_u8((uint8_t)((b.gate_open ? 1 : 0) | (b.gate_locked ? 2 : 0)));
	}

	// 2b. Passengers of ships and land siege, by carrier id
	{
		std::vector<uint32_t> carriers;
		for (const auto &kv : sim.transport_passengers())
			if (!kv.second.empty()) carriers.push_back(kv.first);
		std::sort(carriers.begin(), carriers.end());
		mix_u32((uint32_t)carriers.size());
		for (uint32_t cid : carriers) {
			mix_u32(cid);
			for (uint32_t puid : sim.transport_passengers().at(cid)) mix_u32(puid);
		}
	}

	// 2c. Units' production queues (triremes, Cyrus II), by unit id
	if (!sim.unit_queues().empty()) mix_u32((uint32_t)sim.unit_queues().size());
	for (const auto &kv : sim.unit_queues()) {
		mix_u32(kv.first);
		mix_u8(kv.second.player);
		mix_u32((uint32_t)kv.second.orders.size());
		mix_u16(kv.second.orders.front().tmpl);
		mix_u8(kv.second.orders.front().count);
		mix_u8(kv.second.orders.front().tech);
		mix_float(kv.second.orders.front().timer);
	}

	// 3. Resource Nodes — amount, alive, type, coords
	auto rview = sim.resources();
	mix_u32((uint32_t)rview.count);
	for (size_t i = 0; i < rview.count; ++i) {
		mix_u32(rview.id[i]);
		mix_float(rview.x[i]);
		mix_float(rview.z[i]);
		mix_i32(rview.amount[i]);
		mix_u8(rview.alive[i]);
		mix_u8(rview.type[i]);
	}

	// Diplomacy state: include reciprocal stances because shared LOS and
	// capture eligibility depend on mutual alliance, not only player 0's view.
	// Every player's own stances: the full 8x8 matrix is simulation state.
	for (uint8_t a = 0; a < 8; ++a)
		for (uint8_t b = 0; b < 8; ++b) mix_u8(sim.get_diplomacy(a, b));

	// 4-5. Per-player table: stockpiles, researched techs, phase, civ, team, slot flags
	for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
		const PlayerState &ps = sim.player(p);
		for (float r : ps.res) mix_float(r);
		for (uint64_t w : ps.techs.words) { mix_u32((uint32_t)w); mix_u32((uint32_t)(w >> 32)); }
		mix_u8(ps.phase);
		mix_u8(ps.civ);
		mix_u8(ps.team);
		mix_u8(ps.active);
		mix_u8(ps.is_ai);
		for (int r = 0; r < RES_COUNT; ++r) mix_u8(sim.trading_goods(p)[r]);
	}
	// Match state: who is out (their orders are dropped) and who has won.
	mix_u8(sim.participant_mask());
	mix_u8(sim.defeated_mask());
	mix_u8(sim.winner_mask());
	mix_u8(sim.is_game_over() ? 1 : 0);
	// Map triggers (Trigger.js timers) and the water level the rising-water maps move.
	mix_u32(sim.trigger_hash());
	mix_u32(sim.los_hash()); // per-player LOS (visible and explored bits)
	mix_float(sim.water_height());

	// 6. Active Projectiles & Relics
	const auto &projs = sim.projectiles();
	mix_u32((uint32_t)projs.size());
	for (const auto &p : projs) {
		mix_u32(p.id);
		mix_u8(p.alive ? 1 : 0);
		if (!p.alive) continue;
		mix_float(p.current_x);
		mix_float(p.current_y);
		mix_float(p.current_z);
		mix_float(p.target_x);
		mix_float(p.target_y);
		mix_float(p.target_z);
		mix_float(p.damage);
		mix_u8(p.team);
		mix_u8(p.damage_type);
		mix_u8(p.visual);
		mix_u8(p.status_effect);
		mix_u32(p.target_unit_id);
		mix_float(p.progress);
	}

	const auto &relics = sim.relics();
	mix_u32((uint32_t)relics.size());
	for (const auto &r : relics) {
		mix_u32(r.id);
		mix_float(r.x);
		mix_float(r.z);
		mix_u8(r.civ);
		mix_i32(r.capture_cp);
		for (float c : r.cp) mix_float(c);
		mix_u8(r.claimed ? 1 : 0);
	}

	const auto &treasures = sim.treasures();
	mix_u32((uint32_t)treasures.size());
	for (const auto &t : treasures) {
		mix_u32(t.id);
		mix_u8(t.type);
		mix_float(t.collect_progress);
		mix_u32(t.collector_id);
	}
	mix_u32((uint32_t)sim.scenery().size());
	for (const auto &sc : sim.scenery()) {
		mix_u32(sc.id);
		mix_u8(sc.type);
		mix_float(sc.hp);
	}

	// Wildlife: animals move, fight and die in the sim, so they are lockstep state.
	const auto fv = sim.fauna();
	mix_u32((uint32_t)fv.count);
	for (size_t f = 0; f < fv.count; ++f) {
		mix_u32(fv.id[f]);
		mix_float(fv.pos_x[f]);
		mix_float(fv.pos_z[f]);
		mix_float(fv.hp[f]);
		mix_i32(fv.food_amount[f]);
		mix_u8(fv.fauna_type[f]);
		mix_u8(fv.behavior[f]);
	}

	// 7. FoW Grid
	const uint8_t *fow = sim.fow_grid();
	const int fow_cells = sim.fow_res() * sim.fow_res();
	for (int i = 0; i < fow_cells; ++i) {
		if ((i & 3) == 0 && i + 3 < fow_cells) {
			uint32_t chunk = (uint32_t)fow[i] | ((uint32_t)fow[i+1] << 8) | ((uint32_t)fow[i+2] << 16) | ((uint32_t)fow[i+3] << 24);
			mix_u32(chunk);
		} else if ((i & 3) == 0) {
			mix_u8(fow[i]);
		}
	}

	// 8. Territory Grid & Market Prices
	const auto &tg = sim.territory_grid();
	if (!tg.blank() && !tg.data.empty()) {
		mix_u32((uint32_t)tg.w);
		mix_u32((uint32_t)tg.h);
		for (size_t i = 0; i < tg.data.size(); ++i) {
			if ((i & 3) == 0 && i + 3 < tg.data.size()) {
				uint32_t chunk = (uint32_t)tg.data[i] | ((uint32_t)tg.data[i+1] << 8) | ((uint32_t)tg.data[i+2] << 16) | ((uint32_t)tg.data[i+3] << 24);
				mix_u32(chunk);
			} else if ((i & 3) == 0) {
				mix_u8(tg.data[i]);
			}
		}
	} else {
		mix_u32(0x54455252u);
	}

	for (int rt = 0; rt < 4; ++rt) {
		mix_float(sim.market_buy_price((uint8_t)rt));
		mix_float(sim.market_sell_price((uint8_t)rt));
	}

	// 9. Ticks & Wave state + determinism scalars (id allocator + sim RNG stream)
	mix_u32((uint32_t)sim.stats().ticks);
	mix_u32((uint32_t)sim.wave_number());
	mix_u32(sim.next_id());
	mix_u32(sim.sim_seed());
	mix_u32(sim.sim_rng_state());

	return hash;
}

// ---- P3: late-join turn-0 state resend -------------------------------------
void TurnManager::request_join_sync(uint32_t requested_turn) {
	if (!net_host_) return;
	net_host_->request_join_sync(requested_turn);
}

bool TurnManager::answer_join_sync(uint32_t requester_peer_id, uint32_t requested_turn,
                                   uint32_t &sync_turn_out, uint32_t &sync_hash_out) {
	sync_turn_out = requested_turn;
	sync_hash_out = 0;
	if (!net_host_ || !net_host_->isHost()) return false;
	// Nothing is resent until a GAME_START has been applied: replaying a setup the
	// host itself never applied would put the joiner on a world the host is not on.
	if (!net_game_started_) return false;
	if (last_game_start_attributes_.empty()) return false;

	// Window check. Turn 0 is served with the BOOT hash: the world before any
	// command or sim step of turn 1 ran, which is exactly what a joiner rebuilding
	// from the resend produces. Past the window there is no such proof available
	// (RTS0 has no state snapshot -- see the MULTIPLAYER.md Rejoin/FileTransfer
	// gap), so refuse instead of sending a number the joiner cannot check.
	if (requested_turn == 0 && current_turn_ > join_sync_turn_window_) return false;
	if (requested_turn > 0) {
		// Post-turn hash of the last completed turn, for a joiner that will replay
		// locally; 0 means "no turn has completed yet".
		sync_hash_out = turn_history_.empty() ? 0u : turn_history_.back().sim_state_hash;
	} else {
		// The host's boot hash, captured on the first tick of the session before any
		// command or sim step ran.
		sync_hash_out = boot_hash_;
	}

	// Resend the authoritative setup, byte-identical to the original broadcast.
	// This is a pure replay of attributes the host is already broadcasting, so it
	// cannot reset or diverge the host.
	net_host_->sendGameStartTo(requester_peer_id, last_game_start_attributes_);
	join_sync_round_trips_++;
	return true;
}

bool TurnManager::receive_join_sync(uint32_t sender_peer_id, const TurnStateSnapshotMsg &snap) {
	(void)sender_peer_id;
	join_sync_accepted_ = snap.accepted;
	join_sync_error_ = snap.accepted ? 0 : snap.error;
	join_sync_turn_ = snap.turn;
	join_sync_hash_ = snap.hash;
	join_sync_mismatch_ = false;
	if (snap.accepted) {
		// Accepted: stay gated until the resend arrives and is applied. Opening
		// the gate on the accepted flag would tick a world the joiner has not
		// rebuilt yet -- exactly the turn-1 OOS the gate exists to prevent.
		net_game_started_ = false;
	} else {
		// Rejected: nothing will arrive, so the joiner must not sit behind the
		// gate forever. Open it explicitly; the status stays visible for the UI.
		net_game_started_ = true;
		if (snap.error != 0)
			std::cerr << "[TurnManager] late-join resync rejected, error " << (unsigned)snap.error << "\n";
	}
	return true;
}

bool TurnManager::receive_join_sync_check(uint32_t sender_peer_id, const JoinSyncCheckMsg &chk) {
	(void)sender_peer_id;
	join_sync_turn_ = chk.turn;
	join_sync_hash_ = chk.hash;
	if (chk.accepted) {
		join_sync_mismatch_ = false;
		join_sync_accepted_ = true;
		join_sync_error_ = 0;
		// The joiner rebuilt turn 0 from the resend and its hash matched ours, so
		// the lockstep may start. clear_game_started() would reset the apply base
		// too, so the gate opens without touching it.
		net_game_started_ = true;
		if (apply_base_turn_ > current_turn_) apply_base_turn_ = current_turn_;
		std::cout << "[TurnManager] late-join turn-0 hash confirmed (" << chk.turn
		          << ", hash " << chk.hash << ") -- lockstep released\n";
		return true;
	}
	// Mismatch (or an explicit abort): the joiner stays out of the lockstep.
	join_sync_mismatch_ = true;
	join_sync_accepted_ = false;
	join_sync_error_ = chk.error ? chk.error : TurnStateSnapshotMsg::ERR_UNKNOWN;
	net_game_started_ = false;
	std::cerr << "[TurnManager] late-join sync FAILED at turn " << chk.turn
	          << ": joiner rebuilt hash " << chk.hash << " but reported no match to the"
	          << " host -- join aborted\n";
	return false;
}

bool TurnManager::tick(float dt, Simulation &sim) {
	if (oos_dump_pending_) write_oos_dump(sim);
	if (rejoin_wanted_ && !rejoined_) apply_pending_rejoin(sim);
	if (!rejoin_requests_.empty() && net_host_ && net_host_->isHost()) {
		std::vector<uint32_t> reqs;
		reqs.swap(rejoin_requests_);
		for (uint32_t c : reqs) serve_rejoin(c, sim);
	}
	if (net_host_ && net_host_->isHost() && net_game_started()) send_player_assignment();
	if (rejoin_outbox_next_ < rejoin_outbox_.size() && net_host_) {
		const size_t end = std::min(rejoin_outbox_.size(), rejoin_outbox_next_ + REJOIN_CHUNKS_PER_TICK);
		for (; rejoin_outbox_next_ < end; ++rejoin_outbox_next_) {
			const auto &e = rejoin_outbox_[rejoin_outbox_next_];
			net_host_->sendCommand(e.first, e.second);
		}
		if (rejoin_outbox_next_ == rejoin_outbox_.size()) { rejoin_outbox_.clear(); rejoin_outbox_next_ = 0; }
	}
	// 0 A.D. pause: no turn runs anywhere while any client is paused, and the
	// paused time is not banked (no catch-up burst on resume).
	if (is_paused() || (net_host_ && net_host_->isHost() && saved_game_hold())) {
		turn_timer_ = std::min(turn_timer_, TURN_DURATION);
		return false;
	}
	const bool net_client = net_game_started() && lockstep_net_active() && net_host_ && !net_host_->isHost();
	if (rejoin_fast_forward_ && (!net_client || turns_behind() <= CATCH_UP_TURNS)) rejoin_fast_forward_ = false;
	float rate = 1.0f;
	if (auto_catch_up_ && net_client && is_observer() && !rejoin_fast_forward_) {
		// NetworkDelayOverlay.CheckDelay: 10% faster while too far behind.
		const uint32_t behind = turns_behind();
		if (behind > OBSERVER_MAX_LIVE_DELAY) observer_fast_ = true;
		else if (behind <= OBSERVER_NORMAL_DELAY) observer_fast_ = false;
		if (observer_fast_) rate = OBSERVER_CATCH_UP_RATE;
	} else {
		observer_fast_ = false;
	}
	turn_timer_ += dt * rate;
	if (turn_timer_ < TURN_DURATION) {
		// A rejoiner runs the turns it got while loading without waiting out
		// each turn length (CNetClient::LoadFinished fast-forwards them).
		if (!rejoin_fast_forward_) return false;
		turn_timer_ = TURN_DURATION;
	}

	// G8 lockstep gate. Peers that tick before NMT_GAME_START is applied hash
	// their own INDEPENDENT boot worlds, so turn 1 compares two unrelated
	// simulations and every later turn inherits that offset -- the turn-1 OOS the
	// presentation-side net_game_started_ gate fixed. This is the network-layer
	// half of that fix and it cannot be relaxed: tick() returns false so the
	// accumulator time is NOT consumed and the turn index does not move, keeping
	// the gate closed for divergent boot worlds. Offline play is ungated.
	if (!net_game_started()) {
		++gated_ticks_;
		turn_timer_ = TURN_DURATION; // clamp, not reset: no unbounded timer growth
		return false;
	}
	// Safety net for the boot hash: it is normally captured when the session is
	// armed (arm_boot_capture from note_game_start_applied / mark_game_started /
	// set_net_host). If a caller armed the session through some other path, capture
	// it here instead of leaving a late joiner with nothing to compare against.
	if (!boot_hash_captured_ && sim_boot_hash_ptr_) {
		boot_hash_captured_ = true;
		boot_hash_ = compute_sim_hash(*sim_boot_hash_ptr_);
	}

	// Lockstep gate (NetClientTurnManager.cpp:188): turn N runs only once every
	// player's batch for N is here. Time keeps accruing so a peer that waited can
	// catch up, but capped so a long stall does not replay as a burst.
	if (lockstep_net_active() && !turn_ready(current_turn_ + 1)) {
		if (!stall_since_ms_) stall_since_ms_ = rejoin_now_ms();
		++waiting_ticks_;
		if (turn_timer_ > 5.0f * TURN_DURATION) turn_timer_ = 5.0f * TURN_DURATION;
		return false;
	}

	turn_timer_ -= TURN_DURATION;
	current_turn_++;
	stall_since_ms_ = 0;

	TurnFrame frame;
	frame.turn_index = current_turn_;

	// Replay verify: stored hash is post-turn state (recorded after the turn's
	// sim steps, like the live path below), so the compare must run AFTER the
	// shared command-execution + sim-step block. Capture the expectation here.
	uint32_t replay_expected_hash = 0;
	if (is_replaying_) {
		if (replay_frame_idx_ < turn_history_.size()) {
			frame = turn_history_[replay_frame_idx_++];
			replay_expected_hash = frame.sim_state_hash;
		}
	} else {
		for (auto &c : pending_local_cmds_) {
			c.client_id = local_client_id_;
		}

		// If connected to network, broadcast our local command batch for
		// current_turn_ + COMMAND_DELAY and keep our own copy for that turn: every
		// peer, us included, runs it there.
		const bool lockstep = lockstep_net_active();
		const auto my_since = lockstep_since_.find(local_client_id_);
		if (lockstep && !net_host_->isHost() && (is_observer() || rejoin_lockstep_wait_ ||
				(my_since != lockstep_since_.end() && current_turn_ + COMMAND_DELAY < my_since->second))) {
			// An observer only watches, and a rejoining player sends nothing
			// until the lockstep takes it: whatever it posted goes nowhere.
			// (The host still relays seat changes.)
			pending_local_cmds_.clear();
		} else if (lockstep) {
			const uint32_t sched = current_turn_ + COMMAND_DELAY;
			SimulationCommandMsg scm;
			scm.turn = sched;
			scm.client_id = local_client_id_;
			if (net_host_->isHost()) {
				// Seat changes ride in our batch (see CMD_PEER_JOINED).
				ensure_lockstep_peers();
				for (auto &c : pending_control_) {
					c.client_id = local_client_id_;
					pending_local_cmds_.push_back(c);
				}
				pending_control_.clear();
			}
			scm.commands = pending_local_cmds_;
			std::vector<uint8_t> payload = NetPacketSerializer::frame_simulation_command(scm);
			std::vector<uint8_t> pkt = NetPacketSerializer::pack_message(NetMessageType::NMT_SIMULATION_COMMAND, sched, payload);
			net_host_->broadcastCommand(pkt);
			receive_remote_turn_batch(sched, local_client_id_, pending_local_cmds_);
			pending_local_cmds_.clear();

			if (net_host_->isHost()) {
				EndCommandBatchMsg ebm;
				ebm.turn = current_turn_;
				ebm.turn_length = static_cast<uint16_t>(TURN_DURATION * 1000.0f);
				std::vector<uint8_t> eb_payload = NetPacketSerializer::frame_end_command_batch(ebm);
				std::vector<uint8_t> eb_pkt = NetPacketSerializer::pack_message(NetMessageType::NMT_END_COMMAND_BATCH, current_turn_, eb_payload);
				net_host_->broadcastCommand(eb_pkt);
			}
		}

		frame.commands = std::move(pending_local_cmds_);
		pending_local_cmds_.clear();

		received_batches_.erase(current_turn_);
		auto it = turn_remote_cmds_.find(current_turn_);
		if (it != turn_remote_cmds_.end()) {
			for (auto &rc : it->second) {
				frame.commands.push_back(std::move(rc));
			}
			turn_remote_cmds_.erase(it);
		}

		for (auto &rc : pending_remote_cmds_) {
			frame.commands.push_back(std::move(rc));
		}
		pending_remote_cmds_.clear();

		// Deterministic order: by client, and each client's commands in the order it sent
		// them (every peer gets the same batch per client), so Shift-queued orders of
		// different kinds queue in the order they were given (0 A.D. runs a turn's
		// commands in arrival order too).
		std::stable_sort(frame.commands.begin(), frame.commands.end(), [](const NetCommand &a, const NetCommand &b) {
			return a.client_id < b.client_id;
		});
	}

	// Execute turn commands into simulation (0 A.D. applies batch then advances sim).
	// 0 A.D. Commands.js: each command runs as its sender's player, and FilterEntityList
	// drops every unit the sender doesn't own, so a peer can only order its own side.
	std::vector<uint32_t> owned;
	for (const auto &c : frame.commands) {
		if (c.cmd_type == 39 && c.param == Simulation::CHEAT_DEV_PERSPECTIVE) {
			// The developer overlay's Change perspective (PlayerViewControl: Engine.SetPlayerID):
			// with cheats on, the sender plays `target_id` from here on, or watches (0xFF).
			// Defeated players may use it too, as in 0.28 (the host drops observers' commands).
			if (sim.cheats_enabled() && (c.target_id < MAX_PLAYERS || c.target_id == 0xFF)) {
				client_player_[c.client_id] = (uint8_t)c.target_id;
				if (c.client_id == local_client_id_ && !is_replaying_) perspective_changed_ = true;
			}
			continue;
		}
		const uint8_t pl = player_for_client(c.client_id);
		if (pl >= MAX_PLAYERS || sim.is_defeated(pl)) continue; // defeated players only watch
		sim.filter_owned_units(pl, c.entity_ids.data(), c.entity_ids.size(), owned);
		const uint32_t *ids = owned.data();
		const size_t n = owned.size();
		const bool queued = (c.param & NET_PARAM_QUEUED) != 0;
		const bool capture = (c.param & NET_PARAM_NO_CAPTURE) == 0;
		note_player_command(sim, pl, c);
		if (record_applied_ && applied_cmds_.size() < 256) {
			applied_cmds_.push_back({pl, c});
			if (c.cmd_type != (uint8_t)NetCommandType::NET_CMD_RALLY) // its entity_ids are targets
				applied_cmds_.back().cmd.entity_ids.assign(ids, ids + n);
		}
		// The sender's own livestock in the selection walks, stops and garrisons in a
		// corral too (0 A.D. livestock are units; filter_owned_units keeps units only).
		const uint32_t *raw = c.entity_ids.data();
		const size_t raw_n = c.entity_ids.size();
		if (c.cmd_type == 1) { // Move (a player's walk: forced, ignores enemies)
			sim.order_walk(ids, n, c.target_x, c.target_z, queued, true);
			if (raw_n > n) sim.order_fauna_walk(pl, raw, raw_n, c.target_x, c.target_z);
		} else if (c.cmd_type == 2) { // Stop
			sim.cmd_stop(ids, n);
			if (raw_n > n) sim.order_fauna_stop(pl, raw, raw_n);
		} else if (c.cmd_type == 3) { // Attack
			sim.order_attack(ids, n, c.target_id, queued);
		} else if (c.cmd_type == 4) { // Gather
			sim.order_gather(ids, n, c.target_id, queued);
		} else if (c.cmd_type == 5) { // Build
			uint32_t bid = sim.cmd_build(pl, (int)c.target_x, (int)c.target_z, c.param, Simulation::unpack_angle(c.target_id));
			if (bid && n) {
				// TryConstructBuilding hands the builders a "repair" of the new foundation.
				if (sim.has_map_triggers()) sim.note_player_command(pl, TRIGGER_CMD_REPAIR, bid, 0, 0, 0);
				sim.order_build_or_repair(ids, n, bid);
			}
		} else if (c.cmd_type == 6) { // Train
			// At a corral target_x names the animal (corral Trainer list); 0 is the civ's default.
			if (c.param == UNIT_TYPE_NONE && c.target_x > 0.0f)
				sim.cmd_breed_livestock(pl, c.target_id, (uint8_t)c.target_x, batch_count(c.target_z));
			else
				sim.cmd_train(pl, c.target_id, (UnitType)c.param, batch_count(c.target_z));
		} else if (c.cmd_type == 7) { // Stance
			sim.order_set_stance(ids, n, c.param);
		} else if (c.cmd_type == 8) { // Formation
			sim.order_set_formation(ids, n, c.param);
		} else if (c.cmd_type == 9) { // Research
			sim.cmd_research(pl, c.target_id, c.param);
		} else if (c.cmd_type == 10) { // Attack-move
			sim.order_attack_move(ids, n, c.target_x, c.target_z, queued, (c.param & NET_PARAM_UNITS_ONLY) != 0, capture);
		} else if (c.cmd_type == 11) { // Patrol
			sim.order_patrol(ids, n, c.target_x, c.target_z, queued);
		} else if (c.cmd_type == 12) { // Garrison
			sim.order_garrison(ids, n, c.target_id, queued, (c.param & NET_PARAM_OCCUPY_TURRET) != 0);
			if (raw_n > n) sim.order_fauna_walk(pl, raw, raw_n, 0.0f, 0.0f, c.target_id);
		} else if (c.cmd_type == 13) { // Ungarrison / disembark-all / leave-turret
			if (c.param == 1) {
				if (n) sim.order_disembark_all(ids[0]);
			} else if (c.param == 2) { // 0 A.D. "leave-turret": entity_ids are the turret units
				sim.cmd_leave_turret(pl, c.entity_ids.data(), c.entity_ids.size());
			} else if (c.param == 3) { // 0 A.D. "unload-turrets": target_id is the holder
				sim.cmd_unload_turrets(pl, c.target_id);
			} else { // entity_ids: the garrisoned units to unload (UnloadTemplate); none: all
				sim.cmd_unload(pl, c.target_id, c.entity_ids.data(), c.entity_ids.size());
			}
		} else if (c.cmd_type == 14) { // Rally (entity_ids carry target ids, not units)
			uint32_t res = c.entity_ids.size() > 0 ? c.entity_ids[0] : 0;
			uint32_t bld = c.entity_ids.size() > 1 ? c.entity_ids[1] : 0;
			if ((c.param & ~NET_PARAM_QUEUED) == 1) sim.cmd_unset_rally_point(pl, c.target_id);
			else sim.cmd_set_rally_point(pl, c.target_id, c.target_x, c.target_z, res, bld, queued);
		} else if (c.cmd_type == 15) { // Attack building
			sim.order_attack_building(ids, n, c.target_id, queued, capture); // right-click captures, Ctrl attacks
		} else if (c.cmd_type == 16) { // Build / repair
			sim.order_build_or_repair(ids, n, c.target_id, queued);
		} else if (c.cmd_type == 17) { // Hunt / herd
			if (c.param == 1)
				sim.order_herd(ids, n, c.target_id);
			else if ((c.param & (uint8_t)~NET_PARAM_QUEUED) == 2) // TreasureCollector: collect a Gaia treasure
				sim.order_collect_treasure(ids, n, c.target_id, queued);
			else if ((c.param & (uint8_t)~NET_PARAM_QUEUED) == NET_HUNT_SCENERY_NO_CAPTURE)
				sim.order_attack_scenery(ids, n, c.target_id, false);
			else
				sim.order_hunt(ids, n, c.target_id);
		} else if (c.cmd_type == 18) { // Board
			sim.order_board_transport(ids, n, c.target_id);
		} else if (c.cmd_type == 19) { // Disembark
			if (n) sim.order_disembark(ids[0], c.target_x, c.target_z);
		} else if (c.cmd_type == 20) { // Cancel train
			sim.cmd_cancel_train(pl, c.target_id, (size_t)c.param);
		} else if (c.cmd_type == 21) { // Guard
			sim.order_guard(ids, n, c.target_id, queued);
		} else if (c.cmd_type == 30) { // Delete — entity_ids = the player's units and structures.
			// cmd_delete checks ownership itself: structures aren't in `owned` (units only).
			sim.cmd_delete(pl, c.entity_ids.data(), c.entity_ids.size());
		} else if (c.cmd_type == 31) { // Back to work
			sim.order_back_to_work(ids, n);
		} else if (c.cmd_type == 32) { // Heal
			sim.order_heal(ids, n, c.target_id, queued);
		} else if (c.cmd_type == 34) { // Return resource
			sim.order_return_resource(ids, n, c.target_id, queued);
		} else if (c.cmd_type == 35) { // Trade route — target_id = first market, target_x = second market's id.
			// Ids travel as floats; a whole number below 2^24 is exact.
			if (c.target_x >= 1.0f && c.target_x < 16777216.0f && c.target_x == (float)(uint32_t)c.target_x)
				sim.order_trade(ids, n, c.target_id, (uint32_t)c.target_x, queued);
		} else if (c.cmd_type == 37) { // Remove guard
			sim.order_remove_guard(ids, n);
		} else if (c.cmd_type == 38) { // Call to arms — attack-walk to (target_x, target_z).
			sim.order_call_to_arms(ids, n, c.target_x, c.target_z, queued, (c.param & NET_PARAM_UNITS_ONLY) != 0,
					(c.param & NET_PARAM_CAPTURE) != 0);
		} else if (c.cmd_type == 41) { // WallSet wall — from the point in target_id to (target_x, target_z);
			float sx, sz;                 // param's low bits name the WallSetKind (1 palisade).
			Simulation::unpack_wall_point(c.target_id, sx, sz);
			sim.cmd_build_wall(pl, sx, sz, c.target_x, c.target_z, ids, n, queued, (uint8_t)(c.param & 0x7F));
		} else if (c.cmd_type == 43) { // Unit upgrade — target_id = roster template, param 1 cancel.
			if (c.param == 1) sim.cmd_cancel_unit_upgrades(pl, ids, n);
			else if (c.target_id < CIV_UNIT_TEMPLATE_COUNT) sim.cmd_upgrade_units(pl, ids, n, (uint16_t)c.target_id);
		} else if (c.cmd_type == 36) { // Raise (param 0) or end (1) an alert
			if (c.param == 1) sim.cmd_end_alert(pl, c.target_id);
			else sim.cmd_raise_alert(pl, c.target_id);
		} else if (c.cmd_type == 26) { // Structure upgrade — target_id=building, param 0 start / 1 cancel,
			// target_x = which upgrade choice (the Tachara's four tribute forms).
			if (c.param == 1) sim.cmd_cancel_upgrade(pl, c.target_id);
			else if (c.target_x >= 0.0f && c.target_x < 8.0f) sim.cmd_upgrade_building(pl, c.target_id, (int)c.target_x);
		} else if (c.cmd_type == 45) { // Dropsite sharing — target_id=building, param 1 share / 0 stop.
			sim.cmd_set_dropsite_sharing(pl, c.target_id, c.param == 1);
		} else if (c.cmd_type == 28) { // Gate lock — target_id=building, param 1 lock / 0 unlock.
			sim.cmd_lock_gate(pl, c.target_id, c.param == 1);
		} else if (c.cmd_type == 29) { // Pack / unpack siege — param 1 pack / 0 unpack.
			sim.order_pack(ids, n, c.param == 1);
		} else if (c.cmd_type == 25) { // Train roster template — target_id=building, target_x=template.
			if (c.target_x >= 0.0f && c.target_x < (float)CIV_UNIT_TEMPLATE_COUNT)
				sim.cmd_train_template(pl, c.target_id, (uint16_t)c.target_x, batch_count(c.target_z));
		} else if (c.cmd_type == 48) { // Unit Trainer/Researcher — target_id=unit, target_x=template (param 0), tech (2) or
			// queue slot (param 1 cancels), target_z=batch.
			if (c.param == 2) { // research technology target_x (Researcher on a unit)
				if (c.target_x > 0.0f && c.target_x < (float)TECH_COUNT) sim.cmd_research_from_unit(pl, c.target_id, (uint8_t)c.target_x);
			} else if (c.target_x >= 0.0f && c.target_x < (float)CIV_UNIT_TEMPLATE_COUNT) {
				if (c.param == 1) sim.cmd_cancel_unit_train(pl, c.target_id, (size_t)c.target_x);
				else sim.cmd_train_from_unit(pl, c.target_id, (uint16_t)c.target_x, batch_count(c.target_z));
			}
		} else if (c.cmd_type == 44) { // Autoqueue — target_id=building, param 1 on / 0 off.
			sim.cmd_set_autoqueue(pl, c.target_id, c.param == 1);
		} else if (c.cmd_type == 33) { // Trading goods — target_id = one percent byte per ResourceType.
			uint8_t pct[RES_COUNT];
			for (int r = 0; r < RES_COUNT; ++r) pct[r] = (uint8_t)(c.target_id >> (8 * r));
			sim.cmd_set_trading_goods(pl, pct);
		} else if (c.cmd_type == 27) { // Tribute — target_id=recipient, target_x=amount, param=resource.
			if (c.target_x > 0.0f && c.target_x <= 1e6f && c.target_id < MAX_PLAYERS)
				sim.cmd_tribute(pl, (uint8_t)c.target_id, c.param, (int)c.target_x);
		} else if (c.cmd_type == 23) { // Barter — target_x=sell, target_z=buy, target_id=amount.
			// 0 A.D. Barter.js / Market.js exchange. Legacy gap: UI previously called
			// sim->cmd_barter locally even in lockstep -> resource/price divergence.
			sim.cmd_barter(pl, (rtso::ResourceType)(int)c.target_x,
			               (rtso::ResourceType)(int)c.target_z, (int)c.target_id);
		} else if (c.cmd_type == (uint8_t)NetCommandType::NET_CMD_RESIGN) { // Resign (0 A.D. "resign")
			sim.cmd_resign(pl);
		} else if (c.cmd_type == (uint8_t)NetCommandType::NET_CMD_FOCUS_FIRE) { // Focus fire — entity_ids=structures.
			// cmd_focus_fire checks that `pl` owns each structure itself.
			sim.cmd_focus_fire(pl, c.entity_ids.data(), c.entity_ids.size(), c.target_id, queued,
					(c.param & NET_PARAM_PUSH_FRONT) != 0);
		} else if (c.cmd_type == 39) { // Cheat (0 A.D. Cheat.js) — param=action, target_id=number,
			// target_x=resource or tech; acts on the raw selection, whoever owns it.
			sim.cmd_cheat(pl, c.param, (int32_t)c.target_id, (uint32_t)c.target_x, c.entity_ids.data(), c.entity_ids.size());
		} else if (c.cmd_type == 42) { // Bribe (0 A.D. "spy-request") — target_id=player.
			if (c.target_id < MAX_PLAYERS) sim.cmd_bribe(pl, (uint8_t)c.target_id);
		} else if (c.cmd_type == 46) { // Attack request (0 A.D. "attack-request") — target_id=enemy player.
			if (c.target_id < MAX_PLAYERS) sim.cmd_attack_request(pl, (uint8_t)c.target_id);
		} else if (c.cmd_type == 22) { // Diplomacy stance — target_id=player, param=stance.
			// 0 A.D. Diplomacy.js / DiplomacyDialog.js parity. Applied at the turn
			// boundary on EVERY peer (deterministic: diplomacy_state() is mixed into
			// compute_sim_hash at turn_manager.cpp:213, so a local-only change OOSes).
			// UI entry: main_node.cpp _on_diplo_stance_changed -> post_lockstep(22, ...).
			// Every player writes its own row, so cmd_set_diplomacy takes the
			// issuing player the command was filtered for.
			sim.cmd_set_diplomacy(pl, (uint8_t)c.target_id, c.param);
		}
	}

	// HashSerializer runs after the turn's simulation steps, not before
	for (int i = 0; i < TICKS_PER_TURN; ++i)
		sim.tick(Simulation::FIXED_STEP);

	frame.sim_state_hash = compute_sim_hash(sim);

	if (is_replaying_) {
		replay_expected_hash_ = replay_expected_hash;
		replay_computed_hash_ = frame.sim_state_hash;
		replay_oos_ = replay_expected_hash != 0 && replay_expected_hash != frame.sim_state_hash;
		replay_oos_turn_ = replay_oos_ ? frame.turn_index : 0;
		replay_complete_ = replay_frame_idx_ >= turn_history_.size();
	}

	if (replay_expected_hash != 0 && replay_expected_hash != frame.sim_state_hash) {
		std::cerr << "[TurnManager] replay OOS at turn " << frame.turn_index
		          << " stored=" << replay_expected_hash << " computed=" << frame.sim_state_hash << "\n";
	}

	// Network state hash exchange and OOS detection
	if (net_host_ && net_host_->getRole() != NetRole::None) {
		SyncCheckMsg scm;
		scm.turn = current_turn_;
		scm.hash = frame.sim_state_hash;
		scm.hash_str = std::to_string(frame.sim_state_hash);
		std::vector<uint8_t> sc_payload = NetPacketSerializer::frame_sync_check(scm);
		std::vector<uint8_t> sc_pkt = NetPacketSerializer::pack_message(NetMessageType::NMT_SYNC_CHECK, current_turn_, sc_payload);
		net_host_->broadcastCommand(sc_pkt);

		auto hit = peer_hashes_.find(current_turn_);
		if (hit != peer_hashes_.end()) {
			for (const auto &pair : hit->second) {
				uint32_t peer_id = pair.first;
				uint32_t peer_hash = pair.second;
				if (peer_hash != 0 && peer_hash != frame.sim_state_hash)
					note_out_of_sync(current_turn_, frame.sim_state_hash, peer_hash, peer_id);
			}
		}
	}

	if (!is_replaying_) {
		turn_history_.push_back(frame);
	}
	return true;
}

// NetServerTurnManager names the out-of-sync players by their user names.
std::string TurnManager::client_label(uint32_t client_id) const {
	const std::string name = client_id == 0 && net_host_ ? net_host_->localPlayerName() : [&] {
		auto it = client_names_.find(client_id);
		return it == client_names_.end() ? std::string() : it->second;
	}();
	if (!name.empty()) return name;
	const uint8_t pl = player_for_client(client_id);
	if (pl < MAX_PLAYERS) return "Player " + std::to_string((unsigned)pl + 1);
	return "Observer " + std::to_string(client_id);
}

bool TurnManager::oos_matches_host() const {
	return (net_host_ && net_host_->isHost()) || oos_expected_hash_ == oos_received_hash_;
}

uint32_t TurnManager::local_hash_at(uint32_t turn) const {
	for (auto it = turn_history_.rbegin(); it != turn_history_.rend(); ++it)
		if (it->turn_index == turn) return it->sim_state_hash;
	return 0;
}

void TurnManager::note_out_of_sync(uint32_t turn, uint32_t local_hash, uint32_t peer_hash, uint32_t peer_id) {
	if (oos_rearmed_) {
		oos_rearmed_ = false;
		oos_detected_ = false;
		oos_reported_ = false;
		oos_peers_.clear();
	}
	const bool earlier = !oos_detected_ || turn < oos_turn_;
	if (earlier) {
		oos_turn_ = turn;
		oos_expected_hash_ = local_hash;
		oos_received_hash_ = peer_hash;
	}
	oos_detected_ = true;
	if (std::find(oos_peers_.begin(), oos_peers_.end(), peer_id) == oos_peers_.end())
		oos_peers_.push_back(peer_id);
	std::cerr << "[TurnManager] Out-of-sync detected at turn " << turn << " local_hash=" << local_hash
	          << " peer=" << peer_id << " peer_hash=" << peer_hash << "\n";
	if (!net_host_ || !net_host_->isHost() || (oos_reported_ && !earlier)) return;
	// The host's hash is the expected one (NetServerTurnManager "assume the host
	// is correct"); resent if an earlier diverging turn turns up later, since
	// reliable UDP can deliver sync checks out of order.
	oos_players_.clear();
	for (uint32_t p : oos_peers_) oos_players_.push_back(client_label(p));
	SyncErrorMsg sem;
	sem.turn = oos_turn_;
	sem.hash_expected = oos_expected_hash_;
	sem.player_names = oos_players_;
	std::vector<uint8_t> payload = NetPacketSerializer::frame_sync_error(sem);
	std::vector<uint8_t> pkt = NetPacketSerializer::pack_message(NetMessageType::NMT_SYNC_ERROR, oos_turn_, payload);
	net_host_->broadcastCommand(pkt);
	oos_reported_ = true;
	oos_dump_pending_ = true;
}

void TurnManager::receive_sync_error(const SyncErrorMsg &msg) {
	if (oos_from_host_ && msg.turn >= oos_turn_) return; // already have this or an earlier turn
	oos_detected_ = true;
	oos_from_host_ = true;
	oos_turn_ = msg.turn;
	oos_expected_hash_ = local_hash_at(msg.turn);
	oos_received_hash_ = msg.hash_expected;
	oos_players_ = msg.player_names;
	oos_dump_pending_ = true;
	std::cerr << "[TurnManager] Host reports Out-Of-Sync on turn " << msg.turn << "\n";
}

// NetClientTurnManager::OnSyncError: a text header and a full binary state dump.
void TurnManager::write_oos_dump(const Simulation &sim) {
	oos_dump_pending_ = false;
	if (oos_dump_dir_.empty()) return;
	const std::string base = oos_dump_dir_ + "/oos_dump";
	std::ofstream file(base + ".txt", std::ios::out | std::ios::trunc);
	if (!file) return;
	char buf[16];
	auto hex = [&](uint32_t h) { std::snprintf(buf, sizeof(buf), "%08X", h); return std::string(buf); };
	file << "oos turn: " << oos_turn_ << "\n";
	file << "net client turn: " << current_turn_ << "\n";
	file << "local hash at oos turn: " << hex(oos_expected_hash_) << "\n";
	file << "local hash now: " << hex(compute_sim_hash(sim)) << "\n";
	file << "expected hash: " << hex(net_host_ && net_host_->isHost() ? oos_expected_hash_ : oos_received_hash_) << "\n";
	file << "players:";
	for (size_t i = 0; i < oos_players_.size(); ++i) file << (i ? ", " : " ") << oos_players_[i];
	file << "\n";
	if (current_turn_ != oos_turn_)
		file << "WARNING: dump is from turn " << current_turn_ << "; OOS was detected on turn " << oos_turn_
		     << " -- state may have advanced past the diverge point.\n";
	// A readable state (0 A.D. DumpDebugState), so two peers' dumps diff.
	file.precision(9); // every float bit that differs shows
	file << "\n-- players (food wood stone metal)\n";
	for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
		const PlayerState &ps = sim.player(p);
		file << "player " << (unsigned)p << ":";
		for (float r : ps.res) file << " " << r;
		file << "\n";
	}
	auto uview = sim.units();
	file << "\n-- units (id type team x z hp action carry)\n";
	for (size_t i = 0; i < uview.count; ++i)
		if (uview.alive[i])
			file << uview.id[i] << " " << (unsigned)uview.unit_type[i] << " " << (unsigned)uview.team[i] << " "
			     << uview.pos_x[i] << " " << uview.pos_z[i] << " " << uview.hp[i] << " "
			     << (unsigned)uview.action_state[i] << " " << uview.carry[i] << "\n";
	file << "\n-- buildings (id type team x z hp progress queue garrison)\n";
	for (const auto &b : sim.buildings())
		file << b.id << " " << (unsigned)b.type << " " << (unsigned)b.team << " " << b.x << " " << b.z << " "
		     << b.hp << " " << b.build_progress << " " << b.queue.size() << " " << b.garrisoned_units.size() << "\n";
	file.close();
	if (sim.save_state((base + ".dat").c_str())) oos_dump_written_ = base + ".txt";
}

void TurnManager::set_local_paused(bool paused) {
	if (paused) paused_clients_.insert(local_client_id_);
	else paused_clients_.erase(local_client_id_);
	if (!net_host_ || net_host_->getRole() == NetRole::None) return;
	ClientPausedMsg cpm;
	cpm.guid = std::to_string(local_client_id_);
	cpm.pause = paused ? 1 : 0;
	std::vector<uint8_t> payload = NetPacketSerializer::frame_client_paused(cpm);
	std::vector<uint8_t> pkt = NetPacketSerializer::pack_message(NetMessageType::NMT_CLIENT_PAUSED, current_turn_, payload);
	net_host_->broadcastCommand(pkt); // client -> host; host -> every client
}

// 0 A.D. NetServer.cpp OnClientPaused: the host trusts the session, not the
// payload, for who paused, and relays the change to everyone else. A client
// takes the host's word for which client it was.
void TurnManager::receive_client_paused(uint32_t author, const ClientPausedMsg &msg) {
	uint32_t who = author;
	if (net_host_ && net_host_->isHost()) {
		ClientPausedMsg relay = msg;
		relay.guid = std::to_string(author);
		std::vector<uint8_t> payload = NetPacketSerializer::frame_client_paused(relay);
		std::vector<uint8_t> pkt = NetPacketSerializer::pack_message(NetMessageType::NMT_CLIENT_PAUSED, current_turn_, payload);
		net_host_->broadcastCommand(pkt, author);
	} else {
		if (author != 0 || msg.guid.empty() || msg.guid.size() > 9 ||
				msg.guid.find_first_not_of("0123456789") != std::string::npos) return;
		who = (uint32_t)std::stoul(msg.guid);
	}
	if (msg.pause) paused_clients_.insert(who);
	else paused_clients_.erase(who);
}

bool TurnManager::write_replay_file(const std::string &path) const {
	std::ofstream out(path, std::ios::binary);
	if (!out.is_open()) return false;
	if (turn_history_.size() > REPLAY_MAX_FRAMES) return false;
	for (size_t i = 0; i < turn_history_.size(); ++i) {
		const auto &f = turn_history_[i];
		if (i && f.turn_index <= turn_history_[i - 1].turn_index) return false;
		if (f.commands.size() > REPLAY_MAX_COMMANDS_PER_FRAME) return false;
		for (const auto &c : f.commands)
			if (c.entity_ids.size() > REPLAY_MAX_ENTITIES_PER_COMMAND) return false;
	}
	// Keep the established v2 wire layout. The format is native-endian and is
	// intentionally an internal turn recorder, not a portable 0 A.D. replay.
	const uint32_t magic = 0x52545330; // RTS0
	const uint32_t version = 2;
	out.write(reinterpret_cast<const char *>(&magic), sizeof(magic));
	out.write(reinterpret_cast<const char *>(&version), sizeof(version));
	const uint32_t count = static_cast<uint32_t>(turn_history_.size());
	out.write(reinterpret_cast<const char *>(&count), sizeof(count));
	for (const auto &f : turn_history_) {
		out.write(reinterpret_cast<const char *>(&f.turn_index), sizeof(f.turn_index));
		out.write(reinterpret_cast<const char *>(&f.sim_state_hash), sizeof(f.sim_state_hash));
		const uint32_t nc = static_cast<uint32_t>(f.commands.size());
		out.write(reinterpret_cast<const char *>(&nc), sizeof(nc));
		for (const auto &c : f.commands) {
			out.write(reinterpret_cast<const char *>(&c.client_id), sizeof(c.client_id));
			out.write(reinterpret_cast<const char *>(&c.cmd_type), sizeof(c.cmd_type));
			out.write(reinterpret_cast<const char *>(&c.target_x), sizeof(c.target_x));
			out.write(reinterpret_cast<const char *>(&c.target_z), sizeof(c.target_z));
			out.write(reinterpret_cast<const char *>(&c.target_id), sizeof(c.target_id));
			out.write(reinterpret_cast<const char *>(&c.param), sizeof(c.param));
			const uint32_t ne = static_cast<uint32_t>(c.entity_ids.size());
			out.write(reinterpret_cast<const char *>(&ne), sizeof(ne));
			if (ne) out.write(reinterpret_cast<const char *>(c.entity_ids.data()), ne * sizeof(uint32_t));
		}
	}
	return out.good();
}

bool TurnManager::read_replay_file(const std::string &path, std::vector<TurnFrame> &out,
                                  std::string &error_out) const {
	out.clear(); error_out.clear();
	std::ifstream in(path, std::ios::binary);
	if (!in.is_open()) { error_out = "cannot open replay"; return false; }
	const uint32_t magic = 0x52545330;
	uint32_t got_magic = 0, version = 0, count = 0;
	in.read(reinterpret_cast<char *>(&got_magic), sizeof(got_magic));
	in.read(reinterpret_cast<char *>(&version), sizeof(version));
	if (!in.good() || got_magic != magic) { error_out = "bad replay magic"; return false; }
	if (version != 1 && version != 2) { error_out = "unsupported replay version"; return false; }
	in.read(reinterpret_cast<char *>(&count), sizeof(count));
	if (!in.good() || count > REPLAY_MAX_FRAMES) { error_out = "invalid replay frame count"; return false; }
	out.reserve(count);
	uint32_t previous = 0;
	for (uint32_t i = 0; i < count; ++i) {
		TurnFrame f;
		in.read(reinterpret_cast<char *>(&f.turn_index), sizeof(f.turn_index));
		in.read(reinterpret_cast<char *>(&f.sim_state_hash), sizeof(f.sim_state_hash));
		if (!in.good()) { error_out = "truncated replay frame"; return false; }
		if (i && f.turn_index <= previous) { error_out = "non-monotonic replay turns"; return false; }
		previous = f.turn_index;
		uint32_t nc = 0; in.read(reinterpret_cast<char *>(&nc), sizeof(nc));
		if (!in.good() || nc > REPLAY_MAX_COMMANDS_PER_FRAME) { error_out = "invalid replay command count"; return false; }
		f.commands.resize(nc);
		for (auto &c : f.commands) {
			uint32_t ne = 0;
			in.read(reinterpret_cast<char *>(&c.client_id), sizeof(c.client_id));
			in.read(reinterpret_cast<char *>(&c.cmd_type), sizeof(c.cmd_type));
			in.read(reinterpret_cast<char *>(&c.target_x), sizeof(c.target_x));
			in.read(reinterpret_cast<char *>(&c.target_z), sizeof(c.target_z));
			in.read(reinterpret_cast<char *>(&c.target_id), sizeof(c.target_id));
			in.read(reinterpret_cast<char *>(&c.param), sizeof(c.param));
			in.read(reinterpret_cast<char *>(&ne), sizeof(ne));
			if (!in.good() || ne > REPLAY_MAX_ENTITIES_PER_COMMAND) { error_out = "invalid replay entity count"; return false; }
			c.entity_ids.resize(ne);
			if (ne) in.read(reinterpret_cast<char *>(c.entity_ids.data()), ne * sizeof(uint32_t));
			if (!in.good()) { error_out = "truncated replay command"; return false; }
		}
		out.push_back(std::move(f));
	}
	return true;
}

bool TurnManager::validate_replay(const std::string &path, std::string &error_out) const {
	std::vector<TurnFrame> frames;
	return read_replay_file(path, frames, error_out);
}

bool TurnManager::save_replay(const std::string &path) const {
	return write_replay_file(path);
}

bool TurnManager::load_replay(const std::string &path) {
	std::vector<TurnFrame> frames;
	std::string error;
	if (!read_replay_file(path, frames, error)) {
		last_replay_error_ = error;
		return false;
	}
	turn_history_ = std::move(frames);
	is_replaying_ = true;
	replay_loaded_ = true;
	replay_complete_ = false;
	replay_oos_ = false;
	replay_oos_turn_ = 0;
	replay_expected_hash_ = 0;
	replay_computed_hash_ = 0;
	last_replay_error_.clear();
	replay_frame_idx_ = 0;
	current_turn_ = 0;
	turn_timer_ = 0.0f;
	return true;
}

} // namespace rtso

