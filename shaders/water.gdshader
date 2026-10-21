// 0 A.D. Mediterranean Animated Water Shader
shader_type spatial;
render_mode specular_schlick_ggx, cull_back, depth_draw_always;

uniform vec4 shallow_color : source_color = vec4(0.12, 0.52, 0.62, 0.78);
uniform vec4 deep_color : source_color = vec4(0.03, 0.16, 0.32, 0.94);
uniform vec4 foam_color : source_color = vec4(0.92, 0.96, 1.0, 0.85);

uniform float wave_speed : hint_range(0.1, 2.0) = 0.5;
uniform float wave_scale : hint_range(0.1, 5.0) = 0.8;
uniform float wave_height : hint_range(0.01, 0.5) = 0.06;
uniform float roughness : hint_range(0.0, 1.0) = 0.08;
uniform float metallic : hint_range(0.0, 1.0) = 0.2;

// Line of sight, as in terrain.gdshader (0 A.D. water shaders multiply by losTex too).
uniform sampler2D los_tex : hint_default_white, filter_linear, repeat_disable;
uniform float los_map_half = 0.0;
// 0.28 water fogs the colour and then multiplies it by the LOS, so unexplored
// water stays black under the haze. RtsoWorldView passes the environment's
// exponential fog (light colour times energy, density; 0 = off).
uniform vec4 scene_fog = vec4(0.0);

// Seabed heights, for the water depth 0 A.D.'s water_high.fs reads from its
// depth buffer: colour murks with depth, the shoreline turns clear and
// breaks into foam. terrain_half = 0 leaves the depth effects off.
uniform sampler2D height_tex : hint_default_black, filter_linear, repeat_disable;
uniform float terrain_half = 0.0;
uniform float water_level = 0.0;
uniform float murkiness : hint_range(0.0, 2.0) = 0.8;

// 0 A.D.'s animated water normals (art/textures/animated/water/lake), two
// frames scrolled against each other at two scales as water_high.fs does
// with normalMap/normalMap2. Mipmapped, so distant water averages out
// instead of the regular highlight grid the old sin/cos ripple drew.
uniform sampler2D wave_normal_a : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D wave_normal_b : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform float wave_tile = 6.0;     // world units per normal-map repeat
uniform float wave_strength = 0.45;

// 0.28's water options (watereffects, waterfancyeffects, waterreflection,
// waterrefraction, waterrealdepth), 0 or 1, set by scenes/menu/user_settings.gd.
global uniform float rtso_water_effects;
global uniform float rtso_water_fancy;
global uniform float rtso_water_reflection;
global uniform float rtso_water_refraction;
global uniform float rtso_water_realdepth;

varying vec3 v_world_pos;

void vertex() {
	v_world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	float t = TIME * wave_speed;
	// Dual-frequency wave vertex displacement
	float w1 = sin(v_world_pos.x * wave_scale + t) * cos(v_world_pos.z * wave_scale * 0.8 + t * 0.9);
	float w2 = sin(v_world_pos.x * wave_scale * 1.7 - t * 1.3) * 0.4;
	VERTEX.y += (w1 + w2) * wave_height * rtso_water_effects; // Water effects off: flat water
}

void fragment() {
	float t = TIME * wave_speed;
	
	// Two scrolling lookups of the wave normals, flat water's tangent frame
	// being world x/z.
	vec2 uv1 = v_world_pos.xz / wave_tile + vec2(t * 0.050, t * 0.030);
	vec2 uv2 = v_world_pos.xz / (wave_tile * 1.7) - vec2(t * 0.035, -t * 0.045);
	vec2 n1 = texture(wave_normal_a, uv1).rg * 2.0 - 1.0;
	vec2 n2 = texture(wave_normal_b, uv2).rg * 2.0 - 1.0;
	vec2 slope = (n1 + n2) * wave_strength * rtso_water_effects;
	float wave_n1 = n1.x * 2.0;
	vec3 perturbed_norm = normalize(vec3(slope.x, 1.0, slope.y));
	NORMAL = (VIEW_MATRIX * vec4(perturbed_norm, 0.0)).xyz;

	// Fresnel effect for realistic surface reflectance
	float fresnel = pow(1.0 - clamp(dot(normalize(NORMAL), normalize(VIEW)), 0.0, 1.0), 4.0);
	// Water reflections off: no mirrored sky.
	float reflect_on = rtso_water_effects * rtso_water_reflection;

	// Water colour by depth (water_high.fs: refraction murks to the tint as
	// the water deepens); without heights, the old 60% blend.
	float depth = 3.0;
	float murk = 0.6;
	// Real water depth (and refraction, which it needs) reads the seabed; without them
	// 0.28 renders water as a plain transparent tint.
	float depth_on = step(0.001, terrain_half) * rtso_water_effects * rtso_water_refraction * rtso_water_realdepth;
	if (depth_on > 0.0) {
		vec2 h_uv = (v_world_pos.xz + vec2(terrain_half)) / (2.0 * terrain_half);
		depth = max(water_level - texture(height_tex, h_uv).r, 0.0);
		// Off the map edge there is no seabed: open water, no shore.
		if (any(lessThan(h_uv, vec2(0.0))) || any(greaterThan(h_uv, vec2(1.0)))) depth = 3.0;
		murk = 1.0 - exp(-depth * murkiness);
	}
	vec4 water_base = mix(shallow_color, deep_color, murk);
	vec3 final_color = mix(water_base.rgb, vec3(0.95, 0.98, 1.0), fresnel * 0.6 * reflect_on);
	// Shore foam (High-quality water effects): bands that roll in over the last half
	// metre of depth.
	float shore = 1.0 - smoothstep(0.0, 0.55, depth);
	float roll = 0.5 + 0.5 * sin(depth * 14.0 - t * 3.0 + wave_n1 * 2.0);
	float foam = shore * mix(0.35, 1.0, roll) * depth_on * rtso_water_fancy;
	final_color = mix(final_color, foam_color.rgb, foam * foam_color.a);
	// The seabed shows through at the shore and fades out with depth.
	water_base.a = mix(water_base.a, clamp(depth * 1.6, 0.25, water_base.a), depth_on);
	water_base.a = max(water_base.a, foam * foam_color.a);

	float los = 1.0;
	if (los_map_half > 0.0) {
		vec2 los_uv = (v_world_pos.xz + vec2(los_map_half)) / (2.0 * los_map_half);
		los = texture(los_tex, los_uv).r;
	}

	ALBEDO = final_color * los;
	FOG = vec4(scene_fog.rgb * los, 1.0 - exp(min(0.0, -length(VERTEX) * scene_fog.a)));
	SPECULAR = 0.85 * los;
	// No sky reflection off water the viewer has not explored.
	METALLIC = metallic * los * reflect_on;
	ROUGHNESS = mix(1.0, roughness, los);
	// Unexplored water goes opaque black instead of showing the seabed.
	ALPHA = mix(mix(water_base.a, 0.98, fresnel * reflect_on), 1.0, 1.0 - los);
}
