#include "user_config.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>

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
	{"view.drag.speed", "0.5"},
	{"view.scroll.speed.modifier", "1.05"},
	{"view.rotate.speed.modifier", "1.05"},
	{"view.zoom.speed.modifier", "1.05"},
	{"view.drag.inverted", "false"},
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
	{"lobby.history", "0"},
	{"gui.gamesetup.aidifficulty", "3"},
	{"gui.gamesetup.aibehavior", "random"},
	{"gui.gamesetup.assignplayers", "everyone"},
	{"lobby.buddies", ","},
	// 0 A.D. default.cfg [lobby]: the account last signed in, and its password as
	// EncryptPassword gives it (LobbySession.encrypt_password), kept only when remembered.
	{"lobby.login", ""},
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

struct State {
	bool loaded = false;
	std::map<std::string, std::string> values; // user values for known keys
	uint32_t generation = 1;
};

State &state() {
	static State s;
	return s;
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

void ensure_loaded() {
	State &s = state();
	if (s.loaded) return;
	s.loaded = true;
	s.values.clear();
	each_line(read_file(), [&](const std::string &key, const std::string &value, const std::string &) {
		for (int i = 0; i < kDefaultCount; ++i)
			if (key == kDefaults[i].key) { s.values[key] = value; break; }
	});
}

} // namespace

const char *default_value(const std::string &key) {
	for (int i = 0; i < kDefaultCount; ++i)
		if (key == kDefaults[i].key) return kDefaults[i].value;
	return "";
}

std::string get(const std::string &key) {
	ensure_loaded();
	auto it = state().values.find(key);
	return it != state().values.end() ? it->second : std::string(default_value(key));
}

bool get_bool(const std::string &key) { return get(key) == "true"; }

double get_number(const std::string &key) { return std::atof(get(key).c_str()); }

void set(const std::string &key, const std::string &value) {
	ensure_loaded();
	State &s = state();
	if (value == default_value(key)) s.values.erase(key);
	else s.values[key] = value;
	++s.generation;
}

void reset_all() {
	ensure_loaded();
	state().values.clear();
	++state().generation;
}

void reload() {
	state().loaded = false;
	ensure_loaded();
	++state().generation;
}

uint32_t generation() { return state().generation; }

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
	ClassDB::bind_method(D_METHOD("get_keys"), &RtsoUserConfig::get_keys);
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

PackedStringArray RtsoUserConfig::get_keys() const {
	PackedStringArray out;
	int n = 0;
	const char *const *keys = rtso::user_config::known_keys(n);
	for (int i = 0; i < n; ++i) out.push_back(keys[i]);
	return out;
}
