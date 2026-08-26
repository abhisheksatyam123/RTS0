extends Control
# Main menu — mirrors 0 A.D. page_splashscreen.xml + page_pregame.xml
# Routes to gamesetup / lobby / options. Keeps SoA sim out of menu.

func _on_single_pressed():
	# Single-player skirmish: Gerasa (aegean) — mirrors 0ad autostart + page_pregame
	get_tree().change_scene_to_file("res://scenes/menu/gamesetup.tscn")

func _on_multi_pressed():
	get_tree().change_scene_to_file("res://scenes/menu/lobby.tscn")

func _on_options_pressed():
	get_tree().change_scene_to_file("res://scenes/menu/options.tscn")

func _on_exit_pressed():
	get_tree().quit()
