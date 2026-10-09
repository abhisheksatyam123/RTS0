extends Control

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
var current_bg_index: int = 0
var bg_layers: Array[TextureRect] = []

var time_elapsed: float = 0.0
var mouse_pos_norm: Vector2 = Vector2.ZERO

@onready var parallax_container: Control = $ParallaxContainer
@onready var main_panel: Control = $MainPanel
@onready var submenu_panel: Control = $SubmenuPanel
@onready var submenu_title: Label = $SubmenuPanel/SubmenuContent/SubmenuTitle
@onready var submenu_buttons_box: VBoxContainer = $SubmenuPanel/SubmenuContent/SubmenuButtons
@onready var click_sfx: AudioStreamPlayer = $ClickSFX
@onready var music_player: AudioStreamPlayer = $MusicPlayer
@onready var credits_modal: Control = $CreditsModal

var active_menu_id: String = ""

# Canonical 0 A.D. submenus
var submenu_data = {
	"learn": {
		"title": "LEARN TO PLAY",
		"items": [
			{"label": "Civilization Overview", "tooltip": "Civilization bonuses, specific technologies, structures and heroes, and history.", "action": "open_encyclopedia"},
			{"label": "Structure Tree", "tooltip": "Every structure by phase, with the units it trains and the technologies it researches.", "action": "open_tech_tree"},
			{"label": "Tips & Tactics", "tooltip": "Review micro, economy balancing, and hotkey tricks.", "action": "open_tips"},
			{"label": "Map Overview", "tooltip": "Preview maps, biomes, and strategic chokepoints.", "action": "open_setup"}
		]
	},
	"single": {
		"title": "SINGLE-PLAYER",
		"items": [
			{"label": "Custom Matches", "tooltip": "Start a custom skirmish with RMS biomes, AI, or 0 A.D. maps.", "action": "open_setup"},
			{"label": "Skirmish Arena", "tooltip": "Direct launch into quick tactical combat match.", "action": "open_skirmish"},
			{"label": "Load Game", "tooltip": "Continue a saved game.", "action": "open_load"},
			{"label": "Replays", "tooltip": "Watch the replay of any match you played.", "action": "open_replays"},
			{"label": "Asset Viewer", "tooltip": "Inspect 3D unit models, animations, and civic buildings.", "action": "open_asset_viewer"}
		]
	},
	"multi": {
		"title": "MULTIPLAYER",
		"items": [
			{"label": "Game Lobby", "tooltip": "Connect to local LAN or Internet multiplayer lobby.", "action": "open_lobby"},
			{"label": "Direct Connect IP", "tooltip": "Join match directly using host IP address.", "action": "open_lobby"},
			{"label": "Host Dedicated / Match", "tooltip": "Host an authoritative lockstep match.", "action": "open_lobby"}
		]
	},
	"settings": {
		"title": "SETTINGS",
		"items": [
			{"label": "Game Options", "tooltip": "Audio, graphics, display mode, camera speeds.", "action": "open_options"},
			{"label": "Hotkeys & Controls", "tooltip": "Standard 0 A.D. hotkeys (WASD, MMB pan, H stop, Ctrl+RMB).", "action": "open_options"}
		]
	}
}

func _ready() -> void:
	# Randomize starting civ background or default to hellenes
	current_bg_index = randi() % BG_KEYS.size()
	_setup_background(BG_KEYS[current_bg_index])

	# Start 0 A.D. Honor Bound music if not already playing
	if music_player and not music_player.playing:
		music_player.play()

	submenu_panel.visible = false
	credits_modal.visible = false

	# Connect main navigation buttons
	$MainPanel/Buttons/BtnLearn.pressed.connect(func(): _toggle_submenu("learn", $MainPanel/Buttons/BtnLearn))
	$MainPanel/Buttons/BtnSingle.pressed.connect(func(): _toggle_submenu("single", $MainPanel/Buttons/BtnSingle))
	$MainPanel/Buttons/BtnMulti.pressed.connect(func(): _toggle_submenu("multi", $MainPanel/Buttons/BtnMulti))
	$MainPanel/Buttons/BtnSettings.pressed.connect(func(): _toggle_submenu("settings", $MainPanel/Buttons/BtnSettings))
	$MainPanel/Buttons/BtnEditor.pressed.connect(_on_editor_pressed)
	$MainPanel/Buttons/BtnCredits.pressed.connect(_on_credits_pressed)
	$MainPanel/Buttons/BtnExit.pressed.connect(_on_exit_pressed)

	$CivSwitcher.pressed.connect(_cycle_background)
	$CreditsModal/CreditsBox/BtnCloseCredits.pressed.connect(_close_credits)

	# Connect hover sounds
	for btn in $MainPanel/Buttons.get_children():
		if btn is Button:
			btn.mouse_entered.connect(_play_hover)

