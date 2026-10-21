// 0 A.D. Authentic High-Fidelity Terrain Shader
// Triplanar cliff projection, organic multi-octave biome blending, and natural Mediterranean palette.
shader_type spatial;
// cull_disabled: rebuild_terrain winds its triangles counter-clockwise seen from above,
// which Vulkan (Forward+) culls as back faces.
render_mode blend_mix, depth_draw_opaque, cull_disabled, diffuse_burley;

uniform sampler2D noise_tex : hint_default_black, filter_linear_mipmap, repeat_enable;
uniform sampler2D grass_tex : hint_default_white, filter_linear_mipmap, repeat_enable;
uniform sampler2D dirt_tex : hint_default_white, filter_linear_mipmap, repeat_enable;
uniform sampler2D rock_tex : hint_default_white, filter_linear_mipmap, repeat_enable;

uniform vec4 grass_col : source_color = vec4(0.95, 1.0, 0.92, 1.0);
uniform vec4 dirt_col : source_color = vec4(1.0, 0.95, 0.88, 1.0);
uniform vec4 rock_col : source_color = vec4(0.92, 0.90, 0.88, 1.0);

uniform vec3 territory_player : source_color = vec3(0.36, 0.62, 0.94);
uniform vec3 territory_enemy : source_color = vec3(0.92, 0.32, 0.28);
uniform float territory_strength : hint_range(0, 1) = 0.0;
uniform sampler2D territory_map : hint_default_black;

// Line of sight (0 A.D. CLOSTexture + terrain shader `color *= los`): one texel
// per FoW cell over [-los_map_half, los_map_half]^2, 255 visible, ~127 explored,
// 0 unexplored, blurred and eased by RtsoWorldView. los_map_half 0 = no fog.
uniform sampler2D los_tex : hint_default_white, filter_linear, repeat_disable;
uniform float los_map_half = 0.0;
// terrain_common.fs fogs the colour first and multiplies by the LOS after, so
// unexplored ground stays black under the haze. RtsoWorldView passes the
// environment's exponential fog (light colour times energy, density; 0 = off).
uniform vec4 scene_fog = vec4(0.0);

// Painted ground, as 0 A.D. random maps paint tiles (rmbiome shore and
// forestFloor terrains, dirt under mines). paint_tex: g = forest floor,
// b = mine dirt, per terrain cell; the shore follows the water level.
uniform sampler2D shore_tex : hint_default_white, filter_linear_mipmap, repeat_enable;
uniform sampler2D forest_tex : hint_default_white, filter_linear_mipmap, repeat_enable;
uniform sampler2D paint_tex : hint_default_black, filter_linear, repeat_disable;
uniform float paint_half = 0.0;
uniform float water_level = -1000.0;

// Per-tile terrain (0 A.D. CTerrain tiles, Simulation::terrain_tiles): tile_index
// holds each tile's layer of tile_layers over [-tile_half, tile_half]^2, tile_res
// tiles a side. tile_blends (src/presentation/terrain_blend.h, 0 A.D.'s
// CPatchRData::BuildBlends) holds, per tile, up to 8 higher-priority neighbour
// terrains drawn over it, each masked by one of 0 A.D.'s alpha maps (alpha_maps,
// turned and flipped to fit). tile_res 0 = paint by biome.
uniform sampler2DArray tile_layers : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D tile_index : filter_nearest, repeat_disable;
uniform sampler2D tile_blends : filter_nearest, repeat_disable;
uniform sampler2DArray alpha_maps : filter_linear, repeat_disable;
uniform float tile_res = 0.0;
uniform float tile_half = 0.0;
// World units a terrain texture spans (0 A.D. terrains repeat every 8 tiles).
uniform float tile_repeat = 8.0;

varying vec2 v_uv;
varying vec3 v_world_pos;
varying vec3 v_world_normal;
varying float v_height;

void vertex() {
	v_uv = UV * 16.0;
	v_world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	v_world_normal = normalize((MODEL_MATRIX * vec4(NORMAL, 0.0)).xyz);
	v_height = VERTEX.y;
}

