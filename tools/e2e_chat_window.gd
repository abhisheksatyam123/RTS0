extends SceneTree
# 0 A.D. 0.28's chat window and Match Settings dialog (gui/session/chat/chat_window.xml,
# ChatWindow.js, match_settings/MatchSettingsDialog.xml):
#   1. the chat window is a genericPanel with chat_window.xml's widgets at their sizes
#      (Filter, the history box, To:, Text:, Cancel, the History tick box, Send), StoneButton
#      buttons, ModernDropDown, ModernInput and ChatPanel, and no placeholder text;
#   2. with the history shown, the width puts the history text in the golden ratio
#      (resizeChatWindow); without it, chatDialogPanelSmall's 432 x 120 and no history;
#      History keeps the input focused;
#   3. Match Settings is a ModernDialog with a ModernDarkBoxGold description and a
#      StoneButton Close.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_chat_window.gd
# A control never shrinks below its minimum size (headless fonts are taller), so the
# bottom edges are checked against that.
# Exit code 0 = pass.

var main
var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func rel(c: Control, panel: Control) -> Rect2:
	return Rect2(c.global_position - panel.global_position, c.size)

func run() -> void:
	await frames(30)
	var vp: Vector2 = root.get_visible_rect().size
	main._on_chat_history_toggled(true)
	main.chat_open_page()
	await frames(4)
	var panel: Panel = main.find_child("ChatDialogPanel", true, false)
	var input: LineEdit = main.find_child("ChatInput", true, false)
	check(panel != null and panel.visible and input != null, "the chat window is open")
	if panel == null or input == null:
		quit(1)
		return

	print("1. Layout and skin")
	check(panel.theme_type_variation == "GenericPanel" and panel.theme != null, "genericPanel in the session skin")
	check(input.placeholder_text == "", "no placeholder text (chat_window.xml chatInput)")
	var r := panel.get_global_rect()
	var h := r.size.y
	print("    panel ", r, " of ", vp)
	check(absf(r.position.y - (vp.y * 0.22 - 75)) < 1 and absf(r.end.y - (vp.y * 0.78 - 85)) < 1, "chatDialogPanelLarge's top and bottom")
	var ir := rel(input, panel)
	check(ir.position.x == 75 and absf(ir.end.x - (r.size.x - 16)) < 1 and absf(ir.position.y - (h - 82)) < 1 and absf(ir.end.y - maxf(h - 56, ir.position.y + input.get_combined_minimum_size().y)) < 1,
		"chatInput at 75 100%-82 100%-16 100%-56: " + str(ir))
	var buttons := {}
	for b in panel.find_children("*", "Button", true, false):
		if b is CheckBox:
			buttons["tick"] = b
		elif (b as Button).text in ["Cancel", "Send"]:
			buttons[(b as Button).text] = b
	check(buttons.size() == 3, "Cancel, Send and the History tick box")
	if buttons.size() == 3:
		var cr := rel(buttons["Cancel"], panel)
		var sr := rel(buttons["Send"], panel)
		var tr := rel(buttons["tick"], panel)
		check(cr.position.x == 16 and absf(cr.end.x - (r.size.x * 0.3 + 16)) < 1 and absf(cr.position.y - (h - 48)) < 1
			and absf(cr.end.y - maxf(h - 12, cr.position.y + (buttons["Cancel"] as Button).get_combined_minimum_size().y)) < 1,
			"Cancel at 16 100%-48 30%+16 100%-12: " + str(cr))
		check(absf(sr.position.x - (r.size.x * 0.7 - 16)) < 1 and absf(sr.end.x - (r.size.x - 16)) < 1, "Send at 70%-16 .. 100%-16: " + str(sr))
		check(absf(tr.position.x - (r.size.x * 0.5 - 40)) < 1 and absf(tr.position.y - (h - 40)) < 1, "the History tick box at 50%-40 100%-40: " + str(tr))
		var sb: StyleBox = (buttons["Send"] as Button).get_theme_stylebox("normal")
		check(sb != null and "images" in sb, "StoneButton's stone sprite")
		var hover: StyleBox = (buttons["Send"] as Button).get_theme_stylebox("hover")
		check(hover != null and "images" in hover and hover.images.size() == 1, "StoneButtonOver: the selected stone, no trims")
	var log: RichTextLabel = null
	for t in panel.find_children("*", "RichTextLabel", true, false):
		log = t
	check(log != null and log.theme_type_variation == "ChatPanel" and log.visible, "the history is a ChatPanel")
	if log:
		var lr := rel(log, panel)
		check(lr.position == Vector2(10, 46) and absf(lr.end.y - (h - 120)) < 1, "chatHistoryText at 10 46 100%-10 100%(-120): " + str(lr))
		# resizeChatWindow: width = phi x the history text's height, plus 10 px a side.
		var want := (1.0 + sqrt(5.0)) / 2.0 * lr.size.y + 20.0
		check(absf(r.size.x - want) < 1.5 and absf(r.get_center().x - vp.x / 2) < 1, "the golden-ratio width: %.1f (want %.1f)" % [r.size.x, want])

	print("2. Without the history")
	input.grab_focus()
	(buttons.get("tick") as CheckBox).button_pressed = false
	await frames(3)
	r = panel.get_global_rect()
	check(absf(r.size.x - 432) < 1 and absf(r.size.y - 120) < 1 and absf(r.position.y - (vp.y * 0.5 - 140)) < 1, "chatDialogPanelSmall: " + str(r))
	check(log != null and not log.is_visible_in_tree(), "no history")
	check(input.has_focus(), "History keeps the input focused")
	ir = rel(input, panel)
	check(absf(ir.position.y - (120 - 82)) < 1, "the input still sits at 100%-82: " + str(ir))
	(buttons.get("tick") as CheckBox).button_pressed = true
	await frames(3)
	main.toggle_chat()
	await frames(2)

	print("2b. Overlay, history timestamps and a bare /allies")
	main.chat_open_page()
	await frames(2)
	input.text = "hello there"
	main._on_chat_send_pressed()
	await frames(2)
	var chat_panel: Control = main.find_child("ChatPanel", true, false)
	check(chat_panel != null and chat_panel.position == Vector2(3, 131) and is_equal_approx(chat_panel.size.y, vp.y - 131 - 240), "chatPanel at 3 131 100% 100%-240: " + (str(chat_panel.get_rect()) if chat_panel else "none"))
	var lines: Array = main.debug_chat_overlay()
	var line: RichTextLabel = lines.back()["node"] if lines.size() else null
	check(line != null and line.text.begins_with("[b]") and line.get_theme_font_size("normal_font_size") == 14 and line.get_theme_font_size("bold_font_size") == 14,
		"chatPanelOverlay lines in sans-bold-stroke-14")
	var hist: String = main.debug_chat_history()
	check(RegEx.create_from_string("^\\[lb\\]\\d\\d:\\d\\d\\] ").search(hist.get_slice("\n", hist.get_slice_count("\n") - 2)) != null and not hist.contains("aaaaaa"),
		"the \\[HH:mm] prefix in the history's own colour: " + hist.get_slice("\n", hist.get_slice_count("\n") - 2))
	main.chat_open_page()
	await frames(2)
	input.text = "/allies"
	main._on_chat_send_pressed()
	await frames(2)
	check(not panel.visible and input.text == "", "a bare /allies sends nothing and closes the window (closePage)")

	print("3. Match Settings")
	main.toggle_objectives()
	await frames(3)
	var title: Label = main.find_child("MatchSettingsTitle", true, false)
	var dlg: Panel = title.get_parent() if title else null
	check(dlg != null and dlg.visible and dlg.theme_type_variation == "ModernDialog", "a ModernDialog")
	if dlg:
		var box: Panel = null
		var close: Button = null
		for c in dlg.get_children():
			if c is Panel:
				box = c
			elif c is Button:
				close = c
		check(box != null and box.theme_type_variation == "ModernDarkBoxGold", "the description in ModernDarkBoxGold")
		check(close != null and "images" in close.get_theme_stylebox("normal"), "a StoneButton Close")
	main.toggle_objectives()

	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
