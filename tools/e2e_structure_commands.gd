extends SceneTree
# unit_actions.js g_EntityCommands read the whole selection (getInfo(entStates)):
#   1. Unload All counts every selected structure's garrison;
#   2. autoqueue-on shows while some trainer is not auto-queuing, autoqueue-off while
#      some is, so a mixed selection has both;
#   3. share-dropsite needs no technology, only a mutual ally: Lock Dropsite on a shared
#      storehouse, Share Dropsite when any selected one is locked, and that shares them all;
#   4. Delete's tooltip has the hotkey colour on its confirmation line.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_structure_commands.gd
# Exit code 0 = pass.

const BLD_BARRACKS := 2
const BLD_STOREHOUSE := 3

var main
var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	var ms = root.get_node("MatchSettings")
	ms.slot_teams = [1, 1]
	ms.slot_types = [0, 0] # an ally with no AI
	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func named(label: String) -> Array:
	return main.debug_command_row().filter(func(e): return str(e["text"]).begins_with(label))

func press(label: String) -> void:
	var e := named(label)
	if e.size() == 1:
		(instance_from_id(e[0]["id"]) as Button).pressed.emit()
	await frames(30)

func run() -> void:
	await frames(60)
	var cc: int = main.debug_civic_centre_id(0)
	var barracks: int = main.debug_spawn_building(BLD_BARRACKS, 40.0, -30.0)
	print("1. Unload All over two structures")
	check(main.debug_put_inside(main.debug_spawn_units("athen/infantry_spearman_b", 1), cc) == 1, "one in the Civic Center")
	check(main.debug_put_inside(main.debug_spawn_units("athen/infantry_spearman_b", 2), barracks) == 2, "two in the barracks")
	await frames(10)
	main.debug_select_structures(PackedInt32Array([cc, barracks]))
	await frames(4)
	var unload := named("Unload All")
	check(unload.size() == 1 and str(unload[0]["text"]).contains("(3)"), "Unload All (3): " + str(unload.map(func(e): return e["text"])))

	print("2. Auto-queue on one of two trainers")
	main.debug_select_structures(PackedInt32Array([cc]))
	await frames(4)
	await press("Activate Auto-Queue")
	main.debug_select_structures(PackedInt32Array([cc, barracks]))
	await frames(4)
	check(named("Activate Auto-Queue").size() == 1 and named("Deactivate Auto-Queue").size() == 1, "a mixed selection has both: " + str(main.debug_command_row().map(func(e): return e["text"])))

	print("3. Share Dropsite")
	var s1: int = main.debug_spawn_building(BLD_STOREHOUSE, -40.0, 30.0)
	var s2: int = main.debug_spawn_building(BLD_STOREHOUSE, -40.0, -30.0)
	main.debug_select_structures(PackedInt32Array([s1]))
	await frames(4)
	var lock := named("Lock Dropsite")
	check(lock.size() == 1 and str(lock[0]["tip"]).contains("Prevent allies from using this dropsite, now shared."), "a shared storehouse without Diaspora: Lock Dropsite")
	await press("Lock Dropsite")
	check(named("Share Dropsite").size() == 1, "locked: Share Dropsite")
	main.debug_select_structures(PackedInt32Array([s2, s1]))
	await frames(4)
	check(named("Share Dropsite").size() == 1 and named("Lock Dropsite").is_empty(), "one of two locked: Share Dropsite")
	await press("Share Dropsite")
	check(named("Lock Dropsite").size() == 1, "it shares both")

	print("4. Delete")
	var del := named("Delete")
	var tip := str(del[0]["tip"]) if del.size() == 1 else ""
	check(tip.contains("Self-Destruct\nDestroy the selected entities.\nUse [color=#fffb83][lb]"), "Delete's hotkey line: " + tip.replace("\n", " | "))
	print("FAILURES: ", failures)
	quit(0 if failures == 0 else 1)
