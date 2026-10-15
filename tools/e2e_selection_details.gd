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
	check(String(u["health"]).begins_with("Health: ") and " / " in String(u["health"]), "health reads \"Health: current / max\"")
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

	var elite: PackedInt32Array = main.debug_spawn_units("spart/champion_infantry_swordsman", 1)
	await frames(2)
	var e: Dictionary = main.debug_inspector(str(elite[0]))
	check(String(e["rank_icon"]).ends_with("/ranks/Elite.png") and String(e["rank_tooltip"]).begins_with("Elite Rank\nElite units +20% training time"), "an Elite champion's rank icon gives the unit_elite bonus")
	var m: Dictionary = main.debug_inspector("multi")
	print("multi: ", m)
	check(String(m["title_tooltip"]).begins_with("Garrison Size: "), "a group's tooltip gives its garrison size")
	check("Loot: " in String(m["title_tooltip"]), "and its loot")

	var b: Dictionary = main.debug_inspector("building")
	print("building: ", b)
	check(String(b["stats_tooltip"]).begins_with("Attack:\n  Bow: ") and " arrows / " in String(b["stats_tooltip"]), "a Civic Center's tooltip opens with its arrows, \"Rate: n arrows / 4 seconds\"")
	check("\nResistance:\n" in String(b["stats_tooltip"]), "then its resistance")
	check("Projectile Limit: " in String(b["stats_tooltip"]) and ", Per Unit: " in String(b["stats_tooltip"]), "its projectile limit, default and per unit")
	check("Population Bonus: " in String(b["stats_tooltip"]) and "\nLoot: " in String(b["stats_tooltip"]), "its population bonus and loot")
	check("Garrison Limit: " in String(b["stats_tooltip"]), "and its garrison limit")
	check(String(b["portrait"]).begins_with("res://assets/textures/ui/archive/portraits/structures/"), "a Civic Center shows its template's 0.28 icon")
	check(String(b["portrait_tooltip"]).ends_with("Left-click to focus. Right-click for more information.") and "Territory root." in String(b["portrait_tooltip"]), "its portrait tooltip gives the template tooltip and the focus click")
	check(String(b["rank_icon"]) == "", "a structure has no rank glyph")
	var parts: Array = b["capture_parts"]
	check(parts.size() == 1 and float(parts[0]["width"]) > 300.0 and absf(Color(parts[0]["color"]).a - 0.5) < 0.01, "a fully held Civic Center's capture bar is one part in the owner's colour at half alpha")

	var r: Dictionary = main.debug_inspector("resource")
	print("resource: ", r)
	check(String(r["stats"]).begins_with("Wood: "), "a tree shows \"Wood:\" and its supply")
	check(String(r["stats"]).split("\n").size() == 2 and " / " in String(r["stats"]).split("\n")[1], "and its gatherers, current / max")
	check(String(r["stats_tooltip"]) == "Current/max gatherers", "with 0.28's gatherers tooltip")
	check(String(r["portrait"]) == "res://assets/textures/ui/archive/portraits/gaia/flora_tree_generic.png", "a tree shows template_gaia_tree's icon")
	check(String(r["portrait_tooltip"]).contains("Chop trees for wood."), "a tree's portrait tooltip gives template_gaia_tree's tooltip")

	var f: Dictionary = main.debug_inspector("fauna")
	print("fauna: ", f)
	check(String(f["extra"]).begins_with("Food: ") and " / " in String(f["extra"]), "an animal shows its food, amount / max")
	check(String(f["stats_tooltip"]).contains("Resistance:"), "and its resistance")
	check(String(f["portrait"]).begins_with("res://assets/textures/ui/archive/portraits/gaia/fauna_"), "an animal shows its template's icon")
	check(String(f["owner"]) == "Gaia" and String(f["owner_tooltip"]) == "" and Color(f["owner_color"]).a == 0.0, "Gaia has no colour, emblem or tooltip")

	print("e2e_selection_details: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(0 if failures == 0 else 1)
