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
	# is-dropsite-shared shows for observers (allowedPlayers Ally and Observer), disabled.
	var drop: Array = row.filter(func(e): return e["text"].ends_with("Dropsite"))
	check(drop.size() == 1, "the Civic Center's dropsite badge shows")
	check(drop.size() == 1 and (drop[0]["text"] == "Locked Dropsite" or drop[0]["tip"].contains("Allies are allowed")),
			"in the observer's words: %s" % (drop[0]["tip"] if drop.size() else ""))
	# focus-rally (allowedPlayers Player and Observer): the observer may press it.
	var start: Vector2 = main.debug_camera()["target"]
	main.debug_set_rally(cc, start.x + 20.0, start.y + 20.0)
	await frames(2)
	var rp = main.debug_selection_state().get("rally", null)
	check(rp != null, "the Civic Center has a rally point: %s" % rp)
	var rally_row: Array = main.debug_command_row().filter(func(e): return e["text"] == "Focus on Rally Point")
	check(rally_row.size() == 1 and not rally_row[0]["disabled"], "Focus on Rally Point shows, pressable")
	# Read at once: headless, the mouse rests on the screen corner and edge-scrolls the view.
	main._on_entity_command("focus-rally")
	var cam: Vector2 = main.debug_camera()["target"]
	check(rp != null and cam.distance_to(rp) < 0.5 and cam.distance_to(start) > 20.0, "it centres the view on the rally point: %s" % cam)
	var roster: Array = main.debug_roster()
	check(roster.size() > 0, "the roster lists what it trains: %d" % roster.size())
	check(roster.all(func(e): return e["disabled"]), "and none of it can be trained")
	print("FAILURES: ", failures)
	quit(1 if failures else 0)
