// Replays, saved games and the match summary's statistics for RtsoMain.
//
// Replays follow 0 A.D. source/ps/Replay.cpp: every match is recorded from its
// setup, and a replay is that setup plus every turn's commands. 0 A.D. writes
// replays/<id>/commands.txt (a "start" line holding the game attributes, then
// the turns) and replays/<id>/metadata.json (the summary data). RTS0 keeps the
// same folder layout: commands.rts0rec is TurnManager::save_replay's turn
// stream and metadata.json holds the setup, the players and the statistics.
// Single-player orders run through the TurnManager too (0 A.D. CLocalTurnManager),
// so they land in the replay. A game continued from a save is recorded too: its
// replay folder keeps a copy of the save (start.rts0sav) to start from.
// Watching one, the viewer picks whose sight to follow or watches as an
// observer with the map revealed (0 A.D. replay "Player view").
//
// Saved games follow gui/savedgames: user://saves/<name>.rts0sav with a
// <name>.json beside it (date, description, map, setup), so the save and load
// dialogs can list them and a save can be loaded from the main menu (the map
// is rebuilt from the setup first, as Simulation::load_state expects).

#include "main_node.h"
#include <godot_cpp/classes/canvas_layer.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/viewport_texture.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace {

// Game Setup keys a match is rebuilt from (scenes/menu/match_settings.gd).
const char *const kSetupKeys[] = {
	"map_id", "victory", "waves_enabled", "last_man_standing", "pop_cap", "team_pop", "ceasefire_min",
	"reveal_map", "explore_map", "ally_view", "wonder_min", "relic_min", "relic_count", "map_size",
	"ai_difficulty", "ai_behavior", "start_res", "slot_types", "slot_civs", "slot_teams",
	"player_civ", "ai_civ", "campaign_run", "campaign_level", "lock_teams",
	// Everything else apply_match_settings reads: the options that shape the rebuilt map
	// before a save's state is loaded onto it, and the rest, so a replay never picks up
	// a setting changed in the Game Setup since it was recorded.
	"biome", "landscape", "team_placement", "water_level", "sea_level_rise_min", "trigger_difficulty", "map_daytime",
	"nomad", "regicide_garrison", "cheats", "disable_spies", "disable_treasures",
	"slot_diffs", "slot_behs", "slot_colors", "slot_names", "ai_seed", "local_slot",
};

constexpr uint32_t kReplayFormat = 2; // 2: may start from a save (start_save)
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
	Dictionary trained = by_class(st.units_trained, true);
	trained["Domestic"] = (int64_t)st.livestock_bred; // not in the total, as StatisticsTracker.js
	// The "Unit" class counter (the summary's Total column) also counts livestock: it
	// is a class in UnitClasses, unlike `total`. Lost and killed units never include it.
	trained["Unit"] = (int64_t)st.units_trained[rtso::STAT_UNIT_TOTAL] + (int64_t)st.livestock_bred;
	d["unitsTrained"] = trained;
	d["unitsLost"] = by_class(st.units_lost, true);
	d["enemyUnitsKilled"] = by_class(st.enemy_units_killed, true);
	d["unitsLostValue"] = (int64_t)st.units_lost_value;
	d["enemyUnitsKilledValue"] = (int64_t)st.enemy_units_killed_value;
	d["unitsCaptured"] = by_class(st.units_captured, true);
	d["unitsCapturedValue"] = (int64_t)st.units_captured_value;
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
	d["resourcesCount"] = res(st.resources_count);
	d["populationCount"] = (int64_t)st.population_count;
	d["successfulBribes"] = (int64_t)st.successful_bribes;
	d["failedBribes"] = (int64_t)st.failed_bribes;
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
	return (int)std::llround((double)(st.enemy_units_killed_value + st.units_captured_value +
	                                  st.enemy_buildings_destroyed_value + st.buildings_captured_value) / 10.0);
}
// RangeManager GetPercentMapExplored is a whole percent in 0 A.D.
int exploration_score(float percent_explored) { return (int)std::floor(percent_explored) * 10; }

} // namespace

// The Game Setup a match was started from, with the seed it really used.
Dictionary RtsoMain::match_settings_snapshot(uint32_t seed) const {
	Dictionary d;
	if (Node *ms = get_node_or_null(NodePath("/root/MatchSettings"))) {
		for (const char *k : kSetupKeys) d[k] = ms->get(k);
	}
	// PlayerPlacement.pickRandomItems: Random is recorded as the pattern the map drew.
	if (sim && sim->played_team_placement()) d["team_placement"] = (int)sim->played_team_placement();
	d["seed"] = (int64_t)seed;
	return d;
}

