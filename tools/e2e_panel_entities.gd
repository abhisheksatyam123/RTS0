extends SceneTree
# 0.28's hero and relic buttons (gui/session PanelEntityManager.js, PanelEntity.js):
# a hero of the viewed player gets a button with its portrait and name under the
# top panel (other units don't), a click selects it, a double click also moves the
# camera to it, and the button goes when the hero does.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_panel_entities.gd
# Exit code 0 = pass.

var main
var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	root.get_node("MatchSettings").cheats = true # "wololo" hands the test relic over
	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func shown(row: Node) -> Array:
	return row.get_children().filter(func(b): return b.visible)

func press(b: Button, double := false) -> void:
	var mb := InputEventMouseButton.new()
	mb.button_index = MOUSE_BUTTON_LEFT
	mb.pressed = true
	mb.double_click = double
	b.gui_input.emit(mb)

func run() -> void:
	await frames(60)
	var row: Node = main.find_child("PanelEntities", true, false)
	check(row != null, "the session has the PanelEntities row")
	if row == null:
		quit(1)
		return
	# Headless, the mouse sits in the corner: no edge scrolling while the camera is checked.
	main.find_children("*", "RtsoCameraRig", true, false)[0].set_edge_scroll_enabled(false)
	var before := shown(row).size()
	var hero: PackedInt32Array = main.debug_spawn_units("athen/hero_pericles", 1)
	var other: PackedInt32Array = main.debug_spawn_units("athen/infantry_spearman_b", 1)
	await frames(4)
	var buttons := shown(row)
	check(hero.size() == 1 and buttons.size() == before + 1, "a hero adds one button, a spearman none: %d -> %d" % [before, buttons.size()])
	var b: Button = buttons[-1] if not buttons.is_empty() else null
	check(b != null and (b.tooltip_text.contains("Pericles") or b.tooltip_text.contains("Periklēs")) and b.tooltip_text.contains("Health"), "its tooltip names it with its health: " + (b.tooltip_text if b else ""))
	if b:
		var tip := b.tooltip_text
		check(tip.begins_with("[font_size=16][b]") and not tip.contains("(Hero)"), "the name first in sans-bold-16, the specific name alone")
		check(tip.contains("Resistance") and tip.contains("Attack"), "then getAttackTooltip and getResistanceTooltip")
		check(tip.contains("Range: ") or tip.contains("Auras") or tip.contains("meters"), "and the auras (getAurasTooltip)")
	if b:
		var tex = b.get_child(0).texture
		check(tex != null, "with its portrait: " + (tex.resource_path if tex else "none"))
		press(b)
		await frames(2)
		check(Array(main.debug_selection_state()["selected"]) == [hero[0]], "a click selects it")
		press(b, true)
		await frames(2)
		await frames(60)
		var rig: Node3D = main.find_children("*", "RtsoCameraRig", true, false)[0]
		var hero_pos := Vector2(INF, INF)
		for u in main.debug_selection_state()["units"]:
			if int(u["id"]) == hero[0]:
				hero_pos = u["pos"]
		check(Vector2(rig.global_position.x, rig.global_position.z).distance_to(hero_pos) < 2.0,
			"a double click moves the camera to it: %s vs %s" % [rig.global_position, hero_pos])
	main.debug_delete_units(hero + other)
	await frames(60)
	check(shown(row).size() == before, "its button goes with it")

	print("Relics")
	var off: Vector2 = main.debug_spawn_offset("athen/infantry_spearman_b", 10)
	var relic: int = main.debug_spawn_relic(off.x, off.y + 6.0)
	var captors: PackedInt32Array = main.debug_spawn_units("athen/infantry_spearman_b", 10)
	main.debug_capture_relic(captors, relic)
	var rb: Button = null
	var parts_seen := 0
	for i in 1200:
		await frames(1)
		buttons = shown(row)
		rb = buttons[-1] if buttons.size() > before else null
		if rb:
			break
	check(rb != null, "a relic the player captures adds a button")
	if rb:
		var health: Control = null
		var capture: Control = null
		for c in rb.get_children():
			if c is ColorRect and c.get_child_count() == 1:
				health = c
			elif c is ColorRect and c.get_child_count() > 1:
				capture = c
		print("    relic tip: ", rb.tooltip_text.replace("\n", " | "))
		check(health != null and not health.visible, "no health bar (the catafalque has no Health)")
		check(capture != null and capture.visible and absf(capture.position.y - 45.0) < 0.5, "its capture bar takes the health bar's place")
		var parts := capture.get_children().filter(func(p): return p.visible) if capture else []
		print("    parts ", parts.map(func(p): return [p.size.x, p.color]))
		check(parts.size() >= 1 and parts[0].size.x > 0.0, "the owner's points first, in its colour")
		check(rb.tooltip_text.contains("Capture points") and not rb.tooltip_text.contains("Health"), "its tooltip gives the capture points, no health")
	main.debug_delete_units(captors + PackedInt32Array([relic]))
	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(0 if failures == 0 else 1)
