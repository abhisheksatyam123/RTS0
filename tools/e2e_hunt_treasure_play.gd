extends SceneTree
# Hunting and treasure played through the session's own mouse input, as 0 A.D. 0.28's
# gui/session/input.js and UnitAI do it:
#   1. a javelin cavalryman selected by a click, then a right-click on a deer (Gaia)
#      orders a hunt: the order targets the deer, the deer dies, and the cavalryman brings
#      the meat home, so the food stock rises;
#   2. a spearman selected by a click, then a right-click on a treasure collects it: the
#      treasure leaves the map and the stock of its resource rises.
# Every step that errors or does nothing is a FAIL. The numbers are checks that the
# behaviour happens, not 0.28's exact timings.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_hunt_treasure_play.gd
# Exit code 0 = pass.

const RES_FOOD := 3
const ACTION_ATTACK := 2
const FAUNA_DEER := 0
const TREASURE_FOOD_BARREL := 0

var main
var failures := 0

func check(cond: bool, what: String) -> bool:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1
	return cond

func _initialize() -> void:
	var ms = root.get_node("MatchSettings")
	ms.map_id = "random_continental"
	ms.seed = 11
	ms.map_size = 128
	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

# Waits until `cond` holds: by game frames under --fixed-fps 20, or by wall time without it.
func until(cond: Callable, seconds := 5.0) -> void:
	var end := Time.get_ticks_msec() + int(seconds * 1000.0)
	var n := 0
	while not cond.call() and (n < int(seconds * 20.0) or Time.get_ticks_msec() < end):
		await process_frame
		n += 1

func button_event(pos: Vector2, button: MouseButton, down: bool) -> void:
	var mb := InputEventMouseButton.new()
	mb.button_index = button
	mb.pressed = down
	mb.position = pos
	mb.global_position = pos
	root.push_input(mb, true)
	await frames(1)

# Centres the camera on a world point with the minimap's own click handler, as a person would.
func look_at(world: Vector2) -> void:
	main._on_minimap_clicked(world)
	await frames(6)

# Clicks a unit or animal or treasure: first centres the camera on it, then clicks its screen point.
func click_world(world: Vector2, pos_fn: Callable, button: MouseButton) -> void:
	await look_at(world)
	var at: Vector2 = pos_fn.call()
	await button_event(at, button, true)
	await button_event(at, button, false)
	await frames(3)

func food() -> float:
	return float(main.debug_player_state(-1)["res"][RES_FOOD])

func selected(id: int) -> bool:
	return id in main.debug_selection_state()["selected"]

func run() -> void:
	# The mouse rests mid-screen: at the corner it would scroll the camera.
	await frames(60)

	print("1. A cavalryman hunts a deer")
	var cav: int = main.debug_spawn_units("athen/cavalry_javelineer_b", 1)[0]
	var deer: int = main.debug_spawn_fauna(FAUNA_DEER, 1, false, 14.0, 0.0)[0]
	await until(func(): return main.debug_unit_screen(cav) != Vector2(-1, -1) and main.debug_fauna_screen(deer) != Vector2(-1, -1), 5.0)
	check(main.debug_fauna_alive(deer), "the deer is on the map")
	var food0 := food()
	await click_world(main.debug_unit_pos(cav), func(): return main.debug_unit_screen(cav), MOUSE_BUTTON_LEFT)
	check(selected(cav), "the cavalryman is selected by a click")
	await click_world(main.debug_unit_pos(cav), func(): return main.debug_fauna_screen(deer), MOUSE_BUTTON_RIGHT)
	await frames(10)
	var order: Dictionary = main.debug_unit_order(cav)
	# A hunt is the attack action on the animal (no order entry): ACTION_ATTACK, state 2.
	check(int(order.get("state", -1)) == ACTION_ATTACK, "the right-click sends the cavalryman to hunt the deer")
	await until(func(): return not main.debug_fauna_alive(deer), 90.0)
	check(not main.debug_fauna_alive(deer), "the deer dies to the cavalryman")
	await until(func(): return food() > food0, 60.0)
	check(food() > food0, "the cavalryman brings meat home: food %.0f -> %.0f" % [food0, food()])

	print("2. A spearman collects a treasure")
	var spear: int = main.debug_spawn_units("athen/infantry_spearman_b", 1)[0]
	var tid: int = main.debug_spawn_treasure(TREASURE_FOOD_BARREL, 12.0, 6.0)
	await until(func(): return main.debug_unit_screen(spear) != Vector2(-1, -1) and main.debug_treasure_screen(tid) != Vector2(-1, -1), 5.0)
	check(main.debug_treasure_alive(tid), "the treasure is on the map")
	var food1 := food()
	await click_world(main.debug_unit_pos(spear), func(): return main.debug_unit_screen(spear), MOUSE_BUTTON_LEFT)
	check(selected(spear), "the spearman is selected by a click")
	await click_world(main.debug_unit_pos(spear), func(): return main.debug_treasure_screen(tid), MOUSE_BUTTON_RIGHT)
	await frames(10)
	order = main.debug_unit_order(spear)
	check(int(order.get("target", 0)) == tid, "the right-click orders the spearman to collect the treasure")
	await until(func(): return not main.debug_treasure_alive(tid), 60.0)
	check(not main.debug_treasure_alive(tid), "the treasure leaves the map once collected")
	check(food() > food1, "the collector's pickup adds to the stock: food %.0f -> %.0f" % [food1, food()])

	print("FAILURES: ", failures)
	quit(1 if failures > 0 else 0)
