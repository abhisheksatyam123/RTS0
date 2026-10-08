extends Control
## Game Setup — mirrors 0 A.D. gui/page_gamesetup.xml:
## map filter + map list + description/players/size preview + seed,
## per-player slot rows (Human/AI/Closed, civ, team, difficulty),
## match options (victory, starting resources). Everything is persisted
## to the MatchSettings autoload; Start validates then loads main.tscn,
## whose _ready() applies the setup via load_map_seeded + sim setters.
##
## Sim constraints honored (see scratchpad/todo.md GameSetup section):
## - victory / petra difficulty / behavior / starting-res have sim setters
## - NO popcap / reveal / nomad / ceasefire setters exist -> not shown
## - Petra only drives team 1, RMS spawns team-per-spawn-index -> slots > 4
##   still spawn (base+units) but have no AI economy; teams beyond 2 share
##   teams 0/1 via diplomacy defaults. Slot cap is 8 like 0 A.D.

const CIVS := [
	{"code": "athen", "name": "Athenians", "emblem": "res://assets/textures/ui/emblems/athenians.png", "blurb": "Silver Owls: +15% metal gather. Balanced hoplites and a strong navy."},
	{"code": "spart", "name": "Spartans", "emblem": "res://assets/textures/ui/emblems/spartans.png", "blurb": "Agoge: +35% HP and +25% melee for citizen-soldiers and champions."},
	{"code": "pers", "name": "Persians", "emblem": "res://assets/textures/ui/emblems/persians.png", "blurb": "Achaemenid trade: +25% market gain, faster cavalry production."},
	{"code": "rome", "name": "Romans", "emblem": "res://assets/textures/ui/emblems/romans.png", "blurb": "Legionary discipline: +30% build rate, heavy legionary armor."},
	{"code": "cart", "name": "Carthaginians", "emblem": "res://assets/textures/ui/emblems/carthaginians.png", "blurb": "Naval mastery and war elephants. Elite mercenary forces."},
	{"code": "mace", "name": "Macedonians", "emblem": "res://assets/textures/ui/emblems/macedonians.png", "blurb": "Sarissa phalanx and companion cavalry shock charges."},
	{"code": "brit", "name": "Britons", "emblem": "res://assets/textures/ui/emblems/britons.png", "blurb": "Celtic chariot raiders, war dogs, and rotary mills."},
	{"code": "gaul", "name": "Gauls", "emblem": "res://assets/textures/ui/emblems/gauls.png", "blurb": "Carnyx morale aura and fearless broadsword champions."},
	{"code": "iber", "name": "Iberians", "emblem": "res://assets/textures/ui/emblems/iberians.png", "blurb": "Balearic slingers, fast cavalry, +20% mining rate."},
	{"code": "maur", "name": "Mauryans", "emblem": "res://assets/textures/ui/emblems/mauryans.png", "blurb": "Worker elephants, archer maidens, high economic stamina."},
	{"code": "kush", "name": "Kushites", "emblem": "res://assets/textures/ui/emblems/kushites.png", "blurb": "Bleeding and poison arrows. Nubian archers, pyramid shrines."},
	{"code": "ptol", "name": "Ptolemies", "emblem": "res://assets/textures/ui/emblems/ptolemies.png", "blurb": "Mercenary camps, camel raiders, +30% trade gain."},
	{"code": "sele", "name": "Seleucids", "emblem": "res://assets/textures/ui/emblems/seleucids.png", "blurb": "Silver shields, cataphracts, and scythed chariots."},
	{"code": "han", "name": "Han Chinese", "emblem": "res://assets/textures/ui/emblems/han.png", "blurb": "Crossbowmen, war chariots and imperial ministers."},
]

