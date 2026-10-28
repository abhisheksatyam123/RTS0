#include "user_config.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/os.hpp>

#include <cstdlib>
#include <map>

using namespace godot;

namespace rtso {
namespace user_config {
namespace {

constexpr const char *kPath = "user://config/user.cfg";

// 0 A.D. binaries/data/config/default.cfg (0 A.D. 61a3b95, GPL-2.0-or-later)
// for the options RTS0 implements. "windowed" is true here because RTS0 has
// always opened in a window.
struct Default {
	const char *key;
	const char *value;
};
const Default kDefaults[] = {
	{"locale", ""}, // L10n.cpp: empty follows the system language
	{"playername.singleplayer", ""}, // empty: the system user name (singleplayerName)
	{"playername.multiplayer", ""}, // empty: the system user name (multiplayerName)
	{"persistmatchsettings", "true"}, // PersistentMatchSettings
	{"pauseonfocusloss", "true"},
	{"overlay.fps", "false"},
	{"overlay.realtime", "false"},
	{"gui.session.howtoshownames", "0"},
	{"gui.splashscreen.enable", "true"},
	{"gui.splashscreen.version", "0"},
	{"mod.enabledmods", "mod public"},
	{"windowed", "true"},
	{"fog", "true"},
	{"postproc", "true"},
	{"renderer.scale", "1.0"},
	{"renderer.upscale.technique", "fsr"},
	{"shadows", "true"},
	{"shadowquality", "0"},
	{"shadowpcf", "true"},
	{"shadowscutoffdistance", "300.0"},
	{"shadowscascadecount", "1"},
	{"silhouettes", "true"},
	{"shadowscovermap", "false"},
	{"watereffects", "true"},
	{"waterfancyeffects", "false"},
	{"waterrealdepth", "true"},
	{"waterrefraction", "true"},
	{"waterreflection", "true"},
	{"textures.quality", "2"},
	{"textures.maxanisotropy", "2"},
	{"sharpening", "disabled"},
	{"sharpness", "0.3"},
	{"variant_diversity", "full"},
	{"materialmgr.quality", "10.0"},
	{"view.drag.speed", "0.5"},
	{"view.scroll.speed.modifier", "1.05"},
	{"view.rotate.speed.modifier", "1.05"},
	{"view.zoom.speed.modifier", "1.05"},
	{"view.drag.inverted", "false"},
	{"view.rotate.x.min", "28.0"},
	{"view.rotate.x.max", "60.0"},
	{"view.rotate.x.default", "35.0"},
	{"view.zoom.min", "50.0"},
	{"view.zoom.max", "200.0"},
	{"view.zoom.default", "120.0"},
	{"view.zoom.speed", "256.0"},
	{"view.zoom.speed.wheel", "32.0"},
	{"particles", "true"},
	{"vsync", "false"},
	{"adaptivefps.menu", "60"},
	{"adaptivefps.session", "60"},
	{"gui.scale", "1.0"},
	{"window.mousegrabinfullscreen", "true"},
	{"window.mousegrabinwindowmode", "false"},
	{"antialiasing", "disabled"},
	{"sound.mastergain", "0.9"},
	{"sound.musicgain", "0.2"},
	{"sound.ambientgain", "0.6"},
	{"sound.actiongain", "0.7"},
	{"sound.uigain", "0.7"},
	{"sound.notify.nick", "true"},
	{"sound.notify.gamesetup.join", "false"},
	{"lobby.tls", "false"}, // 0.28 true; RTS0's lobby server (tools/lobby_server.py) speaks plain HTTP
	{"lobby.history", "0"},
	{"gui.gamesetup.aidifficulty", "3"},
	{"gui.gamesetup.aibehavior", "random"},
	{"gui.gamesetup.assignplayers", "everyone"},
	{"lobby.buddies", ","},
	// 0 A.D. default.cfg [lobby]: the account last signed in, and its password as
	// EncryptPassword gives it (LobbySession.encrypt_password), kept only when remembered.
	{"lobby.login", ""},
	// 0 A.D. [lobby] server is lobby.wildfiregames.com; RTS0's lobby is
	// tools/lobby_server.py, at "ip:port".
	{"lobby.server", "127.0.0.1:8088"},
	{"lobby.password", ""},
	{"lobby.rememberpassword", "true"},
	{"lobby.columns.gamerating", "false"},
	// 0 A.D. default.cfg [lobby.stun]; an empty server is the lobby's own host
	// (tools/lobby_server.py answers STUN on --stun-port).
	{"lobby.stun.enabled", "true"},
	{"lobby.stun.server", ""},
	{"lobby.stun.port", "3478"},
	// 0 A.D. default.cfg [network] and overlay.netwarnings (the host reads the
	// observer settings at Host, a joiner autocatchup when it joins).
	{"network.lateobservers", "everyone"},
	{"network.observerlimit", "8"},
	{"network.observermaxlag", "-1"},
	{"network.autocatchup", "true"},
	{"network.duplicateplayernames", "false"},
	// 0.28 maps the host's port with UPnP when built with miniupnpc
	// (NetServer SetupUPnP); RTS0 makes that build switch a setting.
	{"network.upnp", "true"},
	{"overlay.netwarnings", "true"},
	{"gui.session.dragdelta", "4"},
	{"gui.session.batchtrainingsize", "5"},
	{"gui.session.formationwalkonly", "true"},
	{"gui.session.defaultformation", "special/formations/box"},
	{"gui.session.selectformationasone", "true"},
	{"gui.session.rankabovestatusbar", "true"},
	{"gui.session.experiencestatusbar", "true"},
	{"gui.session.timeelapsedcounter", "false"},
	{"gui.session.ceasefirecounter", "false"},
	{"gui.session.minimap.icons.enabled", "true"},
	{"gui.session.minimap.icons.opacity", "1.0"},
	{"gui.session.minimap.icons.sizescale", "1.0"},
	{"gui.session.scrollbatchratio", "1"},
	{"gui.session.snaptoedges", "disabled"},
	{"gui.session.disjointcontrolgroups", "true"},
	{"showdetailedtooltips", "false"},
	{"gui.gamesetup.enabletips", "true"},
	{"gui.gamesetup.settingsslide", "true"},
	{"gui.session.flarelifetime", "6"},
	{"gui.session.camerajump.threshold", "40"},
	{"gui.session.woundedunithotkeythreshold", "33"},
	{"gui.session.attackrange", "true"},
	{"gui.session.aurasrange", "true"},
	{"gui.session.healrange", "true"},
	{"gui.session.notifications.attack", "true"},
	{"gui.session.notifications.tribute", "true"},
	{"gui.session.notifications.barter", "true"},
	{"gui.session.notifications.phase", "completed"},
	{"gui.session.notifications.flare", "always"},
	{"gui.session.respoptooltipsort", "0"},
	{"gui.session.diplomacycolors.self", "21 55 149"},
	{"gui.session.diplomacycolors.ally", "86 180 31"},
	{"gui.session.diplomacycolors.neutral", "231 200 5"},
	{"gui.session.diplomacycolors.enemy", "150 20 20"},
	{"chat.timestamp", "true"},
	{"chat.session.extended", "true"}, // ChatWindow "History"
	{"gui.loadingscreen.progressdescription", "false"},
	// gamesetup_mp.js: the last address and port joined directly, and the last port hosted on.
	{"multiplayerserver", "127.0.0.1"},
	{"multiplayerjoining.port", "20595"},
	{"multiplayerhosting.port", "20595"},
};
constexpr int kDefaultCount = (int)(sizeof(kDefaults) / sizeof(kDefaults[0]));

// Android and iOS (or -mobiledefaults): phones start on the cheapest look and a 30 fps
// cap to save battery. Under GL Compatibility the 3D resolution and shadows cost most
// (a match on llvmpipe at 1280x720: scale 0.75 -20%, shadows -10% and a third of the
// draws, post-processing -6%); FSR is Forward+ only. Desktop keeps 0 A.D.'s defaults.
const Default kMobileDefaults[] = {
	{"shadows", "false"},
	{"postproc", "false"},
	{"particles", "false"},
	{"renderer.scale", "0.75"},
	{"renderer.upscale.technique", "bilinear"},
	{"waterreflection", "false"},
	{"waterrefraction", "false"},
	{"textures.maxanisotropy", "1"},
	{"adaptivefps.menu", "30"},
	{"adaptivefps.session", "30"},
};

struct State {
	bool loaded = false;
	std::map<std::string, std::string> values; // user values for known keys
	uint32_t generation = 1;
	bool changes = false; // ConfigDB_HasChanges("user"): set since the last save or reload
	// CFG_COMMAND: the command line's values (Config.cpp ProcessCommandLineArgs), above the
	// user's and never saved.
	bool command_parsed = false;
	std::map<std::string, std::string> command;
	bool audio_disabled = false; // g_DisableAudio: -nosound or -quickstart
	bool mobile_defaults = false; // -mobiledefaults: the phone defaults on any platform
};

State &state() {
	static State s;
	return s;
}

void parse_command_args(State &s, const PackedStringArray &args) {
	std::string mods; // GameSetup.cpp InitVfs: -mod=NAME (repeatable) replaces mod.enabledmods
	for (int i = 0; i < args.size(); ++i) {
		std::string a = args[i].utf8().get_data();
		if (a.rfind("--", 0) == 0) a = a.substr(2);
		else if (a.rfind("-", 0) == 0) a = a.substr(1);
		else continue;
		const size_t eq = a.find('=');
		const std::string name = a.substr(0, eq);
		const std::string value = eq == std::string::npos ? std::string() : a.substr(eq + 1);
		if (name == "conf") {
			const size_t colon = value.find(':');
			if (colon != std::string::npos) s.command[value.substr(0, colon)] = value.substr(colon + 1);
		} else if (name == "shadows" || name == "vsync") {
			s.command[name] = "true";
		} else if ((name == "xres" || name == "yres") && !value.empty()) {
			s.command[name] = value;
		} else if (name == "mobiledefaults") {
			s.mobile_defaults = true;
		} else if (name == "nosound" || name == "quickstart") {
			s.audio_disabled = true;
		} else if (name == "mod" && !value.empty()) {
			mods += " " + value;
		}
	}
	// Mod.cpp EnableMods: "mod" always first.
	if (!mods.empty()) s.command["mod.enabledmods"] = "mod" + mods;
}

std::string trim(const std::string &s) {
	size_t a = s.find_first_not_of(" \t\r");
	if (a == std::string::npos) return "";
	size_t b = s.find_last_not_of(" \t\r");
	return s.substr(a, b - a + 1);
}

// "key = value ; comment" lines under [section] headers, as CConfigDB reads them.
template <typename Fn>
void each_line(const std::string &text, Fn fn) {
	std::string section;
	size_t pos = 0;
	while (pos <= text.size()) {
		size_t nl = text.find('\n', pos);
		const std::string raw = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
		pos = nl == std::string::npos ? text.size() + 1 : nl + 1;
		std::string line = raw;
		bool quoted = false;
		for (size_t i = 0; i < line.size(); ++i) {
			if (line[i] == '"') quoted = !quoted;
			if (!quoted && (line[i] == ';' || line[i] == '#')) { line.resize(i); break; }
		}
		line = trim(line);
		if (line.empty()) continue;
		if (line.front() == '[') {
			size_t close = line.find(']');
			section = trim(line.substr(1, close == std::string::npos ? std::string::npos : close - 1));
			continue;
		}
		size_t eq = line.find('=');
		if (eq == std::string::npos) continue;
		std::string key = trim(line.substr(0, eq));
		std::string value = trim(line.substr(eq + 1));
		if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);
		fn(section.empty() ? key : section + "." + key, value, raw);
	}
}

std::string read_file() {
	if (!FileAccess::file_exists(kPath)) return "";
	Ref<FileAccess> f = FileAccess::open(kPath, FileAccess::READ);
	return f.is_null() ? std::string() : std::string(f->get_as_text().utf8().get_data());
}

// -conf=KEY:VALUE (any number), -shadows, -xres=N, -yres=N, -vsync, -nosound, -quickstart;
// one dash or two, before or after Godot's "--".
void parse_command(State &s) {
	if (s.command_parsed) return;
	s.command_parsed = true;
	PackedStringArray args = OS::get_singleton()->get_cmdline_args();
	args.append_array(OS::get_singleton()->get_cmdline_user_args());
	parse_command_args(s, args);
}

void ensure_loaded() {
	State &s = state();
	parse_command(s);
	if (s.loaded) return;
	s.loaded = true;
	s.values.clear();
	each_line(read_file(), [&](const std::string &key, const std::string &value, const std::string &) {
		for (int i = 0; i < kDefaultCount; ++i)
			if (key == kDefaults[i].key) { s.values[key] = value; break; }
	});
}

} // namespace

bool mobile_defaults() {
	parse_command(state());
	return state().mobile_defaults || OS::get_singleton()->has_feature("mobile");
}

const char *default_value(const std::string &key) {
	if (mobile_defaults())
		for (const Default &d : kMobileDefaults)
			if (key == d.key) return d.value;
	for (int i = 0; i < kDefaultCount; ++i)
		if (key == kDefaults[i].key) return kDefaults[i].value;
	return "";
}

std::string get(const std::string &key) {
	ensure_loaded();
	if (auto c = state().command.find(key); c != state().command.end()) return c->second;
	auto it = state().values.find(key);
	return it != state().values.end() ? it->second : std::string(default_value(key));
}

bool get_bool(const std::string &key) { return get(key) == "true"; }

double get_number(const std::string &key) { return std::atof(get(key).c_str()); }

void set(const std::string &key, const std::string &value) {
	ensure_loaded();
	State &s = state();
	auto it = s.values.find(key);
	if (value != (it != s.values.end() ? it->second : std::string(default_value(key)))) s.changes = true;
	if (value == default_value(key)) s.values.erase(key);
	else s.values[key] = value;
	++s.generation;
}

void reset_all() {
	ensure_loaded();
	if (!state().values.empty()) state().changes = true;
	state().values.clear();
	++state().generation;
}

void reload() {
	state().loaded = false;
	ensure_loaded();
	state().changes = false;
	++state().generation;
}

uint32_t generation() { return state().generation; }

bool has_changes() { return state().changes; }

bool audio_disabled() {
	ensure_loaded();
	return state().audio_disabled;
}

void set_command_args(const PackedStringArray &args) {
	State &s = state();
	s.command_parsed = true;
	s.command.clear();
	s.audio_disabled = false;
	s.mobile_defaults = false;
	parse_command_args(s, args);
	++s.generation;
}

std::string settings_cfg() {
	ensure_loaded();
	std::string out;
	for (int i = 0; i < kDefaultCount; ++i) {
		auto it = state().values.find(kDefaults[i].key);
		if (it == state().values.end()) continue;
		out += std::string(kDefaults[i].key) + " = \"" + it->second + "\"\n";
	}
	return out;
}

bool save() {
	// Keep the hotkey editor's lines (written by hotkeys::save_user_cfg).
	std::string hotkeys;
	each_line(read_file(), [&](const std::string &key, const std::string &, const std::string &raw) {
		if (key.rfind("hotkey.", 0) == 0) hotkeys += trim(raw) + "\n";
	});
	DirAccess::make_dir_recursive_absolute("user://config");
	Ref<FileAccess> f = FileAccess::open(kPath, FileAccess::WRITE);
	if (f.is_null()) return false;
	f->store_string(String::utf8((settings_cfg() + hotkeys).c_str()));
	state().changes = false;
	return true;
}

const char *const *known_keys(int &count) {
	static const char *keys[kDefaultCount];
	for (int i = 0; i < kDefaultCount; ++i) keys[i] = kDefaults[i].key;
	count = kDefaultCount;
	return keys;
}

} // namespace user_config
} // namespace rtso

