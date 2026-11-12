// Pixel-art unit outline: an inverted hull. The crowd MultiMesh is drawn a second time
// through this shader with front faces culled, pushed out along its normals, so only a
// dark rim shows around each unit. The node holding it is hidden unless the Options
// "pixelart" toggle is on (WorldView sets its visibility), so this costs nothing in the
// default look. Shadows and the sun ignore it.
// GPL-2.0-or-later, written for RTS0.
shader_type spatial;
render_mode blend_mix, cull_front, depth_draw_opaque, unshaded, shadows_disabled;

// Heavier than the model's own edges so a unit reads against tan grass and dark trees.
uniform vec4 outline_color : source_color = vec4(0.05, 0.035, 0.025, 1.0);
// Model-space push along the normals. Not measured against a device yet; tune on a pixel-mode capture.
uniform float outline_width = 0.045;

void vertex() {
	VERTEX += NORMAL * outline_width;
}

void fragment() {
	ALBEDO = outline_color.rgb;
	ALPHA = outline_color.a;
}
