extends SceneTree
# Lobby accounts as 0 A.D. 0.28's prelobby has them:
#   1. EncryptPassword: what the game sends for a password matches
#      tools/lobby_server.py encrypt_password, and the Register checks
#      (checkUsername / checkPassword) refuse bad input;
#   2. against tools/lobby_server.py --accounts-only, through the lobby screen:
#      Register creates the account and signs in, Remember password keeps the
#      encrypted password in lobby.password so the next sign-in needs no typing,
#      a wrong password and a name with no account are refused;
#   3. AccountSettingsPage: "Account settings" changes the password (8 or
#      more characters, typed twice) and keeps the new one in lobby.password
#      when it is remembered; the old one no longer signs in.
#
#   godot --headless --path . -s tools/e2e_lobby_accounts.gd
# Exit code 0 = pass. Needs python3 for the lobby server.

const PORT := 18094
const LobbySessionScript = preload("res://scenes/menu/lobby_session.gd")
# tools/lobby_server.py encrypt_password("hunter22", "Alice")
const ALICE_PASSWORD := "B7F19FEAB59F29067E71E39EF66AAD5C7C96B7CDDBB9C2C1926E192F37C33A56"

var failures := 0
var server_pid := -1

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	run.call_deferred()

# max_frames counts frames at 60 fps of real time: headless --fixed-fps runs
# frames unthrottled, and the lobby's HTTP answers take wall-clock time.
func until(cond: Callable, max_frames := 600) -> bool:
	var deadline := Time.get_ticks_msec() + max_frames * 1000 / 60
	while Time.get_ticks_msec() < deadline:
		if cond.call():
			return true
		await process_frame
	return cond.call()

