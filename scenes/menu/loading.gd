extends CanvasLayer
## Loading page, ported from 0 A.D. gui/loading (loading.js, ProgressBar.js,
## TitleDisplay.js, TipDisplay.js, QuoteDisplay.js; layout from loading.xml).
## MatchSettings.go() sends every match start here: a new game, a saved game,
## a replay or a multiplayer game. The page shows the map title, a random
## gameplay tip with its picture, a random quote and a progress bar. It loads
## main.tscn on a loader thread, then adds the match to the tree and follows
## RtsoMain's load_progress signal while the map is set up. When the match has
## drawn its first frames, the page frees itself (0 A.D.'s reallyStartGame).
## A networked Start (0.28 GameSettingsController switching every peer to
## page_loading at "start") loads the map inside the match already in the
## tree: RtsoMain adds this page over itself first, with overlay_for set, and
## the page follows load_progress the same way.
## The tip is scenes/reference/tip_display.gd; the tips, quotes and pictures
## come from tools/extract_0ad_loading.py.

const MATCH_SCENE := "res://scenes/main.tscn"
const DATA := "res://data/reference/loading.json"
const ART := "res://assets/textures/ui/loading/"
## ProgressBar.prototype.ConfigKey: show the stage instead of the percentage.
const DESCRIPTION_KEY := "gui.loadingscreen.progressdescription"
## Frames the match draws under the page before the page goes away, so models
## and terrain are built behind it rather than in front of the player.
const SETTLE_FRAMES := 3

enum State { LOADING_SCENE, STARTING, SETTLING, DONE }

var state := State.LOADING_SCENE
var match_node: Node
var tip_name := ""
var quote := ""
var title := ""
var progress := 0
## Overlay mode: the match scene that loads the map, and the map it loads.
var overlay_for: Node
var overlay_map := ""
## The stages the last match reported (for tests and the log).
static var last_stages: Array = []

var _settle := SETTLE_FRAMES
var _show_description := false
var _bar_fill: TextureRect
var _bar_right: TextureRect
var _bar_text: Label
var _bar_width := 400.0

const WAIT_CURSOR := "res://assets/textures/cursors/cursor-wait.png"

func _ready() -> void:
	layer = 100
	process_mode = Node.PROCESS_MODE_ALWAYS
	_show_description = RtsoUserConfig.new().get_bool(DESCRIPTION_KEY)
	var data := _read_json(DATA)
	_pick(data)
	_build()
	# loading.js: Engine.SetCursor("cursor-wait"), 0.28's art (cursor-wait.txt: hotspot 16 16).
	if ResourceLoader.exists(WAIT_CURSOR):
		Input.set_custom_mouse_cursor(load(WAIT_CURSOR), Input.CURSOR_WAIT, Vector2(16, 16))
	Input.set_default_cursor_shape(Input.CURSOR_WAIT)
	if overlay_for != null:
		last_stages = []
		match_node = overlay_for
		if match_node.has_signal("load_progress"):
			match_node.load_progress.connect(_on_load_progress)
		set_progress(25, "Loading")
		state = State.SETTLING
		return
	if ResourceLoader.load_threaded_request(MATCH_SCENE) != OK:
		push_error("[Loading] could not start loading " + MATCH_SCENE)
	set_progress(0, "Loading")

func _process(_delta: float) -> void:
	match state:
		State.LOADING_SCENE:
			var p: Array = []
			var status := ResourceLoader.load_threaded_get_status(MATCH_SCENE, p)
			if status == ResourceLoader.THREAD_LOAD_IN_PROGRESS:
				set_progress(int(float(p[0] if not p.is_empty() else 0.0) * 25.0), "Loading")
			elif status == ResourceLoader.THREAD_LOAD_LOADED:
				set_progress(25, "Loading")
				# Start next frame, so the bar at 25% is on screen first.
				state = State.STARTING
			else:
				push_error("[Loading] loading %s failed" % MATCH_SCENE)
				state = State.DONE
				get_tree().change_scene_to_file("res://scenes/menu/main_menu.tscn")
		State.STARTING:
			_start_match()
		State.SETTLING:
			_settle -= 1
			if _settle <= 0:
				_finish()

