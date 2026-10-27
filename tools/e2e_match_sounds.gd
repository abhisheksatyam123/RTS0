extends SceneTree
# 0 A.D. 0.28's match music and alarms (session.js initializeMusic and
# onSimulationUpdate, Player.OnGlobalPlayerDefeated, AlertRaiser, Gate.js,
# TechnologyManager soundComplete):
#   1. a German player hears germ.json's five peace tracks;
#   2. raising and ending an alert rings alarm_alert_0 and alarm_alert_1;
#   3. a finished technology plays alarm_upgradearmory (phases alarm_phase);
#   4. resigning plays "defeated" and the defeat music, which stays when the
#      defeated player watches someone else (the music never follows the view);
#   5. quitting to the summary keeps the defeat track playing there (summary.js
#      has no music calls; only the main menu sets MENU).
# Section 1 also checks Ambient.js's one dayscape loop at a constant level.
#
# Siege engines train with ram_trained (ram_move.ogg), not footsteps: 0.28's
# template_unit_siege.xml names it, the group file and the ogg are in the shared
# data and in assets/audio/attack/siege/, and play_trained plays the group.
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
	# The first peace track starts once the match is running; under load that can take
	# longer than the 60 frames above.
	var a: Dictionary = m.get_audio_state()
	for i in 600:
		if str(a.track) != "":
			break
		await process_frame
		a = m.get_audio_state()
	check(a.music_state == "peace" and GERM_TRACKS.any(func(t): return str(a.track).contains(t)),
		"peace, on a German track: " + str(a.track))
	# Ambient.js: one looping dayscape track at a constant sound.ambientgain.
	var amb: Array = a.ambient
	check(amb.size() == 1 and str(amb[0].path).ends_with("day_temperate_gen_03.ogg") and bool(amb[0].playing)
		and absf(float(amb[0].db) + 1.3) < 0.01, "one dayscape loop at sound.ambientgain: %s" % str(amb))

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

	print("5. Summary keeps the match music")
	var quit_script: GDScript = load("res://scenes/session/quit_confirmation.gd")
	quit_script.end_game(m, true)
	await frames(40)
	var menu_audio = root.get_node("MenuAudio")
	check(str(menu_audio.match_music_track()).contains("Dried_Tears"), "the defeat track plays on the summary: " + menu_audio.match_music_track())
	menu_audio.play_match_track("Tale_of_Warriors.ogg", 0.0, ["Tale_of_Warriors.ogg", "The_Road_Ahead.ogg"])
	menu_audio._next_track()
	await frames(2)
	var after: String = menu_audio.match_music_track()
	menu_audio.play_menu_music()
	await frames(2)
	check(after == "The_Road_Ahead.ogg" and menu_audio.menu_music_track() == "Honor_Bound.ogg",
		"the summary carries on through the session's playlist; the main menu then plays MENU: %s, %s" % [after, menu_audio.menu_music_track()])
	menu_audio.stop_music()

	print("FAILURES: ", failures)
	quit(1 if failures else 0)
