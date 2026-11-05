extends SceneTree
# Headless render check for one walking rig: spawns one unit of RIG for player 0, selects it,
# right-clicks a ground point to walk, and saves viewport crops around the unit while it walks.
# Used to eyeball foot planting against the ground (walk cadence work).
#
#   RIG=athen/cavalry_swordsman_b OUT=/tmp/claude-0/render VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
#     xvfb-run -a -s "-screen 0 1280x720x24" /tmp/claude-0/godot.KJC1/Godot_v4.3-stable_linux.x86_64 \
#     --path . --rendering-driver vulkan --fixed-fps 20 -s tools/render_walk_check.gd
# Exit code 0 = frames written and the unit moved.

const CROP := 220

var main
var failures := 0
var rig := ""
var out_dir := "/tmp/claude-0/render"

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	rig = OS.get_environment("RIG")
	if rig == "":
		rig = "athen/siege_ram"
	var o := OS.get_environment("OUT")
	if o != "":
		out_dir = o
	DirAccess.make_dir_recursive_absolute(out_dir)
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

func turns(n: int) -> void:
	var until: int = int(main.get_replay_status().turn) + n
	var guard := 0
	while int(main.get_replay_status().turn) < until and guard < n * 40:
		await process_frame
		guard += 1

func move(pos: Vector2) -> void:
	var mm := InputEventMouseMotion.new()
	mm.position = pos
	mm.global_position = pos
	root.push_input(mm, true)
	await frames(2)

func click(pos: Vector2, button := MOUSE_BUTTON_LEFT) -> void:
	await move(pos)
	for down in [true, false]:
		var mb := InputEventMouseButton.new()
		mb.button_index = button
		mb.pressed = down
		mb.position = pos
		mb.global_position = pos
		root.push_input(mb, true)
		await frames(1)
	await frames(2)

func save_crop(tag: String, center: Vector2) -> void:
	var img: Image = root.get_texture().get_image()
	var x := int(clamp(center.x - CROP / 2, 0, img.get_width() - CROP))
	var y := int(clamp(center.y - CROP / 2, 0, img.get_height() - CROP))
	var crop := img.get_region(Rect2i(x, y, CROP, CROP))
	crop.resize(CROP * 2, CROP * 2, Image.INTERPOLATE_NEAREST)
	var path := "%s/%s_%s.png" % [out_dir, rig.replace("/", "_"), tag]
	crop.save_png(path)
	print("    saved ", path)

func run() -> void:
	await frames(60) # let the sim finish loading the map
	var ids: PackedInt32Array = main.debug_spawn_units(rig, 1, 0, 0)
	check(ids.size() > 0, "spawned %s" % rig)
	if ids.size() == 0:
		quit(1)
		return
	await turns(2)
	var uid := ids[0]
	main.debug_select_units(PackedInt32Array([uid]))
	await frames(2)
	var start: Vector2 = main.debug_unit_screen(uid)
	var goal := start + Vector2(260, 40)
	await click(goal, MOUSE_BUTTON_RIGHT)
	for t in [0, 1, 2, 3]:
		await turns(2)
		save_crop("t%d" % t, main.debug_unit_screen(uid))
	var end: Vector2 = main.debug_unit_screen(uid)
	check(start.distance_to(end) > 20.0, "unit walked on screen (%.0f px)" % start.distance_to(end))
	print("RESULT rig=%s failures=%d" % [rig, failures])
	quit(failures)
