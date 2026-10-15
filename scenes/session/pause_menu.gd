extends CanvasLayer
# The session menu: 0 A.D. 0.28 gui/session/Menu.js and MenuButtons.js (Manual,
# Chat, Save, Summary, Lobby, Options, Hotkeys, Pause, Resign, Exit). Opening it
# doesn't pause the game; its Pause button does, and Manual, Save, Options and
# Hotkeys pause a single-player game while their page is open
# (PauseControl.implicitPause). It processes while
# paused so its buttons, Esc ("cancel") and the menu hotkey keep working.

const L10n = preload("res://scenes/menu/l10n.gd")

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

# Menu.xml / Menu.js startAnimation: the panel (164 wide) hangs under the Menu button at the
# top right and slides down out of it.
func _slide_down() -> void:
	var box: Control = $CenterContainer
	var panel: Control = $CenterContainer/Panel
	var h: float = panel.get_combined_minimum_size().y
	var main := get_parent()
	var at := Rect2(Vector2(get_viewport().get_visible_rect().size.x - 164.0, 0.0), Vector2(164.0, 32.0))
	if main and main.has_method("menu_button_rect"):
		var r: Rect2 = main.menu_button_rect()
		if r.size.x > 0.0:
			at = r
	box.position = Vector2(at.end.x - 164.0, at.end.y + 2.0)
	box.size = Vector2(164.0, h)
	panel.size = Vector2(164.0, h)
	panel.position = Vector2(0.0, -h)
	var tw := create_tween()
	tw.tween_property(panel, "position:y", 0.0, 0.15)

# Closes the menu, through RtsoMain when it is there (Menu.close).
func _on_resume_pressed():
	var main := get_parent()
	if visible and main and main.has_method("_on_menu_button_pressed"):
		main._on_menu_button_pressed()
		return
	visible = false
	$CenterContainer.visible = false

# Menu.initButton: every button closes the menu before it acts.
func _close() -> void:
	if visible:
		_on_resume_pressed()

func _networked() -> bool:
	var main := get_parent()
	return main != null and main.has_method("get_summary_gui") and bool(main.get_summary_gui(false).get("networked", false))

# PauseControl.implicitPause / resumeGame: a single-player game waits while a
# page from the menu is open; a networked one never does.
func _implicit_pause(page: Object, closed_signal: String) -> void:
	if page == null or _networked() or get_tree().paused:
		return
	get_tree().paused = true
	page.connect(closed_signal, func(): get_tree().paused = false, CONNECT_ONE_SHOT)

# MenuButtons Chat: the chat page.
func _on_chat_pressed():
	_close()
	var main := get_parent()
	if main.has_method("toggle_chat"):
		main.toggle_chat()

# MenuButtons Options: the options page over the game (Engine.OpenChildPage).
func _on_options_pressed():
	_close()
	var page = load("res://scenes/menu/options.gd").open_overlay(get_parent())
	_implicit_pause(page, "closed")

# MenuButtons Manual: the game manual (gui/manual).
func _on_manual_pressed():
	_close()
	var page = load("res://scenes/reference/manual.gd").open_overlay(get_parent())
	_implicit_pause(page, "closed")

# MenuButtons Hotkeys: the hotkey editor (gui/hotkeys) over the game.
func _on_hotkeys_pressed():
	_close()
	var page = load("res://scenes/menu/hotkeys.gd").open_overlay(get_parent())
	_implicit_pause(page, "closed")

# MenuButtons Pause: PauseControl.setPaused(!explicitPause, true).
func _on_pause_pressed():
	_close()
	var main := get_parent()
	if main.has_method("toggle_pause"):
		main.toggle_pause()

## MenuButtons Pause.rebuild: enabled when PauseControl.canPause(true), "Resume"
## while the player's own pause holds.
func rebuild_pause() -> void:
	var btn: Button = get_node_or_null("CenterContainer/Panel/VBox/BtnPause")
	var main := get_parent()
	if btn == null or main == null or not main.has_method("is_game_paused"):
		return
	btn.text = L10n.t("Resume") if main.is_game_paused() else L10n.t("Pause")
	btn.disabled = main.has_method("can_pause") and not main.can_pause()
	var keys: Array = _hotkeys.get_hotkey_map().get("pause", [])
	btn.tooltip_text = L10n.sprintf(L10n.t("Press %(hotkey)s to pause or resume the game."),
		{"hotkey": ", ".join(PackedStringArray(keys)) if not keys.is_empty() else L10n.t("Unassigned")})

# 0 A.D. MenuButtons Summary: the summary page over the game ("summary" hotkey).
func _on_summary_pressed():
	var main := get_parent()
	_close()
	if main.has_method("close_open_dialogs"):
		main.close_open_dialogs() # Summary.onPress: closeOpenDialogs
	if main.has_method("toggle_summary"):
		main.toggle_summary()

# MenuButtons Lobby: the multiplayer lobby page over the game, only with a
# lobby connection.
func _on_lobby_pressed():
	_close()
	var main := get_parent()
	if main.has_method("has_lobby_client") and main.has_lobby_client():
		main._on_lobby_button_pressed()

# MenuButtons Save: 0 A.D.'s saved-game page in save mode (gui/savedgames); 0.28's
# session menu has no Load, which stays on the main menu and F6 (quickload).
const SavegameDialog = preload("res://scenes/session/savegame_dialog.gd")

func _on_save_pressed():
	_close()
	var d = SavegameDialog.open(get_parent(), "save", get_parent())
	_implicit_pause(d, "closed")

# 0 A.D. MenuButtons Exit: QuitConfirmationMenu asks first (Stay, Quit and View Summary, Quit).
func _on_exit_pressed():
	_close()
	var main := get_parent()
	if main.has_method("get_summary_gui"):
		load("res://scenes/session/quit_confirmation.gd").menu(main)
		return
	get_tree().paused = false
	MatchSettings.go("res://scenes/menu/main_menu.tscn")

# 0 A.D. MenuButtons Resign: ResignConfirmation asks first; observers (and players
# whose game is over) can't resign.
func _on_resign_pressed():
	_close()
	var main := get_parent()
	if main.has_method("resign"):
		load("res://scenes/session/quit_confirmation.gd").resign(main)

func toggle():
	visible = !visible
	$CenterContainer.visible = visible
	if visible:
		_slide_down()
	rebuild_pause()
	# MenuButtons.js Chat: the chat hotkeys' lines.
	var chat: Button = get_node_or_null("CenterContainer/Panel/VBox/BtnChat")
	if chat and get_parent().has_method("chat_open_hotkey_tooltip"):
		chat.tooltip_text = get_parent().chat_open_hotkey_tooltip()
	var lobby: Button = get_node_or_null("CenterContainer/Panel/VBox/BtnLobby")
	if lobby:
		lobby.disabled = not (get_parent().has_method("has_lobby_client") and get_parent().has_lobby_client())
		var keys: Array = _hotkeys.get_hotkey_map().get("lobby", [])
		lobby.tooltip_text = L10n.sprintf(L10n.t("Press %(hotkey)s to open the multiplayer lobby page without leaving the game."),
			{"hotkey": ", ".join(PackedStringArray(keys)) if not keys.is_empty() else L10n.t("Unassigned")})
	var resign_btn := get_node_or_null("CenterContainer/Panel/VBox/BtnResign")
	if resign_btn and visible and get_parent().has_method("get_summary_gui"):
		resign_btn.disabled = get_parent().get_summary_gui(false).get("isObserver", true)