void RtsoMain::apply_settings_snapshot(const Dictionary &settings) {
	Node *ms = get_node_or_null(NodePath("/root/MatchSettings"));
	if (!ms) return;
	// A key an older replay or save lacks gets its default, not the last Game Setup's value.
	Ref<Script> script = ms->get_script();
	for (const char *k : kSetupKeys) {
		if (settings.has(k)) ms->set(k, settings[k]);
		else if (script.is_valid()) ms->set(k, script->get_property_default_value(k));
	}
	if (settings.has("seed")) ms->set("seed", settings["seed"]);
}

Array RtsoMain::player_list() const {
	Array players;
	if (!sim) return players;
	// 0 A.D. playerState.team: the Game Setup team (-1 = none, 0-3 = Team 1-4).
	// RTS0's sim team is its lowest member's id, so the setup slot is kept too.
	Node *ms = get_node_or_null(NodePath("/root/MatchSettings"));
	const Array slot_teams = ms ? (Array)ms->get("slot_teams") : Array();
	for (uint8_t p = 0; p < rtso::MAX_PLAYERS; ++p) {
		if (!sim->player_in_match(p) && !((sim->participant_mask() >> p) & 1)) continue;
		const rtso::PlayerState &ps = sim->player(p);
		Dictionary d;
		d["id"] = (int)p;
		d["name"] = player_display_name(p);
		d["civ"] = (int)ps.civ;
		d["civ_name"] = String(rtso::civ_info(ps.civ).name);
		d["team"] = (int)ps.team + 1;
		d["setup_team"] = p < slot_teams.size() && (int)slot_teams[p] > 0 ? (int)slot_teams[p] - 1 : -1;
		d["ai"] = sim->petra_runs_for(p);
		if (sim->petra_runs_for(p)) {
			d["ai_difficulty"] = (int)sim->petra_config_for(p).difficulty;
			d["ai_behavior"] = (int)sim->petra_config_for(p).behavior;
		}
		d["defeated"] = sim->is_defeated(p);
		d["winner"] = (bool)((sim->winner_mask() >> p) & 1);
		// 0 A.D. playerState.state and playerState.color (0..1 per channel).
		d["state"] = (bool)d["winner"] ? "won" : (sim->is_defeated(p) ? "defeated" : "active");
		const uint32_t rgb = sim->player_color(p);
		Dictionary color;
		color["r"] = ((rgb >> 16) & 0xFF) / 255.0;
		color["g"] = ((rgb >> 8) & 0xFF) / 255.0;
		color["b"] = (rgb & 0xFF) / 255.0;
		d["color"] = color;
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
	out["map_size"] = replay_meta_.get("map_size", random_map_size);
	out["lock_teams"] = sim->teams_locked(); // summary.js initTeamData: mapSettings.LockTeams

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

	// Chart rows (0 A.D. playerState.sequences, one row per 30 s plus the
	// state right now): every shown player's counters and map percentages.
	std::vector<rtso::StatisticsSample> rows = sim->statistics_samples();
	rows.push_back(sim->statistics_snapshot());
	Array samples;
	for (const rtso::StatisticsSample &row : rows) {
		Dictionary r;
		r["time"] = row.time;
		Array per;
		for (int i = 0; i < players.size(); ++i) {
			const uint8_t p = (uint8_t)(int)((Dictionary)players[i])["id"];
			Dictionary c;
			c["stats"] = stats_dict(row.players[p]);
			c["percentMapExplored"] = row.percent_map_explored[p];
			c["teamPercentMapExplored"] = row.team_percent_map_explored[p];
			c["percentMapControlled"] = row.percent_map_controlled[p];
			c["teamPercentMapControlled"] = row.team_percent_map_controlled[p];
			per.push_back(c);
		}
		r["players"] = per;
		samples.push_back(r);
	}
	out["samples"] = samples;
	return out;
}

// The summary page's "gui" init data (0 A.D. session.js leaveGame and
// MenuButtons Summary): where it was opened from and which players it shows.
// In game a player sees itself and its mutual allies, whose sight RTS0
// always shares; observers, defeated players and replays see everyone.
Dictionary RtsoMain::summary_gui(bool in_game) const {
	Dictionary g;
	if (!sim) return g;
	const bool replay = turn_mgr_.is_replaying();
	const uint8_t me = own_seat();
	g["isInGame"] = in_game;
	// MenuButtons keeps summarySelection for the match; the summary after it starts afresh.
	if (in_game) g["selectionOwner"] = "match " + String::num_uint64(get_instance_id());
	g["dialog"] = in_game;
	g["isReplay"] = replay;
	g["networked"] = net_lockstep();
	g["disconnected"] = net_disconnected_; // session.js g_Disconnected
	// session.js g_IsController (the host, or any offline game) and g_IsObserver
	// (no seat, or a seat whose game is over).
	g["isController"] = net_host_.getRole() == rtso::NetRole::None || net_host_.getRole() == rtso::NetRole::Host;
	g["victory"] = !replay && me < rtso::MAX_PLAYERS && sim->is_victory_for(me);
	g["isObserver"] = replay || me >= rtso::MAX_PLAYERS || sim->is_defeated(me) || sim->is_victory_for(me);
	g["assignedPlayer"] = replay || me >= rtso::MAX_PLAYERS ? -1 : (int)me;
	g["replayDirectory"] = replay ? watched_replay_dir_ : !replay_dir_.is_empty() && FileAccess::file_exists(replay_dir_ + String("metadata.json"))
		? replay_dir_ : String();
	// MenuButtons.js openGameSummary: during the game a player sees itself, and its
	// mutual allies once it shares their sight (Cartography); observers and players
	// whose game is over see everyone.
	if (in_game && !replay && me < rtso::MAX_PLAYERS && !sim->is_defeated(me) && !sim->is_victory_for(me)) {
		Array shown;
		for (uint8_t p = 0; p < rtso::MAX_PLAYERS; ++p)
			if (sim->shares_los(me, p)) shown.push_back((int)p);
		g["shownPlayers"] = shown;
	}
	return g;
}

// One of 0 A.D.'s session quit questions (scenes/session/quit_confirmation.gd).
void RtsoMain::quit_confirmation(const String &kind, bool won, bool ask_exit) {
	Ref<Script> q = ResourceLoader::get_singleton()->load("res://scenes/session/quit_confirmation.gd");
	if (q.is_null()) return;
	if (kind == "defeat") q->call("defeat", this, won, ask_exit);
	else q->call(StringName(kind), this);
}

// QuitConfirmationDefeat.onPlayersFinished: once the local player has won or
// been defeated, ask whether to quit. The host isn't invited to leave while
// other humans still play.
void RtsoMain::tick_quit_confirmation() {
	const uint8_t me = own_seat();
	if (finished_asked_ || !sim || turn_mgr_.is_replaying() || me >= rtso::MAX_PLAYERS) return;
	const bool won = sim->is_victory_for(me);
	if (!won && !sim->is_defeated(me)) return;
	finished_asked_ = true;
	write_replay(); // the player's game is over: keep its replay as it stands
	bool ask_exit = net_host_.getRole() != rtso::NetRole::Host;
	if (!ask_exit) {
		ask_exit = true;
		for (uint8_t p = 0; p < rtso::MAX_PLAYERS; ++p)
			if (p != me && sim->player(p).active && !sim->player(p).is_ai && !sim->is_defeated(p) && !sim->is_victory_for(p))
				ask_exit = false;
	}
	quit_confirmation("defeat", won, ask_exit);
}

// ResignConfirmation / ReturnQuestion "Yes" / "I resign": Engine.PostNetworkCommand({"type": "resign"}).
void RtsoMain::resign() {
	const uint8_t me = own_seat();
	if (!sim || turn_mgr_.is_replaying() || me >= rtso::MAX_PLAYERS || sim->is_defeated(me)) return;
	post_lockstep((uint8_t)rtso::NetCommandType::NET_CMD_RESIGN, nullptr, 0, 0.0f, 0.0f, 0, 0);
}

// The "summary" hotkey and the menu's Summary button: the page over the
// running game, or back to the game when it is already open.
void RtsoMain::toggle_summary() {
	Node *summary = get_node_or_null(NodePath("SummaryScreen"));
	if (!summary || !sim) return;
	if ((bool)summary->call("is_open")) {
		summary->call("continue_pressed");
		return;
	}
	summary->call("configure", summary_gui(true));
	summary->call("show_summary", sim->is_victory_for(own_seat()), statistics_summary(),
	              (float)((double)sim->stats().ticks * rtso::Simulation::FIXED_STEP));
}

// --- Replays -------------------------------------------------------------------

int RtsoMain::get_save_version() { return (int)rtso::Simulation::SAVE_VERSION; }
int RtsoMain::get_replay_format() { return (int)kReplayFormat; }

void RtsoMain::begin_match_recording(const rtso::GameSetupParams *mp_setup) {
	if (!sim || turn_mgr_.is_replaying()) return;
	if (!mp_setup) turn_mgr_.reset(); // offline: turn 1 is the first turn of this match
	replay_finished_ = false;
	replay_written_turn_ = 0;
	// 0 A.D. -autostart-disable-replay: no replay is written for this run.
	if (Node *ms = get_node_or_null(NodePath("/root/MatchSettings")); ms && !(bool)ms->get("record_replay")) {
		replay_recording_ = false;
		return;
	}
	Ref<DirAccess> dir = DirAccess::open("user://");
	if (dir.is_null()) { replay_recording_ = false; return; }
	String id = timestamp_id();
	for (int n = 2; dir->dir_exists("replays/" + id); ++n) id = timestamp_id() + "_" + String::num_int64(n);
	dir->make_dir_recursive("replays/" + id);
	replay_dir_ = "user://replays/" + id + "/";
	replay_meta_ = Dictionary();
	replay_meta_["format"] = (int64_t)kReplayFormat;
	replay_meta_["engine_version"] = "RTS0";
	replay_meta_["save_version"] = (int64_t)rtso::Simulation::SAVE_VERSION;
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

// A loaded game goes on as a new replay, which starts from a copy of the save.
void RtsoMain::begin_recording_from_save(const String &save_path) {
	begin_match_recording(nullptr);
	if (!replay_recording_) return;
	const String start = replay_dir_ + "start.rts0sav";
	if (DirAccess::copy_absolute(ProjectSettings::get_singleton()->globalize_path(save_path),
	                             ProjectSettings::get_singleton()->globalize_path(start)) != OK) {
		replay_recording_ = false; // without its starting state the replay could not be played
		return;
	}
	replay_meta_["start_save"] = "start.rts0sav";
	replay_meta_["start_time"] = (double)sim->stats().ticks * rtso::Simulation::FIXED_STEP;
}

void RtsoMain::write_replay(bool ended) {
	if (!replay_recording_ || replay_dir_.is_empty() || !sim) return;
	const uint32_t turns = (uint32_t)turn_mgr_.turn_history().size();
	if (turns < 15) return; // VisualReplay lists no replay shorter than 3 s (minimumReplayDuration)
	const String cmd_path = ProjectSettings::get_singleton()->globalize_path(replay_dir_ + "commands.rts0rec");
	if (!turn_mgr_.save_replay(cmd_path.utf8().get_data())) {
		UtilityFunctions::printerr("[RTS0] Replay write failed: ", cmd_path);
		return;
	}
	Dictionary meta = replay_meta_.duplicate();
	meta["turns"] = (int64_t)turns;
	meta["duration"] = (double)turns * rtso::TurnManager::TURN_DURATION;
	meta["players"] = player_list();
	// ReplayMetadata: 0.28 writes it only in endGame, so a replay whose match never closed
	// (a crash) has no summary and the replay menu hides its Summary button.
	if (ended) meta["summary"] = statistics_summary();
	meta["finished"] = sim->is_game_over() || sim->is_victory();
	write_text(replay_dir_ + "metadata.json", JSON::stringify(meta, "\t"));
	replay_written_turn_ = turns;
}

void RtsoMain::stop_replay_recording(bool ended) {
	// What was recorded still replays: it ends where the game left its commands.
	write_replay(ended);
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
	watched_replay_dir_ = dir;
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
		const String start = meta.get("start_save", "");
		if (!start.is_empty()) {
			// Continued from a save: the save it started from, then its turns.
			loading_replay_start_ = true;
			const bool loaded = load_game(dir.path_join(start));
			loading_replay_start_ = false;
			if (!loaded) {
				UtilityFunctions::printerr("[RTS0] Replay start save unreadable: ", dir.path_join(start));
				return false;
			}
		}
		turn_mgr_.reset();
	}
	if (!turn_mgr_.load_replay(cmd_path.utf8().get_data())) {
		UtilityFunctions::printerr("[RTS0] Replay load failed: ", String(turn_mgr_.last_replay_error().c_str()));
		return false;
	}
	replay_recording_ = false;
	replay_finished_ = false;
	replay_meta_ = meta;
	// 0 A.D. starts a replay as an observer (player -1) with the map revealed and
	// no counters; the picker follows one player.
	replay_view_ = rtso::OBSERVER_VIEW;
	replay_hud_player_ = 0xFF; // seat_player()'s observer
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
	d["view"] = turn_mgr_.is_replaying() && replay_view_ < rtso::MAX_PLAYERS ? (int)replay_view_ : -1;
	d["view_gaia"] = replay_view_gaia_;
	Array views;
	if (replay_view_picker_)
		for (int i = 0; i < replay_view_picker_->get_item_count(); ++i) views.push_back(replay_view_picker_->get_item_text(i));
	d["view_items"] = views;
	d["observer_mode"] = replay_observer_label_ && replay_observer_label_->is_visible();
	// The session's own viewPlayer dropdown, for observers and players who have finished.
	Dictionary picker;
	picker["visible"] = observer_view_picker_ && observer_view_picker_->is_visible_in_tree();
	Array items;
	if (observer_view_picker_)
		for (int i = 0; i < observer_view_picker_->get_item_count(); ++i) items.push_back(observer_view_picker_->get_item_text(i));
	picker["items"] = items;
	picker["view"] = observer_view_ < rtso::MAX_PLAYERS ? (int)observer_view_ : -1;
	picker["gaia"] = observer_view_gaia_;
	picker["observer_mode"] = observer_mode_label_ && observer_mode_label_->is_visible_in_tree();
	d["observer_picker"] = picker;
	Dictionary follow;
	CheckBox *box = turn_mgr_.is_replaying() ? replay_follow_box_ : observer_follow_box_;
	follow["visible"] = box && box->is_visible_in_tree();
	follow["on"] = follow_player_;
	follow["camera_follows"] = cam_rig ? (int64_t)cam_rig->follow_id() : (int64_t)0;
	follow["selected"] = PackedInt32Array(selected_ids);
	follow["selected_building"] = (int64_t)selected_building_id;
	d["follow_player"] = follow;
	// The game speed dropdown's titles as this client would see them now.
	Array speed_items;
	const Array speeds = speed_choices(fast_forward_allowed());
	for (int i = 0; i < speeds.size(); ++i) speed_items.push_back(speed_title((float)speeds[i]));
	d["speed_items"] = speed_items;
	d["speed_button"] = speed_btn && speed_btn->is_visible();
	// The player whose counters and time notifications show (local_player()); -1 for none.
	d["hud_player"] = local_player() < rtso::MAX_PLAYERS ? (int)local_player() : -1;
	d["start_time"] = (double)replay_meta_.get("start_time", 0.0);
	d["speed"] = game_speed;
	d["paused"] = is_game_paused();
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
		build_replay_view_picker();
	}
	const int64_t total = (int64_t)turn_mgr_.turn_history().size();
	const int64_t turn = std::min<int64_t>((int64_t)turn_mgr_.current_turn(), total);
	auto clock = [](int64_t turns) {
		const int64_t s = (int64_t)((double)turns * rtso::TurnManager::TURN_DURATION);
		return String::num_int64(s / 60).pad_zeros(2) + ":" + String::num_int64(s % 60).pad_zeros(2);
	};
	// A replay continued from a save shows the game clock, which starts at the save's time.
	const int64_t start_turns = (int64_t)((double)replay_meta_.get("start_time", 0.0) / rtso::TurnManager::TURN_DURATION + 0.5);
	String text = "REPLAY  " + clock(start_turns + turn) + " / " + clock(start_turns + total);
	if (turn_mgr_.replay_out_of_sync()) {
		text += "  (out of sync at turn " + String::num_int64(turn_mgr_.replay_out_of_sync_turn()) + ")";
		// OutOfSyncReplay: an "Out of Sync" message box that pauses the replay until answered.
		if (!replay_oos_shown_) {
			replay_oos_shown_ = true;
			quit_confirmation("out_of_sync_replay");
		}
	}
	if (!replay_finished_ && turn_mgr_.replay_complete()) {
		replay_finished_ = true;
		quit_confirmation("replay_finished"); // QuitConfirmationReplay
	}
	if (replay_finished_) text += "  finished";
	replay_label_->set_text(text);
}

