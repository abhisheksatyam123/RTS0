#ifndef RTSO_CELL_SET_H
#define RTSO_CELL_SET_H

// A set of integer grid cells, each marked FULL or PART, in a dense byte grid that grows to cover
// every marked cell. Simulation::occupied_cells_ marks a cell FULL where a building's footprint
// covers it wholly and PART where the footprint only reaches into it. Simulation::can_place asks
// it once per footprint cell, and Petra's placement search asks it for every cell of every ring
// around its base.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace rtso {

class CellSet {
public:
	static constexpr uint8_t PART = 1, FULL = 2;

	void clear() { std::fill(cells_.begin(), cells_.end(), (uint8_t)0); }

	// Marks (x, y) with `mark`; a cell keeps the higher of its marks.
	void insert(int x, int y, uint8_t mark = FULL)
	{
		if (!covers(x, y)) grow(x, y);
		uint8_t &c = cells_[index(x, y)];
		c = std::max(c, mark);
	}

	bool contains(int x, int y) const { return covers(x, y) && cells_[index(x, y)]; }
	uint8_t mark(int x, int y) const { return covers(x, y) ? cells_[index(x, y)] : (uint8_t)0; }

private:
	static constexpr int PAD = 64; // cells added beyond an insert outside the grid

	int x0_ = 0, y0_ = 0, w_ = 0, h_ = 0;
	std::vector<uint8_t> cells_;

	bool covers(int x, int y) const
	{
		return (unsigned)(x - x0_) < (unsigned)w_ && (unsigned)(y - y0_) < (unsigned)h_;
	}
	size_t index(int x, int y) const { return (size_t)(y - y0_) * (size_t)w_ + (size_t)(x - x0_); }

	void grow(int x, int y)
	{
		const int nx0 = w_ ? std::min(x0_, x - PAD) : x - PAD, ny0 = h_ ? std::min(y0_, y - PAD) : y - PAD;
		const int nx1 = w_ ? std::max(x0_ + w_, x + PAD) : x + PAD, ny1 = h_ ? std::max(y0_ + h_, y + PAD) : y + PAD;
		std::vector<uint8_t> grown((size_t)(nx1 - nx0) * (size_t)(ny1 - ny0), 0);
		for (int r = 0; r < h_; ++r)
			std::copy_n(cells_.begin() + (ptrdiff_t)((size_t)r * (size_t)w_), w_,
			            grown.begin() + (ptrdiff_t)((size_t)(y0_ + r - ny0) * (size_t)(nx1 - nx0) + (size_t)(x0_ - nx0)));
		cells_ = std::move(grown);
		x0_ = nx0;
		y0_ = ny0;
		w_ = nx1 - nx0;
		h_ = ny1 - ny0;
	}
};

} // namespace rtso

#endif // RTSO_CELL_SET_H
