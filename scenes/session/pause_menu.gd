extends CanvasLayer
# Pause overlay — mirrors 0 A.D. gui/session/Menu.xml + page_session.xml
# Handles ESC pause, resume, options, lobby

func _on_resume_pressed():
	visible = false
	$Background.visible = false
	$CenterContainer.visible = false
	get_tree().paused = false

func _on_options_pressed():
	get_tree().change_scene_to_file("res://scenes/menu/options.tscn")

func _on_lobby_pressed():
	get_tree().change_scene_to_file("res://scenes/menu/lobby.tscn")

func _on_save_pressed():
	var parent = get_parent()
	if parent and parent.has_method("save_game"):
		parent.save_game("user://saves/quicksave.rts0sav")
		if has_node("CenterContainer/Panel/VBox/Title"):
			$CenterContainer/Panel/VBox/Title.text = "Game Saved! (quicksave.rts0sav)"

func _on_load_pressed():
	var parent = get_parent()
	if parent and parent.has_method("load_game"):
		if parent.load_game("user://saves/quicksave.rts0sav"):
			if has_node("CenterContainer/Panel/VBox/Title"):
				$CenterContainer/Panel/VBox/Title.text = "Game Loaded!"
			_on_resume_pressed()

func _on_exit_pressed():
	get_tree().paused = false
	get_tree().change_scene_to_file("res://scenes/menu/main_menu.tscn")

func toggle():
	visible = !visible
	$Background.visible = visible
	$CenterContainer.visible = visible
	get_tree().paused = visible
