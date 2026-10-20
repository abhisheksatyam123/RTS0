extends SceneTree
# 0.28's developer overlay (gui/session/developer_overlay): Alt+D opens the panel
# and says so in the chat; Control all units orders every live unit ("control-all");
# Display selection state shows the first selected entity's state; Reveal map reveals
# and hides the map again ("reveal-map"); Restrict camera lets the camera leave the
# map when cleared; Promote selected units raises the selection a rank ("promote");
# Change perspective shows the player view dropdown, and picking a player or Observer
# re-seats this client; Enable time warp snapshots the game every 10 turns and rewinds
# to the last one; the pathfinder, hierarchical pathfinder, obstruction, unit motion,
# range and bounding box overlays draw while ticked; Enable culling, Lock cull camera,
# Display camera frustum and Display shadows frustum drive the culling and its boxes;
# Activate Rejoin Test (not on random maps) rejoins a copy of
# the game and replays every turn onto it, an Out of Sync box naming any turn
# where the two part.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_dev_overlay.gd
# Exit code 0 = pass.

var main
var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	# Commands.js's "reveal-map" and "promote" need cheats (GetCheatsEnabled), as single player has them.
	root.get_node("MatchSettings").cheats = true
	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	run.call_deferred()

# Types a line into the chat input and sends it, as the player would.
func say(text: String) -> void:
	main.toggle_chat()
	await frames(2)
	var le := root.gui_get_focus_owner() as LineEdit
	if le:
		le.text = text
	main._on_chat_text_submitted(text)
	await frames(2)

func frames(n: int) -> void:
	for i in n:
		await process_frame

## A row by its 0.28 label.
func box(label: String) -> CheckBox:
	var i: int = Array(main.debug_dev_overlay()["commands"]).find(label)
	if i < 0:
		check(false, "there is a %s row" % label)
	return main.find_child("DevCommand%d" % i, true, false)

func press(label: String) -> void:
	var b := box(label)
	b.set_pressed_no_signal(not b.button_pressed) # what a click does: toggle, then "pressed"
	b.pressed.emit()

## The open message box titled `title`, or null.
func _box(title: String) -> Node:
	for q in main.get_children():
		if q.has_method("press") and q.has_method("button_texts") and not q.is_queued_for_deletion():
			var t = q.find_child("Title", true, false)
			if t and t.text == title:
				return q
	return null

## Pick a player view dropdown item by id (0 Observer, N player N) and wait for the turn.
func pick(id: int) -> void:
	var dd: OptionButton = null
	for o in main.find_children("*", "OptionButton", true, false):
		if o.is_visible_in_tree() and o.get_item_count() > 0 and o.get_item_text(0) == "Observer":
			dd = o
	if dd == null:
		check(false, "the player view dropdown is there")
		return
	var index := dd.get_item_index(id)
	dd.select(index)
	dd.item_selected.emit(index)
	await frames(20)

func _listed(id: int) -> bool:
	for u in main.debug_selection_state()["units"]:
		if int(u["id"]) == id:
			return true
	return false

