extends SceneTree
# RTS0 Realistic Large-Scale Game Performance Benchmark & Diagnostic Suite
# Runs realistic 0 A.D.-style clash / skirmish scenarios under Vulkan Forward+.
# Measures: FPS, frame-times (mean, p50, p95, p99, max), sim tick times,
# draw calls, primitives, GPU frequencies, and logs all engine / sim anomalies.

var args := {
	"mode": "clash",       # "clash" (1500-2000 units massive clash) or "skirmish" (Petra AI economy + army)
	"units": "2000",       # Total units for clash scenario
	"frames": "180",       # Benchmark duration in frames
	"warmup": "20",        # Warmup frames
	"map": "random_mainland",
	"map_size": "256",
	"camera": "clash",     # "clash", "dynamic", "base", "high"
	"vsync": "0"
}

var main: Node
var cam_rig: Node3D

func _initialize() -> void:
	var a := OS.get_cmdline_user_args()
	var i := 0
	while i < a.size():
		var k := a[i].trim_prefix("--")
		if args.has(k) and i + 1 < a.size():
			args[k] = a[i + 1]
			i += 1
		i += 1

	if str(args["vsync"]) == "0":
		DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)

	print("=================================================================")
	print("  RTS0 REALISTIC SCENARIO PERFORMANCE & STRESS BENCHMARK")
	print("=================================================================")
	print("Settings:")
	print("  Mode:       ", args["mode"])
	print("  Units:      ", args["units"])
	print("  Frames:     ", args["frames"], " (warmup: ", args["warmup"], ")")
	print("  Map:        ", args["map"], " (size: ", args["map_size"], "m)")
	print("  Camera:     ", args["camera"])
	print("  VSync:      ", args["vsync"])
	print("-----------------------------------------------------------------")

	var ms = root.get_node_or_null("MatchSettings")
	if ms:
		ms.map_id = str(args["map"])
		ms.seed = 42
		ms.map_size = int(args["map_size"])
		ms.slot_teams = [1, 2, 1, 2]
		ms.slot_types = [0, 1, 1, 1]
		ms.slot_civs = [0, 1, 2, 3]  # Athenians, Spartans, Persians, Carthaginians
		ms.start_res = 2

	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func get_gpu_metric(fname: String) -> int:
	var path := "/sys/class/drm/card1/" + fname
	if FileAccess.file_exists(path):
		var f := FileAccess.open(path, FileAccess.READ)
		if f:
			var s := f.get_line().strip_edges()
			f.close()
			return s.to_int()
	return 0

func pct(v: Array, p: float) -> float:
	if v.is_empty():
		return 0.0
	var s := v.duplicate()
	s.sort()
	return s[mini(int(p * (s.size() - 1) + 0.5), s.size() - 1)]

func mean(v: Array) -> float:
	if v.is_empty():
		return 0.0
	var t := 0.0
	for x in v:
		t += x
	return t / float(v.size())

func update_camera(progress: float) -> void:
	if not cam_rig:
		return
	var mode: String = str(args["camera"])
	if mode == "clash":
		# Dramatic 35-degree angle positioned right at the collision frontline
		cam_rig.position = Vector3(0.0, 24.0, 32.0)
		cam_rig.rotation = Vector3(-0.62, 0.0, 0.0)
	elif mode == "base":
		# Looking at West base structures & workers
		cam_rig.position = Vector3(-60.0, 28.0, 15.0)
		cam_rig.rotation = Vector3(-0.75, -0.4, 0.0)
	elif mode == "high":
		# High bird's eye view over the entire 256m battlefield
		cam_rig.position = Vector3(0.0, 85.0, 45.0)
		cam_rig.rotation = Vector3(-1.15, 0.0, 0.0)
	else:
		# "dynamic": cinematic pan across base -> army march -> clash center -> bird's eye
		if progress < 0.25:
			var t := progress / 0.25
			cam_rig.position = Vector3(-60.0 + t * 40.0, 25.0, 25.0)
			cam_rig.rotation = Vector3(-0.65, -0.2 * (1.0 - t), 0.0)
		elif progress < 0.70:
			var t := (progress - 0.25) / 0.45
			cam_rig.position = Vector3(-10.0 + t * 20.0, 22.0 - t * 2.0, 28.0)
			cam_rig.rotation = Vector3(-0.60, 0.1 * sin(t * 3.14), 0.0)
		else:
			var t := (progress - 0.70) / 0.30
			cam_rig.position = Vector3(0.0, 20.0 + t * 65.0, 28.0 + t * 30.0)
			cam_rig.rotation = Vector3(-0.60 - t * 0.55, 0.0, 0.0)

