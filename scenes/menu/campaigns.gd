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
## A level is marked completed as soon as the player wins it (session
## CampaignSession and default_menu/endgame); the summary then comes back here.

const SetupPage = preload("res://scenes/menu/gamesetup.gd")
const ScenarioStart = preload("res://scenes/menu/scenario_start.gd")
const MSGBOX := preload("res://scenes/common/msgbox.gd")
const GOLD := Color(0.9, 0.75, 0.31)
const GRAY := Color8(128, 128, 128)
## gui/common/setup.xml's named colours "green" and "gray", and the list columns'
## textcolor (CampaignMenu.xml, CampaignSetupPage.xml).
const GREEN := Color8(0, 200, 0)
const LOCKED := Color8(143, 142, 140)
const NAME_COLOR := Color8(172, 172, 212)
const STATUS_COLOR := Color8(192, 192, 192)

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
## Name and Status columns (CampaignMenu.xml levelSelection list_name/list_status); the
## template and run lists use the first column only.
var list: Tree
var info_name: Label
var info_preview: TextureRect
var info_desc: RichTextLabel
var start_button: Button
var back_button: Button
var delete_button: Button
var saved_games_button: Button
var empty_text: Label
var name_dialog: ConfirmationDialog
var name_edit: LineEdit
var confirm: Control # the open delete question (scenes/common/msgbox.gd), null when closed

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

## A row of the list: its name (and, on the level list, its status).
func _add_row(text: String, status := "") -> TreeItem:
	var item := list.create_item(list.get_root())
	item.set_text(0, text)
	item.set_text(1, status)
	if mode != "load":
		item.set_custom_color(0, NAME_COLOR)
		item.set_custom_color(1, STATUS_COLOR)
	return item

## The rows' texts, name and status joined by four spaces when there is a status.
func row_texts() -> Array:
	var out := []
	for item in list.get_root().get_children():
		out.append(item.get_text(0) + ("    " + item.get_text(1) if item.get_text(1) != "" else ""))
	return out

func render() -> void:
	list.clear()
	list.create_item()
	entries.clear()
	var levels := mode != "setup" and mode != "load"
	# The olists' headings: "Name" on the campaign list, "Scenario Name" and "Status"
	# on the level list; the run list (LoadModal) is a plain list.
	list.column_titles_visible = mode != "load"
	list.set_column_title(0, tr("Name") if mode == "setup" else tr("Scenario Name"))
	list.set_column_expand(1, levels)
	list.set_column_custom_minimum_width(1, 0)
	match mode:
		"setup":
			title.text = tr("Campaigns")
			for t in templates:
				_add_row(template_text(t, "Name", str(t["identifier"])))
				entries.append(t)
			start_button.text = tr("Start Campaign")
		"load":
			title.text = tr("Load Campaign")
			for r in runs:
				var cr: CampaignRun = r["run"]
				# BrokenRun.getLabel(true): "<file>.0adcampaign (file cannot be loaded)", in red.
				var item := _add_row("%s (%s)" % [cr.filename + CampaignRun.EXTENSION, tr("file cannot be loaded")] if r["broken"] else cr.label(true))
				if r["broken"]:
					item.set_custom_color(0, Color.RED)
				entries.append(r)
			start_button.text = tr("Load Campaign")
		_:
			title.text = run.label()
			# displayLevelsList: ShowUnavailable lists the levels not yet open with grey
			# names; only "Available" is green.
			for e in run.ordered_levels():
				var level: Dictionary = e["level"]
				var open := run.meets_requirements(level)
				if not open and not run.template.get("ShowUnavailable", false):
					continue
				var status := tr("Completed") if run.is_completed(e["id"]) \
					else (tr("Available") if open else "")
				var item := _add_row(level_name(level), status)
				if not open:
					item.set_custom_color(0, LOCKED)
				elif not run.is_completed(e["id"]):
					item.set_custom_color(1, GREEN)
				entries.append(e["id"])
			start_button.text = tr("Start Scenario")
	# CampaignMenu.xml "Back to Main Menu", LoadModal.xml "Cancel", CampaignSetupPage.xml "Main Menu".
	back_button.text = tr("Cancel") if mode == "load" else tr("Main Menu") if mode == "setup" else tr("Back to Main Menu")
	# Only the run list has an empty text (LoadModal.xml noCampaignsText).
	empty_text.visible = mode == "load" and entries.is_empty()
	empty_text.text = tr("No ongoing campaigns.")
	if selected >= entries.size():
		selected = -1
	if selected >= 0:
		list.get_root().get_child(selected).select(0)
	render_details()

