extends Control
## Civilization Overview and Structure Tree from the main menu, ports of 0 A.D.
## gui/reference/civinfo and gui/reference/structree (GPL-2.0-or-later); see
## scenes/reference/. Both pages share the selected civilization, as 0 A.D.'s
## CivInfoButton / StructreeButton pass it between the pages. Any entry opens
## the Template Viewer (gui/reference/viewer).

const Structree := preload("res://scenes/reference/structree_page.gd")
const CivInfo := preload("res://scenes/reference/civinfo_page.gd")

## Set before changing to this scene to open on the Structure Tree.
static var open_structree := false

var _civinfo
var _tree

func _ready() -> void:
	var tabs: TabContainer = $Center/Panel/VBox/TabContainer
	_civinfo = CivInfo.new()
	_tree = Structree.new()
	tabs.add_child(_civinfo)
	tabs.add_child(_tree)
	_civinfo.civ_changed.connect(_tree.set_civ)
	_tree.civ_changed.connect(_civinfo.set_civ)
	if open_structree:
		tabs.current_tab = 1
		open_structree = false

func _on_back_pressed() -> void:
	MatchSettings.go("res://scenes/menu/main_menu.tscn")
