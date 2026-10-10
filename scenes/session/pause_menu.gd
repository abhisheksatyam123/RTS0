extends CanvasLayer
# Pause overlay — mirrors 0 A.D. gui/session/Menu.xml + page_session.xml
# Handles ESC pause, resume, options, lobby
#
# The menu pauses the tree, so it processes while paused (it would ignore its
# own buttons otherwise) and closes itself on Esc ("cancel") or the menu
# hotkey, which RtsoMain cannot see while it is paused.

var _hotkeys := RtsoHotkeyConfig.new()

func _ready():
	process_mode = Node.PROCESS_MODE_ALWAYS

func _unhandled_input(event):
	if not visible or not (event is InputEventKey or event is InputEventMouseButton):
		return
	var hk: PackedStringArray = _hotkeys.fired(event)
	if "cancel" in hk or "session.gui.menu.toggle" in hk:
		get_viewport().set_input_as_handled()
		_on_resume_pressed()

# Through RtsoMain when it is there, so its menu pause (and a multiplayer
# pause) ends too.
func _on_resume_pressed():
	var main := get_parent()
	if visible and main and main.has_method("_on_menu_button_pressed"):
		main._on_menu_button_pressed()
		return
	visible = false
	$Background.visible = false
	$CenterContainer.visible = false
	get_tree().paused = false

# 0 A.D. opens the options page over the game (Engine.PushGuiPage) instead of leaving the match.
func _on_options_pressed():
	load("res://scenes/menu/options.gd").open_overlay(get_parent())

# 0 A.D. MenuButtons Summary: the summary page over the game ("summary" hotkey).
func _on_summary_pressed():
	var main := get_parent()
	if main.has_method("_on_menu_button_pressed"):
		main._on_menu_button_pressed() # close the menu the way Esc does
	if main.has_method("toggle_summary"):
		main.toggle_summary()

func _on_lobby_pressed():
	get_tree().paused = false
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
