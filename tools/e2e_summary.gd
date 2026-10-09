extends SceneTree
# End-to-end check of the summary page (0 A.D. gui/summary) in the real match
# scene:
#   1. Ctrl+Tab during a 2v2 opens "Current Scores" over the paused game,
#      showing the player and its ally grouped as a team with "Team total";
#   2. the counters follow counters.js (scores, units, percentages, ratios);
#   3. the Charts tab charts any counter per player or per team;
#   4. Ctrl+Tab goes back to the game;
#   5. the replay menu's Summary shows "Scores at the end of the game." with
#      Watch Replay, and Continue closes it.
#
#   godot --headless --path . --fixed-fps 60 -s tools/e2e_summary.gd
# Exit code 0 = pass.

var failures := 0

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

func ctrl_tab() -> InputEventKey:
	var e := InputEventKey.new()
	e.keycode = KEY_TAB
	e.physical_keycode = KEY_TAB
	e.ctrl_pressed = true
	e.pressed = true
	return e

## With a display (xvfb), saves what the page looks like to $SUMMARY_SHOTS.
func shot(name: String) -> void:
	var dir := OS.get_environment("SUMMARY_SHOTS")
	if dir == "" or DisplayServer.get_name() == "headless":
		return
	await RenderingServer.frame_post_draw
	root.get_texture().get_image().save_png(dir.path_join(name + ".png"))