# (map_id, display name, kind, players, size_m, biome, description)
# Scenario JSONs are read live for desc/size/players; procedural RMS
# entries come from Simulation::get_available_maps order.
const RMS_MAPS := [
	["random_continental", "Continental (RMS)", "Random", 8, 128, "mediterranean", "A grand landmass with winding rivers and rolling resource woodlands. Supports 2-8 players."],
	["random_archipelago", "Archipelago (RMS)", "Random", 8, 128, "aegean", "Deep-water island cluster: docks and naval supremacy required. Supports 2-8 players."],
	["random_oasis", "Oasis (RMS)", "Random", 8, 128, "oasis", "Forested desert borders around a lush central lake basin. Supports 2-8 players."],
	["random_highlands", "Highlands (RMS)", "Random", 8, 128, "alpine", "Rugged alpine terrain with cliffs, gorge rivers, and passes. Supports 2-8 players."],
	["random_valley", "Fertile Valley (RMS)", "Random", 8, 128, "latium", "Lush river valley with balanced contested resources. Supports 2-8 players."],
	["random_mediterranean", "Mediterranean Sea (RMS)", "Random", 8, 128, "mediterranean", "Central sea ringed by fertile mainland coasts. Supports 2-8 players."],
	["random_acropolis", "Acropolis Heights (RMS)", "Random", 8, 128, "mediterranean", "Elevated central plateaus over resource valleys. Supports 2-8 players."],
	["random_spartan_valley", "Spartan Valley (RMS)", "Random", 2, 128, "alpine", "Narrow pass for phalanx warfare and choke points. Best with 2 players."],
	["random_danubius", "Danubius (RMS)", "Random", 4, 128, "temperate", "Meandering dual-channel river with fords and a central island. Supports 2-4 players."],
	["random_saharan_oases", "Saharan Oases (RMS)", "Random", 8, 128, "oasis", "Arid dunes, date palm groves, and freshwater oases. Supports 2-8 players."],
	["random_anatolian_plateau", "Anatolian Plateau (RMS)", "Random", 8, 128, "steppe", "High plateau steppe with canyon ravines and rich minerals. Supports 2-8 players."],
	["random_jebel_barkal", "Jebel Barkal (RMS)", "Random", 8, 128, "savanna", "Central sandstone mesa with high-ground advantage. Supports 2-8 players."],
	["random_mainland", "Mainland (RMS)", "Random", 8, 128, "mediterranean", "Open rolling land with no water barriers. Supports 2-8 players."],
	["random_alpine_lakes", "Alpine Lakes (RMS)", "Random", 8, 128, "alpine", "Ridged mountains cradling cold lakes. Supports 2-8 players."],
	["random_lake", "Lake (RMS)", "Random", 8, 128, "temperate", "A great central lake ringed by the players. Supports 2-8 players."],
	["random_gulf_of_bothnia", "Gulf of Bothnia (RMS)", "Random", 8, 128, "alpine", "A northern gulf cuts in from one edge; players around its shores. Supports 2-8 players."],
	["random_ardennes_forest", "Ardennes Forest (RMS)", "Random", 8, 128, "temperate", "Dense hilly forest with clearings for each base. Supports 2-8 players."],
	["random_nearestern_badlands", "Near-Eastern Badlands (RMS)", "Random", 8, 128, "oasis", "Eroded desert canyons with a few oases and rich mines. Supports 2-8 players."],
	["random_rivers", "Rivers (RMS)", "Random", 8, 128, "temperate", "Rivers run from a central lake out between the players, crossed at fords. Supports 2-8 players."],
	["random_canyon", "Canyon (RMS)", "Random", 8, 128, "oasis", "Each base sits in a gorge leading to a central basin. Supports 2-8 players."],
	["random_corinthian_isthmus", "Corinthian Isthmus (RMS)", "Random", 8, 128, "mediterranean", "Two lands joined by a narrow isthmus between two gulfs. Supports 2-8 players."],
	["random_belgian_uplands", "Belgian Uplands (RMS)", "Random", 8, 128, "temperate", "Steep wooded hills and valleys with no water. Supports 2-8 players."],
	["random_lorraine_plain", "Lorraine Plain (RMS)", "Random", 8, 128, "temperate", "A flat plain split by a river with a few fords. Supports 2-8 players."],
	["random_hyrcanian_shores", "Hyrcanian Shores (RMS)", "Random", 8, 128, "temperate", "A sea along one edge, a coastal strip and wooded highlands. Supports 2-8 players."],
	["random_northern_lights", "Northern Lights (RMS)", "Random", 8, 128, "polar", "Snowy land with a frozen sea and ice floes; scarce wood. Supports 2-8 players."],
	["random_kerala", "Kerala (RMS)", "Random", 8, 128, "india", "India's south-western shore, between the sea and the mountains. Supports 2-8 players."],
	["random_polar_sea", "Polar Sea (RMS)", "Random", 8, 128, "polar", "Icy land round a central sea, hardly any trees. Supports 2-8 players."],
	["random_sahel", "Sahel (RMS)", "Random", 8, 128, "savanna", "Open savanna with a few water holes; little wood. Supports 2-8 players."],
	["random_african_plains", "African Plains (RMS)", "Random", 8, 128, "savanna", "Lush plains with watering holes, hills and wildlife. Supports 2-8 players."],
	["random_alpine_valley", "Alpine Valley (RMS)", "Random", 8, 128, "alpine", "Deep valleys walled off by mountain ranges with passes. Supports 2-8 players."],
	["random_syria", "Syria (RMS)", "Random", 8, 128, "desert", "Dry plains with slightly rolling highlands. Supports 2-8 players."],
	["random_cantabrian_highlands", "Cantabrian Highlands (RMS)", "Random", 8, 128, "temperate", "Each player starts on a cliff-ringed hill with one ramp. Supports 2-8 players."],
	["random_pyrenean_sierra", "Pyrenean Sierra (RMS)", "Random", 8, 128, "alpine", "High mountains with two passes separate the teams. Supports 2-8 players."],
	["random_sahel_watering_holes", "Sahel Watering Holes (RMS)", "Random", 8, 128, "savanna", "Lines of water with shallows between the players. Supports 2-8 players."],
	["random_persian_highlands", "Persian Highlands (RMS)", "Random", 8, 128, "persia", "A dry sunken plateau rich in minerals, ringed by rocky hills. Supports 2-8 players."],
	["random_volcanic_lands", "Volcanic Lands (RMS)", "Random", 8, 128, "volcanic", "A charred land around a smoking volcano. Supports 2-8 players."],
	["random_lions_den", "Lion's Den (RMS)", "Random", 8, 128, "steppe", "Cliff-ringed dens with ramps down to a sunken valley. Supports 2-8 players."],
	["random_hells_pass", "Hell's Pass (RMS)", "Random", 8, 128, "alpine", "Mountain spines between the players, with narrow passes. Supports 2-8 players."],
	["random_deep_forest", "Deep Forest (RMS)", "Random", 8, 128, "temperate", "A deep forest crossed by paths between the bases. Supports 2-8 players."],
	["random_india", "India (RMS)", "Random", 8, 128, "india", "Parched land round one half dried-up lake. Supports 2-8 players."],
	["random_guadalquivir_river", "Guadalquivir River (RMS)", "Random", 8, 128, "mediterranean", "A coast split by a river with three shallows. Supports 2-8 players."],
	["random_fields_of_meroe", "Fields of Meroe (RMS)", "Random", 8, 128, "savanna", "The Nile with two cataract crossings, dunes and acacias. Supports 2-8 players."],
	["random_gear", "Gear (RMS)", "Random", 8, 128, "temperate", "Waterways like a spider web round a central lake. Supports 2-8 players."],
	["random_atlas_mountains", "Atlas Mountains (RMS)", "Random", 8, 128, "desert", "Rugged mountains with little room and scarce wood. Supports 2-8 players."],
	["random_frontier", "Frontier (RMS)", "Random", 8, 128, "temperate", "A random wild landscape of hills, plateaus and lakes. Supports 2-8 players."],
	["random_wild_lake", "Wild Lake (RMS)", "Random", 8, 128, "temperate", "Rough wild terrain round a lake. Supports 2-8 players."],
	["random_stronghold", "Stronghold (RMS)", "Random", 8, 128, "temperate", "Teammates start side by side and defend together. Supports 2-8 players."],
	["random_caledonian_meadows", "Caledonian Meadows (RMS)", "Random", 8, 128, "alpine", "Fertile meadows amid harsh terrain and rivers. Supports 2-8 players."],
	["random_rhine_marshlands", "Rhine Marshlands (RMS)", "Random", 8, 128, "temperate", "Shallow wetlands units can wade through. Supports 2-8 players."],
]

