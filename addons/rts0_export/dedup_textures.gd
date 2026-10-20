@tool
extends EditorExportPlugin
## Godot unpacks every model's embedded images next to it (<model>_texN.png), so
## the same image sits in the project once per model that carries it: about
## 6,000 copies and 1 GB of the exported pack. The first copy of an image is
## exported as usual; every later one with the same import settings is exported
## as just its .import remap, pointed at the first copy's imported files, so its
## path, uid and loading stay the same.

const TEXTURE_EXTS := ["png", "jpg", "jpeg", "webp"]

var _first := {}  # source md5 + import params -> the first copy's [remap] ConfigFile
var _shared := 0
var _bytes := 0


func _get_name() -> String:
	return "RTS0DedupTextures"


func _export_begin(_features: PackedStringArray, _is_debug: bool, _path: String, _flags: int) -> void:
	_first.clear()
	_shared = 0
	_bytes = 0


func _export_end() -> void:
	print("dedup_textures: %d duplicate textures share an imported copy, %d MB saved" % [_shared, _bytes >> 20])


func _export_file(path: String, _type: String, _features: PackedStringArray) -> void:
	if not path.get_extension().to_lower() in TEXTURE_EXTS:
		return
	var imp_path := path + ".import"
	if not FileAccess.file_exists(imp_path):
		return
	var text := FileAccess.get_file_as_string(imp_path)
	var at := text.find("[params]")
	if at < 0:
		return
	var key := FileAccess.get_md5(path) + text.substr(at)
	var imp := ConfigFile.new()
	if imp.parse(text) != OK or imp.get_value("remap", "importer", "") != "texture":
		return
	if not _first.has(key):
		_first[key] = imp
		return
	var first: ConfigFile = _first[key]
	var remap := ConfigFile.new()
	for k in imp.get_section_keys("remap"):
		remap.set_value("remap", k, imp.get_value("remap", k))
	for k in first.get_section_keys("remap"):
		if k.begins_with("path"):
			remap.set_value("remap", k, first.get_value("remap", k))
			var f := FileAccess.open(imp.get_value("remap", k), FileAccess.READ)
			if f:
				_bytes += f.get_length()
	skip()
	add_file(imp_path, remap.encode_to_text().to_utf8_buffer(), false)
	_shared += 1
