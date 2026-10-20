extends SceneTree
# Game Setup's speed reaches the match for every entry of 0.28's game_speeds.json:
# Turtle (0.1×) and the Fast Forward speeds (5×, 10×, 20×, offered with no human
# seated) start at that speed, not at Normal.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_setup_speed.gd
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

func run() -> void:
	var ms = root.get_node("MatchSettings")
	ms.map_id = "random_continental"
	ms.seed = 4242
	ms.map_size = 128
	ms.slot_types = [0, 1]
	ms.slot_civs = [0, 1]
	ms.slot_teams = [1, 2]
	ms.replay_dir = ""
	ms.load_save_path = ""
	for speed in [0.1, 0.25, 1.5, 5.0, 20.0]:
		ms.game_speed = speed
		change_scene_to_file("res://scenes/main.tscn")
		await frames(5)
		var got: float = current_scene.get_game_speed()
		check(is_equal_approx(got, speed), "a %s× setup starts at %s× (got %s)" % [speed, speed, got])
	ms.game_speed = 1.0
	change_scene_to_file("res://scenes/menu/replay_browser.tscn")
	await frames(10)
	print("FAILURES: ", failures)
	quit(1 if failures else 0)
