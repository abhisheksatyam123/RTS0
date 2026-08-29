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

	// Replay recording & playback — saves TurnFrame{turn_index, sim_state_hash, commands} per turn.
	// save_replay writes hash per frame + turn_index ordering (monotonic). load_replay verifies hash/ordering.
	bool save_replay(const std::string &path) const;
	bool load_replay(const std::string &path);
	bool is_replaying() const { return is_replaying_; }

	// Accessor for verification / debugging
	const std::vector<TurnFrame>& turn_history() const { return turn_history_; }

private:
	uint32_t current_turn_ = 0;
	float turn_timer_ = 0.0f;
	std::vector<NetCommand> pending_local_cmds_;
	std::vector<TurnFrame> turn_history_;
	bool is_replaying_ = false;
	size_t replay_frame_idx_ = 0;

	// Network state
	NetHost *net_host_ = nullptr;
	uint32_t local_client_id_ = 0;
	std::vector<NetCommand> pending_remote_cmds_;
	std::unordered_map<uint32_t, std::vector<NetCommand>> turn_remote_cmds_;
	std::unordered_map<uint32_t, std::unordered_map<uint32_t, uint32_t>> peer_hashes_;

	bool oos_detected_ = false;
	uint32_t oos_turn_ = 0;
	uint32_t oos_expected_hash_ = 0;
	uint32_t oos_received_hash_ = 0;
	std::vector<uint32_t> oos_peers_;
};

} // namespace rtso

#endif

