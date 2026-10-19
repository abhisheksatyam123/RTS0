#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace rtso {

// Per-player line of sight in the simulation, ported from 0 A.D.'s
// CCmpRangeManager LOS (source/simulation2/components/CCmpRangeManager.cpp):
// every LOS cell keeps a count per player of the sources that see it
// (m_LosPlayerCounts), a visible bit per player that is set while that count
// is above zero, and an explored bit per player that is set the first time it
// is (m_LosState). A source adds its circle when it appears and removes it
// when it moves, changes owner or vision range, or goes away (LosAdd,
// LosRemove), so sources that stay put cost nothing.
//
// RTS0 runs it on the 2-unit (8 m) spatial grid, with source positions kept
// to half a cell, 4 m: 0 A.D.'s LOS tile (which it refines further). All
// circle maths is integer, so a removal clears exactly the cells its add lit.
// Like 0 A.D.'s LOS, terrain does not block it; the fog players see is this LOS.
struct LosSource {
	int16_t qx = 0, qz = 0; // centre in half cells from the map's -x/-z corner
	uint16_t rq = 0;        // vision radius in half cells; 0 = no source
	uint8_t owner = 0xFF;   // player id; >= LosGrid::PLAYERS = no source
	uint8_t pad = 0;
	bool active() const { return rq > 0 && owner < 8; }
	bool operator==(const LosSource &o) const { return qx == o.qx && qz == o.qz && rq == o.rq && owner == o.owner; }
	bool operator!=(const LosSource &o) const { return !(*this == o); }
};
static_assert(sizeof(LosSource) == 8, "LosSource is saved raw");

class LosGrid {
public:
	static constexpr int PLAYERS = 8;
	static constexpr int Q = 2; // source steps per cell
	static constexpr int MAX_RQ = 128; // vision radii are clamped below 64 cells

	void init(int res, float half) {
		res_ = res;
		half_ = half;
		cell_ = res > 0 ? 2.0f * half / (float)res : 1.0f;
		const size_t n = (size_t)res * (size_t)res;
		counts_.assign(n * PLAYERS, 0);
		state_.assign(n, 0);
		build_off_world();
		if (wtab_.empty()) {
			wtab_.resize((size_t)MAX_RQ * MAX_RQ);
			for (int r = 0; r < MAX_RQ; ++r)
				for (int d = 0; d <= r; ++d) wtab_[(size_t)r * MAX_RQ + (size_t)d] = (uint8_t)isqrt((int64_t)r * r - (int64_t)d * d);
		}
	}
	int res() const { return res_; }
	size_t cells() const { return state_.size(); }

	// 0 A.D. circular maps (CCmpRangeManager SetLosCircular / LosIsOffWorld): the cells
	// whose centres lie outside the circle the map covers are off the world, never
	// visible or explored by anyone. Square maps (the default) have no such cells.
	void set_circular(bool on) {
		circular_ = on;
		build_off_world();
		for (size_t c = 0; c < off_world_.size(); ++c)
			if (off_world_[c]) state_[c] = 0;
	}
	bool circular() const { return circular_; }
	bool off_world(int cell) const { return !off_world_.empty() && off_world_[(size_t)cell]; }
	size_t in_world_cells() const { return state_.size() - off_world_count_; }

	// The source a vision circle of radius r around (x, z) makes for owner.
	LosSource source_at(float x, float z, float r, uint8_t owner) const {
		LosSource s;
		if (res_ <= 0 || r <= 0.0f || owner >= PLAYERS) return s;
		const float q = (float)Q / cell_;
		const float lim = (float)(res_ * Q);
		s.qx = (int16_t)std::clamp((x + half_) * q, 0.0f, lim);
		s.qz = (int16_t)std::clamp((z + half_) * q, 0.0f, lim);
		// At least a whole cell, so a source always lights the cell it stands in (a wall's
		// 4 m reaches no cell centre when the wall sits on a cell corner).
		s.rq = (uint16_t)std::clamp(r * q + 0.5f, (float)Q, (float)(MAX_RQ - 1));
		s.owner = owner;
		return s;
	}

	// source_at for a source that last added `have`: a centre that has not
	// moved a whole step from have's stays put, so a unit jostling on a step
	// boundary does not move its circle back and forth.
	LosSource source_near(const LosSource &have, float x, float z, float r, uint8_t owner) const {
		LosSource s = source_at(x, z, r, owner);
		if (!s.active() || !have.active()) return s;
		const float q = (float)Q / cell_;
		if (std::fabs((x + half_) * q - ((float)have.qx + 0.5f)) < 1.0f) s.qx = have.qx;
		if (std::fabs((z + half_) * q - ((float)have.qz + 0.5f)) < 1.0f) s.qz = have.qz;
		return s;
	}

