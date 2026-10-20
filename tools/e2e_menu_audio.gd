extends SceneTree
# music.js MENU state and the modern styles' click sounds outside a match:
#   1. the main menu starts Honor Bound on the Music bus, then the other three
#      menu tracks shuffled, looping (StartPlaylist(isLooping));
#   2. the music keeps playing on Game Setup and Options, and a second visit to
#      the main menu doesn't restart it;
#   3. buttons, dropdowns and tabs on those pages click on the UI bus;
#   4. a match stops the menu music, and its own buttons click only once.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_menu_audio.gd
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

func first(n: Node, type: String) -> Node:
	for c in n.find_children("*", type, true, false):
		if not (c is BaseButton and c.disabled):
			return c
	return null

func run() -> void:
	var audio = root.get_node("MenuAudio")
	print("1. The main menu's playlist")
	change_scene_to_file("res://scenes/menu/main_menu.tscn")
	await frames(3)
	check(audio.menu_music_track() == "Honor_Bound.ogg", "Honor Bound first")
	check(audio._music.bus == "Music", "on the Music bus")
	check(audio._playlist.size() == 4 and audio._playlist.slice(1).has("Juno_Protect_You.ogg"), "then the other three: %s" % [audio._playlist])
	audio._music.finished.emit()
	check(audio.menu_music_track() == audio._playlist[1], "the next track follows")
	audio._track = 3
	audio._music.finished.emit()
	check(audio.menu_music_track() == "Honor_Bound.ogg", "and the list loops")

	print("2. It carries over the other pages")
	var playing: String = audio.menu_music_track()
	change_scene_to_file("res://scenes/menu/gamesetup.tscn")
	await frames(5)
	check(audio.menu_music_track() == playing, "still playing in Game Setup")

	print("3. Clicks outside a match")
	var before: int = audio.clicks
	var opt = first(current_scene, "RtsoSafeSelect")
	opt.pressed.emit()
	check(audio.clicks == before + 1, "a Game Setup dropdown opening clicks")
	await frames(1)
	opt.item_selected.emit(0)
	opt.menu_closed.emit()
	check(audio.clicks == before + 2, "and so does its pick, once with the list closing")
	await frames(1)
	opt.menu_closed.emit()
	check(audio.clicks == before + 3, "closing the list without a pick clicks too (sound_closed)")
	change_scene_to_file("res://scenes/menu/options.tscn")
	await frames(5)
	check(audio.menu_music_track() == playing, "still playing in Options")
	before = audio.clicks
	var tabs: TabBar = first(current_scene, "TabBar")
	if tabs:
		tabs.tab_clicked.emit(0)
		check(audio.clicks == before + 1, "an Options tab clicks")
	await frames(1)
	before = audio.clicks
	var slider: HSlider = first(current_scene, "HSlider")
	if slider:
		var mb := InputEventMouseButton.new()
		mb.button_index = MOUSE_BUTTON_LEFT
		mb.pressed = true
		slider.gui_input.emit(mb)
		check(audio.clicks == before + 1, "pressing an Options slider clicks (ModernSlider sound_pressed)")
	await frames(1)
	before = audio.clicks
	var btn: Button = first(current_scene, "Button")
	btn.pressed.emit()
	check(audio.clicks == before + 1, "an Options button clicks")
	check(audio._click.bus == "UI", "on the UI bus")
	change_scene_to_file("res://scenes/menu/main_menu.tscn")
	await frames(3)
	check(audio.menu_music_track() == playing, "back at the main menu the track goes on")

	print("4. A match")
	var main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	await frames(30)
	check(audio.menu_music_track() == "", "the menu music stops")
	before = audio.clicks
	var sb: Button = first(main, "Button")
	sb.pressed.emit()
	check(audio.clicks == before, "session buttons are the session's to click")

	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
