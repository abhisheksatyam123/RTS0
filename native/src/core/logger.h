#ifndef RTSO_LOGGER_H
#define RTSO_LOGGER_H

#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace rtso {

enum class LogLevel {
	Verbose = 0,
	Debug = 1,
	Info = 2,
	Warn = 3,
	Error = 4
};

class Logger {
public:
	static void info(const char *category, const char *msg);
	static void debug(const char *category, const char *msg);
	static void warn(const char *category, const char *msg);
	static void error(const char *category, const char *msg);
};

} // namespace rtso

#endif
