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
uniform bool use_vertex_color = false;
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
// 0 A.D.'s *_parallax* materials (model_common.fs USE_PARALLAX): the height in
// normTex's alpha (tools/extract_normal_maps.py --heights), and effectSettings.z.
uniform bool use_parallax = false;
uniform sampler2D height_tex : filter_linear, hint_default_white;
uniform float parallax_scale = 0.0075;
// Close-up units and buildings set this per node; crowd MultiMeshes pass the
// owner's colour in INSTANCE_CUSTOM (alpha 1 + variant_u).
instance uniform vec4 player_color : source_color = vec4(1.0);
// 0 A.D. picks each unit's body, head, helmet and shield skins by frequency from
// their actors' skin-only variant groups (tools/extract_unit_variants.py).
// variant_tex holds the skins as 0 A.D. draws them, the player-colour mask in
// alpha (0 for skins without player colour); variant_cut the cumulative
// frequency where each layer after the first starts (2.0 past the last).
uniform bool use_variants = false;
uniform sampler2DArray variant_tex : source_color, filter_linear, hint_default_white;
uniform vec4 variant_cut[2];
// Offsets the entity's draw per image, so its head, helmet and shield pick apart.
uniform float variant_salt = 0.0;
// The entity's draw in [0, 1): crowds pass it in INSTANCE_CUSTOM.a - 1.
instance uniform float variant_u = 0.0;
// A head's objectcolor material (model_common.fs USE_OBJECTCOLOR): its hair, the
// layer's alpha, takes one of the actor's colours (linear), drawn by object_cut.
uniform bool use_object_color = false;
uniform vec4 object_colors[8];
uniform vec4 object_cut[2];
// A crowd's one surface for all of a unit's parts (ModelLibrary crowd_parts):
// part_tex holds each part's skins as variant_tex does, UV2 = (first layer, part),
// and each part draws by its own cut and salt (2.0 cuts: one layer). obj_part is
// the part the object colours tint.
uniform bool use_parts = false;
uniform sampler2DArray part_tex : source_color, filter_linear_mipmap, hint_default_white;
uniform vec4 part_cut[16];
uniform float part_salt[8];
uniform int obj_part = -1;

varying vec3 v_player;
varying float v_variant;
varying flat vec2 v_part;
varying float v_spec;
varying float v_spec_pow;

void vertex() {
	if (srgb_vertex_color) {
		vec3 c = COLOR.rgb;
		COLOR.rgb = mix(pow((c + vec3(0.055)) * (1.0 / 1.055), vec3(2.4)), c * (1.0 / 12.92), lessThan(c, vec3(0.04045)));
	}
	v_player = INSTANCE_CUSTOM.a > 0.5 ? INSTANCE_CUSTOM.rgb : player_color.rgb;
	v_variant = INSTANCE_CUSTOM.a > 0.5 ? INSTANCE_CUSTOM.a - 1.0 : variant_u;
	v_part = UV2;
}

// model_common.fs USE_PARALLAX: step along the eye ray in tangent space until it
// falls below the height, then interpolate back to the crossing. Up to 20 steps,
// fewer with distance and none beyond 250 m (RTS0 units are 4 m).
vec2 parallax_uv(vec2 coord, vec3 vertex, vec3 tangent, vec3 binormal, vec3 normal) {
	vec3 eye_dir = normalize(-vertex) * mat3(tangent, -binormal, normal);
	float dist = length(vertex) * 4.0;
	int iter = int(min(20.0, 25.0 - dist / 10.0));
	if (iter <= 0) return coord;
	float h = texture(height_tex, coord).r;
	float height = 1.0;
	float s = 1.0 / float(iter);
	float t = s;
	vec2 move = -eye_dir.xy * parallax_scale / (max(eye_dir.z, 0.1) * float(iter));
	for (int i = 0; i < iter; ++i) {
		height -= t;
		bool below = h < height;
		t = below ? s : 0.0;
		coord += below ? move : vec2(0.0);
		h = texture(height_tex, coord).r;
	}
	float hp = texture(height_tex, coord - move).r;
	coord -= move * ((h - height) / (s + h - hp));
	return coord;
}

void fragment() {
	vec2 uv = use_parallax ? parallax_uv(UV, VERTEX, TANGENT, BINORMAL, NORMAL) : UV;
	vec4 tex = texture(albedo_tex, uv) * albedo;
	float m = texture(mask_tex, uv).r;
	vec3 baked = mix(vec3(1.0), bake_tint, m);
	if (use_variants) {
		float layer = 0.0;
		float u = fract(v_variant + variant_salt);
		for (int i = 0; i < 8; ++i)
			layer += step(variant_cut[i / 4][i % 4], u);
		vec4 vt = texture(variant_tex, vec3(uv, layer));
		tex = vec4(vt.rgb, 1.0) * albedo;
		m = vt.a;
		baked = vec3(1.0);
	}
	float salt = variant_salt;
	bool obj_here = use_object_color;
	if (use_parts) {
		int p = int(v_part.y + 0.5);
		salt = part_salt[p];
		float u = fract(v_variant + salt);
		float layer = v_part.x;
		for (int i = 0; i < 8; ++i)
			layer += step(part_cut[p * 2 + i / 4][i % 4], u);
		vec4 vt = texture(part_tex, vec3(uv, layer));
		tex = vec4(vt.rgb, 1.0) * albedo;
		m = vt.a;
		baked = vec3(1.0);
		obj_here = use_object_color && p == obj_part;
	}
	vec3 tint = v_player;
	if (obj_here) {
		// Drawn apart from the skin: 0 A.D. picks each group of the actor by itself.
		float u = fract(v_variant * 7.0 + salt + 0.5);
		int pick = 0;
		for (int i = 0; i < 8; ++i)
			pick += int(step(object_cut[i / 4][i % 4], u));
		tint = object_colors[pick].rgb;
	}
	vec3 owner = mix(vec3(1.0), tint, m);
	ALBEDO = tex.rgb * owner / max(baked, vec3(0.05)) * (use_vertex_color ? COLOR.rgb : vec3(1.0));
	ROUGHNESS = roughness;
	METALLIC = metallic;
	SPECULAR = specular;
	v_spec = 0.0;
	v_spec_pow = 1.0;
	if (use_specular_map) {
		vec2 s = texture(specular_tex, uv).rg;
		v_spec = s.r;
		v_spec_pow = max(s.g * 255.0, 1.0);
	}
	if (use_normal_map) {
		NORMAL_MAP = texture(normal_tex, uv).rgb;
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
