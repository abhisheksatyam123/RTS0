extends Node
## The -autostart options that act once the match scene is up (0 A.D. 0.28
## GameSetup.cpp Autostart and AutostartClient, autostart_host.js/autostart_client.js;
## GPL-2.0-or-later): a host starts the match once -autostart-host-players humans
## are in and ready, a client readies up for each new setup, and
## -autostart-nonvisual draws no world and plays no sound, and quits when the
## match ends. It sits under MatchSettings so it outlives the loading page.

var host := false
var client := false
var host_players := 2
var nonvisual := false

var _ready_for := -1
var _started := false
var _quiet := false

func _process(_delta: float) -> void:
	var main := get_tree().current_scene
	if main == null or not main.has_method("get_lobby_state"):
		return
	if nonvisual:
		if not _quiet:
			_quiet = true
			get_tree().root.disable_3d = true
			AudioServer.set_bus_mute(0, true)
		if bool(main.get_match_summary().get("ended", false)):
			print("[Autostart] the match ended; quitting (-autostart-nonvisual)")
			print_statistics(main.get_match_statistics())
			get_tree().quit(0)
			return
	if host or client:
		var st: Dictionary = main.get_lobby_state()
		if bool(st.get("started", false)):
			_started = true
		elif main.is_net_pregame():
			var ns: Dictionary = main.get_net_setup_state()
			# autostart_client.js: ready for every settings version the host sends.
			if client and int(ns.get("my_slot", -1)) >= 0 and int(ns.get("settings_version", 0)) != _ready_for:
				_ready_for = int(ns.get("settings_version", 0))
				main.set_net_ready(1)
			# autostart_host.js: start once the expected players are in and ready.
			if host and int(st.get("players", 0)) >= host_players and bool(ns.get("can_start", false)):
				main._on_lobby_start_pressed()
	if not nonvisual and (_started or not (host or client)):
		queue_free()

## NonVisualTrigger.js: once the match is over, every player's statistics
## (StatisticsTracker GetStatisticsJSON) on stdout, one JSON line each, for tools
## that read a non-visual match's result.
static func print_statistics(summary: Dictionary) -> void:
	for line in statistics_lines(summary):
		print(line)

static func statistics_lines(summary: Dictionary) -> Array:
	var out := []
	for pd in summary.get("players", []):
		var d: Dictionary = pd
		out.append("[Autostart] statistics %s" % JSON.stringify({
			"playerID": d.get("id", -1), "name": d.get("name", ""), "civ": d.get("civ_name", ""),
			"defeated": d.get("defeated", false), "percentMapExplored": d.get("percentMapExplored", 0),
			"statistics": d.get("stats", {})}))
	return out
