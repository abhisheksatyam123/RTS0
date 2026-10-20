extends RefCounted
## 0 A.D. 0.28's stone skin for the session menu (gui/common/styles.xml and
## sprites.xml): StonePanelThinBorder (stone_background tiled inside global/border's
## 4 px lines and corners) and StoneButtonFancy (StoneButton; over: the selected
## stone with its 32x28 left and right trims; pressed: the same with add_color
## 60 42 42; disabled: StoneButton with add_color 42 42 42; white
## sans-bold-stroke-14 text, 210 210 210 160 when disabled).

const DIR := "res://assets/textures/ui/global/"
## real_texture_placement="0 0 256 28" and "0 0 32 28": the top 28 rows of the 32 px art.
const BUTTON_REGION := Rect2(0, 0, 256, 28)
const TRIM_REGION := Rect2(0, 0, 32, 28)

static var _theme: Theme

## Tiled textures drawn into rects of the box: [texture, anchors (l, t, r, b), offsets (l, t, r, b)].
class TiledBox extends StyleBox:
	var parts: Array = []

	func _draw(to_canvas_item: RID, rect: Rect2) -> void:
		for p in parts:
			var t: Texture2D = p[0]
			var a: Array = p[1]
			var o: Array = p[2]
			var r := Rect2()
			r.position = rect.position + Vector2(rect.size.x * a[0] + o[0], rect.size.y * a[1] + o[1])
			r.end = rect.position + Vector2(rect.size.x * a[2] + o[2], rect.size.y * a[3] + o[3])
			if r.size.x > 0.0 and r.size.y > 0.0:
				RenderingServer.canvas_item_add_texture_rect(to_canvas_item, r, t.get_rid(), true)

## A sprite of stretched images: [texture, source region, anchors, offsets].
class SpriteBox extends StyleBox:
	var images: Array = []

	func _draw(to_canvas_item: RID, rect: Rect2) -> void:
		for p in images:
			var t: Texture2D = p[0]
			var a: Array = p[2]
			var o: Array = p[3]
			var r := Rect2()
			r.position = rect.position + Vector2(rect.size.x * a[0] + o[0], rect.size.y * a[1] + o[1])
			r.end = rect.position + Vector2(rect.size.x * a[2] + o[2], rect.size.y * a[3] + o[3])
			RenderingServer.canvas_item_add_texture_rect_region(to_canvas_item, r, t.get_rid(), p[1])

## The session menu's skin; set it with `theme = StoneTheme.theme()`.
static func theme() -> Theme:
	if _theme == null:
		_theme = _build()
	return _theme

static func _tex(file: String) -> Texture2D:
	return load(DIR + file) if ResourceLoader.exists(DIR + file) else null

## sprites.xml <effect add_color>: the colour added to every pixel (alpha kept).
static func _added(t: Texture2D, add: Color) -> Texture2D:
	if t == null:
		return null
	var img := t.get_image()
	if img == null:
		return t
	if img.is_compressed():
		img.decompress()
	img.convert(Image.FORMAT_RGBA8)
	for y in img.get_height():
		for x in img.get_width():
			var c := img.get_pixel(x, y)
			img.set_pixel(x, y, Color(minf(c.r + add.r, 1.0), minf(c.g + add.g, 1.0), minf(c.b + add.b, 1.0), c.a))
	return ImageTexture.create_from_image(img)

static func panel_thin_border() -> StyleBox:
	var sb := TiledBox.new()
	var parts := [
		["stone_background.png", [0, 0, 1, 1], [4, 4, -4, -4]],
		["border/line_horiz.png", [0, 0, 1, 0], [4, 0, -4, 4]],
		["border/line_vert.png", [1, 0, 1, 1], [-4, 4, 0, -4]],
		["border/line_horiz.png", [0, 1, 1, 1], [4, -4, -4, 0]],
		["border/line_vert.png", [0, 0, 0, 1], [0, 4, 4, -4]],
		["border/line_corner_top_right.png", [1, 0, 1, 0], [-4, 0, 0, 4]],
		["border/line_corner_bottom_right.png", [1, 1, 1, 1], [-4, -4, 0, 0]],
		["border/line_corner_bottom_left.png", [0, 1, 0, 1], [0, -4, 4, 0]],
		["border/line_corner_top_left.png", [0, 0, 0, 0], [0, 0, 4, 4]]]
	for p in parts:
		var t := _tex(p[0])
		if t != null:
			sb.parts.append([t, p[1], p[2]])
	# Menu.xml: the buttons sit at 4 4 100%-4 32.
	sb.set_content_margin_all(4)
	return sb

static func _button(stone: Texture2D, trims: Array) -> StyleBox:
	var sb := SpriteBox.new()
	if stone != null:
		sb.images.append([stone, BUTTON_REGION, [0, 0, 1, 1], [0, 0, 0, 0]])
	if trims.size() == 2 and trims[0] != null and trims[1] != null:
		sb.images.append([trims[0], TRIM_REGION, [0, 0, 0, 0], [0, 0, 32, 28]])
		sb.images.append([trims[1], TRIM_REGION, [1, 0, 1, 0], [-32, 0, 0, 28]])
	sb.content_margin_left = 6
	sb.content_margin_right = 6
	return sb

static func _build() -> Theme:
	var t := Theme.new()
	var unselected := _tex("button_stone_unselected.png")
	var selected := _tex("button_stone_selected.png")
	var trims := [_tex("button_stone_selected_left_trim.png"), _tex("button_stone_selected_right_trim.png")]
	var glow := Color8(60, 42, 42, 0)
	var over := _button(selected, trims)
	var pressed := _button(_added(selected, glow), [_added(trims[0], glow), _added(trims[1], glow)])
	t.set_stylebox("normal", "Button", _button(unselected, []))
	t.set_stylebox("hover", "Button", over)
	t.set_stylebox("pressed", "Button", pressed)
	t.set_stylebox("hover_pressed", "Button", pressed)
	t.set_stylebox("disabled", "Button", _button(_added(unselected, Color8(42, 42, 42, 0)), []))
	# The GUI draws no focus frame on a button.
	t.set_stylebox("focus", "Button", StyleBoxEmpty.new())
	for c in ["font_color", "font_hover_color", "font_pressed_color", "font_hover_pressed_color", "font_focus_color"]:
		t.set_color(c, "Button", Color.WHITE)
	t.set_color("font_disabled_color", "Button", Color8(210, 210, 210, 160))
	t.set_font_size("font_size", "Button", 14)
	t.set_stylebox("panel", "PanelContainer", panel_thin_border())
	return t
