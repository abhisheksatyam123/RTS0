extends SceneTree
# The command row as 0 A.D. 0.28 gui/session/unit_actions.js g_EntityCommands
# builds it:
#   1. soldiers and citizens get Delete, Call to Arms, Garrison, Occupy Turret, Repair
#      and Guard, the row's six (Patrol has no room); civilians alone get no Call to
#      Arms, Guard or Patrol;
#   2. Abort shows while units have orders and stops them;
#   3. Delete asks first (DeleteSelectionConfirmation), Yes destroys, and
#      holding Shift (session.noconfirmation) skips the question;
#   4. a structure shows Delete, and Focus on Rally Point once it has one;
#   5. Garrison arms one right-click that garrisons.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_entity_commands.gd
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

# Waits until `cond` holds, by game frames under --fixed-fps 20 or by wall time
# without it (the sim steps on time, so frames alone may be too few).
func until(cond: Callable, seconds := 5.0) -> void:
	var end := Time.get_ticks_msec() + int(seconds * 1000.0)
	var n := 0
	while not cond.call() and (n < int(seconds * 20.0) or Time.get_ticks_msec() < end):
		await process_frame
		n += 1

func row() -> Array:
	return main.debug_command_row()

func texts() -> Array:
	return row().map(func(e): return str(e["text"]).get_slice(" (", 0))

func button(label: String) -> Dictionary:
	for e in row():
		if str(e["text"]).get_slice(" (", 0) == label:
			return e
	return {}

func press(label: String) -> bool:
	var e := button(label)
	if e.is_empty():
		return false
	instance_from_id(e["id"]).emit_signal("pressed")
	return true

func alive(id: int) -> bool:
	for u in main.debug_selection_state()["units"]:
		if int(u["id"]) == id:
			return true
	return false

