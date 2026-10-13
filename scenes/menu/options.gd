extends Control
## Options page, port of 0 A.D. gui/options (options.js and options.json,
## GPL-2.0-or-later): one tab per category, each option a checkbox, slider,
## number or dropdown bound to a 0 A.D. config key. Changes apply
## at once (ConfigDB_CreateValue); Save writes them to user://config/user.cfg,
## Revert goes back to the saved values, Reset to 0 A.D.'s defaults, and Close
## with unsaved changes asks first. Only the options RTS0 implements are
## listed; scenes/menu/user_settings.gd and RtsoMain apply them. Hotkeys opens
## the hotkey editor (Session UI thread). From a game the page opens over it.

## [category, [[type, key, label, tooltip, extra], ...]]; extra is
## [min, max] for slider and number, [[label, value], ...] for dropdowns.
const SCHEMA := [
	["General", [
		["dropdown", "locale", "Language", "Choose the language of the game.", "locales"],
		["boolean", "pauseonfocusloss", "Background pause", "Pause single-player games when window loses focus."],
		["boolean", "overlay.fps", "FPS overlay", "Show frames per second in top right corner."],
		["boolean", "overlay.realtime", "Real time overlay", "Show current system time in top right corner."],
		["dropdown", "gui.session.howtoshownames", "Naming of entities.", "How to show entity names.",
			[["Specific names first", "0"], ["Generic names first", "1"], ["Only specific names", "2"], ["Only generic names", "3"]]],
	]],
	["Graphics (general)", [
		["boolean", "windowed", "Windowed mode", "Start RTS0 in a window."],
		["boolean", "fog", "Fog", "Enable fog."],
		["boolean", "postproc", "Post-processing", "Use screen-space post-processing filters (HDR, Bloom, DOF, etc)."],
		["dropdown", "renderer.scale", "Resolution scale", "A smaller scale makes rendering faster but produces a more blurry picture, a large scale makes rendering slower but produces a better picture.",
			[["50%", "0.5"], ["75%", "0.75"], ["87.5%", "0.875"], ["100%", "1.0"], ["125%", "1.25"], ["150%", "1.5"], ["175%", "1.75"], ["200%", "2.0"]]],
		["dropdown", "renderer.upscale.technique", "Upscale technique", "Technique defines performance and quality of upscaling process.",
			[["FidelityFX Super Resolution 1.0", "fsr"], ["Bilinear", "bilinear"]]],
		["boolean", "shadows", "Shadows", "Enable shadows."],
		["boolean", "particles", "Particles", "Enable particles."],
		["boolean", "vsync", "VSync", "Run vertical sync to fix screen tearing."],
		["slider", "adaptivefps.menu", "FPS throttling in menus", "To save CPU workload, throttle render frequency in all menus. Set to maximum to disable throttling.", [20, 360]],
		["slider", "adaptivefps.session", "FPS throttling in games", "To save CPU workload, throttle render frequency in running games. Set to maximum to disable throttling.", [20, 360]],
		["dropdown", "gui.scale", "GUI scale", "GUI scale",
			[["75%", "0.75"], ["100%", "1.0"], ["125%", "1.25"], ["150%", "1.5"], ["175%", "1.75"], ["200%", "2.0"], ["225%", "2.25"], ["250%", "2.5"]]],
		["number", "gui.session.dragdelta", "Mouse drag", "Number of pixels the mouse can move before the action is considered a drag.", [1, 200]],
		["boolean", "window.mousegrabinfullscreen", "Mouse grab in fullscreen", "Constrain mouse in the fullscreen mode to the window boundaries. It's used to avoid mouse going out of a display in case of multiple displays."],
		["boolean", "window.mousegrabinwindowmode", "Mouse grab in window mode", "Constrain mouse in the window mode to the window boundaries."],
	]],
	["Graphics (advanced)", [
		["dropdown", "antialiasing", "Antialiasing", "Reduce aliasing effect on edges.",
			[["Disabled", "disabled"], ["FXAA", "fxaa"], ["MSAA (2×)", "msaa2"], ["MSAA (4×)", "msaa4"], ["MSAA (8×)", "msaa8"]]],
		["dropdown", "shadowquality", "Shadow quality", "Shadow map resolution. High values can crash the game when using a graphics card with low memory!",
			[["Low", "-1"], ["Medium", "0"], ["High", "1"], ["Very High", "2"]]],
		["boolean", "shadowpcf", "Shadow filtering", "Smooth shadows."],
		["slider", "shadowscutoffdistance", "Shadow cutoff distance", "Hides shadows beyond a certain distance from a camera.", [100, 1500]],
	]],
	["Sound", [
		["slider", "sound.mastergain", "Master volume", "Master audio gain.", [0, 2]],
		["slider", "sound.musicgain", "Music volume", "In game music gain.", [0, 2]],
		["slider", "sound.ambientgain", "Ambient volume", "In game ambient sound gain.", [0, 2]],
		["slider", "sound.actiongain", "Action volume", "In game unit action sound gain.", [0, 2]],
		["slider", "sound.uigain", "UI volume", "UI sound gain.", [0, 2]],
	]],
	["Game Setup", [
		["dropdown", "gui.gamesetup.aidifficulty", "Default AI difficulty", "Default difficulty of the AI.",
			[["Sandbox", "0"], ["Very Easy", "1"], ["Easy", "2"], ["Medium", "3"], ["Hard", "4"], ["Very Hard", "5"]]],
		["dropdown", "gui.gamesetup.aibehavior", "Default AI behavior", "Default behavior of the AI.",
			[["Random", "random"], ["Balanced", "balanced"], ["Aggressive", "aggressive"], ["Defensive", "defensive"]]],
		["dropdown", "gui.gamesetup.assignplayers", "Assign players", "Automatically assign joining clients to free player slots during the match setup.",
			[["Everyone", "everyone"], ["Buddies", "buddies"], ["Disabled", "disabled"]]],
	]],
	["Game Session", [
		["slider", "gui.session.woundedunithotkeythreshold", "Wounded unit health", "The wounded unit hotkey considers the selected units as wounded if their health percentage falls below this number.", [0, 100]],
		["number", "gui.session.batchtrainingsize", "Batch training size", "Number of units trained per batch by default.", [1, 20]],
		["slider", "gui.session.flarelifetime", "Flare display duration", "How long the flare markers on the minimap are displayed in seconds.", [0, 60]],
		["boolean", "chat.timestamp", "Chat timestamp", "Display the time at which a chat message was posted."],
		["boolean", "gui.session.notifications.attack", "Chat notification attack", "Show a chat notification if you are attacked by another player."],
		["boolean", "gui.session.notifications.tribute", "Chat notification tribute", "Show a chat notification if an ally tributes resources to another team member if teams are locked, and all tributes in observer mode."],
		["boolean", "gui.session.notifications.barter", "Chat notification barter", "Show a chat notification to observers when a player bartered resources."],
		["dropdown", "gui.session.notifications.phase", "Chat notification phase", "Show a chat notification if you or an ally have started, aborted or completed a new phase, and phases of all players in observer mode.",
			[["Disable", "none"], ["Completed", "completed"], ["All displayed", "all"]]],
		["color", "gui.session.diplomacycolors.self", "Diplomacy colors: self", "Color of your units when diplomacy colors are enabled."],
		["color", "gui.session.diplomacycolors.ally", "Diplomacy colors: ally", "Color of allies when diplomacy colors are enabled."],
		["color", "gui.session.diplomacycolors.neutral", "Diplomacy colors: neutral", "Color of neutral players when diplomacy colors are enabled."],
		["color", "gui.session.diplomacycolors.enemy", "Diplomacy colors: enemy", "Color of enemies when diplomacy colors are enabled."],
		["boolean", "gui.session.attackrange", "Attack range visualization", "Display the attack range of selected defensive structures. (It can also be toggled with the hotkey during a game)."],
		["boolean", "gui.session.aurasrange", "Aura range visualization", "Display the range of auras of selected units and structures. (It can also be toggled with the hotkey during a game)."],
		["boolean", "gui.session.healrange", "Heal range visualization", "Display the healing range of selected units. (It can also be toggled with the hotkey during a game)."],
	]],
]

