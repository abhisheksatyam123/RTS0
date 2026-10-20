extends CanvasLayer

# In-game reference pages (0 A.D. session "structree" and "civinfo" hotkeys,
# Alt+Shift+T and Alt+Shift+H): the Structure Tree and Civilization Overview
# for the player's civilization, with technologies already researched in
# green. Any entry opens the Template Viewer over the game. As 0.28's CivIcon
# opens them, the other session dialogs close first, a single-player game
# pauses while they are open (PauseControl.implicitPause), the two hotkeys
# switch between them (pageLoop) and a click on the top panel's civ emblem
# reopens the last one viewed.

const Structree := preload("res://scenes/reference/structree_page.gd")
const CivInfo := preload("res://scenes/reference/civinfo_page.gd")

var is_open: bool = false
var _paused_by_me := false
var _hotkeys := RtsoHotkeyConfig.new()
var _tabs: TabContainer
var _tree
var _civinfo

func _ready() -> void:
	preload("res://scenes/common/fit_screen.gd").cap($Center/Panel, Vector2(1000, 620))
	visible = false
	process_mode = Node.PROCESS_MODE_ALWAYS
	var vbox: VBoxContainer = $Center/Panel/VBox
	_tabs = TabContainer.new()
	_tabs.size_flags_vertical = Control.SIZE_EXPAND_FILL
	vbox.add_child(_tabs)
	_tree = Structree.new()
	_tree.viewer_host = self
	_tabs.add_child(_tree)
	_civinfo = CivInfo.new()
	_civinfo.viewer_host = self
	_tabs.add_child(_civinfo)
	_tabs.tab_changed.connect(func(i): $Center/Panel/VBox/Header/Title.text = _tabs.get_tab_title(i))

func toggle() -> void:
	_toggle_tab(0)

func toggle_civinfo() -> void:
	_toggle_tab(1)

## CivIcon.onPress: the last page viewed (dialogSelection), the Structure Tree at first.
func open_last() -> void:
	if not is_open:
		_toggle_tab(_tabs.current_tab)

func _toggle_tab(tab: int) -> void:
	var opening := not is_open
	if is_open and _tabs.current_tab == tab:
		is_open = false
	else:
		is_open = true
		_tabs.current_tab = tab
		_refresh()
	if opening and is_open:
		_on_open()
	visible = is_open
	if not is_open:
		_close_viewers()
		if _paused_by_me:
			_paused_by_me = false
			get_tree().paused = false

## closeOpenDialogs, then PauseControl.implicitPause (single player only).
func _on_open() -> void:
	var main := get_parent()
	if main and main.has_method("close_open_dialogs"):
		main.close_open_dialogs()
	var networked: bool = main != null and main.has_method("get_summary_gui") and main.get_summary_gui(false).get("networked", false)
	if not networked and not get_tree().paused:
		_paused_by_me = true
		get_tree().paused = true

func _refresh() -> void:
	var main := get_parent()
	if main == null or not main.has_method("get_tech_tree"):
		return
	var tree: Dictionary = main.get_tech_tree()
	var researched := {}
	for t in tree.get("techs", []):
		if t["researched"]:
			researched[int(t["id"])] = true
	_tree.researched = researched
	var civ := int(tree.get("civ", 0))
	_tree.set_civ(civ)
	_civinfo.set_civ(civ)

func _close_viewers() -> void:
	for c in get_children():
		if c.name.begins_with("TemplateViewer"):
			c.queue_free()

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo:
		if is_open and "cancel" in RtsoHotkeyConfig.new().fired(event):
			_toggle_tab(_tabs.current_tab)
			get_viewport().set_input_as_handled()
			return
		# The game is paused under the pages, so they take their own hotkeys.
		if is_open:
			var hk: PackedStringArray = _hotkeys.fired(event)
			if "structree" in hk or "civinfo" in hk:
				_toggle_tab(0 if "structree" in hk else 1)
				get_viewport().set_input_as_handled()

func _on_close_pressed() -> void:
	_toggle_tab(_tabs.current_tab)
