extends Control
# Options — mirrors 0 A.D. gui/options/options.json + page_options.xml
# Persists to ConfigFile user://config.ini (like 0ad config/playername.*.xml)

func _on_back_pressed():
	get_tree().change_scene_to_file("res://scenes/menu/main_menu.tscn")
