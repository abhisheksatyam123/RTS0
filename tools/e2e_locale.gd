extends SceneTree
# 0.28's language pages (gui/locale, gui/locale_advanced), from the main menu's
# Settings > Language:
#   1. Language lists each translated language under its own name with the
#      current locale; picking one shows its code, Accept saves it to "locale"
#      and the game switches language; Cancel and Escape save nothing;
#   2. Advanced builds a locale from language, country and script, refuses an
#      invalid one ("invalid locale", Accept disabled), names the dictionary it
#      would use, and its Accept fills the Language page's locale;
#   3. Chinese, Japanese and Korean draw with the CJK font RTS0 ships (0.28's Source
#      Han Sans; Noto Sans CJK here) behind every UI font, and other languages
#      drop it.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_locale.gd
# Exit code 0 = pass.

const LocalePage := preload("res://scenes/menu/locale_page.gd")

var failures := 0
var cfg := RtsoUserConfig.new()

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
	var saved = cfg.get_value("locale")
	cfg.set_value("locale", "")
	change_scene_to_file("res://scenes/menu/main_menu.tscn")
	await frames(10)
	var menu = current_scene

	print("1. Language")
	menu._execute_action("open_locale")
	await frames(2)
	var page = menu.get_node("LocalePage")
	var names: Array = []
	for i in page.language_list.item_count:
		names.append(page.language_list.get_item_text(i))
	check(names[0] == "English" and names.has("Deutsch") and names.has("Français") and names.size() == 26,
		"every translated language under its own name (%d)" % names.size())
	check(page.locale_text.text == OS.get_locale(), "the locale is the system's while none is saved")
	# locale.xml: the title and the row labels are ModernLabelText / ModernLeftLabelText.
	var bolds: Array = page.find_children("*", "Label", true, false).filter(func(l): return l.text in ["Language", "Language:", "Locale:"])
	check(bolds.size() == 3 and bolds.all(func(l): return l.get_theme_font("font") is FontVariation and l.get_theme_font_size("font_size") == 14),
		"the title and labels are bold 14 px")
	var de: int = page._languages.find("de")
	page.language_list.select(de)
	page.language_list.item_selected.emit(de)
	check(page.locale_text.text == "de", "picking Deutsch shows de")
	page.accept_button.pressed.emit()
	await frames(3)
	check(cfg.get_value("locale") == "de" and TranslationServer.get_locale() == "de", "Accept saves de and switches the game")
	check(menu.tr("Exit the game.") != "Exit the game.", "the menu is in German: " + menu.tr("Exit the game."))
	check(not menu.has_node("LocalePage"), "and the page closes")

	menu._execute_action("open_locale")
	await frames(2)
	page = menu.get_node("LocalePage")
	check(page.locale_text.text == "de" and page.language_list.selected == de, "it opens on the saved locale")
	page.language_list.select(0)
	page.language_list.item_selected.emit(0)
	var esc := InputEventKey.new()
	esc.keycode = KEY_ESCAPE
	esc.pressed = true
	root.push_input(esc)
	await frames(2)
	check(not is_instance_valid(page) or page.is_queued_for_deletion() or not page.is_inside_tree(), "Escape closes it")
	check(cfg.get_value("locale") == "de", "without saving")

	print("2. Advanced")
	menu._execute_action("open_locale")
	await frames(2)
	page = menu.get_node("LocalePage")
	var adv = page.open_advanced_menu()
	await frames(2)
	check(adv.language_list.get_item_text(adv.language_list.selected) == "de" and adv.country_list.selected == 0,
		"it starts from de, no country")
	adv.country_list.select(adv._countries.find("AT"))
	adv.update_resulting_locale()
	check(adv.resulting_locale.text == "de_AT" and adv.dictionary_file.text.ends_with("de.po") and not adv.accept_button.disabled,
		"Austria: de_AT, with the German dictionary")
	adv.script_input.text = "Qqqq"
	adv.update_resulting_locale()
	check(adv.resulting_locale.text == "invalid locale" and adv.accept_button.disabled, "an unknown script is an invalid locale")
	adv.script_input.text = "Latn"
	adv.update_resulting_locale()
	check(adv.resulting_locale.text == "de_Latn_AT", "a script goes between: " + adv.resulting_locale.text)
	adv.accept_button.pressed.emit()
	await frames(2)
	check(page.locale_text.text == "de_Latn_AT" and page.language_list.selected == de, "Accept fills the Language page")
	adv = page.open_advanced_menu()
	await frames(1)
	adv.auto_detect_locale()
	check(adv.resulting_locale.text.begins_with(OS.get_locale().get_slice("_", 0)) or adv.resulting_locale.text == "invalid locale",
		"Auto detect takes the system's: " + adv.resulting_locale.text)
	adv._close("")
	page._close("")

	print("3. CJK fonts")
	var us = root.get_node("UserSettings")
	var label := Label.new()
	label.theme = load("res://assets/themes/zeroad_theme.tres")
	menu.add_child(label)
	for loc in ["ja", "ko", "zh", "zh_TW"]:
		cfg.set_value("locale", loc)
		us.apply_all()
		await frames(1)
		var cjk: FontFile = us.cjk_font()
		check(cjk != null and us.ui_fonts().all(func(f): return f.fallbacks.size() > 0 and f.fallbacks[0] == cjk),
			"%s: the CJK font is behind every UI font" % loc)
		label.text = menu.tr("Exit the game.")
		var font: Font = label.get_theme_font("font")
		var missing: Array = []
		for c in label.text:
			var u := c.unicode_at(0)
			if not font.has_char(u) and not font.fallbacks.any(func(f): return f.has_char(u)):
				missing.append(c)
		check(TranslationServer.get_locale() == loc and missing.is_empty(),
			"%s: \"%s\" has every glyph (missing %s)" % [loc, label.text, str(missing)])
		check(cjk != null and cjk.has_char("日".unicode_at(0)) and cjk.has_char("한".unicode_at(0)),
			"%s: the Language page's names draw too" % loc)
	cfg.set_value("locale", "de")
	us.apply_all()
	check(us.cjk_font() == null and us.ui_fonts().all(func(f): return f.fallbacks.is_empty()), "German drops it")
	label.queue_free()

	cfg.set_value("locale", saved)
	cfg.save()
	await frames(3)
	print("RESULT: " + ("PASS" if failures == 0 else "FAIL (%d)" % failures))
	quit(0 if failures == 0 else 1)
