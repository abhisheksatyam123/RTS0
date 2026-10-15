extends SceneTree
# ChatInput.js onHotkeyChange and MenuButtons.js Chat: the chat input and Send button carry
# "Type the message to send.", the autocomplete line and the chat hotkeys' lines; the menu's
# Chat entry carries the hotkey lines.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_chat_tooltips.gd
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

func run() -> void:
	await frames(30)
	var t: Dictionary = main.debug_chat_tooltips()
	print("    input: ", str(t["input"]).replace("\n", " | "))
	var lines: PackedStringArray = str(t["input"]).split("\n")
	check(lines.size() == 5 and lines[0] == "Type the message to send." and lines[1] == "Press Tab to autocomplete player names.",
		"the input's first lines")
	check(str(t["open"]) == "Press Return to open the public chat.\nPress T to open the ally chat.\nPress L to open the previously selected private chat.",
		"the hotkey lines: " + str(t["open"]).replace("\n", " | "))
	main._on_menu_button_pressed()
	await frames(6)
	var chat: Button = main.get_node("PauseMenu").get_node("CenterContainer/Panel/VBox/BtnChat")
	check(chat.tooltip_text == str(t["open"]), "the menu's Chat entry: " + chat.tooltip_text.replace("\n", " | "))
	# ChatInput.autoComplete: Tab completes the name before the caret (tryAutoComplete).
	var names: PackedStringArray = t["names"]
	check(names.size() == 1, "one name to complete in single player: " + str(names))
	main.chat_open_page()
	await frames(4)
	var input: LineEdit = null
	for le in main.find_children("*", "LineEdit", true, false):
		if (le as LineEdit).placeholder_text == "Type message...":
			input = le
	check(input != null and input.has_focus(), "the chat input is open and focused")
	if input != null and names.size() == 1:
		var name := names[0]
		input.text = "hello " + name.substr(0, 1).to_lower() + " world"
		input.caret_column = 7
		var tab := InputEventKey.new()
		tab.keycode = KEY_TAB
		tab.pressed = true
		Input.parse_input_event(tab)
		await frames(3)
		check(input.text == "hello " + name + " world" and input.caret_column == 6 + name.length(),
			"Tab completes the name before the caret: '%s' caret %d" % [input.text, input.caret_column])
		check(input.has_focus(), "Tab keeps the focus in the chat input")
		input.text = "zz"
		input.caret_column = 2
		Input.parse_input_event(tab)
		await frames(3)
		check(input.text == "zz", "no match leaves the text")
	print("FAILURES: ", failures)
	quit(1 if failures else 0)
