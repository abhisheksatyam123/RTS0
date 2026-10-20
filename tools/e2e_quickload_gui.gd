extends SceneTree
# session.js getSavedGameData / restoreSavedGameData with 0.28's CTurnManager::QuickSave: the
# quicksave is kept for this match only, out of the save and load dialogs, and keeps the control
# groups but not the camera. Quickloading restores the groups, leaves the camera where it is and
# empties the selection, so a group key after it picks what the group held at the quicksave.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_quickload_gui.gd
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

func key(code: int, ctrl: bool) -> void:
	for down in [true, false]:
		var k := InputEventKey.new()
		k.keycode = code
		k.physical_keycode = code
		k.ctrl_pressed = ctrl
		k.pressed = down
		Input.parse_input_event(k)
		await frames(1)

func group(n: int) -> Array:
	return Array(main.debug_saved_gui()["groups"][n])

func run() -> void:
	await frames(60)
	# Keep the mouse off the screen edges, or edge scrolling moves the camera.
	var mm := InputEventMouseMotion.new()
	mm.position = root.get_final_transform() * (root.get_visible_rect().size / 2)
	mm.global_position = mm.position
	Input.parse_input_event(mm)
	var men: PackedInt32Array = main.debug_spawn_units("athen/infantry_spearman_b", 4)
	await frames(5)
	var a := int(men[0])
	var b := int(men[1])

	print("1. Save with a group and a camera")
	main.debug_select_units(PackedInt32Array([a]))
	await key(KEY_1, true)
	check(group(1) == [a], "group 1 holds the first man: " + str(group(1)))
	main.debug_set_camera_data({"x": 12.0, "z": -7.0, "yaw": 0.5, "zoom": 30.0})
	await frames(3)
	var log_label: RichTextLabel = main.find_child("LogMessages", true, false)
	main.quickload()
	check(log_label != null and log_label.visible and log_label.get_parsed_text() == "Cannot quickload game - no game was quicksaved", "quickloading before any quicksave says there is none")
	main.quicksave()
	check(not FileAccess.file_exists("user://saves/quicksave.rts0sav") and not FileAccess.file_exists("user://saves/quicksave.json"), "the quicksave is not a save the save and load dialogs list")
	check(log_label.get_parsed_text().ends_with("\nQuicksaved game"), "\"Quicksaved game\" in the top-left log (LOGMESSAGERENDER)")

	print("2. Change both, then quickload")
	main.debug_select_units(PackedInt32Array([b]))
	await key(KEY_1, true)
	check(group(1) == [b], "group 1 now holds the second man")
	main.debug_set_camera_data({"x": -20.0, "z": 15.0, "yaw": 1.5, "zoom": 18.0})
	await frames(3)
	main.quickload()
	await frames(3)
	check(group(1) == [a], "the load brings group 1 back: " + str(group(1)))
	check(log_label.get_parsed_text().ends_with("Quicksaved game\nQuickloaded game"), "then \"Quickloaded game\" under it")
	var cam: Dictionary = main.debug_saved_gui()["camera"]
	check(absf(cam["x"] + 20.0) < 0.5 and absf(cam["z"] - 15.0) < 0.5, "the camera stays where it was: " + str(cam))
	check(absf(cam["yaw"] - 1.5) < 0.01 and absf(cam["zoom"] - 18.0) < 0.01, "with its rotation and zoom")
	check(Array(main.debug_selection_state()["selected"]).is_empty(), "the selection is empty")
	await key(KEY_1, false)
	check(Array(main.debug_selection_state()["selected"]) == [a], "key 1 selects the saved group")

	root.remove_child(main)
	main.free()
	check(not FileAccess.file_exists("user://quicksave.rts0sav"), "leaving the match drops the quicksave")
	print("FAILURES: ", failures)
	quit(1 if failures else 0)
