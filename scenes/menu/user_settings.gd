extends Node
## Applies the options page's settings everywhere (autoload). The values are
## 0 A.D.'s config keys from RtsoUserConfig (user://config/user.cfg, see
## src/presentation/user_config.h); this is what 0 A.D.'s engine does when
## options.js calls ConfigDB_CreateValue and the renderer, sound manager and
## GUI read them back. The session's own options (batch size, wounded
## threshold, flares, range overlays, attack notices, pause on focus loss) are
## read in C++ by RtsoMain.

## Bus per 0 A.D. sound gain. Each bus is scaled by gain / default gain, so
## the default settings keep RTS0's mix and the sliders move it as in 0 A.D.
const BUSES := {
	"Music": "sound.musicgain",
	"Ambient": "sound.ambientgain",
	"Action": "sound.actiongain",
	"UI": "sound.uigain",
}
const SHADOW_ATLAS := [1024, 2048, 4096, 8192] # shadowquality -1 Low .. 2 Very High
# shadowscovermap: a cutoff past the largest map's diagonal (Giant, 512 m).
const SHADOW_COVER_MAP_DISTANCE := 2000.0
# FidelityFX CAS (CASFilter in 0.28's postproc), on the 3D picture under the GUI.
const CAS_SHADER := """
shader_type canvas_item;
uniform sampler2D screen: hint_screen_texture, filter_linear;
uniform float sharpness = 0.3;
void fragment() {
	vec2 px = SCREEN_PIXEL_SIZE;
	vec3 a = texture(screen, SCREEN_UV + vec2(-px.x, -px.y)).rgb;
	vec3 b = texture(screen, SCREEN_UV + vec2(0.0, -px.y)).rgb;
	vec3 c = texture(screen, SCREEN_UV + vec2(px.x, -px.y)).rgb;
	vec3 d = texture(screen, SCREEN_UV + vec2(-px.x, 0.0)).rgb;
	vec3 e = texture(screen, SCREEN_UV).rgb;
	vec3 f = texture(screen, SCREEN_UV + vec2(px.x, 0.0)).rgb;
	vec3 g = texture(screen, SCREEN_UV + vec2(-px.x, px.y)).rgb;
	vec3 h = texture(screen, SCREEN_UV + vec2(0.0, px.y)).rgb;
	vec3 i = texture(screen, SCREEN_UV + vec2(px.x, px.y)).rgb;
	vec3 mn = min(min(min(d, e), min(f, b)), h);
	mn += min(mn, min(min(a, c), min(g, i)));
	vec3 mx = max(max(max(d, e), max(f, b)), h);
	mx += max(mx, max(max(a, c), max(g, i)));
	vec3 amp = clamp(min(mn, 2.0 - mx) / max(mx, vec3(1e-5)), 0.0, 1.0);
	amp = sqrt(amp);
	vec3 w = -amp / mix(8.0, 5.0, clamp(sharpness, 0.0, 1.0));
	COLOR = vec4(clamp((b * w + d * w + f * w + h * w + e) / (1.0 + 4.0 * w), 0.0, 1.0), 1.0);
}
"""

## 0 A.D.'s translations, one merged .po per language (tools/fetch_0ad_l10n.py).
const L10N_DIR := "res://assets/l10n"
## Each language under its own name, as 0 A.D.'s locale page lists them.
const LANGUAGE_NAMES := {
	"en": "English", "bg": "Български", "ca": "Català", "cs": "Čeština",
	"de": "Deutsch", "el": "Ελληνικά", "es": "Español", "fi": "Suomi",
	"fr": "Français", "gl": "Galego", "hu": "Magyar", "id": "Bahasa Indonesia",
	"it": "Italiano", "ja": "日本語", "ko": "한국어", "nl": "Nederlands",
	"pl": "Polski", "pt_BR": "Português (Brasil)", "pt_PT": "Português (Portugal)",
	"ru": "Русский", "sk": "Slovenčina", "sv": "Svenska", "tr": "Türkçe",
	"uk": "Українська", "zh": "中文 (简体)", "zh_TW": "中文 (繁體)",
}

