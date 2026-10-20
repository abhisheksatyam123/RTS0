extends SceneTree
# A match played by touch alone (scenes/common/touch_input.gd on Android and iOS):
# tap selects, a drag box-selects, a hold or a two-finger tap orders, two fingers pan
# and pinch-zoom the camera, the minimap takes taps and holds, and a building is
# placed from its command button. A touch near the screen edge does not leave the
# camera scrolling.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_touch_match.gd
# Exit code 0 = pass.

var main
var touch: Node
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

# Test points are in viewport coordinates (what debug hooks report); a touch is in
# window coordinates, as the screen sends it.
func win(p: Vector2) -> Vector2:
	return root.get_final_transform() * p

func finger(index: int, p: Vector2, pressed: bool) -> void:
	var t := InputEventScreenTouch.new()
	t.index = index
	t.position = win(p)
	t.pressed = pressed
	Input.parse_input_event(t)
	await frames(2)

func slide(index: int, p: Vector2) -> void:
	var d := InputEventScreenDrag.new()
	d.index = index
	d.position = win(p)
	Input.parse_input_event(d)
	await frames(2)

func tap(p: Vector2) -> void:
	await finger(0, p, true)
	await finger(0, p, false)
	await frames(2)

func hold(p: Vector2) -> void:
	await finger(0, p, true)
	var t0 := Time.get_ticks_msec()
	while Time.get_ticks_msec() - t0 < int(touch.HOLD_SEC * 1000.0) + 150:
		await process_frame
	await finger(0, p, false)
	await frames(2)

func selected() -> Array:
	return Array(main.debug_selection_state()["selected"])

func camera() -> Dictionary:
	return main.debug_camera()

# World ground point (x, z) under a unit.
func ground(u: int) -> Vector2:
	return main.debug_unit_ground(u)

# Look at the men from straight above their middle, clear of the HUD panels.
func centre_on(men: PackedInt32Array) -> void:
	var sum := Vector2.ZERO
	for m in men:
		sum += ground(m)
	for i in 40:
		main._on_minimap_clicked(sum / men.size())
		await frames(20)
		if (camera()["target"] as Vector2).distance_to(sum / men.size()) < 1.0:
			break

func walks_to(u: int, goal: Vector2, what: String) -> void:
	var from := ground(u)
	await frames(60)
	var now := ground(u)
	check(now.distance_to(goal) < from.distance_to(goal) - 1.0, "%s: %s -> %s, goal %s" % [what, from, now, goal])

