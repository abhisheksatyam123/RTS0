#ifndef RTSO_RMS_GENERATOR_H
#define RTSO_RMS_GENERATOR_H

#include "sim_types.h"
#include <cstdint>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <utility>

namespace rtso {

class Simulation;

// Deterministic PRNG for procedural generation with seed reproducibility
class RmsRng {
public:
	explicit RmsRng(uint32_t seed = 1337) : state_(seed ? seed : 1337) {}

	void set_seed(uint32_t seed) { state_ = seed ? seed : 1337; }
	uint32_t seed() const { return state_; }

	uint32_t next_u32() {
		uint32_t x = state_;
		x ^= x << 13;
		x ^= x >> 17;
		x ^= x << 5;
		state_ = x;
		return x;
	}

	float next_float() {
		return (next_u32() & 0xFFFFFF) / 16777216.0f; // [0, 1)
	}

	float range_float(float min_val, float max_val) {
		return min_val + next_float() * (max_val - min_val);
	}

	int range_int(int min_val, int max_val) {
		if (min_val >= max_val) return min_val;
		return min_val + (int)(next_u32() % (uint32_t)(max_val - min_val + 1));
	}

	float next_gaussian(float mean = 0.0f, float stddev = 1.0f) {
		float u1 = std::max(1e-6f, next_float());
		float u2 = next_float();
		float z0 = std::sqrt(-2.0f * std::log(u1)) * std::cos(6.28318530718f * u2);
		return mean + z0 * stddev;
	}

private:
	uint32_t state_;
};

// 2D Heightmap Grid for multi-scale terrain synthesis
class HeightmapGrid {
public:
	int res = 129; // (2^n + 1) e.g., 65 or 129
	float map_half = 64.0f;
	std::vector<float> data;

	void init(int r, float half, float init_val = 0.0f) {
		res = r;
		map_half = half;
		data.assign((size_t)(res * res), init_val);
	}

	inline float get(int x, int z) const {
		if (x < 0) x = 0; else if (x >= res) x = res - 1;
		if (z < 0) z = 0; else if (z >= res) z = res - 1;
		return data[(size_t)(z * res + x)];
	}

	inline void set(int x, int z, float val) {
		if (x >= 0 && x < res && z >= 0 && z < res) {
			data[(size_t)(z * res + x)] = val;
		}
	}

	inline void add(int x, int z, float val) {
		if (x >= 0 && x < res && z >= 0 && z < res) {
			data[(size_t)(z * res + x)] += val;
		}
	}

	float sample_world(float wx, float wz) const;
	void world_to_grid(float wx, float wz, float &gx, float &gz) const;
	void grid_to_world(int gx, int gz, float &wx, float &wz) const;

	float compute_slope_world(float wx, float wz) const;

	// Terrain synthesis operations
	void diamond_square(RmsRng &rng, float initial_range, float roughness);
	void apply_fbm(RmsRng &rng, int octaves, float frequency, float persistence, float lacunarity, float amplitude);
	void apply_ridged_noise(RmsRng &rng, int octaves, float frequency, float persistence, float lacunarity, float amplitude);
	void apply_radial_falloff(float start_fraction, float end_fraction, float ocean_height);
	void apply_border_falloff(float border_width, float target_height);
	void blur(int passes = 1, float weight = 0.5f);
	void thermal_erosion(int iterations = 10, float talus_threshold = 0.8f, float erosion_rate = 0.15f);
	void terrace(float step_height, float ramp_fraction = 0.2f);
	void level_area(float center_wx, float center_wz, float radius, float blend_radius, float target_height);
	void carve_spline_path(const std::vector<std::pair<float, float>> &points, float river_width, float bed_depth, float water_height, float ford_start_dist = -1.0f, float ford_length = 0.0f);
	void carve_crater_lake(float center_wx, float center_wz, float radius, float blend_margin, float depth, float water_height);
	void raise_plateau(float center_wx, float center_wz, float radius, float ramp_angle, float ramp_width_rad, float height_gain, float cliff_blend);
	void apply_sinusoidal_dunes(float angle_rad, float wavelength, float amplitude, float secondary_amp);
	void raise_sandstone_mesa(float center_wx, float center_wz, float radius_base, float radius_top, float height_gain, float ramp1_angle_rad, float ramp2_angle_rad, float ramp_width_rad);
	void carve_canyon_network(RmsRng &rng, float border_margin, float canyon_width, float canyon_depth);
};

// RMS Procedural Map Generator Engine
class RmsGenerator {
public:
	struct Config {
		MapType map_type = MAP_RANDOM_CONTINENTAL;
		uint32_t seed = 0;
		int num_players = 2;
		float map_half = 64.0f;
		int heightmap_res = 129; // 129x129 grid for high-fidelity terrain
		BiomeType biome = BIOME_AEGEAN;
		// Game-setup team of player i (1-based, 0 = no team), for team placement.
		uint8_t teams[8] = {};
		// Game Setup Team Placement (TEAM_PLACEMENT_*), for the maps that offer it.
		uint8_t team_placement = TEAM_PLACEMENT_RANDOM;
	};