// The 0 A.D. viewPlayer dropdown's entries: Observer, Gaia, then each player
// as "■ name", the square in the player's colour, tooltip "Choose player to view".
OptionButton *RtsoMain::new_player_view_picker() const {
	OptionButton *picker = memnew(OptionButton);
	picker->set_tooltip_text("Choose player to view");
	picker->add_item("Observer", 0);
	auto add_player = [&](uint8_t pid, const String &name, int id) {
		const uint32_t rgb = sim->player_color(pid);
		Ref<Image> img = Image::create(12, 12, false, Image::FORMAT_RGB8);
		img->fill(Color(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f));
		picker->add_icon_item(ImageTexture::create_from_image(img), name, id);
	};
	add_player(rtso::GAIA_PLAYER, "Gaia", kReplayViewGaiaId);
	for (uint8_t p = 0; p < rtso::MAX_PLAYERS; ++p) {
		if (!sim->player_in_match(p) && !((sim->participant_mask() >> p) & 1)) continue;
		add_player(p, player_display_name(p), p + 1);
	}
	picker->set_focus_mode(Control::FOCUS_NONE);
	return picker;
}

Control *RtsoMain::new_player_view_row(OptionButton *picker, Label *observer_label, CheckBox *follow) const {
	// The top panel's frame (12 8, 100%-12 54).
	Control *row = memnew(Control);
	row->set_name("PlayerViewControl");
	row->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	row->set_anchor(SIDE_RIGHT, 1.0f);
	row->set_offset(SIDE_LEFT, 12.0f);
	row->set_offset(SIDE_RIGHT, -12.0f);
	row->set_offset(SIDE_TOP, 8.0f);
	row->set_offset(SIDE_BOTTOM, 54.0f);
	auto place = [&](Control *c, float al, float l, float ar, float r, float t, float ab, float b) {
		row->add_child(c);
		c->set_anchor(SIDE_LEFT, al);
		c->set_anchor(SIDE_RIGHT, ar);
		c->set_anchor(SIDE_TOP, 0.0f);
		c->set_anchor(SIDE_BOTTOM, ab);
		c->set_offset(SIDE_LEFT, l);
		c->set_offset(SIDE_RIGHT, r);
		c->set_offset(SIDE_TOP, t);
		c->set_offset(SIDE_BOTTOM, b);
	};
	// viewPlayer "85%-282 5 100%-290 31", inside RTS0's taller panel.
	picker->add_theme_font_size_override("font_size", 13);
	picker->set_clip_text(true);
	picker->set_fit_to_longest_item(false);
	// ModernDropDown: a dark box with a thin border, no taller than the panel row.
	for (const char *state : {"normal", "hover", "pressed", "focus", "disabled"}) {
		Ref<StyleBoxFlat> sb;
		sb.instantiate();
		sb->set_bg_color(Color(0.05f, 0.05f, 0.05f, 0.9f));
		sb->set_border_color(Color(0.55f, 0.5f, 0.35f));
		sb->set_border_width_all(1);
		sb->set_content_margin(SIDE_LEFT, 6.0f);
		sb->set_content_margin(SIDE_RIGHT, 6.0f);
		sb->set_content_margin(SIDE_TOP, 2.0f);
		sb->set_content_margin(SIDE_BOTTOM, 2.0f);
		picker->add_theme_stylebox_override(state, sb);
	}
	place(picker, 0.85f, -282.0f, 1.0f, -290.0f, 2.0f, 1.0f, -2.0f);
	// observerText "24 2 200 100%-2" over the counters it replaces.
	observer_label->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
	observer_label->add_theme_font_size_override("font_size", 16);
	place(observer_label, 0.0f, 24.0f, 0.0f, 200.0f, 2.0f, 1.0f, -2.0f);
	// optionFollowPlayer "50%+54 4 50%+256 100%".
	place(follow, 0.5f, 54.0f, 0.5f, 256.0f, 4.0f, 1.0f, 0.0f);
	return row;
}

