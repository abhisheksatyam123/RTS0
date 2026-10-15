extends SceneTree
# 0.28's research progress column (gui/session ResearchProgress.js): a technology
# the viewed player researches gets a button on the right under the top panel
# with its name as tooltip and the time left as m:ss, the time runs down, a click
# selects the researcher, and the button goes when the research is done.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_research_progress.gd
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

func shown(col: Node) -> Array:
	return col.get_children().filter(func(b): return b.visible)

func run() -> void:
	await frames(60)
	var col: Node = main.find_child("ResearchProgress", true, false)
	check(col != null and shown(col).is_empty(), "the session has the empty ResearchProgress column")
	if col == null:
		quit(1)
		return
	var r: Dictionary = main.debug_research_any()
	check(not r.is_empty(), "a structure starts a technology: " + str(r))
	if r.is_empty():
		quit(1)
		return
	await frames(4)
	var b: Button = shown(col)[0] if shown(col).size() == 1 else null
	# getEntityNames: "Specific (Generic)" when the civ has a specific name.
	check(b != null and (b.tooltip_text == r["name"] or b.tooltip_text.ends_with(" (%s)" % r["name"])), "one button names it: " + (b.tooltip_text if b else "none"))
	check(b != null and not b.tooltip_text.contains("This item is paused.") and not b.get_child(3).visible, "the front item is not paused")
	if b:
		var t0: String = b.get_child(2).text
		check(t0.length() >= 4 and t0.contains(":"), "with the time left as m:ss: " + t0)
		await frames(60)
		check(b.get_child(2).text != t0, "the time runs down: %s -> %s" % [t0, b.get_child(2).text])
		b.pressed.emit()
		await frames(2)
		check(int(main.debug_selection_state()["building"]) == int(r["building"]), "a click selects the researcher")
	var waited := 0
	while not shown(col).is_empty() and waited < 3000:
		await frames(20)
		waited += 20
	check(shown(col).is_empty(), "the button goes when the research is done")
	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(0 if failures == 0 else 1)
