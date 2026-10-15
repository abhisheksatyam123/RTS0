extends SceneTree
# unit_actions.js "setup-trade-route" / "cancel-setup-trade-route" with GetTradingDetails:
# hovering a market with traders selected says "Right-click to set as origin trade market";
# the picked origin offers "This is the origin trade market. Right-click to cancel trade
# route."; the next market gives its Gain; once the traders trade, their own markets read
# "Origin trade market." and "Destination trade market." with the route's gain. No chat lines.
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

func run() -> void:
	await frames(60)
	var d: Dictionary = main.debug_trade_route_clicks()
	print("    ", d)
	check(d["hover_first"] == "Right-click to set as origin trade market", "set first")
	check(d["origin_cursor"] == "action-cancel-setup-trade-route", "the picked origin offers cancel")
	check(d["hover_origin"] == "This is the origin trade market.\nRight-click to cancel trade route.", "with 0.28's words")
	check(str(d["hover_second"]).begins_with("Right-click to set as destination trade market.\nGain: "), "set second with the gain")
	await frames(20)
	var t1: Dictionary = main.debug_trade_tip(d["m1"])
	var t2: Dictionary = main.debug_trade_tip(d["m2"])
	print("    ", t1, t2)
	check(str(t1["tip"]).begins_with("Origin trade market.\nGain: "), "the traders' origin market")
	check(str(t2["tip"]).begins_with("Destination trade market.\nGain: "), "the traders' destination market")
	var chat := ""
	for l in main.debug_chat_overlay():
		chat += str(l["text"])
	check(not chat.contains("Trade route set") and not chat.contains("Origin trade market set"), "no chat lines")
	print("FAILURES: ", failures)
	quit(1 if failures else 0)
