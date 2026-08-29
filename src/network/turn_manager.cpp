#include "turn_manager.h"
#include <iostream>

namespace rtso {

TurnManager::TurnManager() {
	reset();
}

TurnManager::~TurnManager() = default;

void TurnManager::reset() {
	current_turn_ = 0;
	turn_timer_ = 0.0f;
	pending_local_cmds_.clear();
	turn_history_.clear();
	is_replaying_ = false;
	replay_frame_idx_ = 0;
}

void TurnManager::post_local_command(const NetCommand &cmd) {
	if (is_replaying_) return;
	pending_local_cmds_.push_back(cmd);
}

uint32_t TurnManager::compute_sim_hash(const Simulation &sim) const {
	// Simple FNV-1a state hash of unit positions & resources for deterministic sync check
	uint32_t hash = 2166136261u;
	auto uview = sim.units();
	for (size_t i = 0; i < uview.count; ++i) {
		if (!uview.alive[i]) continue;
		uint32_t x_bits, z_bits, hp_bits;
		float px = uview.pos_x[i];
		float pz = uview.pos_z[i];
		float php = uview.hp[i];
		std::memcpy(&x_bits, &px, sizeof(float));
		std::memcpy(&z_bits, &pz, sizeof(float));
		std::memcpy(&hp_bits, &php, sizeof(float));

		hash = (hash ^ x_bits) * 16777619u;
		hash = (hash ^ z_bits) * 16777619u;
		hash = (hash ^ hp_bits) * 16777619u;
		hash = (hash ^ (uint32_t)uview.action_state[i]) * 16777619u;
	}
	hash = (hash ^ (uint32_t)sim.food()) * 16777619u;
	hash = (hash ^ (uint32_t)sim.wood()) * 16777619u;
	hash = (hash ^ (uint32_t)sim.gold()) * 16777619u;
	hash = (hash ^ (uint32_t)sim.stone()) * 16777619u;
	return hash;
}

bool TurnManager::tick(float dt, Simulation &sim) {
	turn_timer_ += dt;
	if (turn_timer_ < TURN_DURATION) return false;

	turn_timer_ -= TURN_DURATION;
	current_turn_++;

	TurnFrame frame;
	frame.turn_index = current_turn_;

	if (is_replaying_) {
		if (replay_frame_idx_ < turn_history_.size()) {
			frame = turn_history_[replay_frame_idx_++];
		}
	} else {
		frame.commands = std::move(pending_local_cmds_);
		pending_local_cmds_.clear();
	}

	// Execute turn commands into simulation
	for (const auto &c : frame.commands) {
		if (c.cmd_type == 1) { // Move
			sim.cmd_move(c.entity_ids.data(), c.entity_ids.size(), c.target_x, c.target_z);
		} else if (c.cmd_type == 2) { // Stop
			sim.cmd_stop(c.entity_ids.data(), c.entity_ids.size());
		} else if (c.cmd_type == 3) { // Attack
			sim.order_attack(c.entity_ids.data(), c.entity_ids.size(), c.target_id);
		} else if (c.cmd_type == 4) { // Gather
			sim.order_gather(c.entity_ids.data(), c.entity_ids.size(), c.target_id);
		} else if (c.cmd_type == 5) { // Build
			sim.cmd_build((int)c.target_x, (int)c.target_z, c.param);
		} else if (c.cmd_type == 6) { // Train
			sim.cmd_train(c.target_id, (UnitType)c.param);
		} else if (c.cmd_type == 7) { // Stance
			sim.order_set_stance(c.entity_ids.data(), c.entity_ids.size(), c.param);
		} else if (c.cmd_type == 8) { // Formation
			sim.order_set_formation(c.entity_ids.data(), c.entity_ids.size(), c.param);
		}
	}

	frame.sim_state_hash = compute_sim_hash(sim);

	if (!is_replaying_) {
		turn_history_.push_back(frame);
	}
	return true;
}

bool TurnManager::save_replay(const std::string &path) const {
	std::ofstream out(path, std::ios::binary);
	if (!out.is_open()) return false;

	uint32_t magic = 0x52545330; // "RTS0"
	uint32_t version = 1;
	out.write((const char*)&magic, sizeof(magic));
	out.write((const char*)&version, sizeof(version));

	uint32_t frame_count = (uint32_t)turn_history_.size();
	out.write((const char*)&frame_count, sizeof(frame_count));

	for (const auto &f : turn_history_) {
		out.write((const char*)&f.turn_index, sizeof(f.turn_index));
		out.write((const char*)&f.sim_state_hash, sizeof(f.sim_state_hash));

		uint32_t cmd_count = (uint32_t)f.commands.size();
		out.write((const char*)&cmd_count, sizeof(cmd_count));
		for (const auto &c : f.commands) {
			out.write((const char*)&c.client_id, sizeof(c.client_id));
			out.write((const char*)&c.cmd_type, sizeof(c.cmd_type));
			out.write((const char*)&c.target_x, sizeof(c.target_x));
			out.write((const char*)&c.target_z, sizeof(c.target_z));
			out.write((const char*)&c.target_id, sizeof(c.target_id));
			out.write((const char*)&c.param, sizeof(c.param));

			uint32_t entity_count = (uint32_t)c.entity_ids.size();
			out.write((const char*)&entity_count, sizeof(entity_count));
			if (entity_count > 0) {
				out.write((const char*)c.entity_ids.data(), entity_count * sizeof(uint32_t));
			}
		}
	}
	return true;
}

bool TurnManager::load_replay(const std::string &path) {
	std::ifstream in(path, std::ios::binary);
	if (!in.is_open()) return false;

	uint32_t magic = 0, version = 0, frame_count = 0;
	in.read((char*)&magic, sizeof(magic));
	in.read((char*)&version, sizeof(version));
	if (magic != 0x52545330 || version != 1) return false;

	in.read((char*)&frame_count, sizeof(frame_count));
	turn_history_.clear();
	turn_history_.reserve(frame_count);

	for (uint32_t i = 0; i < frame_count; ++i) {
		TurnFrame f;
		in.read((char*)&f.turn_index, sizeof(f.turn_index));
		in.read((char*)&f.sim_state_hash, sizeof(f.sim_state_hash));

		uint32_t cmd_count = 0;
		in.read((char*)&cmd_count, sizeof(cmd_count));
		f.commands.resize(cmd_count);
		for (uint32_t c = 0; c < cmd_count; ++c) {
			NetCommand &cmd = f.commands[c];
			in.read((char*)&cmd.client_id, sizeof(cmd.client_id));
			in.read((char*)&cmd.cmd_type, sizeof(cmd.cmd_type));
			in.read((char*)&cmd.target_x, sizeof(cmd.target_x));
			in.read((char*)&cmd.target_z, sizeof(cmd.target_z));
			in.read((char*)&cmd.target_id, sizeof(cmd.target_id));
			in.read((char*)&cmd.param, sizeof(cmd.param));

			uint32_t entity_count = 0;
			in.read((char*)&entity_count, sizeof(entity_count));
			cmd.entity_ids.resize(entity_count);
			if (entity_count > 0) {
				in.read((char*)cmd.entity_ids.data(), entity_count * sizeof(uint32_t));
			}
		}
		turn_history_.push_back(std::move(f));
	}

	is_replaying_ = true;
	replay_frame_idx_ = 0;
	current_turn_ = 0;
	turn_timer_ = 0.0f;
	return true;
}

} // namespace rtso
