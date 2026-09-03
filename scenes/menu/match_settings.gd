extends Node
# Survives scene changes so Game Setup can start a match with the chosen civ.

var player_civ: int = 0
var ai_civ: int = 1
var map_index: int = 0
var player_count: int = 2

# Godot 4.3 OptionButton popups must have FLAG_POPUP cleared *before*
# they enter the tree. Clearing it later disconnects root Window signals
# with an empty callable.

func prepare_option(ob: OptionButton) -> OptionButton:
	if ob == null:
		return null
	if not ob.is_inside_tree():
		var p: Window = ob.get_popup()
		if p:
			p.exclusive = false
			p.transient = false
			p.unfocusable = true
			p.set_flag(Window.FLAG_POPUP, false)
	return ob

func prepare_line_edit(le: LineEdit) -> LineEdit:
	if le:
		le.context_menu_enabled = false
	return le

func hide_option_popups(_n: Node = null) -> void:
	var tree := get_tree()
	if tree and tree.root:
		tree.root.gui_release_focus()

func go(path: String) -> int:
	hide_option_popups()
	var tree := get_tree()
	if tree == null:
		return ERR_UNAVAILABLE
	tree.change_scene_to_file.call_deferred(path)
	return OK
