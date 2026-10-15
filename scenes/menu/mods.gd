extends RefCounted
## Mods as 0 A.D. 0.28 has them (gui/modmod/modmod.js, validatemod.js,
## source/ps/Mod.cpp): each folder under user://mods/ with a mod.json
## {name, version, label, url, description, dependencies} is a mod;
## "mod.enabledmods" lists the enabled ones in load order ("mod" first, as
## 0 A.D. writes it). RTS0 loads an enabled mod's .pck and .zip files over the
## game's own files at startup (ProjectSettings.load_resource_pack), so a
## later mod overrides an earlier one. The game itself is the built-in
## "public" mod (0 A.D.'s 0ad mod), always loaded.

const MODS_DIR := "user://mods/"
const CONFIG_KEY := "mod.enabledmods"
const PUBLIC := "public"
## 0.28's binaries/data/mods/public/mod.json.
const PUBLIC_MOD := {
	"name": "0ad", "version": "0.28.0", "label": "0 A.D. - Empires Ascendant",
	"url": "https://play0ad.com", "description": "A free, open-source, historical RTS game.",
	"dependencies": [],
}
## validatemod.js g_ModProperties: [type, required].
const PROPERTIES := {
	"name": [TYPE_STRING, true], "version": [TYPE_STRING, true], "dependencies": [TYPE_ARRAY, true],
	"label": [TYPE_STRING, true], "description": [TYPE_STRING, true], "url": [TYPE_STRING, false],
}
const RE_NAME := "[a-zA-Z0-9\\-\\_]+"
const RE_VERSION := "[0-9]+(\\.[0-9]+){0,2}"
const RE_OPERATOR := "(<=|>=|<|>|=)"

## The mods packs were loaded for at startup, in order.
static var loaded: Array = []
## Engine.GetIncompatibleMods: the enabled mods that are missing, invalid or
## lack a dependency, found at startup.
static var incompatible: Array = []

static func _full(re: String) -> RegEx:
	return RegEx.create_from_string("^" + re + "$")

## Engine.GetAvailableMods: folder -> mod.json, the built-in public mod first.
static func available(dir: String = MODS_DIR) -> Dictionary:
	var out := {PUBLIC: PUBLIC_MOD}
	var d := DirAccess.open(dir)
	if d == null:
		return out
	for folder in d.get_directories():
		var path := dir.path_join(folder).path_join("mod.json")
		if folder == PUBLIC or not FileAccess.file_exists(path):
			continue
		var data: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
		if typeof(data) == TYPE_DICTIONARY:
			out[folder] = data
	return out

## validatemod.js validateMod: the properties and their formats.
static func validate(data: Dictionary) -> bool:
	for key in PROPERTIES:
		var prop: Array = PROPERTIES[key]
		if not data.has(key):
			if prop[1]:
				return false
			continue
		if typeof(data[key]) != prop[0]:
			return false
	if not _full(RE_NAME).search(data["name"]) or not _full(RE_VERSION).search(data["version"]):
		return false
	if not RegEx.create_from_string(".*\\S.*").search(data["label"]):
		return false
	var comparison := _full(RE_NAME + RE_OPERATOR + RE_VERSION)
	for dep in data["dependencies"]:
		if typeof(dep) != TYPE_STRING or not (_full(RE_NAME).search(dep) or comparison.search(dep)):
			return false
	return true

## The enabled mods in load order ("mod" left out).
static func enabled() -> Array:
	var list: Array = []
	for f in RtsoUserConfig.new().get_value(CONFIG_KEY).split(" ", false):
		if f != "mod" and not list.has(f):
			list.append(f)
	return list

## modmod.js saveMods.
static func save(folders: Array) -> void:
	var cfg := RtsoUserConfig.new()
	cfg.set_value(CONFIG_KEY, " ".join(PackedStringArray(["mod"] + folders)))
	cfg.save()

## modmod.js versionSatisfied: numbers compared part by part, "5.3" < "5.3.0".
static func version_satisfied(v1: String, op: String, v2: String) -> bool:
	var a := v1.split("-")[0].split("_")[0].split(".")
	var b := v2.split("-")[0].split("_")[0].split(".")
	var eq := op.contains("=")
	var lt := op.contains("<")
	var gt := op.contains(">")
	for i in mini(a.size(), b.size()):
		var diff := int(a[i]) - int(b[i])
		if (gt and diff > 0) or (lt and diff < 0):
			return true
		if (gt and diff < 0) or (lt and diff > 0) or (eq and diff != 0):
			return false
	var ldiff := a.size() - b.size()
	if ldiff == 0:
		return eq
	return lt if ldiff < 0 else gt

