#include "perf_tracker.h"
#include <godot_cpp/variant/utility_functions.hpp>

namespace rtso {

static uint64_t s_tick_ms = 0;
static int s_unit_count = 0;
static int s_building_count = 0;

void PerfTracker::begin_frame() {
}

void PerfTracker::record_tick(uint64_t ms) {
	s_tick_ms = ms;
}

void PerfTracker::record_draw(int unit_count, int building_count) {
	s_unit_count = unit_count;
	s_building_count = building_count;
}

godot::String PerfTracker::get_report() {
	return godot::String("Tick: ") + godot::String::num_int64(s_tick_ms) +
		"ms | Units: " + godot::String::num_int64(s_unit_count) +
		" | Buildings: " + godot::String::num_int64(s_building_count);
}

uint64_t PerfTracker::get_tick_ms() { return s_tick_ms; }
int PerfTracker::get_last_unit_count() { return s_unit_count; }
int PerfTracker::get_last_building_count() { return s_building_count; }

} // namespace rtso
