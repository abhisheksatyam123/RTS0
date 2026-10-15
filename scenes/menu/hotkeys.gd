extends Control
## Hotkey editor, a port of 0 A.D. gui/hotkeys/ (HotkeysPage.js, HotkeyPicker.js,
## HotkeyMetadata.js; GPL-2.0-or-later). Lists every hotkey by category from
## data/hotkeys/spec/*.json (0 A.D.'s gui/hotkeys/spec plus RTS0's own), lets
## the player give each up to four combinations, warns about conflicts and
## saves the changes to user://config/user.cfg.

const SPEC_DIR := "res://data/hotkeys/spec/"
const DEFAULT_CATEGORY := "other"
const HOLD_TO_REGISTER := 2.0 # HotkeyPicker: hold a combination 2 s to register it
const SLOTS := 4

## Opened over a match from the session menu (MenuButtons Hotkeys,
## Engine.OpenChildPage): Close returns to the game instead of the options page.
signal closed
var overlay := false

var cfg := RtsoHotkeyConfig.new()
var meta_hotkeys := {} # name -> {name, desc, order, categories}
var categories := {} # key -> {name, desc, hotkeys: []}
var category_keys: Array = []
var hotkeys := {} # name -> PackedStringArray, edited copy
var dirty := false
var pending := {} # hotkeys edited since the last save

var filter_select
var text_filter: LineEdit
var tree: Tree
var save_btn: Button
var dialog: PanelContainer

# Picker
var picker: Control
var picker_hint: Label
var picker_name := ""
var picker_title: Label
var picker_desc: Label
var picker_combos: Array = []
var slot_labels: Array = []
var slot_conflicts: Array = []
var slot_delete: Array = []
var entering := -1
var held_keys: PackedStringArray = []
var held_since := 0.0
var picker_buttons: Array = []

func _ready() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	_parse_spec()
	_build_ui()
	_setup_data()
	_setup_filters()
	_setup_list()

# HotkeyMetadata.parseSpec
func _parse_spec() -> void:
	var cats := {DEFAULT_CATEGORY: {"name": "Other Hotkeys", "desc": "Other Hotkeys"}}
	var order := 0
	var files := DirAccess.get_files_at(SPEC_DIR)
	files.sort()
	for f in files:
		if not f.ends_with(".json"):
			continue
		var data = JSON.parse_string(FileAccess.get_file_as_string(SPEC_DIR + f))
		if typeof(data) != TYPE_DICTIONARY:
			continue
		for c in data.get("categories", {}):
			cats[c] = data["categories"][c]
		for h in data.get("hotkeys", {}):
			var e: Dictionary = data["hotkeys"][h]
			e["order"] = order
			order += 1
			e["categories"] = e.get("categories", [DEFAULT_CATEGORY])
			meta_hotkeys[h.to_lower()] = e # the hotkey table keeps names lower-case
		for c in data.get("mapped_hotkeys", {}):
			for h in data["mapped_hotkeys"][c]:
				var e: Dictionary = data["mapped_hotkeys"][c][h]
				e["order"] = order
				order += 1
				e["categories"] = [c]
				meta_hotkeys[h.to_lower()] = e
	var keys := cats.keys()
	keys.sort_custom(func(a, b):
		if a == DEFAULT_CATEGORY or b == DEFAULT_CATEGORY:
			return b == DEFAULT_CATEGORY
		var oa = cats[a].get("order", null)
		var ob = cats[b].get("order", null)
		if oa == null or ob == null:
			return oa == null
		return oa < ob)
	for k in keys:
		categories[k] = cats[k].duplicate()

