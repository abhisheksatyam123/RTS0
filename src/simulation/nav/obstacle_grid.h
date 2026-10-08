#ifndef RTSO_NAV_OBSTACLE_GRID_H
#define RTSO_NAV_OBSTACLE_GRID_H

// Spatial index for Simulation::separation()'s per-unit building scan.

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace rtso {
namespace nav {

// A building list as one bit set per square cell: bit i of a cell is set when building i's
// footprint (x, z) +- half_size overlaps the cell. A query ORs the cells under its box and visits
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
			const int x0 = axis(b.x - b.half_size), x1 = axis(b.x + b.half_size);
			const int z0 = axis(b.z - b.half_size), z1 = axis(b.z + b.half_size);
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
		uint64_t near[MAX_WORDS] = {};
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

	size_t size() const { return count_; }

private:
	static constexpr float CELL = 8.0f;
	static constexpr int MAX_WORDS = 16; // lists of up to 1024 buildings use the grid
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

} // namespace nav
} // namespace rtso

#endif // RTSO_NAV_OBSTACLE_GRID_H
