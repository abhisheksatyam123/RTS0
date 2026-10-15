extends SceneTree
# selection_panels.js Research setupButton: a phase whose entity requirement fails shows the
# tech's requirementsTooltip and what is left: "Requires five Village Structures.
# Remaining: N entities of class Village".
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_phase_remaining.gd
# Exit code 0 = pass.

var main
var failures := 0

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

func run() -> void:
	await frames(60)
	main.debug_select_entity(main.debug_civic_centre_id(0))
	await frames(4)
	var tips: Array = main.debug_command_row().filter(func(e): return str(e["tip"]).contains("Village Structures"))
	check(tips.size() == 1, "the Town Phase button names its requirement")
	if tips.size() == 1:
		var tip := str(tips[0]["tip"])
		print("    ", tip.replace("\n", " | "))
		var re := RegEx.create_from_string("Requires five Village Structures\\. Remaining: (\\d) entit(y|ies) of class Village")
		var m := re.search(tip)
		check(m != null, "with the count of Village structures left")
		check(m != null and int(m.get_string(1)) >= 1 and int(m.get_string(1)) <= 5, "between 1 and 5")
	print("FAILURES: ", failures)
	quit(1 if failures else 0)
