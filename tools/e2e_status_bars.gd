extends SceneTree
# 0.28's status-bar options (gui/options/options.json, Display; StatusBars.js):
#   1. gui.session.rankabovestatusbar: a selected unit with a rank (Basic citizen
#      soldiers too, not champions without one) shows its rank icon above its bars
#      (AddRankIcon); off hides it;
#   2. gui.session.experiencestatusbar: a selected unit that can still promote and has
#      some experience shows an experience bar under its health bar (AddExperienceBar;
#      none at 0 XP); off hides it.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_status_bars.gd
# Exit code 0 = pass.

var main
var view
var failures := 0
var cfg := RtsoUserConfig.new()

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

# The rank icons and experience bars the world view's status sprites drew last frame.
func rank_icons() -> PackedStringArray:
	return main.debug_status_rank_icons()

func xp_bars() -> int:
	return main.debug_status_xp_bars()

func run() -> void:
	await frames(60)
	view = main.find_children("*", "RtsoWorldView", true, false)[0]
	var keep_rank: String = cfg.get_value("gui.session.rankabovestatusbar")
	var keep_xp: String = cfg.get_value("gui.session.experiencestatusbar")
	cfg.set_value("gui.session.rankabovestatusbar", "true")
	cfg.set_value("gui.session.experiencestatusbar", "true")
	var elite: PackedInt32Array = main.debug_spawn_units("spart/champion_infantry_swordsman", 1)
	check(elite.size() == 1, "an Elite Spartan champion spawned")
	main.debug_selection_grid(true)
	await frames(10)
	var sel: Array = main.debug_selection_state()["selected"]
	var ranked := 0
	var basic_soldiers: Array = []
	var unranked := 0
	for u in main.debug_selection_state()["units"]:
		if sel.has(u["id"]):
			if u["has_rank"]:
				ranked += 1
				if int(u["rank"]) == 1 and not u["civilian"]:
					basic_soldiers.append(u["id"])
			else:
				unranked += 1
	print("  %d selected, %d ranked, %d basic soldiers, %d without a rank" % [sel.size(), ranked, basic_soldiers.size(), unranked])

	print("1. Rank icon")
	var icons := rank_icons()
	check(icons.size() == ranked and ranked >= 2, "%d rank icons for %d ranked units" % [icons.size(), ranked])
	check(unranked > 0, "the women have no rank, so no icon")
	check(Array(icons).any(func(p): return p.ends_with("Elite.png")), "the champion shows Elite")
	check(Array(icons).any(func(p): return p.ends_with("Basic.png")), "a Basic soldier shows Basic")
	cfg.set_value("gui.session.rankabovestatusbar", "false")
	await frames(3)
	check(rank_icons().is_empty(), "off: no rank icons")

	print("2. Experience bar")
	check(xp_bars() == 0 and basic_soldiers.size() > 1, "no experience bars at 0 XP")
	main.debug_add_xp(PackedInt32Array([basic_soldiers[0]]), 10.0)
	await frames(3)
	check(xp_bars() == 1, "%d experience bar once one soldier gains 10 XP" % xp_bars())
	cfg.set_value("gui.session.experiencestatusbar", "false")
	await frames(3)
	check(xp_bars() == 0, "off: no experience bars")
	main.clear_selection()
	cfg.set_value("gui.session.rankabovestatusbar", "true")
	cfg.set_value("gui.session.experiencestatusbar", "true")
	await frames(3)
	check(rank_icons().is_empty() and xp_bars() == 0, "nothing shows without a selection")

	cfg.set_value("gui.session.rankabovestatusbar", keep_rank)
	cfg.set_value("gui.session.experiencestatusbar", keep_xp)
	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
