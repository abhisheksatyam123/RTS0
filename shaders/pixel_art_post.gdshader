// 3D pixel-art outline pass: depth outlines (outer silhouettes) and single-pixel normal
// highlights (inner creases). Runs on a full-screen quad that is a child of the
// camera inside the low-res SubViewport. Forward+ / Mobile renderers only: the normal
// texture needs hint_normal_roughness_texture (GL Compatibility uses
// pixel_art_post_depth.gdshader instead).
//
// Depth tests are relative (a fraction of the pixel's own distance), not metres. A metre
// threshold is a fine line up close and a wide one far away: at 100 m, 0.3 m is 0.3% of the
// distance, which the gentle depth steps of distant ground exceed, so the ground got dark
// halos. Relative gaps keep the same line weight at every zoom.
shader_type spatial;
render_mode unshaded, blend_mix, depth_draw_never, depth_test_disabled, cull_disabled;

uniform sampler2D screen_texture : hint_screen_texture, filter_nearest;
uniform sampler2D depth_texture : hint_depth_texture, filter_nearest;
uniform sampler2D normal_roughness_texture : hint_normal_roughness_texture, filter_nearest;

uniform float brighter_line : hint_range(0.0, 1.0) = 0.25; // inner highlight strength
uniform float darker_line : hint_range(0.0, 1.0) = 0.5;    // outer outline darkness
// Outline ink, multiplied into the darkened frame colour. White keeps the scene's own
// colours (team colours and lighting survive inside the ink).
uniform vec3 outline_tint = vec3(1.0, 1.0, 1.0);
// Relative depth gap (fraction of the pixel's distance) that draws an outer outline.
// The line fades in over [0.5, 1.5] x this value, so sub-pixel camera motion moves the
// edge smoothly instead of switching pixels on and off (shimmer).
uniform float depth_threshold : hint_range(0.0, 0.2) = 0.03;
uniform float normal_threshold : hint_range(0.0, 1.0) = 0.2;
// Beyond this distance (metres) no outline is drawn: flat distant ground otherwise
// turns dark from depth noise.
uniform float max_outline_distance = 400.0;

void vertex() {
	// Lock the quad to the screen corners in clip space.
	POSITION = vec4(VERTEX.xy, 1.0, 1.0);
}

// Linear eye-space depth (positive, metres) at a screen UV.
// INV_PROJECTION_MATRIX and PROJECTION_MATRIX are not available to fragment() in Godot 4.3
// (the shader failed to compile, which left the quad washed out). The camera's planes come in
// as uniforms instead. Godot's forward renderers store reversed depth (1 at the near plane).
uniform float cam_near = 0.05;
uniform float cam_far = 4000.0;
float linear_depth(vec2 uv) {
	float d = texture(depth_texture, uv).r;
	return cam_near * cam_far / (cam_near + d * (cam_far - cam_near));
}

vec3 decode_normal(vec2 uv) {
	return normalize(texture(normal_roughness_texture, uv).xyz * 2.0 - 1.0);
}

void fragment() {
	vec2 texel = 1.0 / VIEWPORT_SIZE;
	vec2 offsets[4] = vec2[4](vec2(-1.0, 0.0), vec2(1.0, 0.0), vec2(0.0, -1.0), vec2(0.0, 1.0));

	float center_depth = max(linear_depth(SCREEN_UV), 0.001);
	// No outline beyond max_outline_distance (fragment() may not return early in Godot 4.3).
	float in_range = step(center_depth, max_outline_distance);
	vec3 center_normal = decode_normal(SCREEN_UV);

	// Outer silhouettes: neighbours much farther away than the centre (relative gap, 0..1).
	float depth_diff = 0.0;
	// Background side: neighbours nearer than the centre (relative gap, 0..1). Suppresses
	// the inner highlight on the far side of an outline, so the two never double up.
	float near_diff = 0.0;
	// Inner creases: neighbour normals differ while depths stay close.
	float normal_diff = 0.0;

	for (int i = 0; i < 4; i++) {
		vec2 uv = SCREEN_UV + offsets[i] * texel;
		float neighbour_depth = linear_depth(uv);
		vec3 neighbour_normal = decode_normal(uv);
		float gap = (neighbour_depth - center_depth) / center_depth;

		depth_diff += clamp(gap, 0.0, 1.0);
		near_diff += clamp(-gap, 0.0, 1.0);

		float depth_close = 1.0 - smoothstep(0.0, 0.05 * max(center_depth, 1.0), abs(neighbour_depth - center_depth));
		normal_diff += depth_close * smoothstep(normal_threshold, 1.0, 1.0 - dot(center_normal, neighbour_normal));
	}

	float outer = smoothstep(depth_threshold * 0.5, depth_threshold * 1.5, depth_diff);
	// Suppress the normal highlight wherever an outer outline already sits (no double lines).
	float mask = smoothstep(depth_threshold * 0.5, depth_threshold * 1.5, near_diff);
	float inner = clamp(clamp(normal_diff, 0.0, 1.0) - mask, 0.0, 1.0);
	inner = clamp(inner - outer, 0.0, 1.0) * in_range;
	outer *= in_range;

	// Blend only where a line is drawn (alpha 0 elsewhere): pixels without an outline keep
	// whatever is already on screen, so transparent overlays (selection and placement
	// footprints, range rings) and the colours of the scene survive the pass.
	vec3 base = texture(screen_texture, SCREEN_UV).rgb;
	vec3 dark = base * (1.0 - darker_line) * outline_tint;
	vec3 light = mix(base, vec3(1.0), brighter_line);
	// Full alpha on a line: darker_line and brighter_line set the colour change itself.
	ALBEDO = outer >= inner ? dark : light;
	ALPHA = max(outer, inner);
}
