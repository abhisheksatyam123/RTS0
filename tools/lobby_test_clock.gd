extends RefCounted
## The lobby e2e tests talk to a real tools/lobby_server.py, and LobbySession's
## request timeouts and the lobby page's refresh timers count game time. Under
## --fixed-fps a headless run's game time races about a thousand times ahead
## of the wall clock, so a 5 s request timeout lapses after a few milliseconds
## (the server then logs a BrokenPipe) and the page polls the server hundreds
## of times a second. Run that way, a test starts itself again without
## --fixed-fps and passes its output and exit code on; otherwise it just runs.

static func run_in_wall_time(tree: SceneTree, script_path: String, run: Callable) -> void:
	(func() -> void:
		var t0 := Time.get_ticks_usec()
		var game := 0.0
		for i in 20:
			await tree.process_frame
			game += tree.root.get_process_delta_time()
		var wall := (Time.get_ticks_usec() - t0) / 1e6
		if game < wall * 3.0 + 0.05:
			run.call()
			return
		print("[lobby test] game time runs ahead of the wall clock (--fixed-fps?): running again without it")
		var args := PackedStringArray(["--headless", "--path", ProjectSettings.globalize_path("res://"), "-s", script_path])
		var user := OS.get_cmdline_user_args()
		if not user.is_empty():
			args.append("--")
			args.append_array(user)
		var out: Array = []
		var code := OS.execute(OS.get_executable_path(), args, out, true)
		for chunk in out:
			printraw(str(chunk))
		tree.quit(code)).call_deferred()
