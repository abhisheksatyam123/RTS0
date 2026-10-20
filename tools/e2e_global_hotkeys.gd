extends SceneTree
# 0.28's global hotkeys on every page (hotkeys/misc.xml, OverlayCounter.js, main.cpp):
#   1. fps.toggle (Alt+F) and realtime.toggle (Alt+T) flip overlay.fps and
#      overlay.realtime for the run (OverlayCounter.js: not saved) from the main menu;
#   2. in a match Alt+F does the same, and shows the session's timing line with it;
#   3. screenshot (F2) and bigscreenshot (Shift+F2) are taken on every page (headless
#      has no image to save, so only the hotkey routing is checked there);
#   4. copy/paste/cut and text.* hotkeys drive Godot's text editing actions;
#   5. wireframe (Alt+Shift+W) flips the view to wireframe and back; profile.toggle
#      (F11) shows the profiler table and profile.save (Shift+F11) appends it to
#      logs/profile.txt.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_global_hotkeys.gd
# Exit code 0 = pass.

var failures := 0
var cfg := RtsoUserConfig.new()
var keep := {}

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func press(code: Key, alt := false, shift := false) -> void:
	for down in [true, false]:
		var k := InputEventKey.new()
		k.keycode = code
		k.physical_keycode = code
		k.alt_pressed = alt
		k.shift_pressed = shift
		k.pressed = down
		root.push_input(k, true)
		await frames(1)
	await frames(2)

func run() -> void:
	for k in ["overlay.fps", "overlay.realtime"]:
		keep[k] = cfg.get_value(k)
		cfg.set_value(k, "false")
	var us = root.get_node("UserSettings")
	print("1. On the main menu")
	change_scene_to_file("res://scenes/menu/main_menu.tscn")
	await frames(3)
	await press(KEY_F, true)
	check(cfg.get_bool("overlay.fps"), "Alt+F turns the FPS counter on")
	us._update_overlay()
	var fps_line: String = us._overlay_label.text.split("\n")[0]
	check(fps_line.begins_with("FPS:") and fps_line.length() == 9, "and it shows, padded as \"FPS: %(fps)4s\": [" + fps_line + "]")
	check(us._overlay_label.offset_top == us.OVERLAY_TOP and us._overlay_label.offset_right == -5.0
		and us._overlay_label.get_theme_stylebox("normal").bg_color == Color8(0, 0, 0, 100), "under the top panel at the right edge, on a black backing (dataCounter)")
	await press(KEY_T, true)
	check(cfg.get_bool("overlay.realtime"), "Alt+T turns the real time on")
	await press(KEY_T, true)
	await press(KEY_F, true)
	check(not cfg.get_bool("overlay.fps") and not cfg.get_bool("overlay.realtime"), "and again off")

	print("2. In a match")
	var main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	await frames(30)
	await press(KEY_F, true)
	await frames(2)
	check(cfg.get_bool("overlay.fps") and main.debug_overlay_visible(), "Alt+F shows the FPS counter and the session timings")
	await press(KEY_F, true)
	await frames(2)
	check(not cfg.get_bool("overlay.fps") and not main.debug_overlay_visible(), "and hides both")

	print("3. Screenshots")
	check(us.has_method("take_screenshot"), "screenshot is global")
	check(us.take_screenshot() == "", "headless: nothing to capture, no file")

	print("4. Text-box hotkeys")
	var has := func(action: String, code: Key, ctrl: bool) -> bool:
		return InputMap.action_get_events(action).any(func(e): return e is InputEventKey and e.keycode == code and e.ctrl_pressed == ctrl)
	check(has.call("ui_copy", KEY_C, true) and has.call("ui_text_caret_word_left", KEY_LEFT, true)
		and has.call("ui_text_delete_word", KEY_DELETE, true), "copy, text.move.left and text.delete.right drive Godot's text actions")
	var hk := RtsoHotkeyConfig.new()
	hk.set_hotkey("copy", PackedStringArray(["Ctrl+K"]))
	us.sync_text_hotkeys()
	check(has.call("ui_copy", KEY_K, true) and not has.call("ui_copy", KEY_C, true), "rebinding copy to Ctrl+K applies to text boxes")
	hk.reload()
	us.sync_text_hotkeys()
	check(has.call("ui_copy", KEY_C, true), "and reloads back")

	print("5. Wireframe and profiler")
	await press(KEY_W, true, true)
	check(us.get_viewport().debug_draw == Viewport.DEBUG_DRAW_WIREFRAME, "Alt+Shift+W: wireframe")
	await press(KEY_W, true, true)
	check(us.get_viewport().debug_draw == Viewport.DEBUG_DRAW_DISABLED, "again: solid")
	await press(KEY_F11)
	var table := us.find_child("profilerTable", true, false) as Label
	check(table != null and table.visible and table.text.begins_with("Profiling Information for: root"), "F11: the profiler table")
	var before := FileAccess.get_file_as_string("user://logs/profile.txt").length() if FileAccess.file_exists("user://logs/profile.txt") else 0
	await press(KEY_F11, false, true)
	check(FileAccess.get_file_as_string("user://logs/profile.txt").length() > before, "Shift+F11 appends it to logs/profile.txt")
	await press(KEY_F11)
	check(not table.visible, "F11 again hides it")
	check(us.take_big_screenshot() == "", "headless: no big screenshot either")

	for k in keep:
		cfg.set_value(k, keep[k])
	cfg.save()
	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
