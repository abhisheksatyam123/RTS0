#include "job_system.h"
#include <algorithm>

namespace rtso {

JobSystem &JobSystem::instance() {
	static JobSystem js;
	return js;
}

void JobSystem::start(unsigned worker_count) {
	std::lock_guard<std::mutex> lock(mu_);
	if (started_) return;
	unsigned hw = std::max(1u, std::thread::hardware_concurrency());
	if (worker_count == 0) {
		// Leave a core for Godot's render/main threads; remaining cores run sim jobs.
		worker_count = hw > 2 ? hw - 1 : 1;
	}
	stop_ = false;
	epoch_ = 0;
	workers_finished_ = 0;
	job_ = nullptr;
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
		job_ = nullptr;
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
	uint64_t seen = 0;
	for (;;) {
		Job *job = nullptr;
		{
			std::unique_lock<std::mutex> lock(mu_);
			cv_work_.wait(lock, [this, seen]() { return stop_ || epoch_ != seen; });
			if (stop_ && epoch_ == seen) return;
			seen = epoch_;
			job = job_;
		}
		if (job) run_chunks(*job);
		{
			std::lock_guard<std::mutex> lock(mu_);
			workers_finished_++;
			cv_done_.notify_all();
		}
	}
}

void JobSystem::run_chunks(Job &job) {
	for (;;) {
		size_t c = job.next.fetch_add(1, std::memory_order_relaxed);
		if (c >= job.chunks) return;
		size_t a = c * job.grain;
		size_t b = std::min(a + job.grain, job.count);
		job.fn(a, b);
		job.remaining.fetch_sub(1, std::memory_order_acq_rel);
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

	Job job;
	job.fn = fn;
	job.count = count;
	job.grain = grain;
	job.chunks = chunks;
	job.next.store(0, std::memory_order_relaxed);
	job.remaining.store(chunks, std::memory_order_relaxed);

	{
		std::lock_guard<std::mutex> lock(mu_);
		job_ = &job;
		workers_finished_ = 0;
		epoch_++;
	}
	cv_work_.notify_all();
	run_chunks(job);

	{
		std::unique_lock<std::mutex> lock(mu_);
		cv_done_.wait(lock, [this, &job]() {
			return job.remaining.load(std::memory_order_acquire) == 0 &&
					workers_finished_ >= workers_.size();
		});
		job_ = nullptr;
	}
	in_parallel_.store(false);
}

} // namespace rtso