const DEFAULT_CURSOR := "res://assets/textures/cursors/default-arrow.png"

var cfg := RtsoUserConfig.new()
var _gen := -1
var _in_session := false
var _overlay: CanvasLayer
## The water shader's global uniforms as last set (the headless renderer keeps none).
var water_globals := {}
var _cas: CanvasLayer
var _cas_material: ShaderMaterial
var _overlay_label: Label
var _overlay_timer := 0.0
var _message: Label # take_screenshot's on-screen line
var _message_left := 0.0
var _hotkeys: RtsoHotkeyConfig
var last_mouse_grab := false # what mousegrabtoggle last asked for
var _locale := ""
var _translation: Translation

func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS
	# 0 A.D.'s enabled mods (mod.enabledmods) load over the game's files first.
	preload("res://scenes/menu/mods.gd").load_enabled_packs()
	_make_buses()
	_make_overlay()
	get_tree().node_added.connect(_on_node_added)
	apply_all()
	sync_text_hotkeys()
	# VideoMode.cpp DEFAULT_CURSOR_NAME: 0 A.D.'s own arrow (default-arrow.txt: hotspot 1 1).
	if ResourceLoader.exists(DEFAULT_CURSOR):
		Input.set_custom_mouse_cursor(load(DEFAULT_CURSOR), Input.CURSOR_ARROW, Vector2(1, 1))

## main.cpp's global hotkeys: togglefullscreen (Alt+Return, CVideoMode::ToggleFullscreen,
## not saved) and mousegrabtoggle (F3, the window grab on or off until the next change).
func _input(event: InputEvent) -> void:
	if not (event is InputEventKey) or not event.pressed or event.echo:
		return
	if _hotkeys == null:
		_hotkeys = RtsoHotkeyConfig.new()
	var fired: Array = Array(_hotkeys.fired(event))
	if "exit" in fired: # main.cpp: the custom exit key (unbound by default) quits at once
		get_viewport().set_input_as_handled()
		get_tree().quit()
	elif "togglefullscreen" in fired:
		get_viewport().set_input_as_handled()
		toggle_fullscreen()
	elif "mousegrabtoggle" in fired:
		get_viewport().set_input_as_handled()
		toggle_mouse_grab()
	elif "fps.toggle" in fired or "realtime.toggle" in fired:
		# OverlayCounter.js: the toggle is saved (ConfigDB_CreateAndSaveValue).
		get_viewport().set_input_as_handled()
		var key := "overlay.fps" if "fps.toggle" in fired else "overlay.realtime"
		cfg.set_value(key, "false" if cfg.get_bool(key) else "true")
		cfg.save()
	elif "bigscreenshot" in fired or "screenshot" in fired:
		get_viewport().set_input_as_handled()
		take_screenshot()

## The engine's text-box hotkeys (copy, paste, cut, text.delete/move.left/right) drive
## Godot's text editing actions, so rebinding them in the Hotkeys page applies.
const TEXT_ACTIONS := {"copy": "ui_copy", "paste": "ui_paste", "cut": "ui_cut",
	"text.delete.left": "ui_text_backspace_word", "text.delete.right": "ui_text_delete_word",
	"text.move.left": "ui_text_caret_word_left", "text.move.right": "ui_text_caret_word_right"}
const KEY_NAMES := {"leftarrow": KEY_LEFT, "rightarrow": KEY_RIGHT, "uparrow": KEY_UP, "downarrow": KEY_DOWN,
	"return": KEY_ENTER, "del": KEY_DELETE, "esc": KEY_ESCAPE}

func sync_text_hotkeys() -> void:
	if not ClassDB.class_exists("RtsoHotkeyConfig"):
		return
	if _hotkeys == null:
		_hotkeys = RtsoHotkeyConfig.new()
	var map: Dictionary = _hotkeys.get_hotkey_map()
	for h in TEXT_ACTIONS:
		if not map.has(h) or not InputMap.has_action(TEXT_ACTIONS[h]):
			continue
		InputMap.action_erase_events(TEXT_ACTIONS[h])
		for combo in map[h]:
			var ev := combo_event(str(combo))
			if ev:
				InputMap.action_add_event(TEXT_ACTIONS[h], ev)