# 0 A.D. population limits per player (0 = unlimited) and ceasefire lengths in minutes.
const POP_CAPS := [[50, "50"], [100, "100"], [150, "150"], [200, "200"], [250, "250"], [300, "300"], [0, "Unlimited"]]
const CEASEFIRES := [0, 1, 2, 3, 4, 5, 10, 15, 20, 30, 45, 60]
# 0 A.D. Wonder / Capture the Relic hold times in minutes (default 20) and relic counts (default 2).
const VICTORY_TIMES := [0, 1, 2, 5, 10, 15, 20, 30, 45, 60]
const RELIC_COUNTS := [1, 2, 3, 4, 5, 6, 8, 10, 12, 16]

# 0 A.D. map sizes (width in meters) for random maps.
const MAP_SIZES := [[128, "Tiny (128 m)"], [192, "Small (192 m)"], [256, "Medium (256 m)"],
	[320, "Normal (320 m)"], [384, "Large (384 m)"], [448, "Very Large (448 m)"], [512, "Giant (512 m)"]]

# Index == rtso::VictoryCondition id (sim_types.h).
const VICTORIES := ["Conquest — destroy all enemies", "Wonder — hold a Wonder", "Relic — claim and hold all relics", "Endless — no victory, sandbox",
	"Conquest Units — kill every enemy unit", "Conquest Structures — raze every enemy structure", "Regicide — kill the enemy hero, keep yours"]
