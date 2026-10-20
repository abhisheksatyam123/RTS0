extends SceneTree
# End-to-end check of the Catafalque Overview (0 A.D. gui/reference/catafalque)
# and the credits (gui/credits), opened from the main menu:
#   1. Learn to Play lists 0 A.D.'s items in 0 A.D.'s order;
#   2. the Catafalque Overview shows every civ's catafalque on a circle with
#      its name and its auras as the tooltip;
#   3. Credits opens 0 A.D.'s credits with one tab per credits file.
#
#   godot --headless --path . --fixed-fps 60 -s tools/e2e_learn_pages.gd
# Exit code 0 = pass.

var failures := 0

func check(cond: bool, what: String) -> void:
	if cond:
		print("  ok   ", what)
	else:
		print("  FAIL ", what)
		failures += 1

func _initialize() -> void:
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func run() -> void:
	change_scene_to_file("res://scenes/menu/main_menu.tscn")
	await frames(4)
	var menu = current_scene

	print("1. Learn to Play")
	var labels: Array = menu.submenu_data["learn"]["items"].map(func(it): return it["label"])
	check(labels == ["Manual", "Tutorial", "Tips and Tricks", "Structure Tree", "Civilization Overview", "Catafalque Overview", "Map Overview"],
		"0 A.D.'s items: " + str(labels))

	print("2. Catafalque Overview")
	menu._execute_action("open_catafalque")
	await frames(3)
	var cat = menu.find_child("CatafalqueOverview", true, false)
	check(cat != null, "the page opens over the menu")
	if cat:
		check(cat.entries.size() == 15, "15 catafalques, one per civ (0.28's Germans too): %d" % cat.entries.size())
		var athen: Dictionary = cat.entries.filter(func(e): return e["civ"] == 0)[0] if not cat.entries.is_empty() else {}
		check(str(athen.get("name", "")).contains("Solon"), "the Athenian catafalque is Solon's: " + str(athen.get("name", "")))
		check(str(athen.get("tooltip", "")).begins_with("[b]Economic Reforms:[/b] ") and str(athen.get("tooltip", "")).contains("[b]Economic Fortune:[/b] "),
			"its tooltip lists its auras, each name in the header font")
		var close_btn: Button = cat.find_child("CloseButton", true, false)
		check(close_btn != null and close_btn.size.x == 192.0 and close_btn.size.y <= 40.0 and close_btn.tooltip_text.ends_with(": Close Catafalque Bonuses."),
			"Close is 192x32 with the hotkey-led tooltip: %s %s %s" % [close_btn.size if close_btn else null, close_btn.get_combined_minimum_size() if close_btn else null, close_btn.tooltip_text if close_btn else ""])
		check(cat.entries.all(func(e): return e["tooltip"] != "" and e["emblem"] != ""), "every catafalque has auras and an emblem")
		# 15 round the circle: the first on top, the eighth and ninth either side of the bottom.
		var first: Control = cat.find_child("Emblem0", true, false)
		var left: Control = cat.find_child("Emblem7", true, false)
		var right: Control = cat.find_child("Emblem8", true, false)
		check(first and left and right and first.position.y < left.position.y and absf(left.position.y - right.position.y) < 1.0
			and absf((left.position.x + right.position.x) / 2.0 - first.position.x) < 1.0,
			"the first emblem is at the top of the circle, the eighth and ninth either side of the bottom")
		var img: TextureRect = first.get_child(0) if first else null
		check(img != null and img.tooltip_text == cat.entries[0]["tooltip"], "hovering an emblem shows its auras")
		cat.close_page()
		await frames(2)
		check(menu.find_child("CatafalqueOverview", true, false) == null, "Close removes it")

	print("3. Credits")
	menu._on_credits_pressed()
	await frames(3)
	var cr = menu.find_child("Credits", true, false)
	check(cr != null, "Credits opens 0 A.D.'s credits page")
	if cr:
		var tabs: Array = cr.panels.map(func(p): return p["label"])
		check(tabs.size() == 11 and tabs[0] == "RTS0" and tabs[1] == "Special Credits" and tabs[-1] == "Donors",
			"RTS0 then 0 A.D.'s tabs in its order: " + str(tabs))
		cr.select_panel(tabs.find("Art"))
		await frames(1)
		var shown: String = cr.text.get_parsed_text()
		check(shown.contains("Art lead") and shown.contains("Enrique — Enrique Keykens"), "the Art tab lists its people")
		cr.select_panel(tabs.find("Translators"))
		await frames(1)
		check(cr.text.get_parsed_text().length() > 1000, "the Translators tab has its languages")
		cr.close_page()
		await frames(2)
		check(menu.find_child("Credits", true, false) == null, "Close removes it")
	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(0 if failures == 0 else 1)