signal closed

## True when the page was opened over a game (open_overlay); Close then just
## removes it instead of going back to the main menu.
var overlay := false
var cfg := RtsoUserConfig.new()
var _controls := {} # key -> [type, control, extra]
var _saved := {} # key -> value at the last save / load
var _tabs: TabContainer
var _description: Label
var _confirm: HBoxContainer
var _updating := false

static func open_overlay(parent: Node) -> Control:
	var layer := CanvasLayer.new()
	layer.layer = 30
	layer.process_mode = Node.PROCESS_MODE_ALWAYS
	parent.add_child(layer)
	var page: Control = load("res://scenes/menu/options.tscn").instantiate()
	page.overlay = true
	page.process_mode = Node.PROCESS_MODE_ALWAYS
	layer.add_child(page)
	page.closed.connect(layer.queue_free)
	return page

func _ready() -> void:
	_build()
	_snapshot()
	_load_controls()

func _build() -> void:
	var bg := ColorRect.new()
	bg.color = Color(0.07, 0.08, 0.12, 0.96 if overlay else 1.0)
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(bg)
	var center := CenterContainer.new()
	center.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(center)
	var panel := PanelContainer.new()
	panel.custom_minimum_size = Vector2(860, 600)
	center.add_child(panel)
	var vb := VBoxContainer.new()
	vb.add_theme_constant_override("separation", 8)
	panel.add_child(vb)
	var head := HBoxContainer.new()
	vb.add_child(head)
	var title := Label.new()
	title.text = "Game Options"
	title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	title.add_theme_font_size_override("font_size", 22)
	title.add_theme_color_override("font_color", Color(1, 0.85, 0.4))
	head.add_child(title)
	if not overlay:
		head.add_child(_button("Hotkeys", _on_hotkeys_pressed, "BtnHotkeys"))
	_tabs = TabContainer.new()
	_tabs.size_flags_vertical = Control.SIZE_EXPAND_FILL
	vb.add_child(_tabs)
	for cat in SCHEMA:
		var scroll := ScrollContainer.new()
		scroll.name = cat[0]
		scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
		_tabs.add_child(scroll)
		var grid := GridContainer.new()
		grid.columns = 2
		grid.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		grid.add_theme_constant_override("h_separation", 16)
		grid.add_theme_constant_override("v_separation", 6)
		scroll.add_child(grid)
		for opt in cat[1]:
			_add_option(grid, opt)
	_description = Label.new()
	_description.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_description.custom_minimum_size = Vector2(0, 40)
	_description.add_theme_color_override("font_color", Color(0.85, 0.85, 0.75))
	vb.add_child(_description)
	_confirm = HBoxContainer.new()
	_confirm.visible = false
	_confirm.alignment = BoxContainer.ALIGNMENT_END
	var q := Label.new()
	q.text = "You have unsaved changes. Save them?"
	_confirm.add_child(q)
	_confirm.add_child(_button("Save", func(): _on_save(); _close()))
	_confirm.add_child(_button("Discard", func(): _on_revert(); _close()))
	_confirm.add_child(_button("Cancel", func(): _confirm.visible = false))
	vb.add_child(_confirm)
	var foot := HBoxContainer.new()
	foot.alignment = BoxContainer.ALIGNMENT_END
	vb.add_child(foot)
	foot.add_child(_button("Reset", _on_reset, "BtnReset"))
	foot.add_child(_button("Revert", _on_revert, "BtnRevert"))
	foot.add_child(_button("Save", _on_save, "BtnSave"))
	foot.add_child(_button("Close", _on_close, "BtnBack"))

