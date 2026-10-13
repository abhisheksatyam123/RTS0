extends SceneTree
# Chat notices built from parts translate as 0 A.D. 0.28 builds them: one
# translated format string with named arguments (sprintf "%(player)s ..."),
# so a language can reorder them (ChatMessageFormatSimulation.js, tooltips.js
# getLocalizedResourceAmounts):
#   1. in English the lines read as 0.28's;
#   2. in German the same notices use the German format strings and resource
#      names;
#   3. so do the "Requires %(technology)s" lines on the command buttons;
#   4. the top panel's counters read CounterPopulation's caption and tooltip,
#      and once the player is defeated (an observer, g_IsObserver) every other
#      player's stock follows in the tooltips, with the order line
#      (CounterManager.getAllyStatTooltip); a click on a counter changes it.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_l10n_notices.gd
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

func chat() -> String:
	return str(main.get_lobby_state()["chat"])

func plain(s: String) -> String:
	var re := RegEx.create_from_string("\\[[^\\]]*\\]")
	return re.sub(s, "", true)

func run() -> void:
	var mm := InputEventMouseMotion.new()
	mm.position = Vector2(32, 26)
	Input.parse_input_event(mm)
	await frames(60)
	main.toggle_diplomacy()
	await frames(2)

	print("1. English")
	main._on_diplo_tribute_pressed(1, 3)  # food
	await frames(10)
	var text := plain(chat())
	print("  chat: ", text.strip_edges().replace("\n", " | "))
	check(text.contains("You have sent ") and text.contains(" 100 food."), "tribute: \"You have sent <player> 100 food.\"")
	main._on_diplo_stance_changed(1, 1)
	await frames(10)
	text = plain(chat())
	check(text.contains("You are now neutral with "), "diplomacy: \"You are now neutral with <player>.\"")

	print("2. German")
	var de: Translation = load("res://assets/l10n/de.po")
	TranslationServer.add_translation(de)
	TranslationServer.set_locale("de")
	main._on_diplo_stance_changed(2, 1)
	await frames(10)
	main._on_diplo_tribute_pressed(1, 0)  # wood
	await frames(10)
	text = plain(chat())
	print("  chat: ", text.strip_edges().replace("\n", " | "))
	var want_diplo: String = TranslationServer.translate("You are now at war with %(player)s.").get_slice("%(player)s", 0)
	check(want_diplo != "You are now neutral with " and text.contains(want_diplo), "diplomacy in German: " + want_diplo)
	var wood: String = TranslationServer.translate("Wood")
	check(wood != "Wood" and text.contains("100 " + wood), "tribute in German with the German resource name: 100 " + wood)
	print("3. Requirements")
	main.debug_train_buttons(11)  # a dock: its Town Phase techs
	await frames(2)
	var tips: Array = main.debug_command_row().map(func(e): return str(e["tip"]))
	var req: String = TranslationServer.translate("Requires %(technology)s").get_slice("%(technology)s", 0)
	check(req != "Requires " and tips.any(func(t): return t.contains(req)), "\"%s...\" on a tech button" % req)
	TranslationServer.set_locale("en")
	TranslationServer.remove_translation(de)
	main.debug_train_buttons(11)
	tips = main.debug_command_row().map(func(e): return str(e["tip"]))
	check(tips.any(func(t): return t.contains("Requires Town Phase")), "\"Requires Town Phase\" in English")

	print("4. Counters")
	await frames(4)
	var pop: Control = null
	var food: Control = null
	for c in main.find_children("*", "HBoxContainer", true, false):
		if c.tooltip_text.begins_with("Population: current/limit (max)"):
			pop = c
		elif c.tooltip_text.begins_with("Food"):
			food = c
	check(pop != null and food != null, "the population and food counters")
	if pop and food:
		var cap: String = pop.find_children("*", "Label", true, false).map(func(l): return l.text).filter(func(t): return t.contains("/"))[0]
		check(RegEx.create_from_string("^\\d+/\\d+ \\(\\d+\\)$").search(cap) != null, "caption \"count/limit (max)\": " + cap)
		check(pop.tooltip_text == "Population: current/limit (max)" and food.tooltip_text == "Food", "no allies' stats while playing: " + food.tooltip_text)
		main.resign()
		await frames(10)
		var tip: String = food.tooltip_text
		print("  ", tip.replace("\n", " | "))
		check(tip.begins_with("Food\nUnordered: [Click] to change order.\n") and tip.contains(": "), "a defeated player sees the others' food")
		var ev := InputEventMouseButton.new()
		ev.button_index = MOUSE_BUTTON_LEFT
		ev.pressed = true
		main._on_top_counter_gui_input(ev)
		check(food.tooltip_text.contains("\nDescending: [Click]"), "a click orders them descending")
		main._on_top_counter_gui_input(ev)
		check(food.tooltip_text.contains("\nAscending: [Click]"), "the next ascending")
		main._on_top_counter_gui_input(ev)
		check(food.tooltip_text.contains("\nUnordered: [Click]"), "and back to unordered")

	print("FAILURES: ", failures)
	quit(1 if failures else 0)
