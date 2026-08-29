extends CanvasLayer

signal barter_requested(sell_type: int, buy_type: int, amount: int)

@onready var panel: Panel = $CenterContainer/Panel

func _ready() -> void:
	visible = false

func toggle() -> void:
	visible = not visible

func _on_close_pressed() -> void:
	visible = false

func _on_barter_btn_pressed(sell_type: int, buy_type: int, amount: int) -> void:
	emit_signal("barter_requested", sell_type, buy_type, amount)
