extends SceneTree
# Options "Shader effects" (materialmgr.quality, 0 A.D. 0.28 MaterialManager):
# a material whose <alternative quality="N"> is above the setting falls back to
# that alternative, so parallax (_parallax_spec -> _norm_spec at 8) needs 8 and
# normal/specular maps (_norm_spec -> plain at 3) need 3. Materials read the
# setting once, so each quality runs in its own process:
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_shader_effects.gd -- 10
#   (then -- 7 and -- 2). Exit code 0 = pass.

var main
var quality := 10.0

func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() > 0:
		quality = float(args[0])
	RtsoUserConfig.new().set_value("materialmgr.quality", str(quality))
	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	run.call_deferred()

func tally(m: Material, out: Dictionary) -> void:
	var sm := m as ShaderMaterial
	if sm:
		if sm.get_shader_parameter("use_parallax"): out["parallax"] += 1
		if sm.get_shader_parameter("use_normal_map"): out["normal"] += 1
		if sm.get_shader_parameter("use_specular_map"): out["specular"] += 1
	var bm := m as BaseMaterial3D
	if bm and bm.get_feature(BaseMaterial3D.FEATURE_NORMAL_MAPPING): out["normal"] += 1

func count(n: Node, out: Dictionary) -> void:
	var gi := n as GeometryInstance3D
	if gi and gi.material_override:
		tally(gi.material_override, out)
	var mesh: Mesh = null
	if n is MeshInstance3D:
		mesh = n.mesh
	elif n is MultiMeshInstance3D and n.multimesh:
		mesh = n.multimesh.mesh
	if mesh:
		for s in mesh.get_surface_count():
			var m: Material = n.get_surface_override_material(s) if n is MeshInstance3D else null
			tally(m if m else mesh.surface_get_material(s), out)
	for c in n.get_children():
		count(c, out)

func run() -> void:
	for i in 30:
		await process_frame
	# Houses and storehouses use *_parallax_spec materials in every civ.
	main.debug_spawn_building(1, -30.0, 0.0)  # BLD_HOUSE
	main.debug_spawn_building(3, 30.0, 0.0)  # BLD_STOREHOUSE
	for i in 30:
		await process_frame
	var got := {"parallax": 0, "normal": 0, "specular": 0}
	count(root, got)
	print("quality ", quality, ": ", got)
	var ok: bool
	if quality >= 8.0:
		ok = got["parallax"] > 0 and got["normal"] > 0
	elif quality >= 3.0:
		ok = got["parallax"] == 0 and got["normal"] > 0
	else:
		ok = got["parallax"] == 0 and got["normal"] == 0 and got["specular"] == 0
	print("  ok   " if ok else "  FAIL ", "shader effects follow materialmgr.quality ", quality)
	quit(0 if ok else 1)
