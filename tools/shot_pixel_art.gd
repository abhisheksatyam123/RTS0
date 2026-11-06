extends SceneTree
# Screenshots of a running match for the 3D pixel-art look (Options: pixelart).
# Starts a random mainland match, spawns a few units, points the camera at them and
# saves PNGs to $SHOT_DIR: <name>_0.png (camera at the default pitch) and <name>_1.png
# (the camera closer, at a unit cluster). Set PIXELART=1 or 0 to choose the look.
#
#   VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json xvfb-run -a -s "-screen 0 1280x720x24" \
#     PIXELART=1 SHOT_DIR=/tmp/shots/on SHOT_NAME=match \
#     godot --path . --rendering-driver vulkan --fixed-fps 20 -s tools/shot_pixel_art.gd

var main
var frames_wait := 90

func _initialize() -> void:
	var us = root.get_node("UserSettings")
	us.cfg.set_value("pixelart", "true" if OS.get_environment("PIXELART") == "1" else "false")
	var ms = root.get_node("MatchSettings")
	ms.map_id = OS.get_environment("SHOT_MAP") if OS.get_environment("SHOT_MAP") != "" else "random_mainland"
	ms.seed = 42
	ms.map_size = 192
	ms.slot_teams = [1, 2]
	ms.slot_types = [0, 3]
	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func run() -> void:
	await frames(frames_wait)
	var out := OS.get_environment("SHOT_DIR")
	var name := OS.get_environment("SHOT_NAME") if OS.get_environment("SHOT_NAME") != "" else "match"
	DirAccess.make_dir_recursive_absolute(out)
	var rig_nodes = main.find_children("*", "RtsoCameraRig", true, false)
	if rig_nodes.is_empty():
		print("no camera rig")
		quit(1)
		return
	var rig = rig_nodes[0]
	var spawned = main.debug_spawn_units("athen/infantry_spearman_b", 1, 0, 0)
	await frames(30)
	await RenderingServer.frame_post_draw
	root.get_texture().get_image().save_png(out.path_join("%s_0.png" % name))
	if spawned is Array and spawned.size() > 0 and rig.has_method("set_target_pos"):
		rig.set_target_pos(Vector3(0, 0, 0))
	await frames(30)
	await RenderingServer.frame_post_draw
	root.get_texture().get_image().save_png(out.path_join("%s_1.png" % name))
	print("saved to ", out)
	quit(0)
