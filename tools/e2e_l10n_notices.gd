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
#      "Shift-click to train 5." (only while Shift is held: one unit has no
#      batch line) and getNeededResourcesTooltip's
#      "Insufficient resources:" line (checked in 3, before the defeat),
#      in German too.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_l10n_notices.gd
# Exit code 0 = pass.

const TT = preload("res://scenes/common/tooltips.gd")
const BIG := "[font_size=16][b]%s[/b][/font_size]"
const CLICK := "[color=#fffb83][lb]Click[rb][/color]"

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

func shift(down: bool) -> void:
	var k := InputEventKey.new()
	k.keycode = KEY_SHIFT
	k.physical_keycode = KEY_SHIFT
	k.shift_pressed = down
	k.pressed = down
	Input.parse_input_event(k)
	await frames(2)

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
	# The session's message boxes translate each line (OutOfSyncNetwork's captions).
	var QC = load("res://scenes/session/quit_confirmation.gd")
	var oos = QC.out_of_sync_network(main)
	await frames(2)
	var oos_title: String = oos.find_child("Title", true, false).text
	var oos_text: String = oos.find_child("Text", true, false).text
	var want_oos: String = TranslationServer.translate("Out-Of-Sync error on turn %(turn)s.").replace("%(turn)s", "0")
	print("  ", oos_title, " | ", oos_text.replace("\n", " | "))
	check(oos_title == TranslationServer.translate("Out of Sync") and oos_title != "Out of Sync" and oos_text.begins_with(want_oos + "\n")
		and oos_text.contains(TranslationServer.translate("Ensure all players use the same mods.")) and not oos_text.contains("Ensure all players"), "the Out of Sync box in German")
	oos.press(0)
	await frames(2)
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
	var pop: Control = null
	var food: Control = null
	# The counters refresh every 200 ms of wall time, so wait for one back in English.
	var t1 := Time.get_ticks_msec()
	while Time.get_ticks_msec() - t1 < 3000:
		await frames(1)
		# Counters.xml: 73 px slots side by side, titles in sans-bold-16 (ResourceTitleTags).
		pop = main.find_child("Population", true, false)
		food = main.find_child("Resource0", true, false)
		if pop != null and food != null and not (pop.tooltip_text.begins_with(BIG % "Population: current/limit (max)") and food.tooltip_text.begins_with(BIG % "Food")):
			pop = null
			food = null
		if pop != null and food != null:
			break
	check(pop != null and food != null, "the population and food counters")
	if pop and food:
		var cap: String = pop.find_children("*", "Label", true, false).map(func(l): return l.text).filter(func(t): return t.contains("/"))[0]
		check(RegEx.create_from_string("^\\d+/\\d+ \\(\\d+\\)$").search(cap) != null, "caption \"count/limit (max)\": " + cap)
		check(pop.size == Vector2(73, 38) and food.size == Vector2(73, 38) and is_equal_approx(pop.position.x - food.position.x, 4 * 73.0), "five 73 px counters, touching")
		var count: Label = food.find_children("*", "Label", true, false)[0]
		check(count.position == Vector2(33, 4) and count.get_theme_font_size("font_size") == 14 and count.get_theme_color("font_color") == Color.WHITE, "the count at 33 4, white 14")
		check(pop.tooltip_text == BIG % "Population: current/limit (max)" and food.tooltip_text == (BIG % "Food") + "\nHarvest from animals, berry bushes, fish, or fields.", "no allies' stats while playing (the name and CounterResource's description): " + food.tooltip_text)
		main.resign()
		# The defeat question pauses a single-player game (messageBox, resumeGame on "No").
		var t0 := Time.get_ticks_msec()
		while Time.get_ticks_msec() - t0 < 3000 and not paused:
			await frames(1)
		for q in main.get_children().filter(func(c): return c.get_script() == load("res://scenes/session/quit_confirmation.gd") and c.title == "DEFEATED!"):
			q.press(0)
		t0 = Time.get_ticks_msec()
		while Time.get_ticks_msec() - t0 < 3000 and food.tooltip_text == (BIG % "Food") + "\nHarvest from animals, berry bushes, fish, or fields.":
			await frames(1)
		var tip: String = food.tooltip_text
		print("  ", tip.replace("\n", " | "))
		# getOrderTooltip: "\\[Click]" in g_HotkeyTags; the rows: "■" in the player's colour, the name, in AllyStatTags.
		check(tip.begins_with((BIG % "Food") + "\nHarvest from animals, berry bushes, fish, or fields.\nUnordered: " + CLICK + " to change order.[b]\n[color=#") and tip.contains("]■[/color] ") and tip.ends_with("[/b]"), "a defeated player sees the others' food")
		var ev := InputEventMouseButton.new()
		ev.button_index = MOUSE_BUTTON_LEFT
		ev.pressed = true
		main._on_top_counter_gui_input(ev)
		check(food.tooltip_text.contains("\nDescending: " + CLICK), "a click orders them descending")
		main._on_top_counter_gui_input(ev)
		check(food.tooltip_text.contains("\nAscending: " + CLICK), "the next ascending")
		main._on_top_counter_gui_input(ev)
		check(food.tooltip_text.contains("\nUnordered: " + CLICK), "and back to unordered")

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
	# The primary name is in small caps (getEntityNamesFormatted), the secondary as written.
	check(names_by_mode["0"].to_upper() == ("%s (%s)" % [spec, gen]).to_upper() and names_by_mode["1"].to_upper() == ("%s (%s)" % [gen, spec]).to_upper()
			and names_by_mode["0"].begins_with(spec + " (") and names_by_mode["0"] != names_by_mode["0"].to_upper(), "specific first, then generic first")

	print("6. Cost tooltips")
	main.debug_train_buttons(0)
	await frames(2)
	var roster_tip := func() -> String:
		var ls: Array = main.find_children("*", "ItemList", true, false).filter(func(l): return l.visible and l.item_count > 0)
		return TT.plain(str(ls[0].get_item_tooltip(0))) if ls.size() > 0 else ""
	var rt: String = roster_tip.call()
	print("  ", rt.replace("\n", " | "))
	check(RegEx.create_from_string("\nCost: (Food|Wood|Stone|Metal) \\d+.* \\d+ seconds?").search(rt) != null, "a unit's cost with population and time")
	check(not rt.contains("Shift-click"), "no batch line for one unit")
	await shift(true)
	main.debug_train_buttons(0)  # the defeated player's panel refreshes on a rebuild
	await frames(2)
	rt = roster_tip.call()
	print("  ", rt.replace("\n", " | "))
	check(rt.contains("\nShift-click to train "), "the batch line while Shift is held")
	await shift(false)
	main.debug_train_buttons(11)
	await frames(2)
	var techs: Array = main.debug_command_row().map(func(e): return TT.plain(str(e["tip"]))).filter(func(t): return t.contains("Cost: "))
	print("  ", techs.map(func(t): return t.replace("\n", " | ")))
	check(techs.size() > 0, "techs show their cost")
	check(techs.all(func(t): return not t.contains("Not enough resources")), "no bare \"Not enough resources\"")
	TranslationServer.add_translation(de)
	TranslationServer.set_locale("de")
	await shift(true)
	main.debug_train_buttons(0)
	await frames(2)
	rt = roster_tip.call()
	await shift(false)
	print("  ", rt.replace("\n", " | "))
	check(rt.contains("Kosten: ") and rt.contains(TranslationServer.translate("Shift-click")), "in German: Kosten, Umschalt+Klick")
	TranslationServer.set_locale("en")
	TranslationServer.remove_translation(de)

	print("FAILURES: ", failures)
	quit(1 if failures else 0)
