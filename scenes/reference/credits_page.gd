extends Control
## Credits, ported from 0 A.D. gui/credits (credits.js, credits.xml): one tab
## per credits file in 0 A.D.'s order, each listing its sections as
## "nick — name" lines (parseHelper). The RTS0 tab comes first; the 0 A.D.
## files are copied as is to data/reference/credits/ (0 A.D. 0.28.0).

const DIR := "res://data/reference/credits/"
const TABS := preload("res://scenes/common/tab_buttons.gd")
const L10n := preload("res://scenes/menu/l10n.gd")
## credits.js g_OrderTabNames.
const ORDER := ["special", "programming", "art", "audio", "maps", "history",
	"balancing", "community", "translators", "donators"]
const RTS0_CREDITS := "[font_size=16][b]RTS0[/b][/font_size]\nA Godot 4.3 and C++ GDExtension port of 0 A.D.: Empires Ascendant\n\n" + \
	"[font_size=16][b]0 A.D.: Empires Ascendant[/b][/font_size]\nOriginal game by Wildfire Games and the 0 A.D. community.\n" + \
	"Code GPL-2.0-or-later; art, audio, maps and texts CC BY-SA 3.0.\nThe other tabs are 0 A.D.'s own credits.\n\n" + \
	"[font_size=16][b]RTS0 engine port[/b][/font_size]\nLead architecture and implementation: Abhi\nDeterministic netcode and SoA simulation: RTS0 Team\n"

## [{label, content}] per tab.
var panels: Array = []
var text: RichTextLabel
var tabs: VBoxContainer
var _panel := 0
var _hotkeys := RtsoHotkeyConfig.new()

static func open(parent: Node) -> Control:
	var page = load("res://scenes/reference/credits_page.gd").new()
	page.name = "Credits"
	parent.add_child(page)
	return page

## parseHelper: titles and subtitles in bold, then each person on a line.
static func parse(list: Array) -> String:
	var out := ""
	for o in list:
		if typeof(o) != TYPE_DICTIONARY:
			continue
		# translateObjectKeys(json, ["Title", "Subtitle", "LangName"]); Title and
		# LangName in sans-bold-stroke-14, Subtitle in sans-bold-14, names in sans-14.
		for key in ["LangName", "Title"]:
			if o.has(key):
				out += "[font_size=14][outline_size=4][outline_color=black][b]%s[/b][/outline_color][/outline_size][/font_size]\n" % _esc(L10n.t(str(o[key])))
		if o.has("Subtitle"):
			out += "[font_size=14][b]%s[/b][/font_size]\n" % _esc(L10n.t(str(o["Subtitle"])))
		if o.has("List"):
			for e in o["List"]:
				var nick := str(e.get("nick", ""))
				var name := str(e.get("name", ""))
				var credit := L10n.sprintf(L10n.t("%(nick)s — %(name)s"), {"nick": nick, "name": name}) if nick != "" and name != "" else (nick if nick != "" else name)
				if credit != "":
					out += _esc(credit) + "\n"
			out += "\n"
		if o.has("Content"):
			out += "\n" + parse(o["Content"]) + "\n"
	return out

static func _esc(s: String) -> String:
	return s.replace("[", "[lb]")

