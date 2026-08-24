// thronefall flat-ramp + outline — Godot 4.3 forward_plus
// Use on MultiMesh units/buildings. Colors via INSTANCE_CUSTOM (r=team, g=hp)
// Reference: Thronefall low-poly flat + screen-space outline
shader_type spatial;
render_mode blend_mix, depth_draw_opaque, cull_back, diffuse_lambert, specular_disabled;

uniform vec3 color_player : source_color = vec3(0.36, 0.62, 0.94);
uniform vec3 color_enemy : source_color = vec3(0.92, 0.32, 0.28);
uniform vec3 color_team_mix = vec3(1.0); // driven by INSTANCE_CUSTOM.x (0=player,1=enemy)
uniform float hp_frac : hint_range(0,1) = 1.0; // INSTANCE_CUSTOM.y
uniform vec3 outline_color : source_color = vec3(0.08, 0.09, 0.11);
uniform float outline_width : hint_range(0,.05) = 0.012;
uniform float flat_steps : hint_range(2,8) = 3.0;
uniform float ao_strength : hint_range(0,1) = 0.18;

varying vec3 v_vertex;
varying vec3 v_normal;
varying vec4 v_custom;

void vertex() {
    v_vertex = VERTEX;
    v_normal = NORMAL;
    v_custom = INSTANCE_CUSTOM;
    // slight top-face AO via y
    // wind for foliage instances: offset by INSTANCE_CUSTOM.z
    float wind = INSTANCE_CUSTOM.z;
    if (wind > 0.001) {
        float phase = TIME * 0.9 + float(INSTANCE_ID) * 0.13;
        VERTEX.x += sin(phase) * 0.06 * wind * smoothstep(0.2, 1.0, UV.y);
        VERTEX.z += cos(phase * 0.7) * 0.04 * wind * smoothstep(0.2, 1.0, UV.y);
    }
}

void fragment() {
    float team = clamp(v_custom.x, 0.0, 1.0);
    vec3 base = mix(color_player, color_enemy, team);
    // hp desaturate when wounded
    float hp = clamp(v_custom.y, 0.0, 1.0);
    base = mix(base * 0.55, base, hp);

    // flat ramp: N·L quantized
    float ndl = dot(normalize(v_normal), normalize(vec3(0.35, 0.85, 0.25)));
    ndl = smoothstep(0.0, 1.0, ndl);
    float ramp = floor(ndl * flat_steps) / flat_steps;
    ramp = mix(0.72, 1.0, ramp);

    // outline: screen-space via view-normal edge
    float vdn = dot(normalize(NORMAL), normalize(VIEW));
    float outline = smoothstep(0.35, 0.28, vdn) * outline_width * 80.0;
    // also depth edge via fwidth (cheap)
    float edge = smoothstep(0.4, 0.5, length(fwidth(base))) * 0.0;

    vec3 col = base * ramp;
    // top AO
    float ao = 1.0 - ao_strength * (1.0 - clamp(v_vertex.y * 0.6, 0.0, 1.0));
    col *= ao;
    col = mix(col, outline_color, clamp(outline, 0.0, 1.0));
    ALBEDO = col;
    ROUGHNESS = 0.75;
}
