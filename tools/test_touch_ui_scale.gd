extends SceneTree
# Headless check of scenes/common/touch_ui_scale.gd: the factor is 1 on desktop,
# grows with DPI on a phone, is clamped to [1, MAX_FACTOR], and falls back to 160 dpi
# when the platform reports none.
#
#   godot --headless --path . -s tools/test_touch_ui_scale.gd
# Exit code 0 = pass.

const TouchUiScale := preload("res://scenes/common/touch_ui_scale.gd")

var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	# Desktop never scales, whatever the DPI.
	check(TouchUiScale.factor_for(96.0, 1.0, false) == 1.0, "desktop keeps factor 1.0")
	check(TouchUiScale.factor_for(400.0, 1.5, false) == 1.0, "desktop at phone DPI keeps factor 1.0")

	# A 400 dpi phone in landscape (2340x1080 is 1.5 physical px per design px) hits the cap.
	var phone := TouchUiScale.factor_for(400.0, 1.5, true)
	check(is_equal_approx(phone, TouchUiScale.MAX_FACTOR), "phone at 400 dpi is clamped to MAX_FACTOR (%.2f)" % phone)

	# A missing DPI (0) falls back to 160 dpi, which gives a smaller factor than 400.
	var fallback := TouchUiScale.factor_for(0.0, 1.5, true)
	check(fallback >= 1.0 and fallback < phone, "missing DPI falls back to 160 (%.2f < %.2f)" % [fallback, phone])

	# A low-DPI touch screen never shrinks the UI below 1.0.
	check(TouchUiScale.factor_for(50.0, 1.0, true) == 1.0, "low DPI never drops below factor 1.0")

	# Denser screens never get a smaller factor.
	var prev := 0.0
	var monotonic := true
	for dpi in [120.0, 160.0, 240.0, 320.0, 400.0, 480.0]:
		var f := TouchUiScale.factor_for(dpi, 1.5, true)
		if f < prev:
			monotonic = false
		prev = f
	check(monotonic, "factor never falls as DPI rises")

	# Zero or negative scale does not divide by zero.
	var safe := TouchUiScale.factor_for(400.0, 0.0, true)
	check(safe >= 1.0 and safe <= TouchUiScale.MAX_FACTOR, "zero scale is clamped, not infinite (%.2f)" % safe)

	print("test_touch_ui_scale: %s (%d failure(s))" % ["PASS" if failures == 0 else "FAIL", failures])
	quit(1 if failures else 0)