func _start_match() -> void:
	last_stages = []
	var packed: PackedScene = ResourceLoader.load_threaded_get(MATCH_SCENE)
	match_node = packed.instantiate()
	if match_node.has_signal("load_progress"):
		match_node.load_progress.connect(_on_load_progress)
	var tree := get_tree()
	# The match sets itself up in _ready (map, terrain, units), reporting
	# through load_progress while the page stays on top.
	tree.root.add_child(match_node)
	tree.current_scene = match_node
	set_progress(95, "Starting")
	state = State.SETTLING

func _on_load_progress(percent: int, description: String) -> void:
	last_stages.append([percent, description])
	set_progress(maxi(percent, progress), description)
	# 0 A.D. redraws the loading page between load steps (LDR_ProgressiveLoad);
	# setup runs on the main thread, so draw the frame by hand.
	if DisplayServer.get_name() != "headless":
		RenderingServer.force_draw(false)

func _finish() -> void:
	set_progress(100, "Starting")
	state = State.DONE
	Input.set_default_cursor_shape(Input.CURSOR_ARROW)
	queue_free()

## ProgressBar.onGameLoadProgress: "Show 100 when it is really 99".
func set_progress(percent: int, description: String) -> void:
	progress = clampi(percent, 0, 100)
	if _bar_text == null:
		return
	var shown := mini(progress + 1, 100)
	_bar_text.text = description if _show_description else \
		tr("%(percentage)s%%", "loading screen progress").replace("%(percentage)s", str(shown)).replace("%%", "%")
	# progressBarSize: the bar less half the right cap (16 px).
	var w := roundf(shown * (_bar_width - 8.0) / 100.0)
	_bar_fill.size.x = w
	_bar_right.position.x = _bar_fill.position.x + w

func _read_json(path: String) -> Dictionary:
	if not FileAccess.file_exists(path):
		return {}
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	return parsed if typeof(parsed) == TYPE_DICTIONARY else {}

func _pick(data: Dictionary) -> void:
	var quotes: Array = data.get("quotes", [])
	if not quotes.is_empty():
		quote = tr(str(quotes.pick_random()))
	title = _title()

## Engine.HasNetClient(): a multiplayer match weighs the tips by their MP share.
func _networked() -> bool:
	var ms = get_node_or_null("/root/MatchSettings")
	return ms != null and str(ms.net_mode) != ""

## TitleDisplay.js: "Generating “map”" for random maps, else "Loading “map”".
func _title() -> String:
	var ms = get_node_or_null("/root/MatchSettings")
	# gamesetup_mp.js: a joiner is connecting; the host's map comes later.
	if overlay_for == null and ms and str(ms.net_mode) == "join":
		return "Connecting to server..."
	var map_id := ""
	var map_name := ""
	if overlay_for != null:
		map_id = overlay_map
	elif ms:
		if ms.replay_dir != "":
			var meta := _read_json(ms.replay_dir.path_join("metadata.json"))
			map_id = str(meta.get("map_id", ""))
			map_name = str(meta.get("map_name", ""))
		elif ms.load_save_path != "":
			var meta := _read_json(ms.load_save_path.get_basename() + ".json")
			map_id = str(meta.get("map_id", ""))
			map_name = str(meta.get("map_name", ""))
		else:
			map_id = ms.map_id
	if map_name == "":
		map_name = map_display_name(map_id)
	var random := map_id.begins_with("random_") or map_id.begins_with("procedural_")
	return tr("Generating “%(map)s”" if random else "Loading “%(map)s”").replace("%(map)s", tr(map_name))

static func map_display_name(map_id: String) -> String:
	for r in load("res://scenes/menu/gamesetup.gd").RMS_MAPS:
		if r[0] == map_id:
			return str(r[1])
	# Scenario maps keep their name near the top of the file; skip parsing the rest.
	var path := "res://data/maps/%s.json" % map_id
	if FileAccess.file_exists(path):
		var f := FileAccess.open(path, FileAccess.READ)
		var head := f.get_buffer(1024).get_string_from_utf8()
		var m := RegEx.create_from_string("\"name\"\\s*:\\s*\"([^\"]*)\"").search(head)
		if m:
			return m.get_string(1)
	return map_id.trim_prefix("random_").capitalize()

func _tex(file: String) -> Texture2D:
	return load(ART + file) as Texture2D if ResourceLoader.exists(ART + file) else null

## A control placed like a 0 A.D. GUI object: offsets from the screen centre.
func _centered(parent: Control, l: float, t: float, r: float, b: float) -> Control:
	var c := Control.new()
	parent.add_child(c)
	c.anchor_left = 0.5
	c.anchor_right = 0.5
	c.anchor_top = 0.5
	c.anchor_bottom = 0.5
	c.offset_left = l
	c.offset_top = t
	c.offset_right = r
	c.offset_bottom = b
	return c