## A template's Name or Description, translateWithContext("Campaign Template", …). RTS0's
## .po files keep context entries under their bare msgid (tools/fetch_0ad_l10n.py), so
## the lookups here take no context.
func template_text(t: Dictionary, key: String, fallback := "") -> String:
	return tr(str(t[key])) if t.has(key) else fallback

func render_details() -> void:
	delete_button.visible = mode == "load"
	saved_games_button.visible = mode == "menu"
	delete_button.disabled = selected < 0
	start_button.disabled = selected < 0
	info_name.text = ""
	info_desc.text = ""
	info_preview.texture = null
	match mode:
		"setup":
			if selected < 0:
				info_name.text = tr("No campaign selected.")
			else:
				var t: Dictionary = entries[selected]
				info_name.text = template_text(t, "Name")
				info_desc.text = template_text(t, "Description")
				# CampaignSetupPage: the template's Image, else 0 A.D.'s "no preview" picture.
				info_preview.texture = _texture(str(t.get("Image", "")))
			if info_preview.texture == null:
				info_preview.texture = no_preview()
		"load":
			# LoadModal.xml: "Name of selected run:" over the run's getLabel().
			info_name.text = tr("Name of selected run:")
			if selected >= 0:
				var r: Dictionary = entries[selected]
				start_button.disabled = r["broken"]
				info_desc.text = run_label(r)
		_:
			if selected < 0:
				info_name.text = tr("No scenario selected")
				return
			var level: Dictionary = run.template["Levels"][entries[selected]]
			info_name.text = level_name(level)
			# getLevelDescription and getLevelPreview: the level's own, else its map's.
			info_desc.text = TranslationServer.translate(str(level["Description"])) if level.has("Description") \
				else tr(str(_map_json(CampaignRun.level_map_id(level)).get("desc", "")))
			info_preview.texture = _level_preview(level)
			start_button.disabled = not run.meets_requirements(level)
	info_preview.visible = info_preview.texture != null

## A level's Preview ("session/icons/mappreview/Introductory_Tutorial.png") is one of
## 0 A.D.'s map previews, which RTS0 keeps as assets/maps/previews/<name>.jpg;
## without one, the map's own preview.
static func _level_preview(level: Dictionary) -> Texture2D:
	if level.has("Preview"):
		var base := str(level["Preview"]).get_file().get_basename().to_lower()
		for ext in [".jpg", ".png"]:
			var t := _texture("res://assets/maps/previews/" + base + ext)
			if t:
				return t
	return SetupPage.map_preview(CampaignRun.level_map_id(level))

## "cropped:0.78,0.59:session/icons/mappreview/nopreview.png": its top-left 400x300 of 512.
static func no_preview() -> Texture2D:
	var t := _texture("res://assets/textures/ui/archive/icons/mappreview/nopreview.png")
	if t == null:
		return null
	var a := AtlasTexture.new()
	a.atlas = t
	a.region = Rect2(0, 0, t.get_width() * 400.0 / 512.0, t.get_height() * 300.0 / 512.0)
	return a

## A saved run's getLabel(): a run that cannot be loaded is "<file>.0adcampaign" (BrokenRun).
static func run_label(r: Dictionary) -> String:
	var cr: CampaignRun = r["run"]
	return cr.filename + CampaignRun.EXTENSION if r["broken"] else cr.label()

static func _texture(p: String) -> Texture2D:
	if p == "" or not ResourceLoader.exists(p):
		return null
	return load(p) as Texture2D