	// Moves the circle `have` to `want` (either may be inactive) and stores it.
	void update(LosSource &have, const LosSource &want) {
		if (have == want) return;
		if (have.active() && want.active() && have.owner == want.owner) move(have, want);
		else {
			if (have.active()) apply(have, false);
			if (want.active()) apply(want, true);
		}
		have = want;
	}

	// Visible / explored bits of the cell holding (x, z), one bit per player.
	int cell_of(float x, float z) const {
		const int cx = std::clamp((int)((x + half_) / cell_), 0, res_ - 1);
		const int cz = std::clamp((int)((z + half_) / cell_), 0, res_ - 1);
		return cz * res_ + cx;
	}
	uint8_t visible_bits(int cell) const { return (uint8_t)(state_[(size_t)cell] & 0xFF); }
	uint8_t explored_bits(int cell) const { return (uint8_t)(state_[(size_t)cell] >> 8); }
	uint16_t count(uint8_t player, int cell) const { return counts_[(size_t)player * state_.size() + (size_t)cell]; }
	const std::vector<uint16_t> &state() const { return state_; }

	// Every cell explored by every player (0 A.D. "Explored map", or a loaded save's bits).
	void set_explored(int cell, uint8_t bits) {
		if (!off_world(cell)) state_[(size_t)cell] |= (uint16_t)((uint16_t)bits << 8);
	}
	void explore_all(uint8_t players) {
		for (size_t c = 0; c < state_.size(); ++c)
			if (!off_world((int)c)) state_[c] |= (uint16_t)((uint16_t)players << 8);
	}

	uint32_t hash() const {
		uint32_t h = 2166136261u;
		for (const uint16_t s : state_) {
			h = (h ^ (s & 0xFF)) * 16777619u;
			h = (h ^ (s >> 8)) * 16777619u;
		}
		return h;
	}

private:
	// The cells of row j whose centres lie within rq of the source, i0..i1
	// (LosUpdateHelper's strips); false when the row misses the circle.
	bool strip(const LosSource &s, int j, int &i0, int &i1) const {
		const int dz = std::abs(Q * j + Q / 2 - (int)s.qz);
		if (dz > (int)s.rq) return false;
		const int w = half_width(s.rq, dz);
		i0 = std::max(0, fdiv((int)s.qx - w - Q / 2 + Q - 1));
		i1 = std::min(res_ - 1, fdiv((int)s.qx + w - Q / 2));
		return i0 <= i1;
	}
	int row_lo(const LosSource &s) const { return std::max(0, fdiv((int)s.qz - (int)s.rq - Q / 2 + Q - 1)); }
	int row_hi(const LosSource &s) const { return std::min(res_ - 1, fdiv((int)s.qz + (int)s.rq - Q / 2)); }

	void add_cells(uint16_t *counts, uint16_t bits, size_t row, int i0, int i1) {
		if (off_world_.empty()) {
			for (int i = i0; i <= i1; ++i)
				if (counts[row + (size_t)i]++ == 0) state_[row + (size_t)i] |= bits;
			return;
		}
		for (int i = i0; i <= i1; ++i)
			if (counts[row + (size_t)i]++ == 0 && !off_world_[row + (size_t)i]) state_[row + (size_t)i] |= bits;
	}

