extends Control
## Gameplay tips, a port of 0 A.D. 0.28 gui/reference/tips/sections
## (TipDisplay.js/.xml, sprites.xml, styles.xml; GPL-2.0-or-later, art CC BY-SA 3.0).
## A 904 x 400 block: the tip's picture in a frame on the left, the parchment
## with the title and bullets on the right. The loading screen shows one tip,
## picked from a category weighted by loadingScreenOccurrence_SP/_MP
## (getLoadingScreenTip); the Tips and Tricks page shuffles every tip and pages
## through them with Previous/Next (item.prev/item.next) and through a tip's
## pictures with the arrows under the frame ("1  /  3").
## The data is data/reference/loading.json from tools/extract_0ad_loading.py.

const DATA := "res://data/reference/loading.json"
const TIPS := "res://assets/textures/ui/loading/tips/"
const DECO := "res://assets/textures/ui/tipdisplay/"
const LOADING_ART := "res://assets/textures/ui/loading/"

## initData.tipScrolling: Previous/Next and the picture arrows.
var scrolling := false
## initData.isOnLoadingScreen: one weighted tip instead of all of them.
var loading_screen := false
## Engine.HasNetClient(): the loading screen weighs the categories by their MP share.
var networked := false

## [{"name": stem, "images": [...]}] in display order (tipFilesData).
var tips: Array = []
var tip_index := -1
var image_index := -1

var _data: Dictionary = {}
var _hotkeys := RtsoHotkeyConfig.new()
var _image: TextureRect
var _title: Label
var _text: Label
var _deco: Control
var _rule: ColorRect
var _ornament_right: TextureRect
var _prev: Button
var _next: Button
var _image_panel: Control
var _image_text: Label
var _prev_image: Button
var _next_image: Button

## The tip's text-file stem (army_camp), or "" with no data.
func current_name() -> String:
	return str(tips[tip_index]["name"]) if tip_index >= 0 and tip_index < tips.size() else ""

func current_image() -> String:
	if tip_index < 0 or tip_index >= tips.size():
		return ""
	var images: Array = tips[tip_index]["images"]
	return str(images[image_index]) if image_index >= 0 and image_index < images.size() else ""

## getRandomWeightedCategory: R in [0, 1) picks a category by its SP or MP share.
static func weighted_category(categories: Array, multiplayer_share: bool, r: float) -> Dictionary:
	var key := "mp" if multiplayer_share else "sp"
	var total := 0.0
	for c in categories:
		total += float(c.get(key, 0.0))
	var pick := r * total
	var cumulative := 0.0
	for c in categories:
		cumulative += float(c.get(key, 0.0))
		if pick <= cumulative and float(c.get(key, 0.0)) > 0.0:
			return c
	return {}

func _ready() -> void:
	custom_minimum_size = Vector2(904, 400)
	size = custom_minimum_size
	if FileAccess.file_exists(DATA):
		var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(DATA))
		if typeof(parsed) == TYPE_DICTIONARY:
			_data = parsed
	tips = _loading_tip() if loading_screen else _all_tips()
	_build()
	var hide := not scrolling or tips.size() < 2
	_prev.visible = not hide
	_next.visible = not hide
	change_tip(1)

func _entry(stem: String) -> Dictionary:
	var images: Array = _data.get("tips", {}).get(stem, {}).get("images", []).duplicate()
	images.shuffle()
	return {"name": stem, "images": images}

func _loading_tip() -> Array:
	var cat := weighted_category(_data.get("categories", []), networked, randf())
	var stems: Array = cat.get("tips", [])
	return [] if stems.is_empty() else [_entry(str(stems.pick_random()))]

func _all_tips() -> Array:
	var out: Array = []
	for c in _data.get("categories", []):
		for stem in c.get("tips", []):
			out.append(_entry(str(stem)))
	out.shuffle()
	return out

## onTipIndexChange.
func change_tip(step: int) -> void:
	if tips.is_empty():
		return
	tip_index = clampi(tip_index + step, 0, tips.size() - 1)
	var tip: Dictionary = _data.get("tips", {}).get(current_name(), {})
	_title.text = str(tip.get("title", ""))
	var bullets: PackedStringArray = []
	for line in tip.get("lines", []):
		bullets.append("• " + tr(str(line))) # [icon="BulletPoint"]; Engine.TranslateLines
	_text.text = "\n\n".join(bullets)
	_fit_title()
	_prev.disabled = not _prev.visible or tip_index == 0
	_next.disabled = not _next.visible or tip_index >= tips.size() - 1
	change_image(-image_index)

