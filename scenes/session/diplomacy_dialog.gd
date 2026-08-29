extends CanvasLayer

signal tribute_sent(player_id: int, res_type: int, amount: int)
signal stance_changed(player_id: int, new_stance: int)

func _ready() -> void:
	visible = false

func toggle() -> void:
	visible = not visible

func _on_close_pressed() -> void:
	visible = false

func _on_tribute_pressed(player_id: int, res_type: int, amount: int) -> void:
	emit_signal("tribute_sent", player_id, res_type, amount)

func _on_stance_pressed(player_id: int, new_stance: int) -> void:
	emit_signal("stance_changed", player_id, new_stance)