## "Ctrl+LeftArrow" as a key event (null for mouse buttons or unknown names).
static func combo_event(combo: String) -> InputEventKey:
	var ev := InputEventKey.new()
	for part in combo.split("+", false):
		var k := part.strip_edges().to_lower()
		match k:
			"ctrl": ev.ctrl_pressed = true
			"shift": ev.shift_pressed = true
			"alt": ev.alt_pressed = true
			"super": ev.meta_pressed = true
			_:
				var code: int = KEY_NAMES.get(k, OS.find_keycode_from_string(part.strip_edges()))
				if code == KEY_NONE:
					return null
				ev.keycode = code
	return ev if ev.keycode != KEY_NONE else null

## main.cpp screenshot (F2) and bigscreenshot (Shift+F2) on every page:
## screenshots/screenshotNNNN.png, "Screenshot written to '<path>'" on screen.
## The big screenshot is the window's size too (0.28 renders it tiled, larger).
## Returns the path written, "" when there is nothing to capture (headless).
func take_screenshot() -> String:
	var img := get_viewport().get_texture().get_image() if not is_headless() else null
	DirAccess.make_dir_recursive_absolute("user://screenshots")
	var n := 1
	while FileAccess.file_exists("user://screenshots/screenshot%04d.png" % n):
		n += 1
	var path := "user://screenshots/screenshot%04d.png" % n
	if img == null or img.is_empty() or img.save_png(path) != OK:
		return ""
	show_message(tr("Screenshot written to '%s'") % ProjectSettings.globalize_path(path))
	return path

## LOGMESSAGERENDER: a line in the top-left corner for a few seconds.
func show_message(text: String) -> void:
	if _message == null:
		_message = Label.new()
		_message.position = Vector2(8, 4)
		_message.mouse_filter = Control.MOUSE_FILTER_IGNORE
		_message.add_theme_color_override("font_outline_color", Color.BLACK)
		_message.add_theme_constant_override("outline_size", 3)
		_overlay.add_child(_message)
	_message.text = text
	_message.visible = true
	_message_left = 5.0

func toggle_fullscreen() -> void:
	if is_headless():
		return
	var mode := DisplayServer.window_get_mode()
	var full := mode == DisplayServer.WINDOW_MODE_FULLSCREEN or mode == DisplayServer.WINDOW_MODE_EXCLUSIVE_FULLSCREEN
	DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_WINDOWED if full else DisplayServer.WINDOW_MODE_FULLSCREEN)

## Returns whether the mouse is grabbed now (headless has no window to grab).
func toggle_mouse_grab() -> bool:
	var grab := not last_mouse_grab if is_headless() else Input.mouse_mode != Input.MOUSE_MODE_CONFINED
	if not is_headless():
		Input.mouse_mode = Input.MOUSE_MODE_CONFINED if grab else Input.MOUSE_MODE_VISIBLE
	last_mouse_grab = grab
	return grab

func _process(delta: float) -> void:
	if cfg.get_generation() != _gen:
		apply_all()
	var scene := get_tree().current_scene
	var in_session := scene != null and scene.has_method("get_tech_tree")
	if in_session != _in_session:
		_in_session = in_session
		_apply_fps()
	if _message != null and _message.visible:
		_message_left -= delta
		_message.visible = _message_left > 0.0
	_overlay_timer -= delta
	if _overlay_timer <= 0.0:
		_overlay_timer = 0.25
		_update_overlay()

func is_headless() -> bool:
	return DisplayServer.get_name() == "headless"

func apply_all() -> void:
	_gen = cfg.get_generation()
	preload("res://scenes/reference/reference_common.gd").name_mode = int(cfg.get_number("gui.session.howtoshownames"))
	_apply_locale()
	_apply_sound()
	_apply_fps()
	_apply_viewport()
	_apply_window()
	_apply_shadow_server()
	_apply_water()
	for n in get_tree().root.find_children("*", "", true, false):
		_apply_node(n)

