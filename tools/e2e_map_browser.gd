extends SceneTree
# End-to-end check of the Map Browser (0 A.D. 0.28 gui/maps/mapbrowser):
#   1. Learn to Play's Map Overview opens it on Skirmish maps to look only
#      (Close, no Select or Pick Random), and Escape closes it;
#   2. Game Setup's Browse Maps opens it on the current map; the search box
#      sorts matches first (MatchSort), and Select sets Game Setup's type,
#      filter and map;
#   3. pages move by a row, zoom changes the columns, and the M hotkey opens it.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_map_browser.gd
# Exit code 0 = pass.

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

func key(code: Key) -> void:
	for down in [true, false]:
		var k := InputEventKey.new()
		k.keycode = code
		k.physical_keycode = code
		k.pressed = down
		Input.parse_input_event(k)
		await frames(1)

func browser() -> Node:
	return root.find_child("MapBrowser", true, false)

func shown_names(b) -> Array:
	var out := []
	for item in b._items:
		if item.visible:
			out.append(item.get_child(0).get_node("Caption").text)
	return out

func run() -> void:
	var MB = load("res://scenes/menu/map_browser.gd")
	print("0. MatchSort")
	var names := [{"name": "Mainland"}, {"name": "Alpine Lakes"}, {"name": "Marmara"}, {"name": "Arabia"}]
	var sorted: Array = MB.match_sort("ar", names).map(func(e): return e["name"])
	check(sorted == ["Arabia", "Marmara"], "\"ar\": the letters in order, best score first: " + str(sorted))
	sorted = MB.match_sort("Mai", names).map(func(e): return e["name"])
	check(sorted == ["Mainland"], "\"Mai\": Mainland alone: " + str(sorted))

	print("1. Map Overview")
	change_scene_to_file("res://scenes/menu/main_menu.tscn")
	await frames(10)
	current_scene._execute_action("open_map_browser")
	await frames(6)
	var b = browser()
	check(b != null, "Map Overview opens the Map Browser")
	if b:
		check(b.selected_map_type() == "skirmish" and b.selected_map_filter() == "default", "on Skirmish maps, Default filter")
		# MapBrowser.xml: ModernLabelText title, status and tile captions; ModernDarkBoxGold tiles.
		var cap: Label = b.find_child("Caption", true, false)
		check([b.find_child("Status", true, false), cap].all(func(l): return l.get_theme_font("font") is FontVariation and l.get_theme_font_size("font_size") == 14)
			and (cap.get_parent() as Panel).get_theme_stylebox("panel") is StyleBoxFlat, "bold 14 px status and captions on gold-edged tiles")
		check(b.map_list.size() > 0 and shown_names(b).size() > 0, "shows %d maps, %d on the page" % [b.map_list.size(), shown_names(b).size()])
		check(not b._select_btn.visible and not b._pick_random.visible and b._close_btn.text == "Close", "to look only: Close, no Select or Pick Random")
		check(b._status.text == "Maps: %d   Page: 1/%d" % [b.map_list.size(), maxi(1, b.page_count)] and b.page_count == int(ceilf(float(b.map_list.size()) / b.items_per_row)) - b.row_count + 1, "status reads %s (%d columns, %d rows)" % [b._status.text, b.column_count, b.row_count])
		await key(KEY_ESCAPE)
		await frames(3)
		check(browser() == null, "Escape closes it")

	print("2. Game Setup Browse Maps")
	change_scene_to_file("res://scenes/menu/gamesetup.tscn")
	await frames(10)
	var setup = current_scene
	var browse: Button = setup.find_child("BrowseMaps", true, false)
	check(browse != null and browse.tooltip_text.contains("M"), "Browse Maps button with the hotkey tooltip: " + (browse.tooltip_text if browse else ""))
	var before := str(setup._selected_map().get("id", ""))
	browse.pressed.emit()
	await frames(6)
	b = browser()
	check(b != null and b.allow_selection, "opens it to pick a map")
	if b:
		check(b.selected >= 0 and str(b.map_list[b.selected]["id"]) == before, "on Game Setup's map (%s)" % before)
		check(b._close_btn.text == "Cancel" and b._select_btn.visible, "Cancel and Select")
		# Pages move by a row; zoom in shows fewer columns.
		var cols: int = b.column_count
		b.increase_column_count(-1)
		await frames(1)
		check(b.column_count == max(b.min_columns, cols - 1), "zoom in: %d -> %d columns" % [cols, b.column_count])
		b.go_to_page(0)
		var first: String = shown_names(b)[0]
		b.next_page()
		await frames(1)
		check(b.page_count <= 1 or shown_names(b)[0] == b.map_list[b.items_per_row]["name"], "next page starts a row on (was %s)" % first)
		# Search for a random map from the Skirmish type: no match there, so every type's maps.
		b._type.select(0)
		b._render_map_filter()
		b._search.text = "Mainland"
		b._on_change()
		await frames(2)
		check(b.map_list.size() > 0 and b.map_list[0]["name"] == "Mainland", "searching \"Mainland\" lists it first: " + str(b.map_list.slice(0, 3).map(func(m): return m["name"])))
		b.set_selected_index(0)
		b.submit_map_selection()
		await frames(4)
		check(browser() == null, "Select closes it")
		check(setup._map_type == "random" and str(setup._selected_map().get("name", "")) == "Mainland", "Game Setup now has Random type and Mainland: %s %s" % [setup._map_type, setup._selected_map().get("name", "")])

	print("3. M hotkey")
	await key(KEY_M)
	await frames(4)
	b = browser()
	check(b != null, "M opens the Map Browser in Game Setup")
	if b:
		check(str(b.map_list[b.selected]["name"]) == "Mainland", "on the selected map")
		b._close_btn.pressed.emit()
		await frames(3)
		check(browser() == null and setup._map_type == "random", "Cancel keeps the map")
	print("PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
