extends Control
## The Map Browser, a port of 0 A.D. 0.28 gui/maps/mapbrowser (MapBrowser.js,
## grid/GridBrowser.js, grid/MapGridBrowser.js, grid/MapGridBrowserItem.js,
## controls/MapBrowserControls.js, controls/MapFiltering.js,
## controls/Pagination.js, controls/MapDescription.js, utils/MatchSort.js;
## GPL-2.0-or-later). A grid of map previews with a search box, the Map Type
## and Map Filter dropdowns, pages that move by a row, zoom, and the selected
## map's preview and description. Learn to Play's Map Overview opens it to
## look only; Game Setup opens it to pick a map (Pick Random Map, Select).
const MODERN := preload("res://scenes/common/modern_theme.gd")

signal map_chosen(map_id: String, map_type: String, map_filter: String)
signal closed

# MapGridBrowser.prototype: ItemRatio, DefaultItemWidth, MinItemWidth; the grid has 100 items.
const ITEM_RATIO := 4.0 / 3.0
const DEFAULT_ITEM_WIDTH := 200.0
const MIN_ITEM_WIDTH := 100.0
const MAX_ITEMS := 100
const RIGHT_WIDTH := 375.0
const SELECTED_COLOR := Color8(120, 0, 0)
const CAPTIONS := {
	"close": ["Close", "%(hotkey)s: Close map browser."],
	"cancel": ["Cancel", "%(hotkey)s: Close map browser and discard the selection."],
}
const RANDOM_ENTRY := {"id": "random", "name": "Random", "desc": "Pick a map at random.", "type": "random", "filter": "default"}

var Setup = load("res://scenes/menu/gamesetup.gd")
var L10n = load("res://scenes/menu/l10n.gd")

var allow_selection := false
var map_list: Array = []
var selected := -1
var current_page := 0
var page_count := 1
var column_count := -1
var min_columns := 1
var max_columns := 1
var row_count := 1
var items_per_row := 1
var items_per_page := 1
var item_width := DEFAULT_ITEM_WIDTH
var item_height := DEFAULT_ITEM_WIDTH / ITEM_RATIO

var _maps: Array = []
var _filters: Array = [] # [name, title] of the type's filters, in dropdown order
var _hotkeys := RtsoHotkeyConfig.new()
var _grid: Control
var _items: Array = []
var _search: LineEdit
var _type: OptionButton
var _filter: OptionButton
var _status: Label
var _prev: Button
var _next: Button
var _zoom_in: Button
var _zoom_out: Button
var _sel_name: Label
var _sel_preview: TextureRect
var _sel_desc: Label
var _pick_random: Button
var _close_btn: Button
var _select_btn: Button

## Opens the browser over parent: on map_id (type and filter as given), to pick
## a map when allow_select (MapBrowser.openPage).
static func open_overlay(parent: Node, maps: Array, map_type: String, map_filter: String, map_id: String, allow_select: bool) -> Control:
	var layer := CanvasLayer.new()
	layer.layer = 40
	layer.process_mode = Node.PROCESS_MODE_ALWAYS
	parent.add_child(layer)
	var page = load("res://scenes/menu/map_browser.gd").new()
	page.name = "MapBrowser"
	page._maps = maps
	page.allow_selection = allow_select
	layer.add_child(page)
	page.closed.connect(layer.queue_free)
	page.select(map_filter, map_type)
	if not map_id.is_empty():
		page.select_map(map_id)
	return page

