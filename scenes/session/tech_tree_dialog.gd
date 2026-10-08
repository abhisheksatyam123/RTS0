extends CanvasLayer

# Structure tree (0 A.D. gui/reference/structree): buildings and technologies
# for the player's civilization, grouped by phase. Filled from
# RtsoMain.get_tech_tree() each time the dialog opens.

const PHASE_LISTS := [
	"Center/Panel/VBox/PhasesContainer/Phase1/VBox",
	"Center/Panel/VBox/PhasesContainer/Phase2/VBox",
	"Center/Panel/VBox/PhasesContainer/Phase3/VBox",
]

var is_open: bool = false
var _texts: Array = []

func _ready() -> void:
	visible = false
	# Swap each static list label for a scrolling rich text box.
	for path in PHASE_LISTS:
		var vbox: VBoxContainer = get_node(path)
		var old := vbox.get_node_or_null("List")
		if old:
			vbox.remove_child(old)
			old.queue_free()
		var rt := RichTextLabel.new()
		rt.name = "List"
		rt.bbcode_enabled = true
		rt.scroll_active = true
		rt.size_flags_vertical = Control.SIZE_EXPAND_FILL
		rt.custom_minimum_size = Vector2(240, 420)
		rt.add_theme_font_size_override("normal_font_size", 12)
		rt.add_theme_font_size_override("bold_font_size", 12)
		vbox.add_child(rt)
		_texts.append(rt)

func toggle() -> void:
	is_open = !is_open
	if is_open:
		_refresh()
	visible = is_open

func _cost_text(t: Dictionary) -> String:
	var parts: PackedStringArray = []
	for k in [["food", "f"], ["wood", "w"], ["metal", "m"], ["stone", "s"]]:
		if int(t[k[0]]) > 0:
			parts.append("%d%s" % [int(t[k[0]]), k[1]])
	return ", ".join(parts)

func _refresh() -> void:
	var main := get_parent()
	if main == null or not main.has_method("get_tech_tree"):
		return
	var tree: Dictionary = main.get_tech_tree()
	for p in range(3):
		var phase := p + 1
		var s := "[b]Structures[/b]\n"
		for b in tree.get("buildings", []):
			if int(b["phase"]) == phase:
				s += "• %s\n" % b["name"]
		# Group techs by the building that researches them.
		var by_bld := {}
		for t in tree.get("techs", []):
			if int(t["phase"]) != phase:
				continue
			var r: String = t["researcher"]
			if not by_bld.has(r):
				by_bld[r] = []
			by_bld[r].append(t)
		for r in by_bld.keys():
			s += "\n[b]%s[/b]\n" % r
			for t in by_bld[r]:
				var line: String = t["name"]
				var cost := _cost_text(t)
				if cost != "":
					line += " [color=#aaaaaa](%s)[/color]" % cost
				if t["researched"]:
					line = "[color=#7fd67f]✓ %s[/color]" % line
				else:
					line = "• " + line
				if String(t["prerequisite"]) != "":
					line += " [color=#888888]after %s[/color]" % t["prerequisite"]
				s += line + "\n"
		_texts[p].text = s

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo:
		# The structree hotkey (Alt+Shift+T) opens this from main_node.cpp.
		if is_open and event.keycode == KEY_ESCAPE:
			toggle()
			get_viewport().set_input_as_handled()

func _on_close_pressed() -> void:
	toggle()
