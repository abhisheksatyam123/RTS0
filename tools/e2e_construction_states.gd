extends SceneTree
# selection_panels.js Construction setupButton: a structure the player cannot afford is
# disabled under a red mask and its tooltip says what is missing; one whose requirements
# are unmet, or whose entity limit is reached, is disabled and grey.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_construction_states.gd
# Exit code 0 = pass.

const BLD_WONDER := 10
const RES_WOOD := 0

var main
var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	root.get_node("MatchSettings").cheats = true
	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func cheat(c: String) -> void:
	main.toggle_chat()
	await frames(2)
	var le := root.gui_get_focus_owner() as LineEdit
	if le:
		le.text = c
	main._on_chat_text_submitted(c)
	await frames(20)

func button(prefix: String) -> Dictionary:
	for e in main.debug_command_row():
		if str(e["text"]).begins_with(prefix):
			return e
	return {}

func run() -> void:
	await frames(60)
	var women: PackedInt32Array = main.debug_spawn_units("athen/support_female_citizen", 1)
	main.debug_select_units(women)
	await frames(4)
	print("    ", main.debug_command_row().map(func(e): return e["text"]))
	var house := button("House")
	check(not house.is_empty() and not house["disabled"] and not house["masked"] and not house["grey"], "House: enabled")
	var wonder := button("Wonder")
	check(not wonder.is_empty() and wonder["disabled"] and wonder["grey"] and str(wonder["tip"]).contains("Requires"), "Wonder before City Phase: grey, Requires")

	print("2. No wood")
	main.debug_set_resource(RES_WOOD, 0)
	await frames(4)
	house = button("House")
	print("    ", house.get("tip", ""))
	check(house["disabled"] and house["masked"] and not house["grey"], "House: disabled under the red mask")
	check(str(house["tip"]).contains("Insufficient resources"), "the tooltip lists what is missing")

	print("3. The Wonder limit")
	await cheat("back to the future")
	await cheat("back to the future")
	await cheat("i want pizza 50000")
	main.debug_spawn_building(BLD_WONDER, 60.0, 60.0)
	main.debug_select_units(women)
	await frames(6)
	wonder = button("Wonder")
	print("    ", wonder.get("tip", ""))
	check(wonder["disabled"] and wonder["grey"] and not wonder["masked"] and str(wonder["tip"]).contains("Current Count: 1, Limit: 1."), "a second Wonder: grey at its limit")
	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
