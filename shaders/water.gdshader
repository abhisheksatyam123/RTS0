shader_type spatial;
render_mode specular_schlick_ggx, cull_back;

uniform vec4 shallow_color : source_color = vec4(0.1, 0.45, 0.65, 0.85);
uniform vec4 deep_color : source_color = vec4(0.02, 0.15, 0.35, 0.95);
uniform vec4 foam_color : source_color = vec4(0.85, 0.95, 1.0, 1.0);
uniform float wave_speed : hint_range(0.1, 2.0) = 0.6;
uniform float wave_scale : hint_range(0.5, 10.0) = 2.5;
uniform float wave_height : hint_range(0.01, 0.5) = 0.08;
uniform float roughness : hint_range(0.0, 1.0) = 0.12;

void vertex() {
	vec3 world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	float w1 = sin(world_pos.x * wave_scale * 0.5 + TIME * wave_speed) * cos(world_pos.z * wave_scale * 0.5 + TIME * wave_speed);
	float w2 = sin(world_pos.x * wave_scale * 0.8 - TIME * wave_speed * 1.2 + 1.5) * 0.5;
	VERTEX.y += (w1 + w2) * wave_height;
}

void fragment() {
	float fresnel = pow(1.0 - clamp(dot(NORMAL, VIEW), 0.0, 1.0), 3.5);
	vec4 water_col = mix(shallow_color, deep_color, 0.5);
	vec3 final_color = mix(water_col.rgb, vec3(1.0), fresnel * 0.4);

	ALBEDO = final_color;
	METALLIC = 0.15;
	ROUGHNESS = roughness;
	SPECULAR = 0.65;
	ALPHA = water_col.a;
}
