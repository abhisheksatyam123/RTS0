extends CanvasLayer

@onready var chat_log: RichTextLabel = $Panel/VBox/ChatLog
@onready var line_edit: LineEdit = $Panel/VBox/LineEdit
@onready var panel: Panel = $Panel

func _ready() -> void:
	panel.visible = false
	var ms = (Engine.get_main_loop() as SceneTree).root.get_node_or_null("MatchSettings") if Engine.get_main_loop() is SceneTree else null
	if ms:
		ms.prepare_line_edit(line_edit)

func toggle() -> void:
	panel.visible = not panel.visible
	if panel.visible:
		line_edit.grab_focus()

# The chat hotkey (Return) opens the session chat in chat_window.h; this
# overlay no longer toggles on Enter, so one key doesn't open two chats.

func _on_send_pressed() -> void:
	var msg = line_edit.text.strip_edges()
	if msg != "":
		chat_log.append_text("[color=#55bbff][Athens][/color]: " + msg + "\n")
		line_edit.clear()
	panel.visible = false
