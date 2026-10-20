extends Control
## The mod selector, ported from 0 A.D. 0.28 gui/modmod (modmod.js,
## modmod.xml, help/): Available Mods and Enabled Mods with Name, Version,
## (Folder), Mod Label, Dependencies and Website columns, the text filter with
## Negate and Filter compatible, Enable/Disable, the load-order arrows,
## Visit Website, Help, Save Configuration and Save and Restart. Mods are
## scenes/menu/mods.gd's; RTS0 has no mod.io, so Download Mods is left out.
const MODERN := preload("res://scenes/common/modern_theme.gd")

const L10n := preload("res://scenes/menu/l10n.gd")
const Mods := preload("res://scenes/menu/mods.gd")
const COLUMNS := [["name", "Name"], ["version", "Version"], ["folder", "(Folder)"], ["label", "Mod Label"],
	["dependencies", "Dependencies"], ["url", "Website"]]
const COLOR_NOT_MET := Color8(255, 100, 100)
const COLOR_NO_MOD := "#ffff64"
const COLOR_DESCRIPTION := Color8(180, 180, 180)
## help/help.txt, with RTS0's mod folders in place of .pyromod files.
const HELP := "0 A.D. is designed to be easily modded. RTS0 loads a mod from a folder in the game's user data directory, mods/<folder>/, holding its mod.json and the .pck or .zip packs (Godot resource packs) whose files replace the game's.\n\nEnable a mod here and restart the game to load it; mods load from top to bottom, so a later one wins. You can delete a mod manually using your file browser if needed.\n\nFor more information about modding the game, see the Modding Guide online (click the Modding Guide button below)."
const MODDING_GUIDE := "https://gitea.wildfiregames.com/0ad/0ad/wiki/Modding_Guide"

var mods: Dictionary = {}
var enabled: Array = []
var disabled: Array = []
var enabled_shown: Array = []
var disabled_shown: Array = []
var compatible: Dictionary = {}

var filter_edit: LineEdit
var negate: CheckBox
var compat_filter: CheckBox
var description: RichTextLabel
var enabled_list: Tree
var disabled_list: Tree
var toggle_button: Button
var web_button: Button
var up_button: Button
var down_button: Button
var save_button: Button
var start_button: Button
var message: Label
var _picked := ""  # "enabled" or "disabled"
## The Available list's selected_column and selected_column_order.
var sort_column := 0
var sort_order := 1
## g_HasIncompatibleMods: the enabled list keeps the incompatible and missing mods.
var has_incompatible := false

## 0.28 modmod.js init with HasIncompatibleMods: the Mod Selection, and over
## it gui/incompatible_mods (page_incompatible_mods.xml, incompatible_mods.txt).
static func open_incompatible(parent: Node) -> Control:
	var page := open(parent, true)
	var msg := L10n.t("You tried to start the game with incompatible or missing mods!") + "\n\n" \
		+ L10n.t("Solve the compatibility issue by disabling invalid mods or enabling required dependencies. You can then save & start the game.") + "\n\n" \
		+ L10n.t("Incompatible mods will appear in red in the list of enabled mods (bottom panel). Dependencies can be added by enabling them in the upper panel.")
	var box: Control = load("res://scenes/common/msgbox.gd").open(page, 700, 400, msg, L10n.t("Incompatible mods"), [L10n.t("Ok")])
	# ModernTextPanel: left-aligned from the top, 20 px in.
	var text: Label = box.find_child("Text", true, false)
	text.horizontal_alignment = HORIZONTAL_ALIGNMENT_LEFT
	text.vertical_alignment = VERTICAL_ALIGNMENT_TOP
	text.offset_left = 20
	text.offset_top = 20
	text.offset_right = -20
	text.offset_bottom = -52
	return page

static func open(parent: Node, incompatible := false) -> Control:
	var page = load("res://scenes/menu/modmod.gd").new()
	page.name = "ModSelection"
	page.has_incompatible = incompatible
	parent.add_child(page)
	return page

func _ready() -> void:
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_STOP
	_build()
	init_mods()

## modmod.js initMods.
func init_mods(dir: String = Mods.MODS_DIR) -> void:
	mods = Mods.available(dir)
	enabled = Mods.enabled() if has_incompatible else Mods.enabled().filter(func(f): return mods.has(f))
	disabled = mods.keys().filter(func(f): return not enabled.has(f))
	recompute_compatibility()
	negate.button_pressed = false
	compat_filter.button_pressed = true
	save_button.disabled = true
	display_lists()
	_selected(null)

func recompute_compatibility() -> void:
	for f in mods:
		compatible[f] = Mods.dependencies_met(f, enabled, mods)

