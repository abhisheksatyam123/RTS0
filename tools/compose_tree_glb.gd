# Builds one static GLB from a composed 0 A.D. actor plan written by
# fetch_0ad_trees.py: a tree of {"mesh", "skin", "alpha", "props": [{"attach",
# "node"}]} where every prop's mesh sits at its parent mesh's prop point.
#
# Usage, from the scratch Godot project holding the plan's .dae and .png files:
#   godot --headless --path <dir> --import
#   godot --headless --path <dir> -s compose_tree_glb.gd -- <plan.json> <out.glb> [info.json]
#
# The GLB is a scene of mesh instances (like dae_to_glb.gd's default output), one
# per source mesh with its node transforms baked into the vertices: Y up, 0 A.D.
# metres, the root mesh's origin at the scene origin. 0 A.D. sinks trunks and
# roots below the terrain, but RTS0 sits a model on the ground by its lowest
# point, so vertices below y = 0 are pressed up onto it. info.json gets the
# bounds ({"size": [x, y, z], "position": [x, y, z]}).
extends SceneTree

func _init():
	var args := OS.get_cmdline_user_args()
	if args.size() < 2:
		push_error("usage: -- <plan.json> <out.glb> [info.json]")
		quit(2)
		return
	var plan = JSON.parse_string(FileAccess.get_file_as_string(args[0]))
	if typeof(plan) != TYPE_DICTIONARY:
		push_error("bad plan " + args[0])
		quit(2)
		return
	var composed := Node3D.new()
	_add_actor(composed, plan, Transform3D())
	var root := Node3D.new()
	root.name = String(plan.get("actor", "tree")).get_file().get_basename()
	_bake(composed, root)
	composed.free()
	var aabb := _scene_aabb(root)
	var doc := GLTFDocument.new()
	var state := GLTFState.new()
	var err := doc.append_from_scene(root, state)
	if err == OK:
		err = doc.write_to_filesystem(state, args[1])
	root.free()
	if args.size() > 2:
		var f := FileAccess.open(args[2], FileAccess.WRITE)
		f.store_string(JSON.stringify({
			"size": [aabb.size.x, aabb.size.y, aabb.size.z],
			"position": [aabb.position.x, aabb.position.y, aabb.position.z]}))
		f.close()
	print("aabb ", aabb)
	quit(0 if err == OK else 1)

# Instances one plan node's mesh under `root` at `xf`, then its props at the
# mesh's prop points.
func _add_actor(root: Node3D, node: Dictionary, xf: Transform3D) -> void:
	var scene: Node3D = null
	if node.get("mesh"):
		scene = _load_mesh(node["mesh"], node.get("skin"), node.get("alpha", false))
		scene.transform = xf
		root.add_child(scene)
	for p in node.get("props", []):
		var at := xf
		if scene != null and p["attach"] != "root":
			var local = _prop_point(scene, p["attach"])
			if local == null:
				push_warning("%s: no prop point %s, using origin" % [node["mesh"], p["attach"]])
			else:
				at = xf * local
		_add_actor(root, p["node"], at)

# A loaded .dae scene with its skin applied, animations dropped and the
# Collada <unit> scale undone.
func _load_mesh(dae: String, skin, alpha: bool) -> Node3D:
	var packed: PackedScene = load("res://" + dae)
	var scene: Node3D = packed.instantiate()
	scene.name = dae.get_file().get_basename().split("__")[-1]  # gaia__tree_x -> tree_x
	var mat := StandardMaterial3D.new()
	if skin:
		mat.albedo_texture = load("res://" + skin)
	mat.roughness = 0.8
	if alpha:
		# Leaf cards: cut out, seen from both sides (0 A.D.'s basic_trans materials).
		mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR
		mat.alpha_scissor_threshold = 0.5
		mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	# Wind/idle clips are not used; ship the bind pose only.
	for c in scene.get_children():
		if c is AnimationPlayer:
			scene.remove_child(c)
			c.free()
	_apply(scene, mat)
	# 0 A.D.'s Collada converter ignores <unit> but Godot scales by it (see
	# dae_to_glb.gd); scale the root's children back.
	var meter := _unit_meter(dae)
	if meter > 0.0 and absf(meter - 1.0) > 1e-6:
		for c in scene.get_children():
			if c is Node3D:
				c.transform = Transform3D(Basis.from_scale(Vector3.ONE / meter), Vector3.ZERO) * c.transform
	return scene