// 0 A.D. replay "Player view" (PlayerViewControl): Observer (everything revealed),
// then every player, Gaia first, as "■ name" with the square in the player's colour;
// "Observer Mode" shows while the view is Observer or Gaia. Presentation only, so the
// replay's hashes are unaffected.
void RtsoMain::build_replay_view_picker() {
	if (replay_view_picker_ || !replay_label_) return;
	replay_view_picker_ = new_player_view_picker();
	replay_view_picker_->connect("item_selected", Callable(this, "_on_replay_view_selected"));
	replay_observer_label_ = memnew(Label);
	replay_observer_label_->set_text("Observer Mode");
	replay_follow_box_ = new_follow_player_box();
	replay_label_->get_parent()->add_child(new_player_view_row(replay_view_picker_, replay_observer_label_, replay_follow_box_));
}

void RtsoMain::_on_replay_view_selected(int index) {
	if (!replay_view_picker_) return;
	const int id = replay_view_picker_->get_item_id(index);
	set_replay_view(id == kReplayViewGaiaId ? (int)rtso::GAIA_PLAYER : id - 1);
}

void RtsoMain::set_replay_view(int player) {
	if (!turn_mgr_.is_replaying()) return;
	replay_view_gaia_ = player == rtso::GAIA_PLAYER;
	if (player < 0 || player >= rtso::MAX_PLAYERS) {
		// g_ViewedPlayer -1: the counters, civ icon and player panels go, and
		// time notifications are the ones every observer sees. Gaia (0) sees
		// everything, so its view is the observer's.
		replay_view_ = rtso::OBSERVER_VIEW;
		replay_hud_player_ = 0xFF; // seat_player()'s observer
	} else {
		replay_view_ = (uint8_t)player;
		replay_hud_player_ = (uint8_t)player;
	}
	clear_selection();
	if (replay_view_picker_)
		for (int i = 0; i < replay_view_picker_->get_item_count(); ++i)
			if (replay_view_picker_->get_item_id(i) ==
					(replay_view_gaia_ ? kReplayViewGaiaId : replay_view_ == rtso::OBSERVER_VIEW ? 0 : replay_view_ + 1))
				replay_view_picker_->select(i);
	if (replay_observer_label_) replay_observer_label_->set_visible(replay_view_ == rtso::OBSERVER_VIEW);
}