func _build() -> void:
	theme = MODERN.theme() # 0.28's modern skin
	var fade := ColorRect.new()
	fade.color = Color(0, 0, 0, 0.85)
	fade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(fade)
	var root := VBoxContainer.new()
	root.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	for side in ["left", "right", "top", "bottom"]:
		root.set("offset_" + side, 16 if side in ["left", "top"] else -16)
	add_child(root)
	var title := Label.new()
	title.text = L10n.t("Modifications")
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title.add_theme_font_size_override("font_size", 20)
	root.add_child(title)
	var filters := HBoxContainer.new()
	root.add_child(filters)
	filter_edit = LineEdit.new()
	filter_edit.placeholder_text = L10n.t("Filter")
	filter_edit.custom_minimum_size = Vector2(160, 0)
	filter_edit.text_changed.connect(func(_t): display_lists())
	filters.add_child(filter_edit)
	negate = CheckBox.new()
	negate.text = L10n.t("Negate")
	negate.toggled.connect(func(_b): display_lists())
	filters.add_child(negate)
	description = RichTextLabel.new()
	description.bbcode_enabled = true
	description.fit_content = true
	description.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	description.add_theme_color_override("default_color", COLOR_DESCRIPTION)
	filters.add_child(description)
	compat_filter = CheckBox.new()
	compat_filter.text = L10n.t("Filter compatible")
	compat_filter.toggled.connect(func(_b): display_lists())
	filters.add_child(compat_filter)
	var avail_title := Label.new()
	avail_title.text = L10n.t("Available Mods")
	root.add_child(avail_title)
	disabled_list = _make_list()
	disabled_list.column_title_clicked.connect(func(c, _b): sort_by(c))
	root.add_child(disabled_list)
	var en_title := Label.new()
	en_title.text = L10n.t("Enabled Mods")
	en_title.tooltip_text = L10n.t("Enabled mods are loaded from top to bottom. Mods loaded later might overwrite settings of mods loaded earlier. It is advisable to have more complex mods loaded last. Use the arrows on the right to change the loading order.")
	en_title.mouse_filter = Control.MOUSE_FILTER_PASS
	root.add_child(en_title)
	var en_row := HBoxContainer.new()
	en_row.size_flags_vertical = Control.SIZE_EXPAND_FILL
	root.add_child(en_row)
	enabled_list = _make_list()
	enabled_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	en_row.add_child(enabled_list)
	var arrows := VBoxContainer.new()
	en_row.add_child(arrows)
	up_button = _button(arrows, "▲", func(): move_item(true))
	down_button = _button(arrows, "▼", func(): move_item(false))
	for b in [up_button, down_button]:
		b.tooltip_text = L10n.t("Change the order in which mods are launched. This should match the mods dependencies.")
	disabled_list.item_selected.connect(func(): _selected(disabled_list))
	enabled_list.item_selected.connect(func(): _selected(enabled_list))
	# MouseLeftDoubleClickItem: enableMod / disableMod.
	for list in [disabled_list, enabled_list]:
		list.item_activated.connect(_activated.bind(list))
	var row1 := HBoxContainer.new()
	root.add_child(row1)
	toggle_button = _button(row1, L10n.t("Enable"), toggle_mod)
	web_button = _button(row1, L10n.t("Visit Website"), visit_website)
	message = Label.new()
	message.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row1.add_child(message)
	var row2 := HBoxContainer.new()
	row2.alignment = BoxContainer.ALIGNMENT_END
	root.add_child(row2)
	_button(row2, L10n.t("Cancel"), close_page)
	_button(row2, L10n.t("Help"), show_help)
	save_button = _button(row2, L10n.t("Save Configuration"), save_mods)
	start_button = _button(row2, L10n.t("Save and Restart"), start_mods)

func _make_list() -> Tree:
	var t := Tree.new()
	t.columns = COLUMNS.size()
	t.column_titles_visible = true
	t.hide_root = true
	t.select_mode = Tree.SELECT_ROW
	t.size_flags_vertical = Control.SIZE_EXPAND_FILL
	t.custom_minimum_size = Vector2(0, 120)
	for i in COLUMNS.size():
		t.set_column_title(i, L10n.t(COLUMNS[i][1]))
		t.set_column_expand_ratio(i, [10, 7, 13, 26, 20, 24][i])
	return t

func _button(parent: Node, text: String, cb: Callable) -> Button:
	var b := Button.new()
	b.text = text
	b.custom_minimum_size = Vector2(150, 28)
	b.pressed.connect(cb)
	parent.add_child(b)
	return b

