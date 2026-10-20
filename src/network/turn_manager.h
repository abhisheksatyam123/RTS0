#ifndef RTSO_TURN_MANAGER_H
#define RTSO_TURN_MANAGER_H

#include <utility>
#include <cstdint>
#include <map>
#include <set>
#include <vector>
#include <string>
#include <fstream>
#include <cstring>
#include <unordered_map>
#include "net_types.h"
#include "../simulation/simulation.h"

namespace rtso {

class NetHost;

struct TurnFrame {
	uint32_t turn_index = 0;
	uint32_t sim_state_hash = 0;
	std::vector<NetCommand> commands;
};

class TurnManager {
public:
	// TURN_DURATION = 0.200f (200ms = 5 turns/sec) lockstep turn length matching 0 A.D.
	// DEFAULT_TURN_LENGTH / COMMAND_DELAY_MP.
	static constexpr float TURN_DURATION = 0.200f; // 200ms lockstep turns (5 turns/sec) matching 0 A.D. COMMAND_DELAY_MP
	static constexpr int TICKS_PER_TURN = 4;       // 200ms / 50ms FIXED_STEP — NetClientTurnManager NotifyFinishedUpdate

	TurnManager();
	explicit TurnManager(NetHost *host, uint32_t client_id = 0);
	~TurnManager();

	void reset();
	void post_local_command(const NetCommand &cmd);

	// Network synchronization
	void set_net_host(NetHost *net_host, uint32_t client_id = 0);
	NetHost* get_net_host() const { return net_host_; }
	uint32_t local_client_id() const { return local_client_id_; }
	// Lockstep client -> player slot. Defaults to client_id (host 0 = player 0, joiner 1 =
	// player 1); game setup may remap. Ids that map past MAX_PLAYERS issue nothing.
	void set_client_player(uint32_t client_id, uint8_t player) { client_player_[client_id] = player; }
	// Host: a client that dropped out of the running match (0.28 g_Players offline).
	bool client_left(uint32_t client_id) const { return left_clients_.count(client_id) != 0; }
	// Whether client_id's NMT_SYNC_CHECK for turn has come (the host's: it ran that turn).
	bool has_peer_hash(uint32_t turn, uint32_t client_id) const {
		auto it = peer_hashes_.find(turn);
		return it != peer_hashes_.end() && it->second.count(client_id) != 0;
	}
	uint8_t player_for_client(uint32_t client_id) const {
		auto it = client_player_.find(client_id);
		if (it != client_player_.end()) return it->second;
		if (client_id == 0) return offline_seat_;
		return client_id < MAX_PLAYERS ? (uint8_t)client_id : (uint8_t)0xFF;
	}
	// Single player: the slot Game Setup's player assignment seated this machine in
	// (0.28 PlayerAssignment), played by client 0. reset() keeps it; a networked game
	// seats every client explicitly.
	void set_offline_seat(uint8_t player) { offline_seat_ = player < MAX_PLAYERS ? player : (uint8_t)0; }
	uint8_t offline_seat() const { return offline_seat_; }
	uint8_t local_player() const { return player_for_client(local_client_id_); }
	// The developer overlay's Change perspective has re-seated this client (offline too).
	bool perspective_changed() const { return perspective_changed_; }
	// Seat lockstep clients from NMT_GAME_START: client k takes the k-th human
	// ('h') slot, so host 0 gets the first one and joiner 1 the second. Both
	// peers run this on the same setup, so they agree. A client with no human
	// slot left is seated as an observer (0xFF, its commands do nothing).
	void assign_slot_players(const GameSetupParams &setup);
	// The slot each lockstep client 0..7 plays under `setup` (0xFF: it watches).
	// present: the clients to seat (bit k); the rest get 0xFF. NMT_GAME_START
	// carries no such list, so a host seats every connected client explicitly.
	static void seat_clients(const GameSetupParams &setup, uint8_t out[NET_SETUP_CLIENTS], uint64_t present = ~0ull);
	void set_local_client_id(uint32_t id) { local_client_id_ = id; }