void RtsoUserConfig::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_value", "key"), &RtsoUserConfig::get_value);
	ClassDB::bind_method(D_METHOD("get_bool", "key"), &RtsoUserConfig::get_bool);
	ClassDB::bind_method(D_METHOD("get_number", "key"), &RtsoUserConfig::get_number);
	ClassDB::bind_method(D_METHOD("get_default", "key"), &RtsoUserConfig::get_default);
	ClassDB::bind_method(D_METHOD("set_value", "key", "value"), &RtsoUserConfig::set_value);
	ClassDB::bind_method(D_METHOD("reset_all"), &RtsoUserConfig::reset_all);
	ClassDB::bind_method(D_METHOD("reload"), &RtsoUserConfig::reload);
	ClassDB::bind_method(D_METHOD("save"), &RtsoUserConfig::save);
	ClassDB::bind_method(D_METHOD("get_generation"), &RtsoUserConfig::get_generation);
	ClassDB::bind_method(D_METHOD("has_changes"), &RtsoUserConfig::has_changes);
	ClassDB::bind_method(D_METHOD("get_keys"), &RtsoUserConfig::get_keys);
	ClassDB::bind_method(D_METHOD("is_audio_disabled"), &RtsoUserConfig::is_audio_disabled);
	ClassDB::bind_method(D_METHOD("set_command_args", "args"), &RtsoUserConfig::set_command_args);
}

