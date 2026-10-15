extends SceneTree
# 0 A.D. 0.28's match music and alarms (session.js initializeMusic and
# onSimulationUpdate, Player.OnGlobalPlayerDefeated, AlertRaiser, Gate.js,
# TechnologyManager soundComplete):
#   1. a German player hears germ.json's five peace tracks;
#   2. raising and ending an alert rings alarm_alert_0 and alarm_alert_1;
#   3. a finished technology plays alarm_upgradearmory (phases alarm_phase);
#   4. resigning plays "defeated" and the defeat music, which stays when the
#      defeated player watches someone else (the music never follows the view).
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_match_sounds.gd
# Exit code 0 = pass.

const GERM_TRACKS := ["Northern_Frontier", "As_Seasons_Change", "Epitaph", "From_Every_Corner", "Whistler"]

var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func run() -> void:
	var ms = root.get_node("MatchSettings")
	ms.map_id = "random_continental"
	ms.seed = 7
	ms.slot_types = [0, 1]
	ms.slot_civs = [15, 0]
	ms.replay_dir = ""
	ms.load_save_path = ""
	change_scene_to_file("res://scenes/main.tscn")
	await frames(60)
	var m = current_scene

	print("1. Germans' music")
	var a: Dictionary = m.get_audio_state()
	check(a.music_state == "peace" and GERM_TRACKS.any(func(t): return str(a.track).contains(t)),
		"peace, on a German track: " + str(a.track))

	print("2. Alert bells")
	var cc: int = m.debug_civic_centre_id(0)
	m.debug_select_structures(PackedInt32Array([cc]))
	await frames(2)
	m._on_alert_pressed(0)
	check(str(m.get_audio_state().last_sound).contains("alarmalert0"), "raise: " + str(m.get_audio_state().last_sound))
	await frames(30)
	m._on_alert_pressed(1)
	check(str(m.get_audio_state().last_sound).contains("alarmalert1"), "end: " + str(m.get_audio_state().last_sound))

	print("3. Technology finished")
	m.debug_select_structures(PackedInt32Array([]))
	var r: Dictionary = m.debug_research_any()
	check(r.has("tech"), "researching %s" % str(r))
	var heard := ""
	for i in 6000:
		await process_frame
		var s := str(m.get_audio_state().last_sound)
		if s.contains("alarmupgradearmory") or s.contains("alarmresearch"):
			heard = s
			break
	check(heard.contains("alarmupgradearmory") or heard.contains("alarmresearchphase"), "upgrade armory (or phase): " + heard)

	print("4. Defeated")
	m.resign()
	await frames(40)
	a = m.get_audio_state()
	check(a.music_state == "defeat" and str(a.last_sound).contains("alarmdefeat"), "defeat music and alarm: %s %s" % [a.music_state, a.last_sound])
	m._on_observer_view_selected(3)
	await frames(40)
	check(m.get_audio_state().music_state == "defeat", "watching the AI keeps the defeat music: " + m.get_audio_state().music_state)

	print("FAILURES: ", failures)
	quit(1 if failures else 0)
