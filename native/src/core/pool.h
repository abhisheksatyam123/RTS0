#ifndef RTSO_POOL_H
#define RTSO_POOL_H

#include <vector>
#include <cstdint>
#include <cassert>
#include <cstring>

namespace rtso {

// === Stable index pool — cache-friendly free list, no pointer chasing ===
// Indices never move; slot reuse via free list. Companion SoA arrays index by same slot.
// 32-bit index: 24 bits slot, 8 bits generation (ABA guard).

struct PoolHandle {
	uint32_t id = 0; // 0 = null
	static PoolHandle from_parts(uint32_t slot, uint32_t gen) { return { (gen << 24) | (slot & 0xFFFFFFu) }; }
	uint32_t slot() const { return id & 0xFFFFFFu; }
	uint32_t gen() const { return id >> 24; }
	bool valid() const { return id != 0; }
};

template <typename T>
class Pool {
	struct Slot {
		alignas(64) T value;
		uint32_t gen = 1;
		bool alive = false;
	};
	std::vector<Slot> slots; // slots[0] unused — id 0 = null
	std::vector<uint32_t> free_list;

public:
	Pool() { slots.reserve(1024); slots.push_back({}); } // dummy 0

	PoolHandle alloc(const T &v = T{}) {
		uint32_t idx;
		if (!free_list.empty()) {
			idx = free_list.back(); free_list.pop_back();
			slots[idx].value = v;
			slots[idx].alive = true;
		} else {
			idx = (uint32_t)slots.size();
			slots.push_back({});
			slots[idx].value = v;
			slots[idx].alive = true;
		}
		return PoolHandle::from_parts(idx, slots[idx].gen);
	}
	void free(PoolHandle h) {
		uint32_t s = h.slot();
		if (s == 0 || s >= slots.size() || !slots[s].alive || slots[s].gen != h.gen()) return;
		slots[s].alive = false;
		slots[s].gen = (slots[s].gen + 1) & 0xFFu;
		if (slots[s].gen == 0) slots[s].gen = 1;
		free_list.push_back(s);
	}
	T *get(PoolHandle h) {
		uint32_t s = h.slot();
		if (s == 0 || s >= slots.size() || !slots[s].alive || slots[s].gen != h.gen()) return nullptr;
		return &slots[s].value;
	}
	const T *get(PoolHandle h) const {
		uint32_t s = h.slot();
		if (s == 0 || s >= slots.size() || !slots[s].alive || slots[s].gen != h.gen()) return nullptr;
		return &slots[s].value;
	}
	size_t alive_count() const { return slots.size() - free_list.size() - 1; }
	size_t capacity() const { return slots.size(); }
	void clear() { slots.clear(); slots.push_back({}); free_list.clear(); }

	// Iterate alive slots cache-linearly
	template <typename F>
	void for_each(F &&fn) {
		for (uint32_t i = 1; i < slots.size(); ++i) if (slots[i].alive) fn(PoolHandle::from_parts(i, slots[i].gen), slots[i].value);
	}
};

} // namespace rtso

#endif