func _ready() -> void:
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_STOP
	var fade := ColorRect.new()
	fade.color = Color8(0, 0, 0, 85)
	fade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(fade)
	var dialog := Panel.new()
	dialog.name = "Dialog"
	dialog.anchor_left = 0.05
	dialog.anchor_top = 0.05
	dialog.anchor_right = 0.95
	dialog.anchor_bottom = 0.95
	dialog.gui_input.connect(_on_wheel)
	theme = MODERN.theme() # 0.28's modern skin (ModernDialog and its widgets)
	dialog.theme_type_variation = "ModernDialog"
	add_child(dialog)
	var title := Label.new()
	title.text = tr("Map Browser")
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	MODERN.label_text(title) # ModernLabelText
	title.anchor_left = 0.5
	title.anchor_right = 0.5
	title.offset_left = -128
	title.offset_right = 128
	title.offset_top = -18
	title.offset_bottom = 14
	dialog.add_child(title)
	dialog.add_child(_build_controls())
	_grid = Control.new()
	_grid.name = "Grid"
	_grid.clip_contents = true
	_grid.anchor_right = 1.0
	_grid.anchor_bottom = 1.0
	_grid.offset_left = 15
	_grid.offset_top = 15
	_grid.offset_right = -(RIGHT_WIDTH + 25)
	_grid.offset_bottom = -15
	_grid.resized.connect(_on_grid_resized)
	_grid.gui_input.connect(_on_wheel)
	dialog.add_child(_grid)
	for i in MAX_ITEMS:
		_items.append(_make_item(i))

func _make_item(i: int) -> Button:
	var b := Button.new()
	b.name = "Map%d" % i
	b.add_theme_stylebox_override("normal", StyleBoxEmpty.new())
	b.add_theme_stylebox_override("focus", StyleBoxEmpty.new())
	b.focus_mode = Control.FOCUS_NONE
	b.visible = false
	var over := StyleBoxFlat.new()
	over.bg_color = Color8(255, 0, 0, 100)
	b.add_theme_stylebox_override("hover", over)
	var pressed := StyleBoxFlat.new()
	pressed.bg_color = Color8(255, 0, 0, 150)
	b.add_theme_stylebox_override("pressed", pressed)
	b.add_theme_stylebox_override("hover_pressed", pressed)
	var box := Panel.new() # ModernDarkBoxGold
	box.add_theme_stylebox_override("panel", MODERN._dark_box(MODERN.GOLD, MODERN.DARK_BOX, 0))
	box.mouse_filter = Control.MOUSE_FILTER_IGNORE
	box.set_anchors_preset(Control.PRESET_FULL_RECT)
	box.offset_left = 3
	box.offset_top = 3
	box.offset_right = -3
	box.offset_bottom = -3
	b.add_child(box)
	var img := TextureRect.new()
	img.name = "Preview"
	img.mouse_filter = Control.MOUSE_FILTER_IGNORE
	img.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	img.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_COVERED
	img.set_anchors_preset(Control.PRESET_FULL_RECT)
	img.offset_left = 1
	img.offset_top = 1
	img.offset_right = -1
	img.offset_bottom = -1
	box.add_child(img)
	var cap := Label.new()
	cap.name = "Caption"
	cap.mouse_filter = Control.MOUSE_FILTER_IGNORE
	cap.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	cap.vertical_alignment = VERTICAL_ALIGNMENT_BOTTOM
	cap.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	cap.set_anchors_preset(Control.PRESET_FULL_RECT)
	cap.offset_left = 10
	cap.offset_right = -10
	cap.offset_bottom = -10
	MODERN.label_text(cap) # mapPreview: ModernLabelText
	box.add_child(cap)
	b.pressed.connect(func(): set_selected_index(i + current_page * items_per_row))
	b.gui_input.connect(func(ev): _on_item_input(ev))
	_grid.add_child(b)
	return b

