extends Control
## The AI Configuration dialog, a port of 0 A.D. 0.28
## gui/gamesetup/Pages/AIConfigPage (AIConfigPage.js/.xml, AIDescription.js,
## AIGameSettingControl.js, Controls/AISelection.js, AIDifficulty.js,
## AIBehavior.js; GPL-2.0-or-later). Game Setup's gear button on an AI row
## opens it for that player: AI Player (None or Petra Bot), AI Difficulty and
## AI Behavior, the AI's description, and OK. Choosing None makes the slot an
## unassigned one again and closes the dialog (AIConfigPage.maybeClose).
const MODERN := preload("res://scenes/common/modern_theme.gd")

signal closed

# simulation/ai/petra/data.json
const PETRA_NAME := "Petra Bot"
const PETRA_DESCRIPTION := "Petra is the default 0 A.D. AI bot. Please report issues to Wildfire Games (see the link in the main menu).\n\nThe AI's resource gathering rate and trade gain depend on the difficulty level (Sandbox −58%, Very Easy −44%, Easy −25%, Medium ±0%, Hard +25%, Very Hard +56%). The easiest levels also have a slower research, training, and building rate. In addition, the Sandbox level does not expand nor attack."
const NO_AI_DESCRIPTION := "AI will be disabled for this player."
const ROW_HEIGHT := 28.0
const ROW_MARGIN := 7.0

var setup: Node
var slot := 0
var enabled := true

var _hotkeys := RtsoHotkeyConfig.new()
var _ai: OptionButton
var _diff: OptionButton
var _beh: OptionButton
var _desc: Label

## AIConfigPage.openPage(playerIndex, enabled) over the Game Setup page.
static func open_overlay(page: Node, player_slot: int, can_edit: bool) -> Control:
	var layer := CanvasLayer.new()
	layer.layer = 40
	page.add_child(layer)
	var dlg = load("res://scenes/menu/ai_config.gd").new()
	dlg.name = "AIConfig"
	dlg.setup = page
	dlg.slot = player_slot
	dlg.enabled = can_edit
	layer.add_child(dlg)
	dlg.closed.connect(layer.queue_free)
	return dlg

## PlayerAI.describe through gamedescription.js translateAISettings:
## "%(AIdifficulty)s %(AIbehavior)s %(AIname)s".
static func describe(diff: int, beh: int, diffs: Array, behs: Array) -> String:
	return "%s %s %s" % [diffs[clampi(diff, 0, diffs.size() - 1)], behs[clampi(beh, 0, behs.size() - 1)], PETRA_NAME]

func _ready() -> void:
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_STOP
	var fade := ColorRect.new()
	fade.color = Color(0, 0, 0, 0.6)
	fade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(fade)
	# ModernDialog, 580 x 400 in the middle of the screen.
	var dialog := Panel.new()
	dialog.name = "Dialog"
	dialog.set_anchors_preset(Control.PRESET_CENTER)
	dialog.offset_left = -290
	dialog.offset_right = 290
	dialog.offset_top = -200
	dialog.offset_bottom = 200
	theme = MODERN.theme() # 0.28's modern skin (ModernDialog and its widgets)
	dialog.theme_type_variation = "ModernDialog"
	add_child(dialog)
	var title := Label.new()
	title.text = tr("AI Configuration")
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title.add_theme_font_size_override("font_size", 18)
	title.anchor_left = 0.5
	title.anchor_right = 0.5
	title.offset_left = -128
	title.offset_right = 128
	title.offset_top = -18
	title.offset_bottom = 14
	dialog.add_child(title)
	_ai = _row(dialog, 0, tr("AI Player"))
	_ai.add_item(tr("None"))
	_ai.add_item(tr(PETRA_NAME))
	_ai.item_selected.connect(_on_ai_selected)
	_diff = _row(dialog, 1, tr("AI Difficulty"))
	for d in setup.DIFFS:
		_diff.add_item(tr(d))
	_diff.item_selected.connect(func(i): setup._on_slot_diff(i, slot); setup._render_ai_button(slot))
	_beh = _row(dialog, 2, tr("AI Behavior"))
	for b in setup.BEHAVIORS:
		_beh.add_item(tr(b))
	_beh.item_selected.connect(func(i): setup._on_slot_beh(i, slot); setup._render_ai_button(slot))
	_desc = Label.new()
	_desc.name = "Description"
	_desc.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_desc.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_desc.anchor_left = 0.08
	_desc.anchor_right = 0.92
	_desc.anchor_bottom = 1.0
	_desc.offset_top = 140
	_desc.offset_bottom = -50
	dialog.add_child(_desc)
	var ok := Button.new()
	ok.name = "OK"
	ok.text = tr("OK")
	ok.anchor_left = 1.0
	ok.anchor_right = 1.0
	ok.anchor_top = 1.0
	ok.anchor_bottom = 1.0
	ok.offset_left = -220
	ok.offset_right = -20
	ok.offset_top = -45
	ok.offset_bottom = -17
	ok.pressed.connect(close_page)
	dialog.add_child(ok)
	render()

# AIGameSettingControlDropdown.setControl: a right-aligned title, the dropdown on its right.
func _row(dialog: Control, i: int, caption: String) -> OptionButton:
	var top := 30.0 + i * (ROW_HEIGHT + ROW_MARGIN)
	var lbl := Label.new()
	lbl.text = caption
	lbl.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	lbl.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	lbl.anchor_left = 0.5
	lbl.anchor_right = 0.5
	lbl.offset_left = -290 + 100
	lbl.offset_right = -6
	lbl.offset_top = top
	lbl.offset_bottom = top + ROW_HEIGHT
	dialog.add_child(lbl)
	var dd := OptionButton.new()
	dd.name = caption.replace(" ", "")
	dd.anchor_left = 0.5
	dd.anchor_right = 0.5
	dd.offset_left = 6
	dd.offset_right = 190
	dd.offset_top = top
	dd.offset_bottom = top + ROW_HEIGHT
	dd.disabled = not enabled
	dialog.add_child(dd)
	return dd

## The controls and description from the slot's settings (AISelection.render,
## AIDifficulty.render, AIBehavior.render, AIDescription.render).
func render() -> void:
	var row: Dictionary = setup._slot_rows[slot]
	var has_ai := int(row["kind"]) == 1
	_ai.select(1 if has_ai else 0)
	_diff.select(clampi(int(row["diff"]), 0, _diff.item_count - 1))
	_beh.select(clampi(int(row["beh"]), 0, _beh.item_count - 1))
	for c in [_diff, _beh]:
		c.visible = has_ai
		c.get_parent().get_child(c.get_index() - 1).visible = has_ai
	_desc.text = tr(PETRA_DESCRIPTION) if has_ai else tr(NO_AI_DESCRIPTION)

func _on_ai_selected(i: int) -> void:
	# None: an unassigned slot (RTS0's Human kind, which nobody plays in single-player).
	setup._set_slot_kind(slot, 1 if i == 1 else 0)
	render()
	if i == 0:
		close_page()

func close_page() -> void:
	closed.emit()

func _input(event: InputEvent) -> void:
	if not (event is InputEventKey or event is InputEventMouseButton):
		return
	if "cancel" in _hotkeys.fired(event):
		get_viewport().set_input_as_handled()
		close_page()
