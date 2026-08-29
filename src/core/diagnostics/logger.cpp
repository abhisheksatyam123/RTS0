#include "logger.h"

namespace rtso {

void Logger::info(const char *category, const char *msg) {
	godot::UtilityFunctions::print("[INFO] [", category, "] ", msg);
}

void Logger::debug(const char *category, const char *msg) {
	godot::UtilityFunctions::print("[DEBUG] [", category, "] ", msg);
}

void Logger::warn(const char *category, const char *msg) {
	godot::UtilityFunctions::push_warning("[WARN] [", category, "] ", msg);
}

void Logger::error(const char *category, const char *msg) {
	godot::UtilityFunctions::printerr("[ERROR] [", category, "] ", msg);
}

} // namespace rtso
