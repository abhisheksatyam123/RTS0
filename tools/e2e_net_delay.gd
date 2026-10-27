extends SceneTree
# 0.28's NetworkDelayOverlay: once a second the session checks how many turns
# this client holds but hasn't run; from 6 on, "Delay to live stream: 1.2s"
# shows top right (5 turns a second, one decimal under 5 s), and it hides again
# below 6. A single-player game never shows it.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_net_delay.gd
# Exit code 0 = pass.

var main
var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func run() -> void:
	await frames(60)
	check(main.debug_net_delay_text() == "", "a single-player game shows no delay")
	main.debug_set_net_delay(5)
	await frames(2)
	check(main.debug_net_delay_text() == "", "5 turns behind is within the command delay")
	main.debug_set_net_delay(6)
	await frames(2)
	check(main.debug_net_delay_text() == "Delay to live stream: 1.2s", "6 turns: " + main.debug_net_delay_text())
	main.debug_set_net_delay(40)
	await frames(2)
	check(main.debug_net_delay_text() == "Delay to live stream: 8s", "40 turns: " + main.debug_net_delay_text())
	var label: Label = main.find_child("NetDelayOverlay", true, false)
	check(label != null and label.horizontal_alignment == HORIZONTAL_ALIGNMENT_RIGHT, "top right, right-aligned")
	check(label != null and str(label.get_theme_font("font").resource_path).ends_with("DejaVuSansMono.ttf"), "in mono-10")
	# NetworkStatusOverlay: sessionOverlayBackground (0 0 0 185), sans-bold-18 text, a StoneButton Exit.
	var ns: Control = main.find_child("NetStatusOverlay", true, false)
	var dim: ColorRect = ns.get_child(0) if ns else null
	var exit: Button = main.find_child("DisconnectedExitButton", true, false)
	check(dim != null and is_equal_approx(dim.color.a, 185.0 / 255.0) and exit != null and exit.theme != null
			and ns.get_child(1).get_theme_font("font") is FontVariation, "the network status overlay's backdrop, bold text and stone Exit button")
	main.debug_set_net_delay(3)
	await frames(2)
	check(main.debug_net_delay_text() == "" and not label.visible, "caught up: hidden")
	main.debug_set_net_delay(-1)
	print("RESULT: %s" % ("PASS" if failures == 0 else "FAIL (%d)" % failures))
	quit(0 if failures == 0 else 1)
