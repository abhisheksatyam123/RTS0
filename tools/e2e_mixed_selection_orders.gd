extends SceneTree
# unit_actions.js: with units selected, Shift+click on an own structure keeps the units,
# and a right-click on the ground orders the units to walk there instead of setting the
# structure's rally point.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_mixed_selection_orders.gd
# Exit code 0 = pass.

var main
var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	# One map for every run: the random default's coast or trees can stand where the units walk.
	var ms = root.get_node("MatchSettings")
	ms.map_id = "random_continental"
	ms.seed = 11
	ms.map_size = 128
	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

# Waits on simulation turns, not frames, so a slow frame rate cannot cut the walk short.
func turns(n: int) -> void:
	var until: int = int(main.get_replay_status().turn) + n
	var guard := 0
	while int(main.get_replay_status().turn) < until and guard < n * 40:
		await process_frame
		guard += 1

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
	k.shift_pressed = down and code == KEY_SHIFT
	k.pressed = down
	Input.parse_input_event(k)
	await frames(1)

func click(pos: Vector2, button: int) -> void:
	await move(pos)
	for down in [true, false]:
		var mb := InputEventMouseButton.new()
		mb.button_index = button
		mb.pressed = down
		mb.position = pos
		mb.global_position = pos
		mb.shift_pressed = Input.is_key_pressed(KEY_SHIFT)
		root.push_input(mb, true)
		await frames(1)
	await frames(2)

func unit(state: Dictionary, id: int) -> Dictionary:
	for u in state["units"]:
		if u["id"] == id:
			return u
	return {}

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
	var cc: int = main.debug_civic_centre_id(0)
	main.debug_select_entity(cc)
	await frames(4)
	var at: Vector2 = main.debug_selected_building()["screen"]
	var had_rally: bool = main.debug_selected_building()["has_rally"]
	var men: PackedInt32Array = main.debug_spawn_units("athen/infantry_spearman_b", 2)
	main.debug_select_units(men)
	await frames(4)
	var before: Vector2 = unit(main.debug_selection_state(), men[0])["pos"]
	await key(KEY_SHIFT, true)
	await click(at, MOUSE_BUTTON_LEFT)
	await key(KEY_SHIFT, false)
	var s: Dictionary = main.debug_selection_state()
	check(s["building"] == cc and s["selected"].size() == 2, "Shift+click adds the Civic Center and keeps both units")
	await click(at + Vector2(0, 160), MOUSE_BUTTON_RIGHT)
	await turns(10)
	s = main.debug_selection_state()
	check(not s.has("rally") or had_rally, "no rally point set on the Civic Center")
	check((unit(s, men[0])["pos"] as Vector2).distance_to(before) > 1.0, "the units walk")
	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
