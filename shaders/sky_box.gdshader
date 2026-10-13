shader_type sky;

// 0 A.D.'s sky box (SkyManager): five faces of art/textures/skies/<SkySet>,
// written by tools/extract_sky_boxes.py. Turning right the faces run
// left -> back -> right -> front; here front faces -Z and left faces +X
// before sky_yaw turns the box so the "left" face, the brightest part of each
// set, lies toward the sun. Below the horizon the faces fade into the map's
// fog colour, which is all the camera sees past the map edge.

uniform sampler2D face_front : source_color, filter_linear, repeat_disable;
uniform sampler2D face_back : source_color, filter_linear, repeat_disable;
uniform sampler2D face_left : source_color, filter_linear, repeat_disable;
uniform sampler2D face_right : source_color, filter_linear, repeat_disable;
uniform sampler2D face_top : source_color, filter_linear, repeat_disable;
uniform float sky_yaw = 0.0;
uniform float energy = 1.0;
uniform vec3 ground_color : source_color = vec3(0.8, 0.85, 0.9);

void sky() {
	float c = cos(sky_yaw), s = sin(sky_yaw);
	vec3 d = vec3(EYEDIR.x * c + EYEDIR.z * s, EYEDIR.y, -EYEDIR.x * s + EYEDIR.z * c);
	vec3 a = abs(d);
	vec3 col;
	if (d.y > max(a.x, a.z)) {
		col = texture(face_top, vec2(0.5 + 0.5 * d.x / d.y, 0.5 - 0.5 * d.z / d.y)).rgb;
	} else if (a.z >= a.x) {
		float v = clamp(0.5 - 0.5 * d.y / a.z, 0.0, 1.0);
		col = d.z < 0.0
				? texture(face_front, vec2(0.5 + 0.5 * d.x / a.z, v)).rgb
				: texture(face_back, vec2(0.5 - 0.5 * d.x / a.z, v)).rgb;
	} else {
		float v = clamp(0.5 - 0.5 * d.y / a.x, 0.0, 1.0);
		col = d.x > 0.0
				? texture(face_left, vec2(0.5 + 0.5 * d.z / a.x, v)).rgb
				: texture(face_right, vec2(0.5 - 0.5 * d.z / a.x, v)).rgb;
	}
	col = mix(col, ground_color, smoothstep(0.0, 0.12, -EYEDIR.y));
	COLOR = col * energy;
}