func run() -> void:
	var ms = root.get_node("MatchSettings")
	ms.map_id = "random_continental"
	ms.seed = 4242
	ms.map_size = 192
	ms.slot_types = [0, 1, 1, 1]
	ms.slot_civs = [0, 1, 2, 3]
	ms.slot_teams = [1, 1, 2, 2]
	ms.victory = 3 # endless
	ms.replay_dir = ""
	ms.load_save_path = ""
	change_scene_to_file("res://scenes/main.tscn")
	await frames(3)
	var m = current_scene
	var replay_dir: String = m.get_replay_status().replay_dir
	await frames(60 * 75) # 75 s of game time: three 30 s rows

	print("1. Ctrl+Tab opens the summary over the game")
	var s = m.get_node("SummaryScreen")
	m.toggle_summary()
	await frames(2)
	check(s.is_open() and paused, "the summary opens and pauses the single-player game")
	check(s.find_child("SummaryText", true, false).text == "Current Scores", "it reads Current Scores")
	check(s.find_child("TimeElapsed", true, false).text.begins_with("Game time elapsed: 01:1"), "with the game time: " + s.find_child("TimeElapsed", true, false).text)
	check(s.find_child("MapName", true, false).text.ends_with(" - Small"), "and the map with its size: " + s.find_child("MapName", true, false).text)
	check(s.players.size() == 2 and s.players.map(func(p): return p.id) == [0, 1], "a player sees itself and its ally only")
	check(s.teams.size() == 1 and s.teams.values()[0].size() == 2, "the two are grouped as one team")
	check(s.find_child("Team total", true, false) != null or s.find_child("TeamHeading1", true, false) != null, "with a team heading")
	check(s._tab_buttons.map(func(b): return b.text) == ["Score", "Structures", "Units", "Resources", "Market", "Miscellaneous", "Charts"],
		"0 A.D.'s tabs")
	check(not s.find_child("ReplayButton", true, false).visible, "no Watch Replay in game")

	print("2. counters")
	var econ: int = s.player_value(1, 0, 1)
	var mil: int = s.player_value(1, 0, 2)
	var expl: int = s.player_value(1, 0, 3)
	check(econ > 0 and int(s.player_value(1, 0, 0)) == econ + mil + expl, "total score = economy + military + exploration (%d)" % s.player_value(1, 0, 0))
	check(expl % 10 == 0 and expl > 0, "exploration score is ten per whole percent explored")
	var units: Dictionary = s.player_value(1, 2, 0)
	check(units.keys() == ["trained", "killed", "lost"] and units.trained > 0, "units count trained / killed / lost: " + str(units))
	var team_units: Dictionary = s.team_value(1, 2, 0)
	check(team_units.trained == int(s.player_value(0, 2, 0).trained) + units.trained, "the team total adds its players")
	var team_score: int = s.team_value(1, 0, 0)
	check(team_score > 0, "team score %d" % team_score)
	check(s.format_value({"trained": 3, "killed": 1, "lost": 2}).contains("3[/color] / ") and
		s.format_value({"trained": 3, "killed": 1, "lost": 2}).contains("1[/color]\n"), "trained / killed, then lost on the next line")
	check(s.format_value(s.calculate_ratio(3, 0)) == "∞" and s.calculate_ratio(0, 0) == 0 and is_equal_approx(s.calculate_ratio(2, 3), 0.67),
		"kill / death ratio as counters.js calculateRatio")
	check(s.calculate_percent(1, 3) == {"percent": 33} and s.format_value({"percent": 33}) == "33%", "percentages are floored")
	var res: Dictionary = s.player_value(1, 3, 0)
	check(res.keys() == ["gathered", "used"] and res.gathered > 0, "resources gathered / used: " + str(res))
	await shot("score")
	s.select_panel(2)
	await frames(1)
	await shot("units")
	var row: Control = s.find_child("Player1", true, false)
	check(row != null and row.get_node("Value0").get_parsed_text().contains(" / "), "the Units tab shows the counters in the player's row")

	print("3. charts")
	s.select_panel(6)
	await frames(2)
	check(s.find_child("ChartsPanel", true, false).visible, "the Charts tab shows the charts")
	check(s.series[0].size() == 2 and s.series[0][0].points.size() == s.sample_count() and s.sample_count() >= 3,
		"one series per player with a point per row (%d)" % s.sample_count())
	check(not s._pickers[0][2].visible, "Score charts have no Type picker")
	s._pickers[0][0].select(2)
	s._pickers[0][0].item_selected.emit(2)
	await frames(1)
	check(s._pickers[0][2].visible and s._pickers[0][2].item_count == 3, "Units charts pick trained, killed or lost")
	var last: Vector2 = s.series[0][1].points[-1]
	check(int(last.y) == units.trained, "the trained chart ends at the trained count")
	s._team_toggle.button_pressed = true
	await frames(1)
	check(s.series[0].size() == 1 and int(s.series[0][0].points[-1].y) == team_units.trained, "Group by team charts the team")
	check(s.find_child("ChartLegend", true, false).get_parsed_text().contains(", "), "the legend lists the team's players")
	await shot("charts")

	print("4. back to the game")
	Input.parse_input_event(ctrl_tab())
	await frames(2)
	check(not s.is_open() and not paused, "Ctrl+Tab goes back to the running game")
	m.toggle_summary()
	await frames(2)
	check(s.is_open() and s.panel_index == 6 and s.team_charts(), "it reopens on the last tab and charts")
	s.continue_pressed()
	await frames(2)
	check(not paused, "Continue resumes the game")

	print("5. replay menu summary")
	change_scene_to_file("res://scenes/menu/replay_browser.tscn")
	await frames(5)
	var browser = current_scene
	check(browser._replays.size() >= 1 and browser._replays[0].dir + "/" == replay_dir, "the match is in the replay menu")
	browser.replay_list.select(0)
	browser._on_selected(0)
	browser._on_summary_pressed()
	await frames(2)
	var rs = browser.find_child("SummaryScreen", true, false)
	check(rs != null and rs.is_open(), "Summary opens the summary page")
	if rs:
		check(rs.find_child("SummaryText", true, false).text == "Scores at the end of the game.", "it reads Scores at the end of the game.")
		check(rs.players.size() == 4 and rs.teams.size() == 2, "every player, in two teams")
		check(rs.find_child("ReplayButton", true, false).visible, "with Watch Replay")
		rs.select_panel(1)
		await shot("replay_structures")
		rs.continue_pressed()
		await frames(2)
		check(browser.find_child("SummaryScreen", true, false) == null, "Continue goes back to the replay menu")
	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(0 if failures == 0 else 1)