## The languages with a translation, English first.
static func available_locales() -> PackedStringArray:
	var out := PackedStringArray(["en"])
	var files := DirAccess.get_files_at(L10N_DIR)
	files.sort()
	for f in files:
		# Exported builds list "fr.po.remap" style names too; take the language.
		if f.get_extension() == "po":
			out.append(f.get_basename())
	return out

## locale "" is 0 A.D.'s "use the system language": the system locale if
## there is a translation for it, else its language, else English.
static func resolve_locale(want: String) -> String:
	var have := available_locales()
	if want == "":
		want = OS.get_locale()
	if want in have:
		return want
	var lang := want.get_slice("_", 0)
	if lang in have:
		return lang
	for l in have:
		if l.get_slice("_", 0) == lang:
			return l
	return "en"

func current_locale() -> String:
	return _locale

## locale: load that language's 0 A.D. strings into the TranslationServer.
## Every Control auto-translates its text, so labels, buttons and tooltips
## that carry 0 A.D.'s English strings switch language at once.
func _apply_locale() -> void:
	var loc := resolve_locale(cfg.get_value("locale"))
	if loc == _locale:
		return
	if _translation:
		TranslationServer.remove_translation(_translation)
		_translation = null
	if loc != "en":
		var t = load(L10N_DIR + "/" + loc + ".po")
		if t is Translation:
			t.locale = loc
			_translation = t
			TranslationServer.add_translation(t)
		else:
			push_warning("No translation for %s" % loc)
			loc = "en"
	TranslationServer.set_locale(loc)
	_locale = loc

func _make_buses() -> void:
	for bus in BUSES:
		if AudioServer.get_bus_index(bus) >= 0:
			continue
		AudioServer.add_bus()
		var i := AudioServer.bus_count - 1
		AudioServer.set_bus_name(i, bus)
		AudioServer.set_bus_send(i, "Master")

func _gain_db(key: String) -> float:
	var gain := cfg.get_number(key)
	var base := float(cfg.get_default(key))
	if gain <= 0.001 or base <= 0.0:
		return -80.0
	return linear_to_db(gain / base)

func _apply_sound() -> void:
	AudioServer.set_bus_volume_db(AudioServer.get_bus_index("Master"), _gain_db("sound.mastergain"))
	for bus in BUSES:
		var i := AudioServer.get_bus_index(bus)
		if i >= 0:
			AudioServer.set_bus_volume_db(i, _gain_db(BUSES[bus]))

## adaptivefps.menu / adaptivefps.session; vsync.
func _apply_fps() -> void:
	var key := "adaptivefps.session" if _in_session else "adaptivefps.menu"
	var fps := int(cfg.get_number(key))
	Engine.max_fps = 0 if fps >= 360 else clampi(fps, 20, 360)
	if not is_headless():
		DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_ENABLED if cfg.get_bool("vsync") else DisplayServer.VSYNC_DISABLED)

## renderer.scale with renderer.upscale.technique, antialiasing and
## gui.scale on the main viewport.
func _apply_viewport() -> void:
	var vp := get_tree().root
	vp.scaling_3d_scale = clampf(cfg.get_number("renderer.scale"), 0.25, 2.0)
	var tech := cfg.get_value("renderer.upscale.technique")
	vp.scaling_3d_mode = Viewport.SCALING_3D_MODE_FSR if tech == "fsr" and vp.scaling_3d_scale < 1.0 else Viewport.SCALING_3D_MODE_BILINEAR
	var aa := cfg.get_value("antialiasing")
	vp.screen_space_aa = Viewport.SCREEN_SPACE_AA_FXAA if aa == "fxaa" else Viewport.SCREEN_SPACE_AA_DISABLED
	match aa:
		"msaa2": vp.msaa_3d = Viewport.MSAA_2X
		"msaa4": vp.msaa_3d = Viewport.MSAA_4X
		"msaa8", "msaa16": vp.msaa_3d = Viewport.MSAA_8X
		_: vp.msaa_3d = Viewport.MSAA_DISABLED
	vp.content_scale_factor = clampf(cfg.get_number("gui.scale"), 0.75, 2.5)
	# textures.quality as a mipmap bias: each step below High drops a mip level, as
	# 0.28's texture manager loads smaller textures.
	# textures.maxanisotropy (1x..16x) as Godot's level 0..4, which the renderer reads
	# when the viewport's samplers are rebuilt; a mip bias a hair different per level
	# makes it rebuild them now (the GPU clamps an unsupported level, as 0.28 does).
	var aniso := clampi(int(cfg.get_number("textures.maxanisotropy")), 1, 16)
	var level := 0
	while level < 4 and (2 << level) <= aniso:
		level += 1
	ProjectSettings.set_setting("rendering/textures/default_filters/anisotropic_filtering_level", level)
	vp.texture_mipmap_bias = float(2 - clampi(int(cfg.get_number("textures.quality")), 0, 2)) - 0.0001 * level
	_apply_sharpening()

