extends SceneTree
# 0.28's structure panels as icon buttons (selection_panels.js):
#   1. Alert: the Civic Center's Raise (bell_level1) and End (bell_level0) with 0.28's
#      tooltips;
#   2. Upgrade: a sentry tower's Defense Tower, its icon the target's portrait, "Upgrade to
#      a <name>. <tooltip>", the cost, disabled and grey before the Town Phase;
#   3. in the Town Phase it starts; then "Cancel Upgrading" over the shrinking progress,
#      and a click cancels.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_structure_panels.gd
# Exit code 0 = pass.

const BLD_SENTRY_TOWER := 19
const BLD_HOUSE := 1
const BLD_GATE := 8

const TT = preload("res://scenes/common/tooltips.gd")

var main
var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	root.get_node("MatchSettings").cheats = true
	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func cheat(c: String) -> void:
	main.toggle_chat()
	await frames(2)
	var le := root.gui_get_focus_owner() as LineEdit
	if le:
		le.text = c
	main._on_chat_text_submitted(c)
	await frames(20)

func row_with(prefix: String) -> Array:
	return main.debug_command_row().filter(func(e): return TT.plain(str(e["tip"])).begins_with(prefix))

func run() -> void:
	await frames(60)
	print("1. Alert")
	main.select_first_own(true)
	await frames(4)
	var raise := row_with("Raise an alert!")
	var end := row_with("End the alert.")
	check(raise.size() == 1 and TT.plain(str(raise[0]["tip"])).ends_with("\nAlert nearby Civilians to seek refuge.") and raise[0]["text"] == "Raise alert", "Raise an alert!")
	check(end.size() == 1 and TT.plain(str(end[0]["tip"])).ends_with("\nUnload nearby Civilians."), "End the alert.")
	if raise.size() == 1:
		var b: Button = instance_from_id(raise[0]["id"])
		var icon := b.get_child(0) as TextureRect
		check(b.text == "" and icon != null and icon.texture != null and icon.texture.resource_path.ends_with("bell_level1.png"), "an icon button: bell_level1")

	print("2. A sentry tower in the Village Phase")
	var tower: int = main.debug_spawn_building(BLD_SENTRY_TOWER, 30.0, 30.0)
	main.debug_select_entity(tower)
	await frames(4)
	var u := row_with("Upgrade to a")
	print("    ", u.map(func(e): return "%s | %s" % [e["text"], TT.plain(str(e["tip"])).replace("\n", " | ")]))
	check(u.size() == 1 and TT.plain(str(u[0]["tip"])).contains("Reinforce with stone and upgrade to a defense tower.") and TT.plain(str(u[0]["tip"])).contains("Cost: "), "names, tooltip and cost")
	check(u.size() == 1 and u[0]["disabled"] and u[0]["grey"] and TT.plain(str(u[0]["tip"])).contains("Requires Town Phase"), "disabled and grey: Requires Town Phase")

	print("3. In the Town Phase")
	await cheat("back to the future")
	await cheat("i want pizza 5000")
	main.debug_select_entity(tower)
	await frames(30)
	u = row_with("Upgrade to a")
	check(u.size() == 1 and not u[0]["disabled"], "it can start")
	if u.size() == 1:
		(instance_from_id(u[0]["id"]) as Button).pressed.emit()
	await frames(30)
	u = row_with("Cancel Upgrading")
	var p0: int = u[0]["progress"] if u.size() == 1 else -1
	await frames(100)
	u = row_with("Cancel Upgrading")
	var p1: int = u[0]["progress"] if u.size() == 1 else -1
	check(u.size() == 1 and p0 > 0 and p1 >= 0 and p1 < p0, "Cancel Upgrading over the progress: %d then %d px" % [p0, p1])
	if u.size() == 1:
		(instance_from_id(u[0]["id"]) as Button).pressed.emit()
	await frames(30)
	check(row_with("Upgrade to a").size() == 1, "a click cancels")
	print("4. Gates and alerts across the selection (selection_panels.js Gate, Alert)")
	var house: int = main.debug_spawn_building(BLD_HOUSE, -30.0, 30.0)
	var g1: int = main.debug_spawn_building(BLD_GATE, -30.0, -30.0)
	var g2: int = main.debug_spawn_building(BLD_GATE, -40.0, -30.0)
	await frames(4)
	main.debug_select_structures(PackedInt32Array([house, g1]))
	await frames(4)
	check(row_with("Lock Gate").size() == 1 and row_with("Unlock Gate").size() == 1, "a house first and a gate: the Gate panel shows")
	main.debug_select_entity(g1)
	await frames(4)
	(instance_from_id(row_with("Lock Gate")[0]["id"]) as Button).pressed.emit()
	await frames(30)
	main.debug_select_structures(PackedInt32Array([g1, g2]))
	await frames(4)
	var lk := row_with("Lock Gate")
	var ul := row_with("Unlock Gate")
	check(lk.size() == 1 and ul.size() == 1 and lk[0]["corners"] and ul[0]["corners"], "a locked and an unlocked gate frame both buttons")
	var cc: int = main.debug_civic_centre_id(0)
	main.debug_select_structures(PackedInt32Array([house, cc]))
	await frames(4)
	check(row_with("Raise an alert!").size() == 1, "a house first and a Civic Center: the Alert panel shows")
	(instance_from_id(row_with("Raise an alert!")[0]["id"]) as Button).pressed.emit()
	var inside := 0
	for i in 400:
		await frames(1)
		if i % 20 != 19:
			continue
		inside = 0
		for id in [cc, house]:
			main.debug_select_entity(id)
			await frames(1)
			var g = main.debug_selected_building().get("garrisoned", 0)
			inside += g.size() if g is Array or g is PackedInt32Array else int(g)
		if inside > 0:
			break
	print("    garrisoned after the alert: ", inside)
	check(inside > 0, "the Civic Center's alert sends its civilians inside")
	print("RESULT: %s" % ("PASS" if failures == 0 else "FAIL (%d)" % failures))
	quit(0 if failures == 0 else 1)
