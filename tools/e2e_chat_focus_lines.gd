extends SceneTree
# ChatMessageFormatSimulation.attack + ChatOverlay.js: "You have been attacked by X!" is a
# button in the chat overlay with the icon_focusattacked icon and the tooltip "Click to focus
# on the attacked unit."; a click runs focusAttack (the camera follows an attacked unit, or
# looks at an attacked structure's spot). Plain chat lines stay ghosts. An attacked domestic
# animal reads "Your livestock has been attacked by X!".
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_chat_focus_lines.gd
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

func last_line() -> Dictionary:
	var lines: Array = main.debug_chat_overlay()
	return lines[lines.size() - 1] if lines.size() > 0 else {}

func move(pos: Vector2) -> void:
	var mm := InputEventMouseMotion.new()
	mm.position = root.get_final_transform() * pos
	mm.global_position = mm.position
	Input.parse_input_event(mm)
	await frames(2)

func click(node: Control) -> void:
	var at: Vector2 = node.get_global_rect().get_center()
	await move(at)
	for down in [true, false]:
		var mb := InputEventMouseButton.new()
		mb.button_index = MOUSE_BUTTON_LEFT
		mb.pressed = down
		mb.position = at
		mb.global_position = at
		root.push_input(mb, true)
		await frames(1)
	await frames(2)

func run() -> void:
	await frames(60)
	await move(Vector2(512, 400)) # off the screen edges, which scroll the camera
	var man := int(main.debug_spawn_units("athen/infantry_spearman_b", 1)[0])
	await frames(4)
	var pos: Vector2 = main.debug_ground_at(main.debug_unit_screen(man))

	print("1. The attack line")
	main.notify_attack(Vector3(pos.x, 0, pos.y), 2, false, man)
	await frames(4)
	var l: Dictionary = last_line()
	print("    ", l.get("text"))
	check(str(l.get("text", "")).contains("You have been attacked by"), "the attack line is in the overlay")
	check(l.get("clickable", false), "it is a button")
	check(l.get("tooltip", "") == "Click to focus on the attacked unit.", "with 0.28's tooltip")
	await click(l["node"])
	check(int(main.debug_camera()["follow"]) == man, "a click makes the camera follow the attacked unit")

	print("2. An attacked structure: the camera looks at the spot")
	main.notify_attack(Vector3(pos.x + 40, 0, pos.y + 40), 2, true, 0)
	await frames(4)
	l = last_line()
	await click(l["node"])
	var cam: Dictionary = main.debug_camera()
	check(int(cam["follow"]) == 0 and (cam["target"] as Vector2).distance_to(pos + Vector2(40, 40)) < 1.0, "the camera moves to the attacked spot: " + str(cam))

	print("3. AttackDetection: every hit entity pings the minimap, alerted or suppressed")
	var before: int = main.debug_minimap_panel()["pings"]
	main.debug_attacked(man, 1)
	var other := int(main.debug_spawn_units("athen/infantry_spearman_b", 1)[0])
	await frames(2)
	main.debug_attacked(other, 1) # same spot, suppressed: no new line, still a ping
	await frames(2)
	check(int(main.debug_minimap_panel()["pings"]) >= before + 2, "both hit units ping the minimap: %d" % int(main.debug_minimap_panel()["pings"]))

	print("FAILURES: ", failures)
	quit(1 if failures else 0)
