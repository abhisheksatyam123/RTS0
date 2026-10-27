extends SceneTree
# The selection details as 0 A.D. 0.28 gui/session/selection_details.js fills them,
# with gui/common/tooltips.js strings: a unit's health, attack and resistance lines,
# its detailed tooltip (attack, resistance with reduction percentages, gather rates,
# speed, loot), a group's garrison size and loot, a structure's resistance and
# garrison limit, a tree's supply and gatherers, an animal's food.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_selection_details.gd
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

# The panel's strings with their BBCode read as plain text (TT.plain: icons as names).
func insp(what: String) -> Dictionary:
	var d: Dictionary = main.debug_inspector(what)
	for k in d.keys():
		if d[k] is String:
			d[k] = TT.plain(d[k])
	return d

func frames(n: int) -> void:
	for i in n:
		await process_frame

func run() -> void:
	await frames(60)
	var u: Dictionary = insp("unit")
	print("unit: ", u)
	check(u["visible"], "a selected unit shows the details panel")
	check(not String(u["health"]).begins_with("Health: ") and " / " in String(u["health"]), "health reads \"current / max\" (healthStats, rounded up, no label)")
	var tip := String(u["attack_resistance"])
	check(tip.begins_with("Attack:\n"), "the detailed tooltip opens with the attack")
	check("Hack" in tip and "Interval: " in tip and "Range: " in tip, "the attack names its damage types, range and interval")
	check(tip.begins_with("Attack:\n    Capture: ") and "\n    Spear: " in tip, "each attack of the template in its order, under the weapon's name (getAttackTooltip)")
	check("Resistance:\n    Damage: " in tip and "%)" in tip, "resistance lists levels with reduction percentages")
	check("Gather Rates: " in tip and "Speed: " in tip and " Walk, " in tip and " Acceleration" in tip, "gather rates and walk, run and acceleration follow")
	var raw: String = main.debug_inspector("unit")["attack_resistance"] # the icons' BBCode
	check(raw.begins_with("[b]Attack:[/b]\n    [b]") and raw.contains("[color=orange]Hack[/color]") and raw.contains("[color=orange]meters[/color]"),
			"headerFont labels and unitFont damage types and units")
	var gather := raw.substr(raw.find("Gather Rates:"))
	gather = gather.substr(0, gather.find("\n"))
	check("grain_small.png" in gather and gather.find("fruit_small.png") < gather.find("grain_small.png") and gather.find("grain_small.png") < gather.find("wood_small.png"),
			"gather rates show 0.28's subtype icons in data/resources order (getGatherTooltip): " + gather)
	check("Loot: " in tip, "the loot closes the tooltip")
	# sprites.xml: experienceBackground darkgray under a white foreground, in barBorder and the lighting.
	var xp: ColorRect = main.find_child("experience", true, false)
	check(xp != null and xp.color.is_equal_approx(Color8(169, 169, 169)) and (xp.get_child(0) as ColorRect).color == Color.WHITE
			and xp.find_child("barBorder", false, false) != null and xp.find_child("statsBarShader", false, false) != null, "the experience bar's 0.28 sprites")
	var rb: ProgressBar = main.find_child("resourceBar", true, false)
	check(rb != null and (rb.get_theme_stylebox("fill") as StyleBoxFlat).bg_color.is_equal_approx(Color(1, 165 / 255.0, 0))
			and (rb.get_theme_stylebox("background") as StyleBoxFlat).bg_color.is_equal_approx(Color8(169, 169, 169)), "resourceForeground orange on darkgray")
	var portrait_raw: String = main.debug_inspector("unit")["portrait_tooltip"]
	check(portrait_raw.begins_with("[font_size=16][b]") and portrait_raw.contains("[b]Classes:[/b] "), "the portrait tooltip's name in namePrimaryBig, Classes: in headerFont")
	check(String(u["stats"]) == "" and String(u["attack_resistance"]).contains("Damage: "), "attack and resistance are the stance icon's tooltip, not text (attackAndResistanceStats)")
	var ptip := String(u["portrait_tooltip"])
	check(ptip.begins_with(String(u["title"]).get_slice(" (", 0)) and not ptip.split("\n")[0].ends_with(")"),
		"the portrait tooltip opens with the unit's primary name only")
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
	check(" — " in String(u["owner_tooltip"]) and "Village Phase" in String(u["owner_tooltip"]), "its tooltip names the civ and, for the viewer, the phase")
	# getEntityNames of phase_village: every civ but the Germans has a specific name.
	check(String(u["owner_tooltip"]).ends_with("(Village Phase)") or String(u["owner_tooltip"]).begins_with("Germans"),
			"the phase is the civ's own name: " + String(u["owner_tooltip"]))
	check(String(u["owner_phase"]).begins_with("res://assets/textures/ui/archive/panel_phase_emblems_") and not String(u["owner_phase"]).ends_with("hidden.png"), "the viewer's own phase emblems show")
	check(not " (" in String(u["owner"]), "the row names the player, the civ moves to the tooltip")
	check(not "Kills" in String(u["owner"]) and String(u["extra"]) == "", "no RTS0-only kill count or extra line (displaySingle has no free text)")
	check(String(u["xp"]).begins_with("Experience: ") and " / " in String(u["xp"]), "a Basic soldier's experience shows on the portrait's vertical bar")

	var elite: PackedInt32Array = main.debug_spawn_units("spart/champion_infantry_swordsman", 1)
	await frames(2)
	var e: Dictionary = insp(str(elite[0]))
	check(String(e["rank_icon"]).ends_with("/ranks/Elite.png") and String(e["rank_tooltip"]).begins_with("Elite Rank\nElite units +20% training time"), "an Elite champion's rank icon gives the unit_elite bonus")
	var siege: PackedInt32Array = main.debug_spawn_units("athen/siege_lithobolos_packed", 1)
	await frames(2)
	var sg: Dictionary = insp(str(siege[0]))
	print("siege: ", sg["title"])
	check(String(sg["title"]).ends_with(String.chr(0x2014) + " Packed)") or String(sg["title"]).ends_with("(Packed)"), "a packed siege engine adds \"Packed\" to its secondary name")
	var m: Dictionary = insp("multi")
	print("multi: ", m)
	check(String(m["title_tooltip"]).begins_with("Garrison Size: "), "a group's tooltip gives its garrison size")
	check("Loot: " in String(m["title_tooltip"]), "and its loot")
	check(String(m["count"]).is_valid_int() and String(m["count_tooltip"]) == String(m["title_tooltip"]), "numberOfUnits shows the bare count, with that tooltip")
	check(String(m["health_multiple"]).begins_with("Health: ") and String(m["portrait"]) != "", "and a vertical health bar beside it")

	var b: Dictionary = insp("building")
	print("building: ", b)
	check(String(b["attack_resistance"]).begins_with("Attack:\n    Bow: ") and " arrows / " in String(b["attack_resistance"]), "a Civic Center's tooltip opens with its arrows, \"Rate: n arrows / 4 seconds\"")
	# elevationAdaptedRange: the range its arrows' height and the ground round it give, when that
	# differs ("Range: 67 (+3) meters"); the map is random, so the number varies.
	var rng_m := RegEx.create_from_string("Bow: [^\\n]*Range: (\\d+)( \\(([+-]\\d+)\\))? meters").search(String(b["attack_resistance"]))
	print("    range ", rng_m.get_string(0) if rng_m else "none")
	check(rng_m != null and (rng_m.get_string(3) == "" or int(rng_m.get_string(3)) != 0), "its range, with what the ground there adds")
	check("\nResistance:\n" in String(b["attack_resistance"]), "then its resistance")
	check("\n    Status Effects: Blocks Poisoned" in String(b["attack_resistance"]), "which blocks Poisoned, as every 0.28 structure's does")
	check("Projectile Limit: " in String(b["attack_resistance"]) and ", Per Unit: " in String(b["attack_resistance"]), "its projectile limit, default and per unit")
	check("Population Bonus: " in String(b["attack_resistance"]) and "\nLoot: " in String(b["attack_resistance"]), "its population bonus and loot")
	check("Garrison Limit: " in String(b["attack_resistance"]), "and its garrison limit")
	check(String(b["portrait"]).begins_with("res://assets/textures/ui/archive/portraits/structures/"), "a Civic Center shows its template's 0.28 icon")
	check(String(b["portrait_tooltip"]).ends_with("Left-click to focus. Right-click for more information.") and "Territory root." in String(b["portrait_tooltip"]), "its portrait tooltip gives the template tooltip and the focus click")
	check(String(b["rank_icon"]) == "", "a structure has no rank glyph")
	check(not "Garrison:" in String(b["owner"]) and not "Production:" in String(b["stats"]) and not "Attack:" in String(b["stats"])
			and String(b["extra"]) == "", "no RTS0-only garrison, queue, attack or rally text on a structure")
	check(String(b["title"]) == "Agora (Civic Center)", "an Athenian Civic Center is named as 0.28: \"Agora (Civic Center)\"")
	var parts: Array = b["capture_parts"]
	check(parts.size() == 1 and float(parts[0]["width"]) > 90.0 and absf(Color(parts[0]["color"]).a - 0.5) < 0.01, "a fully held Civic Center's capture bar is one part in the owner's colour at half alpha")

	# getAurasTooltip on a structure's portrait (selection_details.js iconBorder).
	var temple: int = main.debug_spawn_building(9, 20.0, -20.0)
	await frames(10)
	var tp: Dictionary = insp("building:%d" % temple)
	check(String(tp["portrait_tooltip"]).contains("Medical Treatment:"), "a temple's portrait tooltip lists its Medical Treatment aura: " + String(tp["portrait_tooltip"]).replace("\n", " | "))

	insp("field")
	await frames(40) # a few turns: the field's supply comes with the next tick_gather
	var fd: Dictionary = insp("field")
	print("field: ", fd)
	check(String(fd["resource"]) == "Food: ∞", "a field's resource bar reads \"Food:\" and \"∞\"")
	check(String(fd["extra"]).ends_with("0 / 5") or String(fd["extra"]).contains("0 / "), "and its gatherers show, current / max")

	var r: Dictionary = insp("resource")
	print("resource: ", r)
	check(String(r["resource"]).begins_with("Wood: "), "a tree's resource bar reads \"Wood:\" and its supply (a tree keeps no max)")
	check(String(r["stats"]) == "" and " / " in String(r["extra"]), "and its gatherers, current / max, in the carrying slot")
	check(String(r["extra_tooltip"]) == "Current/max gatherers" and String(r["extra_icon"]).ends_with("icons/repair.png"),
		"beside the repair icon with 0.28's gatherers tooltip (resourceCarryingIcon)")
	check(String(r["portrait"]) == "res://assets/textures/ui/archive/portraits/gaia/flora_tree_generic.png", "a tree shows template_gaia_tree's icon")
	check(String(r["title"]).ends_with(" (Tree)") or String(r["title"]) == "Tree", "a tree is named \"Specific (Tree)\" as getEntityNames")
	check(String(r["portrait_tooltip"]).contains("Chop trees for wood."), "a tree's portrait tooltip gives template_gaia_tree's tooltip")

	var f: Dictionary = insp("fauna")
	print("fauna: ", f)
	check(String(f["resource"]).begins_with("Food: ") and " / " in String(f["resource"]), "an animal's resource bar shows its food, amount / max")
	check(String(f["attack_resistance"]).contains("Resistance:"), "and its resistance")
	check(String(f["attack_resistance"]).contains("\nSpeed: ") and " Walk, " in String(f["attack_resistance"]) and " Acceleration" in String(f["attack_resistance"]), "and its speed with acceleration (getSpeedTooltip)")
	check(String(f["portrait"]).begins_with("res://assets/textures/ui/archive/portraits/gaia/fauna_"), "an animal shows its template's icon")
	check(String(f["owner"]) == "Gaia" and String(f["owner_tooltip"]) == "" and absf(Color(f["owner_color"]).a - 0.502) < 0.01,
		"Gaia keeps its colour band at alpha 128, with no emblem or tooltip")

	var ram: PackedInt32Array = main.debug_spawn_units("athen/siege_ram", 1)
	var crew: PackedInt32Array = main.debug_spawn_units("athen/infantry_spearman_b", 2)
	main.debug_put_inside(crew, ram[0])
	var rt: Dictionary = {}
	for i in 30: # they walk aboard
		await frames(10)
		rt = insp(str(ram[0]))
		if "Garrison Size: 1 + 2" in String(rt["attack_resistance"]):
			break
	print("ram: ", String(rt["attack_resistance"]).replace("\n", " | "))
	check("\nGarrison Limit: " in String(rt["attack_resistance"]), "a ram's tooltip gives its Garrison Limit (getGarrisonTooltip)")
	check("\nGarrison Size: 1 + 2" in String(rt["attack_resistance"]), "and its Garrison Size with what it carries")
	var tower: PackedInt32Array = main.debug_spawn_units("mace/siege_tower", 1)
	await frames(2)
	var tw: Dictionary = insp(str(tower[0]))
	check("\nProjectile Limit: 10, Default: 0, Per Unit: 1" in String(tw["attack_resistance"]), "a siege tower's tooltip gives its projectiles (getProjectilesTooltip)")

	main.debug_spawn_fauna(2, 2, true) # FAUNA_SHEEP
	main.debug_spawn_fauna(31, 1, true) # FAUNA_GOAT
	await frames(2)
	var ls: Dictionary = insp("livestock")
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

	var rl: Dictionary = insp("relic")
	print("relic: ", rl["title"], " ", rl["capture_parts"].size())
	var rparts: Array = rl["capture_parts"]
	check(rparts.size() == 1 and float(rparts[0]["width"]) > 90.0, "a Gaia relic shows its capture bar, all Gaia's (captureSection)")

	print("e2e_selection_details: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(0 if failures == 0 else 1)
