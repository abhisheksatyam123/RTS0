extends SceneTree
# A whole match played the way a person plays it, through the session's own input paths
# only (mouse clicks on the world and the panels, hotkeys), one civ against Petra:
# select the Civic Centre, train, set a rally point, find idle workers, gather, build a
# house and a barracks, train soldiers, put them in a formation, garrison and unload,
# research, tribute, phase up, build a market, barter, and resign from the menu.
# Every step that errors, does nothing, or differs from what 0.28's GUI does is a FAIL.
#
# Clicks and keys go through Input and the viewport. The tests only read state through the
# debug hooks, centre the camera with the minimap's click handler, and (in place of a
# long economy) set the stockpile before the barracks and the phase-up.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_human_path.gd -- --civ 3
# Exit code 0 = pass.

const TT = preload("res://scenes/common/tooltips.gd")

const BLD_HOUSE := 1
const BLD_BARRACKS := 2
const BLD_STOREHOUSE := 3
const BLD_MARKET := 6
const RES_WOOD := 0
const RES_FOOD := 3

var main
var failures := 0
var civ := 0

func check(cond: bool, what: String) -> bool:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1
	return cond

func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	for i in args.size() - 1:
		if args[i] == "--civ":
			civ = int(args[i + 1])
	var ms = root.get_node("MatchSettings")
	ms.map_id = "random_continental"
	ms.seed = 5
	ms.map_size = 192
	ms.slot_types = [0, 1]
	ms.slot_civs = [civ, 0 if civ != 0 else 1]
	ms.slot_teams = [1, 2]
	ms.slot_diffs = [3, 1] # Petra Very Easy: it shouldn't end the match before the script does
	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	run.call_deferred()

# ---- time --------------------------------------------------------------------------

func frames(n: int) -> void:
	for i in n:
		await process_frame

func turns(n: int) -> void:
	var until: int = int(main.get_replay_status().turn) + n
	var guard := 0
	while int(main.get_replay_status().turn) < until and guard < n * 40:
		await process_frame
		guard += 1

# Waits up to `max_turns` for `cond` to hold.
func wait_for(cond: Callable, max_turns: int) -> bool:
	for i in max_turns:
		if cond.call():
			return true
		await turns(1)
	return cond.call()

# ---- input -------------------------------------------------------------------------

# Motion goes through Input in window pixels, so the session's mouse position follows;
# buttons go to the viewport in its own pixels (the headless window is smaller than it).
func move(pos: Vector2, held := 0) -> void:
	var mm := InputEventMouseMotion.new()
	mm.position = root.get_final_transform() * pos
	mm.global_position = mm.position
	mm.button_mask = held
	Input.parse_input_event(mm)
	await frames(2)

func button_event(pos: Vector2, button: MouseButton, down: bool, double := false) -> void:
	var mb := InputEventMouseButton.new()
	mb.button_index = button
	mb.pressed = down
	mb.double_click = double and down
	mb.position = pos
	mb.global_position = pos
	mb.shift_pressed = Input.is_key_pressed(KEY_SHIFT)
	mb.ctrl_pressed = Input.is_key_pressed(KEY_CTRL)
	root.push_input(mb, true)
	await frames(1)

func click(pos: Vector2, button := MOUSE_BUTTON_LEFT) -> void:
	await move(pos)
	await button_event(pos, button, true)
	await button_event(pos, button, false)
	await frames(3)

func drag(from: Vector2, to: Vector2) -> void:
	await move(from)
	await button_event(from, MOUSE_BUTTON_LEFT, true)
	for k in range(1, 6):
		await move(from.lerp(to, k / 5.0), MOUSE_BUTTON_MASK_LEFT)
	await button_event(to, MOUSE_BUTTON_LEFT, false)
	await frames(3)

func key(code: Key, down: bool, ctrl := false, shift := false) -> void:
	var k := InputEventKey.new()
	k.keycode = code
	k.physical_keycode = code
	k.pressed = down
	k.ctrl_pressed = ctrl or (down and code == KEY_CTRL)
	k.shift_pressed = shift or (down and code == KEY_SHIFT)
	Input.parse_input_event(k)
	await frames(1)

func tap(code: Key, ctrl := false, shift := false) -> void:
	if ctrl:
		await key(KEY_CTRL, true)
	await key(code, true, ctrl, shift)
	await key(code, false, ctrl, shift)
	if ctrl:
		await key(KEY_CTRL, false)
	await frames(3)

