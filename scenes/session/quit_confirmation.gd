extends CanvasLayer
# The session's quit questions, ports of 0 A.D. gui/session/message_box
# (GPL-2.0-or-later): SessionMessageBox, QuitConfirmation, QuitConfirmationMenu
# (the menu's Exit), QuitConfirmationDefeat (the player has won or lost) and
# QuitConfirmationReplay (the replay has finished), OutOfSyncReplay and
# OutOfSyncNetwork (a multiplayer match fell out of sync), with session.js endGame:
# "Quit and View Summary" leaves the match for the summary page, "Quit" for the
# main menu (the replay menu after a replay), "Stay" goes back to the game.
#
# ResignConfirmation (the menu's Resign) and ReturnQuestion (an active
# multiplayer client leaving) post the resign command (lockstep 49).

signal pressed(index: int)

const L10n := preload("res://scenes/menu/l10n.gd")

const TITLE := "Confirmation"
const CAPTION := "The game has finished, what do you want to do?"
const BUTTONS := ["Stay", "Quit and View Summary", "Quit"]
const CAPTION_HOST := "Are you sure you want to quit? Leaving will disconnect all other players."
const CAPTION_CLIENT := "Are you sure you want to quit?"
const CAPTION_REPLAY := "The replay has finished. What do you want to do?"
const TITLE_OOS := "Out of Sync"
const CAPTIONS_OOS := ["Out-Of-Sync error on turn %(turn)s.", "The current game state is different from the original game state."]
const CAPTIONS_OOS_NET := {
	"turn": "Out-Of-Sync error on turn %(turn)s.",
	"players": "Players: %(players)s",
	"identical": "Your game state is identical to the hosts game state.",
	"differs": "Your game state differs from the hosts game state.",
	"ai": "Rejoining Multiplayer games with AIs is not supported yet!",
	"mods": "Ensure all players use the same mods.",
	"bug": "Click on \"Report a Bug\" in the main menu to help fix this.",
	"replay": "Replay saved to %(filepath)s",
	"dump": "Dumping current state to %(filepath)s",
}
const TITLE_VICTORY := "VICTORIOUS!"
const TITLE_DEFEATED := "DEFEATED!"
const STATE_MESSAGES := {"won": "You have won!", "defeated": "You have been defeated!"} # messages.js g_PlayerStateMessages
const QUESTION := "Do you want to quit?"
const CAPTION_RESIGN := "Are you sure you want to resign?"
const CAPTION_RETURN := "Do you want to resign or will you return soon?"
const BUTTONS_RETURN := ["I will return", "I resign"]
const SIZE_QUIT := Vector2(600, 200) # QuitConfirmation and its subclasses
const SIZE_OOS_REPLAY := Vector2(500, 140)
const SIZE_OOS_NET := Vector2(600, 280)

var title := ""
var caption := ""
var buttons: Array = []
var main: Node
var _paused_by_me := false
var box_size := Vector2.ZERO # the box's own size (0: SessionMessageBox's)

## QuitConfirmationMenu: the menu's Exit. Single player, replays and observers
## who aren't the host get QuitConfirmation; the host is told the others will
## be disconnected; an active client is asked "Are you sure you want to quit?".
static func menu(m: Node) -> Node:
	var g: Dictionary = m.get_summary_gui(false)
	var networked: bool = g.get("networked", false)
	var controller: bool = g.get("isController", true)
	var observer: bool = g.get("isObserver", false)
	if networked and not controller and not observer:
		var q := _open(m, TITLE, CAPTION_CLIENT, ["No", "Yes"], SIZE_QUIT)
		q.pressed.connect(func(i): if i == 1: return_question(m))
		return q
	return _quit(m, TITLE, CAPTION_HOST if networked and controller else CAPTION)

## ReturnQuestion: "I will return" leaves; "I resign" resigns (and the player then
## gets QuitConfirmationDefeat's question).
static func return_question(m: Node) -> Node:
	var q := _open(m, TITLE, CAPTION_RETURN, BUTTONS_RETURN)
	q.pressed.connect(func(i):
		if i == 0:
			end_game(m, false)
		else:
			m.resign())
	return q

## DeleteSelectionConfirmation: the Delete command (Shift skips it); Yes destroys
## what was selected when it was asked.
static func delete_selection(m: Node) -> Node:
	var q := _open(m, "Delete", "Destroy everything currently selected?", ["No", "Yes"])
	q.pressed.connect(func(i): if i == 1: m._on_delete_confirmed())
	return q

## ResignConfirmation: the menu's Resign.
static func resign(m: Node) -> Node:
	var q := _open(m, TITLE, CAPTION_RESIGN, ["No", "Yes"])
	q.pressed.connect(func(i): if i == 1: m.resign())
	return q

