extends Node
## Keeps panels on the screen's edges clear of a phone's notch, rounded corners and home
## indicator. iOS draws the game under them in landscape (Android letterboxes the cutout),
## so on a phone every Control at the top level of a page or a CanvasLayer that hangs on
## a screen edge moves in by DisplayServer.get_display_safe_area()'s insets: one anchored
## left moves right, one anchored right moves left, one stretched across shrinks, and the
## same top and bottom. Centred panels, full-screen overlays and the controls that follow
## the pointer keep their place. Desktops have no insets.
## `-- --safe-area=L,T,R,B` (window pixels) simulates them anywhere (tools/shot_layouts.gd).

const META := "rtso_safe_inset"
## Drawn where the pointer is, or over the whole view: never moved.
const SKIP_CLASSES := ["RtsoSelectionRect", "RtsoDebugOverlay"]

var _insets := Vector4.ZERO # left, top, right, bottom in UI pixels

func _ready() -> void:
	if _forced().is_empty() and not OS.has_feature("mobile"):
		return
	get_tree().node_added.connect(_on_node_added)
	get_tree().root.size_changed.connect(_refresh.call_deferred)
	_refresh.call_deferred()

## The insets now, in UI pixels (left, top, right, bottom).
func insets() -> Vector4:
	return _insets

static func _forced() -> Array:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--safe-area="):
			var p := a.trim_prefix("--safe-area=").split(",")
			if p.size() == 4:
				return [Vector4(float(p[0]), float(p[1]), float(p[2]), float(p[3]))]
	return []

## The insets in window pixels (left, top, right, bottom); zero on a desktop.
static func window_insets() -> Vector4:
	var forced := _forced()
	if not forced.is_empty():
		return forced[0]
	if not OS.has_feature("mobile"):
		return Vector4.ZERO
	var screen := Vector2(DisplayServer.screen_get_size())
	var safe := Rect2(DisplayServer.get_display_safe_area())
	if safe.size.x <= 0.0 or safe.size.y <= 0.0:
		return Vector4.ZERO
	return Vector4(safe.position.x, safe.position.y, screen.x - safe.end.x, screen.y - safe.end.y).max(Vector4.ZERO)

func _refresh() -> void:
	var scale := maxf(get_tree().root.get_final_transform().get_scale().x, 0.001)
	# A notch or a home indicator takes a sliver of the screen: anything past an eighth of
	# its width or height is a bad report (or a headless window a few pixels wide).
	var vis := get_tree().root.get_visible_rect().size
	var cap := Vector4(vis.x, vis.y, vis.x, vis.y) * 0.125
	_insets = (window_insets() / scale).min(cap)
	for n in get_tree().root.find_children("*", "Control", true, false):
		_apply(n)

func _on_node_added(n: Node) -> void:
	if n is Control:
		_apply.call_deferred(n)

func _edge_control(c: Control) -> bool:
	var p := c.get_parent()
	if p is CanvasLayer:
		return true
	# A page's own panels: the children of a full-screen page at the top of the tree.
	return p is Control and p.get_parent() == get_tree().root and _full_rect(p)

static func _full_rect(c: Control) -> bool:
	return c.anchor_left == 0.0 and c.anchor_top == 0.0 and c.anchor_right == 1.0 and c.anchor_bottom == 1.0

func _apply(c) -> void:
	if not is_instance_valid(c) or not c.is_inside_tree() or not _edge_control(c) or _full_rect(c):
		return
	if c.get_class() in SKIP_CLASSES or c.top_level:
		return
	var old: Vector4 = c.get_meta(META, Vector4.ZERO)
	if old == _insets:
		return
	var d := _insets - old
	c.set_meta(META, _insets)
	# Horizontal: hang left -> right by L; hang right -> left by R; stretched -> both.
	if c.anchor_left == 0.0:
		c.offset_left += d.x
	elif c.anchor_left == 1.0:
		c.offset_left -= d.z
	if c.anchor_right == 0.0:
		c.offset_right += d.x
	elif c.anchor_right == 1.0:
		c.offset_right -= d.z
	if c.anchor_top == 0.0:
		c.offset_top += d.y
	elif c.anchor_top == 1.0:
		c.offset_top -= d.w
	if c.anchor_bottom == 0.0:
		c.offset_bottom += d.y
	elif c.anchor_bottom == 1.0:
		c.offset_bottom -= d.w
