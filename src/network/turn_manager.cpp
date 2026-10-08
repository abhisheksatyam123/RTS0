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
	reset();
	turn_remote_cmds_ = std::move(cmds);
	received_batches_ = std::move(batches);
	peer_hashes_ = std::move(hashes);
}

bool TurnManager::lockstep_net_active() const {
	return net_host_ && net_host_->getRole() != NetRole::None && !is_replaying_;
}

bool TurnManager::turn_ready(uint32_t turn) {
	if (turn <= COMMAND_DELAY) return true;
	if (!lockstep_peers_set_) {
		lockstep_peers_.clear();
		lockstep_peers_.push_back(0);
		if (net_host_->isHost()) {
			for (const auto &p : net_host_->getPeers())
				if (p.connected && !p.is_observer && p.id != 0) lockstep_peers_.push_back(p.id);
		} else if (local_client_id_ != 0) {
			lockstep_peers_.push_back(local_client_id_);
		}
		lockstep_peers_set_ = true;
	}
	auto it = received_batches_.find(turn);
	if (it == received_batches_.end()) return false;
	for (uint32_t id : lockstep_peers_)
		if (std::find(it->second.begin(), it->second.end(), id) == it->second.end()) return false;
	return true;
}

void TurnManager::reset() {
	current_turn_ = 0;
	turn_timer_ = 0.0f;
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
	oos_expected_hash_ = 0;
	oos_received_hash_ = 0;
	oos_peers_.clear();
	net_game_started_ = false;
	apply_base_turn_ = 0;
	gated_ticks_ = 0;
	boot_hash_ = 0;
	boot_hash_captured_ = false;
	last_game_start_attributes_.clear();
	pending_diplomacy_.clear();
	has_applied_diplomacy_ = false;
	for (int i = 0; i < 8; ++i) last_applied_diplomacy_[i] = 1; // DIPLO_NEUTRAL
	fleet_upkeep_requests_ = 0;
	fleet_upkeep_paused_ = false;
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
		net_host_->onCommand([this](uint32_t peer_id, const uint8_t *data, size_t size) {
			SimulationCommandMsg scm;
			if (NetPacketSerializer::parse_simulation_command(data, size, scm)) {
				this->receive_remote_turn_batch(scm.turn, scm.client_id != 0 ? scm.client_id : peer_id, scm.commands);
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
			} else if (msg.type == static_cast<uint8_t>(NetMessageType::NMT_DIPLOMACY)) {
				// G9: the ONE route from the wire to the sim. This replaces the
				// presentation-gated onDiplomacy applier: a stance now lands on
				// every peer through the lockstep turn boundary regardless of UI
				// state. NetHost::dispatchMessage forwards NMT_DIPLOMACY to this
				// callback on host AND client, and has already rejected a malformed
				// payload / out-of-range player_id / invalid stance.
				DiplomacyMsg dm;
				if (NetPacketSerializer::parse_diplomacy(msg.payload.data(), msg.payload.size(), dm))
					this->post_diplomacy(dm.player_id, dm.stance, msg.turn);
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

void TurnManager::receive_remote_turn_batch(uint32_t turn, uint32_t client_id, const std::vector<NetCommand> &cmds) {
	if (is_replaying_) return;
	// One batch per (turn, client): a retransmitted or doubly-delivered batch
	// must not queue its commands twice.
	if (turn <= current_turn_) {
		// Gating makes this unreachable for a lockstep peer (a retransmitted
		// batch for a turn already run is the benign case). Its commands can no
		// longer run on the turn the sender ran them, so do not queue them.
		std::cerr << "[TurnManager] late batch for turn " << turn << " from client " << client_id
		          << " (current " << current_turn_ << ")\n";
		return;
	}
	auto &got = received_batches_[turn];
	if (std::find(got.begin(), got.end(), client_id) != got.end()) return;
	got.push_back(client_id);
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
		for (float capture : b.capture_points_by_team) mix_float(capture);
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

	// Diplomacy state: include reciprocal stances because shared LOS and
	// capture eligibility depend on mutual alliance, not only player 0's view.
	for (size_t i = 0; i < 8; ++i) mix_u8(sim.diplomacy_state()[i]);
	for (size_t i = 0; i < 8; ++i) mix_u8(sim.diplomacy_reverse_state()[i]);

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

	// 9. Ticks & Wave state + determinism scalars (id allocator + sim RNG stream)
	mix_u32((uint32_t)sim.stats().ticks);
	mix_u32((uint32_t)sim.wave_number());
	mix_u32(sim.next_id());
	mix_u32(sim.sim_seed());
	mix_u32(sim.sim_rng_state());

	return hash;
}

// ---- G8/G9: deferred stance application -----------------------------------
// Total order on (turn, player_id, sender_peer_id): a deterministic function of
// the messages themselves, so two peers that received the same set of stances
// apply them in the same sequence no matter the arrival order.
bool TurnManager::post_diplomacy(uint8_t player_id, uint8_t stance, uint32_t origin_turn) {
	if (is_replaying_) return false;
	if (player_id >= 8 || stance > 2) return false;
	const uint32_t target = apply_base_turn_ + current_turn_ + DIPLOMACY_APPLY_DELAY;
	PendingDiplomacy pd;
	pd.turn = target;
	pd.player_id = player_id;
	pd.stance = stance;
	pd.sender_peer_id = local_client_id_;
	// The sender id is a tiebreaker only (see the header): net_host routes the
	// authoritative id into post_diplomacy via this call site on the wire path;
	// the local origination path uses local_client_id_. Identical on every peer
	// because both are derived from the assignment the host made.
	size_t pos = 0;
	while (pos < pending_diplomacy_.size()) {
		const PendingDiplomacy &q = pending_diplomacy_[pos];
		if (q.turn != pd.turn) {
			if (q.turn > pd.turn) break;
		} else if (q.player_id != pd.player_id) {
			if (q.player_id > pd.player_id) break;
		} else if (q.sender_peer_id > pd.sender_peer_id) {
			break;
		}
		++pos;
	}
	pending_diplomacy_.insert(pending_diplomacy_.begin() + (long)pos, pd);
	return true;
}

// Single sim write point for a stance. Every stance reaching the sim -- inbound
// NMT_DIPLOMACY, the locally originated send, and the cmd_type 22 batch in tick()
// -- goes through a validated Simulation::cmd_set_diplomacy call; recording what
// was applied lets tests and the hash check see the same value.
void TurnManager::apply_diplomacy(const std::vector<PendingDiplomacy> &batch, Simulation &sim) {
	for (const PendingDiplomacy &pd : batch) {
		if (pd.player_id >= 8) continue;
		sim.cmd_set_diplomacy(pd.player_id, pd.stance);
		last_applied_diplomacy_[pd.player_id] = pd.stance;
		has_applied_diplomacy_ = true;
	}
}

uint8_t TurnManager::last_applied_diplomacy(uint8_t player_id) const {
	return (player_id < 8) ? last_applied_diplomacy_[player_id] : (uint8_t)1;
}

// ---- P3: maintainFleet upkeep (0 A.D. navalManager.js:507-543) --------------
// 0 A.D. semantics reproduced here, using only Simulation's public API:
//
//   if (queues.ships.hasQueuedUnits()) return;                       // already training
//   if (!docks.filter(isBuilt).hasEntities()) return;                // no dock -> no ship
//   for (let sea of activeSeas) {
//     if (countOwnQueuedEntitiesWithMetadata("sea", sea) > 0) continue;
//     if (seaTransportShips[sea].length < wantedTransportShips[sea]) { train one; continue; }
//     if (seaFishShips[sea].length     < wantedFishShips[sea])     { train one; continue; }
//   }
//
// RTS0 collapses the per-sea partitioning (Simulation exposes a per-sea warship
// registry but no per-sea transport/fish wishlists or per-sea training queues),
// so the loop runs over active seas and trains at most one ship per turn: that
// keeps the production rate on the same side of the 0 A.D. behaviour as the
// existing trireme/fisher targets in Simulation::petra_manage_naval
// (1/2/4 triremes, 2/3 fishing boats by phase) without double-producing.
//
// TODO(sim owner): a real navalManager seam does not exist on the Simulation side.
// When src/simulation exposes the equivalent of
//   Simulation::tick_petra_ai -> Simulation::petra_manage_naval
//     (navalManager.js:876 call site, simulation.cpp:7366)
// add `bool cmd_maintain_fleet(uint8_t team, uint8_t unit_type)` alongside the
// other cmd_* entry points and call it from there; it can then honour
// per-sea queues, wantedTransportShips/wantedFishShips and the per-sea
// naval registry exactly. Until then this turn-boundary pass is the substitute
// and must stay deterministic (same world -> same commands) because it feeds the
// lockstep hash through the command stream.
std::vector<NetCommand> TurnManager::generate_fleet_upkeep_commands(const Simulation &sim) const {
	std::vector<NetCommand> out;
	if (fleet_upkeep_paused_) return out;

	// 1. queues.ships.hasQueuedUnits(): any dock already training blocks upkeep.
	//    RTS0 has one naval queue per dock, so this is the whole naval queue set.
	// 2. docks.filter(isBuilt): a dock that is fully built (build_progress >= 1).
	struct DockInfo { uint32_t id = 0; float x = 0.0f, z = 0.0f; };
	std::vector<DockInfo> docks;
	for (const auto &b : sim.buildings()) {
		if (b.team != 1 || b.type != BLD_DOCK) continue;
		if (b.hp <= 0.0f || b.build_progress < 1.0f) continue;
		docks.push_back(DockInfo{b.id, b.x, b.z});
	}
	if (docks.empty()) return out;
	// queues.ships hasQueuedUnits() -> anything already training blocks the upkeep.
	// Scoped to the OWNING team: RTS0 docks are team 1 while a human barracks is
	// team 0, and the two never share a naval queue. Counting the human's queue
	// here would starve Petra's navy for the whole game, which is the opposite of
	// navalManager.js:509 (whose `queues` is Petra's own ship queue).
	for (const auto &d : docks) {
		for (const auto &b : sim.buildings()) {
			if (b.id != d.id) continue;
			if (b.queue.size() > 0) return out;
			break;
		}
	}

	// Count the existing fleet by class. Counting from units() in id order keeps
	// this a pure function of sim state (no per-sea accessor exists on the
	// Simulation side yet -- see the TODO above).
	int warships = 0, fishers = 0;
	auto uview = sim.units();
	for (size_t i = 0; i < uview.count; ++i) {
		if (!uview.alive[i] || uview.team[i] != 1) continue;
		if (uview.unit_type[i] == UNIT_TRIREME) warships++;
		else if (uview.unit_type[i] == UNIT_FISHING_BOAT) fishers++;
	}

	// 0 A.D. iterates the seas the AI has ships in. Without the per-sea registry
	// on the Simulation side the whole map is one sea here, so active_seas is 1;
	// the wishlists keep the per-sea multiples correct if that accessor lands.
	const int active_seas = 1;

	// One commission per turn: warships (transport) first, then fishing, mirroring
	// the `continue`-after-train order inside 0 A.D.'s per-sea loop. The wishlists
	// match the trireme/fisher targets Simulation::petra_manage_naval already uses
	// (1/2/4 triremes, 2/3 fishing boats), read out of the shared PetraConfig.
	const int wanted_warships = (sim.petra_config().difficulty >= PETRA_HARD) ? 4
	                        : ((sim.petra_config().difficulty >= PETRA_EASY) ? 2 : 1);
	const int wanted_fishers = (sim.petra_phase() >= 2) ? 3 : 2;

	uint8_t type = 0;
	if (warships < active_seas * wanted_warships) {
		type = UNIT_TRIREME;
	} else if (fishers < active_seas * wanted_fishers) {
		type = UNIT_FISHING_BOAT;
	}
	if (!type) return out;

	NetCommand c;
	c.client_id = local_client_id_;
	c.cmd_type = static_cast<uint8_t>(NetCommandType::NET_CMD_MAINTAIN_FLEET);
	c.target_id = type;
	c.param = 0;
	c.target_x = docks[0].x; // anchor: the built dock the commission hangs off
	c.target_z = docks[0].z;
	c.entity_ids.push_back(docks[0].id);
	out.push_back(std::move(c));
	return out;
}

// Called from tick() AFTER the turn's commands and BEFORE the TICKS_PER_TURN sim
// steps, so a fleet command follows the same 0 A.D. ordering as the Petra naval
// pass it stands in for. Any Petra-enabled mode reaches this path; a mode with the
// AI off simply has no dock, and generate_fleet_upkeep_commands() then returns
// empty because its first gate is "a built dock must exist".
void TurnManager::maintain_fleet_upkeep(Simulation &sim) {
	std::vector<NetCommand> cmds = generate_fleet_upkeep_commands(sim);
	if (cmds.empty()) return;
	fleet_upkeep_requests_ += (uint32_t)cmds.size();

	// BLOCKED on src/simulation, which is a separate ownership stream. The
	// commission must be produced by Simulation so that
	//     Simulation::tick_petra_ai -> Simulation::petra_manage_naval
	//     (navalManager.js:876 call site, simulation.cpp:7366)
	// is the single writer of the naval queue, exactly like the rest of the Petra
	// trainers. Exact hook point for the sim owner:
	//
	//   1. simulation.h, next to `bool cmd_set_diplomacy(uint8_t, uint8_t);`
	//      (simulation.h:239) add
	//          bool cmd_maintain_fleet(uint8_t team, uint8_t unit_type);
	//          uint32_t naval_upkeep_requests() const { return naval_upkeep_requests_; }
	//   2. simulation.cpp: extend Simulation::petra_manage_naval
	//      (simulation.cpp:5258) with a navalManager.js:507 maintainFleet() block
	//      placed AFTER the per-sea registry fill (simulation.cpp:5427-5458) so it
	//      sees this turn's fleet, and call cmd_maintain_fleet() from there.
	//   3. turn_manager.cpp: replace this loop with
	//          const uint32_t before = sim.naval_upkeep_requests();
	//          sim.cmd_maintain_fleet(1, type);   // per generated command
	//          fleet_upkeep_requests_ += sim.naval_upkeep_requests() - before;
	//
	// Until that lands the GENERATION half is complete and deterministic (it is a
	// pure function of sim state at the turn boundary) and only the commit is
	// missing. It is intentionally NOT emulated from this layer: enqueuing here
	// would (a) write Petra's naval queue from the network layer and (b) mint
	// building ids through a second allocator, both of which desync.
	// Singleplayer is unaffected by construction: the first gate in
	// generate_fleet_upkeep_commands() is "a built dock must exist".
	(void)sim;
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
	turn_timer_ += dt;
	if (turn_timer_ < TURN_DURATION) return false;

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
		++waiting_ticks_;
		if (turn_timer_ > 5.0f * TURN_DURATION) turn_timer_ = 5.0f * TURN_DURATION;
		return false;
	}

	turn_timer_ -= TURN_DURATION;
	current_turn_++;

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
		if (lockstep) {
			const uint32_t sched = current_turn_ + COMMAND_DELAY;
			SimulationCommandMsg scm;
			scm.turn = sched;
			scm.client_id = local_client_id_;
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

		// Deterministic sort of commands so all peers execute identical order
		std::stable_sort(frame.commands.begin(), frame.commands.end(), [](const NetCommand &a, const NetCommand &b) {
			if (a.client_id != b.client_id) return a.client_id < b.client_id;
			if (a.cmd_type != b.cmd_type) return a.cmd_type < b.cmd_type;
			return a.target_id < b.target_id;
		});
	}

	// G8/G9: apply the stances queued by NMT_DIPLOMACY / post_diplomacy for THIS
	// turn, immediately before the command batch so they are already reflected
	// when the batch executes (identical ordering to a cmd_type 22 entry, which
	// runs inside the batch below). Stances for a later turn stay queued.
	if (!pending_diplomacy_.empty()) {
		std::vector<PendingDiplomacy> due;
		size_t keep = 0;
		for (size_t i = 0; i < pending_diplomacy_.size(); ++i) {
			if (pending_diplomacy_[i].turn <= current_turn_) due.push_back(pending_diplomacy_[i]);
			else pending_diplomacy_[keep++] = pending_diplomacy_[i];
		}
		pending_diplomacy_.resize(keep);
		if (!due.empty()) apply_diplomacy(due, sim);
	}

	// Execute turn commands into simulation (0 A.D. applies batch then advances sim)
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
			uint32_t bid = sim.cmd_build((int)c.target_x, (int)c.target_z, c.param);
			if (bid && !c.entity_ids.empty())
				sim.order_build_or_repair(c.entity_ids.data(), c.entity_ids.size(), bid);
		} else if (c.cmd_type == 6) { // Train
			sim.cmd_train(c.target_id, (UnitType)c.param);
		} else if (c.cmd_type == 7) { // Stance
			sim.order_set_stance(c.entity_ids.data(), c.entity_ids.size(), c.param);
		} else if (c.cmd_type == 8) { // Formation
			sim.order_set_formation(c.entity_ids.data(), c.entity_ids.size(), c.param);
		} else if (c.cmd_type == 9) { // Research
			sim.cmd_research(c.target_id, c.param);
		} else if (c.cmd_type == 10) { // Attack-move
			sim.order_attack_move(c.entity_ids.data(), c.entity_ids.size(), c.target_x, c.target_z);
		} else if (c.cmd_type == 11) { // Patrol
			sim.order_patrol(c.entity_ids.data(), c.entity_ids.size(), c.target_x, c.target_z);
		} else if (c.cmd_type == 12) { // Garrison
			sim.order_garrison(c.entity_ids.data(), c.entity_ids.size(), c.target_id);
		} else if (c.cmd_type == 13) { // Ungarrison / disembark-all
			if (c.param == 1 && !c.entity_ids.empty())
				sim.order_disembark_all(c.entity_ids[0]);
			else
				sim.order_ungarrison(c.target_id);
		} else if (c.cmd_type == 14) { // Rally
			uint32_t res = c.entity_ids.size() > 0 ? c.entity_ids[0] : 0;
			uint32_t bld = c.entity_ids.size() > 1 ? c.entity_ids[1] : 0;
			sim.cmd_set_rally_point(c.target_id, c.target_x, c.target_z, res, bld);
		} else if (c.cmd_type == 15) { // Attack building
			sim.order_attack_building(c.entity_ids.data(), c.entity_ids.size(), c.target_id);
		} else if (c.cmd_type == 16) { // Build / repair
			sim.order_build_or_repair(c.entity_ids.data(), c.entity_ids.size(), c.target_id);
		} else if (c.cmd_type == 17) { // Hunt / herd
			if (c.param == 1)
				sim.order_herd(c.entity_ids.data(), c.entity_ids.size(), c.target_id);
			else
				sim.order_hunt(c.entity_ids.data(), c.entity_ids.size(), c.target_id);
		} else if (c.cmd_type == 18) { // Board
			sim.order_board_transport(c.entity_ids.data(), c.entity_ids.size(), c.target_id);
		} else if (c.cmd_type == 19) { // Disembark
			if (!c.entity_ids.empty())
				sim.order_disembark(c.entity_ids[0], c.target_x, c.target_z);
		} else if (c.cmd_type == 20) { // Cancel train
			sim.cmd_cancel_train(c.target_id, (size_t)c.param);
		} else if (c.cmd_type == 21) { // Guard
			sim.order_guard(c.entity_ids.data(), c.entity_ids.size(), c.target_id);
		} else if (c.cmd_type == 23) { // Barter — target_x=sell, target_z=buy, target_id=amount.
			// 0 A.D. Barter.js / Market.js exchange. Legacy gap: UI previously called
			// sim->cmd_barter locally even in lockstep -> resource/price divergence.
			sim.cmd_barter((rtso::ResourceType)(int)c.target_x,
			               (rtso::ResourceType)(int)c.target_z, (int)c.target_id);
		} else if (c.cmd_type == 22) { // Diplomacy stance — target_id=player, param=stance.
			// 0 A.D. Diplomacy.js / DiplomacyDialog.js parity. Applied at the turn
			// boundary on EVERY peer (deterministic: diplomacy_state() is mixed into
			// compute_sim_hash at turn_manager.cpp:213, so a local-only change OOSes).
			// UI entry: main_node.cpp _on_diplo_stance_changed -> post_lockstep(22, ...).
			sim.cmd_set_diplomacy((uint8_t)c.target_id, c.param);
		}
	}

	// P3 maintainFleet upkeep. 0 A.D. navalManager.js:876 calls maintainFleet()
	// inside the naval manage() pass, i.e. AFTER the turn's orders and BEFORE the
	// sim steps -- the same slot this occupies, so the fleet command the upkeep
	// produces is visible to the turn's sim steps and to the hash.
	maintain_fleet_upkeep(sim);

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

