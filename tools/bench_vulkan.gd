extends SceneTree
# RTS0 Vulkan In-Engine Performance Benchmark
# Measures real frame-times, FPS, and GPU/CPU behavior under Vulkan Forward+ renderer.
#
# Usage:
#   DISPLAY=:0 godot --rendering-driver vulkan --rendering-method forward_plus -s tools/bench_vulkan.gd -- \
#       --units 3000 --frames 120 --camera center [--vsync 0]

var args := {
	"units": "1000",
	"frames": "120",
	"warmup": "30",
	"map_size": "256",
	"camera": "center", # "center" (looking at units), "away" (camera facing away from army)
	"vsync": "0"
}

var main: Node
var spawned := 0

func _initialize() -> void:
	var a := OS.get_cmdline_user_args()
	var i := 0
	while i < a.size():
		var k := a[i].trim_prefix("--")
		if args.has(k) and i + 1 < a.size():
			args[k] = a[i + 1]
			i += 1
		i += 1

	if args["vsync"] == "0":
		DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)

	print("=== RTS0 Vulkan In-Engine Benchmark ===")
	print("Settings: units=", args["units"], " frames=", args["frames"], " camera=", args["camera"], " vsync=", args["vsync"])

	var ms = root.get_node("MatchSettings")
	if ms:
		ms.map_id = "random_mainland"
		ms.seed = 42
		ms.map_size = int(args["map_size"])
		ms.slot_teams = [1, 2]
		ms.slot_types = [0, 3]

	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func get_gpu_freq() -> int:
	var f := FileAccess.open("/sys/class/drm/card1/gt_cur_freq_mhz", FileAccess.READ)
	if f:
		var line = f.get_line().strip_edges()
		f.close()
		return line.to_int()
	return 0

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

func run() -> void:
	# Wait until sim is initialized
	var guard := 0
	while true:
		var test = main.debug_spawn_units("athen/infantry_spearman_b", 1, 0, 0)
		if test.size() > 0:
			spawned += test.size()
			break
		await process_frame
		guard += 1
		if guard > 200:
			print("ERROR: Sim failed to initialize")
			quit(1)
			return

	var target_units := int(args["units"])
	var to_spawn := target_units - spawned
	var chunk := 250
	while to_spawn > 0:
		var count := mini(to_spawn, chunk)
		var ids = main.debug_spawn_units("athen/infantry_spearman_b", count, 0, 0)
		spawned += ids.size()
		to_spawn -= count
		await process_frame

	print("Successfully spawned ", spawned, " units")

	# Position camera
	var rig = main.find_children("*", "RtsoCameraRig", true, false)
	if rig.size() > 0:
		var cam_rig = rig[0]
		cam_rig.set_edge_scroll_enabled(false)
		if args["camera"] == "away":
			# Point camera far away looking into empty terrain corner
			cam_rig.position = Vector3(120.0, 30.0, 120.0)
			cam_rig.rotation = Vector3(-0.6, 2.3, 0.0)
		else:
			# Look directly down at the spawned army
			cam_rig.position = Vector3(0.0, 35.0, 30.0)
			cam_rig.rotation = Vector3(-0.8, 0.0, 0.0)

	# Warmup frames
	var warmup := int(args["warmup"])
	await frames(warmup)

	# Benchmark run
	var total_frames := int(args["frames"])
	var frame_times := []
	var gpu_freqs := []

	var last_tick := Time.get_ticks_usec()
	for f in total_frames:
		await process_frame
		var now := Time.get_ticks_usec()
		var dt_ms := (now - last_tick) / 1000.0
		last_tick = now
		frame_times.append(dt_ms)
		if f % 10 == 0:
			gpu_freqs.append(get_gpu_freq())

	var avg_ms := mean(frame_times)
	var p50_ms := pct(frame_times, 0.50)
	var p95_ms := pct(frame_times, 0.95)
	var max_ms := pct(frame_times, 1.00)
	var fps := 1000.0 / avg_ms if avg_ms > 0 else 0.0
	var avg_gpu := mean(gpu_freqs) if gpu_freqs.size() > 0 else 0.0

	print("=== BENCHMARK RESULTS (Vulkan Forward+) ===")
	print("Units: %d  Frames: %d  Camera: %s  VSync: %s" % [spawned, total_frames, args["camera"], args["vsync"]])
	print("Frame Time: mean=%.2f ms  p50=%.2f ms  p95=%.2f ms  max=%.2f ms" % [avg_ms, p50_ms, p95_ms, max_ms])
	print("FPS: %.1f fps" % fps)
	print("GPU Freq: %.0f MHz (current active: %d MHz)" % [avg_gpu, get_gpu_freq()])

	var result := {
		"units": spawned,
		"frames": total_frames,
		"camera": args["camera"],
		"mean_ms": avg_ms,
		"p50_ms": p50_ms,
		"p95_ms": p95_ms,
		"max_ms": max_ms,
		"fps": fps,
		"gpu_freq_mhz": avg_gpu
	}
	print("JSON_RESULT: ", JSON.stringify(result))
	quit(0)
