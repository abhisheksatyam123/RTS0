extends SceneTree
# unit_commands.js g_IsObserver: an observer who selects a player's structure gets that
# player's panels (training roster, research, command row), with every button disabled
# (controlsPlayer), instead of the details alone.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_observer_panels.gd
# Exit code 0 = pass.

var main
var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	var ms = root.get_node("MatchSettings")
	ms.slot_types = [1, 1] # every slot an AI: this client watches
	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func run() -> void:
	await frames(60)
	var cc: int = main.debug_civic_centre_id(0)
	check(cc != 0, "player 1's Civic Center")
	main.debug_select_entity(cc)
	await frames(4)
	var row: Array = main.debug_command_row()
	print("    ", row.map(func(e): return "%s%s" % [e["text"], " (off)" if e["disabled"] else ""]))
	check(row.size() >= 3, "its panels show: %d buttons" % row.size())
	check(row.all(func(e): return e["disabled"]), "every one disabled")
	var roster: Array = main.debug_roster()
	check(roster.size() > 0, "the roster lists what it trains: %d" % roster.size())
	check(roster.all(func(e): return e["disabled"]), "and none of it can be trained")
	print("FAILURES: ", failures)
	quit(1 if failures else 0)