# Scene-root-relative transform of the "prop_<name>" / "prop-<name>" node (or
# skeleton bone), rotation and position only like a PMD prop point; null if absent.
func _prop_point(scene: Node3D, name: String):
	for cand in ["prop_" + name, "prop-" + name]:
		var n := _find(scene, cand)
		if n != null:
			return _relative(scene, n).orthonormalized()
		for sk in scene.find_children("*", "Skeleton3D", true, false):
			var b: int = sk.find_bone(cand)
			if b >= 0:
				return (_relative(scene, sk) * sk.get_bone_global_rest(b)).orthonormalized()
	return null

func _find(n: Node, name: String) -> Node3D:
	for c in n.get_children():
		if c is Node3D and String(c.name).to_lower() == name.to_lower():
			return c
		var r := _find(c, name)
		if r != null:
			return r
	return null

func _relative(top: Node3D, n: Node3D) -> Transform3D:
	var xf := Transform3D()
	var cur: Node = n
	while cur != top and cur is Node3D:
		xf = cur.transform * xf
		cur = cur.get_parent()
	return xf

func _apply(n: Node, mat: Material) -> void:
	if n is MeshInstance3D and n.mesh:
		for i in n.mesh.get_surface_count():
			n.mesh.surface_set_material(i, mat)
	for c in n.get_children():
		_apply(c, mat)

# Copies every mesh under `composed` into `out` as a MeshInstance3D with an
# identity transform, its scene transform baked in and y clamped at 0.
func _bake(composed: Node3D, out: Node3D) -> void:
	var stack := [[composed, Transform3D()]]
	while not stack.is_empty():
		var item = stack.pop_back()
		var n: Node = item[0]
		var xf: Transform3D = item[1]
		if n is Node3D and n != composed:
			xf = xf * n.transform
		for c in n.get_children():
			stack.push_back([c, xf])
		if not (n is MeshInstance3D) or n.mesh == null:
			continue
		var flip := xf.basis.determinant() < 0.0  # mirrored node: keep faces outward
		var mesh := ArrayMesh.new()
		for s in n.mesh.get_surface_count():
			var arr: Array = n.mesh.surface_get_arrays(s)
			var v: PackedVector3Array = arr[Mesh.ARRAY_VERTEX]
			var nr: PackedVector3Array = arr[Mesh.ARRAY_NORMAL]
			for i in v.size():
				var p := xf * v[i]
				v[i] = Vector3(p.x, maxf(p.y, 0.0), p.z)
			var nbasis := xf.basis.inverse().transposed()
			for i in nr.size():
				nr[i] = (nbasis * nr[i]).normalized()
			var idx: PackedInt32Array = arr[Mesh.ARRAY_INDEX]
			if idx.is_empty():
				idx.resize(v.size())
				for i in v.size():
					idx[i] = i
			if flip:
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
			mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, clean)
			mesh.surface_set_material(mesh.get_surface_count() - 1, n.mesh.surface_get_material(s))
		var mi := MeshInstance3D.new()
		mi.name = n.get_parent().name if n.get_parent() != composed else n.name
		mi.mesh = mesh
		out.add_child(mi)
		mi.owner = out

# Bounds of the baked meshes (identity transforms under `root`).
func _scene_aabb(root: Node3D) -> AABB:
	var out := AABB()
	var first := true
	for c in root.get_children():
		if c is MeshInstance3D:
			var box: AABB = c.mesh.get_aabb()
			out = box if first else out.merge(box)
			first = false
	return out

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
