extends Control
# Gamesetup — mirrors 0 A.D. gui/gamesetup/gamesetup.xml + page_gamesetup.xml
# Forwards to session (main.tscn) with selected map/civ. In native, this would set TurnManager map seed.

func _ready():
	# populate civs like 0ad templates
	var civs = ["Athenians", "Spartans", "Romans", "Carthaginians", "Britons", "Gauls", "Persians"]
	for c in civs:
		$VBox/HBox/Left/CivOption.add_item(c)
	$VBox/HBox/Left/MapOption.add_item("Gerasa (Aegean)")
	$VBox/HBox/Left/MapOption.add_item("Latium")
	$VBox/HBox/Left/MapOption.add_item("Alpine Lakes")

func _on_back_pressed():
	get_tree().change_scene_to_file("res://scenes/menu/main_menu.tscn")

func _on_start_pressed():
	# Would pass map/civ to Simulation via Autoload or NetHost
	var players_node = get_node_or_null("VBox/HBox/Left/Players/SpinPlayers")
	var players = players_node.value if players_node else 2
	print("[GameSetup] Start — map Gerasa, civ Athenians, players ", players)
	var err = get_tree().change_scene_to_file("res://scenes/main.tscn")
	if err != OK:
		print("[GameSetup] change_scene failed:", err, " — fallback load main.tscn directly")
		var sc = load("res://scenes/main.tscn")
		if sc:
			get_tree().change_scene_to_packed(sc)
