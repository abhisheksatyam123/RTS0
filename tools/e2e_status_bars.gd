extends SceneTree
# 0.28's status-bar options (gui/options/options.json, Display; StatusBars.js):
#   1. gui.session.rankabovestatusbar: a selected Advanced or Elite unit shows its
#      rank icon above its bars (AddRankIcon); off hides it;
#   2. gui.session.experiencestatusbar: a selected unit that can still promote shows
#      an experience bar under its health bar (AddExperienceBar); off hides it.
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

func rank_icons() -> Array:
	return view.get_children().filter(func(c): return c is Sprite3D and c.visible and c.texture != null \
			and c.texture.resource_path.contains("/ranks/"))

func xp_bars() -> int:
	var n := 0
	for c in view.get_children():
		if c is MeshInstance3D and c.visible and c.material_override is StandardMaterial3D \
				and c.material_override.albedo_color.is_equal_approx(Color(0.95, 0.75, 0.2, 0.95)):
			n += 1
	return n

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
	var basic_soldiers := 0
	for u in main.debug_selection_state()["units"]:
		if sel.has(u["id"]):
			if int(u["rank"]) >= 2:
				ranked += 1
			elif not u["civilian"]:
				basic_soldiers += 1
	print("  %d selected, %d ranked, %d basic soldiers" % [sel.size(), ranked, basic_soldiers])

	print("1. Rank icon")
	var icons := rank_icons()
	check(icons.size() == ranked and ranked >= 1, "%d rank icons for %d ranked units" % [icons.size(), ranked])
	check(icons.size() > 0 and icons[0].texture.resource_path.ends_with("Elite.png"), "the champion shows Elite")
	cfg.set_value("gui.session.rankabovestatusbar", "false")
	await frames(3)
	check(rank_icons().is_empty(), "off: no rank icons")

	print("2. Experience bar")
	check(xp_bars() >= basic_soldiers and basic_soldiers > 0, "%d experience bars, %d basic soldiers" % [xp_bars(), basic_soldiers])
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
