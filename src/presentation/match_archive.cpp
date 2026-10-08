// Replays, saved games and the match summary's statistics for RtsoMain.
//
// Replays follow 0 A.D. source/ps/Replay.cpp: every match is recorded from its
// setup, and a replay is that setup plus every turn's commands. 0 A.D. writes
// replays/<id>/commands.txt (a "start" line holding the game attributes, then
// the turns) and replays/<id>/metadata.json (the summary data). RTS0 keeps the
// same folder layout: commands.rts0rec is TurnManager::save_replay's turn
// stream and metadata.json holds the setup, the players and the statistics.
// Single-player orders run through the TurnManager too (0 A.D. CLocalTurnManager),
// so they land in the replay.
//
// Saved games follow gui/savedgames: user://saves/<name>.rts0sav with a
// <name>.json beside it (date, description, map, setup), so the save and load
// dialogs can list them and a save can be loaded from the main menu (the map
// is rebuilt from the setup first, as Simulation::load_state expects).

#include "main_node.h"
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace {

// Game Setup keys a match is rebuilt from (scenes/menu/match_settings.gd).
const char *const kSetupKeys[] = {
	"map_id", "victory", "waves_enabled", "last_man_standing", "pop_cap", "ceasefire_min",
	"reveal_map", "explore_map", "wonder_min", "relic_min", "relic_count", "map_size",
	"ai_difficulty", "ai_behavior", "start_res", "slot_types", "slot_civs", "slot_teams",
	"player_civ", "ai_civ",
};

constexpr uint32_t kReplayFormat = 1;
constexpr uint32_t kReplayWriteEveryTurns = 300; // a minute of game time: a crash loses at most that

String timestamp_id() {
	Dictionary dt = Time::get_singleton()->get_datetime_dict_from_system();
	return String("{0}-{1}-{2}_{3}{4}{5}").format(Array::make(
		String::num_int64((int)dt["year"]).pad_zeros(4), String::num_int64((int)dt["month"]).pad_zeros(2),
		String::num_int64((int)dt["day"]).pad_zeros(2), String::num_int64((int)dt["hour"]).pad_zeros(2),
		String::num_int64((int)dt["minute"]).pad_zeros(2), String::num_int64((int)dt["second"]).pad_zeros(2)));
}

bool write_text(const String &path, const String &text) {
	Ref<FileAccess> f = FileAccess::open(path, FileAccess::WRITE);
	if (!f.is_valid()) return false;
	f->store_string(text);
	f->close();
	return true;
}

Dictionary read_json(const String &path) {
	if (!FileAccess::file_exists(path)) return Dictionary();
	Ref<FileAccess> f = FileAccess::open(path, FileAccess::READ);
	if (!f.is_valid()) return Dictionary();
	Variant v = JSON::parse_string(f->get_as_text());
	f->close();
	return v.get_type() == Variant::DICTIONARY ? (Dictionary)v : Dictionary();
}

Array u32_array(const uint32_t *v, int n) {
	Array a;
	for (int i = 0; i < n; ++i) a.push_back((int64_t)v[i]);
	return a;
}

// One player's counters, keyed like StatisticsTracker.GetStatistics().
Dictionary stats_dict(const rtso::PlayerStatistics &st) {
	Dictionary d;
	auto by_class = [](const uint32_t *v, bool units) {
		Dictionary c;
		const int n = units ? rtso::STAT_UNIT_SLOTS : rtso::STAT_BLD_SLOTS;
		for (int i = 0; i < n; ++i)
			c[units ? rtso::stat_unit_class_name((uint8_t)i) : rtso::stat_building_class_name((uint8_t)i)] = (int64_t)v[i];
		return c;
	};
	d["unitsTrained"] = by_class(st.units_trained, true);
	d["unitsLost"] = by_class(st.units_lost, true);
	d["enemyUnitsKilled"] = by_class(st.enemy_units_killed, true);
	d["unitsLostValue"] = (int64_t)st.units_lost_value;
	d["enemyUnitsKilledValue"] = (int64_t)st.enemy_units_killed_value;
	d["buildingsConstructed"] = by_class(st.buildings_constructed, false);
	d["buildingsLost"] = by_class(st.buildings_lost, false);
	d["enemyBuildingsDestroyed"] = by_class(st.enemy_buildings_destroyed, false);
	d["buildingsCaptured"] = by_class(st.buildings_captured, false);
	d["buildingsLostValue"] = (int64_t)st.buildings_lost_value;
	d["enemyBuildingsDestroyedValue"] = (int64_t)st.enemy_buildings_destroyed_value;
	d["buildingsCapturedValue"] = (int64_t)st.buildings_captured_value;
	// 0 A.D. resource codes; RTS0's gold is 0 A.D.'s metal.
	auto res = [](auto const *v) {
		Dictionary r;
		r["food"] = (int64_t)v[rtso::RES_FOOD];
		r["wood"] = (int64_t)v[rtso::RES_WOOD];
		r["stone"] = (int64_t)v[rtso::RES_STONE];
		r["metal"] = (int64_t)v[rtso::RES_GOLD];
		return r;
	};
	Dictionary gathered = res(st.resources_gathered);
	gathered["vegetarianFood"] = (int64_t)st.vegetarian_food_gathered;
	d["resourcesGathered"] = gathered;
	d["resourcesUsed"] = res(st.resources_used);
	d["resourcesSold"] = res(st.resources_sold);
	d["resourcesBought"] = res(st.resources_bought);
	d["tributesSent"] = (int64_t)st.tributes_sent;
	d["tributesReceived"] = (int64_t)st.tributes_received;
	d["tradeIncome"] = (int64_t)st.trade_income;
	d["treasuresCollected"] = (int64_t)st.treasures_collected;
	d["lootCollected"] = (int64_t)st.loot_collected;
	d["peakPercentMapControlled"] = st.peak_percent_map_controlled;
	d["teamPeakPercentMapControlled"] = st.team_peak_percent_map_controlled;
	return d;
}

// gui/summary/counters.js calculateEconomyScore / MilitaryScore / ExplorationScore.
int economy_score(const rtso::PlayerStatistics &st) {
	int64_t total = st.trade_income;
	for (uint32_t g : st.resources_gathered) total += g;
	return (int)std::llround((double)total / 10.0);
}
int military_score(const rtso::PlayerStatistics &st) {
	return (int)std::llround((double)(st.enemy_units_killed_value + st.enemy_buildings_destroyed_value +
	                                  st.buildings_captured_value) / 10.0);
}
int exploration_score(float percent_explored) { return (int)std::lround(percent_explored * 10.0f); }

} // namespace

