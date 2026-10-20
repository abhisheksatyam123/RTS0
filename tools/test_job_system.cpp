// JobSystem (src/core/job_system.cpp) with its worker threads running, as the game always starts it
// (register_types.cpp) and no other headless test does.
//  1. parallel_for runs every index of a job exactly once and returns only when all are done, over
//     many jobs back to back whose chunk counts go up and down, as a move command's loop over its
//     units (cmd_move_individual) and then the tick's loops over every unit do. A worker still
//     finishing one job must never claim a chunk of the next: it would run it outside that job's
//     count and finish the next job early, while another worker is still inside its loop.
//  2. A match with units walking through a herd ends on the same lockstep hash with three workers as
//     serially.
// A watchdog fails the test if a job never returns.
//
// g++ -std=c++20 -O2 -Isrc tools/test_job_system.cpp src/simulation/simulation.cpp \
//   src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//   src/network/net_host.cpp -lpthread -o /tmp/test_job_system && /tmp/test_job_system
#define private public
#include "simulation/simulation.h"
#undef private
#include "core/job_system.h"
#include "network/turn_manager.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>
#include <vector>

using namespace rtso;

static int failures = 0;
#define CHECK(cond, msg)                                                                 \
	do {                                                                                 \
		if (!(cond)) {                                                                   \
			std::printf("  FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__);                \
			++failures;                                                                  \
		} else {                                                                         \
			std::printf("  ok: %s\n", msg);                                              \
		}                                                                                \
	} while (0)

static std::atomic<uint64_t> g_progress{0};
static std::atomic<bool> g_done{false};

// Fails the test when no job has returned for 10 s: a job whose count of chunks left never
// reaches 0 spins its caller for good.
static void watchdog() {
	uint64_t last = g_progress.load();
	auto since = std::chrono::steady_clock::now();
	while (!g_done.load()) {
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		const uint64_t now = g_progress.load();
		if (now != last) {
			last = now;
			since = std::chrono::steady_clock::now();
		} else if (std::chrono::steady_clock::now() - since > std::chrono::seconds(10)) {
			std::printf("  FAIL: a parallel_for has not returned for 10 s (job %llu)\nRESULT: FAIL\n", (unsigned long long)now);
			std::fflush(stdout);
			std::_Exit(1);
		}
	}
}

static uint32_t run_match(int ticks) {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(11);
	s->reset();
	for (uint8_t p = 0; p < 2; ++p) s->set_player_active(p, true, false);
	s->spawn_building_world(-56.0f, -56.0f, BLD_TOWN_CENTER, 0, 1.0f);
	s->spawn_building_world(56.0f, 56.0f, BLD_TOWN_CENTER, 1, 1.0f);
	// Units walk through a herd of animals of every size.
	const uint8_t kinds[] = {FAUNA_DEER, FAUNA_SHEEP, FAUNA_BOAR, FAUNA_GOAT, FAUNA_CATTLE_COW, FAUNA_ELEPHANT_AFRICAN_BUSH};
	for (int k = 0; k < 300; ++k) s->spawn_fauna(-30.0f + 1.6f * (k % 30), -8.0f + 1.6f * (k / 30), kinds[k % 6]);
	std::vector<uint32_t> men;
	for (int k = 0; k < 60; ++k) men.push_back(s->spawn_unit(-30.0f + 2.0f * (k % 30), -30.0f + 1.5f * (k / 30), 0, UNIT_SPEARMAN));
	s->order_walk(men.data(), men.size(), 0.0f, 30.0f);
	for (int t = 0; t < ticks; ++t) {
		s->tick(Simulation::FIXED_STEP);
		g_progress.fetch_add(1);
	}
	return TurnManager().boot_sim_hash(*s);
}

int main() {
	std::thread dog(watchdog);
	JobSystem &js = JobSystem::instance();
	js.start(3);
	std::printf("JobSystem with %u workers\n", js.worker_count());

	{
		// Bursts of 16 jobs back to back, of 2 to 16 chunks (min_grain 16, width 4), each counting the
		// runs of its indices in its own array; a chunk that runs after its job returned is late.
		constexpr int B = 16, BURSTS = 8000;
		constexpr size_t sizes[B] = {64, 4096, 48, 2048, 64, 4096, 32, 1024, 64, 4096, 40, 3000, 64, 4096, 33, 512};
		std::vector<std::vector<std::atomic<uint32_t>>> hits(B);
		for (auto &h : hits) h = std::vector<std::atomic<uint32_t>>(4096 + 64);
		std::atomic<uint32_t> returned[B];
		std::atomic<uint64_t> late{0};
		uint64_t wrong = 0, extra = 0;
		for (int burst = 0; burst < BURSTS; ++burst) {
			for (auto &r : returned) r.store(0);
			for (int k = 0; k < B; ++k) {
				auto &h = hits[k];
				auto &ret = returned[k];
				js.for_each_index(sizes[k], [&](size_t i) {
					h[i].fetch_add(1, std::memory_order_relaxed);
					if (ret.load(std::memory_order_relaxed)) late.fetch_add(1);
				});
				ret.store(1);
				g_progress.fetch_add(1);
			}
			// Every index of each job ran exactly once, and nothing past a job's end.
			for (int k = 0; k < B; ++k) {
				for (size_t i = 0; i < sizes[k]; ++i) wrong += hits[k][i].exchange(0, std::memory_order_relaxed) != 1;
				for (size_t i = sizes[k]; i < hits[k].size(); ++i) extra += hits[k][i].exchange(0, std::memory_order_relaxed) != 0;
			}
		}
		std::printf("  %d jobs: %llu indices not run exactly once, %llu run past a job's end, %llu run after the job returned\n", B * BURSTS,
		            (unsigned long long)wrong, (unsigned long long)extra, (unsigned long long)late.load());
		CHECK(wrong == 0 && extra == 0 && late.load() == 0, "parallel_for runs each index of each job once, before it returns");
	}

	{
		constexpr int TICKS = 600;
		const uint32_t threaded = run_match(TICKS);
		js.stop();
		const uint32_t serial = run_match(TICKS);
		std::printf("  units walking through a herd, %d ticks: hash %08x with 3 workers, %08x serially\n", TICKS, threaded, serial);
		CHECK(threaded == serial, "a match of units walking through a herd ends on the same hash with workers as without");
	}

	g_done = true;
	dog.join();
	std::printf(failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", failures);
	return failures ? 1 : 0;
}
