extends SceneTree
# Game Setup's map choice as 0 A.D. 0.28 gamesetup shows it (MapType.js,
# MapFilter.js, gui/maps/MapFilters.js, MapSelection.js) and the settings a
# scenario fixes (PlayerCiv/PlayerTeam/PlayerColor, PopulationCap,
# StartingResources, Ceasefire, the map and Last Man Standing checkboxes):
#   1. the Map Type is Skirmish, Random or Scenario, Skirmish by default, and
#      the list holds only that type's maps, by name;
#   2. the Map Filter offers the filters with maps of that type; Default hides
#      naval, demo and hidden maps;
#   3. random maps read without "(RMS)", start with a Random entry, and only
#      they let you pick the player count; Start on Random picks one of them;
#   4. a scenario sets and locks its civs, teams, starting resources and map
#      settings, hides the colours; a skirmish frees them again.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_setup_map_type.gd
# Exit code 0 = pass.

# Loaded at run time: the page script names autoloads, which -s scripts compile before.
var Setup

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

func names(page) -> Array:
	return page._filtered.map(func(m): return str(m["name"]))

func ids(page) -> Array:
	return page._filtered.map(func(m): return str(m["id"]))

func filter_titles(page) -> Array:
	return page._filter_names.map(func(n): return Setup.MAP_FILTERS[page._filter_index(n)][1])

func select_map(page, id: String) -> bool:
	var i: int = ids(page).find(id)
	if i < 0:
		return false
	page.get_node("%MapList").select(i)
	page._on_map_list_selected(i)
	return true

func set_filter(page, title: String) -> void:
	page._on_map_filter_selected(filter_titles(page).find(title))

func node(page, path: String):
	return page.get_node("%" + path.get_file())

func slot_selects(page) -> Array:
	# Per row: name label, kind, civ, team, AI gear, colour.
	var out := []
	var grid: GridContainer = page.get_node("VBox/HBox/Right/SlotsBox/SlotsGrid")
	var kids := grid.get_children().filter(func(c): return not c.is_queued_for_deletion())
	for r in range(kids.size() / 6):
		out.append({"civ": kids[r * 6 + 2].get_child(0), "team": kids[r * 6 + 3].get_child(0), "color": kids[r * 6 + 5]})
	return out