func click_control(c: Control, button := MOUSE_BUTTON_LEFT) -> void:
	await click(c.get_global_rect().get_center(), button)

# ---- what is on screen ---------------------------------------------------------------

func state() -> Dictionary:
	return main.debug_player_state(-1)

func own(type: int, done_only := false) -> Array:
	return state()["buildings"].filter(func(b): return int(b["type"]) == type and (b["done"] or not done_only))

func look_at(world: Vector2) -> void:
	main._on_minimap_clicked(world)
	await frames(6)

func row() -> Array:
	return main.debug_command_row()

# The command, production or construction button whose label (or tooltip's first line) names one of `names`.
func row_button(names: Array) -> Dictionary:
	for b in visible_buttons(main):
		var first: String = TT.plain(b.tooltip_text).get_slice("\n", 0)
		for n in names:
			if first.ends_with("(" + n + ")") or first == n:
				return {"id": b.get_instance_id(), "text": first, "tip": b.tooltip_text, "disabled": b.disabled}
	for e in row():
		var first: String = TT.plain(str(e["tip"])).get_slice("\n", 0)
		for n in names:
			if str(e["text"]).begins_with(n) or first.begins_with(n) or first.contains("(" + n):
				return e
	return {}

func visible_buttons(root_node: Node) -> Array:
	var out := []
	for n in root_node.find_children("*", "BaseButton", true, false):
		if (n as Control).is_visible_in_tree():
			out.append(n)
	return out

# A visible button whose text or tooltip contains `text`.
func find_button(text: String) -> BaseButton:
	for b in visible_buttons(main):
		var t: String = (b as Button).text if b is Button else ""
		if t == text or t.begins_with(text) or TT.plain(b.tooltip_text).get_slice("\n", 0) == text:
			return b
	for b in visible_buttons(root):
		var t: String = (b as Button).text if b is Button else ""
		if t == text or t.begins_with(text):
			return b
	return null

func roster() -> ItemList:
	for n in main.find_children("*", "ItemList", true, false):
		if (n as ItemList).is_visible_in_tree() and (n as ItemList).item_count > 0:
			return n
	return null

# Clicks the roster entry whose tooltip names `what` (a class word such as "Female", "Spearman").
func train(what: Array, shift_clicks := 0) -> bool:
	var rl := roster()
	if rl == null:
		return false
	for i in rl.item_count:
		var tip: String = TT.plain(rl.get_item_tooltip(i))
		for w in what:
			if tip.contains(w) and not rl.is_item_disabled(i):
				var at: Vector2 = rl.get_global_rect().position + rl.get_item_rect(i).get_center()
				if shift_clicks > 0:
					await key(KEY_SHIFT, true)
					for k in shift_clicks:
						await click(at)
					await key(KEY_SHIFT, false)
				else:
					await click(at)
				return true
	return false

func queued() -> int:
	var n := 0
	for i in 16:
		var s: Dictionary = main.debug_queue_slot(i)
		if s.get("visible", false) and not s.get("ghost", false):
			n += 1
	return n

func select_building(id: int) -> bool:
	for b in state()["buildings"]:
		if int(b["id"]) == id:
			await look_at(b["pos"])
	# A unit standing in front of the structure takes the click: try a few points across it.
	var at: Vector2 = main.debug_building_screen(id)
	for off in [Vector2.ZERO, Vector2(0, -14), Vector2(14, 0), Vector2(-14, 0), Vector2(0, 14)]:
		await click(at + off)
		if int(main.debug_selection_state()["building"]) == id:
			return true
	return false