const START_RES := ["Sparse (150/200/150/100)", "Normal (300/400/300/200)", "Generous (500/700/500/350)", "Abundant (900/1200/900/600)"]
const DIFFS := ["Sandbox", "Very Easy", "Easy", "Medium", "Hard", "Very Hard"]
const BEHAVIORS := ["Random", "Defensive", "Balanced", "Aggressive"]
const SLOT_KINDS := ["Human", "AI", "Closed"]

var _maps: Array = []          # all map dicts
var _filtered: Array = []      # after filter
var _sel_map: int = 0
var _slot_rows: Array = []     # per-slot {kind,civ,team,diff,beh}
var _rebuilding_slots := false

func _ready() -> void:
	_load_maps()
	_build_map_filter()
	_build_map_list()
	_build_options()
	_restore_settings()
	refresh_map_list()
	refresh_slots()
	_update_preview()

# ---------------------------------------------------------------- maps ---

func _load_maps() -> void:
	_maps.clear()
	var dir := DirAccess.open("res://data/maps")
	if dir != null:
		dir.list_dir_begin()
		var file_name := dir.get_next()
		var scenario_ids: Array = []
		while file_name != "":
			if not dir.current_is_dir() and file_name.ends_with(".json"):
				scenario_ids.append(file_name.get_basename())
			file_name = dir.get_next()
		scenario_ids.sort()
		for sid in scenario_ids:
			_maps.append(_read_scenario(sid))
	if _maps.is_empty():
		for p in ["acropolis_bay", "corinthian_isthmus", "atlas_valleys", "bactria", "battle_for_the_tiber"]:
			_maps.append(_read_scenario(p))
	for r in RMS_MAPS:
		_maps.append({"id": r[0], "name": r[1], "kind": r[2], "players": r[3],
			"size": r[4], "biome": r[5], "desc": r[6]})

func _read_scenario(map_id: String) -> Dictionary:
	var d := {"id": map_id, "name": map_id, "kind": "Scenario",
		"players": 2, "size": 256, "biome": "mediterranean", "desc": ""}
	var path := "res://data/maps/" + map_id + ".json"
	if not FileAccess.file_exists(path):
		return d
	var fa := FileAccess.open(path, FileAccess.READ)
	if fa == null:
		return d
	var parsed: Variant = JSON.parse_string(fa.get_as_text())
	if typeof(parsed) != TYPE_DICTIONARY:
		return d
	d["name"] = str(parsed.get("name", map_id))
	d["biome"] = str(parsed.get("biome", "mediterranean"))
	d["desc"] = str(parsed.get("desc", ""))
	d["size"] = int(float(parsed.get("world_size", 256.0)))
	var pl: Array = parsed.get("players", [])
	if not pl.is_empty():
		d["players"] = pl.size()
	return d

func _make_option(slot: Control) -> RtsoSafeSelect:
	for c in slot.get_children():
		c.queue_free()
	var ob := RtsoSafeSelect.new()
	ob.set_anchors_preset(Control.PRESET_FULL_RECT)
	slot.add_child(ob)
	return ob

func _build_map_filter() -> void:
	var f := _make_option($VBox/HBox/Left/MapFilterSlot)
	f.add_item("All maps")
	f.add_item("Scenarios")
	f.add_item("Random (RMS)")
	f.select(0)
	f.item_selected.connect(func(_i: int) -> void: refresh_map_list())

func _build_map_list() -> void:
	var list: ItemList = $VBox/HBox/Left/MapList
	list.clear()
	list.item_selected.connect(_on_map_list_selected)

func _map_filter() -> int:
	var f: RtsoSafeSelect = $VBox/HBox/Left/MapFilterSlot.get_child(0)
	return f.get_selected()

func refresh_map_list() -> void:
	var list: ItemList = $VBox/HBox/Left/MapList
	list.clear()
	_filtered.clear()
	var flt := _map_filter()
	for m in _maps:
		if flt == 1 and m["kind"] != "Scenario":
			continue
		if flt == 2 and m["kind"] != "Random":
			continue
		_filtered.append(m)
		list.add_item(str(m["name"]))
	if _sel_map < 0 or _sel_map >= _filtered.size():
		_sel_map = 0
	if not _filtered.is_empty():
		list.select(_sel_map)
	_update_preview()

func _on_map_list_selected(idx: int) -> void:
	_sel_map = idx
	_update_preview()
	_clamp_slots_to_map()

func _selected_map() -> Dictionary:
	if _filtered.is_empty():
		return {}
	return _filtered[clampi(_sel_map, 0, _filtered.size() - 1)]