	// Post or receive remote commands
	void post_remote_command(uint32_t client_id, const NetCommand &cmd);
	void post_remote_commands(uint32_t client_id, const std::vector<NetCommand> &cmds);
	// True when the batch is new (not a duplicate or a late one); the host relays it.
	bool receive_remote_turn_batch(uint32_t turn, uint32_t client_id, const std::vector<NetCommand> &cmds);
	void receive_sync_check(uint32_t turn, uint32_t client_id, uint32_t hash);

	// --- G8: NMT_GAME_START lockstep gate ------------------------------------
	// Until a well-formed NMT_GAME_START has been APPLIED, tick() refuses to
	// advance the lockstep. This is the network-layer half of the turn-1 OOS fix
	// (presentation mirrors it with net_game_started_): peers that tick before
	// Start are hashing their own INDEPENDENT boot worlds, so turn 1 compares two
	// unrelated simulations and every later turn inherits the offset. A host
	// applies its own setup locally (_on_lobby_start_pressed -> apply_game_setup),
	// so it calls mark_game_started() explicitly; a client applies it when the
	// game layer consumes take_pending_game_start_full() and calls
	// note_game_start_applied(), which take_pending_game_start_full() does for it.
	//
	// Offline play (no net_host_, or role == None) is ungated -- only a session
	// with peers is held closed.
	// Defined in turn_manager.cpp: NetHost is only forward-declared here.
	bool net_game_started() const;
	// Lockstep command delay (0 A.D. NetClientTurnManager COMMAND_DELAY_MP,
	// NetServerTurnManager.cpp:43). A command posted while turn T runs is
	// scheduled for turn T + COMMAND_DELAY on every peer, and no peer runs turn
	// N until it holds every player's batch for N (empty batches included).
	// Turns 1..COMMAND_DELAY have no batches and run unconditionally.
	static constexpr uint32_t COMMAND_DELAY = 4;
	// Client ids whose batch every turn waits on. Defaults on the first networked
	// turn to {0} + connected peer ids (host) or {0, local id} (joiner).
	void set_lockstep_peers(const std::vector<uint32_t> &ids) { lockstep_peers_ = ids; lockstep_peers_set_ = true; }
	// Host: a client left (NetHost disconnect or timeout). Turns stop waiting for
	// its batches (0 A.D. NetServerTurnManager::UninitialiseClient); its seat
	// stays in the match and can be taken back by a rejoining client.
	void drop_client(uint32_t client_id);

