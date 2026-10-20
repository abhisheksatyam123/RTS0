extends SceneTree
# The lobby's moderators and room subject as 0 A.D. 0.28 has them:
#   1. PlayerColor.ColorPlayerName: a moderator's name carries "@" in the
#      player list and in chat; Subject.js: the room subject shows while no
#      game is selected;
#   2. ChatCommandHandler: /help lists /kick and /ban for a moderator, and
#      "/kick bob spam" removes bob, who is told why (KickStrings.js);
#   3. a moderator kicking us: the lobby says "You have been kicked from the
#      lobby! Reason: ..." and signs us out.
#
#   godot --headless --path . -s tools/e2e_lobby_moderation.gd
# Exit code 0 = pass. Needs python3 for the lobby server.

const PORT := 18097

var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	preload("res://tools/lobby_test_clock.gd").run_in_wall_time(self, "res://tools/e2e_lobby_moderation.gd", run)

# max_frames counts frames at 60 fps of real time: headless --fixed-fps runs
# frames unthrottled, and the lobby's HTTP answers take wall-clock time.
func until(cond: Callable, max_frames := 600) -> bool:
	var deadline := Time.get_ticks_msec() + max_frames * 1000 / 60
	while Time.get_ticks_msec() < deadline:
		if cond.call():
			return true
		await process_frame
	return cond.call()

func names(tree: Tree, col: int) -> Array:
	var out := []
	if tree.get_root():
		for it in tree.get_root().get_children():
			out.append(it.get_text(col))
	return out

