extends Control

const L10n = preload("res://scenes/menu/l10n.gd")

# 0 A.D. Main Menu Controller
# Replicating 0 A.D.'s authentic pregame menus, parallax backdrops, stone panels, and audio.

const BACKGROUNDS = {
	"hellenes": [
		"res://assets/textures/ui/pregame/backgrounds/hellenes1-1.png",
		"res://assets/textures/ui/pregame/backgrounds/hellenes1-2.png",
		"res://assets/textures/ui/pregame/backgrounds/hellenes1-3.png"
	],
	"carthage": [
		"res://assets/textures/ui/pregame/backgrounds/carthage1_1.png",
		"res://assets/textures/ui/pregame/backgrounds/carthage1_2.png",
		"res://assets/textures/ui/pregame/backgrounds/carthage1_3.png",
		"res://assets/textures/ui/pregame/backgrounds/carthage1_4.png"
	],
	"kush": [
		"res://assets/textures/ui/pregame/backgrounds/kush1_1.png",
		"res://assets/textures/ui/pregame/backgrounds/kush1_2.png",
		"res://assets/textures/ui/pregame/backgrounds/kush1_3.png",
		"res://assets/textures/ui/pregame/backgrounds/kush1_4.png"
	],
	"seleucid": [
		"res://assets/textures/ui/pregame/backgrounds/seleucid1_1.png",
		"res://assets/textures/ui/pregame/backgrounds/seleucid1_2.png",
		"res://assets/textures/ui/pregame/backgrounds/seleucid1_3.png"
	]
}

const BG_KEYS = ["hellenes", "carthage", "kush", "seleucid"]
# pregame/backgrounds/background.js, per layer: [cosine amplitude (share of the
# 16:9 width), cosine rate (per second), constant offset (share of the width),
# tiling]; the Kushite third layer adds 0.01 * width * cos(0.04 t) and the fourth
# sits at -0.1 px.
const LAYERS = {
	"carthage": [[0.02, 0.05, 0.0, true], [0.04, 0.05, 0.0, true], [0.10, 0.05, 0.0, false], [0.18, 0.05, 0.0, false]],
	"hellenes": [[0.02, 0.05, 0.0, true], [0.12, 0.05, -0.1, false], [0.16, 0.05, 0.25, false]],
	"kush": [[0.07, 0.1, 0.0, true], [0.05, 0.1, 0.0, true], [0.04, 0.1, 0.0, true], [0.0, 0.0, 0.0, true]],
	"seleucid": [[0.05, 0.02, 0.0, true], [0.10, 0.04, 0.0, true], [0.17, 0.05, 0.125, false]],
}
const MENU_SPEED := 1200.0 # MainMenuItemHandler.js MenuSpeed: 1.2 px/ms
var current_bg_index: int = 0
var bg_layers: Array[Control] = []
var _bg_specs: Array = []
var _bg_civ := "hellenes"

var time_elapsed: float = 0.0
var _slide: Tween

@onready var parallax_container: Control = $ParallaxContainer
@onready var main_panel: Control = $MainPanel
@onready var submenu_panel: Control = $SubmenuPanel
@onready var submenu_title: Label = $SubmenuPanel/SubmenuContent/SubmenuTitle
@onready var submenu_buttons_box: VBoxContainer = $SubmenuPanel/SubmenuContent/SubmenuButtons

var active_menu_id: String = ""

