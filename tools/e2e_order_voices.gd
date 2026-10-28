extends SceneTree
# 0.28's order voices are the sound groups a unit's template names
# (voice/{lang}/civ/civ_{phenotype}_<group>.xml), each listing its own clips
# (data/voice_groups.json): an Athenian hoplite told to board a ram says the
# Greek men's garrison group, "As you wish" (civ_male_Asyouwish_1), not the
# women's garrison line; an Athenian woman says civ_female_garrison_1 or _2.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_order_voices.gd
# Exit code 0 = pass.

var main
var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	var ms = root.get_node("MatchSettings")
	ms.slot_civs = [0, 3]
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

func key(code: int, down: bool) -> void:
	var k := InputEventKey.new()
	k.keycode = code
	k.physical_keycode = code
	k.ctrl_pressed = down and code == KEY_CTRL
	k.pressed = down
	Input.parse_input_event(k)
	await frames(1)

func click(button: int, pos: Vector2) -> void:
	await move(pos)
	for down in [true, false]:
		var mb := InputEventMouseButton.new()
		mb.button_index = button
		mb.pressed = down
		mb.position = pos
		mb.global_position = pos
		root.push_input(mb, true)
		await frames(1)

# Board the ram with the new unit (session.garrison: Ctrl + right click, unit_actions.js
# "garrison", order_garrison) and return the sound that order played.
func garrison_voice(template: String, ram: int) -> String:
	var ids: PackedInt32Array = main.debug_spawn_units(template, 1)
	await frames(10)
	main.debug_select_units(ids)
	await frames(2)
	var at: Vector2 = main.debug_unit_screen(ram)
	await move(at)
	await key(KEY_CTRL, true)
	for i in 40:
		at = main.debug_unit_screen(ram)
		await move(at)
		if main.debug_cursor_tip()["cursor"] == "action-garrison":
			break
	await click(MOUSE_BUTTON_RIGHT, at)
	var snd := str(main.get_audio_state().last_sound)
	await key(KEY_CTRL, false)
	return snd

func run() -> void:
	await frames(60)
	var r: Dictionary = main.debug_civic_centre_roof()
	for i in 200:
		await frames(1)
		var r2: Dictionary = main.debug_civic_centre_roof()
		var still: bool = (r2["roof"] as Vector2).distance_to(r["roof"]) < 0.5
		r = r2
		if still and i > 10:
			break
	var ram := int(main.debug_spawn_units("athen/siege_ram", 1)[0])

	print("1. A Greek man's garrison order")
	var snd := await garrison_voice("athen/infantry_spearman_b", ram)
	check(snd.ends_with("voice/greek/civ/civ_male_Asyouwish_1.ogg"), "civ_male_garrison.xml's one clip: " + snd)

	print("2. A Greek woman's garrison order")
	snd = await garrison_voice("athen/support_female_citizen", ram)
	check(snd.contains("voice/greek/civ/civ_female_garrison_"), "civ_female_garrison.xml: " + snd)

	print("PASS" if failures == 0 else "FAILURES: %d" % failures)
	quit(1 if failures else 0)
