extends SceneTree
# 0.28's TimeNotificationOverlay: the time left reads "mm:ss" (l10n.js timeToString,
# whole seconds dropped down), and the text shows player names in their colours
# instead of their markup. Checked on CeasefireManager's "You can attack in" line.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_time_notifications.gd
# Exit code 0 = pass.

var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	var ms = root.get_node("MatchSettings")
	ms.map_id = "random_continental"
	ms.seed = 7
	ms.map_size = 128
	ms.slot_types = [0, 1]
	ms.slot_civs = [0, 1]
	ms.ceasefire_min = 1
	ms.replay_dir = ""
	ms.load_save_path = ""
	change_scene_to_file("res://scenes/main.tscn")
	run.call_deferred()

func run() -> void:
	for i in 10:
		await process_frame
	var m = current_scene
	m.set_game_speed(2.0)
	var label: RichTextLabel = m.find_child("TimeNotificationText", true, false)
	check(label != null and label.bbcode_enabled, "the notification text takes markup")
	var seen := ""
	for i in 1400:
		await process_frame
		var t := label.get_parsed_text() if label else ""
		if t.contains("You can attack in"):
			seen = t
			break
	check(seen.strip_edges().begins_with("You can attack in 00:"), "the ceasefire counts down as mm:ss: \"%s\"" % seen.strip_edges())
	check(not seen.contains("["), "no markup shows")
	# notificationPanel: text_valign bottom, buffer_zone 5, in 50%-300 60 50%+300 120.
	await process_frame
	check(label != null and is_equal_approx(label.get_global_rect().end.y, 115.0) and label.size.x == 600.0,
			"the text sits on the box's bottom, 5 px in: " + (str(label.get_global_rect()) if label else ""))
	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
