extends Control
## Options page, port of 0 A.D. gui/options (options.js and options.json,
## GPL-2.0-or-later): one tab per category, each option a checkbox, slider,
## number or dropdown bound to a 0 A.D. config key. Changes apply
## at once (ConfigDB_CreateValue); Save writes them to user://config/user.cfg,
## Revert goes back to the saved values, Reset erases the saved settings for
## 0 A.D.'s defaults, and Close with unsaved changes asks first, both in the
## mod's message box (scenes/common/msgbox.gd). Only the options RTS0 implements are
## listed; scenes/menu/user_settings.gd and RtsoMain apply them. Hotkeys opens
## the hotkey editor (Session UI thread). From a game the page opens over it.

## [category, [[type, key, label, tooltip, extra], ...]]; extra is
## [min, max] for slider and number, [[label, value], ...] for dropdowns.
const MSGBOX := preload("res://scenes/common/msgbox.gd")
const TABS := preload("res://scenes/common/tab_buttons.gd")
const MODERN := preload("res://scenes/common/modern_theme.gd")
# options.json tooltip of each SCHEMA category, in order.
const TAB_TIPS := ["Change general options.", "Set the balance between performance and visual appearance.",
	"Adjust advanced graphic options.", "Make changes to the sound options.",
	"Change options relevant on the game setup page.", "Change multiplayer options.",
	"Configure the in-game chat.", "Customize the in-game display.", "Change options affecting the gameplay."]

