extends Control
## Campaign pages, ported from 0 A.D. gui/campaigns (setup/CampaignSetupPage.js,
## new_modal/NewCampaignModal.js, load_modal/LoadModal.js,
## default_menu/CampaignMenu.js; GPL-2.0-or-later). One scene in three modes:
##   "setup": the campaign templates; Start asks for the run's name, creates it
##            and opens its menu (New Campaign);
##   "load":  the saved runs, to open or delete (Load Campaign);
##   "menu":  the current run's levels with their status (Completed, Available,
##            greyed out until what they Require is done), the selected level's
##            name, preview and description, and Start to play it.
## A level won marks it completed when the summary page closes, which then
## comes back here (default_menu/endgame).

const SetupPage = preload("res://scenes/menu/gamesetup.gd")
const GOLD := Color(0.9, 0.75, 0.31)
const GRAY := Color8(128, 128, 128)
const GREEN := Color8(0, 200, 0)

## Which page to show; set before changing to this scene.
static var open_mode := "menu"

var mode := "menu"
var run: CampaignRun
var templates: Array = []
var runs: Array = []
## The list's entries: template dictionaries, saved runs or level ids.
var entries: Array = []
var selected := -1

var title: Label
var list: ItemList
var info_name: Label
var info_preview: TextureRect
var info_desc: RichTextLabel
var start_button: Button
var delete_button: Button
var empty_text: Label
var name_dialog: ConfirmationDialog
var name_edit: LineEdit
var confirm: ConfirmationDialog

func _ready() -> void:
	mode = open_mode
	open_mode = "menu"
	_build()
	match mode:
		"setup":
			templates = CampaignRun.available_templates()
		"load":
			runs = CampaignRun.saved_runs()
		_:
			run = CampaignRun.current_run()
			if run == null:
				push_error("[Campaigns] No campaign run to show.")
				back_to_main_menu.call_deferred()
				return
	render()

# --- Content ------------------------------------------------------------------------

func render() -> void:
	list.clear()
	entries.clear()
	match mode:
		"setup":
			title.text = "Campaigns"
			for t in templates:
				list.add_item(str(t.get("Name", t["identifier"])))
				entries.append(t)
			start_button.text = "Start Campaign"
		"load":
			title.text = "Load Campaign"
			for r in runs:
				var cr: CampaignRun = r["run"]
				var idx := list.add_item(cr.filename + CampaignRun.EXTENSION + " (file cannot be loaded)" if r["broken"] else cr.label())
				if r["broken"]:
					list.set_item_custom_fg_color(idx, Color.RED)
				entries.append(r)
			start_button.text = "Load Campaign"
		_:
			title.text = run.label()
			# displayLevelsList: ShowUnavailable lists the levels not yet open, greyed out.
			for e in run.ordered_levels():
				var level: Dictionary = e["level"]
				var open := run.meets_requirements(level)
				if not open and not run.template.get("ShowUnavailable", false):
					continue
				var status := "Completed" if run.is_completed(e["id"]) else ("Available" if open else "")
				var idx := list.add_item(level_name(level) + ("    " + status if status != "" else ""))
				if not open:
					list.set_item_custom_fg_color(idx, GRAY)
				elif status == "Available":
					list.set_item_custom_fg_color(idx, GREEN)
				entries.append(e["id"])
			start_button.text = "Start"
	empty_text.visible = entries.is_empty()
	empty_text.text = "No saved campaigns." if mode == "load" else "No campaigns." if mode == "setup" else "No levels."
	if selected >= entries.size():
		selected = -1
	if selected >= 0:
		list.select(selected)
	render_details()

func render_details() -> void:
	delete_button.visible = mode == "load"
	delete_button.disabled = selected < 0
	start_button.disabled = selected < 0
	info_name.text = ""
	info_desc.text = ""
	info_preview.texture = null
	match mode:
		"setup":
			if selected < 0:
				info_name.text = "No campaign selected."
				return
			var t: Dictionary = entries[selected]
			info_name.text = str(t.get("Name", ""))
			info_desc.text = str(t.get("Description", ""))
		"load":
			if selected < 0:
				return
			var r: Dictionary = entries[selected]
			start_button.disabled = r["broken"]
			info_name.text = (r["run"] as CampaignRun).label()
		_:
			if selected < 0:
				return
			var level: Dictionary = run.template["Levels"][entries[selected]]
			info_name.text = level_name(level)
			info_desc.text = str(level.get("Description", ""))
			info_preview.texture = SetupPage.map_preview(CampaignRun.level_map_id(level))
			start_button.disabled = not run.meets_requirements(level)
	info_preview.visible = info_preview.texture != null

