extends SceneTree
# The ported 0.28 skirmish maps' skirmish/structures/default_X pieces load as
# the civ's own X (SkirmishReplacer.js): sentry towers, outposts and fields keep
# their type, each civ keeps only the default house of its own size (Han
# neither), the Spartans drop the default walls, and only Iberians keep the
# iber_wall_* starting walls. Gaia structures load by template with their template's civ
# (a captured camp or CC trains its {native} roster): Kushite camps and outposts on Nubian
# Frontier, the Ashoka pillars and the Ptolemaic military colony on Egypt, the Gallic fortress
# on Alpine Mountains.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_skirmish_structures.gd
# Exit code 0 = pass.

var failures := 0

func check(cond: bool, what: String) -> void:
	if cond:
		print("  ok   ", what)
	else:
		print("  FAIL ", what)
		failures += 1

func _initialize() -> void:
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

# Loads a map with the given slot civs; {player: {structure name: count}}.
func structures(id: String, civs: Array, with_civ := false) -> Dictionary:
	var ms = root.get_node("MatchSettings")
	ms.map_id = id
	ms.seed = 1234
	ms.slot_types = []
	ms.slot_civs = []
	ms.slot_teams = []
	for i in civs.size():
		ms.slot_types.append(0 if i == 0 else 1)
		ms.slot_civs.append(civs[i])
		ms.slot_teams.append(0)
	ms.victory = 3
	ms.replay_dir = ""
	ms.load_save_path = ""
	change_scene_to_file("res://scenes/main.tscn")
	await frames(5)
	var m = current_scene
	if m == null or not m.has_method("debug_structure_types"):
		check(false, "%s: the match scene starts" % id)
		return {}
	return m.debug_structure_types(with_civ)

func n(d: Dictionary, p: int, name: String) -> int:
	return int(d.get(p, {}).get(name, 0))

func run() -> void:
	# Civ ids: 0 athen, 1 spart, 6 brit, 8 iber, 11 ptol, 13 han.
	var y := await structures("yangtze", [0, 1])
	check(n(y, 0, "Sentry Tower") == 4 and n(y, 1, "Sentry Tower") == 4, "yangtze: 4 sentry towers each (%s)" % str(y))
	var dc := await structures("dueling_cliffs", [0, 0, 0, 0, 0, 0])
	check(n(dc, 0, "Outpost") == 1 and n(dc, 3, "Outpost") == 1, "dueling_cliffs: an outpost for players 1-4")
	var sp := await structures("sandbox_ptolemies_02", [11, 11, 11])
	check(n(sp, 0, "Field") == 14 and n(sp, 0, "Farmstead") == 1, "sandbox_ptolemies_02: 9 + 5 default fields and 1 farmstead (%d, %d)" % [n(sp, 0, "Field"), n(sp, 0, "Farmstead")])
	var athen := await structures("skirmish_demo", [0, 0])
	var brit := await structures("skirmish_demo", [6, 0])
	var han := await structures("skirmish_demo", [13, 0])
	var spart := await structures("skirmish_demo", [1, 0])
	print("  athen ", athen.get(0, {}))
	print("  han   ", han.get(0, {}))
	print("  spart ", spart.get(0, {}))
	check(n(athen, 0, "Storehouse") >= 1 and n(athen, 0, "Forge") >= 1, "skirmish_demo: storehouse and forge keep their type")
	var h := n(han, 0, "House")
	check(n(athen, 0, "House") == h + 4 and n(brit, 0, "House") == h + 8, "skirmish_demo houses: athen +4 (house_10), brit +8 (house_5), han none (%d/%d/%d)" % [n(athen, 0, "House"), n(brit, 0, "House"), h])
	var walls := func(d: Dictionary) -> int:
		var c := 0
		for k in d.get(0, {}):
			if String(k).to_lower().contains("wall") or String(k).to_lower().contains("gate"):
				c += int(d[0][k])
		return c
	check(walls.call(athen) > 0 and walls.call(spart) < walls.call(athen), "skirmish_demo: the Spartans drop the default walls (%d vs %d)" % [walls.call(spart), walls.call(athen)])
	# skirmish/structures/iber_wall_*: only the Iberians' SkirmishReplacements keep them.
	var ab := await structures("acropolis_bay", [8, 0])
	var ab2 := await structures("acropolis_bay", [0, 8])
	var walls_of := func(d: Dictionary, p: int) -> int:
		var c := 0
		for k in d.get(p, {}):
			if String(k).to_lower().contains("wall") or String(k).to_lower().contains("gate"):
				c += int(d[p][k])
		return c
	check(walls_of.call(ab, 0) > 0 and walls_of.call(ab, 1) == 0 and walls_of.call(ab2, 0) == 0 and walls_of.call(ab2, 1) > 0,
			"acropolis_bay: only the Iberian keeps the starting walls (%s / %s)" % [str(ab), str(ab2)])
	# Gaia (player 254) structures by template, "name/civ".
	var nf := await structures("nubian_frontier", [0, 0], true)
	print("  nubian_frontier gaia ", nf.get(254, {}))
	check(n(nf, 254, "Blemmye Camp/kush") == 5 and n(nf, 254, "Noba Village/kush") == 2 and n(nf, 254, "Outpost/kush") == 10,
			"nubian_frontier: 5 Blemmye and 2 Noba camps, 10 outposts, all Kushite")
	var eg := await structures("egypt", [0, 0, 0, 0, 0, 0], true)
	print("  egypt gaia ", eg.get(254, {}))
	check(n(eg, 254, "Edict Pillar of Ashoka/maur") == 4 and n(eg, 254, "Military Colony/ptol") >= 1,
			"egypt: 4 Ashoka pillars and the Ptolemaic military colony (not a civic center)")
	var am := await structures("alpine_mountains", [0, 0, 0], true)
	check(n(am, 254, "Fortress/gaul") == 1, "alpine_mountains: the Gaia Gallic fortress (%s)" % str(am.get(254, {})))
	change_scene_to_file("res://scenes/menu/main_menu.tscn")
	await frames(3)
	print("e2e_skirmish_structures: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
