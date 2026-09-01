extends Control

const CIVS := [
	{"name": "Athenians", "emblem": "res://assets/textures/ui/emblems/athenians.png", "blurb": "Hoplites, archers, and a strong navy. +15% metal gather."},
	{"name": "Spartans", "emblem": "res://assets/textures/ui/emblems/spartans.png", "blurb": "Elite citizen-soldiers. +35% HP and +25% melee for infantry."},
	{"name": "Persians", "emblem": "res://assets/textures/ui/emblems/persians.png", "blurb": "Spearmen, archers, cavalry and camels. Faster cavalry, +25% trade."},
	{"name": "Romans", "emblem": "res://assets/textures/ui/emblems/romans.png", "blurb": "Legionary swordsmen and javelin cavalry. +30% build speed."},
	{"name": "Carthaginians", "emblem": "res://assets/textures/ui/emblems/carthaginians.png", "blurb": "Mercenary infantry and war elephants. Strong navy and trade."},
	{"name": "Macedonians", "emblem": "res://assets/textures/ui/emblems/macedonians.png", "blurb": "Sarissa pikemen and companion cavalry. Long spear reach."},
	{"name": "Britons", "emblem": "res://assets/textures/ui/emblems/britons.png", "blurb": "Celtic swords, slingers, chariots, and war dogs."},
	{"name": "Gauls", "emblem": "res://assets/textures/ui/emblems/gauls.png", "blurb": "Broadsword champions, chariots, war dogs, and the Carnyx horn."},
	{"name": "Iberians", "emblem": "res://assets/textures/ui/emblems/iberians.png", "blurb": "Swordsmen, Balearic slingers, and fast cavalry. +20% mining."},
	{"name": "Mauryans", "emblem": "res://assets/textures/ui/emblems/mauryans.png", "blurb": "Archer maidens, worker elephants, and war elephants."},
	{"name": "Kushites", "emblem": "res://assets/textures/ui/emblems/kushites.png", "blurb": "Nubian archers, camels, and bleeding / poison missile focus."},
	{"name": "Ptolemies", "emblem": "res://assets/textures/ui/emblems/ptolemies.png", "blurb": "Hellenistic-Egyptian mix with camel raiders. +30% trade."},
	{"name": "Seleucids", "emblem": "res://assets/textures/ui/emblems/seleucids.png", "blurb": "Silver-shield pikes, elephants, and scythed chariots."},
]

func _ready() -> void:
	var civ_opt: OptionButton = $VBox/HBox/Left/CivOption
	civ_opt.clear()
	for c in CIVS:
		var emblem = load(c["emblem"])
		if emblem:
			civ_opt.add_icon_item(emblem, c["name"])
		else:
			civ_opt.add_item(c["name"])
	var map_opt: OptionButton = $VBox/HBox/Left/MapOption
	if map_opt.item_count == 0:
		map_opt.add_item("Acropolis Bay")
		map_opt.add_item("Corinthian Isthmus")
		map_opt.add_item("Procedural Continental")
	if not civ_opt.item_selected.is_connected(_on_civ_changed):
		civ_opt.item_selected.connect(_on_civ_changed)
	_on_civ_changed(civ_opt.selected)

func _on_civ_changed(idx: int) -> void:
	if idx < 0 or idx >= CIVS.size():
		return
	var preview := get_node_or_null("VBox/HBox/Right/Preview/Label")
	if preview:
		preview.text = CIVS[idx]["name"] + "\n\n" + CIVS[idx]["blurb"] + \
			"\n\nBarracks and fortress train this civilization's own soldiers and unique units."

func _on_back_pressed() -> void:
	get_tree().change_scene_to_file("res://scenes/menu/main_menu.tscn")

func _on_start_pressed() -> void:
	var civ_opt: OptionButton = $VBox/HBox/Left/CivOption
	var map_opt: OptionButton = $VBox/HBox/Left/MapOption
	var players_node = get_node_or_null("VBox/HBox/Left/Players/SpinPlayers")
	var players = int(players_node.value) if players_node else 2
	var civ := civ_opt.selected
	if civ < 0:
		civ = 0
	var ai_civ := (civ + 3) % CIVS.size()
	if has_node("/root/MatchSettings"):
		var ms = get_node("/root/MatchSettings")
		ms.player_civ = civ
		ms.ai_civ = ai_civ
		ms.map_index = map_opt.selected
		ms.player_count = players
	print("[GameSetup] Start — civ ", CIVS[civ]["name"], " vs ", CIVS[ai_civ]["name"])
	var err = get_tree().change_scene_to_file("res://scenes/main.tscn")
	if err != OK:
		var sc = load("res://scenes/main.tscn")
		if sc:
			get_tree().change_scene_to_packed(sc)