static std::string to_std(const String &s) { return std::string(s.utf8().get_data()); }

String RtsoUserConfig::get_value(const String &key) const { return String::utf8(rtso::user_config::get(to_std(key)).c_str()); }
bool RtsoUserConfig::get_bool(const String &key) const { return rtso::user_config::get_bool(to_std(key)); }
double RtsoUserConfig::get_number(const String &key) const { return rtso::user_config::get_number(to_std(key)); }
String RtsoUserConfig::get_default(const String &key) const { return String::utf8(rtso::user_config::default_value(to_std(key))); }
void RtsoUserConfig::set_value(const String &key, const String &value) { rtso::user_config::set(to_std(key), to_std(value)); }
void RtsoUserConfig::reset_all() { rtso::user_config::reset_all(); }
void RtsoUserConfig::reload() { rtso::user_config::reload(); }
bool RtsoUserConfig::save() { return rtso::user_config::save(); }
int RtsoUserConfig::get_generation() const { return (int)rtso::user_config::generation(); }
bool RtsoUserConfig::has_changes() const { return rtso::user_config::has_changes(); }
bool RtsoUserConfig::is_audio_disabled() const { return rtso::user_config::audio_disabled(); }
void RtsoUserConfig::set_command_args(const PackedStringArray &args) { rtso::user_config::set_command_args(args); }

PackedStringArray RtsoUserConfig::get_keys() const {
	PackedStringArray out;
	int n = 0;
	const char *const *keys = rtso::user_config::known_keys(n);
	for (int i = 0; i < n; ++i) out.push_back(keys[i]);
	return out;
}
