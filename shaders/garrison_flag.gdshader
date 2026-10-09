// 0 A.D. garrison flag (art/actors/props/special/common/garrison_flag_*.xml):
// player_trans material (player colour where the skin is transparent), with the
// cloth waving like garrison_flag_idle.dae. The pole stands at x = 0 and the
// cloth flies out to x = -2 in the mesh.
shader_type spatial;
render_mode cull_disabled;

uniform sampler2D albedo_tex : source_color, filter_linear_mipmap;
instance uniform vec4 player_color : source_color = vec4(1.0);

void vertex() {
	float along = clamp(-VERTEX.x * 0.5, 0.0, 1.0);
	float ph = TIME * 4.0 + VERTEX.x * 2.2 + NODE_POSITION_WORLD.x;
	VERTEX.z += sin(ph) * 0.18 * along;
	VERTEX.x += cos(ph) * 0.05 * along;
}

void fragment() {
	vec4 t = texture(albedo_tex, UV);
	ALBEDO = mix(player_color.rgb, t.rgb, t.a);
	ROUGHNESS = 0.8;
}
