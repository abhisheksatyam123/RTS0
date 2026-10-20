extends Control
## The welcome page, ported from 0 A.D. 0.28 gui/splashscreen (splashscreen.js,
## splashscreen.xml, splashscreen.txt) and gui/pregame/SplashscreenHandler.js:
## at startup the main menu opens it while Options' gui.splashscreen.enable is
## on, or once again whenever its text changes (gui.splashscreen.version);
## "Show this message in the future" and OK store both.

const L10n := preload("res://scenes/menu/l10n.gd")

## splashscreen.txt, line by line as Engine.TranslateLines translates it (its
## "0 A.D." has a no-break space, as the msgids do).
const LINES := [
	"[font=\"sans-bold-20\"]  Thank you for installing 0 A.D. Empires Ascendant!",
	"[font=\"sans-16\"]",
	"[imgleft=\"constructionIcon\"]This game is still in development. You may encounter bugs, and some features are not as fleshed out as we would like.",
	"",
	"[imgleft=\"iconLag\"]The game can have performance problems, especially with large maps and a great number of units.",
	"",
	"[imgleft=\"iconMap\"]0 A.D. is Free Software: you can participate in its development.",
	"If you want to help with art, sound, gameplay or programming, make sure to join our official forum.",
]
const FORUM_URL := "https://www.wildfiregames.com/forum/"
const ENABLE_KEY := "gui.splashscreen.enable"
const VERSION_KEY := "gui.splashscreen.version"

var main_text: RichTextLabel
var display: CheckBox

## The text's version: 0.28 compares the page file's MD5.
static func version() -> String:
	return "\n".join(LINES).md5_text()

## SplashScreenHandler.openPage: shown when enabled or when the text changed.
static func should_show() -> bool:
	var cfg := RtsoUserConfig.new()
	return cfg.get_bool(ENABLE_KEY) or cfg.get_value(VERSION_KEY) != version()

## SplashScreenHandler.onFirstTick: once per run, on the menu shown at startup.
static func open_at_startup(parent: Node) -> Control:
	if Engine.has_meta("rtso_splash_seen"):
		return null
	Engine.set_meta("rtso_splash_seen", true)
	return open(parent) if should_show() else null

static func open(parent: Node) -> Control:
	var page = load("res://scenes/menu/splashscreen.gd").new()
	page.name = "Splashscreen"
	parent.add_child(page)
	return page

## TranslateLines, with 0 A.D.'s font tags as bold or sized text and its
## inline images left out.
static func bbcode() -> String:
	var out := PackedStringArray()
	var size := 16
	var re_font := RegEx.create_from_string("^\\[font=\"sans-(bold-)?(\\d+)\"\\]")
	var re_img := RegEx.create_from_string("\\[img(left|right)?=\"[^\"]*\"\\]")
	for line in LINES:
		var t: String = L10n.t(line) if line != "" else ""
		var bold := false
		var m := re_font.search(t)
		if m:
			bold = m.get_string(1) != ""
			size = int(m.get_string(2))
			t = t.substr(m.get_end())
		t = re_img.sub(t, "", true).strip_edges().replace("[", "[lb]")
		if t == "" and m:
			continue
		out.append(("[b][font_size=%d]%s[/font_size][/b]" % [size, t]) if bold else ("[font_size=%d]%s[/font_size]" % [size, t]))
	return "\n".join(out)

func _ready() -> void:
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_STOP
	var fade := ColorRect.new() # "ModernFade"
	fade.color = Color(0, 0, 0, 0.6)
	fade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(fade)
	var panel := Panel.new() # size="50%-300 50%-225 50%+300 50%+225"
	panel.set_anchors_preset(Control.PRESET_CENTER)
	panel.offset_left = -300
	panel.offset_right = 300
	panel.offset_top = -225
	panel.offset_bottom = 225
	add_child(panel)
	var title := Label.new()
	title.text = L10n.t("Welcome!")
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title.position = Vector2(172, 2)
	title.size = Vector2(256, 26)
	title.add_theme_font_size_override("font_size", 18)
	panel.add_child(title)
	main_text = RichTextLabel.new()
	main_text.name = "mainText"
	main_text.bbcode_enabled = true
	main_text.position = Vector2(20, 30)
	main_text.size = Vector2(560, 330)
	main_text.text = bbcode()
	panel.add_child(main_text)
	display = CheckBox.new()
	display.name = "displaySplashScreen"
	display.text = L10n.t("Show this message in the future")
	display.position = Vector2(20, 366)
	display.button_pressed = RtsoUserConfig.new().get_bool(ENABLE_KEY)
	panel.add_child(display)
	var ok := Button.new()
	ok.name = "btnOK"
	ok.text = L10n.t("OK")
	ok.position = Vector2(18, 405)
	ok.size = Vector2(277, 28)
	ok.pressed.connect(close_page)
	panel.add_child(ok)
	var forum := Button.new()
	forum.text = L10n.t("Official Forum (web)")
	forum.position = Vector2(305, 405)
	forum.size = Vector2(277, 28)
	forum.pressed.connect(func(): load("res://scenes/common/msgbox.gd").open_url(self, FORUM_URL))
	panel.add_child(forum)

## splashscreen.js closePage.
func close_page() -> void:
	var cfg := RtsoUserConfig.new()
	cfg.set_value(ENABLE_KEY, "true" if display.button_pressed else "false")
	cfg.set_value(VERSION_KEY, version())
	cfg.save()
	queue_free()

func _input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and "cancel" in RtsoHotkeyConfig.new().fired(event):
		get_viewport().set_input_as_handled()
		close_page()
