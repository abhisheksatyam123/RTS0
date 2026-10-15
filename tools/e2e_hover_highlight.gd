extends SceneTree
# input.js setHighlightList: hovering a unit rings it at g_HighlightedAlpha, and while
# band-boxing the units the box would pick are ringed before the button is released.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_hover_highlight.gd
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

func move(pos: Vector2) -> void:
	var mm := InputEventMouseMotion.new()
	mm.position = root.get_final_transform() * pos
	mm.global_position = mm.position
	Input.parse_input_event(mm)
	await frames(2)

func button(pos: Vector2, down: bool) -> void:
	var mb := InputEventMouseButton.new()
	mb.button_index = MOUSE_BUTTON_LEFT
	mb.pressed = down
	mb.position = pos
	mb.global_position = pos
	root.push_input(mb, true)
	await frames(2)

func key(code: Key, down: bool) -> void:
	var k := InputEventKey.new()
	k.keycode = code
	k.physical_keycode = code
	k.pressed = down
	Input.parse_input_event(k)
	await frames(2)

func right(pos: Vector2, down: bool) -> void:
	var mb := InputEventMouseButton.new()
	mb.button_index = MOUSE_BUTTON_RIGHT
	mb.pressed = down
	mb.position = pos
	mb.global_position = pos
	root.push_input(mb, true)
	await frames(2)

func run() -> void:
	await frames(60)
	await move(Vector2(512, 400))
	var r: Dictionary = main.debug_civic_centre_roof()
	for i in 200:
		await frames(1)
		var r2: Dictionary = main.debug_civic_centre_roof()
		var still: bool = (r2["roof"] as Vector2).distance_to(r["roof"]) < 0.5
		r = r2
		if still and i > 10:
			break
	var men: PackedInt32Array = main.debug_spawn_units("athen/infantry_spearman_b", 1)
	main.debug_select_units(men)
	await frames(4)
	var units: Array = main.debug_selection_state()["units"]
	var at := Vector2.ZERO
	for u in units:
		if int(u["id"]) == men[0]:
			at = u["screen"]
	main.debug_select_units(PackedInt32Array())
	await frames(2)
	await move(at)
	var hl: PackedInt32Array = main.debug_highlight()
	print("    hover ", hl, " unit ", men[0])
	check(hl.size() == 1 and hl[0] == men[0], "hovering the unit highlights it")
	await move(Vector2(40, 300))
	check(main.debug_highlight().size() == 0, "moving off clears the highlight")
	# Band box over the unit: highlighted while dragging, selected on release.
	var from := at - Vector2(30, 30)
	await move(from)
	await button(from, true)
	await move(at + Vector2(30, 30))
	hl = main.debug_highlight()
	print("    box ", hl)
	check(hl.has(men[0]), "the band box highlights the unit it will pick")
	check(main.debug_selection_grid(false)["selected"] == 0, "not selected before release")
	await button(at + Vector2(30, 30), false)
	check(main.debug_selection_grid(false)["selected"] >= 1, "selected on release")
	# session.highlightguarding / highlightguarded (PageDown / PageUp): ring the units
	# guarding the selection, or what the selection guards.
	var guard: PackedInt32Array = main.debug_spawn_units("athen/infantry_spearman_b", 1)
	main.debug_select_units(guard)
	await frames(4)
	await key(KEY_G, true)
	await right(at, true)
	await right(at, false)
	await key(KEY_G, false)
	await frames(10)
	check(main.debug_guard_target(guard[0]) == men[0], "G+right-click: the new unit guards the first")
	await move(Vector2(40, 300))
	main.debug_select_units(men)
	await frames(2)
	await key(KEY_PAGEDOWN, true)
	hl = main.debug_highlight()
	print("    guarding ", hl, " guard ", guard[0])
	check(hl.has(guard[0]), "PageDown rings the unit guarding the selection")
	await key(KEY_PAGEDOWN, false)
	check(not main.debug_highlight().has(guard[0]), "released, the ring goes")
	main.debug_select_units(guard)
	await frames(2)
	await key(KEY_PAGEUP, true)
	hl = main.debug_highlight()
	print("    guarded ", hl)
	check(hl.has(men[0]), "PageUp rings the unit the selection guards")
	await key(KEY_PAGEUP, false)
	print("FAILURES: ", failures)
	quit(1 if failures > 0 else 0)