func _update_preview() -> void:
	var m := _selected_map()
	var lbl := $VBox/HBox/Right/Preview/Label as Label
	var crest := $VBox/HBox/Right/CivPreview/Crest as TextureRect
	var civ_lbl := $VBox/HBox/Right/CivPreview/CivLabel as Label
	if m.is_empty():
		if lbl: lbl.text = "No map selected."
		return
	var seed_txt := "Random seed" if int($VBox/HBox/Left/SeedRow/SpinSeed.value) == 0 else "Seed %d" % int($VBox/HBox/Left/SeedRow/SpinSeed.value)
	if lbl:
		lbl.text = "%s\n[%s • %s • %dm • %d players]\n\n%s\n\n%s" % [
			m["name"], m["kind"], m["biome"], int(m["size"]), int(m["players"]), seed_txt, m["desc"]]
	# Civ preview follows slot 0 (the human player).
	var civ := _slot_civ(0)
	if crest and civ >= 0 and civ < CIVS.size():
		var tex: Texture2D = load(CIVS[civ]["emblem"])
		if tex: crest.texture = tex
	if civ_lbl and civ >= 0 and civ < CIVS.size():
		civ_lbl.text = "%s\n\n%s" % [CIVS[civ]["name"], CIVS[civ]["blurb"]]

# ---------------------------------------------------------------- slots ---

func _slot_count_node() -> SpinBox:
	return $VBox/HBox/Right/SlotsBox/SlotCountRow/SpinSlots as SpinBox

func _slots_grid() -> GridContainer:
	return $VBox/HBox/Right/SlotsBox/SlotsGrid as GridContainer

func _ensure_slot_rows(n: int) -> void:
	while _slot_rows.size() < n:
		var i := _slot_rows.size()
		_slot_rows.append({
			"kind": 1 if i > 0 else 0,
			"civ": i % CIVS.size(),
			"team": i + 1,
			"diff": 3,
			"beh": 2,
		})
	if _slot_rows.size() > n:
		_slot_rows.resize(n)

func refresh_slots() -> void:
	_rebuilding_slots = true
	var grid := _slots_grid()
	for c in grid.get_children():
		c.queue_free()
	var n := int(_slot_count_node().value)
	_ensure_slot_rows(n)
	for i in range(n):
		_add_slot_row(grid, i)
	_rebuilding_slots = false

func _add_slot_row(grid: GridContainer, i: int) -> void:
	var row: Dictionary = _slot_rows[i]
	var name := Label.new()
	name.text = "P%d%s" % [i + 1, " (You)" if i == 0 else ""]
	grid.add_child(name)
	# kind
	var kind_slot := Control.new()
	kind_slot.custom_minimum_size = Vector2(92, 28)
	grid.add_child(kind_slot)
	var kind := _make_option(kind_slot)
	for k in SLOT_KINDS:
		kind.add_item(k)
	kind.select(int(row["kind"]) if i > 0 else 0)
	if i == 0:
		kind.select(0)
	kind.item_selected.connect(_on_slot_kind.bind(i))
	# civ
	var civ_slot := Control.new()
	civ_slot.custom_minimum_size = Vector2(130, 28)
	grid.add_child(civ_slot)
	var civ := _make_option(civ_slot)
	for c in CIVS:
		var emblem: Texture2D = load(c["emblem"])
		if emblem: civ.add_icon_item(emblem, c["name"])
		else: civ.add_item(c["name"])
	civ.select(int(row["civ"]))
	civ.item_selected.connect(_on_slot_civ.bind(i))
	# team
	var team_slot := Control.new()
	team_slot.custom_minimum_size = Vector2(64, 28)
	grid.add_child(team_slot)
	var team := _make_option(team_slot)
	for t in range(1, 5):
		team.add_item("T%d" % t)
	team.select(clampi(int(row["team"]) - 1, 0, 3))
	team.item_selected.connect(_on_slot_team.bind(i))
	# AI difficulty (per-slot; slot 0 uses the global default row)
	var diff_slot := Control.new()
	diff_slot.custom_minimum_size = Vector2(92, 28)
	grid.add_child(diff_slot)
	var diff := _make_option(diff_slot)
	for dname in DIFFS:
		diff.add_item(dname)
	diff.select(clampi(int(row["diff"]), 0, 5))
	diff.item_selected.connect(_on_slot_diff.bind(i))

func _on_slot_kind(idx: int, slot: int) -> void:
	if _rebuilding_slots or slot < 0 or slot >= _slot_rows.size():
		return
	_slot_rows[slot]["kind"] = idx
	_update_preview()

func _on_slot_civ(idx: int, slot: int) -> void:
	if _rebuilding_slots or slot < 0 or slot >= _slot_rows.size():
		return
	_slot_rows[slot]["civ"] = idx
	_update_preview()