func run() -> void:
	print("1. passwords")
	check(LobbySessionScript.encrypt_password("hunter22", "Alice") == ALICE_PASSWORD,
		"EncryptPassword matches the lobby server's")
	check(LobbySessionScript.encrypt_password("hunter22", "alice") == ALICE_PASSWORD,
		"the name salts it in any case")
	check(LobbySessionScript.encrypt_password("hunter23", "Alice") != ALICE_PASSWORD, "another password differs")
	check(LobbySessionScript.register_problem("", "x", "x") == "Please enter your username", "a name is needed")
	check(LobbySessionScript.register_problem("bad name", "x", "x") == "Invalid username", "no spaces in a name")
	check(LobbySessionScript.register_problem("alice", "", "") == "Please enter your password", "a password is needed")
	check(LobbySessionScript.register_problem("al", "hunter22", "hunter22") == "Invalid username" and
		LobbySessionScript.register_problem("1234", "hunter22", "hunter22") == "Invalid username",
		"a name needs 3 characters and a letter")
	check(LobbySessionScript.register_problem("alice", "hunter2", "hunter2") == "Please choose a longer password",
		"a password needs 8 characters")
	check(LobbySessionScript.register_problem("alice", "hunter22", "") == "Please enter your password again",
		"the password is asked twice")
	check(LobbySessionScript.register_problem("alice", "hunter22", "hunter23") == "Passwords do not match", "the confirmation must match")
	check(LobbySessionScript.register_problem("Alice.x_1-2", "hunter22", "hunter22") == "", "a good name and password pass")

	print("2. the lobby")
	var accounts := OS.get_user_data_dir().path_join("e2e_accounts.json")
	DirAccess.remove_absolute(accounts)
	server_pid = OS.create_process("python3", [ProjectSettings.globalize_path("res://tools/lobby_server.py"),
		"--port", str(PORT), "--quiet", "--stun-port", "0", "--accounts", accounts, "--accounts-only"])
	check(server_pid > 0, "the lobby server starts")
	var cfg := RtsoUserConfig.new()
	var saved := {}
	for key in ["lobby.login", "lobby.password", "lobby.rememberpassword"]:
		saved[key] = cfg.get_value(key)
	var ls: Node = root.get_node("/root/LobbySession")
	# Wait for the server: a guest is refused, which proves it answers.
	var probe := [0]
	for attempt in 50:
		ls.sign_in("127.0.0.1:%d" % PORT, "carol", func(code: int, _o) -> void: probe[0] = code)
		await until(func() -> bool: return probe[0] != 0, 30)
		if probe[0] == 401:
			break
		probe[0] = 0
		await create_timer(0.1).timeout
	check(probe[0] == 401, "with --accounts-only a name with no account is refused")

	var lobby: Control = load("res://scenes/menu/lobby.tscn").instantiate()
	root.add_child(lobby)
	await process_frame
	lobby._server.text = "127.0.0.1:%d" % PORT
	lobby._name.text = "Alice"
	lobby._password.text = "hunter22"
	lobby._password2.text = "hunter22"
	lobby._remember.button_pressed = true
	lobby._on_register_pressed()
	await until(func() -> bool: return ls.signed_in())
	check(ls.signed_in() and ls.player_name == "Alice", "Register creates the account and signs in")
	cfg.reload()
	check(cfg.get_value("lobby.login") == "Alice" and cfg.get_value("lobby.password") == ALICE_PASSWORD,
		"lobby.login and the encrypted lobby.password are kept")
	check(lobby._password.text == "" and not FileAccess.get_file_as_string(accounts).contains("hunter22")
		and not FileAccess.get_file_as_string(accounts).contains(ALICE_PASSWORD),
		"the typed password is cleared, and the server keeps neither it nor its encryption")

	lobby._on_sign_pressed() # sign out
	check(not ls.signed_in(), "sign out")
	lobby._on_sign_pressed() # the remembered password
	await until(func() -> bool: return ls.signed_in())
	check(ls.signed_in(), "Remember password signs in again with nothing typed")

	lobby._on_sign_pressed()
	lobby._remember.button_pressed = false
	lobby._password.text = "wrong"
	lobby._on_sign_pressed()
	await until(func() -> bool: return lobby._status.text.begins_with("Sign in refused"))
	check(not ls.signed_in() and lobby._status.text == "Sign in refused: Wrong username or password",
		"a wrong password is refused (%s)" % lobby._status.text)
	cfg.reload()
	check(cfg.get_value("lobby.password") == ALICE_PASSWORD, "a refused sign-in keeps the remembered password")

	lobby._password.text = "hunter22"
	lobby._password2.text = "hunter22"
	lobby._on_register_pressed()
	await until(func() -> bool: return lobby._status.text.begins_with("Registration failed"))
	check(lobby._status.text == "Registration failed: Username already taken", "a name registers once")

	lobby._password.text = "hunter22"
	lobby._on_sign_pressed()
	await until(func() -> bool: return ls.signed_in())
	cfg.reload()
	check(ls.signed_in() and cfg.get_value("lobby.password") == "", "without Remember password nothing is kept")

	print("3. account settings")
	lobby._on_account_pressed()
	var dlg: AcceptDialog = lobby.find_child("AccountSettings", true, false)
	check(dlg != null and dlg.title == "Account settings", "Account settings opens")
	var pw: LineEdit = dlg.find_child("Password", true, false) if dlg else null
	var pw2: LineEdit = dlg.find_child("PasswordConfirm", true, false) if dlg else null
	var result: Label = dlg.find_child("RequestResult", true, false) if dlg else null
	check(pw != null and pw.secret and pw2 != null and pw2.secret and result != null, "with two hidden password fields")
	if dlg and pw and pw2 and result:
		pw.text = "short"
		pw2.text = "short"
		dlg.custom_action.emit("change")
		check(result.text == "Please choose a longer password", "a short password is refused")
		pw.text = "correct horse"
		pw2.text = "correct horsf"
		dlg.custom_action.emit("change")
		check(result.text == "Passwords do not match", "so is a confirmation that differs")
		cfg.set_value("lobby.rememberpassword", "true")
		cfg.save()
		pw2.text = "correct horse"
		dlg.custom_action.emit("change")
		await until(func() -> bool: return result.text != "Changing password…")
		check(result.text == "Password changed successfully.", "the password changes: " + result.text)
		cfg.reload()
		check(cfg.get_value("lobby.password") == ls.encrypt_password("correct horse", "Alice"),
			"and the remembered lobby.password is the new one")
		dlg.confirmed.emit()
	ls.sign_out()
	var old := [0]
	ls.sign_in("127.0.0.1:%d" % PORT, "Alice", func(code: int, _o) -> void: old[0] = code, ALICE_PASSWORD)
	await until(func() -> bool: return old[0] != 0)
	check(old[0] == 401, "the old password no longer signs in")
	var new := [0]
	ls.sign_in("127.0.0.1:%d" % PORT, "Alice", func(code: int, _o) -> void: new[0] = code, ls.encrypt_password("correct horse", "Alice"))
	await until(func() -> bool: return new[0] != 0)
	check(new[0] == 200, "the new one does")
	ls.sign_out()

	for key in saved:
		cfg.set_value(key, saved[key])
	cfg.save()
	OS.kill(server_pid)
	DirAccess.remove_absolute(accounts)
	print("RESULT: %s" % ("PASS" if failures == 0 else "FAIL (%d)" % failures))
	quit(0 if failures == 0 else 1)
