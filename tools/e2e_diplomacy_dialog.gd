extends SceneTree
# The diplomacy dialog as 0 A.D. 0.28 gui/session/diplomacy shows it:
#   1. a player sees every player's row with working buttons, and an AI's name has
#      translateAISettings as its tooltip ("Medium Balanced Petra Bot");
#   2. once out of the game, they open it again by following a player
#      (g_ViewedPlayer >= 1): that player's side shows, its buttons disabled
#      (controlsPlayer); watching the whole map it doesn't open.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_diplomacy_dialog.gd
# Exit code 0 = pass.

var main
var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	# Three players, so the AIs stay in the game once the human resigns.
	var ms = root.get_node("MatchSettings")
	ms.player_count = 3
	ms.slot_types = [0, 1, 1]
	ms.slot_civs = [0, 1, 2]
	ms.slot_teams = [0, 0, 0]
	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func others(d: Dictionary, me: int) -> Array:
	return d["rows"].filter(func(r): return int(r["player"]) != me)

func run() -> void:
	await frames(60)
	print("1. As a player")
	main.toggle_diplomacy()
	await frames(2)
	var d: Dictionary = main.debug_diplomacy()
	print("  ", d)
	check(d["open"] and d["rows"].size() >= 2, "the dialog lists the players")
	var mine: Array = d["rows"].filter(func(r): return int(r["player"]) == 0)
	check(mine.size() == 1 and String(mine[0]["name_tooltip"]) == "", "a human's name has no AI tooltip")
	var ai: Array = others(d, 0).filter(func(r): return String(r["name_tooltip"]).ends_with(" Petra Bot"))
	check(ai.size() >= 1, "an AI's name reads its difficulty, behaviour and Petra Bot")
	check(others(d, 0).all(func(r): return not r["tribute_shown"] or r["tribute_enabled"]), "tribute buttons work")
	check(d["rows"].all(func(r): return absf(Color(r["tint"]).a - 32.0 / 255.0) < 0.01), "each row on its player's colour at alpha 32")
	check(others(d, 0).all(func(r): return str(r["tribute_icon"]).ends_with("_small.png") and str(r["tribute_text"]) == ""), "tribute buttons are resource icons")
	# TributeButton.update: the player's name in their colour; the stance buttons carry no tooltip.
	check(others(d, 0).all(func(r): return str(r["tribute_tooltip"]).begins_with("Tribute 100 food to [color=#") and str(r["tribute_tooltip"]).ends_with("Shift-click to tribute 500.")),
			"tribute tooltips colour the player's name: " + str(others(d, 0)[0]["tribute_tooltip"] if others(d, 0).size() else ""))
	check(others(d, 0).all(func(r): return str(r["stance_tooltip"]) == ""), "the stance buttons have no tooltip")
	check(str(d["close_text"]) == "Close" and str(d["colors_tooltip"]).ends_with("Toggle Diplomacy Colors"), "\"Close\" and the colours icon's hotkey-led tooltip")
	main.toggle_diplomacy()

	print("2. Out of the game")
	main.resign()
	await frames(60)
	main.toggle_diplomacy()
	await frames(2)
	check(not main.debug_diplomacy()["open"], "watching the whole map, the dialog stays shut")
	var follow := int(ai[0]["player"]) if ai.size() > 0 else 1
	main.debug_set_observer_view(follow)
	await frames(2)
	main.toggle_diplomacy()
	await frames(2)
	d = main.debug_diplomacy()
	print("  ", d)
	check(d["open"], "following a player opens it")
	var theirs: Array = others(d, follow)
	check(theirs.size() >= 1 and theirs.any(func(r): return r["tribute_shown"]), "it shows that player's tribute buttons")
	check(theirs.all(func(r): return not r["tribute_enabled"]), "all disabled: an observer orders nothing")

	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
