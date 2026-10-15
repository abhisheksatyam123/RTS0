extends SceneTree
# input.js INPUT_UNIT_POSITION: holding the right button and drawing a line spreads the
# selected units evenly along it (walk-custom) when the button is released; a plain
# right-click still gives the usual order, on release.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_line_positioning.gd
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

func move(pos: Vector2) -> void:
	var mm := InputEventMouseMotion.new()
	mm.position = root.get_final_transform() * pos
	mm.global_position = mm.position
	Input.parse_input_event(mm)
	await frames(1)

func right(pos: Vector2, down: bool) -> void:
	var mb := InputEventMouseButton.new()
	mb.button_index = MOUSE_BUTTON_RIGHT
	mb.pressed = down
	mb.position = pos
	mb.global_position = pos
	root.push_input(mb, true)
	await frames(1)

func screens(ids: PackedInt32Array) -> Array:
	var out := []
	for u in main.debug_selection_state()["units"]:
		if ids.has(int(u["id"])):
			out.append(u["pos"])
	return out

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
	# The starting citizen soldiers and cavalry, which stand free of the Civic Center.
	var men := PackedInt32Array()
	for u in main.debug_selection_state()["units"]:
		if not u["civilian"] and men.size() < 5:
			men.append(int(u["id"]))
	main.debug_select_units(men)
	await frames(4)
	# Passive, so an idle unit does not go after an animal mid-test.
	main._on_stance_selected(3)
	await frames(4)
	var a := Vector2(700, 200)
	var b := Vector2(1000, 260)
	var ga: Vector2 = main.debug_ground_at(a)
	var gb: Vector2 = main.debug_ground_at(b)
	print("    line ", ga, " -> ", gb)
	await right(a, true)
	check(main.debug_right_line()["active"], "the press waits for the release")
	for k in range(1, 21):
		await move(a.lerp(b, k / 20.0))
	var line: Dictionary = main.debug_right_line()
	print("    ", line)
	check(line["drawing"] and int(line["points"]) > 5, "dragging draws a line")
	await right(b, false)
	check(not main.debug_right_line()["active"], "released")
	var res: Dictionary = main.debug_right_line()
	var tg: PackedVector2Array = res["targets"]
	var ents: PackedInt32Array = res["entities"]
	print("    targets ", tg)
	check(tg.size() == 5 and ents.size() == 5, "one position per unit")
	# The screen line follows the terrain, so the positions are evenly spaced along the
	# drawn ground path rather than on the straight chord.
	var mean := 0.0
	for k in range(1, tg.size()):
		mean += tg[k].distance_to(tg[k - 1]) / 4.0
	var even := mean > 0.2 * ga.distance_to(gb)
	for k in range(1, tg.size()):
		even = even and absf(tg[k].distance_to(tg[k - 1]) - mean) < 0.5
	check(even, "the positions are spread evenly along the drawn line")
	check(tg[0].distance_to(ga) < 0.6 or tg[0].distance_to(gb) < 0.6, "the line's ends are used")
	await frames(600)
	var arrived := 0
	for u in main.debug_selection_state()["units"]:
		var k := ents.find(int(u["id"]))
		if k >= 0 and (u["pos"] as Vector2).distance_to(tg[k]) < 2.0:
			arrived += 1
	print("    arrived ", arrived)
	check(arrived >= 4, "the units walk to their own positions")
	# A plain right-click: the usual walk, given on release.
	var c := Vector2(850, 330)
	var gc: Vector2 = main.debug_ground_at(c)
	print("    gc ", gc)
	await right(c, true)
	await right(c, false)
	await frames(600)
	var pts := screens(men)
	var far := 0.0
	for p in pts:
		far += (p as Vector2).distance_to(gc) / pts.size()
	print("    click ", pts)
	check(far < 3.0, "a plain right-click walks them to the point")
	print("FAILURES: ", failures)
	quit(1 if failures > 0 else 0)