func _ready() -> void:
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_STOP
	panels.append({"label": "RTS0", "content": RTS0_CREDITS})
	for cat in ORDER:
		var path: String = DIR + cat + ".json"
		var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(path)) if FileAccess.file_exists(path) else null
		if typeof(parsed) != TYPE_DICTIONARY or not parsed.has("Content"):
			push_error("Could not load credits for %s!" % cat)
			continue
		panels.append({"label": L10n.t(str(parsed.get("Title", cat))), "content": parse(parsed["Content"])})
	var fade := ColorRect.new() # "ModernFade"
	fade.color = Color(0, 0, 0, 0.6)
	fade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(fade)
	theme = preload("res://scenes/common/modern_theme.gd").theme()
	var panel := Panel.new() # size="50%-473 24 50%+473 100%-24"
	panel.theme_type_variation = "ModernDialog"
	panel.anchor_left = 0.5
	panel.anchor_right = 0.5
	panel.anchor_bottom = 1.0
	panel.offset_left = -473
	panel.offset_right = 473
	panel.offset_top = 24
	panel.offset_bottom = -24
	add_child(panel)
	var title := Label.new()
	title.name = "Title"
	title.text = "0 A.D. Credits"
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	# ModernLabelText at 50%-128 -18 50%+128 14: across the dialog's top edge.
	title.anchor_left = 0.5
	title.anchor_right = 0.5
	title.offset_left = -128
	title.offset_right = 128
	title.offset_top = -18
	title.offset_bottom = 14
	title.add_theme_font_size_override("font_size", 18)
	title.add_theme_color_override("font_outline_color", Color.BLACK)
	title.add_theme_constant_override("outline_size", 4)
	panel.add_child(title)
	tabs = VBoxContainer.new()
	tabs.position = Vector2(20, 30)
	tabs.size = Vector2(190, 400)
	tabs.add_theme_constant_override("separation", 5)
	panel.add_child(tabs)
	var group := ButtonGroup.new()
	var keys: Dictionary = _hotkeys.get_hotkey_map()
	for i in panels.size():
		var b := Button.new()
		b.text = panels[i]["label"]
		b.tooltip_text = TABS.tooltip("", false, keys)
		b.gui_input.connect(func(ev: InputEvent):
			var d: int = TABS.wheel(ev)
			if d != 0:
				select_panel(TABS.next_index(_panel, d, panels.size()))
				b.accept_event())
		b.toggle_mode = true
		b.button_group = group
		b.theme_type_variation = "ModernTabButtonVertical"
		b.custom_minimum_size = Vector2(190, 35)
		b.pressed.connect(select_panel.bind(i))
		tabs.add_child(b)
	# creditsText sits on a ModernFade image of the same size.
	var back := ColorRect.new()
	back.name = "CreditsFade"
	back.color = Color(0, 0, 0, 0.6)
	back.mouse_filter = Control.MOUSE_FILTER_IGNORE
	back.anchor_right = 1.0
	back.anchor_bottom = 1.0
	back.offset_left = 220
	back.offset_top = 30
	back.offset_right = -20
	back.offset_bottom = -54
	panel.add_child(back)
	text = RichTextLabel.new()
	text.name = "CreditsText"
	text.bbcode_enabled = true
	text.selection_enabled = true
	text.anchor_right = 1.0
	text.anchor_bottom = 1.0
	text.offset_left = 220
	text.offset_top = 30
	text.offset_right = -20
	text.offset_bottom = -54
	panel.add_child(text)
	var close := Button.new()
	close.text = "Close"
	close.anchor_left = 1.0
	close.anchor_right = 1.0
	close.anchor_top = 1.0
	close.anchor_bottom = 1.0
	close.offset_left = -200
	close.offset_right = -17
	close.offset_top = -45
	close.offset_bottom = -17
	close.pressed.connect(close_page)
	panel.add_child(close)
	select_panel(0)

func select_panel(i: int) -> void:
	if i < 0 or i >= panels.size():
		return
	_panel = i
	(tabs.get_child(i) as Button).button_pressed = true
	text.text = "[center]" + str(panels[i]["content"]) + "[/center]"
	text.scroll_to_line(0) # scroll_top="true"

func close_page() -> void:
	queue_free()

func _input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and "cancel" in RtsoHotkeyConfig.new().fired(event):
		get_viewport().set_input_as_handled()
		close_page()
		return
	var d: int = TABS.direction(event, _hotkeys)
	if d != 0:
		get_viewport().set_input_as_handled()
		select_panel(TABS.next_index(_panel, d, panels.size()))