const SCHEMA := [
	["General", [
		["string", "playername.singleplayer", "Player name (single-player)", "How you want to be addressed in single-player matches."],
		["string", "playername.multiplayer", "Player name (multiplayer)", "How you want to be addressed in multiplayer matches (except lobby)."],
		["dropdown", "locale", "Language", "Choose the language of the game.", "locales"],
		["boolean", "pauseonfocusloss", "Background pause", "Pause single-player games when window loses focus."],
		["boolean", "gui.splashscreen.enable", "Enable welcome screen", "If you disable it, the welcome screen will still appear, each time a new version is available. You can always launch it from the main menu."],
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
		["boolean", "silhouettes", "Unit silhouettes", "Show outlines of units behind structures."],
		["boolean", "particles", "Particles", "Enable particles."],
		["boolean", "vsync", "VSync", "Run vertical sync to fix screen tearing."],
		["slider", "adaptivefps.menu", "FPS throttling in menus", "To save CPU workload, throttle render frequency in all menus. Set to maximum to disable throttling.", [20, 360]],
		["slider", "adaptivefps.session", "FPS throttling in games", "To save CPU workload, throttle render frequency in running games. Set to maximum to disable throttling.", [20, 360]],
		["dropdown", "gui.scale", "GUI scale", "GUI scale",
			[["75%", "0.75"], ["100%", "1.0"], ["125%", "1.25"], ["150%", "1.5"], ["175%", "1.75"], ["200%", "2.0"], ["225%", "2.25"], ["250%", "2.5"]]],
		["number", "gui.session.dragdelta", "Mouse drag", "Number of pixels the mouse can move before the action is considered a drag.", [1, 200]],
		["boolean", "view.drag.inverted", "Mouse pan inverted", "Invert middle-click drag pan direction."],
		["slider", "view.drag.speed", "Mouse pan speed", "Speed mouse pans the map when holding middle-click and dragging.", [0.01, 0.6]],
		["boolean", "window.mousegrabinfullscreen", "Mouse grab in fullscreen", "Constrain mouse in the fullscreen mode to the window boundaries. It's used to avoid mouse going out of a display in case of multiple displays."],
		["boolean", "window.mousegrabinwindowmode", "Mouse grab in window mode", "Constrain mouse in the window mode to the window boundaries."],
	]],
	["Graphics (advanced)", [
		["dropdown", "antialiasing", "Antialiasing", "Reduce aliasing effect on edges.",
			[["Disabled", "disabled"], ["FXAA", "fxaa"], ["MSAA (2×)", "msaa2"], ["MSAA (4×)", "msaa4"], ["MSAA (8×)", "msaa8"]]],
		["dropdown", "sharpening", "Sharpening", "Reduce blurry effects.",
			[["Disabled", "disabled"], ["FidelityFX CAS", "cas"]]],
		["slider", "sharpness", "Sharpness factor", "The sharpness of the chosen pass.", [0, 1]],
		["dropdown", "variant_diversity", "Model appearance randomization", "Randomize the appearance of entities. Disabling gives a small performance improvement.",
			[["None", "none"], ["Limited", "limited"], ["Normal", "full"]]],
		["dropdown", "shadowquality", "Quality", "Shadow map resolution. High values can crash the game when using a graphics card with low memory!",
			[["Low", "-1"], ["Medium", "0"], ["High", "1"], ["Very High", "2"]]],
		["boolean", "shadowpcf", "Filtering", "Smooth shadows."],
		["boolean", "shadowscovermap", "Cover whole map", "When ON shadows cover the whole map and shadows cutoff distance is ignored. Useful for making screenshots of a whole map."],
		["slider", "shadowscutoffdistance", "Cutoff distance", "Hides shadows beyond a certain distance from a camera.", [100, 1500]],
		["boolean", "watereffects", "Water effects", "When OFF, use the lowest settings possible to render water. This makes other settings irrelevant."],
		["boolean", "waterfancyeffects", "High-quality water effects", "Use higher-quality effects for water, rendering coastal waves, shore foam, and ships trails."],
		["boolean", "waterreflection", "Water reflections", "Allow water to reflect a mirror image."],
		["boolean", "waterrefraction", "Water refraction", "Use a real water refraction map and not transparency."],
		["boolean", "waterrealdepth", "Real water depth", "Use actual water depth in rendering calculations."],
		["dropdown", "textures.quality", "Texture quality", "Decrease texture quality making them blurrier but increases game performance.",
			[["Low", "0"], ["Medium", "1"], ["High", "2"]]],
		["dropdown", "textures.maxanisotropy", "Texture anisotropic filter", "Makes textures look better, especially terrain. If the anisotropic filter value is unsupported it will be set to the max supported value.",
			[["1x", "1"], ["2x", "2"], ["4x", "4"], ["8x", "8"], ["16x", "16"]]],
	]],
	["Sound", [
		["slider", "sound.mastergain", "Master volume", "Master audio gain.", [0, 2]],
		["slider", "sound.musicgain", "Music volume", "In game music gain.", [0, 2]],
		["slider", "sound.ambientgain", "Ambient volume", "In game ambient sound gain.", [0, 2]],
		["slider", "sound.actiongain", "Action volume", "In game unit action sound gain.", [0, 2]],
		["slider", "sound.uigain", "UI volume", "UI sound gain.", [0, 2]],
		["boolean", "sound.notify.nick", "Nick notification", "Receive audio notification when someone types your nick."],
		["boolean", "sound.notify.gamesetup.join", "New player notification in game setup", "Receive audio notification when a new client joins the game setup."],
	]],
	["Game Setup", [
		["boolean", "gui.gamesetup.enabletips", "Enable game setting tips", "Show tips when setting up a game."],
		["boolean", "persistmatchsettings", "Persist match settings", "Save and restore match settings for quick reuse when hosting another game."],
		["dropdown", "gui.gamesetup.aidifficulty", "Default AI difficulty", "Default difficulty of the AI.",
			[["Sandbox", "0"], ["Very Easy", "1"], ["Easy", "2"], ["Medium", "3"], ["Hard", "4"], ["Very Hard", "5"]]],
		["dropdown", "gui.gamesetup.aibehavior", "Default AI behavior", "Default behavior of the AI.",
			[["Random", "random"], ["Balanced", "balanced"], ["Aggressive", "aggressive"], ["Defensive", "defensive"]]],
		["dropdown", "gui.gamesetup.assignplayers", "Assign players", "Automatically assign joining clients to free player slots during the match setup.",
			[["Everyone", "everyone"], ["Buddies", "buddies"], ["Disabled", "disabled"]]],
	]],
	["Networking / Lobby", [
		["number", "lobby.history", "Chat backlog", "Number of backlogged messages to load when joining the lobby.", [0, 1000]],
		["boolean", "lobby.columns.gamerating", "Game rating column", "Show the average rating of the participating players in a column of the gamelist."],
		["boolean", "overlay.netwarnings", "Network warnings", "Show which player has a bad connection in multiplayer games."],
		["dropdown", "network.lateobservers", "Late observer joins", "Allow everybody or buddies only to join the game as observer after it started.",
			[["Everyone", "everyone"], ["Buddies", "buddies"], ["Disabled", "disabled"]]],
		["number", "network.observerlimit", "Observer limit", "Prevent further observers from joining if the limit is reached.", [0, 32]],
		["number", "network.observermaxlag", "Max lag for observers", "When hosting, pause the game if observers are lagging more than this many turns. If set to -1, observers are ignored.", [-1, 10000]],
		["boolean", "network.autocatchup", "(Observer) Speed up when lagging.", "When observing a game, automatically speed up if you start lagging, to catch up with the live match."],
	]],
	["Chat & Notifications", [
		["boolean", "chat.timestamp", "Chat timestamp", "Display the time when a chat message was posted."],
		["boolean", "gui.session.notifications.attack", "Chat notification attack", "Show a chat notification if you are attacked by another player."],
		["boolean", "gui.session.notifications.tribute", "Chat notification tribute", "Show a chat notification if an ally tributes resources to another team member if teams are locked, and all tributes in observer mode."],
		["boolean", "gui.session.notifications.barter", "Chat notification barter", "Show a chat notification to observers when a player bartered resources."],
		["dropdown", "gui.session.notifications.flare", "Chat notification flare", "Show a chat notification when a player sends a flare.",
			[["Never", "never"], ["Only as observer", "observer"], ["Always", "always"]]],
		["dropdown", "gui.session.notifications.phase", "Chat notification phase", "Show a chat notification if you or an ally have started, aborted or completed a new phase, and phases of all players in observer mode.",
			[["Disable", "none"], ["Completed", "completed"], ["All displayed", "all"]]],
	]],
	["Display", [
		["dropdown", "gui.session.howtoshownames", "Naming of entities", "How to show entity names.",
			[["Specific names first", "0"], ["Generic names first", "1"], ["Only specific names", "2"], ["Only generic names", "3"]]],
		["boolean", "gui.session.rankabovestatusbar", "Rank icon above status bar", "Show rank icons above status bars."],
		["boolean", "gui.session.experiencestatusbar", "Experience status bar", "Show an experience status bar above each selected unit."],
		["dropdown", "gui.session.respoptooltipsort", "Sort resources and population tooltip", "Dynamically sort players in the resources and population tooltip by value.",
			[["Unordered", "0"], ["Ascending", "-1"], ["Descending", "1"]]],
		["boolean", "overlay.fps", "FPS overlay", "Show frames per second in top right corner."],
		["boolean", "overlay.realtime", "Real time overlay", "Show current system time in top right corner."],
		["boolean", "gui.session.timeelapsedcounter", "Game time overlay", "Show current simulation time in top right corner."],
		["boolean", "gui.session.ceasefirecounter", "Ceasefire time overlay", "Always show the remaining ceasefire time."],
		["boolean", "showdetailedtooltips", "Detailed tooltips", "Show detailed tooltips for trainable units in structures."],
		["boolean", "gui.session.minimap.icons.enabled", "Minimap icons", "Show special icons for some entities on the minimap."],
		["slider", "gui.session.flarelifetime", "Flare display duration", "How long the flare markers on the minimap are displayed in seconds.", [0, 60]],
		["boolean", "gui.session.attackrange", "Attack range visualization", "Display the attack range of selected defensive structures. (It can also be toggled with the hotkey during a game)."],
		["boolean", "gui.session.aurasrange", "Aura range visualization", "Display the range of auras of selected units and structures. (It can also be toggled with the hotkey during a game)."],
		["boolean", "gui.session.healrange", "Heal range visualization", "Display the healing range of selected units. (It can also be toggled with the hotkey during a game)."],
		["color", "gui.session.diplomacycolors.self", "Diplomacy colors: self", "Color of your units when diplomacy colors are enabled."],
		["color", "gui.session.diplomacycolors.ally", "Diplomacy colors: ally", "Color of allies when diplomacy colors are enabled."],
		["color", "gui.session.diplomacycolors.neutral", "Diplomacy colors: neutral", "Color of neutral players when diplomacy colors are enabled."],
		["color", "gui.session.diplomacycolors.enemy", "Diplomacy colors: enemy", "Color of enemies when diplomacy colors are enabled."],
	]],
	["Gameplay", [
		["slider", "gui.session.woundedunithotkeythreshold", "Wounded unit health", "The wounded unit hotkey considers the selected units as wounded if their health percentage falls below this number.", [0, 100]],
		["number", "gui.session.batchtrainingsize", "Batch training size", "Number of units trained per batch by default.", [1, 20]],
		["slider", "gui.session.scrollbatchratio", "Scroll batch increment ratio", "Number of times you have to scroll to increase/decrease the batchsize by 1.", [0.1, 30]],
		["dropdown", "gui.session.snaptoedges", "Snap to edges", "This option allows to align new structures with nearby structures.",
			[["Hotkey to enable snapping", "disabled"], ["Hotkey to disable snapping", "enabled"]]],
		["dropdown", "gui.session.disjointcontrolgroups", "Control group membership", "Decide whether units can be part of multiple control groups.",
			[["Single", "true"], ["Multiple", "false"]]],
		["dropdown", "gui.session.formationwalkonly", "Formation control", "Decide whether formations are enabled for all orders or only 'Walk' and 'Patrol'.",
			[["Walk/Patrol Only", "true"], ["No override", "false"]]],
		["boolean", "gui.session.selectformationasone", "Battalion-style formations", "Whether formations are selected as a whole."],
	]],
]

