extends SceneTree
# 0.28's Barter & Trade dialog (gui/session/trade/TradeDialog.xml, BarterPanel.xml,
# BarterButton.xml, TradePanel.xml, TraderStatusText.js):
#   1. a 432 x 388 dialog titled "Barter & Trade" with "Sell:", "Buy:" and
#      "Resources:" row labels and a Close button;
#   2. without a market the barter row reads "No Markets Available";
#   3. the instructions are the tooltips of the (i) buttons, not text on the page;
#   4. 38 px barter icons with the amount on them, 32 px trade buttons with 16 px
#      arrows beside them;
#   5. the land and ship trader texts are a blank line apart.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_trade_dialog.gd
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

func texts(n: Node) -> Array:
	return n.find_children("*", "Label", true, false).filter(func(l): return l.is_visible_in_tree()).map(func(l): return l.text)

func run() -> void:
	await frames(60)
	main._toggle_session_dialog("BarterDialog")
	await frames(6)
	var d: CanvasLayer = main.get_node("BarterDialog")
	check(d.is_open(), "the dialog opens")
	var body: Control = d.get_node("CenterContainer/Panel/Body")
	print("1. Layout")
	check(body.size == Vector2(432, 388), "432 x 388: %s" % body.size)
	var t := texts(body)
	print("    ", t)
	check(t.has("Barter & Trade") and t.has("Barter") and t.has("Trade"), "the title and the two sections")
	check(t.has("Sell:") and t.has("Buy:") and t.has("Resources:"), "the Sell:, Buy: and Resources: row labels")
	var close := body.find_child("closeTrade", true, false) as Button
	check(close != null and close.size == Vector2(128, 28), "a 128 x 28 Close button: %s" % close.size)

	print("2. No market")
	check(t.has("No Markets Available"), "No Markets Available")
	check(not (body.find_child("barterResources", true, false) as Control).visible, "no barter buttons")

	print("3. Instructions on the (i) buttons")
	var trade_help := body.find_child("tradeHelp", true, false) as Button
	check(trade_help.visible and trade_help.tooltip_text.begins_with("Select one type of goods"), "trade help tooltip: " + trade_help.tooltip_text.left(40))
	check(not t.any(func(s): return str(s).begins_with("Select one type") or str(s).begins_with("Start by selecting")), "no instruction text on the page")

	print("4. Buttons")
	var market: int = main.debug_spawn_building(6, 24.0, 0.0)
	await frames(30)
	d.update_panels()
	await frames(2)
	t = texts(body)
	check(not t.has("No Markets Available"), "with a market the notice goes (market %d)" % market)
	var barter_help := body.find_child("barterHelp", true, false) as Button
	check(barter_help.visible and barter_help.tooltip_text.begins_with("Start by selecting"), "barter help tooltip")
	var sells: Array = d._sell_buttons.values()
	check(sells.all(func(b): return b.size == Vector2(38, 38)), "38 px barter icons")
	check(sells.all(func(b): return str(b.get_node("amount").text) == "-100"), "-100 on each sell icon")
	var row: Dictionary = d._trade_rows[0]
	check(row["button"].size == Vector2(32, 32) and row["up"].size == Vector2(16, 16), "32 px trade buttons, 16 px arrows: %s %s" % [row["button"].size, row["up"].size])

	print("5. Trader texts")
	var tt: String = (body.find_child("traderCountText", true, false) as RichTextLabel).get_parsed_text()
	check(tt.contains(".\n\nThere"), "a blank line between them: " + tt.replace("\n", " | "))

	print("FAILURES: ", failures)
	quit(1 if failures else 0)
