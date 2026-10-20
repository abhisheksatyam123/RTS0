extends SceneTree
# Screenshots of the menus and a match at one window size, to check layouts on phone
# and tablet screens (needs a display):
#   xvfb-run -s "-screen 0 2400x1200x24" godot --rendering-driver opengl3 --path . \
#     -s tools/shot_layouts.gd -- --size 2400x1080 --out /tmp/shots
# Writes <out>/<size>_<page>.png.

var out := "user://shots"
var size := Vector2i(1920, 1080)

func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	for i in args.size():
		if args[i] == "--size" and i + 1 < args.size():
			var p: PackedStringArray = args[i + 1].split("x")
			size = Vector2i(int(p[0]), int(p[1]))
		elif args[i] == "--out" and i + 1 < args.size():
			out = args[i + 1]
	DisplayServer.window_set_size(size)
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func shot(page: String) -> void:
	await frames(4)
	await RenderingServer.frame_post_draw
	var img := root.get_texture().get_image()
	DirAccess.make_dir_recursive_absolute(out)
	img.save_png("%s/%dx%d_%s.png" % [out, size.x, size.y, page])
	print("shot ", page, " ", img.get_size(), " visible ", root.get_visible_rect().size)

func scene(path: String) -> Node:
	var n: Node = load(path).instantiate()
	root.add_child(n)
	return n

func page(path: String, name: String) -> void:
	var n := scene(path)
	await frames(30)
	await shot(name)
	n.queue_free()
	await frames(2)

# A session dialog opened and closed by the same toggle.
func dialog(main: Node, method: String, name: String, args := []) -> void:
	main.callv(method, args)
	await frames(15)
	await shot(name)
	main.callv(method, args)
	await frames(5)

func run() -> void:
	var only := ""
	var args := OS.get_cmdline_user_args()
	var k := args.find("--only")
	if k >= 0 and k + 1 < args.size():
		only = args[k + 1]
	await frames(5)
	if only == "" or only == "menus":
		await page("res://scenes/menu/main_menu.tscn", "menu")
		await page("res://scenes/menu/gamesetup.tscn", "gamesetup")
		await page("res://scenes/menu/options.tscn", "options")
		await page("res://scenes/menu/hotkeys.tscn", "hotkeys")
		await page("res://scenes/menu/replay_browser.tscn", "replays")
		await page("res://scenes/menu/campaigns.tscn", "campaigns")
		await page("res://scenes/menu/encyclopedia.tscn", "encyclopedia")
		await page("res://scenes/session/tech_tree_dialog.tscn", "techtree")
		var d: Node = load("res://scenes/session/savegame_dialog.gd").open(root, "load")
		await frames(30)
		await shot("load_game")
		d.queue_free()
		await frames(2)
	if only == "" or only == "session":
		var main := scene("res://scenes/main.tscn")
		await frames(120)
		main.debug_command_button("House")
		await frames(10)
		await shot("session_worker")
		await dialog(main, "_on_menu_button_pressed", "session_menu")
		await dialog(main, "toggle_pause", "pause")
		await dialog(main, "toggle_diplomacy", "diplomacy")
		await dialog(main, "toggle_objectives", "objectives")
		await dialog(main, "toggle_summary", "summary")
		await dialog(main, "toggle_chat", "chat")
	quit()