# HotkeysPage.setupHotkeyData
func _setup_data() -> void:
	hotkeys = cfg.get_hotkey_map()
	for h in meta_hotkeys:
		if not hotkeys.has(h):
			hotkeys[h] = PackedStringArray()
	for c in categories:
		categories[c]["hotkeys"] = []
	for h in hotkeys:
		if meta_hotkeys.has(h):
			for c in meta_hotkeys[h]["categories"]:
				if categories.has(c):
					categories[c]["hotkeys"].append(h)
		else:
			categories[DEFAULT_CATEGORY]["hotkeys"].append(h)
	category_keys = []
	for c in categories:
		categories[c]["hotkeys"].sort_custom(func(a, b):
			if not meta_hotkeys.has(a) or not meta_hotkeys.has(b):
				return meta_hotkeys.has(a) and not meta_hotkeys.has(b) or (not meta_hotkeys.has(a) and not meta_hotkeys.has(b) and a < b)
			return meta_hotkeys[a]["order"] < meta_hotkeys[b]["order"])
		if not categories[c]["hotkeys"].is_empty():
			category_keys.append(c)

func _display_name(h: String) -> String:
	return meta_hotkeys[h]["name"] if meta_hotkeys.has(h) else h

## formatHotkeyCombinations: the names 0.28 shows (KeyName.cpp), shortest first.
func _format(combos) -> String:
	var parts: Array = []
	for c in combos:
		parts.append(cfg.display_combo(str(c)))
	parts.sort_custom(func(a, b): return a.length() < b.length())
	return ", ".join(PackedStringArray(parts))

## hotkeys.xml: a 700×688 ModernDialog over a translucent background, with
## "Category:" and "Filter:" above the Name / Mapping list.
func _build_ui() -> void:
	theme = preload("res://scenes/common/modern_theme.gd").theme()
	var bg := ColorRect.new()
	bg.color = Color(0, 0, 0, 0.5) # BackgroundTranslucent
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(bg)
	var center := CenterContainer.new()
	center.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(center)
	dialog = PanelContainer.new()
	dialog.name = "HotkeysDialog"
	center.add_child(dialog)
	_fit_dialog()
	get_viewport().size_changed.connect(_fit_dialog)
	var margin := MarginContainer.new()
	for side in ["left", "right", "top", "bottom"]:
		margin.add_theme_constant_override("margin_" + side, 24)
	dialog.add_child(margin)
	var vb := VBoxContainer.new()
	vb.add_theme_constant_override("separation", 8)
	margin.add_child(vb)
	var title := Label.new()
	title.text = tr("Hotkeys")
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title.add_theme_font_size_override("font_size", 20)
	vb.add_child(title)

	var top := HBoxContainer.new()
	vb.add_child(top)
	var cat_label := Label.new()
	cat_label.text = tr("Category:")
	top.add_child(cat_label)
	filter_select = RtsoSafeSelect.new()
	filter_select.custom_minimum_size = Vector2(218, 26)
	filter_select.item_selected.connect(func(_i): _setup_list())
	top.add_child(filter_select)
	var gap := Control.new()
	gap.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	top.add_child(gap)
	var filter_label := Label.new()
	filter_label.text = tr("Filter:")
	top.add_child(filter_label)
	text_filter = LineEdit.new()
	text_filter.custom_minimum_size = Vector2(168, 26)
	MatchSettings.prepare_line_edit(text_filter)
	text_filter.text_changed.connect(func(_t): _setup_list())
	top.add_child(text_filter)

	tree = Tree.new()
	tree.columns = 2
	tree.hide_root = true
	tree.column_titles_visible = true
	tree.set_column_title(0, tr("Name"))
	tree.set_column_title(1, tr("Mapping"))
	tree.set_column_expand_ratio(0, 3)
	tree.set_column_expand_ratio(1, 2)
	tree.size_flags_vertical = Control.SIZE_EXPAND_FILL
	tree.item_activated.connect(_on_item_activated) # onMouseLeftDoubleClickItem
	vb.add_child(tree)

	var bottom := HBoxContainer.new()
	vb.add_child(bottom)
	var reset_btn := Button.new()
	reset_btn.text = tr("Reset")
	reset_btn.tooltip_text = tr("Resets user settings to their game default")
	reset_btn.custom_minimum_size = Vector2(156, 28)
	reset_btn.pressed.connect(_on_reset_all)
	bottom.add_child(reset_btn)
	var spacer := Control.new()
	spacer.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	bottom.add_child(spacer)
	save_btn = Button.new()
	save_btn.text = tr("Save")
	save_btn.disabled = true
	save_btn.tooltip_text = tr("Saves changes")
	save_btn.custom_minimum_size = Vector2(156, 28)
	save_btn.pressed.connect(_on_save)
	bottom.add_child(save_btn)
	var close_btn := Button.new()
	close_btn.text = tr("Close")
	close_btn.tooltip_text = tr("Unsaved changes will be lost")
	close_btn.custom_minimum_size = Vector2(156, 28)
	close_btn.pressed.connect(_on_close)
	bottom.add_child(close_btn)
	_build_picker()

