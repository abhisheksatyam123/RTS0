extends SceneTree
# Han class models load their own 0.28 actors (pass-3 audit): the woman
# (units/han/female_citizen.xml), the cavalry (the Academy champion's
# cavalry_spearman_c_m) and the fishing boat (structures/han/fishing_ship.xml),
# not the Athenian stand-ins.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_han_models.gd
# Exit code 0 = pass.

var failures := 0

func check(cond: bool, what: String) -> void:
	if cond:
		print("  ok   ", what)
	else:
		print("  FAIL ", what)
		failures += 1

func _initialize() -> void:
	run.call_deferred()

func run() -> void:
	var ms = root.get_node("MatchSettings")
	ms.map_id = "skirmish_demo"
	ms.seed = 1234
	ms.slot_types = [0, 1]
	ms.slot_civs = [13, 0]
	ms.slot_teams = [0, 0]
	ms.replay_dir = ""
	ms.load_save_path = ""
	change_scene_to_file("res://scenes/main.tscn")
	for i in 5:
		await process_frame
	var m = current_scene
	if m == null or not m.has_method("debug_unit_model_path"):
		check(false, "the match scene starts")
	else:
		# Civ 13 Han; unit types 17 female citizen, 4 cavalry, 15 fishing boat.
		var woman: String = m.debug_unit_model_path(13, 17)
		var cav: String = m.debug_unit_model_path(13, 4)
		var boat: String = m.debug_unit_model_path(13, 15)
		check(woman.ends_with("units/han_villager_female.glb"), "Han woman: %s" % woman)
		check(cav.ends_with("units/han_cavalry_spearman.glb"), "Han cavalry: %s" % cav)
		check(boat.ends_with("buildings/han_fishing_ship.glb"), "Han fishing boat: %s" % boat)
		var athen: String = m.debug_unit_model_path(0, 17)
		check(athen != "" and athen != woman, "Athenian woman keeps her own model: %s" % athen)
	change_scene_to_file("res://scenes/menu/main_menu.tscn")
	for i in 3:
		await process_frame
	print("e2e_han_models: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
