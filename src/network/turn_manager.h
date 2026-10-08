#ifndef RTSO_TURN_MANAGER_H
#define RTSO_TURN_MANAGER_H

#include <cstdint>
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
	void set_local_client_id(uint32_t id) { local_client_id_ = id; }

	// Post or receive remote commands
	void post_remote_command(uint32_t client_id, const NetCommand &cmd);
	void post_remote_commands(uint32_t client_id, const std::vector<NetCommand> &cmds);
	void receive_remote_turn_batch(uint32_t turn, uint32_t client_id, const std::vector<NetCommand> &cmds);
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
	static constexpr uint32_t COMMAND_DELAY = 2;
	// Client ids whose batch every turn waits on. Defaults on the first networked
	// turn to {0} + connected peer ids (host) or {0, local id} (joiner).
	void set_lockstep_peers(const std::vector<uint32_t> &ids) { lockstep_peers_ = ids; lockstep_peers_set_ = true; }
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

	// --- G8/G9: NMT_DIPLOMACY routed through the lockstep turn boundary -------
	// Queued by the onMessage NMT_DIPLOMACY branch, applied by tick() on turn
	// apply_base_turn_ + DIPLOMACY_APPLY_DELAY. The sender applies locally
	// through the same queue (post_diplomacy), so host and client run identical
	// code for host-originated and client-originated stances.
	// DIPLOMACY_APPLY_DELAY 1 == the inbound NMT_SIMULATION_COMMAND batch for the
	// same turn rides the same reliable channel ahead of the stance (TurnManager
	// sends SIMULATION_COMMAND before END_COMMAND_BATCH in tick()), so a stance
	// ordered on turn N must not be applied on turn N or it would jump the
	// command batch it is sorted into. Keep this in lockstep with the ordering in
	// tick(); the wire format does not depend on it, but the APPLY ORDER does.
	static constexpr uint32_t DIPLOMACY_APPLY_DELAY = 1;
	bool post_diplomacy(uint8_t player_id, uint8_t stance, uint32_t origin_turn);
	uint32_t pending_diplomacy_count() const { return (uint32_t)pending_diplomacy_.size(); }
	// Turn the stance recorded by the last apply_diplomacy(), for verification.
	uint8_t last_applied_diplomacy(uint8_t player_id) const;

	// --- P3: maintainFleet upkeep (navalManager.js:507) ----------------------
	// Commission one warship / fishing boat per docking structure, once per turn.
	// Commissions are produced DETERMINISTICALLY from the sim state at the turn
	// boundary (see maintain_fleet_upkeep) and converted into ordinary
	// NET_CMD_MAINTAIN_FLEET NetCommands by request_fleet_upkeep(), so they ride
	// the existing NMT_SIMULATION_COMMAND path and land in the replay stream.
	// No src/simulation edit is required; see the TODO in turn_manager.cpp for the
	// optional sim-side cmd_maintain_fleet hook.
	uint32_t fleet_upkeep_requests() const { return fleet_upkeep_requests_; }
	// Feed a live / post-command-later (0 A.D. maintenance pause) policy into
	// tick(). main_node.cpp is owned by another stream, so this is the seam it
	// can call from the Petra naval pass.
	void set_fleet_upkeep_paused(bool paused) { fleet_upkeep_paused_ = paused; }
	bool fleet_upkeep_paused() const { return fleet_upkeep_paused_; }
	// Public for verification: returns the NET_CMD_MAINTAIN_FLEET NetCommands the
	// upkeep would commission for `sim` right now (empty when any gate blocks).
	// tick() feeds exactly these to maintain_fleet_upkeep().
	std::vector<NetCommand> generate_fleet_upkeep_commands(const Simulation &sim) const;

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
	//         resources food/wood/gold/stone, researched_techs_mask_, fow_grid_, projectiles count+pos, relics claimed,
	//         territory_grid_ if present, market prices if present. Mirrors 0 A.D. HashSerializer/StdSerializer.
	uint32_t compute_sim_hash(const Simulation &sim) const;

	// Out-of-sync detection status
	bool has_out_of_sync() const { return oos_detected_; }
	uint32_t oos_turn() const { return oos_turn_; }
	uint32_t oos_expected_hash() const { return oos_expected_hash_; }
	uint32_t oos_received_hash() const { return oos_received_hash_; }
	const std::vector<uint32_t>& oos_peers() const { return oos_peers_; }
	void clear_out_of_sync() { oos_detected_ = false; oos_turn_ = 0; oos_peers_.clear(); }

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

private:
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

	// --- G8 lockstep gate (see the public section for the rationale) ---------
	bool net_game_started_ = false;
	uint32_t apply_base_turn_ = 0;  // turn recorded when GAME_START was applied
	uint32_t gated_ticks_ = 0;     // ticks refused for want of a GAME_START
	uint32_t boot_hash_ = 0;       // pre-turn state hash (late-join turn-0 proof)
	bool boot_hash_captured_ = false;
	const Simulation *sim_boot_hash_ptr_ = nullptr; // non-owning, next tick() only
	const Simulation *game_start_applied_sim_ = nullptr; // non-owning
	std::string last_game_start_attributes_; // authoritative setup kept for resend

	// --- G8/G9 deferred stance applications -----------------------------------
	// Ordered by (turn, player_id, sender_peer_id) on insert, so every peer
	// applies the same stances in the same order inside tick(). Deliberately NOT
	// sorted by sender: cmd_type 22 NetCommands are ordered by client_id inside the
	// turn batch (see tick's stable_sort), and client_id order is the sim's
	// authoritative command order.
	struct PendingDiplomacy {
		uint32_t turn = 0;
		uint8_t player_id = 0;
		uint8_t stance = 1;
		uint32_t sender_peer_id = 0;
	};
	std::vector<PendingDiplomacy> pending_diplomacy_;
	uint8_t last_applied_diplomacy_[8] = {};
	bool has_applied_diplomacy_ = false;

	// --- P3 maintainFleet ------------------------------------------------------
	uint32_t fleet_upkeep_requests_ = 0;
	bool fleet_upkeep_paused_ = false;

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

	// Turn-boundary helpers. apply_diplomacy() is the single sim write point for
	// a stance; maintain_fleet_upkeep() is the single command generation point.
	void apply_diplomacy(const std::vector<PendingDiplomacy> &batch, Simulation &sim);
	void maintain_fleet_upkeep(Simulation &sim);
};

// Two commands are interchangeable for the upkeep (and for the turn batch) when
// they address the same action on the same target. Peer equality of the generated
// upkeep is asserted with this rather than with vector::operator==, so a future
// field on NetCommand does not silently change what "identical" means.
inline bool fleet_upkeep_command_equal(const NetCommand &a, const NetCommand &b) {
	return a.cmd_type == b.cmd_type && a.client_id == b.client_id &&
	       a.target_id == b.target_id && a.param == b.param &&
	       a.entity_ids == b.entity_ids;
}

} // namespace rtso

#endif

