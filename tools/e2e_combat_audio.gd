extends SceneTree
# Combat sounds as 0.28's SoundManager plays them (SoundGroup::PlayNext, CSoundManager):
#   1. a hit near where the camera looks is heard;
#   2. one beyond the spatial players' 75 m is culled before it is played;
#   3. at most 16 spatial voices sound at once, from a reused pool.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_combat_audio.gd
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
	var a: Dictionary = main.get_audio_state()
	var at: Vector2 = a["listener"]
	var played0 := int(a["combat_sounds"])
	var culled0 := int(a["combat_culled"])
	print("1. Near the camera")
	check(main.debug_combat_sound(at.x + 5.0, at.y), "a hit 5 m from where the camera looks plays")
	a = main.get_audio_state()
	check(int(a["combat_sounds"]) == played0 + 1 and int(a["combat_culled"]) == culled0, "counted as played")
	print("2. Far away")
	check(not main.debug_combat_sound(at.x + 120.0, at.y), "a hit 120 m away is not played")
	a = main.get_audio_state()
	check(int(a["combat_sounds"]) == played0 + 1 and int(a["combat_culled"]) == culled0 + 1, "it is culled")
	print("3. Voices")
	var most := 0
	for i in 40:
		main.debug_combat_sound(at.x + float(i % 7), at.y)
		most = maxi(most, int(main.get_audio_state()["voices"]))
	a = main.get_audio_state()
	print("    most %d, dropped %d" % [most, a["voices_dropped"]])
	check(most == 16 and int(a["voices_dropped"]) > 0, "no more than 16 voices at once; the rest are dropped")
	check(int(a["combat_sounds"]) == played0 + 41, "every near hit went to the pool")
	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
