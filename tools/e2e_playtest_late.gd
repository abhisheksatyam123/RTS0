extends SceneTree
# What Rendering's 2026-10-13 late-game mouse run found, as 0 A.D. 0.28
# gui/session shows it:
#   1. idle-unit cycling (input.js findIdleUnit, GuiInterface FindIdleUnits):
#      the idle-worker hotkey walks g_WorkerTypes ("Civilian", "Trader",
#      "FishingBoat", "Citizen") in order: every idle civilian first by id
#      (traders are civilians too), then the citizens by id, then starts over;
#   2. technology names and tooltips read "−" as 0.28 writes it, not "â";
#   3. the production queue shows only queued items, so an idle dock or Civic
#      Centre shows no empty slots;
#   4. no touch-screen button bar covers the session with a mouse.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_playtest_late.gd
# Exit code 0 = pass.

var main
var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func press(code: Key) -> void:
	for down in [true, false]:
		var k := InputEventKey.new()
		k.keycode = code
		k.physical_keycode = code
		k.pressed = down
		Input.parse_input_event(k)
		await frames(1)

func run() -> void:
	var mm := InputEventMouseMotion.new()
	mm.position = Vector2(32, 26)
	Input.parse_input_event(mm)
	await frames(60)

	print("1. Idle workers, civilians first")
	# The skirmish starts with RTS0 villagers (Citizen): add civilians and a trader.
	var females: Array = Array(main.debug_spawn_units("athen/support_civilian", 2))
	var traders: Array = Array(main.debug_spawn_units("athen/support_trader", 1))
	await frames(4)
	check(females.size() == 2 and traders.size() == 1, "spawned %s and %s" % [str(females), str(traders)])
	var units: Array = main.debug_selection_state()["units"]
	var idle: Array = units.filter(func(u): return u["idle"]).map(func(u): return int(u["id"]))
	check(females.all(func(id): return idle.has(id)), "the civilians are idle")
	var picks: Array = []
	for i in range(units.size() + 1):
		await press(KEY_PERIOD)
		var sel: Array = main.debug_selection_state()["selected"]
		if sel.size() != 1:
			break
		var id := int(sel[0])
		picks.append(id)
		if picks.count(id) > 1:
			break
	print("  picks: ", picks)
	# The match's own start civilians (0.28 start entities) come in that first block too.
	var civ_end := picks.find(traders[0]) + 1 if traders.size() > 0 else 0
	var civs := picks.slice(0, civ_end)
	var sorted_civs := civs.duplicate()
	sorted_civs.sort()
	check(civ_end >= 3 and civs == sorted_civs and females.all(func(id): return civs.has(id)), "the civilians first, by id, the trader among them: " + str(civs))
	var rest := picks.slice(civ_end, picks.size() - 1)
	var sorted_rest := rest.duplicate()
	sorted_rest.sort()
	check(rest.size() > 0 and rest == sorted_rest and not rest.any(func(id): return females.has(id) or traders.has(id)), "then the citizens by id: " + str(rest))
	check(picks.size() > 3 and picks[-1] == picks[0], "then the first civilian again")

	print("2. Technology text")
	main.debug_train_buttons(11)  # a dock
	await frames(2)
	var row: Array = main.debug_command_row()
	var techs: Array = row.filter(func(e): return str(e["tip"]).length() > 0)
	var mangled: Array = row.filter(func(e): return str(e["text"]).contains("â") or str(e["tip"]).contains("â"))
	check(techs.size() > 0 and mangled.is_empty(), "no \"â\" in %d buttons: %s" % [row.size(), str(mangled.map(func(e): return e["text"]))])
	check(row.any(func(e): return str(e["tip"]).contains("−")), "the dock's techs read \"−\"")

	print("3. Queue slots")
	var slots: Array = main.find_children("*", "Panel", true, false).filter(func(p): return p.custom_minimum_size == Vector2(32, 36))
	check(slots.size() >= 16, "%d queue slots built" % slots.size())
	check(not slots.any(func(p): return p.is_visible_in_tree()), "none shows while nothing is queued")

	print("4. Touch bar")
	var bar: Control = root.find_child("RightBar", true, false)
	check(bar == null or not bar.is_visible_in_tree(), "no touch-screen bar with a mouse")

	print("FAILURES: ", failures)
	quit(1 if failures else 0)
