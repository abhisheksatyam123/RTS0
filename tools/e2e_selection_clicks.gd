extends SceneTree
# Selection and minimap clicks as 0 A.D. 0.28 gui/session/input.js and
# minimap/MiniMap.js handle them:
#   1. double-click selects the clicked unit's template on screen, triple-click
#      the same, Ctrl+double-click removes them again;
#   2. Shift+click only adds (a selected unit stays selected);
#   3. a right-click while band-boxing cancels the box;
#   4. Shift+N adds control group N to the selection;
#   5. a right-click on the minimap walks the selected units there and sets a
#      selected structure's rally point;
#   6. a unit pressed on and walking away before the release is still selected;
#   8. a Ctrl+click on empty ground keeps the selection (input.js:963-975);
#   9. every rank of a template shares one selection icon (selection.js EntityGroups by
#      selectionGroupName), and a click on another entity stops the camera following.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_selection_clicks.gd
# Exit code 0 = pass.

var main
var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	# One map for every run: the random default's coast or trees can stand in the walks below.
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

func key(code: Key, pressed: bool, ctrl := false, shift := false) -> void:
	var k := InputEventKey.new()
	k.keycode = code
	k.physical_keycode = code
	k.pressed = pressed
	k.ctrl_pressed = ctrl
	k.shift_pressed = shift
	Input.parse_input_event(k)

# Motion goes through Input in window pixels, so the session's mouse position
# (edge scrolling) follows; buttons go to the viewport in its own pixels, as
# the headless window is 64 px wide and window pixels land up to 16 px off.
func move(pos: Vector2, mask := 0) -> void:
	var mm := InputEventMouseMotion.new()
	mm.position = root.get_final_transform() * pos
	mm.global_position = mm.position
	mm.button_mask = mask
	Input.parse_input_event(mm)

func button(pos: Vector2, index: MouseButton, pressed: bool, double := false) -> void:
	move(pos)
	var mb := InputEventMouseButton.new()
	mb.button_index = index
	mb.pressed = pressed
	mb.double_click = double
	mb.position = pos
	mb.global_position = pos
	root.push_input(mb, true)

func click(pos: Vector2, double := false) -> void:
	move(pos)
	await frames(2)
	button(pos, MOUSE_BUTTON_LEFT, true, double)
	await frames(1)
	button(pos, MOUSE_BUTTON_LEFT, false)
	await frames(2)

# Game time, not frames: n turns of 200 ms however fast the frames run.
func turns(n: int) -> void:
	var until: int = int(main.get_replay_status().turn) + n
	var guard := 0
	while int(main.get_replay_status().turn) < until and guard < n * 40:
		await process_frame
		guard += 1

func state() -> Dictionary:
	return main.debug_selection_state()

func selected() -> Array:
	return Array(state()["selected"])

func window_pos(u: Dictionary) -> Vector2:
	return u["screen"]

func on_screen(u: Dictionary, vp: Vector2) -> bool:
	var p: Vector2 = u["screen"]
	return not u["behind"] and p.x > 8 and p.y > 8 and p.x < vp.x - 8 and p.y < vp.y - 200