func _on_slot_team(idx: int, slot: int) -> void:
	if _rebuilding_slots or slot < 0 or slot >= _slot_rows.size():
		return
	_slot_rows[slot]["team"] = idx + 1

func _on_slot_diff(idx: int, slot: int) -> void:
	if _rebuilding_slots or slot < 0 or slot >= _slot_rows.size():
		return
	_slot_rows[slot]["diff"] = idx

func _slot_civ(slot: int) -> int:
	if slot < 0 or slot >= _slot_rows.size():
		return 0
	return int(_slot_rows[slot]["civ"])

func _clamp_slots_to_map() -> void:
	# RMS maps spawn any count; scenarios are fixed-size. Warn, don't force:
	# extra spawns still get bases but no AI economy (Petra drives team 1).
	var m := _selected_map()
	if m.is_empty() or str(m.get("kind", "")) != "Scenario":
		return
	var want := int(m["players"])
	var spin := _slot_count_node()
	if int(spin.value) != want:
		spin.value = want
		refresh_slots()

# ---------------------------------------------------------------- options ---

func _build_options() -> void:
	var vic := _make_option($VBox/HBox/Left/VictorySlot)
	for v in VICTORIES:
		vic.add_item(v)
	vic.select(0)
	var msz := _make_option($VBox/HBox/Left/MapSizeSlot)
	for s in MAP_SIZES:
		msz.add_item(s[1], s[0])
	msz.select(0)
	var pop := _make_option($VBox/HBox/Left/PopCapSlot)
	for p in POP_CAPS:
		pop.add_item(p[1])
	pop.select(3)
	var cf := _make_option($VBox/HBox/Left/CeasefireSlot)
	for c in CEASEFIRES:
		cf.add_item("No ceasefire" if c == 0 else "%d min" % c)
	cf.select(0)
	var wt := _make_option($VBox/HBox/Left/VictoryTimesRow/WonderTimeSlot)
	var rt := _make_option($VBox/HBox/Left/VictoryTimesRow/RelicTimeSlot)
	for t in VICTORY_TIMES:
		wt.add_item("%d min" % t)
		rt.add_item("hold %d min" % t)
	wt.select(VICTORY_TIMES.find(20))
	rt.select(VICTORY_TIMES.find(20))
	var rc := _make_option($VBox/HBox/Left/VictoryTimesRow/RelicCountSlot)
	for c in RELIC_COUNTS:
		rc.add_item(str(c))
	rc.select(RELIC_COUNTS.find(2))
	var res := _make_option($VBox/HBox/Left/ResSlot)
	for r in START_RES:
		res.add_item(r)
	res.select(1)
	var beh := _make_option($VBox/HBox/Left/BehaviorSlot)
	for b in BEHAVIORS:
		beh.add_item(b)
	beh.select(2)
	_mute_spin($VBox/HBox/Left/SeedRow/SpinSeed)
	_mute_spin($VBox/HBox/Right/SlotsBox/SlotCountRow/SpinSlots)
	$VBox/HBox/Right/SlotsBox/SlotCountRow/SpinSlots.value_changed.connect(
		func(_v: float) -> void: refresh_slots())
	$VBox/HBox/Left/SeedRow/BtnReroll.pressed.connect(_on_reroll_seed)
	$VBox/HBox/Left/SeedRow/SpinSeed.value_changed.connect(
		func(_v: float) -> void: _update_preview())

func _mute_spin(sb: SpinBox) -> void:
	if sb:
		MatchSettings.prepare_line_edit(sb.get_line_edit())
		MatchSettings.strip_engine_popups(sb)

func _on_reroll_seed() -> void:
	$VBox/HBox/Left/SeedRow/SpinSeed.value = randi() % 1000000000
	_update_preview()