func run() -> void:
	Setup = load("res://scenes/menu/gamesetup.gd")
	saved_persist = cfg.get_value("persistmatchsettings")
	cfg.set_value("persistmatchsettings", "false")
	var ms = root.get_node("MatchSettings")
	ms.map_id = ""
	change_scene_to_file("res://scenes/menu/gamesetup.tscn")
	await frames(6)
	var page = current_scene

	print("1. Map Type")
	var mt: RtsoSafeSelect = node(page, "MapTypeSlot").get_child(0)
	check(mt.get_item_count() == 3 and mt.text == "Skirmish" and page._map_type == "skirmish", "Skirmish, Random, Scenario; Skirmish first: " + mt.text)
	check(mt.tooltip_text.begins_with("Select a map type."), "with 0.28's tooltip")
	var n := names(page)
	var sorted := n.duplicate()
	sorted.sort_custom(func(a, b): return a.naturalnocasecmp_to(b) < 0)
	check(n.size() > 20 and n == sorted, "%d skirmish maps by name" % n.size())
	check(page._filtered.all(func(m): return m["type"] == "skirmish"), "only skirmishes")
	check(ids(page).has("acropolis_bay") and not ids(page).has("arcadia"), "Acropolis Bay is one, the Arcadia scenario isn't")
	check(ids(page).has("sahel") and not ids(page).has("sahel_scenario"), "Sahel's skirmish is one, its scenario isn't")

	print("2. Map Filter")
	check(filter_titles(page) == ["Default", "Naval Maps", "Demo Maps", "Trigger Maps", "All Maps"], "a skirmish's filters (Demo Maps since the test maps came over): " + str(filter_titles(page)))
	check(not ids(page).has("cycladic_archipelago"), "Default hides naval maps")
	set_filter(page, "Naval Maps")
	check(ids(page).has("cycladic_archipelago") and ids(page).has("nile_river") and ids(page).has("yangtze") and ids(page).has("sporades_islands") and ids(page).size() == 5, "Naval Maps: " + str(ids(page)))
	set_filter(page, "Default")

	print("3. Random maps")
	page._on_map_type_selected(1)
	await frames(2)
	n = names(page)
	check(n.size() > 50 and n[0] == "Random", "Random comes first: " + str(n.slice(0, 3)))
	check(not n.any(func(s): return s.contains("(RMS)")), "no (RMS) suffix")
	check(n.has("Mainland") and not n.has("Aegean Sea"), "Default lists Mainland, not the naval Aegean Sea")
	check(filter_titles(page).has("Best for MP") and filter_titles(page).has("Naval Maps"), "random filters: " + str(filter_titles(page)))
	var spin: SpinBox = page.get_node("VBox/HBox/Right/SlotsBox/SlotCountRow/SpinSlots")
	check(spin.editable, "the player count can be picked")
	print("3b. Player Placement")
	var tp_slot: Control = node(page, "MapOptionsRow/TeamPlacementSlot")
	set_filter(page, "All Maps")
	select_map(page, "random_mainland")
	await frames(2)
	var tpl: RtsoSafeSelect = tp_slot.get_child(0)
	var items := func(): return range(tpl.get_item_count()).map(func(i): return tpl.get_item_id(i))
	if Setup.PLACEMENT_EVERY_MAP:
		check(tp_slot.is_visible_in_tree() and items.call() == [0, 1, 5, 2, 3, 4], "Mainland: Random, Circle, River, Grouped Lines, Random Group, Stronghold: " + str(items.call()))
		check(node(page, "MapOptionsRow/TeamPlacementLabel").text == "Player Placement", "titled Player Placement")
		tpl.select(2)
		page._on_team_placement_selected(2)
		check(tpl.text == "River" and tpl.tooltip_text == "Allied players are placed on two parallel lines.", "River, with 0.28's description: " + tpl.tooltip_text)
		select_map(page, "random_lake")
		await frames(2)
		check(items.call() == [0, 1, 5] and tpl.text == "River", "Lake offers Circle and River, River stays: " + tpl.text)
		select_map(page, "random_cantabrian_highlands")
		await frames(2)
		check(items.call() == [0, 1, 4] and tpl.text == "Random", "Cantabrian Highlands has no River: " + tpl.text)
		# 0.28's new maps from F's #483 (foothills.json: circle, river, randomGroup, stronghold).
		check(select_map(page, "random_foothills") and select_map(page, "random_coast_range") and select_map(page, "random_continent"), "Continent, Foothills and Coast Range are listed")
		select_map(page, "random_foothills")
		await frames(2)
		check(items.call() == [0, 1, 5, 3, 4], "Foothills: Random, Circle, River, Random Group, Stronghold: " + str(items.call()))
		print("3c. Biome follows 0.28's SupportedBiomes")
		var bio_slot: Control = page.get_node("%BiomeSlot")
		var bio: RtsoSafeSelect = bio_slot.get_child(0)
		var bio_ids := func(): return range(bio.get_item_count()).map(func(i): return bio.get_item_id(i))
		select_map(page, "random_african_plains")
		await frames(2)
		check(bio_slot.is_visible_in_tree() and bio_ids.call() == [page.RANDOM_BIOME, 4, 7, 11], "African Plains: Random, Oasis, Savanna, Nubia: " + str(bio_ids.call()))
		check(bio.text == "Random" and page._collect_setup()["biome"] == page.RANDOM_BIOME, "Random is the default: " + bio.text)
		check(bio.get_item_tooltip(0) == "Pick a biome at random." and bio.get_item_tooltip(2).begins_with("The transition between the dry Sahara"), "each entry has its 0.28 description")
		bio.select(2)
		page._on_biome_selected(2)
		check(page._collect_setup()["biome"] == 7, "Savanna goes to the match")
		select_map(page, "random_mainland")
		await frames(2)
		check(bio_ids.call().has(7) and bio.text == "Savanna", "Mainland offers Savanna too and keeps it: " + bio.text)
		print("3d. 0.28's Last Man Standing, Nomad and Hero Garrison rules")
		var lock: CheckBox = page.get_node("%LockTeams")
		var lms: CheckBox = page.get_node("%LastManStanding")
		var hg: CheckBox = page.get_node("%RegicideGarrison")
		check(page.get_node("%Nomad").visible, "Nomad shows on a random map")
		lms.button_pressed = true
		lock.button_pressed = true
		check(lms.disabled and not page._collect_setup()["lms"], "Locked Teams disables Last Man Standing")
		lock.button_pressed = false
		check(not lms.disabled and page._collect_setup()["lms"], "unlocked teams bring it back")
		lms.button_pressed = false
		var vm: int = page._victory_mask()
		page._set_victory_mask(vm | page.VK_REGICIDE)
		check(hg.visible, "Hero Garrison shows with Regicide")
		page._set_victory_mask(vm & ~page.VK_REGICIDE)
		check(not hg.visible, "and hides without it")
		select_map(page, "random_archipelago")
		await frames(2)
		check(not bio_slot.is_visible_in_tree() and page._collect_setup()["biome"] == 0, "RTS0's own Archipelago has no Biome")
		check(not tp_slot.is_visible_in_tree(), "RTS0's own Archipelago has no placement")
		select_map(page, "random_lake")
		await frames(2)
		check(tpl.text == "River", "River comes back on Lake")
	else:
		# Until F's #429: only Ambush and Frontier, without River.
		check(not tp_slot.is_visible_in_tree(), "Mainland has no Player Placement yet")
		select_map(page, "random_ambush")
		await frames(2)
		check(tp_slot.is_visible_in_tree() and items.call().size() > 1 and not items.call().has(5), "Ambush offers its patterns without River: " + str(items.call()))
		check(node(page, "MapOptionsRow/TeamPlacementLabel").text == "Player Placement", "titled Player Placement")
	set_filter(page, "Default")
	select_map(page, "random_mainland")
	spin.value = 3
	await frames(2)
	# 0.28 MapSize: random maps only, Normal (256) by default; WonderDuration and the relic
	# sliders only while their condition is checked.
	var msz: RtsoSafeSelect = node(page, "MapSizeSlot").get_child(0)
	check(node(page, "MapSizeSlot").visible and msz.text == "Normal (256 m)", "map size Normal (256 m): " + msz.text)
	var vt: Control = node(page, "VictoryTimesRow")
	page._set_victory_mask(1)
	check(not vt.visible, "no wonder or relic settings under Conquest")
	page._set_victory_mask(1 | 16)
	check(vt.visible and vt.get_node("WonderTimeSlot").visible and not vt.get_node("RelicCountSlot").visible, "Wonder shows its duration only")
	page._set_victory_mask(32)
	check(vt.visible and not vt.get_node("WonderTimeSlot").visible and vt.get_node("RelicCountSlot").visible and vt.get_node("RelicTimeSlot").visible, "Capture the Relic shows its count and duration")
	page._set_victory_mask(1)
	var civs: RtsoSafeSelect = slot_selects(page)[2]["civ"]
	check(civs.get_selected() == 0 and civs.text == "Random", "a new slot is Random, listed first: " + civs.text)
	var civ_names := []
	for i in range(1, civs.get_item_count()):
		civs.select(i)
		civ_names.append(civs.text)
	civs.select(0)
	var sorted_names := civ_names.duplicate()
	sorted_names.sort_custom(func(x, y): return x.naturalnocasecmp_to(y) < 0)
	check(civ_names == sorted_names, "then the civs by name")

	print("4. Scenario locks")
	page._on_map_type_selected(2)
	await frames(2)
	check(not ids(page).has("sandbox_athenians") and not ids(page).has("the_persian_gates"), "Default hides demo and hidden scenarios")
	check(select_map(page, "laconia"), "Laconia")
	await frames(2)
	check(int(spin.value) == 2 and not spin.editable, "two players, fixed")
	check(not node(page, "MapSizeSlot").visible, "no map size on a scenario")
	var rows := slot_selects(page)
	check(rows.size() == 2 and rows[0]["civ"].text == "Spartans" and rows[1]["civ"].text == "Macedonians", "the map's civs")
	check(rows.size() == 2 and rows[0]["civ"].disabled and rows[1]["civ"].disabled, "locked")
	check(rows.size() == 2 and rows[0]["team"].text == "T1" and rows[1]["team"].text == "T2" and rows[0]["team"].disabled, "the map's teams, locked")
	check(rows.size() == 2 and rows[0]["color"].modulate.a == 0.0, "no colour picker")
	var res: RtsoSafeSelect = node(page, "ResSlot").get_child(0)
	var pop: RtsoSafeSelect = node(page, "PopCapSlot").get_child(0)
	var cf: RtsoSafeSelect = node(page, "CeasefireSlot").get_child(0)
	check(res.disabled and res.text == "Per Player", "starting resources Per Player: " + res.text)
	check(pop.disabled and cf.disabled and cf.text == "No ceasefire", "population and ceasefire locked")
	for path in ["LastManStanding", "StartRow/LockTeams", "StartRow/DisableTreasures", "MapVisRow/RevealMap", "MapVisRow/ExploreMap", "MapVisRow/AlliedView", "MapVisRow/DisableSpies"]:
		check(node(page, path).disabled, path + " locked")
	check(not page.get_node("%Nomad").visible and not page.get_node("%RegicideGarrison").visible, "no Nomad or Hero Garrison on a scenario")
	check(select_map(page, "sahel_scenario"), "the Sahel scenario")
	await frames(2)
	rows = slot_selects(page)
	check(rows.map(func(r): return r["civ"].text) == ["Romans", "Carthaginians", "Persians", "Iberians"], "its civs: " + str(rows.map(func(r): return r["civ"].text)))
	res = node(page, "ResSlot").get_child(0)
	check(res.disabled and res.text == "Per Player", "its starting resources Per Player: " + res.text)
	check(select_map(page, "saharan_oases_scenario"), "the Saharan Oases scenario")
	await frames(2)
	rows = slot_selects(page)
	check(rows.map(func(r): return r["civ"].text) == ["Iberians", "Persians", "Britons", "Carthaginians"], "its civs: " + str(rows.map(func(r): return r["civ"].text)))
	set_filter(page, "All Maps")
	check(select_map(page, "the_persian_gates"), "All Maps lists The Persian Gates")
	await frames(2)
	rows = slot_selects(page)
	check(rows.map(func(r): return r["team"].text) == ["T1", "T1", "T2", "T2"], "its teams: " + str(rows.map(func(r): return r["team"].text)))
	check(node(page, "StartRow/LockTeams").button_pressed, "and its locked teams")
	check(select_map(page, "miletus"), "Miletus")
	await frames(2)
	pop = node(page, "PopCapSlot").get_child(0)
	check(pop.text == "Per Player" and pop.tooltip_text.contains("Player 1: 1000"), "its per-player population limit: " + pop.tooltip_text.replace("\n", " | "))
	check(select_map(page, "the_persian_gates"), "back to The Persian Gates")
	await frames(2)
	page._on_start_pressed()
	check(ms.map_id == "the_persian_gates" and ms.slot_civs == [5, 5, 2, 2] and ms.lock_teams, "Start sends its civs: %s %s" % [ms.map_id, str(ms.slot_civs)])
	await frames(4)

	print("5. Back to a skirmish")
	change_scene_to_file("res://scenes/menu/gamesetup.tscn")
	await frames(6)
	page = current_scene
	check(page._map_type == "scenario" and str(page._selected_map().get("id")) == "the_persian_gates", "the last map shows again, with its type and filter")
	page._on_map_type_selected(0)
	await frames(2)
	rows = slot_selects(page)
	check(rows.size() > 0 and not rows[0]["civ"].disabled and not rows[0]["team"].disabled and rows[0]["color"].modulate.a == 1.0, "civs, teams and colours are free")
	check(rows.all(func(r): return r["team"].text == "None"), "the scenario's teams are gone")
	res = node(page, "ResSlot").get_child(0)
	check(not res.disabled and res.text != "Per Player" and not node(page, "MapVisRow/RevealMap").disabled, "and the match settings")
	spin = page.get_node("VBox/HBox/Right/SlotsBox/SlotCountRow/SpinSlots")
	check(not spin.editable, "a skirmish keeps its player count")

	print("6. Start on Random")
	page._on_map_type_selected(1)
	await frames(2)
	check(str(page._selected_map().get("id")) == "random", "Random is selected first")
	page._on_start_pressed()
	check(ms.map_id.begins_with("random_"), "Start picks a random map: " + ms.map_id)
	await frames(4)

	cfg.set_value("persistmatchsettings", saved_persist)
	print("FAILURES: ", failures)
	quit(1 if failures else 0)
