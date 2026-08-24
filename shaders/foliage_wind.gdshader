// foliage wind — extends unit_thronefall for trees
shader_type spatial;
render_mode blend_mix, depth_draw_opaque, cull_back, diffuse_lambert;

uniform vec3 trunk_col : source_color = vec3(0.42, 0.28, 0.18);
uniform vec3 leaves_col : source_color = vec3(0.18, 0.55, 0.22);
uniform float wind_strength : hint_range(0,1) = 0.6;
uniform float wind_speed : hint_range(0,3) = 0.9;

void vertex() {
    float h = clamp(VERTEX.y / 1.8, 0.0, 1.0);
    float id = float(INSTANCE_ID) * 0.37;
    float sway = sin(TIME * wind_speed + id) * wind_strength * 0.08 * h;
    float sway2 = cos(TIME * wind_speed * 0.7 + id * 1.3) * wind_strength * 0.04 * h;
    VERTEX.x += sway;
    VERTEX.z += sway2;
    // slight squash on wind gust
    VERTEX.y -= abs(sway) * 0.05;
}

void fragment() {
    // trunk vs leaves via UV.y (cylinder: trunk <0.5)
    bool isLeaves = UV.y > 0.45;
    vec3 base = isLeaves ? leaves_col : trunk_col;
    float ndl = dot(NORMAL, normalize(vec3(0.3, 0.85, 0.2)));
    float ramp = floor(clamp(ndl,0.0,1.0) * 3.0) / 3.0;
    ramp = mix(0.7, 1.0, ramp);
    ALBEDO = base * ramp;
    ROUGHNESS = isLeaves ? 0.85 : 0.9;
}
