@tool
extends EditorPlugin

var _dedup: EditorExportPlugin


func _enter_tree() -> void:
	_dedup = preload("dedup_textures.gd").new()
	add_export_plugin(_dedup)


func _exit_tree() -> void:
	remove_export_plugin(_dedup)
	_dedup = null