## getLevelName: the level's Name, else its map's.
static func level_name(level: Dictionary) -> String:
	if level.has("Name"):
		return str(level["Name"])
	return str(_map_json(CampaignRun.level_map_id(level)).get("name", CampaignRun.level_map_id(level)))

static func _map_json(map_id: String) -> Dictionary:
	var p := "res://data/maps/%s.json" % map_id
	if not FileAccess.file_exists(p):
		return {}
	var f := FileAccess.open(p, FileAccess.READ)
	var parsed: Variant = JSON.parse_string(f.get_as_text()) if f else null
	return parsed if typeof(parsed) == TYPE_DICTIONARY else {}

# --- Actions --------------------------------------------------------------------------

func select(index: int) -> void:
	selected = index
	render_details()

func start_pressed() -> void:
	if selected < 0:
		return
	match mode:
		"setup":
			name_edit.text = str(entries[selected].get("Name", ""))
			name_dialog.popup_centered(Vector2i(420, 120))
			name_edit.grab_focus()
		"load":
			if not entries[selected]["broken"]:
				open_run(entries[selected]["run"])
		_:
			start_level(entries[selected])

## createAndStartCampaign.
func create_run(description: String) -> CampaignRun:
	var t: Dictionary = entries[selected]
	var r := CampaignRun.new(CampaignRun.new_filename(str(t["identifier"])))
	r.set_template(t).set_meta_description(description).save().set_current()
	open_run(r)
	return r

func open_run(r: CampaignRun) -> void:
	r.set_current()
	mode = "menu"
	run = r
	selected = -1
	render()

func delete_pressed() -> void:
	if mode != "load" or selected < 0:
		return
	confirm.dialog_text = "Are you sure you want to delete run %s? This cannot be undone." % list.get_item_text(selected)
	confirm.popup_centered(Vector2i(420, 140))

func delete_selected() -> void:
	if mode != "load" or selected < 0:
		return
	(entries[selected]["run"] as CampaignRun).destroy()
	runs.remove_at(selected)
	selected = -1
	render()

## startScenario: the level's scenario with its own players, the local player in
## the first seat, cheats on, and the run and level kept for the end of the game.
func start_level(level_id: String) -> void:
	var level: Dictionary = run.template["Levels"].get(level_id, {})
	if level.is_empty() or not run.meets_requirements(level):
		return
	var map_id := CampaignRun.level_map_id(level)
	var ms := get_node_or_null("/root/MatchSettings")
	if ms == null:
		push_error("[Campaigns] MatchSettings autoload missing.")
		return
	var players: Array = _map_json(map_id).get("players", [])
	var n := maxi(1, players.size())
	var types: Array = []
	var civs: Array = []
	for i in n:
		types.append(0 if i == 0 else 1)
		civs.append(int(players[i].get("civ", 0)) if i < players.size() and typeof(players[i]) == TYPE_DICTIONARY else 0)
	ms.map_id = map_id
	ms.seed = 0
	ms.victory = 0
	ms.waves_enabled = false
	ms.cheats = true
	ms.player_count = n
	ms.slot_types = types
	ms.slot_civs = civs
	ms.slot_teams = range(n).map(func(_i): return 0)
	ms.slot_names = []
	ms.slot_colors = []
	ms.player_civ = civs[0]
	ms.ai_civ = civs[1] if n > 1 else civs[0]
	ms.campaign_run = run.filename
	ms.campaign_level = level_id
	run.save()
	ms.go("res://scenes/main.tscn")

