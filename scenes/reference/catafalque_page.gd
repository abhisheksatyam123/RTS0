extends Control
## Catafalque Overview, ported from 0 A.D. gui/reference/catafalque
## (CatafalquePage.js, Emblem.js, catafalque.xml): every civ's catafalque
## ("units/<civ>/catafalque") as its civ emblem on a circle round the catafalque
## portrait, captioned with the catafalque's name; hovering an emblem lists its
## auras (getAurasTooltip). Opened from Learn to Play over the menu.

const Ref = preload("res://scenes/reference/reference_common.gd")
const L10n := preload("res://scenes/menu/l10n.gd")
const PORTRAIT := "res://assets/textures/ui/portraits/catafalque.png"
const DIALOG := Vector2(800, 738) # ModernDialog 50%-400 50%-368 50%+400 50%+370
const EMBLEM := Vector2(160, 128) # style "Emblem"

## [{civ, name, tooltip, emblem}] for each catafalque shown.
var entries: Array = []
var _canvas: Control
var _panel: Panel

static func open(parent: Node) -> Control:
	var page = load("res://scenes/reference/catafalque_page.gd").new()
	page.name = "CatafalqueOverview"
	parent.add_child(page)
	return page

## getEntityPrimaryNameFormatted: the specific name unless the player shows
## generic names only (gui.session.howtoshownames 3).
static func primary_name(generic: String, specific: String) -> String:
	return generic if specific == "" or Ref.name_mode == 3 else specific

## getAurasTooltip: "<name>:" in the header font, the description in the body font,
## then "Range: N meter(s)" when it has one (the unit in the orange unit font).
static func auras_tooltip(auras: Array) -> String:
	var lines: PackedStringArray = []
	for a in auras:
		var line := L10n.sprintf(L10n.t("%(auralabel)s %(aurainfo)s"), {
			"auralabel": "[b]%s[/b]" % L10n.sprintf(L10n.t("%(auraname)s:"), {"auraname": L10n.t(str(a.get("name", "")))}),
			"aurainfo": "[font_size=13]%s[/font_size]" % L10n.t(str(a.get("description", ""))),
		})
		var radius := float(a.get("radius", 0.0))
		if radius > 0.0:
			var n := roundi(radius)
			line += " " + L10n.sprintf(TranslationServer.translate_plural("%(label)s %(val)s %(unit)s", "%(label)s %(val)s %(unit)s", n), {
				"label": TranslationServer.translate("Range:", "aura"),
				"val": str(n),
				"unit": "[font_size=10][color=orange]%s[/color][/font_size]" % TranslationServer.translate_plural("meter", "meters", n),
			})
		lines.append(line)
	return "\n".join(lines)