func run() -> void:
	await frames(60)
	var rig = main.find_children("*", "RtsoCameraRig", true, false)[0]
	rig.set_edge_scroll_enabled(false)
	check(not main.debug_dev_overlay()["open"], "closed at the start")
	main.toggle_dev_overlay()
	await frames(2)
	var d: Dictionary = main.debug_dev_overlay()
	check(d["open"] and main.find_child("DevCommandsOverlay", true, false).visible, "Alt+D opens it")
	check(d["restrict"] and not d["reveal"], "the camera is restricted and the map not revealed")
	check(main.debug_dev_overlay()["chat_log"].contains("The Developer Overlay was opened."), "the chat says it was opened")

	main.select_first_own(false)
	press("Display selection state")
	await frames(2)
	d = main.debug_dev_overlay()
	check(d["state_shown"] and str(d["state"]).contains("entity: {") and str(d["state"]).contains("hitpoints:")
		and str(d["state"]).contains("velocity:") and str(d["state"]).contains("template: {name:"),
		"Display selection state shows the selected entity")

	press("Reveal map")
	await frames(3)
	check(main.debug_dev_overlay()["map_revealed"], "Reveal map reveals it")
	press("Reveal map")
	await frames(3)
	check(not main.debug_dev_overlay()["map_revealed"], "and clearing it hides it again")
	var log: String = main.debug_dev_overlay()["chat_log"]
	check(log.count("(Cheat - reveal map)") == 2 and log.contains("> (Cheat - reveal map)"), "each press tells everyone in the player's chat line (Commands.js aichat)")

	press("Restrict camera")
	await frames(1)
	rig.set_target_pos(Vector3(100000, 0, 100000))
	check(float(main.debug_dev_overlay()["camera_x"]) > 10000.0, "unrestricted, the camera may leave the map")
	press("Restrict camera")
	await frames(1)
	rig.set_target_pos(Vector3(100000, 0, 100000))
	check(float(main.debug_dev_overlay()["camera_x"]) < 10000.0, "restricted again, it stays on the map")

	main.debug_selection_grid(true) # every own unit: the citizen soldiers rise a rank, women don't
	var before: int = int(main.debug_dev_overlay()["selection_ranks"])
	press("Promote selected units")
	await frames(3)
	var after: int = int(main.debug_dev_overlay()["selection_ranks"])
	check(after > before, "Promote selected units raises their ranks: %d -> %d" % [before, after])
	check(not box("Promote selected units").button_pressed, "its box doesn't stay ticked")
	check(main.debug_dev_overlay()["chat_log"].contains("> (Cheat - promoted units)"), "and \"(Cheat - promoted units)\" in the chat")

	print("Cheat codes")
	await say("i want pizza lots")
	await frames(3)
	check(main.debug_dev_overlay()["chat_log"].contains("> This cheat requires a numeric parameter."), "a non-numeric parameter: the player's own chat line")
	await say("retro me")
	await frames(3)
	check(main.get_audio_state()["music_state"] == "custom", "\"retro me\" plays the custom track: " + str(main.get_audio_state()["track"]))
	await say("retro me off")

	check(not main.debug_dev_overlay()["view_picker"] and int(main.debug_dev_overlay()["seat"]) == 0, "a player has no view dropdown")
	press("Change perspective")
	await frames(2)
	check(main.debug_dev_overlay()["view_picker"], "Change perspective shows the player view dropdown")
	await pick(2)
	check(int(main.debug_dev_overlay()["seat"]) == 1, "picking player 2 plays as player 2")
	await pick(0)
	check(int(main.debug_dev_overlay()["seat"]) == 255 and main.debug_dev_overlay()["view_picker"], "picking Observer watches, dropdown kept")
	await pick(1)
	check(int(main.debug_dev_overlay()["seat"]) == 0, "and picking player 1 plays as player 1 again")

	press("Enable time warp")
	await frames(2)
	var tw: Node = main.find_child("MessageBox", true, false)
	check(tw != null and str(tw.find_child("Title", true, false).text) == "Time warp mode", "time warp says what it is")
	for q in main.get_children():
		if q.has_method("press") and q.has_method("button_texts"):
			q.press(0)
	await frames(2)
	var waited := 0
	while main.time_warp_snapshot_count() < 2 and waited < 2000:
		await process_frame
		waited += 1
	await frames(20) # half way to the next snapshot
	var snaps: int = main.time_warp_snapshot_count()
	check(snaps >= 2, "it takes a snapshot every 10 turns (%d)" % snaps)
	var t_before: float = main.get_match_statistics().get("game_time", 0.0)
	check(main.time_warp_rewind(), "rewind loads the latest snapshot")
	await frames(2)
	var t_after: float = main.get_match_statistics().get("game_time", 0.0)
	check(t_after < t_before and main.time_warp_snapshot_count() == snaps - 1, "back in time %.1f -> %.1f, that snapshot used up" % [t_before, t_after])
	press("Enable time warp")
	await frames(1)
	check(main.time_warp_snapshot_count() == 0, "turning it off drops the snapshots")

	var rows: Array = Array(main.debug_dev_overlay()["commands"])
	check(rows.slice(0, 8) == ["Control all units", "Change perspective", "Display selection state", "Pathfinder overlay",
		"Hierarchical pathfinder overlay", "Obstruction overlay", "Unit motion overlay", "Range overlay"], "the rows run in 0.28's order: %s" % [rows])
	check(rows.slice(13) == ["Promote selected units", "Enable culling", "Lock cull camera",
		"Display camera frustum", "Display shadows frustum"], "the culling boxes close the list: %s" % [rows.slice(13)])
	for o in ["pathfinder", "hier_pathfinder", "obstruction", "unit_motion", "range", "bounding_box"]:
		check(int(main.debug_dev_overlay()[o + "_drawn"]) == 0, "nothing drawn by the %s overlay while it is off" % o)
	main.debug_selection_grid(true)
	main._on_minimap_order(Vector2(0, 0))
	await frames(4)
	for label in ["Pathfinder overlay", "Hierarchical pathfinder overlay", "Obstruction overlay", "Unit motion overlay", "Range overlay", "Bounding box overlay"]:
		press(label)
	await frames(2)
	d = main.debug_dev_overlay()
	check(int(d["pathfinder_drawn"]) > 0, "the pathfinder overlay fills the navcells land units can't cross (%d)" % int(d["pathfinder_drawn"]))
	check(int(d["hier_pathfinder_drawn"]) > 0, "the hierarchical pathfinder overlay tints the chunk regions (%d)" % int(d["hier_pathfinder_drawn"]))
	check(int(d["obstruction_drawn"]) > 0, "the obstruction overlay outlines the obstructions (%d)" % int(d["obstruction_drawn"]))
	check(int(d["unit_motion_drawn"]) > 0, "the unit motion overlay draws the walkers' paths (%d)" % int(d["unit_motion_drawn"]))
	check(int(d["range_drawn"]) > 0, "the range overlay rings the attackers (%d)" % int(d["range_drawn"]))
	check(int(d["bounding_box_drawn"]) > 0, "the bounding box overlay boxes the models (%d)" % int(d["bounding_box_drawn"]))
	check(main.find_child("DebugOverlays", true, false).visible, "and they are shown")
	for label in ["Pathfinder overlay", "Hierarchical pathfinder overlay", "Obstruction overlay", "Unit motion overlay", "Range overlay", "Bounding box overlay"]:
		check(box(label).button_pressed, "%s stays ticked" % label)
		press(label)
	await frames(2)
	check(not main.find_child("DebugOverlays", true, false).visible and int(main.debug_dev_overlay()["obstruction_drawn"]) == 0,
		"cleared, they are gone")

	# DeveloperOverlayControlCheckboxes.js: enabled() needs g_InitAttributes.mapType != "random".
	await frames(2)
	check(box("Activate Rejoin Test").disabled, "Activate Rejoin Test is disabled on a random map")
	main.load_map("arcadia")
	await frames(10)
	check(not box("Activate Rejoin Test").disabled, "and enabled on a skirmish map")
	press("Activate Rejoin Test")
	await frames(2)
	var rj: Node = _box("Rejoin Test")
	check(rj != null, "Activate Rejoin Test warns first")
	if rj:
		rj.press(0) # Cancel
	await frames(2)
	check(not main.get_rejoin_test()["active"] and not box("Activate Rejoin Test").button_pressed, "Cancel leaves it off")
	press("Activate Rejoin Test")
	await frames(2)
	rj = _box("Rejoin Test")
	if rj:
		rj.press(1) # OK
	var rw := 0
	while int(main.get_rejoin_test()["turns"]) < 20 and rw < 2000:
		await process_frame
		rw += 1
	var r: Dictionary = main.get_rejoin_test()
	check(r["active"] and int(r["rejoined_at"]) > 0 and int(r["turns"]) >= 20,
		"OK rejoins a copy at turn %d and replays %d turns onto it" % [int(r["rejoined_at"]), int(r["turns"])])
	check(int(r["out_of_sync_turn"]) == 0 and _box("Out of Sync") == null, "and the copy stays in sync")
	check(box("Activate Rejoin Test").button_pressed and box("Activate Rejoin Test").disabled, "it can't be turned off")
	main.debug_set_resource(0, 98765.0) # only the game changes: the copy must part from it
	rw = 0
	while int(main.get_rejoin_test()["out_of_sync_turn"]) == 0 and rw < 200:
		await process_frame
		rw += 1
	var oos: Node = _box("Out of Sync")
	check(int(main.get_rejoin_test()["out_of_sync_turn"]) > 0 and oos != null, "a change the copy didn't get shows as Out of Sync (turn %d)" % int(main.get_rejoin_test()["out_of_sync_turn"]))
	if oos:
		oos.press(0)
	await frames(2)

	print("Control all units and the culling boxes")
	press("Control all units")
	await frames(3)
	d = main.debug_dev_overlay()
	check(d["control_all"], "Control all units sets the flag")
	check(box("Control all units").button_pressed, "its box stays ticked")
	check(main.debug_dev_overlay()["chat_log"].contains("> (Cheat - control all units)"), "and \"(Cheat - control all units)\" in the chat")
	# Commands.js FilterEntityList with controlAllUnits: another player's structure trains for its owner.
	var their_cc: int = int(main.debug_civic_centre_id(1))
	main.debug_select_entity(their_cc)
	await frames(2)
	var their_roster: Array = main.debug_roster()
	var first_row: int = -1
	for row in their_roster.size():
		if not their_roster[row]["disabled"]:
			first_row = row
			break
	check(first_row >= 0, "another player's Civic Center shows its training roster (%d rows)" % their_roster.size())
	var queued_before: int = int(main.debug_building_queue(their_cc))
	if first_row >= 0:
		main._on_roster_item_clicked(first_row, Vector2(), MOUSE_BUTTON_LEFT)
	await frames(20)
	check(int(main.debug_building_queue(their_cc)) > queued_before, "and a click there queues a unit in it")
	var theirs: PackedInt32Array = main.debug_spawn_units("athen/infantry_spearman_b", 1, 1)
	main.debug_select_units(theirs)
	check(_listed(theirs[0]), "another player's unit is listed while selected")
	main.debug_delete_units(theirs)
	await frames(2)
	check(not _listed(theirs[0]), "and it can be deleted")
	main.debug_select_units(PackedInt32Array())
	press("Control all units")
	await frames(3)
	check(not main.debug_dev_overlay()["control_all"], "clearing it drops the flag")

	press("Enable culling")
	await frames(2)
	check(not main.debug_dev_overlay()["culling_enabled"], "Enable culling clears, and the box follows")
	press("Enable culling")
	await frames(2)
	check(main.debug_dev_overlay()["culling_enabled"], "ticking it again restores culling")
	press("Lock cull camera")
	await frames(2)
	check(main.debug_dev_overlay()["lock_cull_camera"], "Lock cull camera holds the cull frustum")
	press("Display camera frustum")
	press("Display shadows frustum")
	await frames(2)
	d = main.debug_dev_overlay()
	check(int(d["cull_camera_frustum_drawn"]) > 0, "the cull camera frustum draws its box (%d)" % int(d["cull_camera_frustum_drawn"]))
	check(int(d["shadows_frustum_drawn"]) > 0, "the shadow cascades draw their boxes (%d)" % int(d["shadows_frustum_drawn"]))
	press("Display camera frustum")
	press("Display shadows frustum")
	press("Lock cull camera")
	await frames(2)

	var rdm = main.find_child("RenderDebugMode", true, false)
	check(rdm != null and rdm.get_item_count() == 4 and rdm.get_item_text(0) == "Render Debug Mode Disabled"
		and rdm.get_item_text(3) == "Render Debug Mode Custom", "the Render Debug Mode dropdown has 0.28's four modes")
	check(int(main.debug_dev_overlay()["viewport_debug_draw"]) == Viewport.DEBUG_DRAW_DISABLED, "it starts disabled")
	for m in [[1, Viewport.DEBUG_DRAW_SSAO], [2, Viewport.DEBUG_DRAW_OVERDRAW], [3, Viewport.DEBUG_DRAW_UNSHADED], [0, Viewport.DEBUG_DRAW_DISABLED]]:
		rdm.select(m[0])
		rdm.item_selected.emit(m[0])
		await frames(1)
		d = main.debug_dev_overlay()
		check(int(d["render_debug"]) == m[0] and int(d["viewport_debug_draw"]) == m[1], "%s draws as %d" % [rdm.get_item_text(m[0]), m[1]])
	rdm.select(2)
	rdm.item_selected.emit(2)

	main.toggle_dev_overlay()
	await frames(1)
	check(not main.debug_dev_overlay()["open"] and main.debug_dev_overlay()["chat_log"].contains("The Developer Overlay was closed."), "Alt+D closes it")
	check(root.debug_draw == Viewport.DEBUG_DRAW_OVERDRAW, "a mode stays while the match goes on")
	root.remove_child(main)
	main.free()
	check(root.debug_draw == Viewport.DEBUG_DRAW_DISABLED, "leaving the match draws normally again")
	print("RESULT: %s" % ("PASS" if failures == 0 else "FAIL (%d)" % failures))
	quit(0 if failures == 0 else 1)