func run() -> void:
	await frames(60)
	# The mouse rests mid-screen: at the corner it would scroll the camera away.
	move(Vector2(512, 400))
	# The camera glides to the Civic Centre; wait until it stops.
	var r: Dictionary = main.debug_civic_centre_roof()
	for i in 200:
		await frames(1)
		var r2: Dictionary = main.debug_civic_centre_roof()
		var still: bool = (r2["roof"] as Vector2).distance_to(r["roof"]) < 0.5
		r = r2
		if still and i > 10:
			break
	# Walk the starting units out in front of the Civic Centre, which hides
	# the ones behind it from clicks.
	main.debug_selection_grid(true)
	var cc: Vector2 = r["centre"]
	main._on_minimap_order(cc + Vector2(0, 10))
	await frames(240)
	main.clear_selection()
	await frames(2)
	var st := state()
	var vp: Vector2 = st["viewport"]
	# The template with most units on screen, and a unit of another template.
	var by_tmpl := {}
	for u in st["units"]:
		if on_screen(u, vp):
			by_tmpl.get_or_add(u["tmpl"], []).append(u)
	var tmpl = null
	for t in by_tmpl:
		if tmpl == null or by_tmpl[t].size() > by_tmpl[tmpl].size():
			tmpl = t
	check(tmpl != null and by_tmpl[tmpl].size() >= 2, "two or more units of one template on screen")
	if tmpl == null or by_tmpl[tmpl].size() < 2:
		print("  CC ", r["centre"], " units ", st["units"].map(func(u): return [u["pos"], u["screen"]]))
		quit(1)
		return
	# Click the units nearest the camera, which nothing stands in front of.
	by_tmpl[tmpl].sort_custom(func(x, y): return x["screen"].y > y["screen"].y)
	var other = null
	for t in by_tmpl:
		if t != tmpl:
			other = by_tmpl[t][0]
	var same: Array = by_tmpl[tmpl].map(func(u): return int(u["id"]))
	var a: Dictionary = by_tmpl[tmpl][0]

	print("1. Double and triple click")
	await click(window_pos(a))
	print("  unit ", a, " at ", window_pos(a), " hovered ", root.gui_get_hovered_control())
	check(selected() == [int(a["id"])], "a click selects one unit: %s" % str(selected()))
	button(window_pos(a), MOUSE_BUTTON_LEFT, true)
	await frames(1)
	button(window_pos(a), MOUSE_BUTTON_LEFT, false)
	await frames(1)
	await click(window_pos(a), true)
	var s := selected()
	check(s.size() >= same.size() and same.all(func(i): return s.has(i)), "double-click selects all %d on-screen units of the template (%d)" % [same.size(), s.size()])
	check(other == null or not s.has(int(other["id"])), "and not another template")
	await click(window_pos(a))
	key(KEY_CTRL, true, true)
	button(window_pos(a), MOUSE_BUTTON_LEFT, true, false)
	await frames(1)
	button(window_pos(a), MOUSE_BUTTON_LEFT, false)
	await frames(1)
	await click(window_pos(a), true)
	await click(window_pos(a))  # triple
	key(KEY_CTRL, false)
	await frames(2)
	s = selected()
	check(not same.any(func(i): return s.has(i)), "Ctrl+double/triple-click removes them: %s" % str(s))

	print("2. Shift+click adds")
	main.clear_selection()
	await frames(20)  # past the triple-click window
	await click(window_pos(a))
	key(KEY_SHIFT, true, false, true)
	await click(window_pos(a))
	var b: Dictionary = by_tmpl[tmpl][1]
	await frames(12)
	await click(window_pos(b))
	key(KEY_SHIFT, false)
	await frames(2)
	s = selected()
	check(s.has(int(a["id"])) and s.has(int(b["id"])), "Shift+click keeps the selected unit and adds another: %s" % str(s))

	print("3. Right-click cancels the band box")
	main.clear_selection()
	await frames(2)
	var p0: Vector2 = window_pos(a) + Vector2(-60, -60)
	var p1: Vector2 = window_pos(a) + Vector2(60, 60)
	button(p0, MOUSE_BUTTON_LEFT, true)
	await frames(2)
	move(p1, MOUSE_BUTTON_MASK_LEFT)
	await frames(2)
	button(p1, MOUSE_BUTTON_RIGHT, true)
	await frames(1)
	button(p1, MOUSE_BUTTON_RIGHT, false)
	await frames(1)
	button(p1, MOUSE_BUTTON_LEFT, false)
	await frames(2)
	check(selected().is_empty(), "the box selected nothing: %s" % str(selected()))

	print("4. Shift+N adds the group")
	await click(window_pos(a))
	key(KEY_1, true, true)
	await frames(1)
	key(KEY_1, false, true)
	await frames(12)
	await click(window_pos(b))
	check(selected() == [int(b["id"])], "another unit selected")
	key(KEY_SHIFT, true, false, true)
	key(KEY_1, true, false, true)
	await frames(1)
	key(KEY_1, false, false, true)
	key(KEY_SHIFT, false)
	await frames(2)
	s = selected()
	check(s.has(int(a["id"])) and s.has(int(b["id"])), "Shift+1 adds group 1 to it: %s" % str(s))

	print("5. Minimap right-click")
	main.clear_selection()
	await frames(12)
	await click(window_pos(a))
	var from: Vector2 = a["pos"]
	var target: Vector2 = from + Vector2(20, 0)
	main._on_minimap_order(target)
	await turns(20)
	var now := from
	for u in state()["units"]:
		if int(u["id"]) == int(a["id"]):
			now = u["pos"]
	check(now.distance_to(target) < from.distance_to(target) - 3.0, "the unit walks to the spot (%.1f -> %.1f from it)" % [from.distance_to(target), now.distance_to(target)])
	main.select_first_own(true)
	await frames(2)
	var spot: Vector2 = from + Vector2(0, 16)
	main._on_minimap_order(spot)
	await frames(4)
	st = state()
	check(int(st["building"]) != 0 and st.has("rally") and (st["rally"] as Vector2).distance_to(spot) < 0.5, "a selected structure gets its rally point there: %s" % str(st.get("rally")))
	# RallyPointRenderer: the civ's waypoint flag stands on it.
	check(main.debug_rally_flags() == 1, "its rally flag is drawn (%d flags)" % main.debug_rally_flags())
	# StatusBars.js: the selected structure shows its bars (a full-health one too).
	check(main.debug_status_sprites() >= 2, "the selected structure has status bars (%d sprites)" % main.debug_status_sprites())

	print("6. Picked on the press")
	main.clear_selection()
	await frames(2)
	var walker: Dictionary = {}
	for u in state()["units"]:
		if int(u["id"]) == int(a["id"]):
			walker = u
	main.debug_select_units(PackedInt32Array([int(a["id"])]))
	main._on_minimap_order((walker["pos"] as Vector2) + Vector2(-30, 0))
	await turns(2)
	main.clear_selection()
	await frames(1)
	# Press only once the walker is clear of the screen edges: a press held near one
	# edge-scrolls the camera away from the units and supplies the later steps use
	# (the default box formation leaves it at the edge after step 5).
	var vp6: Vector2 = state()["viewport"]
	for _i in 40:
		for u in state()["units"]:
			if int(u["id"]) == int(a["id"]):
				walker = u
		var wp: Vector2 = window_pos(walker)
		if wp.x > 120 and wp.y > 120 and wp.x < vp6.x - 120 and wp.y < vp6.y - 120:
			break
		await turns(1)
	var press_at: Vector2 = window_pos(walker)
	move(press_at)
	await frames(2)
	button(press_at, MOUSE_BUTTON_LEFT, true)
	await turns(6)
	var moved := 0.0
	for u in state()["units"]:
		if int(u["id"]) == int(a["id"]):
			moved = (u["pos"] as Vector2).distance_to(walker["pos"])
	button(press_at, MOUSE_BUTTON_LEFT, false)
	await frames(2)
	check(moved > 2.0 and selected() == [int(a["id"])], "walked %.1f away and still selected: %s" % [moved, str(selected())])

	print("7. Hovering a supply")
	# The supply nearest the unit is found while it is selected (selected units don't
	# cover it), then the selection is cleared, so the unit itself, still walking from
	# step 6, or another walking unit can pass over it and rightly win the pick
	# (0.28 PickEntityAtPoint): wait until no unit is near it on screen.
	for _i in 60:
		main.debug_select_units(PackedInt32Array([int(a["id"])]))
		await frames(2)
		st = state()
		main.clear_selection()
		await frames(2)
		if not st.has("nearest_resource_screen"):
			break
		var clear := true
		for u in state()["units"]:
			# A walking unit's model reaches well above its feet and moves on before the hover.
			var near := 150.0 if not u["idle"] else 40.0
			if window_pos(u).distance_to(st["nearest_resource_screen"]) < near:
				clear = false
		if clear:
			break
	if st.has("nearest_resource_screen"):
		var rpos: Vector2 = st["nearest_resource_screen"]
		move(rpos)
		await frames(4)
		var hl: PackedInt32Array = main.debug_highlight()
		check(hl.has(int(st["nearest_resource"])) or (!hl.is_empty() and int(hl[0]) > 0), "the tree or mine under the mouse is highlighted: %s" % str(hl))
		# StatusBars.js: its ResourceSupply bar (background and foreground).
		check(main.debug_status_sprites() >= 2, "it shows its supply bar (%d sprites)" % main.debug_status_sprites())
		if hl.has(int(st["nearest_resource"])):
			# selection.js addList plays a Gaia entity's select sound (sel_tree, sel_metal, ...).
			button(rpos, MOUSE_BUTTON_LEFT, true)
			await frames(1)
			button(rpos, MOUSE_BUTTON_LEFT, false)
			await frames(2)
			var snd := str(main.get_audio_state()["last_sound"])
			check(snd.contains("select/resource/sel_"), "clicking it plays its select sound: " + snd)
	else:
		check(false, "a supply near the unit")

	print("8. Ctrl+click on empty ground keeps the selection")
	var fresh := {}
	for u in state()["units"]:
		fresh[int(u["id"])] = u
	var fa: Dictionary = fresh.get(int(a["id"]), a)
	var fb: Dictionary = fresh.get(int(b["id"]), b)
	var pick_two := func() -> void:
		main.clear_selection()
		await frames(20)
		await click(window_pos(fa))
		await frames(12)
		key(KEY_SHIFT, true, false, true)
		await click(window_pos(fb))
		key(KEY_SHIFT, false)
		await frames(12)
	# Empty ground: the first of a few spots where a plain click clears the selection.
	var empty = null
	for off in [Vector2(-160, 0), Vector2(160, 0), Vector2(-160, 60), Vector2(160, 60), Vector2(0, 120)]:
		await pick_two.call()
		await click(window_pos(fa) + off)
		if selected().is_empty() and int(state()["building"]) == 0:
			empty = window_pos(fa) + off
			break
	check(empty != null, "a plain click on empty ground clears the selection")
	if empty != null:
		await pick_two.call()
		var before := selected()
		key(KEY_CTRL, true, true)
		await click(empty)
		key(KEY_CTRL, false)
		await frames(2)
		s = selected()
		check(not before.is_empty() and before.all(func(i): return s.has(i)),
				"input.js: a Ctrl+click there leaves the selection (%s -> %s)" % [str(before), str(s)])

	print("9. Ranks share one icon; a click elsewhere stops the camera follow")
	var ranked := {}
	for u in state()["units"]:
		if u["has_rank"]:
			ranked.get_or_add(u["tmpl"], []).append(u)
	var pair: Array = []
	for t in ranked:
		if ranked[t].size() >= 2:
			pair = ranked[t].slice(0, 2)
	check(pair.size() == 2, "two ranked units of one template")
	if pair.size() == 2:
		var r1 := int(pair[0]["id"])
		var r2 := int(pair[1]["id"])
		main.debug_add_xp(PackedInt32Array([r2]), 100000.0)
		await turns(2)
		var ranks := {}
		for u in state()["units"]:
			ranks[int(u["id"])] = int(u["rank"])
		check(ranks.get(r2, 0) != ranks.get(r1, 0), "one of them promoted (ranks %d, %d)" % [ranks.get(r1, 0), ranks.get(r2, 0)])
		main.debug_select_units(PackedInt32Array([r1, r2]))
		await frames(4)
		var icons9: Array = main.debug_selection_grid(false)["icons"]
		check(icons9.size() == 1, "selection.js: both ranks show as one group icon (%d)" % icons9.size())
		main.debug_select_units(PackedInt32Array([r1]))
		await frames(2)
		# camera.follow has no default key (0.28 hotkeys): bind one for the run (memory only).
		var hk := RtsoHotkeyConfig.new()
		var follow_key := "Ctrl+Shift+F12"
		check(hk.get_conflicts(follow_key).is_empty(), "%s is free for camera.follow" % follow_key)
		hk.set_hotkey("camera.follow", PackedStringArray([follow_key]))
		var fk := InputEventKey.new()
		fk.keycode = KEY_F12
		fk.physical_keycode = KEY_F12
		fk.ctrl_pressed = true
		fk.shift_pressed = true
		fk.pressed = true
		Input.parse_input_event(fk)
		await frames(1)
		var fk_up: InputEventKey = fk.duplicate()
		fk_up.pressed = false
		Input.parse_input_event(fk_up)
		hk.set_hotkey("camera.follow", PackedStringArray())
		await frames(10)
		check(int(main.debug_camera()["follow"]) == r1, "camera.follow follows the selected unit")
		var other_id := -1
		var other_at := Vector2()
		for u in state()["units"]:
			if int(u["id"]) != r1 and not u["behind"] and on_screen(u, state()["viewport"]):
				other_id = int(u["id"])
				other_at = window_pos(u)
		await frames(20)
		if other_id >= 0:
			await click(other_at)
			check(int(main.debug_camera()["follow"]) == 0, "input.js: clicking another unit stops the follow (%d)" % int(main.debug_camera()["follow"]))
		else:
			check(false, "another unit on screen")

	print("FAILURES: ", failures)
	quit(1 if failures else 0)
