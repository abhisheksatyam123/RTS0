// 3D pixel-art fallback for the GL Compatibility renderer, where the normal-roughness
// texture is not available: depth-only outer outlines, no inner normal highlights.
shader_type spatial;
render_mode unshaded, blend_mix, depth_draw_never, depth_test_disabled, cull_disabled;

uniform sampler2D screen_texture : hint_screen_texture, filter_nearest;
uniform sampler2D depth_texture : hint_depth_texture, filter_nearest;

// The camera's planes (the fragment stage has no projection matrix in Godot 4.3).
uniform float cam_near = 0.05;
uniform float cam_far = 4000.0;
uniform float darker_line : hint_range(0.0, 1.0) = 0.5;
uniform float depth_threshold : hint_range(0.0, 1.0) = 0.3;
// Beyond this distance (metres) no outline is drawn, as in the full shader.
uniform float max_outline_distance = 400.0;

void vertex() {
	POSITION = vec4(VERTEX.xy, 1.0, 1.0);
}

// Reversed depth (1 at the near plane), as Godot's forward renderers store it.
float linear_depth(vec2 uv) {
	float d = texture(depth_texture, uv).r;
	return cam_near * cam_far / (cam_near + d * (cam_far - cam_near));
}

void fragment() {
	vec2 texel = 1.0 / VIEWPORT_SIZE;
	vec2 offsets[4] = vec2[4](vec2(-1.0, 0.0), vec2(1.0, 0.0), vec2(0.0, -1.0), vec2(0.0, 1.0));

	float center_depth = linear_depth(SCREEN_UV);
	// No outline beyond max_outline_distance (no early return in fragment() in Godot 4.3).
	float in_range = step(center_depth, max_outline_distance);
	float depth_diff = 0.0;
	for (int i = 0; i < 4; i++) {
		depth_diff += clamp(linear_depth(SCREEN_UV + offsets[i] * texel) - center_depth, 0.0, 1.0);
	}
	float outer = smoothstep(depth_threshold, depth_threshold + 0.1, depth_diff) * in_range;

	vec3 color = texture(screen_texture, SCREEN_UV).rgb;
	color -= color * outer * darker_line;
	ALBEDO = clamp(color, 0.0, 1.0);
}