# Places the selected unit's construction `names` near `near` (world), trying a ring of spots.
func place(names: Array, type: int, near: Vector2) -> int:
	var before: Array = own(type).map(func(b): return int(b["id"]))
	var e := row_button(names)
	if e.is_empty():
		print("    no construction button for ", names, "; buttons: ", row().map(func(x): return x["text"]))
		return -1
	if e["disabled"]:
		print("    ", names[0], " is disabled: ", TT.plain(str(e["tip"])).replace("\n", " | "))
		return -1
	await look_at(near)
	for attempt in 12:
		if not main.debug_placing():
			await click_control(instance_from_id(e["id"]))
		var ang := attempt * TAU / 12.0
		var spot: Vector2 = main.get_viewport().get_visible_rect().get_center()
		spot += Vector2(cos(ang), sin(ang)) * (110 + 25 * (attempt / 4))
		await move(spot)
		await click(spot)
		await turns(2)
		var now: Array = own(type).filter(func(b): return not before.has(int(b["id"])))
		if not now.is_empty():
			var sel: Array = main.debug_selection_state()["selected"]
			print("    placed %s with %d selected: orders %s" % [names[0], sel.size(),
				main.debug_selection_state()["units"].filter(func(u): return sel.has(int(u["id"]))).map(func(u): return int(u["order_kind"]))])
			if main.debug_placing():
				await tap(KEY_ESCAPE)
			return int(now[0]["id"])
	if main.debug_placing():
		await tap(KEY_ESCAPE)
	return -1

func idle_count() -> int:
	return main.debug_selection_state()["units"].filter(func(u): return u["idle"]).size()

# Waits for structure `id` to finish. A builder that hasn't moved it off 0% in 30 s is
# reported (with where it stands), and other workers are sent to it with a right-click,
# as a player would.
func finish(id: int, what: String, max_turns: int) -> bool:
	var prog := func():
		for b in state()["buildings"]:
			if int(b["id"]) == id:
				return float(b["progress"])
		return -1.0
	var sel: Array = main.debug_selection_state()["selected"]
	if await wait_for(func(): return prog.call() >= 1.0, 150):
		return true
	if prog.call() <= 0.0 and not sel.is_empty():
		var at: Vector2 = main.debug_unit_pos(sel[0])
		# The stuck builder as it stands: its order, where it walks to, and the foundation.
		print("    builder %d at %s: order %s goal %s" % [sel[0], at, main.debug_unit_order(sel[0]), main.debug_unit_ground(sel[0], true)])
		for b in state()["buildings"]:
			if int(b["id"]) == id:
				print("    foundation %d: type %d at %s half %s" % [id, b["type"], b["pos"], b["half"]])
		for u in state()["unit_list"]:
			if int(u[0]) != sel[0] and Vector2(u[1], u[2]).distance_to(at) < 3.0:
				print("    unit %d at (%.2f, %.2f): order %s" % [u[0], u[1], u[2], main.debug_unit_order(u[0])])
		await look_at(at)
		var walk_to: Vector2 = main.get_viewport().get_visible_rect().get_center() + Vector2(0, 120)
		print("    selected %s, walking to %s" % [main.debug_selection_state()["selected"], main.debug_ground_at(walk_to)])
		await click(walk_to, MOUSE_BUTTON_RIGHT)
		await turns(2)
		print("    order %s goal %s" % [main.debug_unit_order(sel[0]), main.debug_unit_ground(sel[0], true)])
		await turns(28)
		print("    after: %s" % main.debug_unit_order(sel[0]))
		var moved: float = at.distance_to(main.debug_unit_pos(sel[0]))
		for p in [-1, 1]:
			for b in main.debug_player_state(p)["buildings"]:
				if Vector2(b["pos"]).distance_to(at) < 8.0:
					print("    nearby: type %d at %s half %s done %s" % [b["type"], b["pos"], b["half"], b["done"]])
		for u in state()["unit_list"]:
			if int(u[0]) != sel[0] and Vector2(u[1], u[2]).distance_to(at) < 1.5:
				print("    unit %d at (%.2f, %.2f) state %d" % u)
		check(false, "the %s's builder works on it (unit %d at %s never started it; a walk order moves it %.1f)" % [what, sel[0], at, moved])
		await tap(KEY_PERIOD)
		for b in state()["buildings"]:
			if int(b["id"]) == id:
				await look_at(b["pos"])
		await click(main.debug_building_screen(id), MOUSE_BUTTON_RIGHT)
	return await wait_for(func(): return prog.call() >= 1.0, max_turns)

# ---- the match -----------------------------------------------------------------------

