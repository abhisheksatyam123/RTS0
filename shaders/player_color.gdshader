// 0 A.D. player_trans materials: model_common.fs colours the skin by the
// owner where its alpha is low, diffuse *= mix(playerColor, 1, tex.a).
// The extracted skins already carry one fixed tint (bake_tint) in that area,
// so it is divided back out before the owner's colour goes in. mask_tex is
// 1 - alpha from 0 A.D.'s skin (tools/extract_player_masks.py).
// GPL-2.0-or-later, derived from 0 A.D. (Copyright (C) Wildfire Games).
shader_type spatial;
render_mode blend_mix, depth_draw_opaque, cull_disabled, diffuse_burley, specular_schlick_ggx;

uniform sampler2D albedo_tex : source_color, filter_linear, hint_default_white;
uniform sampler2D mask_tex : filter_linear, hint_default_black;
uniform vec4 albedo : source_color = vec4(1.0);
uniform vec3 bake_tint = vec3(1.0);
uniform bool srgb_vertex_color = false;
uniform float roughness = 1.0;
uniform float metallic = 0.0;
uniform float specular = 0.5;
// Close-up units and buildings set this per node; crowd MultiMeshes pass the
// owner's colour in INSTANCE_CUSTOM (alpha 1).
instance uniform vec4 player_color : source_color = vec4(1.0);

varying vec3 v_player;

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
}
