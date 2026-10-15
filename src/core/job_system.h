#ifndef RTSO_JOB_SYSTEM_H
#define RTSO_JOB_SYSTEM_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace rtso {

// Persistent worker pool. Main thread also participates in parallel_for.
// Used by Simulation::tick for SoA unit loops (move / separation / aggro).
class JobSystem {
public:
	static JobSystem &instance();

	void start(unsigned worker_count = 0);
	void stop();
	~JobSystem() { stop(); }
	bool running() const { return started_; }
	unsigned worker_count() const { return (unsigned)workers_.size(); }
	// Workers + the caller of parallel_for.
	unsigned parallel_width() const { return worker_count() + 1u; }

	void parallel_for(size_t count, const std::function<void(size_t begin, size_t end)> &fn,
			size_t min_grain = 16);

	template <typename F>
	void for_each_index(size_t count, F &&fn, size_t min_grain = 16) {
		parallel_for(count, [&](size_t a, size_t b) {
			for (size_t i = a; i < b; ++i) fn(i);
		}, min_grain);
	}

private:
	// One parallel_for at a time. Its chunks are claimed from `next_`, which carries the job's
	// epoch in its high 32 bits: a worker that wakes late, after the job is over, fails to claim
	// and goes back to waiting, so the caller waits only for the chunks already claimed, never for
	// every worker to wake, and a worker never runs a chunk of a job it didn't see start.
	struct Job {
		const std::function<void(size_t, size_t)> *fn = nullptr;
		size_t count = 0;
		size_t grain = 1;
		std::atomic<uint32_t> chunks{0};
	};
	void worker_main();
	// Runs chunks of the job of `epoch` until none is left (or another job has begun).
	void run_chunks(uint32_t epoch);
	std::mutex mu_;
	std::condition_variable cv_work_;
	std::vector<std::thread> workers_;
	Job job_;
	std::atomic<uint32_t> epoch_{0};     // bumped as each job is published
	std::atomic<uint64_t> next_{0};      // (epoch << 32) | next chunk
	std::atomic<uint32_t> remaining_{0}; // chunks of the current job not yet finished
	std::atomic<uint32_t> sleepers_{0};  // workers waiting on cv_work_
	std::atomic<bool> stop_{false};
	std::atomic<bool> in_parallel_{false};
	bool started_ = false;
};

} // namespace rtso

#endif
