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

var cfg := RtsoUserConfig.new()
var _gen := -1
var _in_session := false
var _overlay: CanvasLayer
var _overlay_label: Label
var _overlay_timer := 0.0

func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS
	_make_buses()
	_make_overlay()
	get_tree().node_added.connect(_on_node_added)
	apply_all()

func _process(delta: float) -> void:
	if cfg.get_generation() != _gen:
		apply_all()
	var scene := get_tree().current_scene
	var in_session := scene != null and scene.has_method("get_tech_tree")
	if in_session != _in_session:
		_in_session = in_session
		_apply_fps()
	_overlay_timer -= delta
	if _overlay_timer <= 0.0:
		_overlay_timer = 0.25
		_update_overlay()

func is_headless() -> bool:
	return DisplayServer.get_name() == "headless"

func apply_all() -> void:
	_gen = cfg.get_generation()
	preload("res://scenes/reference/reference_common.gd").name_mode = int(cfg.get_number("gui.session.howtoshownames"))
	_apply_sound()
	_apply_fps()
	_apply_viewport()
	_apply_window()
	_apply_shadow_server()
	for n in get_tree().root.find_children("*", "", true, false):
		_apply_node(n)

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
	_overlay_label.text = "\n".join(lines)
	_overlay_label.visible = not lines.is_empty()