# 0 A.D. 0.28 gui/pregame/MainMenuItems.js submenus ("hotkey" items name the hotkey
# that also opens them from the main menu, MainMenuItemHandler.setupHotkeys). RTS0
# adds Skirmish Arena and Asset Viewer to Single-player.
var submenu_data = {
	"learn": {
		"title": "LEARN TO PLAY",
		"items": [
			{"label": "Manual", "tooltip": "Open the 0 A.D. Game Manual.", "action": "open_manual"},
			{"label": "Tutorial", "tooltip": "Start the introductory tutorial.", "action": "start_tutorial"},
			{"label": "Tips and Tricks", "tooltip": "Discover simple tips, tricks, and game mechanics.", "action": "open_tips"},
			{"label": "Structure Tree", "tooltip": "%(hotkey)s: View the structure tree of civilizations featured in 0 A.D.", "hotkey": "structree", "action": "open_tech_tree"},
			{"label": "Civilization Overview", "tooltip": "%(hotkey)s: Learn about the civilizations featured in 0 A.D.", "hotkey": "civinfo", "action": "open_encyclopedia"},
			{"label": "Catafalque Overview", "tooltip": "Compare the bonuses of catafalques featured in 0 A.D.", "action": "open_catafalque"},
			{"label": "Map Overview", "tooltip": "View the different maps featured in 0 A.D.", "action": "open_map_browser"}
		]
	},
	"single": {
		"title": "SINGLE-PLAYER",
		"items": [
			{"label": "Matches", "tooltip": "Start a new single-player game.", "action": "open_setup"},
			{"label": "Load Game", "tooltip": "Load a saved game.", "action": "open_load"},
			{"label": "Continue Campaign", "tooltip": "Relive history through historical military campaigns.", "action": "continue_campaign", "needs_campaign": true},
			{"label": "New Campaign", "tooltip": "Relive history through historical military campaigns.", "action": "new_campaign"},
			{"label": "Load Campaign", "tooltip": "Relive history through historical military campaigns.", "action": "load_campaign"},
			{"label": "Replays", "tooltip": "Playback previous games.", "action": "open_replays_sp"},
			{"label": "Skirmish Arena", "tooltip": "Direct launch into quick tactical combat match.", "action": "open_skirmish"},
			{"label": "Asset Viewer", "tooltip": "Inspect 3D unit models, animations, and civic buildings.", "action": "open_asset_viewer"}
		]
	},
	"multi": {
		"title": "MULTIPLAYER",
		"items": [
			{"label": "Game Lobby", "tooltip": "%(hotkey)s: Launch the multiplayer lobby to join and host publicly visible games and chat with other players.", "hotkey": "lobby", "action": "open_lobby"},
			{"label": "Connect by IP", "tooltip": "Joining an existing multiplayer game at a given IP address.", "action": "open_lobby"},
			{"label": "Host New Game", "tooltip": "Host a new multiplayer game. Other players can connect directly to you via your IP address.", "action": "open_lobby"},
			{"label": "Host Saved Game", "tooltip": "Continue playing a game from a savegame.", "action": "open_lobby"},
			{"label": "Replays", "tooltip": "Playback previous games.", "action": "open_replays_mp"}
		]
	},
	"settings": {
		"title": "SETTINGS",
		"items": [
			{"label": "Options", "tooltip": "Adjust game settings.", "action": "open_options"},
			{"label": "Hotkeys", "tooltip": "Adjust hotkeys.", "action": "open_hotkeys"},
			{"label": "Language", "tooltip": "Choose the language of the game.", "action": "open_locale"},
			{"label": "Mod Selection", "tooltip": "Select and download mods for the game.", "action": "open_mods"},
			{"label": "Welcome Screen", "tooltip": "Show the Welcome Screen again. Useful if you hid it by mistake.", "action": "open_splash"}
		]
	}
}

# MainMenuItems.js captions' tooltips for the main buttons.
const MAIN_TOOLTIPS := {
	"BtnLearn": "Learn how to play, start the tutorial, discover the technology trees, and the history behind the civilizations.",
	"BtnContinue": "Relive history through historical military campaigns.",
	"BtnSingle": "Start, load, or replay a single-player game.",
	"BtnMulti": "Fight against one or more human players in a multiplayer game.",
	"BtnSettings": "Change game options.",
	"BtnEditor": "Inspect 3D unit models, animations, and civic buildings.",
	"BtnCredits": "Show the 0 A.D. credits.",
	"BtnExit": "Exit the game.",
}
# MainMenuItemHandler.setupHotkeys: the submenu items with a hotkey, by hotkey.
const MENU_HOTKEYS := {"structree": "open_tech_tree", "civinfo": "open_encyclopedia", "lobby": "open_lobby"}

var _hotkeys := RtsoHotkeyConfig.new()

