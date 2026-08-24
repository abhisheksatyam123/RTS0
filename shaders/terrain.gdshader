// Thronefall terrain — 3-layer splat + territory tint + LOS fog
// Use on PlaneMesh ground. Expects splat via UV2, territory via custom.
shader_type spatial;
render_mode blend_mix, depth_draw_opaque, cull_back, diffuse_lambert;

uniform sampler2D noise_tex : hint_default_black;
uniform vec4 grass_col : source_color = vec4(0.55, 0.72, 0.42, 1);
uniform vec4 dirt_col : source_color = vec4(0.62, 0.55, 0.45, 1);
uniform vec4 rock_col : source_color = vec4(0.58, 0.59, 0.60, 1);
uniform vec3 territory_player : source_color = vec3(0.36, 0.62, 0.94);
uniform vec3 territory_enemy : source_color = vec3(0.92, 0.32, 0.28);
uniform float territory_strength : hint_range(0,1) = 0.12;
uniform float fog_strength : hint_range(0,1) = 0.45;

varying vec2 v_uv;
varying vec3 v_pos;

void vertex() {
    v_uv = UV * 18.0;
    v_pos = VERTEX;
}

void fragment() {
    vec2 uv = v_uv;
    float n = texture(noise_tex, uv * 0.07).r;
    float n2 = texture(noise_tex, uv * 0.13 + vec2(4.2,1.1)).r;

    float splat = n * 0.55 + n2 * 0.45;
    // 3-stop splat: grass | dirt | rock
    vec3 col = mix(grass_col.rgb, dirt_col.rgb, smoothstep(0.32, 0.55, splat));
    col = mix(col, rock_col.rgb, smoothstep(0.68, 0.85, splat));

    // subtle grid hint (Thronefall)
    float grid = smoothstep(0.97, 1.0, max(fract(uv.x), fract(uv.y)));
    col = mix(col, col * 0.92, grid * 0.25);

    // territory tint via INSTANCE_CUSTOM (for batched terrain chunks) or uniform
    float terr = clamp(INSTANCE_CUSTOM.x * 2.0 - 1.0, -1.0, 1.0); // -1 enemy, +1 player
    vec3 terrCol = terr > 0.0 ? territory_player : territory_enemy;
    col = mix(col, mix(col, terrCol, 0.22), abs(terr) * territory_strength);

    // simple height AO
    float ao = 1.0 - smoothstep(0.0, 2.5, length(v_pos.xz) * 0.02) * 0.08;
    col *= ao;

    // LOS fog via second UV (fog texture sampled in WorldView custom)
    float fog = clamp(INSTANCE_CUSTOM.y, 0.0, 1.0); // 0 hidden, 0.5 fogged, 1 visible
    vec3 fogCol = vec3(0.12, 0.14, 0.18);
    col = mix(fogCol * 0.7, col, mix(0.45, 1.0, fog) * (1.0 - fog_strength * (1.0-fog)));

    ALBEDO = col;
    ROUGHNESS = 0.95;
    // no normal map — flat thronefall
}
