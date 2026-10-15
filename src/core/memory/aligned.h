#ifndef RTSO_ALIGNED_H
#define RTSO_ALIGNED_H

#include <cstddef>
#include <cstdlib>
#if defined(_WIN32)
#include <malloc.h>
#endif

namespace rtso {

// Cache-aligned heap blocks. Windows has neither posix_memalign nor a
// free()-compatible aligned_alloc, so blocks from here go back through aligned_free.
inline void *aligned_malloc(size_t align, size_t n) {
#if defined(_WIN32)
	return _aligned_malloc(n, align);
#else
	void *p = nullptr;
	if (posix_memalign(&p, align, n) != 0) return nullptr;
	return p;
#endif
}

inline void aligned_free(void *p) {
#if defined(_WIN32)
	_aligned_free(p);
#else
	free(p);
#endif
}

} // namespace rtso

#endif
