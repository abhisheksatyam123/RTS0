extends RefCounted
## 0 A.D. 0.28's command-line autostart (source/ps/GameSetup/GameSetup.cpp
## Autostart, binaries/system/readme.txt; GPL-2.0-or-later): start a match
## straight from the command line, skipping the menus. Godot passes these after
## "--", with one dash as in 0 A.D. or two:
##   godot --path . -- -autostart="random/mainland" -autostart-seed=-1
##     -autostart-players=2 -autostart-civ=1:athen -autostart-civ=2:brit
##     -autostart-ai=2:petra
## Single-player options are ported. The local player is slot 1, or an observer
## with -autostart-player=-1 (then every seat needs an AI; one without becomes a
## Sandbox AI). -autostart-host, -autostart-client, -autostart-nonvisual,
## -autostart-aiseed and -autostart-disable-replay are not ported.

const GameSetup := preload("res://scenes/menu/gamesetup.gd")

## Set once the command line has started a match: back at the menu it doesn't again.
static var consumed := false

## The -autostart options in args: each name (without dashes) to its values in order.
static func parse(args: PackedStringArray) -> Dictionary:
	var out := {}
	for a in args:
		var s := a.trim_prefix("-").trim_prefix("-")
		if not s.begins_with("autostart"):
			continue
		var eq := s.find("=")
		var key := s if eq < 0 else s.substr(0, eq)
		var value := "" if eq < 0 else s.substr(eq + 1).trim_prefix("\"").trim_suffix("\"")
		if not out.has(key):
			out[key] = []
		out[key].append(value)
	return out

static func _last(opts: Dictionary, key: String, fallback: String) -> String:
	var v: Array = opts.get(key, [])
	return str(v[-1]) if not v.is_empty() else fallback

## "PLAYER:VALUE" options as {player index from 0: value}.
static func _per_player(opts: Dictionary, key: String) -> Dictionary:
	var out := {}
	for v in opts.get(key, []):
		var parts := str(v).split(":", true, 1)
		if parts.size() == 2 and parts[0].is_valid_int():
			out[int(parts[0]) - 1] = parts[1]
	return out

## The RTS0 map an autostart path names ("random/mainland", "skirmishes/x",
## "scenarios/x", "tutorials/x"), or {} with none.
static func find_map(path: String, maps: Array) -> Dictionary:
	var dir := path.get_base_dir().get_file()
	var name := path.get_file().get_basename()
	for m in maps:
		var random := str(m.get("type", "")) == "random"
		if dir == "random" and random and (GameSetup.rms_script(str(m["id"])) == name or str(m["id"]) == name):
			return m
		if dir != "random" and not random and str(m["id"]) == name:
			return m
	if dir == "tutorials" and name in load("res://scenes/menu/scenario_start.gd").TUTORIAL_MAPS:
		return GameSetup._read_scenario(name)
	return {}

static func civ_id(code: String) -> int:
	var playable := GameSetup.CIVS.map(func(c): return int(c["id"]))
	if code == "random":
		return playable[randi() % playable.size()]
	for c in GameSetup.CIVS:
		if c["code"] == code:
			return int(c["id"])
	return -1

