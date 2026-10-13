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
#   3. outside a networked game the page is the single-player one.
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
	func _on_lobby_start_pressed() -> void:
		started += 1

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

func picker(page, slot: int, col: int):
	return cells(page)[slot * 7 + col].get_child(0)

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

	print("1. the host")
	var host := StubMain.new()
	root.add_child(host)
	host.state = {"controller": true, "my_client": 0, "my_slot": 0, "can_start": false, "all_ready": false,
		"slots": [{"client": 0, "name": "alice", "ready": 2}, {"client": 1, "name": "bob", "ready": 0}],
		"observers": [], "locked": false, "version": 1, "settings_version": 1}
	var page := await open_page(host)
	check(page._mp_role == "host", "the controller hosts the page")
	var start: Button = page.get_node("VBox/HBox/Right/BtnStart")
	check(start.visible and start.disabled and start.tooltip_text == page.START_WAIT_TOOLTIP,
		"Start waits for every player to be ready")
	check(not page.get_node("VBox/HBox/Right/BtnReady").visible, "the host has no Ready button")
	check(ms.slot_types == [0, 0] and ms.slot_civs == [1, -1] and ms.player_count == 2,
		"the setup drops the Closed slot and keeps Random (%s %s)" % [str(ms.slot_types), str(ms.slot_civs)])
	check(host.pushes > 0 and not ms.cheats, "the host sends its setup, cheats off")
	check(cells(page)[0].text == "alice (You)" and cells(page)[7].text == "bob", "slots show their players")
	page.get_node("VBox/HBox/LeftScroll/Left/SeedRow/SpinSeed").value = 99
	await create_timer(0.4).timeout
	check(ms.seed == 99, "a change on the page reaches MatchSettings")
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
	check(cells(page)[7].text == "bob (Ready)", "bob is ready")
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
	page = await open_page(client)
	check(page._mp_role == "client", "a client's page")
	start = page.get_node("VBox/HBox/Right/BtnStart")
	var ready_btn: Button = page.get_node("VBox/HBox/Right/BtnReady")
	check(not start.visible and page.get_node("VBox/HBox/Right/NetWaitLabel").visible, "no Start: waiting for the host")
	check(page.get_node("VBox/HBox/LeftScroll/Left/SeedRow/SpinSeed").value == 1234 and page._slot_rows[1]["civ"] == 3,
		"the host's setup shows")
	check(not page.get_node("VBox/HBox/LeftScroll/Left/SeedRow/SpinSeed").editable
		and page.get_node("VBox/HBox/LeftScroll/Left/MapList").mouse_filter == Control.MOUSE_FILTER_IGNORE
		and page.get_node("VBox/HBox/LeftScroll/Left/Waves").disabled, "the host's settings are locked")
	check(picker(page, 0, 2).disabled and picker(page, 0, 3).disabled and picker(page, 1, 1).disabled and picker(page, 1, 4).disabled,
		"other slots, and our slot's type and AI, are locked")
	check(not picker(page, 1, 2).disabled and not picker(page, 1, 3).disabled and not picker(page, 1, 6).disabled,
		"our own civ, team and colour are not")
	page._on_slot_civ(0, 1)
	page._on_slot_team(3, 1)
	page._on_slot_color(6, 1)
	check(client.requests == [["civ", int(page.CIVS[0]["id"])], ["team", 3], ["color", 6]], "picks are asked of the host: " + str(client.requests))
	check(ready_btn.text == "I'm ready" and not ready_btn.disabled, "Ready: I'm ready")
	ready_btn.pressed.emit()
	check(client.is_ready == 1 and ready_btn.text == "Stay ready", "pressed: ready, then Stay ready")
	ready_btn.pressed.emit()
	check(client.is_ready == 2 and ready_btn.text == "I'm not ready!", "pressed again: stays ready, then I'm not ready!")
	ready_btn.pressed.emit()
	check(client.is_ready == 0 and ready_btn.text == "I'm ready", "pressed again: not ready")
	ready_btn.pressed.emit()
	ms.seed = 555
	client.is_ready = 0 # RtsoMain takes a Ready (not a Stay ready) back when a setting changes
	client.net_setup_changed.emit()
	await frames(3)
	check(page.get_node("VBox/HBox/LeftScroll/Left/SeedRow/SpinSeed").value == 555 and ready_btn.text == "I'm ready",
		"a new setup shows, and Ready is back")
	check(not picker(page, 1, 2).disabled and picker(page, 0, 2).disabled, "the locks hold after the update")
	client.net_setup_refused.emit("That colour is taken.")
	check(page.get_node("VBox/HBox/Right/ErrorLabel").text == "That colour is taken.", "a refused request says why")
	page._on_back_pressed()
	check(client.left == 1, "Back leaves the networked game")
	client.state["locked"] = true
	client.net_setup_changed.emit()
	await frames(3)
	check(picker(page, 1, 2).disabled, "a map that sets the players locks ours too")
	page.queue_free()
	client.queue_free()
	await frames(2)

	print("3. single player")
	page = await open_page(null)
	check(page._mp_role == "none" and page.get_node("VBox/HBox/Right/BtnStart").visible
		and not page.has_node("VBox/HBox/Right/BtnReady") and not page.get_node("VBox/HBox/LeftScroll/Left/Waves").disabled,
		"the single-player page")
	page.queue_free()
	cfg.set_value("persistmatchsettings", saved_persist)
	print("RESULT: " + ("PASS" if failures == 0 else "FAIL (%d)" % failures))
	quit(0 if failures == 0 else 1)
