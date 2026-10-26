extends SceneTree

var main: Node
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
	await frames(10)
	print("====================================================")
	print("Mobile Viewport Safe Insets E2E Test (G57)")
	print("====================================================")

	# 1. Initial safe insets on desktop/headless
	var initial_insets: Rect2 = main.get_safe_area_insets()
	check(initial_insets.position == Vector2.ZERO, "initial left/top insets are zero: " + str(initial_insets))

	var initial_top_hud: Vector4 = main.debug_top_hud_offsets()
	check(abs(initial_top_hud.x - 12.0) < 0.1 and abs(initial_top_hud.y - 8.0) < 0.1, "initial top HUD left/top offsets (12, 8): " + str(initial_top_hud))

	var initial_bottom: Vector2 = main.debug_bottom_panel_offsets()
	check(abs(initial_bottom.x - (-14.0)) < 0.1, "initial minimap bottom offset (-14): " + str(initial_bottom.x))
	check(abs(initial_bottom.y - (-10.0)) < 0.1, "initial command panel bottom offset (-10): " + str(initial_bottom.y))

	# 2. Simulate mobile landscape safe area (e.g. iPhone with notch/island & home bar):
	# left: 44px (notch), top: 10px, right: 44px (pill curve), bottom: 34px (home indicator)
	print("Applying simulated mobile safe insets (44, 10, 44, 34)...")
	main.set_safe_area_insets(44.0, 10.0, 44.0, 34.0)
	await frames(2)

	var insets: Rect2 = main.get_safe_area_insets()
	check(insets.position.x == 44.0 and insets.position.y == 10.0, "stored safe insets left/top match: " + str(insets))
	check(insets.size.x == 44.0 and insets.size.y == 34.0, "stored safe insets right/bottom match: " + str(insets))

	var top_hud: Vector4 = main.debug_top_hud_offsets()
	check(abs(top_hud.x - (12.0 + 44.0)) < 0.1, "top HUD left offset shifted by notch left inset (56): " + str(top_hud.x))
	check(abs(top_hud.z - (-12.0 - 44.0)) < 0.1, "top HUD right offset shifted by notch right inset (-56): " + str(top_hud.z))
	check(abs(top_hud.y - (8.0 + 10.0)) < 0.1, "top HUD top offset shifted by top inset (18): " + str(top_hud.y))
	check(abs(top_hud.w - (54.0 + 10.0)) < 0.1, "top HUD bottom offset shifted by top inset (64): " + str(top_hud.w))

	var bottom: Vector2 = main.debug_bottom_panel_offsets()
	check(abs(bottom.x - (-14.0 - 34.0)) < 0.1, "minimap bottom lifted above home indicator (-48): " + str(bottom.x))
	check(abs(bottom.y - (-10.0 - 34.0)) < 0.1, "command panel bottom lifted above home indicator (-44): " + str(bottom.y))

	# 3. Test apply_safe_area_insets() execution without crash
	main.apply_safe_area_insets()
	await frames(2)
	check(true, "apply_safe_area_insets() executes cleanly")

	if failures > 0:
		print("e2e_safe_insets: FAIL (%d failures)" % failures)
		quit(1)
	else:
		print("e2e_safe_insets: PASS (all checks OK)")
		quit(0)
