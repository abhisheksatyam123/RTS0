extends RefCounted
## gui/common/tab_buttons.xml and tab_buttons.js, shared by Options, Credits and
## Game Setup: tab.next / tab.prev (Tab / Shift+Tab) move a tab on, the mouse
## wheel over a tab button does the same, and each button's tooltip adds the
## two lines saying so after its own tooltip (placeTabButtons).

const HOTKEY_TEXT := preload("res://scenes/menu/replay_browser.gd")
const L10n := preload("res://scenes/menu/l10n.gd")

## placeTabButtons' tooltip: the tab's own tooltip, then the scroll lines.
static func tooltip(own: String, horizontal: bool, keys: Dictionary) -> String:
	var next := L10n.t("Scroll down or use %(hotkey)s to move a tab right.") if horizontal \
			else L10n.t("Scroll down or use %(hotkey)s to move a tab down.")
	var prev := L10n.t("Scroll up or use %(hotkey)s to move a tab left.") if horizontal \
			else L10n.t("Scroll up or use %(hotkey)s to move a tab up.")
	var lines := [] if own.is_empty() else [own]
	lines.append(L10n.sprintf(next, {"hotkey": HOTKEY_TEXT.hotkey_text(keys, "tab.next")}))
	lines.append(L10n.sprintf(prev, {"hotkey": HOTKEY_TEXT.hotkey_text(keys, "tab.prev")}))
	return "\n".join(lines)

## +1 for tab.next, -1 for tab.prev, else 0.
static func direction(event: InputEvent, hotkeys: RtsoHotkeyConfig) -> int:
	if not (event is InputEventKey and event.pressed):
		return 0
	var fired := hotkeys.fired(event)
	return 1 if "tab.next" in fired else -1 if "tab.prev" in fired else 0

## +1 for a wheel down, -1 for a wheel up (MouseWheelDown / MouseWheelUp), else 0.
static func wheel(event: InputEvent) -> int:
	if not (event is InputEventMouseButton and event.pressed):
		return 0
	return 1 if event.button_index == MOUSE_BUTTON_WHEEL_DOWN else -1 if event.button_index == MOUSE_BUTTON_WHEEL_UP else 0

## selectNextTab: the tab `direction` on from `current`, wrapping; with none
## selected (-1) the first going forward, the last going back.
static func next_index(current: int, direction: int, count: int) -> int:
	if count <= 0:
		return current
	if current < 0:
		return 0 if direction > 0 else count - 1
	return (current + direction + count) % count
