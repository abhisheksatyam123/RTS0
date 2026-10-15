extends SceneTree
# End-to-end check of the summary page (0 A.D. gui/summary) in the real match
# scene:
#   1. Ctrl+Tab during a 2v2 opens "Current Scores" over the paused game,
#      showing the player and its ally grouped as a team with "Team total";
#   2. the counters follow counters.js (scores, units, percentages, ratios);
#   3. the Charts tab charts any counter per player or per team;
#   4. Ctrl+Tab goes back to the game;
#   5. Exit asks QuitConfirmation's question and Stay goes back; Resign asks
#      first, puts the player out and asks whether to quit; Quit and View
#      Summary leaves for the summary page and Continue for the main menu;
#   6. the replay menu's Summary shows "Scores at the end of the game." with
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
	ms.lock_teams = true # summary.js groups teams only when they are locked
	ms.victory = 3 # endless
	ms.replay_dir = ""
	ms.load_save_path = ""
	change_scene_to_file("res://scenes/main.tscn")
	await frames(3)
	var m = current_scene
	var replay_dir: String = m.get_replay_status().replay_dir
	# 75 s of game time (three 30 s rows), at whatever --fixed-fps the run uses.
	while float(m.get_match_statistics().get("game_time", 0.0)) < 75.0:
		await frames(10)

	print("1. Ctrl+Tab opens the summary over the game")
	var s = m.get_node("SummaryScreen")
	m.toggle_summary()
	await frames(2)
	check(s.is_open() and paused, "the summary opens and pauses the single-player game")
	check(s.find_child("SummaryText", true, false).text == "Current Scores", "it reads Current Scores")
	check(s.find_child("TimeElapsed", true, false).text.begins_with("Game time elapsed: 01:1"), "with the game time: " + s.find_child("TimeElapsed", true, false).text)
	check(s.find_child("MapName", true, false).text.ends_with(" - Small"), "and the map with its size: " + s.find_child("MapName", true, false).text)
	# As 0 A.D.: allies show once the player shares their sight (Cartography).
	check(s.players.map(func(p): return p.id) == [0], "a player sees only itself during the game")
	# The rest checks every player's counters, as the page shows them once the game is over.
	s.configure({"isInGame": true, "dialog": true, "selectionOwner": m.get_summary_gui(true).selectionOwner})
	s.show_summary(false, m.get_match_statistics(), 0.0)
	await frames(2)
	check(s.players.size() == 4 and s.teams.size() == 2 and s.teams[0].size() == 2 and s.teams[1].size() == 2, "every player, the two allies as one team")
	check(s.find_child("TeamHeading0", true, false) != null and s.find_child("TeamHeading0", true, false).text == "Team 1"
		and s.find_child("TeamHeading1", true, false).text == "Team 2", "headed Team 1 and Team 2, the Game Setup teams")
	check(s.find_child("Heading0", true, false).get_parsed_text() == "", "no Player name heading when everyone has a team")
	var loose: Dictionary = m.get_match_statistics()
	for p in loose.players:
		p.setup_team = -1 if p.id >= 2 else 0
	s.show_summary(false, loose, 0.0)
	await frames(2)
	check(s.teams.keys() == [0, -1] and s.find_child("TeamHeading-1", true, false) == null
		and s.find_child("Heading0", true, false).get_parsed_text() == "Player name", "players without a team come first, untitled, under Player name")
	loose.lock_teams = false
	s.show_summary(false, loose, 0.0)
	await frames(2)
	check(s.teams.is_empty(), "no teams when they were not locked")
	s.show_summary(false, m.get_match_statistics(), 0.0)
	await frames(2)
	check(s._tab_buttons.map(func(b): return b.text) == ["Score", "Structures", "Units", "Resources", "Market", "Miscellaneous", "Charts"],
		"0 A.D.'s tabs")
	check(not s.find_child("ReplayButton", true, false).visible, "no Watch Replay in game")
	var lobby_btn = s.find_child("LobbyButton", true, false)
	check(lobby_btn is TextureButton and lobby_btn.texture_normal != null and lobby_btn.size == Vector2(16, 16),
		"the lobby button is 0.28's 16 px speech bubble")

	print("2. counters")
	var econ: int = s.player_value(1, 0, 1)
	var mil: int = s.player_value(1, 0, 2)
	var expl: int = s.player_value(1, 0, 3)
	check(econ > 0 and int(s.player_value(1, 0, 0)) == econ + mil + expl, "total score = economy + military + exploration (%d)" % s.player_value(1, 0, 0))
	check(expl % 10 == 0 and expl > 0, "exploration score is ten per whole percent explored")
	var units: Dictionary = s.player_value(1, 2, 0)
	check(units.keys() == ["trained", "killed", "lost"] and units.trained > 0, "units count trained / killed / lost: " + str(units))
	var team_units: Dictionary = s.team_value(0, 2, 0)
	check(team_units.trained == int(s.player_value(0, 2, 0).trained) + units.trained, "the team total adds its players")
	var team_score: int = s.team_value(0, 0, 0)
	check(team_score > 0, "team score %d" % team_score)
	check(s.format_value({"trained": 3, "killed": 1, "lost": 2}).contains("3[/color] / ") and
		s.format_value({"trained": 3, "killed": 1, "lost": 2}).contains("1[/color]\n"), "trained / killed, then lost on the next line")
	check(s.format_value(s.calculate_ratio(3, 0)) == "∞" and s.calculate_ratio(0, 0) == 0 and is_equal_approx(s.calculate_ratio(2, 3), 0.67),
		"kill / death ratio as counters.js calculateRatio")
	check(s.format_value(s.calculate_ratio(4, 2)) == "2" and s.format_value(s.calculate_ratio(1, 4)) == "0.25",
		"a whole ratio shows without decimals, as +toFixed(2) prints it")
	check(s.time_to_string(3903.0) == "01:05:03" and s.time_to_string(599.0) == "09:59", "times read mm:ss, or HH:mm:ss from an hour")
	check(s.calculate_percent(1, 3) == {"percent": 33} and s.format_value({"percent": 33}) == "33%", "percentages are floored")
	check(s._by({"unitsTrained": {"Civilian": 4}}, "unitsTrained", "Civilian") == 4 and
		s._by({"unitsTrained": {"FemaleCitizen": 3}}, "unitsTrained", "Civilian") == 3, "Civilian reads FemaleCitizen in replays from before 0.28")
	var misc: Array = s.score_panels()[5].headings.map(func(h): return h.caption)
	check(misc.has("Civilianization") and not misc.has("Feminization"), "0.28's Civilianization column: " + str(misc))
	var res: Dictionary = s.player_value(1, 3, 0)
	check(res.keys() == ["count", "gathered", "used"] and res.gathered > 0, "resources count / gathered / used: " + str(res))
	var shown: String = s.format_value({"count": 7, "gathered": 5, "used": 3})
	check(not shown.contains("7"), "the count is charted, not shown in the table: " + shown)
	check(s.panels[3].headings.map(func(h): return h.id).has("livestock") and s.player_value(1, 3, 8) is int, "Livestock bred")
	var by = s.get_script()
	check(by._by({"unitsTrained": {"total": 3, "Unit": 5}}, "unitsTrained", "Unit") == 5
		and by._by({"unitsLost": {"total": 2}}, "unitsLost", "Unit") == 2,
		"the Units Total column is the Unit class counter, livestock included")
	var misc_ids: Array = s.panels[5].headings.map(func(h): return h.id)
	check(misc_ids.has("population") and misc_ids.has("bribes"), "Population and Bribes in Miscellaneous")
	check(int(s.player_value(1, 5, 1).population) > 0, "population count %s" % str(s.player_value(1, 5, 1)))
	check(s.player_value(1, 5, 7).keys() == ["succeeded", "failed"], "bribes succeeded / failed")
	await shot("score")
	s.select_panel(2)
	await frames(1)
	await shot("units")
	var row: Control = s.find_child("Player1", true, false)
	check(row != null and row.get_node("Value0").get_parsed_text().contains(" / "), "the Units tab shows the counters in the player's row")
	s.select_panel(5)
	await frames(1)
	var misc_heads: Array = []
	for i in 10:
		var h = s.find_child("Heading%d" % i, true, false)
		if h: misc_heads.append(h.get_parsed_text())
	check(not misc_heads.has("Population") and misc_heads.any(func(t): return t.begins_with("Bribes")),
		"Miscellaneous shows Bribes and leaves Population to the charts: " + str(misc_heads))

	print("3. charts")
	s.select_panel(6)
	await frames(2)
	check(s.find_child("ChartsPanel", true, false).visible, "the Charts tab shows the charts")
	check(s.series[0].size() == 4 and s.series[0][0].points.size() == s.sample_count() and s.sample_count() >= 3,
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
	check(s.series[0].size() == 2 and int(s.series[0][0].points[-1].y) == team_units.trained, "Group by team charts each team")
	check(s.find_child("ChartLegend", true, false).get_parsed_text().contains(", "), "the legend lists the team's players")
	var runs: Array = s.chart_lines(PackedVector2Array([Vector2(0, 1), Vector2(30, INF), Vector2(60, 3), Vector2(90, 4)]))
	check(runs.size() == 2 and runs[0].size() == 1 and runs[1].size() == 2 and runs[1][0] == Vector2(60, 3),
		"an infinite kill/death ratio breaks the chart's line, as CChart leaves it out")
	var bnd: Array = s.chart_bounds([{"points": PackedVector2Array([Vector2(30, 120), Vector2(60, INF), Vector2(90, 150)])}])
	check(bnd[0] == Vector2(30, 120) and bnd[1] == Vector2(90, 150) and not bnd[2] and not bnd[3],
		"CChart bounds run from the smallest to the largest finite value, with no zero baseline: " + str(bnd))
	bnd = s.chart_bounds([{"points": PackedVector2Array([Vector2(30, 5), Vector2(60, 5)])}])
	check(bnd[3] and bnd[1].y == 6.0, "a flat series gets 1 added to its top")
	check(s.format_axis(125.0, "DURATION_SHORT") == "2:05" and s.format_axis(42.4, "PERCENTAGE") == "42%",
		"axis values read as CChart formats them")
	await shot("charts")

	print("4. back to the game")
	Input.parse_input_event(ctrl_tab())
	await frames(2)
	check(not s.is_open() and not paused, "Ctrl+Tab goes back to the running game")
	m.toggle_summary()
	await frames(2)
	check(s.is_open() and s.panel_index == 6 and s.selection["teamCharts"] and s.players.size() == 1, "it reopens on the last tab and remembers team charts, with only the player")
	s.configure({"dialog": false})
	check(s.selection["panel"] == 0, "a summary outside the match starts on the Score tab (summarySelection is the match's)")
	s.configure(m.get_summary_gui(true))
	s.continue_pressed()
	await frames(2)
	check(not paused, "Continue resumes the game")

	print("5. Exit asks first, then leaves for the summary page")
	var QuitConfirmation: GDScript = load("res://scenes/session/quit_confirmation.gd")
	var pm = m.get_node("PauseMenu")
	pm.toggle()
	pm._on_exit_pressed()
	await frames(1)
	var q = m.get_children().filter(func(c): return c.get_script() == QuitConfirmation)
	check(q.size() == 1 and not pm.visible and paused, "Exit closes the menu and asks, with the game paused")
	if q.size() == 1:
		check(q[0].title == "Confirmation" and q[0].caption == "The game has finished, what do you want to do?"
			and q[0].button_texts() == ["Stay", "Quit and View Summary", "Quit"], "QuitConfirmation's question and buttons")
		await shot("quit_confirmation")
		q[0].press(0)
	await frames(1)
	check(not paused and m.get_children().filter(func(c): return c.get_script() == QuitConfirmation).is_empty(), "Stay goes back to the game")
	var d = QuitConfirmation.defeat(m, false, true)
	await frames(1)
	check(d.title == "DEFEATED!" and d.caption == "You have been defeated!\nDo you want to quit?", "a defeat asks whether to quit")
	d.press(0)
	d = QuitConfirmation.defeat(m, true, false)
	await frames(1)
	check(d.title == "VICTORIOUS!" and d.caption == "You have won!" and d.button_texts() == ["OK"], "a host with humans still playing is only told")
	d.press(0)
	await frames(1)
	pm.toggle()
	check(not pm.get_node("CenterContainer/Panel/VBox/BtnResign").disabled, "the menu's Resign is open to a player")
	pm._on_resign_pressed()
	await frames(1)
	q = m.get_children().filter(func(c): return c.get_script() == QuitConfirmation)
	check(q.size() == 1 and q[0].caption == "Are you sure you want to resign?" and q[0].button_texts() == ["No", "Yes"],
		"Resign asks ResignConfirmation's question")
	if q.size() == 1:
		q[0].press(1)
	var asked = null
	for i in 120:
		await process_frame
		var found = m.get_children().filter(func(c): return c.get_script() == QuitConfirmation)
		if not found.is_empty():
			asked = found[0]
			break
	check(m.get_summary_gui(false).get("isObserver", false), "Yes resigns: the player is out and watches")
	check(asked != null and asked.title == "DEFEATED!" and asked.caption == "You have been defeated!\nDo you want to quit?",
		"and is asked whether to quit")
	if asked:
		asked.press(0)
	await frames(1)
	pm.toggle()
	check(pm.get_node("CenterContainer/Panel/VBox/BtnResign").disabled, "a resigned player can't resign again")
	pm.toggle()
	await frames(1)
	pm._on_exit_pressed()
	await frames(1)
	q = m.get_children().filter(func(c): return c.get_script() == QuitConfirmation)
	if q.size() == 1:
		q[0].press(1)
	for i in 600:
		await process_frame
		if current_scene != m and current_scene != null and current_scene.has_method("summary_text"):
			break
	var page = current_scene
	check(page != null and page.has_method("summary_text") and page.is_open() and not paused, "Quit and View Summary opens the summary page")
	if page and page.has_method("summary_text"):
		check(page.find_child("SummaryText", true, false).text == "You have been defeated…", "it reads You have been defeated…")
		check(page.players.size() == 4 and page.find_child("ReplayButton", true, false).visible, "every player, with Watch Replay")
		page.continue_pressed()
		await frames(5)
		check(current_scene != null and current_scene.scene_file_path == "res://scenes/menu/main_menu.tscn", "Continue goes to the main menu")

	print("6. replay menu summary")
	change_scene_to_file("res://scenes/menu/replay_browser.tscn")
	await frames(5)
	var browser = current_scene
	check(browser._replays.size() >= 1 and browser._replays[0].dir + "/" == replay_dir, "the match is in the replay menu")
	browser.select_replay(0)
	browser.show_summary()
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
