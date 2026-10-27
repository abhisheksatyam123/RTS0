#ifndef RTSO_NAV_OBSTACLE_GRID_H
#define RTSO_NAV_OBSTACLE_GRID_H

// Spatial index for Simulation::separation()'s per-unit building scan.

#include "simulation/nav/footprint.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace rtso {
namespace nav {

// A building list as one bit set per square cell: bit i of a cell is set when building i's
// footprint's bounding box (x, z) +- footprint_extent overlaps the cell. A query ORs the cells under its box and visits
// the set bits, so candidates come in list order, once each, and a loop over them that applies the
// same tests as a loop over the whole list adds up the same results in the same order. Rebuild it
// whenever the list changes.
class ObstacleGrid {
public:
	template <class Buildings>
	void build(const Buildings& buildings, float map_half)
	{
		count_ = buildings.size();
		words_ = static_cast<int>((count_ + 63) / 64);
		origin_ = -map_half;
		w_ = std::max(1, static_cast<int>(std::ceil(2.0f * map_half / CELL)));
		masks_.assign(static_cast<size_t>(w_) * w_ * words_, 0);
		for (size_t i = 0; i < count_; ++i) {
			const auto& b = buildings[i];
			const float e = footprint_extent(b);
			const int x0 = axis(b.x - e), x1 = axis(b.x + e);
			const int z0 = axis(b.z - e), z1 = axis(b.z + e);
			for (int cz = z0; cz <= z1; ++cz)
				for (int cx = x0; cx <= x1; ++cx)
					masks_[(static_cast<size_t>(cz) * w_ + cx) * words_ + i / 64] |= uint64_t{1} << (i % 64);
		}
	}

	// Calls f(i), in ascending i, for each building i whose footprint may come within `reach` of
	// (x, z) on both axes: every one that does, maybe a few more (f still tests each). Stops when f
	// returns true. Returns false, calling nothing, when the box is so wide that scanning the whole
	// list costs less.
	template <class F>
	bool for_each_near(float x, float z, float reach, F&& f) const
	{
		if (words_ == 0) return true;
		if (words_ > MAX_WORDS || masks_.size() != static_cast<size_t>(w_) * w_ * words_) return false;
		// The 0.5 margin covers float rounding in the callers' distance tests.
		const float ext = reach + 0.5f;
		const int x0 = axis(x - ext), x1 = axis(x + ext);
		const int z0 = axis(z - ext), z1 = axis(z + ext);
		const size_t cells = static_cast<size_t>(x1 - x0 + 1) * static_cast<size_t>(z1 - z0 + 1);
		if (cells * words_ > count_) return false;
		uint64_t near[MAX_WORDS];
		std::fill_n(near, words_, uint64_t{0});
		for (int cz = z0; cz <= z1; ++cz)
			for (int cx = x0; cx <= x1; ++cx) {
				const uint64_t* m = &masks_[(static_cast<size_t>(cz) * w_ + cx) * words_];
				for (int k = 0; k < words_; ++k) near[k] |= m[k];
			}
		for (int k = 0; k < words_; ++k)
			for (uint64_t bits = near[k]; bits; bits &= bits - 1)
				if (f(static_cast<uint32_t>(k * 64 + std::countr_zero(bits)))) return true;
		return true;
	}

	// The same for the box [x0, x1] x [z0, z1]: f(i) for each building whose footprint's bounding
	// box may overlap it.
	template <class F>
	bool for_each_in_box(float x0, float z0, float x1, float z1, F&& f) const
	{
		if (words_ == 0) return true;
		if (words_ > MAX_WORDS || masks_.size() != static_cast<size_t>(w_) * w_ * words_) return false;
		const int cx0 = axis(x0 - 0.5f), cx1 = axis(x1 + 0.5f);
		const int cz0 = axis(z0 - 0.5f), cz1 = axis(z1 + 0.5f);
		const size_t cells = static_cast<size_t>(cx1 - cx0 + 1) * static_cast<size_t>(cz1 - cz0 + 1);
		if (cells * words_ > count_) return false;
		uint64_t near[MAX_WORDS];
		std::fill_n(near, words_, uint64_t{0});
		for (int cz = cz0; cz <= cz1; ++cz)
			for (int cx = cx0; cx <= cx1; ++cx) {
				const uint64_t* m = &masks_[(static_cast<size_t>(cz) * w_ + cx) * words_];
				for (int k = 0; k < words_; ++k) near[k] |= m[k];
			}
		for (int k = 0; k < words_; ++k)
			for (uint64_t bits = near[k]; bits; bits &= bits - 1)
				if (f(static_cast<uint32_t>(k * 64 + std::countr_zero(bits)))) return true;
		return true;
	}