func run() -> void:
	var server_pid := OS.create_process("python3", [ProjectSettings.globalize_path("res://tools/lobby_server.py"),
		"--port", str(PORT), "--quiet", "--stun-port", "0", "--exit-with-parent", "--moderators", "alice,mod", "--subject", "Welcome to the lobby"])
	var ls: Node = root.get_node("/root/LobbySession")
	var lobby: Control = load("res://scenes/menu/lobby.tscn").instantiate()
	root.add_child(lobby)
	await process_frame
	var signed := [0]
	# Wait for the server to answer, then sign in once: retrying a sign-in that
	# is slow to answer (a loaded machine) leaves the first one signed in on the
	# server and the retries refused as "that name is in use".
	ls.address = "127.0.0.1:%d" % PORT
	var up := [false]
	for attempt in 100:
		ls.get_json("/players", func(code: int, _o) -> void: up[0] = up[0] or code == 200)
		await until(func() -> bool: return up[0], 30)
		if up[0]:
			break
		await create_timer(0.1).timeout
	ls.sign_in("127.0.0.1:%d" % PORT, "alice", func(code: int, _o) -> void: signed[0] = code)
	await until(func() -> bool: return signed[0] != 0)
	var login := func(who: String, out: Array) -> void:
		ls.post_json("/login", {"name": who}, func(code: int, o) -> void:
			out[0] = str(o.get("token", "")) if code == 200 and o is Dictionary else "-")
	var bob := [""]
	login.call("bob", bob)
	await until(func() -> bool: return bob[0] != "")
	lobby._update_signed_in()

	print("1. moderators and subject")
	lobby._refresh()
	await until(func() -> bool: return names(lobby._players, 1).size() == 2)
	check(names(lobby._players, 1) == ["@alice", "bob"], "alice is listed as @alice: %s" % [names(lobby._players, 1)])
	check(ls.moderator, "and knows she is a moderator")
	check(lobby._details.visible and lobby._details.get_parsed_text() == "Welcome to the lobby", "the subject shows")
	check(lobby._chat_line.max_length == 1024, "the lobby chat takes 1024 characters, as 0.28's")
	lobby._chat_line.text = "hello"
	lobby._on_send_pressed()
	await until(func() -> bool: return lobby._chat_log.get_parsed_text().contains("@alice: hello"))
	check(lobby._chat_log.get_parsed_text().contains("@alice: hello"), "her chat lines read @alice")

	var subj := [false]
	ls.post_json("/subject", {"token": ls.token, "subject": "Play nice"}, func(_c: int, _o) -> void: subj[0] = true)
	await until(func() -> bool: return subj[0])
	lobby._refresh()
	await until(func() -> bool: return lobby._chat_log.get_parsed_text().contains("alice changed the lobby subject to:"))
	check(lobby._chat_log.get_parsed_text().contains("alice changed the lobby subject to:\nPlay nice"),
		"a new subject is announced in the chat")

	print("2. /kick")
	lobby._chat_line.text = "/help"
	lobby._on_send_pressed()
	check(lobby._chat_log.get_parsed_text().contains("kick - Kick a specified user"), "/help lists /kick for a moderator")
	lobby._chat_line.text = "/kick bob spam"
	lobby._on_send_pressed()
	await until(func() -> bool: return names(lobby._players, 1) == ["@alice"])
	check(names(lobby._players, 1) == ["@alice"], "bob is gone")
	await until(func() -> bool: return lobby._chat_log.get_parsed_text().contains("bob has been kicked from the lobby. Reason: spam"))
	check(lobby._chat_log.get_parsed_text().contains("bob has been kicked from the lobby. Reason: spam"), "the lobby is told why")
	var told := [""]
	ls.post_json("/presence", {"token": bob[0], "status": "available"}, func(_c: int, o) -> void:
		told[0] = str(o.get("kicked", "-")) if o is Dictionary else "-")
	await until(func() -> bool: return told[0] != "")
	check(told[0] == "You have been kicked from the lobby! Reason: spam", "bob is told: " + told[0])

	print("2b. muting and notices")
	var carl := [""]
	login.call("carl", carl)
	await until(func() -> bool: return carl[0] != "")
	lobby._chat_line.text = "/mute carl"
	lobby._on_send_pressed()
	await until(func() -> bool: return lobby._chat_log.get_parsed_text().contains("carl has been muted."))
	check(lobby._chat_log.get_parsed_text().contains("carl has been muted."), "/mute carl: the room hears carl has been muted")
	var said := [0]
	ls.post_json("/chat", {"token": carl[0], "text": "hey"}, func(c: int, _o) -> void: said[0] = c)
	await until(func() -> bool: return said[0] != 0)
	check(said[0] == 403, "a muted player cannot chat")
	lobby._selected_player = "carl"
	lobby._refresh()
	await until(func() -> bool: return lobby._muted.has("carl"))
	lobby._request_profile("carl")
	check(lobby._profile_role.text == "Muted Player", "carl's profile says Muted Player: " + lobby._profile_role.text)
	# StatusMessageEvents.js Role, worded for the reader; ChatInputPanel hides a muted reader's input.
	check(lobby._format_event({"kind": "role", "nick": "alice", "newrole": "visitor", "oldrole": "participant"}, "alice")
		.contains("You have been muted.") and lobby._format_event({"kind": "role", "nick": "carl", "newrole": "participant",
		"oldrole": "visitor"}, "alice").contains("carl has been unmuted.") and lobby._format_event({"kind": "role",
		"nick": "alice", "newrole": "moderator", "oldrole": "participant"}, "alice").contains("You are now a moderator."),
		"role lines read You have been muted. / carl has been unmuted. / You are now a moderator.")
	lobby._muted["alice"] = true
	lobby._update_chat_input()
	check(not lobby._chat_line.visible and not lobby._chat_send.visible, "a muted reader has no chat input")
	lobby._muted.erase("alice")
	lobby._update_chat_input()
	check(lobby._chat_line.visible, "and gets it back when unmuted")
	var mod := [""]
	login.call("mod", mod)
	await until(func() -> bool: return mod[0] != "")
	var refused := [0]
	ls.post_json("/mute", {"token": mod[0], "nick": "alice"}, func(c: int, _o) -> void: refused[0] = c)
	await until(func() -> bool: return refused[0] != 0)
	check(refused[0] == 403, "a moderator cannot be muted")
	lobby._chat_line.text = "/announce Server restarts at noon."
	lobby._on_send_pressed()
	await until(func() -> bool: return lobby.find_child("NoticeDialog", true, false) != null)
	var notice: AcceptDialog = lobby.find_child("NoticeDialog", true, false)
	check(notice != null and notice.title == "Notice" and notice.dialog_text == "Server restarts at noon.", "/announce opens a Notice box")
	check(lobby._chat_log.get_parsed_text().contains("== Notice:\nServer restarts at noon."), "and the chat reads == Notice:")

	print("3. kicked ourselves")
	var done := [false]
	ls.post_json("/kick", {"token": mod[0], "nick": "alice", "reason": "testing", "ban": true}, func(_c: int, _o) -> void: done[0] = true)
	await until(func() -> bool: return done[0])
	lobby._refresh()
	ls._beat()
	await until(func() -> bool: return not ls.signed_in())
	check(not ls.signed_in() and not ls.moderator, "alice is signed out")
	check(lobby._chat_log.get_parsed_text().contains("You have been banned from the lobby! Reason: testing"), "and told she was banned")
	var box: AcceptDialog = lobby.find_child("KickedDialog", true, false)
	check(box != null and box.title == "BANNED" and box.dialog_text.begins_with("You have been banned from the lobby!"),
		"in a BANNED message box (ConnectionHandler.onClientKicked)")
	var again := [0]
	ls.sign_in("127.0.0.1:%d" % PORT, "alice", func(code: int, _o) -> void: again[0] = code)
	await until(func() -> bool: return again[0] != 0)
	check(again[0] == 403, "a banned name cannot sign in again (%d)" % again[0])

	ls.sign_out()
	OS.kill(server_pid)
	print("RESULT: %s" % ("PASS" if failures == 0 else "FAIL (%d)" % failures))
	quit(0 if failures == 0 else 1)