func run() -> void:
	touch = root.get_node_or_null("TouchInput")
	if touch == null:
		touch = load("res://scenes/common/touch_input.gd").new()
		touch.name = "TouchInput"
		root.add_child(touch)
	touch.activate()
	await frames(60)
	# The camera glides to the Civic Centre first; wait for it to stop.
	var r: Dictionary = main.debug_civic_centre_roof()
	for i in 200:
		await frames(1)
		var r2: Dictionary = main.debug_civic_centre_roof()
		var still: bool = (r2["roof"] as Vector2).distance_to(r["roof"]) < 0.5
		r = r2
		if still and i > 10:
			break
	var men: PackedInt32Array = main.debug_spawn_units("athen/infantry_spearman_b", 4)
	await frames(10)
	# A start near the map's edge can't be framed (the camera keeps inside the map): walk
	# the men part of the way to the middle first.
	main.debug_select_units(men)
	var start := ground(int(men[0]))
	main._on_minimap_order(start * 0.6)
	for i in 30:
		await frames(10)
		if ground(int(men[0])).distance_to(start * 0.6) < 3.0:
			break
	await centre_on(men)
	main.debug_select_units(PackedInt32Array())
	await frames(2)

	print("1. Tap and drag select")
	var a := int(men[0])
	await tap(main.debug_unit_screen(a))
	# (He walked in a formation with the others, and a click on a formation member selects it all.)
	check(a in selected(), "a tap on a man selects him: " + str(selected()))
	var lo := Vector2(1e9, 1e9)
	var hi := Vector2(-1e9, -1e9)
	for m in men:
		var p: Vector2 = main.debug_unit_screen(m)
		lo = lo.min(p)
		hi = hi.max(p)
	await finger(0, lo - Vector2(30, 30), true)
	await slide(0, (lo + hi) * 0.5)
	await slide(0, hi + Vector2(30, 30))
	await finger(0, hi + Vector2(30, 30), false)
	await frames(2)
	var sel := selected()
	check(Array(men).all(func(m): return m in sel), "a drag around the men selects all %d: %s" % [men.size(), str(sel)])

	print("2. Orders: a hold, then a two-finger tap")
	var a_at: Vector2 = main.debug_unit_screen(a)
	var spot := a_at + Vector2(-170, -60)
	var goal: Vector2 = main.debug_ground_at(spot)
	await hold(spot)
	await walks_to(a, goal, "a hold on open ground walks the men there")
	spot = a_at + Vector2(170, -40)
	goal = main.debug_ground_at(spot)
	await finger(0, spot, true)
	await finger(1, spot + Vector2(60, 0), true)
	await finger(1, spot + Vector2(60, 0), false)
	await finger(0, spot, false)
	await walks_to(a, goal, "a two-finger tap walks them to the first finger")
	check(selected().size() == men.size(), "orders keep the selection")

	print("3. Two fingers: pan and pinch")
	var c0 := camera()
	var mid := Vector2(640, 300)
	await finger(0, mid - Vector2(60, 0), true)
	await finger(1, mid + Vector2(60, 0), true)
	for k in 4:
		await slide(0, mid - Vector2(70 + 10 * k, 0))
		await slide(1, mid + Vector2(70 + 10 * k, 0))
	await finger(1, mid + Vector2(100, 0), false)
	await finger(0, mid - Vector2(100, 0), false)
	await frames(10)
	var c1 := camera()
	check(float(c1["zoom"]) < float(c0["zoom"]) - 0.5, "spreading two fingers zooms in: %.1f -> %.1f" % [c0["zoom"], c1["zoom"]])
	await finger(0, mid - Vector2(100, 0), true)
	await finger(1, mid + Vector2(100, 0), true)
	for k in 4:
		await slide(0, mid - Vector2(90 - 10 * k, 0))
		await slide(1, mid + Vector2(90 - 10 * k, 0))
	await finger(1, mid + Vector2(60, 0), false)
	await finger(0, mid - Vector2(60, 0), false)
	await frames(10)
	check(float(camera()["zoom"]) > float(c1["zoom"]) + 0.5, "pinching them together zooms back out: %.1f -> %.1f" % [c1["zoom"], camera()["zoom"]])
	c1 = camera()
	await finger(0, mid - Vector2(50, 0), true)
	await finger(1, mid + Vector2(50, 0), true)
	for k in 6:
		await slide(0, mid - Vector2(50, -25 * (k + 1)))
		await slide(1, mid + Vector2(50, 25 * (k + 1)))
	await finger(1, mid + Vector2(50, 150), false)
	await finger(0, mid - Vector2(50, -150), false)
	await frames(10)
	var c2 := camera()
	check((c2["target"] as Vector2).distance_to(c1["target"]) > 2.0, "moving two fingers pans the camera: %s -> %s" % [c1["target"], c2["target"]])
	check(selected().size() == men.size(), "two fingers neither select nor order")

	print("4. A touch at the screen edge leaves the camera still")
	var vr: Rect2 = root.get_visible_rect()
	await tap(Vector2(4, vr.size.y * 0.5))
	await frames(2)
	var e0: Vector2 = camera()["target"]
	await frames(30)
	var e1: Vector2 = camera()["target"]
	check(e1.distance_to(e0) < 0.05, "no edge scrolling after the finger lifts: %s -> %s" % [e0, e1])

	print("5. The minimap")
	var mms: Array = main.find_children("*", "RtsoMiniMapView", true, false)
	check(mms.size() == 1, "the session has a minimap")
	if mms.size() == 1:
		var mm: Control = mms[0]
		var rect := mm.get_global_rect()
		var mtap := rect.position + rect.size * Vector2(0.3, 0.5)
		var m0: Vector2 = camera()["target"]
		await tap(mtap)
		await frames(10)
		var m1: Vector2 = camera()["target"]
		check(m1.distance_to(m0) > 2.0, "a tap on the minimap moves the camera: %s -> %s" % [m0, m1])
		main.debug_select_units(men)
		# Let the men finish their last walk, then order them from the minimap.
		var last := ground(a)
		for k in 30:
			await frames(10)
			if ground(a).distance_to(last) < 0.05:
				break
			last = ground(a)
		var mspot := rect.position + rect.size * Vector2(0.5, 0.5)
		await hold(mspot)
		check(selected().size() == men.size(), "a hold on the minimap keeps the selection")
		var still := ground(a)
		await frames(60)
		check(ground(a).distance_to(still) > 2.0, "a hold on the minimap orders the idle men to walk: %s -> %s" % [still, ground(a)])

	print("6. Place a House")
	main.debug_select_units(PackedInt32Array())
	for i in 40:
		main._on_minimap_clicked(main.debug_civic_centre_roof()["centre"])
		await frames(10)
	main.debug_command_button("House")
	await frames(5)
	var b: Dictionary = main.debug_command_button("House")
	check(b.has("id"), "a worker shows a House button")
	if b.has("id"):
		var houses_before := house_count()
		await tap((b["rect"] as Rect2).get_center())
		check(main.debug_placing(), "tapping the House button starts placement")
		# Open ground near the Civic Centre: try spots around it until one is free.
		var cc: Vector2 = main.debug_civic_centre_roof()["roof"]
		var placed := false
		for k in 12:
			var site: Vector2 = cc + Vector2.from_angle(TAU * k / 12.0) * 230.0
			if not Rect2(Vector2(150, 90), Vector2(980, 380)).has_point(site):
				continue
			await tap(site)
			await frames(5)
			if house_count() > houses_before:
				placed = true
				break
		check(placed, "a tap on open ground places a House foundation: %d -> %d" % [houses_before, house_count()])
		check(not main.debug_placing(), "and ends placement")

	print("FAILURES: %d" % failures)
	quit(1 if failures else 0)

func house_count() -> int:
	var n := 0
	var all: Dictionary = main.debug_structure_types()
	for p in all:
		n += int((all[p] as Dictionary).get("House", 0))
	return n
