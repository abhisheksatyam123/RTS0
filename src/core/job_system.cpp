#include "job_system.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <set>
#include <string>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#define RTSO_CPU_RELAX() _mm_pause()
#elif defined(__aarch64__) || defined(__arm__)
#define RTSO_CPU_RELAX() __asm__ __volatile__("yield")
#else
#define RTSO_CPU_RELAX() ((void)0)
#endif

namespace rtso {

namespace {
// A worker that runs out of chunks keeps watching for the next job for a while before it sleeps:
// a tick's parallel loops are a few hundred microseconds long and come a millisecond or so apart,
// shorter than a sleeping thread takes to wake, so sleeping workers would miss most of each loop.
// It spins briefly, then yields its core to any other thread (Godot's) while it waits, and sleeps
// once no job came for kWatchFor (between turns and frames).
constexpr auto kSpinFor = std::chrono::microseconds(500);
constexpr auto kWatchFor = std::chrono::microseconds(2000);
} // namespace

JobSystem &JobSystem::instance() {
	static JobSystem js;
	return js;
}

unsigned JobSystem::physical_core_count() {
	unsigned logical = std::max(1u, std::thread::hardware_concurrency());
#if defined(__linux__)
	std::ifstream cpuinfo("/proc/cpuinfo");
	if (cpuinfo.is_open()) {
		std::set<std::pair<int, int>> phys_cores;
		std::string line;
		int phys_id = 0, core_id = -1;
		while (std::getline(cpuinfo, line)) {
			if (line.compare(0, 11, "physical id") == 0) {
				auto pos = line.find(':');
				if (pos != std::string::npos) phys_id = std::atoi(line.c_str() + pos + 1);
			} else if (line.compare(0, 7, "core id") == 0) {
				auto pos = line.find(':');
				if (pos != std::string::npos) {
					core_id = std::atoi(line.c_str() + pos + 1);
					phys_cores.insert({phys_id, core_id});
				}
			}
		}
		if (!phys_cores.empty()) {
			return (unsigned)phys_cores.size();
		}
	}
#endif
	return logical > 2 ? std::max(2u, logical / 2) : logical;
}

void JobSystem::start(unsigned worker_count) {
	std::lock_guard<std::mutex> lock(mu_);
	if (started_) return;
	if (worker_count == 0) {
		// Bound default worker count to physical cores (TODO-JOB-03) to avoid SMT cache
		// contention; main thread participates in parallel_for alongside workers.
		unsigned phys = physical_core_count();
		worker_count = phys > 1 ? phys - 1 : 1;
	}
	stop_ = false;
	workers_.reserve(worker_count);
	for (unsigned i = 0; i < worker_count; ++i) {
		workers_.emplace_back([this]() { worker_main(); });
	}
	started_ = true;
}

void JobSystem::stop() {
	{
		std::lock_guard<std::mutex> lock(mu_);
		if (!started_) return;
		stop_ = true;
	}
	cv_work_.notify_all();
	for (auto &t : workers_) {
		if (t.joinable()) t.join();
	}
	{
		std::lock_guard<std::mutex> lock(mu_);
		workers_.clear();
		started_ = false;
		stop_ = false;
	}
}

void JobSystem::worker_main() {
	uint32_t seen = epoch_.load(std::memory_order_acquire);
	for (;;) {
		uint32_t e = epoch_.load(std::memory_order_acquire);
		if (e == seen && !stop_.load(std::memory_order_acquire)) {
			const auto start = std::chrono::steady_clock::now();
			const auto spin_until = start + kSpinFor, until = start + kWatchFor;
			bool spinning = true;
			for (int k = 0;; ++k) {
				if (spinning) RTSO_CPU_RELAX();
				else std::this_thread::yield();
				e = epoch_.load(std::memory_order_acquire);
				if (e != seen || stop_.load(std::memory_order_acquire)) break;
				if (spinning && (k & 15) != 15) continue;
				const auto now = std::chrono::steady_clock::now();
				spinning = now < spin_until;
				if (now >= until) {
					std::unique_lock<std::mutex> lock(mu_);
					sleepers_.fetch_add(1, std::memory_order_seq_cst);
					cv_work_.wait(lock, [&]() {
						e = epoch_.load(std::memory_order_acquire);
						return e != seen || stop_.load(std::memory_order_acquire);
					});
					sleepers_.fetch_sub(1, std::memory_order_relaxed);
					break;
				}
			}
		}
		if (stop_.load(std::memory_order_acquire)) return;
		seen = e;
		run_chunks(e);
	}
}

void JobSystem::run_chunks(uint32_t epoch) {
	const uint64_t tag = (uint64_t)epoch << 32;
	for (;;) {
		uint64_t cur = next_.load(std::memory_order_seq_cst);
		uint32_t c;
		do {
			if ((cur & ~0xFFFFFFFFull) != tag) return; // another job by now
			c = (uint32_t)cur;
			// Read after `cur`: a count of the next job comes with `next_` already closed to this one
			// (parallel_for), and the compare-and-swap below then fails.
			if (c >= job_.chunks.load(std::memory_order_seq_cst)) return;
		} while (!next_.compare_exchange_weak(cur, cur + 1, std::memory_order_seq_cst, std::memory_order_seq_cst));
		// The job's fields stay put until its last chunk is done, and this one isn't yet.
		const size_t a = (size_t)c * job_.grain;
		const size_t b = std::min(a + job_.grain, job_.count);
		(*job_.fn)(a, b);
		remaining_.fetch_sub(1, std::memory_order_acq_rel);
	}
}

void JobSystem::parallel_for(size_t count, const std::function<void(size_t, size_t)> &fn, size_t min_grain) {
	if (count == 0) return;
	if (!started_ || workers_.empty() || count < min_grain * 2) {
		fn(0, count);
		return;
	}
	// Nested / overlapping parallel_for would overwrite job_ and deadlock the pool.
	if (in_parallel_.exchange(true)) {
		fn(0, count);
		return;
	}

	size_t width = (size_t)parallel_width();
	size_t grain = std::max(min_grain, (count + width * 4 - 1) / (width * 4));
	size_t chunks = (count + grain - 1) / grain;

	// Close the last job's claim word before its fields change. A worker that ran that job's last
	// chunk may still be in run_chunks, with `next_` read under the last job's tag and its chunk
	// index at that job's count. Were it to read this job's (larger) chunk count before `next_`
	// moved on, its compare-and-swap would still succeed: it would run a chunk past the last job's
	// end, with this job's fields, and count it off `remaining_`, so this job would finish early, or
	// never. With the new tag and no chunk left in `next_` first, that compare-and-swap fails. The
	// job's fields are written after it, and its chunk count is read and written in sequentially
	// consistent order with `next_`, so a worker that reads the new count also reads the closed word.
	const uint32_t epoch = epoch_.load(std::memory_order_relaxed) + 1;
	next_.store(((uint64_t)epoch << 32) | 0xFFFFFFFFull, std::memory_order_seq_cst);
	job_.fn = &fn;
	job_.count = count;
	job_.grain = grain;
	job_.chunks.store((uint32_t)chunks, std::memory_order_seq_cst);
	remaining_.store((uint32_t)chunks, std::memory_order_relaxed);
	next_.store((uint64_t)epoch << 32, std::memory_order_release);
	epoch_.store(epoch, std::memory_order_seq_cst);
	if (sleepers_.load(std::memory_order_seq_cst) > 0) {
		// Taking the lock orders this wake after a sleeper's check of the epoch.
		{ std::lock_guard<std::mutex> lock(mu_); }
		cv_work_.notify_all();
	}
	run_chunks(epoch);
	// Only chunks a worker already claimed can be left; they finish shortly.
	while (remaining_.load(std::memory_order_acquire) != 0) RTSO_CPU_RELAX();
	in_parallel_.store(false);
}

} // namespace rtso
