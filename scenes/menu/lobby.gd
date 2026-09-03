extends Control
# Lobby — mirrors 0 A.D. gui/gamesetup_mp + lobby + NetServer/NetClient
# Stub: host/join just changes to session; real NetHost poll is in native NetHost

func _ready() -> void:
	for path in [
		"VBox/HBox/Left/HostBox/VBox2/HBoxName/LineEditName",
		"VBox/HBox/Left/JoinBox/VBox3/HBoxIP/LineEditIP",
		"VBox/HBox/Right/HBoxChat/LineEditChat",
	]:
		var le = get_node_or_null(path)
		if le is LineEdit:
			MatchSettings.prepare_line_edit(le)

func _on_back_pressed():
	MatchSettings.go("res://scenes/menu/main_menu.tscn")

func _on_host_pressed():
	# In native, NetHost::startHost({7777}) + NetServerTurnManager
	print("[Lobby] Host pressed — would start NetHost Host :7777")
	MatchSettings.go("res://scenes/main.tscn")

func _on_join_pressed():
	print("[Lobby] Join pressed — would start NetClient 127.0.0.1:7777")
	MatchSettings.go("res://scenes/main.tscn")

func _on_send_pressed():
	print("[Lobby] Chat send stub")
