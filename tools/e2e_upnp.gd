extends SceneTree
# 0 A.D. 0.28 NetServer SetupUPnP: a host maps its game port on the router.
#   1. LobbySession.map_upnp runs discovery on a thread: frames keep coming
#      while it looks, and it ends with an outcome (in this container, with
#      no router, "No IGD found");
#   2. network.upnp = false skips it.
#
#   godot --headless --path . -s tools/e2e_upnp.gd
# Exit code 0 = pass.

var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	run.call_deferred()

func run() -> void:
	var ls: Node = root.get_node("/root/LobbySession")
	var cfg := RtsoUserConfig.new()
	var saved: String = cfg.get_value("network.upnp")

	print("1. mapping on a thread")
	cfg.set_value("network.upnp", "true")
	var t0 := Time.get_ticks_msec()
	ls.map_upnp(20595)
	check(Time.get_ticks_msec() - t0 < 500, "map_upnp returns at once (%d ms)" % (Time.get_ticks_msec() - t0))
	check(ls.upnp_status == "discovering", "and reports it is discovering")
	var frames := 0
	while ls.upnp_busy() or ls.upnp_status == "discovering":
		frames += 1
		await process_frame
		if Time.get_ticks_msec() - t0 > 20000:
			break
	check(frames > 5, "frames kept coming while it looked (%d)" % frames)
	check(ls.upnp_status != "discovering" and ls.upnp_status != "", "it ended with: %s" % ls.upnp_status)

	print("2. network.upnp = false")
	cfg.set_value("network.upnp", "false")
	ls.map_upnp(20595)
	check(ls.upnp_status == "off" and not ls.upnp_busy(), "no mapping is tried")

	cfg.set_value("network.upnp", saved)
	print("RESULT: %s" % ("PASS" if failures == 0 else "FAIL (%d)" % failures))
	quit(0 if failures == 0 else 1)