func run() -> void:
	var guard := 0
	while true:
		if main and main.has_method("debug_spawn_units"):
			var test = main.debug_spawn_units("athen/infantry_spearman_b", 1, 0, 0)
			if test.size() > 0:
				break
		await process_frame
		guard += 1
		if guard > 300:
			print("FATAL: Engine / Simulation failed to initialize within 300 frames")
			quit(1)
			return

	var rigs := main.find_children("*", "RtsoCameraRig", true, false)
	if not rigs.is_empty():
		cam_rig = rigs[0]
		cam_rig.set_edge_scroll_enabled(false)

	var target_units := int(args["units"])
	var mode: String = str(args["mode"])

	if mode == "clash":
		print("[Scenario] Spawning large-scale realistic clash: ", target_units, " units...")
		main.spawn_large_battle(target_units)
		# Allow units to deploy and initiate charges
		await frames(15)
	else:
		print("[Scenario] Running realistic skirmish mode with active Petra AI...")
		main.debug_spawn_units("athen/infantry_spearman_b", 100, 0, 0)
		main.debug_spawn_units("spart/infantry_swordsman_b", 100, 1, 1)

	# Warmup phase
	var warmup_count := int(args["warmup"])
	print("[Benchmark] Running warmup for ", warmup_count, " frames...")
	for w in warmup_count:
		update_camera(float(w) / float(warmup_count))
		await process_frame

	# Main benchmark execution
	var total_frames := int(args["frames"])
	print("[Benchmark] Sampling performance over ", total_frames, " frames...")

	var frame_times := []
	var sim_times := []
	var draw_calls := []
	var primitives := []
	var gpu_act_freqs := []
	var gpu_cur_freqs := []

	var last_tick := Time.get_ticks_usec()

	for f in total_frames:
		var progress := float(f) / float(total_frames)
		update_camera(progress)

		await process_frame

		var now := Time.get_ticks_usec()
		var dt_ms := (now - last_tick) / 1000.0
		last_tick = now

		frame_times.append(dt_ms)
		sim_times.append(Performance.get_monitor(Performance.TIME_PHYSICS_PROCESS) * 1000.0)
		draw_calls.append(Performance.get_monitor(Performance.RENDER_TOTAL_DRAW_CALLS_IN_FRAME))
		primitives.append(Performance.get_monitor(Performance.RENDER_TOTAL_PRIMITIVES_IN_FRAME))

		if f % 10 == 0:
			gpu_act_freqs.append(get_gpu_metric("gt_act_freq_mhz"))
			gpu_cur_freqs.append(get_gpu_metric("gt_cur_freq_mhz"))

	var avg_frame := mean(frame_times)
	var p50_frame := pct(frame_times, 0.50)
	var p95_frame := pct(frame_times, 0.95)
	var p99_frame := pct(frame_times, 0.99)
	var max_frame := pct(frame_times, 1.00)
	var fps := 1000.0 / avg_frame if avg_frame > 0 else 0.0

	var avg_sim := mean(sim_times)
	var p95_sim := pct(sim_times, 0.95)
	var max_sim := pct(sim_times, 1.00)

	var avg_draws := mean(draw_calls)
	var p95_draws := pct(draw_calls, 0.95)
	var avg_prims := mean(primitives)

	var avg_gpu_act := mean(gpu_act_freqs)
	var avg_gpu_cur := mean(gpu_cur_freqs)
	var mem_static_mb := Performance.get_monitor(Performance.MEMORY_STATIC) / (1024.0 * 1024.0)

	print("=================================================================")
	print("  REALISTIC BENCHMARK RESULTS (Vulkan Forward+)")
	print("=================================================================")
	print("Scenario:              ", mode, " (", target_units, " units on ", args["map"], ")")
	print("Camera Profile:        ", args["camera"])
	print("Total Frames Sampled:  ", total_frames)
	print("Frame Time (ms):       mean=%.2f | p50=%.2f | p95=%.2f | p99=%.2f | max=%.2f" % [avg_frame, p50_frame, p95_frame, p99_frame, max_frame])
	print("Framerate (FPS):       %.2f fps" % fps)
	print("Sim Physics Time (ms): mean=%.2f | p95=%.2f | max=%.2f" % [avg_sim, p95_sim, max_sim])
	print("Draw Calls / Frame:    mean=%.0f | p95=%.0f" % [avg_draws, p95_draws])
	print("Primitives / Frame:    mean=%.0f" % avg_prims)
	print("GPU Clock (MHz):       active=%.0f MHz | cur=%.0f MHz" % [avg_gpu_act, avg_gpu_cur])
	print("Engine Memory:         %.1f MB (Static)" % mem_static_mb)
	print("=================================================================")

	var json_data := {
		"scenario": mode,
		"units": target_units,
		"map": str(args["map"]),
		"camera": str(args["camera"]),
		"frames": total_frames,
		"fps": fps,
		"frame_mean_ms": avg_frame,
		"frame_p50_ms": p50_frame,
		"frame_p95_ms": p95_frame,
		"frame_p99_ms": p99_frame,
		"frame_max_ms": max_frame,
		"sim_mean_ms": avg_sim,
		"sim_p95_ms": p95_sim,
		"sim_max_ms": max_sim,
		"draw_calls_mean": avg_draws,
		"draw_calls_p95": p95_draws,
		"primitives_mean": avg_prims,
		"gpu_act_freq_mhz": avg_gpu_act,
		"gpu_cur_freq_mhz": avg_gpu_cur,
		"mem_static_mb": mem_static_mb
	}

	var json_str := JSON.stringify(json_data)
	print("PERF_JSON_METRICS: ", json_str)

	var out_file := FileAccess.open("scratchpad/realistic_perf_report.json", FileAccess.WRITE)
	if out_file:
		out_file.store_string(json_str)
		out_file.close()

	await frames(5)
	quit(0)
