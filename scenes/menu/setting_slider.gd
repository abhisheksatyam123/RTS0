extends HBoxContainer
## 0.28's GameSettingControlSlider (GameSettingControlSlider.xml): a slider with
## the value's caption beside it, for Ceasefire, Relic Count, Relic Duration,
## Wonder Duration and Sea Level Rise Time. It answers as the dropdowns do
## (add_item / select / get_selected / item_selected), one step per item, so the
## page reads it the same way.

signal item_selected(index: int)

var _texts: PackedStringArray = []
var _slider := HSlider.new()
var _label := Label.new()

var disabled: bool:
	get:
		return not _slider.editable
	set(v):
		_slider.editable = not v

func _init() -> void:
	add_theme_constant_override("separation", 6)
	_slider.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_slider.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	_slider.step = 1
	_slider.min_value = 0
	_slider.max_value = 0
	# The row's tooltip (setControlTooltip: the slider's and the label's).
	_slider.mouse_filter = Control.MOUSE_FILTER_PASS
	_label.mouse_filter = Control.MOUSE_FILTER_PASS
	_label.custom_minimum_size = Vector2(96, 0)
	_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	add_child(_slider)
	add_child(_label)
	_slider.value_changed.connect(func(v: float) -> void:
		_render()
		item_selected.emit(int(v)))

func add_item(text: String, _id: int = -1) -> void:
	_texts.append(text)
	_slider.max_value = maxi(_texts.size() - 1, 0)
	_render()

func set_item_tooltip(_index: int, _tip: String) -> void:
	pass

func get_item_count() -> int:
	return _texts.size()

func get_item_text(index: int) -> String:
	return _texts[index] if index >= 0 and index < _texts.size() else ""

func get_item_id(index: int) -> int:
	return index

func clear() -> void:
	_texts.clear()
	_slider.max_value = 0
	_render()

## setSelectedValue: the GUI moves it without a change event.
func select(index: int) -> void:
	_slider.set_value_no_signal(clampi(index, 0, maxi(_texts.size() - 1, 0)))
	_render()

func get_selected() -> int:
	return int(_slider.value) if not _texts.is_empty() else -1

func get_slider() -> HSlider:
	return _slider

func get_caption() -> String:
	return _label.text

func _render() -> void:
	_label.text = get_item_text(int(_slider.value))