	size_t size() const { return count_; }

private:
	static constexpr float CELL = 8.0f;
	static constexpr int MAX_WORDS = 64; // lists of up to 4096 buildings use the grid
	// Monotonic in f, so a coordinate between two others lands in a cell between theirs.
	int axis(float f) const
	{
		const float c = (f - origin_) / CELL;
		return c >= 0.0f ? (c < static_cast<float>(w_) ? static_cast<int>(c) : w_ - 1) : 0;
	}

	size_t count_ = 0;
	int words_ = 0, w_ = 0;
	float origin_ = 0.0f;
	std::vector<uint64_t> masks_; // (cell row * w_ + cell column) * words_ + word
};

// A long list of small obstacles (trees, fruit bushes, treasures) bucketed by the cell holding
// each one's centre, for lists too long for ObstacleGrid's bit sets. A query visits the cells
// under its box grown by the widest obstacle, row by row, and each cell's obstacles in list
// order, so a given query always visits the same obstacles in the same order. Rebuild it
// whenever the list changes.
class BucketGrid {
public:
	template <class List>
	void build(const List& list, float map_half)
	{
		origin_ = -map_half;
		w_ = std::max(1, static_cast<int>(std::ceil(2.0f * map_half / CELL)));
		max_ext_ = 0.0f;
		start_.assign(static_cast<size_t>(w_) * w_ + 1, 0);
		for (const auto& o : list) {
			++start_[cell(o.x, o.z) + 1];
			max_ext_ = std::max(max_ext_, footprint_extent(o));
		}
		for (size_t c = 1; c < start_.size(); ++c) start_[c] += start_[c - 1];
		items_.resize(list.size());
		std::vector<uint32_t> fill(start_.begin(), start_.end() - 1);
		for (size_t i = 0; i < list.size(); ++i) items_[fill[cell(list[i].x, list[i].z)]++] = static_cast<uint32_t>(i);
	}

	// Calls f(i) for each obstacle i whose footprint may reach into the box [x0, x1] x [z0, z1]:
	// every one that does, and some that don't (f tests each).
	template <class F>
	void for_each_in_box(float x0, float z0, float x1, float z1, F&& f) const
	{
		if (items_.empty()) return;
		const int cx0 = axis(x0 - max_ext_), cx1 = axis(x1 + max_ext_);
		const int cz0 = axis(z0 - max_ext_), cz1 = axis(z1 + max_ext_);
		for (int cz = cz0; cz <= cz1; ++cz) {
			const size_t row = static_cast<size_t>(cz) * w_;
			for (uint32_t k = start_[row + cx0], e = start_[row + cx1 + 1]; k < e; ++k) f(items_[k]);
		}
	}

	// The same, stopping at the first i for which f returns true. True when one did.
	template <class F>
	bool any_in_box(float x0, float z0, float x1, float z1, F&& f) const
	{
		if (items_.empty()) return false;
		const int cx0 = axis(x0 - max_ext_), cx1 = axis(x1 + max_ext_);
		const int cz0 = axis(z0 - max_ext_), cz1 = axis(z1 + max_ext_);
		for (int cz = cz0; cz <= cz1; ++cz) {
			const size_t row = static_cast<size_t>(cz) * w_;
			for (uint32_t k = start_[row + cx0], e = start_[row + cx1 + 1]; k < e; ++k)
				if (f(items_[k])) return true;
		}
		return false;
	}

	bool empty() const { return items_.empty(); }

private:
	static constexpr float CELL = 2.0f;
	int axis(float f) const
	{
		const float c = (f - origin_) / CELL;
		return c >= 0.0f ? (c < static_cast<float>(w_) ? static_cast<int>(c) : w_ - 1) : 0;
	}
	size_t cell(float x, float z) const { return static_cast<size_t>(axis(z)) * w_ + axis(x); }

	int w_ = 0;
	float origin_ = 0.0f, max_ext_ = 0.0f;
	std::vector<uint32_t> start_; // CSR: cell c's obstacles are items_[start_[c] .. start_[c + 1])
	std::vector<uint32_t> items_;
};

// Two lists iterated as one (a then b), for the steering templates that take one range of
// obstacles: the buildings and mines, then the trees near the unit.
template <class T>
struct ConcatRange {
	const std::vector<T>& a;
	const std::vector<T>& b;
	struct iterator {
		const ConcatRange* r;
		size_t i;
		const T& operator*() const { return i < r->a.size() ? r->a[i] : r->b[i - r->a.size()]; }
		iterator& operator++() { ++i; return *this; }
		bool operator!=(const iterator& o) const { return i != o.i; }
	};
	iterator begin() const { return {this, 0}; }
	iterator end() const { return {this, a.size() + b.size()}; }
};

} // namespace nav
} // namespace rtso

#endif // RTSO_NAV_OBSTACLE_GRID_H
