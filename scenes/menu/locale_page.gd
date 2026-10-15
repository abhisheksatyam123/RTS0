extends Control
## The language pages, ported from 0 A.D. 0.28 gui/locale (locale.js/.xml) and
## gui/locale_advanced (locale_advanced.js/.xml; GPL-2.0-or-later), opened from
## the main menu's Settings > Language. The Language page lists each language
## with a translation under its own name and shows the locale it would save;
## Advanced builds a locale from a language, a country and a script code, says
## whether it is valid and which dictionary it would use, and Auto detect
## takes the system's. Accept saves the locale (config "locale"), and the game
## switches language at once.
##
##     var page = LocalePage.open(self)            # Language
##     var adv = LocalePage.open_advanced(self, "de_AT")

signal closed(locale: String)

const L10n := preload("res://scenes/menu/l10n.gd")
const UserSettingsScript := preload("res://scenes/menu/user_settings.gd")

var advanced := false
## The locale the page starts from (localeText, initData.locale).
var locale := ""

var language_list: OptionButton
var locale_text: Label
var country_list: OptionButton
var script_input: LineEdit
var variant_input: LineEdit
var keywords_input: LineEdit
var resulting_locale: Label
var dictionary_file: Label
var accept_button: Button
var _languages: Array = []  # list_data
var _countries: Array = []
var _cfg := RtsoUserConfig.new()

## page_locale.xml over parent.
static func open(parent: Node) -> Control:
	var page = load("res://scenes/menu/locale_page.gd").new()
	page.name = "LocalePage"
	page.locale = current_locale()
	parent.add_child(page)
	return page

## page_locale_advanced.xml over parent, starting from initData.locale.
static func open_advanced(parent: Node, from_locale: String) -> Control:
	var page = load("res://scenes/menu/locale_page.gd").new()
	page.name = "LocaleAdvancedPage"
	page.advanced = true
	page.locale = from_locale
	parent.add_child(page)
	return page

## Engine.GetCurrentLocale: the saved locale, else the system's.
static func current_locale() -> String:
	var want := str(RtsoUserConfig.new().get_value("locale"))
	return want if want != "" else OS.get_locale()

## Engine.GetFallbackToAvailableDictLocale.
static func dictionary_locale(loc: String) -> String:
	return UserSettingsScript.resolve_locale(loc)

## The locale's parts: [language, script, country].
static func split_locale(loc: String) -> Array:
	var parts := loc.replace("-", "_").split("_", false)
	var lang := parts[0] if parts.size() > 0 else ""
	var scr := ""
	var country := ""
	for i in range(1, parts.size()):
		if parts[i].length() == 4 and scr == "" and country == "":
			scr = parts[i]
		elif country == "":
			country = parts[i]
	return [lang, scr, country]

## Engine.ValidateLocale: a known language, and script and country if given.
static func validate_locale(loc: String) -> bool:
	var p := split_locale(loc)
	if p[0] == "" or not TranslationServer.get_all_languages().has(p[0]):
		return false
	if p[1] != "" and not TranslationServer.get_all_scripts().has(p[1]):
		return false
	return p[2] == "" or TranslationServer.get_all_countries().has(p[2])

func _ready() -> void:
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_STOP
	var fade := ColorRect.new() # "ModernFade"
	fade.color = Color(0, 0, 0, 0.6)
	fade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(fade)
	var half := Vector2(220, 210) if advanced else Vector2(210, 100)
	theme = preload("res://scenes/common/modern_theme.gd").theme()
	var panel := Panel.new() # ModernDialog
	panel.name = "Dialog"
	panel.theme_type_variation = "ModernDialog"
	panel.set_anchors_preset(Control.PRESET_CENTER)
	panel.offset_left = -half.x
	panel.offset_right = half.x
	panel.offset_top = -half.y
	panel.offset_bottom = half.y
	add_child(panel)
	var title := Label.new()
	title.text = "Language"
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title.position = Vector2(half.x - 150, -18)
	title.size = Vector2(300, 32)
	panel.add_child(title)
	if advanced:
		_build_advanced(panel, half * 2)
	else:
		_build_basic(panel, half * 2)

func _row_label(panel: Control, text: String, y: float, h: float, w: float) -> void:
	var l := Label.new()
	l.text = text
	l.position = Vector2(20, y)
	l.size = Vector2(w, h)
	l.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	panel.add_child(l)

