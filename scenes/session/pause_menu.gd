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
		# misc.xml cancel: closeOpenDialogs() closes the chat and the other dialogs too.
		var main := get_parent()
		if "cancel" in hk and main and main.has_method("close_open_dialogs"):
			main.close_open_dialogs()

## Menu.js MENU_SPEED: 1.2 px per ms, sliding down to open and back up to close.
const MENU_SPEED := 1200.0

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
	# Menu.xml: the 164 px panel spans 100%-164 to 100%, 8 px past the button's right edge.
	box.position = Vector2(at.end.x + 8.0 - 164.0, at.end.y + 2.0)
	box.size = Vector2(164.0, h)
	panel.size = Vector2(164.0, h)
	panel.position = Vector2(0.0, -h)
	var tw := create_tween()
	tw.tween_property(panel, "position:y", 0.0, h / MENU_SPEED)

# Menu.js close: the panel slides back up into the Menu button. The menu is closed at
# once (so its buttons and Esc stop at once); an inert copy of the panel slides up.
func _slide_up() -> void:
	var box: Control = $CenterContainer
	var panel: Control = $CenterContainer/Panel
	var h: float = panel.size.y
	var main := get_parent()
	if main == null or h <= 0.0 or not box.visible:
		return
	var ghost_layer := CanvasLayer.new()
	ghost_layer.name = "PauseMenuClosing"
	ghost_layer.layer = layer
	ghost_layer.process_mode = Node.PROCESS_MODE_ALWAYS
	var ghost := Control.new()
	ghost.position = box.position
	ghost.size = box.size
	ghost.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var copy: Control = panel.duplicate(0)
	copy.position = panel.position
	copy.size = panel.size
	for c in [copy] + copy.find_children("*", "Control", true, false):
		(c as Control).mouse_filter = Control.MOUSE_FILTER_IGNORE
	ghost.add_child(copy)
	ghost_layer.add_child(ghost)
	main.add_child(ghost_layer)
	var tw := ghost_layer.create_tween()
	tw.tween_property(copy, "position:y", -h, maxf(copy.position.y + h, 0.0) / MENU_SPEED)
	tw.tween_callback(ghost_layer.queue_free)

# Closes the menu, through RtsoMain when it is there (Menu.close).
func _on_resume_pressed():
	var main := get_parent()
	if visible and main and main.has_method("_on_menu_button_pressed"):
		main._on_menu_button_pressed()
		return
	_slide_up()
	visible = false
	$CenterContainer.visible = false

# Menu.initButton: every button closes the menu before it acts.
func _close() -> void:
	if visible:
		_on_resume_pressed()

## MenuButtons: closeOpenDialogs() before a page opens over the game.
func _close_dialogs() -> void:
	_close()
	var main := get_parent()
	if main and main.has_method("close_open_dialogs"):
		main.close_open_dialogs()

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
	# Chat.openPage: opens it afresh on the public chat, never closes it.
	if main.has_method("chat_open_page"):
		main.chat_open_page()

# MenuButtons Options: the options page over the game (Engine.OpenChildPage).
func _on_options_pressed():
	_close_dialogs()
	var page = load("res://scenes/menu/options.gd").open_overlay(get_parent())
	_implicit_pause(page, "closed")

# MenuButtons Manual: the game manual (gui/manual).
func _on_manual_pressed():
	_close_dialogs()
	var page = load("res://scenes/reference/manual.gd").open_overlay(get_parent())
	_implicit_pause(page, "closed")

# MenuButtons Hotkeys: the hotkey editor (gui/hotkeys) over the game.
func _on_hotkeys_pressed():
	_close_dialogs()
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
	btn.tooltip_text = _hotkeys.colorize(L10n.t("Press %(hotkey)s to pause or resume the game."), "pause")

# 0 A.D. MenuButtons Summary: the summary page over the game ("summary" hotkey).
func _on_summary_pressed():
	var main := get_parent()
	_close_dialogs()
	if main.has_method("toggle_summary"):
		main.toggle_summary()

# MenuButtons Lobby: the multiplayer lobby page over the game, only with a
# lobby connection.
func _on_lobby_pressed():
	_close_dialogs()
	var main := get_parent()
	if main.has_method("open_lobby_page"):
		main.open_lobby_page()

# MenuButtons Save: 0 A.D.'s saved-game page in save mode (gui/savedgames); 0.28's
# session menu has no Load, which stays on the main menu and F6 (quickload).
const SavegameDialog = preload("res://scenes/session/savegame_dialog.gd")

func _on_save_pressed():
	_close_dialogs()
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
	if visible:
		_slide_up()
	visible = !visible
	$CenterContainer.visible = visible
	if visible:
		_slide_down()
	rebuild_pause()
	# MenuButtons.js Chat: the chat hotkeys' lines.
	var chat: Button = get_node_or_null("CenterContainer/Panel/VBox/BtnChat")
	if chat and get_parent().has_method("chat_open_hotkey_tooltip"):
		chat.tooltip_text = get_parent().chat_open_hotkey_tooltip()
	# MenuButtons.js Summary: the current "summary" hotkey.
	var summary: Button = get_node_or_null("CenterContainer/Panel/VBox/BtnSummary")
	if summary:
		summary.tooltip_text = _hotkeys.colorize(L10n.t("Press %(hotkey)s to open the summary screen."), "summary")
	var lobby: Button = get_node_or_null("CenterContainer/Panel/VBox/BtnLobby")
	if lobby:
		lobby.disabled = not (get_parent().has_method("has_lobby_client") and get_parent().has_lobby_client())
		lobby.tooltip_text = _hotkeys.colorize(L10n.t("Press %(hotkey)s to open the multiplayer lobby page without leaving the game."), "lobby")
	var resign_btn := get_node_or_null("CenterContainer/Panel/VBox/BtnResign")
	if resign_btn and visible and get_parent().has_method("get_summary_gui"):
		resign_btn.disabled = get_parent().get_summary_gui(false).get("isObserver", true)
