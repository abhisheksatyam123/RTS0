// 0 A.D.'s unit silhouettes (the "silhouettes" option, Renderer.cpp
// RenderSilhouettes): where a unit is hidden behind a structure or the terrain,
// its outline is drawn flat in its owner's colour. Drawn as the unit's material
// overlay: a fragment the scene's depth puts behind something else by more than
// `min_gap` (so units in a crowd don't outline each other) takes the colour,
// every other fragment is discarded.
// GPL-2.0-or-later, derived from 0 A.D. (Copyright (C) Wildfire Games).
shader_type spatial;
render_mode unshaded, blend_mix, depth_test_disabled, depth_draw_never, cull_back, shadows_disabled, fog_disabled;

uniform sampler2D depth_tex : hint_depth_texture, filter_nearest;
uniform float min_gap = 0.75;
// Close-up units set this per node; crowd MultiMeshes pass the owner's colour
// in INSTANCE_CUSTOM (alpha 1), as player_color.gdshader reads it.
instance uniform vec4 silhouette_color : source_color = vec4(1.0);

varying vec3 v_color;

void vertex() {
	v_color = INSTANCE_CUSTOM.a > 0.5 ? INSTANCE_CUSTOM.rgb : silhouette_color.rgb;
}

void fragment() {
	float depth = textureLod(depth_tex, SCREEN_UV, 0.0).r;
	vec4 view = INV_PROJECTION_MATRIX * vec4(SCREEN_UV * 2.0 - 1.0, depth, 1.0);
	float scene_dist = -view.z / view.w;
	if (-VERTEX.z - scene_dist < min_gap) discard;
	ALBEDO = v_color;
	// effects/model.xml MODE_SILHOUETTEDISPLAY: blended at constant alpha 191 / 255.
	// 0 A.D.'s stencil keeps each pixel to one silhouette; Godot 4.3 spatial shaders
	// have none, so a model's own overlapping parts can blend twice.
	ALPHA = 191.0 / 255.0;
}