	// --- Rejoin of a game in progress (0 A.D. NetServer join-syncing) --------
	// Client: ask the host for the match. The host answers at its next turn
	// boundary T with the saved world and the batches for turns after T (see
	// RejoinMsg); apply_pending_rejoin() then loads it and the client plays on
	// from turn T+1 in its old seat. tick() also applies it.
	// observe: join as an observer instead (0 A.D. observer late join): the
	// client takes no seat, sends no batches and nobody waits on it.
	void request_rejoin(bool observe = false);
	// Host: who may join a running match as an observer (0 A.D.
	// network.lateobservers): everyone (the default), only the players named
	// in lobby.buddies, or nobody. Anyone else is refused with reason 6.
	enum LateObservers : uint8_t { LATE_OBSERVERS_EVERYONE, LATE_OBSERVERS_BUDDIES, LATE_OBSERVERS_DISABLED };
	void set_late_observers(uint8_t mode, std::set<std::string> buddies = {}) {
		late_observers_ = mode;
		buddies_ = std::move(buddies);
	}
	void set_allow_late_observers(bool allow) { late_observers_ = allow ? LATE_OBSERVERS_EVERYONE : LATE_OBSERVERS_DISABLED; }
	// Host: an observer more than this many turns behind the host holds the
	// match until it catches up (0 A.D. network.observermaxlag). -1, the
	// default, never waits on observers; like 0 A.D., above 10000 is -1 too.
	void set_observer_max_lag(int turns) { observer_max_lag_ = turns < 0 || turns > 10000 ? -1 : turns; }
	// 0 A.D. NetworkDelayOverlay.CheckDelay with network.autocatchup: an
	// observer more than OBSERVER_MAX_LIVE_DELAY turns behind runs at
	// OBSERVER_CATCH_UP_RATE times the turn rate, and at the normal rate
	// again once within OBSERVER_NORMAL_DELAY. A rejoiner (a late observer
	// too) first runs the turns queued while it loaded one per tick, until
	// within CATCH_UP_TURNS of them (CNetClient::LoadFinished's fast-forward).
	void set_auto_catch_up(bool on) { auto_catch_up_ = on; }
	static constexpr uint32_t CATCH_UP_TURNS = 4;
	static constexpr uint32_t OBSERVER_MAX_LIVE_DELAY = 6, OBSERVER_NORMAL_DELAY = 3;
	static constexpr float OBSERVER_CATCH_UP_RATE = 1.1f;
	bool observer_catching_up() const { return observer_fast_; }
	// Turns after the current one whose every batch is already here (capped
	// at 64): how far this peer is behind the host.
	uint32_t turns_behind();
	// Turns past the current one with any batch in hand, ready or not (a lost
	// batch may still be on its way): a rejoiner fast-forwards until these run out.
	uint32_t turns_in_hand() const;
	// Host: at most this many observers in a running match (0 A.D.
	// network.observerlimit, 8); one more is refused with reason 7.
	void set_observer_limit(int limit) { observer_limit_ = limit < 0 ? 0 : limit; }
	// This client watches the match without a seat.
	bool is_observer() const;
	bool apply_pending_rejoin(Simulation &sim);
	// --- A saved game in multiplayer (0 A.D. NMT_SAVED_GAME_START) ----------
	// Host: the saved world is already loaded in sim. Every client the host
	// starts with is sent that world as a rejoiner is, and takes the seat given
	// in seats (client id -> player, 0xFF watches); the host plays seats[0].
	// No turn runs until each of them has loaded or left (0 A.D. waits for
	// every client's NMT_LOADED_GAME), with no time limit.
	void start_saved_game(const std::map<uint32_t, uint8_t> &seats, const Simulation *sim);
	bool saved_game() const { return saved_game_; }
	// Host: the clients a saved game still waits on.
	const std::set<uint32_t> &saved_game_waiting() const { return saved_waiting_; }
	bool rejoin_requested() const { return rejoin_wanted_; }
	// Client: the host's NMT_GAME_START seated us, so the request made at
	// connect is moot and must not be asked again (apply_pending_rejoin).
	void cancel_rejoin() { rejoin_wanted_ = false; }
	bool rejoined() const { return rejoined_; }
	uint32_t rejoin_turn() const { return rejoin_turn_; }
	uint8_t rejoin_reject_reason() const { return rejoin_reject_; }
	// Client: the host is sending its world for our rejoin (0 A.D. join_syncing).
	bool rejoin_syncing() const { return rejoin_have_header_ && !rejoined_; }
	// 0 A.D. "Waiting for players": the lockstep clients whose batch the next turn
	// still needs, and how long this peer has been held (0 while turns run).
	std::vector<uint32_t> waiting_on();
	uint64_t waiting_ms() const;
	// Where the saved world is staged on disk (default: the system temp dir).
	void set_rejoin_dir(const std::string &dir) { rejoin_dir_ = dir; }
	const std::vector<uint32_t> &lockstep_peers() const { return lockstep_peers_; }
	// Ticks refused because a peer's batch for the next turn had not arrived.
	uint32_t waiting_tick_count() const { return waiting_ticks_; }
	// reset() for a joiner applying NMT_GAME_START: batches and sync checks the
	// host sent in the same poll as GAME_START must survive, or the joiner waits
	// forever on a turn the host already scheduled.
	void reset_keep_inbound();
	// `sim` is the world the session starts from; it is hashed NOW (boot state) so
	// a late joiner has a turn-0 value it can actually reproduce. Non-owning.
	void mark_game_started(const Simulation *sim = nullptr) {
		net_game_started_ = true;
		if (sim) arm_boot_capture(*sim);
	}
	// Called by take_pending_game_start_full()/take_pending_game_start() once the
	// setup has been handed to the game layer. Recorded in current_turn_ at that
	// moment so a stance or fleet request already in flight is applied on the SAME
	// turn every peer applies it.
	void note_game_start_applied(const Simulation *sim = nullptr) {
		net_game_started_ = true;
		if (apply_base_turn_ > current_turn_) apply_base_turn_ = current_turn_;
		if (sim) arm_boot_capture(*sim);
	}
	void clear_game_started() { net_game_started_ = false; apply_base_turn_ = 0; }
	uint32_t apply_base_turn() const { return apply_base_turn_; }
	// Number of ticks() refused because no GAME_START was applied yet. Diagnostic
	// only: a non-zero value on a peer that never joined a session is the turn-1
	// OOS regression the gate exists to prevent.
	uint32_t gated_tick_count() const { return gated_ticks_; }

