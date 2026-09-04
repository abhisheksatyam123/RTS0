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
	{"name": "Mauryans", "emblem": "res://assets/textures/ui/emblems/mauryans.png", "blurb": "Archer maidens, worker elephants, and elephant archers."},
	{"name": "Kushites", "emblem": "res://assets/textures/ui/emblems/kushites.png", "blurb": "Nubian archers, camels, and bleeding / poison missile focus."},
	{"name": "Ptolemies", "emblem": "res://assets/textures/ui/emblems/ptolemies.png", "blurb": "Hellenistic-Egyptian mix with camel raiders. +30% trade."},
	{"name": "Seleucids", "emblem": "res://assets/textures/ui/emblems/seleucids.png", "blurb": "Silver-shield pikes, elephants, and scythed chariots."},
]

func _ready() -> void:
	var civ_opt := _make_option($VBox/HBox/Left/CivSlot)
	for c in CIVS:
		var emblem = load(c["emblem"])
		if emblem:
			civ_opt.add_icon_item(emblem, c["name"])
		else:
			civ_opt.add_item(c["name"])
	var map_opt := _make_option($VBox/HBox/Left/MapSlot)
	map_opt.add_item("Acropolis Bay")
	map_opt.add_item("Corinthian Isthmus")
	map_opt.add_item("Procedural Continental")
	civ_opt.item_selected.connect(_on_civ_changed)
	_on_civ_changed(civ_opt.selected)
	_mute_spin($VBox/HBox/Left/Players/SpinPlayers)
	_mute_spin($VBox/HBox/Left/Resources/SpinRes)

func _make_option(slot: Control) -> RtsoSafeSelect:
	var ob := RtsoSafeSelect.new()
	ob.set_anchors_preset(Control.PRESET_FULL_RECT)
	slot.add_child(ob)
	return ob

func _mute_spin(sb: SpinBox) -> void:
	if sb:
		MatchSettings.prepare_line_edit(sb.get_line_edit())

func _civ_option() -> RtsoSafeSelect:
	return $VBox/HBox/Left/CivSlot.get_child(0)

func _map_option() -> RtsoSafeSelect:
	return $VBox/HBox/Left/MapSlot.get_child(0)

func _on_civ_changed(idx: int) -> void:
	if idx < 0 or idx >= CIVS.size():
		return
	var preview := get_node_or_null("VBox/HBox/Right/Preview/Label")
	if preview:
		preview.text = CIVS[idx]["name"] + "\n\n" + CIVS[idx]["blurb"] + \
			"\n\nBarracks and fortress train this civilization's own soldiers and unique units."

func _on_back_pressed() -> void:
	MatchSettings.go("res://scenes/menu/main_menu.tscn")

func _on_start_pressed() -> void:
	var civ_opt: RtsoSafeSelect = _civ_option()
	var map_opt: RtsoSafeSelect = _map_option()
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
	var err = MatchSettings.go("res://scenes/main.tscn")
	if err != OK:
		var sc = load("res://scenes/main.tscn")
		if sc:
			MatchSettings.hide_option_popups()
			get_tree().change_scene_to_packed(sc)