## scaleGuiElementsToFit: the title grows to its text, the ornament spans the
## title's width plus 12 px each side just under it, and the bullets start 16 px
## below the ornament.
func _fit_title() -> void:
	var font := _title.get_theme_font("font")
	var fs := _title.get_theme_font_size("font_size")
	var text_size := font.get_multiline_string_size(_title.text, HORIZONTAL_ALIGNMENT_CENTER, _title.size.x, fs)
	_title.size.y = text_size.y
	var bottom := _title.position.y + _title.size.y # at least the Label's own minimum
	var w := minf(text_size.x, _title.size.x)
	_deco.position = Vector2(354 / 2.0 - (w / 2 + 12), bottom - 4)
	_deco.size = Vector2(w + 24, 16)
	_rule.size.x = _deco.size.x - 22
	_ornament_right.position.x = _deco.size.x - 16
	_text.position.y = _deco.position.y + _deco.size.y + 16
	_text.size.y = 400 - 60 - _text.position.y

## onTipImageIndexChange.
func change_image(step: int) -> void:
	if tips.is_empty():
		return
	var images: Array = tips[tip_index]["images"]
	image_index = clampi(image_index + step, 0, maxi(images.size() - 1, 0))
	var path := TIPS + current_image()
	_image.texture = load(path) as Texture2D if current_image() != "" and ResourceLoader.exists(path) else null
	_image_panel.visible = scrolling and images.size() > 1
	_image_text.text = "%d  /  %d" % [image_index + 1, images.size()]
	_prev_image.disabled = image_index <= 0
	_next_image.disabled = image_index >= images.size() - 1

func _tex(file: String) -> Texture2D:
	for path in [DECO + file, LOADING_ART + file]:
		if ResourceLoader.exists(path):
			return load(path) as Texture2D
	return null

func _build() -> void:
	# The picture: TipImageFrame (4 px border lines) round a 512 x 512 image
	# cut to the frame, under tip-image-gradient.
	var frame := Panel.new()
	frame.name = "TipImageFrame"
	frame.position = Vector2(0, 4)
	frame.size = Vector2(520, 392)
	frame.clip_contents = true
	frame.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var sb := StyleBoxFlat.new()
	sb.bg_color = Color(0, 0, 0, 0.6)
	sb.set_border_width_all(4)
	sb.border_color = Color(0.75, 0.66, 0.42)
	frame.add_theme_stylebox_override("panel", sb)
	add_child(frame)
	_image = TextureRect.new()
	_image.name = "TipImage"
	_image.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	_image.position = Vector2(4, 4)
	_image.size = Vector2(512, 512)
	_image.show_behind_parent = true
	frame.add_child(_image)
	var cover := TextureRect.new()
	cover.texture = _tex("tip-image-gradient.png") if _tex("tip-image-gradient.png") else _tex("gradient_tips.png")
	cover.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	cover.size = Vector2(512, 512)
	_image.add_child(cover)

	# imageControlPanel: under the frame, "n  /  N" between two arrows.
	_image_panel = ColorRect.new()
	_image_panel.name = "ImageControlPanel"
	(_image_panel as ColorRect).color = Color(0, 0, 0, 0.6)
	_image_panel.position = Vector2(120, 412)
	_image_panel.size = Vector2(280, 32)
	add_child(_image_panel)
	_image_text = Label.new()
	_image_text.name = "TipImageText"
	_image_text.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_image_text.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_image_text.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	_image_text.add_theme_font_size_override("font_size", 16)
	_image_panel.add_child(_image_text)
	_prev_image = _arrow("PreviousImageButton", "◀", Vector2(36, 2), tr("Switch to the previous image."))
	_prev_image.pressed.connect(change_image.bind(-1))
	_next_image = _arrow("NextImageButton", "▶", Vector2(216, 2), tr("Switch to the next image."))
	_next_image.pressed.connect(change_image.bind(1))

	# The parchment, title with its ornament, bullets and the tip buttons.
	var side := Control.new()
	side.name = "TipTextBox"
	side.position = Vector2(556, 0)
	side.size = Vector2(354, 400)
	add_child(side)
	var parchment := TextureRect.new()
	var sheet := _tex("parchment.png")
	if sheet and sheet.get_width() > 322:
		# TipTextBackground: real_texture_placement="-4 0 317 386".
		var region := AtlasTexture.new()
		region.atlas = sheet
		region.region = Rect2(0, 0, 317, 386)
		sheet = region
	parchment.texture = sheet
	parchment.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	parchment.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	side.add_child(parchment)
	_title = Label.new()
	_title.name = "TipTitle"
	_title.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_title.position = Vector2(30, 25)
	_title.size = Vector2(354 - 60, 20)
	_title.add_theme_color_override("font_color", Color.BLACK)
	_title.add_theme_font_size_override("font_size", 16)
	side.add_child(_title)
	# TipTitleDecoration: an ornament at each end of a black line under the title.
	var deco := Control.new()
	_deco = deco
	deco.name = "TipTitleDecoration"
	deco.position = Vector2(65, 49)
	deco.size = Vector2(354 - 130, 16)
	side.add_child(deco)
	var rule := ColorRect.new()
	rule.color = Color.BLACK
	rule.position = Vector2(11, 9)
	rule.size = Vector2(deco.size.x - 22, 2)
	deco.add_child(rule)
	_rule = rule
	for right in [false, true]:
		var orn := TextureRect.new()
		orn.texture = _tex("title-ornament.png")
		orn.flip_h = right
		orn.position = Vector2(deco.size.x - 16 if right else 0, 0)
		orn.size = Vector2(16, 16)
		deco.add_child(orn)
		if right:
			_ornament_right = orn
	_text = Label.new()
	_text.name = "TipText"
	_text.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_text.position = Vector2(30, 73)
	_text.size = Vector2(354 - 55, 400 - 60 - 73)
	_text.add_theme_color_override("font_color", Color.BLACK)
	_text.add_theme_font_size_override("font_size", 14)
	side.add_child(_text)
	_prev = _tip_button(side, "PreviousTipButton", tr("Previous"), 354 / 2.0 - 140, "item.prev", tr("Switch to the previous tip."))
	_prev.pressed.connect(change_tip.bind(-1))
	_next = _tip_button(side, "NextTipButton", tr("Next"), 354 / 2.0 + 15, "item.next", tr("Switch to the next tip."))
	_next.pressed.connect(change_tip.bind(1))

