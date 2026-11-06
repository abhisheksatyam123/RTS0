// foliage wind -- extends unit_thronefall for trees
shader_type spatial;
global uniform float rtso_pixel_art;
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
    // Pixel-art look (Options: pixelart, set by UserSettings): the same three light bands, now
    // in palette steps with a touch more colour, so trees read as flat blocks like the units.
    if (rtso_pixel_art > 0.5) {
        float lum = dot(ALBEDO, vec3(0.2126, 0.7152, 0.0722));
        vec3 c = max(mix(vec3(lum), ALBEDO, 1.2), vec3(0.0));
        ALBEDO = pow(floor(pow(c, vec3(1.0 / 2.2)) * 14.0 + 0.5) / 14.0, vec3(2.2));
    }
    ROUGHNESS = isLeaves ? 0.85 : 0.9;
}
