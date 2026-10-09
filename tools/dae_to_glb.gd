# Converts one 0 A.D. source mesh (.dae from binaries/data/mods/public/art/meshes)
# with its skin (.png from art/textures/skins) into a static GLB for
# assets/models/. 0 A.D.'s release archive (public.zip, read by
# extract_0ad_assets.py) carries cached .pmd meshes; this covers art that is
# only reachable as source files.
#
# Usage, from a scratch Godot project holding the .dae and .png (Godot imports
# the Collada file itself):
#   godot --headless --path <dir> --import
#   godot --headless --path <dir> -s dae_to_glb.gd -- <mesh.dae> <skin.png> <out.glb> [prop]
#
# "prop" bakes the mesh into prop-point space the way the unit GLBs from
# extract_0ad_assets.py are laid out (0 A.D.'s PMD axes: x, z_up, y), so it
# can be parented straight to a unit bone such as weapon_R.
extends SceneTree

func _init():
	var args := OS.get_cmdline_user_args()
	if args.size() < 3:
		push_error("usage: -- <mesh.dae> <skin.png> <out.glb> [prop]")
		quit(2)
		return
	var root: Node = load("res://" + args[0]).instantiate()
	var mat := StandardMaterial3D.new()
	mat.albedo_texture = load("res://" + args[1])
	mat.roughness = 0.6
	# Animations come from separate 0 A.D. .dae clips; ship the bind pose only.
	for c in root.get_children():
		if c is AnimationPlayer:
			root.remove_child(c)
			c.free()
	_apply(root, mat)
	# 0 A.D.'s Collada converter ignores <unit> (StandardizeUpAxisAndLength is
	# called without a length), but Godot scales by it; undo that so meshes
	# keep their 0 A.D. size.
	var meter := _unit_meter(args[0])
	if meter > 0.0 and absf(meter - 1.0) > 1e-6:
		# Scale the root's children: glTF import drops a lone root's transform.
		for c in root.get_children():
			if c is Node3D:
				c.transform = Transform3D(Basis.from_scale(Vector3.ONE / meter), Vector3.ZERO) * c.transform
	if args.size() > 3 and args[3] == "prop":
		var baked := _bake_prop(root, mat)
		root.free()
		root = baked
	var doc := GLTFDocument.new()
	var state := GLTFState.new()
	var err := doc.append_from_scene(root, state)
	if err == OK:
		err = doc.write_to_filesystem(state, args[2])
	root.free()
	quit(0 if err == OK else 1)

func _apply(n: Node, mat: Material):
	if n is MeshInstance3D and n.mesh:
		for i in n.mesh.get_surface_count():
			n.mesh.surface_set_material(i, mat)
	for c in n.get_children():
		_apply(c, mat)

func _unit_meter(path: String) -> float:
	var f := FileAccess.open("res://" + path, FileAccess.READ)
	if f == null:
		return 1.0
	var re := RegEx.new()
	re.compile("<unit[^>]*meter=\"([0-9.eE+-]+)\"")
	var m := re.search(f.get_as_text())
	if m == null:
		return 1.0
	return m.get_string(1).to_float()

# Godot turns Collada Z_UP into (x, z, -y); 0 A.D. (and the unit GLBs) use
# (x, z, y). Bake every node transform, mirror z and reverse the winding.
func _bake_prop(root: Node, mat: Material) -> Node3D:
	var out := ArrayMesh.new()
	var stack := [[root, Transform3D()]]
	while not stack.is_empty():
		var item = stack.pop_back()
		var n: Node = item[0]
		var xf: Transform3D = item[1]
		if n is Node3D and n != root:
			xf = xf * n.transform
		for c in n.get_children():
			stack.push_back([c, xf])
		if not (n is MeshInstance3D) or n.mesh == null:
			continue
		for s in n.mesh.get_surface_count():
			var arr: Array = n.mesh.surface_get_arrays(s)
			var v: PackedVector3Array = arr[Mesh.ARRAY_VERTEX]
			var nr: PackedVector3Array = arr[Mesh.ARRAY_NORMAL]
			for i in v.size():
				var p := xf * v[i]
				v[i] = Vector3(p.x, p.y, -p.z)
			for i in nr.size():
				var q := (xf.basis * nr[i]).normalized()
				nr[i] = Vector3(q.x, q.y, -q.z)
			var idx: PackedInt32Array = arr[Mesh.ARRAY_INDEX]
			if idx.is_empty():
				idx.resize(v.size())
				for i in v.size():
					idx[i] = i
			for t in range(0, idx.size() - 2, 3):
				var tmp := idx[t + 1]
				idx[t + 1] = idx[t + 2]
				idx[t + 2] = tmp
			var clean := []
			clean.resize(Mesh.ARRAY_MAX)
			clean[Mesh.ARRAY_VERTEX] = v
			clean[Mesh.ARRAY_NORMAL] = nr
			clean[Mesh.ARRAY_TEX_UV] = arr[Mesh.ARRAY_TEX_UV]
			clean[Mesh.ARRAY_INDEX] = idx
			out.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, clean)
			out.surface_set_material(out.get_surface_count() - 1, mat)
	var top := Node3D.new()
	top.name = root.name
	var mi := MeshInstance3D.new()
	mi.name = String(root.name) + "_mesh"
	mi.mesh = out
	top.add_child(mi)
	mi.owner = top
	return top