func _arrow(node_name: String, glyph: String, pos: Vector2, tooltip: String) -> Button:
	var b := Button.new()
	b.name = node_name
	b.text = glyph
	b.flat = true
	b.position = pos
	b.size = Vector2(28, 28)
	b.tooltip_text = tooltip
	_image_panel.add_child(b)
	return b

func _tip_button(parent: Control, node_name: String, caption: String, x: float, hotkey: String, tooltip: String) -> Button:
	var b := Button.new()
	b.name = node_name
	b.text = caption
	b.position = Vector2(x, 400 - 52)
	b.size = Vector2(125, 24)
	b.add_theme_color_override("font_color", Color.BLACK)
	b.add_theme_color_override("font_hover_color", Color.BLACK)
	b.add_theme_color_override("font_pressed_color", Color.BLACK)
	b.add_theme_color_override("font_disabled_color", Color8(70, 65, 50))
	b.add_theme_font_size_override("font_size", 16)
	# PreviousTipButton/NextTipButton: the highlight textures, 10 px past the edges.
	for state in [["normal", "base"], ["hover", "over"], ["pressed", "pressed"], ["disabled", "disabled"], ["focus", "over"]]:
		var tex := _tex("button-highlight_%s.png" % state[1])
		if tex == null:
			continue
		var box := StyleBoxTexture.new()
		box.texture = tex
		box.set_expand_margin_all(10)
		b.add_theme_stylebox_override(state[0], box)
	# colorizeHotkey("%(hotkey)s: ", hotkey) + tooltip
	b.tooltip_text = _hotkeys.colorize("%(hotkey)s: ", hotkey) + tooltip
	parent.add_child(b)
	return b

func _input(event: InputEvent) -> void:
	if not scrolling or not is_visible_in_tree() or not (event is InputEventKey and event.pressed and not event.echo):
		return
	var fired: Array = _hotkeys.fired(event)
	if "item.next" in fired and not _next.disabled:
		get_viewport().set_input_as_handled()
		change_tip(1)
	elif "item.prev" in fired and not _prev.disabled:
		get_viewport().set_input_as_handled()
		change_tip(-1)
