extends SceneTree
# 0.28's garrison panel (selection_panels.js g_SelectionPanels.Garrison, unloadTemplate):
#   1. a siege tower shows its passengers as icons, one per template and owner, on the
#      owner's colour, "Unload <names>", the click help and "Player: <name>";
#   2. a click unloads one of that type, Shift+click the rest of them;
#   3. an ally's Civic Center shows its units too, but only ours can be unloaded:
#      the ally's are disabled and grey, with their names and owner;
#   4. a vertical bar beside the icons shows the garrisoned units' total health
#      (updateGarrisonHealthBar), "Health: <sum> / <max sum>", hidden when empty;
#   5. two selected siege towers share one panel (Garrison.getItems groups every
#      holder's units), a click unloads one from each (Commands.js "unload-template"),
#      and the Formation panel gives way to it (conflictsWith Garrison).
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_garrison_panel.gd
# Exit code 0 = pass.

var main
var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	var ms = root.get_node("MatchSettings")
	ms.slot_teams = [1, 1]
	ms.slot_types = [0, 0] # no AI to take its units back out
	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func key(code: int, down: bool) -> void:
	var k := InputEventKey.new()
	k.keycode = code
	k.physical_keycode = code
	k.shift_pressed = down and code == KEY_SHIFT
	k.pressed = down
	Input.parse_input_event(k)
	await frames(1)

func icons() -> Array:
	return main.debug_garrison_icons()

func named(g: Array, name: String) -> Dictionary:
	for e in g:
		if str(e["tip"]).contains(name):
			return e
	return {}

func garrison_health() -> ProgressBar:
	return root.find_child("healthGarrison", true, false) as ProgressBar

func is_formation_button(b: Button) -> bool:
	if not b.is_visible_in_tree():
		return false
	for c in b.get_signal_connection_list("pressed"):
		if c["callable"].get_method() == "_on_formation_selected":
			return true
	return false

func click(i: Dictionary) -> void:
	(instance_from_id(i["id"]) as Button).pressed.emit()
	await frames(20)

func run() -> void:
	await frames(60)
	print("1. A siege tower")
	var tower: PackedInt32Array = main.debug_spawn_units("han/siege_tower", 1)
	var men: PackedInt32Array = main.debug_spawn_units("athen/infantry_spearman_b", 3)
	men.append_array(main.debug_spawn_units("athen/infantry_archer_b", 1))
	main.debug_put_inside(men, tower[0])
	await frames(200)
	main.debug_select_entity(tower[0])
	await frames(4)
	var g := icons()
	print("    ", g.map(func(e): return "%s x%d%s" % [str(e["tip"]).get_slice("\n", 0), e["count"], " disabled" if e["disabled"] else ""]))
	check(g.size() == 2 and named(g, "Hoplite").get("count") == 3 and named(g, "Archer").get("count") == 1, "one icon per template: 3 spearmen, 1 archer")
	if g.size() == 2:
		check(str(g[0]["tip"]).begins_with("Unload ") and str(g[0]["tip"]).contains("\nSingle-click to unload 1. Shift-click to unload all of this type.\nPlayer: "), "names, click help, owner: " + str(g[0]["tip"]).replace("\n", " | "))
		check(not g[0]["disabled"] and (g[0]["bg"] as Color).a > 0.5, "enabled, on the owner's colour")
		var bar := garrison_health()
		check(bar != null and bar.visible and bar.max_value > 0.0 and is_equal_approx(bar.value, bar.max_value)
			and bar.tooltip_text == "[b]Health:[/b] %d / %d" % [roundi(bar.value), roundi(bar.max_value)], "4. the garrison's total health: " + (bar.tooltip_text if bar else "none"))
		print("2. Clicks")
		await click(named(g, "Hoplite"))
		g = icons()
		check(g.size() == 2 and named(g, "Hoplite").get("count") == 2, "a click unloads one")
		await key(KEY_SHIFT, true)
		await click(named(g, "Hoplite"))
		await key(KEY_SHIFT, false)
		g = icons()
		check(g.size() == 1 and named(g, "Hoplite").is_empty(), "Shift+click unloads the rest of that type")
		var archer_only: float = garrison_health().max_value
		for i in 10: # Unload all, until the tower finds room for everyone
			if icons().is_empty():
				break
			main._on_ungarrison_pressed()
			await frames(40)
		check(icons().is_empty() and not garrison_health().visible and archer_only > 0.0, "4. no bar once empty")

	print("3. An ally's Civic Center")
	var ally_cc: int = main.debug_civic_centre_id(1)
	var mine: PackedInt32Array = main.debug_spawn_units("athen/infantry_spearman_b", 1)
	var theirs: PackedInt32Array = main.debug_spawn_units("athen/infantry_spearman_b", 2, 1)
	check(main.debug_put_inside(mine, ally_cc) == 1 and main.debug_put_inside(theirs, ally_cc) == 2, "one of ours and two of theirs inside")
	await frames(4)
	main.debug_select_entity(ally_cc)
	await frames(4)
	g = icons()
	print("    ", g.map(func(e): return "%s x%d%s" % [str(e["tip"]).replace("\n", " | "), e["count"], " disabled" if e["disabled"] else ""]))
	var own: Array = g.filter(func(e): return not e["disabled"])
	var other: Array = g.filter(func(e): return e["disabled"])
	check(ally_cc != 0 and own.size() == 1 and other.size() == 1, "ours and theirs apart, though the same template")
	if own.size() == 1 and other.size() == 1:
		check(other[0]["grey"] and not str(other[0]["tip"]).begins_with("Unload ") and str(other[0]["tip"]).contains("\nPlayer: "), "theirs: disabled, grey, names and owner")
		check(own[0]["bg"] != other[0]["bg"], "each on its owner's colour")
		await click(own[0])
		g = icons()
		check(g.size() == 1 and g[0]["disabled"], "a click unloads ours; theirs stay")
	print("5. Two siege towers selected")
	var towers: PackedInt32Array = main.debug_spawn_units("han/siege_tower", 2)
	for t in towers:
		main.debug_put_inside(main.debug_spawn_units("athen/infantry_archer_b", 1), t)
	await frames(200)
	main.debug_select_units(towers)
	await frames(30)
	g = icons()
	print("    ", g.map(func(e): return "%s x%d" % [str(e["tip"]).get_slice("\n", 0), e["count"]]))
	check(g.size() == 1 and g[0]["count"] == 2 and not g[0]["disabled"], "one Archer icon for both towers, x2")
	var forms: Array = main.find_children("*", "Button", true, false).filter(is_formation_button)
	check(forms.is_empty(), "no Formation panel under it: %d buttons" % forms.size())
	if g.size() == 1:
		await click(g[0])
		await frames(20)
		check(icons().is_empty(), "a click unloads one from each tower: " + str(icons().size()))
	print("RESULT: %s" % ("PASS" if failures == 0 else "FAIL (%d)" % failures))
	quit(0 if failures == 0 else 1)