## Fills MatchSettings from the options. Returns "" or what is wrong.
static func configure(ms: Node, opts: Dictionary, maps: Array) -> String:
	var path := _last(opts, "autostart", "")
	var dir := path.get_base_dir().get_file()
	if not dir in ["random", "skirmishes", "scenarios", "tutorials"]:
		return "Unrecognized map type '%s'." % dir
	var m := find_map(path, maps)
	if m.is_empty():
		return "Map '%s' not found." % path
	var random := dir == "random"
	var n := int(_last(opts, "autostart-players", "2")) if random else int(m.get("players", 2))
	n = clampi(n, 1, 8)
	var map_players: Array = m.get("map_players", [])
	var types := []
	var civs := []
	var teams := []
	var diffs := []
	var behs := []
	for i in n:
		types.append(0) # a seat nobody controls, as 0 A.D. leaves one without AI
		# 0 A.D. gives random maps Athenians; other maps keep their own civs.
		var mc: int = int(map_players[i].get("civ", 0)) if not random and i < map_players.size() else 0
		civs.append(mc)
		teams.append(0)
		diffs.append(3)
		behs.append(2)
	var ai := _per_player(opts, "autostart-ai")
	for p in ai:
		if p >= 0 and p < n:
			types[p] = 1 # "petra"; RTS0 has no other AI
	var d := _per_player(opts, "autostart-aidiff")
	for p in d:
		if p >= 0 and p < n:
			diffs[p] = clampi(int(d[p]), 0, 5)
	# -autostart-team: 0 A.D. teams are 1-based there, 0 for none (RTS0's slot_teams too).
	var t := _per_player(opts, "autostart-team")
	for p in t:
		if p >= 0 and p < n:
			teams[p] = clampi(int(t[p]), 0, 4)
	var civ_args := _per_player(opts, "autostart-civ")
	if dir == "scenarios" and not civ_args.is_empty():
		push_warning("[Autostart] Option 'autostart-civ' is invalid for scenarios")
	elif not civ_args.is_empty():
		for p in civ_args:
			var c := civ_id(str(civ_args[p]))
			if p >= 0 and p < n and c >= 0:
				civs[p] = c
	var local := int(_last(opts, "autostart-player", "1"))
	if local == -1:
		# An observer: every seat is played, one with no AI by a Sandbox AI.
		for i in n:
			if types[i] == 0:
				types[i] = 1
				diffs[i] = 0
	elif local != 1:
		push_warning("[Autostart] -autostart-player=%d: RTS0 seats the local player in slot 1." % local)
	types[0] = 1 if local == -1 else 0

	var seed_arg := _last(opts, "autostart-seed", "0")
	ms.map_id = str(m["id"])
	ms.campaign_run = ""
	ms.campaign_level = ""
	ms.seed = (randi() & 0x7FFFFFFF) if seed_arg == "-1" else int(seed_arg)
	var mask := 0
	for v in opts.get("autostart-victory", ["conquest"]):
		mask |= int(GameSetup.VICTORY_NAMES.get(str(v), 0))
	ms.victory = GameSetup.victory_code(mask)
	ms.ceasefire_min = maxi(0, int(_last(opts, "autostart-ceasefire", "0")))
	ms.wonder_min = maxi(0, int(_last(opts, "autostart-wonderduration", "10")))
	ms.relic_min = maxi(0, int(_last(opts, "autostart-relicduration", "10")))
	ms.relic_count = maxi(1, int(_last(opts, "autostart-reliccount", "2")))
	ms.cheats = true # 0 A.D. autostarts with CheatsEnabled
	if random:
		ms.map_size = int(_last(opts, "autostart-size", "192"))
		ms.biome = int(GameSetup.RMBIOME_INDEX.get(_last(opts, "autostart-biome", ""), 0))
	ms.player_count = n
	ms.slot_types = types
	ms.slot_civs = civs
	ms.slot_teams = teams
	ms.slot_diffs = diffs
	ms.slot_behs = behs
	ms.slot_names = []
	ms.slot_colors = []
	ms.player_civ = civs[0]
	ms.ai_civ = civs[1] if n > 1 else civs[0]
	return ""

## At startup: start the match the command line names, once. True when one starts.
static func run(ms: Node) -> bool:
	if consumed or ms == null:
		return false
	var opts := parse(OS.get_cmdline_user_args() + OS.get_cmdline_args())
	if not opts.has("autostart"):
		return false
	consumed = true
	for k in ["autostart-host", "autostart-client", "autostart-nonvisual"]:
		if opts.has(k):
			push_warning("[Autostart] -%s is not supported; starting a single-player match." % k)
	var err := configure(ms, opts, GameSetup.load_all_maps())
	if err != "":
		push_error("[Autostart] " + err)
		return false
	print("[Autostart] %s seed=%d slots=%s civs=%s teams=%s" % [ms.map_id, ms.seed, str(ms.slot_types), str(ms.slot_civs), str(ms.slot_teams)])
	return ms.go("res://scenes/main.tscn") == OK
