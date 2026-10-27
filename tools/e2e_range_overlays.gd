extends SceneTree
# 0.28's range overlays for the selection (RangeOverlayManager.js): Auras.js draws a ring
# for every applied range aura, on structures as well as units, at the range the aura
# reaches; Attack.js draws the MinRange and MaxRange rings of every template with an
# Attack RangeOverlay, siege engines included.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_range_overlays.gd
# Exit code 0 = pass.

const BLD_TOWER := 4
const BLD_TEMPLE := 9
const SCALE := 0.28 # COMBAT_DISTANCE_SCALE: 0 A.D. metres of a range to RTS0 units

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

func has_ring(rings: PackedFloat32Array, r: float) -> bool:
	for x in rings:
		if absf(x - r) < 0.05:
			return true
	return false

func rings_of_structure(id: int) -> PackedFloat32Array:
	main.debug_select_structures(PackedInt32Array([id]))
	await frames(4)
	return main.debug_range_rings()

func rings_of_units(ids: PackedInt32Array) -> PackedFloat32Array:
	main.debug_select_units(ids)
	await frames(4)
	return main.debug_range_rings()

func run() -> void:
	await frames(60)
	var temple: int = main.debug_spawn_building(BLD_TEMPLE, -30.0, 10.0)
	await frames(30)
	var r := await rings_of_structure(temple)
	check(has_ring(r, 40.0 * SCALE), "a temple shows its 40 m healing aura ring: " + str(r))

	# StatusBars.js AddAuraIcons draws each aura's own overlayIcon: the Rotary Mill's
	# farmstead_60 (farm_bonus), not another table's row.
	var mill: int = main.debug_spawn_building(86, -30.0, -30.0)
	await frames(30)
	main.debug_select_structures(PackedInt32Array([mill]))
	await frames(4)
	var shown: PackedStringArray = main.debug_status_aura_sources()
	check(shown.has("%d:farmstead_60" % mill), "a selected Rotary Mill shows its farmstead_60 aura icon: " + str(shown))
	var vesta: int = main.debug_spawn_building(49, -50.0, 30.0)
	await frames(30)
	main.debug_select_structures(PackedInt32Array([vesta]))
	await frames(4)
	shown = main.debug_status_aura_sources()
	check(shown.has("%d:eternal_fire" % vesta) and not shown.has("%d:athen_catafalque_1" % vesta),
			"the Temple of Vesta shows eternal_fire, not another aura's icon: " + str(shown))

	var tower: int = main.debug_spawn_building(BLD_TOWER, 30.0, -10.0)
	await frames(30)
	r = await rings_of_structure(tower)
	check(r.size() == 2 and r[1] > 0.0 and r[1] < r[0], "a tower shows its MinRange ring inside its MaxRange ring: " + str(r))

	var onager: PackedInt32Array = main.debug_spawn_units("rome/siege_onager_packed", 1)
	await frames(10)
	r = await rings_of_units(onager)
	check(r.size() == 2 and absf(r[1] / r[0] - 1.0 / 3.0) < 0.01, "an onager shows its attack range, 20 of 60 m inside: " + str(r))

	var boiorix: PackedInt32Array = main.debug_spawn_units("germ/hero_boiorix", 1)
	await frames(30)
	r = await rings_of_units(boiorix)
	check(has_ring(r, 60.0 * SCALE), "Boiorix's High Ground ring is at the 60 m it reaches: " + str(r))

	var spear: PackedInt32Array = main.debug_spawn_units("athen/infantry_spearman_b", 1)
	await frames(10)
	r = await rings_of_units(spear)
	check(r.is_empty(), "a spearman has no range rings: " + str(r))

	# GuiInterface.SetBuildingPlacementPreview: a tower being placed shows its rings too.
	main.clear_selection()
	main.enter_build_mode(true, BLD_TOWER)
	var mm := InputEventMouseMotion.new()
	mm.position = root.get_final_transform() * Vector2(512, 400)
	mm.global_position = mm.position
	Input.parse_input_event(mm)
	await frames(6)
	r = main.debug_range_rings()
	check(r.size() == 2 and r[1] > 0.0 and r[1] < r[0], "a tower placement preview shows its MinRange and MaxRange rings: " + str(r))
	main.enter_build_mode(false)
	await frames(3)
	check(main.debug_range_rings().is_empty(), "the preview rings go with the preview")

	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures > 0 else 0)
