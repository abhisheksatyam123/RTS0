extends SceneTree
# 0.28's remaining command icons:
#   1. a Civic Center's autoqueue is g_EntityCommands "autoqueue-on" / "autoqueue-off"
#      (autoqueue-on.png, "Activate Auto-Queue"), and a click turns it on;
#   2. a gate has g_SelectionPanels.Gate's Lock Gate and Unlock Gate icons, the current
#      state framed by the corners, and Lock Gate locks it;
#   3. a packed bolt shooter has g_SelectionPanels.Pack's Unpack; unpacking, it has
#      Cancel Unpacking, and a click on that packs it back.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_command_icons.gd
# Exit code 0 = pass.

const BLD_GATE := 8

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

func named(label: String) -> Array:
	return main.debug_command_row().filter(func(e): return e["text"] == label)

func press(e: Dictionary) -> void:
	(instance_from_id(e["id"]) as Button).pressed.emit()
	await frames(30)

func run() -> void:
	await frames(60)
	print("1. Auto-queue")
	main.select_first_own(true)
	await frames(4)
	var on := named("Activate Auto-Queue")
	check(on.size() == 1 and str(on[0]["tip"]).contains("Activate Auto-Queue\nActivate the production auto-queue for the selected structures.") and named("Deactivate Auto-Queue").is_empty(), "autoqueue-on: " + str(on.map(func(e): return e["tip"])))
	if on.size() == 1:
		await press(on[0])
	check(main.debug_selected_building()["autoqueue"] and named("Deactivate Auto-Queue").size() == 1 and named("Activate Auto-Queue").is_empty(), "a click turns it on; then autoqueue-off")

	print("2. A gate")
	var gate: int = main.debug_spawn_building(BLD_GATE, 40.0, -40.0)
	main.debug_select_entity(gate)
	await frames(4)
	var lock := named("Lock Gate")
	var unlock := named("Unlock Gate")
	check(lock.size() == 1 and unlock.size() == 1 and unlock[0]["corners"] and not lock[0]["corners"], "Lock Gate and Unlock Gate, Unlock framed")
	if lock.size() == 1:
		await press(lock[0])
	lock = named("Lock Gate")
	check(lock.size() == 1 and lock[0]["corners"] and not named("Unlock Gate")[0]["corners"], "Lock Gate locks it")

	print("3. A bolt shooter")
	var bolt: PackedInt32Array = main.debug_spawn_units("rome/siege_ballista_packed", 1)
	main.debug_select_units(bolt)
	await frames(4)
	var unpack := named("Unpack")
	check(unpack.size() == 1 and named("Pack").is_empty(), "packed: Unpack")
	if unpack.size() == 1:
		await press(unpack[0])
	var cancel := named("Cancel Unpacking")
	check(cancel.size() == 1 and named("Unpack").is_empty(), "unpacking: Cancel Unpacking")
	if cancel.size() == 1:
		await press(cancel[0])
	check(named("Unpack").size() == 1 and named("Cancel Unpacking").is_empty(), "a click packs it back")
	print("RESULT: %s" % ("PASS" if failures == 0 else "FAIL (%d)" % failures))
	quit(0 if failures == 0 else 1)
