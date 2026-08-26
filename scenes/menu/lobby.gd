extends Control
# Lobby — mirrors 0 A.D. gui/gamesetup_mp + lobby + NetServer/NetClient
# Stub: host/join just changes to session; real NetHost poll is in native NetHost

func _on_back_pressed():
	get_tree().change_scene_to_file("res://scenes/menu/main_menu.tscn")

func _on_host_pressed():
	# In native, NetHost::startHost({7777}) + NetServerTurnManager
	print("[Lobby] Host pressed — would start NetHost Host :7777")
	get_tree().change_scene_to_file("res://scenes/main.tscn")

func _on_join_pressed():
	print("[Lobby] Join pressed — would start NetClient 127.0.0.1:7777")
	get_tree().change_scene_to_file("res://scenes/main.tscn")

func _on_send_pressed():
	print("[Lobby] Chat send stub")
