extends CanvasLayer

@onready var chat_log: RichTextLabel = $Panel/VBox/ChatLog
@onready var line_edit: LineEdit = $Panel/VBox/LineEdit
@onready var panel: Panel = $Panel

func _ready() -> void:
	panel.visible = false

func toggle() -> void:
	panel.visible = not panel.visible
	if panel.visible:
		line_edit.grab_focus()

func _input(event: InputEvent) -> void:
	if event is InputEventKey and event.is_pressed() and not event.is_echo():
		if event.keycode == KEY_ENTER:
			if panel.visible and line_edit.text.strip_edges() != "":
				_on_send_pressed()
			else:
				toggle()

func _on_send_pressed() -> void:
	var msg = line_edit.text.strip_edges()
	if msg != "":
		chat_log.append_text("[color=#55bbff][Athens][/color]: " + msg + "\n")
		line_edit.clear()
	panel.visible = false