## getLevelName: the level's Name (translated in the "Campaign Template" context), else
## its map's.
static func level_name(level: Dictionary) -> String:
	if level.has("Name"):
		return TranslationServer.translate(str(level["Name"]))
	return TranslationServer.translate(str(_map_json(CampaignRun.level_map_id(level)).get("name", CampaignRun.level_map_id(level))))

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
			name_edit.text = template_text(entries[selected], "Name")
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
	# LoadModal.js messageBox(400, 200, …, "Confirmation", No, Yes).
	confirm = MSGBOX.open(self, 400, 200, tr("Are you sure you want to delete run %s? This cannot be undone.") % run_label(entries[selected]),
		tr("Confirmation"), [tr("No"), tr("Yes")])
	confirm.answered.connect(func(i: int):
		confirm = null
		if i == 1: delete_selected())

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
	if not ScenarioStart.configure(ms, map_id):
		return
	ms.campaign_run = run.filename
	ms.campaign_level = level_id
	run.save()
	ms.go("res://scenes/main.tscn")

## CampaignMenu.loadSavegame: the load page with this run's saves only (SavegameList
## campaignFilter reads MatchSettings.campaign_run); loading one restores its run and level.
func load_savegame() -> CanvasLayer:
	var ms := get_node_or_null("/root/MatchSettings")
	if ms == null or run == null:
		return null
	var before: String = ms.campaign_run
	ms.campaign_run = run.filename
	var d = load("res://scenes/session/savegame_dialog.gd").open(self, "load")
	d.closed.connect(func(): ms.campaign_run = before)
	return d

func back_to_main_menu() -> void:
	if run:
		run.save()
	var ms := get_node_or_null("/root/MatchSettings")
	if ms:
		ms.go("res://scenes/menu/main_menu.tscn")
	else:
		get_tree().change_scene_to_file("res://scenes/menu/main_menu.tscn")

## LoadModal.xml's Cancel has hotkey="cancel": Escape goes back to the main menu.
func _unhandled_input(event: InputEvent) -> void:
	if mode == "load" and confirm == null and event is InputEventKey and event.pressed \
			and "cancel" in RtsoHotkeyConfig.new().fired(event):
		get_viewport().set_input_as_handled()
		back_to_main_menu()

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

	list = Tree.new()
	list.name = "Selection"
	list.columns = 2
	list.hide_root = true
	list.select_mode = Tree.SELECT_ROW
	list.set_column_title(1, tr("Status"))
	list.set_column_title_alignment(0, HORIZONTAL_ALIGNMENT_LEFT)
	list.set_column_title_alignment(1, HORIZONTAL_ALIGNMENT_LEFT)
	list.set_column_expand_ratio(0, 3)
	list.item_selected.connect(func(): select(list.get_selected().get_index()))
	list.item_activated.connect(func(): select(list.get_selected().get_index()); start_pressed())
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
	back_button = _button("Back to Main Menu", "BackButton", back_to_main_menu)
	bottom.add_child(back_button)
	delete_button = _button("Delete", "DeleteButton", delete_pressed)
	bottom.add_child(delete_button)
	saved_games_button = _button("Saved Games", "SavedGamesButton", load_savegame)
	bottom.add_child(saved_games_button)
	start_button = _button("Start", "StartButton", start_pressed)
	bottom.add_child(start_button)

	# NewCampaignModal: the run's name, the campaign's name to start with.
	name_dialog = ConfirmationDialog.new()
	name_dialog.name = "NewCampaignModal"
	name_dialog.title = tr("Start a campaign")
	name_dialog.ok_button_text = tr("Start Campaign")
	name_dialog.cancel_button_text = tr("Cancel")
	var name_box := VBoxContainer.new()
	name_box.add_child(_label(tr("Please enter the name of your new campaign run:")))
	name_edit = LineEdit.new()
	if has_node("/root/MatchSettings"):
		get_node("/root/MatchSettings").prepare_line_edit(name_edit)
	name_box.add_child(name_edit)
	name_dialog.add_child(name_box)
	# onTextEdit: Start needs a name that is not empty; the name is kept as typed.
	name_edit.text_changed.connect(func(t): name_dialog.get_ok_button().disabled = t == "")
	name_edit.text_submitted.connect(func(t): if t != "": name_dialog.hide(); create_run(t))
	name_dialog.confirmed.connect(func(): create_run(name_edit.text))
	add_child(name_dialog)

