// 0 A.D.'s model_common shading for player_trans materials and opaque skins
// with a specular map. model_common.fs colours the skin by the owner where its
// alpha is low, diffuse *= mix(playerColor, 1, tex.a).
// The extracted skins already carry one fixed tint (bake_tint) in that area,
// so it is divided back out before the owner's colour goes in. mask_tex is
// 1 - alpha from 0 A.D.'s skin (tools/extract_player_masks.py).
// GPL-2.0-or-later, derived from 0 A.D. (Copyright (C) Wildfire Games).
shader_type spatial;
render_mode blend_mix, depth_draw_opaque, cull_disabled;

uniform sampler2D albedo_tex : source_color, filter_linear, hint_default_white;
uniform sampler2D mask_tex : filter_linear, hint_default_black;
uniform vec4 albedo : source_color = vec4(1.0);
uniform vec3 bake_tint = vec3(1.0);
uniform bool srgb_vertex_color = false;
uniform float roughness = 1.0;
uniform float metallic = 0.0;
uniform float specular = 0.5;
// 0 A.D.'s normTex for this skin (tools/extract_normal_maps.py), when its
// actor's material is normal mapped.
uniform bool use_normal_map = false;
uniform sampler2D normal_tex : hint_normal, filter_linear;
// 0 A.D.'s specTex (tools/extract_specular_maps.py) when its actor's material
// has a specular map: brightness in red, specPow / 255 in green.
uniform bool use_specular_map = false;
uniform sampler2D specular_tex : filter_linear, hint_default_black;
// Close-up units and buildings set this per node; crowd MultiMeshes pass the
// owner's colour in INSTANCE_CUSTOM (alpha 1).
instance uniform vec4 player_color : source_color = vec4(1.0);

varying vec3 v_player;
varying float v_spec;
varying float v_spec_pow;

void vertex() {
	if (srgb_vertex_color) {
		vec3 c = COLOR.rgb;
		COLOR.rgb = mix(pow((c + vec3(0.055)) * (1.0 / 1.055), vec3(2.4)), c * (1.0 / 12.92), lessThan(c, vec3(0.04045)));
	}
	v_player = INSTANCE_CUSTOM.a > 0.5 ? INSTANCE_CUSTOM.rgb : player_color.rgb;
}

void fragment() {
	vec4 tex = texture(albedo_tex, UV) * albedo;
	float m = texture(mask_tex, UV).r;
	vec3 baked = mix(vec3(1.0), bake_tint, m);
	vec3 owner = mix(vec3(1.0), v_player, m);
	ALBEDO = tex.rgb * owner / max(baked, vec3(0.05)) * COLOR.rgb;
	ROUGHNESS = roughness;
	METALLIC = metallic;
	SPECULAR = specular;
	v_spec = 0.0;
	v_spec_pow = 1.0;
	if (use_specular_map) {
		vec2 s = texture(specular_tex, UV).rg;
		v_spec = s.r;
		v_spec_pow = max(s.g * 255.0, 1.0);
	}
	if (use_normal_map) {
		NORMAL_MAP = texture(normal_tex, UV).rgb;
	}
}

// model_common.fs: Lambert sun diffuse, plus with a specular map the
// Blinn-Phong sunColor * specCol * pow(N.H, specPow) of calculateSpecular.
// LIGHT_COLOR carries a factor of PI.
void light() {
	DIFFUSE_LIGHT += max(dot(NORMAL, LIGHT), 0.0) * ATTENUATION * LIGHT_COLOR / PI;
	if (v_spec > 0.0) {
		float nh = max(dot(NORMAL, normalize(LIGHT + VIEW)), 0.0);
		SPECULAR_LIGHT += v_spec * pow(nh, v_spec_pow) * ATTENUATION * LIGHT_COLOR / PI;
	}
}