## QuitConfirmationDefeat.confirmExit: the player has won or been defeated. The
## host isn't invited to leave while other humans still play (no buttons then).
static func defeat(m: Node, won: bool, ask_exit: bool) -> Node:
	var text: String = L10n.t(STATE_MESSAGES["won" if won else "defeated"]) + ("\n" + L10n.t(QUESTION) if ask_exit else "")
	var t := TITLE_VICTORY if won else TITLE_DEFEATED
	if not ask_exit:
		return _open(m, t, text, [], SIZE_QUIT)
	return _quit(m, t, text)

## QuitConfirmationReplay: the replay reached its last turn.
static func replay_finished(m: Node) -> Node:
	return _quit(m, TITLE, CAPTION_REPLAY)

## OutOfSyncReplay: the replay's state differs from the recorded game's. The box
## pauses the replay until it is answered.
static func out_of_sync_replay(m: Node) -> Node:
	var turn := int(m.get_replay_status().get("out_of_sync_turn", 0))
	# OutOfSyncReplay: each line its own msgid, then sprintf.
	return _open(m, TITLE_OOS, L10n.sprintf("\n".join(CAPTIONS_OOS.map(func(x): return L10n.t(x))), {"turn": turn}), [], SIZE_OOS_REPLAY)

## TimeWarpMessageBox (developer overlay, Enable time warp): 500x250, OK.
static func time_warp(m: Node) -> Node:
	return _open(m, "Time warp mode", "Note: time warp mode is a developer option, and not intended for use over long periods of time. Using it incorrectly may cause the game to run out of memory or crash.", [], Vector2(500, 250))

## The developer overlay's Activate Rejoin Test (its SessionMessageBox): OK starts it.
static func rejoin_test(m: Node) -> Node:
	var q := _open(m, "Rejoin Test", "Warning: the rejoin test can't be de-activated and is quite slow. Its only purpose is to check for OOS.", ["Cancel", "OK"])
	q.pressed.connect(func(i): if i == 1: m.activate_rejoin_test())
	return q

## The rejoin test found the rejoined copy's state differs from the game's.
static func out_of_sync_rejoin_test(m: Node) -> Node:
	var s: Dictionary = m.get_rejoin_test()
	return _open(m, TITLE_OOS, "Out-Of-Sync on turn %d\nThe rejoin test's copy, rejoined at turn %d, differs from the game." % [int(s.get("out_of_sync_turn", 0)), int(s.get("rejoined_at", 0))], [])

## OutOfSyncNetwork: the host found a peer whose state differs (NMT_SYNC_ERROR).
## The match runs on behind the box; OK closes it.
static func out_of_sync_network(m: Node) -> Node:
	var s: Dictionary = m.get_lobby_state()
	var c := {}
	for k in CAPTIONS_OOS_NET:
		c[k] = L10n.t(CAPTIONS_OOS_NET[k])
	var txt := [L10n.sprintf(c["turn"], {"turn": int(s.get("oos_turn", 0))}),
		L10n.sprintf(c["players"], {"players": L10n.t(", ").join(s.get("oos_players", []))}),
		c["identical"] if s.get("oos_identical", false) else c["differs"], ""]
	if int(s.get("oos_turn", 0)) > 1 and s.get("oos_ai", false):
		txt.append(c["ai"])
	else:
		txt.append_array([c["mods"], c["bug"], L10n.sprintf(c["replay"], {"filepath": str(s.get("oos_replay", ""))}),
			L10n.sprintf(c["dump"], {"filepath": str(s.get("oos_dump", ""))})])
	var q := _open(m, TITLE_OOS, "\n".join(txt), [], SIZE_OOS_NET)
	return q

## session.js endGame: leave the match, for the summary page or the menu the
## match came from: the campaign's menu in a campaign, else the lobby while
## signed in to it, the replay menu after a replay, else the main menu.
static func end_game(m: Node, show_summary: bool) -> void:
	var tree := m.get_tree()
	tree.paused = false
	var ms = tree.root.get_node("MatchSettings")
	var status: Dictionary = m.get_replay_status()
	if show_summary:
		var g: Dictionary = m.get_summary_gui(false)
		g["dialog"] = false
		# The replay is written as the match scene leaves the tree.
		if status.get("recording", false):
			g["replayDirectory"] = str(status.get("replay_dir", ""))
		var summary_script: GDScript = load("res://scenes/session/summary_screen.gd")
		summary_script.pending = {"gui": g, "data": m.get_match_statistics()}
		# 0.28's summary keeps the match's music playing until the main menu
		# (gui/summary/summary.js has no music calls): hand the track over, since
		# the session scene (and its players) leaves the tree with the match.
		var audio: Dictionary = m.get_audio_state()
		ms.match_music = {"track": str(audio.get("track", "")), "position": float(audio.get("music_position", 0.0))}
		ms.go("res://scenes/session/summary_screen.tscn")
	elif ms.campaign_run != "":
		load("res://scenes/session/summary_screen.gd").leave_campaign(m.get_summary_gui(false).get("victory", false))
	elif tree.root.has_node("LobbySession") and tree.root.get_node("LobbySession").signed_in():
		ms.go("res://scenes/menu/lobby.tscn") # Engine.HasXmppClient: back to the lobby
	elif status.get("replaying", false):
		ms.go("res://scenes/menu/replay_browser.tscn")
	else:
		ms.go("res://scenes/menu/main_menu.tscn")

