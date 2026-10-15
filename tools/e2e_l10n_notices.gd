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
#      (CounterManager.getAllyStatTooltip); a click on a counter changes it;
#   5. Options' "Naming of entities" (gui.session.howtoshownames) names units
#      as tooltips.js getEntityNames does: specific first by default, then
#      generic first, only specific, only generic;
#   6. training, research and construction tooltips read getEntityCostTooltip's
#      "Cost: Food 50  Wood 50  10 seconds", formatBatchTrainingString's
#      "Shift-click to train 5." and getNeededResourcesTooltip's
#      "Insufficient resources:" line (checked in 3, before the defeat),
#      in German too.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_l10n_notices.gd
# Exit code 0 = pass.

const TT = preload("res://scenes/common/tooltips.gd")

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
	var tips: Array = main.debug_command_row().map(func(e): return TT.plain(str(e["tip"])))
	# selection_panels.js: the tech's requirementsTooltip, translated.
	var req: String = TranslationServer.translate("Unlocked in Town Phase.")
	check(req != "Unlocked in Town Phase." and tips.any(func(t): return t.contains(req)), "\"%s\" on a tech button" % req)
	TranslationServer.set_locale("en")
	TranslationServer.remove_translation(de)
	main.debug_train_buttons(11)
	tips = main.debug_command_row().map(func(e): return TT.plain(str(e["tip"])))
	check(tips.any(func(t): return t.contains("Unlocked in Town Phase.")), "\"Unlocked in Town Phase.\" in English")
	main._on_diplo_tribute_pressed(1, 0)  # 100 more wood: 100 left, under Fishing Nets' 200
	await frames(10)
	main.debug_train_buttons(11)
	await frames(2)
	var nets: Array = main.debug_command_row().map(func(e): return TT.plain(str(e["tip"]))).filter(func(t): return t.begins_with("Fishing Nets"))
	print("  ", nets.map(func(t): return t.replace("\n", " | ")))
	check(nets.size() == 1 and RegEx.create_from_string("\nInsufficient resources: Wood \\d+$").search(nets[0]) != null, "Insufficient resources: Wood N")

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

	print("5. Naming of entities")
	var cfg := RtsoUserConfig.new()
	var saved: String = cfg.get_value("gui.session.howtoshownames")
	var names_by_mode := {}
	for mode in ["0", "1", "2", "3"]:
		cfg.set_value("gui.session.howtoshownames", mode)
		main.debug_train_buttons(0)  # a Civic Centre: the roster list
		await frames(2)
		var lists: Array = main.find_children("*", "ItemList", true, false).filter(func(l): return l.visible and l.item_count > 0)
		# The first line, after the Training panel's "[Z] " hotkey.
		names_by_mode[mode] = TT.plain(str(lists[0].get_item_tooltip(0))).get_slice("\n", 0).trim_prefix("[Z] ") if lists.size() > 0 else ""
	cfg.set_value("gui.session.howtoshownames", saved)
	print("  ", names_by_mode)
	var spec: String = names_by_mode["2"]
	var gen: String = names_by_mode["3"]
	check(spec != "" and gen != "" and spec != gen, "only specific: %s, only generic: %s" % [spec, gen])
	check(names_by_mode["0"] == "%s (%s)" % [spec, gen] and names_by_mode["1"] == "%s (%s)" % [gen, spec], "specific first, then generic first")

	print("6. Cost tooltips")
	main.debug_train_buttons(0)
	await frames(2)
	var roster_tip := func() -> String:
		var ls: Array = main.find_children("*", "ItemList", true, false).filter(func(l): return l.visible and l.item_count > 0)
		return TT.plain(str(ls[0].get_item_tooltip(0))) if ls.size() > 0 else ""
	var rt: String = roster_tip.call()
	print("  ", rt.replace("\n", " | "))
	check(RegEx.create_from_string("\nCost: (Food|Wood|Stone|Metal) \\d+.* \\d+ seconds?").search(rt) != null, "a unit's cost with population and time")
	check(rt.contains("\nShift-click to train "), "and the batch line")
	main.debug_train_buttons(11)
	await frames(2)
	var techs: Array = main.debug_command_row().map(func(e): return TT.plain(str(e["tip"]))).filter(func(t): return t.contains("Cost: "))
	print("  ", techs.map(func(t): return t.replace("\n", " | ")))
	check(techs.size() > 0, "techs show their cost")
	check(techs.all(func(t): return not t.contains("Not enough resources")), "no bare \"Not enough resources\"")
	TranslationServer.add_translation(de)
	TranslationServer.set_locale("de")
	main.debug_train_buttons(0)
	await frames(2)
	rt = roster_tip.call()
	print("  ", rt.replace("\n", " | "))
	check(rt.contains("Kosten: ") and rt.contains(TranslationServer.translate("Shift-click")), "in German: Kosten, Umschalt+Klick")
	TranslationServer.set_locale("en")
	TranslationServer.remove_translation(de)

	print("FAILURES: ", failures)
	quit(1 if failures else 0)