## sharpening ("cas") and sharpness, under postproc (options.json dependencies).
func _apply_sharpening() -> void:
	var on := cfg.get_bool("postproc") and cfg.get_value("sharpening") == "cas" and not is_headless()
	if on and _cas == null:
		_cas = CanvasLayer.new()
		_cas.layer = -100 # over the 3D view, under every GUI layer
		var rect := ColorRect.new()
		rect.set_anchors_preset(Control.PRESET_FULL_RECT)
		rect.mouse_filter = Control.MOUSE_FILTER_IGNORE
		var sh := Shader.new()
		sh.code = CAS_SHADER
		_cas_material = ShaderMaterial.new()
		_cas_material.shader = sh
		rect.material = _cas_material
		_cas.add_child(rect)
		add_child(_cas)
	if _cas:
		_cas.visible = on
		_cas_material.set_shader_parameter("sharpness", clampf(cfg.get_number("sharpness"), 0.0, 1.0))

## windowed; window.mousegrabinfullscreen / window.mousegrabinwindowmode.
func _apply_window() -> void:
	if is_headless():
		return
	var windowed := cfg.get_bool("windowed")
	var mode := DisplayServer.window_get_mode()
	var full := mode == DisplayServer.WINDOW_MODE_FULLSCREEN or mode == DisplayServer.WINDOW_MODE_EXCLUSIVE_FULLSCREEN
	if windowed and full:
		DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_WINDOWED)
	elif not windowed and not full:
		DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_FULLSCREEN)
	var grab := cfg.get_bool("window.mousegrabinwindowmode") if windowed else cfg.get_bool("window.mousegrabinfullscreen")
	if grab and Input.mouse_mode == Input.MOUSE_MODE_VISIBLE:
		Input.mouse_mode = Input.MOUSE_MODE_CONFINED
	elif not grab and Input.mouse_mode == Input.MOUSE_MODE_CONFINED:
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE

## shadowquality (map resolution) and shadowpcf (filtering).
func _apply_shadow_server() -> void:
	var q := clampi(int(cfg.get_number("shadowquality")) + 1, 0, 3)
	RenderingServer.directional_shadow_atlas_set_size(SHADOW_ATLAS[q], true)
	RenderingServer.directional_soft_shadow_filter_set_quality(
		RenderingServer.SHADOW_QUALITY_SOFT_LOW if cfg.get_bool("shadowpcf") else RenderingServer.SHADOW_QUALITY_HARD)

## watereffects, waterfancyeffects, waterreflection, waterrefraction and
## waterrealdepth: shaders/water.gdshader's global uniforms.
func _apply_water() -> void:
	for k in [["rtso_water_effects", "watereffects"], ["rtso_water_fancy", "waterfancyeffects"],
			["rtso_water_reflection", "waterreflection"], ["rtso_water_refraction", "waterrefraction"],
			["rtso_water_realdepth", "waterrealdepth"]]:
		water_globals[k[0]] = 1.0 if cfg.get_bool(k[1]) else 0.0
		RenderingServer.global_shader_parameter_set(k[0], water_globals[k[0]])

func _on_node_added(n: Node) -> void:
	if n is DirectionalLight3D or n is WorldEnvironment or n is GPUParticles3D or n is CPUParticles3D:
		_apply_node.call_deferred(n)

