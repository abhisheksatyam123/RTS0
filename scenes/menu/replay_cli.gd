extends Node
## 0 A.D. 0.28's replay command line (source/main.cpp, binaries/system/readme.txt;
## GPL-2.0-or-later):
##   -replay-visual=PATH  watch the replay at PATH;
##   -replay=PATH         play it without drawing or sound, check every turn's
##                        hash, print the result and quit;
##   -hashtest-full=X     whether -replay checks the hashes (default true);
##   -hashtest-quick=X    0.28's cheaper hash; RTS0 records one hash per turn,
##                        so either one turns the check on.
## PATH is the replay's commands.rts0rec (0.28: commands.txt) or its folder.
## A -replay run exits 0 when every turn matched, 1 on a mismatch or when the
## replay can't be read.

## Set once the command line has started a replay: back at the menu it doesn't again.
static var consumed := false
## Used in place of the command line when set (a test run without its arguments).
static var args_override := PackedStringArray()
## Called with the exit code in place of quitting (a test run).
static var quit_hook := Callable()

var check_hashes := true
var _quiet := false

## {"visual": bool, "path": String, "check_hashes": bool}, or {} without a replay flag.
static func parse(args: PackedStringArray) -> Dictionary:
	var out := {}
	var full := true
	var quick := false
	for a in args:
		var s := a.trim_prefix("-").trim_prefix("-")
		var eq := s.find("=")
		var key := s if eq < 0 else s.substr(0, eq)
		var value := "" if eq < 0 else s.substr(eq + 1).trim_prefix("\"").trim_suffix("\"")
		match key:
			"replay", "replay-visual":
				out["visual"] = key == "replay-visual"
				out["path"] = value
			"hashtest-full":
				full = value == "true"
			"hashtest-quick":
				quick = value == "true"
	if not out.is_empty():
		out["check_hashes"] = full or quick
	return out

## The replay folder PATH names (with a trailing slash), or "" when it has no
## commands.rts0rec.
static func replay_folder(path: String) -> String:
	var dir := path.get_base_dir() if path.get_file() == "commands.rts0rec" else path
	dir = dir.trim_suffix("/") + "/"
	return dir if FileAccess.file_exists(dir + "commands.rts0rec") else ""

## At startup: play the replay the command line names, once. True when one starts.
static func run(ms: Node) -> bool:
	if consumed or ms == null:
		return false
	var opts := parse(args_override if not args_override.is_empty() else OS.get_cmdline_user_args() + OS.get_cmdline_args())
	if opts.is_empty():
		return false
	consumed = true
	var dir := replay_folder(str(opts["path"]))
	if dir == "":
		printerr("ERROR: The requested replay file '%s' does not exist!" % opts["path"])
		if not opts["visual"]:
			_finish(ms.get_tree(), 1)
		return false
	for old in ms.get_children():
		if old.name == "ReplayDriver":
			old.free()
	if not opts["visual"]:
		var driver: Node = load("res://scenes/menu/replay_cli.gd").new()
		driver.name = "ReplayDriver"
		driver.check_hashes = opts["check_hashes"]
		# The replay's Out of Sync and finished boxes pause the tree.
		driver.process_mode = Node.PROCESS_MODE_ALWAYS
		ms.add_child(driver)
	ms.replay_dir = dir
	ms.load_save_path = ""
	print("[Replay] %s %s" % ["watching" if opts["visual"] else "checking", dir])
	return ms.go("res://scenes/main.tscn") == OK

static func _finish(tree: SceneTree, code: int) -> void:
	if quit_hook.is_valid():
		quit_hook.call(code)
	else:
		tree.quit(code)

## -replay: as fast as the replay goes, nothing drawn or heard, until its last
## turn or a hash mismatch.
func _process(_delta: float) -> void:
	var main := get_tree().current_scene
	if main == null or not main.has_method("get_replay_status"):
		return
	if not main.is_replay_playback():
		# The match scene started without the replay (it couldn't be read).
		printerr("[Replay] the replay could not be played")
		_done(1)
		return
	if not _quiet:
		_quiet = true
		get_tree().root.disable_3d = true
		AudioServer.set_bus_mute(0, true)
		main.set_game_speed(1000.0) # the fastest Fast Forward
	var st: Dictionary = main.get_replay_status()
	if not check_hashes:
		# The Out of Sync box would hold the replay until answered.
		for box in main.get_children().filter(func(c): return c.has_method("button_texts")):
			box.press(0)
	if check_hashes and bool(st.get("out_of_sync", false)):
		print("[Replay] Hash mismatch on turn %d" % int(st.get("out_of_sync_turn", 0)))
		_done(1)
	elif bool(st.get("finished", false)):
		print("[Replay] finished %d turns%s" % [int(st.get("turns", 0)), ", every hash matched" if check_hashes else ""])
		_done(0)

func _done(code: int) -> void:
	set_process(false)
	get_tree().root.disable_3d = false
	queue_free()
	_finish(get_tree(), code)
