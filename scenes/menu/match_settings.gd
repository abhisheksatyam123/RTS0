extends Node
# Survives scene changes so Game Setup can start a match with the chosen civ.

# Core match choices (legacy fields kept for back-compat).
var player_civ: int = 0
var ai_civ: int = 1
var map_index: int = 0
var player_count: int = 2
# Full 0 A.D.-style setup (page_gamesetup): map id as in
# RtsoMain.get_available_maps(), 0 = randomize at start.
var map_id: String = "random_continental"
var seed: int = 0
# 0 A.D. -autostart-aiseed: the AIs' personality seed, 0 = the match seed.
var ai_seed: int = 0
# Victory: rtso::VictoryCondition id. 0 conquest, 1 wonder, 2 relic, 3 endless,
# 4 conquest units, 5 conquest structures, 6 regicide; 0x80 | VictoryKind mask for any other
# combination of Game Setup's victory conditions.
var victory: int = 0
# Legacy wave-defense mode (enemy waves every 35s). Off for skirmish.
var waves_enabled: bool = false
# 0 A.D. "Last Man Standing": no allied victory, play until one player is left.
var last_man_standing: bool = false
# 0 A.D. population limit per player (0 = unlimited, negative = World Population shared by
# the players still in the game) and ceasefire length in minutes.
var pop_cap: int = 300
# 0 A.D. 0.28 Team Population: a positive pop_cap is each team's limit, shared by its
# players still in the game (a teamless player keeps all of it).
var team_pop: bool = false
var ceasefire_min: int = 0
# 0 A.D. map visibility: everything visible, or terrain known from the start.
var reveal_map: bool = false
var explore_map: bool = false
# 0 A.D. AllyView: every player starts seeing what their allies see (Cartography researched).
var ally_view: bool = false
# Wonder / Capture the Relic: hold times in minutes and the number of relics.
var wonder_min: int = 20
var relic_min: int = 20
var relic_count: int = 2
# Per-slot Petra difficulty (0..5) and behaviour (0..3), in slot order.
var slot_diffs: Array = []
var slot_behs: Array = []
# Per-slot rtso::PLAYER_COLORS index (0 A.D. PlayerColor), in slot order.
var slot_colors: Array = []
# Per-slot player names (0 A.D. PlayerName), in slot order; empty = "Player N".
var slot_names: Array = []
# 0 A.D. Nomad (random maps: no starting structures) and RegicideGarrison (the Regicide hero may garrison).
var nomad: bool = false
var regicide_garrison: bool = false
# 0 A.D. LockTeams: nobody can change diplomacy during the match.
var lock_teams: bool = false
# 0 A.D. CheatsEnabled: chat cheat codes ("i want pizza", ...) work.
var cheats: bool = false
# 0 A.D. DisableSpies: Espionage can't be researched, so nobody bribes units for vision.
var disable_spies: bool = false
# 0 A.D. DisableTreasures: no treasures on the map.
var disable_treasures: bool = false
# Random-map biome: 0 = the map's own, else rtso::BiomeType + 1. Game speed at the start.
var biome: int = 0
# 0.28 PlayerAssignment: the setup slot this machine plays offline (index into
# slot_types), -1 to watch; -2 (older setups, -autostart) seats you in slot 1.
var local_slot: int = -2
var game_speed: float = 1.0
# The map sets every slot's civ, team and colour (a scenario): in a networked
# game setup, clients may not ask to change theirs.
var map_locks_players: bool = false
# 0 A.D. RatingEnabled: a rated lobby game (the host sets it; clients get it with the setup).
var rated: bool = false
# Map trigger options (0 A.D. WaterLevel, SeaLevelRiseTime): Flood water level
# 0 rising, 1 shallow, 2 deep; Extinct Volcano minutes until the water rises.
var water_level: int = 0
var sea_level_rise_min: int = 25
# 0 A.D. Player Placement (the patterns a random map lists): rtso::TeamPlacement, 0 = Random.
var team_placement: int = 0
var landscape: int = 0 # Unknown map Landscape (RmsGenerator::UNKNOWN_LANDSCAPE_*), 0 = random
var trigger_difficulty: int = 3 # 0 A.D. Trigger Difficulty: 1 Very Easy .. 5 Very Hard, 3 Medium
var map_daytime: int = 0 # 0 A.D. Daytime: 0 = the map picks, else the option in DAYTIMES (1-based)
# Random-map width in meters, 0 A.D.'s sizes 128 (Tiny) to 512 (Giant).
var map_size: int = 256 # 0.28 map_sizes.json default, Normal
# Petra difficulty 0..5 (sandbox..very hard), behavior 0..3.
var ai_difficulty: int = 3
var ai_behavior: int = 2
# Starting resources preset index 0..3 (sparse/normal/generous/abundant).
var start_res: int = 1
# Per-slot setup, slot 0 is always the human player.
# slot_types: 0 human, 1 AI, 2 closed. slot_civs: civ index per slot.
# slot_teams: team per slot, 0 = None, 1-4 = T1..T4 (0 A.D. Team -1 / 0-3).
var slot_types: Array = [0, 1]
var slot_civs: Array = [0, 1]
var slot_teams: Array = [0, 0]
# Set by the replay menu / load dialog, consumed (and cleared) by the match scene:
# a replay folder (user://replays/<id>/) to watch, or a saved game to load.
var replay_dir: String = ""
var load_save_path: String = ""
# Multiplayer handed from the lobby screen to the match (main_node _start_menu_net):
# net_mode "host" or "join" (cleared once used), net_lobby the lobby server "ip:port".
var net_mode: String = ""
var net_lobby: String = ""
var net_player_name: String = "Player"
var net_ip: String = "127.0.0.1"
var net_port: int = 20595
var net_password: String = ""
var net_observe: bool = false
# 0 A.D. "Host Saved Game": with net_mode "host", the save in load_save_path is
# continued in multiplayer (cleared once used).
var net_saved_game: bool = false
# 0 A.D. campaignData: the campaign run (its file name under user://saves/campaigns/)
# and the level being played; empty outside a campaign. Saves and replays keep them.
var campaign_run: String = ""
# 0 A.D. -autostart-disable-replay: false, and matches this run write no replay.
var record_replay: bool = true
var campaign_level: String = ""

func strip_engine_popups(n: Node) -> void:
	if n == null:
		return
	for p in n.find_children("*", "Popup", true, false):
		var par: Node = p.get_parent()
		if par:
			par.remove_child(p)
		p.queue_free()

func prepare_line_edit(le: LineEdit) -> LineEdit:
	if le:
		le.context_menu_enabled = false
		le.middle_mouse_paste_enabled = false
		strip_engine_popups(le)
	return le

func hide_option_popups(_n: Node = null) -> void:
	var tree := get_tree()
	if tree and tree.root:
		tree.root.gui_release_focus()

func go(path: String) -> int:
	hide_option_popups()
	var tree := get_tree()
	if tree == null:
		return ERR_UNAVAILABLE
	# 0 A.D. starts every match through page_loading.xml (tips, quotes, progress).
	if path == "res://scenes/main.tscn":
		path = "res://scenes/menu/loading.tscn"
	tree.change_scene_to_file.call_deferred(path)
	return OK
