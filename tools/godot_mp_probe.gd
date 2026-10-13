extends SceneTree
# One player of tools/playtest_godot_mp.py: a real game client, run headless,
# that goes through the menu lobby as a person would. It signs in to the lobby
# server, then hosts (and presses Start once the other player is in) or picks
# the host's game from the list and joins it, plays to a turn, and prints that
# turn's lockstep hash. Arguments after "--":
#   role=host|join name=N lobby=IP:PORT port=P turns=T timeout=S [leave=host|join]
# With leave, that player then quits the match: a host left behind must read
# "<name> has left the game." in its chat, and a client whose host left must
# get the "Connection to the server has been lost." overlay, whose Exit opens
# the summary as a disconnected player (0 A.D. g_Disconnected).
# In game setup the client asks for its own civ, team and colour and then
# presses Ready; the host's Start is refused until it does, and the match
# plays at the host's game speed on both.

var args := {}


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		var kv := str(a).split("=", true, 1)
		if kv.size() == 2:
			args[kv[0]] = kv[1]
	_run.call_deferred()


func _say(text: String) -> void:
	print("[gmp] %s %s" % [args.get("role", "?"), text])


func _wait(s: float) -> void:
	await create_timer(s).timeout


func _fail(text: String) -> void:
	_say("FAIL " + text)
	quit(1)


func _run() -> void:
	var role: String = args.get("role", "host")
	var port := int(args.get("port", "20640"))
	var turns := int(args.get("turns", "150"))
	var deadline := Time.get_ticks_msec() + int(args.get("timeout", "240")) * 1000
	var ms = root.get_node("MatchSettings")
	if role == "host":
		# Two human slots: the joiner takes the second.
		ms.slot_types = [0, 0]
		ms.slot_civs = [0, 1]
		ms.slot_teams = [0, 0]
		ms.player_count = 2
		ms.map_size = 128
		ms.slot_colors = [0, 1]
		ms.game_speed = 1.5
	var lobby = load("res://scenes/menu/lobby.tscn").instantiate()
	root.add_child(lobby)
	current_scene = lobby
	await _wait(0.3)
	lobby._server.text = args.get("lobby", "127.0.0.1:8088")
	lobby._name.text = args.get("name", role)
	lobby._on_sign_pressed()
	var session = root.get_node("LobbySession")
	while not session.signed_in():
		if Time.get_ticks_msec() > deadline:
			return _fail("could not sign in: " + lobby._status.text)
		await _wait(0.2)
	_say("signed in as " + session.player_name)

	if role == "host":
		lobby._host_name.text = "Godot session"
		lobby._host_port.text = str(port)
		lobby._on_host_pressed()
	else:
		var row = null
		while row == null:
			if Time.get_ticks_msec() > deadline:
				return _fail("the host's game never showed in the list")
			lobby._refresh()
			await _wait(0.5)
			var top = lobby._games.get_root()
			if top:
				for it in top.get_children():
					if str(it.get_metadata(0)) == "room-%d" % port:
						row = it
		row.select(0)
		await _wait(0.1)
		_say("picked %s at %s:%s" % [row.get_text(0), lobby._join_ip.text, lobby._join_port.text])
		lobby._on_join_pressed()

	# The match: main.tscn behind the loading page.
	var main = null
	var started := false
	var saw_wait := false
	var asked := false
	var refused := false
	var ready_for := -1
	while true:
		if Time.get_ticks_msec() > deadline:
			return _fail("timed out at turn %s" % (main.get_lobby_state().get("turn", -1) if main else -1))
		await _wait(0.1)
		var scene = current_scene
		if main == null and scene != null and scene.has_method("get_lobby_state"):
			main = scene
			_say("in the match scene")
		if main == null:
			continue
		var st: Dictionary = main.get_lobby_state()
		if not saw_wait and str(st.get("net_status", "")) != "":
			saw_wait = true
			_say("waiting=%s %s" % [st.get("net_status", ""), st.get("net_loading", "")])
		if main.is_net_pregame():
			var ns: Dictionary = main.get_net_setup_state()
			var slot := int(ns.get("my_slot", -1))
			if role == "join" and slot >= 0:
				if not asked:
					asked = true
					main.request_net_player_setting("civ", 15)
					main.request_net_player_setting("team", 2)
					main.request_net_player_setting("color", 5)
					_say("asked for civ 15, team 2, colour 5 in slot %d" % slot)
				var civs: Array = ms.slot_civs
				var teams: Array = ms.slot_teams
				var cols: Array = ms.slot_colors
				var got: bool = slot < civs.size() and int(civs[slot]) == 15 and int(teams[slot]) == 2 and int(cols[slot]) == 5
				if got and int(ns.get("settings_version", 0)) != ready_for:
					ready_for = int(ns.get("settings_version", 0))
					main.set_net_ready(true)
					_say("ready for settings version %d" % ready_for)
			if role == "host" and not started and int(st.get("players", 0)) >= 2:
				if not bool(ns.get("can_start", false)):
					if not refused:
						refused = true
						main._on_lobby_start_pressed()
						_say("start_refused=%s" % str(not bool(main.get_lobby_state().get("started", false))))
				else:
					started = true
					_say("both players in and ready, pressing Start")
					main._on_lobby_start_pressed()
		if st.get("oos", false):
			return _fail("out of sync at turn %d" % int(st.get("oos_turn", 0)))
		if int(st.get("turn", 0)) >= turns + 5:
			_say("done turn=%d seat=%d hash=%08X speed=%s civ1=%d team1=%d color1=%d" % [turns, int(st.get("seat", -1)),
					int(main.get_turn_hash(turns)), str(main.get_game_speed()), int(ms.slot_civs[1]),
					int(ms.slot_teams[1]) if ms.slot_teams.size() > 1 else -1, int(ms.slot_colors[1])])
			break
	var leave: String = args.get("leave", "")
	if leave == role:
		_say("leaving the match")
		ms.go("res://scenes/menu/main_menu.tscn")
		await _wait(1.5)
	elif leave != "":
		while true:
			if Time.get_ticks_msec() > deadline:
				return _fail("never saw the other player leave")
			await _wait(0.2)
			var st: Dictionary = main.get_lobby_state()
			if role == "host" and str(st.get("chat", "")).contains("has left the game."):
				var left := 0
				for line in str(st.get("chat", "")).split("\n"):
					if line.contains("has left the game."):
						left += 1
						_say("chat_left=" + line.strip_edges())
				_say("chat_left_lines=%d" % left)
				break
			if role == "join" and st.get("disconnected", false):
				_say("net_status=" + str(st.get("net_status", "")).replace("\n", " | ") +
						" reason=%d" % int(st.get("disconnect_reason", -1)))
				main._on_disconnected_exit_pressed()
				var g: Dictionary = load("res://scenes/session/summary_screen.gd").pending.get("gui", {})
				_say("summary_disconnected=%s" % str(g.get("disconnected", false)))
				await _wait(1.0)
				break
	session.sign_out()
	await _wait(0.5)
	quit(0)