func _build_controls() -> Control:
	var right := Control.new()
	right.name = "Controls"
	right.anchor_left = 1.0
	right.anchor_right = 1.0
	right.anchor_bottom = 1.0
	right.offset_left = -(RIGHT_WIDTH + 15)
	right.offset_right = -15
	right.offset_top = 15
	right.offset_bottom = -15
	# MapBrowserControls: a ModernFade behind the column, 5 px wider on each side.
	var shade := ColorRect.new()
	shade.color = Color8(0, 0, 0, 85)
	shade.mouse_filter = Control.MOUSE_FILTER_IGNORE
	shade.set_anchors_preset(Control.PRESET_FULL_RECT)
	shade.offset_left = -5
	shade.offset_right = 5
	right.add_child(shade)
	var col := VBoxContainer.new()
	col.set_anchors_preset(Control.PRESET_FULL_RECT)
	col.add_theme_constant_override("separation", 6)
	right.add_child(col)
	# Pagination: ← and → halves of 20%..80%-58, + and − in the last 58 px, then the
	# status line in ModernLabelText at 5%..95%-60.
	var pager := Control.new()
	pager.custom_minimum_size = Vector2(0, 28)
	_prev = _button(pager, "←", tr("Go to the previous page."), func(): previous_page())
	_next = _button(pager, "→", tr("Go to the next page."), func(): next_page())
	_zoom_in = _button(pager, tr("+"), tr("Increase map preview size."), func(): increase_column_count(-1))
	_zoom_out = _button(pager, tr("−"), tr("Decrease map preview size."), func(): increase_column_count(1))
	_span(_prev, 0.2, 0, 0.5, -29)
	_span(_next, 0.5, -29, 0.8, -58)
	_span(_zoom_in, 1.0, -58, 1.0, -29)
	_span(_zoom_out, 1.0, -29, 1.0, 0)
	col.add_child(pager)
	_status = Label.new()
	_status.name = "Status"
	_status.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	MODERN.label_text(_status)
	col.add_child(_status)
	_search = LineEdit.new()
	_search.name = "Search"
	_search.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_search.add_theme_font_size_override("font_size", 16) # font="sans-16"
	_search.text_changed.connect(func(_t): _on_change())
	col.add_child(_row(tr("Search Map:"), _search))
	_type = OptionButton.new()
	_type.name = "MapType"
	for mt in Setup.MAP_TYPES:
		_type.add_item(tr(mt[1]))
	_type.item_selected.connect(func(_i): _render_map_filter(); _on_change())
	col.add_child(_row(tr("Map Type:"), _type))
	_filter = OptionButton.new()
	_filter.name = "MapFilter"
	_filter.item_selected.connect(func(_i): _on_change())
	col.add_child(_row(tr("Map Filter:"), _filter))
	# MapDescription: the name over a 4:3 preview, then the description.
	_sel_preview = TextureRect.new()
	_sel_preview.name = "SelectedPreview"
	_sel_preview.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	# 4:3 at full width when the page is tall enough, smaller on short screens.
	_sel_preview.custom_minimum_size = Vector2(0, 120)
	_sel_preview.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_sel_preview.size_flags_stretch_ratio = 3.0
	_sel_preview.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	_sel_name = Label.new()
	_sel_name.name = "SelectedName"
	_sel_name.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_sel_name.vertical_alignment = VERTICAL_ALIGNMENT_BOTTOM
	MODERN.label_text(_sel_name)
	_sel_name.add_theme_font_size_override("font_size", 16) # sans-bold-16
	_sel_name.set_anchors_preset(Control.PRESET_FULL_RECT)
	_sel_preview.add_child(_sel_name)
	col.add_child(_sel_preview)
	var desc_scroll := ScrollContainer.new()
	desc_scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	desc_scroll.custom_minimum_size = Vector2(0, 60)
	desc_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	_sel_desc = Label.new()
	_sel_desc.name = "SelectedDescription"
	_sel_desc.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_sel_desc.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	desc_scroll.add_child(_sel_desc)
	col.add_child(desc_scroll)
	_pick_random = _button(col, tr("Pick Random Map"), "", _on_pick_random)
	_pick_random.name = "PickRandom"
	var bottom := HBoxContainer.new()
	var caption: Array = CAPTIONS["cancel" if allow_selection else "close"]
	_close_btn = _button(bottom, tr(caption[0]), L10n.sprintf(tr(caption[1]), {"hotkey": _hotkey_text("cancel")}), close_page)
	_close_btn.name = "Close"
	_select_btn = _button(bottom, tr("Select"), "", submit_map_selection)
	_select_btn.name = "Select"
	for b in [_close_btn, _select_btn]:
		b.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		b.custom_minimum_size = Vector2(0, 30)
	col.add_child(bottom)
	_pick_random.visible = allow_selection
	_select_btn.visible = allow_selection
	return right