func _ready() -> void:
	# menupanel.xml tooltip_style="pgToolTip": the fixed top-right panel, at once.
	main_panel.set_meta("tooltip_style", "pg")
	submenu_panel.set_meta("tooltip_style", "pg")
	# Randomize starting civ background or default to hellenes
	current_bg_index = randi() % BG_KEYS.size()
	_setup_background(BG_KEYS[current_bg_index])

	# MainMenuPage.js initMusic: the menu playlist, kept playing through the other pages.
	MenuAudio.play_menu_music()

	submenu_panel.visible = false
	move_child(submenu_panel, main_panel.get_index()) # it slides out from behind the panel
	# Back at the main menu, nothing played next belongs to a campaign until one is started.
	if has_node("/root/MatchSettings"):
		get_node("/root/MatchSettings").campaign_run = ""
		get_node("/root/MatchSettings").campaign_level = ""

	# Connect main navigation buttons
	$MainPanel/Buttons/BtnLearn.pressed.connect(func(): _toggle_submenu("learn", $MainPanel/Buttons/BtnLearn))
	$MainPanel/Buttons/BtnSingle.pressed.connect(func(): _toggle_submenu("single", $MainPanel/Buttons/BtnSingle))
	$MainPanel/Buttons/BtnMulti.pressed.connect(func(): _toggle_submenu("multi", $MainPanel/Buttons/BtnMulti))
	$MainPanel/Buttons/BtnSettings.pressed.connect(func(): _toggle_submenu("settings", $MainPanel/Buttons/BtnSettings))
	$MainPanel/Buttons/BtnEditor.pressed.connect(_on_editor_pressed)
	$MainPanel/Buttons/BtnCredits.pressed.connect(_on_credits_pressed)
	$MainPanel/Buttons/BtnExit.pressed.connect(_on_exit_pressed)
	# 0.28's top-level Continue Campaign, enabled while a campaign run is current.
	var cont: Button = $MainPanel/Buttons/BtnContinue
	cont.pressed.connect(func(): _execute_action("continue_campaign"))
	cont.disabled = CampaignRun.current_run() == null
	for n in MAIN_TOOLTIPS:
		get_node("MainPanel/Buttons/" + n).tooltip_text = tr(MAIN_TOOLTIPS[n])

	$CivSwitcher.pressed.connect(_cycle_background)

	# 0 A.D.'s -autostart command line starts the match without the menus.
	if preload("res://scenes/menu/autostart.gd").run(get_node_or_null("/root/MatchSettings")):
		return
	# 0.28 SplashScreenHandler: the welcome page over the menu shown at startup
	# (not under a -s test script).
	# With incompatible mods 0.28 starts in the Mod Selection instead (modmod.js).
	if get_tree().get_script() == null:
		if not preload("res://scenes/menu/mods.gd").incompatible.is_empty():
			load("res://scenes/menu/modmod.gd").open_incompatible(self)
		else:
			load("res://scenes/menu/splashscreen.gd").open_at_startup(self)

	# Connect hover sounds
	for btn in $MainPanel/Buttons.get_children():
		if btn is Button:
			btn.mouse_entered.connect(_play_hover)

func _setup_background(civ_name: String) -> void:
	# Clean existing layers
	for child in parallax_container.get_children():
		child.queue_free()
	bg_layers.clear()
	_bg_civ = civ_name if BACKGROUNDS.has(civ_name) else "hellenes"
	_bg_specs = LAYERS[_bg_civ]
	parallax_container.clip_contents = true
	var textures: Array = BACKGROUNDS[_bg_civ]
	for i in textures.size():
		var tex = load(textures[i])
		# BackgroundLayer: a tiling layer repeats its 2:1 image across the screen,
		# the others are one 2:1 piece placed from the centre.
		var layer := Control.new()
		layer.mouse_filter = Control.MOUSE_FILTER_IGNORE
		for k in (3 if _bg_specs[i][3] else 1):
			var rect := TextureRect.new()
			rect.texture = tex
			rect.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
			rect.stretch_mode = TextureRect.STRETCH_SCALE
			rect.mouse_filter = Control.MOUSE_FILTER_IGNORE
			layer.add_child(rect)
		parallax_container.add_child(layer)
		bg_layers.append(layer)
	_place_layers()

	var civ_lbl = get_node_or_null("CivSwitcher/CivLabel")
	if civ_lbl:
		civ_lbl.text = "CIVILIZATION: " + civ_name.to_upper()

## BackgroundLayer.update: each layer's offset at the time since the menu opened.
func _layer_offset(i: int, t: float, width: float) -> float:
	var spec: Array = _bg_specs[i]
	if _bg_civ == "kush" and i == 3:
		return -0.1
	var off: float = spec[0] * width * cos(spec[1] * t) + spec[2] * width
	if _bg_civ == "kush" and i == 2:
		off += 0.01 * width * cos(0.04 * t)
	return off