func _ready() -> void:
	set_meta("tooltip_style", "reference") # 0.28 referenceTooltip (tooltips.gd)
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_STOP
	var fade := ColorRect.new() # "BackgroundTranslucent"
	fade.color = Color8(0, 0, 0, 85)
	fade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(fade)
	var panel := Panel.new()
	panel.anchor_left = 0.5
	panel.anchor_right = 0.5
	panel.anchor_top = 0.5
	panel.anchor_bottom = 0.5
	panel.offset_left = -DIALOG.x / 2
	panel.offset_right = DIALOG.x / 2
	panel.offset_top = -DIALOG.y / 2
	panel.offset_bottom = DIALOG.y / 2
	add_child(panel)
	_panel = panel
	panel.pivot_offset = DIALOG / 2
	_fit()
	resized.connect(_fit)
	var title := Label.new() # TitleText on the dialog's top edge: sans-bold-stroke-14
	title.name = "Title"
	title.text = tr("Catafalque Bonuses")
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	title.position = Vector2(DIALOG.x / 2 - 128, -18)
	title.size = Vector2(256, 32)
	title.add_theme_font_size_override("font_size", 14)
	title.add_theme_color_override("font_outline_color", Color.BLACK)
	title.add_theme_constant_override("outline_size", 4)
	panel.add_child(title)
	var portrait := TextureRect.new()
	portrait.texture = Ref.texture(PORTRAIT)
	portrait.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	portrait.position = DIALOG / 2 - Vector2(128, 128)
	portrait.size = Vector2(256, 256)
	portrait.mouse_filter = Control.MOUSE_FILTER_IGNORE
	panel.add_child(portrait)
	_canvas = Control.new()
	_canvas.position = Vector2(8, 4) # "canvas" 8 4 100%-8 100%-12
	_canvas.size = DIALOG - Vector2(16, 16)
	panel.add_child(_canvas)
	_build_emblems()
	var close := Button.new() # gui/reference/common/Buttons/CloseButton.xml: 100%-216 100%-56 100%-24 100%-24
	close.name = "CloseButton"
	close.text = tr("Close")
	close.tooltip_text = RtsoHotkeyConfig.new().colorize(L10n.t("%(hotkey)s: Close Catafalque Bonuses."), "cancel")
	close.set_anchors_preset(Control.PRESET_BOTTOM_RIGHT)
	close.offset_left = -216
	close.offset_top = -56
	close.offset_right = -24
	close.offset_bottom = -24
	close.grow_vertical = Control.GROW_DIRECTION_BEGIN
	close.pressed.connect(close_page)
	panel.add_child(close)

## CatafalquePage: emblems evenly spaced on a circle 4/5 of the way out.
func _build_emblems() -> void:
	var civs := {}
	for c in Ref.ref().get_civs():
		civs[int(c["id"])] = c
	for cat in Ref.ref().get_catafalques():
		var civ := int(cat["civ"])
		entries.append({
			"civ": civ,
			"name": primary_name(str(cat["name"]), str(cat["specific_name"])),
			"tooltip": auras_tooltip(cat.get("auras", [])),
			"emblem": str(civs.get(civ, {}).get("emblem", "")),
		})
	var center := _canvas.size / 2
	var radius := minf(center.x, center.y) / 5.0 * 4.0
	for i in entries.size():
		var e: Dictionary = entries[i]
		var angle := TAU / entries.size() * i
		var box := Control.new()
		box.name = "Emblem%d" % i
		box.size = EMBLEM
		box.position = center + Vector2(radius * sin(angle), -radius * cos(angle)) - EMBLEM / 2
		_canvas.add_child(box)
		var img := TextureRect.new()
		img.texture = Ref.texture(e["emblem"])
		img.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
		img.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
		img.position = EMBLEM / 2 - Vector2(64, 64)
		img.size = Vector2(128, 128)
		img.tooltip_text = e["tooltip"]
		box.add_child(img)
		var cap := Label.new() # "EmblemText" over "EmblemMask"
		cap.text = e["name"]
		cap.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		cap.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		cap.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
		cap.position = Vector2(0, EMBLEM.y / 2 - 18)
		cap.size = Vector2(EMBLEM.x, 36)
		cap.mouse_filter = Control.MOUSE_FILTER_IGNORE
		cap.add_theme_font_size_override("font_size", 13)
		var mask := StyleBoxFlat.new()
		mask.bg_color = Color(0, 0, 0, 0.55)
		cap.add_theme_stylebox_override("normal", mask)
		box.add_child(cap)

## The dialog is laid out for 0 A.D.'s 1024x768 minimum; shrink it on
## shorter screens so the title and Close stay visible.
func _fit() -> void:
	if _panel:
		var k := minf(1.0, minf((size.y - 8.0) / DIALOG.y, (size.x - 8.0) / DIALOG.x)) if size.y > 0.0 else 1.0
		_panel.scale = Vector2(k, k)

func close_page() -> void:
	queue_free()

func _input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and "cancel" in RtsoHotkeyConfig.new().fired(event):
		get_viewport().set_input_as_handled()
		close_page()
