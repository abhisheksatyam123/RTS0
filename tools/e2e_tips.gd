extends SceneTree
# End-to-end check of 0 A.D. 0.28's gameplay tips (gui/reference/tips):
#   1. the data: four categories with their SP/MP loading-screen shares, every
#      tip with a title, bullets and pictures that exist;
#   2. getRandomWeightedCategory: Multiplayer is never picked for single-player
#      and is picked by its share for multiplayer;
#   3. Learn to Play > Tips and Tricks opens the page: every tip, Previous
#      disabled on the first, Next pages on, a tip with several pictures shows
#      "1  /  N" and its arrows page the pictures; Escape closes it.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_tips.gd
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

func first_with(list: Array, pred: Callable) -> int:
	for i in list.size():
		if pred.call(list[i]):
			return i
	return -1

func key(code: Key) -> void:
	for down in [true, false]:
		var k := InputEventKey.new()
		k.keycode = code
		k.physical_keycode = code
		k.pressed = down
		Input.parse_input_event(k)
		await frames(1)

func run() -> void:
	print("1. Data")
	var data: Dictionary = JSON.parse_string(FileAccess.get_file_as_string("res://data/reference/loading.json"))
	var cats: Array = data["categories"]
	check(cats.map(func(c): return c["name"]) == ["Beginner", "Intermediate", "Advanced", "Multiplayer"], "0.28's four categories")
	check(cats[3]["sp"] == 0.0 and cats[3]["mp"] == 0.3 and cats[0]["sp"] == 0.33, "their loading-screen shares")
	var tips: Dictionary = data["tips"]
	var bad: Array = []
	for k in tips:
		var ok: bool = tips[k]["title"] != "" and not tips[k]["lines"].is_empty() and not tips[k]["images"].is_empty()
		for i in tips[k]["images"]:
			ok = ok and FileAccess.file_exists("res://assets/textures/ui/loading/tips/" + i)
		if not ok:
			bad.append(k)
	check(tips.size() == 78 and bad.is_empty(), "78 tips with text and pictures: missing %s" % str(bad))

	print("2. Weighted category")
	var TipDisplay = load("res://scenes/reference/tip_display.gd")
	var mp_in_sp := 0
	var mp_in_mp := 0
	for i in 1000:
		if TipDisplay.weighted_category(cats, false, i / 1000.0)["name"] == "Multiplayer":
			mp_in_sp += 1
		if TipDisplay.weighted_category(cats, true, i / 1000.0)["name"] == "Multiplayer":
			mp_in_mp += 1
	check(mp_in_sp == 0, "single-player never shows a Multiplayer tip")
	check(absi(mp_in_mp - 300) <= 2, "multiplayer shows one by its 0.3 share (%d / 1000)" % mp_in_mp)

	print("3. Tips and Tricks")
	change_scene_to_file("res://scenes/menu/main_menu.tscn")
	await frames(10)
	var menu = current_scene
	menu._toggle_submenu("learn", menu.get_node("MainPanel/Buttons/BtnLearn"))
	var item: Array = menu.submenu_buttons_box.get_children().filter(func(b): return b.text == "Tips and Tricks")
	check(item.size() == 1 and item[0].tooltip_text == "Discover simple tips, tricks, and game mechanics.", "Learn to Play has Tips and Tricks")
	if item.is_empty():
		quit(1)
		return
	item[0].pressed.emit()
	await frames(4)
	var page = menu.find_child("TipsPage", true, false)
	check(page != null, "it opens the page over the menu")
	if page == null:
		quit(1)
		return
	var d = page.display
	var prev: Button = d.find_child("PreviousTipButton", true, false)
	var next: Button = d.find_child("NextTipButton", true, false)
	check(d.tips.size() == 78 and d.tip_index == 0 and prev.visible and prev.disabled and not next.disabled, "all 78 tips, on the first, Previous disabled")
	check(page.get_node("Dialog/CloseButton").tooltip_text.ends_with(": Close Tips and Tricks."), "Close names its hotkey")
	var first: String = d.current_name()
	next.pressed.emit()
	await frames(1)
	check(d.tip_index == 1 and d.current_name() != first and not prev.disabled, "Next shows another tip")
	var title: Label = d.find_child("TipTitle", true, false)
	check(title.text == tips[d.current_name()]["title"], "with its title: " + title.text)
	var multi := first_with(d.tips, func(t): return t["images"].size() > 1)
	d.change_tip(multi - d.tip_index)
	await frames(1)
	var n: int = d.tips[multi]["images"].size()
	var panel: Control = d.find_child("ImageControlPanel", true, false)
	check(panel.visible and d.find_child("TipImageText", true, false).text == "1  /  %d" % n, "%s: \"1  /  %d\"" % [d.current_name(), n])
	var shown: String = d.current_image()
	d.find_child("NextImageButton", true, false).pressed.emit()
	await frames(1)
	check(d.image_index == 1 and d.current_image() != shown and d.find_child("TipImageText", true, false).text == "2  /  %d" % n, "the arrow shows the next picture")
	var single := first_with(d.tips, func(t): return t["images"].size() == 1)
	d.change_tip(single - d.tip_index)
	await frames(1)
	check(not panel.visible, "a tip with one picture has no arrows")
	# scaleGuiElementsToFit: the ornament hugs the title, the bullets follow it.
	var deco: Control = d.find_child("TipTitleDecoration", true, false)
	var text_box: Control = d.find_child("TipText", true, false)
	var short_w: float = deco.size.x
	title.text = "A much longer tip title that has to wrap over two lines here"
	d._fit_title()
	check(title.size.y > 30 and deco.position.y >= title.position.y + title.size.y - 4 and text_box.position.y == deco.position.y + 32,
		"a two-line title pushes the ornament and bullets down")
	check(deco.size.x > short_w, "the ornament spans the title's width (%d > %d)" % [deco.size.x, short_w])
	await key(KEY_ESCAPE)
	await frames(2)
	check(menu.find_child("TipsPage", true, false) == null, "Escape closes it")

	print("4. Translated (fr)")
	TranslationServer.add_translation(load("res://assets/l10n/fr.po"))
	TranslationServer.set_locale("fr")
	item[0].pressed.emit()
	await frames(4)
	var tp = menu.find_child("TipsPage", true, false)
	var auto := first_with(tp.display.tips, func(t): return t["name"] == "autoqueue")
	if auto >= 0:
		tp.display.change_tip(auto - tp.display.tip_index)
		await frames(1)
	var body: String = tp.display.find_child("TipText", true, false).text
	check(auto >= 0 and body.contains("Activez la fonction de mise en file d'attente automatique"), "TranslateLines: the bullets in French")
	await key(KEY_ESCAPE)
	await frames(2)
	var Credits = load("res://scenes/reference/credits_page.gd")
	var cp = Credits.open(menu)
	await frames(2)
	var all := "".join(cp.panels.map(func(p): return str(p["content"])))
	check(all.contains("Directeurs de la programmation") and not all.contains("Programming managers"), "translateObjectKeys: credit titles in French")
	check(cp.find_child("CreditsFade", true, false) != null, "the credits text sits on a ModernFade")
	var ct: Label = cp.find_child("Title", true, false)
	check(ct != null and ct.offset_top == -18, "the credits title sits across the top edge")
	cp.queue_free()
	TranslationServer.set_locale("en")
	print("PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