func _place_layers() -> void:
	var vp := get_viewport_rect().size
	var height := vp.y
	var width := height * 16.0 / 9.0 # BackgroundLayer.AspectRatio
	for i in bg_layers.size():
		var layer := bg_layers[i]
		if not is_instance_valid(layer):
			continue
		var off := _layer_offset(i, time_elapsed, width)
		layer.position = Vector2.ZERO
		layer.size = vp
		var pieces := layer.get_children()
		if _bg_specs[i][3]:
			var iw := height * 2.0
			var left := fmod(off, iw)
			if left >= 0.0:
				left -= iw
			for k in pieces.size():
				pieces[k].position = Vector2(left + k * iw, 0)
				pieces[k].size = Vector2(iw, height)
			# enough copies to reach the right edge
			while left + pieces.size() * iw < vp.x:
				var extra: TextureRect = pieces[0].duplicate()
				layer.add_child(extra)
				pieces = layer.get_children()
				extra.position = Vector2(left + (pieces.size() - 1) * iw, 0)
				extra.size = Vector2(iw, height)
		else:
			var right := vp.x / 2.0 + off
			pieces[0].position = Vector2(right - height, 0)
			pieces[0].size = Vector2(2.0 * height, height)

func _cycle_background() -> void:
	current_bg_index = (current_bg_index + 1) % BG_KEYS.size()
	_setup_background(BG_KEYS[current_bg_index])

func _process(delta: float) -> void:
	time_elapsed += delta
	if get_viewport_rect().size.x > 0:
		_place_layers()

func _input(event: InputEvent) -> void:
	if event is InputEventMouseButton and event.pressed and event.button_index == MOUSE_BUTTON_LEFT:
		# If clicked outside main panel and submenu, close submenu
		if submenu_panel.visible:
			var main_rect = main_panel.get_global_rect()
			var sub_rect = submenu_panel.get_global_rect()
			if not main_rect.has_point(event.position) and not sub_rect.has_point(event.position):
				_close_submenu()

func _toggle_submenu(menu_id: String, anchor_button: Button) -> void:
	if active_menu_id == menu_id and submenu_panel.visible:
		_close_submenu()
		return

	active_menu_id = menu_id
	var data = submenu_data.get(menu_id, null)
	if not data:
		_close_submenu()
		return

	submenu_title.text = data["title"]
	submenu_title.visible = false # 0.28's submenu has no heading

	# Populate items
	for c in submenu_buttons_box.get_children():
		c.queue_free()

	for item in data["items"]:
		if item.get("needs_campaign", false) and CampaignRun.current_run() == null:
			continue
		var btn = Button.new()
		btn.text = item["label"]
		btn.tooltip_text = _item_tooltip(item)
		btn.custom_minimum_size = Vector2(220, 34)
		btn.alignment = HORIZONTAL_ALIGNMENT_CENTER
		var action = item["action"]
		btn.pressed.connect(func(): _execute_action(action))
		btn.mouse_entered.connect(_play_hover)
		submenu_buttons_box.add_child(btn)

	# Position adjacent to the anchor button
	var btn_global_pos = anchor_button.global_position
	var main_global_rect = main_panel.get_global_rect()
	var target_x: float = main_global_rect.position.x + main_global_rect.size.x + 4
	submenu_panel.global_position.y = clamp(btn_global_pos.y - 12, 10, get_viewport_rect().size.y - submenu_panel.size.y - 10)
	# MainMenuItemHandler.onTick: it slides out from behind the panel at MenuSpeed.
	var was_open: bool = submenu_panel.visible
	submenu_panel.visible = true
	if _slide:
		_slide.kill()
	if was_open:
		submenu_panel.global_position.x = target_x
	else:
		submenu_panel.global_position.x = target_x - submenu_panel.size.x
		_slide = create_tween()
		_slide.tween_property(submenu_panel, "global_position:x", target_x, submenu_panel.size.x / MENU_SPEED)

func _close_submenu() -> void:
	active_menu_id = ""
	if _slide:
		_slide.kill()
	submenu_panel.visible = false

## colorizeHotkey: "%(hotkey)s" becomes the item's hotkey keys.
func _item_tooltip(item: Dictionary) -> String:
	var tip := tr(str(item.get("tooltip", "")))
	if not item.has("hotkey"):
		return tip
	var keys: Array = _hotkeys.get_hotkey_map().get(item["hotkey"], [])
	return L10n.sprintf(tip, {"hotkey": ", ".join(PackedStringArray(keys)) if not keys.is_empty() else L10n.t("Unassigned")})

func _unhandled_input(event: InputEvent) -> void:
	if not (event is InputEventKey and event.pressed and not event.echo):
		return
	for hk in _hotkeys.fired(event):
		if hk == "cancel" and submenu_panel.visible: # menupanel.xml closeMenuButton
			get_viewport().set_input_as_handled()
			_close_submenu()
			return
		if MENU_HOTKEYS.has(hk):
			get_viewport().set_input_as_handled()
			_execute_action(MENU_HOTKEYS[hk])
			return

