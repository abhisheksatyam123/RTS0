extends CanvasLayer

# Port of 0 A.D. gui/session/trade (TradeDialog.js, TradeDialogBarterPanel.js,
# BarterButtonManager.js, BarterButton.js, TradeDialogTradePanel.js,
# TradeButtonManager.js, TradeButton.js, TraderStatusText*.js). Ctrl+B
# (session.gui.barter.toggle) opens it. The barter panel sells 100 of the
# resource picked in the upper row for the quoted amount of the one clicked
# in the lower row (500 while session.massbarter is held); the trade panel
# sets the shares of goods traders carry home, 5% per arrow click
# (session.fulltradeswap puts the clicked good to 100%).

signal barter_requested(sell_type: int, buy_type: int, amount: int)
signal trading_goods_requested(shares: PackedInt32Array)

# 0 A.D. order: food, wood, stone, metal. Values are RTS0 ResourceType.
const RES_ORDER := [3, 0, 2, 1]
const RES_NAMES := {0: "wood", 1: "metal", 2: "stone", 3: "food"}
const AMOUNT_STEP := 5
const SELL_TOOLTIP := "Sell %s"
const BUY_TOOLTIP := "Buy %s"
const BARTER_HELP := "Start by selecting the resource you wish to sell from the upper row. For each time the lower buttons are pressed, %d of the upper resource will be sold for the displayed quantity of the lower. Press and hold %s to temporarily multiply the traded amount by %d."
const TRADE_HELP := "Select one type of goods you want to modify by clicking on it, and then use the arrows of the other types to modify their shares. You can also press %s while selecting one type of goods to bring its share to 100%%."
const IDLE_COLOR := "orange"

@onready var vbox: VBoxContainer = $CenterContainer/Panel/VBox

var selected_sell := 3
var selected_good := 3
var trading_goods := PackedInt32Array([25, 25, 25, 25])
var _goods_from_sim := true
var _sell_buttons := {}
var _buy_buttons := {}
var _trade_rows := {}
var _barter_box: Control
var _no_market: Label
var _barter_help: Label
var _trade_help: Label
var _trader_text: RichTextLabel
var _icons := {}
var _hotkeys = null
var _refresh := 0.0

func _ready() -> void:
	visible = false
	if ClassDB.class_exists("RtsoHotkeyConfig"):
		_hotkeys = ClassDB.instantiate("RtsoHotkeyConfig")
	for r in RES_ORDER:
		var path := "res://assets/textures/ui/icons/%s.png" % RES_NAMES[r]
		if ResourceLoader.exists(path):
			_icons[r] = load(path)
	_build()

func _label(text: String, size := 13, color := Color(0.85, 0.88, 0.92)) -> Label:
	var l := Label.new()
	l.text = text
	l.add_theme_font_size_override("font_size", size)
	l.add_theme_color_override("font_color", color)
	return l

func _caption(text: String) -> Label:
	var l := _label(text, 15, Color(0.9, 0.75, 0.31))
	l.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	return l

func _help_label() -> Label:
	var l := _label("", 11, Color(0.75, 0.78, 0.82))
	l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	l.custom_minimum_size = Vector2(480, 0)
	return l

func _res_button(r: int) -> Button:
	var b := Button.new()
	b.custom_minimum_size = Vector2(96, 56)
	b.icon = _icons.get(r)
	b.expand_icon = true
	b.add_theme_font_size_override("font_size", 14)
	return b

