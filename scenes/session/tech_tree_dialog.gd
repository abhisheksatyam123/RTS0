extends CanvasLayer

var is_open: bool = false

func _ready() -> void:
	visible = false

func toggle() -> void:
	is_open = !is_open
	visible = is_open

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo:
		if event.keycode == KEY_T:
			toggle()
			get_viewport().set_input_as_handled()
		elif is_open and event.keycode == KEY_ESCAPE:
			toggle()
			get_viewport().set_input_as_handled()

func _on_close_pressed() -> void:
	toggle()