func _value(folder: String, column: String) -> String:
	if column == "folder":
		return folder
	# g_FakeMod for a folder that isn't there.
	var mod: Dictionary = mods.get(folder, {"name": L10n.t("This mod does not exist")})
	if column == "dependencies":
		return " ".join(PackedStringArray(Array(mod.get("dependencies", [])).map(func(d): return str(d))))
	return str(mod.get(column, ""))

## filterMod: the text in any column or the description, or not with Negate.
func filter_mod(folder: String) -> bool:
	var s := filter_edit.text
	if s == "":
		return not negate.button_pressed
	var hit := folder.contains(s) or str(mods.get(folder, {}).get("description", "")).contains(s)
	for c in COLUMNS:
		hit = hit or _value(folder, c[0]).contains(s)
	return hit != negate.button_pressed

## displayModLists.
func display_lists() -> void:
	enabled_shown = _display(enabled_list, enabled, true)
	disabled_shown = _display(disabled_list, disabled, false)

func _display(list: Tree, folders: Array, is_enabled: bool) -> Array:
	var keep := ""
	var sel := list.get_selected()
	if sel:
		keep = str(sel.get_metadata(0))
	var shown := folders.filter(filter_mod)
	if not is_enabled:
		var col: String = COLUMNS[sort_column][0]
		var key := func(f): return _value(f, col) if _value(f, col) != "" else f
		shown.sort_custom(func(a, b): return sort_order * str(key.call(a)).naturalnocasecmp_to(str(key.call(b))) < 0)
		if compat_filter.button_pressed:
			shown = shown.filter(func(f): return compatible.get(f, false))
	list.clear()
	var root := list.create_item()
	for f in shown:
		var it := list.create_item(root)
		it.set_metadata(0, f)
		for i in COLUMNS.size():
			it.set_text(i, _value(f, COLUMNS[i][0]))
			# getModColor: incompatible mods red when enabled, grey when not.
			if not compatible.get(f, false):
				it.set_custom_color(i, COLOR_NOT_MET if is_enabled else Color.GRAY)
		if f == keep:
			it.select(0)
	return shown

## SelectionColumnChange: a new column sorts ascending, the same one flips.
func sort_by(column: int) -> void:
	sort_order = -sort_order if column == sort_column else 1
	sort_column = column
	display_lists()

## Selects row i of list (clamped), or nothing when it is empty.
func _select_row(list: Tree, i: int) -> void:
	var rows := list.get_root().get_children() if list.get_root() else []
	list.deselect_all()
	if i >= 0 and not rows.is_empty():
		rows[mini(i, rows.size() - 1)].select(0)

func _selected_folder(list: Tree) -> String:
	var it := list.get_selected()
	return str(it.get_metadata(0)) if it else ""

## modmod.js selectedMod.
func _selected(list) -> void:
	var folder := ""
	if list != null:
		folder = _selected_folder(list)
		var other: Tree = enabled_list if list == disabled_list else disabled_list
		other.deselect_all()
		_picked = "disabled" if list == disabled_list else "enabled"
	var picked_disabled := _picked != "enabled"
	toggle_button.text = L10n.t("Enable") if picked_disabled else L10n.t("Disable")
	toggle_button.disabled = folder == "" or (picked_disabled and not compatible.get(folder, false))
	web_button.disabled = folder == "" or _value(folder, "url") == ""
	var filtering := filter_edit.text != ""
	up_button.disabled = folder == "" or _picked != "enabled" or filtering
	down_button.disabled = up_button.disabled
	description.text = str(mods.get(folder, {}).get("description", "")).replace("[", "[lb]") if folder != "" \
		else "[color=%s]%s[/color]" % [COLOR_NO_MOD, L10n.t("No mod has been selected.")]
	if enabled.is_empty():
		message.text = L10n.t("Enable at least 0ad mod")
		message.add_theme_color_override("font_color", COLOR_NOT_MET)
	start_button.disabled = enabled.is_empty()

func _activated(list: Tree) -> void:
	_selected(list)
	toggle_mod()

func toggle_mod() -> void:
	if _picked == "enabled":
		disable_mod(_selected_folder(enabled_list))
	else:
		enable_mod(_selected_folder(disabled_list))

## enableMod.
func enable_mod(folder: String) -> void:
	if folder == "" or not compatible.get(folder, false) or enabled.has(folder):
		return
	var pos := disabled_shown.find(folder)
	enabled.append(folder)
	disabled.erase(folder)
	recompute_compatibility()
	save_button.disabled = false
	message.text = ""
	display_lists()
	# The row that took its place stays selected.
	_select_row(disabled_list, pos)
	_selected(disabled_list)

