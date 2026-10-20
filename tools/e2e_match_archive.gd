extends SceneTree
# End-to-end check of replays, saved games and the statistics summary in the
# real match scene (scenes/main.tscn + the GDExtension):
#   1. plays a short single-player match with a few orders, saves it with a
#      description, and leaves it (which writes its replay);
#   2. replays it from the replay menu's folder and checks every turn's hash;
#   3. loads the save as the load dialog does, checks the game time, plays on
#      and leaves, which records a replay that starts from the save;
#   4. watches that replay (hash-checked from the save onwards) and switches
#      the player view between a player and the observer.
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

# Game time, not frames: n turns of 200 ms however fast the frames run (the frame rate and
# a slow machine change how many turns a frame count covers).
func turns(m: Node, n: int) -> void:
	var until: int = int(m.get_replay_status().turn) + n
	var guard := 0
	while int(m.get_replay_status().turn) < until and guard < n * 40:
		await process_frame
		guard += 1

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
	ms.nomad = true # setup-only settings the replay must bring back
	ms.slot_diffs = [3, 5]
	ms.slot_behs = [2, 3]
	ms.disable_treasures = true
	ms.replay_dir = ""
	ms.load_save_path = ""

	print("1. record a match")
	var m := await start_scene()
	check(m != null and m.has_method("get_replay_status"), "match scene with RtsoMain")
	var st: Dictionary = m.get_replay_status()
	check(st.recording, "the match is being recorded")
	var replay_dir: String = st.replay_dir
	await turns(m, 150) # 30 s of game time
	m._on_stance_selected(1)
	m._on_formation_selected(2)
	await turns(m, 150)
	m._on_stance_selected(0)
	await turns(m, 300)
	check(m.save_game_as("e2e_test", "end-to-end test"), "save_game_as writes a save")
	check(FileAccess.file_exists("user://saves/e2e_test.json"), "the save has its metadata")
	m.quicksave()
	check(not FileAccess.file_exists("user://saves/quicksave.json"), "the quicksave stays out of the saved games")
	m.set_game_speed(10.0)
	check(is_equal_approx(m.get_game_speed(), 2.0), "a game outside replays tops out at 2x")
	m.set_game_speed(1.0)
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
	var rmeta = JSON.parse_string(FileAccess.get_file_as_string(replay_dir + "metadata.json"))
	check(rmeta is Dictionary and rmeta.get("mods") is Array and not rmeta.mods.is_empty(), "the replay records the mods it was played with")
	var browser = current_scene
	check(browser._replays.size() >= 1 and browser._replays[0].dir + "/" == replay_dir, "the replay menu lists it first")

	print("2. watch the replay")
	# A Game Setup changed since the match must not leak into its replay.
	ms.nomad = false
	ms.slot_diffs = [3, 1]
	ms.slot_behs = [2, 1]
	ms.disable_treasures = false
	ms.replay_dir = replay_dir
	m = await start_scene()
	check(m.is_replay_playback(), "the match scene plays the replay")
	await frames(30)
	m.toggle_pause()
	var paused_turn: int = m.get_replay_status().turn
	await frames(120)
	check(m.get_replay_status().paused and int(m.get_replay_status().turn) == paused_turn, "a paused replay holds its turn")
	m.toggle_pause()
	m.set_game_speed(10.0)
	check(is_equal_approx(float(m.get_replay_status().speed), 10.0), "a replay plays at 10x")
	check(m.get_replay_status().speed_button and m.get_replay_status().speed_items.has("Fast Forward (20×)"),
		"a replay shows the Game Speed button (GameSpeedButton), its list with Fast Forward")
	var guard := 0
	while not m.get_replay_status().finished and guard < 20000:
		await process_frame
		guard += 1
	st = m.get_replay_status()
	print("  replayed to turn ", st.turn, " of ", st.turns, " in ", guard, " frames")
	check(guard < 600, "10x reaches the end of a 40 s replay well inside 10 s of frames")
	m.set_game_speed(1.0)
	check(st.finished, "the replay reaches its last turn")
	check(not st.out_of_sync, "every replayed turn matches its recorded hash")
	await process_frame
	var q = m.get_children().filter(func(c): return c.has_method("button_texts"))
	check(q.size() == 1 and q[0].caption == "The replay has finished. What do you want to do?" and paused,
		"the end of the replay asks what to do and pauses (QuitConfirmationReplay)")
	if q.size() == 1:
		check(q[0].button_texts() == ["Stay", "Quit and View Summary", "Quit"], "Stay, Quit and View Summary or Quit")
		q[0].press(0)
	await process_frame
	check(not paused, "Stay goes back to the replay")
	check(int(st.turns) >= turns_recorded, "the replay holds every recorded turn")
	check(int(st.view) == -1 and int(st.hud_player) == -1, "a replay starts in the observer view, with no player's counters")
	var items: Array = st.view_items
	check(items.size() >= 4 and items[0] == "Observer" and items[1] == "Gaia"
		and not str(items[2]).contains("("), "the picker lists Observer, Gaia and the players by name (PlayerViewControl): " + str(items))
	check(st.observer_mode, "\"Observer Mode\" shows in the observer view")
	m.set_replay_view(1)
	st = m.get_replay_status()
	check(int(st.view) == 1 and int(st.hud_player) == 1, "the player view follows player 2, counters and time notifications too")
	check(not st.observer_mode, "and \"Observer Mode\" goes")
	m.set_replay_view(254)
	st = m.get_replay_status()
	check(int(st.view) == -1 and st.view_gaia and st.observer_mode, "Gaia's view is the observer's, in Observer Mode")
	m.set_replay_view(-1)
	st = m.get_replay_status()
	check(int(st.view) == -1 and int(st.hud_player) == -1, "and goes back to the observer, hiding the counters (CounterManager)")
	# OutOfSyncReplay: a message box that pauses the replay until it is answered.
	var oos = load("res://scenes/session/quit_confirmation.gd").out_of_sync_replay(m)
	await process_frame
	check(oos.title == "Out of Sync" and oos.caption.begins_with("Out-Of-Sync error on turn ")
		and oos.caption.ends_with("The current game state is different from the original game state.") and paused,
		"an out-of-sync replay asks with \"Out of Sync\" and pauses")
	oos.press(0)
	await process_frame
	check(not paused, "OK resumes it")

	print("3. load the save from the menu")
	ms.load_save_path = "user://saves/e2e_test.rts0sav"
	m = await start_scene()
	var summary: Dictionary = m.get_match_statistics()
	check(float(summary.game_time) >= 39.0, "the loaded game resumes at the saved time (%.1f s)" % float(summary.game_time))
	st = m.get_replay_status()
	check(st.recording, "a loaded game goes on as a new replay")
	var continued_dir: String = st.replay_dir
	await frames(1200)
	var turns_continued: int = m.get_replay_status().turn
	change_scene_to_file("res://scenes/menu/replay_browser.tscn")
	await frames(5)
	check(FileAccess.file_exists(continued_dir + "start.rts0sav"), "its replay keeps the save it started from")
	var meta = JSON.parse_string(FileAccess.get_file_as_string(continued_dir + "metadata.json"))
	check(meta is Dictionary and float(meta.get("start_time", 0.0)) >= 39.0, "its replay starts at the saved time")

	print("4. watch the replay of the continued game")
	ms.replay_dir = continued_dir
	m = await start_scene()
	check(m.is_replay_playback(), "the match scene plays the continued replay")
	guard = 0
	while not m.get_replay_status().finished and guard < 20000:
		await process_frame
		guard += 1
	st = m.get_replay_status()
	print("  replayed to turn ", st.turn, " of ", st.turns, " from ", st.start_time, " s")
	check(st.finished, "the continued replay reaches its last turn")
	check(not st.out_of_sync, "every turn after the save matches its recorded hash")
	check(int(st.turns) >= turns_continued, "the continued replay holds every recorded turn")
	DirAccess.remove_absolute(ProjectSettings.globalize_path("user://saves/e2e_test.rts0sav"))
	DirAccess.remove_absolute(ProjectSettings.globalize_path("user://saves/e2e_test.json"))
	for f in ["e2e_test.png"]:
		DirAccess.remove_absolute(ProjectSettings.globalize_path("user://saves/" + f))

	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
