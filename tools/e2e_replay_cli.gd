extends SceneTree
# 0.28's replay command line (main.cpp, readme.txt):
#   1. records a short match;
#   2. -replay=FOLDER plays it with nothing drawn, every hash matching, and exits 0;
#   3. -replay with a hash flipped in commands.rts0rec exits 1 on that turn, and
#      -hashtest-full=false plays it through;
#   4. -replay-visual=.../commands.rts0rec watches it in the match scene;
#   5. a missing replay says so and -replay exits 1;
#   6. -replay with -serializationtest, -rejointest=N, -ooslog and -unique-logs
#      runs the simulation tests on every turn and exits 0.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_replay_cli.gd
# Exit code 0 = pass.

const ReplayCli := preload("res://scenes/menu/replay_cli.gd")

var failures := 0
var exit_code := -1

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func cli(args: Array) -> bool:
	ReplayCli.consumed = false
	ReplayCli.args_override = PackedStringArray(args)
	exit_code = -1
	return ReplayCli.run(root.get_node("MatchSettings"))

# Until the driver reports, or the guard runs out.
func wait_exit(guard: int) -> void:
	while exit_code < 0 and guard > 0:
		await process_frame
		guard -= 1
	# The finished box paused the tree; a real -replay has quit by now.
	paused = false

