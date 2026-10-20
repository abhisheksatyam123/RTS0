extends SceneTree
# gui/common/styles.xml sound_pressed: 0.28's session buttons (iconButton, StoneButton,
# BrownButton, CivIconOverlay) and dropdowns play ui_button_click.ogg when pressed. Every
# button of the session UI does here, buttons added later included; a disabled one, a
# button outside the session, the minimap's three unstyled buttons and the pause
# overlay stay silent.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_ui_click_sound.gd
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
	var mm := InputEventMouseMotion.new()
	mm.position = root.get_final_transform() * pos
	mm.global_position = mm.position
	Input.parse_input_event(mm)
	await frames(2)
	for down in [true, false]:
		var mb := InputEventMouseButton.new()
		mb.button_index = MOUSE_BUTTON_LEFT
		mb.pressed = down
		mb.position = pos
		mb.global_position = pos
		root.push_input(mb, true)
		await frames(1)
	await frames(2)

func find_button(n: Node, text: String) -> BaseButton:
	if n is Button and (n as Button).text == text and (n as Button).is_visible_in_tree():
		return n
	for c in n.get_children():
		var b := find_button(c, text)
		if b:
			return b
	return null

func run() -> void:
	await frames(60)
	var before: int = main.debug_ui_clicks()

	print("1. A session button built with the HUD")
	var menu := find_button(main, "Menu")
	check(menu != null, "the top panel's Menu button")
	if menu:
		await click(menu.get_global_rect().get_center())
		check(main.debug_ui_clicks() == before + 1, "its press clicks: %d" % (main.debug_ui_clicks() - before))
		await click(menu.get_global_rect().get_center()) # close it again
		before = main.debug_ui_clicks()

	print("2. A button added later")
	var layer := CanvasLayer.new()
	layer.layer = 120
	var b := Button.new()
	b.text = "Test"
	b.position = Vector2(600, 600)
	b.size = Vector2(80, 30)
	layer.add_child(b)
	main.add_child(layer)
	await frames(2)
	await click(Vector2(640, 615))
	check(main.debug_ui_clicks() == before + 1, "a new session button clicks too")
	b.disabled = true
	await click(Vector2(640, 615))
	check(main.debug_ui_clicks() == before + 1, "a disabled one is silent")

	print("2b. A session dropdown: sound_closed, and a pick that closes the list clicks once")
	var sel := RtsoSafeSelect.new()
	sel.add_item("A")
	sel.add_item("B")
	layer.add_child(sel)
	await frames(2)
	before = main.debug_ui_clicks()
	sel.menu_closed.emit()
	await frames(1)
	check(main.debug_ui_clicks() == before + 1, "closing the list unpicked clicks (sound_closed)")
	sel.item_selected.emit(1)
	sel.menu_closed.emit()
	await frames(1)
	check(main.debug_ui_clicks() == before + 2, "a pick and the close in one frame click once: %d" % (main.debug_ui_clicks() - before))

	print("3. A button outside the session")
	var outside := CanvasLayer.new()
	outside.layer = 121
	var o := Button.new()
	o.position = Vector2(300, 300)
	o.size = Vector2(80, 30)
	outside.add_child(o)
	root.add_child(outside)
	await frames(2)
	before = main.debug_ui_clicks()
	await click(Vector2(340, 315))
	check(main.debug_ui_clicks() == before, "is not the session's to sound")

	print("4. MiniMap.xml's buttons have no sounding style")
	for name in ["idleWorkerButton", "diplomacyColorsButton", "flareButton"]:
		var mb: BaseButton = main.find_child(name, true, false)
		before = main.debug_ui_clicks()
		if mb:
			mb.pressed.emit()
			await frames(2)
		check(mb != null and main.debug_ui_clicks() == before, name + " is silent")
	if main.has_method("_on_flare_button_pressed"):
		main._on_flare_button_pressed() # disarm the flare again

	print("FAILURES: ", failures)
	quit(1 if failures else 0)