func _build() -> void:
	var root := Control.new()
	root.name = "LoadingPage"
	root.set_anchors_preset(Control.PRESET_FULL_RECT)
	root.mouse_filter = Control.MOUSE_FILTER_STOP
	add_child(root)
	var bg := TextureRect.new()
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	bg.texture = _tex("background-tile.png")
	bg.stretch_mode = TextureRect.STRETCH_TILE
	bg.modulate = Color(0.35, 0.35, 0.35)
	root.add_child(bg)

	# ProgressBar.xml: 512 x 32 at the top centre, the bar 56..456 inside.
	var bar := Control.new()
	root.add_child(bar)
	bar.anchor_left = 0.5
	bar.anchor_right = 0.5
	bar.offset_left = -256
	bar.offset_right = 256
	bar.offset_top = 4
	bar.offset_bottom = 36
	for piece in [["progressbar/progressbar_background_left.png", 0, 64],
			["progressbar/progressbar_background_middle.png", 64, 448],
			["progressbar/progressbar_background_right.png", 448, 512]]:
		var t := TextureRect.new()
		t.texture = _tex(piece[0])
		t.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
		t.stretch_mode = TextureRect.STRETCH_SCALE
		t.position = Vector2(piece[1], 0)
		t.size = Vector2(piece[2] - piece[1], 32)
		bar.add_child(t)
	_bar_fill = TextureRect.new()
	_bar_fill.texture = _tex("progressbar/progressbar_middle.png")
	_bar_fill.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	_bar_fill.stretch_mode = TextureRect.STRETCH_SCALE
	_bar_fill.position = Vector2(56, 0)
	_bar_fill.size = Vector2(0, 32)
	bar.add_child(_bar_fill)
	var left := TextureRect.new()
	left.texture = _tex("progressbar/progressbar_left.png")
	left.position = Vector2(56 - 24, 0)
	bar.add_child(left)
	_bar_right = TextureRect.new()
	_bar_right.texture = _tex("progressbar/progressbar_right.png")
	_bar_right.position = Vector2(56, 0)
	bar.add_child(_bar_right)
	_bar_text = Label.new()
	_bar_text.name = "ProgressText"
	_bar_text.position = Vector2(56, 0)
	_bar_text.size = Vector2(_bar_width, 32)
	_bar_text.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_bar_text.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	_outlined(_bar_text, 14)
	bar.add_child(_bar_text)

	# TitleDisplay.xml: between the bar and the tip.
	var title_label := Label.new()
	title_label.name = "MapTitle"
	title_label.text = title
	title_label.anchor_left = 0.5
	title_label.anchor_right = 0.5
	title_label.anchor_bottom = 0.5
	title_label.offset_left = -256 # loading.xml: 50%-256 .. 50%+256
	title_label.offset_right = 256
	title_label.offset_top = 36
	title_label.offset_bottom = -196
	title_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	title_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	_outlined(title_label, 26)
	root.add_child(title_label)

	# TipDisplay.xml: one tip from a category weighted for single- or multiplayer.
	var tip := _centered(root, -452, -196, 452, 196)
	tip.name = "Tip"
	var display = load("res://scenes/reference/tip_display.gd").new()
	display.name = "TipDisplay"
	display.loading_screen = true
	display.networked = _networked()
	tip.add_child(display)
	tip_name = display.current_name()

	# QuoteDisplay.xml: under the tip.
	var quote_label := Label.new()
	quote_label.name = "Quote"
	quote_label.text = quote
	quote_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	quote_label.anchor_left = 0.5
	quote_label.anchor_right = 0.5
	quote_label.anchor_top = 0.5
	quote_label.anchor_bottom = 1.0
	quote_label.offset_left = -448
	quote_label.offset_right = 448
	quote_label.offset_top = 230
	quote_label.offset_bottom = -16
	_outlined(quote_label, 14)
	root.add_child(quote_label)

func _outlined(l: Label, size: int) -> void:
	l.add_theme_font_size_override("font_size", size)
	l.add_theme_color_override("font_color", Color.WHITE)
	l.add_theme_color_override("font_outline_color", Color.BLACK)
	l.add_theme_constant_override("outline_size", 4)