func _fit_dialog() -> void:
	var vp := get_viewport_rect().size
	dialog.custom_minimum_size = Vector2(minf(700, vp.x - 16), minf(688, vp.y - 16))

func _setup_filters() -> void:
	filter_select.clear()
	filter_select.add_item("All Hotkeys")
	filter_select.set_item_tooltip(0, tr("All available hotkeys.")) # HotkeysPage.onFilterHover
	for c in category_keys:
		filter_select.add_item(categories[c]["name"])
		filter_select.set_item_tooltip(filter_select.get_item_count() - 1, tr(str(categories[c].get("desc", ""))))
	filter_select.select(0)

func _visible_hotkeys() -> Array:
	var list: Array = []
	var sel: int = filter_select.get_selected()
	if sel > 0:
		list = categories[category_keys[sel - 1]]["hotkeys"]
	else:
		for c in category_keys:
			list += categories[c]["hotkeys"]
	var f := text_filter.text.to_lower()
	if f == "":
		return list
	return list.filter(func(h): return h.find(f) >= 0 or _display_name(h).to_lower().find(f) >= 0)

# HotkeysPage.setupHotkeyList
func _setup_list() -> void:
	tree.clear()
	var root := tree.create_item()
	for h in _visible_hotkeys():
		var it := tree.create_item(root)
		it.set_text(0, _display_name(h))
		it.set_text(1, _format(hotkeys.get(h, [])))
		it.set_metadata(0, h)
		# HotkeysPage.onHotkeyHover: the description, else "No tooltip available."
		var desc: String = str(meta_hotkeys[h].get("desc", "")) if meta_hotkeys.has(h) else ""
		var tip := tr(desc) if desc != "" else tr("No tooltip available.")
		it.set_tooltip_text(0, tip)
		it.set_tooltip_text(1, tip)

func _on_item_activated() -> void:
	var it := tree.get_selected()
	if it:
		open_picker(it.get_metadata(0))

