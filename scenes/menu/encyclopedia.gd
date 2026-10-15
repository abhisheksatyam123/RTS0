extends Control
## Civilization Overview and Structure Tree from the main menu, ports of 0 A.D.
## gui/reference/civinfo and gui/reference/structree (GPL-2.0-or-later); see
## scenes/reference/. Both pages share the selected civilization, as 0 A.D.'s
## CivInfoButton / StructreeButton pass it between the pages. Any entry opens
## the Template Viewer (gui/reference/viewer).

const L10n = preload("res://scenes/menu/l10n.gd")
const Structree := preload("res://scenes/reference/structree_page.gd")
const CivInfo := preload("res://scenes/reference/civinfo_page.gd")

## Set before changing to this scene to open on the Structure Tree.
static var open_structree := false

var _civinfo
var _tree
var _tabs: TabContainer
var _hotkeys := RtsoHotkeyConfig.new()

func _ready() -> void:
	var tabs: TabContainer = $Center/Panel/VBox/TabContainer
	_tabs = tabs
	_civinfo = CivInfo.new()
	_tree = Structree.new()
	tabs.add_child(_civinfo)
	tabs.add_child(_tree)
	_civinfo.civ_changed.connect(_tree.set_civ)
	_tree.civ_changed.connect(_civinfo.set_civ)
	if open_structree:
		tabs.current_tab = 1
		open_structree = false
	tabs.tab_changed.connect(func(_t): _render_tooltips())
	_render_tooltips()

func _hotkey_text(name: String) -> String:
	var keys: Array = _hotkeys.get_hotkey_map().get(name, [])
	return ", ".join(PackedStringArray(keys)) if not keys.is_empty() else L10n.t("Unassigned")

## CloseButton / CivInfoButton / StructreeButton tooltips (StructreePage.js:62,
## CivInfoPage.js:131, Buttons/*Button.js:32): the close key, and the key that
## switches to the other page.
func _render_tooltips() -> void:
	var on_tree := _tabs.current_tab == 1
	$Center/Panel/VBox/Header/BtnBack.tooltip_text = L10n.sprintf(
		L10n.t("%(hotkey)s: Close Structure Tree.") if on_tree else L10n.t("%(hotkey)s: Close Civilization Overview."),
		{"hotkey": _hotkey_text("cancel")})
	var bar := _tabs.get_tab_bar()
	bar.set_tab_tooltip(0, L10n.sprintf(L10n.t("%(hotkey)s: Switch to Civilization Overview."), {"hotkey": _hotkey_text("civinfo")}))
	bar.set_tab_tooltip(1, L10n.sprintf(L10n.t("%(hotkey)s: Switch to Structure Tree."), {"hotkey": _hotkey_text("structree")}))

## StructreePage.js / CivInfoPage.js: cancel closes the page; the page's own key
## closes it too and the other page's key switches to that page.
func _unhandled_input(event: InputEvent) -> void:
	var hk: PackedStringArray = _hotkeys.fired(event)
	var page := -1
	if "cancel" in hk:
		page = _tabs.current_tab
	elif "structree" in hk:
		page = 1
	elif "civinfo" in hk:
		page = 0
	if page < 0:
		return
	get_viewport().set_input_as_handled()
	if page == _tabs.current_tab:
		_on_back_pressed()
	else:
		_tabs.current_tab = page

func _on_back_pressed() -> void:
	MatchSettings.go("res://scenes/menu/main_menu.tscn")
