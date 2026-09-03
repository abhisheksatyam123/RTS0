extends Control

@onready var replay_list: ItemList = $Center/Panel/VBox/ReplayList

func _ready() -> void:
	load_replays()

func load_replays() -> void:
	replay_list.clear()
	var dir = DirAccess.open("user://replays")
	if dir:
		dir.list_dir_begin()
		var file_name = dir.get_next()
		while file_name != "":
			if not dir.current_is_dir() and file_name.ends_with(".rts0rec"):
				replay_list.add_item(file_name)
			file_name = dir.get_next()
	if replay_list.item_count == 0:
		replay_list.add_item("No saved matches found in user://replays/")

func _on_back_pressed() -> void:
	MatchSettings.go("res://scenes/menu/main_menu.tscn")

func _on_play_pressed() -> void:
	if replay_list.get_selected_items().size() > 0:
		MatchSettings.go("res://scenes/main.tscn")
