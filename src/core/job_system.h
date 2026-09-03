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
	struct Job {
		std::function<void(size_t, size_t)> fn;
		size_t count = 0;
		size_t grain = 1;
		size_t chunks = 0;
		std::atomic<size_t> next{0};
		std::atomic<size_t> remaining{0};
	};

	void worker_main();
	void run_chunks(Job &job);

	std::mutex mu_;
	std::condition_variable cv_work_;
	std::condition_variable cv_done_;
	std::vector<std::thread> workers_;
	Job *job_ = nullptr;
	uint64_t epoch_ = 0;
	size_t workers_finished_ = 0;
	std::atomic<bool> stop_{false};
	std::atomic<bool> in_parallel_{false};
	bool started_ = false;
};

} // namespace rtso

#endif
