extends RefCounted

## Touch-only UI scale for phones and tablets.
##
## The UI is laid out on a 1280x720 design canvas (project.godot, canvas_items).
## On a phone that canvas is scaled down to the screen, so a 28 px menu button is
## only a few millimetres tall. Platform touch guidelines (48 dp on Android,
## 44 pt on iOS) want about 9 mm. Reaching that would need about 99 design px per
## button on a 1080-px-tall screen, so the scale is capped: it grows the UI enough
## to be easier to hit without leaving the layouts with too little room.
##
## Desktop never scales (the factor is 1.0).

const TARGET_MM := 9.0
const BUTTON_PX := 28.0 # the design height of a menu button
const MAX_FACTOR := 1.6

## The factor for a screen of `dpi`, with `scale` physical px per design px.
## Pure, so a headless test can check the numbers without a device.
static func factor_for(dpi: float, scale: float, touch: bool) -> float:
	if not touch:
		return 1.0
	var d := dpi if dpi > 0.0 else 160.0
	var s := maxf(scale, 0.001)
	var target_px := TARGET_MM / 25.4 * d
	return clampf(target_px / (BUTTON_PX * s), 1.0, MAX_FACTOR)

## Apply the factor to the root window. `scale` is the current physical px per
## design px, read from the viewport.
static func apply(root: Window, scale: float, touch: bool) -> float:
	var f := factor_for(DisplayServer.screen_get_dpi(), scale, touch)
	root.content_scale_factor = f
	return f
