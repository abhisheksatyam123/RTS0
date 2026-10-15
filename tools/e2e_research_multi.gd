extends SceneTree
# 0.28's Research panel with several structures selected (selection_panels.js
# g_SelectionPanels.Research.getItems):
#   1. two forges list their technologies once, not twice;
#   2. the one not upgrading and with the shortest queue comes first, and each button
#      researches at its own structure (researchFacilityId);
#   3. a forge and a barracks show both lists while they fit in 10 slots.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_research_multi.gd
# Exit code 0 = pass.

const BLD_BARRACKS := 2
const BLD_FORGE := 17

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
	current_scene = main
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

## [tech, building] of every visible Research button.
func tech_items(n: Node = null, out: Array = []) -> Array:
	if n == null:
		n = main
	if n is Button and n.is_visible_in_tree():
		for c in n.get_signal_connection_list("pressed"):
			if c["callable"].get_method() == "_on_tech_pressed_at":
				out.append(c["callable"].get_bound_arguments() + [n.disabled])
	for c in n.get_children():
		tech_items(c, out)
	return out

func say(text: String) -> void:
	main.toggle_chat()
	await frames(2)
	var le := root.gui_get_focus_owner() as LineEdit
	if le:
		le.text = text
	main._on_chat_text_submitted(text)
	await frames(2)

func select(ids: Array) -> Array:
	main.debug_select_structures(PackedInt32Array(ids))
	await frames(4)
	return tech_items()

func run() -> void:
	await frames(60)
	for res in 4:
		main.debug_set_resource(res, 5000)
	var f1: int = main.debug_spawn_building(BLD_FORGE, -6, -14)
	var f2: int = main.debug_spawn_building(BLD_FORGE, 6, -14)
	var bk: int = main.debug_spawn_building(BLD_BARRACKS, 0, 14)
	await say("back to the future") # Town Phase, so the forge's technologies are open
	await frames(30)

	print("1. Two forges, no duplicates")
	var one: Array = await select([f1])
	var both: Array = await select([f1, f2])
	var techs := both.map(func(a): return a[0])
	print("    one ", one, "\n    both ", both)
	check(not one.is_empty(), "a forge lists its technologies")
	check(both.size() == one.size(), "two forges list the same number")
	var uniq := {}
	for t in techs:
		uniq[t] = true
	check(uniq.size() == techs.size(), "with no technology twice")

	print("2. The less busy forge researches")
	var ok_items := one.filter(func(a): return not a[2])
	check(not ok_items.is_empty(), "a forge technology can be researched now")
	if ok_items.is_empty():
		quit(1)
		return
	main._on_tech_pressed_at(int(ok_items[0][0]), f1)
	await frames(30)
	both = await select([f1, f2])
	print("    after f1 queued: ", both)
	check(not both.is_empty() and both.all(func(a): return int(a[1]) == f2), "the buttons research at the forge with the shorter queue")
	var pick: Array = both.filter(func(a): return not a[2])[0]
	main._on_tech_pressed_at(int(pick[0]), int(pick[1]))
	await frames(30)
	print("    queues f1 ", main.debug_building_queue(f1), " f2 ", main.debug_building_queue(f2))
	check(main.debug_building_queue(f1) == 1 and main.debug_building_queue(f2) == 1, "a click queues it at that forge")

	print("3. A forge and a barracks")
	var b_only: Array = await select([bk])
	var f_only: Array = await select([f2])
	var mixed: Array = await select([bk, f2])
	print("    barracks ", b_only.size(), " forge ", f_only.size(), " mixed ", mixed.size())
	if b_only.size() + f_only.size() <= 10:
		check(mixed.size() == b_only.size() + f_only.size(), "both lists show")
	else:
		check(mixed.size() >= mini(b_only.size(), f_only.size()), "a list that would pass 10 slots is left out")
	check(mixed.any(func(a): return int(a[1]) == bk) or b_only.is_empty(), "the barracks' own technologies research there")

	print("FAILURES: ", failures)
	quit(1 if failures > 0 else 0)
