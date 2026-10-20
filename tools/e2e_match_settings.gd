extends SceneTree
# The in-game Match Settings dialog as 0 A.D. 0.28 shows it
# (gui/session/match_settings/MatchSettingsDialog.js, gui/common/gamedescription.js):
#   1. a random map with Regicide, locked teams, Nomad, High resources, a World
#      Population and cheats lists each setting as getGameDescription words it;
#   2. the top panel button and the hotkey open it, and opening it closes the
#      other session dialogs;
#   3. a scenario's own per-player resources read "Per Player";
#   4. a defeated player sees "You have been defeated!" over the settings.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_match_settings.gd
# Exit code 0 = pass.

# Loaded at run time: under -s the script compiles before the autoloads exist.
var GameDescription: GDScript
var SetupPage: GDScript

var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func frames(n: int) -> void:
	for i in n:
		await process_frame

func _initialize() -> void:
	run.call_deferred()

func has_line(text: String, label: String, details: String) -> bool:
	return text.split("\n").has("[color=orange]%s[/color] %s" % [label, details])

func run() -> void:
	GameDescription = load("res://scenes/session/game_description.gd")
	SetupPage = load("res://scenes/menu/gamesetup.gd")
	var ms = root.get_node("MatchSettings")
	ms.map_id = "random_continental"
	ms.seed = 99
	ms.map_size = 192
	ms.slot_types = [0, 1]
	ms.slot_civs = [0, 1]
	ms.slot_teams = [1, 2]
	ms.victory = 6 # rtso::VICTORY_REGICIDE: Conquest and Regicide
	ms.regicide_garrison = false
	ms.lock_teams = true
	ms.nomad = true
	ms.start_res = 3
	ms.pop_cap = -600
	ms.cheats = true
	ms.ceasefire_min = 5
	ms.replay_dir = ""
	ms.load_save_path = ""
	change_scene_to_file("res://scenes/main.tscn")
	await frames(60)
	var main = current_scene

	print("1. getGameDescription lines")
	main._toggle_session_dialog("Objectives")
	await frames(2)
	var d: Dictionary = main.debug_dialogs()
	check(d["objectives"], "the top panel button opens Match Settings")
	var text: String = d["match_settings_text"]
	print(text)
	check(has_line(text, "Conquest", GameDescription.CONQUEST[1]) and has_line(text, "Regicide", "Defeat opponents by killing their hero."), "Conquest and Regicide")
	check(has_line(text, "Exposed Heroes", "Heroes cannot be garrisoned and they are vulnerable to raids."), "Exposed Heroes")
	check(has_line(text, "Locked Teams", "Players can't change the initial teams."), "Locked Teams instead of Diplomacy")
	check(not text.contains("Players can make alliances"), "no alliance line with locked teams")
	check(has_line(text, "Allied Victory", "If one player wins, his or her allies win too. If one group of allies remains, they win."), "Allied Victory")
	check(has_line(text, "Ceasefire", "For the first 5 minutes, other players will stay neutral."), "Ceasefire")
	check(has_line(text, "Map Name", "Continental"), "Map Name")
	check(has_line(text, "Map Type", "Random"), "Map Type")
	check(has_line(text, "Map Size", "Small"), "Map Size")
	# PlayerPlacement.pickRandomItems: a running match names the pattern the map drew, never Random.
	check(not text.contains("Random Player Placement"), "a Random placement shows the pattern in play")
	var placed: Dictionary = main.get_match_settings_info()
	placed["map_id"] = "random_ambush"
	placed["team_placement"] = 1
	check(has_line(GameDescription.describe(placed), "Circle", "Players are placed in a circle spanning the map."), "a chosen placement reads its title and description")
	check(has_line(text, "Nomad Mode", "Players start with only few units and have to find a suitable place to build their city."), "Nomad Mode")
	check(has_line(text, "Starting Resources", "High (1000)"), "Starting Resources")
	check(has_line(text, "Population Cap Type", "World Population"), "Population Cap Type")
	check(has_line(text, "World Population Cap", "600"), "World Population Cap")
	check(has_line(text, "Treasures", "As defined by the map."), "Treasures")
	check(has_line(text, "Explored Map", "disabled") and has_line(text, "Revealed Map", "disabled"), "Explored and Revealed Map")
	check(has_line(text, "Allied View", "disabled"), "Allied View")
	check(has_line(text, "Cheats", "enabled"), "Cheats")
	check(str(d["match_settings_state"]) == "", "no player-state line while playing")

	print("2. One dialog at a time")
	main.toggle_diplomacy()
	await frames(2)
	d = main.debug_dialogs()
	check(d["diplomacy"] and not d["objectives"], "diplomacy closes Match Settings")
	main.toggle_objectives()
	await frames(2)
	d = main.debug_dialogs()
	check(d["objectives"] and not d["diplomacy"], "Match Settings closes diplomacy")
	main.toggle_objectives()
	await frames(2)
	check(not main.debug_dialogs()["objectives"], "toggling again closes it")

	print("3. Scenario settings")
	var info: Dictionary = main.get_match_settings_info()
	info["map_id"] = "laconia"
	info["map_name"] = "Laconia"
	info["pop_cap"] = 300
	var scen: String = GameDescription.describe(info)
	check(has_line(scen, "Map Type", "Scenario"), "a scenario's map type")
	check(has_line(scen, "Starting Resources", "Per Player"), "its own resources read Per Player")
	check(has_line(scen, "Player Population Cap", "300"), "a player cap")
	check(not scen.contains("Map Size") and not scen.contains("Nomad Mode"), "no random-map lines")
	info["victory_mask"] = GameDescription.VK_CONQUEST | GameDescription.VK_WONDER
	info["wonder_min"] = 1
	info["lock_teams"] = false
	scen = GameDescription.describe(info)
	check(has_line(scen, "Conquest", GameDescription.CONQUEST[1]) and has_line(scen, "Wonder (1 minute)", GameDescription.WONDER_DESC), "Wonder adds to Conquest")
	check(has_line(scen, "Diplomacy", "Players can make alliances and declare war on allies."), "Diplomacy when teams are open")
	info["victory_mask"] = GameDescription.VK_CONQUEST_UNITS | GameDescription.VK_CONQUEST_CIVIC_CENTRES | GameDescription.VK_RELIC
	info["relic_min"] = 3
	info["relic_count"] = 4
	info["pop_cap"] = -GameDescription.TEAM_POP_FLAG - 250
	scen = GameDescription.describe(info)
	var lines := Array(scen.split("\n"))
	check(not scen.contains("[color=orange]Conquest[/color]"), "a combination without Conquest leaves it out")
	check(lines.find("[color=orange]Conquest Units[/color] " + GameDescription.CONQUEST_UNITS[1]) < lines.find("[color=orange]Conquest Civic Centers[/color] " + GameDescription.CONQUEST_CIVIC_CENTRES[1])
		and lines.find("[color=orange]Conquest Civic Centers[/color] " + GameDescription.CONQUEST_CIVIC_CENTRES[1]) < lines.find("[color=orange]Capture the Relic (3 minutes)[/color] " + GameDescription.RELIC_DESC)
		and has_line(scen, "Relic Count", "4"), "combined conditions in GUIOrder, each with its lines")
	check(has_line(scen, "Population Cap Type", "Team Population") and has_line(scen, "Team Population Cap", "250"), "Team Population")
	info["victory_mask"] = 0
	check(has_line(GameDescription.describe(info), "Endless Game", "No winner will be determined, even if everyone is defeated."), "no condition is an Endless Game")
	# gamedescription.js Biome and Rated game lines.
	var rinfo: Dictionary = main.get_match_settings_info()
	rinfo["map_id"] = "random_archipelago_0ad"
	var offered: Array = SetupPage.biome_values(SetupPage._map_types_data().get("random_biomes", {}).get("archipelago", []))
	check(not offered.is_empty(), "archipelago offers biomes: " + str(offered))
	rinfo["biome"] = offered[0] if not offered.is_empty() else 1
	var bt: Array = GameDescription.BIOME_TEXT.get(int(rinfo["biome"]), ["", ""])
	check(has_line(GameDescription.describe(rinfo), bt[0], bt[1]), "a picked biome reads its Title and Description")
	rinfo["biome"] = 0
	check(not GameDescription.describe(rinfo).contains(bt[0] + "[/color]"), "Map default has no biome line")
	rinfo["biome"] = SetupPage.RANDOM_BIOME
	check(has_line(GameDescription.describe(rinfo), "Random Biome", "Randomly select a biome from the list."), "Random reads Random Biome until Start")
	rinfo["rated"] = true
	rinfo["player_count"] = 2
	check(has_line(GameDescription.describe(rinfo), "Rated game", GameDescription.RATING_DESC), "a rated 1v1 says so")
	rinfo["rated"] = false
	check(not GameDescription.describe(rinfo).contains("Rated game"), "no rated line outside a lobby game")
	rinfo["lobby_game"] = true
	check(has_line(GameDescription.describe(rinfo), "Rated game", "disabled"), "an unrated lobby game reads disabled")
	# gamedescription.js translates each line: labels, values, enabled/disabled and the plural minutes.
	info["victory_mask"] = GameDescription.VK_WONDER
	info["wonder_min"] = 2
	info["ally_view"] = true
	TranslationServer.add_translation(load("res://assets/l10n/de.po"))
	TranslationServer.set_locale("de")
	var de: String = GameDescription.describe(info)
	TranslationServer.set_locale("en")
	check(de.contains("[color=orange]Wunder (2 Minuten)[/color] ") and has_line(de, "Kartenname", "Lakonien") and has_line(de, "Sicht von Verbündeten", "aktiviert"),
		"German Match Settings are translated: " + de.substr(0, 160))

	print("4. Player state")
	main.resign()
	await frames(40)
	main.close_open_dialogs()
	main.toggle_objectives()
	await frames(2)
	d = main.debug_dialogs()
	check(str(d["match_settings_state"]) == "You have been defeated!", "a defeated player sees the state line: " + str(d["match_settings_state"]))

	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
