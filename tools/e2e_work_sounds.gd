extends SceneTree
# 0.28's gather sounds (CUnitAnimation): a worker's gather clip plays its Sound
# group once per loop at the clip's sound or event point, so workers in earshot
# are heard about once per swing, never more than one sound per worker per
# 0.8 s (the shortest work clip, the male build).
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_work_sounds.gd
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
	main.find_children("*", "RtsoCameraRig", true, false)[0].set_edge_scroll_enabled(false)
	main.debug_gather_nearest()
	# Walk there and start working.
	var a: Dictionary = {}
	for i in 30:
		await frames(20)
		a = main.get_audio_state()
		if int(a["workers_in_earshot"]) > 0:
			break
	var heard := int(a["workers_in_earshot"])
	check(heard > 0, "workers at work in earshot: %d" % heard)
	var before := int(a["work_sounds"])
	# 10 s of game time, whatever the frame rate (a fruit gatherer's loop is
	# 4.1-6.6 s, so 200 frames at 60 fps could end before any second swing).
	var waited := 0.0
	while waited < 10.0:
		await process_frame
		waited += main.get_process_delta_time()
	a = main.get_audio_state()
	var played := int(a["work_sounds"]) - before
	check(played > 0, "their swings are heard: %d sounds in 10 s" % played)
	# At most one sound per worker per loop, at least 0.8 s each, and 3 per half second overall.
	check(played <= mini(heard * 14, 60), "no more than one per loop: %d for %d workers" % [played, heard])
	print("RESULT: %s" % ("PASS" if failures == 0 else "FAIL (%d)" % failures))
	quit(0 if failures == 0 else 1)