// The Game Setup a match was started from, with the seed it really used.
Dictionary RtsoMain::match_settings_snapshot(uint32_t seed) const {
	Dictionary d;
	if (Node *ms = get_node_or_null(NodePath("/root/MatchSettings"))) {
		for (const char *k : kSetupKeys) d[k] = ms->get(k);
	}
	d["seed"] = (int64_t)seed;
	return d;
}

void RtsoMain::apply_settings_snapshot(const Dictionary &settings) {
	Node *ms = get_node_or_null(NodePath("/root/MatchSettings"));
	if (!ms) return;
	for (const char *k : kSetupKeys)
		if (settings.has(k)) ms->set(k, settings[k]);
	if (settings.has("seed")) ms->set("seed", settings["seed"]);
}

Array RtsoMain::player_list() const {
	Array players;
	if (!sim) return players;
	for (uint8_t p = 0; p < rtso::MAX_PLAYERS; ++p) {
		if (!sim->player_in_match(p) && !((sim->participant_mask() >> p) & 1)) continue;
		const rtso::PlayerState &ps = sim->player(p);
		Dictionary d;
		d["id"] = (int)p;
		d["name"] = p == local_player() ? String("You") : String("Player ") + String::num_int64(p + 1);
		d["civ"] = (int)ps.civ;
		d["civ_name"] = String(rtso::civ_info(ps.civ).name);
		d["team"] = (int)ps.team + 1;
		d["ai"] = sim->petra_runs_for(p);
		d["defeated"] = sim->is_defeated(p);
		d["winner"] = (bool)((sim->winner_mask() >> p) & 1);
		players.push_back(d);
	}
	return players;
}

