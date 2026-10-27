extends SceneTree
# 0.28's minimap panel buttons (gui/session/minimap/MiniMap.xml):
#   1. the idle worker button (bottom right) shows the idle count, is disabled
#      with none, and selects an idle worker;
#   2. the diplomacy colours button (bottom left) toggles them and shows its
#      on or off sprite;
#   3. the flare button (top left) arms a flare (INPUT_FLARE, also
#      session.flareactivate): the next left click on the map sends one and
#      disarms it, a right click cancels it, and only the drawn corner of each
#      sprite takes the click.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_minimap_panel.gd
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

func run() -> void:
	await frames(60)
	var d: Dictionary = main.debug_minimap_panel()
	print("1. Idle worker")
	check(not d["idle"]["disabled"] and int(d["idle_count"]) > 0, "the starting women are idle: " + str(d["idle_count"]))
	check(str(d["idle"]["tooltip"]).ends_with("Find idle worker\nNumber of idle workers.") and str(d["idle"]["texture"]).ends_with("minimap-idle.png"),
		"its tooltip and sprite")
	var idle: TextureButton = main.find_child("idleWorkerButton", true, false)
	check(idle.texture_click_mask != null, "a click mask from the sprite")
	# colorizeHotkey("%(hotkey)s ", ...): the key in gold brackets (or "Unassigned hotkey").
	check(str(d["idle"]["tooltip"]).begins_with("[color=#fffb83][lb]") and str(d["flare"]["tooltip"]).begins_with("[color=#fffb83][lb]"), "hotkeys in gold: " + str(d["idle"]["tooltip"]))
	check(idle.action_mode == BaseButton.ACTION_MODE_BUTTON_PRESS, "onMouseLeftPress: it acts on press")
	var count: Label = main.find_child("totalNumberIdleWorkers", true, false)
	check(count != null and count.get_theme_font("font") is FontVariation and count.get_theme_font("font").variation_embolden > 0.0, "the count in sans-bold-stroke-14")
	var tt = preload("res://scenes/common/tooltips.gd")
	check(tt._reference_style(idle).get("bottom", false) and not tt._reference_style(main.find_child("flareButton", true, false)).get("bottom", false),
		"sessionToolTipBottom on the idle button, sessionToolTip on the flare")
	main.clear_selection()
	idle.pressed.emit()
	await frames(2)
	check(main.debug_selection_grid(false)["selected"] == 1, "a press selects an idle worker")

	print("2. Diplomacy colours")
	check(str(d["diplomacy"]["texture"]).ends_with("minimap-diplomacy-off.png"), "off at the start")
	var diplo: TextureButton = main.find_child("diplomacyColorsButton", true, false)
	diplo.pressed.emit()
	await frames(2)
	d = main.debug_minimap_panel()
	check(str(d["diplomacy"]["texture"]).ends_with("minimap-diplomacy-on.png"), "a press turns them on")
	check(str(d["diplomacy"]["tooltip"]).ends_with("Toggle Diplomacy Colors"), "its tooltip")
	diplo.pressed.emit()
	await frames(2)
	check(str(main.debug_minimap_panel()["diplomacy"]["texture"]).ends_with("minimap-diplomacy-off.png"), "and off again")

	print("3. Flare")
	check(str(d["flare"]["tooltip"]).ends_with("Send a flare to your allies.") and not d["flare"]["disabled"], "a player's flare button")
	var flare: TextureButton = main.find_child("flareButton", true, false)
	flare.pressed.emit()
	check(main.debug_minimap_panel()["flare_armed"], "a press arms the flare")
	var vp := root.get_visible_rect().size
	var mb := InputEventMouseButton.new()
	mb.button_index = MOUSE_BUTTON_RIGHT
	mb.pressed = true
	mb.position = vp / 2
	root.push_input(mb)
	await frames(1)
	check(not main.debug_minimap_panel()["flare_armed"], "a right click cancels it")
	main._on_flare_button_pressed()
	mb = InputEventMouseButton.new()
	mb.button_index = MOUSE_BUTTON_LEFT
	mb.pressed = true
	mb.position = vp / 2
	root.push_input(mb)
	await frames(2)
	check(not main.debug_minimap_panel()["flare_armed"] and main.get_target_marker_actor_count() > 0, "a left click sends the flare and disarms it")

	print("4. Masks")
	for b in [idle, diplo, flare]:
		var m: BitMap = b.texture_click_mask
		var sz := m.get_size()
		var share := float(m.get_true_bit_count()) / (sz.x * sz.y)
		check(share > 0.05 and share < 0.8, "%s takes clicks only on its drawn part (%d%%)" % [b.name, int(share * 100)])
	print("5. Background (MiniMapPanel.setCivBackgroundTexture)")
	var bg := str(main.debug_minimap_panel()["civ_background"])
	print("    ", bg)
	check(bg.contains("/icons/bkg/background_circle_") and not bg.ends_with("_gaia.png"), "the viewed player's civ background")
	var circle := main.find_child("minimapCircle", true, false) as TextureRect
	check(circle != null and circle.texture != null and circle.texture.resource_path.ends_with("minimap_circle_modern.png"), "under minimap_circle_modern.png")
	var layer_buttons := 0
	for b in main.find_children("*", "Button", true, false):
		if b.text in ["Shroud", "Terr", "Res"]:
			layer_buttons += 1
	check(layer_buttons == 0, "no RTS0-only Shroud/Terr/Res buttons")
	print("RESULT: %s" % ("PASS" if failures == 0 else "FAIL (%d)" % failures))
	quit(0 if failures == 0 else 1)