# HotkeyPicker (hotkeys.xml hotkeyPicker): a 600×410 dialog over a translucent background.
func _build_picker() -> void:
	picker = ColorRect.new()
	picker.name = "HotkeyPicker"
	(picker as ColorRect).color = Color(0, 0, 0, 0.5)
	picker.set_anchors_preset(Control.PRESET_FULL_RECT)
	picker.visible = false
	add_child(picker)
	var center := CenterContainer.new()
	center.set_anchors_preset(Control.PRESET_FULL_RECT)
	picker.add_child(center)
	var panel := PanelContainer.new()
	panel.custom_minimum_size = Vector2(600, 410)
	center.add_child(panel)
	var margin := MarginContainer.new()
	for side in ["left", "right", "top", "bottom"]:
		margin.add_theme_constant_override("margin_" + side, 12)
	panel.add_child(margin)
	var vb := VBoxContainer.new()
	vb.add_theme_constant_override("separation", 6)
	margin.add_child(vb)
	picker_title = Label.new()
	picker_title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	picker_title.add_theme_font_size_override("font_size", 20)
	vb.add_child(picker_title)
	picker_desc = Label.new() # hotkeyPickerDescHotkey
	picker_desc.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	picker_desc.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	vb.add_child(picker_desc)
	picker_hint = Label.new() # hotkeyPickerDesc
	picker_hint.text = tr("Click on any mapping to modify it.\n You may have up to 4 different hotkeys.")
	picker_hint.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	vb.add_child(picker_hint)
	for i in SLOTS:
		var row := HBoxContainer.new()
		vb.add_child(row)
		var nb := Label.new()
		nb.text = "#%d" % i
		nb.custom_minimum_size = Vector2(22, 0)
		row.add_child(nb)
		var btn := Button.new()
		btn.custom_minimum_size = Vector2(320, 30)
		btn.tooltip_text = tr("Click to set the hotkey")
		btn.pressed.connect(func(): _start_entering(i))
		row.add_child(btn)
		slot_labels.append(btn)
		var del := Button.new()
		del.text = "✕" # sprite crossRed
		del.add_theme_color_override("font_color", Color(0.9, 0.15, 0.1))
		del.add_theme_color_override("font_hover_color", Color(1.0, 0.3, 0.2))
		del.custom_minimum_size = Vector2(24, 24)
		del.tooltip_text = tr("Click to delete the hotkey")
		del.pressed.connect(func(): picker_combos[i] = ""; _render_picker())
		row.add_child(del)
		slot_delete.append(del)
		var conf := Label.new()
		conf.add_theme_color_override("font_color", Color8(255, 153, 0))
		conf.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		conf.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		conf.custom_minimum_size = Vector2(180, 0)
		row.add_child(conf)
		slot_conflicts.append(conf)
	var fill := Control.new()
	fill.size_flags_vertical = Control.SIZE_EXPAND_FILL
	vb.add_child(fill)
	var buttons := HBoxContainer.new()
	vb.add_child(buttons)
	for spec in [["Reset", "Resets user settings to their game default", _on_picker_reset],
			["", "", null],
			["Accept", "Change the hotkeys and close", func(): _close_picker(true)],
			["Cancel", "The hotkeys will not be modified", func(): _close_picker(false)]]:
		if spec[2] == null:
			var gap := Control.new()
			gap.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			buttons.add_child(gap)
			continue
		var b := Button.new()
		b.text = tr(spec[0])
		b.tooltip_text = tr(spec[1])
		b.custom_minimum_size = Vector2(156, 28)
		b.pressed.connect(spec[2])
		buttons.add_child(b)
		picker_buttons.append(b)

func open_picker(h: String) -> void:
	picker_name = h
	picker_title.text = _display_name(h)
	picker_desc.text = tr(str(meta_hotkeys[h].get("desc", ""))) if meta_hotkeys.has(h) else ""
	picker_desc.visible = meta_hotkeys.has(h)
	picker_combos = []
	for c in hotkeys.get(h, []):
		picker_combos.append(c)
	while picker_combos.size() < SLOTS:
		picker_combos.append("")
	entering = -1
	picker.visible = true
	_render_picker()

func _start_entering(i: int) -> void:
	entering = i
	held_keys = []
	_render_picker()

func _render_picker() -> void:
	for i in SLOTS:
		if i == entering:
			slot_labels[i].text = cfg.display_combo(cfg.format_keys(held_keys)) + tr(" (hold to register)") if held_keys.size() > 0 else tr("Enter new Hotkey, hold to register.")
		else:
			slot_labels[i].text = cfg.display_combo(picker_combos[i]) if picker_combos[i] != "" else tr("(unused)")
		slot_delete[i].visible = picker_combos[i] != ""
		var conflicts: Array = []
		if picker_combos[i] != "":
			for n in cfg.get_conflicts(picker_combos[i]):
				if n != picker_name:
					conflicts.append(_display_name(n))
		slot_conflicts[i].text = tr("May conflict with: ") + ", ".join(conflicts) if conflicts.size() > 0 else ""
		slot_conflicts[i].visible = conflicts.size() > 0
	for b in picker_buttons:
		b.disabled = entering != -1

## hotkey="cancel" (Escape) on the picker's Cancel and the page's Close button;
## while a combination is being entered every key belongs to it.
func _unhandled_input(event: InputEvent) -> void:
	if entering >= 0 or not (event is InputEventKey and event.pressed and not event.echo):
		return
	if not "cancel" in cfg.fired(event):
		return
	get_viewport().set_input_as_handled()
	if picker.visible:
		_close_picker(false)
	else:
		_on_close()

