extends SceneTree
# 0.28's replay command line (main.cpp, readme.txt):
#   1. records a short match;
#   2. -replay=FOLDER plays it with nothing drawn, every hash matching, and exits 0;
#   3. -replay with a hash flipped in commands.rts0rec exits 1 on that turn, and
#      -hashtest-full=false plays it through;
#   4. -replay-visual=.../commands.rts0rec watches it in the match scene;
#   5. a missing replay says so and -replay exits 1.
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
	await frames(400)
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

	print("4. -replay-visual")
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
	quit(1 if failures else 0)
