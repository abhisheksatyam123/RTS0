extends SceneTree
# End-to-end check of replays, saved games and the statistics summary in the
# real match scene (scenes/main.tscn + the GDExtension):
#   1. plays a short single-player match with a few orders, saves it with a
#      description, and leaves it (which writes its replay);
#   2. replays it from the replay menu's folder and checks every turn's hash;
#   3. loads the save as the load dialog does and checks the game time.
#
#   godot --headless --path . --fixed-fps 60 -s tools/e2e_match_archive.gd
# Exit code 0 = pass.

var failures := 0
var phase := ""

func check(cond: bool, what: String) -> void:
	if cond:
		print("  ok   ", what)
	else:
		print("  FAIL ", what)
		failures += 1

func _initialize() -> void:
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func main_node() -> Node:
	return current_scene

func start_scene() -> Node:
	change_scene_to_file("res://scenes/main.tscn")
	await frames(3)
	return current_scene

func run() -> void:
	var ms = root.get_node("MatchSettings")
	ms.map_id = "random_continental"
	ms.seed = 777
	ms.map_size = 128
	ms.slot_types = [0, 1]
	ms.slot_civs = [0, 1]
	ms.slot_teams = [1, 2]
	ms.victory = 3 # endless
	ms.replay_dir = ""
	ms.load_save_path = ""

	print("1. record a match")
	var m := await start_scene()
	check(m != null and m.has_method("get_replay_status"), "match scene with RtsoMain")
	var st: Dictionary = m.get_replay_status()
	check(st.recording, "the match is being recorded")
	var replay_dir: String = st.replay_dir
	await frames(600) # 10 s of game time
	m._on_stance_selected(1)
	m._on_formation_selected(2)
	await frames(600)
	m._on_stance_selected(0)
	await frames(1200)
	check(m.save_game_as("e2e_test", "end-to-end test"), "save_game_as writes a save")
	check(FileAccess.file_exists("user://saves/e2e_test.json"), "the save has its metadata")
	var stats: Dictionary = m.get_match_statistics()
	check(stats.get("players", []).size() >= 2, "statistics list every player")
	check(stats.get("samples", []).size() >= 2, "statistics have 30 s rows")
	var p1: Dictionary = stats.players[1]
	check(int(p1.stats.unitsTrained.total) > 0, "the AI player's trained units are counted")
	var turns_recorded: int = m.get_replay_status().turn
	print("  recorded turns: ", turns_recorded, " in ", replay_dir)
	change_scene_to_file("res://scenes/menu/replay_browser.tscn")
	await frames(5)
	check(FileAccess.file_exists(replay_dir + "commands.rts0rec"), "leaving the match wrote commands.rts0rec")
	check(FileAccess.file_exists(replay_dir + "metadata.json"), "leaving the match wrote metadata.json")
	var browser = current_scene
	check(browser._replays.size() >= 1 and browser._replays[0].dir + "/" == replay_dir, "the replay menu lists it first")

	print("2. watch the replay")
	ms.replay_dir = replay_dir
	m = await start_scene()
	check(m.is_replay_playback(), "the match scene plays the replay")
	var guard := 0
	while not m.get_replay_status().finished and guard < 20000:
		await process_frame
		guard += 1
	st = m.get_replay_status()
	print("  replayed to turn ", st.turn, " of ", st.turns)
	check(st.finished, "the replay reaches its last turn")
	check(not st.out_of_sync, "every replayed turn matches its recorded hash")
	check(int(st.turns) >= turns_recorded, "the replay holds every recorded turn")

	print("3. load the save from the menu")
	ms.load_save_path = "user://saves/e2e_test.rts0sav"
	m = await start_scene()
	var summary: Dictionary = m.get_match_statistics()
	check(float(summary.game_time) >= 39.0, "the loaded game resumes at the saved time (%.1f s)" % float(summary.game_time))
	check(not m.get_replay_status().recording, "a loaded game is not recorded as a replay")
	DirAccess.remove_absolute(ProjectSettings.globalize_path("user://saves/e2e_test.rts0sav"))
	DirAccess.remove_absolute(ProjectSettings.globalize_path("user://saves/e2e_test.json"))

	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
