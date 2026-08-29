extends Control

func _on_single_pressed() -> void:
	play_click()
	get_tree().change_scene_to_file("res://scenes/main.tscn")

func _on_setup_pressed() -> void:
	play_click()
	get_tree().change_scene_to_file("res://scenes/menu/gamesetup.tscn")

func _on_encyclopedia_pressed() -> void:
	play_click()
	get_tree().change_scene_to_file("res://scenes/menu/encyclopedia.tscn")

func _on_asset_viewer_pressed() -> void:
	play_click()
	get_tree().change_scene_to_file("res://scenes/tools/asset_viewer.tscn")

func _on_replays_pressed() -> void:
	play_click()
	get_tree().change_scene_to_file("res://scenes/menu/replay_browser.tscn")

func _on_multi_pressed() -> void:
	play_click()
	get_tree().change_scene_to_file("res://scenes/menu/lobby.tscn")

func _on_options_pressed() -> void:
	play_click()
	get_tree().change_scene_to_file("res://scenes/menu/options.tscn")

func _on_exit_pressed() -> void:
	get_tree().quit()

func play_click() -> void:
	var sfx: AudioStreamPlayer = get_node_or_null("ClickSFX")
	if sfx:
		sfx.play()
