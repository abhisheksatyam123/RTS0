#include "turn_manager.h"
#include "net_host.h"
#include <iostream>
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

void TurnManager::reset() {
	current_turn_ = 0;
	turn_timer_ = 0.0f;
	pending_local_cmds_.clear();
	pending_remote_cmds_.clear();
	turn_remote_cmds_.clear();
	peer_hashes_.clear();
	turn_history_.clear();
	is_replaying_ = false;
	replay_frame_idx_ = 0;
	oos_detected_ = false;
	oos_turn_ = 0;
	oos_expected_hash_ = 0;
	oos_received_hash_ = 0;
	oos_peers_.clear();
}

void TurnManager::set_net_host(NetHost *net_host, uint32_t client_id) {
	net_host_ = net_host;
	local_client_id_ = client_id;
	if (net_host_) {
		net_host_->onCommand([this](uint32_t peer_id, const uint8_t *data, size_t size) {
			SimulationCommandMsg scm;
			if (NetPacketSerializer::parse_simulation_command(data, size, scm)) {
				this->receive_remote_turn_batch(scm.turn, scm.client_id != 0 ? scm.client_id : peer_id, scm.commands);
			}
		});

		net_host_->onMessage([this](uint32_t peer_id, const NetMessage &msg) {
			if (msg.type == static_cast<uint8_t>(NetMessageType::NMT_SIMULATION_COMMAND)) {
				SimulationCommandMsg scm;
				if (NetPacketSerializer::parse_simulation_command(msg.payload.data(), msg.payload.size(), scm)) {
					this->receive_remote_turn_batch(scm.turn != 0 ? scm.turn : msg.turn, scm.client_id != 0 ? scm.client_id : peer_id, scm.commands);
				}
			} else if (msg.type == static_cast<uint8_t>(NetMessageType::NMT_SYNC_CHECK)) {
				SyncCheckMsg scm;
				if (NetPacketSerializer::parse_sync_check(msg.payload.data(), msg.payload.size(), scm)) {
					this->receive_sync_check(scm.turn != 0 ? scm.turn : msg.turn, peer_id, scm.hash);
				}
			}
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

void TurnManager::receive_remote_turn_batch(uint32_t turn, uint32_t client_id, const std::vector<NetCommand> &cmds) {
	if (is_replaying_) return;
	for (auto c : cmds) {
		c.client_id = client_id;
		turn_remote_cmds_[turn].push_back(c);
	}
}

void TurnManager::receive_sync_check(uint32_t turn, uint32_t client_id, uint32_t hash) {
	peer_hashes_[turn][client_id] = hash;
	if (turn <= current_turn_ && turn > 0) {
		for (const auto &f : turn_history_) {
			if (f.turn_index == turn) {
				if (f.sim_state_hash != 0 && f.sim_state_hash != hash) {
					oos_detected_ = true;
					oos_turn_ = turn;
					oos_expected_hash_ = f.sim_state_hash;
					oos_received_hash_ = hash;
					if (std::find(oos_peers_.begin(), oos_peers_.end(), client_id) == oos_peers_.end()) {
						oos_peers_.push_back(client_id);
					}
					std::cerr << "[TurnManager] Out-of-sync detected at turn " << turn
					          << " local_hash=" << f.sim_state_hash
					          << " peer=" << client_id << " peer_hash=" << hash << "\n";
				}
				break;
			}
		}
	}
}

uint32_t TurnManager::compute_sim_hash(const Simulation &sim) const {
	// Phase4 HashSerializer parity — comprehensive FNV-1a state hashing for lockstep desync verification.
	// Hashes all unit SoA columns (pos_x, pos_z, hp, action_state, unit_type, team, carry, kills, rank, status_effects),
	// buildings (hp, build_progress, type, team, capture_points), resource nodes (amount, alive, type),
	// player and AI stockpiles (Food, Wood, Gold, Stone), researched tech masks (researched_techs_mask_, ai_researched_techs_mask_),
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
		mix_float(b.max_capture_points);
		mix_u32((uint32_t)b.queue.size());
		mix_u32((uint32_t)b.garrisoned_units.size());
		mix_u8(b.is_burning ? 1 : 0);
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

	// 4. Player and AI Stockpiles (Food, Wood, Gold, Stone)
	mix_i32(sim.food());
	mix_i32(sim.wood());
	mix_i32(sim.gold());
	mix_i32(sim.stone());
	mix_float(sim.ai_food());
	mix_float(sim.ai_wood());
	mix_float(sim.ai_gold());
	mix_float(sim.ai_stone());

	// 5. Researched Tech Masks & Game Phases
	mix_u32(sim.researched_techs_mask());
	mix_u32(sim.ai_researched_techs_mask());
	mix_u8(sim.player_phase());
	mix_u8((uint8_t)sim.petra_phase());

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
		mix_u8(r.res_type);
		mix_i32(r.amount);
		mix_u8(r.claimed ? 1 : 0);
	}

	// 7. FoW Grid
	const uint8_t *fow = sim.fow_grid();
	for (int i = 0; i < Simulation::FOW_GRID * Simulation::FOW_GRID; ++i) {
		if ((i & 3) == 0 && i + 3 < Simulation::FOW_GRID * Simulation::FOW_GRID) {
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

	// 9. Ticks & Wave state
	mix_u32((uint32_t)sim.stats().ticks);
	mix_u32((uint32_t)sim.wave_number());

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
			uint32_t now_hash = compute_sim_hash(sim);
			if (frame.sim_state_hash != 0 && frame.sim_state_hash != now_hash) {
				std::cerr << "[TurnManager] replay OOS at turn " << frame.turn_index
				          << " stored=" << frame.sim_state_hash << " computed=" << now_hash << "\n";
			}
		}
	} else {
		for (auto &c : pending_local_cmds_) {
			c.client_id = local_client_id_;
		}

		// If connected to network, broadcast our local command batch
		if (net_host_ && net_host_->getRole() != NetRole::None) {
			SimulationCommandMsg scm;
			scm.turn = current_turn_;
			scm.client_id = local_client_id_;
			scm.commands = pending_local_cmds_;
			std::vector<uint8_t> payload = NetPacketSerializer::frame_simulation_command(scm);
			std::vector<uint8_t> pkt = NetPacketSerializer::pack_message(NetMessageType::NMT_SIMULATION_COMMAND, current_turn_, payload);
			net_host_->broadcastCommand(pkt);

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

		// Deterministic sort of commands so all peers execute identical order
		std::stable_sort(frame.commands.begin(), frame.commands.end(), [](const NetCommand &a, const NetCommand &b) {
			if (a.client_id != b.client_id) return a.client_id < b.client_id;
			if (a.cmd_type != b.cmd_type) return a.cmd_type < b.cmd_type;
			return a.target_id < b.target_id;
		});
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
				if (peer_hash != 0 && peer_hash != frame.sim_state_hash) {
					oos_detected_ = true;
					oos_turn_ = current_turn_;
					oos_expected_hash_ = frame.sim_state_hash;
					oos_received_hash_ = peer_hash;
					if (std::find(oos_peers_.begin(), oos_peers_.end(), peer_id) == oos_peers_.end()) {
						oos_peers_.push_back(peer_id);
					}
					std::cerr << "[TurnManager] Out-of-sync detected at turn " << current_turn_
					          << " local_hash=" << frame.sim_state_hash
					          << " peer=" << peer_id << " peer_hash=" << peer_hash << "\n";

					if (net_host_->isHost()) {
						SyncErrorMsg sem;
						sem.turn = current_turn_;
						sem.hash_expected = frame.sim_state_hash;
						sem.player_names.push_back("Peer" + std::to_string(peer_id));
						std::vector<uint8_t> se_payload = NetPacketSerializer::frame_sync_error(sem);
						std::vector<uint8_t> se_pkt = NetPacketSerializer::pack_message(NetMessageType::NMT_SYNC_ERROR, current_turn_, se_payload);
						net_host_->broadcastCommand(se_pkt);
					}
				}
			}
		}
	}

	if (!is_replaying_) {
		turn_history_.push_back(frame);
	}
	return true;
}

bool TurnManager::save_replay(const std::string &path) const {
	std::ofstream out(path, std::ios::binary);
	if (!out.is_open()) return false;

	uint32_t magic = 0x52545330; // "RTS0"
	uint32_t version = 2; // v2 = Phase4 detailed hash
	out.write((const char*)&magic, sizeof(magic));
	out.write((const char*)&version, sizeof(version));

	uint32_t frame_count = (uint32_t)turn_history_.size();
	out.write((const char*)&frame_count, sizeof(frame_count));

	// Ensure monotonic turn_index ordering before write
	for (size_t i = 0; i < turn_history_.size(); ++i) {
		if (i > 0 && turn_history_[i].turn_index <= turn_history_[i-1].turn_index) {
			std::cerr << "[TurnManager] save_replay ordering violation at frame " << i << "\n";
			return false;
		}
	}

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
	return out.good();
}

bool TurnManager::load_replay(const std::string &path) {
	std::ifstream in(path, std::ios::binary);
	if (!in.is_open()) return false;

	uint32_t magic = 0, version = 0, frame_count = 0;
	in.read((char*)&magic, sizeof(magic));
	in.read((char*)&version, sizeof(version));
	if (magic != 0x52545330) return false;
	if (version != 1 && version != 2) return false; // accept v1 legacy + v2 detailed hash

	in.read((char*)&frame_count, sizeof(frame_count));
	if (!in.good()) return false;
	turn_history_.clear();
	turn_history_.reserve(frame_count);

	uint32_t prev_turn = 0;
	for (uint32_t i = 0; i < frame_count; ++i) {
		TurnFrame f;
		in.read((char*)&f.turn_index, sizeof(f.turn_index));
		in.read((char*)&f.sim_state_hash, sizeof(f.sim_state_hash));
		if (!in.good()) return false;
		// Verify monotonic turn_index ordering
		if (i == 0) {
			if (f.turn_index == 0) { /* allow 0 or 1 start */ }
		} else {
			if (f.turn_index <= prev_turn) {
				std::cerr << "[TurnManager] load_replay out-of-order turn at frame " << i
				          << " got " << f.turn_index << " prev " << prev_turn << "\n";
				return false;
			}
		}
		prev_turn = f.turn_index;

		uint32_t cmd_count = 0;
		in.read((char*)&cmd_count, sizeof(cmd_count));
		if (!in.good()) return false;
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
			if (!in.good()) return false;
			cmd.entity_ids.resize(entity_count);
			if (entity_count > 0) {
				in.read((char*)cmd.entity_ids.data(), entity_count * sizeof(uint32_t));
				if (!in.good()) return false;
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