## shadows and shadowscutoffdistance on the sun, fog and postproc on the
## environment, particles on particle emitters. Each node's own value is kept
## as the "on" state, so turning an option back on restores the scene's look.
func _apply_node(n) -> void:
	if not is_instance_valid(n):
		return
	if n is DirectionalLight3D:
		if not n.has_meta("rtso_shadow"):
			n.set_meta("rtso_shadow", n.shadow_enabled)
			n.set_meta("rtso_shadow_dist", n.directional_shadow_max_distance)
		n.shadow_enabled = n.get_meta("rtso_shadow") and cfg.get_bool("shadows")
		# 0 A.D.'s default cutoff is 300 m; scale the scene's own distance by the setting.
		n.directional_shadow_max_distance = float(n.get_meta("rtso_shadow_dist")) * clampf(cfg.get_number("shadowscutoffdistance"), 100.0, 1500.0) / 300.0
		if cfg.get_bool("shadowscovermap"):
			n.directional_shadow_max_distance = SHADOW_COVER_MAP_DISTANCE
	elif n is WorldEnvironment and n.environment:
		var env: Environment = n.environment
		if not env.has_meta("rtso_fog"):
			env.set_meta("rtso_fog", env.fog_enabled)
			env.set_meta("rtso_glow", env.glow_enabled)
			env.set_meta("rtso_ssao", env.ssao_enabled)
		env.fog_enabled = env.get_meta("rtso_fog") and cfg.get_bool("fog")
		env.glow_enabled = env.get_meta("rtso_glow") and cfg.get_bool("postproc")
		env.ssao_enabled = env.get_meta("rtso_ssao") and cfg.get_bool("postproc")
	elif n is GPUParticles3D or n is CPUParticles3D:
		var on := cfg.get_bool("particles")
		if not on and n.visible:
			n.set_meta("rtso_hidden", true)
			n.visible = false
		elif on and n.has_meta("rtso_hidden"):
			n.remove_meta("rtso_hidden")
			n.visible = true

## overlay.fps and overlay.realtime: top-right corner, as in 0 A.D.
func _make_overlay() -> void:
	_overlay = CanvasLayer.new()
	_overlay.layer = 128
	add_child(_overlay)
	_overlay_label = Label.new()
	_overlay_label.set_anchors_preset(Control.PRESET_TOP_RIGHT)
	_overlay_label.offset_left = -220
	_overlay_label.offset_right = -8
	_overlay_label.offset_top = 4
	_overlay_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	_overlay_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_overlay_label.add_theme_color_override("font_outline_color", Color.BLACK)
	_overlay_label.add_theme_constant_override("outline_size", 3)
	_overlay.add_child(_overlay_label)

func _update_overlay() -> void:
	var lines: PackedStringArray = []
	if cfg.get_bool("overlay.fps"):
		lines.append("FPS: %d" % int(Engine.get_frames_per_second()))
	if cfg.get_bool("overlay.realtime"):
		var t := Time.get_time_dict_from_system()
		lines.append("%02d:%02d:%02d" % [t.hour, t.minute, t.second])
	# OverlayCounterElapsedTime / OverlayCounterRemainingCeasefire, in a match.
	var scene := get_tree().current_scene
	if scene != null and scene.has_method("get_overlay_times"):
		var times: Dictionary = scene.get_overlay_times()
		if cfg.get_bool("gui.session.timeelapsedcounter"):
			var speed: float = times.get("speed", 1.0)
			var elapsed := _time_string(times.get("elapsed", 0.0))
			lines.append(elapsed if is_equal_approx(speed, 1.0) else "%s (%sx)" % [elapsed, String.num(speed, 2)])
		if cfg.get_bool("gui.session.ceasefirecounter") and float(times.get("ceasefire", 0.0)) > 0.0:
			lines.append(_time_string(times["ceasefire"]))
	_overlay_label.text = "\n".join(lines)
	_overlay_label.visible = not lines.is_empty()

## 0 A.D. timeToString: "MM:SS", or "HH:MM:SS" from an hour on.
static func _time_string(seconds: float) -> String:
	var t := int(seconds)
	if t >= 3600:
		return "%02d:%02d:%02d" % [t / 3600, t / 60 % 60, t % 60]
	return "%02d:%02d" % [t / 60, t % 60]
