// Trees and other flora swaying in the wind: 0 A.D. shaders/glsl/model_common.vs
// (USE_WIND, used by the basic_trans_wind_* materials). GPL-2.0-or-later,
// derived from 0 A.D. (Copyright (C) Wildfire Games).
shader_type spatial;
render_mode blend_mix, depth_draw_opaque, cull_disabled, diffuse_burley, specular_schlick_ggx;

uniform sampler2D albedo_tex : source_color, filter_linear, hint_default_white;
uniform vec4 albedo : source_color = vec4(1.0);
uniform bool srgb_vertex_color = false;
uniform float alpha_scissor = 0.5;
// windData: 0 A.D. derives it from the map's wind angle and speed.
uniform vec2 wind = vec2(0.85, 0.5);

// model_common.vs fakeCos: a smoothed triangle wave, cheaper than cos().
vec4 fake_cos(vec4 x) {
	vec4 tri = abs(fract(x + 0.5) * 2.0 - 1.0);
	return tri * tri * (3.0 - 2.0 * tri);
}

void vertex() {
	if (srgb_vertex_color) {
		vec3 c = COLOR.rgb;
		COLOR.rgb = mix(pow((c + vec3(0.055)) * (1.0 / 1.055), vec3(2.4)), c * (1.0 / 12.92), lessThan(c, vec3(0.04045)));
	}
	vec3 origin = MODEL_MATRIX[3].xyz;
	vec3 model_pos = clamp(fract(origin), vec3(0.4), vec3(1.0));
	float abs_wind = abs(wind.x) + abs(wind.y);
	vec3 world = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	vec4 cv = fake_cos(vec4(
		TIME * model_pos.x + world.x,
		TIME * model_pos.z / 3.0 + origin.x,
		TIME * abs_wind / 4.0 + world.z,
		0.0));
	// Long branches flutter, high parts sway more.
	float limit = clamp((VERTEX.x * VERTEX.z * VERTEX.y) / 3000.0, 0.0, 0.2);
	float diff = cv.x * limit;
	float diff2 = cv.y * clamp(VERTEX.y / 60.0, 0.0, 0.25);
	VERTEX += vec3(cv.z * limit * clamp(abs_wind, 1.2, 1.7));
	VERTEX.xz += diff + diff2 * wind;
}

void fragment() {
	vec4 tex = texture(albedo_tex, UV) * albedo;
	ALBEDO = tex.rgb * COLOR.rgb;
	ALPHA = tex.a;
	ALPHA_SCISSOR_THRESHOLD = alpha_scissor;
	ROUGHNESS = 0.9;
}
