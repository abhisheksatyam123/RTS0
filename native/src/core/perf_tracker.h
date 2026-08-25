#ifndef RTSO_PERF_TRACKER_H
#define RTSO_PERF_TRACKER_H

#include <cstdint>
#include <godot_cpp/variant/string.hpp>

namespace rtso {

class PerfTracker {
public:
	static void begin_frame();
	static void record_tick(uint64_t ms);
	static void record_draw(int unit_count, int building_count);
	static godot::String get_report();

	static uint64_t get_tick_ms();
	static int get_last_unit_count();
	static int get_last_building_count();
};

} // namespace rtso

#endif
