extends SceneTree
# RTS0 mass-unit stress test - CPU/sim focused (headless).
# Spawns thousands of units, measures spawn throughput and sim tick rate using real-time waiting.
#
# Run:
#   /home/abhi/project/RTS0/build/linux/RTS0.x86_64 --display-driver headless --audio-driver Dummy --log-file /tmp/opencode/stress.log -s res://stress_units.gd

const TEMPLATE := "athen/infantry_spearman_b"
const GROUP := 400
const GROUPS := 20
const MEASURE_SECONDS := 60

var main
var spawned := 0
var spawn_start_ms := 0
var spawn_end_ms := 0
var measure_start_ms := 0
var measure_end_ms := 0
var failures := 0

func _initialize() -> void:
	print("== RTS0 mass-unit stress test (headless) ==")
	print("Template: ", TEMPLATE, "  ", GROUPS, " x ", GROUP, " = ", GROUPS * GROUP, " units")

	var ms = root.get_node("MatchSettings")
	ms.map_id = "random_mainland"
	ms.seed = 42
	ms.map_size = 256
	ms.slot_teams = [1, 2]
	ms.slot_types = [0, 3]

	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	run.call_deferred()

func run() -> void:
	# Wait until sim ready
	var guard := 0
	while true:
		var test = main.debug_spawn_units(TEMPLATE, 1, 0, 0)
		if test.size() > 0:
			spawned += test.size()
			break
		await process_frame
		guard += 1
		if guard > 200:
			print("FAIL: sim never ready")
			quit(1)
			return

	# Spawn all units, timing it
	spawn_start_ms = Time.get_ticks_msec()
	for g in range(GROUPS - 1):
		var ids = main.debug_spawn_units(TEMPLATE, GROUP, 0, 0)
		spawned += ids.size()
		if g % 5 == 0:
			print("Spawned wave ", g + 2, "/", GROUPS, " (", spawned, " units total)")
			await process_frame
	spawn_end_ms = Time.get_ticks_msec()

	var spawn_ms = spawn_end_ms - spawn_start_ms
	print("Spawn complete: ", spawned, " units in ", spawn_ms, " ms")
	print("Spawn rate: ", spawned / (spawn_ms / 1000.0), " units/sec")

	# Measure sim tick rate for MEASURE_SECONDS using real time
	var start_status = main.get_replay_status()
	var start_tick = start_status.turn if start_status.has("turn") else 0
	measure_start_ms = Time.get_ticks_msec()

	# Wait real time
	var target_ms = measure_start_ms + MEASURE_SECONDS * 1000
	while Time.get_ticks_msec() < target_ms:
		await process_frame

	measure_end_ms = Time.get_ticks_msec()
	var end_status = main.get_replay_status()
	var end_tick = end_status.turn if end_status.has("turn") else 0

	var elapsed_tick = end_tick - start_tick
	var real_ms = measure_end_ms - measure_start_ms

	print("== SIM RESULTS ==")
	print("Units spawned: ", spawned)
	print("Sim ticks: ", elapsed_tick, " over ", real_ms, " ms real time")
	if elapsed_tick > 0:
		print("Tick rate: ", elapsed_tick / (real_ms / 1000.0), " ticks/sec (target 20.0)")
		print("Avg ms per tick: ", real_ms / elapsed_tick, " ms")
	else:
		print("No sim ticks advanced (turn counter may not increment in headless)")

	quit(0)
