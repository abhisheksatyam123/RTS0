#include "perf_tracker.h"
#include "logger.h"
#include <cstdio>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/performance.hpp>
#include <godot_cpp/classes/engine.hpp>

namespace rtso {

static uint64_t s_tick_ms = 0;
static uint64_t s_sync_ms = 0;
static int s_unit_count = 0;
static int s_building_count = 0;
static int s_frame = 0;

static uint64_t s_mem_static = 0;
static uint64_t s_mem_peak = 0;
static uint64_t s_vram = 0;
static double s_nodes = 0;
static double s_objects = 0;
static double s_draw_calls = 0;
static double s_render_objects = 0;
static double s_prims = 0;

void PerfTracker::begin_frame() {
}

void PerfTracker::record_tick(uint64_t ms) {
	s_tick_ms = ms;
}

void PerfTracker::record_sync(uint64_t ms) {
	s_sync_ms = ms;
}

void PerfTracker::record_draw(int unit_count, int building_count) {
	s_unit_count = unit_count;
	s_building_count = building_count;
}

void PerfTracker::sample_memory() {
	godot::OS *os = godot::OS::get_singleton();
	if (os) {
		s_mem_static = os->get_static_memory_usage();
		s_mem_peak = os->get_static_memory_peak_usage();
	}
	godot::Performance *perf = godot::Performance::get_singleton();
	if (!perf) return;
	s_nodes = perf->get_monitor(godot::Performance::OBJECT_NODE_COUNT);
	s_objects = perf->get_monitor(godot::Performance::OBJECT_COUNT);
	s_draw_calls = perf->get_monitor(godot::Performance::RENDER_TOTAL_DRAW_CALLS_IN_FRAME);
	s_render_objects = perf->get_monitor(godot::Performance::RENDER_TOTAL_OBJECTS_IN_FRAME);
	s_prims = perf->get_monitor(godot::Performance::RENDER_TOTAL_PRIMITIVES_IN_FRAME);
	s_vram = (uint64_t)perf->get_monitor(godot::Performance::RENDER_VIDEO_MEM_USED);
}

void PerfTracker::maybe_log() {
	if ((++s_frame % 60) != 0) return;
	sample_memory();
	char buf[320];
	int fps = 0;
	if (godot::Engine::get_singleton()) {
		fps = (int)godot::Engine::get_singleton()->get_frames_per_second();
	}
	std::snprintf(buf, sizeof(buf),
			"fps=%d tick_ms=%llu sync_ms=%llu units=%d nodes=%.0f objs=%.0f draws=%.0f r_obj=%.0f prims=%.0f mem=%.1fMB peak=%.1fMB vram=%.1fMB",
			fps,
			(unsigned long long)s_tick_ms,
			(unsigned long long)s_sync_ms,
			s_unit_count,
			s_nodes, s_objects, s_draw_calls, s_render_objects, s_prims,
			s_mem_static / (1024.0 * 1024.0),
			s_mem_peak / (1024.0 * 1024.0),
			s_vram / (1024.0 * 1024.0));
	Logger::info("mem", buf);
}

godot::String PerfTracker::get_report() {
	return godot::String("Tick: ") + godot::String::num_int64(s_tick_ms) +
		"ms | Sync: " + godot::String::num_int64(s_sync_ms) +
		"ms | Units: " + godot::String::num_int64(s_unit_count) +
		" | Nodes: " + godot::String::num((s_nodes), 0) +
		" | Draws: " + godot::String::num((s_draw_calls), 0) +
		" | Mem: " + godot::String::num(s_mem_static / (1024.0 * 1024.0), 1) + "MB";
}

uint64_t PerfTracker::get_tick_ms() { return s_tick_ms; }
int PerfTracker::get_last_unit_count() { return s_unit_count; }
int PerfTracker::get_last_building_count() { return s_building_count; }

} // namespace rtso
