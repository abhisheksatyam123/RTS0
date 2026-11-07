extends SceneTree
# RTS0 Vulkan In-Engine Frame-Time Benchmark (zoomed-out / zoomed-in battle scenarios).
# Reports p50/p95/p99/max frame ms over a fixed frame count, the sorted frame list
# (optional JSON file), and the Viewport render info draw-call count per frame.
#
# Usage: see docs/RENDER_FRAME_MEASURE.md
#   godot --rendering-driver vulkan --rendering-method forward_plus -s tools/bench_vulkan.gd -- \
#       --units 1000 --camera zoomed-out --frames 300 --warmup 60 --seed 42 [--json out.json]

var args := {
	"units": "1000",
	"frames": "300",
	"warmup": "60",
	"map_size": "256",
	"seed": "42",
	"camera": "center", # center | away | zoomed-out | zoomed-in
	"zoom": "", # optional zoom override (rig zoom units) for any camera mode
	"vsync": "0",
	"json": "" # optional absolute path: write the full result (incl. sorted frame list) as JSON
}

# Camera presets: yaw, pitch (deg), zoom (rig units), x/z focus.
# zoomed-out: pulled back so the whole army is on screen and far from the lens.
# zoomed-in: close to the army (zoom 25, as requested for the zoomed-in comparison).
const CAMERA_PRESETS := {
	"center": {"x": 0.0, "z": 0.0, "yaw": 0.0, "pitch": 45.0, "zoom": 35.0},
	"away": {"x": 100.0, "z": 100.0, "yaw": 2.3, "pitch": 40.0, "zoom": 45.0},
	"zoomed-out": {"x": 0.0, "z": 0.0, "yaw": 0.0, "pitch": 45.0, "zoom": 120.0},
	"zoomed-in": {"x": 0.0, "z": 0.0, "yaw": 0.0, "pitch": 45.0, "zoom": 25.0},
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

	if not CAMERA_PRESETS.has(args["camera"]):
		print("ERROR: unknown --camera ", args["camera"], " (use ", ", ".join(CAMERA_PRESETS.keys()), ")")
		quit(2)
		return

	if args["vsync"] == "0":
		DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)

	print("=== RTS0 Vulkan In-Engine Benchmark ===")
	print("Settings: units=", args["units"], " frames=", args["frames"], " warmup=", args["warmup"],
		" camera=", args["camera"], " seed=", args["seed"], " vsync=", args["vsync"])

	var ms = root.get_node("MatchSettings")
	if ms:
		ms.map_id = "random_mainland"
		ms.seed = int(args["seed"])
		ms.map_size = int(args["map_size"])
		ms.slot_teams = [1, 2]
		ms.slot_types = [0, 3]

	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func pct(sorted_v: Array, p: float) -> float:
	if sorted_v.is_empty():
		return 0.0
	return sorted_v[mini(int(p * (sorted_v.size() - 1) + 0.5), sorted_v.size() - 1)]

func mean(v: Array) -> float:
	var t := 0.0
	for x in v:
		t += x
	return t / maxf(1.0, v.size())

# Draw calls and objects rendered in the last frame, over all viewports (global counter;
# the per-viewport Viewport.get_render_info on the root reports only a small part of the frame).
func render_counts() -> Vector2i:
	var draws: int = RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_TOTAL_DRAW_CALLS_IN_FRAME)
	var objects: int = RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_TOTAL_OBJECTS_IN_FRAME)
	return Vector2i(draws, objects)

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
	var cam_data: Dictionary = CAMERA_PRESETS[args["camera"]].duplicate()
	if args["zoom"] != "":
		cam_data["zoom"] = float(args["zoom"])
	var rig = main.find_children("*", "RtsoCameraRig", true, false)
	if rig.size() > 0:
		var cam_rig = rig[0]
		cam_rig.set_edge_scroll_enabled(false)
		cam_rig.set_cinema_view(true)
		cam_rig.set_camera_data(cam_data)
	else:
		print("WARNING: no RtsoCameraRig found, camera not positioned")

	# Warmup frames
	var warmup := int(args["warmup"])
	await frames(warmup)

	# Benchmark run
	var total_frames := int(args["frames"])
	var frame_times := []
	var draw_calls := []
	var objects := []

	var last_tick := Time.get_ticks_usec()
	for f in total_frames:
		await process_frame
		var now := Time.get_ticks_usec()
		var dt_ms := (now - last_tick) / 1000.0
		last_tick = now
		frame_times.append(dt_ms)
		var c := render_counts()
		draw_calls.append(c.x)
		objects.append(c.y)

	var sorted_ms := frame_times.duplicate()
	sorted_ms.sort()
	var sorted_draws := draw_calls.duplicate()
	sorted_draws.sort()
	var sorted_objects := objects.duplicate()
	sorted_objects.sort()
	var avg_ms := mean(frame_times)
	var p50_ms := pct(sorted_ms, 0.50)
	var p95_ms := pct(sorted_ms, 0.95)
	var p99_ms := pct(sorted_ms, 0.99)
	var max_ms: float = sorted_ms[-1]
	var fps := 1000.0 / avg_ms if avg_ms > 0 else 0.0
	var draw_med := pct(sorted_draws, 0.50)
	var draw_max: int = sorted_draws[-1]

	print("=== BENCHMARK RESULTS (Vulkan Forward+) ===")
	print("Units: %d  Frames: %d  Camera: %s  Zoom: %s  Seed: %s  VSync: %s" % [spawned, total_frames, args["camera"],
		str(cam_data["zoom"]), args["seed"], args["vsync"]])
	print("Frame Time: mean=%.2f ms  p50=%.2f ms  p95=%.2f ms  p99=%.2f ms  max=%.2f ms" % [avg_ms, p50_ms, p95_ms, p99_ms, max_ms])
	print("FPS: %.1f fps" % fps)
	print("Draw calls/frame: median=%d  max=%d  (objects median=%d)" % [draw_med, draw_max, pct(sorted_objects, 0.50)])

	var result := {
		"units": spawned,
		"frames": total_frames,
		"warmup": warmup,
		"seed": int(args["seed"]),
		"camera": args["camera"],
		"zoom": cam_data["zoom"],
		"mean_ms": avg_ms,
		"p50_ms": p50_ms,
		"p95_ms": p95_ms,
		"p99_ms": p99_ms,
		"max_ms": max_ms,
		"fps": fps,
		"draw_calls_median": draw_med,
		"draw_calls_max": draw_max,
		"objects_median": pct(sorted_objects, 0.50),
		"frame_ms_sorted": sorted_ms,
	}
	if args["json"] != "":
		var f := FileAccess.open(args["json"], FileAccess.WRITE)
		if f:
			f.store_string(JSON.stringify(result))
			f.close()
			print("Wrote ", args["json"])
	# Compact summary without the full frame list (the list goes to --json only)
	var summary := result.duplicate()
	summary.erase("frame_ms_sorted")
	print("JSON_RESULT: ", JSON.stringify(summary))
	quit(0)
