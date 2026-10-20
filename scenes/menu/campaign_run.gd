class_name CampaignRun
extends RefCounted
## A campaign run, ported from 0 A.D. gui/campaigns (CampaignRun.js,
## CampaignTemplate.js, default_menu/utils.js; GPL-2.0-or-later).
##
## A campaign template (res://data/campaigns/<id>.json, 0 A.D.'s campaigns/*.json)
## names its levels, their maps and what each level Requires. A run is one
## playthrough of a template, kept in user://saves/campaigns/<filename>.0adcampaign
## as 0 A.D.'s {"data", "meta", "template_identifier"}: the template is read
## from data/campaigns when the run loads, so a changed template reaches old runs.
## data.completedLevels lists the levels won. 0 A.D. keeps the current run in user.cfg's "currentcampaign";
## RTS0 keeps it in user://saves/campaigns/current.

const TEMPLATE_DIR := "res://data/campaigns"
const RUN_DIR := "user://saves/campaigns"
const CURRENT := "user://saves/campaigns/current"
const EXTENSION := ".0adcampaign"

var filename := ""
var template := {}
var data := {}
var meta := {}

func _init(file := "") -> void:
	filename = file

# --- Templates (CampaignTemplate.getAvailableTemplates) -------------------------

## Every campaign template, each with its "identifier" (the file name), in file order.
static func available_templates() -> Array:
	var out: Array = []
	var dir := DirAccess.open(TEMPLATE_DIR)
	if dir == null:
		return out
	var files: Array = []
	for f in dir.get_files():
		if f.ends_with(".json"):
			files.append(f)
	files.sort()
	for f in files:
		var parsed: Variant = _read_json(TEMPLATE_DIR.path_join(f))
		if typeof(parsed) != TYPE_DICTIONARY:
			continue
		var t: Dictionary = parsed
		t["identifier"] = f.get_basename()
		out.append(t)
	return out

# --- The run file (CampaignRun.js) ----------------------------------------------

func path() -> String:
	return RUN_DIR.path_join(filename + EXTENSION)

func set_template(t: Dictionary) -> CampaignRun:
	template = t.duplicate(true)
	return self

## userDescription: the name the player gave the run.
func set_meta_description(description: String) -> CampaignRun:
	meta["userDescription"] = description
	return self

## The template with this identifier (CampaignTemplate.getTemplate), {} if there is none.
static func template_by_id(id: String) -> Dictionary:
	for t in available_templates():
		if t["identifier"] == id:
			return t
	return {}

## Reads the run; false when the file is missing or broken, or its template is
## gone (0 A.D.'s BrokenRun). Runs saved before RTS0 used 0 A.D.'s format carry
## a copy of their template, which is used when its file is gone.
func load_run() -> bool:
	var parsed: Variant = _read_json(path())
	if typeof(parsed) != TYPE_DICTIONARY:
		return false
	var t := template_by_id(str(parsed.get("template_identifier", "")))
	if t.is_empty() and typeof(parsed.get("template")) == TYPE_DICTIONARY:
		var copy: Dictionary = parsed["template"]
		t = template_by_id(str(copy.get("identifier", "")))
		if t.is_empty():
			t = copy
	if t.is_empty():
		return false
	template = t
	data = parsed.get("data", {}) if typeof(parsed.get("data")) == TYPE_DICTIONARY else {}
	meta = parsed.get("meta", {}) if typeof(parsed.get("meta")) == TYPE_DICTIONARY else {}
	return true

func save() -> CampaignRun:
	DirAccess.make_dir_recursive_absolute(RUN_DIR)
	var f := FileAccess.open(path(), FileAccess.WRITE)
	if f:
		f.store_string(JSON.stringify({"data": data, "meta": meta, "template_identifier": str(template.get("identifier", ""))}, "\t"))
	return self

func destroy() -> void:
	DirAccess.remove_absolute(path())
	if current_filename() == filename:
		clear_current()