// Everything the summary screen shows (0 A.D. gui/summary): every player's
// counters, scores and the 30 s rows for the charts. The flat keys at the top
// are the local player's, as the old one-player record had them.
Dictionary RtsoMain::statistics_summary() const {
	Dictionary out;
	if (!sim) return out;
	const uint8_t me = local_player();
	const rtso::PlayerStatistics mine = sim->statistics(me);
	out["units_trained"] = (int64_t)mine.units_trained[rtso::STAT_UNIT_TOTAL];
	out["units_lost"] = (int64_t)mine.units_lost[rtso::STAT_UNIT_TOTAL];
	out["enemies_killed"] = (int64_t)mine.enemy_units_killed[rtso::STAT_UNIT_TOTAL];
	out["buildings_constructed"] = (int64_t)mine.buildings_constructed[rtso::STAT_BLD_TOTAL];
	out["buildings_destroyed"] = (int64_t)mine.enemy_buildings_destroyed[rtso::STAT_BLD_TOTAL];
	out["total_food"] = (int64_t)mine.resources_gathered[rtso::RES_FOOD];
	out["total_wood"] = (int64_t)mine.resources_gathered[rtso::RES_WOOD];
	out["total_gold"] = (int64_t)mine.resources_gathered[rtso::RES_GOLD];
	out["total_stone"] = (int64_t)mine.resources_gathered[rtso::RES_STONE];
	out["local_player"] = (int)me;
	out["map_name"] = String(sim->current_map_name().c_str());
	out["map_id"] = String(sim->current_map_id().c_str());
	out["game_time"] = (double)sim->stats().ticks * rtso::Simulation::FIXED_STEP;
	out["victory"] = sim->is_victory_for(me);

	Array players = player_list();
	for (int i = 0; i < players.size(); ++i) {
		Dictionary pd = players[i];
		const uint8_t p = (uint8_t)(int)pd["id"];
		const rtso::PlayerStatistics st = sim->statistics(p);
		const float explored = sim->percent_map_explored(p);
		pd["stats"] = stats_dict(st);
		pd["percentMapExplored"] = explored;
		pd["teamPercentMapExplored"] = sim->team_percent_map_explored(p);
		pd["percentMapControlled"] = sim->percent_map_controlled(p);
		pd["teamPercentMapControlled"] = sim->team_percent_map_controlled(p);
		pd["economyScore"] = economy_score(st);
		pd["militaryScore"] = military_score(st);
		pd["explorationScore"] = exploration_score(explored);
		pd["totalScore"] = economy_score(st) + military_score(st) + exploration_score(explored);
		players[i] = pd;
	}
	out["players"] = players;

	// Chart rows: the sampled sequences plus the state right now.
	std::vector<rtso::StatisticsSample> rows = sim->statistics_samples();
	rows.push_back(sim->statistics_snapshot());
	Array samples;
	for (const rtso::StatisticsSample &row : rows) {
		Dictionary r;
		r["time"] = row.time;
		Array per;
		for (int i = 0; i < players.size(); ++i) {
			const uint8_t p = (uint8_t)(int)((Dictionary)players[i])["id"];
			const rtso::PlayerStatistics &st = row.players[p];
			uint32_t gathered = 0;
			for (uint32_t g : st.resources_gathered) gathered += g;
			Dictionary c;
			c["economyScore"] = economy_score(st);
			c["militaryScore"] = military_score(st);
			c["explorationScore"] = exploration_score(row.percent_map_explored[p]);
			c["totalScore"] = economy_score(st) + military_score(st) + exploration_score(row.percent_map_explored[p]);
			c["resourcesGathered"] = (int64_t)gathered;
			c["unitsTrained"] = (int64_t)st.units_trained[rtso::STAT_UNIT_TOTAL];
			c["enemyUnitsKilled"] = (int64_t)st.enemy_units_killed[rtso::STAT_UNIT_TOTAL];
			c["unitsLost"] = (int64_t)st.units_lost[rtso::STAT_UNIT_TOTAL];
			c["buildingsConstructed"] = (int64_t)st.buildings_constructed[rtso::STAT_BLD_TOTAL];
			c["percentMapControlled"] = row.percent_map_controlled[p];
			c["percentMapExplored"] = row.percent_map_explored[p];
			c["tradeIncome"] = (int64_t)st.trade_income;
			per.push_back(c);
		}
		r["players"] = per;
		samples.push_back(r);
	}
	out["samples"] = samples;
	return out;
}

