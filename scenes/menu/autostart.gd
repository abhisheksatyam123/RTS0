extends RefCounted
## 0 A.D. 0.28's command-line autostart (source/ps/GameSetup/GameSetup.cpp
## Autostart, binaries/system/readme.txt; GPL-2.0-or-later): start a match
## straight from the command line, skipping the menus. Godot passes these after
## "--", with one dash as in 0 A.D. or two:
##   godot --path . -- -autostart="random/mainland" -autostart-seed=-1
##     -autostart-players=2 -autostart-civ=1:athen -autostart-civ=2:brit
##     -autostart-ai=2:petra
## Multiplayer: -autostart-host (with -autostart-host-players, default 2) hosts
## the named match and starts it once that many humans are in and ready;
## -autostart-client=IP joins a host and readies up; -autostart-port and
## -autostart-playername go with either. -autostart-nonvisual draws and plays
## nothing and quits when the match ends; -autostart-disable-replay writes no
## replay, and -autostart-aiseed seeds the AIs' personalities. The local player is
## -autostart-player's slot (1 by default), or an observer with -autostart-player=-1
## (then every seat needs an AI; one without becomes a Sandbox AI).

const GameSetup := preload("res://scenes/menu/gamesetup.gd")
const Driver := preload("res://scenes/menu/autostart_driver.gd")

## Set once the command line has started a match: back at the menu it doesn't again.
static var consumed := false
## Used in place of the command line when set (a test run without its arguments).
static var args_override := PackedStringArray()

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
	if not dir.is_empty() and not dir in ["random", "skirmishes", "scenarios", "tutorials"]:
		return "Unrecognized map type '%s'." % dir
	var m := find_map(path, maps)
	if m.is_empty():
		return "Map '%s' not found." % path
	if dir.is_empty():
		var mtype := str(m.get("type", ""))
		if mtype == "scenario":
			dir = "scenarios"
		elif mtype == "skirmish":
			dir = "skirmishes"
		elif mtype == "random":
			dir = "random"
	if not dir in ["random", "skirmishes", "scenarios", "tutorials"]:
		return "Unrecognized map type '%s'." % dir
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
		behs.append(0) # player_defaults.json AIBehavior "random"
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
	elif local < 1 or local > n:
		push_warning("[Autostart] -autostart-player=%d: no such player; playing player 1." % local)
		local = 1
	if local >= 1:
		types[local - 1] = 0 # you play it, whatever -autostart-ai said
	ms.local_slot = local - 1 if local >= 1 else -1

	var seed_arg := _last(opts, "autostart-seed", "0")
	ms.map_id = str(m["id"])
	if ms.map_id == "frontline_clash":
		ms.battle_preset = "frontline_clash"
		ms.battle_units = 1500
	ms.campaign_run = ""
	ms.campaign_level = ""
	ms.campaign_map_name = ""
	ms.seed = (randi() & 0x7FFFFFFF) if seed_arg == "-1" else int(seed_arg)
	ms.seed_exact = seed_arg != "-1" # GameSetup.cpp Autostart: seed 0 unless -1
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

## The multiplayer and run options: MatchSettings' net_* fields and replay flag, and
## the driver that acts on them in the match, or null with nothing to do there.
static func configure_run(ms: Node, opts: Dictionary) -> Node:
	ms.record_replay = not opts.has("autostart-disable-replay")
	# -autostart-aiseed: the AIs' seed, -1 for a random one; 0 (the default) follows the match seed.
	var ai_seed_arg := _last(opts, "autostart-aiseed", "0")
	ms.ai_seed = (randi() & 0x7FFFFFFF) | 1 if ai_seed_arg == "-1" else maxi(0, int(ai_seed_arg))
	var driver: Node = Driver.new()
	driver.name = "AutostartDriver"
	driver.nonvisual = opts.has("autostart-nonvisual")
	ms.net_mode = ""
	if opts.has("autostart-host") or opts.has("autostart-client"):
		ms.net_lobby = ""
		ms.net_password = ""
		ms.net_observe = false
		ms.net_port = int(_last(opts, "autostart-port", str(ms.net_port)))
		ms.net_player_name = _last(opts, "autostart-playername", "anonymous")
		if opts.has("autostart-client"):
			driver.client = true
			ms.net_mode = "join"
			ms.net_ip = _last(opts, "autostart-client", "127.0.0.1")
			if ms.net_ip == "":
				ms.net_ip = "127.0.0.1"
		else:
			driver.host = true
			driver.host_players = maxi(1, int(_last(opts, "autostart-host-players", "2")))
			ms.net_mode = "host"
	if not (driver.host or driver.client or driver.nonvisual):
		driver.free()
		return null
	return driver

## At startup: start the match the command line names, once. True when one starts.
static func run(ms: Node) -> bool:
	if consumed or ms == null:
		return false
	var opts := parse(args_override if not args_override.is_empty() else OS.get_cmdline_user_args() + OS.get_cmdline_args())
	# 0.28 AutostartClient: a client needs no map, the host sends it.
	if not opts.has("autostart") and not opts.has("autostart-client"):
		return false
	consumed = true
	if not opts.has("autostart-client"):
		var err := configure(ms, opts, GameSetup.load_all_maps())
		if err != "":
			push_error("[Autostart] " + err)
			return false
	var driver := configure_run(ms, opts)
	if driver:
		for old in ms.get_children():
			if old.name == "AutostartDriver":
				old.free()
		ms.add_child(driver)
	print("[Autostart] %s %s seed=%d slots=%s civs=%s teams=%s" % [ms.net_mode if ms.net_mode != "" else "offline", ms.map_id, ms.seed,
		str(ms.slot_types), str(ms.slot_civs), str(ms.slot_teams)])
	return ms.go("res://scenes/main.tscn") == OK