func _execute_action(action: String) -> void:
	match action:
		"open_setup":
			_go_to_scene("res://scenes/menu/gamesetup.tscn")
		"open_skirmish":
			_go_to_scene("res://scenes/main.tscn")
		"open_encyclopedia":
			_go_to_scene("res://scenes/menu/encyclopedia.tscn")
		"open_tech_tree":
			load("res://scenes/menu/encyclopedia.gd").open_structree = true
			_go_to_scene("res://scenes/menu/encyclopedia.tscn")
		"open_manual":
			_close_submenu()
			load("res://scenes/reference/manual.gd").open(self)
		"open_tips":
			_close_submenu()
			load("res://scenes/reference/tips_page.gd").open(self)
		"start_tutorial":
			# MainMenuItems.js Tutorial: autostart maps/tutorials/introductory_tutorial
			# as a scenario with cheats on.
			var ms := get_node_or_null("/root/MatchSettings")
			var start = load("res://scenes/menu/scenario_start.gd")
			if ms and start.configure(ms, start.INTRODUCTORY_TUTORIAL):
				ms.go("res://scenes/main.tscn")
		"open_map_browser":
			# MainMenuItems.js Map Overview: page_mapbrowser, Skirmish maps, Default filter, to look only.
			_close_submenu()
			var setup = load("res://scenes/menu/gamesetup.gd")
			load("res://scenes/menu/map_browser.gd").open_overlay(self, setup.load_all_maps(), "skirmish", "default", "", false)
		"open_catafalque":
			_close_submenu()
			load("res://scenes/reference/catafalque_page.gd").open(self)
		"open_load":
			_close_submenu()
			load("res://scenes/session/savegame_dialog.gd").open(self, "load")
		"open_replays":
			_go_to_scene("res://scenes/menu/replay_browser.tscn")
		"open_lobby":
			_go_to_scene("res://scenes/menu/lobby.tscn")
		"open_options": # MainMenuItems.js: Engine.PushGuiPage("page_options.xml") over the menu
			_close_submenu()
			load("res://scenes/menu/options.gd").open_overlay(self)
		"open_locale":
			_close_submenu()
			load("res://scenes/menu/locale_page.gd").open(self)
		"open_mods":
			_close_submenu()
			load("res://scenes/menu/modmod.gd").open(self)
		"open_splash":
			_close_submenu()
			load("res://scenes/menu/splashscreen.gd").open(self)
		"open_asset_viewer":
			_go_to_scene("res://scenes/tools/asset_viewer.tscn")
		"open_replays_sp", "open_replays_mp":
			# MainMenuItems.js Replays: the replay menu filtered to the submenu's kind of game.
			load("res://scenes/menu/replay_browser.gd").selection_data = {"filters": {"singleplayer": "Single-player" if action == "open_replays_sp" else "Multiplayer"}}
			_go_to_scene("res://scenes/menu/replay_browser.tscn")
		"open_hotkeys":
			_close_submenu()
			load("res://scenes/menu/hotkeys.gd").open_overlay(self)
		"continue_campaign", "new_campaign", "load_campaign":
			load("res://scenes/menu/campaigns.gd").open_mode = {"continue_campaign": "menu", "new_campaign": "setup", "load_campaign": "load"}[action]
			_go_to_scene("res://scenes/menu/campaigns.tscn")

func _on_editor_pressed() -> void:
	_go_to_scene("res://scenes/tools/asset_viewer.tscn")

# 0 A.D. pushes page_credits.xml over the main menu.
func _on_credits_pressed() -> void:
	_close_submenu()
	load("res://scenes/reference/credits_page.gd").open(self)

# MainMenuItems.js Exit: "Are you sure you want to quit 0 A.D.?" No / Yes.
func _on_exit_pressed() -> void:
	_close_submenu()
	var box = load("res://scenes/common/msgbox.gd").open(self, 400, 200, tr("Are you sure you want to quit 0 A.D.?"), tr("Confirmation"), [tr("No"), tr("Yes")])
	box.answered.connect(func(i):
		if i == 1:
			get_tree().quit())

func _play_hover() -> void:
	# Subtle hover feedback
	pass

func _go_to_scene(path: String) -> void:
	if has_node("/root/MatchSettings"):
		get_node("/root/MatchSettings").go(path)
	else:
		get_tree().change_scene_to_file(path)

