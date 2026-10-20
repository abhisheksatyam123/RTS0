extends SceneTree
# unit_actions.js "set-rallypoint" getActionInfo: with a structure selected, the cursor shows
# what the trained units will do at the rally point: action-garrison with "Current garrison:
# x/y" over an own holder, action-capture over an enemy structure, the plain rally cursor on
# open ground. With session.focusfire (F) held an attacking structure aims instead
# ("action-target"), and with session.flare (K) held the cursor is input.js's
# "action-player-flare".
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_rally_cursor.gd
# Exit code 0 = pass.

const BLD_SENTRY_TOWER := 19
const BLD_HOUSE := 1

var main
var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	# One map for every run: the random default's trees can stand under the points below.
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

func move(pos: Vector2) -> void:
	var mm := InputEventMouseMotion.new()
	mm.position = root.get_final_transform() * pos
	mm.global_position = mm.position
	Input.parse_input_event(mm)
	await frames(2)

func hold(code: Key, on: bool) -> void:
	var k := InputEventKey.new()
	k.keycode = code
	k.physical_keycode = code
	k.pressed = on
	Input.parse_input_event(k)
	await frames(3)

func screen_of(id: int) -> Vector2:
	main.debug_select_entity(id)
	await frames(2)
	return main.debug_selected_building()["screen"]

func run() -> void:
	await frames(60)
	await move(Vector2(512, 400))
	var r: Dictionary = main.debug_civic_centre_roof()
	for i in 200:
		await frames(1)
		var r2: Dictionary = main.debug_civic_centre_roof()
		var still: bool = (r2["roof"] as Vector2).distance_to(r["roof"]) < 0.5
		r = r2
		if still and i > 10:
			break
	var tower: int = main.debug_spawn_building(BLD_SENTRY_TOWER, 18.0, 0.0)
	var enemy: int = main.debug_spawn_building(BLD_HOUSE, 0.0, -18.0, 1)
	await frames(4)
	var tower_at: Vector2 = await screen_of(tower)
	var enemy_at: Vector2 = await screen_of(enemy)
	var cc: int = main.debug_civic_centre_id(0)
	main.debug_select_entity(cc)
	await frames(2)

	print("1. Over an own tower")
	await move(tower_at)
	var t: Dictionary = main.debug_cursor_tip()
	print("    ", t)
	check(t["cursor"] == "action-garrison", "the garrison cursor")
	check(t["visible"] and str(t["text"]).begins_with("Current garrison: 0/"), "Current garrison: 0/capacity")

	print("2. Over an enemy house")
	await move(enemy_at)
	t = main.debug_cursor_tip()
	check(t["cursor"] == "action-capture", "the capture cursor: " + str(t["cursor"]))

	print("3. Over open ground")
	await move(tower_at + (tower_at - enemy_at).normalized() * 160.0)
	t = main.debug_cursor_tip()
	check(t["cursor"] == "cursor-rally", "the rally cursor: " + str(t["cursor"]))

	print("4. F held: the Civic Center aims its arrows")
	await hold(KEY_F, true)
	await move(enemy_at)
	t = main.debug_cursor_tip()
	check(t["cursor"] == "action-target", "the target cursor: " + str(t["cursor"]))
	await hold(KEY_F, false)
	await move(enemy_at + Vector2(1, 0))
	t = main.debug_cursor_tip()
	check(t["cursor"] == "action-capture", "back to capture on release: " + str(t["cursor"]))

	print("5. K held: the player's flare cursor")
	await hold(KEY_K, true)
	await move(tower_at)
	t = main.debug_cursor_tip()
	check(t["cursor"] == "action-player-flare", "the player flare cursor: " + str(t["cursor"]))
	await hold(KEY_K, false)

	print("FAILURES: ", failures)
	quit(1 if failures else 0)