	// LosIsOffWorld (CCmpRangeManager.cpp:2145): on a circular map a cell is off the world
	// when its centre lies outside the LOS circle, which sits MAP_EDGE_TILES-1 tiles (8 m)
	// inside the map's edge (r = verts/2 - MAP_EDGE_TILES + 1); on a square map the outer
	// MAP_EDGE_TILES (12 m) are off the world on every side.
	void build_off_world() {
		off_world_.clear();
		off_world_count_ = 0;
		if (res_ <= 0) return;
		off_world_.assign((size_t)res_ * (size_t)res_, 0);
		if (circular_) {
			// Circular: radius half_-8 m (2 RTS0 units).
			const float r_world = half_ - 2.0f;
			if (r_world <= 0.0f) {
				std::fill(off_world_.begin(), off_world_.end(), 1);
				off_world_count_ = off_world_.size();
				return;
			}
			for (int j = 0; j < res_; ++j)
				for (int i = 0; i < res_; ++i) {
					const float x = -half_ + ((float)i + 0.5f) * cell_;
					const float z = -half_ + ((float)j + 0.5f) * cell_;
					if (x * x + z * z >= r_world * r_world) {
						off_world_[(size_t)j * (size_t)res_ + (size_t)i] = 1;
						++off_world_count_;
					}
				}
		} else {
			// Square: the outer 3 tiles (12 m, MAP_EDGE_DIST) on every side.
			const float edge = 3.0f;
			const float lim = half_ - edge;
			for (int j = 0; j < res_; ++j)
				for (int i = 0; i < res_; ++i) {
					const float x = -half_ + ((float)i + 0.5f) * cell_;
					const float z = -half_ + ((float)j + 0.5f) * cell_;
					if (std::fabs(x) >= lim || std::fabs(z) >= lim) {
						off_world_[(size_t)j * (size_t)res_ + (size_t)i] = 1;
						++off_world_count_;
					}
				}
		}
	}
	void remove_cells(uint16_t *counts, uint16_t vis, size_t row, int i0, int i1) {
		for (int i = i0; i <= i1; ++i)
			if (--counts[row + (size_t)i] == 0) state_[row + (size_t)i] &= (uint16_t)~vis;
	}

	void apply(const LosSource &s, bool adding) {
		uint16_t *counts = counts_.data() + (size_t)s.owner * state_.size();
		const uint16_t vis = (uint16_t)(1u << s.owner);
		for (int j = row_lo(s), j1 = row_hi(s); j <= j1; ++j) {
			int i0, i1;
			if (!strip(s, j, i0, i1)) continue;
			if (adding) add_cells(counts, (uint16_t)(vis | (vis << 8)), (size_t)j * (size_t)res_, i0, i1);
			else remove_cells(counts, vis, (size_t)j * (size_t)res_, i0, i1);
		}
	}

	// LosUpdateHelperIncremental: one owner's circle moves (or changes size);
	// each row adds the cells only the new strip covers and removes the ones
	// only the old strip covered, so a short step touches a few cells.
	void move(const LosSource &from, const LosSource &to) {
		uint16_t *counts = counts_.data() + (size_t)to.owner * state_.size();
		const uint16_t vis = (uint16_t)(1u << to.owner), bits = (uint16_t)(vis | (vis << 8));
		const int j0 = std::min(row_lo(from), row_lo(to)), j1 = std::max(row_hi(from), row_hi(to));
		for (int j = j0; j <= j1; ++j) {
			int a0 = 0, a1 = -1, b0 = 0, b1 = -1;
			const bool had = strip(from, j, a0, a1), has = strip(to, j, b0, b1);
			const size_t row = (size_t)j * (size_t)res_;
			if (!had) {
				if (has) add_cells(counts, bits, row, b0, b1);
				continue;
			}
			if (!has || b1 < a0 || b0 > a1) {
				if (has) add_cells(counts, bits, row, b0, b1);
				remove_cells(counts, vis, row, a0, a1);
				continue;
			}
			if (b0 < a0) add_cells(counts, bits, row, b0, a0 - 1);
			if (b1 > a1) add_cells(counts, bits, row, a1 + 1, b1);
			if (a0 < b0) remove_cells(counts, vis, row, a0, b0 - 1);
			if (a1 > b1) remove_cells(counts, vis, row, b1 + 1, a1);
		}
	}
	// isqrt(rq^2 - dz^2), from a table (radii stay below MAX_RQ).
	int half_width(int rq, int dz) const { return wtab_[(size_t)rq * MAX_RQ + (size_t)dz]; }
	// floor(a / Q) for a > -1024 * Q (sources sit on the map, radii stay far below that).
	static int fdiv(int a) { return (a + 1024 * Q) / Q - 1024; }
	static int isqrt(int64_t v) {
		int64_t r = (int64_t)std::sqrt((double)v);
		while (r * r > v) --r;
		while ((r + 1) * (r + 1) <= v) ++r;
		return (int)r;
	}

	int res_ = 0;
	float half_ = 0.0f, cell_ = 1.0f;
	std::vector<uint16_t> counts_; // [player][cell] sources seeing the cell
	std::vector<uint16_t> state_;  // per cell: bit p visible to p, bit 8 + p explored by p
	std::vector<uint8_t> wtab_;   // half_width table
	bool circular_ = false;
	std::vector<uint8_t> off_world_; // per cell: 1 = outside a circular map (empty on square maps)
	size_t off_world_count_ = 0;
};

} // namespace rtso