func _restore_settings() -> void:
	if not has_node("/root/MatchSettings"):
		return
	var ms := get_node("/root/MatchSettings")
	var vic: RtsoSafeSelect = $VBox/HBox/Left/VictorySlot.get_child(0)
	var res: RtsoSafeSelect = $VBox/HBox/Left/ResSlot.get_child(0)
	var beh: RtsoSafeSelect = $VBox/HBox/Left/BehaviorSlot.get_child(0)
	vic.select(clampi(int(ms.get("victory")), 0, VICTORIES.size() - 1))
	$VBox/HBox/Left/Waves.button_pressed = bool(ms.get("waves_enabled"))
	$VBox/HBox/Left/LastManStanding.button_pressed = bool(ms.get("last_man_standing"))
	var pop: RtsoSafeSelect = $VBox/HBox/Left/PopCapSlot.get_child(0)
	for i in range(POP_CAPS.size()):
		if int(POP_CAPS[i][0]) == int(ms.get("pop_cap")):
			pop.select(i)
	var cf: RtsoSafeSelect = $VBox/HBox/Left/CeasefireSlot.get_child(0)
	cf.select(maxi(CEASEFIRES.find(int(ms.get("ceasefire_min"))), 0))
	$VBox/HBox/Left/MapVisRow/RevealMap.button_pressed = bool(ms.get("reveal_map"))
	$VBox/HBox/Left/MapVisRow/ExploreMap.button_pressed = bool(ms.get("explore_map"))
	var wt: RtsoSafeSelect = $VBox/HBox/Left/VictoryTimesRow/WonderTimeSlot.get_child(0)
	var rt: RtsoSafeSelect = $VBox/HBox/Left/VictoryTimesRow/RelicTimeSlot.get_child(0)
	var rc: RtsoSafeSelect = $VBox/HBox/Left/VictoryTimesRow/RelicCountSlot.get_child(0)
	var wi := VICTORY_TIMES.find(int(ms.get("wonder_min")))
	var ri := VICTORY_TIMES.find(int(ms.get("relic_min")))
	var ci := RELIC_COUNTS.find(int(ms.get("relic_count")))
	wt.select(wi if wi >= 0 else VICTORY_TIMES.find(20))
	rt.select(ri if ri >= 0 else VICTORY_TIMES.find(20))
	rc.select(ci if ci >= 0 else RELIC_COUNTS.find(2))
	var msz: RtsoSafeSelect = $VBox/HBox/Left/MapSizeSlot.get_child(0)
	for i in range(MAP_SIZES.size()):
		if int(MAP_SIZES[i][0]) == int(ms.get("map_size")):
			msz.select(i)
	res.select(clampi(int(ms.get("start_res")), 0, 3))
	beh.select(clampi(int(ms.get("ai_behavior")), 0, 3))
	$VBox/HBox/Left/SeedRow/SpinSeed.value = int(ms.get("seed"))
	# Restore map selection by id.
	var want: String = str(ms.get("map_id"))
	for i in range(_filtered.size()):
		if str(_filtered[i]["id"]) == want:
			_sel_map = i
			break
	# Restore slots.
	var types: Array = ms.get("slot_types")
	var civs: Array = ms.get("slot_civs")
	var teams: Array = ms.get("slot_teams")
	if not types.is_empty():
		$VBox/HBox/Right/SlotsBox/SlotCountRow/SpinSlots.value = clampi(types.size(), 2, 8)
		_ensure_slot_rows(int($VBox/HBox/Right/SlotsBox/SlotCountRow/SpinSlots.value))
		for i in range(mini(types.size(), _slot_rows.size())):
			_slot_rows[i]["kind"] = clampi(int(types[i]), 0, 2)
			if i < civs.size():
				_slot_rows[i]["civ"] = clampi(int(civs[i]), 0, CIVS.size() - 1)
			if i < teams.size():
				_slot_rows[i]["team"] = clampi(int(teams[i]), 1, 4)
			if i == 0:
				_slot_rows[i]["kind"] = 0
		refresh_slots()

# ---------------------------------------------------------------- start ---

func _on_back_pressed() -> void:
	MatchSettings.go("res://scenes/menu/main_menu.tscn")

