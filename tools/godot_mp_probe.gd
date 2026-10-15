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
# role=make_save saves=NAME plays a two-player match alone for a while and saves
# it as NAME; the host given saved=NAME then hosts that save (0 A.D. Host Saved
# Game): the joiner gets the host's world at Start, and the turn hashes match.

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
	if args.has("stun"):
		# The playtest's own STUN responder (tools/lobby_server.py serve_stun).
		RtsoUserConfig.new().set_value("lobby.stun.port", args["stun"])
	if role == "host" or role == "make_save":
		# Two human slots: the joiner takes the second.
		ms.slot_types = [0, 0]
		ms.slot_civs = [0, 1]
		ms.slot_teams = [0, 0]
		ms.player_count = 2
		ms.map_size = 128
		ms.slot_colors = [0, 1]
		ms.game_speed = 1.5
	if role == "make_save":
		await _make_save(ms, args.get("saves", "mp_saved"))
		return
	var lobby = load("res://scenes/menu/lobby.tscn").instantiate()
	root.add_child(lobby)
	current_scene = lobby
	await _wait(0.3)
	lobby._server.text = args.get("lobby", "127.0.0.1:8088")
	lobby._name.text = args.get("name", role)
	lobby._on_sign_pressed()
	var session = root.get_node("LobbySession")
	session.publish_lan = args.get("lan", "1") != "0"
	while not session.signed_in():
		if Time.get_ticks_msec() > deadline:
			return _fail("could not sign in: " + lobby._status.text)
		await _wait(0.2)
	_say("signed in as " + session.player_name)

	if role == "host":
		lobby._host_name.text = "Godot session"
		lobby._host_port.text = str(port)
		if args.has("saved"):
			# Host Saved Game, with the save the load page would hand over.
			ms.load_save_path = "user://saves/%s.rts0sav" % args["saved"]
			ms.net_saved_game = true
		lobby._on_host_pressed()
	elif args.has("join_addr"):
		# A typed address nobody answers at.
		var addr: PackedStringArray = str(args["join_addr"]).split(":")
		lobby._join_ip.text = addr[0]
		lobby._join_port.text = addr[1]
		_say("joining %s" % args["join_addr"])
		lobby._on_join_pressed()
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
		_say("picked %s (address %s)" % [row.get_text(0), "hidden" if lobby._join_ip.text == "" else lobby._join_ip.text])
		lobby._on_join_pressed()
		# The lobby hands the address over on request (/rooms/connect).
		while ms.net_mode != "join":
			if Time.get_ticks_msec() > deadline:
				return _fail("the lobby never gave the game's address: " + lobby._status.text)
			await _wait(0.1)
		_say("joining %s:%d" % [ms.net_ip, ms.net_port])

	# The match: main.tscn behind the loading page.
	var main = null
	var started := false
	var saw_wait := false
	var saw_net_warning := false
	var asked := false
	var refused := false
	var ready_for := -1
	var refusals: Array = []
	var setup_leave: bool = args.get("setup_leave", "") == "1"
	var saw_setup := false
	var saw_observer := false
	while true:
		if Time.get_ticks_msec() > deadline:
			return _fail("timed out at turn %s" % (main.get_lobby_state().get("turn", -1) if main else -1))
		await _wait(0.1)
		var scene = current_scene
		if main == null and scene != null and scene.has_method("get_lobby_state"):
			main = scene
			_say("in the match scene")
			main.net_setup_refused.connect(func(why): refusals.append(why))
		if main == null:
			continue
		var st: Dictionary = main.get_lobby_state()
		if args.has("join_addr"):
			# 0.28 reportConnectionRequestTimeOut.
			if st.get("disconnected", false):
				_say("connect_status=%s reason=%d" % [str(st.get("net_status", "")).replace("\n", " | "),
						int(st.get("disconnect_reason", -1))])
				main._on_disconnected_exit_pressed()
				await _wait(1.0)
				_say("connect_exit_scene=" + (current_scene.scene_file_path if current_scene else ""))
				break
			continue
		if setup_leave:
			# 0.28 reportDisconnect in the setup: the host leaves before Start, and
			# the client's page gives way to "Lost connection to the server.".
			saw_setup = saw_setup or bool(st.get("setup_page", false))
			if role == "join" and not saw_observer and main.is_net_pregame():
				var ns: Dictionary = main.get_net_setup_state()
				if int(ns.get("my_slot", 0)) == -1 and Array(ns.get("observers", [])).any(func(e): return str(e.get("name", "")) == "bob"):
					saw_observer = true
					var obs = root.find_child("NetObservers", true, false)
					_say("moved_to_observers=%s" % (obs.text if obs else "-"))
			if role == "host" and main.is_net_pregame() and int(st.get("players", 0)) >= 2:
				await _wait(2.0)
				# 0.28 PlayerAssignment: the host makes bob an observer from his slot's dropdown.
				var cell := root.find_child("Assign1", true, false)
				var pick = cell.find_child("AssignSelect", true, false) if cell else null
				if pick:
					for idx in pick.get_item_count():
						if pick.get_item_text(idx) == "Unassigned":
							pick.select(idx)
							pick.item_selected.emit(idx)
					_say("unassigned slot 2")
				await _wait(3.0)
				_say("leaving the setup")
				main.leave_net_game()
				await _wait(1.5)
				break
			if role == "join" and st.get("disconnected", false):
				_say("setup_status=%s reason=%d saw_page=%s page=%s" % [str(st.get("net_status", "")).replace("\n", " | "),
						int(st.get("disconnect_reason", -1)), str(saw_setup), str(st.get("setup_page", true))])
				main._on_disconnected_exit_pressed()
				await _wait(1.0)
				_say("setup_exit_scene=" + (current_scene.scene_file_path if current_scene else ""))
				break
			continue
		if not saw_wait and str(st.get("net_status", "")) != "":
			saw_wait = true
			_say("waiting=%s %s" % [st.get("net_status", ""), st.get("net_loading", "")])
		if not saw_net_warning and str(st.get("net_warnings", "")) != "":
			saw_net_warning = true
			_say("net_warning=%s" % str(st.get("net_warnings", "")).replace("\n", " | "))
		if main.is_net_pregame():
			var ns: Dictionary = main.get_net_setup_state()
			var slot := int(ns.get("my_slot", -1))
			if role == "join" and slot >= 0:
				if not asked:
					asked = true
					main.request_net_player_setting("color", 0) # the host's: refused
					main.request_net_player_setting("civ", 15)
					main.request_net_player_setting("team", 2)
					main.request_net_player_setting("color", 5)
					_say("asked for civ 15, team 2, colour 5 in slot %d" % slot)
				var civs: Array = ms.slot_civs
				var teams: Array = ms.slot_teams
				var cols: Array = ms.slot_colors
				var got: bool = slot < civs.size() and int(civs[slot]) == 15 and int(teams[slot]) == 2 and int(cols[slot]) == 5
				if got and refusals.size() and int(ns.get("settings_version", 0)) != ready_for:
					_say("refused=%s" % refusals[0])
					ready_for = int(ns.get("settings_version", 0))
					main.set_net_ready(1)
					_say("ready for settings version %d" % ready_for)
					# 0.28 gamesetup ChatPanel: a line typed in the setup.
					main.send_setup_chat("hello from bob")
			# Once the joiner is seated: before its slot is given it needs no
			# ready (0.28 ReadyController), so Start would not be refused yet.
			if role == "host" and not started and int(st.get("players", 0)) >= 2 \
					and Array(ns.get("slots", [])).any(func(e): return str(e.get("name", "")) == "bob"):
				if not bool(ns.get("can_start", false)):
					if not refused:
						refused = true
						main._on_lobby_start_pressed()
						_say("start_refused=%s" % str(not bool(main.get_lobby_state().get("started", false))))
				else:
					started = true
					# The setup page's chat: the join, the ready and bob's line.
					var plain := ""
					for i in 50:
						plain = RegEx.create_from_string("\\[[^\\]]*\\]").sub(str(main.get_setup_chat().get("text", "")), "", true)
						if plain.contains("hello from bob"):
							break
						await _wait(0.1)
					_say("setup_chat=" + " | ".join(Array(plain.strip_edges().split("\n")).map(func(l): return l.strip_edges())))
					# 0.28 LobbyButton: the lobby as a dialog over the setup, its own game listed, no Host or Join.
					var lb_btn = root.find_child("LobbyButton", true, false)
					if lb_btn and lb_btn.visible:
						lb_btn.pressed.emit()
						await _wait(0.2)
						var dlg = root.find_child("LobbyDialog", true, false)
						var dlobby = dlg.get_child(0) if dlg else null
						var listed := false
						for i in 40:
							if dlobby and dlobby._games.get_root() and dlobby._games.get_root().get_child_count() > 0:
								listed = true
								break
							await _wait(0.1)
						var join_hidden := true
						for b in dlobby.find_children("*", "Button", true, false) if dlobby else []:
							if b.text in ["Join Game", "Host Game"] and b.is_visible_in_tree():
								join_hidden = false
						var opened: bool = dlobby != null and dlobby.dialog
						if dlobby:
							dlobby._on_back_pressed()
						await _wait(0.2)
						_say("lobby_dialog=%s listed=%s join_hidden=%s closed=%s" % [str(opened), str(listed), str(join_hidden),
							str(root.find_child("LobbyDialog", true, false) == null)])
					_say("both players in and ready, pressing Start")
					main._on_lobby_start_pressed()
		if st.get("oos", false):
			return _fail("out of sync at turn %d" % int(st.get("oos_turn", 0)))
		if int(st.get("turn", 0)) >= turns + 5:
			# 0.28 /list: "Users (N): ..." in the chat, and the command is no chat line.
			for le in main.find_children("*", "LineEdit", true, false):
				if le.placeholder_text == "Type message...":
					le.text = "/list"
					main._on_chat_send_pressed()
			var users := RegEx.create_from_string("Users \\((\\d+)\\): (.*)").search(str(main.get_lobby_state().get("chat", "")))
			_say("users=%s list=%s" % [users.get_string(1) if users else "-", users.get_string(2).replace(" ", "_") if users else "-"])
			st = main.get_lobby_state()
			# 0.28 page_loading at "start": the networked Start loads under the loading page.
			_say("start_loading=" + ",".join(load("res://scenes/menu/loading.gd").last_stages.map(func(x): return str(x[1]).replace(" ", "_"))))
			_say("done turn=%d seat=%d hash=%08X speed=%s civ1=%d team1=%d color1=%d rejoined_lines=%d" % [turns, int(st.get("seat", -1)),
					int(main.get_turn_hash(turns)), str(main.get_game_speed()), int(ms.slot_civs[1]),
					int(ms.slot_teams[1]) if ms.slot_teams.size() > 1 else -1, int(ms.slot_colors[1]),
					str(st.get("chat", "")).count("has rejoined the game.")])
			# 0.28 PauseOverlay: the client sees "Paused by" the host's name in its colour.
			if role == "host":
				main.toggle_pause()
				await _wait(4.0)
				main.toggle_pause()
			else:
				var by := ""
				for i in 40:
					by = str(main.debug_dialogs().get("overlay_by", ""))
					if by != "":
						break
					await _wait(0.2)
				_say("paused_by=" + by)
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
				# LobbyGamelistReporter: the lobby's game report marks bob offline.
				var rep: Dictionary = session.room_details(root.get_node("/root/MatchSettings"), main.get_net_setup_state(), 0)
				_say("report_offline=" + ",".join(rep["player_names"].filter(func(p): return p.get("offline", false)).map(func(p): return str(p["name"]))))
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


# A match of two human slots played alone for 20 s of game time, then saved.
func _make_save(ms, name: String) -> void:
	ms.go("res://scenes/main.tscn")
	var main = null
	for i in 600:
		await _wait(0.1)
		if current_scene != null and current_scene.has_method("save_game_as"):
			main = current_scene
			break
	if main == null:
		return _fail("no match scene to save")
	await _wait(20.0)
	if not main.save_game_as(name, "multiplayer saved game test"):
		return _fail("could not save")
	_say("saved " + name)
	quit(0)
