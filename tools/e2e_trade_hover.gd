extends SceneTree
# unit_actions.js "setup-trade-route" / "cancel-setup-trade-route" with GetTradingDetails:
# hovering a market with traders selected says "Right-click to set as origin trade market";
# the click is a lockstep command, so the traders themselves keep the picked first market
# (Trader.markets) and walk to it; it then offers "This is the origin trade market.
# Right-click to cancel trade route.", which drops it; the next market gives its Gain; once
# the traders trade, their own markets read "Origin trade market." and "Destination trade
# market." with the route's gain. No chat lines.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_trade_hover.gd
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

func turns(n: int) -> void:
	var until: int = int(main.get_replay_status().turn) + n
	var guard := 0
	while int(main.get_replay_status().turn) < until and guard < n * 40:
		await process_frame
		guard += 1

func run() -> void:
	await frames(60)
	var d: Dictionary = main.debug_trade_route_clicks()
	print("    ", d)
	var m1: int = d["m1"]
	var m2: int = d["m2"]
	var t: Dictionary = main.debug_trade_tip(m1)
	check(t["tip"] == "Right-click to set as origin trade market", "set first")
	main.debug_trade_click(m1, false)
	await turns(3)
	t = main.debug_trade_tip(m1)
	print("    ", t)
	check(int(t["market_a"]) == m1 and int(t["market_b"]) == 0, "the trader keeps the first market")
	check(t["cursor"] == "action-cancel-setup-trade-route", "the picked origin offers cancel")
	check(t["tip"] == "This is the origin trade market.\nRight-click to cancel trade route.", "with 0.28's words")
	main.debug_trade_click(m1, false)
	await turns(3)
	t = main.debug_trade_tip(m1)
	check(int(t["market_a"]) == 0 and t["tip"] == "Right-click to set as origin trade market", "cancel drops it")
	main.debug_trade_click(m1, false)
	await turns(3)
	t = main.debug_trade_tip(m2)
	check(str(t["tip"]).begins_with("Right-click to set as destination trade market.\nGain: "), "set second with the gain")
	main.debug_trade_click(m2, false)
	await turns(3)
	var t1: Dictionary = main.debug_trade_tip(m1)
	var t2: Dictionary = main.debug_trade_tip(m2)
	print("    ", t1, t2)
	check(int(t1["market_a"]) == m1 and int(t1["market_b"]) == m2, "the traders trade between them")
	check(str(t1["tip"]).begins_with("Origin trade market.\nGain: "), "the traders' origin market")
	check(str(t2["tip"]).begins_with("Destination trade market.\nGain: "), "the traders' destination market")
	var chat := ""
	for l in main.debug_chat_overlay():
		chat += str(l["text"])
	check(not chat.contains("Trade route set") and not chat.contains("Origin trade market set"), "no chat lines")
	print("FAILURES: ", failures)
	quit(1 if failures else 0)
