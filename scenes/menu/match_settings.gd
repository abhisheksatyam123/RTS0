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
# Victory: rtso::VictoryCondition id. 0 conquest, 1 wonder, 2 relic, 3 endless,
# 4 conquest units, 5 conquest structures, 6 regicide.
var victory: int = 0
# Legacy wave-defense mode (enemy waves every 35s). Off for skirmish.
var waves_enabled: bool = false
# 0 A.D. "Last Man Standing": no allied victory, play until one player is left.
var last_man_standing: bool = false
# 0 A.D. population limit per player (0 = unlimited) and ceasefire length in minutes.
var pop_cap: int = 200
var ceasefire_min: int = 0
# 0 A.D. map visibility: everything visible, or terrain known from the start.
var reveal_map: bool = false
var explore_map: bool = false
# Wonder / Capture the Relic: hold times in minutes and the number of relics.
var wonder_min: int = 20
var relic_min: int = 20
var relic_count: int = 2
# Per-slot Petra difficulty (0..5) and behaviour (0..3), in slot order.
var slot_diffs: Array = []
var slot_behs: Array = []
# Per-slot rtso::PLAYER_COLORS index (0 A.D. PlayerColor), in slot order.
var slot_colors: Array = []
# 0 A.D. Nomad (random maps: no starting structures) and RegicideGarrison (the Regicide hero may garrison).
var nomad: bool = false
var regicide_garrison: bool = false
# 0 A.D. DisableTreasures: no treasures on the map.
var disable_treasures: bool = false
# Random-map biome: 0 = the map's own, else rtso::BiomeType + 1. Game speed at the start.
var biome: int = 0
var game_speed: float = 1.0
# Map trigger options (0 A.D. WaterLevel, SeaLevelRiseTime): Flood water level
# 0 rising, 1 shallow, 2 deep; Extinct Volcano minutes until the water rises.
var water_level: int = 0
var sea_level_rise_min: int = 25
# Random-map width in meters, 0 A.D.'s sizes 128 (Tiny) to 512 (Giant).
var map_size: int = 128
# Petra difficulty 0..5 (sandbox..very hard), behavior 0..3.
var ai_difficulty: int = 3
var ai_behavior: int = 2
# Starting resources preset index 0..3 (sparse/normal/generous/abundant).
var start_res: int = 1
# Per-slot setup, slot 0 is always the human player.
# slot_types: 0 human, 1 AI, 2 closed. slot_civs: civ index per slot.
# slot_teams: team number per slot (1-based, like 0 A.D.).
var slot_types: Array = [0, 1]
var slot_civs: Array = [0, 1]
var slot_teams: Array = [1, 2]
# Set by the replay menu / load dialog, consumed (and cleared) by the match scene:
# a replay folder (user://replays/<id>/) to watch, or a saved game to load.
var replay_dir: String = ""
var load_save_path: String = ""

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
	tree.change_scene_to_file.call_deferred(path)
	return OK
