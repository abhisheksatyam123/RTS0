extends Control
## ModernSortedList's heading sprites (COList::DrawList, gui/common/modern/styles.xml): every
## heading of a sortable list shows, in its top-right 16 px square, ModernArrowUp on the column
## sorted ascending, ModernArrowDown on one sorted descending and ModernNotSorted on the others,
## each image 6 px down in that square (size "0 6 16 22"). Godot 4.3's Tree headings take no
## icons, so this overlay on the Tree draws them.

const MODERN := preload("res://scenes/common/modern_theme.gd")

var _tree: Tree
var _column := -1
var _order := 1
var _up: Texture2D
var _down: Texture2D
var _not_sorted: Texture2D

## Puts the overlay on `tree` (once) and returns it.
static func attach(tree: Tree) -> Control:
	for c in tree.get_children(true):
		if c.has_meta("sort_arrows"):
			return c
	var o: Control = load("res://scenes/common/sort_arrows.gd").new()
	o.set_meta("sort_arrows", true)
	o._tree = tree
	tree.add_child(o, false, Node.INTERNAL_MODE_BACK)
	return o

func _ready() -> void:
	mouse_filter = MOUSE_FILTER_IGNORE
	set_anchors_preset(PRESET_FULL_RECT)
	_up = MODERN._tex("arrow-up.png")
	_down = MODERN._tex("arrow-down.png")
	_not_sorted = MODERN._tex("arrow-up-down.png")
	_tree.draw.connect(queue_redraw)

## The sorted column (-1 for none) and its order (> 0 ascending).
func set_sorted(column: int, order: int) -> void:
	_column = column
	_order = order
	queue_redraw()

## Where heading `column`'s sprite is drawn, in the Tree's coordinates.
func arrow_rect(column: int) -> Rect2:
	var bg := _tree.get_theme_stylebox("panel")
	var x := bg.get_margin(SIDE_LEFT) - _tree.get_scroll().x
	for c in column:
		x += _tree.get_column_width(c)
	return Rect2(x + _tree.get_column_width(column) - 16, bg.get_margin(SIDE_TOP) + 6, 16, 16)

## What heading `column` shows: "asc" (ModernArrowUp), "desc" (ModernArrowDown) or "" (ModernNotSorted).
func sorted_as(column: int) -> String:
	if column != _column:
		return ""
	return "asc" if _order > 0 else "desc"

func arrow_for(column: int) -> Texture2D:
	return {"asc": _up, "desc": _down}.get(sorted_as(column), _not_sorted)

func _draw() -> void:
	if not _tree.column_titles_visible:
		return
	for c in _tree.columns:
		var t := arrow_for(c)
		var r := arrow_rect(c)
		if t and r.position.x >= 0.0 and r.end.x <= size.x:
			draw_texture_rect(t, arrow_rect(c), false)
