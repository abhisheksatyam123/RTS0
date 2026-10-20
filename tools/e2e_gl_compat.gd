extends SceneTree
# Godot's Compatibility renderer (OpenGL: Android devices without Vulkan, the web)
# compiles every shader in shaders/ and draws a match with player colours, a
# garrison flag and silhouettes. tools/check_gl_shaders.py runs it under a virtual
# display with --rendering-driver opengl3 and fails on any shader error; headless
# Godot uses a dummy renderer that compiles nothing.
#   1. every shaders/*.gdshader compiles, its `instance uniform`s loaded as plain
#      uniforms as rtso::load_instanced_shader does under Compatibility;
#   2. the session draws units, a garrisoned Civic Center's flag and silhouettes.
# An optional `-- --shot PATH` saves the last frame.
#
#   python3 tools/check_gl_shaders.py [--shot PATH]
# Exit code 0 = pass (shader errors are caught by the wrapper, from the log).

var main
var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func frames(n: int) -> void:
	for i in n:
		await process_frame

func _initialize() -> void:
	run.call_deferred()

func run() -> void:
	print("1. Every shader compiles")
	var cam := Camera3D.new()
	cam.position = Vector3(0, 0, 6)
	root.add_child(cam)
	var probes: Array[Node] = []
	var n := 0
	for f in DirAccess.get_files_at("res://shaders"):
		if not f.ends_with(".gdshader"):
			continue
		var sh := Shader.new()
		sh.code = FileAccess.get_file_as_string("res://shaders/" + f).replace("instance uniform ", "uniform ")
		var mat := ShaderMaterial.new()
		mat.shader = sh
		if sh.get_mode() == Shader.MODE_SPATIAL:
			var mi := MeshInstance3D.new()
			mi.mesh = BoxMesh.new()
			mi.material_override = mat
			root.add_child(mi)
			probes.append(mi)
		elif sh.get_mode() == Shader.MODE_CANVAS_ITEM:
			var cr := ColorRect.new()
			cr.size = Vector2(8, 8)
			cr.material = mat
			root.add_child(cr)
			probes.append(cr)
		n += 1
	await frames(3)
	check(n > 0, "%d shaders drawn" % n)
	for p in probes:
		p.queue_free()
	cam.queue_free()
	await frames(2)

	print("2. A match with player colours, a garrison flag and silhouettes")
	RtsoUserConfig.new().set_value("silhouettes", "true")
	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	await frames(60)
	var cc: int = main.debug_civic_centre_id(0)
	var men: PackedInt32Array = main.debug_spawn_units("athen/infantry_spearman_b", 4)
	check(main.debug_put_inside(men.slice(0, 2), cc) == 2, "two men inside the Civic Center (its flag)")
	main.debug_select_units(men.slice(2))
	await frames(60)
	var args := OS.get_cmdline_user_args()
	var at := args.find("--shot")
	if at >= 0 and at + 1 < args.size():
		root.get_texture().get_image().save_png(args[at + 1])
		print("  shot ", args[at + 1])

	print("FAILURES: ", failures)
	quit(1 if failures else 0)