func run() -> void:
	ReplayCli.quit_hook = func(code: int) -> void: exit_code = code
	var ms = root.get_node("MatchSettings")
	ms.map_id = "random_continental"
	ms.seed = 4242
	ms.map_size = 128
	ms.slot_types = [0, 1]
	ms.slot_civs = [0, 1]
	ms.slot_teams = [1, 2]
	ms.victory = 3
	ms.replay_dir = ""
	ms.load_save_path = ""

	print("1. Record")
	change_scene_to_file("res://scenes/main.tscn")
	await frames(3)
	var dir: String = current_scene.get_replay_status().replay_dir
	await frames(380)
	current_scene.resign() # the replay ends with player 1 defeated
	await frames(5)
	for box in current_scene.get_children().filter(func(c): return c.has_method("button_texts")):
		box.press(0) # Stay: the defeat box pauses a single-player game
	await frames(15)
	change_scene_to_file("res://scenes/menu/replay_browser.tscn")
	await frames(5)
	check(FileAccess.file_exists(dir + "commands.rts0rec"), "a replay was written: " + dir)

	print("2. -replay")
	check(ReplayCli.parse(PackedStringArray(["-replay=" + dir])).get("check_hashes", false), "hashes are checked by default")
	check(cli(["-replay=" + dir.trim_suffix("/")]), "-replay=FOLDER starts")
	await wait_exit(4000)
	check(exit_code == 0, "every hash matches and it exits 0 (got %d)" % exit_code)
	check(not root.disable_3d, "the world draws again after the driver")

	print("3. A changed hash")
	var bad := "user://replays/zz_e2e_replay_cli/"
	DirAccess.make_dir_recursive_absolute(bad)
	DirAccess.copy_absolute(dir + "metadata.json", bad + "metadata.json")
	var f := FileAccess.open(dir + "commands.rts0rec", FileAccess.READ)
	var bytes := f.get_buffer(f.get_length())
	f.close()
	# The first turn's hash (after magic, version, count and its turn index).
	bytes[16] ^= 0xFF
	var w := FileAccess.open(bad + "commands.rts0rec", FileAccess.WRITE)
	w.store_buffer(bytes)
	w.close()
	cli(["-replay=" + bad + "commands.rts0rec"])
	await wait_exit(4000)
	check(exit_code == 1, "a mismatched hash exits 1")
	cli(["-replay=" + bad, "-hashtest-full=false"])
	await wait_exit(4000)
	check(exit_code == 0, "-hashtest-full=false plays it through (got %d)" % exit_code)

	print("6. The simulation tests")
	var opts := ReplayCli.parse(PackedStringArray(["-replay=" + dir, "-serializationtest", "-rejointest=5", "-ooslog", "-unique-logs"]))
	check(opts.get("sim_tests", {}) == {"serializationtest": true, "rejointest": 5, "ooslog": true, "unique_logs": true},
		"the test flags are read: " + str(opts.get("sim_tests")))
	cli(["-replay=" + dir, "-serializationtest", "-rejointest=5", "-ooslog", "-unique-logs"])
	await wait_exit(8000)
	var tests: Dictionary = current_scene.get_sim_tests() if current_scene and current_scene.has_method("get_sim_tests") else {}
	var rejoin: Dictionary = current_scene.get_rejoin_test() if current_scene and current_scene.has_method("get_rejoin_test") else {}
	check(exit_code == 0, "every turn survives a save and load, and the rejoined copy agrees (got %d)" % exit_code)
	check(int(tests.get("serialization_turns", 0)) >= 90 and int(tests.get("serialization_failure_turn", -1)) == 0,
		"the serialization test ran %d turns" % int(tests.get("serialization_turns", 0)))
	check(bool(rejoin.get("active", false)) and int(rejoin.get("rejoined_at", 0)) >= 5 and int(rejoin.get("turns", 0)) >= 90
		and int(rejoin.get("out_of_sync_turn", -1)) == 0, "the rejoin test started at turn 5: " + str(rejoin))
	var log_dir := str(tests.get("sim_log", ""))
	check(FileAccess.file_exists(log_dir + "00000010.txt") and FileAccess.file_exists(log_dir + "00000010.dat")
		and FileAccess.get_file_as_string(log_dir + "00000010.txt").contains("-- units"),
		"-ooslog writes each turn's readable and binary state to " + log_dir)
	check(str(tests.get("oos_dump_name", "")).begins_with("oos_dump_") and str(tests.get("oos_dump_name", "")).ends_with("_%d" % OS.get_process_id()),
		"-unique-logs names the OOS dump with the time and process id: " + str(tests.get("oos_dump_name", "")))
	for fn in DirAccess.get_files_at(log_dir):
		DirAccess.remove_absolute(log_dir + fn)
	DirAccess.remove_absolute(log_dir)

	print("4. -replay-visual")
	ms.game_speed = 2.0 # Game Setup's last speed doesn't reach a replay
	change_scene_to_file("res://scenes/menu/replay_browser.tscn")
	await frames(5)
	check(cli(["-replay-visual=" + dir + "commands.rts0rec"]), "-replay-visual=.../commands.rts0rec starts")
	var guard := 2000
	while guard > 0 and not (current_scene != null and current_scene.has_method("is_replay_playback") and current_scene.is_replay_playback()):
		await process_frame
		guard -= 1
	await frames(20)
	check(current_scene != null and current_scene.has_method("is_replay_playback") and current_scene.is_replay_playback(),
		"the match scene plays the replay")
	check(ms.get_node_or_null("ReplayDriver") == null and not root.disable_3d, "it draws, with no driver")
	var st: Dictionary = current_scene.get_replay_status()
	check(is_equal_approx(float(st.get("speed", 0.0)), 1.0), "at normal speed, as a new CGame's sim rate: %s" % st.get("speed"))
	check(current_scene.find_children("*", "Label", true, false).filter(func(l): return str(l.text).begins_with("REPLAY")).is_empty(),
		"with no replay HUD of RTS0's own")
	current_scene.set_replay_view(0)
	guard = 3000
	while guard > 0 and int(current_scene.get_replay_status().view) == 0:
		await process_frame
		guard -= 1
	st = current_scene.get_replay_status()
	check(int(st.view) == -1 and st.observer_mode, "the viewed player's defeat moves the view to Observer (onPlayersFinished): %s" % st.view)

	ms.game_speed = 1.0
	print("5. Missing")
	check(not cli(["-replay=user://replays/zz_none"]), "a missing replay doesn't start")
	check(exit_code == 1, "-replay exits 1")
	check(not cli(["-replay-visual=user://replays/zz_none"]) and exit_code == -1, "-replay-visual stays")

	DirAccess.remove_absolute(bad + "metadata.json")
	DirAccess.remove_absolute(bad + "commands.rts0rec")
	DirAccess.remove_absolute(bad)
	change_scene_to_file("res://scenes/menu/replay_browser.tscn")
	await frames(60)
	print("FAILURES: ", failures)
	# A static Callable still holding this script's lambda when the engine clears
	# GDScript statics at exit frees it twice (Godot 4.3): drop it before quitting.
	ReplayCli.quit_hook = Callable()
	ReplayCli.args_override = PackedStringArray()
	quit(1 if failures else 0)