func _button(parent: Node, text: String, tip: String, cb: Callable) -> Button:
	var b := Button.new()
	b.text = text
	b.tooltip_text = tip
	b.focus_mode = Control.FOCUS_NONE
	b.pressed.connect(cb)
	parent.add_child(b)
	return b

## A pager button across anchors al..ar of the row, offset by ol / or_ px.
func _span(b: Control, al: float, ol: float, ar: float, or_: float) -> void:
	b.anchor_left = al
	b.anchor_right = ar
	b.anchor_bottom = 1.0
	b.offset_left = ol
	b.offset_right = or_

func _row(caption: String, control: Control) -> HBoxContainer:
	var row := HBoxContainer.new()
	var lbl := Label.new()
	lbl.text = caption
	MODERN.label_text(lbl) # ModernLeftLabelText, as wide as its text
	row.add_child(lbl)
	control.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(control)
	return row

## colorizeHotkey's "[key]" in the hotkey colour, or "Unassigned hotkey: name".
func _hotkey_text(name: String) -> String:
	return _hotkeys.colorize("%(hotkey)s", name)

# MapFiltering.select: the type and filter, then the list.
func select(filter_name: String, map_type: String) -> void:
	var t := 0
	for i in Setup.MAP_TYPES.size():
		if Setup.MAP_TYPES[i][0] == map_type:
			t = i
	_type.select(t)
	_render_map_filter()
	for i in _filters.size():
		if _filters[i][0] == filter_name:
			_filter.select(i)
	update_map_list()
	go_to_page_of_selected()
	# setTimeout in onOpenPage: focus the search box once the opening key is gone.
	_search.call_deferred("grab_focus")

## MapGridBrowser.select: selects map_id if listed and shows its page.
func select_map(map_id: String) -> void:
	set_selected_index(map_list.map(func(m): return str(m["id"])).find(map_id))
	go_to_page_of_selected()

func selected_map_type() -> String:
	return str(Setup.MAP_TYPES[clampi(_type.selected, 0, Setup.MAP_TYPES.size() - 1)][0])

func selected_map_filter() -> String:
	return str(_filters[_filter.selected][0]) if _filter.selected >= 0 and _filter.selected < _filters.size() else ""

func _render_map_filter() -> void:
	var keep := selected_map_filter()
	_filter.clear()
	_filters.clear()
	for flt in Setup.available_filters(_maps, selected_map_type()):
		_filters.append([flt[0], flt[1]])
		_filter.add_item(tr(flt[1]))
	var i := _filters.map(func(f): return f[0]).find(keep)
	_filter.select(maxi(i, 0) if not _filters.is_empty() else -1)

func _on_change() -> void:
	update_map_list()
	go_to_page_of_selected()

# MapGridBrowser.updateMapList: the type and filter's maps, then the search
# (MatchSort); a search with no match there looks through every type's maps.
func update_map_list() -> void:
	var keep := str(map_list[selected]["id"]) if selected >= 0 and selected < map_list.size() else ""
	var type := selected_map_type()
	var list: Array = []
	for m in Setup.filtered_maps(_maps, type, selected_map_filter()):
		list.append(_entry(m, type, selected_map_filter()))
	var text := _search.text
	if not text.is_empty():
		list = match_sort(text, list)
		if list.is_empty():
			list.append(RANDOM_ENTRY)
			for mt in Setup.MAP_TYPES:
				for m in Setup.filtered_maps(_maps, mt[0], "all"):
					if str(m["id"]) != Setup.RANDOM_MAP_ID:
						list.append(_entry(m, mt[0], "all"))
			list = match_sort(text, list)
	map_list = list
	resize_grid()
	set_selected_index(map_list.map(func(m): return str(m["id"])).find(keep))

