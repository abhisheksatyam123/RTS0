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

varying vec2 v_uv;
varying vec3 v_pos;

void vertex() {
    v_uv = UV * 16.0;
    v_pos = VERTEX;
}

void fragment() {
    vec2 uv = v_uv;
    float n = texture(noise_tex, uv * 0.06).r;
    float n2 = texture(noise_tex, uv * 0.12 + vec2(3.7, 1.9)).r;

    float splat = n * 0.55 + n2 * 0.45;

    // Vibrant 3-stop terrain splat: Rich Grass -> Sunny Dirt / Sand -> Warm Rock
    vec3 grass = texture(grass_tex, uv * 1.5).rgb * grass_col.rgb;
    vec3 dirt  = texture(dirt_tex,  uv * 1.5).rgb * dirt_col.rgb;
    vec3 rock  = texture(rock_tex,  uv * 1.5).rgb * rock_col.rgb;

    vec3 col = mix(grass, dirt, smoothstep(0.35, 0.58, splat));
    col = mix(col, rock, smoothstep(0.72, 0.88, splat));

    // Subtle grid guide
    float grid = smoothstep(0.98, 1.0, max(fract(uv.x), fract(uv.y)));
    col = mix(col, col * 0.94, grid * 0.15);

    ALBEDO = col;
    ROUGHNESS = 0.78;
    SPECULAR = 0.18;
}
