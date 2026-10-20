extends SceneTree
# The selection details as 0 A.D. 0.28 gui/session/selection_details.js fills them,
# with gui/common/tooltips.js strings: a unit's health, attack and resistance lines,
# its detailed tooltip (attack, resistance with reduction percentages, gather rates,
# speed, loot), a group's garrison size and loot, a structure's resistance and
# garrison limit, a tree's supply and gatherers, an animal's food.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_selection_details.gd
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
	await frames(60)
	var u: Dictionary = main.debug_inspector("unit")
	print("unit: ", u)
	check(u["visible"], "a selected unit shows the details panel")
	check(not String(u["health"]).begins_with("Health: ") and " / " in String(u["health"]), "health reads \"current / max\" (healthStats, rounded up, no label)")
	var tip := String(u["stats_tooltip"])
	check(tip.begins_with("Attack:\n"), "the detailed tooltip opens with the attack")
	check("Hack" in tip and "Interval: " in tip and "Range: " in tip, "the attack names its damage types, range and interval")
	check(tip.begins_with("Attack:\n  Capture: ") and "\n  Spear: " in tip, "each attack of the template in its order, under the weapon's name (getAttackTooltip)")
	check("Resistance:\n  Damage: " in tip and "%)" in tip, "resistance lists levels with reduction percentages")
	check("Gather Rates: " in tip and "Speed: " in tip and " Walk, " in tip and " Acceleration" in tip, "gather rates and walk, run and acceleration follow")
	check("Loot: " in tip, "the loot closes the tooltip")
	check(String(u["stats"]).contains("Damage: "), "the stats lines show the attack and resistance")
	var ptip := String(u["portrait_tooltip"])
	check(ptip.begins_with(String(u["title"])), "the portrait tooltip names the unit")
	check("\nClasses: " in ptip, "then its visible classes")
	if "Infantry" in ptip:
		check("\nBattering Team: Battering Rams +0.15 speed" in ptip, "an infantry soldier lists template_unit_infantry's ram_garrison aura")
	check(ptip.ends_with("Left-click to follow. Right-click for more information."), "and ends with what its clicks do")
	check(String(u["rank_icon"]).begins_with("res://assets/textures/ui/archive/icons/ranks/") and String(u["rank_tooltip"]).contains(" Rank"), "a ranked unit shows its rank glyph")
	check(not String(u["title"]).begins_with("Advanced ") and not String(u["title"]).begins_with("Elite "), "the rank moves from the name to the glyph")
	check(Array(u["statuses"]).is_empty(), "an unhurt unit shows no status icons")
	check(String(u["portrait"]).begins_with("res://assets/textures/ui/archive/portraits/units/"), "the portrait is the unit template's 0.28 icon")
	check(String(u["rank_icon"]).ends_with("/ranks/Basic.png") and String(u["rank_tooltip"]) == "Basic Rank", "a Basic soldier's rank icon reads \"Basic Rank\"")
	check(Color(u["owner_color"]).a > 0.4 and Color(u["owner_color"]).a < 0.6, "the owner row sits on the player colour at half alpha")
	check(" — " in String(u["owner_tooltip"]) and String(u["owner_tooltip"]).ends_with("Phase"), "its tooltip names the civ and, for the viewer, the phase")
	check(String(u["owner_phase"]).begins_with("res://assets/textures/ui/archive/panel_phase_emblems_") and not String(u["owner_phase"]).ends_with("hidden.png"), "the viewer's own phase emblems show")
	check(not " (" in String(u["owner"]), "the row names the player, the civ moves to the tooltip")
	check(not "Kills" in String(u["owner"]) and String(u["extra"]) == "", "no RTS0-only kill count or extra line (displaySingle has no free text)")
	check(String(u["xp"]).begins_with("Experience: ") and " / " in String(u["xp"]), "a Basic soldier's experience shows on the portrait's vertical bar")

	var elite: PackedInt32Array = main.debug_spawn_units("spart/champion_infantry_swordsman", 1)
	await frames(2)
	var e: Dictionary = main.debug_inspector(str(elite[0]))
	check(String(e["rank_icon"]).ends_with("/ranks/Elite.png") and String(e["rank_tooltip"]).begins_with("Elite Rank\nElite units +20% training time"), "an Elite champion's rank icon gives the unit_elite bonus")
	var siege: PackedInt32Array = main.debug_spawn_units("athen/siege_lithobolos_packed", 1)
	await frames(2)
	var sg: Dictionary = main.debug_inspector(str(siege[0]))
	print("siege: ", sg["title"])
	check(String(sg["title"]).ends_with(String.chr(0x2014) + " Packed)") or String(sg["title"]).ends_with("(Packed)"), "a packed siege engine adds \"Packed\" to its secondary name")
	var m: Dictionary = main.debug_inspector("multi")
	print("multi: ", m)
	check(String(m["title_tooltip"]).begins_with("Garrison Size: "), "a group's tooltip gives its garrison size")
	check("Loot: " in String(m["title_tooltip"]), "and its loot")
	check(String(m["count"]).is_valid_int() and String(m["count_tooltip"]) == String(m["title_tooltip"]), "numberOfUnits shows the bare count, with that tooltip")
	check(String(m["health_multiple"]).begins_with("Health: ") and String(m["portrait"]) != "", "and a vertical health bar beside it")

	var b: Dictionary = main.debug_inspector("building")
	print("building: ", b)
	check(String(b["stats_tooltip"]).begins_with("Attack:\n  Bow: ") and " arrows / " in String(b["stats_tooltip"]), "a Civic Center's tooltip opens with its arrows, \"Rate: n arrows / 4 seconds\"")
	# elevationAdaptedRange: the range its arrows' height and the ground round it give, when that
	# differs ("Range: 67 (+3) meters"); the map is random, so the number varies.
	var rng_m := RegEx.create_from_string("Bow: [^\\n]*Range: (\\d+)( \\(([+-]\\d+)\\))? meters").search(String(b["stats_tooltip"]))
	print("    range ", rng_m.get_string(0) if rng_m else "none")
	check(rng_m != null and (rng_m.get_string(3) == "" or int(rng_m.get_string(3)) != 0), "its range, with what the ground there adds")
	check("\nResistance:\n" in String(b["stats_tooltip"]), "then its resistance")
	check("\n  Status Effects: Blocks Poisoned" in String(b["stats_tooltip"]), "which blocks Poisoned, as every 0.28 structure's does")
	check("Projectile Limit: " in String(b["stats_tooltip"]) and ", Per Unit: " in String(b["stats_tooltip"]), "its projectile limit, default and per unit")
	check("Population Bonus: " in String(b["stats_tooltip"]) and "\nLoot: " in String(b["stats_tooltip"]), "its population bonus and loot")
	check("Garrison Limit: " in String(b["stats_tooltip"]), "and its garrison limit")
	check(String(b["portrait"]).begins_with("res://assets/textures/ui/archive/portraits/structures/"), "a Civic Center shows its template's 0.28 icon")
	check(String(b["portrait_tooltip"]).ends_with("Left-click to focus. Right-click for more information.") and "Territory root." in String(b["portrait_tooltip"]), "its portrait tooltip gives the template tooltip and the focus click")
	check(String(b["rank_icon"]) == "", "a structure has no rank glyph")
	check(not "Garrison:" in String(b["owner"]) and not "Production:" in String(b["stats"]) and not "Attack:" in String(b["stats"])
			and String(b["extra"]) == "", "no RTS0-only garrison, queue, attack or rally text on a structure")
	check(String(b["title"]) == "Agora (Civic Center)", "an Athenian Civic Center is named as 0.28: \"Agora (Civic Center)\"")
	var parts: Array = b["capture_parts"]
	check(parts.size() == 1 and float(parts[0]["width"]) > 90.0 and absf(Color(parts[0]["color"]).a - 0.5) < 0.01, "a fully held Civic Center's capture bar is one part in the owner's colour at half alpha")

	main.debug_inspector("field")
	await frames(40) # a few turns: the field's supply comes with the next tick_gather
	var fd: Dictionary = main.debug_inspector("field")
	print("field: ", fd)
	check(String(fd["resource"]) == "Food: ∞", "a field's resource bar reads \"Food:\" and \"∞\"")
	check(String(fd["stats"]).ends_with("0 / 5") or String(fd["stats"]).contains("0 / "), "and its gatherers show, current / max")

	var r: Dictionary = main.debug_inspector("resource")
	print("resource: ", r)
	check(String(r["resource"]).begins_with("Wood: "), "a tree's resource bar reads \"Wood:\" and its supply (a tree keeps no max)")
	check(String(r["stats"]).split("\n").size() == 1 and " / " in String(r["stats"]), "and its gatherers, current / max")
	check(String(r["stats_tooltip"]) == "Current/max gatherers", "with 0.28's gatherers tooltip")
	check(String(r["portrait"]) == "res://assets/textures/ui/archive/portraits/gaia/flora_tree_generic.png", "a tree shows template_gaia_tree's icon")
	check(String(r["title"]).ends_with(" (Tree)") or String(r["title"]) == "Tree", "a tree is named \"Specific (Tree)\" as getEntityNames")
	check(String(r["portrait_tooltip"]).contains("Chop trees for wood."), "a tree's portrait tooltip gives template_gaia_tree's tooltip")

	var f: Dictionary = main.debug_inspector("fauna")
	print("fauna: ", f)
	check(String(f["resource"]).begins_with("Food: ") and " / " in String(f["resource"]), "an animal's resource bar shows its food, amount / max")
	check(String(f["stats_tooltip"]).contains("Resistance:"), "and its resistance")
	check(String(f["stats_tooltip"]).contains("\nSpeed: ") and " Walk, " in String(f["stats_tooltip"]), "and its speed (getSpeedTooltip)")
	check(String(f["portrait"]).begins_with("res://assets/textures/ui/archive/portraits/gaia/fauna_"), "an animal shows its template's icon")
	check(String(f["owner"]) == "Gaia" and String(f["owner_tooltip"]) == "" and Color(f["owner_color"]).a == 0.0, "Gaia has no colour, emblem or tooltip")

	var ram: PackedInt32Array = main.debug_spawn_units("athen/siege_ram", 1)
	var crew: PackedInt32Array = main.debug_spawn_units("athen/infantry_spearman_b", 2)
	main.debug_put_inside(crew, ram[0])
	var rt: Dictionary = {}
	for i in 30: # they walk aboard
		await frames(10)
		rt = main.debug_inspector(str(ram[0]))
		if "Garrison Size: 1 + 2" in String(rt["stats_tooltip"]):
			break
	print("ram: ", String(rt["stats_tooltip"]).replace("\n", " | "))
	check("\nGarrison Limit: " in String(rt["stats_tooltip"]), "a ram's tooltip gives its Garrison Limit (getGarrisonTooltip)")
	check("\nGarrison Size: 1 + 2" in String(rt["stats_tooltip"]), "and its Garrison Size with what it carries")
	var tower: PackedInt32Array = main.debug_spawn_units("mace/siege_tower", 1)
	await frames(2)
	var tw: Dictionary = main.debug_inspector(str(tower[0]))
	check("\nProjectile Limit: 10, Default: 0, Per Unit: 1" in String(tw["stats_tooltip"]), "a siege tower's tooltip gives its projectiles (getProjectilesTooltip)")

	main.debug_spawn_fauna(2, 2, true) # FAUNA_SHEEP
	main.debug_spawn_fauna(31, 1, true) # FAUNA_GOAT
	await frames(2)
	var ls: Dictionary = main.debug_inspector("livestock")
	print("livestock: ", ls["title"], " ", ls["owner"], " ", ls["health"])
	check(String(ls["title"]) == "3 units selected" and String(ls["owner"]) == "2 types", "several of the player's livestock show displayMultiple: count and kinds")
	check(String(ls["count"]) == "3" and String(ls["health_multiple"]) != "", "displayMultiple: numberOfUnits in large text beside a vertical health bar")
	check(String(ls["health"]).begins_with("Health: "), "with their summed health")
	var lg: Dictionary = main.debug_selection_grid(false)
	var licons: Array = lg["icons"]
	check(licons.size() == 2 and String(licons[0]["tip"]).begins_with("Sheep"), "one icon per kind of animal")
	(instance_from_id(licons[0]["id"]) as Button).pressed.emit()
	await frames(2)
	check(int(main.debug_selection_grid(false)["selected"]) == 2, "a click on the sheep keeps only the sheep")

	var rl: Dictionary = main.debug_inspector("relic")
	print("relic: ", rl["title"], " ", rl["capture_parts"].size())
	var rparts: Array = rl["capture_parts"]
	check(rparts.size() == 1 and float(rparts[0]["width"]) > 90.0, "a Gaia relic shows its capture bar, all Gaia's (captureSection)")

	print("e2e_selection_details: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(0 if failures == 0 else 1)
