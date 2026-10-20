extends SceneTree
# 0 A.D. 0.28's game speed list and the session's player view for a player who
# stops playing (gui/session/GameSpeedControl.js, top_panel/PlayerViewControl.js,
# simulation/data/settings/game_speeds.json):
#   1. while playing: the eight titled speeds, no Fast Forward, no player view;
#   2. once defeated: Fast Forward 5x/10x/20x, and the "Choose player to view"
#      dropdown (Observer, Gaia, then each player) on Observer with "Observer Mode";
#   3. Gaia keeps Observer Mode; a player's view hides it.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_observer_view.gd
# Exit code 0 = pass.

var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func frames(n: int) -> void:
	for i in n:
		await process_frame

func _initialize() -> void:
	run.call_deferred()

const NORMAL := ["Turtle (0.1×)", "Slow (0.25×)", "Leisurely (0.5×)", "Relaxed (0.75×)", "Normal (1×)",
	"Fast (1.25×)", "Very Fast (1.5×)", "Insane (2×)"]
const FAST := ["Fast Forward (5×)", "Fast Forward (10×)", "Fast Forward (20×)"]

func run() -> void:
	var ms = root.get_node("MatchSettings")
	ms.map_id = "random_continental"
	ms.seed = 7
	ms.slot_types = [0, 1]
	ms.slot_civs = [0, 1]
	ms.replay_dir = ""
	ms.load_save_path = ""
	ms.cheats = true
	change_scene_to_file("res://scenes/main.tscn")
	await frames(60)
	var m = current_scene

	print("1. Playing")
	var st: Dictionary = m.get_replay_status()
	check(st.speed_items == NORMAL, "the eight 0.28 speeds with their titles: " + str(st.speed_items))
	check(st.speed_button, "the top panel speed button shows")
	m.set_game_speed(10.0)
	check(is_equal_approx(m.get_game_speed(), 2.0), "no Fast Forward while playing")
	m.set_game_speed(1.0)
	check(not st.observer_picker.visible, "no player view while playing")

	print("2. Defeated")
	m.resign()
	await frames(40)
	st = m.get_replay_status()
	check(st.speed_items == NORMAL + FAST, "Fast Forward once defeated: " + str(st.speed_items))
	m.set_game_speed(10.0)
	check(is_equal_approx(m.get_game_speed(), 10.0), "a defeated player can fast-forward")
	m.set_game_speed(1.0)
	var pk: Dictionary = st.observer_picker
	var row: Control = m.find_child("PlayerViewControl", true, false)
	var picker: OptionButton = row.get_child(0) if row else null
	var w: float = root.get_visible_rect().size.x
	if picker:
		var r: Rect2 = picker.get_global_rect()
		print("    picker at ", r, " of ", w)
		check(absf(r.position.x - (12.0 + (w - 24.0) * 0.85 - 282.0)) < 1.0 and r.position.y < 54.0,
				"the picker sits in the top panel at 85%-282 (PlayerViewControl.xml)")
	check(pk.visible, "the player view shows once defeated")
	check(pk.items.size() >= 4 and pk.items[0] == "Observer" and pk.items[1] == "Gaia", "Observer, Gaia, then the players: " + str(pk.items))
	check(not str(pk.items).contains("("), "players are named without their civ")
	check(int(pk.view) == -1 and pk.observer_mode, "a loss switches to Observer, with Observer Mode")

	print("3. Choosing a view")
	m._on_observer_view_selected(1)
	pk = m.get_replay_status().observer_picker
	check(pk.gaia and int(pk.view) == -1 and pk.observer_mode, "Gaia sees as the observer, Observer Mode stays")
	m._on_observer_view_selected(3)
	pk = m.get_replay_status().observer_picker
	check(not pk.gaia and int(pk.view) == 1 and not pk.observer_mode, "a player's view hides Observer Mode")
	# Cheats.executeCheat: controlsPlayer(Engine.GetPlayerID()), the own (defeated) seat, so the
	# code goes out as chat instead of cheating for the viewed player.
	check(not m.chat_try_cheat("brainiac"), "viewing a player, a defeated player's cheat code is not run for them")
	ms.cheats = false

	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