func _collect_setup() -> Dictionary:
	var m := _selected_map()
	var seed := int($VBox/HBox/Left/SeedRow/SpinSeed.value)
	var vic: RtsoSafeSelect = $VBox/HBox/Left/VictorySlot.get_child(0)
	var res: RtsoSafeSelect = $VBox/HBox/Left/ResSlot.get_child(0)
	var beh: RtsoSafeSelect = $VBox/HBox/Left/BehaviorSlot.get_child(0)
	var msz: RtsoSafeSelect = $VBox/HBox/Left/MapSizeSlot.get_child(0)
	var pop: RtsoSafeSelect = $VBox/HBox/Left/PopCapSlot.get_child(0)
	var cf: RtsoSafeSelect = $VBox/HBox/Left/CeasefireSlot.get_child(0)
	var wt: RtsoSafeSelect = $VBox/HBox/Left/VictoryTimesRow/WonderTimeSlot.get_child(0)
	var rt: RtsoSafeSelect = $VBox/HBox/Left/VictoryTimesRow/RelicTimeSlot.get_child(0)
	var rc: RtsoSafeSelect = $VBox/HBox/Left/VictoryTimesRow/RelicCountSlot.get_child(0)
	var n := int($VBox/HBox/Right/SlotsBox/SlotCountRow/SpinSlots.value)
	_ensure_slot_rows(n)
	var types: Array = []
	var civs: Array = []
	var teams: Array = []
	var humans := 0
	var ai_total := 0
	for i in range(n):
		var k := int(_slot_rows[i]["kind"])
		if i == 0:
			k = 0
		if k == 2:
			continue # closed: 0 A.D. drops the slot, so later players move up
		types.append(k)
		civs.append(int(_slot_rows[i]["civ"]))
		teams.append(int(_slot_rows[i]["team"]))
		if k == 0:
			humans += 1
		elif k == 1:
			ai_total += 1
	# AI difficulty: first AI slot's pick, else global medium.
	var diff := 3
	for i in range(n):
		if int(_slot_rows[i]["kind"]) == 1:
			diff = int(_slot_rows[i]["diff"])
			break
	return {"map": m, "seed": seed, "victory": vic.get_selected(),
		"waves": $VBox/HBox/Left/Waves.button_pressed,
		"lms": $VBox/HBox/Left/LastManStanding.button_pressed,
		"pop_cap": int(POP_CAPS[maxi(pop.get_selected(), 0)][0]),
		"ceasefire": int(CEASEFIRES[maxi(cf.get_selected(), 0)]),
		"reveal": $VBox/HBox/Left/MapVisRow/RevealMap.button_pressed,
		"explore": $VBox/HBox/Left/MapVisRow/ExploreMap.button_pressed,
		"wonder_min": int(VICTORY_TIMES[maxi(wt.get_selected(), 0)]),
		"relic_min": int(VICTORY_TIMES[maxi(rt.get_selected(), 0)]),
		"relic_count": int(RELIC_COUNTS[maxi(rc.get_selected(), 0)]),
		"map_size": int(MAP_SIZES[maxi(msz.get_selected(), 0)][0]),
		"res": res.get_selected(), "beh": beh.get_selected(), "diff": diff,
		"types": types, "civs": civs, "teams": teams,
		"humans": humans, "ai": ai_total, "slots": types.size()}

func _on_start_pressed() -> void:
	var st := _collect_setup()
	if st["map"].is_empty():
		_show_error("No map selected.")
		return
	if int(st["humans"]) < 1:
		_show_error("Slot 1 must be Human.")
		return
	if int(st["ai"]) < 1:
		_show_error("Need at least 1 AI opponent.")
		return
	var m: Dictionary = st["map"]
	if str(m["kind"]) == "Scenario" and int(st["slots"]) != int(m["players"]):
		_show_error("'%s' needs exactly %d players (scenario)." % [m["name"], int(m["players"])])
		return
	if not has_node("/root/MatchSettings"):
		_show_error("MatchSettings autoload missing.")
		return
	var ms := get_node("/root/MatchSettings")
	ms.map_id = str(m["id"])
	ms.seed = int(st["seed"])
	ms.victory = int(st["victory"])
	ms.waves_enabled = bool(st["waves"])
	ms.last_man_standing = bool(st["lms"])
	ms.pop_cap = int(st["pop_cap"])
	ms.ceasefire_min = int(st["ceasefire"])
	ms.reveal_map = bool(st["reveal"])
	ms.explore_map = bool(st["explore"])
	ms.wonder_min = int(st["wonder_min"])
	ms.relic_min = int(st["relic_min"])
	ms.relic_count = int(st["relic_count"])
	ms.map_size = int(st["map_size"])
	ms.ai_difficulty = int(st["diff"])
	ms.ai_behavior = int(st["beh"])
	ms.start_res = int(st["res"])
	ms.slot_types = st["types"]
	ms.slot_civs = st["civs"]
	ms.slot_teams = st["teams"]
	ms.player_count = int(st["slots"])
	ms.player_civ = int(st["civs"][0])
	# Legacy back-compat: first AI slot's civ.
	ms.ai_civ = int(st["civs"][1]) if int(st["types"][1]) == 1 else int(st["civs"][0])
	print("[GameSetup] Start — %s seed=%d victory=%d diff=%d beh=%d res=%d slots=%s civs=%s teams=%s" % [
		ms.map_id, ms.seed, ms.victory, ms.ai_difficulty, ms.ai_behavior,
		ms.start_res, str(st["types"]), str(st["civs"]), str(st["teams"])])
	var err := MatchSettings.go("res://scenes/main.tscn")
	if err != OK:
		_show_error("Could not load match scene.")

func _show_error(msg: String) -> void:
	var lbl := $VBox/HBox/Right/BtnStart as Button
	push_warning("[GameSetup] " + msg)
	var err := $VBox/HBox/Right/ErrorLabel as Label
	if err:
		err.text = msg
