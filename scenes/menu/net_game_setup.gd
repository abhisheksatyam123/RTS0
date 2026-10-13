extends CanvasLayer
## Shows Game Setup over a networked match until it starts (0 A.D. 0.28 opens
## gamesetup for every player of a multiplayer game before the match): the
## page edits the host's MatchSettings, which the RtsoMain parent sends.
## Child of main.tscn's RtsoMain; does nothing outside a networked game.

const GameSetupScene = preload("res://scenes/menu/gamesetup.tscn")

var page: Control = null

func _process(_delta: float) -> void:
	var main := get_parent()
	var pregame: bool = main != null and main.has_method("is_net_pregame") and main.is_net_pregame()
	if pregame and page == null:
		page = GameSetupScene.instantiate()
		page.net_main = main
		add_child(page)
	elif not pregame and page != null:
		page.queue_free()
		page = null