	// A stance reaches the sim only as lockstep cmd 22 (see tick()): it runs on
	// the same turn on every peer and as the player the sending client is
	// seated as, so no client can set another player's stances. NMT_DIPLOMACY
	// used to be a second route and could do neither.

	// --- P3: late-join turn-0 state resync ------------------------------------
	// Answer a joiner's NMT_JOIN_SYNC_STATE by resending the authoritative setup
	// and reporting the state the joiner can verify. Returns false when there is no
	// setup to replay, or when the session has already left the sync point (NetHost
	// then replies ERR_JOINED_LATE).
	//
	// WHAT the reported hash means -- and the limit of this scheme:
	//   * requester turn 0 -> the BOOT hash, taken BEFORE turn 1 runs any command or
	//     sim step. The joiner rebuilds turn 0 from the resend and compares against
	//     exactly this value. This is the only comparison that is actually provable,
	//     and it is what catches a joiner whose world differs from the host's
	//     (different seed, map, civs or spawn order).
	//   * requester turn N > 0 -> the POST-turn hash of the last completed turn
	//     (TurnFrame::sim_state_hash, the value compute_sim_hash() returned after
	//     N's sim steps). A joiner resuming at N must therefore replay N turns
	//     locally BEFORE it can compare, and the game layer owns that replay. The
	//     host asserts the hash; it is not a proof until the joiner gets there.
	// The resend never resets the host: it is a pure replay of the attributes it
	// is already broadcasting.
	bool answer_join_sync(uint32_t requester_peer_id, uint32_t requested_turn,
	                      uint32_t &sync_turn_out, uint32_t &sync_hash_out);
	// Window, in COMPLETED lockstep turns, in which a joiner can still be served
	// turn-0 state. 0 = only before the first turn has completed.
	uint32_t join_sync_turn_window() const { return join_sync_turn_window_; }
	void set_join_sync_turn_window(uint32_t turns) { join_sync_turn_window_ = turns; }
	// The boot hash: the state before any turn's commands or sim steps ran.
	uint32_t boot_sim_hash(const Simulation &sim) const { return compute_sim_hash(sim); }
	// Host reply received by a joiner. accepted == false stores the error and
	// opens the gate on a rejection so the joiner's lockstep is never stuck
	// behind a resync that will not arrive.
	bool receive_join_sync(uint32_t sender_peer_id, const TurnStateSnapshotMsg &snap);
	// Joiner closing the handshake with NMT_JOIN_SYNC_CHECK.
	bool receive_join_sync_check(uint32_t sender_peer_id, const JoinSyncCheckMsg &chk);
	// Joiner side: ask the host for the setup + turn-0 state.
	void request_join_sync(uint32_t requested_turn);
	const std::string &last_game_start_attributes() const { return last_game_start_attributes_; }
	uint32_t join_sync_round_trips() const { return join_sync_round_trips_; }
	bool join_sync_accepted() const { return join_sync_accepted_; }
	uint8_t join_sync_error() const { return join_sync_error_; }
	uint32_t join_sync_turn() const { return join_sync_turn_; }
	uint32_t join_sync_hash() const { return join_sync_hash_; }
	// Raised when the rebuilt turn-0 hash does not match the host's: the joiner
	// must NOT enter the lockstep.
	bool join_sync_mismatch() const { return join_sync_mismatch_; }

	// Ticks lockstep turn and advances simulation
	bool tick(float dt, Simulation &sim);

	uint32_t current_turn() const { return current_turn_; }
	// Phase4 HashSerializer parity — detailed OOS hash over ALL SoA fields used for sync.
	// Hashes: pos_x/z, hp, unit_type, team, action_state, carry/capacity, building hp/progress/team/type,
	//         per-player table (stockpiles, tech masks, phase, civ, team), fow_grid_, projectiles count+pos, relics claimed,
	//         territory_grid_ if present, market prices if present. Mirrors 0 A.D. HashSerializer/StdSerializer.
	uint32_t compute_sim_hash(const Simulation &sim) const;

