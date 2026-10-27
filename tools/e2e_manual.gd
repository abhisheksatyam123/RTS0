extends SceneTree
# 0.28's manual page (manual.js, manual.xml): intro.txt translated line by line
# (Engine.TranslateLines), every hotkey.<name> its binding, the title on the
# dialog's top edge, and View Online through openURL's "Opening page" box.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_manual.gd
# Exit code 0 = pass.

const Manual := preload("res://scenes/reference/manual.gd")
const MsgBox := preload("res://scenes/common/msgbox.gd")

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

func run() -> void:
	var en := Manual.manual_bbcode()
	check(en.contains("Graphics settings") and not en.contains("hotkey.") and not en.contains("unassigned"),
		"English: hotkeys bound, nothing left as hotkey.<name> or \"unassigned\"")
	var holder := Control.new()
	root.add_child(holder)
	var page = Manual.open(holder)
	await frames(2)
	var title: Label = page.find_child("Title", true, false)
	check(title != null and title.text == "Manual" and title.offset_top < 0, "the title sits on the dialog's top edge")
	var online: Button = null
	for b in page.find_children("*", "Button", true, false):
		if b.text == "View Online":
			online = b
	# manual.xml: a ModernDialog, the text in a ModernTextPanel at 20 20 100%-20 100%-58,
	# ModernButtonRed buttons, over a ModernFade (0 0 0 85).
	var mt: RichTextLabel = page.find_child("MainText", true, false)
	check(mt != null and mt.get_parent().theme_type_variation == "ModernDialog" and mt.position == Vector2(20, 20)
		and mt.size == Vector2(892, 554) and mt.get_theme_stylebox("normal") is StyleBoxFlat, "the text in a ModernTextPanel on a ModernDialog")
	check(online != null and online.get_theme_stylebox("normal") is StyleBoxTexture, "ModernButtonRed buttons")
	check((page.get_child(0) as ColorRect).color == Color8(0, 0, 0, 85), "ModernFade behind the page")
	online.pressed.emit()
	await frames(2)
	var box: Node = page.find_child("MsgBox", true, false)
	check(MsgBox.last_url == "https://gitea.wildfiregames.com/0ad/0ad/wiki/0adManual" and box != null and box.title == "Opening page",
		"View Online opens 0.28's wiki page, saying so: " + MsgBox.last_url)
	page.close_page()
	TranslationServer.add_translation(load("res://assets/l10n/fr.po"))
	TranslationServer.set_locale("fr")
	var fr := Manual.manual_bbcode()
	check(fr.contains("Paramètres graphiques"), "French: each line is translated (TranslateLines)")
	TranslationServer.set_locale("en")
	await frames(2)
	print("FAILURES: ", failures)
	quit(1 if failures else 0)
