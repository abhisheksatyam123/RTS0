extends SceneTree
# unit_actions.js "garrison" takes any own or allied GarrisonHolder, not only ships:
# infantry hovering their own ram or siege tower with session.garrison (Ctrl) held see
# the garrison cursor with "Current garrison: x/y", a Ctrl right click puts them inside,
# and so does the armed Garrison button's left click. Without Ctrl "move" (specificness
# 12) beats "garrison" (20): no garrison cursor.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_garrison_siege.gd
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

func key(code: int, down: bool) -> void:
	var k := InputEventKey.new()
	k.keycode = code
	k.physical_keycode = code
	k.ctrl_pressed = down and code == KEY_CTRL
	k.pressed = down
	Input.parse_input_event(k)
	await frames(1)

func click(button: int, pos: Vector2) -> void:
	await move(pos)
	for down in [true, false]:
		var mb := InputEventMouseButton.new()
		mb.button_index = button
		mb.pressed = down
		mb.position = pos
		mb.global_position = pos
		root.push_input(mb, true)
		await frames(1)
	await frames(2)

func inside(id: int) -> bool:
	return main.debug_unit_screen(id) == Vector2(-1, -1)

func wait_inside(ids: Array) -> bool:
	for i in 300:
		var all := true
		for id in ids:
			all = all and inside(id)
		if all:
			return true
		await frames(1)
	return false

func run() -> void:
	await frames(60)
	var r: Dictionary = main.debug_civic_centre_roof()
	for i in 200:
		await frames(1)
		var r2: Dictionary = main.debug_civic_centre_roof()
		var still: bool = (r2["roof"] as Vector2).distance_to(r["roof"]) < 0.5
		r = r2
		if still and i > 10:
			break
	var ram := int(main.debug_spawn_units("athen/siege_ram", 1)[0])
	var men: PackedInt32Array = main.debug_spawn_units("athen/infantry_spearman_b", 2)
	await frames(10)

	print("1. Hover and right click a ram")
	main.debug_select_units(men)
	await frames(2)
	var at: Vector2 = main.debug_unit_screen(ram)
	await move(at)
	await frames(6)
	check(main.debug_cursor_tip()["cursor"] != "action-garrison", "no garrison cursor over the ram without Ctrl (move wins)")
	await key(KEY_CTRL, true)
	# The hover pick and its tooltip settle over a few frames (the ram may still be easing
	# out of the spearmen's way: follow it).
	var t: Dictionary = main.debug_cursor_tip()
	for i in 40:
		at = main.debug_unit_screen(ram)
		await move(at)
		t = main.debug_cursor_tip()
		if t["visible"] and t["cursor"] == "action-garrison":
			break
	print("    ", t)
	check(t["cursor"] == "action-garrison", "the garrison cursor over the ram")
	check(t["visible"] and str(t["text"]).begins_with("Current garrison: 0/"), "Current garrison: 0/capacity")
	await click(MOUSE_BUTTON_RIGHT, at)
	await key(KEY_CTRL, false)
	check(await wait_inside(Array(men)), "both spearmen went into the ram")

	print("2. The armed Garrison button's left click on a siege tower")
	var tower := int(main.debug_spawn_units("han/siege_tower", 1)[0])
	var man := int(main.debug_spawn_units("athen/infantry_spearman_b", 1)[0])
	await frames(10)
	main.debug_select_units(PackedInt32Array([man]))
	main._on_entity_command("garrison")
	at = main.debug_unit_screen(tower)
	await click(MOUSE_BUTTON_LEFT, at)
	check(await wait_inside([man]), "the spearman went inside")
	var probe := int(main.debug_spawn_units("athen/infantry_spearman_b", 1)[0])
	await frames(4)
	main.debug_select_units(PackedInt32Array([probe]))
	await key(KEY_CTRL, true)
	await move(main.debug_unit_screen(ram))
	var rt: Dictionary = main.debug_cursor_tip()
	print("    ram ", rt["text"])
	check(str(rt["text"]) == "Current garrison: 3/10", "it is in the holder under the cursor (the ram beside the tower)")
	await key(KEY_CTRL, false)

	print("3. A ram is not a holder for another ram")
	var ram2 := int(main.debug_spawn_units("athen/siege_ram", 1)[0])
	await frames(10)
	main.debug_select_units(PackedInt32Array([ram2]))
	await frames(2)
	await key(KEY_CTRL, true)
	await move(main.debug_unit_screen(ram))
	check(main.debug_cursor_tip()["cursor"] != "action-garrison", "no garrison cursor for a ram over a ram")
	await key(KEY_CTRL, false)

	print("FAILURES: ", failures)
	quit(1 if failures else 0)
