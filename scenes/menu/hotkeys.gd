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

var filter_select
var text_filter: LineEdit
var tree: Tree
var save_btn: Button
var desc_label: Label

# Picker
var picker: PanelContainer
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

func _format(combos) -> String:
	var parts: PackedStringArray = []
	for c in combos:
		parts.append(c)
	return ", ".join(parts)

func _build_ui() -> void:
	theme = preload("res://scenes/common/modern_theme.gd").theme()
	var bg := ColorRect.new()
	bg.color = Color(0.08, 0.07, 0.06)
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(bg)
	var margin := MarginContainer.new()
	margin.set_anchors_preset(Control.PRESET_FULL_RECT)
	for side in ["left", "right", "top", "bottom"]:
		margin.add_theme_constant_override("margin_" + side, 24)
	add_child(margin)
	var vb := VBoxContainer.new()
	vb.add_theme_constant_override("separation", 8)
	margin.add_child(vb)
	var title := Label.new()
	title.text = "Hotkeys"
	title.add_theme_font_size_override("font_size", 26)
	vb.add_child(title)

	var top := HBoxContainer.new()
	vb.add_child(top)
	filter_select = RtsoSafeSelect.new()
	filter_select.custom_minimum_size = Vector2(260, 30)
	filter_select.item_selected.connect(func(_i): _setup_list())
	top.add_child(filter_select)
	text_filter = LineEdit.new()
	text_filter.placeholder_text = "Filter"
	text_filter.custom_minimum_size = Vector2(240, 30)
	MatchSettings.prepare_line_edit(text_filter)
	text_filter.text_changed.connect(func(_t): _setup_list())
	top.add_child(text_filter)
	var hint := Label.new()
	hint.text = "Double-click a hotkey to change it."
	hint.modulate = Color(0.8, 0.8, 0.7)
	top.add_child(hint)

	tree = Tree.new()
	tree.columns = 2
	tree.hide_root = true
	tree.column_titles_visible = true
	tree.set_column_title(0, "Action")
	tree.set_column_title(1, "Key")
	tree.set_column_expand_ratio(0, 3)
	tree.set_column_expand_ratio(1, 2)
	tree.size_flags_vertical = Control.SIZE_EXPAND_FILL
	tree.item_activated.connect(_on_item_activated)
	tree.item_selected.connect(_on_item_selected)
	vb.add_child(tree)

	desc_label = Label.new()
	desc_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	desc_label.modulate = Color(0.85, 0.85, 0.75)
	vb.add_child(desc_label)

	var bottom := HBoxContainer.new()
	bottom.alignment = BoxContainer.ALIGNMENT_END
	vb.add_child(bottom)
	var reset_btn := Button.new()
	reset_btn.text = "Reset"
	reset_btn.tooltip_text = "Resets user settings to their game default"
	reset_btn.pressed.connect(_on_reset_all)
	bottom.add_child(reset_btn)
	save_btn = Button.new()
	save_btn.text = "Save"
	save_btn.disabled = true
	save_btn.tooltip_text = "Saves changes"
	save_btn.pressed.connect(_on_save)
	bottom.add_child(save_btn)
	var close_btn := Button.new()
	close_btn.text = "Close"
	close_btn.tooltip_text = "Unsaved changes will be lost"
	close_btn.pressed.connect(_on_close)
	bottom.add_child(close_btn)
	_build_picker()

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
		if meta_hotkeys.has(h):
			it.set_tooltip_text(0, meta_hotkeys[h].get("desc", ""))

func _on_item_selected() -> void:
	var it := tree.get_selected()
	if it == null:
		return
	var h: String = it.get_metadata(0)
	desc_label.text = meta_hotkeys[h].get("desc", "") if meta_hotkeys.has(h) else "No tooltip available."

func _on_item_activated() -> void:
	var it := tree.get_selected()
	if it:
		open_picker(it.get_metadata(0))

# HotkeyPicker
func _build_picker() -> void:
	picker = PanelContainer.new()
	picker.set_anchors_preset(Control.PRESET_CENTER)
	picker.custom_minimum_size = Vector2(620, 420)
	picker.position = Vector2(-310, -210)
	picker.visible = false
	add_child(picker)
	var vb := VBoxContainer.new()
	vb.add_theme_constant_override("separation", 6)
	picker.add_child(vb)
	picker_title = Label.new()
	picker_title.add_theme_font_size_override("font_size", 20)
	vb.add_child(picker_title)
	picker_desc = Label.new()
	picker_desc.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	vb.add_child(picker_desc)
	for i in SLOTS:
		var row := HBoxContainer.new()
		vb.add_child(row)
		var nb := Label.new()
		nb.text = "#%d" % i
		nb.custom_minimum_size = Vector2(30, 0)
		row.add_child(nb)
		var btn := Button.new()
		btn.custom_minimum_size = Vector2(380, 30)
		btn.pressed.connect(func(): _start_entering(i))
		row.add_child(btn)
		slot_labels.append(btn)
		var del := Button.new()
		del.text = "X"
		del.tooltip_text = "Remove this combination."
		del.pressed.connect(func(): picker_combos[i] = ""; _render_picker())
		row.add_child(del)
		slot_delete.append(del)
		var conf := Label.new()
		conf.modulate = Color(1.0, 0.6, 0.0)
		conf.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		vb.add_child(conf)
		slot_conflicts.append(conf)
	var buttons := HBoxContainer.new()
	buttons.alignment = BoxContainer.ALIGNMENT_END
	vb.add_child(buttons)
	for spec in [["Reset", "Resets user settings to their game default", _on_picker_reset],
			["Accept", "Change the hotkeys and close", func(): _close_picker(true)],
			["Cancel", "The hotkeys will not be modified", func(): _close_picker(false)]]:
		var b := Button.new()
		b.text = spec[0]
		b.tooltip_text = spec[1]
		b.pressed.connect(spec[2])
		buttons.add_child(b)
		picker_buttons.append(b)

func open_picker(h: String) -> void:
	picker_name = h
	picker_title.text = _display_name(h)
	picker_desc.text = meta_hotkeys[h].get("desc", "") if meta_hotkeys.has(h) else ""
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
			slot_labels[i].text = cfg.format_keys(held_keys) + " (hold to register)" if held_keys.size() > 0 else "Enter new Hotkey, hold to register."
		else:
			slot_labels[i].text = picker_combos[i] if picker_combos[i] != "" else "(unused)"
		slot_delete[i].visible = picker_combos[i] != ""
		var conflicts: Array = []
		if picker_combos[i] != "":
			for n in cfg.get_conflicts(picker_combos[i]):
				if n != picker_name:
					conflicts.append(_display_name(n))
		slot_conflicts[i].text = "May conflict with: " + ", ".join(conflicts) if conflicts.size() > 0 else ""
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

func _on_picker_reset() -> void:
	picker_combos = []
	for c in cfg.get_default(picker_name):
		picker_combos.append(c)
	while picker_combos.size() < SLOTS:
		picker_combos.append("")
	_render_picker()

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
