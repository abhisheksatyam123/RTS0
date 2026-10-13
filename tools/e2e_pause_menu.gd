extends SceneTree
# The in-game menu pauses the game; its own Resume button and Esc (0 A.D.
# "cancel") must still close it and unpause. With the menu paused along with
# the game, neither did anything.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_pause_menu.gd
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

func click(pos: Vector2) -> void:
	var at := root.get_final_transform() * pos # window pixels
	var mm := InputEventMouseMotion.new()
	mm.position = at
	mm.global_position = at
	Input.parse_input_event(mm)
	await frames(2)
	for down in [true, false]:
		var mb := InputEventMouseButton.new()
		mb.button_index = MOUSE_BUTTON_LEFT
		mb.pressed = down
		mb.position = at
		mb.global_position = at
		Input.parse_input_event(mb)
		await frames(2)

func key(code: Key) -> void:
	for down in [true, false]:
		var k := InputEventKey.new()
		k.keycode = code
		k.physical_keycode = code
		k.pressed = down
		Input.parse_input_event(k)
		await frames(2)

func run() -> void:
	await frames(30)
	var menu = main.get_node("PauseMenu")
	main._on_menu_button_pressed()
	await frames(2)
	check(menu.visible and paused, "the menu opens and pauses the game")
	var resume: Button = menu.get_node("CenterContainer/Panel/VBox/BtnResume")
	await click(resume.get_global_rect().get_center())
	check(not menu.visible and not paused, "Resume closes the menu and unpauses")

	main._on_menu_button_pressed()
	await frames(2)
	check(menu.visible and paused, "the menu opens again")
	await key(KEY_ESCAPE)
	check(not menu.visible and not paused, "Esc closes the menu and unpauses")

	main._on_menu_button_pressed()
	await frames(2)
	await key(KEY_F10)
	check(not menu.visible and not paused, "the menu hotkey (F10) closes it too")
	await key(KEY_F10)
	check(menu.visible and paused, "and opens it again")
	await key(KEY_ESCAPE)
	check(not menu.visible and not paused, "Esc after reopening closes it")
	print("FAILURES: ", failures)
	quit(1 if failures else 0)