func back_to_main_menu() -> void:
	if run:
		run.save()
	var ms := get_node_or_null("/root/MatchSettings")
	if ms:
		ms.go("res://scenes/menu/main_menu.tscn")
	else:
		get_tree().change_scene_to_file("res://scenes/menu/main_menu.tscn")

# --- Page -------------------------------------------------------------------------------

func _label(text: String, size := 14, color := Color.WHITE) -> Label:
	var l := Label.new()
	l.text = text
	l.add_theme_font_size_override("font_size", size)
	l.add_theme_color_override("font_color", color)
	return l

func _anchor(c: Control, l: float, t: float, r: float, b: float, ol := 0.0, ot := 0.0, or_ := 0.0, ob := 0.0) -> Control:
	c.anchor_left = l
	c.anchor_top = t
	c.anchor_right = r
	c.anchor_bottom = b
	c.offset_left = ol
	c.offset_top = ot
	c.offset_right = or_
	c.offset_bottom = ob
	return c

func _button(text: String, name_: String, on_press: Callable) -> Button:
	var b := Button.new()
	b.text = text
	b.name = name_
	b.custom_minimum_size = Vector2(160, 32)
	b.pressed.connect(on_press)
	return b

func _build() -> void:
	title = _label("", 20, GOLD)
	title.name = "CampaignTitle"
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	add_child(_anchor(title, 0, 0, 1, 0, 20, 8, -20, 40))

	list = ItemList.new()
	list.name = "Selection"
	list.item_selected.connect(select)
	list.item_activated.connect(func(i): select(i); start_pressed())
	add_child(_anchor(list, 0, 0, 0.45, 1, 20, 50, -10, -60))
	empty_text = _label("", 14, GRAY)
	empty_text.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	empty_text.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(_anchor(empty_text, 0, 0, 0.45, 0, 20, 70, -10, 100))

	var info := VBoxContainer.new()
	info.add_theme_constant_override("separation", 8)
	add_child(_anchor(info, 0.45, 0, 1, 1, 10, 50, -20, -60))
	info_name = _label("", 18, GOLD)
	info_name.name = "Name"
	info_name.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	info.add_child(info_name)
	info_preview = TextureRect.new()
	info_preview.name = "Preview"
	info_preview.custom_minimum_size = Vector2(400, 300)
	info_preview.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	info_preview.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	info.add_child(info_preview)
	info_desc = RichTextLabel.new()
	info_desc.name = "Description"
	info_desc.size_flags_vertical = Control.SIZE_EXPAND_FILL
	info_desc.add_theme_font_size_override("normal_font_size", 14)
	info.add_child(info_desc)

	var bottom := HBoxContainer.new()
	bottom.alignment = BoxContainer.ALIGNMENT_END
	bottom.add_theme_constant_override("separation", 12)
	add_child(_anchor(bottom, 0, 1, 1, 1, 20, -50, -20, -12))
	bottom.add_child(_button("Main Menu", "BackButton", back_to_main_menu))
	delete_button = _button("Delete", "DeleteButton", delete_pressed)
	bottom.add_child(delete_button)
	start_button = _button("Start", "StartButton", start_pressed)
	bottom.add_child(start_button)

	# NewCampaignModal: the run's name, the campaign's name to start with.
	name_dialog = ConfirmationDialog.new()
	name_dialog.name = "NewCampaignModal"
	name_dialog.title = "Name your campaign run"
	name_edit = LineEdit.new()
	if has_node("/root/MatchSettings"):
		get_node("/root/MatchSettings").prepare_line_edit(name_edit)
	name_dialog.add_child(name_edit)
	name_edit.text_changed.connect(func(t): name_dialog.get_ok_button().disabled = t.strip_edges() == "")
	name_edit.text_submitted.connect(func(t): if t.strip_edges() != "": name_dialog.hide(); create_run(t.strip_edges()))
	name_dialog.confirmed.connect(func(): create_run(name_edit.text.strip_edges()))
	add_child(name_dialog)

	confirm = ConfirmationDialog.new()
	confirm.name = "DeleteConfirmation"
	confirm.title = "Confirmation"
	confirm.ok_button_text = "Yes"
	confirm.cancel_button_text = "No"
	confirm.confirmed.connect(delete_selected)
	add_child(confirm)