func _input(event: InputEvent) -> void:
	if not picker.visible or entering < 0:
		return
	if not (event is InputEventKey or event is InputEventMouseButton):
		return
	if event is InputEventKey and event.is_echo():
		return
	var key: String = cfg.event_key_name(event)
	if key == "":
		return
	get_viewport().set_input_as_handled()
	if event.is_pressed():
		if not held_keys.has(key):
			held_keys.append(key)
			held_since = Time.get_ticks_msec() / 1000.0
	else:
		var idx := held_keys.find(key)
		if idx >= 0:
			held_keys.remove_at(idx)
			held_since = Time.get_ticks_msec() / 1000.0
	_render_picker()

func _process(_delta: float) -> void:
	if entering < 0 or held_keys.is_empty():
		return
	if Time.get_ticks_msec() / 1000.0 - held_since >= HOLD_TO_REGISTER:
		register_combo(cfg.format_keys(held_keys))

## Ends the entry in progress with this combination (picker.onCombination).
func register_combo(combo: String) -> void:
	if entering < 0:
		return
	picker_combos[entering] = combo
	entering = -1
	held_keys = []
	_render_picker()

## HotkeyPicker Reset: ConfigDB_RemoveValueAndSave drops this hotkey's user value and
## saves straight away; the page's other unsaved edits stay pending.
func _on_picker_reset() -> void:
	var defaults: PackedStringArray = cfg.get_default(picker_name)
	cfg.reload() # user.cfg as saved, without the page's pending edits
	cfg.set_hotkey(picker_name, defaults)
	cfg.save_user()
	pending.erase(picker_name)
	for h in pending:
		cfg.set_hotkey(h, hotkeys[h])
	hotkeys[picker_name] = defaults
	picker_combos = []
	for c in defaults:
		picker_combos.append(c)
	while picker_combos.size() < SLOTS:
		picker_combos.append("")
	_render_picker()
	_setup_list()

func _close_picker(save: bool) -> void:
	picker.visible = false
	entering = -1
	if not save:
		return
	var combos := PackedStringArray()
	for c in picker_combos:
		if c != "":
			combos.append(c)
	hotkeys[picker_name] = combos
	# Live table too, so conflicts reflect the edit straight away.
	cfg.set_hotkey(picker_name, combos)
	pending[picker_name] = true
	dirty = true
	save_btn.disabled = false
	_setup_list()

func _on_save() -> void:
	var live: Dictionary = cfg.get_hotkey_map()
	for h in hotkeys:
		if not live.has(h) and hotkeys[h].is_empty():
			continue # in the spec but not bound anywhere (0 A.D. engine-only hotkeys)
		cfg.set_hotkey(h, hotkeys[h])
	cfg.save_user()
	dirty = false
	pending.clear()
	save_btn.disabled = true

## HotkeysPage.js resetUserHotkeys: only after the question.
func _on_reset_all() -> void:
	var box = load("res://scenes/common/msgbox.gd").open(self, 400, 200,
			"Reset all hotkeys to default values?\nWARNING: this cannot be reversed.", "Confirmation", ["No", "Yes"])
	box.answered.connect(func(i: int):
		if i == 1:
			_reset_all())

func _reset_all() -> void:
	cfg.reset_user()
	dirty = false
	pending.clear()
	save_btn.disabled = true
	_setup_data()
	_setup_list()

func _on_close() -> void:
	if dirty:
		cfg.reload() # drop unsaved edits from the live table
	if overlay:
		closed.emit()
		queue_free()
		return
	MatchSettings.go("res://scenes/menu/options.tscn")

## The page over the running scene, on its own layer, working while the game is paused.
static func open_overlay(parent: Node) -> Control:
	var layer := CanvasLayer.new()
	layer.layer = 30
	layer.process_mode = Node.PROCESS_MODE_ALWAYS
	parent.add_child(layer)
	var page: Control = load("res://scenes/menu/hotkeys.tscn").instantiate()
	page.overlay = true
	page.process_mode = Node.PROCESS_MODE_ALWAYS
	layer.add_child(page)
	page.closed.connect(layer.queue_free)
	return page
