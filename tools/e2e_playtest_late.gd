extends SceneTree
# What Rendering's 2026-10-13 late-game mouse run found, as 0 A.D. 0.28
# gui/session shows it:
#   1. idle-unit cycling (input.js findIdleUnit, GuiInterface FindIdleUnits):
#      the idle-worker hotkey walks g_WorkerTypes in order, every idle female
#      citizen first by id, then the other workers by id, then starts over;
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

	print("1. Idle workers, female citizens first")
	var units: Array = main.debug_selection_state()["units"]
	var female := {}
	for u in units:
		female[int(u["id"])] = u["female"]
	var idle_females: Array = units.filter(func(u): return u["idle"] and u["female"]).map(func(u): return int(u["id"]))
	idle_females.sort()
	check(idle_females.size() >= 2, "%d idle female citizens" % idle_females.size())
	var picks: Array = []
	for i in range(units.size() + 1):
		await press(KEY_PERIOD)
		var sel: Array = main.debug_selection_state()["selected"]
		if sel.size() != 1:
			break
		var id := int(sel[0])
		if picks.has(id):
			picks.append(id)
			break
		picks.append(id)
	print("  picks: ", picks)
	var first := picks.slice(0, idle_females.size())
	check(first == idle_females, "the female citizens by id: " + str(first))
	var rest := picks.slice(idle_females.size(), picks.size() - 1)
	var sorted_rest := rest.duplicate()
	sorted_rest.sort()
	check(rest.size() > 0 and rest == sorted_rest and not rest.any(func(id): return female[id]), "then the other workers by id: " + str(rest))
	check(picks.size() > 1 and picks[-1] == idle_females[0], "then the first female citizen again")

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
