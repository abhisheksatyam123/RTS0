#ifndef RTSO_USER_CONFIG_H
#define RTSO_USER_CONFIG_H

// The user's settings, a port of 0 A.D.'s ConfigDB as the options page
// (gui/options/options.js) uses it: every key has 0 A.D.'s default from
// binaries/data/config/default.cfg, the user's own values live in
// user://config/user.cfg next to the hotkey bindings (0 A.D. keeps both in
// config/user.cfg), and only values that differ from the defaults are written.
// Script access is RtsoUserConfig; C++ (the session) reads rtso::user_config.

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include <cstdint>
#include <string>

namespace rtso {
namespace user_config {

// 0 A.D. default.cfg value of a known key ("" for an unknown one).
const char *default_value(const std::string &key);
// The user's value, else the default.
std::string get(const std::string &key);
bool get_bool(const std::string &key);
double get_number(const std::string &key);
// Changes a value in memory (ConfigDB_CreateValue); save() writes it.
void set(const std::string &key, const std::string &value);
// Drops every user value (the options page "Reset").
void reset_all();
// Re-reads user.cfg (ConfigDB_Reload), discarding unsaved changes.
void reload();
// Writes user.cfg: the hotkey lines already there, then every setting that
// differs from its default. Returns false when the file can't be written.
bool save();
// Bumped by every set/reset/reload so readers can cache values.
uint32_t generation();
// -nosound or -quickstart on the command line (Config.cpp g_DisableAudio).
bool audio_disabled();
// Replaces the command line's values (tests); the real ones are read on first use.
void set_command_args(const godot::PackedStringArray &args);
// The settings part of user.cfg, as save() writes it (hotkey saves keep it).
std::string settings_cfg();
// Every key the options page knows, in default.cfg order.
const char *const *known_keys(int &count);

} // namespace user_config
} // namespace rtso

class RtsoUserConfig : public godot::RefCounted {
	GDCLASS(RtsoUserConfig, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	godot::String get_value(const godot::String &key) const;
	bool get_bool(const godot::String &key) const;
	double get_number(const godot::String &key) const;
	godot::String get_default(const godot::String &key) const;
	void set_value(const godot::String &key, const godot::String &value);
	void reset_all();
	void reload();
	bool save();
	int get_generation() const;
	godot::PackedStringArray get_keys() const;
	bool is_audio_disabled() const;
	void set_command_args(const godot::PackedStringArray &args);
};

#endif
