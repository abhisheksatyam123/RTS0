extends SceneTree
# Game Setup in a networked game, as 0 A.D. 0.28's gamesetup has it, against a
# stand-in for RtsoMain's networked setup (tools/godot_mp_probe.gd plays the
# real one over the network):
#   1. the host edits the page; its MatchSettings is the setup sent, Closed
#      slots dropped; a client's own civ, team and colour, which RtsoMain
#      writes into MatchSettings, show on the host's page; Start is disabled,
#      with 0.28's tooltip, until every player is ready, then starts the game;
#   2. a client sees the host's setup, may change only its own slot's civ,
#      team and colour (asked of the host), presses Ready ("I'm ready",
#      "Stay ready", "I'm not ready!") and waits for the host, with no
#      Start button; a refused request says why, and Back leaves the game;
#   2b. with a saved game (0.28 isSavedGame) the page shows "Saved game", the
#      host may change only who plays which slot (no AI slot takes a player),
#      the save keeps its cheats and is never rated, and a client changes
#      nothing but its Ready;
#   3. outside a networked game the page is the single-player one;
#   4. with "Persist match settings" on, the host's Start and Back keep its page in
#      matchsettings.mp.json (PersistentMatchSettings(isNetworked)), apart from
#      the single-player file; its next setup starts from it, a client's never,
#      and a saved game's is not kept.
# The host's slot names are 0.28's PlayerAssignment dropdowns: the clients and
# Unassigned, which makes the slot's player an observer.
# A lobby game of two players is a Rated Game by default (0.28 Rating.js),
# and a rated game has no cheats (Cheats.js).
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_setup_net.gd
# Exit code 0 = pass.

class StubMain extends Node:
	signal net_setup_changed
	signal net_setup_refused(reason: String)
	var state := {}
	var requests: Array = []
	var pushes := 0
	var started := 0
	var is_ready := 0
	var left := 0
	func is_net_pregame() -> bool:
		return true
	func get_net_setup_state() -> Dictionary:
		var d := state.duplicate(true)
		if not bool(d.get("controller", false)):
			d["ready"] = is_ready
		return d
	func push_net_setup() -> void:
		pushes += 1
	func request_net_player_setting(key: String, value: int) -> void:
		requests.append([key, value])
	func set_net_ready(status: int) -> void:
		is_ready = status
	func leave_net_game() -> void:
		left += 1
	var assigns: Array = []
	func assign_net_client(client: int, slot: int) -> void:
		assigns.append([client, slot])
	func _on_lobby_start_pressed() -> void:
		started += 1
	func get_setup_chat() -> Dictionary:
		return {"rev": 1, "text": ""}
	func send_setup_chat(_text: String) -> void:
		pass

var failures := 0
var cfg := RtsoUserConfig.new()
var saved_persist := ""

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func open_page(stub: Node) -> Control:
	var page: Control = load("res://scenes/menu/gamesetup.tscn").instantiate()
	page.net_main = stub
	root.add_child(page)
	await frames(6)
	return page

func cells(page) -> Array:
	return page._slots_grid().get_children().filter(func(c): return not c.is_queued_for_deletion())

## A slot's name cell: a label, or on the host's page the assignment dropdown's pick.
func slot_text(cell) -> String:
	var pick = cell.get_node_or_null("AssignSelect")
	return pick.get_item_text(pick.get_selected()) if pick else str(cell.text)

func picker(page, slot: int, col: int):
	return cells(page)[slot * 6 + col].get_child(0)