	// Out-of-sync detection status
	bool has_out_of_sync() const { return oos_detected_; }
	uint32_t oos_turn() const { return oos_turn_; }
	uint32_t oos_expected_hash() const { return oos_expected_hash_; }
	uint32_t oos_received_hash() const { return oos_received_hash_; }
	const std::vector<uint32_t>& oos_peers() const { return oos_peers_; }
	void clear_out_of_sync() {
		oos_detected_ = false; oos_turn_ = 0; oos_peers_.clear();
		oos_reported_ = false; oos_rearmed_ = false; oos_dump_pending_ = false; oos_players_.clear();
	}
	// 0 A.D. NetServerTurnManager / NetClientTurnManager::OnSyncError: the FIRST
	// turn whose hashes differ is the one reported (later turns inherit it). The
	// host broadcasts NMT_SYNC_ERROR with its hash and the out-of-sync players;
	// every peer then writes an OOS dump (oos_dump.txt + oos_dump.dat, a full
	// save_state of its world) into oos_dump_dir if one is set.
	const std::vector<std::string>& oos_players() const { return oos_players_; }
	bool oos_reported_by_host() const { return oos_from_host_; }
	// OutOfSyncNetwork's "identical to / differs from the hosts game state": the
	// host's own state is the expected one.
	bool oos_matches_host() const;
	void set_oos_dump_dir(const std::string &dir) { oos_dump_dir_ = dir; }
	// -unique-logs: oos_dump_<unix time>_<pid> in place of oos_dump.
	void set_oos_dump_name(const std::string &name) { oos_dump_name_ = name; }
	const std::string &oos_dump_name() const { return oos_dump_name_; }
	// The readable state an OOS dump carries (0 A.D. DumpDebugState): players' resources,
	// units and buildings, every float bit shown so two dumps diff. -ooslog writes it each turn.
	static void write_state_text(std::ostream &file, const Simulation &sim);
	const std::string& oos_dump_path() const { return oos_dump_written_; }
	bool oos_dump_pending() const { return oos_dump_pending_; }

	// 0 A.D. NMT_CLIENT_PAUSED: any client can pause the match; turns stop on
	// every peer while at least one client is paused, and resume when the last
	// one unpauses. Sends the change to the host, which relays it.
	void set_local_paused(bool paused);
	bool is_paused() const { return !paused_clients_.empty(); }
	bool client_paused(uint32_t client_id) const { return paused_clients_.count(client_id) != 0; }
	const std::set<uint32_t>& paused_clients() const { return paused_clients_; }
	// 0 A.D. NMT_REJOINED: the clients that rejoined the running match, in
	// order, once each has loaded ("%(player)s has rejoined the game.").
	// Never cleared, so a reader keeps its own count.
	const std::vector<uint32_t>& rejoined_clients() const { return rejoined_clients_; }

	// Replay records the sorted command batch and the post-turn simulation hash.
	// This is a turn recorder/verifier, not a portable simulation-state snapshot.
	static constexpr size_t REPLAY_MAX_FILE_BYTES = 64u * 1024u * 1024u;
	static constexpr uint32_t REPLAY_MAX_FRAMES = 1000000u;
	static constexpr uint32_t REPLAY_MAX_COMMANDS_PER_FRAME = 65535u;
	static constexpr uint32_t REPLAY_MAX_ENTITIES_PER_COMMAND = 65535u;

	// Writers use v3 with an FNV-1a content checksum. v1/v2 remain readable for
	// compatibility, but cannot provide file-integrity validation.
	bool save_replay(const std::string &path) const;
	// Parses into temporary storage and commits only after the entire file is
	// valid. A loaded replay is terminal: playback cannot silently return live.
	bool validate_replay(const std::string &path, std::string &error_out) const;
	bool load_replay(const std::string &path);

	bool is_replaying() const { return is_replaying_; }
	bool replay_complete() const { return replay_complete_; }
	bool replay_out_of_sync() const { return replay_oos_; }
	uint32_t replay_out_of_sync_turn() const { return replay_oos_turn_; }
	uint32_t replay_expected_hash() const { return replay_expected_hash_; }
	uint32_t replay_computed_hash() const { return replay_computed_hash_; }
	const std::string &last_replay_error() const { return last_replay_error_; }

