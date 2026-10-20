extends SceneTree
# 0 A.D. 0.28 OutOfSyncNetwork: the "Out of Sync" box a multiplayer match opens
# when the host finds a peer whose state differs. The session's own state comes
# from get_lobby_state (a stand-in here; tools/playtest_lockstep.py's forced OOS
# case checks the names, the host comparison and the dumps over the network):
#   1. the turn, the players by name, "differs from the hosts game state",
#      the mods/bug advice and the replay and dump paths;
#   2. the host's own state is the expected one ("identical to");
#   3. with an AI in the match, past turn 1, the AI line replaces the advice.
#
#   godot --headless --path . -s tools/e2e_oos_network.gd
# Exit code 0 = pass.

const Q := preload("res://scenes/session/quit_confirmation.gd")

class Session extends Node:
	var state := {}
	func get_lobby_state() -> Dictionary: return state
	func get_summary_gui(_dialog: bool) -> Dictionary: return {"networked": true}

var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	run.call_deferred()

func run() -> void:
	var s := Session.new()
	root.add_child(s)
	s.state = {"oos_turn": 101, "oos_players": ["bob", "carol"], "oos_identical": false, "oos_ai": false,
		"oos_replay": "/u/replays/x/commands.rts0rec", "oos_dump": "/u/oos_dump.txt"}

	print("1. a client whose state differs")
	var q: Node = Q.out_of_sync_network(s)
	await process_frame
	var lines: PackedStringArray = q.caption.split("\n")
	check(q.title == "Out of Sync", "titled Out of Sync")
	check(lines[0] == "Out-Of-Sync error on turn 101.", "the turn (%s)" % lines[0])
	check(lines[1] == "Players: bob, carol", "the players by name (%s)" % lines[1])
	check(lines[2] == "Your game state differs from the hosts game state.", "differs from the host's")
	check(lines[3] == "" and lines[4] == "Ensure all players use the same mods."
		and lines[5] == "Click on \"Report a Bug\" in the main menu to help fix this.", "the advice")
	check(lines[6] == "Replay saved to /u/replays/x/commands.rts0rec" and lines[7] == "Dumping current state to /u/oos_dump.txt",
		"the replay and dump paths")
	check(q.button_texts() == ["OK"] and not paused, "one OK button, and the match runs on")
	var panel: Control = q.find_child("MessageBox", true, false)
	check(panel != null and Vector2(panel.width, panel.height) == Vector2(600, 280), "OutOfSyncNetwork's 600 x 280, on the msgbox page")
	q.press(0)
	await process_frame

	print("2. the host")
	s.state["oos_identical"] = true
	q = Q.out_of_sync_network(s)
	check(q.caption.split("\n")[2] == "Your game state is identical to the hosts game state.", "identical to the host's")

	print("3. with an AI")
	s.state["oos_ai"] = true
	q = Q.out_of_sync_network(s)
	lines = q.caption.split("\n")
	check(lines.size() == 5 and lines[4] == "Rejoining Multiplayer games with AIs is not supported yet!", "the AI line only")
	s.state["oos_turn"] = 1
	check(Q.out_of_sync_network(s).caption.contains("Ensure all players use the same mods."), "but not on turn 1")

	print("RESULT: %s" % ("PASS" if failures == 0 else "FAIL (%d)" % failures))
	quit(0 if failures == 0 else 1)
