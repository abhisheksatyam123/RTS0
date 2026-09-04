extends Node
# Survives scene changes so Game Setup can start a match with the chosen civ.

var player_civ: int = 0
var ai_civ: int = 1
var map_index: int = 0
var player_count: int = 2

func strip_engine_popups(n: Node) -> void:
	if n == null:
		return
	for p in n.find_children("*", "Popup", true, false):
		var par: Node = p.get_parent()
		if par:
			par.remove_child(p)
		p.queue_free()

func prepare_line_edit(le: LineEdit) -> LineEdit:
	if le:
		le.context_menu_enabled = false
		le.middle_mouse_paste_enabled = false
		strip_engine_popups(le)
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
