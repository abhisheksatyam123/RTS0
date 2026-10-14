extends RefCounted
## 0 A.D.'s translate() and sprintf() for GDScript pages: a string built from
## parts looks up one 0 A.D. format msgid ("Game time elapsed: %(time)s") and
## fills its named arguments, so a language can reorder them. Preload it:
##   const L10n := preload("res://scenes/menu/l10n.gd")
##   label.text = L10n.sprintf(L10n.t("Team %(team)s"), {"team": 2})

## translate(msgid): the current language's string, else the English msgid.
static func t(msgid: String) -> String:
	return TranslationServer.translate(msgid)

## sprintf with named arguments: each "%(key)s" becomes str(value).
static func sprintf(format: String, args: Dictionary) -> String:
	var s := format
	for k in args:
		s = s.replace("%(" + str(k) + ")s", str(args[k]))
	return s
