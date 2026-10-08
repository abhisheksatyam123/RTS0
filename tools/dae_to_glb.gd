# Converts one 0 A.D. source mesh (.dae from binaries/data/mods/public/art/meshes)
# with its skin (.png from art/textures/skins) into a static GLB for
# assets/models/. 0 A.D.'s release archive (public.zip, read by
# extract_0ad_assets.py) carries cached .pmd meshes; this covers art that is
# only reachable as source files.
#
# Usage, from a scratch Godot project holding the .dae and .png (Godot imports
# the Collada file itself):
#   godot --headless --path <dir> --import
#   godot --headless --path <dir> -s dae_to_glb.gd -- <mesh.dae> <skin.png> <out.glb>
extends SceneTree

func _init():
	var args := OS.get_cmdline_user_args()
	if args.size() < 3:
		push_error("usage: -- <mesh.dae> <skin.png> <out.glb>")
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