func _entry(m: Dictionary, type: String, filter_name: String) -> Dictionary:
	if str(m["id"]) == Setup.RANDOM_MAP_ID:
		return RANDOM_ENTRY
	return {"id": str(m["id"]), "name": str(m["name"]), "desc": str(m.get("desc", "")), "type": type, "filter": filter_name}

## utils/MatchSort.get: the entries whose name holds the input's letters in
## order; names starting with the input first (alphabetically), then by score.
static func match_sort(input: String, list: Array) -> Array:
	input = input.to_lower()
	var scored: Array = []
	for e in list:
		var text := str(e["name"])
		var score: Variant = match_score(input, text)
		if score != null:
			scored.append([e, int(score), text, text.begins_with(input)])
	scored.sort_custom(func(a, b):
		if a[3] and b[3]:
			return a[2].naturalnocasecmp_to(b[2]) < 0
		if a[3] != b[3]:
			return a[3]
		return a[1] < b[1])
	return scored.map(func(s): return s[0])

## MatchSort.scoreText: lower is better; null when a letter is missing.
static func match_score(input: String, text: String) -> Variant:
	const HIGHSCORE := -100000000
	if input == text:
		return HIGHSCORE
	text = text.to_lower()
	if input == text:
		return HIGHSCORE / 2
	var score := 0
	var offset := -1
	for i in input.length():
		var next := text.find(input[i], offset + 1)
		if next == -1:
			return null
		score += next + (0 if next == offset + 1 else 1) * next
		offset = next
	return score

# GridBrowser.resizeGrid
func resize_grid() -> void:
	var size := _grid.size
	var max_cols := int(floorf(size.x / MIN_ITEM_WIDTH))
	if max_cols <= 0 or size.y <= 0:
		return
	if column_count < 0:
		column_count = int(floorf(size.x / DEFAULT_ITEM_WIDTH))
	min_columns = int(ceilf(size.x / (size.y * ITEM_RATIO)))
	max_columns = max_cols
	column_count = mini(max_columns, maxi(min_columns, column_count))
	item_width = floorf(size.x / column_count)
	item_height = floorf(item_width / ITEM_RATIO)
	row_count = maxi(1, int(floorf(size.y / item_height)))
	items_per_row = mini(column_count, MAX_ITEMS)
	items_per_page = mini(column_count * row_count, MAX_ITEMS)
	# Pages move by one row, so an item is on several pages.
	page_count = int(ceilf(float(map_list.size()) / items_per_row)) - row_count + 1
	for i in _items.size():
		var b: Button = _items[i]
		b.position = Vector2(item_width * (i % column_count), item_height * (i / column_count))
		b.size = Vector2(item_width, item_height)
	_render_page()

func _on_grid_resized() -> void:
	resize_grid()
	go_to_page_of_selected()

func go_to_page(n: int) -> void:
	current_page = n
	_render_page()

func next_page(wrap := true) -> void:
	var pages := maxi(1, page_count)
	go_to_page((current_page + 1) % pages if wrap else mini(current_page + 1, pages - 1))

func previous_page(wrap := true) -> void:
	var pages := maxi(1, page_count)
	go_to_page((current_page + pages - 1) % pages if wrap else maxi(current_page - 1, 0))

func go_to_page_of_selected() -> void:
	go_to_page(maxi(mini(int(floorf(float(selected) / items_per_row)) - row_count / 2, page_count - 1), 0))

func increase_column_count(diff: int) -> void:
	var row_of_sel := int(floorf(float(selected) / items_per_row))
	var in_page := selected >= 0 and row_of_sel >= current_page and row_of_sel < current_page + row_count
	column_count += diff
	resize_grid()
	if in_page:
		go_to_page_of_selected()
	else:
		go_to_page(mini(current_page, maxi(0, page_count - 1)))