func _build() -> void:
	vbox.add_child(_caption("Barter"))
	_no_market = _label("There are no markets for bartering.", 13, Color(1, 0.6, 0.3))
	_no_market.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	vbox.add_child(_no_market)
	var grid := GridContainer.new()
	grid.columns = RES_ORDER.size()
	grid.add_theme_constant_override("h_separation", 8)
	grid.add_theme_constant_override("v_separation", 6)
	for r in RES_ORDER:
		var sell := _res_button(r)
		sell.toggle_mode = true
		sell.tooltip_text = SELL_TOOLTIP % RES_NAMES[r]
		sell.pressed.connect(_on_sell_selected.bind(r))
		grid.add_child(sell)
		_sell_buttons[r] = sell
	for r in RES_ORDER:
		var buy := _res_button(r)
		buy.tooltip_text = BUY_TOOLTIP % RES_NAMES[r]
		buy.pressed.connect(_on_buy_pressed.bind(r))
		grid.add_child(buy)
		_buy_buttons[r] = buy
	var center := CenterContainer.new()
	center.add_child(grid)
	_barter_box = center
	vbox.add_child(center)
	_barter_help = _help_label()
	vbox.add_child(_barter_help)

	vbox.add_child(HSeparator.new())
	vbox.add_child(_caption("Trade"))
	var row := HBoxContainer.new()
	row.alignment = BoxContainer.ALIGNMENT_CENTER
	row.add_theme_constant_override("separation", 8)
	for r in RES_ORDER:
		var col := VBoxContainer.new()
		col.add_theme_constant_override("separation", 2)
		var up := Button.new()
		up.text = "▲"
		up.custom_minimum_size = Vector2(96, 20)
		up.pressed.connect(_on_arrow.bind(r, 1))
		var good := _res_button(r)
		good.toggle_mode = true
		good.pressed.connect(_on_good_selected.bind(r))
		var dn := Button.new()
		dn.text = "▼"
		dn.custom_minimum_size = Vector2(96, 20)
		dn.pressed.connect(_on_arrow.bind(r, -1))
		col.add_child(up)
		col.add_child(good)
		col.add_child(dn)
		row.add_child(col)
		_trade_rows[r] = {"up": up, "button": good, "dn": dn}
	vbox.add_child(row)
	_trade_help = _help_label()
	vbox.add_child(_trade_help)
	_trader_text = RichTextLabel.new()
	_trader_text.bbcode_enabled = true
	_trader_text.fit_content = true
	_trader_text.scroll_active = false
	_trader_text.custom_minimum_size = Vector2(480, 0)
	_trader_text.add_theme_font_size_override("normal_font_size", 12)
	vbox.add_child(_trader_text)

	var close := Button.new()
	close.text = "Close"
	close.custom_minimum_size = Vector2(120, 32)
	close.size_flags_horizontal = Control.SIZE_SHRINK_CENTER
	close.pressed.connect(_on_close_pressed)
	vbox.add_child(close)

func toggle() -> void:
	visible = not visible
	if visible:
		_goods_from_sim = true
		update_panels()

func is_open() -> bool:
	return visible

func _on_close_pressed() -> void:
	visible = false

func _process(delta: float) -> void:
	if not visible:
		return
	# TradeDialog.updateIfOpen runs on every simulation update; the massbarter
	# modifier changes the quotes while held, so refresh a few times a second.
	_refresh -= delta
	if _refresh <= 0.0:
		_refresh = 0.1
		update_panels()

func _state() -> Dictionary:
	var m := get_parent()
	if m and m.has_method("get_trade_dialog_state"):
		return m.call("get_trade_dialog_state")
	return {}

func update_panels() -> void:
	var st := _state()
	if st.is_empty():
		return
	_update_barter(st)
	_update_trade(st)

func _update_barter(st: Dictionary) -> void:
	var can_barter: bool = st.get("can_barter", false)
	var controls: bool = st.get("controls", true)
	var amount: int = st.get("amount", 100)
	var stock: PackedInt32Array = st.get("stock", PackedInt32Array([0, 0, 0, 0]))
	var quotes: PackedInt32Array = st.get("quotes", PackedInt32Array())
	_no_market.visible = not can_barter
	_barter_box.visible = can_barter
	_barter_help.visible = can_barter
	_barter_help.text = BARTER_HELP % [100, _hotkey_text("session.massbarter"), int(st.get("multiplier", 5))]
	for r in RES_ORDER:
		var selected: bool = r == selected_sell
		var sell: Button = _sell_buttons[r]
		sell.button_pressed = selected
		sell.text = "-%d" % amount
		# BarterButton DisabledModifier: red when the stock can't cover the deal.
		sell.modulate = Color(1, 0.45, 0.45) if stock[r] < amount else (Color(0.6, 0.6, 0.6) if selected else Color.WHITE)
		var buy: Button = _buy_buttons[r]
		buy.visible = not selected
		buy.disabled = not controls
		var q := 0
		if quotes.size() == 16:
			q = quotes[selected_sell * 4 + r]
		buy.text = "+%d" % q
		buy.modulate = Color(1, 0.45, 0.45) if stock[selected_sell] < amount else Color.WHITE