## options.json "dependencies": a control is disabled unless every condition holds.
## A key alone means that boolean is on; [key, op, value] compares the value.
const DEPENDENCIES := {
	"renderer.scale": ["postproc"],
	"renderer.upscale.technique": ["postproc", ["renderer.scale", "<", 1.0]],
	"antialiasing": ["postproc"],
	"sharpening": ["postproc"],
	"sharpness": ["postproc", ["sharpening", "!=", "disabled"]],
	"shadowquality": ["shadows"],
	"shadowpcf": ["shadows"],
	"shadowscovermap": ["shadows"],
	"shadowscutoffdistance": ["shadows", ["shadowscovermap", "==", "false"]],
	"waterfancyeffects": ["watereffects"],
	"waterreflection": ["watereffects"],
	"waterrefraction": ["watereffects"],
	"waterrealdepth": ["watereffects", "waterrefraction"],
}

signal closed

## True when the page was opened over a game (open_overlay); Close then just
## removes it instead of going back to the main menu.
## options.json "timeout": a dropdownNumber change that asks to be kept, else reverts.
const TIMEOUTS := {"gui.scale": 5000}

## options.json list items' tooltips, by option and value (shown with the chosen item).
const ITEM_TIPS := {
	"renderer.upscale.technique": {
		"fsr": "Advanced upscale technique. For better results, use FSR with antialiasing enabled.",
		"bilinear": "Bilinear upscale technique. Produces a slightly blurry picture depending on the scale.",
		"pixelated": "Simplest upscale technique. Used mostly for stylized effect.",
	},
	"antialiasing": {
		"disabled": "Do not use antialiasing.",
		"fxaa": "Fast, but simple antialiasing.",
		"msaa2": "Slow, but high-quality antialiasing, uses two samples per pixel. Supported for GL3.3+.",
		"msaa4": "Slow, but high-quality antialiasing, uses four samples per pixel. Supported for GL3.3+.",
		"msaa8": "Slow, but high-quality antialiasing, uses eight samples per pixel. Supported for GL3.3+.",
		"msaa16": "Slow, but high-quality antialiasing, uses sixteen samples per pixel. Supported for GL3.3+.",
	},
	"sharpening": {
		"disabled": "Do not use sharpening.",
		"cas": "Contrast adaptive sharpening, a fast, contrast based sharpening pass.",
	},
	"variant_diversity": {
		"none": "Entities will all look the same.",
		"limited": "Entities will be less diverse.",
		"full": "Entities appearance is randomized normally.",
	},
	"gui.gamesetup.assignplayers": {
		"everyone": "Players joining the match will be assigned if there is a free slot.",
		"buddies": "Players joining the match will only be assigned if they are a buddy of the host and if there is a free slot.",
		"disabled": "Players only receive a slot when the host assigns them explicitly.",
	},
	"gui.session.notifications.flare": {
		"never": "Disable flare chat notifications.",
		"observer": "Only show flare chat notifications when you are observer.",
		"always": "Always show flare chat notifications – as player and as observer.",
	},
	"gui.session.howtoshownames": {
		"0": "Display specific names before generic names.",
		"1": "Display generic names before specific names.",
		"2": "Display only specific names for entities.",
		"3": "Display only generic names for entities.",
	},
	"gui.session.snaptoedges": {
		"disabled": "New structures are aligned with nearby structures while pressing the hotkey.",
		"enabled": "New structures are aligned with nearby structures unless the hotkey is pressed.",
	},
	"gui.session.disjointcontrolgroups": {
		"true": "When adding a Unit or Structure to a control group, they are removed from other control groups. Use this choice if you want control groups to refer to distinct armies.",
		"false": "Units and Structures can be part of multiple control groups. This is useful to keep control groups for distinct armies and a control group for the entire army simultaneously.",
	},
	"gui.session.formationwalkonly": {
		"true": "Other orders will disband existing formations.",
		"false": "Units in formations stay in formations.",
	},
}