func _button(text: String, cb: Callable, node_name: String = "") -> Button:
	var b := Button.new()
	b.text = text
	if node_name != "":
		b.name = node_name
	b.custom_minimum_size = Vector2(100, 34)
	b.pressed.connect(cb)
	return b

func _add_option(grid: GridContainer, opt: Array) -> void:
	var type: String = opt[0]
	var key: String = opt[1]
	var label := Label.new()
	label.text = opt[2]
	label.custom_minimum_size = Vector2(320, 0)
	label.mouse_filter = Control.MOUSE_FILTER_PASS
	grid.add_child(label)
	var extra = opt[4] if opt.size() > 4 else null
	var c: Control
	match type:
		"boolean":
			var cb := CheckBox.new()
			cb.toggled.connect(func(on): _changed(key, "true" if on else "false"))
			c = cb
		"slider":
			var row := HBoxContainer.new()
			var s := HSlider.new()
			s.min_value = extra[0]
			s.max_value = extra[1]
			s.step = 0.01 if float(extra[1]) <= 2.0 else 1.0
			s.custom_minimum_size = Vector2(240, 24)
			var v := Label.new()
			v.custom_minimum_size = Vector2(56, 0)
			row.add_child(s)
			row.add_child(v)
			s.value_changed.connect(func(x):
				v.text = _fmt(x)
				_changed(key, _fmt(x)))
			c = row
		"number":
			var sb := SpinBox.new()
			sb.min_value = extra[0]
			sb.max_value = extra[1]
			sb.step = 1
			MatchSettings.prepare_line_edit(sb.get_line_edit())
			MatchSettings.strip_engine_popups(sb)
			sb.value_changed.connect(func(x): _changed(key, str(int(x))))
			c = sb
		"dropdown":
			if extra is String and extra == "locales":
				extra = _locale_items()
			var ob = RtsoSafeSelect.new()
			ob.custom_minimum_size = Vector2(300, 30)
			for item in extra:
				ob.add_item(item[0])
			ob.item_selected.connect(func(i): _changed(key, extra[i][1]))
			c = ob
		"color":
			# options.js "color": the "R G B" value with a swatch of it.
			var row := HBoxContainer.new()
			var le := LineEdit.new()
			le.custom_minimum_size = Vector2(160, 30)
			MatchSettings.prepare_line_edit(le)
			var sw := ColorRect.new()
			sw.custom_minimum_size = Vector2(30, 30)
			row.add_child(le)
			row.add_child(sw)
			le.text_changed.connect(func(t): _color_edited(key, t, sw))
			c = row
	c.name = key.replace(".", "_")
	label.mouse_entered.connect(func(): _description.text = opt[3])
	c.mouse_entered.connect(func(): _description.text = opt[3])
	grid.add_child(c)
	_controls[key] = [type, c, extra]

