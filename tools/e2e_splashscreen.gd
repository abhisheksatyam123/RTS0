extends SceneTree
# The welcome page as 0 A.D. 0.28 shows it (gui/splashscreen, gui/pregame/
# SplashscreenHandler.js):
#   1. at startup it opens over the menu while gui.splashscreen.enable is on
#      (the default), with splashscreen.txt's lines, translated per line;
#   2. unticking "Show this message in the future" and OK store the choice and
#      the text's version, so it stays closed;
#   3. a changed text opens it again; and only once per run;
#   4. in German its lines are the German ones.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_splashscreen.gd
# Exit code 0 = pass.

var Splash = preload("res://scenes/menu/splashscreen.gd")
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
	var saved := [cfg.get_value(Splash.ENABLE_KEY), cfg.get_value(Splash.VERSION_KEY)]
	cfg.set_value(Splash.ENABLE_KEY, cfg.get_default(Splash.ENABLE_KEY))
	cfg.set_value(Splash.VERSION_KEY, cfg.get_default(Splash.VERSION_KEY))
	var host := Control.new()
	root.add_child(host)

	print("1. At startup")
	check(Splash.should_show(), "shown by default")
	var page = Splash.open_at_startup(host)
	await frames(2)
	check(page != null and page.is_inside_tree(), "opens over the menu")
	if page == null:
		quit(1)
		return
	var text: String = page.main_text.get_parsed_text()
	check(text.contains("Thank you for installing 0\u00a0A.D. Empires Ascendant!") and text.contains("make sure to join our official forum."), "splashscreen.txt's lines")
	check(not text.contains("imgleft") and not text.contains("sans-bold"), "without 0 A.D.'s tags")
	var dlg: Panel = page.main_text.get_parent().get_parent()
	check(dlg.offset_right - dlg.offset_left == 620 and dlg.find_child("Title", true, false).text == "Welcome!"
		and dlg.find_child("Title", true, false).offset_top == -16, "splashscreen.xml's 620x450 dialog, Welcome! on its top edge")
	check(text.get_slice("\n", 0) == "Thank you for installing 0\u00a0A.D. Empires Ascendant!" and page.main_text.text.count("[table=2]") == 3
		and page.main_text.text.count("pregame/icons/") == 3, "the heading and three paragraphs, each with its icon")
	check(page.display.button_pressed and page.display.text == "Show this message in the future", "the box is ticked")

	print("2. Not again")
	page.display.button_pressed = false
	page.close_page()
	await frames(2)
	check(cfg.get_value(Splash.ENABLE_KEY) == "false" and cfg.get_value(Splash.VERSION_KEY) == Splash.version(), "the choice and version are stored")
	check(not Splash.should_show(), "so it stays closed")

	print("3. A new text, once per run")
	cfg.set_value(Splash.VERSION_KEY, "old")
	check(Splash.should_show(), "a changed text shows it again")
	check(Splash.open_at_startup(host) == null, "but only once per run")

	print("4. German")
	var de: Translation = load("res://assets/l10n/de.po")
	TranslationServer.add_translation(de)
	TranslationServer.set_locale("de")
	page = Splash.open(host)
	await frames(2)
	text = page.main_text.get_parsed_text()
	check(text.contains("Vielen Dank für die Installation von 0\u00a0A.D.") and page.display.text != "Show this message in the future", "German lines: " + text.get_slice("\n", 0))
	page.queue_free()
	TranslationServer.set_locale("en")
	TranslationServer.remove_translation(de)

	cfg.set_value(Splash.ENABLE_KEY, saved[0])
	cfg.set_value(Splash.VERSION_KEY, saved[1])
	cfg.save()
	print("FAILURES: ", failures)
	quit(1 if failures else 0)
