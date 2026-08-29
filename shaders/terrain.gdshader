// 0 A.D. & RTS0 Vibrant Mediterranean Terrain Shader
shader_type spatial;
render_mode blend_mix, depth_draw_opaque, cull_back, diffuse_lambert;

uniform sampler2D noise_tex : hint_default_black;
uniform sampler2D grass_tex : hint_default_white;
uniform sampler2D dirt_tex : hint_default_white;
uniform sampler2D rock_tex : hint_default_white;
uniform vec4 grass_col : source_color = vec4(0.48, 0.72, 0.32, 1.0);
uniform vec4 dirt_col : source_color = vec4(0.76, 0.68, 0.52, 1.0);
uniform vec4 rock_col : source_color = vec4(0.62, 0.62, 0.60, 1.0);
uniform vec3 territory_player : source_color = vec3(0.36, 0.62, 0.94);
uniform vec3 territory_enemy : source_color = vec3(0.92, 0.32, 0.28);
uniform float territory_strength : hint_range(0,1) = 0.08;
uniform sampler2D territory_map : hint_default_black;

varying vec2 v_uv;
varying vec3 v_pos;
varying vec3 v_normal;
varying float v_height;

void vertex() {
    v_uv = UV * 16.0;
    v_pos = VERTEX;
    v_normal = NORMAL;
    v_height = VERTEX.y;
}

void fragment() {
    vec2 uv = v_uv;
    vec3 norm = normalize(v_normal);

    // Multi-octave noise for organic biome transitions
    float n = texture(noise_tex, uv * 0.06).r;
    float n2 = texture(noise_tex, uv * 0.12 + vec2(3.7, 1.9)).r;
    float noise_splat = n * 0.55 + n2 * 0.45;

    // 1. Surface normal slope steepness:
    // norm.y == 1.0 is flat horizontal terrain, norm.y < 0.75 is steep slope / cliff
    float slope = 1.0 - clamp(norm.y, 0.0, 1.0);
    float slope_rock = smoothstep(0.20, 0.48, slope);

    // 2. Terrain elevation (height) splatting:
    // Lowlands: rich grass -> Mid-slopes: dirt/paths -> High peaks: rock/cliffs
    float effective_height = v_height + (noise_splat - 0.5) * 4.0;
    float height_dirt = smoothstep(1.5, 4.5, effective_height);
    float height_rock = smoothstep(6.0, 12.0, effective_height);

    // Combine slope and elevation for rock layer
    float rock_weight = clamp(max(slope_rock, height_rock), 0.0, 1.0);

    // Dirt layer dominates mid-elevations or noise paths where rock is not dominant
    float dirt_mix = clamp(max(height_dirt, smoothstep(0.42, 0.65, noise_splat)), 0.0, 1.0);
    float dirt_weight = dirt_mix * (1.0 - rock_weight);

    // Grass layer fills the lowlands and gentle slopes
    float grass_weight = clamp(1.0 - rock_weight - dirt_weight, 0.0, 1.0);

    // 3. Multi-texture sampling
    vec3 grass = texture(grass_tex, uv * 1.5).rgb * grass_col.rgb;
    vec3 dirt  = texture(dirt_tex,  uv * 1.5).rgb * dirt_col.rgb;
    vec3 rock  = texture(rock_tex,  uv * 1.5).rgb * rock_col.rgb;

    // Splatted composite albedo
    vec3 col = grass * grass_weight + dirt * dirt_weight + rock * rock_weight;

    // Subtle grid guide
    float grid = smoothstep(0.98, 1.0, max(fract(uv.x), fract(uv.y)));
    col = mix(col, col * 0.94, grid * 0.15);

    ALBEDO = col;
    ROUGHNESS = mix(0.82, 0.68, rock_weight);
    SPECULAR = mix(0.14, 0.24, rock_weight);
}