func run() -> void:
	saved_persist = cfg.get_value("persistmatchsettings")
	cfg.set_value("persistmatchsettings", "false")
	var ms = root.get_node("MatchSettings")
	ms.map_id = "random_mainland"
	ms.seed = 7
	ms.game_speed = 1.0
	ms.slot_types = [0, 0, 2]
	ms.slot_civs = [1, -1, 2]
	ms.slot_teams = [0, 0, 0]
	ms.slot_diffs = [3, 3, 3]
	ms.slot_behs = [2, 2, 2]
	ms.slot_colors = [0, 1, 2]
	ms.net_lobby = "127.0.0.1:1"
	ms.lock_teams = false

	print("1. the host")
	var host := StubMain.new()
	root.add_child(host)
	host.state = {"controller": true, "my_client": 0, "my_slot": 0, "can_start": false, "all_ready": false,
		"slots": [{"client": 0, "name": "alice", "ready": 2}, {"client": 1, "name": "bob", "ready": 0}],
		"observers": [], "locked": false, "version": 1, "settings_version": 1}
	var page := await open_page(host)
	check(page._mp_role == "host", "the controller hosts the page")
	var start: Button = page.get_node("VBox/HBox/Right/Buttons/BtnStart")
	check(start.visible and start.disabled and start.tooltip_text == page.START_WAIT_TOOLTIP,
		"Start waits for every player to be ready")
	check(not page.get_node("VBox/HBox/Right/Buttons/BtnReady").visible, "the host has no Ready button")
	check(ms.slot_types == [0, 0] and ms.slot_civs == [1, -1] and ms.player_count == 2,
		"the setup drops the Closed slot and keeps Random (%s %s)" % [str(ms.slot_types), str(ms.slot_civs)])
	check(host.pushes > 0 and not ms.cheats, "the host sends its setup, cheats off")
	# LockedTeams.onLoad: DefaultValue = Engine.HasNetClient(), so a networked setup opens locked.
	check(page.get_node("%LockTeams").button_pressed and ms.lock_teams, "a networked setup opens with Teams Locked on")
	check(slot_text(cells(page)[0]) == "alice (You)" and slot_text(cells(page)[6]) == "bob", "slots show their players")
	var assign = cells(page)[6].get_node_or_null("AssignSelect")
	var items: Array = []
	for i in (assign.get_item_count() if assign else 0):
		items.append(assign.get_item_text(i))
	check(items == ["alice (You)", "bob", "Unassigned"], "the host assigns players from each slot (%s)" % str(items))
	if assign:
		assign.select(2)
		assign.item_selected.emit(2)
	check(host.assigns == [[1, -1]], "Unassigned makes bob an observer (%s)" % str(host.assigns))
	var vis := "%"
	var rated: CheckBox = page.get_node(vis + "RatedGame")
	var cheats: CheckBox = page.get_node(vis + "Cheats")
	check(rated.visible and rated.button_pressed and cheats.disabled and not cheats.button_pressed and ms.rated,
		"a lobby 1v1 is a Rated Game by default, without cheats")
	rated.button_pressed = false
	cheats.button_pressed = true
	await create_timer(0.4).timeout
	check(not cheats.disabled and not ms.rated and ms.cheats, "unrated, the host may allow cheats")
	check(not rated.visible, "with cheats on there is no Rated Game (Rating.js available)")
	cheats.button_pressed = false
	await create_timer(0.4).timeout
	check(rated.visible and rated.button_pressed and ms.rated and not ms.cheats and cheats.disabled,
		"cheats off: rated again by default (maybeUpdate enabled = available)")
	page.get_node("VBox/HBox/Right/SlotsBox/SlotCountRow/SpinSlots").value = 3
	page._on_slot_kind(1, 2)
	await create_timer(0.4).timeout
	check(not rated.visible and not ms.rated, "three players: not rated")
	page._on_slot_kind(2, 2)
	await create_timer(0.4).timeout
	check(rated.visible and ms.rated, "two again")
	page.get_node("%SpinSeed").value = 99
	await create_timer(0.4).timeout
	check(ms.seed == 99, "a change on the page reaches MatchSettings")
	# MapSelection.js Random: the host's map is picked now but stays Random for
	# everyone else until Start (RtsoMain sends it as "random").
	page._on_map_browsed(page.RANDOM_MAP_ID, "random", "")
	await create_timer(0.4).timeout
	var rooms: GDScript = load("res://scenes/menu/lobby_session.gd")
	check(ms.net_map_random and str(ms.map_id) not in ["", page.RANDOM_MAP_ID]
		and rooms.room_details(ms, {}, 0)["map_id"] == "random" and rooms.room_details(ms, {}, 0)["map_name"] == "Random"
		and rooms.room_details(ms, {}, 1760000000)["map_id"] == str(ms.map_id),
		"Random: the host keeps its pick (%s) and the game list says Random until the game starts" % str(ms.map_id))
	page._on_map_browsed("random_mainland", "random", "")
	await create_timer(0.4).timeout
	check(not ms.net_map_random and ms.map_id == "random_mainland", "another map is no longer Random")
	ms.slot_civs = [1, 15]
	ms.slot_teams = [0, 2]
	ms.slot_colors = [0, 5]
	host.state["slots"][1]["ready"] = 1
	host.state["can_start"] = true
	host.state["all_ready"] = true
	host.net_setup_changed.emit()
	await frames(3)
	check(page._slot_rows[1]["civ"] == 15 and page._slot_rows[1]["team"] == 2 and page._slot_rows[1]["color"] == 5,
		"bob's own picks show on the host's page")
	await create_timer(0.4).timeout
	check(ms.slot_civs == [1, 15] and ms.slot_colors == [0, 5], "the host's page keeps them")
	check(slot_text(cells(page)[6]) == "bob (Ready)", "bob is ready")
	check(not start.disabled and start.tooltip_text == page.START_TOOLTIP, "every player ready: Start")
	page._on_start_pressed()
	check(host.started == 1, "Start starts the networked game")
	page.queue_free()
	host.queue_free()
	await frames(2)

	print("2. a client")
	var client := StubMain.new()
	root.add_child(client)
	client.state = {"controller": false, "my_client": 1, "my_slot": 1, "can_start": false, "all_ready": false,
		"slots": [{"client": 0, "name": "alice", "ready": 2}, {"client": 1, "name": "bob", "ready": 0}],
		"observers": [], "locked": false, "version": 2, "settings_version": 2}
	ms.slot_types = [0, 0]
	ms.slot_civs = [1, 3]
	ms.slot_teams = [1, 2]
	ms.slot_colors = [0, 4]
	ms.seed = 1234
	ms.rated = true
	ms.cheats = false
	page = await open_page(client)
	check(page._mp_role == "client", "a client's page")
	start = page.get_node("VBox/HBox/Right/Buttons/BtnStart")
	var ready_btn: Button = page.get_node("VBox/HBox/Right/Buttons/BtnReady")
	check(not start.visible and page.get_node("VBox/HBox/Right/NetWaitLabel").visible, "no Start: waiting for the host")
	check(page.get_node("%SpinSeed").value == 1234 and page._slot_rows[1]["civ"] == 3,
		"the host's setup shows")
	check(page.get_node("%RatedGame").visible
		and page.get_node("%RatedGame").button_pressed
		and page.get_node("%RatedGame").disabled, "the host's Rated Game shows, locked")
	check(not page.get_node("%SpinSeed").editable
		and page.get_node("%MapList").mouse_filter == Control.MOUSE_FILTER_IGNORE
		and page.get_node("%Waves").disabled, "the host's settings are locked")
	check(page._tab == -1 and not page.get_node("%MapTab").visible, "a client starts with no settings tab open (GameSettingTabs.onLoad)")
	check(picker(page, 0, 2).disabled and picker(page, 0, 3).disabled and picker(page, 1, 1).disabled and picker(page, 1, 4).disabled,
		"other slots, and our slot's type and AI, are locked")
	# 0.28 GameSettingControl/PlayerColor: only the controller sets civ, team and colour.
	check(picker(page, 1, 2).disabled and picker(page, 1, 3).disabled and picker(page, 1, 5).disabled
		and cells(page)[1 * 6 + 5].modulate.a == 0.0, "our own civ and team are locked too, and the colours hidden")
	check(ready_btn.text == "I'm ready" and not ready_btn.disabled, "Ready: I'm ready")
	ready_btn.pressed.emit()
	check(client.is_ready == 1 and ready_btn.text == "Stay ready", "pressed: ready, then Stay ready")
	ready_btn.pressed.emit()
	check(client.is_ready == 2 and ready_btn.text == "I'm not ready!", "pressed again: stays ready, then I'm not ready!")
	# ReadyButton.onPressRight: a right-click is the quick way out of Stay ready.
	var rc := InputEventMouseButton.new()
	rc.button_index = MOUSE_BUTTON_RIGHT
	rc.pressed = true
	ready_btn.gui_input.emit(rc)
	check(client.is_ready == 0 and ready_btn.text == "I'm ready", "a right-click: not ready")
	ready_btn.gui_input.emit(rc)
	check(client.is_ready == 0, "and again it stays not ready")
	ready_btn.pressed.emit()
	ms.seed = 555
	client.is_ready = 0 # RtsoMain takes a Ready (not a Stay ready) back when a setting changes
	client.net_setup_changed.emit()
	await frames(3)
	check(page.get_node("%SpinSeed").value == 555 and ready_btn.text == "I'm ready",
		"a new setup shows, and Ready is back")
	check(picker(page, 1, 2).disabled and picker(page, 0, 2).disabled, "the locks hold after the update")
	# ReadyButton.onPlayerAssignmentsChange: an unassigned client (observer) has no Ready button.
	var seat_was = client.state["my_slot"]
	client.state["my_slot"] = -1
	client.net_setup_changed.emit()
	await frames(3)
	check(not ready_btn.visible, "an observer sees no Ready button")
	client.state["my_slot"] = seat_was
	client.net_setup_changed.emit()
	await frames(3)
	check(ready_btn.visible, "seated again, it shows")
	ms.map_id = "random"
	client.net_setup_changed.emit()
	await frames(3)
	check(str(page._selected_map().get("id", "")) == page.RANDOM_MAP_ID, "a host's Random map shows as Random")
	client.net_setup_refused.emit("That colour is taken.")
	check(page.get_node("VBox/HBox/Right/ErrorLabel").text == "That colour is taken.", "a refused request says why")
	check(not page.get_node("VBox/HBox/Right/Buttons/BtnBack").disabled and page.get_node("%NetChatInput").editable
		and not page.get_node("%NetChatSend").disabled, "a client may still go Back and chat")
	# ChatInputPanel.onTab / ChatInputAutocomplete: names first, then civs, then settings.
	var chat_in: LineEdit = page.get_node("%NetChatInput")
	check(chat_in.max_length == 1024, "the setup chat takes 1024 characters, as 0.28's")
	check(chat_in.tooltip_text == "Press Tab to autocomplete player names or settings.", "the chat tooltip names Tab")
	var entries: Array = page.autocomplete_entries()
	check(entries.find("bob") >= 0 and entries.find("bob") < entries.find("Britons")
		and entries.find("Britons") < entries.find("Random"), "names come before civs, civs before settings")
	# "bo" also starts Botswanan Haven, as in 0.28's list.
	check(page.try_autocomplete("hi bo", entries) == "hi bo" and page.try_autocomplete("hi bob", entries) == "hi bob"
		and page.try_autocomplete("kush", entries) == "Kushites", "a word completes only when one entry starts with it")
	chat_in.grab_focus()
	chat_in.text = "gg ali"
	chat_in.caret_column = 6
	var tab := InputEventKey.new()
	tab.keycode = KEY_TAB
	tab.pressed = true
	Input.parse_input_event(tab)
	await frames(3)
	check(chat_in.text == "gg alice" and chat_in.caret_column == 8 and chat_in.has_focus(), "Tab in the chat completes alice: " + chat_in.text)
	chat_in.clear()
	page._on_back_pressed()
	check(client.left == 1, "Back leaves the networked game")
	client.state["locked"] = true
	client.net_setup_changed.emit()
	await frames(3)
	check(picker(page, 1, 2).disabled, "a map that sets the players locks ours too")
	page.queue_free()
	client.queue_free()
	await frames(2)

	print("2b. a saved game")
	ms.net_lobby = "127.0.0.1:1"
	ms.slot_types = [0, 1, 0]
	ms.slot_civs = [1, 3, 5]
	ms.slot_teams = [0, 0, 0]
	ms.slot_colors = [0, 1, 2]
	ms.seed = 4242
	ms.cheats = true
	ms.rated = false
	host = StubMain.new()
	root.add_child(host)
	host.state = {"controller": true, "my_client": 0, "my_slot": 0, "can_start": true, "all_ready": true, "saved_game": true,
		"slots": [{"client": 0, "name": "alice", "ready": 2}, {"client": -1}, {"client": 1, "name": "bob", "ready": 1}],
		"observers": [], "locked": true, "version": 1, "settings_version": 1}
	page = await open_page(host)
	await create_timer(0.4).timeout
	start = page.get_node("VBox/HBox/Right/Buttons/BtnStart")
	check(page.get_node("VBox/HBox/Right/SavedGameLabel").visible
		and page.get_node("VBox/HBox/Right/SavedGameLabel").tooltip_text == "The controller loaded a saved game.",
		"the host's page says Saved game")
	check(not page.get_node("%SpinSeed").editable and page.get_node("%MapList").mouse_filter == Control.MOUSE_FILTER_IGNORE
		and page.get_node("%Waves").disabled and picker(page, 0, 2).disabled and picker(page, 2, 3).disabled
		and picker(page, 1, 1).disabled and picker(page, 2, 5).disabled, "every setting of the save is locked")
	check(not cells(page)[0].get_node("AssignSelect").disabled and not cells(page)[12].get_node("AssignSelect").disabled
		and cells(page)[6].get_node_or_null("AssignSelect") == null, "the host may still seat players, not on the AI's slot")
	check(not start.disabled and not page.get_node("VBox/HBox/Right/Buttons/BtnBack").disabled
		and page.get_node("%NetChatInput").editable, "Start, Back and the chat stay")
	check(ms.cheats and not ms.rated and not page.get_node("%RatedGame").visible and ms.seed == 4242
		and ms.slot_types == [0, 1, 0], "the save keeps its own settings (cheats on, never rated)")
	page.queue_free()
	host.queue_free()
	await frames(2)
	client = StubMain.new()
	root.add_child(client)
	client.state = {"controller": false, "my_client": 1, "my_slot": 2, "can_start": false, "all_ready": false, "saved_game": true,
		"slots": [{"client": 0, "name": "alice", "ready": 2}, {"client": -1}, {"client": 1, "name": "bob", "ready": 0}],
		"observers": [], "locked": true, "version": 1, "settings_version": 1}
	page = await open_page(client)
	ready_btn = page.get_node("VBox/HBox/Right/Buttons/BtnReady")
	check(page.get_node("VBox/HBox/Right/SavedGameLabel").visible, "the client's page says Saved game")
	check(picker(page, 2, 2).disabled and picker(page, 2, 3).disabled and picker(page, 2, 5).disabled
		and not ready_btn.disabled, "a client changes nothing of a saved game but its Ready")
	page.queue_free()
	client.queue_free()
	await frames(2)

	print("3. single player")
	ms.net_lobby = ""
	ms.rated = false
	page = await open_page(null)
	check(page._mp_role == "none" and page.get_node("VBox/HBox/Right/Buttons/BtnStart").visible
		and not page.has_node("VBox/HBox/Right/Buttons/BtnReady") and not page.get_node("%Waves").disabled
		and not page.has_node("%RatedGame")
		and page.get_node("%Cheats").disabled,
		"the single-player page")
	page.queue_free()
	await frames(2)

	print("4. the host's persisted settings")
	var Setup = load("res://scenes/menu/gamesetup.gd")
	cfg.set_value("persistmatchsettings", "true")
	for f in [Setup.PERSIST_FILE, Setup.PERSIST_FILE_MP]:
		DirAccess.remove_absolute(ProjectSettings.globalize_path(f))
	ms.net_lobby = "127.0.0.1:1"
	ms.slot_types = [0, 0]
	ms.slot_civs = [1, 3]
	ms.slot_teams = [0, 0]
	ms.slot_colors = [0, 1]
	ms.seed = 5
	var host_state := {"controller": true, "my_client": 0, "my_slot": 0, "can_start": true, "all_ready": true,
		"slots": [{"client": 0, "name": "alice", "ready": 2}, {"client": 1, "name": "bob", "ready": 1}],
		"observers": [], "locked": false, "version": 1, "settings_version": 1}
	host = StubMain.new()
	root.add_child(host)
	host.state = host_state.duplicate(true)
	page = await open_page(host)
	page.get_node("%SpinSeed").value = 31337
	page._on_start_pressed()
	check(host.started == 1 and Setup.load_persisted(true).get("seed") == 31337 and not FileAccess.file_exists(Setup.PERSIST_FILE),
		"the host's Start keeps its page in matchsettings.mp.json, not the single-player file")
	page.queue_free()
	host.queue_free()
	await frames(2)
	ms.seed = 6
	host = StubMain.new()
	root.add_child(host)
	host.state = host_state.duplicate(true)
	page = await open_page(host)
	check(page.get_node("%SpinSeed").value == 31337, "the host's next setup starts from it")
	page.get_node("%SpinSeed").value = 4040
	page._on_back_pressed()
	check(host.left == 1 and Setup.load_persisted(true).get("seed") == 4040, "Back keeps it too")
	page.queue_free()
	host.queue_free()
	await frames(2)
	ms.seed = 7
	client = StubMain.new()
	root.add_child(client)
	client.state = {"controller": false, "my_client": 1, "my_slot": 1, "can_start": false, "all_ready": false,
		"slots": host_state["slots"], "observers": [], "locked": false, "version": 1, "settings_version": 1}
	page = await open_page(client)
	check(page.get_node("%SpinSeed").value == 7, "a client shows the host's setup, not a persisted one")
	page._on_back_pressed()
	check(Setup.load_persisted(true).get("seed") == 4040, "and keeps nothing")
	page.queue_free()
	client.queue_free()
	await frames(2)
	host = StubMain.new()
	root.add_child(host)
	host.state = host_state.duplicate(true)
	host.state["saved_game"] = true
	page = await open_page(host)
	check(page.get_node("%SpinSeed").value == 7, "a saved game shows the save, not the persisted settings")
	page._on_start_pressed()
	check(Setup.load_persisted(true).get("seed") == 4040, "and keeps nothing at Start")
	page.queue_free()
	host.queue_free()
	await frames(2)
	for f in [Setup.PERSIST_FILE, Setup.PERSIST_FILE_MP]:
		DirAccess.remove_absolute(ProjectSettings.globalize_path(f))
	cfg.set_value("persistmatchsettings", saved_persist)
	print("RESULT: " + ("PASS" if failures == 0 else "FAIL (%d)" % failures))
	quit(0 if failures == 0 else 1)
