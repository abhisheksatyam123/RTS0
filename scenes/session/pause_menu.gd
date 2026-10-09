extends CanvasLayer
# Pause overlay — mirrors 0 A.D. gui/session/Menu.xml + page_session.xml
# Handles ESC pause, resume, options, lobby

func _on_resume_pressed():
	visible = false
	$Background.visible = false
	$CenterContainer.visible = false
	get_tree().paused = false

# 0 A.D. opens the options page over the game (Engine.PushGuiPage) instead of leaving the match.
func _on_options_pressed():
	load("res://scenes/menu/options.gd").open_overlay(get_parent())

func _on_lobby_pressed():
	MatchSettings.go("res://scenes/menu/lobby.tscn")

# Save / Load open 0 A.D.'s saved-game dialogs (gui/savedgames); F5 / F6 still quicksave and quickload.
const SavegameDialog = preload("res://scenes/session/savegame_dialog.gd")

func _on_save_pressed():
	var d = SavegameDialog.open(get_parent(), "save", get_parent())
	d.closed.connect(func():
		if has_node("CenterContainer/Panel/VBox/Title"):
			$CenterContainer/Panel/VBox/Title.text = "GAME PAUSED")

func _on_load_pressed():
	SavegameDialog.open(get_parent(), "load", get_parent())

func _on_exit_pressed():
	get_tree().paused = false
	MatchSettings.go("res://scenes/menu/main_menu.tscn")

func toggle():
	visible = !visible
	$Background.visible = visible
	$CenterContainer.visible = visible
	get_tree().paused = visible
