extends Control
## The welcome page, ported from 0 A.D. 0.28 gui/splashscreen (splashscreen.js,
## splashscreen.xml, splashscreen.txt) and gui/pregame/SplashscreenHandler.js:
## at startup the main menu opens it while Options' gui.splashscreen.enable is
## on, or once again whenever its text changes (gui.splashscreen.version);
## "Show this message in the future" and OK store both.

const L10n := preload("res://scenes/menu/l10n.gd")
const MODERN := preload("res://scenes/common/modern_theme.gd")

## splashscreen.txt (0.27), line by line as Engine.TranslateLines translates it (its
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
	return ("\n".join(LINES) + "\n" + HEADING + "\n" + "\n".join(PackedStringArray(PARAGRAPHS.map(func(p): return p[0])))).md5_text()

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

## splashscreen.xml's heading and three paragraphs. The shipped .po files only
## have splashscreen.txt's msgids, so a paragraph 0.28 doesn't translate yet
## falls back to those lines, tags left out.
const HEADING := "Thank you for installing 0 A.D. Empires Ascendant!"
const PARAGRAPHS := [
	["This game is still in development. You may encounter bugs, and some features are not as fleshed out as we would like.", [2]],
	["The game can have performance problems, especially with large maps and a great number of units.", [4]],
	["0 A.D. is Free Software: you can participate in its development. If you want to help with art, sound, gameplay or programming, make sure to join our official forum.", [6, 7]],
]

static func _old_line(i: int) -> String:
	var re := RegEx.create_from_string("\\[(font|img(left|right)?)=\"[^\"]*\"\\]")
	return re.sub(L10n.t(LINES[i]), "", true).strip_edges()

static func _translate(msgid: String, fallback: Array) -> String:
	var t := L10n.t(msgid)
	if t != msgid:
		return t
	return " ".join(PackedStringArray(fallback.map(func(i): return _old_line(i))))

static func heading() -> String:
	return _translate(HEADING, [0])

static func paragraphs() -> PackedStringArray:
	var out := PackedStringArray()
	for p in PARAGRAPHS:
		out.append(_translate(p[0], p[1]))
	return out

## The page's text as one string, for tests and the version.
static func bbcode() -> String:
	return "\n".join(PackedStringArray([heading()]) + paragraphs())

func _ready() -> void:
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_STOP
	theme = MODERN.theme()
	var fade := ColorRect.new() # "ModernFade"
	fade.color = Color(0, 0, 0, 0.6)
	fade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(fade)
	var panel := Panel.new() # ModernDialog, size="50%-310 50%-225 50%+310 50%+225"
	panel.theme_type_variation = "ModernDialog"
	panel.set_anchors_preset(Control.PRESET_CENTER)
	panel.offset_left = -310
	panel.offset_right = 310
	panel.offset_top = -225
	panel.offset_bottom = 225
	add_child(panel)
	load("res://scenes/menu/hotkeys.gd")._edge_title(panel, L10n.t("Welcome!"))
	# ModernDarkBox with ModernDarkBoxGoldBorder, size="20 20 100%-20 100%-52".
	var box := Panel.new()
	var style := StyleBoxFlat.new()
	style.bg_color = MODERN.DARK_BOX
	style.border_color = MODERN.GOLD
	style.set_border_width_all(1)
	box.add_theme_stylebox_override("panel", style)
	box.set_anchors_preset(Control.PRESET_FULL_RECT)
	box.offset_left = 20
	box.offset_top = 20
	box.offset_right = -20
	box.offset_bottom = -52
	panel.add_child(box)
	main_text = RichTextLabel.new()
	main_text.name = "mainText"
	main_text.bbcode_enabled = true
	main_text.scroll_active = false
	main_text.set_anchors_preset(Control.PRESET_FULL_RECT)
	main_text.offset_left = 10
	main_text.offset_top = 8
	main_text.offset_right = -10
	main_text.offset_bottom = -34
	# sans-bold-20 centred heading, then sans-16 paragraphs 20 px apart.
	var parts := PackedStringArray(["[center][b][font_size=20]%s[/font_size][/b][/center]" % heading().replace("[", "[lb]")])
	for p in paragraphs():
		parts.append("[font_size=16]%s[/font_size]" % p.replace("[", "[lb]"))
	main_text.add_theme_constant_override("paragraph_separation", 20)
	main_text.text = "\n".join(parts)
	box.add_child(main_text)
	display = CheckBox.new() # size="8 100%-30 ..."
	display.name = "displaySplashScreen"
	display.text = L10n.t("Show this message in the future")
	display.anchor_top = 1.0
	display.anchor_bottom = 1.0
	display.offset_left = 8
	display.offset_top = -30
	display.offset_bottom = -8
	display.button_pressed = RtsoUserConfig.new().get_bool(ENABLE_KEY)
	box.add_child(display)
	var buttons := [["btnOK", L10n.t("OK"), close_page],
		["", L10n.t("Official Forum (web)"), func(): load("res://scenes/common/msgbox.gd").open_url(self, FORUM_URL)]]
	for i in 2:
		var b := Button.new()
		if buttons[i][0] != "":
			b.name = buttons[i][0]
		b.text = buttons[i][1]
		b.anchor_left = i * 0.5
		b.anchor_right = (i + 1) * 0.5
		b.anchor_top = 1.0
		b.anchor_bottom = 1.0
		b.offset_left = 18 if i == 0 else 5
		b.offset_right = -5 if i == 0 else -18
		b.offset_top = -45
		b.offset_bottom = -17
		b.pressed.connect(buttons[i][2])
		panel.add_child(b)

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