## isDependencyMet: some enabled mod has that name (and a satisfying version).
static func dependency_met(dep: String, folders: Array, mods: Dictionary) -> bool:
	var m := RegEx.create_from_string(RE_OPERATOR).search(dep)
	var dep_name := dep.substr(0, m.get_start()) if m else dep
	for f in folders:
		var mod: Dictionary = mods.get(f, {})
		if str(mod.get("name", "")) != dep_name:
			continue
		if m == null or version_satisfied(str(mod.get("version", "")), m.get_string(), dep.substr(m.get_end())):
			return true
	return false

## areDependenciesMet against the enabled list.
static func dependencies_met(folder: String, folders: Array, mods: Dictionary) -> bool:
	if not mods.has(folder) or not validate(mods[folder]):
		return false
	for dep in mods[folder]["dependencies"]:
		if not dependency_met(str(dep), folders, mods):
			return false
	return true

## sortEnabledMods: a mod after the mods it depends on.
static func sort_enabled(folders: Array, mods: Dictionary) -> Array:
	var out := folders.duplicate()
	var deps := func(f: String) -> Array:
		return Array(mods.get(f, {}).get("dependencies", [])).map(func(d): return RegEx.create_from_string(RE_OPERATOR).sub(str(d), "|").get_slice("|", 0))
	# A stable insertion sort with modmod.js's comparator.
	for i in range(1, out.size()):
		var j := i
		while j > 0 and deps.call(out[j - 1]).has(str(mods.get(out[j], {}).get("name", ""))):
			var t = out[j - 1]
			out[j - 1] = out[j]
			out[j] = t
			j -= 1
	return out

## At startup (UserSettings): the enabled mods load their .pck and .zip
## files, in the enabled order. As 0.28's CheckAndEnableMods, one missing,
## invalid or lacking a dependency loads none of them: the game starts as it
## ships and opens the Mod Selection with the Incompatible mods notice.
static func load_enabled_packs(dir: String = MODS_DIR) -> Array:
	loaded.clear()
	var mods := available(dir)
	var on := enabled()
	incompatible = on.filter(func(f): return f != PUBLIC and not dependencies_met(f, on, mods))
	if not incompatible.is_empty():
		return loaded
	for folder in on:
		if folder == PUBLIC:
			continue
		var d := DirAccess.open(dir.path_join(folder))
		if d == null:
			continue
		var files := Array(d.get_files()).filter(func(f): return f.get_extension() in ["pck", "zip"])
		files.sort()
		for f in files:
			if ProjectSettings.load_resource_pack(dir.path_join(folder).path_join(f), true):
				if not loaded.has(folder):
					loaded.append(folder)
	return loaded

## Engine.SetModsAndRestartEngine.
static func restart(tree: SceneTree) -> void:
	OS.set_restart_on_exit(true, OS.get_cmdline_args())
	tree.quit()

## Engine.GetEngineInfo().mods: the mods this run plays with, in load order,
## as {name, version, ignoreInCompatibilityChecks} (mod.json's own fields).
## The enabled list counts only when it loaded (no incompatible mod).
static func engine_mods(dir: String = MODS_DIR) -> Array:
	var mods := available(dir)
	var folders := enabled() if incompatible.is_empty() else []
	if not folders.has(PUBLIC):
		folders.push_front(PUBLIC)
	var out := []
	for f in folders:
		if not mods.has(f):
			continue
		var m: Dictionary = mods[f]
		out.append({"name": str(m.get("name", f)), "version": str(m.get("version", "")),
			"ignoreInCompatibilityChecks": m.get("ignoreInCompatibilityChecks", false) == true})
	return out

## Mod.cpp AreModsPlayCompatible: the same mods (name and version) in the
## same order, skipping the ones marked ignoreInCompatibilityChecks.
static func play_compatible(a: Array, b: Array) -> bool:
	var keep := func(list: Array) -> Array:
		return list.filter(func(m) -> bool: return m is Dictionary and not m.get("ignoreInCompatibilityChecks", false)) \
			.map(func(m) -> String: return "%s %s" % [str(m.get("name", "")), str(m.get("version", ""))])
	return keep.call(a) == keep.call(b)

## mod.js modToString: "name (version)", the name alone for a mod that does
## not affect compatibility.
static func mod_to_string(m: Dictionary) -> String:
	if m.get("ignoreInCompatibilityChecks", false):
		return str(m.get("name", ""))
	return "%s (%s)" % [str(m.get("name", "")), str(m.get("version", ""))]

## mod.js comparedModsString.
static func compared_mods_string(required: Array, active: Array) -> String:
	return "Required: %s\nActive: %s" % [", ".join(required.map(func(m) -> String: return mod_to_string(m))), ", ".join(active.map(func(m) -> String: return mod_to_string(m)))]