func _button(panel: Control, text: String, i: int, y: float, inner_w: float, h: float) -> Button:
	# "0 100%-60 33% ...", "33%+5 ... 66%", "66%+5 ... 100%" inside a 15 px margin.
	var b := Button.new()
	b.text = text
	var x0: float = [0.0, 0.33, 0.66][i] * inner_w + (5.0 if i > 0 else 0.0)
	var x1: float = [0.33, 0.66, 1.0][i] * inner_w
	b.position = Vector2(15 + x0, y)
	b.size = Vector2(x1 - x0, h)
	panel.add_child(b)
	return b

func _build_basic(panel: Control, size_: Vector2) -> void:
	var inner := size_.x - 30
	var col := 15 + inner * 0.4 + 10
	_row_label(panel, "Language:", 50, 33, inner * 0.4 - 5)
	language_list = OptionButton.new()
	language_list.name = "languageList"
	language_list.auto_translate_mode = Node.AUTO_TRANSLATE_MODE_DISABLED # each in its own language, or a code
	language_list.position = Vector2(col, 50)
	language_list.size = Vector2(15 + inner - col, 33)
	panel.add_child(language_list)
	# GetSupportedLocaleDisplayNames, each in its own language.
	for l in UserSettingsScript.available_locales():
		_languages.append(l)
		language_list.add_item(UserSettingsScript.LANGUAGE_NAMES.get(l, l))
	language_list.item_selected.connect(_on_language_selected)
	_row_label(panel, "Locale:", 85, 28, inner * 0.4 - 5)
	locale_text = Label.new()
	locale_text.name = "localeText"
	locale_text.auto_translate_mode = Node.AUTO_TRANSLATE_MODE_DISABLED
	locale_text.position = Vector2(col, 85)
	locale_text.size = Vector2(15 + inner - col, 28)
	locale_text.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	panel.add_child(locale_text)
	set_locale_text(locale)
	_button(panel, "Cancel", 0, size_.y - 60, inner, 28).pressed.connect(_close.bind(""))
	_button(panel, "Advanced", 1, size_.y - 60, inner, 28).pressed.connect(open_advanced_menu)
	accept_button = _button(panel, "Accept", 2, size_.y - 60, inner, 28)
	accept_button.pressed.connect(apply_selected_locale)

## The locale shown, with its dictionary's language selected in the list.
func set_locale_text(loc: String) -> void:
	locale_text.text = loc
	var i := _languages.find(dictionary_locale(loc))
	if i >= 0:
		language_list.select(i)

## languageSelectionChanged.
func _on_language_selected(i: int) -> void:
	locale_text.text = _languages[i]

## openAdvancedMenu.
func open_advanced_menu() -> Control:
	var adv := open_advanced(self, locale_text.text)
	adv.closed.connect(func(loc: String):
		if loc != "":
			set_locale_text(loc))
	return adv

## applySelectedLocale: SaveLocale, then ReevaluateCurrentLocaleAndReload; the
## game's Controls retranslate themselves when UserSettings applies it.
func apply_selected_locale() -> void:
	_cfg.set_value("locale", locale_text.text)
	_cfg.save()
	var us := get_node_or_null("/root/UserSettings")
	if us and us.has_method("apply_all"):
		us.apply_all()
	_close(locale_text.text)