	// Accessor for verification / debugging.
	const std::vector<TurnFrame>& turn_history() const { return turn_history_; }

	// The developer overlay's rejoin test (0.28 Engine.ActivateRejoinTest): this turn manager
	// replays `live`'s turns, one push_shadow_frame per turn, onto a simulation loaded from a
	// snapshot of live's, and its replay check (replay_out_of_sync) compares each turn's hash.
	void begin_shadow_replay(const TurnManager &live) {
		client_player_ = live.client_player_;
		turn_history_.clear();
		is_replaying_ = true;
		replay_loaded_ = true;
		replay_complete_ = false;
		replay_oos_ = false;
		replay_oos_turn_ = 0;
		replay_frame_idx_ = 0;
		current_turn_ = live.current_turn_;
		turn_timer_ = 0.0f;
	}
	void push_shadow_frame(const TurnFrame &f) { turn_history_.push_back(f); }
	// The turn just run, carrying the hash this simulation computed after it (a replay's
	// recorded hash may differ), or false before the first turn.
	bool last_turn_frame(TurnFrame &out) const {
		if (is_replaying_) {
			if (replay_frame_idx_ == 0 || replay_frame_idx_ > turn_history_.size()) return false;
			out = turn_history_[replay_frame_idx_ - 1];
			out.sim_state_hash = replay_computed_hash_;
			return true;
		}
		if (turn_history_.empty()) return false;
		out = turn_history_.back();
		return true;
	}

	// Consume a fully parsed NMT_GAME_START payload. The game layer owns
	// simulation setup (map loading and initial spawn), so the network layer
	// only transfers the validated setup atomically.
	bool take_pending_game_start_full(GameSetupParams &out) {
		if (!has_pending_game_start_) return false;
		out = pending_game_start_;
		has_pending_game_start_ = false;
		pending_game_start_ = GameSetupParams();
		note_game_start_applied(game_start_applied_sim_);
		return true;
	}
	bool has_pending_game_start() const { return has_pending_game_start_; }
	// Legacy map/seed-only consumer for old 0 A.D. launch attributes.
	bool take_pending_game_start(std::string &map_id_out, uint32_t &seed_out) {
		if (!has_pending_game_start_) return false;
		map_id_out = pending_game_start_.map_id;
		seed_out = pending_game_start_.seed;
		has_pending_game_start_ = false;
		pending_game_start_ = GameSetupParams();
		note_game_start_applied(game_start_applied_sim_);
		return true;
	}
	// The world the GAME_START setup was applied to. The game layer sets it right
	// after building the map (apply_game_setup) and before consuming the pending
	// setup, so take_pending_game_start_full() can hash the boot world for the
	// late-join turn-0 proof.
	void set_game_start_applied_sim(const Simulation *sim) { game_start_applied_sim_ = sim; }

	// 0 A.D. "playercommand" notifications, for the session's Follow Player: while on,
	// each executed command is kept with its player (and only the units it owns) until
	// the session takes them. Presentation only; nothing here reaches the simulation.
	struct AppliedCommand {
		uint8_t player;
		NetCommand cmd;
	};
	void set_record_applied(bool on) {
		record_applied_ = on;
		if (!on) applied_cmds_.clear();
	}
	std::vector<AppliedCommand> take_applied_commands() { return std::exchange(applied_cmds_, {}); }

private:
	bool record_applied_ = false;
	bool perspective_changed_ = false;
	uint8_t offline_seat_ = 0;
	std::vector<AppliedCommand> applied_cmds_;
	uint32_t current_turn_ = 0;
	float turn_timer_ = 0.0f;
	std::vector<NetCommand> pending_local_cmds_;
	std::vector<TurnFrame> turn_history_;
	bool is_replaying_ = false;
	bool replay_loaded_ = false;
	bool replay_complete_ = false;
	bool replay_oos_ = false;
	uint32_t replay_oos_turn_ = 0;
	uint32_t replay_expected_hash_ = 0;
	uint32_t replay_computed_hash_ = 0;
	std::string last_replay_error_;
	size_t replay_frame_idx_ = 0;

	// Shared by save/load/validation. Legacy formats contain the same fields;
	// v3 appends an FNV-1a checksum of every preceding byte.
	bool write_replay_file(const std::string &path) const;
	bool read_replay_file(const std::string &path, std::vector<TurnFrame> &out,
	                      std::string &error_out) const;