	struct PlayerSpawn {
		uint8_t team;
		float x, z;
		float base_height;
		float forward_dir_x, forward_dir_z;
	};

	static bool generate(Simulation &sim, const Config &config);
	// Paints the per-tile terrain textures (Simulation::set_terrain_tiles) of a
	// generated map from its biome's 0 A.D. terrains (terrain_biomes.h): water,
	// shore and shore blend by height, cliffs and hills by slope, forest floor
	// under the trees, road round each civic centre, dirt under the mines and
	// 0 A.D.'s tier patches over the main terrain. Uses its own hash of seed, so
	// no other placement changes.
	static void paint_terrain(Simulation &sim, uint32_t seed);

private:
	// Map-specific terrain synthesizers
	static void synth_continental(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_archipelago(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_oasis(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_highlands(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_valley(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_mediterranean(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_acropolis(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_spartan_valley(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_sahara(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_peloponnese(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_danubius(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_saharan_oases(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_anatolian_plateau(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_jebel_barkal(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_mainland(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_alpine_lakes(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_lake(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_gulf_of_bothnia(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_ardennes_forest(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_nearestern_badlands(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_rivers(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_canyon(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_corinthian_isthmus(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_belgian_uplands(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_lorraine_plain(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_hyrcanian_shores(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_northern_lights(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_kerala(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_polar_sea(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_flood(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_extinct_volcano(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_sahel(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_african_plains(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_alpine_valley(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_syria(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_cantabrian_highlands(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_pyrenean_sierra(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_sahel_watering_holes(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_persian_highlands(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_volcanic_lands(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_lions_den(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_hells_pass(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_deep_forest(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_india(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_guadalquivir_river(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_fields_of_meroe(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_gear(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_atlas_mountains(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_frontier(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players,
	                           const uint8_t *teams, uint8_t team_placement);
	static void synth_wild_lake(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_stronghold(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players, const uint8_t *teams);
	static void synth_caledonian_meadows(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_rhine_marshlands(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_red_sea(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players, const uint8_t *teams);
	static void synth_hellas(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players, const uint8_t *teams);
	static void synth_english_channel(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players, const uint8_t *teams);
	static void synth_dodecanese(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players, const uint8_t *teams);
	static void synth_fortress(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_aegean_sea(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players, const uint8_t *teams);
	static void synth_archipelago_0ad(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players, const uint8_t *teams);
	static void synth_islands(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players, const uint8_t *teams);
	static void synth_cycladic_archipelago(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players, const uint8_t *teams);
	static void synth_snowflake_searocks(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players, const uint8_t *teams);
	static void synth_harbor(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players, const uint8_t *teams);
	static void synth_corsica(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players, const uint8_t *teams);
	static void synth_phoenician_levant(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players, const uint8_t *teams);
	static void synth_marmara(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players, const uint8_t *teams);
	static void synth_bahrain(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players, const uint8_t *teams);
	static void synth_botswanan_haven(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players, const uint8_t *teams);
	static void synth_scythian_rivulet(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players, const uint8_t *teams);
	static void synth_ambush(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players, const uint8_t *teams,
	                         uint8_t team_placement);
	static void synth_arctic_summer(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_empire(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players, const uint8_t *teams);
	static void synth_schwarzwald(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_ratumacos(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players, const uint8_t *teams);
	static void synth_oasis_0ad(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_mediterranean_0ad(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players, const uint8_t *teams);
	static void synth_elephantine(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players, const uint8_t *teams);
	static void synth_island_stronghold(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players, const uint8_t *teams);
	static void synth_pompeii(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players, const uint8_t *teams);
	static void synth_latium(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players, const uint8_t *teams);
	static void synth_unknown(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players, const uint8_t *teams);

	// Fair entity & resource distribution
	static void distribute_player_bases(Simulation &sim, const HeightmapGrid &hm, const std::vector<PlayerSpawn> &spawns, RmsRng &rng, const Config &config);
	static void distribute_neutral_resources(Simulation &sim, const HeightmapGrid &hm, float water_h, const std::vector<PlayerSpawn> &spawns, RmsRng &rng, const Config &config);
	static void distribute_wildlife(Simulation &sim, const HeightmapGrid &hm, float water_h, const std::vector<PlayerSpawn> &spawns, RmsRng &rng, const Config &config);
	static void distribute_relics(Simulation &sim, const HeightmapGrid &hm, float water_h, const std::vector<PlayerSpawn> &spawns, RmsRng &rng, const Config &config);
	static void place_trigger_map_entities(Simulation &sim, const HeightmapGrid &hm, float water_h, const std::vector<PlayerSpawn> &spawns, RmsRng &rng, const Config &config);
	static void distribute_fish(Simulation &sim, const HeightmapGrid &hm, float water_h, const Config &config);
	// Map-specific wood and mines of the Ambush, Schwarzwald and Oasis ports.
	static void distribute_land_extras(Simulation &sim, const HeightmapGrid &hm, float water_h, const std::vector<PlayerSpawn> &spawns, RmsRng &rng, const Config &config);
};

} // namespace rtso

#endif // RTSO_RMS_GENERATOR_H
