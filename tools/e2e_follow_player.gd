extends SceneTree
# 0 A.D. 0.28's Follow Player (gui/session/top_panel/FollowPlayer.js and the
# "playercommand" notification in messages.js), in a replay:
#   1. records a short match in which player 1 orders a unit;
#   2. watches its replay: no "Follow Player" box as Observer; viewing player 1
#      shows it; ticked, the replayed order moves the camera to the ordered unit
#      and selects it.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_follow_player.gd
# Exit code 0 = pass.

var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func start_scene() -> Node:
	change_scene_to_file("res://scenes/main.tscn")
	await frames(3)
	return current_scene

func run() -> void:
	var ms = root.get_node("MatchSettings")
	ms.map_id = "random_continental"
	ms.seed = 31
	ms.map_size = 128
	ms.slot_types = [0, 1]
	ms.slot_civs = [0, 1]
	ms.victory = 3 # endless
	ms.replay_dir = ""
	ms.load_save_path = ""

	print("1. Record")
	var m := await start_scene()
	await frames(40)
	check(m.select_first_own(false), "select a unit")
	var unit: int = m.get_replay_status().follow_player.selected[0]
	await frames(60)
	m._on_stance_selected(1) # a command with the unit in it
	await frames(120)
	var replay_dir: String = m.get_replay_status().replay_dir
	change_scene_to_file("res://scenes/menu/replay_browser.tscn")
	await frames(5)

	print("2. Follow in the replay")
	ms.replay_dir = replay_dir
	m = await start_scene()
	await frames(5)
	# Headless, the mouse sits at the screen corner and edge-scrolls, which drops the follow.
	m.find_children("*", "RtsoCameraRig", true, false)[0].set_edge_scroll_enabled(false)
	var f: Dictionary = m.get_replay_status().follow_player
	check(not f.visible, "no Follow Player while viewing as Observer")
	m.set_replay_view(0)
	await frames(2)
	f = m.get_replay_status().follow_player
	check(f.visible and not f.on, "viewing player 1 shows Follow Player, unticked")
	m.set_follow_player(true)
	var guard := 0
	while guard < 1200 and not m.get_replay_status().follow_player.selected.has(unit):
		await process_frame
		guard += 1
	f = m.get_replay_status().follow_player
	check(f.selected.has(unit), "the replayed order selects the ordered unit")
	check(int(f.camera_follows) == unit, "the camera follows it: %s vs %d" % [str(f.camera_follows), unit])
	m.set_replay_view(254) # Gaia
	await frames(2)
	check(m.get_replay_status().follow_player.visible, "viewing Gaia keeps it (FollowPlayer.js hides it for Observer only)")
	m.set_replay_view(-1)
	await frames(2)
	check(not m.get_replay_status().follow_player.visible, "back to Observer hides it")

	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
