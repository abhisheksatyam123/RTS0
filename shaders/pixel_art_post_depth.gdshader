// 3D pixel-art fallback for the GL Compatibility renderer, where the normal-roughness
// texture is not available: depth-only outer outlines, no inner normal highlights.
// The depth test is relative to each pixel's distance, as in pixel_art_post.gdshader.
shader_type spatial;
render_mode unshaded, blend_mix, depth_draw_never, depth_test_disabled, cull_disabled;

uniform sampler2D screen_texture : hint_screen_texture, filter_nearest;
uniform sampler2D depth_texture : hint_depth_texture, filter_nearest;

// The camera's planes (the fragment stage has no projection matrix in Godot 4.3).
uniform float cam_near = 0.05;
uniform float cam_far = 4000.0;
uniform float darker_line : hint_range(0.0, 1.0) = 0.5;
// Outline ink, multiplied into the darkened frame colour (white keeps the scene's colours).
uniform vec3 outline_tint = vec3(1.0, 1.0, 1.0);
// Relative depth gap that draws an outline; fades in over [0.5, 1.5] x this value.
uniform float depth_threshold : hint_range(0.0, 0.2) = 0.03;
// Beyond this distance (metres) no outline is drawn, as in the full shader.
uniform float max_outline_distance = 400.0;

void vertex() {
	POSITION = vec4(VERTEX.xy, 1.0, 1.0);
}

// Reversed depth (1 at the near plane), as Godot's forward renderers store it.
// Takes the sampled depth, not the sampler: Godot's generators declare the hint texture
// after user functions, so sampling inside a helper fails to compile.
float linear_depth(float d) {
	return cam_near * cam_far / (cam_near + d * (cam_far - cam_near));
}

void fragment() {
	vec2 texel = 1.0 / VIEWPORT_SIZE;
	vec2 offsets[4] = vec2[4](vec2(-1.0, 0.0), vec2(1.0, 0.0), vec2(0.0, -1.0), vec2(0.0, 1.0));

	float center_depth = max(linear_depth(texture(depth_texture, SCREEN_UV).r), 0.001);
	// No outline beyond max_outline_distance (no early return in fragment() in Godot 4.3).
	float in_range = step(center_depth, max_outline_distance);
	float depth_diff = 0.0;
	for (int i = 0; i < 4; i++) {
		float gap = (linear_depth(texture(depth_texture, SCREEN_UV + offsets[i] * texel).r) - center_depth) / center_depth;
		depth_diff += clamp(gap, 0.0, 1.0);
	}
	float outer = smoothstep(depth_threshold * 0.5, depth_threshold * 1.5, depth_diff) * in_range;

	// Alpha-blended like the full shader: only outline pixels change.
	vec3 base = texture(screen_texture, SCREEN_UV).rgb;
	ALBEDO = base * (1.0 - darker_line) * outline_tint;
	ALPHA = outer;
}