func run() -> void:
	await frames(60)
	await turns(4)
	print("civ %d against Petra" % civ)
	var st := state()
	var ccs: Array = st["buildings"]
	check(ccs.size() >= 1 and int(st["units"]) > 0, "a Civic Centre and starting units (%d structures, %d units)" % [ccs.size(), int(st["units"])])
	var cc: int = main.debug_civic_centre_id(0)
	var cc_pos: Vector2 = Vector2()
	for b in ccs:
		if int(b["id"]) == cc:
			cc_pos = b["pos"]

	print("1. Click the Civic Centre and train a batch of women")
	check(await select_building(cc), "a click on the Civic Centre selects it")
	var q0 := queued()
	var trained := await train(["Civilian"], 0)
	await frames(10)
	check(trained and queued() > q0, "a click on the roster queues one (queue %d -> %d)" % [q0, queued()])
	var q1 := queued()
	trained = await train(["Civilian"], 1)
	await frames(10)
	check(trained and queued() > q1, "Shift+click, then releasing Shift, queues a batch (queue %d -> %d)" % [q1, queued()])

	print("2. Right-click the ground for a rally point")
	var ground: Vector2 = main.get_viewport().get_visible_rect().get_center() + Vector2(-140, 60)
	await click(ground, MOUSE_BUTTON_RIGHT)
	await frames(4)
	check(main.debug_selection_state().has("rally"), "the Civic Centre has a rally point")

	print("3. The idle-worker hotkey, then gather")
	await tap(KEY_PERIOD)
	var sel: Array = main.debug_selection_state()["selected"]
	check(sel.size() >= 1, "Period selects an idle worker (%d selected)" % sel.size())
	var ss: Dictionary = main.debug_selection_state()
	if ss.has("nearest_resource_screen") and sel.size() >= 1:
		var rid: int = ss["nearest_resource"]
		var u0: int = sel[0]
		await look_at(main.debug_ground_at(ss["nearest_resource_screen"]))
		await click(main.debug_resource_screen(rid), MOUSE_BUTTON_RIGHT)
		await turns(4)
		var u: Dictionary = {}
		for x in main.debug_selection_state()["units"]:
			if int(x["id"]) == u0:
				u = x
		check(not u.is_empty() and (int(u["target_resource"]) != 0 or not u["idle"]), "a right-click on a resource sets it to gather")
	else:
		check(false, "a resource near the idle worker to click")

	print("4. Box-select the women and build a house")
	await look_at(cc_pos)
	await click(main.get_viewport().get_visible_rect().get_center() + Vector2(0, 260)) # deselect on empty ground
	var civilians: Array = main.debug_selection_state()["units"].filter(func(x): return x["civilian"])
	if civilians.size() >= 2:
		var pts: Array = civilians.slice(0, 3).map(func(x): return main.debug_unit_screen(x["id"]))
		var lo: Vector2 = pts[0]
		var hi: Vector2 = pts[0]
		for p in pts:
			lo = Vector2(min(lo.x, p.x), min(lo.y, p.y))
			hi = Vector2(max(hi.x, p.x), max(hi.y, p.y))
		await drag(lo - Vector2(30, 30), hi + Vector2(30, 30))
	sel = main.debug_selection_state()["selected"]
	check(sel.size() >= 1, "a drag box selects units (%d)" % sel.size())
	var house := await place(["House"], BLD_HOUSE, cc_pos)
	check(house > 0, "a click with House armed places a house foundation")
	if house > 0:
		check(await finish(house, "house", 400), "the builders finish it")

	print("5. A barracks, then soldiers from it")
	# In place of the economy a match would build up by now.
	for r in 4:
		main.debug_set_resource(r, 2000)
	await turns(2)
	await tap(KEY_PERIOD)
	var barracks := await place(["Barracks"], BLD_BARRACKS, cc_pos)
	check(barracks > 0, "a barracks foundation")
	if barracks > 0:
		check(await finish(barracks, "barracks", 900), "the barracks is finished")
	# Room for the batch: civs with small houses (5 population) need more of them first.
	for i in 4:
		if int(state()["pop_max"]) - int(state()["pop"]) >= 10:
			break
		await tap(KEY_PERIOD)
		var h := await place(["House"], BLD_HOUSE, cc_pos)
		if h > 0:
			await finish(h, "house", 400)
	var units_before: int = int(state()["units"])
	var soldiers_ok := false
	if barracks > 0 and await select_building(barracks):
		soldiers_ok = await train(["Infantry"], 2)
	check(soldiers_ok, "the barracks' roster trains a batch of infantry")
	check(await wait_for(func(): return int(state()["units"]) >= units_before + 10, 900), "and they come out (%d -> %d units)" % [units_before, int(state()["units"])])

	print("6. Select the soldiers, put them in a box formation")
	var all_soldiers: Array = main.debug_selection_state()["units"].filter(func(x): return not x["civilian"])
	if not all_soldiers.is_empty():
		await look_at(all_soldiers.back()["pos"])
	var want_tmpl: int = int(all_soldiers.back()["tmpl"]) if not all_soldiers.is_empty() else -1
	var soldiers: Array = main.debug_selection_state()["units"].filter(func(x): return not x["civilian"] and not x["behind"] and int(x["tmpl"]) == want_tmpl)
	soldiers.sort_custom(func(a, b): return a["screen"].y > b["screen"].y) # nearest the camera: nothing in front
	var picked := false
	# A structure in front (Iberian start walls) takes the click: try the next soldier.
	var first := -1
	for s in soldiers.slice(0, 6):
		await click(main.debug_unit_screen(s["id"]))
		await frames(4)
		if main.debug_selection_state()["selected"].has(int(s["id"])):
			first = int(s["id"])
			break
	if soldiers.size() >= 2 and first >= 0:
		# 0.28: a double-click selects every unit of that type on screen.
		var p: Vector2 = main.debug_unit_screen(first)
		await move(p)
		await button_event(p, MOUSE_BUTTON_LEFT, true, true)
		await button_event(p, MOUSE_BUTTON_LEFT, false)
		await frames(4)
		picked = main.debug_selection_state()["selected"].size() >= 4
	check(picked, "a double-click selects the soldiers (%d)" % main.debug_selection_state()["selected"].size())
	var box := find_button("Box")
	if not check(box != null and not box.disabled, "the Box formation button"):
		print("    buttons: ", visible_buttons(main).filter(func(b): return b.get_global_rect().position.y > 800).map(func(b): return TT.plain(b.tooltip_text).get_slice("\n", 0)))
	if box:
		await click_control(box)
		await turns(3)
		var formed: Array = main.debug_selection_state()["units"].filter(func(x): return main.debug_selection_state()["selected"].has(int(x["id"])) and int(x["formation"]) > 0)
		check(formed.size() >= 2, "a click forms them into a box (%d in formation)" % formed.size())

	print("7. Garrison them in the Civic Centre with Ctrl+right-click, then unload with U")
	var g0: int = 0
	await look_at(cc_pos)
	await key(KEY_CTRL, true)
	await click(main.debug_building_screen(cc), MOUSE_BUTTON_RIGHT)
	await key(KEY_CTRL, false)
	var garrisoned := await wait_for(func():
		for b in state()["buildings"]:
			if int(b["id"]) == cc:
				return int(b["garrisoned"]) > g0
		return false, 300)
	check(garrisoned, "Ctrl+right-click on the Civic Centre garrisons them")
	# Let the stragglers walk in before unloading.
	var inside := -1
	for i in 40:
		var n := 0
		for b in state()["buildings"]:
			if int(b["id"]) == cc:
				n = int(b["garrisoned"])
		if n == inside:
			break
		inside = n
		await turns(5)
	if await select_building(cc):
		# Who is inside when U goes down: a straggler that walks in afterwards stays, as in 0.28.
		var garrisoned_ids := func():
			for b in state()["buildings"]:
				if int(b["id"]) == cc:
					return Array(b["inside"])
			return []
		var before: Array = garrisoned_ids.call()
		await tap(KEY_U)
		await turns(4)
		var after: Array = garrisoned_ids.call()
		var left: Array = before.filter(func(id): return after.has(id))
		if after.size() > left.size():
			print("    %d walked in after U" % (after.size() - left.size()))
		check(not before.is_empty() and left.is_empty(), "U with the Civic Centre selected unloads everyone inside (%d of %d left)" % [left.size(), before.size()])

	print("8. Research a technology at a storehouse")
	await tap(KEY_PERIOD)
	var sh := await place(["Storehouse"], BLD_STOREHOUSE, cc_pos)
	check(sh > 0, "a storehouse foundation")
	var sh_done := sh > 0 and await finish(sh, "storehouse", 900)
	if sh > 0 and check(sh_done, "the storehouse is finished"):
		if await select_building(sh):
			var techs: Array = row().filter(func(e): return not e["disabled"] and TT.plain(str(e["tip"])).contains("Cost:"))
			check(not techs.is_empty(), "the storehouse offers a technology")
			if not techs.is_empty():
				var name: String = techs[0]["text"]
				var qr := queued()
				await click_control(instance_from_id(techs[0]["id"]))
				await frames(6)
				check(queued() > qr, "a click starts %s (queue %d -> %d)" % [name, qr, queued()])

	print("9. Tribute 100 food to Petra from the diplomacy page (Ctrl+H)")
	await tap(KEY_H, true)
	await frames(20)
	var dlg: Dictionary = main.debug_dialogs()
	check(str(dlg).contains("iplomacy"), "Ctrl+H opens the diplomacy page")
	var food0: float = state()["res"][RES_FOOD]
	var trib: BaseButton = null
	for b in visible_buttons(main):
		var t := TT.plain(b.tooltip_text)
		if t.contains("Tribute") and t.contains("ood") and not b.disabled:
			trib = b
			break
	check(trib != null, "a food tribute button")
	if trib:
		await click_control(trib)
		await turns(3)
		check(float(state()["res"][RES_FOOD]) < food0, "a click sends food (%d -> %d)" % [int(food0), int(state()["res"][RES_FOOD])])
	await tap(KEY_H, true)

	print("10. Phase up at the Civic Centre")
	for r in 4:
		main.debug_set_resource(r, 5000)
	# 0.28's town phase needs village structures; houses are cheap ones.
	await tap(KEY_PERIOD)
	for i in 6:
		if not (await select_building(cc)):
			break
		var need := row_button(["Town Phase"])
		var m := RegEx.create_from_string("Remaining: (\\d+)").search(TT.plain(str(need.get("tip", ""))))
		if m == null:
			break
		await tap(KEY_PERIOD)
		var h := await place(["House"], BLD_HOUSE, cc_pos)
		if h > 0:
			await finish(h, "house", 400)
	var phased := false
	if await select_building(cc):
		var ph := row_button(["Town Phase"])
		check(not ph.is_empty(), "the Civic Centre offers Town Phase")
		if not ph.is_empty():
			if ph["disabled"]:
				print("    disabled: ", TT.plain(str(ph["tip"])).replace("\n", " | "))
			await click_control(instance_from_id(ph["id"]))
			phased = await wait_for(func(): return int(state()["phase"]) >= 2, 1600)
	check(phased, "a click researches it and the phase changes (phase %d)" % int(state()["phase"]))

	print("11. A market, then barter (Ctrl+B)")
	await tap(KEY_PERIOD)
	var market := await place(["Market"], BLD_MARKET, cc_pos)
	check(market > 0, "a market foundation")
	if market > 0 and check(await finish(market, "market", 1500), "the market is finished"):
		await tap(KEY_B, true)
		await frames(20)
		var wood0: float = state()["res"][RES_WOOD]
		var food1: float = state()["res"][RES_FOOD]
		var dlg2: Node = main.get_node_or_null("BarterDialog")
		check(dlg2 != null and dlg2.is_open(), "Ctrl+B opens the Barter & Trade page")
		if dlg2 and dlg2.is_open():
			# 0.28 BarterPanel: click the resource to sell, then the one to buy.
			var sell: Button = dlg2._sell_buttons.get(RES_FOOD)
			var buy: Button = dlg2._buy_buttons.get(RES_WOOD)
			check(sell != null and buy != null and sell.is_visible_in_tree() and buy.is_visible_in_tree(), "sell food and buy wood buttons")
			if sell and buy:
				await click_control(sell)
				await click_control(buy)
				await turns(2)
				var r: Array = state()["res"]
				check(float(r[RES_FOOD]) < food1 and float(r[RES_WOOD]) > wood0, "selling food for wood barters (food %d -> %d, wood %d -> %d)" % [int(food1), int(r[RES_FOOD]), int(wood0), int(r[RES_WOOD])])
		await tap(KEY_B, true)

	print("12. Resign from the menu (F10)")
	await tap(KEY_F10)
	await frames(20)
	var resign := find_button("Resign")
	check(resign != null and not resign.disabled, "the menu offers Resign")
	if resign:
		await click_control(resign)
		await frames(20)
		var yes := find_button("Yes")
		check(yes != null, "and asks to confirm")
		if yes:
			await click_control(yes)
			await turns(4)
	check(bool(state()["defeated"]), "the player is defeated")
	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
