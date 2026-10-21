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
	if role == "host":
		# The setup set here, not the last scenario's host page that
		# gamesetup.gd persists in matchsettings.mp.json.
		DirAccess.remove_absolute(ProjectSettings.globalize_path("user://config/matchsettings.mp.json"))
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
	var direct: bool = args.get("nolobby", "") == "1" and args.has("join_addr")
	var late: bool = args.get("late", "") == "1"
	var session = root.get_node("LobbySession")
	if direct:
		# "Connect by IP" from the main menu, not signed in to the lobby.
		var at: PackedStringArray = str(args["join_addr"]).split(":")
		_say("joining %s without the lobby" % args["join_addr"])
		load("res://scenes/menu/gamesetup_mp.gd").start_join(args.get("name", role), at[0], int(at[1]))
	if not direct:
		var lobby = load("res://scenes/menu/lobby.tscn").instantiate()
		root.add_child(lobby)
		current_scene = lobby
		await _wait(0.3)
		lobby._server.text = args.get("lobby", "127.0.0.1:8088")
		lobby._name.text = args.get("name", role)
		lobby._on_sign_pressed()
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
				# late=1: a watcher who joins once the game runs.
				var running := false
				for r in lobby._rooms:
					if str(r.get("id", "")) == "room-%d" % port and str(r.get("state", "init")) != "init":
						running = true
				if top and (running or not late):
					for it in top.get_children():
						if str(it.get_metadata(0)) == "room-%d" % port:
							row = it
			# The list is rebuilt whenever the lobby pushes a change (/events), which
			# frees its TreeItems: never hold one across an await.
			var picked: String = row.get_text(1)
			row.select(0)
			await _wait(0.1)
			_say("picked %s (address %s)" % [picked, "hidden" if lobby._join_ip.text == "" else lobby._join_ip.text])
			if late:
				# JoinButton.js: Yes to "join as observer?" for a running game.
				lobby._observe.button_pressed = true
				lobby._on_join_pressed(true)
			else:
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
	var saw_box := false
	var saw_net_warning := false
	var asked := false
	var host_picked := false
	var refused := false
	var ready_for := -1
	var refusals: Array = []
	var setup_leave: bool = args.get("setup_leave", "") == "1"
	var saw_random := false
	var saw_setup := false
	var saw_observer := false
	# A saved game's Start: the joiner loads under the loading page, never the
	# rejoin box, and the host lists who is still loading.
	var saw_rejoin_box := false
	var saw_saved_loading := false
	var saw_saved_wait := ""
	# A late watcher (NetClient's fake game start): the rejoining box only while
	# the world downloads, then the loading page, then "Synchronizing…" in the match.
	var saw_rejoin_loading := false
	var rejoin_box_after_loading := false
	var saw_sync := false
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
		var rbox = root.find_child("NetConnecting", true, false)
		if rbox and str(rbox.text).contains("rejoining"):
			saw_rejoin_box = true
			rejoin_box_after_loading = rejoin_box_after_loading or saw_rejoin_loading
		if late and saw_rejoin_box and main.net_loading_hold() and root.get_node_or_null("Loading") != null:
			saw_rejoin_loading = true
		if late and str(st.get("net_status", "")).begins_with("Synchronizing") and int(st.get("turn", 0)) > 0:
			saw_sync = true
		if main.net_loading_hold() and root.get_node_or_null("Loading") != null:
			saw_saved_loading = true
		if saw_saved_wait == "" and str(st.get("net_status", "")).begins_with("Waiting for players to connect:"):
			saw_saved_wait = str(st.get("net_status", "")).replace("\n", " ").replace(" ", "_")
		if args.has("join_addr"):
			# gamesetup_mp.js pageConnecting: the box over the screen while connecting.
			var box = root.find_child("NetConnecting", true, false)
			if box and not saw_box:
				saw_box = true
				_say("connecting_box=%s" % box.text)
				if args.get("join_cancel", "") == "1":
					await _wait(0.5)
					box.find_child("Cancel", true, false).pressed.emit()
					await _wait(1.0)
					_say("connect_exit_scene=" + (current_scene.scene_file_path if current_scene else ""))
					var form = current_scene.find_child("GameSetupMp", true, false) if current_scene else null
					_say("reopened=" + ("%s %s:%s" % [form.mode, form.join_server.text, form.join_port.text] if form else "none"))
					break
			# 0.28 reportConnectionRequestTimeOut.
			if st.get("disconnected", false):
				_say("connect_status=%s reason=%d" % [str(st.get("net_status", "")).replace("\n", " | "),
						int(st.get("disconnect_reason", -1))])
				_say("connect_box=%s buttons=%s" % [str(st.get("net_status_title", "")), str(st.get("net_status_buttons", ""))])
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
			if role == "join" and not saw_random and main.is_net_pregame() and str(ms.map_id) == "random":
				saw_random = true
				_say("setup_map=random")
			if role == "host" and main.is_net_pregame() and int(st.get("players", 0)) >= 2:
				await _wait(2.0)
				# MapSelection.js Random: the host's pick stays hidden from the client.
				var page = root.find_child("GameSetup", true, false)
				if page:
					page._on_map_browsed(page.RANDOM_MAP_ID, "random", "")
					await _wait(0.5)
					_say("random_pick=%s hidden=%s" % [str(ms.map_id), str(ms.net_map_random)])
				# 0.28 PlayerAssignment: the host makes bob an observer from his slot's dropdown.
				var cell := root.find_child("Assign1", true, false)
				var pick = cell.find_child("AssignSelect", true, false) if cell else null
				if pick:
					for idx in pick.get_item_count():
						if pick.get_item_text(idx) == "Unassigned":
							pick.select(idx)
							pick.item_selected.emit(idx)
					_say("unassigned slot 2")
				# playtest_godot_mp.py freezes bob now: SetupWindow.js netwarn shows
				# "bob losing connection" over the setup page, not under it.
				var warned := false
				for i in 60:
					await _wait(0.1)
					var w: Dictionary = main.get_lobby_state()
					if not warned and str(w.get("net_warnings", "")) != "":
						warned = true
						_say("setup_net_warning=%s layer=%d" % [str(w.get("net_warnings", "")).split("\n")[0],
								int(w.get("net_warnings_layer", -1))])
				_say("leaving the setup")
				main.leave_net_game()
				await _wait(1.5)
				break
			if role == "join" and st.get("disconnected", false):
				# ChatInputPanel / ReadyButton: the page and its chat stay behind the box,
				# with no chat input and no Ready.
				var chat_in = root.find_child("NetChatInput", true, false)
				var ready_b = root.find_child("BtnReady", true, false)
				var box = root.find_child("NetStatusOverlay", true, false)
				var box_layer: int = box.get_parent().layer if box and box.get_parent() is CanvasLayer else -1
				_say("setup_status=%s reason=%d saw_page=%s page=%s chat_input=%s ready_off=%s box_layer=%d" % [
						str(st.get("net_status", "")).replace("\n", " | "), int(st.get("disconnect_reason", -1)),
						str(saw_setup), str(st.get("setup_page", false)),
						str(chat_in.is_visible_in_tree()) if chat_in else "-", str(ready_b.disabled) if ready_b else "-", box_layer])
				main._on_disconnected_exit_pressed()
				await _wait(1.0)
				_say("setup_exit_scene=" + (current_scene.scene_file_path if current_scene else ""))
				break
			continue
		if not saw_wait and str(st.get("net_status", "")) != "":
			saw_wait = true
			_say("waiting=%s %s" % [st.get("net_status", ""), st.get("net_loading", "")])
			# PauseControl.canPause: no pause before the match is live (g_IsNetworkedActive).
			if "Waiting for players" in str(st.get("net_status", "")):
				_say("wait_can_pause=" + str(main.can_pause()))
		if not saw_net_warning and str(st.get("net_warnings", "")) != "":
			saw_net_warning = true
			_say("net_warning=%s" % str(st.get("net_warnings", "")).replace("\n", " | "))
		if main.is_net_pregame():
			var ns: Dictionary = main.get_net_setup_state()
			var slot := int(ns.get("my_slot", -1))
			if role == "join" and slot >= 0:
				if not asked:
					asked = true
					# 0.28 NetServer: only the controller's setup counts, so this is refused.
					main.request_net_player_setting("civ", 15)
					_say("asked for civ 15 in slot %d" % slot)
				var civs: Array = ms.slot_civs
				var teams: Array = ms.slot_teams
				var cols: Array = ms.slot_colors
				var got: bool = slot < civs.size() and int(civs[slot]) == 15 and int(teams[slot]) == 2 and int(cols[slot]) == 5
				# A saved game's setup refuses every pick (only the seats change).
				if (got or bool(ns.get("saved_game", false))) and refusals.size() and int(ns.get("settings_version", 0)) != ready_for:
					_say("refused=%s" % refusals[0])
					ready_for = int(ns.get("settings_version", 0))
					main.set_net_ready(1)
					_say("ready for settings version %d" % ready_for)
					# 0.28 gamesetup ChatPanel: a line typed in the setup.
					main.send_setup_chat("hello from bob")
					# ChatInputPanel: no addressees in the setup; sent to everyone as typed.
					main.send_setup_chat("/allies team up")
					# 0.28 /list in the setup: g_PlayerAssignments, seated or not.
					main.send_setup_chat("/list")
					var listed := ""
					for i in 30:
						for l in RegEx.create_from_string("\\[[^\\]]*\\]").sub(str(main.get_setup_chat().get("text", "")), "", true).split("\n"):
							if l.contains("Users ("):
								listed = l.strip_edges()
						if listed != "":
							break
						await _wait(0.1)
					_say("setup_list=" + listed.substr(listed.find("Users (")))
			# Once the joiner is seated: before its slot is given it needs no
			# ready (0.28 ReadyController), so Start would not be refused yet.
			# 0.28: only the host sets a player's civ, team and colour, on its page.
			if role == "host" and not host_picked and not bool(ns.get("saved_game", false)):
				var bob_slot := Array(ns.get("slots", [])).map(func(e): return str(e.get("name", ""))).find("bob")
				var page = null
				for n in main.find_children("*", "Control", true, false):
					if "net_main" in n and n.net_main == main:
						page = n
				if bob_slot >= 0 and page:
					host_picked = true
					var row: int = page._net_rows()[bob_slot]
					var civ_idx: int = page._civ_order().map(func(c): return int(page.CIVS[c]["id"])).find(15) + 1
					page._on_slot_civ(civ_idx, row)
					page._on_slot_team(2, row)
					page._on_slot_color(5, row)
					_say("picked civ 15, team 2, colour 5 for bob in slot %d" % bob_slot)
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
						if plain.contains("hello from bob") and plain.contains("team up"):
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
						# LobbyHandler: with the lobby open over the setup, alice is Online, not Busy.
						var presence := ""
						for i in 40:
							if dlobby:
								dlobby._refresh()
								await _wait(0.1)
								for it in dlobby._players.get_root().get_children() if dlobby._players.get_root() else []:
									if str(it.get_metadata(0)) == "alice":
										presence = it.get_text(0)
							if presence == "Online":
								break
						_say("dialog_presence=" + presence)
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
		if late and int(st.get("turn", 0)) >= turns:
			_say("late done turn=%d seat=%d rejoin_box=%s rejoin_loading=%s box_after_loading=%s synchronizing=%s title=%s" % [
					int(st.get("turn", 0)), int(st.get("seat", -1)), str(saw_rejoin_box).to_lower(),
					str(saw_rejoin_loading).to_lower(), str(rejoin_box_after_loading).to_lower(), str(saw_sync).to_lower(),
					str(load("res://scenes/menu/loading.gd").last_title).replace(" ", "_")])
			break
		if int(st.get("turn", 0)) >= turns + 5:
			# 0.28 /list: "Users (N): ..." in the chat, and the command is no chat line.
			for le in main.find_children("*", "LineEdit", true, false):
				if le.placeholder_text == "Type message...":
					le.text = "/list"
					main._on_chat_send_pressed()
			var users := RegEx.create_from_string("Users \\((\\d+)\\): (.*)").search(str(main.get_lobby_state().get("chat", "")))
			# 0.28 ChatAddressees: "■ name" for each other player, sorted by player id; Tab
			# completes from every assigned name.
			# A host that has already ended the game leaves no chat to open (Chat.openPage).
			if bool(main.get_lobby_state().get("disconnected", false)):
				_say("addressees=lost names=-")
			else:
				main.toggle_chat() # the list is filled when the window opens
				var dd: Dictionary = main.debug_dialogs()
				main.toggle_chat()
				_say("addressees=%s names=%s" % [str(dd.get("chat_addressees", "")).replace(" ", "_"), ",".join(dd.get("chat_names", []))])
			_say("users=%s list=%s" % [users.get_string(1) if users else "-", users.get_string(2).replace(" ", "_") if users else "-"])
			# Chat.js: the match's chat history starts empty, without the setup's lines.
			_say("match_chat_carried=%s" % str(str(main.get_setup_chat().get("text", "")).contains("hello from bob")).to_lower())
			st = main.get_lobby_state()
			# 0.28 page_loading at "start": the networked Start loads under the loading page.
			_say("start_loading=" + ",".join(load("res://scenes/menu/loading.gd").last_stages.map(func(x): return str(x[1]).replace(" ", "_"))))
			_say("done turn=%d seat=%d hash=%08X speed=%s civ1=%d team1=%d color1=%d rejoined_lines=%d rejoin_box=%s saved_loading=%s waiting=%s" % [turns, int(st.get("seat", -1)),
					int(main.get_turn_hash(turns)), str(main.get_game_speed()), int(ms.slot_civs[1]),
					int(ms.slot_teams[1]) if ms.slot_teams.size() > 1 else -1, int(ms.slot_colors[1]),
					str(st.get("chat", "")).count("has rejoined the game."), str(saw_rejoin_box).to_lower(),
					str(saw_saved_loading).to_lower(), saw_saved_wait if saw_saved_wait != "" else "-"])
			# PlayerName onFinalizeAttributes: the seated clients' names are the players' names.
			_say("slot_names=" + "|".join(Array(ms.slot_names).slice(0, 2)))
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
				# Chat.openPage: no chat window for a game that is gone.
				var opened: bool = main.chat_open_page()
				main.toggle_chat()
				_say("chat_after_lost=%s open=%s" % [str(opened), str(bool(main.debug_dialogs().get("chat", false)))])
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