var overlay := false
var cfg := RtsoUserConfig.new()
var _controls := {} # key -> [type, control, extra]
var _labels := {} # key -> its Label
var _saved := {} # key -> value at the last save / load
var _tabs: TabContainer
var _tab_buttons: VBoxContainer # tab_buttons.xml, vertical
var _hotkeys := RtsoHotkeyConfig.new()
var _description: Label
var _confirm: Control # the open message box, null when closed
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
	# options.xml: ModernFade over the page behind, an 800x748 ModernDialog titled
	# "Game Options", vertical tab buttons on the left, the options in a box on the
	# right and Reset, Revert, Save and Close across the bottom.
	theme = MODERN.theme()
	var bg := ColorRect.new()
	bg.color = Color(0, 0, 0, 0.6) if overlay else Color(0.07, 0.08, 0.12, 1.0)
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(bg)
	var center := CenterContainer.new()
	center.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(center)
	var panel := PanelContainer.new()
	panel.name = "Dialog"
	panel.theme_type_variation = "ModernDialogContainer"
	panel.custom_minimum_size = Vector2(800, minf(748.0, get_viewport_rect().size.y - 24.0))
	center.add_child(panel)
	var vb := VBoxContainer.new()
	vb.add_theme_constant_override("separation", 8)
	panel.add_child(vb)
	var title := Label.new()
	title.text = "Game Options"
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title.add_theme_font_size_override("font_size", 22)
	title.add_theme_color_override("font_color", Color(1, 0.85, 0.4))
	vb.add_child(title)
	var body := HBoxContainer.new()
	body.size_flags_vertical = Control.SIZE_EXPAND_FILL
	body.add_theme_constant_override("separation", 10)
	vb.add_child(body)
	_tab_buttons = VBoxContainer.new()
	_tab_buttons.name = "TabButtons"
	_tab_buttons.custom_minimum_size = Vector2(215, 0)
	_tab_buttons.add_theme_constant_override("separation", 4)
	body.add_child(_tab_buttons)
	var box := PanelContainer.new() # ModernDarkBoxGold
	box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	var sb := StyleBoxFlat.new()
	sb.bg_color = Color(0, 0, 0, 0.45)
	sb.border_color = Color(0.83, 0.68, 0.21)
	sb.set_border_width_all(1)
	sb.set_content_margin_all(8)
	box.add_theme_stylebox_override("panel", sb)
	body.add_child(box)
	_tabs = TabContainer.new()
	_tabs.tabs_visible = false
	_tabs.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_tabs.add_theme_stylebox_override("panel", StyleBoxEmpty.new())
	_tabs.tab_changed.connect(_on_tab_changed)
	box.add_child(_tabs)
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
	# tab_buttons.js placeTabButtons(g_Options, false, ...): vertical, options.json tooltips.
	var keys: Dictionary = RtsoHotkeyConfig.new().get_hotkey_map()
	var group := ButtonGroup.new()
	for i in SCHEMA.size():
		var b := Button.new()
		b.text = tr(SCHEMA[i][0])
		b.toggle_mode = true
		b.button_group = group
		b.theme_type_variation = "ModernTabButtonVertical"
		b.custom_minimum_size = Vector2(215, 30)
		b.tooltip_text = TABS.tooltip(tr(TAB_TIPS[i]) if i < TAB_TIPS.size() else "", false, keys)
		_tabs.set_tab_tooltip(i, b.tooltip_text)
		b.pressed.connect(func(): _tabs.current_tab = i)
		b.gui_input.connect(func(ev: InputEvent):
			var d: int = TABS.wheel(ev)
			if d != 0:
				_tabs.current_tab = TABS.next_index(_tabs.current_tab, d, _tabs.get_tab_count())
				b.accept_event())
		_tab_buttons.add_child(b)
	_on_tab_changed(_tabs.current_tab)
	_description = Label.new()
	_description.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_description.custom_minimum_size = Vector2(0, 40)
	_description.add_theme_color_override("font_color", Color(0.85, 0.85, 0.75))
	vb.add_child(_description)
	var foot := HBoxContainer.new()
	foot.alignment = BoxContainer.ALIGNMENT_CENTER
	foot.add_theme_constant_override("separation", 10)
	vb.add_child(foot)
	for spec in [["Reset", _on_reset, "BtnReset", "Resets user settings to their game default"],
			["Revert", _on_revert, "BtnRevert", "Reverts to previous saved settings"],
			["Save", _on_save, "BtnSave", "Saves changes"],
			["Close", _on_close, "BtnBack", "Unsaved changes affect this session only"]]:
		var btn := _button(spec[0], spec[1], spec[2])
		btn.custom_minimum_size = Vector2(162, 28)
		btn.tooltip_text = spec[3]
		foot.add_child(btn)

