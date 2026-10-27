extends SceneTree
# A corral trains gaia/fauna_*_trainable through 0.28's Training panel (selection_panels.js
# Training setupButton): hotkey and names ("Ovis aries (Sheep)"), its Ranching and Corralling
# aura, Tooltip, "Cost:" with
# food and time, the Animal limit, the batch line while Shift is held, "Requires City
# Phase" for cattle, and the needed resources under a red mask when the stock is short.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_corral_tooltips.gd
# Exit code 0 = pass.

const CORRAL := 13

const TT = preload("res://scenes/common/tooltips.gd")

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

func shift(down: bool) -> void:
	var k := InputEventKey.new()
	k.keycode = KEY_SHIFT
	k.physical_keycode = KEY_SHIFT
	k.shift_pressed = down
	k.pressed = down
	Input.parse_input_event(k)
	await frames(2)

func roster() -> ItemList:
	for l in main.find_children("*", "ItemList", true, false):
		if l.visible and l.item_count > 0:
			return l
	return null

func tips() -> Array:
	var l := roster()
	var out := []
	if l:
		for i in l.item_count:
			out.append(TT.plain(str(l.get_item_tooltip(i))))
	return out

func run() -> void:
	await frames(60)
	main.debug_train_buttons(CORRAL)
	await frames(4)
	var t: Array = tips()
	for x in t:
		print("    ", x.replace("\n", " | "))
	check(t.size() == 4, "four animals")
	var sheep: Array = t.filter(func(x): return x.contains("(Sheep)"))
	var cow: Array = t.filter(func(x): return x.contains("(Cow)"))
	check(sheep.size() == 1 and sheep[0].begins_with("[") and sheep[0].contains("OVIS ARIES (Sheep)\n") and sheep[0].contains("\nRanching and Corralling: Garrison within the Corral to gain a slow trickle of +1 food.\nKill to gather meat for quick food.\nCost: Food 50  "), "hotkey, names, aura, tooltip and cost")
	check(cow.size() == 1 and cow[0].contains("+3 food."), "cattle: the large corral aura")
	check(sheep.size() == 1 and sheep[0].contains("Current Count: ") and not sheep[0].contains("Shift-click") and not sheep[0].contains("while penned"), "the Animal limit, no batch line, no RTS0-only lines")
	check(cow.size() == 1 and cow[0].ends_with("\nRequires City Phase"), "cattle: Requires City Phase")
	await shift(true)
	main.debug_train_buttons(CORRAL)
	await frames(4)
	t = tips()
	await shift(false)
	sheep = t.filter(func(x): return x.contains("(Sheep)"))
	print("    ", sheep.map(func(x): return x.replace("\n", " | ")))
	check(sheep.size() == 1 and sheep[0].contains("Cost: Food 250 ") and sheep[0].contains("\nShift-click to train 5."), "Shift held: the cost for 5 and the batch line")
	print("RESULT: %s" % ("PASS" if failures == 0 else "FAIL (%d)" % failures))
	quit(0 if failures == 0 else 1)
