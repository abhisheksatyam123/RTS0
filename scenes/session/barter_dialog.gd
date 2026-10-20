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

# TradeDialog.xml: 200 px plus 58 px per resource (BarterPanel/TradePanel.getWidthOffset), 388 tall.
const DIALOG_SIZE := Vector2(200 + 58 * 4, 388)
const COLUMN := 58
const GOLD := Color(0.9, 0.75, 0.31)

@onready var body: Control = $CenterContainer/Panel/Body

var selected_sell := 3
var selected_good := 3
var trading_goods := PackedInt32Array([25, 25, 25, 25])
var _goods_from_sim := true
var _sell_buttons := {}
var _buy_buttons := {}
var _trade_rows := {}
var _barter_box: Control
var _no_market: Label
var _barter_help: Button
var _trade_help: Button
var _trader_text: RichTextLabel
var _icons := {}
var _corners: Texture2D
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
	if ResourceLoader.exists("res://assets/textures/ui/archive/icons/corners.png"):
		_corners = load("res://assets/textures/ui/archive/icons/corners.png")
	_build()

func _place(c: Control, parent: Control, pos: Vector2, size: Vector2) -> Control:
	parent.add_child(c)
	if c is Button:
		_tight(c)
	c.position = pos
	c.size = size
	return c

# The theme's stone buttons pad their content; these have 0.28's fixed sizes.
func _tight(b: Button) -> void:
	for st in ["normal", "hover", "pressed", "disabled", "focus"]:
		var sb: StyleBox = b.get_theme_stylebox(st)
		if sb:
			sb = sb.duplicate()
			sb.set_content_margin_all(0)
			b.add_theme_stylebox_override(st, sb)

func _label(text: String, size := 13, color := Color(0.85, 0.88, 0.92)) -> Label:
	var l := Label.new()
	l.text = text
	l.add_theme_font_size_override("font_size", size)
	l.add_theme_color_override("font_color", color)
	l.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	return l

# ModernLeftLabelText, then the ModernGoldLine under a section title.
func _section(parent: Control, title: String) -> void:
	_place(_label(title, 15, GOLD), parent, Vector2.ZERO, Vector2(parent.size.x, 32))
	var line := ColorRect.new()
	line.color = Color(GOLD, 0.8)
	_place(line, parent, Vector2(0, 28), Vector2(parent.size.x, 1))

# barterHelp / tradeHelp: a disabled StoneButton with the info icon; the
# instructions are its tooltip. 0 A.D.'s global/icon/info.png is copied to
# assets/textures/ui/icons/info.png (see tools/extract_ui_assets.py).
func _info_button() -> Button:
	var b := Button.new()
	b.text = ""
	b.disabled = true
	var info_path := "res://assets/textures/ui/icons/info.png"
	if ResourceLoader.exists(info_path):
		b.icon = load(info_path)
		b.expand_icon = true
	return b

func _arrow_button(dir: String) -> TextureButton:
	var b := TextureButton.new()
	b.name = "arrow_" + dir
	b.ignore_texture_size = true
	b.stretch_mode = TextureButton.STRETCH_SCALE
	for pair in [["texture_normal", ""], ["texture_hover", "_over"]]:
		var path := "res://assets/textures/ui/icons/arrowbox_%s%s.png" % [dir, pair[1]]
		if ResourceLoader.exists(path):
			b.set(pair[0], load(path))
	return b

# BarterButton.xml: a 38 px icon with the amount over its top half and, on the
# selected sell button, the corners frame.
func _icon_button(r: int, size: Vector2) -> Button:
	var b := Button.new()
	b.flat = false
	var ico := TextureRect.new()
	ico.name = "icon"
	ico.texture = _icons.get(r)
	ico.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	ico.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	ico.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_place(ico, b, Vector2(3, 3), size - Vector2(6, 6))
	var t := _label("", 12, Color.WHITE)
	t.name = "amount"
	t.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	t.add_theme_color_override("font_outline_color", Color.BLACK)
	t.add_theme_constant_override("outline_size", 3)
	t.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_place(t, b, Vector2.ZERO, Vector2(size.x, size.y * 0.5))
	if _corners:
		var c := TextureRect.new()
		c.name = "selection"
		c.texture = _corners
		c.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
		c.mouse_filter = Control.MOUSE_FILTER_IGNORE
		c.visible = false
		_place(c, b, Vector2(3, 3), size - Vector2(6, 6))
	return b

