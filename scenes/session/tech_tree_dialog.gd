extends CanvasLayer

# In-game reference pages (0 A.D. session "structree" and "civinfo" hotkeys,
# Alt+Shift+T and Alt+Shift+H): the Structure Tree and Civilization Overview
# for the player's civilization, with technologies already researched in
# green. Any entry opens the Template Viewer over the game.

const Structree := preload("res://scenes/reference/structree_page.gd")
const CivInfo := preload("res://scenes/reference/civinfo_page.gd")

var is_open: bool = false
var _tabs: TabContainer
var _tree
var _civinfo

func _ready() -> void:
	visible = false
	var vbox: VBoxContainer = $Center/Panel/VBox
	_tabs = TabContainer.new()
	_tabs.size_flags_vertical = Control.SIZE_EXPAND_FILL
	vbox.add_child(_tabs)
	_tree = Structree.new()
	_tree.lock_civ = true
	_tree.viewer_host = self
	_tabs.add_child(_tree)
	_civinfo = CivInfo.new()
	_civinfo.lock_civ = true
	_civinfo.viewer_host = self
	_tabs.add_child(_civinfo)
	_tabs.tab_changed.connect(func(i): $Center/Panel/VBox/Header/Title.text = _tabs.get_tab_title(i))

func toggle() -> void:
	_toggle_tab(0)

func toggle_civinfo() -> void:
	_toggle_tab(1)

func _toggle_tab(tab: int) -> void:
	if is_open and _tabs.current_tab == tab:
		is_open = false
	else:
		is_open = true
		_tabs.current_tab = tab
		_refresh()
	visible = is_open
	if not is_open:
		_close_viewers()

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
		if is_open and event.keycode == KEY_ESCAPE:
			_toggle_tab(_tabs.current_tab)
			get_viewport().set_input_as_handled()

func _on_close_pressed() -> void:
	_toggle_tab(_tabs.current_tab)