void fragment() {
	vec2 uv = v_uv;
	vec3 norm = normalize(v_world_normal);

	// Multi-octave natural terrain noise
	float n1 = texture(noise_tex, uv * 0.05).r;
	float n2 = texture(noise_tex, uv * 0.15 + vec2(4.3, 1.7)).r;
	float noise_splat = n1 * 0.6 + n2 * 0.4;

	// Slope calculation: 1.0 is vertical cliff, 0.0 is completely flat
	float slope = 1.0 - clamp(abs(norm.y), 0.0, 1.0);
	float cliff_weight = smoothstep(0.22, 0.52, slope);

	// Elevation calculation: rich grass in valleys -> dirt along slopes -> rock on peaks
	float eff_height = v_height + (noise_splat - 0.5) * 5.0;
	float dirt_height = smoothstep(1.2, 5.0, eff_height);
	float rock_height = smoothstep(6.5, 14.0, eff_height);

	// Total rock weight combines cliffs and high peaks
	float rock_weight = clamp(max(cliff_weight, rock_height), 0.0, 1.0);

	// Dirt dominates mid-slopes and noise pathways
	float dirt_mix = clamp(max(dirt_height, smoothstep(0.40, 0.65, noise_splat)), 0.0, 1.0);
	float dirt_weight = dirt_mix * (1.0 - rock_weight);

	// Grass fills the lush lowlands and plains
	float grass_weight = clamp(1.0 - rock_weight - dirt_weight, 0.0, 1.0);

	// Samples
	// Lowlands grass (macro + micro tiling)
	vec3 grass_sample = (texture(grass_tex, uv * 1.0).rgb * 0.65 + texture(grass_tex, uv * 2.8).rgb * 0.35) * grass_col.rgb;
	// Dirt / pathway sample
	vec3 dirt_sample = (texture(dirt_tex, uv * 1.2).rgb * 0.7 + texture(dirt_tex, uv * 3.2).rgb * 0.3) * dirt_col.rgb;

	// Triplanar sampling for cliffs to eliminate vertical UV stretching
	vec3 tri_uv_x = texture(rock_tex, v_world_pos.zy * 0.15).rgb;
	vec3 tri_uv_z = texture(rock_tex, v_world_pos.xy * 0.15).rgb;
	vec3 tri_uv_y = texture(rock_tex, v_world_pos.xz * 0.15).rgb;
	vec3 tri_blend = abs(norm);
	tri_blend = tri_blend / (tri_blend.x + tri_blend.y + tri_blend.z + 0.0001);
	vec3 rock_sample = (tri_uv_x * tri_blend.x + tri_uv_y * tri_blend.y + tri_uv_z * tri_blend.z) * rock_col.rgb;

	// Composite albedo
	vec3 final_color = grass_sample * grass_weight + dirt_sample * dirt_weight + rock_sample * rock_weight;

	if (tile_res > 0.0) {
		vec2 tc = (v_world_pos.xz + vec2(tile_half)) / (2.0 * tile_half) * tile_res;
		int last = int(tile_res) - 1;
		ivec2 ti = clamp(ivec2(floor(tc)), ivec2(0), ivec2(last));
		vec2 st = clamp(tc - vec2(ti), 0.0, 1.0);
		vec2 tuv = v_world_pos.xz / tile_repeat;
		// Explicit gradients: the layer loop below is not uniform control flow.
		vec2 gx = dFdx(tuv), gy = dFdy(tuv);
		float own = floor(texelFetch(tile_index, ti, 0).r * 255.0 + 0.5);
		final_color = textureGrad(tile_layers, vec3(tuv, own), gx, gy).rgb;
		for (int k = 0; k < 8; k++) {
			vec4 b = texelFetch(tile_blends, ivec2(ti.x * 8 + k, ti.y), 0);
			if (b.a < 0.5) break;
			int flags = int(b.b * 255.0 + 0.5);
			// AddBlend: flip the alpha map's corners, then turn them round the tile;
			// u runs along +x, v along +z, (0, 0) the image's top left.
			vec2 c0 = vec2(float((flags & 2) != 0), float((flags & 1) != 0));
			vec2 c2 = vec2(1.0) - c0;
			vec2 corner[4] = vec2[4](c0, vec2(c2.x, c0.y), c2, vec2(c0.x, c2.y));
			int turn = (flags & 4) != 0 ? 1 : ((flags & 8) != 0 ? 2 : ((flags & 16) != 0 ? 3 : 0));
			vec2 a0 = corner[(4 - turn) % 4], a1 = corner[(5 - turn) % 4], a2 = corner[(6 - turn) % 4], a3 = corner[(7 - turn) % 4];
			vec2 auv = mix(mix(a0, a1, st.x), mix(a3, a2, st.x), st.y);
			auv = auv * (62.0 / 64.0) + vec2(1.0 / 64.0);
			float alpha = 1.0 - textureLod(alpha_maps, vec3(auv, floor(b.g * 255.0 + 0.5)), 0.0).r;
			vec3 over = textureGrad(tile_layers, vec3(tuv, floor(b.r * 255.0 + 0.5)), gx, gy).rgb;
			final_color = mix(final_color, over, alpha);
		}
	}

	// Painted tiles go under cliffs: rock keeps its share.
	float ground = 1.0 - rock_weight;
	if (paint_half > 0.0 && tile_res <= 0.0) {
		vec3 paint = texture(paint_tex, (v_world_pos.xz + vec2(paint_half)) / (2.0 * paint_half)).rgb;
		float fw = smoothstep(0.15, 0.85, paint.g + (noise_splat - 0.5) * 0.5) * ground * 0.85;
		// Shaded by the canopy, as 0 A.D.'s tree shadows darken it.
		vec3 forest_sample = texture(forest_tex, uv * 1.4).rgb * 0.72;
		final_color = mix(final_color, forest_sample, fw);
		float mw = smoothstep(0.2, 0.9, paint.b + (noise_splat - 0.5) * 0.4) * ground;
		final_color = mix(final_color, dirt_sample, mw);
	}
	if (water_level > -999.0 && tile_res <= 0.0) {
		float shore_h = v_height + (noise_splat - 0.5) * 0.8;
		float sw = (1.0 - smoothstep(water_level + 0.3, water_level + 1.4, shore_h)) * (1.0 - cliff_weight * 0.7);
		vec3 shore_sample = texture(shore_tex, uv * 1.6).rgb;
		final_color = mix(final_color, shore_sample, sw);
	}

	// Optional territory border overlay if enabled
	if (territory_strength > 0.001) {
		vec4 t_data = texture(territory_map, UV);
		vec3 t_color = mix(territory_player, territory_enemy, t_data.g);
		final_color = mix(final_color, t_color, t_data.r * territory_strength);
	}

	float los = 1.0;
	if (los_map_half > 0.0) {
		vec2 los_uv = (v_world_pos.xz + vec2(los_map_half)) / (2.0 * los_map_half);
		los = texture(los_tex, los_uv).r;
	}
	final_color *= los;

	ALBEDO = final_color;
	FOG = vec4(scene_fog.rgb * los, 1.0 - exp(min(0.0, -length(VERTEX) * scene_fog.a)));
	ROUGHNESS = mix(0.85, 0.65, rock_weight);
	SPECULAR = mix(0.15, 0.25, rock_weight) * los;
}
