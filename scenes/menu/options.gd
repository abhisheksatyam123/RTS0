extends Control

const CONFIG_PATH = "user://config.ini"

@onready var line_edit_sp: LineEdit = $CenterContainer/Panel/VBox/TabContainer/General/GeneralList/PlayerNameSP/LineEdit
@onready var line_edit_mp: LineEdit = $CenterContainer/Panel/VBox/TabContainer/General/GeneralList/PlayerNameMP/LineEdit
@onready var chk_pause_focus: CheckBox = $CenterContainer/Panel/VBox/TabContainer/General/GeneralList/PauseOnFocus
@onready var chk_windowed: CheckBox = $CenterContainer/Panel/VBox/TabContainer/Graphics/GraphicsList/Windowed
@onready var chk_shadows: CheckBox = $CenterContainer/Panel/VBox/TabContainer/Graphics/GraphicsList/Shadows
@onready var slider_master: HSlider = $CenterContainer/Panel/VBox/TabContainer/Sound/SoundList/MasterVol/HSlider
@onready var spin_port: SpinBox = $CenterContainer/Panel/VBox/TabContainer/Network/NetworkList/Port/SpinBox

func _ready() -> void:
	load_settings()

func load_settings() -> void:
	var cfg = ConfigFile.new()
	var err = cfg.load(CONFIG_PATH)
	if err == OK:
		line_edit_sp.text = cfg.get_value("general", "player_name_sp", "Pericles")
		line_edit_mp.text = cfg.get_value("general", "player_name_mp", "Athenian_General")
		chk_pause_focus.button_pressed = cfg.get_value("general", "pause_on_focus", true)
		chk_windowed.button_pressed = cfg.get_value("graphics", "windowed", true)
		chk_shadows.button_pressed = cfg.get_value("graphics", "shadows", true)
		slider_master.value = cfg.get_value("sound", "master_volume", 0.8)
		spin_port.value = cfg.get_value("network", "port", 7777)
	else:
		line_edit_sp.text = "Pericles"
		line_edit_mp.text = "Athenian_General"
		slider_master.value = 0.8

func save_settings() -> void:
	var cfg = ConfigFile.new()
	cfg.set_value("general", "player_name_sp", line_edit_sp.text)
	cfg.set_value("general", "player_name_mp", line_edit_mp.text)
	cfg.set_value("general", "pause_on_focus", chk_pause_focus.button_pressed)
	cfg.set_value("graphics", "windowed", chk_windowed.button_pressed)
	cfg.set_value("graphics", "shadows", chk_shadows.button_pressed)
	cfg.set_value("sound", "master_volume", slider_master.value)
	cfg.set_value("network", "port", spin_port.value)
	cfg.save(CONFIG_PATH)

	# Apply master volume
	var bus_idx = AudioServer.get_bus_index("Master")
	if bus_idx >= 0:
		var db = linear_to_db(float(slider_master.value))
		AudioServer.set_bus_volume_db(bus_idx, db)

	# Apply window mode
	if chk_windowed.button_pressed:
		DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_WINDOWED)
	else:
		DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_FULLSCREEN)

func _on_back_pressed() -> void:
	save_settings()
	get_tree().change_scene_to_file("res://scenes/menu/main_menu.tscn")