	// Network state
	NetHost *net_host_ = nullptr;
	uint32_t local_client_id_ = 0;
	std::unordered_map<uint32_t, uint8_t> client_player_;
	// Pending NMT_GAME_START seed-sync (P1 playtest gate). Set by the
	// onMessage NMT_GAME_START branch; consumed by the game layer, which owns
	// map loading (Simulation::generate_random_map). Cleared on reset() and
	// on take_pending_game_start*().
	bool has_pending_game_start_ = false;
	GameSetupParams pending_game_start_;
	std::vector<NetCommand> pending_remote_cmds_;
	std::unordered_map<uint32_t, std::vector<NetCommand>> turn_remote_cmds_;
	std::unordered_map<uint32_t, std::unordered_map<uint32_t, uint32_t>> peer_hashes_;
	std::unordered_map<uint32_t, std::vector<uint32_t>> received_batches_; // turn -> client ids
	std::vector<uint32_t> lockstep_peers_;
	bool lockstep_peers_set_ = false;
	uint32_t waiting_ticks_ = 0;
	bool lockstep_net_active() const;
	bool turn_ready(uint32_t turn);

	bool oos_detected_ = false;
	uint32_t oos_turn_ = 0;
	uint32_t oos_expected_hash_ = 0;
	uint32_t oos_received_hash_ = 0;
	std::vector<uint32_t> oos_peers_;
	std::vector<std::string> oos_players_;
	bool oos_reported_ = false;    // host: NMT_SYNC_ERROR sent
	bool oos_rearmed_ = false;     // host: every out-of-sync client left; the next divergence is a new report
	bool oos_from_host_ = false;   // client: turn/hash came from the host's NMT_SYNC_ERROR
	bool oos_dump_pending_ = false;
	std::string oos_dump_dir_;
	std::string oos_dump_name_ = "oos_dump";
	std::string oos_dump_written_;
	void note_out_of_sync(uint32_t turn, uint32_t local_hash, uint32_t peer_hash, uint32_t peer_id);
	void receive_sync_error(const SyncErrorMsg &msg);
	void write_oos_dump(const Simulation &sim);
	uint32_t local_hash_at(uint32_t turn) const;
	std::string client_label(uint32_t client_id) const;
	std::set<uint32_t> paused_clients_;
	std::vector<uint32_t> rejoined_clients_;
	std::set<uint32_t> left_clients_; // host: clients drop_client removed; their hashes are ignored
	// Rejoin: per-peer first turn whose batch is required (a rejoiner sends
	// from T+COMMAND_DELAY+1), host-side requests, and the client's transfer.
	std::unordered_map<uint32_t, uint32_t> lockstep_since_;
	// A client that left: the last turn the lockstep still waits for its batch.
	std::unordered_map<uint32_t, uint32_t> lockstep_until_;
	// Host: seat changes (NET_CMD_PEER_JOINED / NET_CMD_PEER_LEFT) for its next batch.
	std::vector<NetCommand> pending_control_;
	bool peer_active(uint32_t id) const;
	uint64_t stall_since_ms_ = 0;
	void apply_peer_left(uint32_t client_id, uint32_t last_turn);
	void apply_peer_control(const NetCommand &c);
	std::vector<uint32_t> rejoin_requests_;
	std::set<uint32_t> observer_requests_; // host: rejoin requests that asked to observe
	bool rejoin_observe_ = false;
	uint8_t late_observers_ = LATE_OBSERVERS_EVERYONE;
	std::set<std::string> buddies_; // lowercase
	int observer_limit_ = 8;
	int observer_max_lag_ = -1;
	bool auto_catch_up_ = true;
	bool observer_fast_ = false;     // running at OBSERVER_CATCH_UP_RATE
	bool rejoin_fast_forward_ = false; // still running the turns queued while loading
	std::map<uint32_t, uint32_t> observer_turn_; // host: each observer's newest sync-check turn
	bool lagging_observer(uint32_t client) const;
	// Host: the client list last sent as NMT_PLAYER_ASSIGNMENT, and every
	// client's name as last seen (a client that left keeps it in the list).
	std::vector<PlayerAssignmentMsg::Entry> sent_assignment_;
	// Host: rejoining players served a world but not yet loaded (seat by
	// client). Nobody waits on them until they say they loaded (0 A.D. adds a
	// rejoiner to the turn manager on NMT_LOADED_GAME), so the match runs on
	// through the transfer.
	std::map<uint32_t, uint8_t> rejoin_loading_;
	bool saved_game_ = false;
	std::map<uint32_t, uint8_t> saved_seats_; // host: the seat each client of a saved game takes
	std::set<uint32_t> saved_waiting_;        // host: clients of a saved game not yet loaded
	uint64_t saved_since_ms_ = 0;
	bool saved_game_hold();
	// Client: loaded a rejoin world as a player, waiting for the host's LOCKSTEP.
	bool rejoin_lockstep_wait_ = false;
	void admit_rejoiner(uint32_t client_id);
	void send_rejoin_msg(uint32_t to, const RejoinMsg &m);
	std::unordered_map<uint32_t, std::string> client_names_;
	void send_player_assignment();
	bool rejoin_wanted_ = false, rejoin_have_header_ = false, rejoined_ = false;
	bool rejoin_announced_ = false; // the host's CMD_PEER_JOINED for us has arrived
	uint32_t rejoin_turn_ = 0;
	uint8_t rejoin_reject_ = 0;
	RejoinMsg rejoin_header_;
	std::vector<uint8_t> rejoin_world_;
	std::set<uint32_t> rejoin_chunks_;
	// Chunks that came before their header (reliable delivery keeps no
	// order): they are acked, so the host never resends them.
	std::vector<RejoinMsg> rejoin_early_;
	void take_rejoin_chunk(const RejoinMsg &msg);
	size_t rejoin_received_ = 0;
	std::string rejoin_dir_;
	// Host: world chunks still to send, paced so a ~500-packet burst does not
	// overflow the rejoiner's socket buffer. Client: last transfer progress, to
	// ask again when a transfer stalls.
	std::vector<std::pair<uint32_t, std::vector<uint8_t>>> rejoin_outbox_;
	size_t rejoin_outbox_next_ = 0;
	uint64_t rejoin_progress_ms_ = 0;
	static constexpr size_t REJOIN_CHUNKS_PER_TICK = 16;
	static constexpr uint64_t REJOIN_RETRY_MS = 5000;
	void ensure_lockstep_peers();
	std::string rejoin_path(const char *tag) const;
	void receive_rejoin(uint32_t author, const RejoinMsg &msg);
	void serve_rejoin(uint32_t client_id, Simulation &sim);
	void receive_client_paused(uint32_t author, const ClientPausedMsg &msg);