func _build() -> void:
	body.custom_minimum_size = DIALOG_SIZE
	var w := DIALOG_SIZE.x
	var title := _label("Barter & Trade", 18, GOLD)
	title.name = "tradeDialogTitle"
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_place(title, body, Vector2(w * 0.5 - 96, -16), Vector2(192, 32))

	# BarterPanel.xml at 24 24 100%-24 146.
	var barter := Control.new()
	_place(barter, body, Vector2(24, 24), Vector2(w - 48, 122))
	_section(barter, "Barter")
	var row := Control.new()
	_place(row, barter, Vector2(0, 38), Vector2(barter.size.x, 84))
	_place(_label("Sell:", 14, GOLD), row, Vector2.ZERO, Vector2(100, 41))
	_place(_label("Buy:", 14, GOLD), row, Vector2(0, 84 - 41), Vector2(100, 41))
	_no_market = _label("No Markets Available", 13)
	_no_market.name = "barterNoMarketsMessage"
	_place(_no_market, row, Vector2(112, 0), Vector2(row.size.x - 120, 84))
	_barter_box = Control.new()
	_barter_box.name = "barterResources"
	_place(_barter_box, row, Vector2(112, 0), Vector2(row.size.x - 112, 84))
	for i in RES_ORDER.size():
		var r: int = RES_ORDER[i]
		var sell := _icon_button(r, Vector2(38, 38))
		sell.tooltip_text = SELL_TOOLTIP % RES_NAMES[r]
		sell.pressed.connect(_on_sell_selected.bind(r))
		_place(sell, _barter_box, Vector2(i * COLUMN, 0), Vector2(38, 38))
		_sell_buttons[r] = sell
		var buy := _icon_button(r, Vector2(38, 38))
		buy.tooltip_text = BUY_TOOLTIP % RES_NAMES[r]
		buy.pressed.connect(_on_buy_pressed.bind(r))
		_place(buy, _barter_box, Vector2(i * COLUMN, 42), Vector2(38, 38))
		_buy_buttons[r] = buy
	_barter_help = _info_button()
	_barter_help.name = "barterHelp"
	_place(_barter_help, row, Vector2(row.size.x - 24, 30), Vector2(24, 24))

	# TradePanel.xml at 24 168 100%-24 100%-64.
	var trade := Control.new()
	_place(trade, body, Vector2(24, 168), Vector2(w - 48, DIALOG_SIZE.y - 64 - 168))
	_section(trade, "Trade")
	var goods := Control.new()
	_place(goods, trade, Vector2(0, 38), Vector2(trade.size.x, 32))
	_place(_label("Resources:", 14, GOLD), goods, Vector2.ZERO, Vector2(100, 32))
	for i in RES_ORDER.size():
		var r: int = RES_ORDER[i]
		var col := Control.new()
		col.name = "tradeResource%d" % i
		_place(col, goods, Vector2(112 + i * COLUMN, 0), Vector2(COLUMN, 32))
		var good := _icon_button(r, Vector2(32, 32))
		good.pressed.connect(_on_good_selected.bind(r))
		_place(good, col, Vector2(4, 0), Vector2(32, 32))
		(good.get_node("amount") as Label).size = Vector2(32, 32)
		# StoneArrowUp / StoneArrowDn beside the button: bare sprites, the _over
		# one on hover. 0 A.D.'s global/button/arrowbox_*.dds are copied to
		# assets/textures/ui/icons/.
		var up := _arrow_button("up")
		up.pressed.connect(_on_arrow.bind(r, 1))
		_place(up, col, Vector2(36, 0), Vector2(16, 16))
		var dn := _arrow_button("down")
		dn.pressed.connect(_on_arrow.bind(r, -1))
		_place(dn, col, Vector2(36, 16), Vector2(16, 16))
		_trade_rows[r] = {"up": up, "button": good, "dn": dn}
	_trade_help = _info_button()
	_trade_help.name = "tradeHelp"
	_place(_trade_help, goods, Vector2(goods.size.x - 24, 4), Vector2(24, 24))
	_trader_text = RichTextLabel.new()
	_trader_text.name = "traderCountText"
	_trader_text.bbcode_enabled = true
	_trader_text.scroll_active = false
	_trader_text.add_theme_font_size_override("normal_font_size", 12)
	_place(_trader_text, trade, Vector2(8, 88), Vector2(trade.size.x - 8, trade.size.y - 88))

	var close := Button.new()
	close.name = "closeTrade"
	close.text = "Close"
	close.add_theme_font_size_override("font_size", 14)
	close.pressed.connect(_on_close_pressed)
	_place(close, body, Vector2(w * 0.5 - 64, DIALOG_SIZE.y - 50), Vector2(128, 28))

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
	_barter_help.tooltip_text = BARTER_HELP % [100, _hotkey_text("session.massbarter"), int(st.get("multiplier", 5))]
	for r in RES_ORDER:
		var selected: bool = r == selected_sell
		var sell: Button = _sell_buttons[r]
		if sell.has_node("selection"):
			sell.get_node("selection").visible = selected
		(sell.get_node("amount") as Label).text = "-%d" % amount
		# BarterButton DisabledModifier: red when the stock can't cover the deal.
		(sell.get_node("icon") as CanvasItem).modulate = Color(1, 0.45, 0.45) if stock[r] < amount else (Color(0.6, 0.6, 0.6) if selected else Color.WHITE)
		var buy: Button = _buy_buttons[r]
		buy.visible = not selected
		buy.disabled = not controls
		var q := 0
		if quotes.size() == 16:
			q = quotes[selected_sell * 4 + r]
		(buy.get_node("amount") as Label).text = "+%d" % q
		(buy.get_node("icon") as CanvasItem).modulate = Color(1, 0.45, 0.45) if stock[selected_sell] < amount else Color.WHITE

func _update_trade(st: Dictionary) -> void:
	var controls: bool = st.get("controls", true)
	# TradeButtonManager keeps its own copy for players (the sim follows it) and
	# re-reads it for observers.
	if _goods_from_sim or not controls:
		trading_goods = st.get("trading_goods", trading_goods)
		_goods_from_sim = false
	_trade_help.tooltip_text = TRADE_HELP % _hotkey_text("session.fulltradeswap")
	var selected_amount := trading_goods[selected_good]
	for r in RES_ORDER:
		var row: Dictionary = _trade_rows[r]
		var is_sel: bool = r == selected_good
		var current := trading_goods[r]
		var b: Button = row["button"]
		(b.get_node("amount") as Label).text = "%d%%" % current
		b.disabled = not controls
		if b.has_node("selection"):
			b.get_node("selection").visible = controls and is_sel
		(row["up"] as BaseButton).visible = controls and not is_sel and current != 100 and selected_amount != 0
		(row["dn"] as BaseButton).visible = controls and not is_sel and current != 0 and selected_amount != 100
	# TraderStatusText: the land and ship texts a blank line apart.
	_trader_text.text = _land_text(st.get("land", {})) + "\n\n" + _ship_text(st.get("ship", {}))

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