## disableMod: the mods that needed it go too, and so do the ones already
## incompatible (areDependenciesMet with disabledAction).
func disable_mod(folder: String) -> void:
	if folder == "" or not enabled.has(folder):
		return
	var pos := enabled_shown.find(folder)
	enabled.erase(folder)
	if mods.has(folder):
		disabled.append(folder)
	enabled = Mods.sort_enabled(enabled, mods)
	var i := 0
	while i < enabled.size():
		if not compatible.get(enabled[i], false) or not Mods.dependencies_met(enabled[i], enabled, mods):
			if mods.has(enabled[i]):
				disabled.append(enabled[i])
			enabled.remove_at(i)
		else:
			i += 1
	save_button.disabled = false
	recompute_compatibility()
	message.text = ""
	display_lists()
	_select_row(enabled_list, pos)
	_selected(enabled_list)

## moveCurrItem, only while no filter is applied.
func move_item(up: bool) -> void:
	var folder := _selected_folder(enabled_list)
	var i := enabled.find(folder)
	var j := i + (-1 if up else 1)
	if filter_edit.text != "" or i < 0 or j < 0 or j >= enabled.size():
		return
	enabled[i] = enabled[j]
	enabled[j] = folder
	save_button.disabled = false
	display_lists()
	_selected(enabled_list)

## saveMods.
func save_mods() -> void:
	enabled = Mods.sort_enabled(enabled, mods)
	Mods.save(enabled)
	save_button.disabled = true
	display_lists()

## startMods: save, then restart with them.
func start_mods() -> void:
	if not enabled.all(func(f): return compatible.get(f, false)):
		message.text = L10n.t("Dependencies not met")
		message.add_theme_color_override("font_color", COLOR_NOT_MET)
		return
	save_mods()
	Mods.restart(get_tree())

func visit_website() -> void:
	var list: Tree = enabled_list if _picked == "enabled" else disabled_list
	var url := _value(_selected_folder(list), "url")
	if url == "":
		return
	if not url.begins_with("http://") and not url.begins_with("https://"):
		url = "http://" + url
	load("res://scenes/common/msgbox.gd").open_url(self, url)

## page_modhelp.xml (help/help.xml): 680x550 over a fade, the text in a
## panel, Close and Modding Guide.
func show_help() -> Control:
	var page := Control.new()
	page.name = "ModHelp"
	page.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	page.mouse_filter = Control.MOUSE_FILTER_STOP
	add_child(page)
	var fade := ColorRect.new()
	fade.color = Color(0, 0, 0, 0.6)
	fade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	page.add_child(fade)
	var dlg := Panel.new()
	dlg.theme_type_variation = "ModernDialog"
	dlg.set_anchors_preset(Control.PRESET_CENTER)
	dlg.offset_left = -340
	dlg.offset_right = 340
	dlg.offset_top = -275
	dlg.offset_bottom = 275
	page.add_child(dlg)
	load("res://scenes/menu/hotkeys.gd")._edge_title(dlg, L10n.t("Pyrogenesis Mod Selector"))
	var panel := ColorRect.new()
	panel.color = Color(0, 0, 0, 0.5)
	panel.set_anchors_preset(Control.PRESET_FULL_RECT)
	panel.offset_left = 20
	panel.offset_top = 20
	panel.offset_right = -20
	panel.offset_bottom = -58
	dlg.add_child(panel)
	var text := Label.new()
	text.name = "Text"
	# Engine.TranslateLines: each line on its own.
	text.text = "\n".join(PackedStringArray(Array(HELP.split("\n")).map(func(l): return L10n.t(l) if l != "" else "")))
	text.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	text.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	for side in ["left", "top"]:
		text.set("offset_" + side, 10)
	for side in ["right", "bottom"]:
		text.set("offset_" + side, -10)
	panel.add_child(text)
	var buttons := [[L10n.t("Close"), func(): page.queue_free()],
		[L10n.t("Modding Guide"), func(): load("res://scenes/common/msgbox.gd").open_url(self, MODDING_GUIDE)]]
	for i in buttons.size():
		var b := Button.new()
		b.text = buttons[i][0]
		b.anchor_left = i * 0.5
		b.anchor_right = (i + 1) * 0.5
		b.anchor_top = 1.0
		b.anchor_bottom = 1.0
		b.offset_left = 24 if i == 0 else 4
		b.offset_right = -4 if i == 0 else -24
		b.offset_top = -52
		b.offset_bottom = -24
		b.pressed.connect(buttons[i][1])
		dlg.add_child(b)
	return page

func close_page() -> void:
	queue_free()

func _input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and "cancel" in RtsoHotkeyConfig.new().fired(event):
		get_viewport().set_input_as_handled()
		# The help page takes the cancel hotkey first.
		var help := get_node_or_null("ModHelp")
		if help:
			help.queue_free()
		else:
			close_page()
