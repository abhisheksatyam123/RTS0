extends SceneTree
# RTS0 client frame-time benchmark: loads a saved game (a late-game save from
# tools/bench_late.cpp --save-at, say) into the session scene and times each
# frame's _process, which runs the simulation's ticks for the frame and then
# syncs the world view, minimap and HUD from it. Run headless the renderer is a
# dummy, so this is the CPU side of a frame only (what the main thread spends
# before drawing), not GPU time.
#
#   godot --headless --path . --fixed-fps 60 -s tools/bench_client.gd -- \
#       --load /tmp/late.sav --frames 600 [--warmup 60] [--observer]
#
# --observer watches as an observer (every unit drawn, the map revealed), the
# heaviest view; otherwise the save's own player looks on. Prints mean, p50, p95
# and max of the frame's wall time (nothing waits between frames headless with
# --fixed-fps), then a JSON line. At --fixed-fps 20 every
# frame runs one simulation tick (50 ms of match time); at 60 one frame in three
# does. Exit code 0 when the save loaded.

var main
var args := {"load": "", "frames": "600", "warmup": "60", "observer": ""}

func _initialize() -> void:
	var a := OS.get_cmdline_user_args()
	var i := 0
	while i < a.size():
		var k: String = a[i].trim_prefix("--")
		if k == "observer":
			args[k] = "1"
		elif i + 1 < a.size():
			args[k] = a[i + 1]
			i += 1
		i += 1
	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func pct(v: Array, p: float) -> float:
	if v.is_empty():
		return 0.0
	var s := v.duplicate()
	s.sort()
	return s[mini(int(p * (s.size() - 1) + 0.5), s.size() - 1)]

func mean(v: Array) -> float:
	var t := 0.0
	for x in v:
		t += x
	return t / maxf(1.0, v.size())

func line(name: String, v: Array) -> String:
	return "%-10s n=%4d  mean %6.2f  p50 %6.2f  p95 %6.2f  max %6.2f ms" % [name, v.size(), mean(v), pct(v, 0.5), pct(v, 0.95), pct(v, 1.0)]

func observe() -> void:
	main.toggle_dev_overlay()
	await frames(2)
	for o in main.find_children("*", "OptionButton", true, false):
		if o.is_visible_in_tree() and o.get_item_count() > 0 and o.get_item_text(0) == "Observer":
			o.select(0)
			o.item_selected.emit(0)
	await frames(10)
	main.toggle_dev_overlay()

func run() -> void:
	await frames(60)
	var rig = main.find_children("*", "RtsoCameraRig", true, false)
	if rig.size() > 0:
		rig[0].set_edge_scroll_enabled(false)
	if args["load"] != "":
		if not main.load_game(args["load"]):
			print("could not load ", args["load"])
			quit(2)
			return
	if args["observer"] != "":
		await observe()
	await frames(int(args["warmup"])) # models attach and pools fill on the first frames after a load
	var all := []
	var n := int(args["frames"])
	print("[bench_client] measuring ", n, " frames")
	var t0: float = main.get_overlay_times()["elapsed"]
	var last := Time.get_ticks_usec()
	for i in n:
		await process_frame
		var now := Time.get_ticks_usec()
		all.append((now - last) / 1000.0) # headless with --fixed-fps nothing waits between frames
		last = now
	var played: float = main.get_overlay_times()["elapsed"] - t0
	print(line("frame", all))
	print(JSON.stringify({"frames": n, "mean_ms": mean(all), "p95_ms": pct(all, 0.95), "max_ms": pct(all, 1.0),
		"match_seconds": played}))
	quit(0)