func run() -> void:
	# The mouse rests mid-screen: at the corner it would scroll the camera.
	var mm := InputEventMouseMotion.new()
	mm.position = Vector2(32, 26)
	Input.parse_input_event(mm)
	await frames(60)

	print("1. Which commands show")
	main.debug_selection_grid(true)
	await frames(2)
	var t := texts()
	print("  everyone: ", t)
	for want in ["Delete", "Call to Arms", "Garrison", "Occupy Turret", "Repair", "Guard"]:
		check(t.has(want), want + " shows for the starting units")
	# selection_panels.js Command getMaxNumberOfItems: six, in g_EntityCommands order, so
	# Patrol (seventh here) has no room.
	check(not t.has("Patrol"), "the row stops at six: no room for Patrol")
	check(not t.has("Abort"), "no Abort while everyone is idle")
	check(not t.has("Abort Guard"), "no Abort Guard while nobody guards")
	# The tooltips are translate(title) + "\n" + translate(body).
	var de: Translation = load("res://assets/l10n/de.po")
	TranslationServer.add_translation(de)
	TranslationServer.set_locale("de")
	var guard_tip: String = button("Guard").get("tip", "")
	TranslationServer.set_locale("en")
	TranslationServer.remove_translation(de)
	check(guard_tip.ends_with("Bewachen\nAusgewählten Einheiten befehlen, ein Gebäude oder eine Einheit zu bewachen."), "Guard's tooltip is translated: " + guard_tip.replace("\n", " | "))
	# The first group icon is the villagers (e2e_command_panel narrows to them).
	var g: Dictionary = main.debug_selection_grid(false)
	instance_from_id(g["icons"][0]["id"]).emit_signal("pressed")
	await frames(2)
	t = texts()
	print("  villagers: ", t)
	check(t.has("Delete") and t.has("Garrison") and t.has("Repair"), "civilians can delete, garrison and repair")
	check(not t.has("Call to Arms") and not t.has("Guard") and not t.has("Patrol"), "but have no Call to Arms, Guard or Patrol")
	var tip: String = button("Delete").get("tip", "")
	check(tip.contains("Self-Destruct") and tip.contains("to avoid the confirmation dialog"), "Delete's tooltip is 0.28's: " + tip.replace("\n", " | "))

	print("2. Abort")
	main.debug_selection_grid(true)
	await frames(2)
	var st: Dictionary = main.debug_selection_state()
	var u0: Dictionary = st["units"][0]
	main._on_minimap_order(u0["pos"] + Vector2(30, 0))
	await until(func(): return texts().has("Abort"))
	check(texts().has("Abort"), "Abort shows while the units walk")
	check(press("Abort"), "pressing Abort")
	await until(func(): return not texts().has("Abort"))
	check(not texts().has("Abort"), "they stop, and Abort goes")

	print("3. Delete")
	main.clear_selection()
	main.debug_selection_grid(true)
	await frames(2)
	var sel: Array = main.debug_selection_state()["selected"]
	# Keep one unit: Ctrl-free narrowing through the group icon of the last group.
	g = main.debug_selection_grid(false)
	instance_from_id(g["icons"][g["icons"].size() - 1]["id"]).emit_signal("pressed")
	await frames(2)
	sel = main.debug_selection_state()["selected"]
	var victim := int(sel[0])
	check(press("Delete"), "pressing Delete")
	await frames(2)
	var QC = load("res://scenes/session/quit_confirmation.gd")
	var boxes: Array = main.get_children().filter(func(c): return c.get_script() == QC)
	var dlg = boxes[0] if boxes.size() == 1 else null
	check(dlg != null and paused, "a message box asks first, pausing the game")
	if dlg:
		check(dlg.caption == "Destroy everything currently selected?" and dlg.title == "Delete", "with 0.28's title and question")
		check(dlg.button_texts() == ["No", "Yes"], "and No / Yes")
	await frames(10)
	check(alive(victim), "nothing is destroyed before the answer")
	if dlg:
		dlg.press(1)
	check(not paused, "answering resumes it")
	await until(func(): return not alive(victim))
	check(not alive(victim), "Yes destroys the selection")
	main.debug_selection_grid(true)
	await frames(2)
	g = main.debug_selection_grid(false)
	instance_from_id(g["icons"][g["icons"].size() - 1]["id"]).emit_signal("pressed")
	await frames(2)
	sel = main.debug_selection_state()["selected"]
	var victim2 := int(sel[0])
	var k := InputEventKey.new()
	k.keycode = KEY_SHIFT
	k.physical_keycode = KEY_SHIFT
	k.shift_pressed = true
	k.pressed = true
	Input.parse_input_event(k)
	await frames(1)
	press("Delete")
	k = k.duplicate()
	k.pressed = false
	k.shift_pressed = false
	Input.parse_input_event(k)
	await frames(10)
	check(not alive(victim2), "Shift+Delete skips the question")

	print("4. A structure")
	main.select_first_own(true)
	await frames(2)
	t = texts()
	check(t.has("Delete") and not button("Delete")["disabled"], "a Civic Centre can be deleted: " + str(t))
	check(not t.has("Focus on Rally Point"), "no Focus on Rally Point without a rally point")
	main._on_minimap_order(u0["pos"] + Vector2(0, 20))
	await frames(12)
	check(texts().has("Focus on Rally Point"), "Focus on Rally Point once it has one")

	print("5. Garrison")
	var r: Dictionary = main.debug_civic_centre_roof()
	for i in 200:
		await frames(1)
		var r2: Dictionary = main.debug_civic_centre_roof()
		var still: bool = (r2["roof"] as Vector2).distance_to(r["roof"]) < 0.5
		r = r2
		if still and i > 10:
			break
	main.debug_selection_grid(true)
	await frames(2)
	g = main.debug_selection_grid(false)
	instance_from_id(g["icons"][0]["id"]).emit_signal("pressed")
	await frames(2)
	var n_vill: int = main.debug_selection_state()["selected"].size()
	check(press("Garrison"), "pressing Garrison")
	# input.js INPUT_PRESELECTEDACTION: the next left click on the foot of the Civic Centre runs it.
	var c: Vector2 = r["centre"]
	var at: Vector2 = root.get_camera_3d().unproject_position(Vector3(c.x, 0.5, c.y))
	var mb := InputEventMouseButton.new()
	mb.button_index = MOUSE_BUTTON_LEFT
	mb.position = at
	mb.global_position = at
	mb.pressed = true
	root.push_input(mb, true)
	await frames(1)
	mb = mb.duplicate()
	mb.pressed = false
	root.push_input(mb, true)
	await frames(200)
	main.select_first_own(true)
	await frames(2)
	var unload: Dictionary = button("Unload All")
	check(not unload.is_empty() and str(unload["text"]) == "Unload All (%d)" % n_vill, "the %d villagers went inside: %s" % [n_vill, str(unload.get("text"))])
	print("FAILURES: ", failures)
	quit(1 if failures else 0)
