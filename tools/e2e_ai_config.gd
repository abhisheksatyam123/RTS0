extends SceneTree
# End-to-end check of Game Setup's AI Configuration dialog (0 A.D. 0.28
# gui/gamesetup/Pages/AIConfigPage, PerPlayer/AIConfigButton.js):
#   1. AI rows have a gear whose tooltip describes the AI ("Configure AI:
#      Medium Balanced Petra Bot."); Human rows have none;
#   2. the gear opens the dialog on that player: AI Player, AI Difficulty, AI
#      Behavior and Petra's description; changes reach the slot and the tooltip;
#   3. OK and Escape close it; AI Player None makes the slot unassigned and closes it.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_ai_config.gd
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

func key(code: Key) -> void:
	for down in [true, false]:
		var k := InputEventKey.new()
		k.keycode = code
		k.physical_keycode = code
		k.pressed = down
		Input.parse_input_event(k)
		await frames(1)

func dialog(page) -> Node:
	return page.find_child("AIConfig", true, false)

func run() -> void:
	change_scene_to_file("res://scenes/menu/gamesetup.tscn")
	await frames(10)
	var page = current_scene
	page._slot_count_node().value = 3
	await frames(2)
	page.refresh_slots()
	page._slot_rows[1]["kind"] = 1
	page._slot_rows[1]["diff"] = 3
	page._slot_rows[1]["beh"] = 2
	page._slot_rows[2]["kind"] = 0
	page.refresh_slots()
	await frames(2)
	check(page._slots_grid().columns == 6, "six columns: name, colour, assignment, AI, civ, team")
	var gear: Button = page._ai_buttons[1]
	check(gear != null and gear.modulate.a == 1.0 and gear.tooltip_text == "Configure AI: Medium Balanced Petra Bot.", "AI row's gear: " + (gear.tooltip_text if gear else "none"))
	var human: Button = page._ai_buttons[2]
	check(human.modulate.a == 0.0 and human.mouse_filter == Control.MOUSE_FILTER_IGNORE and human.tooltip_text == "", "no gear on an unassigned row")

	print("2. The dialog")
	gear.pressed.emit()
	await frames(3)
	var d = dialog(page)
	check(d != null and d.slot == 1, "the gear opens the AI Configuration for player 2")
	if d:
		check(d._ai.selected == 1 and d._diff.selected == 3 and d._beh.get_selected_id() == 2, "AI Player Petra Bot, Medium, Balanced")
		check(range(d._beh.item_count).map(func(i): return d._beh.get_item_text(i)) == ["Random", "Balanced", "Defensive", "Aggressive"], "0.28's AI Behavior order")
		check(d._desc.text.begins_with("Petra is the default 0 A.D. AI bot."), "Petra's description")
		d._diff.select(4)
		d._diff.item_selected.emit(4)
		d._beh.select(3)
		d._beh.item_selected.emit(3)
		await frames(1)
		check(int(page._slot_rows[1]["diff"]) == 4 and int(page._slot_rows[1]["beh"]) == 3, "Hard and Aggressive reach the slot")
		check(page._ai_buttons[1].tooltip_text == "Configure AI: Hard Aggressive Petra Bot.", "and the gear's tooltip: " + page._ai_buttons[1].tooltip_text)
		await key(KEY_ESCAPE)
		await frames(2)
		check(dialog(page) == null, "Escape closes it")

	print("3. AI Player None")
	page._ai_buttons[1].pressed.emit()
	await frames(3)
	d = dialog(page)
	if d:
		d._ai.select(0)
		d._ai.item_selected.emit(0)
		await frames(3)
		check(dialog(page) == null, "None closes the dialog")
		check(int(page._slot_rows[1]["kind"]) == 0, "and leaves player 2 unassigned")
		check(page._ai_buttons[1].modulate.a == 0.0, "with no gear")
	page._slot_rows[1]["kind"] = 1
	page.refresh_slots()
	await frames(2)
	page._ai_buttons[1].pressed.emit()
	await frames(3)
	d = dialog(page)
	if d:
		d.get_node("Dialog/OK").pressed.emit()
		await frames(2)
		check(dialog(page) == null, "OK closes it")
	print("PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
