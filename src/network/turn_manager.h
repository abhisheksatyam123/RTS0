#ifndef RTSO_TURN_MANAGER_H
#define RTSO_TURN_MANAGER_H

#include <cstdint>
#include <vector>
#include <string>
#include <fstream>
#include "../simulation/simulation.h"

namespace rtso {

struct NetCommand {
	uint32_t client_id = 0;
	uint8_t cmd_type = 0; // 1=Move, 2=Stop, 3=Attack, 4=Gather, 5=Build, 6=Train, 7=Stance, 8=Formation
	std::vector<uint32_t> entity_ids;
	float target_x = 0.0f;
	float target_z = 0.0f;
	uint32_t target_id = 0;
	uint8_t param = 0;
};

struct TurnFrame {
	uint32_t turn_index = 0;
	uint32_t sim_state_hash = 0;
	std::vector<NetCommand> commands;
};

class TurnManager {
public:
	static constexpr float TURN_DURATION = 0.200f; // 200ms lockstep turns (5 turns/sec) matching 0 A.D.

	TurnManager();
	~TurnManager();

	void reset();
	void post_local_command(const NetCommand &cmd);
	bool tick(float dt, Simulation &sim);

	uint32_t current_turn() const { return current_turn_; }
	uint32_t compute_sim_hash(const Simulation &sim) const;

	// Replay recording & playback
	bool save_replay(const std::string &path) const;
	bool load_replay(const std::string &path);
	bool is_replaying() const { return is_replaying_; }

private:
	uint32_t current_turn_ = 0;
	float turn_timer_ = 0.0f;
	std::vector<NetCommand> pending_local_cmds_;
	std::vector<TurnFrame> turn_history_;
	bool is_replaying_ = false;
	size_t replay_frame_idx_ = 0;
};

} // namespace rtso

#endif
