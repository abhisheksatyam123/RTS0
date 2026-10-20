#ifndef RTSO_RMS_GENERATOR_H
#define RTSO_RMS_GENERATOR_H

#include "core/math/det_math.h"
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
		float z0 = std::sqrt(-2.0f * det::log(u1)) * det::cos(6.28318530718f * u2);
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
	void carve_canyon_network(RmsRng &rng, float border_margin, float canyon_width, float canyon_depth);
};

// RMS Procedural Map Generator Engine
// SkirmishReplacer.js for a ported map's unit template ("units/default_cavalry",
// "units/special_starting_unit"): 1 with out_tmpl set to the civ's unit, -1 when the civ has
// none (the entity goes), 0 when the template is not a skirmish one.
int skirmish_replace(uint8_t civ, const char *map_template, uint16_t &out_tmpl);

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
		// Game Setup Landscape of the Unknown map (0 A.D. unknown.json Landscapes):
		// UNKNOWN_LANDSCAPE_RANDOM, _RANDOM_LAND, _RANDOM_NAVAL, or
		// UNKNOWN_LANDSCAPE_FIRST + one landscape (synth_unknown's order).
		uint8_t landscape = 0;
		// Game Setup Trigger Difficulty (data/settings/trigger_difficulties.json:
		// 1 Very Easy .. 5 Very Hard, 3 Medium), for the maps that support it.
		uint8_t trigger_difficulty = 3;
	};
	enum : uint8_t {
		UNKNOWN_LANDSCAPE_RANDOM = 0, UNKNOWN_LANDSCAPE_RANDOM_LAND = 1, UNKNOWN_LANDSCAPE_RANDOM_NAVAL = 2,
		UNKNOWN_LANDSCAPE_FIRST = 3, UNKNOWN_LANDSCAPE_COUNT = 13, // Archipelago .. Mainland
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
	// type: the map type, whose 0 A.D. script's own terrains (script_terrains.h) replace the
	// biome's unless the game setup overrides the biome.
	static void paint_terrain(Simulation &sim, uint32_t seed, uint8_t type);
	// Gives each tree and fruit bush of a generated map its biome's 0 A.D. plant
	// (terrain_biomes.h biome_flora): trees in a forest share tree1/tree2 or
	// tree4/tree5, lone trees take tree1, tree2, tree4 or tree3, bushes the
	// fruitBush. A tree also takes its template's wood (BiomeFlora tree*_wood). Hashed
	// from seed like paint_terrain.
	static void plant_flora(Simulation &sim, uint32_t seed, uint8_t type);
	// Gives large stone quarries their template's MaxGatherers where it differs from
	// template_gaia_rock_large's 24: the temperate and alpine biomes' stoneLarge
	// (gaia/rock/temperate_large_02) takes 48 on the maps that place g_Gaia.stoneLarge,
	// and Caledonian Meadows places that rock itself.
	static void set_mine_gatherers(Simulation &sim, uint8_t type);
	// 0.28's scripts keep mines, fruit and forests off cliffs (avoidClasses(clHill, ...),
	// validTilePassable): a mine or bush on a tile land units can't stand on, or a tree with no
	// standable tile beside it, moves to the nearest standable spot nearby. Animals and
	// resources in a circular map's impassable edge ring move in across it (MAP_BORDER_WIDTH);
	// mines, bushes and wild animals walled off from every base by cliffs move onto the bases' land.
	static void keep_resources_passable(Simulation &sim, uint8_t type);

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
	static void synth_saharan_oases(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void synth_anatolian_plateau(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
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
	// What danubius.js's terrain pass hands its entity pass: the river's frame, the Gallic
	// villages and meeting places, each bank's shore (1 left, 2 right) and the islands.
	struct DanubiusLayout {
		float nx = 1.0f, nz = 0.0f; // across the river, towards the right bank
		bool villages = false, ritual = false;
		float village[2][2] = {}, ritual_pos[2][2] = {}, ritual_r = 0.0f;
		std::vector<uint8_t> shore, islands; // per heightmap vertex
	};
	static void synth_danubius_0ad(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players,
	                               const uint8_t *teams, DanubiusLayout &out);
	static void place_danubius_entities(Simulation &sim, const HeightmapGrid &hm, float water_h, const std::vector<PlayerSpawn> &spawns,
	                                    RmsRng &rng, const DanubiusLayout &layout);
	// What jebel_barkal.js's terrain pass hands its entity pass: the city grid of Napata (row 0
	// at the hill's foot), its temples, walls and ritual place, the pyramids and the tile classes
	// the entities keep to or away from (per heightmap vertex).
	struct JebelBarkalLayout {
		int grid_x = 0, grid_y = 0;               // gridPointsX (one per temple), gridPointsY
		std::vector<float> grid, grid_angle;      // [y * grid_x + x]: x, z pairs, and cityGridAngle (the script's, z mirrored)
		std::vector<uint8_t> temples;             // BuildingType of each temple, by grid column
		std::vector<float> temple_pos;            // x, z pairs
		float ritual[2] = {}, ritual_angle = 0.0f, pyramids[2] = {}, pyramids_r = 0.0f;
		uint8_t wall = 0xFF;                      // WallSetKind of the Napata wall, 0xFF for none
		uint8_t difficulty = 3;                   // getDifficulty(): the Trigger Difficulty, 1..5
		std::vector<float> wall_spans, gates;     // x0, z0, x1, z1 per span; x, z, rot per gate
		float height_hill = 0.0f, height_hilltop = 0.0f, height_archers = 0.0f;
		std::vector<uint8_t> fertile, desert, water, hill, hilltop, cliff, path, crossing, city, keep_clear;
	};
	static void synth_jebel_barkal_0ad(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players,
	                                   const uint8_t *teams, JebelBarkalLayout &out);
	static void place_jebel_barkal_entities(Simulation &sim, const HeightmapGrid &hm, float water_h, const std::vector<PlayerSpawn> &spawns,
	                                        RmsRng &rng, const JebelBarkalLayout &layout);
	static void synth_archipelago_0ad(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players, const uint8_t *teams);
	static void synth_continent(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players, const uint8_t *teams);
	static void synth_foothills(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players, const uint8_t *teams);
	static void synth_coast_range(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players, const uint8_t *teams);
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
	static void synth_lower_nubia(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players, const uint8_t *teams);
	static void synth_unknown(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players, const uint8_t *teams,
	                          uint8_t landscape = 0);
	static void synth_migration(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players, const uint8_t *teams,
	                            uint8_t team_placement);
	static void synth_ngorongoro(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players, const uint8_t *teams);
	// What river_archipelago.js's terrain pass hands its entity pass: the Gaia Mauryans it placed
	// on the outermost strips before the hills, and the tile classes the later objects keep
	// away from (per heightmap vertex).
	struct RiverArchipelagoLayout {
		struct GaiaUnit {
			float x, z;
			uint8_t kind; // 0 spearman, 1 archer, 2 elephant archer
		};
		std::vector<GaiaUnit> gaia;
		std::vector<uint8_t> territory, hill;
	};
	static void synth_river_archipelago(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players,
	                                    const uint8_t *teams, RiverArchipelagoLayout &out);
	static void place_river_archipelago_entities(Simulation &sim, const HeightmapGrid &hm, float water_h, const std::vector<PlayerSpawn> &spawns,
	                                             RmsRng &rng, const RiverArchipelagoLayout &layout);

	// Fair entity & resource distribution
	static void distribute_player_bases(Simulation &sim, const HeightmapGrid &hm, const std::vector<PlayerSpawn> &spawns, RmsRng &rng, const Config &config);
	static void distribute_neutral_resources(Simulation &sim, const HeightmapGrid &hm, float water_h, const std::vector<PlayerSpawn> &spawns, RmsRng &rng, const Config &config);
	static void distribute_wildlife(Simulation &sim, const HeightmapGrid &hm, float water_h, const std::vector<PlayerSpawn> &spawns, RmsRng &rng, const Config &config);
	static void distribute_relics(Simulation &sim, const HeightmapGrid &hm, float water_h, const std::vector<PlayerSpawn> &spawns, RmsRng &rng, const Config &config);
	static void synth_land_grab(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players, const uint8_t *teams);
	static void synth_the_nile(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players, const uint8_t *teams);
	static void synth_survival_of_the_fittest(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players);
	static void place_survival_entities(Simulation &sim, const HeightmapGrid &hm, const std::vector<PlayerSpawn> &spawns, RmsRng &rng, const Config &config);
	static void place_trigger_map_entities(Simulation &sim, const HeightmapGrid &hm, float water_h, const std::vector<PlayerSpawn> &spawns, RmsRng &rng, const Config &config);
	static void place_elephantine_entities(Simulation &sim, const HeightmapGrid &hm, float water_h, const std::vector<PlayerSpawn> &spawns, RmsRng &rng);
	// Ngorongoro's lowland, highland and eden mines, hunt and baobabs.
	static void place_ngorongoro_entities(Simulation &sim, const HeightmapGrid &hm, const std::vector<PlayerSpawn> &spawns, RmsRng &rng, const Config &config);
	static void distribute_fish(Simulation &sim, const HeightmapGrid &hm, float water_h, const Config &config);
	// Adds Gaia mines, fruit and hunt outside the bases until the map has the counts its
	// 0 A.D. script places (resource_counts_data.h); base_resources and base_fauna are the
	// resource and animal counts once the player bases were placed.
	static void top_up_resources(Simulation &sim, const HeightmapGrid &hm, float water_h, const std::vector<PlayerSpawn> &spawns,
			const Config &config, size_t base_resources, size_t base_fauna);
	static void place_elephantine_fish(Simulation &sim, const HeightmapGrid &hm, float water_h, const Config &config);
	// lower_nubia.js's mines, pyramid, Nile palms and straggler trees (in place of the generic groves and mines).
	static void place_lower_nubia_gaia(Simulation &sim, const HeightmapGrid &hm, float water_h, const std::vector<PlayerSpawn> &spawns, RmsRng &rng);
	// Map-specific wood and mines of the Ambush, Schwarzwald and Oasis ports.
	static void distribute_land_extras(Simulation &sim, const HeightmapGrid &hm, float water_h, const std::vector<PlayerSpawn> &spawns, RmsRng &rng, const Config &config);
};

} // namespace rtso

#endif // RTSO_RMS_GENERATOR_H
