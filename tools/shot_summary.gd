extends SceneTree
# Screenshots of the summary page from the newest replay's metadata
# (needs a display: xvfb-run ... --rendering-driver opengl3 -s tools/shot_summary.gd).
# Writes score/units/structures/charts PNGs to $SUMMARY_SHOTS.

func _initialize() -> void:
	run.call_deferred()

func run() -> void:
	var dir := DirAccess.open("user://replays")
	var names := dir.get_directories()
	names.sort()
	names = Array(names).filter(func(n): return FileAccess.file_exists("user://replays/%s/metadata.json" % n))
	if names.is_empty():
		quit(1)
		return
	var meta = JSON.parse_string(FileAccess.get_file_as_string("user://replays/%s/metadata.json" % names[-1]))
	var s = load("res://scenes/session/summary_screen.tscn").instantiate()
	root.add_child(s)
	s.configure({"assignedPlayer": 0, "replayDirectory": "user://replays/%s/" % names[-1]})
	s.show_summary(false, meta.summary, float(meta.duration))
	var out := OS.get_environment("SUMMARY_SHOTS")
	for p in [[0, "score"], [1, "structures"], [2, "units"], [3, "resources"], [5, "misc"], [6, "charts"]]:
		s.select_panel(p[0])
		if p[0] == 6:
			s._pickers[1][0].select(2)
			s._pickers[1][0].item_selected.emit(2)
		for i in 3:
			await process_frame
		await RenderingServer.frame_post_draw
		root.get_texture().get_image().save_png(out.path_join(p[1] + ".png"))
	quit(0)