static func _quit(m: Node, t: String, text: String) -> Node:
	var q := _open(m, t, text, BUTTONS, SIZE_QUIT)
	q.pressed.connect(func(i):
		if i == 1 or i == 2:
			end_game(m, i == 1))
	return q

## SessionMessageBox.display: close other dialogs, pause a single-player game
## until the question is answered.
static func _open(m: Node, t: String, text: String, b: Array, size := Vector2.ZERO) -> Node:
	for old in m.get_children():
		if old.get_script() == load("res://scenes/session/quit_confirmation.gd"):
			old.queue_free()
	var pause_menu := m.get_node_or_null("PauseMenu")
	if pause_menu and pause_menu.visible and pause_menu.has_method("_on_resume_pressed"):
		pause_menu._on_resume_pressed()
	if m.has_method("close_open_dialogs"):
		m.close_open_dialogs() # SessionMessageBox.display: closeOpenDialogs
	var q = load("res://scenes/session/quit_confirmation.gd").new()
	q.main = m
	q.title = t
	q.caption = text
	q.buttons = b
	q.box_size = size
	m.add_child(q)
	return q

func _ready() -> void:
	layer = 60
	process_mode = Node.PROCESS_MODE_ALWAYS
	var networked: bool = main != null and main.get_summary_gui(false).get("networked", false)
	if not networked and not get_tree().paused:
		_paused_by_me = true
		get_tree().paused = true
	_build()

## A button of the box (msgbox.js): the box closes, then its action runs.
## Every answer resumes the game ("Stay" calls resumeGame; the others leave it).
func press(index: int) -> void:
	if _paused_by_me:
		get_tree().paused = false
	_paused_by_me = false
	queue_free()
	pressed.emit(index)

func button_texts() -> Array:
	return buttons if not buttons.is_empty() else ["OK"]

func _build() -> void:
	var fade := ColorRect.new()
	fade.color = Color(0, 0, 0, 0.4)
	fade.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(fade)
	var center := CenterContainer.new()
	center.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(center)
	var panel := PanelContainer.new()
	panel.name = "MessageBox"
	# SessionMessageBox 400 x 200, unless the box names its own.
	panel.custom_minimum_size = box_size if box_size != Vector2.ZERO else Vector2(400, 200)
	center.add_child(panel)
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 12)
	panel.add_child(box)
	var head := Label.new()
	head.name = "Title"
	# translate(Title), translate(Caption) and the buttons; a caption built from parts
	# arrives translated line by line.
	head.auto_translate_mode = Node.AUTO_TRANSLATE_MODE_DISABLED
	head.text = L10n.t(title)
	head.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	head.add_theme_font_size_override("font_size", 20)
	head.add_theme_color_override("font_color", Color(1.0, 0.85, 0.45))
	box.add_child(head)
	var body := Label.new()
	body.name = "Caption"
	body.auto_translate_mode = Node.AUTO_TRANSLATE_MODE_DISABLED
	body.text = L10n.t(caption)
	body.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	body.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	body.size_flags_vertical = Control.SIZE_EXPAND_FILL
	box.add_child(body)
	var row := HBoxContainer.new()
	row.alignment = BoxContainer.ALIGNMENT_CENTER
	row.add_theme_constant_override("separation", 12)
	box.add_child(row)
	var texts := button_texts()
	for i in texts.size():
		var b := Button.new()
		b.name = "Button%d" % i
		b.auto_translate_mode = Node.AUTO_TRANSLATE_MODE_DISABLED
		b.text = L10n.t(texts[i])
		b.custom_minimum_size = Vector2(160, 32)
		b.pressed.connect(press.bind(i))
		row.add_child(b)

func _unhandled_input(event: InputEvent) -> void:
	# The message box's cancel hotkey answers with the first button.
	if event is InputEventKey and event.pressed and not event.echo and "cancel" in RtsoHotkeyConfig.new().fired(event):
		get_viewport().set_input_as_handled()
		press(0)