// --- Saved games ---------------------------------------------------------------

bool RtsoMain::write_save_metadata(const String &path, const String &description) const {
	if (!sim) return false;
	Dictionary meta;
	meta["save_version"] = (int64_t)rtso::Simulation::SAVE_VERSION;
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
	// session.js getSavedGameData: the control groups, and the camera (CGameView's save data).
	Dictionary gui;
	Array groups;
	for (int g = 0; g < 10; ++g) groups.push_back(selected_groups[g]);
	gui["groups"] = groups;
	if (cam_rig) gui["camera"] = cam_rig->camera_data();
	meta["gui"] = gui;
	return write_text(path.get_basename() + ".json", JSON::stringify(meta, "\t"));
}

// A screenshot beside the save (<name>.png) for the load dialog. Headless runs
// have no rendered frame, so the save simply has no picture.
void RtsoMain::write_save_thumbnail(const String &path) {
	const String png = path.get_basename() + ".png";
	if (FileAccess::file_exists(png)) DirAccess::remove_absolute(ProjectSettings::get_singleton()->globalize_path(png));
	Viewport *vp = get_viewport();
	if (!vp) return;
	Ref<ViewportTexture> tex = vp->get_texture();
	if (tex.is_null()) return;
	Ref<Image> img = tex->get_image();
	if (img.is_null() || img->is_empty()) return;
	const int w = 320;
	const int h = std::max(1, (int)std::lround((double)w * img->get_height() / std::max(1, img->get_width())));
	img->resize(w, h, Image::INTERPOLATE_BILINEAR);
	img->save_png(png);
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
	const String file = "saves/" + safe + ".rts0sav";
	if (!save_game("user://" + file, description)) return false;
	log_render("Saved game to '" + file + "'"); // SavedGame.cpp Save: LOGMESSAGERENDER
	return true;
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
	// Host Saved Game: host it once loaded; Start continues it for everyone.
	net_saved_game_ = (bool)ms->get("net_saved_game") && String(ms->get("net_mode")) == "host" ? path : String();
	ms->set("net_saved_game", false);
	net_saved_seats_.clear();
	if (!net_saved_game_.is_empty()) {
		const Array players = meta.get("players", Array());
		for (int i = 0; i < players.size(); ++i) {
			const Dictionary p = players[i];
			const int id = (int)p.get("id", -1);
			if (id < 0 || id >= rtso::MAX_PLAYERS) continue;
			if ((int)net_saved_seats_.size() <= id) net_saved_seats_.resize(id + 1);
			if (!(bool)p.get("ai", false) && !(bool)p.get("defeated", false))
				net_saved_seats_[id] = "@" + String(p.get("name", "")).to_lower(); // "@": a seat, even unnamed
		}
	}
	return true;
}

