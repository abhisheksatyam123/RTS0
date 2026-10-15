extends SceneTree
# reference/common/setup.xml referenceTooltip and civinfo/setup.xml civInfoTooltip:
# the reference pages opened from the main menu show light text on the dark
# bkTooltip after 50 ms, not the menu's black-on-white snToolTip, and the
# Structure Tree's own tooltip label is readable on it.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_reference_tooltips.gd
# Exit code 0 = pass.

var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func move(pos: Vector2) -> void:
	var mm := InputEventMouseMotion.new()
	mm.position = root.get_final_transform() * pos
	mm.global_position = mm.position
	Input.parse_input_event(mm)
	await frames(1)

func run() -> void:
	change_scene_to_file("res://scenes/menu/encyclopedia.tscn")
	await frames(3)
	var tips = root.get_node("Tooltips")
	var tabs: TabContainer = current_scene.get_node("Center/Panel/VBox/TabContainer")
	var civinfo = tabs.get_child(0)
	var tree = tabs.get_child(1)

	print("1. Structure Tree icon")
	tabs.current_tab = 1
	await frames(3)
	var btn: Control = null
	for row in tree._rows.get_children():
		if row is HFlowContainer and btn == null and row.get_child_count() > 0:
			btn = row.get_child(0).get_child(0).get_child(1)
	await move(btn.get_global_rect().get_center())
	var waited := 0
	while tips.shown_text() == "" and waited < 20:
		await frames(1)
		waited += 1
	print("    shown after ", waited, " frames")
	check(tips.shown_text() != "" and waited <= 3, "the tooltip shows after 50 ms, not the menu's 500")
	var bg: Color = (tips._panel.get_theme_stylebox("panel") as StyleBoxFlat).bg_color
	check(bg.get_luminance() < 0.2, "on the dark bkTooltip panel")
	var custom: RichTextLabel = tips._custom
	check(custom != null and custom.get_theme_color("default_color").get_luminance() > 0.8, "with light text")
	await move(Vector2(4, 4))
	await frames(2)

	print("2. Civilization Overview uses civInfoTooltip")
	check(tips._reference_style(civinfo._gameplay).get("width", 0.0) == 440.0, "maxwidth 440, sans-12")
	check(tips._reference_style(btn).get("width", 0.0) == 480.0, "the tree keeps referenceTooltip's 480")

	print("3. The main menu keeps snToolTip")
	change_scene_to_file("res://scenes/menu/main_menu.tscn")
	await frames(3)
	check(tips._reference_style(current_scene).is_empty(), "no reference style on the menu")

	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