// --- Replays -------------------------------------------------------------------

void RtsoMain::begin_match_recording(const rtso::GameSetupParams *mp_setup) {
	if (!sim || turn_mgr_.is_replaying()) return;
	if (!mp_setup) turn_mgr_.reset(); // offline: turn 1 is the first turn of this match
	replay_finished_ = false;
	replay_written_turn_ = 0;
	Ref<DirAccess> dir = DirAccess::open("user://");
	if (dir.is_null()) { replay_recording_ = false; return; }
	String id = timestamp_id();
	for (int n = 2; dir->dir_exists("replays/" + id); ++n) id = timestamp_id() + "_" + String::num_int64(n);
	dir->make_dir_recursive("replays/" + id);
	replay_dir_ = "user://replays/" + id + "/";
	replay_meta_ = Dictionary();
	replay_meta_["format"] = (int64_t)kReplayFormat;
	replay_meta_["engine_version"] = "RTS0";
	replay_meta_["save_version"] = (int64_t)rtso::SAVE_VERSION;
	replay_meta_["timestamp"] = (int64_t)Time::get_singleton()->get_unix_time_from_system();
	replay_meta_["date"] = Time::get_singleton()->get_datetime_string_from_system(false, true);
	replay_meta_["map_id"] = String(sim->current_map_id().c_str());
	replay_meta_["map_name"] = String(sim->current_map_name().c_str());
	if (mp_setup) {
		// Multiplayer: the NMT_GAME_START attributes every peer built the match from.
		replay_meta_["kind"] = "mp";
		replay_meta_["game_start"] = String(rtso::format_game_start_full(*mp_setup).c_str());
		replay_meta_["seed"] = (int64_t)mp_setup->seed;
		replay_meta_["map_size"] = (int)mp_setup->map_size;
	} else {
		replay_meta_["kind"] = "sp";
		replay_meta_["settings"] = match_settings_snapshot(match_seed_);
		replay_meta_["seed"] = (int64_t)match_seed_;
		replay_meta_["map_size"] = random_map_size;
	}
	replay_meta_["local_player"] = (int)local_player();
	replay_recording_ = true;
}

void RtsoMain::write_replay() {
	if (!replay_recording_ || replay_dir_.is_empty() || !sim) return;
	const uint32_t turns = (uint32_t)turn_mgr_.turn_history().size();
	if (turns < 25) return; // under five seconds: nothing worth keeping (0 A.D. drops empty replays too)
	const String cmd_path = ProjectSettings::get_singleton()->globalize_path(replay_dir_ + "commands.rts0rec");
	if (!turn_mgr_.save_replay(cmd_path.utf8().get_data())) {
		UtilityFunctions::printerr("[RTS0] Replay write failed: ", cmd_path);
		return;
	}
	Dictionary meta = replay_meta_.duplicate();
	meta["turns"] = (int64_t)turns;
	meta["duration"] = (double)turns * rtso::TurnManager::TURN_DURATION;
	meta["players"] = player_list();
	meta["summary"] = statistics_summary();
	meta["finished"] = sim->is_game_over() || sim->is_victory();
	write_text(replay_dir_ + "metadata.json", JSON::stringify(meta, "\t"));
	replay_written_turn_ = turns;
}