	// --- G8 lockstep gate (see the public section for the rationale) ---------
	bool net_game_started_ = false;
	uint32_t apply_base_turn_ = 0;  // turn recorded when GAME_START was applied
	uint32_t gated_ticks_ = 0;     // ticks refused for want of a GAME_START
	uint32_t boot_hash_ = 0;       // pre-turn state hash (late-join turn-0 proof)
	bool boot_hash_captured_ = false;
	const Simulation *sim_boot_hash_ptr_ = nullptr; // non-owning, next tick() only
	const Simulation *game_start_applied_sim_ = nullptr; // non-owning
	std::string last_game_start_attributes_; // authoritative setup kept for resend


	// --- P3 late-join state ---------------------------------------------------
	uint32_t join_sync_round_trips_ = 0;
	bool join_sync_accepted_ = false;
	uint8_t join_sync_error_ = 0;
	uint32_t join_sync_turn_ = 0;
	uint32_t join_sync_hash_ = 0;
	bool join_sync_mismatch_ = false;
	// How many completed turns a late joiner may still be served turn-0 state for.
	// Deliberately 0: RTS0 has no state snapshot (see the MULTIPLAYER.md
	// Rejoin/FileTransfer gap), so once turn 1 has completed the host can no longer
	// prove what a joiner will rebuild, and guessing would resurface the turn-1 OOS
	// in a new place. Raise it once real snapshots exist.
	uint32_t join_sync_turn_window_ = 0;

	// Hash the boot world for the late-join turn-0 proof (see arm_boot_capture).
	void arm_boot_capture(const Simulation &sim);
};

} // namespace rtso

#endif