// 0 A.D. NMT_SAVED_GAME_START: everyone keeps the seat the setup page's player
// assignment gave them (which seats only in the save's human seats). Anyone
// not seated there yet takes, as PlayerAssignmentsController does, the seat
// its name had in the save, else the first free seat a human played (no
// AI's), else watches. Every client then loads the host's world (TurnManager::start_saved_game).
void RtsoMain::host_start_saved_game() {
	Dictionary meta = read_json(net_saved_game_.get_basename() + ".json");
	// The setup page could change nothing but the seats (GameSettingControl
	// EnabledWhenSavedGame); the save's own settings go out with it.
	apply_settings_snapshot(meta.get("settings", Dictionary()));
	auto is_seat = [&](int id) { return id >= 0 && id < (int)net_saved_seats_.size() && !net_saved_seats_[id].is_empty(); };
	std::set<uint8_t> taken;
	auto seat_for = [&](const std::string &name) -> uint8_t {
		const String n = "@" + String::utf8(name.c_str()).to_lower();
		for (int id = 0; id < (int)net_saved_seats_.size(); ++id)
			if (net_saved_seats_[id] == n && !taken.count(id)) { taken.insert(id); return id; }
		for (int id = 0; id < (int)net_saved_seats_.size(); ++id)
			if (is_seat(id) && !taken.count(id)) { taken.insert(id); return id; }
		return 0xFF;
	};
	std::map<uint32_t, uint8_t> seats;
	std::vector<const rtso::NetPeer *> clients;
	for (const auto &p : net_host_.getPeers())
		if (p.id != 0 && p.connected && p.is_authenticated && !p.kick_disconnect_ms) clients.push_back(&p);
	// The host's player assignment first (PlayerAssignmentsController: the
	// page seats clients only in the save's human seats; Unassigned watches).
	auto assigned = [&](uint32_t c) -> int {
		if (c >= rtso::NET_SETUP_CLIENTS || !((net_setup_.clients >> c) & 1)) return -2; // not on the page yet
		for (int k = 0; k < 8; ++k)
			if (net_setup_.slot_client[k] == (int8_t)c) return is_seat(k) && !taken.count(k) ? k : -2;
		return -1;
	};
	auto seat_from_page = [&](uint32_t c) {
		const int k = assigned(c);
		if (k >= 0) { taken.insert((uint8_t)k); seats[c] = (uint8_t)k; }
		else if (k == -1) seats[c] = 0xFF;
	};
	seat_from_page(0);
	for (const auto *p : clients) if (!p->is_observer) seat_from_page(p->id);
	// Anyone the page has not seated yet: the seat its name had in the save,
	// else the first free one a human played (no AI's), else watches.
	auto named = [&](const std::string &name) {
		const String n = "@" + String::utf8(name.c_str()).to_lower();
		for (const auto &h : net_saved_seats_) if (h == n) return true;
		return false;
	};
	if (!seats.count(0)) seats[0] = seat_for(net_host_.localPlayerName());
	for (const auto *p : clients) if (!seats.count(p->id) && (p->is_observer || named(p->player_name))) seats[p->id] = p->is_observer ? 0xFF : seat_for(p->player_name);
	for (const auto *p : clients) if (!seats.count(p->id)) seats[p->id] = seat_for(p->player_name);
	rtso::GameSetupParams setup;
	match_settings_setup(setup);
	setup.saved_game = 1;
	turn_mgr_.reset();
	turn_mgr_.set_net_host(&net_host_, 0);
	turn_mgr_.set_game_start_applied_sim(sim);
	turn_mgr_.start_saved_game(seats, sim);
	start_net_speed(setup.speed);
	net_setup_.setup = setup;
	++net_setup_.seq;
	net_host_.note_game_start_sent();
	net_host_.sendGameStart(rtso::format_game_start_full(setup));
	net_game_started_ = true;
	selected_ids.clear();
	selected_building_id = 0;
	UtilityFunctions::print("[RTS0][GameStart] HOST continues saved game ", net_saved_game_, " as player ",
			(int)seats[0] + 1, " with ", (int64_t)clients.size(), " client(s)");
	if (lobby_status) lobby_status->set_text("Continuing the saved game; waiting for every player to load it");
	net_saved_game_ = String();
}

// session.js restoreSavedGameData: the camera, an empty selection and the control groups.
void RtsoMain::restore_saved_gui(const String &path) {
	const Dictionary gui = read_json(path.get_basename() + ".json").get("gui", Dictionary());
	if (cam_rig && gui.has("camera")) cam_rig->set_camera_data(gui["camera"]);
	const Array groups = gui.get("groups", Array());
	for (int g = 0; g < 10; ++g) {
		selected_groups[g].clear();
		if (g >= groups.size()) continue;
		const Array ids = groups[g];
		for (int i = 0; i < ids.size(); ++i) selected_groups[g].push_back((int32_t)(int64_t)ids[i]);
	}
}
