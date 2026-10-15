#ifndef RTSO_ARENA_H
#define RTSO_ARENA_H

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cassert>
#include "aligned.h"

namespace rtso {

// === Bump/Arena allocator — frame-local, zero-cost reset ===
// Aligns to 64B (cache line). No free(), only reset(). Thread-unsafe by design
// — each thread owns its arena. Use for per-tick temp buffers (pathfinding, forces).

class Arena {
	static constexpr size_t CACHE_LINE = 64;
	static constexpr size_t DEFAULT_CAP = 2 * 1024 * 1024; // 2 MB per frame

	uint8_t *buf = nullptr;
	size_t cap = 0;
	size_t offset = 0;

	static size_t align_up(size_t n, size_t a) { return (n + a - 1) & ~(a - 1); }

public:
	explicit Arena(size_t capacity = DEFAULT_CAP) { init(capacity); }
	~Arena() { release(); }

	Arena(const Arena &) = delete;
	Arena &operator=(const Arena &) = delete;
	Arena(Arena &&o) noexcept : buf(o.buf), cap(o.cap), offset(o.offset) { o.buf = nullptr; o.cap = o.offset = 0; }
	Arena &operator=(Arena &&o) noexcept {
		if (this != &o) { release(); buf = o.buf; cap = o.cap; offset = o.offset; o.buf = nullptr; o.cap = o.offset = 0; }
		return *this;
	}

	void init(size_t capacity) {
		cap = align_up(capacity, CACHE_LINE);
		buf = (uint8_t *)aligned_malloc(CACHE_LINE, cap);
		assert(buf && "Arena alloc failed");
		offset = 0;
	}
	void release() {
		if (buf) aligned_free(buf);
		buf = nullptr; cap = offset = 0;
	}
	void reset() { offset = 0; }

	// bump alloc, 64B aligned
	void *alloc(size_t size, size_t align = CACHE_LINE) {
		size_t p = align_up(offset, align);
		size_t next = p + align_up(size, align);
		assert(next <= cap && "Arena overflow — increase capacity or reset");
		if (next > cap) return nullptr;
		void *ptr = buf + p;
		offset = next;
		return ptr;
	}
	template <typename T>
	T *alloc_array(size_t count) {
		return static_cast<T *>(alloc(sizeof(T) * count, alignof(T) > CACHE_LINE ? alignof(T) : CACHE_LINE));
	}
	size_t used() const { return offset; }
	size_t remaining() const { return cap > offset ? cap - offset : 0; }
};

// === Double-buffered frame arena — ping-pong without copy ===
class FrameArena {
	Arena arenas[2];
	int cur = 0;
public:
	FrameArena(size_t per_frame = 2 * 1024 * 1024) { arenas[0].init(per_frame); arenas[1].init(per_frame); }
	Arena &current() { return arenas[cur]; }
	Arena &next() { return arenas[1 - cur]; }
	void flip() { arenas[cur].reset(); cur = 1 - cur; } // reset old, swap
	void reset_all() { arenas[0].reset(); arenas[1].reset(); cur = 0; }
};

} // namespace rtso

#endif
