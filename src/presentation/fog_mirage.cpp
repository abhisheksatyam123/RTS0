// Derived from 0 A.D., GPL-2.0-or-later (Copyright (C) Wildfire Games).
// See fog_mirage.h for the 0 A.D. sources this ports.
#include "fog_mirage.h"

#include "../simulation/simulation.h"

#include <algorithm>
#include <cstring>

namespace rtso {

static_assert(MAX_PLAYERS == 8, "FogMirages::kPlayers");

// =============================================================================
// FogMirages (Fogging.js / Mirage.js)
// =============================================================================

void FogMirages::reset() {
	for (Memory &m : others_) m = Memory{};
	building_memory_.clear();
	resource_memory_.clear();
	treasure_memory_.clear();
	out_buildings_.clear();
	out_resources_.clear();
	out_treasures_.clear();
	have_key_ = false;
	stamp_ = 0;
}

bool los_sees_all(const Simulation &sim, uint8_t viewer) {
	// Player.js: a defeated player observes, and SetLosRevealAll for a winner too.
	return sim.reveal_map() || viewer == OBSERVER_VIEW || viewer == 0xFF || sim.is_defeated(viewer) ||
	       sim.is_victory_for(viewer);
}

bool los_sees_unit(const Simulation &sim, uint8_t viewer, float x, float z, uint8_t team) {
	if (los_sees_all(sim, viewer)) return true;
	// CCmpRangeManager: own entities, and an ally's once its sight is shared (Cartography); other
	// units only in sight (units are not RetainInFog).
	return sim.shares_los(viewer, team) || sim.los_visible(viewer, x, z);
}

bool los_sees_fauna(const Simulation &sim, uint8_t viewer, float x, float z, uint8_t owner, uint8_t ftype) {
	if (los_sees_all(sim, viewer)) return true;
	if (owner < MAX_PLAYERS && sim.shares_los(viewer, owner)) return true;
	// RetainInFog: Gaia's animals stay in explored fog; corral livestock only in sight.
	return fauna_retained_in_fog(ftype) ? sim.los_explored(viewer, x, z) : sim.los_visible(viewer, x, z);
}

void los_view_grid(const Simulation &sim, uint8_t viewer, std::vector<uint8_t> &out) {
	const int res = sim.fow_res();
	const size_t n = (size_t)res * (size_t)res;
	out.resize(n);
	const LosGrid &los = sim.los();
	const bool sees_all = los_sees_all(sim, viewer);
	if (los.cells() != n) {
		std::fill(out.begin(), out.end(), sees_all ? (uint8_t)Simulation::FOW_VISIBLE : (uint8_t)Simulation::FOW_UNEXPLORED);
		return;
	}
	const uint8_t mask = sim.los_shared_mask(viewer);
	const uint8_t unseen = sim.explore_map() ? (uint8_t)Simulation::FOW_EXPLORED : (uint8_t)Simulation::FOW_UNEXPLORED;
	const auto &state = los.state();
	for (size_t i = 0; i < n; ++i) {
		// A circular map's off-world cells stay black for everyone, even with the map revealed or
		// explored (CCmpRangeManager GetLosVisibilityPosition, ExploreMap).
		if (los.off_world((int)i)) { out[i] = (uint8_t)Simulation::FOW_UNEXPLORED; continue; }
		if (sees_all) { out[i] = (uint8_t)Simulation::FOW_VISIBLE; continue; }
		const uint16_t st = state[i];
		out[i] = (st & mask) ? (uint8_t)Simulation::FOW_VISIBLE
				: ((st >> 8) & mask) ? (uint8_t)Simulation::FOW_EXPLORED : unseen;
	}
}

uint8_t FogMirages::los_at(float x, float z) const {
	if (!los_ || res_ <= 0 || map_half_ <= 0.0f) return Simulation::FOW_VISIBLE;
	if (x < -map_half_ || x > map_half_ || z < -map_half_ || z > map_half_)
		return Simulation::FOW_UNEXPLORED;
	// Same cell mapping as Simulation::fow_state_at.
	const int cx = std::clamp((int)((x + map_half_) / (map_half_ * 2.0f) * res_), 0, res_ - 1);
	const int cz = std::clamp((int)((z + map_half_) / (map_half_ * 2.0f) * res_), 0, res_ - 1);
	return los_[cz * res_ + cx];
}

namespace {
constexpr uint32_t kMemoryMagic = 0x31474d46; // "FMG1"
template <typename T>
void put(std::vector<uint8_t> &out, const T &v) {
	const uint8_t *p = reinterpret_cast<const uint8_t *>(&v);
	out.insert(out.end(), p, p + sizeof(T));
}
template <typename T>
bool get(const uint8_t *&p, const uint8_t *end, T &v) {
	if ((size_t)(end - p) < sizeof(T)) return false;
	std::memcpy(&v, p, sizeof(T));
	p += sizeof(T);
	return true;
}
} // namespace

std::vector<uint8_t> FogMirages::export_memory() const {
	std::vector<uint8_t> out;
	if (!have_key_) return out;
	put(out, kMemoryMagic);
	put(out, key_viewer_);
	put(out, (uint32_t)sizeof(FoggedBuilding));
	put(out, (uint32_t)sizeof(FoggedResource));
	put(out, (uint32_t)sizeof(FoggedTreasure));
	// In id order, so a save's bytes don't depend on the hash maps' order.
	auto write = [&](const auto &memory) {
		std::vector<uint32_t> ids;
		for (const auto &e : memory) ids.push_back(e.first);
		std::sort(ids.begin(), ids.end());
		put(out, (uint32_t)ids.size());
		for (uint32_t id : ids) put(out, memory.at(id).seen);
	};
	write(building_memory_);
	write(resource_memory_);
	write(treasure_memory_);
	return out;
}

bool FogMirages::import_memory(const uint8_t *data, size_t n) {
	// A loaded game starts from its own memory, never the match left behind.
	reset();
	pending_ = false;
	const uint8_t *p = data, *end = data + n;
	uint32_t magic = 0, sb = 0, sr = 0, st = 0;
	uint8_t viewer = 0;
	if (!get(p, end, magic) || magic != kMemoryMagic || !get(p, end, viewer) || !get(p, end, sb) || !get(p, end, sr) ||
			!get(p, end, st) || sb != sizeof(FoggedBuilding) || sr != sizeof(FoggedResource) || st != sizeof(FoggedTreasure))
		return false;
	auto read = [&](auto &list) {
		uint32_t count = 0;
		if (!get(p, end, count)) return false;
		list.clear();
		for (uint32_t k = 0; k < count; ++k) {
			list.emplace_back();
			if (!get(p, end, list.back())) return false;
		}
		return true;
	};
	if (!read(pending_buildings_) || !read(pending_resources_) || !read(pending_treasures_)) {
		pending_ = false;
		return false;
	}
	pending_ = true;
	pending_viewer_ = viewer;
	return true;
}

void FogMirages::apply_pending(uint8_t viewer) {
	if (!pending_ || viewer != pending_viewer_) return;
	pending_ = false;
	for (const auto &b : pending_buildings_) building_memory_[b.id].seen = b;
	for (const auto &r : pending_resources_) resource_memory_[r.id].seen = r;
	for (const auto &t : pending_treasures_) treasure_memory_[t.id].seen = t;
	pending_buildings_.clear();
	pending_resources_.clear();
	pending_treasures_.clear();
}

void FogMirages::begin(const uint8_t *los, int res, float map_half) {
	los_ = los;
	res_ = res;
	map_half_ = map_half;
	if (++stamp_ == 0) stamp_ = 1;
	out_buildings_.clear();
	out_resources_.clear();
	out_treasures_.clear();
}

void FogMirages::add_building(const FoggedBuilding &live, bool allied, bool seen_before) {
	FoggedBuilding b = live;
	b.miraged = false;
	if (allied) {
		// Owner (or shared-vision ally): RetainInFog shows the real structure.
		// A mirage of it from before it changed hands is no longer needed.
		building_memory_.erase(b.id);
		out_buildings_.push_back(b);
		return;
	}
	const uint8_t st = los_at(b.x, b.z);
	if (st == Simulation::FOW_VISIBLE) {
		// Fogging.OnVisibilityChanged(VIS_VISIBLE): seen = true; the copy taken
		// when it next drops into fog (LoadMirage) is this frame's state.
		BuildingMemory &m = building_memory_[b.id];
		m.seen = b;
		m.stamp = stamp_;
		out_buildings_.push_back(b);
		return;
	}
	auto it = building_memory_.find(b.id);
	if (it == building_memory_.end()) {
		if (!seen_before) return; // never seen: hidden (ComputeLosVisibility !WasSeen)
		it = building_memory_.emplace(b.id, BuildingMemory{}).first;
		it->second.seen = b;
	}
	it->second.stamp = stamp_;
	if (st == Simulation::FOW_EXPLORED) {
		FoggedBuilding m = it->second.seen;
		m.miraged = true;
		out_buildings_.push_back(m);
	}
}

void FogMirages::add_resource(const FoggedResource &live) {
	FoggedResource r = live;
	r.miraged = false;
	const uint8_t st = los_at(r.x, r.z);
	if (st == Simulation::FOW_VISIBLE) {
		ResourceMemory &m = resource_memory_[r.id];
		m.seen = r;
		m.stamp = stamp_;
		out_resources_.push_back(r);
		return;
	}
	if (st == Simulation::FOW_UNEXPLORED) return;
	auto it = resource_memory_.find(r.id);
	if (it == resource_memory_.end()) {
		// Gaia supplies are RetainInFog and their Fogging only activates once
		// touched, so an explored, never-touched supply shows as it stands.
		// Remember it now so later gathering in the fog doesn't leak.
		ResourceMemory &m = resource_memory_[r.id];
		m.seen = r;
		m.stamp = stamp_;
		out_resources_.push_back(r);
		return;
	}
	it->second.stamp = stamp_;
	FoggedResource m = it->second.seen;
	m.miraged = true;
	out_resources_.push_back(m);
}

void FogMirages::add_treasure(const FoggedTreasure &live) {
	FoggedTreasure t = live;
	t.miraged = false;
	const uint8_t st = los_at(t.x, t.z);
	if (st == Simulation::FOW_UNEXPLORED) return;
	// The Argo has RetainInFog false: hidden outside sight, with no memory.
	if (t.type == TREASURE_SPECIAL_ARGO && st != Simulation::FOW_VISIBLE) {
		treasure_memory_.erase(t.id);
		return;
	}
	// Treasures sit still and their Fogging only activates when taken, so an explored
	// treasure shows as it stands; remember it so its taking in the fog doesn't leak.
	TreasureMemory &m = treasure_memory_[t.id];
	m.seen = t;
	m.stamp = stamp_;
	out_treasures_.push_back(t);
}

void FogMirages::end() {
	// Entities that no longer exist: their mirages stay in the fog until the
	// viewer sees the spot again (Mirage.OnVisibilityChanged → DestroyEntity).
	for (auto it = building_memory_.begin(); it != building_memory_.end();) {
		if (it->second.stamp == stamp_) { ++it; continue; }
		const FoggedBuilding &b = it->second.seen;
		const uint8_t st = los_at(b.x, b.z);
		if (st == Simulation::FOW_VISIBLE) {
			it = building_memory_.erase(it);
			continue;
		}
		if (st == Simulation::FOW_EXPLORED) {
			FoggedBuilding m = b;
			m.miraged = true;
			out_buildings_.push_back(m);
		}
		++it;
	}
	for (auto it = resource_memory_.begin(); it != resource_memory_.end();) {
		if (it->second.stamp == stamp_) { ++it; continue; }
		const FoggedResource &r = it->second.seen;
		const uint8_t st = los_at(r.x, r.z);
		if (st == Simulation::FOW_VISIBLE) {
			it = resource_memory_.erase(it);
			continue;
		}
		if (st == Simulation::FOW_EXPLORED) {
			FoggedResource m = r;
			m.miraged = true;
			out_resources_.push_back(m);
		}
		++it;
	}
	for (auto it = treasure_memory_.begin(); it != treasure_memory_.end();) {
		if (it->second.stamp == stamp_) { ++it; continue; }
		const FoggedTreasure &t = it->second.seen;
		const uint8_t st = los_at(t.x, t.z);
		if (st == Simulation::FOW_VISIBLE) {
			it = treasure_memory_.erase(it);
			continue;
		}
		if (st == Simulation::FOW_EXPLORED) {
			FoggedTreasure m = t;
			m.miraged = true;
			out_treasures_.push_back(m);
		}
		++it;
	}
	// Remembered copies of supplies still standing in sight are redundant with the
	// live entity, but keeping them is what lets a later fogged frame show them.
}

void FogMirages::update(const Simulation &sim, uint8_t viewer) {
	const auto stats = sim.stats();
	const size_t nb = sim.buildings().size();
	const size_t nr = sim.resource_count();
	const size_t nt = sim.treasures().size();
	const float half = sim.map_half();
	const int res = sim.fow_res();
	if (have_key_ && key_viewer_ == viewer && key_ticks_ == (uint64_t)stats.ticks &&
			key_buildings_ == nb && key_resources_ == nr && key_treasures_ == nt && key_half_ == half && key_res_ == res)
		return;
	// A new map starts with no memories. A new viewer takes the memory kept for it while
	// every player's is tracked (0 A.D. keeps one Fogging state per player), else none.
	const bool same_map = have_key_ && key_half_ == half && key_res_ == res && (uint64_t)stats.ticks >= key_ticks_;
	if (!same_map) {
		for (Memory &m : others_) m = Memory{};
	}
	if (!same_map || key_viewer_ != viewer) {
		if (same_map && track_all_ && key_viewer_ < kPlayers) {
			swap_memory(others_[key_viewer_]);
			others_[key_viewer_].valid = true;
		}
		if (same_map && track_all_ && viewer < kPlayers && others_[viewer].valid) {
			swap_memory(others_[viewer]);
			others_[viewer] = Memory{};
		} else {
			building_memory_.clear();
			resource_memory_.clear();
			treasure_memory_.clear();
		}
	}
	apply_pending(viewer);
	have_key_ = true;
	key_viewer_ = viewer;
	key_ticks_ = (uint64_t)stats.ticks;
	key_buildings_ = nb;
	key_resources_ = nr;
	key_treasures_ = nt;
	key_half_ = half;
	key_res_ = res;

	pass(sim, viewer);
	if (!track_all_) return;
	// The other players' memories, kept the same way with their own grids and lists.
	for (uint8_t p = 0; p < kPlayers; ++p) {
		if (p == viewer || !sim.player(p).active) continue;
		Memory &m = others_[p];
		m.valid = true;
		swap_memory(m);
		view_.swap(others_view_);
		out_buildings_.swap(others_buildings_);
		out_resources_.swap(others_resources_);
		out_treasures_.swap(others_treasures_);
		pass(sim, p);
		view_.swap(others_view_);
		out_buildings_.swap(others_buildings_);
		out_resources_.swap(others_resources_);
		out_treasures_.swap(others_treasures_);
		swap_memory(m);
	}
	los_ = view_.data(); // los_at reads the viewer's grid again
}

void FogMirages::set_track_all(bool on) {
	if (on == track_all_) return;
	track_all_ = on;
	if (!on)
		for (Memory &m : others_) m = Memory{};
}

void FogMirages::swap_memory(Memory &m) {
	building_memory_.swap(m.buildings);
	resource_memory_.swap(m.resources);
	treasure_memory_.swap(m.treasures);
}

void FogMirages::pass(const Simulation &sim, uint8_t viewer) {
	const int res = sim.fow_res();
	const float half = sim.map_half();
	los_view_grid(sim, viewer, view_);
	begin(view_.data(), res, half);
	for (const auto &b : sim.buildings()) {
		FoggedBuilding f;
		f.id = b.id;
		f.x = b.x;
		f.z = b.z;
		f.half_size = b.half_size;
		f.rot = b.rot;
		f.hp = b.hp;
		f.max_hp = b.max_hp;
		f.build_progress = b.build_progress;
		f.capture_points = b.capture_points;
		f.max_capture_points = b.max_capture_points;
		f.type = b.type;
		f.team = b.team;
		f.civ = sim.team_civ(b.team);
		f.committed = b.committed;
		f.wrecked = b.wrecked;
		f.is_burning = b.is_burning;
		f.upgrading = b.upgrade_to != BLD_TYPE_COUNT;
		f.production = b.queue.empty() ? 0 : Simulation::researching(b) != TECH_NONE ? 1 : 2;
		const bool allied = sim.shares_los(viewer, b.team); // an unshared ally's structures mirage like anyone's
		const bool in_sight = !allied && los_at(b.x, b.z) == Simulation::FOW_VISIBLE;
		if (in_sight) {
			f.has_resistance = true;
			sim.structure_resistance(b, f.resistance);
		}
		Simulation::BuildWork w;
		if (in_sight && (b.build_progress < 1.0f || b.hp < b.max_hp) && sim.structure_build_work(b, w)) {
			f.work = true;
			f.work_repair = w.repair;
			f.work_builders = (uint16_t)w.builders;
			f.work_remaining = w.remaining;
			f.work_remaining_new = w.remaining_new;
		}
		add_building(f, allied, viewer < MAX_PLAYERS && ((b.seen_mask >> viewer) & 1));
	}
	const auto rv = sim.resources();
	// Each supply's gatherers at work (ResourceSupply numGatherers), remembered with its amount.
	gatherers_.clear();
	{
		const auto uv = sim.units();
		for (size_t i = 0; i < uv.count; ++i)
			if (uv.alive[i] && uv.action_state[i] == ACTION_GATHER && uv.target_resource_id[i]) ++gatherers_[uv.target_resource_id[i]];
	}
	for (size_t i = 0; i < rv.count; ++i) {
		if (!rv.alive[i] || rv.amount[i] <= 0) continue;
		FoggedResource r;
		r.id = rv.id[i];
		r.x = rv.x[i];
		r.z = rv.z[i];
		r.amount = rv.amount[i];
		if (auto g = gatherers_.find(r.id); g != gatherers_.end()) r.gatherers = g->second;
		r.type = rv.type[i];
		r.subtype = rv.subtype ? rv.subtype[i] : 0;
		r.flora = rv.flora ? rv.flora[i] : 0;
		r.fauna = rv.fauna ? rv.fauna[i] : 0;
		r.owner = rv.owner ? rv.owner[i] : 0xFF;
		add_resource(r);
	}
	for (const auto &tr : sim.treasures()) {
		FoggedTreasure t;
		t.id = tr.id;
		t.x = tr.x;
		t.z = tr.z;
		t.type = tr.type;
		add_treasure(t);
	}
	end();
}

// =============================================================================
// LosShading (CLOSTexture)
// =============================================================================

void LosShading::generate_bitmap(const uint8_t *fow, int res, std::vector<uint8_t> &out) {
	const int fr = res * UPSAMPLE; // bitmap pixels a side
	out.assign((size_t)fr * (size_t)fr, 0);
	if (!fow || res <= 0) return;
	// FOW_VISIBLE = 2 -> 255, FOW_EXPLORED = 1 -> 127, FOW_UNEXPLORED = 0 -> 0, one
	// row of unexplored (0) round the grid, as 0 A.D. pads the map's edge.
	const int cp = res + 2;
	thread_local std::vector<uint8_t> c;
	c.assign((size_t)cp * (size_t)cp, 0);
	for (int j = 0; j < res; ++j) {
		uint8_t *row = &c[(size_t)(j + 1) * cp + 1];
		const uint8_t *src = &fow[(size_t)j * res];
		for (int i = 0; i < res; ++i) {
			const uint8_t s = src[i];
			row[i] = (uint8_t)((s == Simulation::FOW_VISIBLE) * 255 + (s == Simulation::FOW_EXPLORED) * 127);
		}
	}
	// Bilinear upsample into a padded copy (BLUR/2 pixels of 0 on every side): pixel
	// 2k sits a quarter cell before cell k's centre, 2k + 1 a quarter after, so each
	// takes 3/4 of its cell and 1/4 of the neighbour on that side.
	const int pad = BLUR / 2;
	const int pitch = fr + 2 * pad;
	thread_local std::vector<uint8_t> uh; // cp rows of fr columns (horizontal pass)
	thread_local std::vector<uint8_t> a;
	uh.assign((size_t)cp * (size_t)fr, 0);
	for (int j = 0; j < cp; ++j) {
		const uint8_t *p = &c[(size_t)j * cp + 1];
		uint8_t *o = &uh[(size_t)j * fr];
		for (int k = 0; k < res; ++k) {
			o[2 * k] = (uint8_t)((3u * p[k] + p[k - 1] + 2u) >> 2);
			o[2 * k + 1] = (uint8_t)((3u * p[k] + p[k + 1] + 2u) >> 2);
		}
	}
	a.assign((size_t)pitch * (size_t)pitch, 0);
	for (int k = 0; k < res; ++k) {
		const uint8_t *p0 = &uh[(size_t)k * fr], *p1 = &uh[(size_t)(k + 1) * fr], *p2 = &uh[(size_t)(k + 2) * fr];
		uint8_t *o0 = &a[(size_t)(2 * k + pad) * pitch + pad];
		uint8_t *o1 = o0 + pitch;
		for (int i = 0; i < fr; ++i) {
			o0[i] = (uint8_t)((3u * p1[i] + p0[i] + 2u) >> 2);
			o1[i] = (uint8_t)((3u * p1[i] + p2[i] + 2u) >> 2);
		}
	}
	// 7-tap binomial blur (1 6 15 20 15 6 1)/64, horizontal then vertical.
	// hz: pitch rows of fr columns (horizontal pass; padding rows stay 0).
	thread_local std::vector<uint8_t> hz;
	hz.assign((size_t)pitch * (size_t)fr, 0);
	for (int j = pad; j < fr + pad; ++j) {
		const uint8_t *p = &a[(size_t)j * pitch];
		uint8_t *o = &hz[(size_t)j * fr];
		for (int i = 0; i < fr; ++i) {
			const unsigned v = 1u * p[i] + 6u * p[i + 1] + 15u * p[i + 2] + 20u * p[i + 3] +
					15u * p[i + 4] + 6u * p[i + 5] + 1u * p[i + 6];
			o[i] = (uint8_t)(v >> 6);
		}
	}
	const size_t s = (size_t)fr;
	for (int j = 0; j < fr; ++j) {
		uint8_t *o = &out[(size_t)j * fr];
		const uint8_t *p0 = &hz[(size_t)j * fr];
		for (int i = 0; i < fr; ++i) {
			const unsigned v = 1u * p0[i] + 6u * p0[i + s] + 15u * p0[i + 2 * s] + 20u * p0[i + 3 * s] +
					15u * p0[i + 4 * s] + 6u * p0[i + 5 * s] + 1u * p0[i + 6 * s];
			o[i] = (uint8_t)(v >> 6);
		}
	}
}

void LosShading::set_target(const uint8_t *fow, int res) {
	if (!fow || res <= 0) return;
	const size_t n = (size_t)res * (size_t)res;
	if (res == res_ && last_fow_.size() == n && std::memcmp(last_fow_.data(), fow, n) == 0)
		return;
	const bool resized = res != res_;
	res_ = res;
	last_fow_.assign(fow, fow + n);
	generate_bitmap(fow, res, target_);
	since_recompute_ = 0.0f;
	settled_ = false;
	if (resized || shown_.size() != target_.size()) shown_ = target_;
}

void LosShading::snap() {
	shown_ = target_;
	since_recompute_ = 1.0f;
	settled_ = true;
}

bool LosShading::interpolate(float dt) {
	if (shown_.size() != target_.size()) {
		shown_ = target_;
		settled_ = true;
		return true;
	}
	if (settled_) return false;
	since_recompute_ += dt;
	// los_interp.fs: mix(previous, current, delta), delta = clamp(t * 2, 0, 1).
	// In 1/128ths, so d * delta fits 16 bits and the loop vectorises.
	const int16_t delta = (int16_t)(std::clamp(since_recompute_ * 2.0f, 0.0f, 1.0f) * 128.0f);
	const size_t n = shown_.size();
	uint8_t *s = shown_.data();
	const uint8_t *t = target_.data();
	uint8_t diff_any = 0;
	for (size_t i = 0; i < n; ++i) {
		const int16_t d = (int16_t)((int16_t)t[i] - (int16_t)s[i]);
		int16_t step = (int16_t)((int16_t)(d * delta) >> 7); // floors, so a falling value always moves
		step = (int16_t)(step + ((d > 0) & (step == 0))); // and a rising one moves at least 1
		const uint8_t v = (uint8_t)(s[i] + step);
		s[i] = v;
		diff_any |= (uint8_t)(v != t[i]);
	}
	settled_ = diff_any == 0;
	return true;
}

} // namespace rtso
