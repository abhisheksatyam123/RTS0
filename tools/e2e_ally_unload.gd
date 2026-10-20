extends SceneTree
# unit_actions.js g_EntityCommands "unload-all" (allowedPlayers Player and Ally): an ally's
# structure that holds some of our units shows Unload All with our count, and it unloads only
# ours (unload-all-by-owner); with none of ours inside there is no button.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_ally_unload.gd
# Exit code 0 = pass.

var main
var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	var ms = root.get_node("MatchSettings")
	ms.slot_teams = [1, 1]
	ms.slot_types = [0, 0] # no AI to take its units back out
	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func unload_button() -> Dictionary:
	for e in main.debug_command_row():
		if str(e["text"]).begins_with("Unload All"):
			return e
	return {}

func badge() -> Dictionary:
	for e in main.debug_command_row():
		if str(e.get("text", "")).ends_with("Dropsite"):
			return e
	return {}


func run() -> void:
	await frames(60)
	# The ally's barracks beside our Civic Center, in our sight (a click picks only what we see).
	var ally_bld: int = main.debug_spawn_building(2, 14.0, 0.0, 1) # BLD_BARRACKS
	await frames(4)
	var theirs: PackedInt32Array = main.debug_spawn_units("athen/infantry_spearman_b", 2, 1)
	check(main.debug_put_inside(theirs, ally_bld) == 2, "two of the ally's men inside its barracks")
	await frames(4)
	# A click on another player's structure shows it alone (0.28 selects it): its Command
	# panel keeps to what an ally may do (allowedPlayers Ally).
	main.debug_inspect_entity(ally_bld)
	await frames(4)
	check(unload_button().is_empty(), "no Unload All with none of ours inside")

	var mine: PackedInt32Array = main.debug_spawn_units("athen/infantry_spearman_b", 1)
	check(main.debug_put_inside(mine, ally_bld) == 1, "one of ours goes in")
	await frames(4)
	var b := unload_button()
	print("    ", b.get("text", ""))
	check(not b.is_empty() and str(b["text"]).contains("(1)") and not b["disabled"], "Unload All with our count")
	main._on_entity_command("unload-all")
	for i in 60:
		if unload_button().is_empty():
			break
		await frames(1)
	check(unload_button().is_empty(), "it unloads ours")
	var left: Array = main.debug_garrison_icons()
	check(left.size() == 1 and left[0]["disabled"] and left[0]["count"] == 2, "the ally's two stay inside")

	# C8: an ally's sharable wagon by our Civic Centre shows the dropsite badge, disabled,
	# once we have Diaspora (TECH_UNLOCK_SHARED_DROPSITES 128).
	var wagon: PackedInt32Array = main.debug_spawn_units("germ/support_wagon", 1, 1, 0)
	check(wagon.size() == 1, "an ally's wagon by our Civic Centre")
	await frames(4)
	main.debug_inspect_entity(wagon[0])
	await frames(4)
	check(badge().is_empty(), "no dropsite badge before Diaspora")
	main.debug_complete_research(0, 128)
	main.debug_inspect_entity(wagon[0])
	await frames(4)
	var d := badge()
	print("    ", d.get("text", ""))
	check(not d.is_empty() and d["disabled"] and str(d.get("tip", "")).contains("dropsite"), "a disabled dropsite badge")

	print("FAILURES: ", failures)
	quit(1 if failures else 0)
