extends SceneTree
# 0.28's Training panel over several structures (selection_panels.js Training): its
# buttons grey with "Cannot train while upgrading." only once every selected
# structure is upgrading. A Han Civic Center upgrading to an Imperial Court:
#   1. alone, it can't train;
#   2. with a second Civic Center that isn't upgrading (one Imperial Court at a
#      time), the panel trains.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_train_while_upgrading.gd
# Exit code 0 = pass.

const TT = preload("res://scenes/common/tooltips.gd")
const BLD_TOWN_CENTER := 0
const CIV_HAN := 13

var main
var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	var ms = root.get_node("MatchSettings")
	ms.cheats = true
	ms.slot_civs = [CIV_HAN, 1]
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

# The Training panel's units: [enabled count, total, any "Cannot train while upgrading."].
func roster() -> Array:
	await frames(10)
	var lists: Array = main.find_children("*", "ItemList", true, false).filter(func(l): return l.visible and l.item_count > 0)
	if lists.is_empty():
		return [0, 0, false]
	var l: ItemList = lists[0]
	var enabled := 0
	var said := false
	for i in l.item_count:
		if not l.is_item_disabled(i):
			enabled += 1
		said = said or TT.plain(str(l.get_item_tooltip(i))).contains("Cannot train while upgrading.")
	return [enabled, l.item_count, said]

func upgrade(cc: int) -> void:
	main.debug_select_structures(PackedInt32Array([cc]))
	await frames(4)
	var u := row_with("Upgrade to a")
	if u.size() == 1:
		(instance_from_id(u[0]["id"]) as Button).pressed.emit()
	await frames(30)

func run() -> void:
	await frames(60)
	await cheat("back to the future")
	await cheat("back to the future")
	await cheat("bring me my axe")
	await cheat("i see a mountain here")
	var cc: int = main.debug_civic_centre_id(0)
	var cc2: int = main.debug_spawn_building(BLD_TOWN_CENTER, 60.0, 60.0)
	await frames(10)
	main.debug_select_structures(PackedInt32Array([cc]))
	var r: Array = await roster()
	print("    before: ", r)
	check(r[0] > 0 and not r[2], "a Civic Center trains")

	print("1. One upgrading")
	await upgrade(cc)
	check(row_with("Cancel Upgrading").size() == 1, "it upgrades to an Imperial Court")
	r = await roster()
	print("    ", r)
	check(r[1] > 0 and r[0] == 0 and r[2], "alone: every unit grey, \"Cannot train while upgrading.\"")

	print("2. With one that isn't")
	main.debug_select_structures(PackedInt32Array([cc, cc2]))
	r = await roster()
	print("    ", r)
	check(r[0] > 0 and not r[2], "the selection trains")

	print("FAILURES: ", failures)
	quit(1 if failures else 0)
