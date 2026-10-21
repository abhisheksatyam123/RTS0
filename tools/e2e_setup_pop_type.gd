extends SceneTree
# Game Setup's population settings as 0 A.D. 0.28 shows them (gamesetup
# PopulationCapType.js, PopulationCap.js, LockedTeams.js and
# simulation/data/settings/population_capacities.json):
#   1. Population Cap Type offers Player, Team and World Population, Player
#      first, with Player's caps (50 to Unlimited, 300 by default);
#   2. Team Population lists its caps (100 to Unlimited, 400 by default) and
#      turns Locked teams on for good; Start sends the cap as a Team
#      Population (MatchSettings.team_pop);
#   3. the page comes back with Team Population;
#   4. World Population starts a negative pop_cap, Unlimited per player 0, and
#      leaving Team Population frees Locked teams.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_setup_pop_type.gd
# Exit code 0 = pass.

var failures := 0
var cfg := RtsoUserConfig.new()
var saved_persist := ""

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func left(page, path: String):
	return page.get_node("%" + path.get_file())

func items(sel) -> Array:
	return range(sel.get_item_count()).map(func(i): return sel.get_item_text(i))

func open_page():
	change_scene_to_file("res://scenes/menu/gamesetup.tscn")
	await frames(6)
	return current_scene

func run() -> void:
	saved_persist = cfg.get_value("persistmatchsettings")
	cfg.set_value("persistmatchsettings", "false")
	var ms = root.get_node("MatchSettings")
	ms.map_id = ""
	ms.pop_cap = 300
	ms.team_pop = false
	ms.lock_teams = true
	var page = await open_page()
	# LockedTeams.onLoad: single player opens with open diplomacy (DefaultValue = HasNetClient()).
	check(not left(page, "StartRow/LockTeams").button_pressed, "single player opens with Teams Locked off")

	print("1. Population Cap Type")
	var typ: RtsoSafeSelect = left(page, "PopCapTypeSlot").get_child(0)
	var pop: RtsoSafeSelect = left(page, "PopCapSlot").get_child(0)
	check(items(typ) == ["Player Population", "Team Population", "World Population"] and typ.text == "Player Population", "types: " + str(items(typ)))
	check(items(pop) == ["50", "100", "150", "200", "250", "300", "Unlimited"] and pop.text == "300", "player caps, 300: " + str(items(pop)))
	check(left(page, "PopCapLabel").text == "Player Population Cap:", "titled Player Population Cap")
	# PopulationCap.onHoverChange: past 1200 units the entry warns of lag.
	var warn := "Warning: These settings can result in significant lag when all players reach their maximum population capacity."
	check(pop.get_item_tooltip(0) == "Choose the player population cap." and pop.get_item_tooltip(6) == warn, "Unlimited warns of lag, 50 does not")
	var spin = page._slot_count_node()
	var was: int = int(spin.value)
	spin.value = 8
	await frames(2)
	check(pop.get_item_tooltip(3) == warn and pop.get_item_tooltip(2) != warn, "8 players: 200 warns (1600), 150 does not (1200)")
	spin.value = was
	await frames(2)

	print("2. Team Population")
	var lock: CheckBox = left(page, "StartRow/LockTeams")
	lock.button_pressed = false
	typ.select(1)
	page._on_pop_type_selected(1)
	check(pop.text == "400" and items(pop).size() == 15 and items(pop)[0] == "100" and items(pop)[-1] == "Unlimited", "team caps, 400: " + str(items(pop)))
	check(left(page, "PopCapLabel").text == "Team Population Cap:" and typ.tooltip_text.contains("Locked teams"), "Team Population Cap, with 0.28's tooltip")
	check(lock.button_pressed and lock.disabled, "Locked teams on and fixed")
	page._on_start_pressed()
	check(ms.pop_cap == 400 and ms.team_pop and ms.lock_teams, "Start: team cap %d, team_pop %s, locked %s" % [ms.pop_cap, ms.team_pop, ms.lock_teams])
	await frames(4)

	print("3. Back to the page")
	page = await open_page()
	typ = left(page, "PopCapTypeSlot").get_child(0)
	pop = left(page, "PopCapSlot").get_child(0)
	lock = left(page, "StartRow/LockTeams")
	check(typ.text == "Team Population" and pop.text == "400" and lock.disabled, "Team Population 400 again: %s %s" % [typ.text, pop.text])

	print("4. World and Unlimited")
	typ.select(2)
	page._on_pop_type_selected(2)
	check(pop.text == "600" and not lock.disabled, "World Population 600, Locked teams free again")
	page._on_start_pressed()
	check(ms.pop_cap == -600 and not ms.team_pop, "Start: World Population %d" % ms.pop_cap)
	await frames(4)
	page = await open_page()
	typ = left(page, "PopCapTypeSlot").get_child(0)
	pop = left(page, "PopCapSlot").get_child(0)
	check(typ.text == "World Population" and pop.text == "600", "World Population 600 again")
	typ.select(0)
	page._on_pop_type_selected(0)
	pop.select(6)
	page._on_start_pressed()
	check(ms.pop_cap == 0 and not ms.team_pop, "Unlimited per player is 0: %d" % ms.pop_cap)
	await frames(4)

	cfg.set_value("persistmatchsettings", saved_persist)
	print("FAILURES: ", failures)
	quit(1 if failures else 0)