func _build_advanced(panel: Control, size_: Vector2) -> void:
	var inner := size_.x - 30
	var col := 15 + inner * 0.5 + 10
	var w := 15 + inner - col
	_row_label(panel, "Language:", 50, 25, inner * 0.5 - 5)
	language_list = OptionButton.new()
	language_list.name = "languageList"
	language_list.auto_translate_mode = Node.AUTO_TRANSLATE_MODE_DISABLED # each in its own language, or a code
	language_list.position = Vector2(col, 50)
	language_list.size = Vector2(w, 25)
	panel.add_child(language_list)
	# Only the languages there is a dictionary for, by code.
	for l in UserSettingsScript.available_locales():
		var lang: String = split_locale(l)[0]
		if not _languages.has(lang):
			_languages.append(lang)
			language_list.add_item(lang)
	_row_label(panel, "Country:", 80, 25, inner * 0.5 - 5)
	country_list = OptionButton.new()
	country_list.name = "countryList"
	country_list.auto_translate_mode = Node.AUTO_TRANSLATE_MODE_DISABLED # codes, and "None" already translated
	country_list.position = Vector2(col, 80)
	country_list.size = Vector2(w, 25)
	panel.add_child(country_list)
	_countries = Array(TranslationServer.get_all_countries())
	_countries.sort()
	_countries.push_front("")
	for c in _countries:
		country_list.add_item(c if c != "" else L10n.t("None"))
	_row_label(panel, "Script:", 108, 29, inner * 0.5 - 5)
	script_input = _line_input(panel, "scriptInput", col, 110, w, "Optional four-letter script code part following the language code (as listed in ISO 15924)")
	_row_label(panel, "Variant (unused):", 138, 29, inner * 0.5 - 5)
	variant_input = _line_input(panel, "variantInput", col, 140, w, "Not implemented yet.")
	_row_label(panel, "Keywords (unused):", 168, 29, inner * 0.5 - 5)
	keywords_input = _line_input(panel, "keywordsInput", col, 170, w, "Not implemented yet.")
	_row_label(panel, "Resulting locale:", 228, 29, inner * 0.5 - 5)
	resulting_locale = Label.new()
	resulting_locale.name = "resultingLocale"
	resulting_locale.position = Vector2(col, 230)
	resulting_locale.size = Vector2(w, 25)
	panel.add_child(resulting_locale)
	_row_label(panel, "Dictionary files used:", 258, 32, inner * 0.5 - 5)
	dictionary_file = Label.new()
	dictionary_file.name = "dictionaryFile"
	dictionary_file.auto_translate_mode = Node.AUTO_TRANSLATE_MODE_DISABLED
	dictionary_file.position = Vector2(col, 260)
	dictionary_file.size = Vector2(w, 85)
	dictionary_file.autowrap_mode = TextServer.AUTOWRAP_ARBITRARY
	panel.add_child(dictionary_file)
	_button(panel, "Cancel", 0, size_.y - 60, inner, 35).pressed.connect(_close.bind(""))
	_button(panel, "Auto detect", 1, size_.y - 60, inner, 35).pressed.connect(auto_detect_locale)
	accept_button = _button(panel, "Accept", 2, size_.y - 60, inner, 35)
	accept_button.pressed.connect(func(): _close(resulting_locale.text))
	# init: the starting locale's language, country and script.
	var p := split_locale(locale)
	var li := _languages.find(p[0])
	language_list.select(li if li >= 0 else 0)
	var ci := _countries.find(p[2])
	country_list.select(ci if ci >= 0 else 0)
	script_input.text = p[1]
	language_list.item_selected.connect(func(_i): update_resulting_locale())
	country_list.item_selected.connect(func(_i): update_resulting_locale())
	update_resulting_locale()

func _line_input(panel: Control, node_name: String, x: float, y: float, w: float, tip: String) -> LineEdit:
	var e := LineEdit.new()
	e.name = node_name
	e.position = Vector2(x, y)
	e.size = Vector2(w, 25)
	e.tooltip_text = tip
	e.text_changed.connect(func(_t): update_resulting_locale())
	panel.add_child(e)
	return e

## updateResultingLocale (0.28 runs it every tick; here on every change).
func update_resulting_locale() -> void:
	var loc: String = _languages[language_list.selected] if language_list.selected >= 0 else ""
	if script_input.text != "":
		loc += "_" + script_input.text
	if country_list.selected > 0:
		loc += "_" + str(_countries[country_list.selected])
	if validate_locale(loc):
		resulting_locale.text = loc
		resulting_locale.auto_translate_mode = Node.AUTO_TRANSLATE_MODE_DISABLED
		# GetDictionariesForLocale: English needs none.
		var dict := dictionary_locale(loc)
		dictionary_file.text = "" if dict == "en" else UserSettingsScript.L10N_DIR.path_join(dict + ".po")
		accept_button.disabled = false
	else:
		resulting_locale.auto_translate_mode = Node.AUTO_TRANSLATE_MODE_INHERIT
		resulting_locale.text = "invalid locale"
		dictionary_file.text = ""
		accept_button.disabled = true

## autoDetectLocale: the system's locale (GetDictionaryLocale("")).
func auto_detect_locale() -> void:
	variant_input.text = ""
	dictionary_file.text = ""
	var p := split_locale(OS.get_locale())
	language_list.select(maxi(0, _languages.find(p[0])))
	country_list.select(maxi(0, _countries.find(p[2])))
	script_input.text = p[1]
	update_resulting_locale()

func _close(result: String) -> void:
	closed.emit(result)
	queue_free()

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and event.keycode == KEY_ESCAPE: # hotkey="cancel"
		get_viewport().set_input_as_handled()
		_close("")
