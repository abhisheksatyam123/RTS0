extends SceneTree
# 0.28's command-line settings (ps/GameSetup/Config.cpp ProcessCommandLineArgs):
#   1. -conf=KEY:VALUE sets a setting above the user's own value, and is never saved;
#   2. -shadows, -vsync, -xres=N and -yres=N set those keys;
#   3. -nosound and -quickstart turn the sound off (g_DisableAudio).
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_cli_flags.gd
# Exit code 0 = pass.

var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	run.call_deferred()

func run() -> void:
	await process_frame
	var cfg = RtsoUserConfig.new()
	print("1. -conf")
	cfg.set_command_args(PackedStringArray(["--conf=overlay.fps:true", "-conf=gui.session.timeelapsedcounter:true", "-conf=nocolon"]))
	check(cfg.get_bool("overlay.fps") and cfg.get_bool("gui.session.timeelapsedcounter"), "both -conf values apply")
	cfg.set_value("overlay.fps", "false")
	check(cfg.get_bool("overlay.fps"), "the command line wins over the user's value")
	cfg.set_command_args(PackedStringArray())
	check(not cfg.get_bool("overlay.fps"), "without it the user's value is back")
	cfg.reload()

	print("2. -shadows, -vsync, -xres, -yres")
	cfg.set_command_args(PackedStringArray(["-vsync", "-xres=1280", "--yres=720", "-shadows"]))
	check(cfg.get_bool("vsync") and cfg.get_bool("shadows") and int(cfg.get_number("xres")) == 1280 and int(cfg.get_number("yres")) == 720, "the keys are set")

	print("3. -nosound, -quickstart")
	check(not cfg.is_audio_disabled(), "sound on by default")
	cfg.set_command_args(PackedStringArray(["-nosound"]))
	check(cfg.is_audio_disabled(), "-nosound")
	cfg.set_command_args(PackedStringArray(["--quickstart"]))
	check(cfg.is_audio_disabled(), "-quickstart")
	var us = root.get_node("UserSettings")
	us._apply_sound()
	check(AudioServer.is_bus_mute(AudioServer.get_bus_index("Master")), "the Master bus is muted")
	cfg.set_command_args(PackedStringArray())
	us._apply_sound()
	check(not AudioServer.is_bus_mute(AudioServer.get_bus_index("Master")), "and unmuted without it")
	print("FAILURES: ", failures)
	quit(0 if failures == 0 else 1)
