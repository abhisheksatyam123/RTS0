extends SceneTree
# 0.28's "text" time notifications (GuiInterface PushNotification): the sim's own
# messages show for 10 s in the centre overlay, for the player they name. Checked on
# ProductionQueue.js's "The production queue is full." (a 17th item at a Civic Center).
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_text_notices.gd
# Exit code 0 = pass.

const FULL := "The production queue is full."
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
	ms.replay_dir = ""
	ms.load_save_path = ""
	change_scene_to_file("res://scenes/main.tscn")
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func shown(label: RichTextLabel) -> int:
	return label.get_parsed_text().count(FULL) if label else 0

func run() -> void:
	await frames(10)
	var m = current_scene
	var label: RichTextLabel = m.find_child("TimeNotificationText", true, false)
	check(label != null, "the overlay is there")
	for r in 4:
		m.debug_set_resource(r, 99999)
	m.debug_train_buttons(0)  # a Civic Center
	await frames(2)
	var rl: ItemList = null
	for n in m.find_children("*", "ItemList", true, false):
		if n.is_visible_in_tree() and n.item_count > 0:
			rl = n
	var pick := -1
	if rl:
		for i in rl.item_count:
			if not rl.is_item_disabled(i):
				pick = i
				break
	check(pick >= 0, "a Civic Center trains something")
	if pick < 0:
		quit(1)
		return
	for i in 16:
		m._on_roster_item_clicked(pick, Vector2(), MOUSE_BUTTON_LEFT)
	await frames(4)
	check(shown(label) == 0, "sixteen items queue without a notice")
	m._on_roster_item_clicked(pick, Vector2(), MOUSE_BUTTON_LEFT)
	var t0: float = m.get_overlay_times()["elapsed"]
	await frames(4)
	check(shown(label) == 1, "the 17th: \"%s\" in the centre" % FULL)
	var left := 0.0
	for i in 800:
		await process_frame
		if shown(label) == 0:
			left = m.get_overlay_times()["elapsed"] - t0
			break
	check(left > 9.5 and left < 10.6, "gone after 10 s of match time: %.2f s" % left)
	quit(1 if failures else 0)