func _setup_background(civ_name: String) -> void:
	# Clean existing layers
	for child in parallax_container.get_children():
		child.queue_free()
	bg_layers.clear()

	var textures = BACKGROUNDS.get(civ_name, BACKGROUNDS["hellenes"])
	for path in textures:
		var tex = load(path)
		if tex:
			var rect = TextureRect.new()
			rect.texture = tex
			rect.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
			rect.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_COVERED
			rect.anchor_right = 1.0
			rect.anchor_bottom = 1.0
			rect.offset_left = -120.0
			rect.offset_right = 120.0
			rect.offset_top = -40.0
			rect.offset_bottom = 40.0
			parallax_container.add_child(rect)
			bg_layers.append(rect)

	var civ_lbl = get_node_or_null("CivSwitcher/CivLabel")
	if civ_lbl:
		civ_lbl.text = "CIVILIZATION: " + civ_name.to_upper()

func _cycle_background() -> void:
	play_click()
	current_bg_index = (current_bg_index + 1) % BG_KEYS.size()
	_setup_background(BG_KEYS[current_bg_index])

func _process(delta: float) -> void:
	time_elapsed += delta
	var vp_size = get_viewport_rect().size
	if vp_size.x <= 0:
		return

	# Gentle 0 A.D. trigonometric parallax drift + subtle mouse reactivity
	var num_layers = bg_layers.size()
	for i in range(num_layers):
		var layer = bg_layers[i]
		if not is_instance_valid(layer):
			continue
		var depth_factor: float = float(i + 1) / float(num_layers)
		var drift: float = sin(time_elapsed * (0.2 + depth_factor * 0.15)) * (20.0 * depth_factor)
		var mouse_drift: float = (mouse_pos_norm.x - 0.5) * (35.0 * depth_factor)
		var target_offset_x: float = -120.0 + drift + mouse_drift
		layer.offset_left = lerp(layer.offset_left, target_offset_x, delta * 3.0)
		layer.offset_right = layer.offset_left + vp_size.x + 240.0

func _input(event: InputEvent) -> void:
	if event is InputEventMouseMotion:
		var vp_size = get_viewport_rect().size
		if vp_size.x > 0 and vp_size.y > 0:
			mouse_pos_norm = Vector2(event.position.x / vp_size.x, event.position.y / vp_size.y)
	elif event is InputEventMouseButton and event.pressed and event.button_index == MOUSE_BUTTON_LEFT:
		# If clicked outside main panel and submenu, close submenu
		if submenu_panel.visible:
			var main_rect = main_panel.get_global_rect()
			var sub_rect = submenu_panel.get_global_rect()
			if not main_rect.has_point(event.position) and not sub_rect.has_point(event.position):
				_close_submenu()

func _toggle_submenu(menu_id: String, anchor_button: Button) -> void:
	play_click()
	if active_menu_id == menu_id and submenu_panel.visible:
		_close_submenu()
		return

	active_menu_id = menu_id
	var data = submenu_data.get(menu_id, null)
	if not data:
		_close_submenu()
		return

	submenu_title.text = data["title"]

	# Populate items
	for c in submenu_buttons_box.get_children():
		c.queue_free()

	for item in data["items"]:
		var btn = Button.new()
		btn.text = item["label"]
		btn.tooltip_text = item.get("tooltip", "")
		btn.custom_minimum_size = Vector2(220, 34)
		btn.alignment = HORIZONTAL_ALIGNMENT_CENTER
		var action = item["action"]
		btn.pressed.connect(func(): _execute_action(action))
		btn.mouse_entered.connect(_play_hover)
		submenu_buttons_box.add_child(btn)

	# Position adjacent to the anchor button
	var btn_global_pos = anchor_button.global_position
	var main_global_rect = main_panel.get_global_rect()
	submenu_panel.global_position.x = main_global_rect.position.x + main_global_rect.size.x + 4
	submenu_panel.global_position.y = clamp(btn_global_pos.y - 12, 10, get_viewport_rect().size.y - submenu_panel.size.y - 10)
	submenu_panel.visible = true

func _close_submenu() -> void:
	active_menu_id = ""
	submenu_panel.visible = false

func _execute_action(action: String) -> void:
	play_click()
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
		"open_tips":
			_go_to_scene("res://scenes/menu/encyclopedia.tscn")
		"open_load":
			_close_submenu()
			load("res://scenes/session/savegame_dialog.gd").open(self, "load")
		"open_replays":
			_go_to_scene("res://scenes/menu/replay_browser.tscn")
		"open_lobby":
			_go_to_scene("res://scenes/menu/lobby.tscn")
		"open_options":
			_go_to_scene("res://scenes/menu/options.tscn")
		"open_asset_viewer":
			_go_to_scene("res://scenes/tools/asset_viewer.tscn")

func _on_editor_pressed() -> void:
	play_click()
	_go_to_scene("res://scenes/tools/asset_viewer.tscn")

func _on_credits_pressed() -> void:
	play_click()
	_close_submenu()
	credits_modal.visible = true

func _close_credits() -> void:
	play_click()
	credits_modal.visible = false

func _on_exit_pressed() -> void:
	play_click()
	get_tree().quit()

func play_click() -> void:
	if click_sfx:
		click_sfx.play()

func _play_hover() -> void:
	# Subtle hover feedback
	pass

func _go_to_scene(path: String) -> void:
	if has_node("/root/MatchSettings"):
		get_node("/root/MatchSettings").go(path)
	else:
		get_tree().change_scene_to_file(path)

