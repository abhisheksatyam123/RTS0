extends SceneTree
# 0.28's Barter panel on a selected market (selection_panels.js g_SelectionPanels.Barter,
# trade/BarterButtonManager.js, BarterButton.js):
#   1. a sell button per resource ("-100", "Sell <resource>"), food picked first (grey with
#      the corners, no buy button under it), and a buy button under the others with the
#      quote ("+<amount>", "Buy <resource>");
#   2. a click on a buy button sells 100 of the picked resource for it;
#   3. a click on another sell button picks that resource;
#   4. a resource the stock can't spare 100 of is red.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_barter_panel.gd
# Exit code 0 = pass.

const BLD_MARKET := 6

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

func col(p: Array, name: String) -> Dictionary:
	for c in p:
		if c["name"] == name:
			return c
	return {}

func stock() -> Dictionary:
	return main.get_trade_dialog_state()

func run() -> void:
	await frames(60)
	var market: int = main.debug_spawn_building(BLD_MARKET, -30.0, 0.0)
	check(market != 0, "a market of ours")
	main.debug_select_entity(market)
	await frames(4)
	var p: Array = main.debug_barter_panel()
	print("    ", p.map(func(c): return "%s %s/%s" % [c["name"], c["sell"]["caption"], c.get("buy", {}).get("caption", "-")]))
	check(p.size() == 4 and p.map(func(c): return c["name"]) == ["food", "wood", "stone", "metal"], "food, wood, stone, metal")
	var food := col(p, "food")
	var wood := col(p, "wood")
	check(not food.is_empty() and food["sell"]["selected"] and not food.has("buy") and food["sell"]["caption"] == "-100" and food["sell"]["tip"] == "Sell food", "food picked: -100, no buy button")
	check(not wood.is_empty() and wood.has("buy") and str(wood["buy"]["caption"]).begins_with("+") and wood["buy"]["tip"] == "Buy wood", "wood's buy button: " + str(wood.get("buy", {}).get("caption")))

	print("2. Buy wood")
	var before: PackedInt32Array = stock()["stock"]
	(instance_from_id(wood["buy"]["id"]) as Button).pressed.emit()
	await frames(30)
	var after: PackedInt32Array = stock()["stock"]
	# RTS0 ResourceType: 0 wood, 1 metal, 2 stone, 3 food.
	check(after[3] == before[3] - 100 and after[0] == before[0] + int(str(wood["buy"]["caption"]).trim_prefix("+")), "100 food for the quoted wood: %s -> %s" % [before, after])

	print("3. Pick stone")
	(instance_from_id(col(main.debug_barter_panel(), "stone")["sell"]["id"]) as Button).pressed.emit()
	await frames(4)
	p = main.debug_barter_panel()
	check(col(p, "stone")["sell"]["selected"] and not col(p, "stone").has("buy") and col(p, "food").has("buy"), "stone picked, food can be bought")

	print("4. Short of stone")
	for i in 20:
		var st: PackedInt32Array = stock()["stock"]
		if st[2] < 100:
			break
		main._on_barter_requested(2, 3, 100)
		await frames(4)
	p = main.debug_barter_panel()
	check(col(p, "stone")["sell"]["red"] and col(p, "food")["buy"]["red"] and not col(p, "food")["sell"]["red"], "stone's sell and every buy are red")
	print("RESULT: %s" % ("PASS" if failures == 0 else "FAIL (%d)" % failures))
	quit(0 if failures == 0 else 1)
