extends CanvasLayer

@onready var title_label: Label = $CenterContainer/Panel/VBox/Title
@onready var score_label: Label = $CenterContainer/Panel/VBox/Score
@onready var stats_text: Label = $CenterContainer/Panel/VBox/Stats

func _ready() -> void:
	visible = false

func show_summary(is_victory: bool, record: Dictionary, elapsed_time: float) -> void:
	visible = true
	if is_victory:
		title_label.text = "🏆 VICTORY — Defended Against Invasions!"
		title_label.add_theme_color_override("font_color", Color(0.3, 1.0, 0.5))
	else:
		title_label.text = "☠️ DEFEAT — Civic Center Fallen!"
		title_label.add_theme_color_override("font_color", Color(1.0, 0.3, 0.3))

	var mins = int(elapsed_time) / 60
	var secs = int(elapsed_time) % 60
	var time_str = "%02d:%02d" % [mins, secs]

	var total_score = record.get("enemies_killed", 0) * 100 + record.get("units_trained", 0) * 50 + (record.get("total_food", 0) + record.get("total_wood", 0) + record.get("total_gold", 0)) / 10

	score_label.text = "Overall Score: %d  |  Match Duration: %s" % [total_score, time_str]

	var details = "⚔️ Military:\n"
	details += "  • Enemies Killed: %d\n" % record.get("enemies_killed", 0)
	details += "  • Units Lost: %d\n" % record.get("units_lost", 0)
	details += "  • Units Trained: %d\n\n" % record.get("units_trained", 0)
	details += "🌾 Economy Gathered:\n"
	details += "  • Food: %d  |  Wood: %d  |  Gold: %d  |  Stone: %d\n\n" % [record.get("total_food", 0), record.get("total_wood", 0), record.get("total_gold", 0), record.get("total_stone", 0)]
	details += "🏛️ Infrastructure:\n"
	details += "  • Structures Built: %d" % record.get("buildings_constructed", 0)

	stats_text.text = details

func _on_restart_pressed() -> void:
	get_tree().reload_current_scene()

func _on_menu_pressed() -> void:
	get_tree().change_scene_to_file("res://scenes/menu/main_menu.tscn")
