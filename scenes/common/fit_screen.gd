extends RefCounted
## Keeps a centred dialog of a fixed design size on the screen: on a short screen (a
## phone in landscape, with the bigger phone UI scale) its minimum size shrinks to the
## viewport less a margin, so its title and close buttons stay reachable. At 1280x720
## and up the design size fits and nothing changes.

const MARGIN := 8.0

static func cap(panel: Control, design: Vector2) -> void:
	panel.custom_minimum_size = design
	var fit := func() -> void:
		if is_instance_valid(panel) and panel.is_inside_tree():
			var room := panel.get_viewport_rect().size - Vector2(MARGIN, MARGIN) * 2.0
			panel.custom_minimum_size = design.min(room)
	var hook := func() -> void:
		fit.call()
		var vp := panel.get_viewport()
		if not vp.size_changed.is_connected(fit):
			vp.size_changed.connect(fit)
			panel.tree_exiting.connect(func(): vp.size_changed.disconnect(fit), CONNECT_ONE_SHOT)
	if panel.is_inside_tree():
		hook.call()
	else:
		panel.tree_entered.connect(hook, CONNECT_ONE_SHOT)
