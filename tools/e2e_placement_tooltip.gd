extends SceneTree
# 0.28's placement tooltip (input.js updateBuildingPlacementPreview, placementTooltip;
# BuildRestrictions.js CheckPlacement messages) beside the cursor while placing:
#   1. a House over the Civic Center: "cannot be built on another building or resource";
#   2. a House out of its territory: "cannot be built in neutral territory. Valid territories: own";
#   3. a Civic Center by your own: "too close to a CivilCentre, must be at least 200 meters away";
#   4. a free spot shows nothing;
#   5. a palisade: no start on the Civic Center, its cost while dragging, and a click with no
#      length that lays nothing and ends the placement (input.js tryPlaceWall);
#   6. a sentry tower on a free spot: "Basic range: N meters" and "Average bonus range: M meters".
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_placement_tooltip.gd
# Exit code 0 = pass.

const BLD_TOWN_CENTER := 0
const BLD_HOUSE := 1
const BLD_PALISADE := 21
const BLD_SENTRY_TOWER := 19

var main
var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	# One map for every run: the random default's coast can leave no neutral land in the grid.
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

func click(pos: Vector2) -> void:
	await move(pos)
	for down in [true, false]:
		var mb := InputEventMouseButton.new()
		mb.button_index = MOUSE_BUTTON_LEFT
		mb.pressed = down
		mb.position = pos
		mb.global_position = pos
		root.push_input(mb, true)
		await frames(1)
	await frames(2)

func tip() -> Dictionary:
	return main.debug_placement_tip()

# The tooltips a structure gets over a grid of screen points round the Civic Center.
func sample(btype: int, centre: Vector2) -> Array:
	main.enter_build_mode(true, btype)
	var out := []
	for dy in range(-600, 601, 60):
		for dx in range(-600, 601, 60):
			await move(centre + Vector2(dx, dy))
			out.append({"at": centre + Vector2(dx, dy), "tip": tip()})
	main.enter_build_mode(false)
	await frames(2)
	return out

func any_text(samples: Array, needle: String) -> Dictionary:
	for s in samples:
		if str(s["tip"]["text"]).contains(needle):
			return s
	return {}

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
	var cc: Vector2 = r["roof"]
	main.select_first_own(true)
	var cc_ground: Vector2 = main.debug_selected_building()["screen"]
	main.clear_selection()

	print("1-4. House")
	main.enter_build_mode(true, BLD_HOUSE)
	await move(cc_ground)
	var t := tip()
	check(t["visible"] and t["error"] and str(t["text"]).ends_with("cannot be built on another building or resource"), "over the Civic Center: " + str(t["text"]))
	main.enter_build_mode(false)
	var houses := await sample(BLD_HOUSE, cc)
	var terr := any_text(houses, "House cannot be built in neutral territory. Valid territories: own")
	check(not terr.is_empty(), "out of its territory: " + str(terr.get("tip", {}).get("text", "")))
	var free := houses.filter(func(s): return not s["tip"]["visible"])
	check(not free.is_empty(), "a free spot shows nothing (%d of %d)" % [free.size(), houses.size()])
	check(not main.debug_placement_tip()["visible"], "gone when placing stops")

	print("3. Civic Center")
	var ccs := await sample(BLD_TOWN_CENTER, cc)
	var near := any_text(ccs, "too close to a CivilCentre, must be at least 200 meters away")
	check(not near.is_empty(), "by your own: " + str(near.get("tip", {}).get("text", "")))

	print("5. Palisade")
	main.enter_build_mode(true, BLD_PALISADE)
	# input.js:1095-1101: a start whose tower can't stand there is not taken.
	await click(cc_ground)
	await move(cc_ground + Vector2(40, 0))
	t = tip()
	check(not str(t["text"]).begins_with("Cost: "), "a start on the Civic Center is refused: " + str(t["text"]))
	var start: Vector2 = free[0]["at"]
	await click(start)
	await move(start + Vector2(40, 0))
	t = tip()
	check(t["visible"] and str(t["text"]).begins_with("Cost: "), "dragging shows its cost: " + str(t["text"]))
	# tryPlaceWall: a wall no longer than a tower has no segment, lays nothing and ends the placement.
	await click(start)
	await move(start + Vector2(30, 30))
	t = tip()
	check(not t["visible"], "a click with no length lays nothing and ends the placement: " + str(t["text"]))
	main.enter_build_mode(false)

	print("6. Sentry tower")
	var towers := await sample(BLD_SENTRY_TOWER, cc_ground)
	var ok := any_text(towers, "Basic range: ")
	print("    ", ok.get("tip", {}))
	check(not ok.is_empty() and not ok["tip"]["error"] and str(ok["tip"]["text"]).contains("\nAverage bonus range: "), "a free spot shows the basic and average bonus range")
	print("RESULT: %s" % ("PASS" if failures == 0 else "FAIL (%d)" % failures))
	quit(0 if failures == 0 else 1)
