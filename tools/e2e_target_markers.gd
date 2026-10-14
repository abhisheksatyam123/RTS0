extends SceneTree
# 0.28's target markers (GuiInterface AddTargetMarker): a walk order leaves
# special/target_marker, the four arrows, for 0.5 s, and a flare (K + click)
# special/flare_target_marker for the flare lifetime, both drawn with their 0 A.D.
# actor and gone when their Decay runs out.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_target_markers.gd
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

func key(code: Key, down: bool) -> void:
	var k := InputEventKey.new()
	k.keycode = code
	k.physical_keycode = code
	k.pressed = down
	Input.parse_input_event(k)

func actors() -> Array:
	return main.find_children("*", "AnimationPlayer", true, false).filter(func(a): return a.is_playing() and String(a.get_parent().name).contains("marker"))

func run() -> void:
	await frames(60)
	main.select_first_own(false)
	await frames(2)
	check(not Array(main.debug_selection_state()["selected"]).is_empty(), "a unit is selected")
	main._on_minimap_order(Vector2(40, 40))
	await frames(1)
	check(main.get_target_marker_count() == 1, "a walk order leaves a target marker")
	check(main.get_target_marker_actor_count() == 1, "drawn with the target_marker actor")
	check(actors().size() >= 1, "playing its Idle animation")
	await frames(12) # 0.6 s at 20 fps
	check(main.get_target_marker_count() == 0, "gone after 0.5 s")

	key(KEY_K, true)
	await frames(1)
	main._on_minimap_order(Vector2(60, 60))
	key(KEY_K, false)
	await frames(1)
	check(main.get_target_marker_count() == 1 and main.get_target_marker_actor_count() == 1, "a flare leaves the flare_target_marker actor")
	await frames(40) # 2 s
	check(main.get_target_marker_count() == 1, "still there after 2 s")
	await frames(90) # past the 6 s lifetime
	check(main.get_target_marker_count() == 0 and actors().is_empty(), "gone after its lifetime")

	print("RESULT: %s" % ("PASS" if failures == 0 else "FAIL (%d)" % failures))
	quit(0 if failures == 0 else 1)