func _on_tab_changed(i: int) -> void:
	if _tab_buttons and i >= 0 and i < _tab_buttons.get_child_count():
		(_tab_buttons.get_child(i) as Button).set_pressed_no_signal(true)

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
	label.custom_minimum_size = Vector2(320, 0) # option_label: 65% of the 545 px box
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	label.mouse_filter = Control.MOUSE_FILTER_PASS
	_labels[key] = label
	if DEPENDENCIES.has(key):
		# g_DependentLabelIndentation: an option that depends on another sits 25 px in.
		var indent := MarginContainer.new()
		indent.add_theme_constant_override("margin_left", 25)
		label.custom_minimum_size.x -= 25
		indent.add_child(label)
		grid.add_child(indent)
	else:
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
			s.custom_minimum_size = Vector2(130, 24)
			var v := Label.new()
			v.custom_minimum_size = Vector2(50, 0)
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
			ob.custom_minimum_size = Vector2(190, 30) # option_control_dropdown: the other 35%
			ob.clip_text = true
			for item in extra:
				ob.add_item(item[0])
			ob.item_selected.connect(func(i):
				var old := cfg.get_value(key)
				_changed(key, extra[i][1])
				if TIMEOUTS.has(key) and not _updating and old != extra[i][1]:
					_ask_keep(key, old))
			c = ob
		"string":
			var le := LineEdit.new()
			le.custom_minimum_size = Vector2(190, 30)
			MatchSettings.prepare_line_edit(le)
			le.text_changed.connect(func(t): _changed(key, t.replace("\"", "").strip_edges())) # user.cfg quotes values
			c = le
		"color":
			# options.js "color": the "R G B" value with a swatch of it.
			var row := HBoxContainer.new()
			var le := LineEdit.new()
			le.custom_minimum_size = Vector2(150, 30)
			MatchSettings.prepare_line_edit(le)
			var sw := ColorRect.new()
			sw.custom_minimum_size = Vector2(30, 30)
			row.add_child(le)
			row.add_child(sw)
			le.text_changed.connect(func(t): _color_edited(key, t, sw))
			c = row
	c.name = key.replace(".", "_")
	label.mouse_entered.connect(func(): _description.text = _describe(opt))
	c.mouse_entered.connect(func(): _description.text = _describe(opt))
	grid.add_child(c)
	_controls[key] = [type, c, extra]