void RtsoMain::stop_replay_recording() {
	// What was recorded still replays: it ends where the game left its commands.
	write_replay();
	replay_recording_ = false;
}

// A replay picked in the replay menu (MatchSettings.replay_dir): build its
// match from the recorded setup and feed the TurnManager its turns.
bool RtsoMain::start_pending_replay() {
	Node *ms = get_node_or_null(NodePath("/root/MatchSettings"));
	if (!ms || !sim) return false;
	const String dir = ms->get("replay_dir");
	if (dir.is_empty()) return false;
	ms->set("replay_dir", String());
	Dictionary meta = read_json(dir.path_join("metadata.json"));
	const String cmd_path = ProjectSettings::get_singleton()->globalize_path(dir.path_join("commands.rts0rec"));
	if (meta.is_empty() || !FileAccess::file_exists(dir.path_join("commands.rts0rec"))) {
		UtilityFunctions::printerr("[RTS0] Replay not found: ", dir);
		return false;
	}
	std::string err;
	if (!turn_mgr_.validate_replay(cmd_path.utf8().get_data(), err)) {
		UtilityFunctions::printerr("[RTS0] Replay unreadable: ", String(err.c_str()));
		return false;
	}
	if ((String)meta.get("kind", "sp") == "mp") {
		rtso::GameSetupParams setup;
		const String attrs = meta.get("game_start", "");
		if (!rtso::parse_game_start_full(attrs.utf8().get_data(), setup)) return false;
		turn_mgr_.reset();
		turn_mgr_.assign_slot_players(setup); // the same client -> slot seating the match had
		apply_game_setup(setup);
	} else {
		apply_settings_snapshot(meta.get("settings", Dictionary()));
		apply_match_settings();
		turn_mgr_.reset();
	}
	if (!turn_mgr_.load_replay(cmd_path.utf8().get_data())) {
		UtilityFunctions::printerr("[RTS0] Replay load failed: ", String(turn_mgr_.last_replay_error().c_str()));
		return false;
	}
	replay_recording_ = false;
	replay_finished_ = false;
	replay_meta_ = meta;
	UtilityFunctions::print("[RTS0] Playing replay ", dir, " (", (int64_t)turn_mgr_.turn_history().size(), " turns)");
	return true;
}

Dictionary RtsoMain::get_replay_status() const {
	Dictionary d;
	d["replaying"] = turn_mgr_.is_replaying();
	d["turn"] = (int64_t)turn_mgr_.current_turn();
	d["turns"] = (int64_t)(turn_mgr_.is_replaying() ? turn_mgr_.turn_history().size() : 0);
	d["finished"] = replay_finished_;
	d["out_of_sync"] = turn_mgr_.replay_out_of_sync();
	d["out_of_sync_turn"] = (int64_t)turn_mgr_.replay_out_of_sync_turn();
	d["recording"] = replay_recording_;
	d["replay_dir"] = replay_dir_;
	return d;
}