## locale: "" follows the system language, then one entry per translation,
## each in its own language (0 A.D. gui/locale).
static func _locale_items() -> Array:
	var items := [["System language", ""]]
	for l in UserSettings.available_locales():
		items.append([UserSettings.LANGUAGE_NAMES.get(l, l), l])
	return items

## A colour option edited: a valid "R G B" updates the swatch and the setting.
func _color_edited(key: String, t: String, sw: ColorRect) -> void:
	var col = _parse_color(t)
	if col == null:
		return
	sw.color = col
	_changed(key, t.strip_edges())

## guiToRgbColor: "R G B" with each 0..255, else null.
static func _parse_color(t: String):
	var parts := t.strip_edges().split(" ", false)
	if parts.size() != 3:
		return null
	var rgb := []
	for p in parts:
		if not p.is_valid_int() or int(p) < 0 or int(p) > 255:
			return null
		rgb.append(int(p) / 255.0)
	return Color(rgb[0], rgb[1], rgb[2])

static func _fmt(x: float) -> String:
	return str(int(x)) if absf(x - roundf(x)) < 0.001 else str(snappedf(x, 0.01))

## Shows every control at its current config value.
func _load_controls() -> void:
	_updating = true
	for key in _controls:
		var type: String = _controls[key][0]
		var c: Control = _controls[key][1]
		var extra = _controls[key][2]
		var v := cfg.get_value(key)
		match type:
			"boolean": c.button_pressed = v == "true"
			"slider":
				c.get_child(0).value = float(v)
				c.get_child(1).text = _fmt(float(v))
			"number": c.value = float(v)
			"color":
				c.get_child(0).text = v
				var col = _parse_color(v)
				c.get_child(1).color = col if col != null else Color.WHITE
			"dropdown":
				var idx := 0
				for i in extra.size():
					if extra[i][1] == v or (v.is_valid_float() and str(extra[i][1]).is_valid_float() and is_equal_approx(float(extra[i][1]), float(v))):
						idx = i
				c.select(idx)
	_updating = false

func _snapshot() -> void:
	_saved.clear()
	for key in cfg.get_keys():
		_saved[key] = cfg.get_value(key)

func has_unsaved_changes() -> bool:
	for key in _saved:
		if cfg.get_value(key) != _saved[key]:
			return true
	return false

func _changed(key: String, value: String) -> void:
	if not _updating:
		cfg.set_value(key, value)

func _on_save() -> void:
	if not cfg.save():
		_description.text = "Could not write user://config/user.cfg."
		return
	_snapshot()
	_description.text = "Options saved."

## options.js "Revert": back to the values last saved.
func _on_revert() -> void:
	for key in _saved:
		cfg.set_value(key, _saved[key])
	_load_controls()

## options.js "Reset": 0 A.D.'s defaults (not saved until Save).
func _on_reset() -> void:
	cfg.reset_all()
	_load_controls()

func _on_close() -> void:
	if has_unsaved_changes():
		_confirm.visible = true
		return
	_close()

func _close() -> void:
	closed.emit()
	if not overlay:
		MatchSettings.go("res://scenes/menu/main_menu.tscn")

func _on_hotkeys_pressed() -> void:
	if has_unsaved_changes():
		_on_save()
	MatchSettings.go("res://scenes/menu/hotkeys.tscn")

func _unhandled_input(event: InputEvent) -> void:
	if overlay and event is InputEventKey and event.pressed and not event.echo and event.keycode == KEY_ESCAPE:
		_on_close()
		get_viewport().set_input_as_handled()