## getLabel: "<the player's name for it> - <campaign name>", or just the name
## when the player kept the campaign's own (unless full).
func label(full := false) -> String:
	var desc := str(meta.get("userDescription", ""))
	var name: String = TranslationServer.translate(str(template["Name"])) if template.has("Name") else filename
	if desc == "" or (not full and desc == name):
		return name if desc == "" else desc
	return TranslationServer.translate("%(userDesc)s - %(templateName)s") \
		.replace("%(userDesc)s", desc).replace("%(templateName)s", name)

# --- The current run -------------------------------------------------------------

static func current_filename() -> String:
	if not FileAccess.file_exists(CURRENT):
		return ""
	var f := FileAccess.open(CURRENT, FileAccess.READ)
	return f.get_as_text().strip_edges() if f else ""

func set_current() -> CampaignRun:
	DirAccess.make_dir_recursive_absolute(RUN_DIR)
	var f := FileAccess.open(CURRENT, FileAccess.WRITE)
	if f:
		f.store_string(filename)
	return self

static func clear_current() -> void:
	if FileAccess.file_exists(CURRENT):
		DirAccess.remove_absolute(CURRENT)

## The current run, or null when there is none or it cannot be read.
static func current_run() -> CampaignRun:
	var file := current_filename()
	if file == "":
		return null
	var run := CampaignRun.new(file)
	return run if run.load_run() else null

## Every saved run (LoadModal.getRuns), broken ones included with "broken" set.
static func saved_runs() -> Array:
	var out: Array = []
	var dir := DirAccess.open(RUN_DIR)
	if dir == null:
		return out
	var files: Array = []
	for f in dir.get_files():
		if f.ends_with(EXTENSION):
			files.append(f)
	files.sort()
	for f in files:
		var run := CampaignRun.new(f.trim_suffix(EXTENSION))
		out.append({"run": run, "broken": not run.load_run()})
	return out

## NewCampaignModal.createAndStartCampaign's file name.
static func new_filename(template_id: String) -> String:
	return "%s_%d_%d" % [template_id, Time.get_unix_time_from_system() * 1000, randi() % 100000]

# --- Levels (default_menu/utils.js) -----------------------------------------------

func is_completed(level_id: String) -> bool:
	return data.get("completedLevels", []).has(level_id)

## meetsRequirements: Requires is a class list ("a b" either, "a+b" both, "!a" not).
func meets_requirements(level: Dictionary) -> bool:
	var req := str(level.get("Requires", ""))
	return req == "" or matches_class_list(data.get("completedLevels", []), req)

func mark_level_complete(level_id: String) -> void:
	if is_completed(level_id):
		return
	var done: Array = data.get("completedLevels", [])
	done.append(level_id)
	data["completedLevels"] = done
	save()

## MatchesClassList: true if one of the space-separated alternatives matches; an
## alternative "a+b" needs every class, "!a" needs a class to be absent.
static func matches_class_list(classes: Array, list: String) -> bool:
	for alt in list.split(" ", false):
		var ok := true
		for c in alt.split("+", false):
			ok = ok and (not classes.has(c.substr(1)) if c.begins_with("!") else classes.has(c))
		if ok:
			return true
	return false

## The levels in the template's Order, each {id, level}; levels missing from
## Order go last.
func ordered_levels() -> Array:
	var levels: Dictionary = template.get("Levels", {})
	var order: Array = template.get("Order", [])
	var ids: Array = levels.keys()
	ids.sort_custom(func(a, b):
		var ia := order.find(a)
		var ib := order.find(b)
		return (ia if ia >= 0 else 1 << 20) < (ib if ib >= 0 else 1 << 20))
	return ids.map(func(id): return {"id": id, "level": levels[id]})

## A level's map as an RTS0 map id: "tutorials/introductory_tutorial.xml" -> "introductory_tutorial".
static func level_map_id(level: Dictionary) -> String:
	return str(level.get("Map", "")).get_file().get_basename()

static func _read_json(p: String) -> Variant:
	if not FileAccess.file_exists(p):
		return null
	var f := FileAccess.open(p, FileAccess.READ)
	return JSON.parse_string(f.get_as_text()) if f else null