## options.js control tooltips: the description, then what the type adds for the current
## value (slider "Value: (min, max)", number "Min: , Max: ", colour "Default: ") or the
## chosen list item's own tooltip.
func _describe(opt: Array) -> String:
	var key: String = opt[1]
	var text: String = tr(opt[3])
	var extra = opt[4] if opt.size() > 4 else null
	match opt[0]:
		"slider":
			text += "
" + tr("Value: %(val)s (min: %(min)s, max: %(max)s)").replace("%(val)s", "%.2f" % float(cfg.get_value(key))) 				.replace("%(min)s", "%.2f" % float(extra[0])).replace("%(max)s", "%.2f" % float(extra[1]))
		"number":
			text += "
" + tr("Min: %(min)s, Max: %(max)s").replace("%(min)s", str(extra[0])).replace("%(max)s", str(extra[1]))
		"color":
			text += "
" + tr("Default: %(value)s").replace("%(value)s", cfg.get_default(key))
		"dropdown":
			var tip: String = ITEM_TIPS.get(key, {}).get(cfg.get_value(key), "")
			if tip != "":
				text += "
" + tr(tip)
	return text

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
		sw.color = Color(1, 0, 1) # g_InsaneColor: the swatch turns magenta, the value stays
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
			"string": c.text = v
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
	_apply_dependencies()

