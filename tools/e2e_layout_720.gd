extends SceneTree
# Game Setup at 1280x720: Back and Start Game stay on screen (the options
# column scrolls), and each player row's civ dropdown keeps its emblem small.
# In a match, the top bar fits 1280 px with or without the multiplayer Lobby button.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_layout_720.gd
# Exit code 0 = pass.

var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	run.call_deferred()

func find_button(n: Node, text: String) -> Button:
	if n.name == "PauseMenu":
		return null # the session menu keeps a disabled Lobby, as 0.28's MenuButtons
	if n is Button and n.text == text:
		return n
	for c in n.get_children():
		var b := find_button(c, text)
		if b:
			return b
	return null

func run() -> void:
	# A 1280x720 box stands in for the window (the headless window is square).
	var frame := Control.new()
	frame.size = Vector2(1280, 720)
	root.add_child(frame)
	var page: Control = load("res://scenes/menu/gamesetup.tscn").instantiate()
	frame.add_child(page)
	page.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	for i in 10:
		await process_frame
	var screen := Rect2(Vector2.ZERO, Vector2(1280, 720))
	var back: Control = page.get_node("VBox/Header/BtnBack")
	var start: Control = page.get_node("VBox/HBox/Right/BtnStart")
	check(screen.encloses(back.get_global_rect()), "Back is on screen at %s" % back.get_global_rect())
	check(screen.encloses(start.get_global_rect()), "Start Game is on screen at %s" % start.get_global_rect())
	var grid: Control = page.get_node("VBox/HBox/Right/SlotsBox/SlotsGrid")
	var tallest := 0.0
	for c in grid.find_children("*", "RtsoSafeSelect", true, false):
		tallest = maxf(tallest, (c as Control).size.y)
	check(tallest > 0.0 and tallest <= 40.0, "player row dropdowns stay one line high, emblems included (tallest %.0f px)" % tallest)
	frame.queue_free()

	# The match's top bar: 12 px panel margins and 8 px padding on each side.
	var ms = root.get_node("MatchSettings")
	ms.map_id = "acropolis_bay"
	change_scene_to_file("res://scenes/main.tscn")
	for i in 60:
		await process_frame
	var lobby := find_button(current_scene, "Lobby")
	check(lobby != null and not lobby.visible, "single player has no Lobby button, as 0 A.D. without a lobby connection")
	if lobby:
		var bar: Control = lobby.get_parent()
		var room := 1280.0 - 2 * 12 - 2 * 8
		check(bar.get_combined_minimum_size().x <= room, "the top bar fits 1280 px: needs %.0f of %.0f" % [bar.get_combined_minimum_size().x, room])
		lobby.visible = true
		await process_frame
		check(bar.get_combined_minimum_size().x <= room, "and with the Lobby button of a multiplayer game: %.0f" % bar.get_combined_minimum_size().x)
	print("FAILURES: ", failures)
	quit(1 if failures else 0)
