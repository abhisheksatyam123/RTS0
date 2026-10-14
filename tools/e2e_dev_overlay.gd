extends SceneTree
# 0.28's developer overlay (gui/session/developer_overlay): Alt+D opens the panel
# and says so in the chat; Display selection state shows the first selected
# entity's state; Reveal map reveals and hides the map again ("reveal-map");
# Restrict camera lets the camera leave the map when cleared; Promote selected
# units raises the selection a rank ("promote").
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_dev_overlay.gd
# Exit code 0 = pass.

var main
var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	# Commands.js's "reveal-map" and "promote" need cheats (GetCheatsEnabled), as single player has them.
	root.get_node("MatchSettings").cheats = true
	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func box(i: int) -> CheckBox:
	return main.find_child("DevCommand%d" % i, true, false)

func press(i: int) -> void:
	var b := box(i)
	b.set_pressed_no_signal(not b.button_pressed) # what a click does: toggle, then "pressed"
	b.pressed.emit()

func run() -> void:
	await frames(60)
	var rig = main.find_children("*", "RtsoCameraRig", true, false)[0]
	rig.set_edge_scroll_enabled(false)
	check(not main.debug_dev_overlay()["open"], "closed at the start")
	main.toggle_dev_overlay()
	await frames(2)
	var d: Dictionary = main.debug_dev_overlay()
	check(d["open"] and main.find_child("DevCommandsOverlay", true, false).visible, "Alt+D opens it")
	check(d["restrict"] and not d["reveal"], "the camera is restricted and the map not revealed")
	check(main.debug_dev_overlay()["chat_log"].contains("The Developer Overlay was opened."), "the chat says it was opened")

	main.select_first_own(false)
	press(0)
	await frames(2)
	d = main.debug_dev_overlay()
	check(d["state_shown"] and str(d["state"]).contains("entity: {") and str(d["state"]).contains("hitpoints:"),
		"Display selection state shows the selected entity")

	press(1)
	await frames(3)
	check(main.debug_dev_overlay()["map_revealed"], "Reveal map reveals it")
	press(1)
	await frames(3)
	check(not main.debug_dev_overlay()["map_revealed"], "and clearing it hides it again")

	press(2)
	await frames(1)
	rig.set_target_pos(Vector3(100000, 0, 100000))
	check(float(main.debug_dev_overlay()["camera_x"]) > 10000.0, "unrestricted, the camera may leave the map")
	press(2)
	await frames(1)
	rig.set_target_pos(Vector3(100000, 0, 100000))
	check(float(main.debug_dev_overlay()["camera_x"]) < 10000.0, "restricted again, it stays on the map")

	main.debug_selection_grid(true) # every own unit: the citizen soldiers rise a rank, women don't
	var before: int = int(main.debug_dev_overlay()["selection_ranks"])
	press(3)
	await frames(3)
	var after: int = int(main.debug_dev_overlay()["selection_ranks"])
	check(after > before, "Promote selected units raises their ranks: %d -> %d" % [before, after])
	check(not box(3).button_pressed, "its box doesn't stay ticked")

	main.toggle_dev_overlay()
	await frames(1)
	check(not main.debug_dev_overlay()["open"] and main.debug_dev_overlay()["chat_log"].contains("The Developer Overlay was closed."), "Alt+D closes it")
	print("RESULT: %s" % ("PASS" if failures == 0 else "FAIL (%d)" % failures))
	quit(0 if failures == 0 else 1)