func set_selected_index(i: int) -> void:
	selected = i
	_render_items()
	_select_btn.disabled = selected == -1
	# MapDescription.onSelectionChange: keeps the last map when nothing is selected.
	if selected < 0 or selected >= map_list.size():
		return
	var m: Dictionary = map_list[selected]
	_sel_name.text = tr(str(m["name"]))
	_sel_desc.text = tr(str(m["desc"]))
	_sel_preview.texture = Setup.map_preview(str(m["id"]))

# GridBrowserItem.updateVisibility, MapGridBrowserItem.updateMapAssignment and Pagination.render.
func _render_page() -> void:
	_render_items()
	_status.text = L10n.sprintf(tr("Maps: %(mapCount)s"), {"mapCount": map_list.size()}) + "   " + \
		L10n.sprintf(tr("Page: %(currentPage)s/%(maxPage)s"), {"currentPage": current_page + 1, "maxPage": maxi(1, page_count)})
	_prev.disabled = page_count <= 1
	_next.disabled = page_count <= 1
	_zoom_in.disabled = column_count <= 0
	_zoom_out.disabled = column_count >= max_columns

func _render_items() -> void:
	for i in _items.size():
		var b: Button = _items[i]
		var idx := i + current_page * items_per_row
		b.visible = i < mini(items_per_page, map_list.size() - current_page * items_per_row)
		if not b.visible:
			continue
		var m: Dictionary = map_list[idx]
		var box: Panel = b.get_child(0)
		var sb := StyleBoxFlat.new()
		sb.bg_color = Color(0.08, 0.07, 0.05)
		sb.border_color = Color(0.72, 0.6, 0.3)
		sb.set_border_width_all(1)
		var sel_sb := StyleBoxFlat.new()
		sel_sb.bg_color = SELECTED_COLOR
		b.add_theme_stylebox_override("normal", sel_sb if idx == selected else StyleBoxEmpty.new())
		box.add_theme_stylebox_override("panel", sb)
		(box.get_node("Caption") as Label).text = tr(str(m["name"]))
		(box.get_node("Preview") as TextureRect).texture = Setup.map_preview(str(m["id"]))
		# MapGridBrowserItem: the description alone (the container's tooltip is empty).
		b.tooltip_text = tr(str(m["desc"]))

func _on_item_input(ev: InputEvent) -> void:
	if ev is InputEventMouseButton and ev.pressed and ev.button_index == MOUSE_BUTTON_LEFT and ev.double_click:
		submit_map_selection()
	else:
		_on_wheel(ev)

func _on_wheel(ev: InputEvent) -> void:
	if ev is InputEventMouseButton and ev.pressed:
		if ev.button_index == MOUSE_BUTTON_WHEEL_UP:
			previous_page(false)
			accept_event()
		elif ev.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			next_page(false)
			accept_event()

func _on_pick_random() -> void:
	if map_list.is_empty():
		return
	set_selected_index(randi_range(0, map_list.size() - 1))
	go_to_page_of_selected()

## MapBrowser.submitMapSelection: the selected map with its own type and
## filter, else the dropdowns' type and filter alone.
func submit_map_selection() -> void:
	if not allow_selection:
		return
	var type := selected_map_type()
	var filter_name := selected_map_filter()
	var id := ""
	if selected >= 0 and selected < map_list.size():
		var m: Dictionary = map_list[selected]
		id = str(m["id"])
		type = str(m["type"])
		filter_name = str(m["filter"])
	map_chosen.emit(id, type, filter_name)
	close_page()

func close_page() -> void:
	closed.emit()

# Global hotkeys, as 0.28 sets them (Engine.SetGlobalHotkey), so the search box keeps none.
func _input(event: InputEvent) -> void:
	if not (event is InputEventKey or event is InputEventMouseButton):
		return
	var hk: PackedStringArray = _hotkeys.fired(event)
	if "cancel" in hk:
		get_viewport().set_input_as_handled()
		close_page()
	elif "tab.next" in hk:
		get_viewport().set_input_as_handled()
		next_page()
	elif "tab.prev" in hk:
		get_viewport().set_input_as_handled()
		previous_page()