func _update_trade(st: Dictionary) -> void:
	var controls: bool = st.get("controls", true)
	# TradeButtonManager keeps its own copy for players (the sim follows it) and
	# re-reads it for observers.
	if _goods_from_sim or not controls:
		trading_goods = st.get("trading_goods", trading_goods)
		_goods_from_sim = false
	_trade_help.text = TRADE_HELP % _hotkey_text("session.fulltradeswap")
	var selected_amount := trading_goods[selected_good]
	for r in RES_ORDER:
		var row: Dictionary = _trade_rows[r]
		var is_sel: bool = r == selected_good
		var current := trading_goods[r]
		var b: Button = row["button"]
		b.text = "%d%%" % current
		b.disabled = not controls
		b.button_pressed = controls and is_sel
		(row["up"] as Button).visible = controls and not is_sel and current != 100 and selected_amount != 0
		(row["dn"] as Button).visible = controls and not is_sel and current != 0 and selected_amount != 100
	_trader_text.text = _land_text(st.get("land", {})) + "\n" + _ship_text(st.get("ship", {}))

func _hotkey_text(name: String) -> String:
	if _hotkeys:
		var keys: PackedStringArray = _hotkeys.call("get_hotkey_map").get(name, PackedStringArray())
		if keys.size() > 0:
			return "[%s]" % keys[0]
	return "[Shift]"

func _held(name: String) -> bool:
	if _hotkeys:
		return _hotkeys.call("is_held", name)
	return Input.is_key_pressed(KEY_SHIFT)

func _on_sell_selected(r: int) -> void:
	selected_sell = r
	update_panels()

func _on_buy_pressed(r: int) -> void:
	var st := _state()
	emit_signal("barter_requested", selected_sell, r, int(st.get("amount", 100)))
	update_panels()

func _on_good_selected(r: int) -> void:
	if _held("session.fulltradeswap"):
		for i in range(trading_goods.size()):
			trading_goods[i] = 0
		trading_goods[r] = 100
		_send_goods()
	selected_good = r
	update_panels()

func _on_arrow(r: int, dir: int) -> void:
	var amount: int
	if dir > 0:
		amount = mini(AMOUNT_STEP, trading_goods[selected_good])
	else:
		amount = -mini(AMOUNT_STEP, trading_goods[r])
	trading_goods[selected_good] -= amount
	trading_goods[r] += amount
	_send_goods()
	update_panels()

func _send_goods() -> void:
	emit_signal("trading_goods_requested", trading_goods.duplicate())

func _idle(text: String) -> String:
	return "[color=%s]%s[/color]" % [IDLE_COLOR, text]

func _plural(n: int, one: String, many: String) -> String:
	return (one if n == 1 else many) % n

# TraderStatusTextLand.js
func _land_text(t: Dictionary) -> String:
	var active: int = t.get("trading", 0)
	var garrisoned: int = t.get("garrisoned", 0)
	var inactive: int = int(t.get("total", 0)) - active - garrisoned
	var trading_s := _plural(active, "There is %d land trader trading", "There are %d land traders trading")
	var inactive_s: String
	if active or garrisoned:
		inactive_s = _idle("%d inactive" % inactive)
	else:
		inactive_s = _idle(_plural(inactive, "%d land trader inactive", "%d land traders inactive"))
	var garrisoned_s: String
	if active or inactive:
		garrisoned_s = "%d garrisoned on a trading merchant ship" % garrisoned
	else:
		garrisoned_s = _plural(garrisoned, "There is %d land trader garrisoned on a trading merchant ship",
				"There are %d land traders garrisoned on a trading merchant ship")
	if active:
		if garrisoned:
			return ("%s, %s, and %s." % [trading_s, garrisoned_s, inactive_s]) if inactive else ("%s, and %s." % [trading_s, garrisoned_s])
		return ("%s, and %s." % [trading_s, inactive_s]) if inactive else ("%s." % trading_s)
	if garrisoned:
		return ("%s, and %s." % [garrisoned_s, inactive_s]) if inactive else ("%s." % garrisoned_s)
	if inactive:
		return ("There is %s." if inactive == 1 else "There are %s.") % inactive_s
	return "There are no land traders."

# TraderStatusTextShip.js
func _ship_text(t: Dictionary) -> String:
	var active: int = t.get("trading", 0)
	var inactive: int = int(t.get("total", 0)) - active
	var trading_s := _plural(active, "There is %d merchant ship trading", "There are %d merchant ships trading")
	var inactive_s: String
	if active:
		inactive_s = _idle("%d inactive" % inactive)
	else:
		inactive_s = _idle(_plural(inactive, "%d merchant ship inactive", "%d merchant ships inactive"))
	if active:
		return ("%s, and %s." % [trading_s, inactive_s]) if inactive else ("%s." % trading_s)
	if inactive:
		return ("There is %s." if inactive == 1 else "There are %s.") % inactive_s
	return "There are no merchant ships."