// Per-frame upkeep: write the replay now and then, show playback progress and
// stop at the replay's last turn.
void RtsoMain::tick_match_archive() {
	if (replay_recording_ && turn_mgr_.turn_history().size() >= replay_written_turn_ + kReplayWriteEveryTurns)
		write_replay();
	if (!turn_mgr_.is_replaying()) return;
	if (!replay_label_) {
		replay_label_ = memnew(Label);
		replay_label_->set_position(Vector2(16.0f, 64.0f));
		replay_label_->add_theme_color_override("font_color", Color(1.0f, 0.85f, 0.4f));
		replay_label_->add_theme_font_size_override("font_size", 18);
		CanvasLayer *layer = memnew(CanvasLayer);
		layer->set_layer(5);
		add_child(layer);
		layer->add_child(replay_label_);
	}
	const int64_t total = (int64_t)turn_mgr_.turn_history().size();
	const int64_t turn = std::min<int64_t>((int64_t)turn_mgr_.current_turn(), total);
	auto clock = [](int64_t turns) {
		const int64_t s = (int64_t)((double)turns * rtso::TurnManager::TURN_DURATION);
		return String::num_int64(s / 60).pad_zeros(2) + ":" + String::num_int64(s % 60).pad_zeros(2);
	};
	String text = "REPLAY  " + clock(turn) + " / " + clock(total);
	if (turn_mgr_.replay_out_of_sync())
		text += "  (out of sync at turn " + String::num_int64(turn_mgr_.replay_out_of_sync_turn()) + ")";
	if (!replay_finished_ && turn_mgr_.replay_complete()) {
		replay_finished_ = true;
		if (Node *summary = get_node_or_null(NodePath("SummaryScreen")))
			summary->call("show_summary", sim->is_victory_for(local_player()), statistics_summary(),
			              (float)((double)total * rtso::TurnManager::TURN_DURATION));
	}
	if (replay_finished_) text += "  finished";
	replay_label_->set_text(text);
}

// --- Saved games ---------------------------------------------------------------

bool RtsoMain::write_save_metadata(const String &path, const String &description) const {
	if (!sim) return false;
	Dictionary meta;
	meta["save_version"] = (int64_t)rtso::SAVE_VERSION;
	meta["timestamp"] = (int64_t)Time::get_singleton()->get_unix_time_from_system();
	meta["date"] = Time::get_singleton()->get_datetime_string_from_system(false, true);
	meta["description"] = description;
	meta["map_id"] = String(sim->current_map_id().c_str());
	meta["map_name"] = String(sim->current_map_name().c_str());
	meta["game_time"] = (double)sim->stats().ticks * rtso::Simulation::FIXED_STEP;
	meta["elapsed"] = match_elapsed_time;
	meta["kind"] = net_lockstep() ? "mp" : "sp";
	meta["settings"] = match_settings_snapshot(match_seed_);
	meta["players"] = player_list();
	meta["local_player"] = (int)local_player();
	return write_text(path.get_basename() + ".json", JSON::stringify(meta, "\t"));
}

bool RtsoMain::save_game_as(String name, String description) {
	// File names keep to what every platform accepts.
	String safe;
	for (int i = 0; i < name.length(); ++i) {
		const char32_t c = name[i];
		const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
		safe += ok ? String::chr(c) : String("_");
	}
	if (safe.is_empty()) safe = "savegame_" + timestamp_id();
	return save_game("user://saves/" + safe + ".rts0sav", description);
}

// A save picked in the load dialog from the main menu (MatchSettings.load_save_path):
// rebuild its map from the saved setup, then load the state on top.
bool RtsoMain::load_pending_save() {
	Node *ms = get_node_or_null(NodePath("/root/MatchSettings"));
	if (!ms || !sim) return false;
	const String path = ms->get("load_save_path");
	if (path.is_empty()) return false;
	ms->set("load_save_path", String());
	Dictionary meta = read_json(path.get_basename() + ".json");
	if (meta.is_empty() || !FileAccess::file_exists(path)) {
		UtilityFunctions::printerr("[RTS0] Saved game not found: ", path);
		return false;
	}
	apply_settings_snapshot(meta.get("settings", Dictionary()));
	apply_match_settings();
	turn_mgr_.reset();
	if (!load_game(path)) return false;
	match_elapsed_time = (double)meta.get("elapsed", 0.0);
	return true;
}