func _snapshot() -> void:
	_saved.clear()
	for key in cfg.get_keys():
		_saved[key] = cfg.get_value(key)
	_update_buttons()

func has_unsaved_changes() -> bool:
	for key in _saved:
		if cfg.get_value(key) != _saved[key]:
			return true
	return false

func _changed(key: String, value: String) -> void:
	if not _updating:
		cfg.set_value(key, value)
		_apply_dependencies()

## options.js updateOptionPanel: options whose dependencies fail are disabled.
func _apply_dependencies() -> void:
	for key in DEPENDENCIES:
		if not _controls.has(key):
			continue
		var on := true
		for d in DEPENDENCIES[key]:
			if d is String:
				on = on and cfg.get_value(d) == "true"
			else:
				var v := cfg.get_value(d[0])
				match d[1]:
					"<": on = on and float(v) < float(d[2])
					"==": on = on and v == str(d[2])
					"!=": on = on and v != str(d[2])
		var c: Control = _controls[key][1]
		_labels[key].modulate.a = 1.0 if on else 0.5 # the label greys out with its control
		match _controls[key][0]:
			"boolean": c.disabled = not on
			"dropdown": c.disabled = not on
			"slider": c.get_child(0).editable = on
			"number": c.editable = on
	_update_buttons()

## updateOptionPanel: Revert and Save only while there are unsaved changes.
func _update_buttons() -> void:
	var changes := has_unsaved_changes()
	for b in ["BtnRevert", "BtnSave"]:
		var btn := find_child(b, true, false) as Button
		if btn:
			btn.disabled = not changes

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

## options.js setDefaults: asks, then erases the saved settings (0 A.D.'s defaults) and saves.
func _on_reset() -> void:
	_ask(tr("Resetting the options will erase your saved settings. Do you want to continue?"), func():
		cfg.reset_all()
		if cfg.save():
			_snapshot()
		_load_controls())

## shouldClosePage: unsaved changes ask first; Yes closes and keeps them for this session.
func _on_close() -> void:
	if has_unsaved_changes():
		_ask(tr("You have unsaved changes, do you want to close this window?"), _close)
		return
	_close()

## messageBox(500, 200, text, "Warning", No, Yes); `yes` runs on Yes.
func _ask(text: String, yes: Callable) -> void:
	_confirm = MSGBOX.open(self, 500, 200, text, tr("Warning"), [tr("No"), tr("Yes")])
	_confirm.answered.connect(func(i: int):
		_confirm = null
		if i == 1: yes.call())

## options.js dropdownNumber timeout: timedConfirmation(500, 200, ...), No or the
## countdown running out puts the old value back (revertChange).
func _ask_keep(key: String, old: String) -> void:
	_confirm = MSGBOX.open_timed(self, 500, 200,
		tr("Changes will be reverted in %(time)s seconds. Do you want to keep changes?"), "time",
		TIMEOUTS[key], tr("Warning"), [tr("No"), tr("Yes")])
	_confirm.answered.connect(func(i: int):
		_confirm = null
		if i == 0:
			cfg.set_value(key, old)
			_load_controls())

func _close() -> void:
	closed.emit()
	if not overlay:
		MatchSettings.go("res://scenes/menu/main_menu.tscn")

func _unhandled_input(event: InputEvent) -> void:
	var d: int = TABS.direction(event, _hotkeys)
	if d != 0 and _confirm == null:
		get_viewport().set_input_as_handled()
		_tabs.current_tab = TABS.next_index(_tabs.current_tab, d, _tabs.get_tab_count())
		return
	# closeButton hotkey="cancel", over a match or opened from the main menu.
	if event is InputEventKey and event.pressed and not event.echo and event.keycode == KEY_ESCAPE and _confirm == null:
		_on_close()
		get_viewport().set_input_as_handled()
