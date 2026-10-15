extends Node
## The out-of-match sounds of 0 A.D. 0.28, kept in one autoload so they carry
## over from page to page as 0.28's global music object does.
##
## Music: gui/common/music.js MENU state, set by the main menu (MainMenuPage.js
## initMusic): Honor Bound, then the other three menu tracks shuffled, looping,
## at musicGain 0.3 on the music volume. It keeps playing through Game Setup,
## Options, Load Game, Replays and the other pages, and stops when a match
## starts (the session plays its own playlists, music_manager.h).
##
## Clicks: every button, dropdown and tab outside a match plays
## ui_button_click.ogg on the UI volume (common/modern/styles.xml sound_pressed,
## sound_opened, sound_selected); the session hooks its own controls.

const MUSIC_PATH := "res://assets/audio/music/"
const MENU_TRACKS := ["Honor_Bound.ogg", "An_old_Warhorse_goes_to_Pasture.ogg", "Calm_Before_the_Storm.ogg",
		"Juno_Protect_You.ogg"]
const MUSIC_DB := -10.5 # musicGain 0.3
const CLICK := "res://assets/sounds/ui/ui_button_click.ogg"

var _music: AudioStreamPlayer
var _click: AudioStreamPlayer
var _playlist: Array = []
var _track := -1
var clicks := 0 # for tests

func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS
	_music = AudioStreamPlayer.new()
	_music.bus = "Music"
	_music.volume_db = MUSIC_DB
	_music.finished.connect(_next_track)
	add_child(_music)
	_click = AudioStreamPlayer.new()
	_click.bus = "UI"
	if ResourceLoader.exists(CLICK):
		_click.stream = load(CLICK)
	add_child(_click)
	get_tree().node_added.connect(_on_node_added)

## music.js setState(MENU): starts the menu playlist unless it is already playing.
func play_menu_music() -> void:
	if _track >= 0:
		return
	var rest := MENU_TRACKS.slice(1)
	rest.shuffle()
	_playlist = [MENU_TRACKS[0]] + rest
	_track = -1
	_next_track()

func stop_music() -> void:
	_track = -1
	_music.stop()

func menu_music_track() -> String:
	return _playlist[_track] if _track >= 0 and _music.playing else ""

func _next_track() -> void:
	if _playlist.is_empty():
		return
	_track = (_track + 1) % _playlist.size() # StartPlaylist(isLooping)
	var stream = load(MUSIC_PATH + _playlist[_track]) if ResourceLoader.exists(MUSIC_PATH + _playlist[_track]) else null
	if stream == null:
		return
	_music.stream = stream
	_music.play()

static func _in_session(n: Node) -> bool:
	while n != null:
		if n.has_method("is_game_paused"):
			return true
		n = n.get_parent()
	return false

func _process(_delta: float) -> void:
	if _track < 0:
		return
	for c in get_tree().root.get_children():
		if c.has_method("is_game_paused"):
			stop_music()
			return

func _on_node_added(n: Node) -> void:
	if not (n is BaseButton or n is TabBar or n is ItemList):
		return
	if n.has_meta("_menu_click"):
		return
	n.set_meta("_menu_click", true)
	_hook.call_deferred(n.get_instance_id()) # by id: the node may be freed before the call

func _hook(id: int) -> void:
	var n := instance_from_id(id) as Node
	if n == null or _in_session(n):
		return
	if n is BaseButton:
		n.pressed.connect(_play_click)
		if n.has_signal("item_selected"): # OptionButton and RtsoSafeSelect: sound_selected
			n.item_selected.connect(func(_i): _play_click())
	elif n is TabBar:
		n.tab_clicked.connect(func(_i): _play_click())
	elif n is ItemList and not n.get_parent().get_parent() is BaseButton: # not a dropdown's own list
		n.item_selected.connect(func(_i): _play_click())

func _play_click() -> void:
	clicks += 1
	if _click.stream:
		_click.play()
