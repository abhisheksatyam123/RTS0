extends SceneTree
# input.js performGroup / EntityGroupsContainer: a control group holds structures too,
# so Ctrl+2 on the Civic Center and then 2 selects it again; a group whose units are
# all inside a structure keeps them, and selecting it leaves the selection as it is.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_control_group_structures.gd
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

func key(code: Key, ctrl := false) -> void:
	for down in [true, false]:
		var k := InputEventKey.new()
		k.keycode = code
		k.physical_keycode = code
		k.pressed = down
		k.ctrl_pressed = ctrl
		Input.parse_input_event(k)
		await frames(1)
	await frames(12) # past the double-press window

func run() -> void:
	await frames(60)
	var cc: int = main.debug_civic_centre_id(0)
	main.debug_select_entity(cc)
	await frames(2)
	await key(KEY_2, true)
	main.clear_selection()
	await frames(2)
	await key(KEY_2)
	check(main.debug_selection_state()["building"] == cc, "2 selects the Civic Center saved with Ctrl+2")

	var men: PackedInt32Array = main.debug_spawn_units("athen/infantry_spearman_b", 1)
	main.debug_select_units(men)
	await frames(2)
	await key(KEY_3, true)
	main.debug_put_inside(men, cc)
	main.debug_select_entity(cc)
	await frames(4)
	await key(KEY_3)
	check(main.debug_selection_state()["building"] == cc, "3, all inside: the selection stays")
	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
