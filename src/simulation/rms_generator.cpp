#include "core/math/det_math.h"
#include "rms_generator.h"
#include "script_terrains.h"
#include "resource_counts_data.h"
#include "base_resources_data.h"
#include "tree_slot_gatherers.h"
#include "simulation.h"
#include "heightmap_images.h"
#include "start_entities_data.h"
#include <cmath>
#include <algorithm>
#include <bit>
#include <vector>
#include <cstring>
#include <functional>
#include <limits>
#include <queue>
#include <unordered_map>

namespace rtso {

namespace {
// The Team Placement of the map being generated (generate() sets it from
// Config::team_placement and map_team_placements): the pattern_locs pattern,
// or -1 for the map's own layout (Circle, or a map without the setting).
struct TeamPlacementContext {
	int pattern = -1;
	uint8_t teams[8] = {};
};
thread_local TeamPlacementContext t_team_placement;
// Whether the map being generated is circular (Simulation::circular_map, set before generate).
thread_local bool t_circular_map = true;

std::vector<std::pair<float, float>> pattern_locs(const HeightmapGrid &hm, RmsRng &rng, int pattern, int n,
		const uint8_t *teams, float distance, float grouped, float start);


// Perlin / Gradient Noise Implementation
class PerlinNoise {
public:
	explicit PerlinNoise(uint32_t seed) {
		reseed(seed);
	}

	void reseed(uint32_t seed) {
		p_.resize(512);
		std::vector<uint8_t> permutation(256);
		for (int i = 0; i < 256; ++i) permutation[i] = (uint8_t)i;

		RmsRng rng(seed);
		for (int i = 255; i > 0; --i) {
			int j = rng.range_int(0, i);
			std::swap(permutation[i], permutation[j]);
		}

		for (int i = 0; i < 256; ++i) {
			p_[i] = permutation[i];
			p_[256 + i] = permutation[i];
		}
	}

	float noise2d(float x, float y) const {
		int X = (int)std::floor(x) & 255;
		int Y = (int)std::floor(y) & 255;

		float xf = x - std::floor(x);
		float yf = y - std::floor(y);

		float u = fade(xf);
		float v = fade(yf);

		int aa = p_[p_[X] + Y];
		int ab = p_[p_[X] + Y + 1];
		int ba = p_[p_[X + 1] + Y];
		int bb = p_[p_[X + 1] + Y + 1];

		float x1 = lerp(grad(aa, xf, yf), grad(ba, xf - 1.0f, yf), u);
		float x2 = lerp(grad(ab, xf, yf - 1.0f), grad(bb, xf - 1.0f, yf - 1.0f), u);
		return lerp(x1, x2, v);
	}

private:
	std::vector<uint8_t> p_;

	static inline float fade(float t) {
		return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
	}

	static inline float lerp(float a, float b, float t) {
		return a + t * (b - a);
	}

	static inline float grad(int hash, float x, float y) {
		int h = hash & 7;
		float u = h < 4 ? x : y;
		float v = h < 4 ? y : x;
		return ((h & 1) ? -u : u) + ((h & 2) ? -2.0f * v : 2.0f * v);
	}
};

// 0 A.D. rmgen scaleByMapSize(min, max): linear from a 128-tile (Tiny) map to
// a 512-tile (Giant) map. One RTS0 world unit is one 0 A.D. tile.
inline int scale_by_map_size(int min_val, int max_val, float map_half) {
	const float size = map_half * 2.0f;
	return min_val + (int)((float)(max_val - min_val) * std::max(0.0f, size - 128.0f) / 384.0f);
}

inline float smoothstep(float edge0, float edge1, float x) {
	float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

// 0 A.D. metres above the water line -> RTS0 height (grid_nav.h HEIGHT_PER_METRE above water_h).
constexpr float kMetre = HEIGHT_PER_METRE;

// Defined further down, with the maps that first used them.
inline float metres(float water_h, float m);
inline float wade_height(float water_h);
inline float coast_metres(float water_h, float m);

// createBumps / SmoothElevationPainter(ELEVATION_MODIFY, h, 2) on small
// clumps: lumps up to `h` on the ground, never dents (the upper half of an fbm).
void add_bumps(HeightmapGrid &hm, RmsRng &rng, float h) {
	const std::vector<float> ground = hm.data;
	hm.apply_fbm(rng, 3, 0.08f, 0.5f, 2.0f, 2.0f * h);
	for (size_t i = 0; i < ground.size(); ++i) hm.data[i] = ground[i] + std::clamp(hm.data[i] - ground[i], 0.0f, h);
}

} // namespace

// =========================================================================
// HeightmapGrid Methods
// =========================================================================

void HeightmapGrid::world_to_grid(float wx, float wz, float &gx, float &gz) const {
	float norm_x = std::clamp((wx + map_half) / (2.0f * map_half), 0.0f, 1.0f);
	float norm_z = std::clamp((wz + map_half) / (2.0f * map_half), 0.0f, 1.0f);
	gx = norm_x * (float)(res - 1);
	gz = norm_z * (float)(res - 1);
}

void HeightmapGrid::grid_to_world(int gx, int gz, float &wx, float &wz) const {
	float step = (map_half * 2.0f) / (float)(res - 1);
	wx = -map_half + gx * step;
	wz = -map_half + gz * step;
}

float HeightmapGrid::sample_world(float wx, float wz) const {
	if (data.empty() || res <= 1) return 0.0f;
	float gx, gz;
	world_to_grid(wx, wz, gx, gz);

	int x0 = (int)gx;
	int z0 = (int)gz;
	int x1 = std::min(x0 + 1, res - 1);
	int z1 = std::min(z0 + 1, res - 1);

	float tx = gx - x0;
	float tz = gz - z0;

	float h00 = get(x0, z0);
	float h10 = get(x1, z0);
	float h01 = get(x0, z1);
	float h11 = get(x1, z1);

	float h0 = h00 * (1.0f - tx) + h10 * tx;
	float h1 = h01 * (1.0f - tx) + h11 * tx;
	return h0 * (1.0f - tz) + h1 * tz;
}

float HeightmapGrid::compute_slope_world(float wx, float wz) const {
	float delta = 1.0f;
	float h_east   = sample_world(wx + delta, wz);
	float h_west   = sample_world(wx - delta, wz);
	float h_north  = sample_world(wx, wz + delta);
	float h_south  = sample_world(wx, wz - delta);

	float dx = (h_east - h_west) / (2.0f * delta);
	float dz = (h_north - h_south) / (2.0f * delta);
	return std::sqrt(dx * dx + dz * dz);
}

void HeightmapGrid::diamond_square(RmsRng &rng, float initial_range, float roughness) {
	if (res <= 1) return;

	// Corner initialization
	set(0, 0, rng.range_float(-initial_range * 0.2f, initial_range * 0.2f));
	set(res - 1, 0, rng.range_float(-initial_range * 0.2f, initial_range * 0.2f));
	set(0, res - 1, rng.range_float(-initial_range * 0.2f, initial_range * 0.2f));
	set(res - 1, res - 1, rng.range_float(-initial_range * 0.2f, initial_range * 0.2f));

	int step = res - 1;
	float scale = initial_range;

	while (step > 1) {
		int half_step = step / 2;

		// 1. Square Step
		for (int z = 0; z < res - 1; z += step) {
			for (int x = 0; x < res - 1; x += step) {
				float a = get(x, z);
				float b = get(x + step, z);
				float c = get(x, z + step);
				float d = get(x + step, z + step);
				float avg = (a + b + c + d) * 0.25f;
				float offset = rng.range_float(-scale, scale);
				set(x + half_step, z + half_step, avg + offset);
			}
		}

		// 2. Diamond Step
		for (int z = 0; z < res; z += half_step) {
			for (int x = (z + half_step) % step; x < res; x += step) {
				float sum = 0.0f;
				int count = 0;

				if (x >= half_step) { sum += get(x - half_step, z); count++; }
				if (x + half_step < res) { sum += get(x + half_step, z); count++; }
				if (z >= half_step) { sum += get(x, z - half_step); count++; }
				if (z + half_step < res) { sum += get(x, z + half_step); count++; }

				float avg = sum / (float)count;
				float offset = rng.range_float(-scale, scale);
				set(x, z, avg + offset);
			}
		}

		scale *= det::pow(0.5f, roughness);
		step = half_step;
	}
}

void HeightmapGrid::apply_fbm(RmsRng &rng, int octaves, float frequency, float persistence, float lacunarity, float amplitude) {
	PerlinNoise perlin(rng.next_u32());
	float step = (map_half * 2.0f) / (float)(res - 1);

	for (int z = 0; z < res; ++z) {
		float wz = -map_half + z * step;
		for (int x = 0; x < res; ++x) {
			float wx = -map_half + x * step;

			float total = 0.0f;
			float freq = frequency;
			float amp = 1.0f;
			float max_amp = 0.0f;

			for (int o = 0; o < octaves; ++o) {
				total += perlin.noise2d(wx * freq, wz * freq) * amp;
				max_amp += amp;
				amp *= persistence;
				freq *= lacunarity;
			}

			float normalized = total / max_amp;
			add(x, z, normalized * amplitude);
		}
	}
}

void HeightmapGrid::apply_ridged_noise(RmsRng &rng, int octaves, float frequency, float persistence, float lacunarity, float amplitude) {
	PerlinNoise perlin(rng.next_u32());
	float step = (map_half * 2.0f) / (float)(res - 1);

	for (int z = 0; z < res; ++z) {
		float wz = -map_half + z * step;
		for (int x = 0; x < res; ++x) {
			float wx = -map_half + x * step;

			float total = 0.0f;
			float freq = frequency;
			float amp = 1.0f;
			float weight = 1.0f;
			float max_amp = 0.0f;

			for (int o = 0; o < octaves; ++o) {
				float n = 1.0f - std::abs(perlin.noise2d(wx * freq, wz * freq));
				n = n * n * weight;
				weight = std::clamp(n * 2.0f, 0.0f, 1.0f);
				total += n * amp;
				max_amp += amp;
				amp *= persistence;
				freq *= lacunarity;
			}

			float val = (total / max_amp);
			add(x, z, val * amplitude);
		}
	}
}

void HeightmapGrid::apply_radial_falloff(float start_fraction, float end_fraction, float ocean_height) {
	float step = (map_half * 2.0f) / (float)(res - 1);
	float r_start = map_half * start_fraction;
	float r_end   = map_half * end_fraction;

	for (int z = 0; z < res; ++z) {
		float wz = -map_half + z * step;
		for (int x = 0; x < res; ++x) {
			float wx = -map_half + x * step;
			float dist = std::sqrt(wx * wx + wz * wz);
			if (dist > r_start) {
				float t = smoothstep(r_start, r_end, dist);
				float current = get(x, z);
				set(x, z, current * (1.0f - t) + ocean_height * t);
			}
		}
	}
}

void HeightmapGrid::apply_border_falloff(float border_width, float target_height) {
	float step = (map_half * 2.0f) / (float)(res - 1);

	for (int z = 0; z < res; ++z) {
		float wz = -map_half + z * step;
		float dist_z = map_half - std::abs(wz);
		for (int x = 0; x < res; ++x) {
			float wx = -map_half + x * step;
			float dist_x = map_half - std::abs(wx);
			float min_edge = std::min(dist_x, dist_z);

			if (min_edge < border_width) {
				float t = smoothstep(0.0f, border_width, min_edge);
				float current = get(x, z);
				set(x, z, target_height * (1.0f - t) + current * t);
			}
		}
	}
}

void HeightmapGrid::blur(int passes, float weight) {
	std::vector<float> temp = data;
	for (int p = 0; p < passes; ++p) {
		for (int z = 0; z < res; ++z) {
			for (int x = 0; x < res; ++x) {
				float center = get(x, z);
				float neighbors = 0.0f;
				int count = 0;

				if (x > 0) { neighbors += get(x - 1, z); count++; }
				if (x < res - 1) { neighbors += get(x + 1, z); count++; }
				if (z > 0) { neighbors += get(x, z - 1); count++; }
				if (z < res - 1) { neighbors += get(x, z + 1); count++; }

				if (count > 0) {
					float avg = neighbors / (float)count;
					temp[(size_t)(z * res + x)] = center * (1.0f - weight) + avg * weight;
				}
			}
		}
		data = temp;
	}
}

void HeightmapGrid::thermal_erosion(int iterations, float talus_threshold, float erosion_rate) {
	std::vector<float> temp = data;
	for (int iter = 0; iter < iterations; ++iter) {
		for (int z = 1; z < res - 1; ++z) {
			for (int x = 1; x < res - 1; ++x) {
				float h = get(x, z);
				float d_max = 0.0f;
				int nx = x, nz = z;

				static const int dxs[4] = {1, -1, 0, 0};
				static const int dzs[4] = {0, 0, 1, -1};

				for (int i = 0; i < 4; ++i) {
					float nh = get(x + dxs[i], z + dzs[i]);
					float diff = h - nh;
					if (diff > d_max) {
						d_max = diff;
						nx = x + dxs[i];
						nz = z + dzs[i];
					}
				}

				if (d_max > talus_threshold) {
					float delta = (d_max - talus_threshold) * 0.5f * erosion_rate;
					temp[(size_t)(z * res + x)] -= delta;
					temp[(size_t)(nz * res + nx)] += delta;
				}
			}
		}
		data = temp;
	}
}

void HeightmapGrid::terrace(float step_height, float ramp_fraction) {
	if (step_height <= 0.01f) return;
	for (size_t i = 0; i < data.size(); ++i) {
		float h = data[i];
		float level = std::floor(h / step_height);
		float frac = (h - level * step_height) / step_height;
		float smooth_frac = smoothstep(0.5f - ramp_fraction * 0.5f, 0.5f + ramp_fraction * 0.5f, frac);
		data[i] = (level + smooth_frac) * step_height;
	}
}

void HeightmapGrid::level_area(float center_wx, float center_wz, float radius, float blend_radius, float target_height) {
	float step = (map_half * 2.0f) / (float)(res - 1);
	float total_r = radius + blend_radius;

	int min_x = std::max(0, (int)std::floor((center_wx - total_r + map_half) / step));
	int max_x = std::min(res - 1, (int)std::ceil((center_wx + total_r + map_half) / step));
	int min_z = std::max(0, (int)std::floor((center_wz - total_r + map_half) / step));
	int max_z = std::min(res - 1, (int)std::ceil((center_wz + total_r + map_half) / step));

	for (int z = min_z; z <= max_z; ++z) {
		float wz = -map_half + z * step;
		for (int x = min_x; x <= max_x; ++x) {
			float wx = -map_half + x * step;
			float dist = std::sqrt((wx - center_wx) * (wx - center_wx) + (wz - center_wz) * (wz - center_wz));

			if (dist <= radius) {
				set(x, z, target_height);
			} else if (dist <= total_r) {
				float t = smoothstep(radius, total_r, dist);
				float orig = get(x, z);
				set(x, z, target_height * (1.0f - t) + orig * t);
			}
		}
	}
}

void HeightmapGrid::carve_spline_path(const std::vector<std::pair<float, float>> &points, float river_width, float bed_depth, float water_height, float ford_start_dist, float ford_length) {
	if (points.size() < 2) return;
	float step = (map_half * 2.0f) / (float)(res - 1);

	// Dense sampling of the river path
	std::vector<std::pair<float, float>> dense_path;
	std::vector<float> dist_along_path;
	float cum_dist = 0.0f;

	dense_path.push_back(points[0]);
	dist_along_path.push_back(0.0f);

	for (size_t i = 0; i < points.size() - 1; ++i) {
		float p0x = points[i].first;
		float p0z = points[i].second;
		float p1x = points[i + 1].first;
		float p1z = points[i + 1].second;
		float seg_len = std::sqrt((p1x - p0x) * (p1x - p0x) + (p1z - p0z) * (p1z - p0z));
		int subdivisions = std::max(2, (int)std::ceil(seg_len / 0.5f));

		for (int s = 1; s <= subdivisions; ++s) {
			float t = (float)s / (float)subdivisions;
			float sx = p0x + (p1x - p0x) * t;
			float sz = p0z + (p1z - p0z) * t;
			cum_dist += seg_len / (float)subdivisions;
			dense_path.push_back({sx, sz});
			dist_along_path.push_back(cum_dist);
		}
	}

	float total_r = river_width * 2.2f;

	for (int z = 0; z < res; ++z) {
		float wz = -map_half + z * step;
		for (int x = 0; x < res; ++x) {
			float wx = -map_half + x * step;

			float min_dist_to_river = 1e9f;
			float best_dist_along = 0.0f;

			for (size_t i = 0; i < dense_path.size(); ++i) {
				float px = dense_path[i].first;
				float pz = dense_path[i].second;
				float d = std::sqrt((wx - px) * (wx - px) + (wz - pz) * (wz - pz));
				if (d < min_dist_to_river) {
					min_dist_to_river = d;
					best_dist_along = dist_along_path[i];
				}
			}

			if (min_dist_to_river < total_r) {
				bool is_ford = false;
				if (ford_start_dist >= 0.0f && best_dist_along >= ford_start_dist && best_dist_along <= (ford_start_dist + ford_length)) {
					is_ford = true;
				}

				// Fords sit in wadeable shallows (NAV_SHALLOW: within
				// LAND_MAX_WATER_DEPTH below the nav water line).
				float bed_h = is_ford ? (water_height + NAV_WATER_MARGIN - LAND_MAX_WATER_DEPTH + 0.05f) : (water_height - bed_depth);
				float orig = get(x, z);

				if (min_dist_to_river <= river_width) {
					float t = min_dist_to_river / river_width;
					float local_h = bed_h + t * t * 0.4f;
					set(x, z, std::min(orig, local_h));
				} else {
					float t = smoothstep(river_width, total_r, min_dist_to_river);
					float target = bed_h * (1.0f - t) + orig * t;
					set(x, z, std::min(orig, target));
				}
			}
		}
	}
}

void HeightmapGrid::carve_crater_lake(float center_wx, float center_wz, float radius, float blend_margin, float depth, float water_height) {
	float step = (map_half * 2.0f) / (float)(res - 1);
	float total_r = radius + blend_margin;

	for (int z = 0; z < res; ++z) {
		float wz = -map_half + z * step;
		for (int x = 0; x < res; ++x) {
			float wx = -map_half + x * step;
			float dist = std::sqrt((wx - center_wx) * (wx - center_wx) + (wz - center_wz) * (wz - center_wz));

			if (dist <= radius) {
				float t = dist / radius;
				float bowl = water_height - depth * (1.0f - t * t * 0.5f);
				set(x, z, std::min(get(x, z), bowl));
			} else if (dist <= total_r) {
				float t = smoothstep(radius, total_r, dist);
				float bowl_edge = water_height - depth * 0.5f;
				float orig = get(x, z);
				float target = bowl_edge * (1.0f - t) + orig * t;
				set(x, z, std::min(orig, target));
			}
		}
	}
}

void HeightmapGrid::raise_plateau(float center_wx, float center_wz, float radius, float ramp_angle, float ramp_width_rad, float height_gain, float cliff_blend) {
	float step = (map_half * 2.0f) / (float)(res - 1);
	float total_r = radius + cliff_blend;

	for (int z = 0; z < res; ++z) {
		float wz = -map_half + z * step;
		for (int x = 0; x < res; ++x) {
			float wx = -map_half + x * step;
			float dx = wx - center_wx;
			float dz = wz - center_wz;
			float dist = std::sqrt(dx * dx + dz * dz);

			if (dist > total_r) continue;

			float angle = det::atan2(dz, dx);
			float angle_diff = std::abs(angle - ramp_angle);
			while (angle_diff > 3.14159265f) angle_diff = std::abs(angle_diff - 6.2831853f);

			if (angle_diff < ramp_width_rad && dist > radius * 0.5f) {
				// Access Ramp Sector
				float t_ramp = (dist - radius * 0.5f) / (total_r - radius * 0.5f);
				float ramp_h = height_gain * (1.0f - t_ramp);
				add(x, z, ramp_h);
			} else if (dist <= radius) {
				add(x, z, height_gain);
			} else {
				float t = smoothstep(radius, total_r, dist);
				add(x, z, height_gain * (1.0f - t));
			}
		}
	}
}

void HeightmapGrid::apply_sinusoidal_dunes(float angle_rad, float wavelength, float amplitude, float secondary_amp) {
	if (wavelength <= 0.01f) return;
	float step = (map_half * 2.0f) / (float)(res - 1);
	float cos_a = det::cos(angle_rad);
	float sin_a = det::sin(angle_rad);
	float k = 6.2831853f / wavelength;

	for (int z = 0; z < res; ++z) {
		float wz = -map_half + z * step;
		for (int x = 0; x < res; ++x) {
			float wx = -map_half + x * step;

			// Rotate coordinates to dune wind direction
			float u = wx * cos_a + wz * sin_a;
			float v = -wx * sin_a + wz * cos_a;

			// Primary sinusoidal dune wave with windward gentle slope and leeward steep slip-face
			float phase = u * k + 0.35f * det::sin(v * k * 0.4f);
			float wave = det::sin(phase);
			float shaped_wave = (wave > 0.0f) ? det::pow(wave, 0.85f) : -det::pow(-wave, 1.2f);

			// Secondary cross-ripple
			float secondary = det::sin(v * k * 1.8f + u * k * 0.5f) * secondary_amp;

			add(x, z, shaped_wave * amplitude + secondary);
		}
	}
}

void HeightmapGrid::carve_canyon_network(RmsRng &rng, float border_margin, float canyon_width, float canyon_depth) {
	(void)rng;
	float half = map_half;
	float step = (half * 2.0f) / (float)(res - 1);

	// Perimeter ravines
	for (int z = 0; z < res; ++z) {
		float wz = -half + z * step;
		float dist_z = half - std::abs(wz);
		for (int x = 0; x < res; ++x) {
			float wx = -half + x * step;
			float dist_x = half - std::abs(wx);
			float min_edge = std::min(dist_x, dist_z);

			if (min_edge < border_margin) {
				float t = smoothstep(0.0f, border_margin, min_edge);
				float cur = get(x, z);
				float target_h = cur - canyon_depth;
				set(x, z, target_h * (1.0f - t) + cur * t);
			}
		}
	}

	// Slicing tributary canyon ravines cutting inward from borders
	std::vector<std::vector<std::pair<float, float>>> ravines = {
		{ {-half * 0.95f,  half * 0.35f}, {-half * 0.65f,  half * 0.25f}, {-half * 0.40f,  half * 0.15f} },
		{ { half * 0.95f, -half * 0.35f}, { half * 0.65f, -half * 0.25f}, { half * 0.40f, -half * 0.15f} },
		{ { half * 0.25f,  half * 0.95f}, { half * 0.20f,  half * 0.65f}, { half * 0.12f,  half * 0.40f} },
		{ {-half * 0.25f, -half * 0.95f}, {-half * 0.20f, -half * 0.65f}, {-half * 0.12f, -half * 0.40f} }
	};

	for (const auto &pts : ravines) {
		carve_spline_path(pts, canyon_width, canyon_depth, -20.0f);
	}
}

// =========================================================================
// RMS Map Synthesizers
// =========================================================================

// Lifts the whole map so that all but `wet_percent`% of the ground within
// `radius_frac` of the map half (a circle) sits at least 0.4 above the nav water
// line. The invented noise maps could otherwise sink nearly everything but the
// base pads, leaving each player on an island of their own.
// Places n gaia/tree trees (TREE_WOOD_SUPPLY wood each, until plant_flora gives
// each its template's wood) for one tree spot: the first on the spot, the rest
// around it at `spread`, on ground above min_h. RTS0's
// groves were laid out for 800-wood trees; two 200-wood trees per spot bring a
// 256 map's wood near what 0 A.D.'s random maps carry. The offsets come from a
// hash of the spot so the generator's random stream is unchanged.
static void spawn_trees(Simulation &sim, const HeightmapGrid &hm, float x, float z, float min_h, int n = 2, float spread = 0.9f) {
	sim.spawn_resource_world(x, z, RES_WOOD, TREE_WOOD_SUPPLY);
	const uint32_t hsh = (uint32_t)(int32_t)std::floor(x * 16.0f) * 73856093u ^ (uint32_t)(int32_t)std::floor(z * 16.0f) * 19349663u;
	const float a0 = (float)((hsh >> 4) & 1023u) * (6.2831853f / 1024.0f);
	const float lim = hm.map_half - 1.0f;
	for (int k = 1; k < n; ++k) {
		const float a = a0 + 6.2831853f * (float)(k - 1) / (float)std::max(1, n - 1);
		const float tx = x + det::cos(a) * spread, tz = z + det::sin(a) * spread;
		if (std::fabs(tx) < lim && std::fabs(tz) < lim && hm.sample_world(tx, tz) > min_h)
			sim.spawn_resource_world(tx, tz, RES_WOOD, TREE_WOOD_SUPPLY);
	}
}

static void lift_noise_to_land(HeightmapGrid &hm, float water_h, float radius_frac, int wet_percent) {
	std::vector<float> inner;
	const float r_in = hm.map_half * radius_frac;
	for (int z = 0; z < hm.res; ++z)
		for (int x = 0; x < hm.res; ++x) {
			const float wx = -hm.map_half + 2.0f * hm.map_half * (float)x / (float)(hm.res - 1);
			const float wz = -hm.map_half + 2.0f * hm.map_half * (float)z / (float)(hm.res - 1);
			if (wx * wx + wz * wz < r_in * r_in) inner.push_back(hm.get(x, z));
		}
	if (inner.empty()) return;
	const size_t k = inner.size() * (size_t)wet_percent / 100;
	std::nth_element(inner.begin(), inner.begin() + (std::ptrdiff_t)k, inner.end());
	const float lift = water_h + NAV_WATER_MARGIN + 0.4f - inner[k];
	if (lift > 0.0f)
		for (auto &v : hm.data) v += lift;
}

void RmsGenerator::synth_continental(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	water_h = 0.5f;
	hm.init(hm.res, hm.map_half, 2.5f);

	// 1. Base rolling terrain with multi-octave fBm
	hm.apply_fbm(rng, 4, 0.030f, 0.52f, 2.1f, 5.0f);
	hm.diamond_square(rng, 2.5f, 0.65f);

	// The noise alone can sink most of the map (seed 7 left only the base pads dry,
	// so every player started on their own island).
	lift_noise_to_land(hm, water_h, 0.68f, 15);

	// 2. Radial coastal falloff
	hm.apply_radial_falloff(0.68f, 0.95f, -2.0f);

	// 3. Central Meandering River with Walkable Ford
	std::vector<std::pair<float, float>> river_pts;
	float half = hm.map_half;
	river_pts.push_back({-half * 0.85f, -half * 0.70f});
	river_pts.push_back({-half * 0.40f, -half * 0.30f});
	river_pts.push_back({-half * 0.10f, -half * 0.05f});
	river_pts.push_back({half * 0.15f, half * 0.10f});
	river_pts.push_back({half * 0.45f, half * 0.40f});
	river_pts.push_back({half * 0.85f, half * 0.75f});

	float ford_dist = half * 0.75f;
	hm.carve_spline_path(river_pts, 4.0f, 2.0f, water_h, ford_dist, 8.0f);

	// 4. Smoothing passes
	hm.blur(2, 0.45f);

	// 5. Compute Balanced Symmetrical Spawns
	spawns.clear();
	float spawn_r = half * 0.55f;
	float start_angle = 3.14159265f * 0.25f; // Diagonally aligned

	for (int i = 0; i < num_players; ++i) {
		float angle = start_angle + (6.2831853f * i) / (float)num_players;
		float sx = det::cos(angle) * spawn_r;
		float sz = det::sin(angle) * spawn_r;

		// Face center
		float fx = -det::cos(angle);
		float fz = -det::sin(angle);

		float bh = std::max(water_h + 1.2f, hm.sample_world(sx, sz));
		spawns.push_back({(uint8_t)i, sx, sz, bh, fx, fz});
		hm.level_area(sx, sz, 14.0f, 6.0f, bh);
	}
}

void RmsGenerator::synth_archipelago(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	water_h = 2.0f;
	hm.init(hm.res, hm.map_half, -1.8f); // Underwater seabed

	float half = hm.map_half;
	spawns.clear();

	// 1. Home Islands for Players: archipelago.js's islandRadius, scaleByMapSize(22, 31) tiles
	// (one RTS0 unit per tile), so a bigger map gives each player the land and trees it needs.
	const float island_r = 22.0f + 9.0f * std::max(0.0f, 2.0f * half - 128.0f) / 384.0f;
	float spawn_r = half * 0.56f;
	for (int i = 0; i < num_players; ++i) {
		float angle = (6.2831853f * i) / (float)num_players + 0.35f;
		float sx = det::cos(angle) * spawn_r;
		float sz = det::sin(angle) * spawn_r;
		float fx = -det::cos(angle);
		float fz = -det::sin(angle);

		// Raise player island
		hm.level_area(sx, sz, island_r, 12.0f, 4.5f);
		spawns.push_back({(uint8_t)i, sx, sz, 4.5f, fx, fz});
	}

	// 2. Neutral Islands (Center & Intermediary)
	hm.level_area(0.0f, 0.0f, 14.0f, 10.0f, 4.0f); // Rich central relic island

	for (int i = 0; i < num_players; ++i) {
		float angle = (6.2831853f * (i + 0.5f)) / (float)num_players + 0.35f;
		float nx = det::cos(angle) * (half * 0.65f);
		float nz = det::sin(angle) * (half * 0.65f);
		hm.level_area(nx, nz, 10.0f, 8.0f, 3.5f);
	}

	// 3. Add organic fractal noise to islands
	hm.apply_fbm(rng, 3, 0.040f, 0.5f, 2.0f, 2.2f);
	hm.blur(2, 0.4f);

	// Re-level bases to ensure flat placement
	for (const auto &sp : spawns) {
		hm.level_area(sp.x, sp.z, 13.0f, 5.0f, sp.base_height);
	}
}

void RmsGenerator::synth_oasis(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	water_h = 0.0f;
	hm.init(hm.res, hm.map_half, 2.0f);

	float half = hm.map_half;

	// 1. Sand Dunes Wave Terrain
	hm.apply_fbm(rng, 3, 0.025f, 0.5f, 2.0f, 2.5f);

	// 2. Central Oasis Lake
	float oasis_radius = half * 0.22f;
	hm.carve_crater_lake(0.0f, 0.0f, oasis_radius, 8.0f, 2.5f, water_h);

	// 3. Dense Perimeter Dunes/Ridge
	hm.apply_border_falloff(half * 0.20f, 6.0f);
	hm.blur(1, 0.35f);

	// 4. Symmetrical Oasis Player Spawns in Desert Ring
	spawns.clear();
	float spawn_r = half * 0.55f;
	for (int i = 0; i < num_players; ++i) {
		float angle = (6.2831853f * i) / (float)num_players + 0.2f;
		float sx = det::cos(angle) * spawn_r;
		float sz = det::sin(angle) * spawn_r;
		float fx = -det::cos(angle);
		float fz = -det::sin(angle);
		float bh = hm.sample_world(sx, sz);

		spawns.push_back({(uint8_t)i, sx, sz, bh, fx, fz});
		hm.level_area(sx, sz, 14.0f, 6.0f, bh);
	}
}

void RmsGenerator::synth_highlands(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	water_h = -15.0f; // High mountain dry map
	hm.init(hm.res, hm.map_half, 3.0f);

	// 1. Jagged Mountain Ridges
	hm.apply_ridged_noise(rng, 5, 0.022f, 0.55f, 2.2f, 16.0f);
	hm.apply_fbm(rng, 3, 0.040f, 0.50f, 2.0f, 4.0f);

	// 2. Carve Mountain Passes connecting quadrants
	float half = hm.map_half;
	std::vector<std::pair<float, float>> pass1 = {{-half * 0.8f, 0.0f}, {0.0f, 0.0f}, {half * 0.8f, 0.0f}};
	std::vector<std::pair<float, float>> pass2 = {{0.0f, -half * 0.8f}, {0.0f, 0.0f}, {0.0f, half * 0.8f}};
	hm.carve_spline_path(pass1, 8.0f, 4.0f, 3.0f);
	hm.carve_spline_path(pass2, 8.0f, 4.0f, 3.0f);

	// 3. Plateau Valleys for Player Starts
	spawns.clear();
	float spawn_r = half * 0.56f;
	for (int i = 0; i < num_players; ++i) {
		float angle = (6.2831853f * i) / (float)num_players + 0.785f;
		float sx = det::cos(angle) * spawn_r;
		float sz = det::sin(angle) * spawn_r;
		float fx = -det::cos(angle);
		float fz = -det::sin(angle);

		float bh = 4.5f;
		hm.level_area(sx, sz, 16.0f, 8.0f, bh);
		spawns.push_back({(uint8_t)i, sx, sz, bh, fx, fz});
	}

	hm.thermal_erosion(8, 0.6f, 0.2f);
	hm.blur(1, 0.35f);
}

void RmsGenerator::synth_valley(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	water_h = 0.0f;
	hm.init(hm.res, hm.map_half, 1.5f);

	float half = hm.map_half;
	float step = (half * 2.0f) / (float)(hm.res - 1);

	// 1. High Mountain Ridges on North & South
	for (int z = 0; z < hm.res; ++z) {
		float wz = -half + z * step;
		float abs_z = std::abs(wz);
		if (abs_z > half * 0.38f) {
			float t = smoothstep(half * 0.38f, half * 0.95f, abs_z);
			for (int x = 0; x < hm.res; ++x) {
				hm.add(x, z, t * 14.0f);
			}
		}
	}

	// 2. Gentle valley floor noise
	hm.apply_fbm(rng, 3, 0.035f, 0.5f, 2.0f, 2.5f);

	// 3. Central Valley River flowing West to East
	std::vector<std::pair<float, float>> river_pts = {
		{-half * 0.9f, 0.0f},
		{-half * 0.45f, 4.0f},
		{0.0f, -2.0f},
		{half * 0.45f, 3.0f},
		{half * 0.9f, 0.0f}
	};
	float ford_dist = half * 0.9f; // Center ford
	hm.carve_spline_path(river_pts, 3.5f, 1.8f, water_h, ford_dist, 7.0f);

	hm.blur(1, 0.4f);

	// 4. Opposing Player Spawns
	spawns.clear();
	if (num_players == 2) {
		float sx0 = -half * 0.58f, sz0 = 0.0f;
		float sx1 = +half * 0.58f, sz1 = 0.0f;
		hm.level_area(sx0, sz0, 14.0f, 6.0f, 2.0f);
		hm.level_area(sx1, sz1, 14.0f, 6.0f, 2.0f);
		spawns.push_back({0, sx0, sz0, 2.0f, 1.0f, 0.0f});
		spawns.push_back({1, sx1, sz1, 2.0f, -1.0f, 0.0f});
	} else {
		for (int i = 0; i < num_players; ++i) {
			float side = (i % 2 == 0) ? -1.0f : 1.0f;
			// Teammates on one side spread along the valley (0.28 to 0.82 of the half-width out),
			// a little to either side of the river in turn, rather than stacked across the narrow floor.
			const int k = (num_players + (i % 2 == 0 ? 1 : 0)) / 2, j = i / 2;
			const float t = k > 1 ? (float)j / (float)(k - 1) : 0.5f;
			float sx = side * half * (0.82f - 0.54f * t);
			float sz = (j % 2 == 0 ? -1.0f : 1.0f) * half * 0.2f;
			float fx = -side;
			float fz = 0.0f;
			float bh = 2.0f;
			hm.level_area(sx, sz, 14.0f, 6.0f, bh);
			spawns.push_back({(uint8_t)i, sx, sz, bh, fx, fz});
		}
	}
}

void RmsGenerator::synth_mediterranean(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	water_h = 1.0f;
	hm.init(hm.res, hm.map_half, 3.0f);

	float half = hm.map_half;

	// 1. Central Sea Depression
	float sea_radius = half * 0.44f;
	hm.carve_crater_lake(0.0f, 0.0f, sea_radius, 10.0f, 3.5f, water_h);

	// 2. Central Treasure Island in the Sea
	hm.level_area(0.0f, 0.0f, 8.0f, 6.0f, 3.0f);

	// 3. Coastal land ring rolling hills
	hm.apply_fbm(rng, 4, 0.030f, 0.5f, 2.1f, 4.0f);
	hm.apply_radial_falloff(0.85f, 0.98f, 6.0f); // High outer hills
	hm.blur(2, 0.4f);

	// 4. Spawns positioned along the fertile coastal land ring
	spawns.clear();
	float spawn_r = half * 0.62f;
	for (int i = 0; i < num_players; ++i) {
		float angle = (6.2831853f * i) / (float)num_players + 0.39f;
		float sx = det::cos(angle) * spawn_r;
		float sz = det::sin(angle) * spawn_r;
		float fx = -det::cos(angle);
		float fz = -det::sin(angle);
		float bh = std::max(water_h + 1.2f, hm.sample_world(sx, sz));

		hm.level_area(sx, sz, 14.0f, 6.0f, bh);
		spawns.push_back({(uint8_t)i, sx, sz, bh, fx, fz});
	}
}

void RmsGenerator::synth_acropolis(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	water_h = -15.0f;
	hm.init(hm.res, hm.map_half, 1.0f);

	float half = hm.map_half;

	// 1. Valley floor with rolling lowlands
	hm.apply_fbm(rng, 3, 0.035f, 0.5f, 2.0f, 2.0f);

	// 2. Symmetrical Acropolis Plateaus for each player
	spawns.clear();
	float spawn_r = half * 0.54f;

	for (int i = 0; i < num_players; ++i) {
		float angle = (6.2831853f * i) / (float)num_players + 0.35f;
		float sx = det::cos(angle) * spawn_r;
		float sz = det::sin(angle) * spawn_r;
		float fx = -det::cos(angle);
		float fz = -det::sin(angle);
		float ramp_angle = det::atan2(fz, fx); // Ramp slopes down towards center

		// 5.4 high: the plateau of the 0.35-a-metre height scale (7.5) at
		// HEIGHT_PER_METRE, so its ramp stays under the land slope limit.
		hm.raise_plateau(sx, sz, 14.0f, ramp_angle, 0.45f, 5.4f, 3.5f);
		float bh = hm.sample_world(sx, sz);
		spawns.push_back({(uint8_t)i, sx, sz, bh, fx, fz});
	}

	hm.blur(1, 0.35f);
}

void RmsGenerator::synth_spartan_valley(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	water_h = 0.0f;
	synth_valley(hm, rng, water_h, spawns, num_players);
}

void RmsGenerator::synth_sahara(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	water_h = -15.0f;
	hm.init(hm.res, hm.map_half, 1.5f);

	// Desert dunes
	hm.apply_fbm(rng, 4, 0.020f, 0.6f, 2.0f, 4.5f);
	hm.terrace(1.8f, 0.3f);
	hm.blur(1, 0.3f);

	float half = hm.map_half;
	spawns.clear();
	float spawn_r = half * 0.55f;

	for (int i = 0; i < num_players; ++i) {
		float angle = (6.2831853f * i) / (float)num_players + 0.4f;
		float sx = det::cos(angle) * spawn_r;
		float sz = det::sin(angle) * spawn_r;
		float fx = -det::cos(angle);
		float fz = -det::sin(angle);
		float bh = hm.sample_world(sx, sz);

		hm.level_area(sx, sz, 14.0f, 6.0f, bh);
		spawns.push_back({(uint8_t)i, sx, sz, bh, fx, fz});
	}
}

void RmsGenerator::synth_peloponnese(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	water_h = 0.5f;
	synth_continental(hm, rng, water_h, spawns, num_players);
}

void RmsGenerator::synth_anatolian_plateau(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// anatolian_plateau.js: open steppe at heightLand 1 with bumps
	// (heightOffsetBump 2, scaleByMapSize(300, 800) of them) and no water,
	// heights in metres over 0.28's water line. RTS0 keeps its bases and rim
	// mines; 0.28 has no ridges or canyons, so the old ridged noise, terraces
	// and canyons (36 m deep, flooded below the water line) are gone.
	water_h = -10.0f; // no water
	const float land = 1.0f * kMetre;
	hm.init(hm.res, hm.map_half, land);

	float half = hm.map_half;

	// 1. Bumps on the steppe.
	add_bumps(hm, rng, 2.0f * kMetre);
	hm.blur(1, 0.35f);

	// 2. Player Bases on the steppe
	spawns.clear();
	float spawn_r = half * 0.48f;
	std::vector<std::pair<float, float>> locs;
	if (t_team_placement.pattern >= 0)
		locs = pattern_locs(hm, rng, t_team_placement.pattern, num_players, t_team_placement.teams, spawn_r, 0.2f * half,
		                    rng.range_float(0.0f, 6.2831853f));
	for (int i = 0; i < num_players; ++i) {
		float angle = (6.2831853f * i) / (float)num_players + 0.52f;
		float sx = det::cos(angle) * spawn_r;
		float sz = det::sin(angle) * spawn_r;
		if (!locs.empty()) { sx = locs[(size_t)i].first; sz = locs[(size_t)i].second; angle = det::atan2(sz, sx); }
		float fx = -det::cos(angle);
		float fz = -det::sin(angle);
		float bh = std::max(land, hm.sample_world(sx, sz));

		hm.level_area(sx, sz, 15.0f, 6.0f, bh);
		spawns.push_back({(uint8_t)i, sx, sz, bh, fx, fz});
	}
}

// =========================================================================
// Entity & Fair Resource Distribution
// =========================================================================

// =========================================================================
// Maps modelled on 0 A.D. maps/random/{mainland,alpine_lakes,lake,
// gulf_of_bothnia,ardennes_forest,neareastern_badlands}.js
// =========================================================================

namespace {
// Players evenly on an arc of a circle (full circle when arc >= 2*pi), each
// base levelled at max(terrain, min_h) and facing the map centre.
void place_ring_spawns(HeightmapGrid &hm, std::vector<RmsGenerator::PlayerSpawn> &spawns, int num_players,
		float radius, float start_angle, float arc, float min_h) {
	spawns.clear();
	const bool full = arc >= 6.2831853f;
	for (int i = 0; i < num_players; ++i) {
		float t = full ? (float)i / (float)num_players
		               : (num_players > 1 ? (float)i / (float)(num_players - 1) : 0.5f);
		float angle = start_angle + arc * t;
		float sx = det::cos(angle) * radius;
		float sz = det::sin(angle) * radius;
		float bh = std::max(min_h, hm.sample_world(sx, sz));
		spawns.push_back({(uint8_t)i, sx, sz, bh, -det::cos(angle), -det::sin(angle)});
		hm.level_area(sx, sz, 14.0f, 6.0f, bh);
	}
}

// playerPlacementByPattern for the maps with Team Placement: Circle keeps the
// map's ring (place_ring_spawns over the full circle); the other patterns come
// from pattern_locs with the ring's radius as the distance (river_distance for
// River when set) and teammates 0.1 of the map apart, each base levelled at max(terrain, min_h) and facing the centre.
void place_team_spawns(HeightmapGrid &hm, RmsRng &rng, std::vector<RmsGenerator::PlayerSpawn> &spawns, int num_players,
		float radius, float start_angle, float min_h, float river_distance = 0.0f) {
	if (t_team_placement.pattern < 0) {
		place_ring_spawns(hm, spawns, num_players, radius, start_angle, 6.2831853f, min_h);
		return;
	}
	// Some scripts set the two River lines further apart (teamDist) than their ring.
	const float distance = t_team_placement.pattern == 4 && river_distance > 0.0f ? river_distance : radius;
	const auto locs = pattern_locs(hm, rng, t_team_placement.pattern, num_players, t_team_placement.teams, distance,
	                               0.2f * hm.map_half, start_angle);
	spawns.clear();
	for (int i = 0; i < num_players; ++i) {
		const float sx = locs[(size_t)i].first, sz = locs[(size_t)i].second, r = det::hypot(sx, sz);
		const float bh = std::max(min_h, hm.sample_world(sx, sz));
		spawns.push_back({(uint8_t)i, sx, sz, bh, r > 1e-3f ? -sx / r : 1.0f, r > 1e-3f ? -sz / r : 0.0f});
		hm.level_area(sx, sz, 14.0f, 6.0f, bh);
	}
}

// Raises terrain near (x, z) back up to at least h (undoing a lake or canyon
// that cut into a base), with a smooth edge.
void lift_area(HeightmapGrid &hm, float cx, float cz, float radius, float blend, float h) {
	const float step = (hm.map_half * 2.0f) / (float)(hm.res - 1);
	for (int z = 0; z < hm.res; ++z) {
		const float wz = -hm.map_half + z * step;
		for (int x = 0; x < hm.res; ++x) {
			const float wx = -hm.map_half + x * step;
			const float d = std::sqrt((wx - cx) * (wx - cx) + (wz - cz) * (wz - cz));
			if (d > radius + blend) continue;
			const float t = d <= radius ? 1.0f : 1.0f - smoothstep(radius, radius + blend, d);
			const float cur = hm.get(x, z);
			if (cur < h) hm.set(x, z, cur + (h - cur) * t);
		}
	}
}
} // namespace

// =========================================================================
// Maps modelled on 0 A.D. maps/random/{rivers,canyon,corinthian_isthmus,
// belgian_uplands,lorraine_plain,hyrcanian_shores}.js
// =========================================================================

namespace {
// 0 A.D. metres (water level 0) -> RTS0 height, keeping low land dry and the
// 2 m shallows wadeable (defined with the coast maps below).
inline float coast_metres(float water_h, float m);

// Flat land at `m` metres with up to `bump_m` metres of rolling bumps on top
// (fbm, never below the land height, so low plains don't dip under water).
void init_land(HeightmapGrid &hm, RmsRng &rng, float water_h, float m, float bump_m, int octaves, float freq) {
	const float amp = 0.5f * bump_m * HEIGHT_PER_METRE;
	hm.init(hm.res, hm.map_half, coast_metres(water_h, m) + amp);
	hm.apply_fbm(rng, octaves, freq, 0.5f, 2.0f, amp);
}

// Players alternate between two sides of the map (player 0 on the negative
// side, player 1 on the positive side, and so on), spread evenly along the
// side. along_x = true puts the sides at -z and +z and spreads players in x.
void place_side_spawns(HeightmapGrid &hm, std::vector<RmsGenerator::PlayerSpawn> &spawns, int num_players,
		bool along_x, float side_dist, float spread, float min_h) {
	spawns.clear();
	const int side_count[2] = {(num_players + 1) / 2, num_players / 2};
	for (int i = 0; i < num_players; ++i) {
		const int side = i & 1;
		const int n = side_count[side];
		const int j = i / 2;
		const float along = n > 1 ? -spread + 2.0f * spread * (float)j / (float)(n - 1) : 0.0f;
		const float across = side ? side_dist : -side_dist;
		const float sx = along_x ? along : across;
		const float sz = along_x ? across : along;
		const float bh = std::max(min_h, hm.sample_world(sx, sz));
		const float fx = along_x ? 0.0f : (side ? -1.0f : 1.0f);
		const float fz = along_x ? (side ? -1.0f : 1.0f) : 0.0f;
		spawns.push_back({(uint8_t)i, sx, sz, bh, fx, fz});
		hm.level_area(sx, sz, 14.0f, 6.0f, bh);
	}
}

// A ford across a river: a dry patch just above the water line (navcells
// count as water below water_h + 0.3).
void place_ford(HeightmapGrid &hm, float x, float z, float radius, float water_h) {
	lift_area(hm, x, z, radius, 3.0f, water_h + 0.6f);
}
} // namespace

void RmsGenerator::synth_corinthian_isthmus(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// corinthian_isthmus.js: two lands (north and south) joined by a narrow
	// isthmus in the middle; the sea fills the gulfs on either side of it.
	// Heights in metres: land 3, sea ground -7.
	water_h = 1.0f;
	init_land(hm, rng, water_h, 3.0f, 1.0f, 4, 0.022f);
	const float half = hm.map_half, land = coast_metres(water_h, 3.0f), sea = coast_metres(water_h, -7.0f);
	const float isthmus = half * 0.10f;
	const float gulf = half * 0.17f;
	const float phase = rng.range_float(0.0f, 6.2831853f);
	const float step = (half * 2.0f) / (float)(hm.res - 1);
	for (int z = 0; z < hm.res; ++z) {
		const float wz = -half + z * step;
		for (int x = 0; x < hm.res; ++x) {
			const float wx = -half + x * step;
			const float w = gulf * (1.0f + 0.25f * det::sin(wx / half * 4.0f + phase));
			// 1 inside the gulfs, 0 on land, smooth at the shore and at the isthmus.
			const float t = (1.0f - smoothstep(w * 0.7f, w * 1.3f, std::fabs(wz))) *
			                smoothstep(isthmus, isthmus + half * 0.10f, std::fabs(wx));
			if (t > 0.0f) {
				const float cur = hm.get(x, z);
				hm.set(x, z, cur + (sea - cur) * t);
			}
		}
	}
	hm.blur(1, 0.4f);
	place_side_spawns(hm, spawns, num_players, true, half * 0.58f, half * 0.62f, land);
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 16.0f, 6.0f, sp.base_height);
}

void RmsGenerator::synth_lorraine_plain(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// lorraine_plain.js: a flat plain with a river from north to south and a
	// few fords; players on the west and east banks. Heights in metres: land
	// 3 with bumps of 2, river bed -4.
	water_h = 1.0f;
	init_land(hm, rng, water_h, 3.0f, 2.0f, 3, 0.020f);
	const float half = hm.map_half, land = coast_metres(water_h, 3.0f);
	std::vector<std::pair<float, float>> river;
	for (int k = 0; k <= 6; ++k) {
		const float z = -half * 1.05f + half * 2.1f * (float)k / 6.0f;
		river.push_back({rng.range_float(-0.08f, 0.08f) * half, z});
	}
	hm.carve_spline_path(river, 3.0f + half * 0.025f, water_h - coast_metres(water_h, -4.0f), water_h);
	const int fords = scale_by_map_size(3, 6, half);
	for (int f = 0; f < fords; ++f) {
		// Ford k sits on the river at an even fraction of its length.
		const float t = ((float)f + 0.5f) / (float)fords * 6.0f;
		const int k = std::min(5, (int)t);
		const float u = t - (float)k;
		const float fx = river[k].first + (river[k + 1].first - river[k].first) * u;
		const float fz = river[k].second + (river[k + 1].second - river[k].second) * u;
		place_ford(hm, fx, fz, 2.0f * (3.0f + half * 0.025f), water_h);
	}
	hm.blur(1, 0.4f);
	place_side_spawns(hm, spawns, num_players, false, half * 0.58f, half * 0.62f, land);
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 16.0f, 6.0f, sp.base_height);
}

void RmsGenerator::synth_hyrcanian_shores(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// hyrcanian_shores.js: the sea along the south (-z) edge, a coastal strip
	// with the players, and wooded highlands rising to the north. Heights in
	// metres: land 1 with bumps of 4, hills 15, sea ground -3.
	water_h = 1.0f;
	init_land(hm, rng, water_h, 1.0f, 4.0f, 4, 0.024f);
	const float half = hm.map_half, land = coast_metres(water_h, 1.0f), sea = coast_metres(water_h, -3.0f);
	const float phase = rng.range_float(0.0f, 6.2831853f);
	const float step = (half * 2.0f) / (float)(hm.res - 1);
	for (int z = 0; z < hm.res; ++z) {
		const float wz = -half + z * step;
		for (int x = 0; x < hm.res; ++x) {
			const float wx = -half + x * step;
			const float coast = -half * (0.62f + 0.06f * det::sin(wx / half * 3.0f + phase));
			const float cur = hm.get(x, z);
			if (wz < coast + half * 0.10f) {
				const float t = smoothstep(coast + half * 0.10f, coast - half * 0.06f, wz);
				hm.set(x, z, cur + (sea - cur) * t);
			} else {
				// Highlands: up to the hills' 15 m towards the north edge.
				hm.add(x, z, 14.0f * HEIGHT_PER_METRE * smoothstep(half * 0.30f, half * 0.95f, wz));
			}
		}
	}
	hm.blur(1, 0.4f);
	spawns.clear();
	// An ellipse squeezed onto the coastal strip.
	const float start = rng.range_float(0.0f, 6.2831853f);
	for (int i = 0; i < num_players; ++i) {
		const float a = start + 6.2831853f * (float)i / (float)std::max(1, num_players);
		const float sx = det::cos(a) * half * 0.64f;
		const float sz = -half * 0.05f + det::sin(a) * half * 0.30f;
		const float bh = std::max(land, hm.sample_world(sx, sz));
		const float len = std::max(1e-3f, std::sqrt(sx * sx + sz * sz));
		spawns.push_back({(uint8_t)i, sx, sz, bh, -sx / len, -sz / len});
		hm.level_area(sx, sz, 14.0f, 6.0f, bh);
	}
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 16.0f, 6.0f, sp.base_height);
}

// =========================================================================
// Maps modelled on 0 A.D. maps/random/{northern_lights,kerala,polar_sea,
// sahel,african_plains,alpine_valley}.js. 0 A.D. map fractions f become
// world coordinates (f - 0.5) * 2 * map_half.
// =========================================================================

namespace {
// Players on a line through (cx, cz), spread over [-spread, spread] along x
// (along_x) or z. With more than 4 players every other one steps `stagger`
// across the line, so bases on small maps don't overlap.
void place_line_spawns(HeightmapGrid &hm, std::vector<RmsGenerator::PlayerSpawn> &spawns, int num_players,
		bool along_x, float cx, float cz, float spread, float stagger, float face_x, float face_z, float min_h) {
	spawns.clear();
	for (int i = 0; i < num_players; ++i) {
		const float along = num_players > 1 ? -spread + 2.0f * spread * (float)i / (float)(num_players - 1) : 0.0f;
		const float across = (num_players > 4 && (i & 1)) ? stagger : 0.0f;
		const float sx = along_x ? cx + along : cx + across;
		const float sz = along_x ? cz + across : cz + along;
		const float bh = std::max(min_h, hm.sample_world(sx, sz));
		spawns.push_back({(uint8_t)i, sx, sz, bh, face_x, face_z});
		hm.level_area(sx, sz, 14.0f, 6.0f, bh);
	}
}

// Sets terrain to `h` inside a band along one edge of the map: x < edge
// (axis 0), z > edge (axis 1). The shoreline wobbles by `wobble` and blends
// over `blend`.
void flood_edge(HeightmapGrid &hm, RmsRng &rng, int axis, float edge, float wobble, float blend, float h) {
	const float half = hm.map_half;
	const float step = (half * 2.0f) / (float)(hm.res - 1);
	const float p1 = rng.range_float(0.0f, 6.2831853f), p2 = rng.range_float(0.0f, 6.2831853f);
	for (int z = 0; z < hm.res; ++z) {
		const float wz = -half + z * step;
		for (int x = 0; x < hm.res; ++x) {
			const float wx = -half + x * step;
			const float along = axis == 0 ? wz : wx;
			const float line = edge + wobble * (0.6f * det::sin(along / half * 3.1f + p1) + 0.4f * det::sin(along / half * 7.3f + p2));
			// d > 0 inside the flooded band.
			const float d = axis == 0 ? line - wx : wz - line;
			if (d <= -blend) continue;
			const float t = smoothstep(-blend, blend, d);
			const float cur = hm.get(x, z);
			hm.set(x, z, cur + (h - cur) * t);
		}
	}
}

// Ponds of radius r scattered at least `clear` from every spawn and `gap`
// from each other, inside `inner` of the centre. Spawns must already be set.
void scatter_ponds(HeightmapGrid &hm, RmsRng &rng, const std::vector<RmsGenerator::PlayerSpawn> &spawns, int count,
		float r, float clear, float gap, float inner, float depth, float water_h) {
	std::vector<std::pair<float, float>> placed;
	for (int i = 0, tries = 0; i < count && tries < count * 40; ++tries) {
		const float a = rng.range_float(0.0f, 6.2831853f);
		const float d = std::sqrt(rng.next_float()) * inner;
		const float px = det::cos(a) * d, pz = det::sin(a) * d;
		bool ok = true;
		for (const auto &sp : spawns)
			if (det::hypot(px - sp.x, pz - sp.z) < clear + r) { ok = false; break; }
		for (const auto &p : placed)
			if (det::hypot(px - p.first, pz - p.second) < gap + 2.0f * r) { ok = false; break; }
		if (!ok) continue;
		hm.carve_crater_lake(px, pz, r, r * 0.5f, depth, water_h);
		placed.push_back({px, pz});
		++i;
	}
}

// Raises a mountain ridge of half-width `w` to height `h` along a polyline,
// leaving a passage of length `pass_len` centred `pass_at` along it.
void raise_ridge(HeightmapGrid &hm, const std::vector<std::pair<float, float>> &pts, float w, float h,
		float pass_at, float pass_len, float blend = -1.0f) {
	const float half = hm.map_half;
	const float step = (half * 2.0f) / (float)(hm.res - 1);
	float minx = 1e9f, maxx = -1e9f, minz = 1e9f, maxz = -1e9f;
	for (const auto &p : pts) {
		minx = std::min(minx, p.first); maxx = std::max(maxx, p.first);
		minz = std::min(minz, p.second); maxz = std::max(maxz, p.second);
	}
	const float reach = blend >= 0.0f ? w + blend : w * 2.0f;
	const int x0 = std::max(0, (int)((minx - reach + half) / step)), x1 = std::min(hm.res - 1, (int)((maxx + reach + half) / step) + 1);
	const int z0 = std::max(0, (int)((minz - reach + half) / step)), z1 = std::min(hm.res - 1, (int)((maxz + reach + half) / step) + 1);
	for (int z = z0; z <= z1; ++z) {
		const float wz = -half + z * step;
		for (int x = x0; x <= x1; ++x) {
			const float wx = -half + x * step;
			float best = 1e9f, along_best = 0.0f, along = 0.0f;
			for (size_t i = 0; i + 1 < pts.size(); ++i) {
				const float ax = pts[i].first, az = pts[i].second;
				const float bx = pts[i + 1].first - ax, bz = pts[i + 1].second - az;
				const float len2 = bx * bx + bz * bz, len = std::sqrt(len2);
				const float t = len2 > 0.0f ? std::clamp(((wx - ax) * bx + (wz - az) * bz) / len2, 0.0f, 1.0f) : 0.0f;
				const float d = det::hypot(wx - (ax + bx * t), wz - (az + bz * t));
				if (d < best) { best = d; along_best = along + len * t; }
				along += len;
			}
			if (best >= reach) continue;
			if (std::fabs(along_best - pass_at) < pass_len * 0.5f) continue;
			const float t = 1.0f - smoothstep(w, reach, best);
			const float cur = hm.get(x, z);
			if (cur < h) hm.set(x, z, cur + (h - cur) * t);
		}
	}
}
} // namespace

void RmsGenerator::synth_northern_lights(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// northern_lights.js: snow land, the sea along the north edge (its
	// shore at f = 0.69) with ice floes, a few lakes; players on a line at
	// f = 0.45. Scarce wood (distribute_neutral_resources). Heights in
	// metres: land 3, sea ground -5, lakes -4.
	water_h = 1.0f;
	init_land(hm, rng, water_h, 3.0f, 1.0f, 3, 0.02f);
	const float half = hm.map_half, land = coast_metres(water_h, 3.0f);
	flood_edge(hm, rng, 1, half * 0.38f, half * 0.05f, half * 0.04f, coast_metres(water_h, -5.0f));
	// Ice floes: islands in the sea (0 A.D.: scaleByMapSize(10, 80) islands).
	const int floes = scale_by_map_size(4, 16, half);
	for (int i = 0; i < floes; ++i) {
		const float fx = rng.range_float(-0.85f, 0.85f) * half;
		const float fz = rng.range_float(0.62f, 0.90f) * half;
		lift_area(hm, fx, fz, half * rng.range_float(0.03f, 0.06f), 2.0f, land);
	}
	place_line_spawns(hm, spawns, num_players, true, 0.0f, -half * 0.10f, half * 0.66f, -half * 0.28f, 0.0f, 1.0f, land);
	scatter_ponds(hm, rng, spawns, scale_by_map_size(1, 4, half) * std::max(1, num_players / 2), half * 0.07f + 2.0f,
	              20.0f, 20.0f, half * 0.85f, water_h - coast_metres(water_h, -4.0f), water_h);
	hm.blur(1, 0.4f);
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 16.0f, 6.0f, sp.base_height);
}

void RmsGenerator::synth_kerala(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// kerala.js: the sea along the west edge (shore at f = 0.31), players on
	// a north-south line at f = 0.55, mountains east of f = 0.69. Land at
	// heightLand 3, the sea bed at heightSeaGround -5, hills set to heightHill
	// 25 with a blend of 3.
	water_h = 1.0f;
	const float land = metres(water_h, 3.0f);
	hm.init(hm.res, hm.map_half, land);
	const float half = hm.map_half;
	hm.apply_fbm(rng, 4, 0.024f, 0.5f, 2.0f, 1.0f * kMetre);
	flood_edge(hm, rng, 0, -half * 0.38f, half * 0.06f, half * 0.04f, metres(water_h, -5.0f));
	// Western Ghats: hills east of f = 0.69.
	const int hills = scale_by_map_size(6, 24, half);
	for (int i = 0; i < hills; ++i) {
		const float hx = rng.range_float(0.42f, 0.98f) * half;
		const float hz = rng.range_float(-0.95f, 0.95f) * half;
		hm.raise_plateau(hx, hz, half * rng.range_float(0.05f, 0.10f), 0.0f, 0.0f, (25.0f - 3.0f) * kMetre, 3.0f);
	}
	for (float &h : hm.data) h = std::min(h, metres(water_h, 25.0f)); // ELEVATION_SET: overlaps stay at 25
	place_line_spawns(hm, spawns, num_players, false, half * 0.10f, 0.0f, half * 0.68f, -half * 0.24f, -1.0f, 0.0f, land);
	hm.blur(1, 0.4f);
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 16.0f, 6.0f, sp.base_height);
}

void RmsGenerator::synth_polar_sea(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// polar_sea.js: icy land with a central sea (f = 0.17 across) and small
	// lakes, players on a circle at f = 0.35, glacier hills. 0 A.D. has no
	// wood here at all; RTS0 keeps a few groves so the AI can build. Heights
	// in metres: land 2 (cliffs 3), sea and lake ground -10.
	water_h = 1.0f;
	init_land(hm, rng, water_h, 2.0f, 1.0f, 4, 0.03f);
	const float half = hm.map_half, land = coast_metres(water_h, 2.0f), deep = water_h - coast_metres(water_h, -10.0f);
	hm.carve_crater_lake(0.0f, 0.0f, half * 0.30f, half * 0.08f, deep, water_h);
	place_ring_spawns(hm, spawns, num_players, half * 0.70f, rng.range_float(0.0f, 6.2831853f), 6.2831853f, land);
	scatter_ponds(hm, rng, spawns, scale_by_map_size(10, 16, half) / 2, half * 0.05f + 2.0f, 20.0f, 12.0f, half * 0.90f, deep, water_h);
	hm.blur(1, 0.4f);
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 16.0f, 6.0f, sp.base_height);
}

void RmsGenerator::synth_sahel(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// sahel.js: open, nearly flat savanna, players on a circle at f = 0.35,
	// one to three water holes away from the bases; little wood, plenty of
	// mines and hunt. Land at heightLand 1 (flat but for a faint half-metre
	// roll), the holes set to heightSeaGround -5 with a blend of 7; through
	// coast_metres, so the 1 m plain stays dry in RTS0's nav grid.
	water_h = 0.5f;
	const float land = coast_metres(water_h, 1.0f);
	hm.init(hm.res, hm.map_half, land);
	const float half = hm.map_half;
	hm.apply_fbm(rng, 3, 0.02f, 0.5f, 2.0f, 0.5f * kMetre);
	place_team_spawns(hm, rng, spawns, num_players, half * 0.70f, rng.range_float(0.0f, 6.2831853f), land);
	scatter_ponds(hm, rng, spawns, scale_by_map_size(1, 3, half), half * 0.07f + 2.0f, 24.0f, 20.0f, half * 0.60f,
	              water_h - coast_metres(water_h, -5.0f), water_h);
	hm.blur(1, 0.4f);
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 16.0f, 6.0f, sp.base_height);
}

void RmsGenerator::synth_african_plains(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// african_plains.js: lush plains with many watering holes (4-10) and
	// hills (6-16), players on a circle at f = 0.35. Land at heightLand 2
	// with 2 m bumps (createBumps), hills set to 18 m (createHills, blend
	// 2), the holes set to heightSeaGround -5.
	water_h = 1.0f;
	const float land = metres(water_h, 2.0f);
	hm.init(hm.res, hm.map_half, land);
	const float half = hm.map_half;
	add_bumps(hm, rng, 2.0f * kMetre);
	place_team_spawns(hm, rng, spawns, num_players, half * 0.70f, rng.range_float(0.0f, 6.2831853f), land);
	const int hills = scale_by_map_size(6, 16, half);
	for (int i = 0, tries = 0; i < hills && tries < hills * 40; ++tries) {
		const float a = rng.range_float(0.0f, 6.2831853f);
		const float d = std::sqrt(rng.next_float()) * half * 0.9f;
		const float hx = det::cos(a) * d, hz = det::sin(a) * d;
		bool ok = true;
		for (const auto &sp : spawns)
			if (det::hypot(hx - sp.x, hz - sp.z) < 30.0f) { ok = false; break; }
		if (!ok) continue;
		hm.raise_plateau(hx, hz, half * rng.range_float(0.04f, 0.07f), 0.0f, 0.0f, (18.0f - 2.0f) * kMetre, 2.0f);
		++i;
	}
	for (float &h : hm.data) h = std::min(h, metres(water_h, 18.0f)); // ELEVATION_SET: overlaps stay at 18
	scatter_ponds(hm, rng, spawns, scale_by_map_size(4, 10, half), half * 0.05f + 2.5f, 22.0f, 8.0f, half * 0.90f,
	              water_h - metres(water_h, -5.0f), water_h);
	hm.blur(1, 0.4f);
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 16.0f, 6.0f, sp.base_height);
}

void RmsGenerator::synth_alpine_valley(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// alpine_valley.js: players on a circle at f = 0.35 in deep valleys;
	// a mountain range from the map edge (f = 0.49) towards the centre
	// (f = 0.18) between each pair of neighbours, with a passage through it.
	// Heights in metres: land 3 with bumps of 2, mountains 30 (a 2-tile blend).
	water_h = -10.0f;
	init_land(hm, rng, water_h, 3.0f, 2.0f, 3, 0.03f);
	const float half = hm.map_half;
	const float start = rng.range_float(0.0f, 6.2831853f);
	const int n = std::max(2, num_players);
	const float w = (float)scale_by_map_size(9, 15, half) * 0.5f;
	const float pass = (float)scale_by_map_size(10, 15, half) + 4.0f;
	for (int i = 0; i < n; ++i) {
		const float a = start + 6.2831853f * ((float)i + 0.5f) / (float)n;
		const float jig = 3.14159265f / (float)n * 0.4f * (rng.next_float() < 0.5f ? -1.0f : 1.0f);
		std::vector<std::pair<float, float>> ridge = {
			{det::cos(a) * half * 0.98f, det::sin(a) * half * 0.98f},
			{det::cos(a + jig) * half * 0.68f, det::sin(a + jig) * half * 0.68f},
			{det::cos(a) * half * 0.36f, det::sin(a) * half * 0.36f}};
		const float len = half * 0.62f;
		raise_ridge(hm, ridge, w, coast_metres(water_h, 30.0f), len * rng.range_float(0.35f, 0.65f), pass, 2.0f);
	}
	hm.thermal_erosion(3, 0.9f, 0.2f);
	hm.blur(1, 0.35f);
	place_ring_spawns(hm, spawns, num_players, half * 0.70f, start, 6.2831853f, coast_metres(water_h, 3.0f));
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 16.0f, 6.0f, sp.base_height);
}

// =========================================================================
// Maps modelled on 0 A.D. maps/random/{syria,cantabrian_highlands,
// sahel_watering_holes,persian_highlands,volcanic_lands}.js
// (same coordinate convention as above).
// =========================================================================

namespace {
// `count` round hills of radius [r0, r1] and height [h0, h1], at least `clear`
// from every spawn. Spawns must already be set.
void scatter_hills(HeightmapGrid &hm, RmsRng &rng, const std::vector<RmsGenerator::PlayerSpawn> &spawns, int count,
		float r0, float r1, float h0, float h1, float clear) {
	const float half = hm.map_half;
	for (int i = 0, tries = 0; i < count && tries < count * 40; ++tries) {
		const float a = rng.range_float(0.0f, 6.2831853f);
		const float d = std::sqrt(rng.next_float()) * half * 0.95f;
		const float hx = det::cos(a) * d, hz = det::sin(a) * d;
		const float r = rng.range_float(r0, r1);
		bool ok = true;
		for (const auto &sp : spawns)
			if (det::hypot(hx - sp.x, hz - sp.z) < clear + r) { ok = false; break; }
		if (!ok) continue;
		hm.raise_plateau(hx, hz, r, 0.0f, 0.0f, rng.range_float(h0, h1), r * 0.6f);
		++i;
	}
}
} // namespace

void RmsGenerator::synth_cantabrian_highlands(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// cantabrian_highlands.js: each player starts on a hill (heightHill 20)
	// ringed by cliffs, with one ramp facing the map centre; players on a
	// circle at f = 0.35, scaleByMapSize(1, 4) lakes per player. Heights in
	// metres: land 3, hills 20 (a 2-tile blend), lake ground -7.
	water_h = 1.0f;
	init_land(hm, rng, water_h, 3.0f, 1.0f, 4, 0.025f);
	const float half = hm.map_half, land = coast_metres(water_h, 3.0f);
	const float start = rng.range_float(0.0f, 6.2831853f);
	const float ring_r = half * 0.70f;
	// Bases are levelled before the hills go up: levelling a base on its hill (14 units and a
	// 6-unit blend) would fill the upper half of its ramp back in, leaving a cliff.
	place_team_spawns(hm, rng, spawns, num_players, ring_r, start, land);
	// One hill per base, or with Stronghold one per team round its members
	// (cantabrian_highlands.js: teamPosition and strongholdRadius + the base radius).
	struct Hill { float x, z, r, a; };
	std::vector<Hill> hills;
	if (t_team_placement.pattern == 3) {
		std::vector<int> group(num_players);
		for (int i = 0; i < num_players; ++i) group[i] = t_team_placement.teams[i] ? t_team_placement.teams[i] : 100 + i;
		for (int i = 0; i < num_players; ++i) {
			bool first = true;
			for (int k = 0; k < i; ++k) first &= group[k] != group[i];
			if (!first) continue;
			float cx = 0.0f, cz = 0.0f, n = 0.0f, r = 0.0f;
			for (int k = 0; k < num_players; ++k)
				if (group[k] == group[i]) { cx += spawns[k].x; cz += spawns[k].z; n += 1.0f; }
			cx /= n; cz /= n;
			for (int k = 0; k < num_players; ++k)
				if (group[k] == group[i]) r = std::max(r, det::hypot(spawns[k].x - cx, spawns[k].z - cz));
			hills.push_back({cx, cz, 19.0f + r, det::atan2(cz, cx)});
		}
	} else {
		for (int i = 0; i < num_players; ++i) {
			// Each hill under its base: on the ring, or where Team Placement put it.
			const bool ring = t_team_placement.pattern < 0;
			const float a = ring ? start + 6.2831853f * (float)i / (float)std::max(1, num_players) : det::atan2(spawns[i].z, spawns[i].x);
			hills.push_back({ring ? det::cos(a) * ring_r : spawns[i].x, ring ? det::sin(a) * ring_r : spawns[i].z, 19.0f, a});
		}
	}
	for (const Hill &h : hills) {
		// The ramp points back towards the centre, give or take pi / 8.
		const float ramp = h.a + 3.14159265f * (1.0f + rng.range_float(-1.0f, 1.0f) / 8.0f);
		hm.raise_plateau(h.x, h.z, h.r, ramp, 0.3f, 17.0f * HEIGHT_PER_METRE, 2.0f);
	}
	scatter_ponds(hm, rng, spawns, scale_by_map_size(1, 4, half) * std::max(1, num_players) / 2,
	              half * 0.05f + 3.0f, 30.0f, 12.0f, half * 0.95f, water_h - coast_metres(water_h, -7.0f), water_h);
	hm.blur(1, 0.3f);
	// The hilltop inside the ramp's start (half the hill's radius).
	for (auto &sp : spawns) {
		sp.base_height = hm.sample_world(sp.x, sp.z);
		lift_area(hm, sp.x, sp.z, 8.0f, 1.5f, sp.base_height);
	}
}

void RmsGenerator::synth_sahel_watering_holes(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// sahel_watering_holes.js: between each pair of neighbours a line of
	// water runs from f = 0.15 to f = 0.49, with a pool at each end and
	// shallows across the middle; players on a circle at f = 0.35. Land at
	// heightLand 3 with 2 m bumps (heightOffsetBump), the water set to
	// heightSeaGround -4 with a blend of 4, the shallows at heightShallows -2
	// (wadeable: RTS0's shallows). RTS0 has no hills (0.28: heightHill 35).
	water_h = 1.0f;
	const float land = metres(water_h, 3.0f), bed = 4.0f * kMetre;
	hm.init(hm.res, hm.map_half, land);
	const float half = hm.map_half;
	add_bumps(hm, rng, 2.0f * kMetre);
	const float start = rng.range_float(0.0f, 6.2831853f);
	const int n = std::max(2, num_players);
	std::vector<std::pair<float, float>> fords;
	for (int i = 0; i < n; ++i) {
		const float a = start + 6.2831853f * ((float)i + 0.5f) / (float)n;
		const float c = det::cos(a), s = det::sin(a);
		hm.carve_spline_path({{c * half * 0.30f, s * half * 0.30f}, {c * half * 0.98f, s * half * 0.98f}},
		                     2.5f + half * 0.02f, bed, water_h);
		hm.carve_crater_lake(c * half * 0.30f, s * half * 0.30f, (float)scale_by_map_size(5, 30, half) * 0.5f + 2.0f, 4.0f, bed, water_h);
		hm.carve_crater_lake(c * half * 0.98f, s * half * 0.98f, (float)scale_by_map_size(5, 22, half) * 0.5f + 2.0f, 4.0f, bed, water_h);
		fords.push_back({c * half * 0.64f, s * half * 0.64f});
	}
	for (const auto &f : fords) lift_area(hm, f.first, f.second, 2.0f * (2.5f + half * 0.02f), 3.0f, wade_height(water_h));
	hm.blur(1, 0.4f);
	place_ring_spawns(hm, spawns, num_players, half * 0.70f, start, 6.2831853f, land);
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 16.0f, 6.0f, sp.base_height);
}

void RmsGenerator::synth_persian_highlands(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// persian_highlands.js: high dry land (heightLand 10) with a sunken
	// central plateau (-10) rich in minerals, ringed by rocky hills;
	// players on a circle at f = 0.35. No water; heights in metres over
	// 0.28's water line (a faint 1 m roll on the land). The plateau is the
	// land less heightOffsetValley 10; the hills are createMountain cones
	// whose peaks reach scaleByMapSize(40, 60) m, the smaller ones less.
	water_h = -10.0f;
	const float land = 10.0f * kMetre, peak = (float)scale_by_map_size(40, 60, hm.map_half);
	hm.init(hm.res, hm.map_half, land);
	const float half = hm.map_half;
	hm.apply_fbm(rng, 4, 0.03f, 0.5f, 2.0f, 1.0f * kMetre);
	hm.level_area(0.0f, 0.0f, half * 0.26f, half * 0.12f, land - 10.0f * kMetre);
	place_team_spawns(hm, rng, spawns, num_players, half * 0.70f, rng.range_float(0.0f, 6.2831853f), land,
	                  1.0f * half); // persian_highlands.js: River banks 0.5 of the map apart
	scatter_hills(hm, rng, spawns, scale_by_map_size(20, 80, half) / 2, 2.5f, 4.5f, (0.4f * peak - 10.0f) * kMetre,
	              (peak - 10.0f) * kMetre, 18.0f);
	for (float &h : hm.data) h = std::min(h, peak * kMetre); // overlapping cones stay under the tallest peak
	hm.blur(1, 0.35f);
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 16.0f, 6.0f, sp.base_height);
}

namespace {
// Sets terrain towards `h` within `w` of the segment a-b, blending out over
// `blend` (rmgen SmoothElevationPainter ELEVATION_SET along a PathPlacer).
void set_along_segment(HeightmapGrid &hm, float ax, float az, float bx, float bz, float w, float blend, float h) {
	const float half = hm.map_half;
	const float step = (half * 2.0f) / (float)(hm.res - 1);
	const float reach = w + blend;
	const int x0 = std::max(0, (int)((std::min(ax, bx) - reach + half) / step));
	const int x1 = std::min(hm.res - 1, (int)((std::max(ax, bx) + reach + half) / step) + 1);
	const int z0 = std::max(0, (int)((std::min(az, bz) - reach + half) / step));
	const int z1 = std::min(hm.res - 1, (int)((std::max(az, bz) + reach + half) / step) + 1);
	const float dx = bx - ax, dz = bz - az, len2 = dx * dx + dz * dz;
	for (int z = z0; z <= z1; ++z) {
		const float wz = -half + z * step;
		for (int x = x0; x <= x1; ++x) {
			const float wx = -half + x * step;
			const float t = len2 > 0.0f ? std::clamp(((wx - ax) * dx + (wz - az) * dz) / len2, 0.0f, 1.0f) : 0.0f;
			const float d = det::hypot(wx - (ax + dx * t), wz - (az + dz * t));
			if (d >= reach) continue;
			const float k = d <= w ? 1.0f : 1.0f - smoothstep(w, reach, d);
			const float cur = hm.get(x, z);
			hm.set(x, z, cur + (h - cur) * k);
		}
	}
}

// Point at angle a, distance r from the centre.
inline std::pair<float, float> polar(float a, float r) { return {det::cos(a) * r, det::sin(a) * r}; }
} // namespace

void RmsGenerator::synth_lions_den(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// lions_den.js createSunkenTerrain: high ground (heightHill 50) with a
	// sunken valley ring (f = 0.29, heightValley 0) round a central hill
	// (f = 0.21). Each player's den (heightDen 15) sits in the high ground at
	// f = 0.4, with ramps (heightPath 10) down to an expansion cut into the
	// central hill and round to both neighbours; more expansions lie in the
	// valley between players. 0 A.D. metres (HEIGHT_PER_METRE).
	water_h = -10.0f;
	constexpr float m = HEIGHT_PER_METRE;
	const float hill = 50.0f * m, den = 15.0f * m, path = 10.0f * m, valley = 0.0f;
	hm.init(hm.res, hm.map_half, hill);
	const float half = hm.map_half;
	const int n = std::max(1, num_players);
	hm.apply_fbm(rng, 3, 0.03f, 0.5f, 2.0f, 2.0f * m);
	hm.level_area(0.0f, 0.0f, half * 0.58f, 3.0f, valley);
	hm.level_area(0.0f, 0.0f, half * 0.42f, 3.0f, hill);
	const float start = rng.range_float(0.0f, 6.2831853f);
	const float exp_dist = (0.1f + (float)n / 200.0f) * 2.0f;
	float exp_r = half * 0.28f / std::sqrt((float)n), exp_angle = 0.75f, n_road = 0.44f, n_exp = 0.425f;
	if (n <= 2) { exp_r = half * 0.15f; exp_angle = 0.72f; }
	if (n < 4) { n_road = 0.42f; n_exp = 0.40f; }
	auto at = [&](float dist, int i, float off) { return polar(start + ((float)i + off) * 6.2831853f / (float)n, dist * 2.0f * half); };
	for (int i = 0; i < n; ++i) {
		const auto p = at(0.4f, i, 0.0f);
		const auto e = polar(start + ((float)i + exp_angle) * 6.2831853f / (float)n, exp_dist * half);
		set_along_segment(hm, p.first, p.second, e.first, e.second, 6.0f, 3.0f, path);
		const auto pp = at(0.47f, i, 0.0f);
		for (float off : {-0.5f, 0.5f}) {
			const auto q = at(n_road, i, off);
			set_along_segment(hm, pp.first, pp.second, q.first, q.second, 9.5f, 3.0f, path);
		}
		hm.level_area(p.first, p.second, half * 0.20f, 3.0f, den);
		hm.level_area(e.first, e.second, exp_r, 3.0f, den);
	}
	for (int i = 0; i < n; ++i) {
		const auto q = at(n_exp, i, 0.5f);
		hm.level_area(q.first, q.second, exp_r, 3.0f, valley);
	}
	place_ring_spawns(hm, spawns, num_players, half * 0.80f, start, 6.2831853f, 0.0f);
	hm.blur(1, 0.3f);
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 15.0f, 3.0f, sp.base_height);
}

void RmsGenerator::synth_hells_pass(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// hells_pass.js: each team on a line out from the centre (pattern
	// "line", f = 0.2, 0.08 between teammates); between neighbouring teams
	// a mountain spine (heightBarrier 30) runs from f = 0.075 to f = 0.42,
	// leaving passes round the centre and along the map edge. RTS0 knows no
	// teams here, so each player is a team. 0 A.D. metres (HEIGHT_PER_METRE):
	// heightLand 1, heightBarrier 30, addHills 6-12.
	water_h = -10.0f;
	constexpr float m = HEIGHT_PER_METRE;
	const float barrier = 30.0f * m;
	hm.init(hm.res, hm.map_half, 1.0f * m);
	const float half = hm.map_half;
	const int n = std::max(1, num_players);
	hm.apply_fbm(rng, 4, 0.025f, 0.5f, 2.0f, 2.0f * m);
	const float start = rng.range_float(0.0f, 6.2831853f);
	// PathPlacer width and tapering (the width grows to 1 - tapering times itself at the
	// end); SmoothElevationPainter(ELEVATION_SET, heightBarrier, 2).
	const float spine_w = (float)scale_by_map_size(14, n >= 5 ? 4 : 8, half);
	const float taper = n >= 5 ? -0.7f : (n > 3 || 2.0f * half <= 192.0f) ? -1.0f : -1.5f;
	const float step = (half * 2.0f) / (float)(hm.res - 1);
	for (int i = 0; i < n; ++i) {
		const float a = start + ((float)i + 0.5f) * 6.2831853f / (float)n;
		const auto s = polar(a, half * 0.15f), e = polar(a, half * 0.84f);
		// PathPlacer waviness: bend the spine through a jittered midpoint.
		const auto m = polar(a + rng.range_float(-0.06f, 0.06f), half * 0.5f);
		const std::pair<float, float> pts[3] = {s, m, e};
		const float l0 = det::hypot(m.first - s.first, m.second - s.second), l1 = det::hypot(e.first - m.first, e.second - m.second);
		for (int z = 0; z < hm.res; ++z) {
			const float wz = -half + z * step;
			for (int x = 0; x < hm.res; ++x) {
				const float wx = -half + x * step;
				// The nearest point on the spine; its ends are square (PathPlacer's quads), so
				// the passes round the centre and along the edge stay open.
				float best = 1e9f, along = 0.0f, past_end = 0.0f;
				for (int k = 0; k < 2; ++k) {
					const float ax = pts[k].first, az = pts[k].second, bx = pts[k + 1].first - ax, bz = pts[k + 1].second - az;
					const float len = k == 0 ? l0 : l1;
					float t = ((wx - ax) * bx + (wz - az) * bz) / (len * len);
					if ((k == 0 && t < 0.0f) || (k == 1 && t > 1.0f)) continue;
					t = std::clamp(t, 0.0f, 1.0f);
					const float d = det::hypot(wx - (ax + bx * t), wz - (az + bz * t));
					if (d < best) {
						best = d;
						along = (k == 0 ? 0.0f : l0) + len * t;
						past_end = std::min(along, l0 + l1 - along);
					}
				}
				const float hw = (1.0f - along / (l0 + l1) * taper) * spine_w * 0.5f;
				if (best >= hw) continue;
				const float w = std::clamp(std::min(hw - best, past_end) / 2.0f, 0.0f, 1.0f);
				const float cur = hm.get(x, z);
				hm.set(x, z, cur + (barrier - cur) * w);
			}
		}
	}
	place_ring_spawns(hm, spawns, num_players, half * 0.40f, start, 6.2831853f, 0.0f);
	scatter_hills(hm, rng, spawns, scale_by_map_size(1, 4, half), 3.0f, 5.0f, 6.0f * m, 12.0f * m, 30.0f);
	hm.blur(1, 0.35f);
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 16.0f, 6.0f, sp.base_height);
}

void RmsGenerator::synth_deep_forest(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// deep_forest.js: a forest filling the map, players at 5/8 to 3/4 of the
	// map radius (angles jittered by a quarter of the spacing), forest paths
	// (heightPath -2) linking every base to each other and to a central
	// hill, and a height field of a middle hill (size/64 * (1 + cos(3/2 pi
	// r/R))) plus rolling hills (5 * (1 + sin(x/10) sin(z/10))). Dense wood
	// comes from distribute_neutral_resources. 0 A.D. metres (HEIGHT_PER_METRE).
	water_h = -10.0f;
	constexpr float m = HEIGHT_PER_METRE;
	hm.init(hm.res, hm.map_half, 0.0f);
	const float half = hm.map_half;
	const int n = std::max(1, num_players);
	const float base_r = 20.0f;
	const float rmin = std::min(half - 1.5f * base_r, 0.625f * half), rmax = std::min(half - base_r, 0.75f * half);
	const float start = rng.range_float(0.0f, 6.2831853f);
	std::vector<std::pair<float, float>> pos;
	for (int i = 0; i < n; ++i) {
		const float a = start + (float)i * 6.2831853f / (float)n + rng.range_float(0.0f, 6.2831853f / (float)n / 4.0f);
		pos.push_back(polar(a, rng.range_float(rmin, rmax)));
	}
	pos.push_back({0.0f, 0.0f});
	const bool blend_all = n <= 4;
	for (int i = 0; i < n + 1; ++i)
		for (int j = i + 1; j < n + 1; ++j)
			if (blend_all || j == n || j == i + 1 || (i == 0 && j == n - 1))
				set_along_segment(hm, pos[i].first, pos[i].second, pos[j].first, pos[j].second, 3.0f, 2.0f, -2.0f * m);
	const float step = (half * 2.0f) / (float)(hm.res - 1);
	for (int z = 0; z < hm.res; ++z) {
		const float wz = -half + z * step;
		for (int x = 0; x < hm.res; ++x) {
			const float wx = -half + x * step;
			const float r = det::hypot(wx, wz);
			const float middle = (half / 32.0f) * (1.0f + det::cos(1.5f * 3.14159265f * std::min(r, half) / half));
			const float hills = 5.0f * (1.0f + det::sin(wx / 10.0f) * det::sin(wz / 10.0f));
			hm.add(x, z, (middle + hills + 1.0f) * m);
		}
	}
	spawns.clear();
	for (int i = 0; i < n && i < num_players; ++i) {
		const float sx = pos[i].first, sz = pos[i].second, r = std::max(1e-3f, det::hypot(sx, sz));
		const float bh = hm.sample_world(sx, sz);
		spawns.push_back({(uint8_t)i, sx, sz, bh, -sx / r, -sz / r});
		hm.level_area(sx, sz, 14.0f, 6.0f, bh);
	}
	hm.blur(1, 0.35f);
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 16.0f, 6.0f, sp.base_height);
}

void RmsGenerator::synth_india(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// india.js: parched land (heightLand 1) with one half dried-up lake in
	// the middle (clumps within scaleByMapSize(15, 40) of the centre, sea
	// ground -3) with a jagged shore (heightShore 3), scaleByMapSize(300,
	// 800) small bumps (heightOffsetBump 2), players on a circle at f = 0.35.
	// Heights through coast_metres, so the 1 m land stays dry in RTS0's nav
	// grid; RTS0 has no 3 m shore jaggedness.
	water_h = 1.0f;
	const float land = coast_metres(water_h, 1.0f);
	hm.init(hm.res, hm.map_half, land);
	const float half = hm.map_half;
	add_bumps(hm, rng, 2.0f * kMetre);
	const float lake_r = (float)scale_by_map_size(15, 40, half);
	const int blobs = std::max(2, scale_by_map_size(2, 16, half) / 2);
	for (int i = 0; i < blobs; ++i) {
		const float b_ = lake_r * std::sqrt(rng.next_float()) * 0.7f; const float a_ = rng.range_float(0.0f, 6.2831853f); const auto c = polar(a_, b_);
		hm.carve_crater_lake(c.first, c.second, lake_r * rng.range_float(0.35f, 0.6f) + 2.0f, 4.0f,
		                     water_h - coast_metres(water_h, -3.0f), water_h);
	}
	place_team_spawns(hm, rng, spawns, num_players, half * 0.70f, rng.range_float(0.0f, 6.2831853f), land);
	hm.blur(1, 0.4f);
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 16.0f, 6.0f, sp.base_height);
}

void RmsGenerator::synth_guadalquivir_river(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// guadalquivir_river.js: a continent (radius f = 0.49 round a point at
	// f = 0.2 from the centre) in the sea, a river (f = 0.07 wide) through
	// the map centre from the sea, with shallows 0.3-0.4, 0.5-0.6 and
	// 0.7-0.8 along it. Players on arcs of radius f = 0.35 round the
	// continent's centre, split between the two banks. Heights in metres:
	// land 3, sea and river ground -3.
	water_h = 1.0f;
	init_land(hm, rng, water_h, 3.0f, 1.0f, 3, 0.025f);
	const float half = hm.map_half, land = coast_metres(water_h, 3.0f), sea = coast_metres(water_h, -3.0f);
	const float rot = rng.range_float(0.0f, 6.2831853f);
	const float ux = det::cos(rot), uz = det::sin(rot); // towards the continent's centre and along the river
	const float ccx = ux * half * 0.40f, ccz = uz * half * 0.40f;
	const float land_r = half * 0.98f;
	const float step = (half * 2.0f) / (float)(hm.res - 1);
	const float p1 = rng.range_float(0.0f, 6.2831853f), p2 = rng.range_float(0.0f, 6.2831853f);
	for (int z = 0; z < hm.res; ++z) {
		const float wz = -half + z * step;
		for (int x = 0; x < hm.res; ++x) {
			const float wx = -half + x * step;
			const float a = det::atan2(wz - ccz, wx - ccx);
			const float r = land_r * (1.0f + 0.05f * det::sin(a * 5.0f + p1) + 0.03f * det::sin(a * 11.0f + p2));
			const float d = det::hypot(wx - ccx, wz - ccz) - r;
			if (d <= -6.0f) continue;
			const float t = smoothstep(-6.0f, 6.0f, d);
			const float cur = hm.get(x, z);
			hm.set(x, z, cur + (sea - cur) * t);
		}
	}
	// The river runs from the far shore (through the continent) to the near one.
	const float river_w = half * 0.07f;
	std::vector<std::pair<float, float>> river;
	for (int i = 0; i <= 8; ++i) {
		const float s = -1.0f + 2.0f * (float)i / 8.0f; // -1 near edge, +1 far edge
		const float wob = (i == 0 || i == 8) ? 0.0f : rng.range_float(-0.04f, 0.04f) * half;
		river.push_back({ux * s * half - uz * wob, uz * s * half + ux * wob});
	}
	hm.carve_spline_path(river, river_w, water_h - sea, water_h);
	hm.blur(1, 0.4f);
	for (float f : {0.35f, 0.55f, 0.75f}) {
		// f from the far (top) end: s = 1 - 2 f.
		const float s = 1.0f - 2.0f * f;
		// Wide enough to span the blended banks and the river's wobble.
		place_ford(hm, ux * s * half, uz * s * half, 2.0f * river_w + 4.0f, water_h);
	}
	// Two arcs facing away from the continent's centre, one per bank.
	spawns.clear();
	const int sides[2] = {(num_players + 1) / 2, num_players / 2};
	const float rp = half * 0.70f;
	const float base_a = rot + 3.14159265f;
	for (int i = 0; i < num_players; ++i) {
		const int side = i & 1, k = i / 2, cnt = sides[side];
		const float off = cnt > 1 ? 0.18f + (0.62f - 0.18f) * (float)k / (float)(cnt - 1) : 0.38f;
		const float a = base_a + (side ? 1.0f : -1.0f) * off * 3.14159265f;
		const float sx = ccx + det::cos(a) * rp, sz = ccz + det::sin(a) * rp;
		const float bh = std::max(land, hm.sample_world(sx, sz));
		const float r = std::max(1e-3f, det::hypot(sx, sz));
		spawns.push_back({(uint8_t)i, sx, sz, bh, -sx / r, -sz / r});
		hm.level_area(sx, sz, 14.0f, 6.0f, bh);
	}
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 16.0f, 6.0f, sp.base_height);
}

void RmsGenerator::synth_fields_of_meroe(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// fields_of_meroe.js: the Nile (scaleByMapSize(12, 36) wide) at f = 0.25
	// across the map, turned by pi/5; two cataracts (shallow passages,
	// heightCataract -1 with bumps) cross it roughly at right angles near
	// f = 0.2 and f = 0.8; players placed at random at least 15 from the
	// river and 30 from each other; sand dunes and acacia groves. Land at
	// heightLand 2 with 1.4 m bumps (heightOffsetBump), the riverbed at the
	// dry biome's seaGround -4, the cataracts at -1 (RTS0's wadeable
	// shallows), dunes set to heightDunes 11.
	water_h = 1.0f;
	const float land = metres(water_h, 2.0f);
	hm.init(hm.res, hm.map_half, land);
	const float half = hm.map_half;
	add_bumps(hm, rng, 1.4f * kMetre);
	const float ra = 3.14159265f / 5.0f;
	const float dx = -det::sin(ra), dz = det::cos(ra); // along the river
	const float ox = det::cos(ra) * -0.5f * half, oz = det::sin(ra) * -0.5f * half; // a point on it
	const float river_w = (float)scale_by_map_size(12, 36, half) * 0.5f;
	// Dunes, raised before the river so it cuts through them.
	scatter_hills(hm, rng, {}, scale_by_map_size(2, 8, half), half * 0.04f + 2.0f, half * 0.07f + 3.0f,
	              (11.0f - 2.0f) * kMetre, (11.0f - 2.0f) * kMetre, 0.0f);
	for (float &h : hm.data) h = std::min(h, metres(water_h, 11.0f)); // ELEVATION_SET: overlaps stay at 11
	hm.carve_spline_path({{ox - dx * 1.5f * half, oz - dz * 1.5f * half}, {ox, oz}, {ox + dx * 1.5f * half, oz + dz * 1.5f * half}},
	                     river_w, 4.0f * kMetre, water_h);
	hm.blur(1, 0.4f);
	for (float f : {rng.range_float(-0.7f, -0.5f), rng.range_float(0.5f, 0.7f)})
		lift_area(hm, ox + dx * f * half, oz + dz * f * half, std::max((float)scale_by_map_size(10, 15, half), 1.7f * river_w), 3.0f,
		          wade_height(water_h));
	// playerPlacementRandom: rejection-sample positions clear of the river.
	spawns.clear();
	auto river_dist = [&](float x, float z) { return std::fabs((x - ox) * dz - (z - oz) * dx); };
	float min_gap = half * 0.9f;
	int tries = 0;
	for (int i = 0; i < num_players && tries < 4000; ++tries) {
		if (tries > 0 && tries % 200 == 0) min_gap *= 0.85f;
		const float b_ = std::sqrt(rng.next_float()) * half * 0.80f; const float a_ = rng.range_float(0.0f, 6.2831853f); const auto p = polar(a_, b_);
		if (river_dist(p.first, p.second) < river_w + 18.0f + 14.0f) continue;
		bool ok = true;
		for (const auto &sp : spawns)
			if (det::hypot(p.first - sp.x, p.second - sp.z) < std::max(30.0f, min_gap)) { ok = false; break; }
		if (!ok) continue;
		const float bh = std::max(land, hm.sample_world(p.first, p.second));
		const float r = std::max(1e-3f, det::hypot(p.first, p.second));
		spawns.push_back({(uint8_t)i, p.first, p.second, bh, -p.first / r, -p.second / r});
		hm.level_area(p.first, p.second, 14.0f, 6.0f, bh);
		++i;
	}
	// Too crowded for random spots: fall back to a circle on the dry side.
	if ((int)spawns.size() < num_players)
		place_ring_spawns(hm, spawns, num_players, half * 0.70f, ra, 6.2831853f, land);
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 16.0f, 6.0f, sp.base_height);
}

void RmsGenerator::synth_gear(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// gear.js: a big circular lake (f = 0.27) with rivers from f = 0.15 to
	// 0.6 between the players and land paths from f = 0.05 to 0.49 through
	// it, like a spider web; inside it a land ring (f = 0.21), a shallow
	// water ring (f = 0.17), a central island (f = 0.14) and a hill
	// (f = 0.12, heightHill 20). Players on a circle at f = 0.35. The number
	// of rivers per player ("split") follows 0 A.D.'s table by map size.
	// RTS0 water is never walkable, so the shallow ring gets a ford on
	// every path. 0 A.D. metres (HEIGHT_PER_METRE) above RTS0's water line:
	// heightSeaGround -4, heightShallow -2, heightLand 3, heightRing 4, heightHill 20.
	water_h = 1.0f;
	constexpr float m = HEIGHT_PER_METRE;
	const float sea = water_h - 4.0f * m, shallow = water_h - 2.0f * m, land = water_h + 3.0f * m,
			ring = water_h + 4.0f * m, hill = water_h + 20.0f * m;
	hm.init(hm.res, hm.map_half, land);
	const float half = hm.map_half;
	const int n = std::max(1, num_players);
	hm.apply_fbm(rng, 3, 0.03f, 0.5f, 2.0f, 1.0f * m);
	const int size = (int)std::lround(half * 2.0f);
	int split = 1;
	if (size == 128 && n <= 2) split = 2;
	else if (size == 192 && n <= 3) split = 2;
	else if (size == 256 || size == 320) split = n <= 3 ? 3 : (n == 4 ? 2 : 1);
	else if (size == 384) split = n <= 3 ? 4 : (n == 4 ? 3 : (n == 5 ? 2 : 1));
	else if (size == 448) split = n <= 2 ? 5 : (n <= 4 ? 4 : (n == 5 ? 3 : (n == 6 ? 2 : 1)));
	const int spokes = n * split;
	const float start = rng.range_float(0.0f, 6.2831853f);
	hm.level_area(0.0f, 0.0f, half * 0.54f, 4.0f, sea);
	const float river_w = (float)scale_by_map_size(14, 40, half) * 0.5f;
	for (int m = 0; m < spokes; ++m) {
		const auto a = polar(start + ((float)m + 0.5f) * 6.2831853f / (float)spokes, half * 0.30f);
		const auto b = polar(start + ((float)m + 0.5f) * 6.2831853f / (float)spokes, half * 1.20f);
		set_along_segment(hm, a.first, a.second, b.first, b.second, river_w * 0.5f, 4.0f, sea);
	}
	const float path_w = (float)scale_by_map_size(10, 40, half) * 0.5f;
	for (int m = 0; m < spokes; ++m) {
		const auto a = polar(start + (float)m * 6.2831853f / (float)spokes, half * 0.10f);
		const auto b = polar(start + (float)m * 6.2831853f / (float)spokes, half * 0.98f);
		set_along_segment(hm, a.first, a.second, b.first, b.second, path_w * 0.5f, 4.0f, land);
	}
	hm.level_area(0.0f, 0.0f, half * 0.42f, 4.0f, ring);
	hm.level_area(0.0f, 0.0f, half * 0.34f, 3.0f, shallow);
	hm.level_area(0.0f, 0.0f, half * 0.28f, 3.0f, ring);
	hm.level_area(0.0f, 0.0f, half * 0.24f, 8.0f, hill);
	place_ring_spawns(hm, spawns, num_players, half * 0.70f, start, 6.2831853f, land);
	hm.blur(1, 0.35f);
	for (int m = 0; m < spokes; ++m) {
		const auto f = polar(start + (float)m * 6.2831853f / (float)spokes, half * 0.31f);
		place_ford(hm, f.first, f.second, std::max(3.0f, path_w * 0.5f), water_h);
	}
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 16.0f, 6.0f, sp.base_height);
}

// 0 A.D. rmbiome Gaia animals (maps/random/rmbiome/generic/*.json, savanna.js): the starting
// animal by each civic centre, the main and secondary huntable animals, plus a predator that
// RTS0 mixes into every fourth wildlife pack.
struct BiomeFauna { uint8_t starting, main_hunt, secondary_hunt, predator; };
static BiomeFauna biome_fauna(BiomeType biome, int pick) {
	switch (biome) {
		case BIOME_ALPINE: return {FAUNA_CHICKEN, FAUNA_GOAT, FAUNA_DEER, FAUNA_BEAR_BROWN};
		case BIOME_OASIS: return {FAUNA_GOAT, FAUNA_CAMEL, FAUNA_GAZELLE, FAUNA_LION};
		case BIOME_STEPPE: return {FAUNA_CHICKEN, FAUNA_HORSE, FAUNA_HORSE, FAUNA_WOLF};
		case BIOME_SAVANNA: {
			static const uint8_t kMain[4] = {FAUNA_WILDEBEEST, FAUNA_ZEBRA, FAUNA_GIRAFFE, FAUNA_GAZELLE};
			return {FAUNA_CHICKEN, kMain[pick & 3], FAUNA_ELEPHANT_AFRICAN_BUSH, FAUNA_LION};
		}
		case BIOME_POLAR: return {FAUNA_CHICKEN, FAUNA_MUSKOX, FAUNA_WALRUS, FAUNA_WOLF_ARCTIC};
		case BIOME_INDIA: return {FAUNA_PEACOCK, FAUNA_PEACOCK, FAUNA_ELEPHANT_ASIAN, FAUNA_TIGER};
		case BIOME_TEMPERATE: return {FAUNA_CHICKEN, FAUNA_DEER, FAUNA_SHEEP, FAUNA_WOLF};
		case BIOME_AUTUMN: return {FAUNA_CHICKEN, FAUNA_DEER, FAUNA_SHEEP, FAUNA_WOLF};
		case BIOME_NUBIA: {
			// setupBiome_nubia's pickRandom of the main huntable animal.
			static const uint8_t kMain[5] = {FAUNA_WILDEBEEST, FAUNA_ZEBRA, FAUNA_GIRAFFE, FAUNA_ELEPHANT_AFRICAN_BUSH, FAUNA_GAZELLE};
			return {FAUNA_CHICKEN, kMain[(unsigned)pick % 5u], FAUNA_GAZELLE, FAUNA_LION};
		}
		default: return {FAUNA_CHICKEN, FAUNA_DEER, FAUNA_SHEEP, FAUNA_BOAR}; // aegean (and latium)
	}
}

void RmsGenerator::synth_stronghold(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players,
		const uint8_t *teams) {
	// stronghold.js: playerPlacementByPattern("stronghold") puts each team
	// on a circle at f = 0.2-0.35 from the centre with its members packed
	// round the team's spot (f = 0.05-0.1 apart), so teammates' civic
	// centres nearly touch; then a random mix of bluffs, hills, mountains,
	// plateaus and valleys on high land (heightLand 30). Players with no
	// team each stand alone. 0 A.D. metres (HEIGHT_PER_METRE): rmgen2 addHills 6-12,
	// addMountains 100-120, addPlateaus 20-30, addValleys down to 2 below the land.
	water_h = -10.0f;
	constexpr float m = HEIGHT_PER_METRE;
	const float land = 30.0f * m;
	hm.init(hm.res, hm.map_half, land);
	const float half = hm.map_half;
	hm.apply_fbm(rng, 4, 0.03f, 0.5f, 2.0f, 2.0f * m);
	// Group players by team, in player order (getTeamsArray).
	std::vector<std::vector<int>> groups;
	std::vector<int> team_group(MAX_PLAYERS + 1, -1);
	for (int i = 0; i < num_players; ++i) {
		const int t = teams ? teams[i] : 0;
		if (t == 0) { groups.push_back({i}); continue; }
		if (team_group[t] < 0) { team_group[t] = (int)groups.size(); groups.push_back({}); }
		groups[team_group[t]].push_back(i);
	}
	const float dist = rng.range_float(0.4f, 0.7f) * half;
	const float grouped = rng.range_float(0.1f, 0.2f) * half;
	const float start = rng.range_float(0.0f, 6.2831853f);
	spawns.assign(num_players, PlayerSpawn{});
	const int ng = std::max(1, (int)groups.size());
	for (int g = 0; g < (int)groups.size(); ++g) {
		const float ta = start + (float)g * 6.2831853f / (float)ng;
		const float cx = det::cos(ta) * dist, cz = det::sin(ta) * dist;
		const int k = (int)groups[g].size();
		// Keep teammates' bases from overlapping (18 apart at least).
		const float r = k == 1 ? 0.0f : std::max(grouped, 9.0f / det::sin(3.14159265f / (float)k));
		for (int m = 0; m < k; ++m) {
			const float ma = ta + (float)m * 6.2831853f / (float)k;
			float sx = cx + det::cos(ma) * r, sz = cz + det::sin(ma) * r;
			sx = std::clamp(sx, -half + 16.0f, half - 16.0f);
			sz = std::clamp(sz, -half + 16.0f, half - 16.0f);
			const float l = std::max(1e-3f, det::hypot(sx, sz));
			const int pid = groups[g][m];
			spawns[pid] = {(uint8_t)pid, sx, sz, hm.sample_world(sx, sz), -sx / l, -sz / l};
		}
	}
	// The feature mix (addElements with random amounts).
	if (rng.range_int(0, 1)) scatter_hills(hm, rng, spawns, scale_by_map_size(2, 8, half), 4.0f, 7.0f, 6.0f * m, 12.0f * m, 30.0f);
	if (rng.range_int(0, 1)) scatter_hills(hm, rng, spawns, scale_by_map_size(1, 6, half), 3.0f, 5.0f, 100.0f * m, 120.0f * m, 25.0f);
	if (rng.range_int(0, 1)) {
		const int plateaus = scale_by_map_size(1, 4, half);
		for (int i = 0; i < plateaus; ++i) {
			const float b_ = rng.range_float(0.0f, 0.9f) * half; const float a_ = rng.range_float(0.0f, 6.2831853f); const auto p = polar(a_, b_);
			bool ok = true;
			for (const auto &sp : spawns)
				if (det::hypot(p.first - sp.x, p.second - sp.z) < 35.0f) { ok = false; break; }
			if (ok) {
				const float c_ = rng.range_float(20.0f, 30.0f) * m; const float b_ = rng.range_float(0.0f, 6.2831853f);
				const float a_ = half * rng.range_float(0.05f, 0.09f);
				hm.raise_plateau(p.first, p.second, a_, b_, 0.4f, c_, 3.0f);
			}
		}
	}
	if (rng.range_int(0, 1)) {
		// Valleys: dry hollows 2 to 30 m below the land (addValleys' -2 to -baseHeight).
		const int valleys = scale_by_map_size(1, 5, half);
		for (int i = 0; i < valleys; ++i) {
			const float b_ = rng.range_float(0.0f, 0.9f) * half; const float a_ = rng.range_float(0.0f, 6.2831853f); const auto p = polar(a_, b_);
			bool ok = true;
			for (const auto &sp : spawns)
				if (det::hypot(p.first - sp.x, p.second - sp.z) < 30.0f) { ok = false; break; }
			if (ok) {
				const float h_ = land - rng.range_float(2.0f, 30.0f) * m; const float r_ = half * rng.range_float(0.04f, 0.08f);
				hm.level_area(p.first, p.second, r_, 5.0f, h_);
			}
		}
	}
	for (auto &sp : spawns) {
		sp.base_height = hm.sample_world(sp.x, sp.z);
		hm.level_area(sp.x, sp.z, 14.0f, 6.0f, sp.base_height);
	}
	hm.blur(1, 0.35f);
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 16.0f, 6.0f, sp.base_height);
}

void RmsGenerator::synth_rhine_marshlands(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// rhine_marshlands.js: low land (heightLand 1) with scaleByMapSize(300,
	// 800) bumps, then 7 rounds of scaleByMapSize(4, 20) marshes (chains of
	// scaleByMapSize(6, 12) clumps) at heightMarsh -2, kept 20 from the bases
	// and scaleByMapSize(7, 16) from each other, with bumps inside them.
	// 2 m deep is just shallow enough to wade (pathfinder.xml MaxWaterDepth
	// 2), so the marshes are NAV_SHALLOW: walkable water. Players on a circle
	// at f = 0.35.
	water_h = 1.0f;
	const float line = water_h + NAV_WATER_MARGIN;
	const float marsh = line - LAND_MAX_WATER_DEPTH + 0.05f; // 2 m below the nav water line
	hm.init(hm.res, hm.map_half, line + 0.25f);
	const float half = hm.map_half;
	hm.apply_fbm(rng, 3, 0.06f, 0.5f, 2.0f, 0.25f);
	place_team_spawns(hm, rng, spawns, num_players, half * 0.70f, rng.range_float(0.0f, 6.2831853f), line + 0.25f);
	std::vector<std::pair<float, float>> marshes;
	const float gap = (float)scale_by_map_size(7, 16, half);
	const int per_round = scale_by_map_size(4, 20, half);
	for (int round = 0; round < 7; ++round) {
		for (int i = 0, tries = 0; i < per_round && tries < per_round * 20; ++tries) {
			const float b_ = std::sqrt(rng.next_float()) * half * 0.95f; const float a_ = rng.range_float(0.0f, 6.2831853f); const auto c = polar(a_, b_);
			const float r = (float)scale_by_map_size(4, 9, half) * rng.range_float(0.8f, 1.35f);
			bool ok = true;
			for (const auto &sp : spawns)
				if (det::hypot(c.first - sp.x, c.second - sp.z) < 20.0f + 14.0f + r) { ok = false; break; }
			for (const auto &m : marshes)
				if (det::hypot(c.first - m.first, c.second - m.second) < gap + 2.0f * r) { ok = false; break; }
			if (!ok) continue;
			// A chain of clumps: the marsh's centre plus a few offset blobs.
			const int clumps = std::max(1, scale_by_map_size(6, 12, half) / 3);
			for (int k = 0; k < clumps; ++k) {
				const auto o = polar(rng.range_float(0.0f, 6.2831853f), k == 0 ? 0.0f : r * 0.8f);
				hm.level_area(c.first + o.first, c.second + o.second, r * rng.range_float(0.5f, 0.8f), 2.0f, marsh);
			}
			marshes.push_back(c);
			++i;
		}
	}
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 16.0f, 6.0f, sp.base_height);
}

// =========================================================================
// rmgen heightmap images (Red Sea, Hellas)
// =========================================================================

namespace {

// Decodes one of heightmap_images.h's base64 images into 8-bit samples.
std::vector<uint8_t> decode_heightmap_image(const char *const *chunks, int size) {
	auto val = [](char c) -> int {
		if (c >= 'A' && c <= 'Z') return c - 'A';
		if (c >= 'a' && c <= 'z') return c - 'a' + 26;
		if (c >= '0' && c <= '9') return c - '0' + 52;
		if (c == '+') return 62;
		if (c == '/') return 63;
		return -1;
	};
	std::vector<uint8_t> out;
	out.reserve((size_t)size * (size_t)size);
	uint32_t acc = 0;
	int bits = 0;
	for (int i = 0; chunks[i]; ++i)
		for (const char *p = chunks[i]; *p; ++p) {
			const int v = val(*p);
			if (v < 0) continue;
			acc = (acc << 6) | (uint32_t)v;
			bits += 6;
			if (bits >= 8) {
				bits -= 8;
				out.push_back((uint8_t)((acc >> bits) & 0xFF));
			}
		}
	out.resize((size_t)size * (size_t)size, 0);
	return out;
}

const std::vector<uint8_t> &red_sea_image() {
	static const std::vector<uint8_t> img = decode_heightmap_image(heightmap_images::red_sea_b64, heightmap_images::red_sea_size);
	return img;
}

const std::vector<uint8_t> &hellas_image() {
	static const std::vector<uint8_t> img = decode_heightmap_image(heightmap_images::hellas_b64, heightmap_images::hellas_size);
	return img;
}

float cubic_weight(float a, float b, float c, float d, float t) {
	// Catmull-Rom through b (t = 0) and c (t = 1).
	return b + 0.5f * t * (c - a + t * (2.0f * a - 5.0f * b + 4.0f * c - d + t * (3.0f * (b - c) + d - a)));
}

// Bicubic sample of an n x n image at pixel coordinates (u, v), clamped at
// the edges, as HeightmapPainter's bicubicInterpolation over a 4x4 brush.
float sample_image_bicubic(const std::vector<uint8_t> &img, int n, float u, float v) {
	const int x = (int)std::floor(u), y = (int)std::floor(v);
	const float tx = u - (float)x, ty = v - (float)y;
	auto px = [&](int i, int j) {
		i = std::clamp(i, 0, n - 1);
		j = std::clamp(j, 0, n - 1);
		return (float)img[(size_t)j * (size_t)n + (size_t)i];
	};
	float col[4];
	for (int j = 0; j < 4; ++j)
		col[j] = cubic_weight(px(x - 1, y - 1 + j), px(x, y - 1 + j), px(x + 1, y - 1 + j), px(x + 2, y - 1 + j), tx);
	return cubic_weight(col[0], col[1], col[2], col[3], ty);
}

// HeightmapPainter.paint over the whole map from the square sub-image at
// (x0, y0) of side `sub` pixels, with scaleHeight's normal range: the image's
// 0..255 maps to min..max metres on a 320-tile map, scaled by (size + 1) / 321.
void paint_heightmap(HeightmapGrid &hm, const std::vector<uint8_t> &img, int n, int x0, int y0, int sub,
		float min_m, float max_m, float water_h) {
	const float size = 2.0f * hm.map_half;
	const float lo = min_m * (size + 1.0f) / 321.0f, hi = max_m * (size + 1.0f) / 321.0f;
	const float step = (float)(sub - 1) / (float)(hm.res - 1);
	for (int z = 0; z < hm.res; ++z)
		for (int x = 0; x < hm.res; ++x) {
			const float g = sample_image_bicubic(img, n, (float)x0 + (float)x * step, (float)y0 + (float)z * step);
			hm.set(x, z, water_h + (lo + (hi - lo) * g / 255.0f) * kMetre);
		}
}

// Distance in tiles from every grid vertex to the nearest vertex where
// `mask` is set (0 on such vertices), by a two-pass 3-4 chamfer transform,
// on a grid `res` vertices wide and `rows` deep.
std::vector<float> distance_to_mask_rect(const std::vector<uint8_t> &mask, int res, int rows, float spacing) {
	const float inf = 1e9f, a = spacing, b = spacing * 1.41421356f;
	std::vector<float> d(mask.size());
	for (size_t i = 0; i < mask.size(); ++i) d[i] = mask[i] ? 0.0f : inf;
	auto at = [&](int x, int z) -> float & { return d[(size_t)z * (size_t)res + (size_t)x]; };
	for (int z = 0; z < rows; ++z)
		for (int x = 0; x < res; ++x) {
			float &c = at(x, z);
			if (x > 0) c = std::min(c, at(x - 1, z) + a);
			if (z > 0) {
				c = std::min(c, at(x, z - 1) + a);
				if (x > 0) c = std::min(c, at(x - 1, z - 1) + b);
				if (x + 1 < res) c = std::min(c, at(x + 1, z - 1) + b);
			}
		}
	for (int z = rows - 1; z >= 0; --z)
		for (int x = res - 1; x >= 0; --x) {
			float &c = at(x, z);
			if (x + 1 < res) c = std::min(c, at(x + 1, z) + a);
			if (z + 1 < rows) {
				c = std::min(c, at(x, z + 1) + a);
				if (x + 1 < res) c = std::min(c, at(x + 1, z + 1) + b);
				if (x > 0) c = std::min(c, at(x - 1, z + 1) + b);
			}
		}
	return d;
}

std::vector<float> distance_to_mask(const std::vector<uint8_t> &mask, int res, float spacing) {
	return distance_to_mask_rect(mask, res, res, spacing);
}

float grid_spacing(const HeightmapGrid &hm) { return 2.0f * hm.map_half / (float)(hm.res - 1); }

// distance_to_mask(!mask) where only the vertices inside `mask` are read: the
// transform runs on the mask's bounding box plus a ring of one vertex, which
// gives the same values there (every path out of the box crosses the ring,
// whose vertices are all sources), at a fraction of the cost for small areas.
std::vector<float> distance_inside_mask(const std::vector<uint8_t> &mask, int res, float spacing) {
	int x0 = res, z0 = res, x1 = -1, z1 = -1;
	for (int z = 0; z < res; ++z)
		for (int x = 0; x < res; ++x)
			if (mask[(size_t)z * (size_t)res + (size_t)x]) { x0 = std::min(x0, x); x1 = std::max(x1, x); z0 = std::min(z0, z); z1 = std::max(z1, z); }
	std::vector<float> d(mask.size(), 0.0f);
	if (x1 < 0) return d;
	x0 = std::max(0, x0 - 1); z0 = std::max(0, z0 - 1); x1 = std::min(res - 1, x1 + 1); z1 = std::min(res - 1, z1 + 1);
	const int w = x1 - x0 + 1, h = z1 - z0 + 1;
	std::vector<uint8_t> outside((size_t)w * (size_t)h);
	for (int z = 0; z < h; ++z)
		for (int x = 0; x < w; ++x) outside[(size_t)z * (size_t)w + (size_t)x] = !mask[(size_t)(z + z0) * (size_t)res + (size_t)(x + x0)];
	const auto sub = distance_to_mask_rect(outside, w, h, spacing);
	for (int z = 0; z < h; ++z)
		for (int x = 0; x < w; ++x) d[(size_t)(z + z0) * (size_t)res + (size_t)(x + x0)] = sub[(size_t)z * (size_t)w + (size_t)x];
	return d;
}

// SmoothElevationPainter(ELEVATION_SET, h, blend) on the vertices below
// `below`: full depth `blend` tiles in from the shore, eased in before that.
void lower_sea_ground(HeightmapGrid &hm, float below, float target, float blend) {
	std::vector<uint8_t> land(hm.data.size());
	for (size_t i = 0; i < hm.data.size(); ++i) land[i] = hm.data[i] >= below;
	const auto dist = distance_to_mask(land, hm.res, grid_spacing(hm));
	for (size_t i = 0; i < hm.data.size(); ++i) {
		if (land[i]) continue;
		const float t = std::clamp(dist[i] / std::max(blend, 1e-3f), 0.0f, 1.0f);
		hm.data[i] += (target - hm.data[i]) * t;
	}
}

// SmoothingPainter(1, strength, 1): each vertex moves `strength` of the way
// to the inverse-distance (Manhattan) weighted mean of its 8 neighbours.
// `inside` limits it to vertices within that many tiles of the centre.
void shepard_smooth(HeightmapGrid &hm, float strength, float inside = 1e9f) {
	const std::vector<float> src = hm.data;
	for (int z = 0; z < hm.res; ++z)
		for (int x = 0; x < hm.res; ++x) {
			float wx, wz;
			hm.grid_to_world(x, z, wx, wz);
			if (wx * wx + wz * wz > inside * inside) continue;
			float sum = 0.0f, wsum = 0.0f;
			for (int dz = -1; dz <= 1; ++dz)
				for (int dx = -1; dx <= 1; ++dx) {
					if (!dx && !dz) continue;
					const int nx = x + dx, nz = z + dz;
					if (nx < 0 || nz < 0 || nx >= hm.res || nz >= hm.res) continue;
					const float w = 1.0f / (float)(std::abs(dx) + std::abs(dz));
					sum += src[(size_t)nz * (size_t)hm.res + (size_t)nx] * w;
					wsum += w;
				}
			float &h = hm.data[(size_t)z * (size_t)hm.res + (size_t)x];
			h = strength * sum / wsum + (1.0f - strength) * src[(size_t)z * (size_t)hm.res + (size_t)x];
		}
}

// RandomMap.getSlope: mean height difference to the 8 neighbours, in 0 A.D.
// metres per tile.
std::vector<float> slopes_m(const HeightmapGrid &hm) {
	const float sp = grid_spacing(hm);
	std::vector<float> s(hm.data.size(), 0.0f);
	for (int z = 0; z < hm.res; ++z)
		for (int x = 0; x < hm.res; ++x) {
			float sum = 0.0f;
			int n = 0;
			for (int dz = -1; dz <= 1; ++dz)
				for (int dx = -1; dx <= 1; ++dx) {
					if (!dx && !dz) continue;
					const int nx = x + dx, nz = z + dz;
					if (nx < 0 || nz < 0 || nx >= hm.res || nz >= hm.res) continue;
					sum += std::fabs(hm.get(nx, nz) - hm.get(x, z)) / sp;
					++n;
				}
			s[(size_t)z * (size_t)hm.res + (size_t)x] = n ? sum / (float)n / kMetre : 0.0f;
		}
	return s;
}

// The largest 8-connected set of vertices land units can reach (not deep
// water). RTS0 keeps every base on it so all players can meet on foot; 0 A.D.
// may start a player on an island and expects ships.
std::vector<uint8_t> largest_walkable_region(const HeightmapGrid &hm, float water_h) {
	const float deep = water_h + NAV_WATER_MARGIN - LAND_MAX_WATER_DEPTH;
	const int res = hm.res;
	std::vector<int> comp(hm.data.size(), -1);
	std::vector<int> sizes;
	std::vector<int> stack;
	for (int start = 0; start < (int)hm.data.size(); ++start) {
		if (comp[start] >= 0 || hm.data[start] < deep) continue;
		const int id = (int)sizes.size();
		sizes.push_back(0);
		stack.assign(1, start);
		comp[start] = id;
		while (!stack.empty()) {
			const int c = stack.back();
			stack.pop_back();
			++sizes[id];
			const int cx = c % res, cz = c / res;
			for (int dz = -1; dz <= 1; ++dz)
				for (int dx = -1; dx <= 1; ++dx) {
					const int nx = cx + dx, nz = cz + dz;
					if (nx < 0 || nz < 0 || nx >= res || nz >= res) continue;
					const int ni = nz * res + nx;
					if (comp[ni] >= 0 || hm.data[ni] < deep) continue;
					comp[ni] = id;
					stack.push_back(ni);
				}
		}
	}
	std::vector<uint8_t> keep(hm.data.size(), 0);
	if (sizes.empty()) return keep;
	const int best = (int)(std::max_element(sizes.begin(), sizes.end()) - sizes.begin());
	for (size_t i = 0; i < keep.size(); ++i) keep[i] = comp[i] == best;
	return keep;
}

// groupPlayersByArea: of all orders of the start spots, take the one where
// the distances between consecutive teammates (players sorted by team),
// averaged per team, add up least.
void group_players_by_area(std::vector<std::pair<float, float>> &locs, int num_players, const uint8_t *teams) {
	std::vector<int> order(num_players);
	for (int i = 0; i < num_players; ++i) order[i] = i;
	auto team = [&](int p) { return teams ? (int)teams[p] : 0; };
	std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return team(a) < team(b); });
	bool any_team = false;
	for (int i = 1; i < num_players; ++i) any_team |= team(order[i]) != 0 && team(order[i]) == team(order[i - 1]);
	if (!any_team) return;
	std::vector<int> perm(num_players);
	for (int i = 0; i < num_players; ++i) perm[i] = i;
	std::vector<int> best = perm;
	float best_cost = 1e30f;
	do {
		float dist = 0.0f, team_dist = 0.0f;
		int team_size = 0;
		for (int i = 1; i < num_players; ++i) {
			const int t1 = team(order[i - 1]), t2 = team(order[i]);
			++team_size;
			if (t1 != 0 && t1 == t2) {
				const auto &a = locs[perm[i - 1]], &b = locs[perm[i]];
				team_dist += det::hypot(a.first - b.first, a.second - b.second);
			} else {
				dist += team_dist / (float)team_size;
				team_dist = 0.0f;
				team_size = 0;
			}
		}
		if (team_size) dist += team_dist / (float)team_size;
		if (dist < best_cost) { best_cost = dist; best = perm; }
	} while (std::next_permutation(perm.begin(), perm.end()));
	std::vector<std::pair<float, float>> out(num_players);
	for (int i = 0; i < num_players; ++i) out[order[i]] = locs[best[i]];
	locs = out;
}

// playerPlacementRandom: random allowed spots at least a quarter of the map
// apart (relaxed by 5% every 25 resets of 500 failed picks) and 0.08 of the
// map in from the edge. Returns false when no spot is allowed or it gives up.
bool player_placement_random(const HeightmapGrid &hm, RmsRng &rng, const std::vector<uint8_t> &allowed, int num_players,
		std::vector<std::pair<float, float>> &out) {
	const float half = hm.map_half, size = 2.0f * half;
	std::vector<std::pair<float, float>> pts;
	for (int z = 0; z < hm.res; ++z)
		for (int x = 0; x < hm.res; ++x)
			if (allowed[(size_t)z * (size_t)hm.res + (size_t)x]) {
				float wx, wz;
				hm.grid_to_world(x, z, wx, wz);
				pts.push_back({wx, wz});
			}
	if (pts.empty()) return false;
	float min_d2 = (0.25f * size) * (0.25f * size);
	const float edge = half - 0.08f * size;
	int attempts = 0, resets = 0;
	out.clear();
	while ((int)out.size() < num_players) {
		const auto p = pts[(size_t)rng.range_int(0, (int)pts.size() - 1)];
		bool bad = p.first * p.first + p.second * p.second > edge * edge;
		for (const auto &q : out)
			if ((q.first - p.first) * (q.first - p.first) + (q.second - p.second) * (q.second - p.second) < min_d2) { bad = true; break; }
		if (!bad) { out.push_back(p); continue; }
		if (++attempts > 500) {
			out.clear();
			attempts = 0;
			if (++resets % 25 == 0) min_d2 *= 0.95f;
			if (resets == 500) return false;
		}
	}
	return true;
}

// Flattens each start like 0 A.D.'s "Flatten the initial CC area" and fills
// in the spawns, facing the map centre.
void make_heightmap_spawns(HeightmapGrid &hm, std::vector<RmsGenerator::PlayerSpawn> &spawns,
		const std::vector<std::pair<float, float>> &locs, float radius, float blend, float water_h) {
	spawns.clear();
	for (int i = 0; i < (int)locs.size(); ++i) {
		const float sx = locs[i].first, sz = locs[i].second, l = std::max(1e-3f, det::hypot(sx, sz));
		const float bh = std::max(water_h + 1.0f, hm.sample_world(sx, sz));
		spawns.push_back({(uint8_t)i, sx, sz, bh, -sx / l, -sz / l});
		hm.level_area(sx, sz, radius, blend, bh);
	}
}

} // namespace

void RmsGenerator::synth_belgian_uplands(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// belgian_uplands.js: random heights between MIN_HEIGHT and MAX_HEIGHT x
	// size / 8192 metres (RandomElevationPainter), SmoothingPainter(2, 1, 20)
	// and rescaled to that range, so the hills keep one steepness at every
	// size; the water line lies scaleByMapSize(1/5, 1/3) of the way up, so
	// lakes fill the lowest valleys. Players at random (playerPlacementRandom)
	// 15 tiles inside the ground between the water line and half way up the
	// land above it, within the map's circle; the script remakes the map
	// until they fit. The bases are not flattened.
	water_h = 1.0f;
	const float half = hm.map_half, size = 2.0f * half;
	const float lo = -20.0f * size / 8192.0f, hi = (65535.0f / 92.0f - 20.0f) * size / 8192.0f;
	const float cover = 0.2f + (1.0f / 3.0f - 0.2f) * std::clamp((size - 128.0f) / 384.0f, 0.0f, 1.0f);
	const float sea = lo + cover * (hi - lo), band_hi = sea + 0.5f * (hi - sea);
	const float sp = grid_spacing(hm);
	const int r2 = std::max(1, (int)std::lround(2.0f / sp));
	const size_t N = hm.data.size();
	std::vector<std::pair<float, float>> locs;
	for (int attempt = 0; attempt < 20; ++attempt) {
		for (float &h : hm.data) h = rng.range_float(lo, hi);
		for (int it = 0; it < 20; ++it) {
			const std::vector<float> src = hm.data;
			for (int z = 0; z < hm.res; ++z)
				for (int x = 0; x < hm.res; ++x) {
					float sum = 0.0f, wsum = 0.0f;
					for (int dz = -r2; dz <= r2; ++dz)
						for (int dx = -r2; dx <= r2; ++dx) {
							const int nx = x + dx, nz = z + dz;
							if ((!dx && !dz) || nx < 0 || nz < 0 || nx >= hm.res || nz >= hm.res) continue;
							const float w = 1.0f / (float)(std::abs(dx) + std::abs(dz));
							sum += src[(size_t)nz * (size_t)hm.res + (size_t)nx] * w;
							wsum += w;
						}
					hm.data[(size_t)z * (size_t)hm.res + (size_t)x] = sum / wsum;
				}
		}
		const auto mm = std::minmax_element(hm.data.begin(), hm.data.end());
		const float a = *mm.first, b = std::max(*mm.second, a + 1e-3f);
		for (float &h : hm.data) h = lo + (h - a) / (b - a) * (hi - lo);
		std::vector<uint8_t> outside(N, 0);
		for (int z = 0; z < hm.res; ++z)
			for (int x = 0; x < hm.res; ++x) {
				float wx, wz;
				hm.grid_to_world(x, z, wx, wz);
				const size_t i = (size_t)z * (size_t)hm.res + (size_t)x;
				outside[i] = det::hypot(wx, wz) > half - 3.0f || hm.data[i] < sea || hm.data[i] > band_hi;
			}
		const auto to_out = distance_to_mask(outside, hm.res, sp);
		std::vector<uint8_t> allowed(N, 0);
		for (size_t i = 0; i < N; ++i) allowed[i] = to_out[i] >= 15.0f;
		if (player_placement_random(hm, rng, allowed, num_players, locs)) break;
		locs.clear();
	}
	for (float &h : hm.data) h = coast_metres(water_h, h - sea);
	if ((int)locs.size() == num_players) {
		// Bases on the slopes as they are; only the Civic Center's own footprint is levelled.
		make_heightmap_spawns(hm, spawns, locs, 6.0f, 4.0f, water_h);
		return;
	}
	place_ring_spawns(hm, spawns, num_players, half * 0.62f, rng.range_float(0.0f, 6.2831853f), 6.2831853f, coast_metres(water_h, 3.0f));
	for (const auto &sp_ : spawns) lift_area(hm, sp_.x, sp_.z, 16.0f, 6.0f, sp_.base_height);
}

void RmsGenerator::synth_red_sea(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players,
		const uint8_t *teams) {
	// red_sea.js: LoadHeightmapImage("red_sea.png", 0, 25) over the whole
	// map, sea ground (height <= 0) lowered to heightScale(-4) with a 2-tile
	// blend, SmoothingPainter(1, scaleByMapSize(0.1, 0.5), 1), cliffs where
	// the slope is 2 or more (2 tiles from water), and players at random
	// (playerPlacementRandom) scaleByMapSize(5, 10) from cliffs and a base
	// radius inside the land. heightScale(n) = n * size / 320 metres.
	water_h = 1.0f;
	const float half = hm.map_half, size = 2.0f * half;
	const int n = heightmap_images::red_sea_size;
	paint_heightmap(hm, red_sea_image(), n, 0, 0, n, 0.0f, 25.0f, water_h);
	lower_sea_ground(hm, water_h + 0.001f, water_h - 4.0f * size / 320.0f * kMetre, 2.0f);
	shepard_smooth(hm, 0.1f + 0.4f * std::clamp((size - 128.0f) / 384.0f, 0.0f, 1.0f));
	const float sp = grid_spacing(hm);
	std::vector<uint8_t> water(hm.data.size()), cliff(hm.data.size()), land(hm.data.size());
	for (size_t i = 0; i < hm.data.size(); ++i) water[i] = hm.data[i] <= water_h;
	const auto to_water = distance_to_mask(water, hm.res, sp);
	const auto slope = slopes_m(hm);
	for (size_t i = 0; i < hm.data.size(); ++i) cliff[i] = to_water[i] > 2.0f && slope[i] >= 2.0f;
	const auto to_cliff = distance_to_mask(cliff, hm.res, sp);
	// Land: the disk of radius half that is not water; stay a base radius in.
	for (int z = 0; z < hm.res; ++z)
		for (int x = 0; x < hm.res; ++x) {
			float wx, wz;
			hm.grid_to_world(x, z, wx, wz);
			const size_t i = (size_t)z * (size_t)hm.res + (size_t)x;
			land[i] = !water[i] && wx * wx + wz * wz <= half * half;
		}
	std::vector<uint8_t> not_land(land.size());
	for (size_t i = 0; i < land.size(); ++i) not_land[i] = !land[i];
	const auto to_not_land = distance_to_mask(not_land, hm.res, sp);
	const auto region = largest_walkable_region(hm, water_h);
	const float base_r = (float)scale_by_map_size(15, 25, half);
	const float cliff_gap = (float)scale_by_map_size(5, 10, half);
	std::vector<uint8_t> allowed(land.size());
	for (size_t i = 0; i < land.size(); ++i)
		allowed[i] = region[i] && to_cliff[i] > cliff_gap && to_not_land[i] > base_r;
	std::vector<std::pair<float, float>> locs;
	if (!player_placement_random(hm, rng, allowed, num_players, locs)) {
		place_ring_spawns(hm, spawns, num_players, half * 0.6f, rng.range_float(0.0f, 6.2831853f), 6.2831853f, water_h + 1.5f);
		for (const auto &s : spawns) lift_area(hm, s.x, s.z, 20.0f, 6.0f, s.base_height);
		return;
	}
	group_players_by_area(locs, num_players, teams);
	make_heightmap_spawns(hm, spawns, locs, base_r * 0.8f, 6.0f, water_h);
}

void RmsGenerator::synth_hellas(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players,
		const uint8_t *teams) {
	// hellas.js: pick a random square of hellas.png (0.01-0.2 of its side)
	// whose land ratio fits the map style (tiny maps: mainland, 0.95-1;
	// otherwise "few water", 0.65-0.9, which is always the style picked
	// there), paint it with heights 0..100, lower everything at or below the
	// shoreline (heightScale(1)) to heightScale(-6) with a 5-tile blend,
	// smooth until cliffs (slope >= 2, 2 tiles from water) cover under 18% of
	// the map (and at least 8% unless the style allows under 0.75 land), and
	// place players at random scaleByMapSize(6, 15) from cliffs and
	// scaleByMapSize(10, 20) from water; any check failing starts over.
	// RTS0 reads a 360 px copy of the 720 px image, so it takes squares of
	// at least 0.04 of the side (14 px) rather than 0.01.
	water_h = 1.0f;
	const float half = hm.map_half, size = 2.0f * half;
	const auto &img = hellas_image();
	const int n = heightmap_images::hellas_size;
	const float min_land = size >= 192.0f ? 0.65f : 0.95f, max_land = size >= 192.0f ? 0.9f : 1.0f;
	const float min_cliff = max_land < 0.75f ? 0.0f : 0.08f, max_cliff = 0.18f;
	const float shoreline = water_h + 1.0f * size / 320.0f * kMetre;
	const float sp = grid_spacing(hm);
	const float lo = 0.0f, hi = 100.0f * (size + 1.0f) / 321.0f;
	auto land_ratio = [&]() {
		int in = 0, landc = 0;
		for (int z = 0; z < hm.res; ++z)
			for (int x = 0; x < hm.res; ++x) {
				float wx, wz;
				hm.grid_to_world(x, z, wx, wz);
				if (wx * wx + wz * wz > half * half) continue;
				++in;
				landc += hm.get(x, z) > shoreline;
			}
		return in ? (float)landc / (float)in : 0.0f;
	};
	for (int attempt = 0; attempt < 1000; ++attempt) {
		const int sub = std::max(4, (int)std::floor(rng.range_float(0.04f, 0.2f) * (float)n));
		const int x0 = (int)std::floor(rng.next_float() * (float)(n - sub));
		const int y0 = (int)std::floor(rng.next_float() * (float)(n - sub));
		// Quick area test on the image pixels inside the sub-square's disk.
		{
			int in = 0, landc = 0;
			const float c = (float)sub / 2.0f, r = std::max(1.0f, c - 1.0f);
			for (int j = 0; j < sub; ++j)
				for (int i = 0; i < sub; ++i) {
					if (((float)i - c) * ((float)i - c) + ((float)j - c) * ((float)j - c) > r * r) continue;
					++in;
					const float m = lo + (hi - lo) * (float)img[(size_t)(y0 + j) * (size_t)n + (size_t)(x0 + i)] / 255.0f;
					landc += water_h + m * kMetre > shoreline;
				}
			const float ratio = in ? (float)landc / (float)in : 0.0f;
			if (ratio < min_land || ratio > max_land) continue;
		}
		paint_heightmap(hm, img, n, x0, y0, sub, 0.0f, 100.0f, water_h);
		const float ratio = land_ratio();
		if (ratio < min_land || ratio > max_land) continue;
		lower_sea_ground(hm, shoreline + 0.001f, water_h - 6.0f * size / 320.0f * kMetre, 5.0f);
		std::vector<uint8_t> water(hm.data.size()), cliff(hm.data.size());
		for (size_t i = 0; i < hm.data.size(); ++i) water[i] = hm.data[i] <= shoreline;
		const auto to_water = distance_to_mask(water, hm.res, sp);
		float cliff_ratio = 1.0f;
		for (int pass = 0; pass < 200 && cliff_ratio >= max_cliff; ++pass) {
			shepard_smooth(hm, 0.5f, half - 4.0f);
			const auto slope = slopes_m(hm);
			int count = 0;
			for (size_t i = 0; i < hm.data.size(); ++i) {
				cliff[i] = to_water[i] > 2.0f && slope[i] >= 2.0f;
				count += cliff[i];
			}
			cliff_ratio = (float)count / (float)hm.data.size();
		}
		if (cliff_ratio < min_cliff || cliff_ratio >= max_cliff) continue;
		const auto to_cliff = distance_to_mask(cliff, hm.res, sp);
		const auto region = largest_walkable_region(hm, water_h);
		const float cliff_gap = (float)scale_by_map_size(6, 15, half), water_gap = (float)scale_by_map_size(10, 20, half);
		std::vector<uint8_t> allowed(hm.data.size());
		for (size_t i = 0; i < allowed.size(); ++i)
			allowed[i] = region[i] && to_cliff[i] > cliff_gap && to_water[i] > water_gap;
		std::vector<std::pair<float, float>> locs;
		if (!player_placement_random(hm, rng, allowed, num_players, locs)) continue;
		group_players_by_area(locs, num_players, teams);
		const float base_r = (float)scale_by_map_size(15, 25, half) * 0.8f;
		make_heightmap_spawns(hm, spawns, locs, base_r, base_r / 2.0f, water_h);
		return;
	}
	// Nothing fitted: a plain mainland so the match still starts.
	hm.init(hm.res, hm.map_half, water_h + 2.0f);
	hm.apply_fbm(rng, 4, 0.03f, 0.5f, 2.0f, 2.0f);
	place_ring_spawns(hm, spawns, num_players, half * 0.6f, rng.range_float(0.0f, 6.2831853f), 6.2831853f, water_h + 2.0f);
	for (const auto &s : spawns) lift_area(hm, s.x, s.z, 20.0f, 6.0f, s.base_height);
}

namespace {

// 0 A.D. height in metres (water level 0) -> RTS0 height on a map whose
// water line is water_h.
inline float metres(float water_h, float m) { return water_h + m * kMetre; }

// Height RTS0's land units can still wade through (NAV_SHALLOW).
inline float wade_height(float water_h) { return water_h + NAV_WATER_MARGIN - LAND_MAX_WATER_DEPTH + 0.05f; }

// SmoothElevationPainter(ELEVATION_SET, h, blend) over the vertices in
// `mask`: full height `blend` tiles in from the area's edge, eased before.
void paint_mask_elevation(HeightmapGrid &hm, const std::vector<uint8_t> &mask, float h, float blend) {
	const auto d = distance_inside_mask(mask, hm.res, grid_spacing(hm));
	for (size_t i = 0; i < mask.size(); ++i) {
		if (!mask[i]) continue;
		const float t = std::clamp(d[i] / std::max(blend, 1e-3f), 0.0f, 1.0f);
		hm.data[i] += (h - hm.data[i]) * t;
	}
}

// The areas the synth_* recorded for paint_terrain (script_terrains.h AreaLayers),
// as tile indices on paint_terrain's grid, in the order the script paints them.
struct MapArea {
	uint8_t kind;
	std::vector<uint32_t> tiles;
};
thread_local std::vector<MapArea> t_map_areas;

// Records the tiles whose centre's nearest vertex is in `mask` as an area of `kind`.
void record_area(const HeightmapGrid &hm, const std::vector<uint8_t> &mask, AreaKind kind) {
	const float half = hm.map_half, sp = grid_spacing(hm);
	const int res = std::clamp((int)std::lround(2.0f * half), 1, Simulation::MAX_TERRAIN_TILE_RES);
	const float cell = 2.0f * half / (float)res;
	MapArea a{(uint8_t)kind, {}};
	for (int tz = 0; tz < res; ++tz)
		for (int tx = 0; tx < res; ++tx) {
			const int vx = std::clamp((int)std::lround(((float)tx + 0.5f) * cell / sp), 0, hm.res - 1);
			const int vz = std::clamp((int)std::lround(((float)tz + 0.5f) * cell / sp), 0, hm.res - 1);
			if (mask[(size_t)vz * (size_t)hm.res + (size_t)vx]) a.tiles.push_back((uint32_t)(tz * res + tx));
		}
	if (!a.tiles.empty()) t_map_areas.push_back(std::move(a));
}

// Sets the vertices within r of (cx, cz) in mask.
void mask_disk(const HeightmapGrid &hm, std::vector<uint8_t> &mask, float cx, float cz, float r) {
	const float sp = grid_spacing(hm), half = hm.map_half;
	const int x0 = std::max(0, (int)((cx - r + half) / sp)), x1 = std::min(hm.res - 1, (int)((cx + r + half) / sp) + 1);
	const int z0 = std::max(0, (int)((cz - r + half) / sp)), z1 = std::min(hm.res - 1, (int)((cz + r + half) / sp) + 1);
	for (int z = z0; z <= z1; ++z)
		for (int x = x0; x <= x1; ++x) {
			const float wx = -half + (float)x * sp, wz = -half + (float)z * sp;
			if ((wx - cx) * (wx - cx) + (wz - cz) * (wz - cz) <= r * r) mask[(size_t)z * (size_t)hm.res + (size_t)x] = 1;
		}
}

// ChainPlacer: circles of radius min_r..max_r, each centred on the edge of
// one placed before, starting from `start` (radius first_r when > 0).
std::vector<uint8_t> chain_placer(const HeightmapGrid &hm, RmsRng &rng, float cx, float cz, float min_r, float max_r,
		int circles, float first_r = 0.0f) {
	std::vector<uint8_t> mask(hm.data.size(), 0);
	struct C { float x, z, r; };
	std::vector<C> placed;
	placed.push_back({cx, cz, first_r > 0.0f ? first_r : rng.range_float(min_r, max_r)});
	mask_disk(hm, mask, cx, cz, placed[0].r);
	for (int i = 1; i < circles; ++i) {
		const C &base = placed[(size_t)rng.range_int(0, (int)placed.size() - 1)];
		const auto p = polar(rng.range_float(0.0f, 6.2831853f), base.r);
		const C c{base.x + p.first, base.z + p.second, rng.range_float(min_r, max_r)};
		if (std::fabs(c.x) > hm.map_half || std::fabs(c.z) > hm.map_half) continue;
		placed.push_back(c);
		mask_disk(hm, mask, c.x, c.z, c.r);
	}
	return mask;
}

// Hills (createHills: elevation 18, smoothing 2) on dry land, `clear` from
// the bases and `water_gap` from water.
void scatter_dry_hills(HeightmapGrid &hm, RmsRng &rng, const std::vector<RmsGenerator::PlayerSpawn> &spawns, int count,
		float clear, float water_gap, float water_h) {
	const float half = hm.map_half;
	for (int i = 0, tries = 0; i < count && tries < count * 40; ++tries) {
		const float hx = rng.range_float(-0.9f, 0.9f) * half, hz = rng.range_float(-0.9f, 0.9f) * half;
		const float r = rng.range_float(4.0f, 7.0f);
		bool ok = true;
		for (const auto &sp : spawns)
			if (det::hypot(hx - sp.x, hz - sp.z) < clear + r) { ok = false; break; }
		for (int k = 0; ok && k < 12; ++k) {
			const auto p = polar((float)k * 0.5235988f, r + water_gap);
			ok = hm.sample_world(hx + p.first, hz + p.second) > water_h + 0.5f;
		}
		if (!ok || hm.sample_world(hx, hz) <= water_h + 0.5f) continue;
		hm.raise_plateau(hx, hz, r, 0.0f, 0.0f, 18.0f * kMetre, r * 0.6f);
		std::vector<uint8_t> m(hm.data.size(), 0);
		mask_disk(hm, m, hx, hz, r);
		record_area(hm, m, AREA_HILL);
		++i;
	}
}

// createBumps: small random lumps on dry land away from the bases.
void dry_bumps(HeightmapGrid &hm, RmsRng &rng, const std::vector<RmsGenerator::PlayerSpawn> &spawns, int count,
		float clear, float above) {
	const float half = hm.map_half;
	for (int i = 0; i < count; ++i) {
		const float bx = rng.range_float(-half, half), bz = rng.range_float(-half, half);
		if (hm.sample_world(bx, bz) <= above) continue;
		bool ok = true;
		for (const auto &sp : spawns)
			if (det::hypot(bx - sp.x, bz - sp.z) < clear) { ok = false; break; }
		if (!ok) continue;
		const float r = rng.range_float(2.0f, 4.0f), h = rng.range_float(1.0f, 2.0f) * kMetre;
		const float sp = grid_spacing(hm);
		for (float dz = -r; dz <= r; dz += sp)
			for (float dx = -r; dx <= r; dx += sp) {
				const float d = det::hypot(dx, dz);
				if (d > r) continue;
				float gx, gz;
				hm.world_to_grid(bx + dx, bz + dz, gx, gz);
				const int ix = (int)std::lround(gx), iz = (int)std::lround(gz);
				if (hm.get(ix, iz) > above) hm.add(ix, iz, h * (1.0f - d / r));
			}
	}
}

} // namespace

void RmsGenerator::synth_english_channel(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players,
		const uint8_t *teams) {
	// english_channel.js: land at 3 m split by a channel a quarter of the map
	// wide (paintRiver, riverbed -4 m, fade scaleByMapSize(3, 10)) through the
	// centre at a random angle; players on both banks (playerPlacementRiver,
	// 0.6 of the map apart); randIntInclusive(9, scaleByMapSize(13, 21))
	// tributaries scaleByMapSize(10, 20) wide from the channel to the map edge,
	// kept off the bases, and two lines of -2 m shallows (scaleByMapSize(8,
	// 12) wide) along the channel at 0.25 and 0.75 of the map so each bank
	// stays walkable. Crossing the channel needs ships. 0 A.D. metres x 0.25.
	water_h = 1.0f;
	const float half = hm.map_half, size = 2.0f * half;
	const float land = metres(water_h, 3.0f), bed = metres(water_h, -4.0f);
	hm.init(hm.res, hm.map_half, land + 0.1f * kMetre);
	const float angle = rng.range_float(0.0f, 6.2831853f);
	const float ux = det::cos(angle), uz = det::sin(angle); // along the channel
	const float nx = -uz, nz = ux;                           // across it
	// Players: playerPlacementRiver(angle + pi/2, 0.6 * size).
	std::vector<std::pair<float, float>> locs(num_players);
	const bool even = num_players % 2 == 0;
	for (int i = 0; i < num_players; ++i) {
		const bool cur_even = i % 2 == 0;
		const int divident = (even || cur_even) ? (i + 1) % 2 : 0;
		const int divisor = even ? 0 : (cur_even ? 1 : -1);
		const float across = (i % 2 ? 0.3f : -0.3f) * size;
		const float f = ((float)(i - 1 + divident) / 2.0f + 1.0f) / ((float)(num_players + divisor) / 2.0f + 1.0f);
		const float along = (f - 0.5f) * size;
		locs[i] = {ux * along + nx * across, uz * along + nz * across};
	}
	group_players_by_area(locs, num_players, teams);
	// The channel.
	const float width = 0.125f * size, fade = (float)scale_by_map_size(3, 10, half);
	const float wobble = rng.range_float(0.0f, 6.2831853f);
	for (int z = 0; z < hm.res; ++z)
		for (int x = 0; x < hm.res; ++x) {
			float wx, wz;
			hm.grid_to_world(x, z, wx, wz);
			const float t = wx * ux + wz * uz;
			const float d = std::fabs(wx * nx + wz * nz + 2.0f * det::sin(t * 0.08f + wobble));
			if (d < width) {
				const float k = smoothstep(width, width - fade, d);
				hm.set(x, z, land + (bed - land) * k);
			}
		}
	// Tributaries, from a point on the channel to past the map edge.
	const int rivers = rng.range_int(9, scale_by_map_size(13, 21, half));
	const float river_w = (float)scale_by_map_size(10, 20, half);
	const float keep = 20.0f + 8.0f; // player class (base radius) + avoidClasses(clPlayer, 8)
	for (int i = 0; i < rivers; ++i) {
		const float t0 = (rng.range_float(0.05f, 0.95f) - 0.5f) * size;
		const float sign = rng.range_int(0, 1) ? 1.0f : -1.0f;
		const float a = angle - sign * rng.range_float(0.6283185f, 6.2831853f - 0.6283185f);
		const float sx = ux * t0, sz = uz * t0, ex = det::cos(a) * size, ez = det::sin(a) * size;
		// PathPlacer waviness 0.4: a few jittered points on the way.
		std::vector<std::pair<float, float>> pts{{sx, sz}};
		const float len = det::hypot(ex - sx, ez - sz), px = -(ez - sz) / len, pz = (ex - sx) / len;
		for (int k = 1; k < 6; ++k) {
			const float f = (float)k / 6.0f, j = rng.range_float(-0.1f, 0.1f) * len * 0.4f;
			pts.push_back({sx + (ex - sx) * f + px * j, sz + (ez - sz) * f + pz * j});
		}
		pts.push_back({ex, ez});
		std::vector<uint8_t> mask(hm.data.size(), 0);
		for (size_t k = 0; k + 1 < pts.size(); ++k) {
			const float ax = pts[k].first, az = pts[k].second, bx = pts[k + 1].first, bz = pts[k + 1].second;
			const float sl = det::hypot(bx - ax, bz - az);
			for (float s = 0.0f; s <= sl; s += 1.0f) {
				const float cx = ax + (bx - ax) * s / sl, cz = az + (bz - az) * s / sl;
				if (std::fabs(cx) > half + river_w || std::fabs(cz) > half + river_w) break;
				mask_disk(hm, mask, cx, cz, river_w * 0.5f);
			}
		}
		for (int z = 0; z < hm.res; ++z)
			for (int x = 0; x < hm.res; ++x) {
				float wx, wz;
				hm.grid_to_world(x, z, wx, wz);
				for (const auto &l : locs)
					if (det::hypot(wx - l.first, wz - l.second) < keep) { mask[(size_t)z * (size_t)hm.res + (size_t)x] = 0; break; }
			}
		std::vector<float> before = hm.data;
		paint_mask_elevation(hm, mask, bed, 4.0f);
		for (size_t k = 0; k < hm.data.size(); ++k) hm.data[k] = std::min(hm.data[k], before[k]);
	}
	// Shallows across the tributaries (createPassage at 0.25 and 0.75).
	const float wade = wade_height(water_h), shallow_w = (float)scale_by_map_size(8, 12, half) * 0.5f;
	for (int z = 0; z < hm.res; ++z)
		for (int x = 0; x < hm.res; ++x) {
			float wx, wz;
			hm.grid_to_world(x, z, wx, wz);
			const float d = std::fabs(std::fabs(wx * nx + wz * nz) - 0.25f * size);
			if (d > shallow_w + 2.0f) continue;
			const float h = hm.get(x, z);
			if (h >= wade) continue;
			const float k = d <= shallow_w ? 1.0f : 1.0f - (d - shallow_w) / 2.0f;
			hm.set(x, z, h + (wade - h) * k);
		}
	std::vector<PlayerSpawn> probe;
	for (const auto &l : locs) probe.push_back({0, l.first, l.second, land, 0.0f, 0.0f});
	dry_bumps(hm, rng, probe, scale_by_map_size(100, 200, half), 20.0f, land - 0.2f);
	scatter_dry_hills(hm, rng, probe, scale_by_map_size(1, 4, half) * num_players, 20.0f, 5.0f, water_h);
	make_heightmap_spawns(hm, spawns, locs, 16.0f, 6.0f, water_h);
	for (auto &sp : spawns) sp.base_height = std::max(sp.base_height, land);
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 18.0f, 6.0f, sp.base_height);
}

void RmsGenerator::synth_dodecanese(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players,
		const uint8_t *teams) {
	// dodecanese.js: sea at -8 m; players at random (playerPlacementRandom)
	// each on an island of radius scaleByMapSize(20, 29) grown by
	// scaleByMapSize(15, 50) small circles (ChainPlacer); scaleByMapSize(25,
	// 80) more islands of floor(scaleByMapSize(10, 35)) circles kept 6 apart;
	// -2.5 m undersea bumps; a 25 m volcano next to an island; hills of 8 m;
	// and up to scaleByMapSize(2, 12) wooden bridges (at -0.5 m, which RTS0
	// makes wadeable shallows) across straits up to 16 tiles wide, at least
	// 128 tiles apart. Islands otherwise need ships. 0 A.D. metres x 0.25.
	water_h = 1.0f;
	const float half = hm.map_half;
	const float land = metres(water_h, 3.0f);
	hm.init(hm.res, hm.map_half, metres(water_h, -8.0f));
	// Players at random anywhere on the map.
	std::vector<uint8_t> anywhere(hm.data.size(), 1);
	std::vector<std::pair<float, float>> locs;
	if (!player_placement_random(hm, rng, anywhere, num_players, locs)) {
		for (int i = 0; i < num_players; ++i)
			locs.push_back(polar(6.2831853f * (float)i / (float)num_players, half * 0.6f));
	}
	group_players_by_area(locs, num_players, teams);
	std::vector<uint8_t> island(hm.data.size(), 0), player_island(hm.data.size(), 0);
	const float island_r = (float)scale_by_map_size(20, 29, half);
	for (const auto &l : locs) {
		const auto m = chain_placer(hm, rng, l.first, l.second, 2.0f, 6.0f, scale_by_map_size(15, 50, half), island_r);
		for (size_t i = 0; i < m.size(); ++i) if (m[i]) island[i] = player_island[i] = 1;
	}
	paint_mask_elevation(hm, island, land, 4.0f);
	record_area(hm, island, AREA_PLAYER_ISLAND);
	// More islands, at most 20% of each closer than 6 to another.
	const float sp = grid_spacing(hm);
	const int islands = scale_by_map_size(25, 80, half);
	const int circles = (int)std::floor((float)scale_by_map_size(10, 35, half));
	const float max_r = std::floor((float)scale_by_map_size(8, 10, half));
	for (int i = 0, tries = 0; i < islands && tries < islands * 4; ++tries) {
		auto dist = distance_to_mask(island, hm.res, sp);
		const float b_ = rng.range_float(-half, half); const float a_ = rng.range_float(-half, half); const auto m = chain_placer(hm, rng, a_, b_, 6.0f, max_r, circles);
		int in = 0, bad = 0;
		for (size_t k = 0; k < m.size(); ++k)
			if (m[k]) { ++in; bad += dist[k] < 6.0f; }
		if (!in || bad > in / 5) continue;
		std::vector<uint8_t> paint(m.size(), 0);
		for (size_t k = 0; k < m.size(); ++k) paint[k] = m[k] && dist[k] >= 6.0f;
		paint_mask_elevation(hm, paint, land, 4.0f);
		record_area(hm, paint, AREA_ISLAND);
		for (size_t k = 0; k < m.size(); ++k) if (paint[k]) island[k] = 1;
		++i;
	}
	// Undersea bumps.
	{
		const auto dist = distance_to_mask(island, hm.res, sp);
		const int bumps = scale_by_map_size(10, 50, half);
		for (int i = 0; i < bumps; ++i) {
			const float b_ = rng.range_float(-half, half); const float a_ = rng.range_float(-half, half); const auto m = chain_placer(hm, rng, a_, b_, 1.0f,
			                            std::floor((float)scale_by_map_size(4, 6, half)), (int)std::floor((float)scale_by_map_size(16, 40, half)));
			std::vector<uint8_t> paint(m.size(), 0);
			for (size_t k = 0; k < m.size(); ++k) paint[k] = m[k] && dist[k] > 2.0f;
			paint_mask_elevation(hm, paint, metres(water_h, -2.5f), 3.0f);
		}
	}
	// The volcano: within 8 of an island, off the player islands.
	{
		const auto to_island = distance_to_mask(island, hm.res, sp);
		const auto to_player = distance_to_mask(player_island, hm.res, sp);
		const float vr = (float)scale_by_map_size(4, 8, half);
		for (int tries = 0; tries < 200; ++tries) {
			const float vx = rng.range_float(-half, half) * 0.9f, vz = rng.range_float(-half, half) * 0.9f;
			float gx, gz;
			hm.world_to_grid(vx, vz, gx, gz);
			const size_t k = (size_t)std::lround(gz) * (size_t)hm.res + (size_t)std::lround(gx);
			if (to_island[k] > 8.0f || to_player[k] <= vr + 6.0f) continue;
			std::vector<uint8_t> m(hm.data.size(), 0);
			mask_disk(hm, m, vx, vz, vr);
			paint_mask_elevation(hm, m, metres(water_h, 25.0f), 6.0f);
			record_area(hm, m, AREA_VOLCANO);
			break;
		}
	}
	std::vector<PlayerSpawn> probe;
	for (const auto &l : locs) probe.push_back({0, l.first, l.second, land, 0.0f, 0.0f});
	dry_bumps(hm, rng, probe, scale_by_map_size(100, 200, half), 10.0f, land - 0.2f);
	{
		// Hills: 8 m, scaleByMapSize(4, 13), 12 from players, 1 from water.
		const int hills = scale_by_map_size(4, 13, half);
		for (int i = 0, tries = 0; i < hills && tries < hills * 40; ++tries) {
			const float hx = rng.range_float(-0.9f, 0.9f) * half, hz = rng.range_float(-0.9f, 0.9f) * half;
			const float r = rng.range_float(3.0f, 6.0f);
			bool ok = hm.sample_world(hx, hz) > land - 0.3f;
			for (const auto &l : locs)
				if (det::hypot(hx - l.first, hz - l.second) < 20.0f + 12.0f + r) { ok = false; break; }
			for (int k = 0; ok && k < 12; ++k) {
				const auto p = polar((float)k * 0.5235988f, r + 1.0f);
				ok = hm.sample_world(hx + p.first, hz + p.second) > water_h + 0.5f;
			}
			if (!ok) continue;
			hm.raise_plateau(hx, hz, r, 0.0f, 0.0f, 5.0f * kMetre, 2.0f);
			std::vector<uint8_t> m(hm.data.size(), 0);
			mask_disk(hm, m, hx, hz, r);
			record_area(hm, m, AREA_HILL);
			++i;
		}
	}
	// Bridges across narrow straits.
	{
		const float shore_lo = metres(water_h, -2.0f), shore_hi = metres(water_h, 1.0f);
		const float wade = wade_height(water_h);
		auto is_water = [&](float x, float z) { return hm.sample_world(x, z) < shore_lo; };
		auto is_shore = [&](float x, float z) { const float h = hm.sample_world(x, z); return h >= shore_lo && h < shore_hi; };
		std::vector<std::pair<float, float>> shore;
		for (int z = 0; z < hm.res; ++z)
			for (int x = 0; x < hm.res; ++x) {
				const float h = hm.get(x, z);
				if (h < shore_lo || h >= shore_hi) continue;
				float wx, wz;
				hm.grid_to_world(x, z, wx, wz);
				shore.push_back({wx, wz});
			}
		for (size_t i = shore.size(); i > 1; --i) std::swap(shore[i - 1], shore[(size_t)rng.range_int(0, (int)i - 1)]);
		std::vector<std::pair<float, float>> bridges;
		const int max_bridges = scale_by_map_size(2, 12, half);
		const float blen = 16.0f;
		for (const auto &s : shore) {
			if ((int)bridges.size() >= max_bridges) break;
			bool near = false;
			for (const auto &b : bridges) near |= det::hypot(b.first - s.first, b.second - s.second) < blen * 8.0f;
			if (near) continue;
			for (int dir = 0; dir < 4; ++dir) {
				const auto d = polar((float)dir * 1.5707963f, 1.0f);
				const float cx = s.first + d.first * blen / 2.0f, cz = s.second + d.second * blen / 2.0f;
				if (!is_water(cx, cz)) continue;
				const float ex = s.first + d.first * blen, ez = s.second + d.second * blen;
				if (!is_shore(ex, ez)) continue;
				if (!is_water(cx - d.second * blen / 2.0f, cz + d.first * blen / 2.0f) ||
				    !is_water(cx + d.second * blen / 2.0f, cz - d.first * blen / 2.0f)) continue;
				// The bridge deck: a 3-wide strip at wading depth.
				std::vector<uint8_t> m(hm.data.size(), 0);
				for (float t = 0.0f; t <= blen; t += 0.5f) mask_disk(hm, m, s.first + d.first * t, s.second + d.second * t, 1.5f);
				for (size_t k = 0; k < m.size(); ++k)
					if (m[k]) hm.data[k] = std::max(hm.data[k], wade);
				bridges.push_back(s);
				break;
			}
		}
	}
	make_heightmap_spawns(hm, spawns, locs, island_r * 0.6f, 4.0f, water_h);
	for (auto &s : spawns) s.base_height = std::max(s.base_height, land);
	for (const auto &s : spawns) lift_area(hm, s.x, s.z, 16.0f, 4.0f, s.base_height);
}

void RmsGenerator::synth_fortress(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// fortress.js: land at 3 m, players on a circle at f = 0.35 each inside a
	// walled "Spahbod" fortress (placed by place_fortress_walls), round(
	// scaleByMapSize(1, 4) * players) lakes of scaleByMapSize(100, 250) tiles
	// at -4 m kept 7 from the bases' paving and 20 from each other, bumps and
	// scaleByMapSize(1, 4) * players hills. 0 A.D. metres x 0.25.
	water_h = 1.0f;
	const float half = hm.map_half;
	const float land = metres(water_h, 3.0f);
	hm.init(hm.res, hm.map_half, land);
	place_ring_spawns(hm, spawns, num_players, half * 0.70f, rng.range_float(0.0f, 6.2831853f), 6.2831853f, land);
	const int lakes = (int)std::lround((float)scale_by_map_size(1, 4, half) * (float)num_players);
	const float lake_r = std::sqrt((float)scale_by_map_size(100, 250, half) / 3.14159265f);
	std::vector<std::pair<float, float>> placed;
	for (int i = 0, tries = 0; i < lakes && tries < lakes * 40; ++tries) {
		const float lx = rng.range_float(-0.9f, 0.9f) * half, lz = rng.range_float(-0.9f, 0.9f) * half;
		bool ok = true;
		// Paving reaches 21 tiles from the base (half its diagonal), +7.
		for (const auto &sp : spawns)
			if (det::hypot(lx - sp.x, lz - sp.z) < 21.0f + 7.0f + lake_r + 3.0f) { ok = false; break; }
		for (const auto &p : placed)
			if (det::hypot(lx - p.first, lz - p.second) < 20.0f + 2.0f * lake_r) { ok = false; break; }
		if (!ok) continue;
		std::vector<uint8_t> m(hm.data.size(), 0);
		mask_disk(hm, m, lx, lz, lake_r + 3.0f);
		paint_mask_elevation(hm, m, metres(water_h, -4.0f), 3.0f);
		record_area(hm, m, AREA_LAKE);
		placed.push_back({lx, lz});
		++i;
	}
	dry_bumps(hm, rng, spawns, scale_by_map_size(100, 200, half), 26.0f, land - 0.2f);
	scatter_dry_hills(hm, rng, spawns, scale_by_map_size(1, 4, half) * num_players, 26.0f, 5.0f, water_h);
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 26.0f, 4.0f, sp.base_height);
}

namespace {

// fortress.js: each player's "Spahbod" fortress, placeCustomFortress with the player's stone
// WallSet: a square of long walls with a gate in two opposite sides, wall towers, a cornerIn
// tower at each corner, and a barracks, stable, temple and market set into the four sides. The
// first gate faces the map centre. A piece nearer another base than its own is left out, so
// fortresses that would overlap on a crowded map open into each other instead of sealing.
void place_fortress_walls(Simulation &sim, const std::vector<RmsGenerator::PlayerSpawn> &spawns, size_t self) {
	using F = Simulation::FortressPart;
	static constexpr F kSpahbod[] = {
		{Simulation::FORT_GATE, 0}, {Simulation::FORT_TOWER, 0}, {Simulation::FORT_LONG, 0},
		{Simulation::FORT_CORNER_IN, 0}, {Simulation::FORT_LONG, 0}, {Simulation::FORT_BUILDING, BLD_BARRACKS},
		{Simulation::FORT_TOWER, 0}, {Simulation::FORT_LONG, 0}, {Simulation::FORT_TOWER, 0}, {Simulation::FORT_LONG, 0},
		{Simulation::FORT_CORNER_IN, 0}, {Simulation::FORT_LONG, 0}, {Simulation::FORT_BUILDING, BLD_STABLE},
		{Simulation::FORT_TOWER, 0}, {Simulation::FORT_GATE, 0}, {Simulation::FORT_TOWER, 0}, {Simulation::FORT_LONG, 0},
		{Simulation::FORT_CORNER_IN, 0}, {Simulation::FORT_LONG, 0}, {Simulation::FORT_BUILDING, BLD_TEMPLE},
		{Simulation::FORT_TOWER, 0}, {Simulation::FORT_LONG, 0}, {Simulation::FORT_TOWER, 0}, {Simulation::FORT_LONG, 0},
		{Simulation::FORT_CORNER_IN, 0}, {Simulation::FORT_LONG, 0}, {Simulation::FORT_BUILDING, BLD_MARKET},
		{Simulation::FORT_TOWER, 0}};
	struct Ctx { const std::vector<RmsGenerator::PlayerSpawn> *spawns; size_t self; } ctx{&spawns, self};
	auto nearer_other = [](float x, float z, const void *p) {
		const Ctx &c = *static_cast<const Ctx *>(p);
		const auto &all = *c.spawns;
		const float own = det::hypot(x - all[c.self].x, z - all[c.self].z);
		for (size_t j = 0; j < all.size(); ++j)
			if (j != c.self && det::hypot(x - all[j].x, z - all[j].z) < own) return true;
		return false;
	};
	const auto &sp = spawns[self];
	sim.place_custom_fortress(sp.team, sp.x, sp.z, det::atan2(sp.forward_dir_z, sp.forward_dir_x), kSpahbod,
			sizeof(kSpahbod) / sizeof(kSpahbod[0]), nearer_other, &ctx);
}

} // namespace

// =========================================================================
// 0 A.D. trigger maps: maps/random/flood.js and extinct_volcano.js. Their
// rising water is a map trigger (triggers.h; the ported flood_triggers.js and
// extinct_volcano_triggers.js are in simulation.cpp). Heights are 0 A.D.'s
// metres above the water (kTriggerMapHeight, the same factor the triggers
// use for the water's rise), so the hills drown in 0 A.D.'s order. Their 0 m
// is the nav water line (water_h + NAV_WATER_MARGIN, where land turns to
// water), so ground at 0 A.D.'s 1 m stays dry until 0 A.D.'s water reaches it.
// =========================================================================

namespace {
constexpr float kTriggerMapHeight = HEIGHT_PER_METRE;
}

void RmsGenerator::synth_flood(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// flood.js: sea ground -2, a home island at 2 for each player on a circle
	// at f = 0.38, and a mainland at 6 wherever the ground is 40 tiles clear
	// of every home island, with hills (set to 6-20) and mountains (+15-29)
	// on it. The water starts at 0 and rises (flood_triggers.js).
	const float k = kTriggerMapHeight;
	water_h = -NAV_WATER_MARGIN;
	hm.init(hm.res, hm.map_half, -2.0f * k);
	const float half = hm.map_half;
	const float island_r = 1.4f * (float)scale_by_map_size(15, 25, half); // diskArea(1.4 * defaultPlayerBaseRadius())
	const float shore = 6.0f;
	place_ring_spawns(hm, spawns, num_players, 0.38f * 2.0f * half, rng.range_float(0.0f, 6.2831853f), 6.2831853f, 2.0f * k);
	for (auto &sp : spawns) {
		sp.base_height = 2.0f * k;
		hm.level_area(sp.x, sp.z, island_r - shore, shore, 2.0f * k);
	}
	// The mainland ("central island", a ChainPlacer blob from the centre):
	// avoidClasses(clPlayer, 40), its shore blended over 6 tiles, and sea
	// all round it so the home islands' waters join up for ships.
	const float clear = island_r + 40.0f;
	const float step = (half * 2.0f) / (float)(hm.res - 1);
	auto island_dist = [&](float wx, float wz) {
		float d = 1e9f;
		for (const auto &sp : spawns) d = std::min(d, det::hypot(wx - sp.x, wz - sp.z));
		return d;
	};
	for (int z = 0; z < hm.res; ++z) {
		const float wz = -half + z * step;
		for (int x = 0; x < hm.res; ++x) {
			const float wx = -half + x * step;
			const float r = det::hypot(wx, wz);
			const float t = smoothstep(clear, clear + shore, island_dist(wx, wz)) * (1.0f - smoothstep(half - 22.0f, half - 16.0f, r));
			if (t <= 0.0f) continue;
			const float cur = hm.get(x, z);
			hm.set(x, z, std::max(cur, cur + (6.0f * k - cur) * t));
		}
	}
	// Hills (ELEVATION_SET 6-20) and mountains (ELEVATION_MODIFY 15-29) on the
	// mainland: 6 tiles inside its shore and 40 clear of the home islands.
	auto on_mainland = [&](float wx, float wz, float r) {
		return island_dist(wx, wz) > clear + shore + r && det::hypot(wx, wz) < half - 22.0f - r &&
		       hm.sample_world(wx, wz) > 6.0f * k - 0.5f;
	};
	const int hills = rng.range_int(20, 34);
	for (int i = 0, tries = 0; i < hills && tries < hills * 30; ++tries) {
		const float hx = rng.range_float(-half, half), hz = rng.range_float(-half, half);
		const float r = rng.range_float(7.0f, 15.0f);
		if (!on_mainland(hx, hz, 0.0f)) continue;
		const float elev = (float)rng.range_int(6, 20);
		hm.level_area(hx, hz, std::max(1.0f, r - elev / 3.0f), elev / 3.0f, elev * k);
		++i;
	}
	const int mountains = rng.range_int(8, 17);
	for (int i = 0, tries = 0; i < mountains && tries < mountains * 30; ++tries) {
		const float mx = rng.range_float(-half, half), mz = rng.range_float(-half, half);
		const float r = rng.range_float(5.0f, 8.0f);
		if (!on_mainland(mx, mz, r)) continue;
		const float elev = (float)rng.range_int(15, 29);
		hm.raise_plateau(mx, mz, r, 0.0f, 0.0f, elev * k, elev / 3.0f);
		++i;
	}
	hm.blur(1, 0.35f);
	for (const auto &sp : spawns) hm.level_area(sp.x, sp.z, 14.0f, 4.0f, sp.base_height);
}

void RmsGenerator::synth_extinct_volcano(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// extinct_volcano.js: land at 1, each player on a hill of 25 on a circle at
	// f = 0.35, a volcano in the centre (rmgen createVolcano: 15, 25, 45 and
	// 62 high rings round a crater at 42), lakes at -4 and hills at 18. The
	// water starts at 0 and rises from minute 25 (extinct_volcano_triggers.js).
	const float k = kTriggerMapHeight;
	water_h = -NAV_WATER_MARGIN;
	hm.init(hm.res, hm.map_half, 1.0f * k);
	const float half = hm.map_half;
	const float player_r = (float)scale_by_map_size(15, 25, half); // defaultPlayerBaseRadius()
	place_team_spawns(hm, rng, spawns, num_players, 0.35f * 2.0f * half, rng.range_float(0.0f, 6.2831853f), 25.0f * k);
	// createMountain's omnidirectional ramp, then the flattened Civic Centre area. The ramp
	// is long enough for land units to climb (0.28's hill is a dome of bumps, walkable all
	// round), so the bases meet round the volcano, not only past the map's edge.
	const float ramp = std::max(player_r, 1.5f * 25.0f * k / (0.8f * LAND_MAX_TERRAIN_SLOPE));
	// The script's areas (AreaLayers in script_terrains.h), each the ground its level_area reaches,
	// recorded in its paint order: the bases' mountains (clPlayer), the volcano, lakes and hills.
	std::vector<uint8_t> mountains(hm.data.size(), 0);
	for (auto &sp : spawns) {
		sp.base_height = 25.0f * k;
		hm.level_area(sp.x, sp.z, 14.0f, ramp, 25.0f * k);
		mask_disk(hm, mountains, sp.x, sp.z, 14.0f + ramp);
	}
	record_area(hm, mountains, AREA_MOUNTAIN);
	// createVolcano(mapCenter, ..., ELEVATION_SET): each ring inside the last.
	const float rings[5][2] = {
		{(float)scale_by_map_size(18, 25, half), 15.0f}, {(float)scale_by_map_size(16, 23, half), 25.0f},
		{(float)scale_by_map_size(10, 15, half), 45.0f}, {(float)scale_by_map_size(8, 11, half), 62.0f},
		{(float)scale_by_map_size(4, 6, half), 42.0f}};
	for (int i = 0; i < 5; ++i)
		hm.level_area(0.0f, 0.0f, std::max(1.0f, rings[i][0] - (i == 4 ? 1.0f : 3.0f)), i == 4 ? 1.0f : 3.0f, rings[i][1] * k);
	const float volcano_r = rings[0][0];
	{
		// Every ring takes tHillVeryDark (no lavaTextures): one area, the outer ring's.
		std::vector<uint8_t> volcano(hm.data.size(), 0);
		mask_disk(hm, volcano, 0.0f, 0.0f, volcano_r);
		record_area(hm, volcano, AREA_VOLCANO);
	}
	auto clear_of = [&](float x, float z, float r, float gap) {
		for (const auto &sp : spawns)
			if (det::hypot(x - sp.x, z - sp.z) < player_r + 14.0f + r + gap) return false;
		return det::hypot(x, z) > volcano_r + r + gap && det::hypot(x, z) < half - 4.0f - r;
	};
	// Lakes: avoidClasses(clPlayer, 0, clHill, 2, clWater, 12).
	std::vector<std::pair<float, float>> lakes;
	const int num_lakes = (int)std::round((float)scale_by_map_size(6, 12, half));
	for (int i = 0, tries = 0; i < num_lakes && tries < num_lakes * 40; ++tries) {
		const float lx = rng.range_float(-half, half), lz = rng.range_float(-half, half);
		const float r = (float)scale_by_map_size(10, 14, half) * 0.6f;
		if (!clear_of(lx, lz, r, 2.0f)) continue;
		bool ok = true;
		for (const auto &l : lakes)
			if (det::hypot(lx - l.first, lz - l.second) < 2.0f * r + 12.0f) { ok = false; break; }
		if (!ok) continue;
		hm.level_area(lx, lz, r, 3.0f, -4.0f * k);
		std::vector<uint8_t> lake(hm.data.size(), 0);
		mask_disk(hm, lake, lx, lz, r + 3.0f);
		record_area(hm, lake, AREA_LAKE);
		lakes.push_back({lx, lz});
		++i;
	}
	// Bumps (createBumps, +scaleByMapSize(4, 10)) and hills of 18.
	const float bump = (float)scale_by_map_size(4, 10, half) * k;
	const int bumps = scale_by_map_size(50, 300, half) / 4;
	for (int i = 0, tries = 0; i < bumps && tries < bumps * 20; ++tries) {
		const float bx = rng.range_float(-half, half), bz = rng.range_float(-half, half);
		const float r = rng.range_float(2.0f, 5.0f);
		if (!clear_of(bx, bz, r, 0.0f) || hm.sample_world(bx, bz) < 0.0f) continue;
		hm.raise_plateau(bx, bz, r, 0.0f, 0.0f, bump, 2.0f);
		++i;
	}
	const int hills = scale_by_map_size(2, 8, half) * std::max(1, num_players);
	std::vector<std::pair<float, float>> placed;
	for (int i = 0, tries = 0; i < hills && tries < hills * 40; ++tries) {
		const float hx = rng.range_float(-half, half), hz = rng.range_float(-half, half);
		const float r = std::sqrt((float)rng.range_int(scale_by_map_size(20, 150, half) / 2, scale_by_map_size(20, 150, half)) / 3.1415927f);
		if (!clear_of(hx, hz, r, 2.0f) || hm.sample_world(hx, hz) < 0.0f) continue;
		bool ok = true;
		for (const auto &p : placed)
			if (det::hypot(hx - p.first, hz - p.second) < 2.0f * r + 15.0f) { ok = false; break; }
		if (!ok) continue;
		hm.level_area(hx, hz, r, 2.0f, 18.0f * k);
		std::vector<uint8_t> hill(hm.data.size(), 0);
		mask_disk(hm, hill, hx, hz, r + 2.0f);
		record_area(hm, hill, AREA_HILL);
		placed.push_back({hx, hz});
		++i;
	}
	for (const auto &sp : spawns) hm.level_area(sp.x, sp.z, 14.0f, 4.0f, sp.base_height);
}

// survivalofthefittest.js heights times this (0 A.D. metres to RTS0 random-map units).
constexpr float kSurvivalHeight = 0.35f;

void RmsGenerator::synth_survival_of_the_fittest(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// survivalofthefittest.js: everything starts as a plateau at heightHill (30); a central
	// area (0.15 of the map across) and a passage from it to the map edge between each pair
	// of neighbours are cut down to heightLand (3). Players sit on the plateau at 0.3, their
	// attackers come from 0.45 behind them; hills (55) stand on the rest of the plateau.
	const float k = kSurvivalHeight;
	water_h = -5.0f;
	hm.init(hm.res, hm.map_half, 30.0f * k);
	const float half = hm.map_half, size = 2.0f * half;
	const int n = std::max(1, num_players);
	hm.level_area(0.0f, 0.0f, 0.15f * size, 3.0f, 3.0f * k);
	const float start = rng.range_float(0.0f, 6.2831853f);
	place_ring_spawns(hm, spawns, num_players, 0.3f * size, start, 6.2831853f, 30.0f * k);
	// Passages: PathPlacer(mapCenter, passage[i], scaleByMapSize(14, 24)), smoothed over 4 tiles.
	const float width = (float)scale_by_map_size(14, 24, half);
	for (int i = 0; i < n; ++i) {
		const float a = start + 3.14159265f / (float)n + 6.2831853f * (float)i / (float)n;
		const float ex = det::cos(a) * 0.5f * size, ez = det::sin(a) * 0.5f * size;
		const float len = det::hypot(ex, ez);
		for (float t = 0.0f; t <= len; t += 2.0f)
			hm.level_area(ex * t / len, ez * t / len, width * 0.5f, 4.0f, 3.0f * k);
	}
	// The plateau's hills, clear of each base, its attacker point and the halfway point
	// (clPlayer) and of the low land.
	auto clear = [&](float x, float z, float r) {
		for (int i = 0; i < n; ++i) {
			const float a = start + 6.2831853f * (float)i / (float)n;
			for (float f : {0.3f, 0.375f, 0.45f})
				if (det::hypot(x - det::cos(a) * f * size, z - det::sin(a) * f * size) < 20.0f + r) return false;
		}
		return true;
	};
	const int hills = scale_by_map_size(3, 12, half) * n;
	for (int i = 0, tries = 0; i < hills && tries < hills * 40; ++tries) {
		const float x = rng.range_float(-half, half), z = rng.range_float(-half, half);
		const float r = rng.range_float(4.0f, 8.0f);
		if (det::hypot(x, z) > half - r - 2.0f || !clear(x, z, r) || hm.sample_world(x, z) < 30.0f * k - 0.5f) continue;
		bool flat = true;
		for (int d = 0; d < 8 && flat; ++d)
			flat = hm.sample_world(x + det::cos(0.785398f * d) * (r + 5.0f), z + det::sin(0.785398f * d) * (r + 5.0f)) >= 30.0f * k - 0.5f;
		if (!flat) continue;
		hm.raise_plateau(x, z, r, 0.0f, 0.0f, 25.0f * k, 3.0f);
		++i;
	}
	for (auto &sp : spawns) sp.base_height = 30.0f * k;
}

// rmgen-common player.js placeStartingEntities: the civ's StartEntities units (civs/*.json)
// round a civic centre at (cx, cz), entry j (the civic centre is entry 0) as a row of its
// Count, 2 tiles apart, 6 tiles out at BUILDING_ORIENTATION - PI * (1 - j / 2).
// `infantry_only` keeps only the templates naming "infantry" (survivalofthefittest.js).
// A unit whose spot is under water is pulled in towards the civic centre until it is dry.
int skirmish_replace(uint8_t civ, const char *map_template, uint16_t &out_tmpl) {
	out_tmpl = CIV_UNIT_TEMPLATE_NONE;
	if (std::strncmp(map_template, "units/", 6) != 0) return 0;
	const char *name = map_template + 6;
	const SkirmishReplacement *table = skirmish_replacement_table();
	for (int i = 0; i < SKIRMISH_REPLACEMENT_COUNT; ++i) {
		if (table[i].civ != civ || std::strcmp(table[i].from, name) != 0) continue;
		if (*table[i].to) out_tmpl = civ_unit_template_by_id(table[i].to);
		return out_tmpl != CIV_UNIT_TEMPLATE_NONE ? 1 : -1;
	}
	return 0;
}

static void place_start_entities(Simulation &sim, const HeightmapGrid &hm, uint8_t team, float cx, float cz,
                                 bool infantry_only = false, float kDist = 6.0f, int j = 0) {
	// `j` counts the entries placed before the first unit (the civic centre is entry 0).
	constexpr float kSpace = 2.0f, kOrientation = -0.25f * 3.14159265f;
	const uint8_t civ = sim.team_civ(team);
	const float dry_h = sim.water_height() + 0.6f;
	const StartEntity *table = start_entity_table();
	for (int e = 0; e < START_ENTITY_COUNT; ++e) {
		const StartEntity &se = table[e];
		if (se.civ != civ || (infantry_only && !std::strstr(se.tmpl, "infantry"))) continue;
		++j;
		uint16_t tmpl = CIV_UNIT_TEMPLATE_NONE;
		for (uint16_t t : civ_roster_ids(civ))
			if (std::strcmp(civ_unit_template(t).id, se.tmpl) == 0) { tmpl = t; break; }
		if (tmpl == CIV_UNIT_TEMPLATE_NONE) continue; // a template RTS0 has no roster row for yet
		const float angle = kOrientation - 3.14159265f * (1.0f - (float)j / 2.0f);
		const float ca = det::cos(angle), sa = det::sin(angle);
		for (int num = 0; num < se.count; ++num) {
			// location + Vector2D(dist, 0).rotate(-angle) + Vector2D(offset, 0).rotate(angle)
			const float off = kSpace * (-(float)num + (float)(se.count - 1) / 2.0f);
			float x = cx + kDist * ca + off * ca, z = cz + kDist * sa - off * sa;
			for (float t = 1.0f; t > 0.0f && hm.sample_world(x, z) < dry_h; t -= 0.1f) {
				x = cx + (x - cx) * 0.9f;
				z = cz + (z - cz) * 0.9f;
			}
			sim.spawn_unit(x, z, team, (UnitType)civ_unit_template(tmpl).unit_type, 0.0f, tmpl);
		}
	}
}

// rmgen-common placePlayersNomad (player.js:435-488): each player's StartEntities units as
// one group 1-3 tiles round a random dry, gentle spot clear of resources and structures, the
// groups kept scaleByMapSize(60, 240) tiles apart, relaxed to 1/2, 1/4 and then 0, in a
// shuffled player order (200 tries each). spawn_nomad_treasures adds the treasures by them.
static void place_nomad_units(Simulation &sim, const HeightmapGrid &hm, const std::vector<RmsGenerator::PlayerSpawn> &spawns, RmsRng &rng) {
	const float half = hm.map_half, lim = half - 6.0f; // MAP_BORDER_WIDTH 3 tiles and a margin
	const float dry_h = sim.water_height() + 0.6f, max_slope = 0.5f * LAND_MAX_TERRAIN_SLOPE;
	const float distance = 60.0f + 180.0f * std::clamp((2.0f * half - 128.0f) / 384.0f, 0.0f, 1.0f);
	const bool circle = sim.circular_map();
	// Tiles next to a resource or a structure (avoidClasses of the map's own constraints).
	const int side = (int)std::ceil(2.0f * half) + 1;
	std::vector<uint8_t> blocked((size_t)side * (size_t)side, 0);
	auto mark = [&](float x, float z, int r) {
		const int cx = (int)std::floor(x + half), cz = (int)std::floor(z + half);
		for (int dz = -r; dz <= r; ++dz)
			for (int dx = -r; dx <= r; ++dx) {
				const int tx = cx + dx, tz = cz + dz;
				if (tx >= 0 && tz >= 0 && tx < side && tz < side) blocked[(size_t)tz * (size_t)side + (size_t)tx] = 1;
			}
	};
	const auto rv = sim.resources();
	for (size_t r = 0; r < rv.count; ++r) mark(rv.x[r], rv.z[r], 1);
	for (const Simulation::Building &b : sim.buildings()) mark(b.x, b.z, 6);
	auto ok = [&](float x, float z) {
		if (circle ? x * x + z * z > lim * lim : (std::abs(x) > lim || std::abs(z) > lim)) return false;
		const int tx = (int)std::floor(x + half), tz = (int)std::floor(z + half);
		if (tx < 0 || tz < 0 || tx >= side || tz >= side || blocked[(size_t)tz * (size_t)side + (size_t)tx]) return false;
		if (hm.sample_world(x, z) < dry_h) return false;
		return std::abs(hm.sample_world(x + 0.5f, z) - hm.sample_world(x - 0.5f, z)) <= max_slope &&
		       std::abs(hm.sample_world(x, z + 0.5f) - hm.sample_world(x, z - 0.5f)) <= max_slope;
	};
	std::vector<size_t> order(spawns.size());
	for (size_t i = 0; i < order.size(); ++i) order[i] = i;
	for (size_t i = order.size(); i > 1; --i) std::swap(order[i - 1], order[(size_t)rng.range_int(0, (int)i - 1)]);
	std::vector<std::pair<float, float>> placed;
	const StartEntity *table = start_entity_table();
	for (size_t o : order) {
		const uint8_t team = spawns[o].team, civ = sim.team_civ(team);
		float gx = spawns[o].x, gz = spawns[o].z; // no spot at all: the planned base
		bool found = false;
		for (float factor : {1.0f, 0.5f, 0.25f, 0.0f}) {
			const float d2 = distance * factor * distance * factor;
			for (int t = 0; t < 200 && !found; ++t) {
				const float x = rng.range_float(-lim, lim), z = rng.range_float(-lim, lim);
				if (!ok(x, z)) continue;
				bool near = false;
				for (const auto &p : placed) near |= (p.first - x) * (p.first - x) + (p.second - z) * (p.second - z) < d2;
				if (near) continue;
				gx = x;
				gz = z;
				found = true;
			}
			if (found) break;
		}
		placed.push_back({gx, gz});
		for (int e = 0; e < START_ENTITY_COUNT; ++e) {
			if (table[e].civ != civ) continue;
			uint16_t tmpl = CIV_UNIT_TEMPLATE_NONE;
			for (uint16_t t : civ_roster_ids(civ))
				if (std::strcmp(civ_unit_template(t).id, table[e].tmpl) == 0) { tmpl = t; break; }
			if (tmpl == CIV_UNIT_TEMPLATE_NONE) continue;
			for (int num = 0; num < table[e].count; ++num) {
				float x = gx, z = gz; // SimpleObject(template, n, n, 1, 3)
				for (int t = 0; t < 20; ++t) {
					const float a = rng.range_float(0.0f, 6.2831853f), r = rng.range_float(1.0f, 3.0f);
					const float ux = gx + det::cos(a) * r, uz = gz + det::sin(a) * r;
					if (ok(ux, uz)) { x = ux; z = uz; break; }
				}
				sim.spawn_unit(x, z, team, (UnitType)civ_unit_template(tmpl).unit_type, 0.0f, tmpl);
			}
		}
	}
}

void RmsGenerator::place_survival_entities(Simulation &sim, const HeightmapGrid &hm, const std::vector<PlayerSpawn> &spawns, RmsRng &rng, const Config &config) {
	const float k = kSurvivalHeight;
	const float half = hm.map_half, size = 2.0f * half;
	const int n = (int)spawns.size();
	const float land_r = 0.15f * size;
	for (int i = 0; i < n; ++i) {
		const auto &sp = spawns[(size_t)i];
		const uint8_t team = sp.team;
		const float a = det::atan2(sp.z, sp.x);
		// getStartingEntities filtered to the civic centre and the infantry.
		sim.spawn_building_world(sp.x, sp.z, BLD_TOWN_CENTER, team, 1.0f);
		place_start_entities(sim, hm, team, sp.x, sp.z, true);
		// The treasure seeker: findLocationInDirectionBasedOnHeight(player, centre, -3, 3.5, 3),
		// the first spot towards the centre down at the low land, then 3 tiles on.
		float wx = sp.x, wz = sp.z;
		const float d = det::hypot(sp.x, sp.z);
		for (float t = 0.0f; t < d; t += 0.5f) {
			const float x = sp.x - det::cos(a) * t, z = sp.z - det::sin(a) * t;
			if (hm.sample_world(x, z) <= 3.5f * k) {
				wx = x - det::cos(a) * 3.0f;
				wz = z - det::sin(a) * 3.0f;
				break;
			}
		}
		sim.spawn_unit(wx, wz, team, UNIT_FEMALE_CITIZEN);
		// The attacker spawn point, owned by the player.
		sim.add_trigger_point('A', det::cos(a) * 0.45f * size, det::sin(a) * 0.45f * size, team);
	}
	// Treasure points B, C and D: scaleByMapSize(40, 140) each in the central area,
	// 5 tiles inside its edge.
	const int count = scale_by_map_size(40, 140, half);
	for (uint8_t ref : {(uint8_t)'B', (uint8_t)'C', (uint8_t)'D'})
		for (int i = 0, tries = 0; i < count && tries < count * 100; ++tries) {
			const float r = (land_r - 5.0f) * std::sqrt(rng.next_float()), t = rng.range_float(0.0f, 6.2831853f);
			const float x = det::cos(t) * r, z = det::sin(t) * r;
			if (hm.sample_world(x, z) > 3.0f * k + 0.5f) continue;
			sim.add_trigger_point(ref, x, z);
			++i;
		}
	// Forests and straggler trees in the central area (stayClasses(clLand, 4)), clear of
	// the treasure seekers.
	auto near_women = [&](float x, float z) {
		for (const auto &sp : spawns) {
			const float a = det::atan2(sp.z, sp.x);
			if (det::hypot(x - det::cos(a) * land_r, z - det::sin(a) * land_r) < 8.0f) return true;
		}
		return false;
	};
	const int forests = scale_by_map_size(3, 10, half);
	for (int f = 0; f < forests; ++f) {
		const float r = (land_r - 8.0f) * std::sqrt(rng.next_float()), t = rng.range_float(0.0f, 6.2831853f);
		const float cx = det::cos(t) * r, cz = det::sin(t) * r;
		if (near_women(cx, cz)) continue;
		const int trees = rng.range_int(4, 8);
		for (int j = 0; j < trees; ++j)
			{ const float b_ = cz + rng.range_float(-3.0f, 3.0f); const float a_ = cx + rng.range_float(-3.0f, 3.0f); sim.spawn_resource_world(a_, b_, RES_WOOD, 800); }
	}
	(void)config;
}

// Map-specific entities of the trigger maps.
void RmsGenerator::place_trigger_map_entities(Simulation &sim, const HeightmapGrid &hm, float water_h, const std::vector<PlayerSpawn> &spawns, RmsRng &rng, const Config &config) {
	const float half = hm.map_half;
	if (config.map_type == MAP_FLOOD) {
		// flood.js: large metal and stone mines (3-6 of each) near the centre of the mainland.
		for (uint8_t res : {RES_GOLD, RES_STONE}) {
			const int n = rng.range_int(3, 6);
			for (int i = 0, tries = 0; i < n && tries < 200; ++tries) {
				const float a = rng.range_float(0.0f, 6.2831853f);
				const float d = rng.range_float(0.0f, 0.25f * 2.0f * half);
				const float x = det::cos(a) * d, z = det::sin(a) * d;
				if (hm.sample_world(x, z) < 6.0f * kTriggerMapHeight - 0.5f || hm.compute_slope_world(x, z) > 0.7f) continue;
				sim.spawn_resource_world(x, z, res, LARGE_MINE_SUPPLY);
				++i;
			}
		}
	} else if (config.map_type == MAP_EXTINCT_VOLCANO) {
		// extinct_volcano.js "Creating towers": Gaia palisade forts on the
		// lowland bumps, 60 tiles apart and 10 clear of the players. The
		// trigger script garrisons them (GarrisonWoodenTowers).
		std::vector<std::pair<float, float>> towers;
		for (int tries = 0; tries < 500; ++tries) {
			const float x = rng.range_float(-half * 0.92f, half * 0.92f), z = rng.range_float(-half * 0.92f, half * 0.92f);
			if (det::hypot(x, z) > half * 0.92f) continue;
			const float h = hm.sample_world(x, z);
			if (h < water_h + 1.0f * kTriggerMapHeight + 0.5f || h > 18.0f * kTriggerMapHeight - 1.0f) continue;
			if (hm.compute_slope_world(x, z) > 0.6f) continue;
			bool ok = true;
			for (const auto &sp : spawns)
				if (det::hypot(x - sp.x, z - sp.z) < (float)scale_by_map_size(15, 25, half) + 24.0f) { ok = false; break; }
			for (const auto &t : towers)
				if (det::hypot(x - t.first, z - t.second) < 60.0f) { ok = false; break; }
			if (!ok) continue;
			// structures/palisades_fort: a template_structure_defensive_tower_sentry (3 slots).
			sim.set_structure_uncapturable(sim.spawn_building_world(x, z, BLD_SENTRY_TOWER, GAIA_PLAYER, 1.0f)); // <Capturable disable=""/>
			towers.push_back({x, z});
		}
	} else if (config.map_type == MAP_POLAR_SEA) {
		// polar_sea.js "Create trigger points where wolves spawn": trigger_point_A wherever it
		// fits, 2 tiles from water, 4 from mines, 15 from the player bases, 2 from hills and 20
		// from each other (createObjectGroupsDeprecated, 1000 x 100 tries). polar_sea_triggers.js
		// spawns its wolf waves there.
		std::vector<std::pair<float, float>> mines, points;
		const auto res = sim.resources();
		for (size_t i = 0; i < res.count; ++i)
			if (res.alive[i] && (res.type[i] == RES_GOLD || res.type[i] == RES_STONE)) mines.push_back({res.x[i], res.z[i]});
		const float base = (float)scale_by_map_size(15, 25, half) + 15.0f;
		for (int tries = 0; tries < 20000; ++tries) {
			const float x = rng.range_float(-half * 0.95f, half * 0.95f), z = rng.range_float(-half * 0.95f, half * 0.95f);
			// A circular map (g_Map.randomCoordinate): the corners outside the circle are off the map.
			bool ok = det::hypot(x, z) <= half * 0.95f && hm.compute_slope_world(x, z) <= 0.6f;
			for (int k = 0; ok && k < 9; ++k) {
				const float a = 0.7853982f * (float)k, r = k == 8 ? 0.0f : 2.0f;
				ok = hm.sample_world(x + det::cos(a) * r, z + det::sin(a) * r) > water_h;
			}
			for (size_t k = 0; ok && k < spawns.size(); ++k) ok = det::hypot(x - spawns[k].x, z - spawns[k].z) >= base;
			for (size_t k = 0; ok && k < mines.size(); ++k) ok = det::hypot(x - mines[k].first, z - mines[k].second) >= 4.0f;
			for (size_t k = 0; ok && k < points.size(); ++k) ok = det::hypot(x - points[k].first, z - points[k].second) >= 20.0f;
			if (!ok) continue;
			sim.add_trigger_point('A', x, z);
			points.push_back({x, z});
		}
	} else if (config.map_type == MAP_ELEPHANTINE) {
		place_elephantine_entities(sim, hm, water_h, spawns, rng);
		place_elephantine_fish(sim, hm, water_h, config);
	}
}

// SimpleObject(template, minCount, maxCount, minDistance, maxDistance) of an animal.
struct ScriptAnimal {
	uint8_t fauna;
	float min_n, max_n, min_d, max_d;
};

// rmgen createObjectGroups(new SimpleGroup(objs, true, clFood), 0, constraint, amount,
// retryFactor) (createObjectGroupsByAreas when centre() picks inside an area): each try
// centres the group on a tile from centre(); each SimpleObject draws its count
// (randIntInclusive) and puts every animal min_d-max_d from the tile's centre, 1 from the
// group's others, on a tile allows() takes; 30 failed spots and the group is dropped
// (retryPlacing counts it bad; `deprecated` makes exactly `amount` tries, as
// createObjectGroupsDeprecated). Placed animals join `food` (clFood), which allows() may read.
static int place_animal_groups(Simulation &sim, RmsRng &rng, const std::vector<ScriptAnimal> &objs, float amount, int retry_factor,
		const std::function<void(float &, float &)> &centre, const std::function<bool(float, float)> &allows,
		std::vector<std::pair<float, float>> &food, bool deprecated = false) {
	struct Spec { float x, z; uint8_t fauna; };
	std::vector<Spec> specs;
	int placed = 0, bad = 0;
	const float max_fail = amount * (float)retry_factor;
	while ((float)(deprecated ? placed + bad : placed) < amount && (float)bad <= max_fail) {
		float cx, cz;
		centre(cx, cz);
		specs.clear();
		bool ok = true;
		for (const ScriptAnimal &o : objs) {
			const int count = (int)std::floor(o.min_n + rng.next_float() * (o.max_n + 1.0f - o.min_n));
			int retries = 0;
			for (int i = 0; i < count && ok; ++i)
				for (;;) {
					// One draw per statement: compilers order a call's arguments differently.
					const float dist = rng.range_float(o.min_d, o.max_d);
					const float ang = rng.range_float(0.0f, 6.2831853f);
					const float x = cx + 0.5f + det::cos(ang) * dist, z = cz + 0.5f + det::sin(ang) * dist;
					bool fits = true;
					for (const Spec &sp : specs) fits = fits && (sp.x - x) * (sp.x - x) + (sp.z - z) * (sp.z - z) >= 1.0f;
					if (fits && allows(std::floor(x) + 0.5f, std::floor(z) + 0.5f)) {
						specs.push_back({x, z, o.fauna});
						break;
					}
					if (retries++ > 30) { ok = false; break; }
				}
			if (!ok) break;
		}
		if (!ok) { ++bad; continue; }
		++placed;
		for (const Spec &sp : specs) {
			sim.spawn_fauna(sp.x, sp.z, sp.fauna);
			food.push_back({sp.x, sp.z});
		}
	}
	return placed;
}

void RmsGenerator::place_elephantine_fish(Simulation &sim, const HeightmapGrid &hm, float water_h, const Config &config) {
	// elephantine.js (0.28) "Creating fish": scaleByMapSize(125, 200) groups of 1-2 tilapia (0-1
	// tiles apart), stayClasses(clWater, 2) and avoidClasses(clFood, 12), retry factor 50. clFood
	// holds the berries, the animals and the fish placed so far. Its own stream keeps every other
	// placement on the map unchanged.
	const float half = hm.map_half;
	const int groups = scale_by_map_size(125, 200, half);
	RmsRng rng(config.seed ^ 0x54494C41u); // 'TILA'
	auto deep = [&](float x, float z) { // a water tile with water 2 tiles round it
		if (x < -half + 2.0f || x > half - 2.0f || z < -half + 2.0f || z > half - 2.0f) return false;
		if (sim.past_map_edge(x, z)) return false; // as distribute_fish
		for (int dz = -2; dz <= 2; ++dz)
			for (int dx = -2; dx <= 2; ++dx)
				if (hm.sample_world(x + (float)dx, z + (float)dz) >= water_h) return false;
		return true;
	};
	std::vector<std::pair<float, float>> food;
	const auto res = sim.resources();
	for (size_t i = 0; i < res.count; ++i)
		if (res.alive[i] && res.type[i] == RES_FOOD) food.push_back({res.x[i], res.z[i]});
	const auto fauna = sim.fauna();
	for (size_t i = 0; i < fauna.count; ++i)
		if (fauna.alive[i]) food.push_back({fauna.pos_x[i], fauna.pos_z[i]});
	int placed = 0;
	for (int attempt = 0; attempt < groups * 50 && placed < groups; ++attempt) {
		const float x = std::floor(rng.range_float(-half, half)) + 0.5f, z = std::floor(rng.range_float(-half, half)) + 0.5f;
		if (!deep(x, z)) continue;
		bool crowded = false;
		for (const auto &[fx, fz] : food)
			if ((fx - x) * (fx - x) + (fz - z) * (fz - z) < 12.0f * 12.0f) { crowded = true; break; }
		if (crowded) continue;
		++placed;
		const int n = rng.range_int(1, 2);
		for (int k = 0; k < n; ++k) {
			const float a = rng.range_float(0.0f, 6.2831853f), d = k ? rng.range_float(0.0f, 1.0f) : 0.0f;
			sim.spawn_resource_world(x + det::cos(a) * d, z + det::sin(a) * d, RES_FOOD, FISH_FOOD_SUPPLY, RES_SUB_FISH);
		}
		food.push_back({x, z});
	}
}

void RmsGenerator::place_elephantine_entities(Simulation &sim, const HeightmapGrid &hm, float water_h, const std::vector<PlayerSpawn> &spawns, RmsRng &rng) {
	// elephantine.js: the island between the banks (clIsland, the river band 0.27 of the map
	// wide at the edges and 0.35 at the centre, turned by riverAngle) holds a Gaia temple (a
	// wonder from 320 tiles), a large pyramid, Kushite towers near them, treasures in their
	// city patches, a Kushite hero and soldiers; elephantine_triggers.js garrisons the towers
	// and temples. Left out: the statues and city-patch paths (actors and terrain only).
	const float half = hm.map_half, size = 2.0f * half;
	auto scale_f = [&](float lo, float hi) { return lo + (hi - lo) * std::max(0.0f, size - 128.0f) / 384.0f; };
	const float angle = 0.22f * 3.14159265f, c = det::cos(angle), sn = det::sin(angle);
	// synth_elephantine's frame: x across the river, z along it (rmgen's z mirrored).
	auto across = [&](float x, float z) { return x * c + z * sn; };
	auto along = [&](float x, float z) { return x * sn - z * c; };
	auto band = [&](float x, float z) {
		const float t = std::min(1.0f, std::fabs(along(x, z)) / half);
		return (0.35f + (0.27f - 0.35f) * t) * size * 0.5f;
	};
	auto land = [&](float x, float z) { return std::fabs(across(x, z)) <= band(x, z) && hm.sample_world(x, z) > water_h + 0.25f; };
	// stayClasses(clIsland, d): the island all round, d tiles in.
	auto inside = [&](float x, float z, float d) {
		if (!land(x, z) || std::fabs(across(x, z)) > band(x, z) - d) return false;
		for (int k = 0; k < 8; ++k) {
			const float a = 0.7853982f * (float)k;
			for (float r : {d * 0.5f, d})
				if (!land(x + det::cos(a) * r, z + det::sin(a) * r)) return false;
		}
		return true;
	};
	auto far_from_players = [&](float x, float z, float d) {
		for (const auto &sp : spawns)
			if (det::hypot(x - sp.x, z - sp.z) < d) return false;
		return true;
	};
	auto random_island_spot = [&](float &x, float &z) {
		const float t = rng.range_float(-half * 0.95f, half * 0.95f), w = rng.range_float(-0.175f * size, 0.175f * size);
		x = w * c + t * sn;
		z = w * sn - t * c;
	};
	struct Placed { float x, z, r; };
	std::vector<Placed> temples, towers, units;
	auto clear_of = [](const std::vector<Placed> &v, float x, float z, float d) {
		for (const Placed &p : v)
			if (det::hypot(x - p.x, z - p.z) < d + p.r) return false;
		return true;
	};
	// Distance from the nearest temple-class footprint (NearTileClassConstraint(clTemple, d)).
	auto near_temple = [&](float x, float z, float d) {
		for (const Placed &p : temples)
			if (det::hypot(x - p.x, z - p.z) <= d + p.r) return true;
		return false;
	};
	auto spawn = [&](float x, float z, uint8_t type, std::vector<Placed> &into) {
		const uint32_t id = sim.spawn_building_world(x, z, type, GAIA_PLAYER, 1.0f);
		if (!id) return 0u;
		// structures/kush/* and structures/ptol/wonder: their template's civ.
		sim.set_building_civ(id, type == BLD_WONDER ? CIV_PTOLEMIES : CIV_KUSHITES);
		for (const auto &b : sim.buildings())
			if (b.id == id) { into.push_back({b.x, b.z, b.half_size}); break; }
		return id;
	};
	const float stay = scale_f(10.0f, 20.0f);
	// "Creating temple": a wonder on 320-tile maps and larger, else one of the two temples.
	const uint8_t temple = size >= 320.0f ? BLD_WONDER : (rng.next_float() < 0.5f ? BLD_TEMPLE_AMUN : BLD_TEMPLE);
	for (int tries = 0; tries < 200 && temples.empty(); ++tries) {
		float x, z;
		random_island_spot(x, z);
		if (inside(x, z, stay) && far_from_players(x, z, 40.0f)) spawn(x, z, temple, temples);
	}
	// "Creating pyramid": scaleByMapSize(20, 50) from the temple.
	const float apart = scale_f(20.0f, 50.0f);
	for (int tries = 0, placed = 0; tries < 200 && !placed; ++tries) {
		float x, z;
		random_island_spot(x, z);
		if (!inside(x, z, stay) || !clear_of(temples, x, z, apart) || !far_from_players(x, z, 40.0f)) continue;
		placed = spawn(x, z, BLD_PYRAMID_LARGE, temples) != 0;
	}
	if (temples.empty()) return;
	// "Creating kushite towers": scaleByMapSize(4, 12), two sentry towers to one defence tower,
	// uncapturable, within 25 of a temple, 12 apart and 30 from the bases.
	const int tower_count = scale_by_map_size(4, 12, half);
	for (int tries = 0, placed = 0; tries < tower_count * 200 && placed < tower_count; ++tries) {
		float x, z;
		random_island_spot(x, z);
		if (!inside(x, z, scale_f(4.0f, 8.0f)) || !near_temple(x, z, 25.0f) || !clear_of(temples, x, z, 11.0f) ||
				!clear_of(towers, x, z, 12.0f) || !far_from_players(x, z, 30.0f + 20.0f)) continue;
		const uint8_t type = rng.range_int(0, 2) < 2 ? BLD_SENTRY_TOWER : BLD_TOWER;
		if (const uint32_t id = spawn(x, z, type, towers)) {
			sim.set_structure_uncapturable(id);
			++placed;
		}
	}
	// "Creating treasure": scaleByMapSize(4, 10) groups of 1-2 in the city patches (10 round
	// the temple, 6 round the pyramid).
	static const uint8_t kTreasures[] = {TREASURE_FOOD_BARREL, TREASURE_FOOD_BIN, TREASURE_WOOD, TREASURE_METAL, TREASURE_STONE};
	const int groups = scale_by_map_size(4, 10, half);
	for (int g = 0, tries = 0; g < groups && tries < groups * 100; ++tries) {
		const Placed &patch = temples[(size_t)rng.range_int(0, (int)temples.size() - 1)];
		const float radius = &patch == &temples[0] ? 10.0f : 6.0f;
		const float r = (patch.r + radius) * std::sqrt(rng.next_float()), a = rng.range_float(0.0f, 6.2831853f);
		const float x = patch.x + det::cos(a) * r, z = patch.z + det::sin(a) * r;
		if (!land(x, z) || !clear_of(temples, x, z, 1.0f) || !clear_of(towers, x, z, 1.0f)) continue;
		const int n = rng.range_int(1, 2);
		for (int i = 0; i < n; ++i)
			sim.spawn_treasure(x + (float)i, z, kTreasures[rng.range_int(0, 4)]);
		++g;
	}
	// The Kushite hero (within 14 of a temple) and scaleByMapSize(12, 60) soldiers, healers and
	// women (within 20), each from the units/kush/ templates (FindTemplates), every rank.
	std::vector<const char *> heroes;
	std::vector<std::pair<const char *, int>> people;
	for (uint16_t t : civ_roster_ids(CIV_KUSHITES)) {
		const CivUnitTemplate &u = civ_unit_template(t);
		if (std::strstr(u.id, "rts0_")) continue;
		if (u.tech_classes & TC_HERO) {
			heroes.push_back(u.id);
			continue;
		}
		if (!(u.tech_classes & (TC_SOLDIER | TC_HEALER | TC_FEMALECITIZEN)) || (u.tech_classes & (TC_SHIP | TC_SIEGE))) continue;
		const size_t len = std::strlen(u.id);
		const bool ranked = len > 2 && std::strcmp(u.id + len - 2, "_b") == 0;
		for (int rank = 1; rank <= (ranked ? 3 : 1); ++rank) people.push_back({u.id, rank});
	}
	auto place_unit = [&](const char *id, int rank, float stay_d, float near_d) {
		for (int tries = 0; tries < 500; ++tries) {
			float x, z;
			random_island_spot(x, z);
			if (!inside(x, z, stay_d) || !near_temple(x, z, near_d) || !clear_of(temples, x, z, 11.0f) ||
					!clear_of(towers, x, z, 2.0f) || !clear_of(units, x, z, 3.0f)) continue;
			const CivUnitTemplate &u = civ_unit_template(civ_unit_template_by_id(id));
			if (sim.spawn_gaia_unit(x, z, (UnitType)u.unit_type, id, rank)) units.push_back({x, z, 0.0f});
			return;
		}
	};
	const float unit_stay = scale_f(2.0f, 24.0f);
	if (!heroes.empty()) place_unit(heroes[(size_t)rng.range_int(0, (int)heroes.size() - 1)], 1, unit_stay, 14.0f);
	const int soldiers = scale_by_map_size(12, 60, half);
	for (int i = 0; i < soldiers && !people.empty(); ++i) {
		const auto &p = people[(size_t)rng.range_int(0, (int)people.size() - 1)];
		place_unit(p.first, p.second, unit_stay, 20.0f);
	}

	// The hunt (elephantine.js "Creating rhinos" to "Creating crocodiles"): createObjectGroups
	// of scaleByMapSize(2, 10) groups each, retry factor 50, under avoidCollisions
	// (avoidClasses(clPlayer, 15, clWater, 1, clForest, 1, clRock, 4, clMetal, 4, clFood, 6,
	// clTemple, 11, clCliff, 0, clSoldier, 3, clTower, 2, clTreasure, 1); clPlayer is each
	// base's disk of defaultPlayerBaseRadius). Gazelles also keep off the island; crocodiles
	// stay within 3 of the river. No lions for Nomad. The statues and paths are left out.
	{
		RmsRng hunt(rng.next_u32());
		const float base_r = scale_f(15.0f, 25.0f) + 15.0f;
		std::vector<std::pair<float, float>> food, wood, mines;
		const auto res = sim.resources();
		for (size_t i = 0; i < res.count; ++i) {
			if (!res.alive[i]) continue;
			if (res.type[i] == RES_FOOD && res.subtype[i] != RES_SUB_FISH) food.push_back({res.x[i], res.z[i]});
			else if (res.type[i] == RES_WOOD) wood.push_back({res.x[i], res.z[i]});
			else if (res.type[i] == RES_GOLD || res.type[i] == RES_STONE) mines.push_back({res.x[i], res.z[i]});
		}
		const auto fv = sim.fauna();
		for (size_t i = 0; i < fv.count; ++i)
			if (fv.alive[i]) food.push_back({fv.pos_x[i], fv.pos_z[i]});
		auto near_any = [](const std::vector<std::pair<float, float>> &v, float x, float z, float d) {
			for (const auto &[px, pz] : v)
				if ((px - x) * (px - x) + (pz - z) * (pz - z) <= d * d) return true;
			return false;
		};
		auto wet_within = [&](float x, float z, int r) {
			for (int dz = -r; dz <= r; ++dz)
				for (int dx = -r; dx <= r; ++dx)
					if (dx * dx + dz * dz <= r * r && hm.sample_world(x + (float)dx, z + (float)dz) <= water_h) return true;
			return false;
		};
		auto island_within = [&](float x, float z, float r) {
			if (land(x, z)) return true;
			for (int k = 0; k < 8; ++k) {
				const float a = 0.7853982f * (float)k;
				if (land(x + det::cos(a) * r, z + det::sin(a) * r)) return true;
			}
			return false;
		};
		auto collisions = [&](float x, float z) {
			if (std::fabs(x) > half - 3.0f || std::fabs(z) > half - 3.0f) return false; // validTilePassable
			if (!far_from_players(x, z, base_r) || wet_within(x, z, 1) || hm.compute_slope_world(x, z) > 0.8f) return false;
			if (near_any(wood, x, z, 1.5f) || near_any(mines, x, z, 4.0f) || near_any(food, x, z, 6.0f)) return false;
			if (!clear_of(temples, x, z, 11.0f) || !clear_of(units, x, z, 3.0f) || !clear_of(towers, x, z, 2.0f)) return false;
			for (const Simulation::Treasure &t : sim.treasures())
				if ((t.x - x) * (t.x - x) + (t.z - z) * (t.z - z) <= 1.0f) return false;
			return true;
		};
		auto centre = [&](float &x, float &z) { // g_Map.randomCoordinate(true)
			x = std::floor(hunt.range_float(-half + 3.0f, half - 3.0f));
			z = std::floor(hunt.range_float(-half + 3.0f, half - 3.0f));
		};
		const float n = scale_f(2.0f, 10.0f);
		place_animal_groups(sim, hunt, {{FAUNA_RHINOCEROS_WHITE, 1, 1, 0, 1}}, n, 50, centre, collisions, food);
		place_animal_groups(sim, hunt, {{FAUNA_BOAR, 1, 1, 0, 1}}, n, 50, centre, collisions, food); // oWarthog
		place_animal_groups(sim, hunt, {{FAUNA_GAZELLE, 5, 7, 2, 4}}, n, 50, centre,
				[&](float x, float z) { return collisions(x, z) && !island_within(x, z, 1.0f); }, food);
		place_animal_groups(sim, hunt, {{FAUNA_GIRAFFE, 2, 3, 2, 4}, {FAUNA_GIRAFFE_INFANT, 2, 3, 2, 4}}, n, 50, centre, collisions, food);
		if (!sim.nomad())
			place_animal_groups(sim, hunt, {{FAUNA_LION, 1, 2, 2, 4}, {FAUNA_LIONESS, 2, 3, 2, 4}}, n, 50, centre, collisions, food);
		place_animal_groups(sim, hunt, {{FAUNA_ELEPHANT_AFRICAN_BUSH, 2, 3, 2, 4}, {FAUNA_ELEPHANT_AFRICAN_INFANT, 2, 3, 2, 4}}, n, 50,
				centre, collisions, food);
		place_animal_groups(sim, hunt, {{FAUNA_CROCODILE_NILE, 2, 3, 3, 5}}, n, 50, centre,
				[&](float x, float z) { return collisions(x, z) && wet_within(x, z, 3); }, food);
	}
}

void RmsGenerator::place_lower_nubia_gaia(Simulation &sim, const HeightmapGrid &hm, float water_h, const std::vector<PlayerSpawn> &spawns, RmsRng &rng) {
	// lower_nubia.js (0.28) from "Creating stone mines" to "Creating straggler trees and bushes". Its tile
	// classes: clWater is the Nile (ground at heightScale(-3) or lower, synth_lower_nubia's sea ground), clCliff
	// the slopes of 2 or more, clPlayer each city patch (a third of the base radius round the civic centre),
	// clBaseResource the starting resources. Groups are tested at their centre tile; avoidClasses(c, d) wants no
	// tile of c within d.
	const float half = hm.map_half, size = 2.0f * half;
	const size_t N = hm.data.size();
	const float sp = grid_spacing(hm);
	const float sea = metres(water_h, -3.0f * size / 320.0f) + 1e-3f;
	std::vector<uint8_t> nile(N), cliff(N);
	const auto slope = slopes_m(hm);
	for (size_t i = 0; i < N; ++i) {
		nile[i] = hm.data[i] <= sea;
		cliff[i] = slope[i] >= 2.0f;
	}
	const auto to_nile = distance_to_mask(nile, hm.res, sp), to_cliff = distance_to_mask(cliff, hm.res, sp);
	auto vertex = [&](float x, float z) {
		float gx, gz;
		hm.world_to_grid(x, z, gx, gz);
		const int ix = std::clamp((int)std::lround(gx), 0, hm.res - 1), iz = std::clamp((int)std::lround(gz), 0, hm.res - 1);
		return (size_t)iz * (size_t)hm.res + (size_t)ix;
	};
	const float patch = (float)scale_by_map_size(15, 25, half) / 3.0f;
	auto to_bases = [&](float x, float z) {
		float d = 1e30f;
		for (const auto &s : spawns) d = std::min(d, det::hypot(x - s.x, z - s.z));
		return d - patch;
	};
	// Tile classes on a one-tile grid.
	enum : uint8_t { kBase = 1, kRock = 2, kMetal = 4, kForest = 8, kPyramid = 16 };
	const int T = std::max(1, (int)std::ceil(size));
	std::vector<uint8_t> cls((size_t)T * (size_t)T, 0);
	auto mark = [&](float x, float z, float r, uint8_t c) {
		const int cx = (int)std::floor(x + half), cz = (int)std::floor(z + half), rr = (int)std::ceil(r);
		for (int dz = -rr; dz <= rr; ++dz)
			for (int dx = -rr; dx <= rr; ++dx) {
				const int tx = cx + dx, tz = cz + dz;
				if (tx < 0 || tz < 0 || tx >= T || tz >= T || (float)(dx * dx + dz * dz) > r * r) continue;
				cls[(size_t)tz * (size_t)T + (size_t)tx] |= c;
			}
	};
	auto clear = [&](float x, float z, float d, uint8_t c) {
		const int cx = (int)std::floor(x + half), cz = (int)std::floor(z + half), rr = (int)std::ceil(d);
		for (int dz = -rr; dz <= rr; ++dz)
			for (int dx = -rr; dx <= rr; ++dx) {
				const int tx = cx + dx, tz = cz + dz;
				if (tx < 0 || tz < 0 || tx >= T || tz >= T || (float)(dx * dx + dz * dz) > d * d) continue;
				if (cls[(size_t)tz * (size_t)T + (size_t)tx] & c) return false;
			}
		return true;
	};
	// The starting resources placed so far (clBaseResource).
	const auto res = sim.resources();
	for (size_t i = 0; i < res.count; ++i)
		if (res.alive[i]) mark(res.x[i], res.z[i], 0.0f, kBase);
	auto random_spot = [&](float &x, float &z) { // a tile of the circular map
		do {
			x = rng.range_float(-half, half);
			z = rng.range_float(-half, half);
		} while (x * x + z * z > (half - 1.0f) * (half - 1.0f));
	};

	// "Creating stone mines" / "Creating metal mines": createMines places scaleByMapSize(10, 30) of each of its
	// two groups (70 tries a group): a large mine (savanna_large, 5000) with 0-2 small ones (desert_small, 1000)
	// within 4, or 3-6 small ones 1-3 from the centre. Both 4 from the Nile and cliffs and 20 from the city
	// patches; stone 10 from other stone, metal 10 from other metal and 5 from stone.
	const int mines = scale_by_map_size(10, 30, half);
	for (int kind = 0; kind < 2; ++kind) {
		const uint8_t type = kind == 0 ? RES_STONE : RES_GOLD, own = kind == 0 ? kRock : kMetal;
		for (int group = 0; group < 2; ++group)
			for (int g = 0, tries = 0; g < mines && tries < mines * 70; ++tries) {
				float x, z;
				random_spot(x, z);
				const size_t v = vertex(x, z);
				if (to_nile[v] <= 4.0f || to_cliff[v] <= 4.0f || to_bases(x, z) <= 20.0f || !clear(x, z, 10.0f, own)) continue;
				if (kind == 1 && !clear(x, z, 5.0f, kRock)) continue;
				const int small = group == 0 ? rng.range_int(0, 2) : rng.range_int(3, 6);
				const float lo = group == 0 ? 2.5f : 1.0f, hi = group == 0 ? 4.0f : 3.0f;
				if (group == 0) {
					sim.spawn_resource_world(x, z, type, LARGE_MINE_SUPPLY);
					mark(x, z, 1.0f, own);
				}
				for (int k = 0; k < small; ++k) {
					const float a = rng.range_float(0.0f, 6.2831853f), r = rng.range_float(lo, hi);
					const float rx = x + det::cos(a) * r, rz = z + det::sin(a) * r;
					if (nile[vertex(rx, rz)]) continue;
					sim.spawn_resource_world(rx, rz, type, MINE_SUPPLY);
					mark(rx, rz, 0.0f, own);
				}
				++g;
			}
	}

	// "Creating pyramid": one Gaia structures/kush/pyramid_large within 10 of the Nile but 6 off it, 6 from cliffs
	// and the mines and 40 from the city patches (500 tries).
	for (int tries = 0; tries < 500; ++tries) {
		float x, z;
		random_spot(x, z);
		const size_t v = vertex(x, z);
		if (to_nile[v] > 10.0f || to_nile[v] <= 6.0f || to_cliff[v] <= 6.0f || to_bases(x, z) <= 40.0f ||
				!clear(x, z, 6.0f, kRock | kMetal)) continue;
		const uint32_t id = sim.spawn_building_world(x, z, BLD_PYRAMID_LARGE, GAIA_PLAYER, 1.0f);
		if (!id) continue;
		sim.set_building_civ(id, CIV_KUSHITES); // structures/kush/pyramid_large
		for (const auto &b : sim.buildings())
			if (b.id == id) { mark(b.x, b.z, b.half_size, kPyramid); break; }
		break;
	}

	auto free_of = [&](float x, float z, float forest) {
		return clear(x, z, forest, kForest) && clear(x, z, 5.0f, kBase) && clear(x, z, 4.0f, kRock | kMetal) && clear(x, z, 6.0f, kPyramid);
	};
	// "Creating trees near the Nile": scaleByMapSize(100, 1000) groups of 1-2 oPalms (1 apart) within
	// scaleByMapSize(1, 8) of the Nile on ground at heightNileForests (heightScale(15)) or higher, off the Nile
	// and cliffs, 1 from other trees, 12 from the city patches, 5 from the base resources, 4 from the mines and 6
	// from the pyramid (200 tries a group). Tries are drawn from the tiles the height and Nile tests allow.
	const float near_nile = 1.0f + 7.0f * std::max(0.0f, size - 128.0f) / 384.0f;
	const float forest_h = metres(water_h, 15.0f * size / 320.0f);
	std::vector<size_t> bank;
	for (size_t i = 0; i < N; ++i)
		if (!nile[i] && !cliff[i] && to_nile[i] <= near_nile && hm.data[i] >= forest_h) bank.push_back(i);
	const int palms = scale_by_map_size(100, 1000, half);
	for (int g = 0, tries = 0; g < palms && tries < palms * 200 && !bank.empty(); ++tries) {
		const size_t v = bank[(size_t)rng.range_int(0, (int)bank.size() - 1)];
		float x, z;
		hm.grid_to_world((int)(v % (size_t)hm.res), (int)(v / (size_t)hm.res), x, z);
		x += rng.range_float(-0.5f, 0.5f) * sp;
		z += rng.range_float(-0.5f, 0.5f) * sp;
		if (x * x + z * z > (half - 1.0f) * (half - 1.0f) || to_bases(x, z) <= 12.0f || !free_of(x, z, 1.0f)) continue;
		const int n = rng.range_int(1, 2);
		for (int k = 0; k < n; ++k) {
			const float a = rng.range_float(0.0f, 6.2831853f);
			const float tx = x + (k ? det::cos(a) : 0.0f), tz = z + (k ? det::sin(a) : 0.0f);
			const size_t tv = vertex(tx, tz);
			if (nile[tv] || cliff[tv]) continue;
			sim.spawn_resource_world(tx, tz, RES_WOOD, TREE_WOOD_SUPPLY);
			mark(tx, tz, 0.0f, kForest);
		}
		++g;
	}

	// "Creating straggler trees and bushes": twice (an acacia, then a dead tree) scaleByMapSize(10, 180) groups
	// of the tree and 0-1 bush_badlands 2 away, 10 from the Nile, off cliffs, 4 from other trees, 12 from the city
	// patches, 5 from the base resources, 4 from the mines and 6 from the pyramid (10 tries a group). plant_flora
	// gives each straggler its acacia, dead tree or bush.
	const int stragglers = scale_by_map_size(10, 180, half);
	for (int pass = 0; pass < 2; ++pass)
		for (int g = 0, tries = 0; g < stragglers && tries < stragglers * 10; ++tries) {
			float x, z;
			random_spot(x, z);
			const size_t v = vertex(x, z);
			if (to_nile[v] <= 10.0f || cliff[v] || to_bases(x, z) <= 12.0f || !free_of(x, z, 4.0f)) continue;
			sim.spawn_resource_world(x, z, RES_WOOD, TREE_WOOD_SUPPLY);
			mark(x, z, 0.0f, kForest);
			if (rng.range_int(0, 1)) {
				const float a = rng.range_float(0.0f, 6.2831853f);
				const float bx = x + det::cos(a) * 2.0f, bz = z + det::sin(a) * 2.0f;
				if (!cliff[vertex(bx, bz)] && bx * bx + bz * bz < half * half) {
					sim.spawn_resource_world(bx, bz, RES_WOOD, TREE_WOOD_SUPPLY);
					mark(bx, bz, 0.0f, kForest);
				}
			}
			++g;
		}
}

void RmsGenerator::distribute_player_bases(Simulation &sim, const HeightmapGrid &hm, const std::vector<PlayerSpawn> &spawns, RmsRng &rng, const Config &config) {
	// placePlayerBase returns at once for Nomad (player.js:152-153): no civic centre, walls,
	// mines, trees, berries, hunt or treasures. place_nomad_units scatters the units later.
	if (sim.nomad()) return;
	const bool fortress = config.map_type == MAP_FORTRESS;
	// 0 A.D.'s base placers (rmgen's placePlayerBase*) keep every starting
	// resource on land and away from water (avoidClasses(clWater, ...)).
	// A cluster that would fall in water, or across a strait on other land,
	// is turned around the civic centre at the same distance, then pulled
	// back towards it, until it is dry (Continental seed 42 had its
	// forward gold under water).
	const MapType mt = config.map_type;
	// Base resources keep 0.6 above the water, or on land lower than that (Flood's 2 m home
	// islands, in 0.28 metres) just below the base's own ground.
	const float dry_h0 = sim.water_height() + 0.6f;
	float dry_h = dry_h0;
	// The script's placePlayerBases arguments (base_resources_data.h); none for a script with its own base code.
	const MapBaseResources *base = map_base_resources(map_type_info(mt).id);
	const float tiles = 2.0f * hm.map_half;
	auto sbms = [&](float lo, float hi) { return lo + (hi - lo) * std::clamp((tiles - 128.0f) / 384.0f, 0.0f, 1.0f); };
	// A tile land units can't stand on (0.28's passability: max minus min corner height
	// over the tile above the class's MaxTerrainSlope), for the mines, which gatherers
	// walk up to.
	auto steep = [&](float x, float z) {
		float lo = 1e9f, hi = -1e9f;
		for (float dz : {-0.5f, 0.5f})
			for (float dx : {-0.5f, 0.5f}) {
				const float h = hm.sample_world(x + dx, z + dz);
				lo = std::min(lo, h);
				hi = std::max(hi, h);
			}
		return hi - lo > LAND_MAX_TERRAIN_SLOPE;
	};
	auto dry = [&](float px, float pz, float &cx, float &cz, float spread, bool flat = false) {
		auto wet = [&](float x, float z) {
			if (hm.sample_world(x, z) < dry_h) return true;
			if (flat) {
				if (steep(x, z)) return true;
				for (int k = 0; k < 8; ++k) {
					const float a = 0.7853982f * (float)k;
					if (steep(x + det::cos(a) * spread, z + det::sin(a) * spread)) return true;
				}
			}
			for (int k = 0; k < 8; ++k) {
				const float a = 0.7853982f * (float)k;
				if (hm.sample_world(x + det::cos(a) * spread, z + det::sin(a) * spread) < dry_h) return true;
			}
			// Dry land across a strait (another island) does not count.
			const float l = det::hypot(x - px, z - pz);
			for (float t = 0.0f; t < l; t += 0.5f)
				if (hm.sample_world(px + (x - px) * t / l, pz + (z - pz) * t / l) < dry_h) return true;
			return false;
		};
		if (!wet(cx, cz)) return;
		const float d = det::hypot(cx - px, cz - pz);
		if (d < 1e-3f) return;
		const float a0 = det::atan2(cz - pz, cx - px);
		const float ox = cx, oz = cz;
		for (float r = d; r >= 7.0f; r -= 0.5f)
			for (int k = 0; k <= 12; ++k) {
				// 0, +15, -15, +30, ... degrees, up to 90 either side.
				const float a = a0 + 0.2617994f * (float)((k + 1) / 2) * ((k & 1) ? 1.0f : -1.0f);
				const float tx = px + det::cos(a) * r, tz = pz + det::sin(a) * r;
				if (!wet(tx, tz)) {
					cx = tx;
					cz = tz;
					return;
				}
			}
		cx = ox;
		cz = oz;
	};
	for (const auto &sp : spawns) {
		dry_h = std::min(dry_h0, std::max(sim.water_height() + 0.2f, sp.base_height - 0.05f));
		uint8_t team = sp.team;
		float px = sp.x;
		float pz = sp.z;
		float fx = sp.forward_dir_x;
		float fz = sp.forward_dir_z;
		float rx = -fz;
		float rz = fx;

		// 1. Town Center
		sim.spawn_building_world(px, pz, BLD_TOWN_CENTER, team, 1.0f);

		// 2. The civ's StartEntities units (placeCivDefaultStartingEntities).
		place_start_entities(sim, hm, team, px, pz);

		// fortress.js gives no starting mines, trees or berries, only treasures; its
		// `for (const dist of [6, 8])` places the infantry a second time at 8 tiles, from a
		// list without the civic centre (fortress.js:79-90).
		if (fortress) {
			place_start_entities(sim, hm, team, px, pz, true, 8.0f, -1);
			place_fortress_walls(sim, spawns, (size_t)(&sp - spawns.data()));
			continue;
		}

		// islands.js and land_grab.js: a dock on the shore towards the map centre
		// (findLocationInDirectionBasedOnHeight from the base to the centre).
		if (mt == MAP_ISLANDS || mt == MAP_LAND_GRAB) {
			const float d = det::hypot(px, pz);
			for (float t = 8.0f; t < d; t += 0.5f) {
				const float dx = px - px / d * t, dz = pz - pz / d * t;
				if (hm.sample_world(dx, dz) >= sim.water_height() + NAV_WATER_MARGIN) continue;
				sim.spawn_building_world(dx, dz, BLD_DOCK, team, 1.0f);
				break;
			}
		}

		// 4. Primary Food: placePlayerBaseBerries, min/maxCount (5) bushes of the script's
		// template, mostly the biome's fruitBush. Spawned unnamed at 200: plant_flora makes each
		// the biome's fruit at its template's Max (dates 400).
		float berry_cx = px + fx * 5.5f + rx * 6.5f;
		float berry_cz = pz + fz * 5.5f + rz * 6.5f;
		dry(px, pz, berry_cx, berry_cz, 2.5f);
		const int berries = !base ? 5 : !base->berries ? 0
				: base->berry_min == base->berry_max ? base->berry_min : rng.range_int(base->berry_min, base->berry_max);
		for (int b = 0; b < berries; ++b) {
			float ba = (6.2831853f * b) / (float)berries;
			float bx = berry_cx + det::cos(ba) * 1.5f;
			float bz = berry_cz + det::sin(ba) * 1.5f;
			sim.spawn_resource_world(bx, bz, RES_FOOD, 200);
		}

		// 5. Primary Forest: placePlayerBaseTrees, one group of "count" trees (default
		// scaleByMapSize(7, 20); mainland 5, many scripts 2), 0-5 apart, 11-13 from the civic
		// centre. A map script with its own base code keeps RTS0's woodline of 10 spots of two.
		float forest_cx = px - fx * 10.5f + rx * 3.5f;
		float forest_cz = pz - fz * 10.5f + rz * 3.5f;
		dry(px, pz, forest_cx, forest_cz, 5.0f);
		if (!base) {
			for (int t = 0; t < 10; ++t) {
				float ta = -1.2f + (2.4f * t) / 9.0f;
				float tx = forest_cx + det::cos(ta) * 4.0f + rng.range_float(-0.4f, 0.4f);
				float tz = forest_cz + det::sin(ta) * 4.0f + rng.range_float(-0.4f, 0.4f);
				spawn_trees(sim, hm, tx, tz, dry_h, 2);
			}
		} else if (base->trees) {
			const int trees = config.biome == BIOME_SAVANNA && base->tree_savanna ? base->tree_savanna
					: (int)std::lround(sbms((float)base->tree_min, (float)base->tree_max));
			// A sunflower spiral: one tree per tile, the group about 5 tiles across.
			const float a0 = rng.range_float(0.0f, 6.2831853f);
			for (int t = 0; t < trees; ++t) {
				const float r = 1.1f * std::sqrt((float)t), a = a0 + 2.3999632f * (float)t;
				const float tx = forest_cx + det::cos(a) * r, tz = forest_cz + det::sin(a) * r;
				if (hm.sample_world(tx, tz) > dry_h) spawn_trees(sim, hm, tx, tz, dry_h, 1);
			}
		}

		// 6-7. rmgen placePlayerBaseMines: one large metal mine and one large
		// stone mine (template_gaia_ore_large / _rock_large, 5000 each), each on a tile
		// land units can stand on. 0.28's bases have no other mines.
		float gold_cx = px + fx * 3.5f - rx * 11.5f;
		float gold_cz = pz + fz * 3.5f - rz * 11.5f;
		dry(px, pz, gold_cx, gold_cz, 2.8f, true);
		sim.spawn_resource_world(gold_cx, gold_cz, RES_GOLD, LARGE_MINE_SUPPLY);

		float stone_cx = px - fx * 8.5f - rx * 9.5f;
		float stone_cz = pz - fz * 8.5f - rz * 9.5f;
		dry(px, pz, stone_cx, stone_cz, 2.5f, true);
		sim.spawn_resource_world(stone_cx, stone_cz, RES_STONE, LARGE_MINE_SUPPLY);

		BiomeFauna bf = biome_fauna(config.biome, (int)config.seed);
		// lower_nubia.js: StartingAnimal gaia/fauna_gazelle (no other base hunt; RTS0's starter hunt takes it too).
		if (mt == MAP_LOWER_NUBIA) bf.starting = bf.main_hunt = FAUNA_GAZELLE;
		// 10. placePlayerBaseStartingAnimal: groupCount (2) groups of the template (gaia/fauna_chicken
		// unless the script names one; rmgen2 maps pass the biome's startingAnimal), each of
		// round(5 * 40 / its food) animals (5 chickens, 400 food in all) unless the script sets
		// min/maxGroupCount, 0-2 apart.
		float deer_cx = px - fx * 14.0f - rx * 5.0f;
		float deer_cz = pz - fz * 14.0f - rz * 5.0f;
		dry(px, pz, deer_cx, deer_cz, 3.5f);
		// jebel_barkal.js places its bases with its own code: StartingAnimal gaia/fauna_gazelle,
		// 2-3 a group (min/maxGroupCount), groupCount 2.
		static constexpr MapBaseResources kJebelAnimals = {"jebel_barkal", true, "gaia/fauna_gazelle", 2, 2, 3,
				false, nullptr, 0, 0, false, nullptr, 0, 0, 0};
		const MapBaseResources *animals = mt == MAP_JEBEL_BARKAL ? &kJebelAnimals : base;
		if (!animals) {
			for (int d = 0; d < 4; ++d) {
				float da = (6.2831853f * d) / 4.0f;
				sim.spawn_fauna(deer_cx + det::cos(da) * 2.5f, deer_cz + det::sin(da) * 2.5f, bf.starting);
			}
			// 11. Starter hunt: 2 of the biome's main huntable animal (RTS0's own; 0.28's
			// placePlayerBase has none).
			float boar_cx = px + fx * 19.0f - rx * 5.0f;
			float boar_cz = pz + fz * 19.0f - rz * 5.0f;
			dry(px, pz, boar_cx, boar_cz, 2.5f);
			sim.spawn_fauna(boar_cx - 1.5f, boar_cz, bf.main_hunt);
			sim.spawn_fauna(boar_cx + 1.5f, boar_cz, bf.main_hunt);
		} else if (animals->animal) {
			const char *tpl = animals->animal_template;
			uint8_t kind = FAUNA_CHICKEN;
			if (!std::strcmp(tpl, "biome:startingAnimal")) kind = bf.starting;
			else if (!std::strncmp(tpl, "gaia/", 5) && fauna_type_by_name(tpl + 5) < FAUNA_COUNT) kind = fauna_type_by_name(tpl + 5);
			const int food = std::max(1, fauna_type_info(kind).food_amount);
			const int each = animals->animal_min ? (animals->animal_min == animals->animal_max ? animals->animal_min
					: rng.range_int(animals->animal_min, animals->animal_max)) : std::max(1, (int)std::lround(5.0f * 40.0f / (float)food));
			// The second group on the base's other side, as far out.
			float herd_cx[2] = {deer_cx, px + fx * 9.0f - rx * 3.0f}, herd_cz[2] = {deer_cz, pz + fz * 9.0f - rz * 3.0f};
			if (animals->animal_groups > 1) dry(px, pz, herd_cx[1], herd_cz[1], 2.0f);
			for (int g = 0; g < std::min<int>(animals->animal_groups, 2); ++g)
				for (int k = 0; k < each; ++k) {
					const float a = rng.range_float(0.0f, 6.2831853f), r = k ? rng.range_float(0.5f, 1.5f) : 0.0f;
					sim.spawn_fauna(herd_cx[g] + det::cos(a) * r, herd_cz[g] + det::sin(a) * r, kind);
				}
		}
	}
}

void RmsGenerator::distribute_neutral_resources(Simulation &sim, const HeightmapGrid &hm, float water_h, const std::vector<PlayerSpawn> &spawns, RmsRng &rng, const Config &config) {
	float half = hm.map_half;
	int num_groves = (int)(half * 0.65f); // ~40 groves for 64m map
	if (config.map_type == MAP_ARDENNES_FOREST) num_groves = num_groves * 5 / 2;
	else if (config.map_type == MAP_MAINLAND) num_groves = num_groves * 4 / 3;
	else if (config.map_type == MAP_NEARESTERN_BADLANDS) num_groves = num_groves * 2 / 5;
	else if (config.map_type == MAP_BELGIAN_UPLANDS) num_groves = num_groves * 3 / 2;
	else if (config.map_type == MAP_HYRCANIAN_SHORES) num_groves = num_groves * 3 / 2;
	else if (config.map_type == MAP_NORTHERN_LIGHTS) num_groves = num_groves / 3;
	else if (config.map_type == MAP_POLAR_SEA) num_groves = num_groves / 5;
	else if (config.map_type == MAP_SAHEL) num_groves = num_groves / 2;
	else if (config.map_type == MAP_KERALA || config.map_type == MAP_AFRICAN_PLAINS) num_groves = num_groves * 4 / 3;
	else if (config.map_type == MAP_SYRIA || config.map_type == MAP_PERSIAN_HIGHLANDS) num_groves = num_groves * 2 / 3;
	else if (config.map_type == MAP_VOLCANIC_LANDS) num_groves = num_groves / 2;
	else if (config.map_type == MAP_DEEP_FOREST) num_groves = num_groves * 3;
	else if (config.map_type == MAP_INDIA || config.map_type == MAP_FIELDS_OF_MEROE) num_groves = num_groves * 2 / 3;
	else if (config.map_type == MAP_ATLAS_MOUNTAINS) num_groves = num_groves / 2;
	else if (config.map_type == MAP_CORSICA) num_groves = num_groves * 3 / 2;
	else if (config.map_type == MAP_BAHRAIN || config.map_type == MAP_SCYTHIAN_RIVULET) num_groves = num_groves * 2 / 3;
	else if (config.map_type == MAP_BOTSWANAN_HAVEN) num_groves = num_groves / 3; // straggler trees only
	else if (config.map_type == MAP_SCHWARZWALD) num_groves = 0; // its own wood field, distribute_land_extras
	else if (config.map_type == MAP_OASIS_0AD) num_groves = num_groves / 5;
	else if (config.map_type == MAP_SAHARAN_OASES) num_groves = 0; // its palms grow in the oases' rings
	else if (config.map_type == MAP_LOWER_NUBIA) num_groves = 0; // its palms and stragglers, place_lower_nubia_gaia
	// migration.js: two forest passes of the biome's forestTrees each, on the
	// continent (stayClasses(clLand, 7)) and on the player islands (3x the clumps).
	else if (config.map_type == MAP_MIGRATION) num_groves = num_groves * 2;
	// ngorongoro.js: rmgen2 addForests plants nothing on generic/savanna; its zones' baobab
	// stragglers, mines and hunt are place_ngorongoro_entities'.
	else if (config.map_type == MAP_NGORONGORO) num_groves = 0;
	else if (config.map_type == MAP_RIVER_ARCHIPELAGO) num_groves = 0; // its own forests, place_river_archipelago_entities
	// 0.28 scripts that size their woods by the biome (getTreeCounts(...rBiomeTreeCount(m)),
	// randombiome.js): forest trees are scaleByMapSize(trees.min, trees.max) * forestProbability
	// from rmbiome/generic/<biome>.json. The counts above are temperate's (1000-3000 at 0.7),
	// so a drier biome gets its share of them: about a quarter on nubia, sahara and savanna.
	switch (config.map_type) {
		case MAP_ARCHIPELAGO_0AD: case MAP_CANTABRIAN_HIGHLANDS: case MAP_CANYON: case MAP_COAST_RANGE:
		case MAP_CONTINENT: case MAP_FLOOD: case MAP_FOOTHILLS: case MAP_GEAR: case MAP_ISLAND_STRONGHOLD:
		case MAP_ISLANDS: case MAP_LAKE: case MAP_LAND_GRAB: case MAP_MAINLAND: case MAP_MIGRATION:
		case MAP_RIVERS: case MAP_SNOWFLAKE_SEAROCKS: case MAP_SURVIVAL_OF_THE_FITTEST: case MAP_UNKNOWN: {
			struct TreeCounts { float min, max, forest; };
			auto counts = [](BiomeType b) -> TreeCounts {
				switch (b) {
					case BIOME_AEGEAN: case BIOME_POLAR: return {500, 2000, 0.6f}; // aegean, arctic
					case BIOME_ALPINE: return {1000, 2000, 0.85f};
					case BIOME_INDIA: return {900, 1500, 0.8f};
					case BIOME_OASIS: case BIOME_NUBIA: return {500, 1000, 0.4f}; // sahara, nubia
					case BIOME_SAVANNA: return {500, 1500, 0.3f};
					case BIOME_STEPPE: return {800, 1400, 0.7f};
					default: return {1000, 3000, 0.7f}; // temperate, autumn
				}
			};
			const float t = std::clamp((2.0f * half - 128.0f) / 384.0f, 0.0f, 1.0f);
			auto forest = [&](const TreeCounts &c) { return (c.min + (c.max - c.min) * t) * c.forest; };
			num_groves = (int)std::lround((float)num_groves * forest(counts(config.biome)) / forest(counts(BIOME_TEMPERATE)));
			break;
		}
		default: break;
	}
	// cycladic_archipelago.js: four forest types (palm, pine, poplar, carob and beech), each scaleByMapSize(10, 64)
	// clumps of randIntInclusive(6, 17) tiles placed by createAreasInAreas in a random island (player islands
	// included, retryFactor 20), every tile 3 from the water, 3 from other forests, 1 from hills and the city
	// patch and 4 from base resources. LayeredPainter([floor, grass, pForest], [floor, pForest]) with
	// pForest = [floor + tree, grass]: a tree on a sixth of the outer two rings' tiles and a quarter inside.
	if (config.map_type == MAP_CYCLADIC_ARCHIPELAGO) {
		num_groves = 0;
		const int res = std::clamp((int)std::lround(2.0f * half), 1, Simulation::MAX_TERRAIN_TILE_RES);
		const float cell = 2.0f * half / (float)res;
		std::vector<const MapArea *> isles;
		std::vector<uint8_t> hill((size_t)res * (size_t)res, 0);
		for (const MapArea &a : t_map_areas) {
			if (a.kind == AREA_ISLAND) isles.push_back(&a);
			if (a.kind == AREA_HILL) for (uint32_t j : a.tiles) hill[j] = 1;
		}
		auto tile_x = [&](uint32_t j) { return -half + ((float)(j % (uint32_t)res) + 0.5f) * cell; };
		auto tile_z = [&](uint32_t j) { return -half + ((float)(j / (uint32_t)res) + 0.5f) * cell; };
		// Chebyshev tile distances to the water (below the nav water line) and to the hills.
		auto distance_from = [&](auto &&seed) {
			std::vector<uint16_t> d((size_t)res * (size_t)res, 0xFFFF);
			std::vector<uint32_t> q;
			for (uint32_t j = 0; j < (uint32_t)d.size(); ++j) if (seed(j)) { d[j] = 0; q.push_back(j); }
			for (size_t k = 0; k < q.size(); ++k) {
				const int x = (int)(q[k] % (uint32_t)res), z = (int)(q[k] / (uint32_t)res);
				for (int dz = -1; dz <= 1; ++dz)
					for (int dx = -1; dx <= 1; ++dx) {
						const int nx = x + dx, nz = z + dz;
						if (nx < 0 || nz < 0 || nx >= res || nz >= res) continue;
						const uint32_t n = (uint32_t)(nz * res + nx);
						if (d[n] != 0xFFFF) continue;
						d[n] = (uint16_t)(d[q[k]] + 1);
						q.push_back(n);
					}
			}
			return d;
		};
		const float line = water_h + NAV_WATER_MARGIN;
		const auto to_water = distance_from([&](uint32_t j) { return hm.sample_world(tile_x(j), tile_z(j)) < line; });
		const auto to_hill = distance_from([&](uint32_t j) { return hill[j] != 0 || hm.compute_slope_world(tile_x(j), tile_z(j)) > 0.85f; });
		std::vector<std::pair<float, float>> base_res;
		{
			const auto rv = sim.resources();
			for (size_t r = 0; r < rv.count; ++r)
				if (rv.alive[r]) base_res.emplace_back(rv.x[r], rv.z[r]);
		}
		const float city_r = (15.0f + 10.0f * std::clamp((2.0f * half - 128.0f) / 384.0f, 0.0f, 1.0f)) / 3.0f + 1.5f + 1.0f;
		std::vector<uint8_t> forest((size_t)res * (size_t)res, 0);
		const int count = (int)scale_by_map_size(10, 64, half);
		for (int type = 0; type < 4 && !isles.empty(); ++type) {
			const int tiles = rng.range_int(6, 17);
			const float r = std::sqrt((float)tiles / 3.14159265f);
			// ClumpPlacer(..., failFraction Infinity): every clump is placed; the tiles the constraints
			// refuse are left out of it.
			for (int placed = 0; placed < count; ++placed) {
				const MapArea &isle = *isles[(size_t)rng.range_int(0, (int)isles.size() - 1)];
				const uint32_t c = isle.tiles[(size_t)rng.range_int(0, (int)isle.tiles.size() - 1)];
				const int cx = (int)(c % (uint32_t)res), cz = (int)(c / (uint32_t)res);
				std::vector<std::pair<uint32_t, int>> clump; // tile, ring depth from the clump's edge
				const int ri = (int)std::ceil(r);
				for (int dz = -ri; dz <= ri; ++dz)
					for (int dx = -ri; dx <= ri; ++dx) {
						const float dd = std::sqrt((float)(dx * dx + dz * dz));
						const int x = cx + dx, z = cz + dz;
						if (dd > r || x < 0 || z < 0 || x >= res || z >= res) continue;
						const uint32_t j = (uint32_t)(z * res + x);
						if (to_water[j] <= 3 || to_hill[j] <= 1) continue;
						bool ok = true;
						for (int fz = std::max(0, z - 3); fz <= std::min(res - 1, z + 3) && ok; ++fz)
							for (int fx = std::max(0, x - 3); fx <= std::min(res - 1, x + 3); ++fx)
								if (forest[(size_t)fz * (size_t)res + (size_t)fx]) { ok = false; break; }
						const float wx = tile_x(j), wz = tile_z(j);
						for (const auto &sp : spawns) ok = ok && det::hypot(wx - sp.x, wz - sp.z) > city_r;
						for (const auto &[bx, bz] : base_res) ok = ok && det::hypot(wx - bx, wz - bz) > 4.0f;
						if (ok) clump.push_back({j, (int)std::floor(r - dd) + 1});
					}
				for (const auto &[j, depth] : clump) {
					forest[j] = 1;
					if (rng.range_int(0, depth <= 2 ? 5 : 3) == 0) sim.spawn_resource_world(tile_x(j), tile_z(j), RES_WOOD, TREE_WOOD_SUPPLY);
				}
			}
		}
		// Straggler trees (carob, beech, two poplars, pine: scaleByMapSize(2, 38) each, retryFactor 50) and
		// scaleByMapSize(5, 75) cypress groups (1-3 and 0-2 more within 3 tiles), by random island, 2 from the
		// water and forests, 3 from the city patch, 4 from base resources, 1 from hills and off the players' islands.
		std::vector<uint8_t> player_isle(isles.size(), 0);
		for (size_t k = 0; k < isles.size(); ++k)
			for (const auto &sp : spawns) {
				const int x = std::clamp((int)((sp.x + half) / cell), 0, res - 1), z = std::clamp((int)((sp.z + half) / cell), 0, res - 1);
				player_isle[k] |= std::binary_search(isles[k]->tiles.begin(), isles[k]->tiles.end(), (uint32_t)(z * res + x));
			}
		auto straggler_spot = [&](uint32_t &out) {
			const size_t k = (size_t)rng.range_int(0, (int)isles.size() - 1);
			if (player_isle[k]) return false;
			const uint32_t j = isles[k]->tiles[(size_t)rng.range_int(0, (int)isles[k]->tiles.size() - 1)];
			if (to_water[j] <= 2 || to_hill[j] <= 1) return false;
			const int x = (int)(j % (uint32_t)res), z = (int)(j / (uint32_t)res);
			for (int fz = std::max(0, z - 2); fz <= std::min(res - 1, z + 2); ++fz)
				for (int fx = std::max(0, x - 2); fx <= std::min(res - 1, x + 2); ++fx)
					if (forest[(size_t)fz * (size_t)res + (size_t)fx]) return false;
			const float wx = tile_x(j), wz = tile_z(j);
			for (const auto &sp : spawns) if (det::hypot(wx - sp.x, wz - sp.z) <= city_r + 2.0f) return false;
			for (const auto &[bx, bz] : base_res) if (det::hypot(wx - bx, wz - bz) <= 4.0f) return false;
			out = j;
			return true;
		};
		for (int pass = 0; pass < 5 && !isles.empty(); ++pass) {
			const int amount = (int)scale_by_map_size(2, 38, half);
			for (int g = 0, tries = 0; g < amount && tries < amount * 50; ++tries) {
				uint32_t j;
				if (!straggler_spot(j)) continue;
				++g;
				forest[j] = 1;
				sim.spawn_resource_world(tile_x(j), tile_z(j), RES_WOOD, TREE_WOOD_SUPPLY);
			}
		}
		{
			const int amount = (int)scale_by_map_size(5, 75, half);
			for (int g = 0, tries = 0; g < amount && tries < amount * 50 && !isles.empty(); ++tries) {
				uint32_t j;
				if (!straggler_spot(j)) continue;
				++g;
				const int n = rng.range_int(1, 3) + rng.range_int(0, 2);
				for (int k = 0; k < n; ++k) {
					const float a = rng.range_float(0.0f, 6.2831853f), d = k == 0 ? 0.0f : rng.range_float(0.0f, 3.0f);
					const float tx = tile_x(j) + det::cos(a) * d, tz = tile_z(j) + det::sin(a) * d;
					if (hm.sample_world(tx, tz) >= line) sim.spawn_resource_world(tx, tz, RES_WOOD, TREE_WOOD_SUPPLY);
				}
			}
		}
	}

	// Map-specific landmark resource clusters
	if (config.map_type == MAP_SAHARAN_OASES || config.map_type == MAP_NEARESTERN_BADLANDS) {
		// saharan_oases.js's oasis rings 3 to 5 tiles in from the edge take [tLush, pForest], and pForest
		// is a date palm, a short Cretan date palm or bare tLush: a palm on about a third of those tiles.
		// neareastern_badlands.js's take [tSand, pForest] 1 to 2 in and [tGrassSand25, pForestOasis] 3 to 5
		// in, a palm on a third of each.
		const uint16_t first_ring = config.map_type == MAP_SAHARAN_OASES ? 3 : 1;
		const int res = std::clamp((int)std::lround(2.0f * half), 1, Simulation::MAX_TERRAIN_TILE_RES);
		const float cell = 2.0f * half / (float)res;
		std::vector<uint8_t> in((size_t)res * (size_t)res, 0);
		std::vector<uint16_t> depth(in.size(), 0);
		std::vector<uint32_t> queue;
		for (const MapArea &a : t_map_areas) {
			if (a.kind != AREA_OASIS) continue;
			for (uint32_t j : a.tiles) in[j] = 1;
			// LayeredPainter's depth: 8-neighbour steps from the area's edge, 1 on the edge.
			queue.clear();
			for (uint32_t j : a.tiles) {
				const int x = (int)(j % (uint32_t)res), z = (int)(j / (uint32_t)res);
				bool edge = false;
				for (int dz = -1; dz <= 1 && !edge; ++dz)
					for (int dx = -1; dx <= 1 && !edge; ++dx) {
						const int nx = x + dx, nz = z + dz;
						edge = nx < 0 || nz < 0 || nx >= res || nz >= res || !in[(size_t)nz * (size_t)res + (size_t)nx];
					}
				if (edge) { depth[j] = 1; queue.push_back(j); }
			}
			for (size_t k = 0; k < queue.size(); ++k) {
				const int x = (int)(queue[k] % (uint32_t)res), z = (int)(queue[k] / (uint32_t)res);
				for (int dz = -1; dz <= 1; ++dz)
					for (int dx = -1; dx <= 1; ++dx) {
						const int nx = x + dx, nz = z + dz;
						if (nx < 0 || nz < 0 || nx >= res || nz >= res) continue;
						const uint32_t n = (uint32_t)(nz * res + nx);
						if (!in[n] || depth[n]) continue;
						depth[n] = (uint16_t)(depth[queue[k]] + 1);
						queue.push_back(n);
					}
			}
			for (uint32_t j : a.tiles) {
				if (depth[j] >= first_ring && depth[j] <= 5 && rng.range_int(0, 5) < 2) {
					const float x = -half + ((float)(j % (uint32_t)res) + 0.5f) * cell, z = -half + ((float)(j / (uint32_t)res) + 0.5f) * cell;
					if (hm.sample_world(x, z) > water_h - kMetre) sim.spawn_resource_world(x, z, RES_WOOD, TREE_WOOD_SUPPLY);
				}
			}
			for (uint32_t j : a.tiles) { in[j] = 0; depth[j] = 0; }
		}
	} else if (config.map_type == MAP_ANATOLIAN_PLATEAU) {
		// Rich mineral deposits along canyon rims and high steppe
		std::vector<std::pair<float, float>> rim_mines = {
			{-half * 0.35f,  half * 0.30f},
			{ half * 0.35f, -half * 0.30f},
			{ half * 0.30f,  half * 0.35f},
			{-half * 0.30f, -half * 0.35f},
			{ 0.0f,          half * 0.38f},
			{ 0.0f,         -half * 0.38f}
		};
		for (size_t mi = 0; mi < rim_mines.size(); ++mi) {
			uint8_t mtype = (mi % 2 == 0) ? RES_GOLD : RES_STONE;
			float mx = rim_mines[mi].first;
			float mz = rim_mines[mi].second;
			sim.spawn_resource_world(mx, mz, mtype, LARGE_MINE_SUPPLY);
		}
	}

	if (config.map_type == MAP_PHOENICIAN_LEVANT || config.map_type == MAP_BAHRAIN) {
		// phoenician_levant.js: 3 x scaleByMapSize(4, 16) stone, metal and
		// small stone groups on Cyprus (land off the bases' landmass), 9 in
		// from its shore; bahrain.js: scaleByMapSize(4, 10) large metal or
		// stone mines on Bahrain island (the land 0.4-0.6 of the map wide
		// north of the centre), 4 in. Kept 8 apart.
		const bool levant = config.map_type == MAP_PHOENICIAN_LEVANT;
		const auto mainland = largest_walkable_region(hm, water_h);
		std::vector<uint8_t> island(hm.data.size(), 0), not_island(hm.data.size(), 0);
		for (int z = 0; z < hm.res; ++z)
			for (int x = 0; x < hm.res; ++x) {
				float wx, wz;
				hm.grid_to_world(x, z, wx, wz);
				const size_t i = (size_t)z * (size_t)hm.res + (size_t)x;
				const bool dry = hm.data[i] >= water_h + 0.45f;
				island[i] = levant ? dry && !mainland[i] : dry && std::fabs(wx) <= 0.2f * half && wz <= 0.0f;
				not_island[i] = !island[i];
			}
		const auto inside = distance_to_mask(not_island, hm.res, 2.0f * half / (float)(hm.res - 1));
		std::vector<std::pair<float, float>> spots;
		for (int z = 0; z < hm.res; ++z)
			for (int x = 0; x < hm.res; ++x) {
				const size_t i = (size_t)z * (size_t)hm.res + (size_t)x;
				if (inside[i] <= (levant ? 9.0f : 4.0f)) continue;
				float wx, wz;
				hm.grid_to_world(x, z, wx, wz);
				spots.push_back({wx, wz});
			}
		const int groups = levant ? 3 * scale_by_map_size(4, 16, half) : scale_by_map_size(4, 10, half);
		std::vector<std::pair<float, float>> placed;
		for (int g = 0, tries = 0; g < groups && !spots.empty() && tries < groups * 40; ++tries) {
			const auto p = spots[(size_t)rng.range_int(0, (int)spots.size() - 1)];
			bool crowded = false;
			for (const auto &q : placed) crowded |= det::hypot(p.first - q.first, p.second - q.second) < 8.0f;
			if (crowded) continue;
			placed.push_back(p);
			const uint8_t type = levant ? (g % 3 == 1 ? RES_GOLD : RES_STONE) : (rng.range_int(0, 1) ? RES_GOLD : RES_STONE);
			if (levant && g % 3 == 2) {
				// The small stone group: three template_gaia_rock rocks.
				for (int r = 0; r < 3; ++r) {
					const float a = 6.2831853f * (float)r / 3.0f;
					sim.spawn_resource_world(p.first + det::cos(a) * 1.6f, p.second + det::sin(a) * 1.6f, type, MINE_SUPPLY);
				}
			} else {
				sim.spawn_resource_world(p.first, p.second, type, LARGE_MINE_SUPPLY);
			}
			++g;
		}
	}

	auto on_player_island = [&](float x, float z) {
		for (const auto &sp : spawns) {
			const float dx = x - sp.x, dz = z - sp.z, d = std::sqrt(dx * dx + dz * dz);
			if (d > 40.0f) continue;
			bool dry_line = true;
			for (float f = 0.0f; f <= d && dry_line; f += 0.5f)
				dry_line = hm.sample_world(sp.x + dx * f / std::max(d, 1e-3f), sp.z + dz * f / std::max(d, 1e-3f)) > water_h;
			if (dry_line) return true;
		}
		return false;
	};
	// dodecanese.js: createForests avoids only clPlayer 4 (the civic centre's 5-tile disk) and
	// clBaseResource 4, so its forests reach the players' small islands.
	const bool forests_near_bases = config.map_type == MAP_DODECANESE;
	std::vector<std::pair<float, float>> base_resources;
	if (forests_near_bases) {
		const auto rv = sim.resources();
		for (size_t r = 0; r < rv.count; ++r)
			if (rv.alive[r]) base_resources.emplace_back(rv.x[r], rv.z[r]);
	}
	// 1. Organic Forest Clumps
	// createForests' createAreas keeps trying (retryFactor 200) until each forest finds land;
	// dodecanese.js's sea would otherwise take most of the tries.
	int grove_retries = forests_near_bases ? 200 : 0;
	for (int g = 0; g < num_groves; ++g) {
		float gx = rng.range_float(-half * 0.82f, half * 0.82f);
		float gz = rng.range_float(-half * 0.82f, half * 0.82f);

		// Exclude player starting radii
		bool near_player = false;
		for (const auto &sp : spawns) {
			float d = std::sqrt((gx - sp.x) * (gx - sp.x) + (gz - sp.z) * (gz - sp.z));
			if (d < (forests_near_bases ? 9.0f : 16.0f)) { near_player = true; break; }
		}
		// Check terrain suitability (cliffs skipped)
		if (near_player || hm.sample_world(gx, gz) < water_h + 0.3f || hm.compute_slope_world(gx, gz) > 0.85f) {
			if (grove_retries > 0) { --grove_retries; --g; }
			continue;
		}

		int trees_in_grove = rng.range_int(6, 12);
		float grove_r = rng.range_float(3.0f, 5.5f);
		for (int t = 0; t < trees_in_grove; ++t) {
			float a = rng.range_float(0.0f, 6.2831853f);
			float r = std::sqrt(rng.next_float()) * grove_r;
			float tx = gx + det::cos(a) * r;
			float tz = gz + det::sin(a) * r;
			if (hm.sample_world(tx, tz) <= water_h + 0.2f) continue;
			// islands.js: forests avoidClasses(clPlayer, 0), so none on a player's island
			// (the straight line from its civic centre stays on land).
			if (config.map_type == MAP_ISLANDS && on_player_island(tx, tz)) continue;
			if (forests_near_bases) {
				bool clear = true;
				for (const auto &sp : spawns) clear &= det::hypot(tx - sp.x, tz - sp.z) >= 9.0f;
				for (const auto &[bx, bz] : base_resources) clear &= det::hypot(tx - bx, tz - bz) >= 4.0f;
				if (!clear) continue;
			}
			spawn_trees(sim, hm, tx, tz, water_h + 0.2f);
		}
	}

	// 2. Contested Neutral Gold & Stone Quarries (maps with a 0 A.D. script take its
	// counts from top_up_resources instead)
	int num_neutral_mines = map_resource_counts(map_type_info(config.map_type).id) ? 0
			: std::max(4, config.num_players * 2) * scale_by_map_size(1, 4, half);
	// lower_nubia.js: its own createMines passes, pyramid and trees.
	if (config.map_type == MAP_LOWER_NUBIA) {
		num_neutral_mines = 0;
		place_lower_nubia_gaia(sim, hm, water_h, spawns, rng);
	}
	// migration.js puts its mines on the continent only (distribute_land_extras).
	if (config.map_type == MAP_MIGRATION) num_neutral_mines = 0;
	if (config.map_type == MAP_NGORONGORO) num_neutral_mines = 0; // the zones' own mines (place_ngorongoro_entities)
	if (config.map_type == MAP_RIVER_ARCHIPELAGO) num_neutral_mines = 0; // its own mines, place_river_archipelago_entities
	for (int m = 0; m < num_neutral_mines; ++m) {
		float mx = rng.range_float(-half * 0.70f, half * 0.70f);
		float mz = rng.range_float(-half * 0.70f, half * 0.70f);

		bool near_player = false;
		for (const auto &sp : spawns) {
			float d = std::sqrt((mx - sp.x) * (mx - sp.x) + (mz - sp.z) * (mz - sp.z));
			if (d < 22.0f) { near_player = true; break; }
		}
		if (near_player) continue;

		float h = hm.sample_world(mx, mz);
		if (h < water_h + 0.4f || hm.compute_slope_world(mx, mz) > 0.7f) continue;

		// Like rmgen's mine passes: a large mine (5000), or a group of
		// small rocks (1000 each).
		uint8_t res_type = (m % 2 == 0) ? RES_GOLD : RES_STONE;
		int rocks = rng.range_int(2, 4);
		if (rocks == 2) {
			sim.spawn_resource_world(mx, mz, res_type, LARGE_MINE_SUPPLY);
			continue;
		}
		for (int r = 0; r < rocks; ++r) {
			float a = (6.2831853f * r) / (float)rocks;
			sim.spawn_resource_world(mx + det::cos(a) * 1.6f, mz + det::sin(a) * 1.6f, res_type, MINE_SUPPLY);
		}
	}

	distribute_land_extras(sim, hm, water_h, spawns, rng, config);
}

void RmsGenerator::distribute_wildlife(Simulation &sim, const HeightmapGrid &hm, float water_h, const std::vector<PlayerSpawn> &spawns, RmsRng &rng, const Config &config) {
	float half = hm.map_half;
	if (config.map_type == MAP_LOWER_NUBIA) {
		// lower_nubia.js: scaleByMapSize(2, 10) groups each of 5-7 gazelles, of 1-2 lions with 2-3 lionesses and
		// of 2-3 African bush elephants with 2-3 infants, 2-4 from the group's centre, off the Nile and cliffs, 12 from the city
		// patches and 6 from other food (avoidCollisions; 50 tries a group).
		struct Herd { uint8_t a; int a_lo, a_hi; uint8_t b; int b_lo, b_hi; };
		static const Herd kHerds[3] = {{FAUNA_GAZELLE, 5, 7, FAUNA_GAZELLE, 0, 0},
		                               {FAUNA_LION, 1, 2, FAUNA_LIONESS, 2, 3},
		                               {FAUNA_ELEPHANT_AFRICAN_BUSH, 2, 3, FAUNA_ELEPHANT_AFRICAN_INFANT, 2, 3}};
		const auto slope = slopes_m(hm);
		const float patch = (float)scale_by_map_size(15, 25, half) / 3.0f;
		const float sea = metres(water_h, -3.0f * 2.0f * half / 320.0f) + 1e-3f;
		const int groups = scale_by_map_size(2, 10, half);
		std::vector<std::pair<float, float>> food;
		for (const Herd &h : kHerds)
			for (int g = 0, tries = 0; g < groups && tries < groups * 50; ++tries) {
				const float x = rng.range_float(-half, half), z = rng.range_float(-half, half);
				if (x * x + z * z > (half - 4.0f) * (half - 4.0f)) continue;
				bool ok = true;
				for (int k = 0; ok && k < 9; ++k) { // off the Nile by a tile
					const float a = 0.7853982f * (float)k, r = k == 8 ? 0.0f : 1.0f;
					ok = hm.sample_world(x + det::cos(a) * r, z + det::sin(a) * r) > sea;
				}
				float gx, gz;
				hm.world_to_grid(x, z, gx, gz);
				const size_t v = (size_t)std::clamp((int)std::lround(gz), 0, hm.res - 1) * (size_t)hm.res +
				                 (size_t)std::clamp((int)std::lround(gx), 0, hm.res - 1);
				ok = ok && slope[v] < 2.0f;
				for (size_t k = 0; ok && k < spawns.size(); ++k) ok = det::hypot(x - spawns[k].x, z - spawns[k].z) > patch + 12.0f;
				for (size_t k = 0; ok && k < food.size(); ++k) ok = det::hypot(x - food[k].first, z - food[k].second) > 6.0f;
				if (!ok) continue;
				food.push_back({x, z});
				const int na = rng.range_int(h.a_lo, h.a_hi), nb = h.b_hi ? rng.range_int(h.b_lo, h.b_hi) : 0;
				for (int i = 0; i < na + nb; ++i) {
					const float a = rng.range_float(0.0f, 6.2831853f), r = rng.range_float(2.0f, 4.0f);
					sim.spawn_fauna(x + det::cos(a) * r, z + det::sin(a) * r, i < na ? h.a : h.b);
				}
				++g;
			}
		return;
	}
	int num_packs = scale_by_map_size(10, 40, half);
	if (config.map_type == MAP_ARCTIC_SUMMER || config.map_type == MAP_RATUMACOS) num_packs *= 2;
	// migration.js: deer, sheep and fruit on the continent only, no predators (distribute_land_extras).
	if (config.map_type == MAP_MIGRATION) num_packs = 0;
	if (config.map_type == MAP_NGORONGORO) num_packs = 0; // each zone's own hunt (place_ngorongoro_entities)
	else if (config.map_type == MAP_ELEPHANTINE) num_packs = 0; // elephantine.js's hunt (place_elephantine_entities)
	else if (config.map_type == MAP_JEBEL_BARKAL) num_packs = 0; // jebel_barkal.js's hunt (place_jebel_barkal_entities)
	else if (config.map_type == MAP_DANUBIUS) num_packs = 0; // danubius.js's hunt (place_danubius_entities)
	// river_archipelago.js's deer, boar and tigers: place_river_archipelago_entities.
	else if (config.map_type == MAP_RIVER_ARCHIPELAGO) num_packs = 0;
	// Maps with a 0 A.D. script take its hunt from top_up_resources.
	if (map_resource_counts(map_type_info(config.map_type).id)) num_packs = 0;

	for (int p = 0; p < num_packs; ++p) {
		float px = rng.range_float(-half * 0.75f, half * 0.75f);
		float pz = rng.range_float(-half * 0.75f, half * 0.75f);

		bool near_player = false;
		for (const auto &sp : spawns) {
			float d = std::sqrt((px - sp.x) * (px - sp.x) + (pz - sp.z) * (pz - sp.z));
			if (d < 18.0f) { near_player = true; break; }
		}
		if (near_player) continue;

		float h = hm.sample_world(px, pz);
		if (h < water_h + 0.3f || hm.compute_slope_world(px, pz) > 0.8f) continue;

		const BiomeFauna bf = biome_fauna(config.biome, p / 4);
		const uint8_t kinds[4] = {bf.main_hunt, bf.predator, bf.secondary_hunt, bf.main_hunt};
		uint8_t ftype = kinds[p % 4];
		const bool herd = !fauna_type_info(ftype).is_dangerous() && fauna_type_info(ftype).food_amount < 300;
		int pack_size = herd ? rng.range_int(3, 5) : rng.range_int(2, 3);

		for (int i = 0; i < pack_size; ++i) {
			float a = (6.2831853f * i) / (float)pack_size;
			float r = rng.range_float(1.5f, 3.5f);
			sim.spawn_fauna(px + det::cos(a) * r, pz + det::sin(a) * r, ftype);
		}
	}
}

// The landscape synth_unknown picked for this generation (unknown.js g_StartingTreasures).
thread_local int g_unknown_landscape = -1;

// Gaia treasures and ruins, where 0 A.D.'s random map scripts place them (maps/random/*.js);
// most scripts place none. Capture the Relic's relics come from the victory condition
// (Simulation::apply_relic_count), not the map.
void RmsGenerator::distribute_relics(Simulation &sim, const HeightmapGrid &hm, float water_h, const std::vector<PlayerSpawn> &spawns, RmsRng &rng, const Config &config) {
	const float half = hm.map_half;
	const int np = (int)spawns.size();
	auto dry = [&](float x, float z) {
		return std::abs(x) < half - 3.0f && std::abs(z) < half - 3.0f && hm.sample_world(x, z) > water_h + 0.3f;
	};
	auto far_from_bases = [&](float x, float z, float d) {
		for (const auto &sp : spawns)
			if (det::hypot(x - sp.x, z - sp.z) < d) return false;
		return true;
	};
	// SimpleObject(template, n, n, 0, spread): n treasures within `spread` of (x, z).
	auto cluster = [&](float x, float z, uint8_t type, int n, float spread) {
		for (int k = 0; k < n; ++k) {
			const float a = rng.range_float(0.0f, 6.2831853f), r = rng.range_float(0.0f, spread);
			const float tx = x + det::cos(a) * r, tz = z + det::sin(a) * r;
			if (dry(tx, tz)) sim.spawn_treasure(tx, tz, type);
		}
	};
	// createObjectGroupsDeprecated(group, 0, avoid player `avoid`, count, 50 tries).
	auto scatter = [&](uint8_t type, int groups, int lo, int hi, float avoid) {
		for (int g = 0, tries = 0; g < groups && tries < groups * 50; ++tries) {
			const float x = rng.range_float(-half * 0.9f, half * 0.9f), z = rng.range_float(-half * 0.9f, half * 0.9f);
			if (!dry(x, z) || !far_from_bases(x, z, avoid)) continue;
			cluster(x, z, type, rng.range_int(lo, hi), 2.0f);
			++g;
		}
	};
	// placePlayerBaseTreasures: the base's treasures on a ring round the civic centre.
	auto base_ring = [&](uint8_t type, int n) {
		if (sim.nomad()) return; // placePlayerBase places nothing for Nomad
		for (const auto &sp : spawns)
			for (int k = 0; k < n; ++k) {
				const float a = rng.range_float(0.0f, 6.2831853f), r = rng.range_float(10.0f, 13.0f);
				const float tx = sp.x + det::cos(a) * r, tz = sp.z + det::sin(a) * r;
				if (dry(tx, tz)) sim.spawn_treasure(tx, tz, type);
			}
	};

	// rmgen scaleByMapSize.
	const float size_frac = std::clamp((2.0f * half - 128.0f) / 384.0f, 0.0f, 1.0f);
	auto scale = [&](float a, float b) { return (int)std::lround(a + (b - a) * size_frac); };
	auto wet = [&](float x, float z) {
		return std::abs(x) < half - 3.0f && std::abs(z) < half - 3.0f && hm.sample_world(x, z) < water_h - 0.5f;
	};
	// Every sample within r of (x, z) passes `pred` (stayClasses / avoidClasses on a tile class).
	auto all_within = [&](float x, float z, float r, auto pred) {
		for (int k = 0; k < 8; ++k) {
			const float a = 0.7853982f * (float)k;
			if (!pred(x + det::cos(a) * r, z + det::sin(a) * r)) return false;
		}
		return pred(x, z);
	};
	auto any_within = [&](float x, float z, float r, auto pred) {
		for (int k = 0; k < 8; ++k) {
			const float a = 0.7853982f * (float)k;
			if (pred(x + det::cos(a) * r, z + det::sin(a) * r)) return true;
		}
		return false;
	};
	// Shipwrecks: `groups` groups of lo-hi treasures within `spread`, each where `ok` holds,
	// 8 apart (avoidClasses(clFood, 8)).
	std::vector<std::pair<float, float>> wrecks;
	auto scatter_water = [&](uint8_t type, int groups, int lo, int hi, float spread, auto ok) {
		for (int g = 0, tries = 0; g < groups && tries < groups * 100; ++tries) {
			const float x = rng.range_float(-half * 0.95f, half * 0.95f), z = rng.range_float(-half * 0.95f, half * 0.95f);
			if (!ok(x, z)) continue;
			bool clear = true;
			for (const auto &w : wrecks) clear = clear && det::hypot(w.first - x, w.second - z) >= 8.0f;
			if (!clear) continue;
			for (int k = 0, n = rng.range_int(lo, hi); k < n; ++k) {
				const float a = rng.range_float(0.0f, 6.2831853f), r = rng.range_float(0.0f, spread);
				const float tx = x + det::cos(a) * r, tz = z + det::sin(a) * r;
				if (wet(tx, tz)) sim.spawn_treasure(tx, tz, type);
			}
			wrecks.push_back({x, z});
			++g;
		}
	};

	switch (config.map_type) {
		case MAP_BAHRAIN: // bahrain.js: 1 to players wood, then food treasures, 25 from bases, and 0-1 wrecks
			for (uint8_t t : {TREASURE_WOOD, TREASURE_FOOD_BIN})
				scatter(t, rng.range_int(1, std::max(1, np)), 1, 1, 25.0f);
			scatter_water(TREASURE_SHIPWRECK_SAIL_BOAT_CUT, rng.range_int(0, 1), 1, 1, 1.0f,
					[&](float x, float z) { return all_within(x, z, 2.0f, wet); });
			break;
		case MAP_CYCLADIC_ARCHIPELAGO: // cycladic_archipelago.js: shipwrecks and debris in the sea
			scatter_water(TREASURE_SHIPWRECK, scale(6, 16), 1, 1, 3.0f, [&](float x, float z) { return all_within(x, z, 1.0f, wet); });
			scatter_water(TREASURE_SHIPWRECK_DEBRIS, scale(10, 20), 1, 2, 4.0f, [&](float x, float z) { return all_within(x, z, 1.0f, wet); });
			break;
		case MAP_ISLAND_STRONGHOLD: // island_stronghold.js: shipwrecks and debris 4 off the land
			scatter_water(TREASURE_SHIPWRECK, scale(12, 16), 1, 1, 1.0f, [&](float x, float z) { return all_within(x, z, 4.0f, wet); });
			scatter_water(TREASURE_SHIPWRECK_DEBRIS, scale(10, 20), 1, 1, 1.0f, [&](float x, float z) { return all_within(x, z, 4.0f, wet); });
			break;
		case MAP_ARCHIPELAGO_0AD: // archipelago.js and islands.js: 14 wood treasures in each base
		case MAP_ISLANDS:
			base_ring(TREASURE_WOOD, 14);
			break;
		case MAP_UNKNOWN: // unknown.js: 14 in each base on its Archipelago landscape only
			if (g_unknown_landscape == 0) base_ring(TREASURE_WOOD, 14);
			break;
		case MAP_ARCTIC_SUMMER: // arctic_summer.js: 10 wood treasures in each base
			base_ring(TREASURE_WOOD, 10);
			break;
		case MAP_SAHARAN_OASES: { // saharan_oases.js: 3 x scaleByMapSize(5, 20) food bins, then wood, by the oases' shores
			// The oases are 2 m shallows (heightOffsetOasis -3 on 1 m sand), so any water counts.
			auto oasis = [&](float x, float z) { return hm.sample_world(x, z) < water_h; };
			auto shore = [&](float x, float z) { return dry(x, z) && any_within(x, z, 5.0f, oasis); };
			for (uint8_t t : {TREASURE_FOOD_BIN, TREASURE_WOOD}) {
				const int n = 3 * scale(5, 20);
				for (int g = 0, tries = 0; g < n && tries < n * 50; ++tries) {
					const float x = rng.range_float(-half * 0.9f, half * 0.9f), z = rng.range_float(-half * 0.9f, half * 0.9f);
					if (!shore(x, z)) continue;
					cluster(x, z, t, 1, 2.0f);
					++g;
				}
			}
			break;
		}
		case MAP_THE_NILE: { // the_nile.js: 3 x scaleByMapSize(5, 20) food bins, then as many wood treasures
			const int n = (int)std::lround(3.0f * (5.0f + 15.0f * std::clamp((2.0f * half - 128.0f) / 384.0f, 0.0f, 1.0f)));
			scatter(TREASURE_FOOD_BIN, n, 1, 1, 20.0f);
			scatter(TREASURE_WOOD, n, 1, 1, 20.0f);
			break;
		}
		case MAP_ATLAS_MOUNTAINS: // atlas_mountains.js: 3 x players groups of 2-3 food bins, then of wood
			scatter(TREASURE_FOOD_BIN, 3 * np, 2, 3, 18.0f);
			scatter(TREASURE_WOOD, 3 * np, 2, 3, 18.0f);
			break;
		case MAP_CANYON: // canyon.js: 3-8 food bins and as many wood treasures within 7 of the centre
			for (int i = 0, n = rng.range_int(3, 8); i < n; ++i)
				for (uint8_t t : {TREASURE_FOOD_BIN, TREASURE_WOOD}) cluster(0.0f, 0.0f, t, 1, 7.0f);
			break;
		case MAP_POLAR_SEA:   // polar_sea.js and lower_nubia.js: 14 wood treasures in each base
		case MAP_LOWER_NUBIA:
		case MAP_LAND_GRAB:   // land_grab.js too
			base_ring(TREASURE_WOOD, 14);
			break;
		case MAP_FORTRESS: // fortress.js: 5 food, 5 wood, 4 metal and 2 stone treasures by each base
			base_ring(TREASURE_FOOD_BIN, 5);
			base_ring(TREASURE_WOOD, 5);
			base_ring(TREASURE_METAL, 4);
			base_ring(TREASURE_STONE, 2);
			break;
		case MAP_WILD_LAKE:          // wild_lake.js and caledonian_meadows.js: a food bin
		case MAP_CALEDONIAN_MEADOWS: // beside each civic centre
			if (!sim.nomad())
			for (const auto &sp : spawns) {
				const float tx = sp.x + sp.forward_dir_x * 6.0f, tz = sp.z + sp.forward_dir_z * 6.0f;
				if (dry(tx, tz)) sim.spawn_treasure(tx, tz, TREASURE_FOOD_BIN);
			}
			break;
		case MAP_DEEP_FOREST: // deep_forest.js: the unfinished Greek temple on the central hill
			sim.spawn_ruins(0.0f, 0.0f, ruins_type_by_name("unfinished_greek_temple"));
			break;
		// RTS0 landmark: a rich treasure on the summit of Jebel Barkal.
		default: break;
	}
}

// =========================================================================
// Maps modelled on 0 A.D. maps/random/{aegean_sea,archipelago,islands,
// cycladic_archipelago,snowflake_searocks,harbor}.js
// =========================================================================

namespace {

constexpr float kSeasPi = 3.14159265f;

// rmgen scaleByMapSize without rounding (0 A.D. returns a float).
inline float seas_scale(float a, float b, float half) {
	return a + (b - a) * std::max(0.0f, 2.0f * half - 128.0f) / 384.0f;
}

// Nearest grid vertex to a world point, clamped to the map.
inline int seas_vertex(const HeightmapGrid &hm, float wx, float wz) {
	const float sp = grid_spacing(hm);
	const int x = std::clamp((int)std::lround((wx + hm.map_half) / sp), 0, hm.res - 1);
	const int z = std::clamp((int)std::lround((wz + hm.map_half) / sp), 0, hm.res - 1);
	return z * hm.res + x;
}

inline void seas_world(const HeightmapGrid &hm, int i, float &wx, float &wz) { hm.grid_to_world(i % hm.res, i / hm.res, wx, wz); }

// Inside the circle a 0 A.D. circular map covers.
inline bool seas_in_circle(const HeightmapGrid &hm, int i) {
	float wx, wz;
	seas_world(hm, i, wx, wz);
	return wx * wx + wz * wz < hm.map_half * hm.map_half;
}

// ChainPlacer (rmgen placer/centered/ChainPlacer.js) on the vertex grid:
// circles of min_r..max_r tiles (from `queue` first, last element first),
// each centred on a random edge vertex of the area so far, edges kept within
// max_dist tiles (Chebyshev, 0 = no limit) of the centre. Vertices `allow`
// rejects are left out; more than fail_fraction of them fails the area
// (returns an empty mask).
template <class Allow>
std::vector<uint8_t> seas_chain(const HeightmapGrid &hm, RmsRng &rng, float cx, float cz, int min_r, int max_r, int circles,
		float fail_fraction, float max_dist, std::vector<int> queue, const Allow &allow) {
	const int res = hm.res;
	const float sp = grid_spacing(hm);
	const int start = seas_vertex(hm, cx, cz);
	if (!allow(start)) return {};
	min_r = std::min(max_r, std::max(min_r, 1));
	std::vector<int> state((size_t)res * (size_t)res, -1);
	std::vector<uint8_t> mask((size_t)res * (size_t)res, 0);
	std::vector<int> edges{start};
	const int sx = start % res, sz = start / res;
	int failed = 0, count = 0;
	for (int c = 0; c < circles && !edges.empty(); ++c) {
		const int e = edges[(size_t)rng.range_int(0, (int)edges.size() - 1)];
		int r;
		if (!queue.empty()) { r = queue.back(); queue.pop_back(); }
		else r = rng.range_int(min_r, max_r);
		const float rv = (float)r / sp, r2 = rv * rv;
		const int ex = e % res, ez = e / res, ri = (int)std::ceil(rv);
		const int x0 = std::max(0, ex - ri), x1 = std::min(res - 1, ex + ri);
		const int z0 = std::max(0, ez - ri), z1 = std::min(res - 1, ez + ri);
		for (int z = z0; z <= z1; ++z)
			for (int x = x0; x <= x1; ++x) {
				if ((float)((x - ex) * (x - ex) + (z - ez) * (z - ez)) >= r2) continue;
				++count;
				const int i = z * res + x;
				if (!allow(i)) { ++failed; continue; }
				int &st = state[(size_t)i];
				if (st == -1) {
					mask[(size_t)i] = 1;
					st = -2;
				} else if (st >= 0) {
					const int k0 = st;
					edges.erase(edges.begin() + k0);
					st = -2;
					for (size_t k = (size_t)k0; k < edges.size(); ++k) --state[(size_t)edges[k]];
				}
			}
		for (int z = z0; z <= z1; ++z)
			for (int x = x0; x <= x1; ++x) {
				if (max_dist > 0.0f && ((float)std::abs(x - sx) * sp > max_dist || (float)std::abs(z - sz) * sp > max_dist)) continue;
				const int i = z * res + x;
				if (state[(size_t)i] != -2) continue;
				if ((x > 0 && state[(size_t)i - 1] == -1) || (z > 0 && state[(size_t)(i - res)] == -1) ||
				    (x < res - 1 && state[(size_t)i + 1] == -1) || (z < res - 1 && state[(size_t)(i + res)] == -1)) {
					edges.push_back(i);
					state[(size_t)i] = (int)edges.size() - 1;
				}
			}
	}
	if ((float)failed > (float)count * fail_fraction) return {};
	return mask;
}

// ClumpPlacer (rmgen placer/centered/ClumpPlacer.js): a roughly round clump of
// about `size` tiles; coherence 1 is a circle, smoothness sets how many
// control points shape its border. Same failure rule as seas_chain.
template <class Allow>
std::vector<uint8_t> seas_clump(const HeightmapGrid &hm, RmsRng &rng, float size, float coherence, float smoothness,
		float fail_fraction, float cx, float cz, const Allow &allow) {
	const int res = hm.res;
	const float sp = grid_spacing(hm);
	const int start = seas_vertex(hm, cx, cz);
	if (!allow(start)) return {};
	const float radius = std::sqrt(size / kSeasPi) / sp;
	const float perim = 4.0f * radius * 2.0f * kSeasPi;
	const int int_perim = std::max(1, (int)std::ceil(perim));
	int ctrl = 1 + (int)std::floor(1.0f / std::max(smoothness, 1.0f / (float)int_perim));
	if ((float)ctrl > radius * 2.0f * kSeasPi) ctrl = (int)std::floor(radius * 2.0f * kSeasPi) + 1;
	ctrl = std::max(1, ctrl);
	std::vector<float> coords((size_t)ctrl + 1, 0.0f), vals((size_t)ctrl + 1, 0.0f), noise((size_t)int_perim, 0.0f);
	for (int i = 0; i < ctrl; ++i) {
		coords[(size_t)i] = (float)i * perim / (float)ctrl;
		vals[(size_t)i] = rng.range_float(0.0f, 2.0f);
	}
	int c = 0;
	bool looped = false;
	for (int i = 0; i < int_perim; ++i) {
		if (coords[(size_t)((c + 1) % ctrl)] < (float)i && !looped) {
			c = (c + 1) % ctrl;
			if (c == ctrl - 1) looped = true;
		}
		const float span = (looped ? perim : coords[(size_t)((c + 1) % ctrl)]) - coords[(size_t)c];
		const float t = span > 0.0f ? ((float)i - coords[(size_t)c]) / span : 0.0f;
		noise[(size_t)i] = cubic_weight(vals[(size_t)((c + ctrl - 1) % ctrl)], vals[(size_t)c], vals[(size_t)((c + 1) % ctrl)],
		                                vals[(size_t)((c + 2) % ctrl)], t);
	}
	std::vector<uint8_t> mask((size_t)res * (size_t)res, 0);
	int failed = 0, count = 0;
	const float sx = (float)(start % res), sz = (float)(start / res);
	for (int step = 0; step < int_perim; ++step) {
		const float a = 2.0f * kSeasPi * (float)step / perim;
		const float dx = det::sin(a), dz = det::cos(a);
		const int steps = (int)std::ceil(radius * (1.0f + (1.0f - coherence) * noise[(size_t)step]));
		count += steps;
		float px = sx, pz = sz;
		for (int s = 0; s < steps; ++s) {
			const int ix = (int)std::lround(px), iz = (int)std::lround(pz);
			const int i = iz * res + ix;
			if (ix >= 0 && iz >= 0 && ix < res && iz < res && allow(i)) mask[(size_t)i] = 1;
			else ++failed;
			px += dx;
			pz += dz;
		}
	}
	if ((float)failed > (float)count * fail_fraction) return {};
	return mask;
}

// PathPlacer (rmgen placer/noncentered/PathPlacer.js): a band `width` tiles
// wide from a to b, bent by smoothed noise (waviness, smoothness, offset) and
// tapered (width x (1 - t x tapering) along it). With fail_fraction 0 any
// rejected vertex fails the whole path.
template <class Allow>
std::vector<uint8_t> seas_path(const HeightmapGrid &hm, RmsRng &rng, float ax, float az, float bx, float bz, float width,
		float waviness, float smoothness, float offset, float tapering, float fail_fraction, const Allow &allow) {
	const int res = hm.res;
	const float sp = grid_spacing(hm), half = hm.map_half;
	const float len = std::max(1e-3f, det::hypot(bx - ax, bz - az));
	const int nw = 1 + (int)std::floor(len / 4.0f * waviness);
	const int nl = 1 + (int)std::floor(len / 4.0f * smoothness);
	const float off = 1.0f + std::floor(len / 4.0f * offset);
	std::vector<float> ctrl((size_t)nw, 0.0f);
	for (int j = 1; j < nw - 1; ++j) ctrl[(size_t)j] = rng.range_float(-off, off);
	const int total = nw * nl;
	std::vector<float> noise((size_t)total + 1, 0.0f);
	for (int j = 0; j < nw; ++j)
		for (int k = 0; k < nl; ++k)
			noise[(size_t)(j * nl + k)] = cubic_weight(ctrl[(size_t)((j + nw - 1) % nw)], ctrl[(size_t)j], ctrl[(size_t)((j + 1) % nw)],
			                                           ctrl[(size_t)((j + 2) % nw)], (float)k / (float)nl);
	const float ux = (bx - ax) / len, uz = (bz - az) / len, px = -uz, pz = ux;
	std::vector<uint8_t> mask((size_t)res * (size_t)res, 0);
	int failed = 0;
	for (int j = 0; j < total; ++j) {
		const float s1 = (float)j / (float)total, s2 = (float)(j + 1) / (float)total;
		const float p1x = ax + (bx - ax) * s1 + px * noise[(size_t)j], p1z = az + (bz - az) * s1 + pz * noise[(size_t)j];
		const float p2x = ax + (bx - ax) * s2 + px * noise[(size_t)j + 1], p2z = az + (bz - az) * s2 + pz * noise[(size_t)j + 1];
		const float w = std::max(0.0f, (1.0f - s1 * tapering) * width / 2.0f);
		const float sl2 = (p2x - p1x) * (p2x - p1x) + (p2z - p1z) * (p2z - p1z);
		const int x0 = std::max(0, (int)std::floor((std::min(p1x, p2x) - w + half) / sp));
		const int x1 = std::min(res - 1, (int)std::ceil((std::max(p1x, p2x) + w + half) / sp));
		const int z0 = std::max(0, (int)std::floor((std::min(p1z, p2z) - w + half) / sp));
		const int z1 = std::min(res - 1, (int)std::ceil((std::max(p1z, p2z) + w + half) / sp));
		for (int z = z0; z <= z1; ++z)
			for (int x = x0; x <= x1; ++x) {
				const float wx = -half + (float)x * sp, wz = -half + (float)z * sp;
				float t = sl2 > 0.0f ? ((wx - p1x) * (p2x - p1x) + (wz - p1z) * (p2z - p1z)) / sl2 : 0.0f;
				// The path's quads (ConvexPolygonPlacer) end square at a and b: no round caps.
				if ((j == 0 && t < 0.0f) || (j == total - 1 && t > 1.0f)) continue;
				t = std::clamp(t, 0.0f, 1.0f);
				const float qx = p1x + (p2x - p1x) * t, qz = p1z + (p2z - p1z) * t;
				if ((wx - qx) * (wx - qx) + (wz - qz) * (wz - qz) > w * w) continue;
				const int i = z * res + x;
				if (!allow(i)) {
					if (fail_fraction <= 0.0f) return {};
					++failed;
					continue;
				}
				mask[(size_t)i] = 1;
			}
	}
	if ((float)failed > fail_fraction * width * len) return {};
	return mask;
}

// SmoothElevationPainter(ELEVATION_MODIFY, dh, blend) over `mask`.
void seas_paint_modify(HeightmapGrid &hm, const std::vector<uint8_t> &mask, float dh, float blend) {
	const auto d = distance_inside_mask(mask, hm.res, grid_spacing(hm));
	for (size_t i = 0; i < mask.size(); ++i)
		if (mask[i]) hm.data[i] += dh * std::clamp(d[i] / std::max(blend, 1e-3f), 0.0f, 1.0f);
}

inline bool seas_any(const std::vector<uint8_t> &m) {
	for (uint8_t v : m) if (v) return true;
	return false;
}

inline void seas_add(std::vector<uint8_t> &dst, const std::vector<uint8_t> &src) {
	for (size_t i = 0; i < src.size(); ++i) if (src[i]) dst[i] = 1;
}

// Distance in tiles from every vertex to the nearest point in `pts` minus r
// (the player tile class: a disk of radius r round each base).
std::vector<float> seas_dist_to_players(const HeightmapGrid &hm, const std::vector<std::pair<float, float>> &pts, float r) {
	std::vector<float> d(hm.data.size(), 1e9f);
	for (size_t i = 0; i < d.size(); ++i) {
		float wx, wz;
		seas_world(hm, (int)i, wx, wz);
		for (const auto &p : pts) d[i] = std::min(d[i], det::hypot(wx - p.first, wz - p.second) - r);
	}
	return d;
}

// Distance to the nearest vertex outside `mask` (stayClasses) or inside it
// (avoidClasses), by distance_to_mask.
std::vector<float> seas_dist_outside(const HeightmapGrid &hm, const std::vector<uint8_t> &mask) {
	std::vector<uint8_t> out(mask.size());
	for (size_t i = 0; i < mask.size(); ++i) out[i] = !mask[i];
	return distance_to_mask(out, hm.res, grid_spacing(hm));
}
inline std::vector<float> seas_dist_inside(const HeightmapGrid &hm, const std::vector<uint8_t> &mask) {
	return distance_to_mask(mask, hm.res, grid_spacing(hm));
}

// g_Map.randomCoordinate on a circular map: a point inside the circle.
std::pair<float, float> seas_random_point(const HeightmapGrid &hm, RmsRng &rng) {
	const float half = hm.map_half;
	for (int k = 0; k < 64; ++k) {
		const float x = rng.range_float(-half, half), z = rng.range_float(-half, half);
		if (x * x + z * z < half * half) return {x, z};
	}
	return {0.0f, 0.0f};
}

// playerPlacementCircle: players sorted by team (sortAllPlayers) on a circle.
std::vector<std::pair<float, float>> seas_circle_players(int n, float start, float r, const uint8_t *teams) {
	std::vector<int> order((size_t)n);
	for (int i = 0; i < n; ++i) order[(size_t)i] = i;
	if (teams) std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return teams[a] < teams[b]; });
	std::vector<std::pair<float, float>> locs((size_t)n);
	for (int k = 0; k < n; ++k) locs[(size_t)order[(size_t)k]] = polar(start + 2.0f * kSeasPi * (float)k / (float)n, r);
	return locs;
}

// rndRiver (gaia_terrain.js), in double like the JS original.
double seas_rnd_river(double f, double seed) {
	double rw = seed;
	for (int i = 0; (double)i <= f; ++i) rw = 10.0 * std::fmod(rw, 1.0);
	const double rr = std::fmod(f, 1.0);
	double ret = (((long)std::floor(f)) % 2 ? -1.0 : 1.0) * rr * (rr - 1.0);
	switch (((long)std::floor(rw)) % 5) {
		case 0: ret *= 2.3 * (rr - 0.5) * (rr - 0.5); break;
		case 1: ret *= 2.6 * (rr - 0.3) * (rr - 0.7); break;
		case 2: ret *= 22.0 * (rr - 0.2) * (rr - 0.3) * (rr - 0.3) * (rr - 0.8); break;
		case 3: ret *= 180.0 * (rr - 0.2) * (rr - 0.2) * (rr - 0.4) * (rr - 0.6) * (rr - 0.6) * (rr - 0.8); break;
		default: ret *= 2.6 * (rr - 0.5) * (rr - 0.7); break;
	}
	return ret;
}

// paintRiver (gaia_terrain.js), banks meandering independently (or together when
// `parallel`), no deviation: the water between the banks gets `bed`, rising to `land` over
// `fade` tiles at each bank.
void seas_paint_river(HeightmapGrid &hm, RmsRng &rng, float sx, float sz, float ex, float ez, float width, float fade,
		float bed, float land, float meander_short, float meander_long, bool parallel = false) {
	const float half = hm.map_half, size = 2.0f * half;
	const double ms = size * meander_short / seas_scale(35.0f, 160.0f, half);
	const double ml = size * meander_long / seas_scale(35.0f, 100.0f, half);
	const double seed1 = rng.range_float(2.0f, 3.0f), seed2_draw = rng.range_float(2.0f, 3.0f);
	const double ang1 = rng.range_float(0.0f, 1.0f), ang2_draw = rng.range_float(0.0f, 1.0f);
	// "parallel": both banks follow the first bank's curve, so the river keeps its width.
	const double seed2 = parallel ? seed1 : seed2_draw, ang2 = parallel ? ang1 : ang2_draw;
	auto curve = [&](double frac, double a, double seed) {
		return ms * seas_rnd_river(a + size * frac / 128.0, seed) + ml * seas_rnd_river(a + size * frac / 256.0, seed);
	};
	const float len = det::hypot(sx - ex, sz - ez);
	const float ux = (sx - ex) / len, uz = (sz - ez) / len;
	for (int z = 0; z < hm.res; ++z)
		for (int x = 0; x < hm.res; ++x) {
			float wx, wz;
			hm.grid_to_world(x, z, wx, wz);
			const float d = ux * (wz - ez) - uz * (wx - ex);
			const float along = (wx - ex) * ux + (wz - ez) * uz; // from the end towards the start
			if (along < 0.0f || along > len) continue;
			const double frac = (len - along) / len;
			const double c1 = curve(frac, ang1, seed1), c2 = curve(frac, ang2, seed2);
			const double sd1 = c1 + d - width / 2.0f, sd2 = c2 + d + width / 2.0f;
			if (!(sd1 < 0.0 && sd2 > 0.0)) continue;
			double h = bed;
			if (sd1 > -fade) h += (land - bed) * (1.0 + sd1 / fade);
			else if (sd2 < fade) h += (land - bed) * (1.0 - sd2 / fade);
			hm.set(x, z, (float)h);
		}
}

// createAreas: up to amount x retry tries at random centres.
template <class Place>
int seas_create_areas(const HeightmapGrid &hm, RmsRng &rng, float amount, int retry, const Place &place) {
	int placed = 0;
	for (int tries = 0; (float)placed < amount && (float)tries < amount * (float)retry; ++tries) {
		const auto c = seas_random_point(hm, rng);
		if (place(c.first, c.second)) ++placed;
	}
	return placed;
}

// createBumps: ChainPlacer(1, floor(scaleByMapSize(4, 6)), floor(scaleByMapSize(2, 5)), 0) raising 2 m.
template <class Allow>
void seas_bumps(HeightmapGrid &hm, RmsRng &rng, float amount, const Allow &allow) {
	const float half = hm.map_half;
	seas_create_areas(hm, rng, amount, 10, [&](float x, float z) {
		const auto m = seas_chain(hm, rng, x, z, 1, (int)std::floor(seas_scale(4, 6, half)), (int)std::floor(seas_scale(2, 5, half)), 0.0f, 0.0f, {}, allow);
		if (!seas_any(m)) return false;
		seas_paint_modify(hm, m, 2.0f * kMetre, 2.0f);
		return true;
	});
}

// createHills (18 m, ChainPlacer(1, floor(scaleByMapSize(4, 6)),
// floor(scaleByMapSize(16, 40)), 0.5), smoothing hill_blend) or createMountains
// (cones of scaleByMapSize(30, 50) m, or mountain_m when set, from circles of floor(scaleByMapSize(3, 4)) to
// floor(scaleByMapSize(6, 12)) tiles), avoiding each other by `spacing`.
// `allow` is the script's other constraints.
template <class Allow>
void seas_hills(HeightmapGrid &hm, RmsRng &rng, float water_h, float amount, bool mountains, float spacing, float hill_m,
		const Allow &allow, float hill_blend = 2.0f, float mountain_m = 0.0f) {
	const float half = hm.map_half;
	std::vector<uint8_t> hills(hm.data.size(), 0);
	std::vector<float> to_hill(hm.data.size(), 1e9f);
	auto ok = [&](int i) { return allow(i) && to_hill[(size_t)i] > spacing; };
	seas_create_areas(hm, rng, amount, 10, [&](float x, float z) {
		std::vector<uint8_t> m;
		if (mountains)
			m = seas_chain(hm, rng, x, z, (int)std::floor(seas_scale(3, 4, half)), (int)std::floor(seas_scale(6, 12, half)),
			               (int)std::floor(seas_scale(4, 10, half)), 0.5f, 0.0f, {}, ok);
		else
			m = seas_chain(hm, rng, x, z, 1, (int)std::floor(seas_scale(4, 6, half)), (int)std::floor(seas_scale(16, 40, half)), 0.5f, 0.0f, {}, ok);
		if (!seas_any(m)) return false;
		if (mountains)
			paint_mask_elevation(hm, m, metres(water_h, mountain_m > 0.0f ? mountain_m : std::floor(seas_scale(30, 50, half))),
			                     (float)std::floor(seas_scale(6, 12, half)));
		else paint_mask_elevation(hm, m, metres(water_h, hill_m), hill_blend);
		record_area(hm, m, mountains ? AREA_MOUNTAIN : AREA_HILL);
		seas_add(hills, m);
		to_hill = seas_dist_inside(hm, hills);
		return true;
	});
}

// Flattens each start, keeps it at least at `min_h`, and faces the centre.
void seas_spawns(HeightmapGrid &hm, std::vector<RmsGenerator::PlayerSpawn> &spawns, const std::vector<std::pair<float, float>> &locs,
		float radius, float blend, float water_h, float min_h) {
	make_heightmap_spawns(hm, spawns, locs, radius, blend, water_h);
	for (auto &s : spawns) s.base_height = std::max(s.base_height, min_h);
	for (const auto &s : spawns) lift_area(hm, s.x, s.z, radius, blend, s.base_height);
}

// createMountain (rmgen-common/gaia_terrain.js) on the vertex grid. Up to `circles` circles of
// min_r..max_r tiles, each centred on a random edge of the ones before; the script's "edges"
// are covered tiles whose four neighbours are all covered, kept within 14 tiles of the start.
// Each circle's square box is checked against `allow` (the script compares a distance with the
// squared radius), and a circle meeting a refused vertex is dropped. Each kept circle then sets
// its disk to randInt(0, 2) + round(2/3 c (sin(2/3 pi (3/4 - d/r)) + 0.5)) metres above ref_h,
// with c = r / max_r x max_h x randFloat(0.8, 1.2); ground already that high is set 4 m above
// it while the start vertex is below that. Returns the disk vertices (the tile class).
template <class Allow>
std::vector<uint8_t> mountain_028(HeightmapGrid &hm, RmsRng &rng, float ref_h, float max_h, int min_r, int max_r, int circles,
		float x, float z, const Allow &allow) {
	const int res = hm.res;
	const float sp = grid_spacing(hm);
	const int start = seas_vertex(hm, x, z);
	if (!allow(start)) return {};
	const int ox = start % res, oz = start / res, fcc = (int)std::lround(14.0f / sp);
	auto in_map = [&](int i) { return !t_circular_map || seas_in_circle(hm, i); };
	std::vector<int> got((size_t)res * (size_t)res, -1);
	std::vector<std::pair<int, int>> edges = {{ox, oz}};
	struct Circle { int x, z, r; };
	std::vector<Circle> kept;
	min_r = std::max(1, std::min(min_r, max_r));
	for (int c = 0; c < circles && !edges.empty(); ++c) {
		const auto e = edges[rng.next_u32() % (uint32_t)edges.size()];
		const int cx = e.first, cz = e.second, r = rng.range_int(min_r, max_r);
		const int rv = std::max(1, (int)std::lround((float)r / sp));
		const int sx = std::max(0, cx - rv), sz = std::max(0, cz - rv), lx = std::min(cx + rv, res - 1), lz = std::min(cz + rv, res - 1);
		bool bad = false;
		for (int ix = sx; ix <= lx && !bad; ++ix)
			for (int iz = sz; iz <= lz; ++iz) {
				const int i = iz * res + ix;
				if (!in_map(i)) continue;
				if (!allow(i)) { bad = true; break; }
				const int s = got[(size_t)i];
				if (s >= 0) {
					edges.erase(edges.begin() + s);
					for (size_t k = (size_t)s; k < edges.size(); ++k) --got[(size_t)(edges[k].second * res + edges[k].first)];
				}
				got[(size_t)i] = -2;
			}
		if (bad) continue;
		kept.push_back({cx, cz, r});
		for (int ix = sx; ix <= lx; ++ix)
			for (int iz = sz; iz <= lz; ++iz) {
				const int i = iz * res + ix;
				if (got[(size_t)i] != -2 || std::abs(ox - ix) > fcc || std::abs(oz - iz) > fcc) continue;
				if ((ix > 0 && got[(size_t)i - 1] == -1) || (iz > 0 && got[(size_t)(i - res)] == -1) ||
				    (ix < res - 1 && got[(size_t)i + 1] == -1) || (iz < res - 1 && got[(size_t)(i + res)] == -1))
					continue;
				edges.push_back({ix, iz});
				got[(size_t)i] = (int)edges.size() - 1;
			}
	}
	std::vector<uint8_t> out(hm.data.size(), 0);
	for (const Circle &c : kept) {
		const int rv = std::max(1, (int)std::lround((float)c.r / sp));
		const int sx = std::max(0, c.x - rv), sz = std::max(0, c.z - rv), lx = std::min(c.x + rv, res - 1), lz = std::min(c.z + rv, res - 1);
		const float clump = (float)c.r / (float)max_r * max_h * rng.range_float(0.8f, 1.2f);
		for (int ix = sx; ix <= lx; ++ix)
			for (int iz = sz; iz <= lz; ++iz) {
				const int i = iz * res + ix;
				const float d = det::hypot((float)(ix - c.x), (float)(iz - c.z)) * sp;
				const float nh = (float)rng.range_int(0, 2) +
				                 std::round(2.0f / 3.0f * clump * (det::sin(kSeasPi * 2.0f / 3.0f * (0.75f - d / (float)c.r)) + 0.5f));
				if (d > (float)c.r || !in_map(i)) continue;
				const float cur = (hm.data[(size_t)i] - ref_h) / kMetre, origin = (hm.data[(size_t)start] - ref_h) / kMetre;
				if (cur < nh) hm.data[(size_t)i] = metres(ref_h, nh);
				else if (origin < nh + 4.0f) hm.data[(size_t)i] = metres(ref_h, nh + 4.0f);
				out[(size_t)i] = 1;
			}
	}
	return out;
}

// createMountains: `count` createMountain tries at random points, `spacing` tiles clear of the
// mountains (and hills) already in `hills`, which each one joins.
template <class Allow>
void mountains_028(HeightmapGrid &hm, RmsRng &rng, float ref_h, float count, float max_h, int min_r, int max_r, int circles,
		float spacing, std::vector<uint8_t> &hills, const Allow &allow) {
	std::vector<float> to_hill = seas_any(hills) ? seas_dist_inside(hm, hills) : std::vector<float>(hm.data.size(), 1e9f);
	for (int i = 0; (float)i < count; ++i) {
		const auto p = seas_random_point(hm, rng);
		const auto m = mountain_028(hm, rng, ref_h, max_h, min_r, max_r, circles, p.first, p.second,
		                            [&](int k) { return allow(k) && to_hill[(size_t)k] > spacing; });
		if (!seas_any(m)) continue;
		record_area(hm, m, AREA_MOUNTAIN);
		seas_add(hills, m);
		to_hill = seas_dist_inside(hm, hills);
	}
}

// The default createMountains (scaleByMapSize(30, 50) m, circles of floor(scaleByMapSize(3, 4))
// to floor(scaleByMapSize(6, 12)) tiles, floor(scaleByMapSize(4, 10)) of them).
template <class Allow>
void default_mountains_028(HeightmapGrid &hm, RmsRng &rng, float ref_h, float count, float spacing, const Allow &allow) {
	const float half = hm.map_half;
	std::vector<uint8_t> hills(hm.data.size(), 0);
	mountains_028(hm, rng, ref_h, count, std::floor(seas_scale(30, 50, half)), (int)std::floor(seas_scale(3, 4, half)),
	              (int)std::floor(seas_scale(6, 12, half)), (int)std::floor(seas_scale(4, 10, half)), spacing, hills, allow);
}

// The players of a synth that placed them with place_team_spawns, as points.
std::vector<std::pair<float, float>> spawn_points(const std::vector<RmsGenerator::PlayerSpawn> &spawns) {
	std::vector<std::pair<float, float>> locs;
	for (const auto &s : spawns) locs.push_back({s.x, s.z});
	return locs;
}

} // namespace

void RmsGenerator::synth_rivers(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// rivers.js (0.28), in metres: land at 1 m, players on a circle at fractionToTiles(0.35), a
	// central lake (ClumpPlacer(diskArea(fractionToTiles(0.075)), 0.7, 0.1), -3 m, blend 4), and
	// between each pair of neighbouring non-allies a paintRiver from the map edge to the centre
	// (scaleByMapSize(10, 30) wide, fade 5, bed -3 m, meanderShort 10, parallel banks) that only
	// deepens the ground, with a -1 m shallow 0.12-0.21 of its length long starting 0.2-0.7 of the
	// way in. Then bumps 2 from the water and 20 from the players, and hills (18 m) or mountains,
	// scaleByMapSize(3, 15) of them, 20 from the players, 15 apart and 2 from the water.
	water_h = 1.0f;
	const float half = hm.map_half, size = 2.0f * half;
	auto M = [&](float m) { return coast_metres(water_h, m); };
	const float land = M(1.0f);
	hm.init(hm.res, half, land);
	const float start = rng.range_float(0.0f, 2.0f * kSeasPi);
	const uint8_t *teams = t_team_placement.teams;
	const auto locs = seas_circle_players(num_players, start, 0.35f * size, teams);
	const auto to_player = seas_dist_to_players(hm, locs, seas_scale(15, 25, half) / 3.0f);
	const float lake_r = 0.075f * size;
	auto water = seas_clump(hm, rng, kSeasPi * lake_r * lake_r, 0.7f, 0.1f, 1e9f, 0.0f, 0.0f, [](int) { return true; });
	paint_mask_elevation(hm, water, M(-3.0f), 4.0f);
	record_area(hm, water, AREA_LAKE);
	// The players round the circle in seas_circle_players' order, for areAllies.
	std::vector<int> order((size_t)num_players);
	for (int i = 0; i < num_players; ++i) order[(size_t)i] = i;
	std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return teams[a] < teams[b]; });
	const float width = seas_scale(10, 30, half), fade = 5.0f;
	const double meander = size * 10.0 / seas_scale(35.0f, 160.0f, half);
	for (int i = 0; i < num_players; ++i) {
		const int a = order[(size_t)i], b = order[(size_t)((i + 1) % num_players)];
		if (a == b || (teams[a] && teams[a] == teams[b])) continue;
		const float shallow_at = rng.range_float(0.2f, 0.7f);
		const float shallow_w = rng.range_float(0.12f, 0.21f);
		const auto s = polar(start + kSeasPi / (float)num_players + 2.0f * kSeasPi * (float)i / (float)num_players, half);
		const double seed = rng.range_float(2.0f, 3.0f);
		rng.range_float(2.0f, 3.0f); // seed2: unused with parallel banks
		const double angle = rng.range_float(0.0f, 1.0f);
		rng.range_float(0.0f, 1.0f); // startingAngle2
		const float len = std::max(1e-3f, det::hypot(s.first, s.second));
		const float ux = s.first / len, uz = s.second / len; // from the centre (the end) towards the start
		for (int z = 0; z < hm.res; ++z)
			for (int x = 0; x < hm.res; ++x) {
				float wx, wz;
				hm.grid_to_world(x, z, wx, wz);
				const float along = wx * ux + wz * uz;
				if (along < 0.0f || along > len) continue;
				const double frac = (len - along) / len; // 0 at the map edge
				const double curve = meander * seas_rnd_river(angle + size * frac / 128.0, seed);
				const float d = ux * wz - uz * wx;
				const double sd1 = curve + d - width / 2.0f, sd2 = curve + d + width / 2.0f;
				if (!(sd1 < 0.0 && sd2 > 0.0)) continue;
				double m = -3.0;
				if (sd1 > -fade) m += 4.0 * (1.0 + sd1 / fade);
				else if (sd2 < fade) m += 4.0 * (1.0 - sd2 / fade);
				const size_t k = (size_t)z * (size_t)hm.res + (size_t)x;
				water[k] = 1;
				const bool shallow = m < -1.0 && frac > shallow_at && frac < shallow_at + shallow_w;
				const float h = M(shallow ? -1.0f : (float)m);
				if (hm.data[k] >= h) hm.data[k] = h;
			}
	}
	const auto to_water = seas_dist_inside(hm, water);
	seas_bumps(hm, rng, seas_scale(100, 200, half), [&](int i) { return to_water[(size_t)i] > 2.0f && to_player[(size_t)i] > 20.0f; });
	const auto away = [&](int i) { return to_player[(size_t)i] > 20.0f && to_water[(size_t)i] > 2.0f; };
	if (rng.range_int(0, 1) == 0) seas_hills(hm, rng, water_h, seas_scale(3, 15, half), false, 15.0f, 18.0f, away);
	else default_mountains_028(hm, rng, water_h, seas_scale(3, 15, half), 15.0f, away);
	seas_spawns(hm, spawns, locs, 16.0f, 6.0f, water_h, land);
}

void RmsGenerator::synth_mainland(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// mainland.js (0.28), in 0 A.D. metres: land at heightLand 3 m and no water; players by
	// playerPlacementByPattern at fractionToTiles(0.35), teammates fractionToTiles(0.1) apart;
	// createBumps 20 from the players, then randBool: createHills (18 m) or createMountains
	// (30-50 m), scaleByMapSize(3, 15) of them, 20 from the players and 15 from each other.
	water_h = -10.0f;
	const float half = hm.map_half, size = 2.0f * half;
	hm.init(hm.res, half, metres(0.0f, 3.0f));
	place_team_spawns(hm, rng, spawns, num_players, 0.35f * size, rng.range_float(0.0f, 2.0f * kSeasPi), 0.0f);
	const auto to_player = seas_dist_to_players(hm, spawn_points(spawns), seas_scale(15, 25, half) / 3.0f);
	const auto away = [&](int i) { return to_player[(size_t)i] > 20.0f; };
	seas_bumps(hm, rng, seas_scale(100, 200, half), away);
	if (rng.range_int(0, 1) == 0) seas_hills(hm, rng, 0.0f, seas_scale(3, 15, half), false, 15.0f, 18.0f, away);
	else default_mountains_028(hm, rng, 0.0f, seas_scale(3, 15, half), 15.0f, away);
}

void RmsGenerator::synth_atlas_mountains(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// atlas_mountains.js (0.28), in 0 A.D. metres: land at heightLand 3 m and no water; players by
	// playerPlacementByPattern at fractionToTiles(0.35), teammates fractionToTiles(0.1) apart;
	// createBumps 9 from the players, then scaleByMapSize(20, 120) createMountains (30-50 m) 20
	// from the players and 8 from each other.
	water_h = -10.0f;
	const float half = hm.map_half, size = 2.0f * half;
	hm.init(hm.res, half, metres(0.0f, 3.0f));
	place_team_spawns(hm, rng, spawns, num_players, 0.35f * size, rng.range_float(0.0f, 2.0f * kSeasPi), 0.0f);
	const auto to_player = seas_dist_to_players(hm, spawn_points(spawns), seas_scale(15, 25, half) / 3.0f);
	seas_bumps(hm, rng, seas_scale(100, 200, half), [&](int i) { return to_player[(size_t)i] > 9.0f; });
	default_mountains_028(hm, rng, 0.0f, seas_scale(20, 120, half), 8.0f, [&](int i) { return to_player[(size_t)i] > 20.0f; });
}

void RmsGenerator::synth_syria(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// syria.js (0.28), in 0 A.D. metres: land at heightLand 1 m and no water; players by
	// playerPlacementByPattern at fractionToTiles(0.35), teammates fractionToTiles(0.1) apart, each
	// in clPlayer's disk of defaultPlayerBaseRadius and a ChainPlacer of grass round it (AREA_GRASS:
	// desert_grass_a_sand 3 tiles in, desert_grass_a inside); scaleByMapSize(300, 800) 2 m bumps 13
	// from the players, then scaleByMapSize(1, 3) * numPlayers * 3 hills set to heightHill 22, 3 from
	// the players, 1 from the grass and 10 from each other.
	water_h = -10.0f;
	const float half = hm.map_half, size = 2.0f * half;
	hm.init(hm.res, half, metres(0.0f, 1.0f));
	place_team_spawns(hm, rng, spawns, num_players, 0.35f * size, rng.range_float(0.0f, 2.0f * kSeasPi), 0.0f);
	const auto to_player = seas_dist_to_players(hm, spawn_points(spawns), seas_scale(15, 25, half));
	std::vector<uint8_t> grass(hm.data.size(), 0);
	for (const auto &sp : spawns) {
		const auto m = seas_chain(hm, rng, sp.x, sp.z, 2, (int)std::floor(seas_scale(5, 12, half)), (int)std::floor(seas_scale(25, 60, half)),
		                          1e9f, 0.0f, {(int)std::floor(seas_scale(16, 30, half))}, [](int) { return true; });
		record_area(hm, m, AREA_GRASS);
		seas_add(grass, m);
	}
	const auto to_grass = seas_dist_inside(hm, grass);
	seas_create_areas(hm, rng, seas_scale(300, 800, half), 10, [&](float x, float z) {
		const auto m = seas_clump(hm, rng, seas_scale(20, 50, half), 0.3f, 0.06f, 1e9f, x, z, [&](int i) { return to_player[(size_t)i] > 13.0f; });
		if (!seas_any(m)) return false;
		seas_paint_modify(hm, m, 2.0f * kMetre, 2.0f);
		return true;
	});
	seas_hills(hm, rng, 0.0f, seas_scale(1, 3, half) * (float)std::max(1, num_players) * 3.0f, false, 10.0f, 22.0f,
	           [&](int i) { return to_player[(size_t)i] > 3.0f && to_grass[(size_t)i] > 1.0f; });
}

void RmsGenerator::synth_volcanic_lands(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// volcanic_lands.js (0.28), in 0 A.D. metres: land at heightLand 1 m and no water; players by
	// playerPlacementByPattern at fractionToTiles(0.35), teammates fractionToTiles(0.1) apart; rmgen
	// createVolcano in the centre (ClumpPlacers of diskArea(scaleByMapSize(18, 25)) at 15 m,
	// (16, 23) at 25 m, (10, 15) at 45 m and (8, 11) at 62 m, each inside the last, and the lava
	// crater (4, 6) at 42 m); then scaleByMapSize(2, 8) * numPlayers hills (ClumpPlacer of
	// scaleByMapSize(20, 150) tiles set to heightHill 18) 12 from the players and 15 from other hills.
	water_h = -10.0f;
	const float half = hm.map_half, size = 2.0f * half;
	hm.init(hm.res, half, metres(0.0f, 1.0f));
	place_team_spawns(hm, rng, spawns, num_players, 0.35f * size, rng.range_float(0.0f, 2.0f * kSeasPi), 0.0f);
	const auto to_player = seas_dist_to_players(hm, spawn_points(spawns), 5.0f);
	const float layers[5][4] = {{18, 25, 15, 3}, {16, 23, 25, 3}, {10, 15, 45, 3}, {8, 11, 62, 3}, {4, 6, 42, 1}};
	std::vector<uint8_t> prev, hills;
	for (int i = 0; i < 5; ++i) {
		const float r = seas_scale(layers[i][0], layers[i][1], half);
		std::vector<float> to_prev;
		if (i > 0) to_prev = distance_inside_mask(prev, hm.res, grid_spacing(hm));
		// stayClasses(previous layer, 1): at least a tile inside it.
		auto m = seas_clump(hm, rng, kSeasPi * r * r, 0.7f, 0.05f, 1e9f, 0.0f, 0.0f,
		                    [&](int k) { return i == 0 || (prev[(size_t)k] && to_prev[(size_t)k] > 1.0f); });
		if (!seas_any(m)) break;
		paint_mask_elevation(hm, m, metres(0.0f, layers[i][2]), layers[i][3]);
		record_area(hm, m, i == 4 ? AREA_VOLCANO : AREA_MOUNTAIN);
		if (i == 0) hills = m;
		prev = std::move(m);
	}
	std::vector<float> to_hill = seas_dist_inside(hm, hills);
	seas_create_areas(hm, rng, seas_scale(2, 8, half) * (float)std::max(1, num_players), 10, [&](float x, float z) {
		const auto m = seas_clump(hm, rng, seas_scale(20, 150, half), 0.2f, 0.1f, 1e9f, x, z,
		                          [&](int k) { return to_player[(size_t)k] > 12.0f && to_hill[(size_t)k] > 15.0f; });
		if (!seas_any(m)) return false;
		paint_mask_elevation(hm, m, metres(0.0f, 18.0f), 2.0f);
		record_area(hm, m, AREA_HILL);
		seas_add(hills, m);
		to_hill = seas_dist_inside(hm, hills);
		return true;
	});
	// 0.28 places the bases before the volcano, which can rise under them on small maps with the
	// River pattern (players 0.175 of the map from the centre): each base keeps its flat ground.
	for (const auto &sp : spawns) hm.level_area(sp.x, sp.z, 14.0f, 6.0f, sp.base_height);
}

void RmsGenerator::synth_alpine_lakes(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// alpine_lakes.js (0.28), in 0 A.D. metres: land at 3 m; players by playerPlacementByPattern at
	// fractionToTiles(0.35), teammates fractionToTiles(0.1) apart; scaleByMapSize(10, 40) x players
	// createMountains of floor(scaleByMapSize(40, 60)) m (circles of floor(scaleByMapSize(4, 5))
	// to floor(scaleByMapSize(7, 15)) tiles, floor(scaleByMapSize(5, 15)) of them) 20 from the
	// players and 8 apart; scaleByMapSize(5, 16) lakes (ChainPlacer(1, floor(scaleByMapSize(4, 8)),
	// floor(scaleByMapSize(40, 180)), 0.7), floor -5 m, blend 5, one try each) 20 from the
	// players and 8 apart; then bumps 2 from the water and 20 from the players.
	water_h = 1.0f;
	const float half = hm.map_half, size = 2.0f * half;
	const float land = metres(water_h, 3.0f);
	hm.init(hm.res, half, land);
	place_team_spawns(hm, rng, spawns, num_players, 0.35f * size, rng.range_float(0.0f, 2.0f * kSeasPi), land);
	const auto to_player = seas_dist_to_players(hm, spawn_points(spawns), seas_scale(15, 25, half) / 3.0f);
	const auto away = [&](int i) { return to_player[(size_t)i] > 20.0f; };
	std::vector<uint8_t> hills(hm.data.size(), 0), water(hm.data.size(), 0);
	mountains_028(hm, rng, water_h, seas_scale(10, 40, half) * (float)num_players, std::floor(seas_scale(40, 60, half)),
	              (int)std::floor(seas_scale(4, 5, half)), (int)std::floor(seas_scale(7, 15, half)), (int)std::floor(seas_scale(5, 15, half)),
	              8.0f, hills, away);
	std::vector<float> to_water(hm.data.size(), 1e9f);
	seas_create_areas(hm, rng, seas_scale(5, 16, half), 1, [&](float x, float z) {
		const auto m = seas_chain(hm, rng, x, z, 1, (int)std::floor(seas_scale(4, 8, half)), (int)std::floor(seas_scale(40, 180, half)), 0.7f,
		                          0.0f, {}, [&](int i) { return away(i) && to_water[(size_t)i] > 8.0f; });
		if (!seas_any(m)) return false;
		paint_mask_elevation(hm, m, metres(water_h, -5.0f), 5.0f);
		record_area(hm, m, AREA_LAKE);
		seas_add(water, m);
		to_water = seas_dist_inside(hm, water);
		return true;
	});
	seas_bumps(hm, rng, seas_scale(100, 200, half), [&](int i) { return to_water[(size_t)i] > 2.0f && away(i); });
}

void RmsGenerator::synth_canyon(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// canyon.js: the map starts as high ground (heightHill 30 m) and the land (3 m) is cut into
	// it: a canyon round each base (ClumpPlacer(diskArea(scaleByMapSize(18, 32)))), a central
	// basin (radius f = 0.16), wild clearings (f = 0.06, 6 apart) each joined to the two nearest
	// bases, and a road from every base to its neighbour and to the centre. Ridges of 30 m
	// (PathPlacer) cross the map first, kept 6 off the bases. Players on a circle at f = 0.35.
	water_h = -10.0f;
	const float half = hm.map_half, size = 2.0f * half;
	const float land = 3.0f * kMetre, hill = 30.0f * kMetre;
	hm.init(hm.res, hm.map_half, hill);
	const size_t nv = (size_t)hm.res * (size_t)hm.res;
	const float sp = grid_spacing(hm);
	std::vector<uint8_t> cl_land(nv, 0), cl_player(nv, 0), cl_hill(nv, 0), cl_hill2(nv, 0);
	auto paint = [&](const std::vector<uint8_t> &m, float h, float blend, std::vector<uint8_t> *cls) {
		if (m.empty()) return;
		paint_mask_elevation(hm, m, h, blend);
		if (cls) seas_add(*cls, m);
	};
	const auto anywhere = [](int) { return true; };
	auto disk = [](float r) { return 3.14159265f * r * r; };
	const float canyon_r = seas_scale(18.0f, 32.0f, half);
	const auto locs = seas_circle_players(num_players, rng.range_float(0.0f, 2.0f * kSeasPi), 0.35f * size, nullptr);
	for (const auto &l : locs)
		for (int j = 1; j <= 2; ++j)
			paint(seas_clump(hm, rng, disk(canyon_r / (float)j), 0.65f, 0.1f, 1e9f, l.first, l.second, anywhere), land, 2.0f,
			      j == 1 ? &cl_land : &cl_player);
	paint(seas_clump(hm, rng, disk(0.16f * size), 0.7f, 0.1f, 1e9f, 0.0f, 0.0f, anywhere), land, 3.0f, &cl_land);
	seas_add(cl_hill, seas_clump(hm, rng, 150.0f, 0.6f, 0.3f, 1e9f, 0.0f, 0.0f, anywhere));
	auto rand_pos = [&]() { const float b_ = -half + (float)rng.range_int(1, (int)size - 1); const float a_ = -half + (float)rng.range_int(1, (int)size - 1); return std::make_pair(a_, b_); };
	{
		const auto d_player = distance_to_mask(cl_player, hm.res, sp), d_hill = distance_to_mask(cl_hill, hm.res, sp);
		for (int i = 0, n = scale_by_map_size(9, 16, half); i < n; ++i) {
			const auto d_hill2 = distance_to_mask(cl_hill2, hm.res, sp);
			const auto a = rand_pos(), b = rand_pos();
			const auto m = seas_path(hm, rng, a.first, a.second, b.first, b.second, seas_scale(11.0f, 16.0f, half), 0.4f,
			                         3.0f * seas_scale(1.0f, 4.0f, half), 0.1f, 0.0f, 0.0f,
			                         [&](int k) { return d_player[(size_t)k] > 6.0f && d_hill2[(size_t)k] > 3.0f && d_hill[(size_t)k] > 2.0f; });
			paint(m, hill, 3.0f, &cl_hill2);
		}
	}
	for (int g = 0, n = scale_by_map_size(5, 30, half); g < n; ++g) {
		const auto c = rand_pos();
		const auto d_land = distance_to_mask(cl_land, hm.res, sp);
		const auto m = seas_clump(hm, rng, disk(0.06f * size), 0.7f, 0.1f, 1e9f, c.first, c.second, [&](int k) { return d_land[(size_t)k] > 6.0f; });
		if (!seas_any(m)) continue;
		paint(m, land, 3.0f, &cl_land);
		// The two nearest bases.
		int p1 = -1, p2 = -1;
		float d1 = 1e9f, d2 = 1e9f;
		for (int i = 0; i < (int)locs.size(); ++i) {
			const float d = det::hypot(c.first - locs[(size_t)i].first, c.second - locs[(size_t)i].second);
			if (d1 >= d) { d2 = d1; p2 = p1; d1 = d; p1 = i; }
			else if (d2 >= d) { d2 = d; p2 = i; }
		}
		for (int p : {p1, p2}) {
			if (p < 0) continue;
			paint(seas_path(hm, rng, c.first, c.second, locs[(size_t)p].first, locs[(size_t)p].second, seas_scale(11.0f, 17.0f, half), 0.4f,
			                seas_scale(3.0f, 12.0f, half), 0.1f, 0.1f, 0.0f, anywhere),
			      land, 3.0f, &cl_land);
		}
	}
	for (int i = 0; i < (int)locs.size(); ++i) {
		const auto &from = locs[(size_t)i];
		for (const auto &to : {locs[(size_t)((i + 1) % (int)locs.size())], std::make_pair(0.0f, 0.0f)}) {
			const auto m = seas_path(hm, rng, from.first, from.second, to.first, to.second, seas_scale(8.0f, 13.0f, half), 0.4f,
			                         3.0f * seas_scale(1.0f, 4.0f, half), 0.1f, 0.0f, 0.0f, anywhere);
			paint(m, land, 2.0f, &cl_land);
			seas_add(cl_hill, m);
		}
	}
	seas_spawns(hm, spawns, locs, 12.0f, 4.0f, water_h, land);
}

void RmsGenerator::synth_lake(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// lake.js: land at 3 m; players by the chosen pattern at f = 0.35 (River
	// 0.55), teammates 0.1 apart; the lake is a ChainPlacer(2,
	// floor(scaleByMapSize(5, 16)), floor(scaleByMapSize(35, 200)), Infinity)
	// from the centre, first circle f = 0.2, set to -3 m (smoothing 4),
	// avoiding the bases (addCivicCenterAreaToClass, a disk of 5) by 20.
	// Then bumps (2 off the water, 20 off the bases) and either hills (18 m)
	// or mountains, scaleByMapSize(1, 4) per player, 20 off the bases, 15
	// apart, 2 off the water.
	water_h = 1.0f;
	const float half = hm.map_half, size = 2.0f * half;
	const float land = metres(water_h, 3.0f);
	hm.init(hm.res, hm.map_half, land);
	const int pattern = t_team_placement.pattern;
	const float start = rng.range_float(0.0f, 2.0f * kSeasPi);
	const auto locs = pattern >= 0
		? pattern_locs(hm, rng, pattern, num_players, t_team_placement.teams, (pattern == 4 ? 0.55f : 0.35f) * size, 0.1f * size, start)
		: seas_circle_players(num_players, start, 0.35f * size, t_team_placement.teams);
	const auto to_player = seas_dist_to_players(hm, locs, 5.0f);
	const auto water = seas_chain(hm, rng, 0.0f, 0.0f, 2, (int)std::floor(seas_scale(5, 16, half)), (int)std::floor(seas_scale(35, 200, half)),
	                              1e9f, 0.0f, {(int)std::floor(0.2f * size)}, [&](int i) { return to_player[(size_t)i] > 20.0f; });
	paint_mask_elevation(hm, water, metres(water_h, -3.0f), 4.0f);
	const auto to_water = seas_dist_inside(hm, water);
	seas_bumps(hm, rng, seas_scale(100, 200, half), [&](int i) { return to_water[(size_t)i] > 2.0f && to_player[(size_t)i] > 20.0f; });
	const bool mountains = rng.range_int(0, 1) == 0;
	seas_hills(hm, rng, water_h, seas_scale(1, 4, half) * (float)num_players, mountains, 15.0f, 18.0f,
	           [&](int i) { return to_player[(size_t)i] > 20.0f && to_water[(size_t)i] > 2.0f; });
	seas_spawns(hm, spawns, locs, 16.0f, 6.0f, water_h, land);
}

void RmsGenerator::synth_gulf_of_bothnia(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// gulf_of_bothnia.js (unfrozen biomes): land at 3 m; players sorted by
	// team on an arc at f = 0.35, from startAngle + pi/3 to startAngle +
	// 5pi/3; the gulf is three ChainPlacer(2, floor(scaleByMapSize(5, 16)),
	// floor(scaleByMapSize(35, n)), Infinity) lakes set to -3 m (smoothing 4)
	// at f = 0, 0.3 and 0.5 from the centre towards startAngle (first circles
	// f = 0.175, 0.2 and 0.225; n = 200, 120, 100), avoiding the bases by
	// scaleByMapSize(20, 28). Then bumps (2 off the gulf, 10 off the bases)
	// and either hills (18 m, smoothing 4) or mountains of
	// floor(scaleByMapSize(20, 40)) m, scaleByMapSize(1, 4) per player, 20
	// off the bases, 15 apart, off the gulf.
	water_h = 1.0f;
	const float half = hm.map_half, size = 2.0f * half;
	const float land = metres(water_h, 3.0f);
	hm.init(hm.res, hm.map_half, land);
	const float start = rng.range_float(0.0f, 2.0f * kSeasPi);
	std::vector<int> order((size_t)num_players);
	for (int i = 0; i < num_players; ++i) order[(size_t)i] = i;
	std::stable_sort(order.begin(), order.end(), [](int a, int b) { return t_team_placement.teams[a] < t_team_placement.teams[b]; });
	std::vector<std::pair<float, float>> locs((size_t)num_players);
	for (int k = 0; k < num_players; ++k) {
		const float t = num_players == 1 ? 1.0f : 2.0f * (float)k / (float)(num_players - 1);
		locs[(size_t)order[(size_t)k]] = polar(start + kSeasPi / 3.0f * (1.0f + 2.0f * t), 0.35f * size);
	}
	const auto to_player = seas_dist_to_players(hm, locs, 5.0f);
	const float avoid = seas_scale(20, 28, half);
	std::vector<uint8_t> gulf(hm.data.size(), 0);
	const struct { float x, r; int circles; } lakes[] = {{0.0f, 0.175f, 200}, {0.3f, 0.2f, 120}, {0.5f, 0.225f, 100}};
	for (const auto &l : lakes) {
		const auto c = polar(start, l.x * size);
		const auto m = seas_chain(hm, rng, c.first, c.second, 2, (int)std::floor(seas_scale(5, 16, half)),
		                          (int)std::floor(seas_scale(35, (float)l.circles, half)), 1e9f, 0.0f, {(int)std::floor(l.r * size)},
		                          [&](int i) { return to_player[(size_t)i] > avoid; });
		paint_mask_elevation(hm, m, metres(water_h, -3.0f), 4.0f);
		seas_add(gulf, m);
	}
	const auto to_gulf = seas_dist_inside(hm, gulf);
	seas_bumps(hm, rng, seas_scale(100, 200, half), [&](int i) { return to_gulf[(size_t)i] > 2.0f && to_player[(size_t)i] > 10.0f; });
	const bool mountains = rng.range_int(0, 1) == 0;
	seas_hills(hm, rng, water_h, seas_scale(1, 4, half) * (float)num_players, mountains, 15.0f, 18.0f,
	           [&](int i) { return to_player[(size_t)i] > 20.0f && to_gulf[(size_t)i] > 0.0f; }, 4.0f, std::floor(seas_scale(20, 40, half)));
	seas_spawns(hm, spawns, locs, 16.0f, 6.0f, water_h, land);
}

void RmsGenerator::synth_aegean_sea(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players,
		const uint8_t *teams) {
	// aegean_sea.js: land at 2 m (the script starts at 1 m and paints 2 m at
	// the river banks; RTS0 keeps the whole land at 2 m so it stays clear of
	// the water line), a sea 0.35 of the map wide through the centre at a
	// random angle (paintRiver, -3 m, fade scaleByMapSize(6, 25), meanderShort
	// 20), players on both shores (playerPlacementRiver, 0.6 of the map
	// apart), bumps, hills or mountains, -2.5 m undersea bumps and
	// scaleByMapSize(1, 4) * players islands of 6 m in the sea (ChainPlacer(1,
	// floor(scaleByMapSize(4, 6)), floor(scaleByMapSize(30, 80)), 0.5), 15
	// apart, 6 from the shore). Crossing the sea needs ships.
	water_h = 1.0f;
	const float half = hm.map_half, size = 2.0f * half;
	const float land = metres(water_h, 2.0f);
	hm.init(hm.res, hm.map_half, land);
	const float angle = rng.range_float(0.0f, 2.0f * kSeasPi);
	const float nx = det::cos(angle), nz = det::sin(angle); // across the sea
	const float ux = -nz, uz = nx;                          // along it
	// playerPlacementRiver(angle, 0.6 * size).
	std::vector<std::pair<float, float>> locs((size_t)num_players);
	const bool even = num_players % 2 == 0;
	for (int i = 0; i < num_players; ++i) {
		const bool cur_even = i % 2 == 0;
		const int divident = (even || cur_even) ? (i + 1) % 2 : 0;
		const int divisor = even ? 0 : (cur_even ? 1 : -1);
		const float across = (i % 2 ? 0.3f : -0.3f) * size;
		const float f = ((float)(i - 1 + divident) / 2.0f + 1.0f) / ((float)(num_players + divisor) / 2.0f + 1.0f);
		const float along = (f - 0.5f) * size;
		locs[(size_t)i] = {ux * along + nx * across, uz * along + nz * across};
	}
	group_players_by_area(locs, num_players, teams);
	const float pclass = seas_scale(15, 25, half) / 3.0f;
	const auto to_player = seas_dist_to_players(hm, locs, pclass);
	seas_paint_river(hm, rng, ux * half, uz * half, -ux * half, -uz * half, 0.35f * size, seas_scale(6, 25, half),
	                 metres(water_h, -3.0f), land, 20.0f, 0.0f);
	std::vector<uint8_t> water(hm.data.size());
	for (size_t i = 0; i < water.size(); ++i) water[i] = hm.data[i] < metres(water_h, 0.7f);
	const auto to_water = seas_dist_inside(hm, water);
	const auto in_water = seas_dist_outside(hm, water);
	seas_bumps(hm, rng, seas_scale(100, 200, half), [&](int i) { return to_water[(size_t)i] > 2.0f && to_player[(size_t)i] > 20.0f; });
	const bool mountains = rng.range_int(0, 1) == 0;
	seas_hills(hm, rng, water_h, seas_scale(3, 15, half), mountains, 15.0f, 18.0f,
	           [&](int i) { return to_player[(size_t)i] > 20.0f && to_water[(size_t)i] > 3.0f; });
	// Undersea bumps.
	seas_create_areas(hm, rng, seas_scale(10, 50, half), 10, [&](float x, float z) {
		const auto m = seas_chain(hm, rng, x, z, 1, (int)std::floor(seas_scale(4, 6, half)), (int)std::floor(seas_scale(16, 40, half)),
		                          0.5f, 0.0f, {}, [&](int i) { return in_water[(size_t)i] > 6.0f; });
		if (!seas_any(m)) return false;
		paint_mask_elevation(hm, m, metres(water_h, -2.5f), 3.0f);
		return true;
	});
	// Islands.
	std::vector<uint8_t> islands(hm.data.size(), 0);
	std::vector<float> to_island(hm.data.size(), 1e9f);
	seas_create_areas(hm, rng, seas_scale(1, 4, half) * (float)num_players, 10, [&](float x, float z) {
		const auto m = seas_chain(hm, rng, x, z, 1, (int)std::floor(seas_scale(4, 6, half)), (int)std::floor(seas_scale(30, 80, half)),
		                          0.5f, 0.0f, {}, [&](int i) {
			                          return to_player[(size_t)i] > 8.0f && to_island[(size_t)i] > 15.0f && in_water[(size_t)i] > 6.0f;
		                          });
		if (!seas_any(m)) return false;
		paint_mask_elevation(hm, m, metres(water_h, 6.0f), 4.0f);
		record_area(hm, m, AREA_ISLAND);
		seas_add(islands, m);
		to_island = seas_dist_inside(hm, islands);
		return true;
	});
	seas_spawns(hm, spawns, locs, 16.0f, 6.0f, water_h, land);
}

void RmsGenerator::synth_danubius_0ad(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players,
		const uint8_t *teams, DanubiusLayout &out) {
	// danubius.js: land at 3 m, the Danube 0.3 of the map wide through the centre at a random
	// angle (paintRiver, -3 m, fade scaleByMapSize(6, 25), meanderShort 30), players on both
	// banks (playerPlacementRiver, 0.6 of the map apart). From 192 tiles a Gallic village
	// stands at the map edge on each bank (its city patch 12 tiles round the civic centre),
	// usually (90%) with a path from its gate down to a meeting place on the shore at 1.5 m.
	// Bumps and hills or mountains keep off the villages and paths; scaleByMapSize(1, 4) *
	// players islands of 6 m (ChainPlacer(floor(scaleByMapSize(3, 4)), floor(scaleByMapSize(4,
	// 8)), floor(scaleByMapSize(50, 80)), 0.5), 30 apart, 10 into the river) carry bumps of
	// their own. place_danubius_entities puts up the villages, island forts and trigger points.
	water_h = 1.0f;
	const float half = hm.map_half, size = 2.0f * half;
	const float land = metres(water_h, 3.0f);
	hm.init(hm.res, hm.map_half, land);
	const float angle = rng.range_float(0.0f, 2.0f * kSeasPi);
	const float nx = det::cos(angle), nz = det::sin(angle); // across the river: the left bank is negative
	const float ux = -nz, uz = nx;                          // along it
	out = DanubiusLayout();
	out.nx = nx;
	out.nz = nz;
	auto at = [&](float across, float along) { return std::make_pair(nx * across + ux * along, nz * across + uz * along); };
	std::vector<uint8_t> gauls(hm.data.size(), 0), path(hm.data.size(), 0);
	const float city_r = 12.0f;
	out.villages = size >= 192.0f;
	if (out.villages) {
		const float border = size < 256.0f ? 10.0f : 18.0f;
		out.ritual = rng.next_float() < 0.9f;
		const float ritual_r = seas_scale(4, 6, half);
		out.ritual_r = ritual_r;
		for (int i = 0; i < 2; ++i) {
			const float side = i == 0 ? -1.0f : 1.0f;
			const auto cc = at(side * (half - border), 0.0f);
			out.village[i][0] = cc.first;
			out.village[i][1] = cc.second;
			if (out.ritual) {
				// The meeting place, off the centre of the bank, and the path from the gate to it.
				const float a_ = rng.range_float(0.1f, 0.4f); const float off = size * a_ * (rng.range_int(0, 1) ? 1.0f : -1.0f);
				const auto meet = at(side * (half - 0.3f * size), off);
				out.ritual_pos[i][0] = meet.first;
				out.ritual_pos[i][1] = meet.second;
				const auto gate = at(side * (half - border - city_r), 0.0f);
				const auto m = seas_path(hm, rng, gate.first, gate.second, meet.first, meet.second, 4.0f, 0.4f, 4.0f, 0.2f, 0.05f, 0.0f,
				                         [](int) { return true; });
				paint_mask_elevation(hm, m, metres(water_h, 1.5f), 4.0f);
				seas_add(path, m);
				mask_disk(hm, path, meet.first, meet.second, ritual_r);
			}
			mask_disk(hm, gauls, cc.first, cc.second, city_r);
		}
	}
	// playerPlacementRiver(angle, 0.6 * size).
	std::vector<std::pair<float, float>> locs((size_t)num_players);
	const bool even = num_players % 2 == 0;
	for (int i = 0; i < num_players; ++i) {
		const bool cur_even = i % 2 == 0;
		const int divident = (even || cur_even) ? (i + 1) % 2 : 0;
		const int divisor = even ? 0 : (cur_even ? 1 : -1);
		const float f = ((float)(i - 1 + divident) / 2.0f + 1.0f) / ((float)(num_players + divisor) / 2.0f + 1.0f);
		locs[(size_t)i] = at((i % 2 ? 0.3f : -0.3f) * size, (f - 0.5f) * size);
	}
	group_players_by_area(locs, num_players, teams);
	const float pclass = seas_scale(15, 25, half) / 3.0f;
	const auto to_player = seas_dist_to_players(hm, locs, pclass);
	seas_paint_river(hm, rng, ux * half, uz * half, -ux * half, -uz * half, 0.3f * size, seas_scale(6, 25, half),
	                 metres(water_h, -3.0f), land, 30.0f, 0.0f);
	// waterFunc: the shore (0 to 1 m) of each bank, 15 tiles clear of the river's ends.
	out.shore.assign(hm.data.size(), 0);
	for (size_t i = 0; i < hm.data.size(); ++i) {
		const float h = hm.data[i];
		if (!(h > metres(water_h, 0.0f) && h < metres(water_h, 1.0f))) continue;
		float wx, wz;
		seas_world(hm, (int)i, wx, wz);
		if (std::fabs(wx * ux + wz * uz) >= half - 15.0f) continue;
		out.shore[i] = wx * nx + wz * nz < 0.0f ? 1 : 2;
	}
	std::vector<uint8_t> water(hm.data.size());
	for (size_t i = 0; i < water.size(); ++i) water[i] = hm.data[i] < metres(water_h, 0.7f);
	const auto to_water = seas_dist_inside(hm, water);
	const auto in_water = seas_dist_outside(hm, water);
	const auto to_gauls = seas_dist_inside(hm, gauls);
	const auto to_path = seas_dist_inside(hm, path);
	// createBumps(avoidClasses(clPlayer, 6, clWater, 2, clPath, 1, clGauls, 1), scaleByMapSize(30, 300), 1, 8, 4, 0, 3).
	auto bumps = [&](float amount, const auto &allow) {
		seas_create_areas(hm, rng, amount, 10, [&](float x, float z) {
			const auto m = seas_chain(hm, rng, x, z, 1, 8, 4, 0.0f, 0.0f, {}, allow);
			if (!seas_any(m)) return false;
			seas_paint_modify(hm, m, 3.0f * kMetre, 2.0f);
			return true;
		});
	};
	bumps(seas_scale(30, 300, half), [&](int i) {
		return to_player[(size_t)i] > 6.0f && to_water[(size_t)i] > 2.0f && to_path[(size_t)i] > 1.0f && to_gauls[(size_t)i] > 1.0f;
	});
	const bool mountains = rng.range_int(0, 1) == 0;
	seas_hills(hm, rng, water_h, seas_scale(3, 15, half), mountains, 20.0f, 18.0f, [&](int i) {
		return to_player[(size_t)i] > 18.0f && to_water[(size_t)i] > 2.0f && to_gauls[(size_t)i] > 5.0f && to_path[(size_t)i] > 1.0f;
	});
	// Islands.
	out.islands.assign(hm.data.size(), 0);
	std::vector<float> to_island(hm.data.size(), 1e9f);
	seas_create_areas(hm, rng, seas_scale(1, 4, half) * (float)num_players, 10, [&](float x, float z) {
		const auto m = seas_chain(hm, rng, x, z, (int)std::floor(seas_scale(3, 4, half)), (int)std::floor(seas_scale(4, 8, half)),
		                          (int)std::floor(seas_scale(50, 80, half)), 0.5f, 0.0f, {},
		                          [&](int i) { return to_island[(size_t)i] > 30.0f && in_water[(size_t)i] > 10.0f; });
		if (!seas_any(m)) return false;
		paint_mask_elevation(hm, m, metres(water_h, 6.0f), 4.0f);
		seas_add(out.islands, m);
		to_island = seas_dist_inside(hm, out.islands);
		return true;
	});
	const auto in_island = seas_dist_outside(hm, out.islands);
	bumps(seas_scale(50, 400, half), [&](int i) { return in_island[(size_t)i] > 2.0f; });
	// The villages stand on level ground.
	if (out.villages)
		for (const auto &v : out.village) lift_area(hm, v[0], v[1], city_r, 4.0f, land);
	seas_spawns(hm, spawns, locs, 16.0f, 6.0f, water_h, land);
}

void RmsGenerator::place_danubius_entities(Simulation &sim, const HeightmapGrid &hm, float water_h, const std::vector<PlayerSpawn> &spawns,
		RmsRng &rng, const DanubiusLayout &lay) {
	// danubius.js's entities: the Gallic villages (a Gaia civic centre in a palisade ring of
	// huts, houses, a longhouse, a tavern, a temple and two towers, 8 to 12 treasures, and the
	// Celtic ritual at the meeting place), the island mines, towers and outposts, and the
	// trigger points danubius_triggers.js sends its ships and soldiers to. RTS0 has no celt
	// hut, longhouse or tavern: they are houses. Left out: the palisade spikes, the actors and
	// the gates (the village wall leaves their gaps open).
	const float half = hm.map_half, size = 2.0f * half;
	const float nx = lay.nx, nz = lay.nz, ux = -nz, uz = nx;
	const float start_angle = det::atan2(nz, nx);
	auto across = [&](float x, float z) { return x * nx + z * nz; };
	auto dry = [&](float x, float z, float m) { return hm.sample_world(x, z) > metres(water_h, m); };
	struct Spot { float x, z; };
	auto clear_of = [](const std::vector<Spot> &v, float x, float z, float d) {
		for (const Spot &s : v)
			if (det::hypot(x - s.x, z - s.z) < d) return false;
		return true;
	};
	auto building = [&](float x, float z, uint8_t type, bool capturable) {
		const uint32_t id = sim.spawn_building_world(x, z, type, GAIA_PLAYER, 1.0f);
		if (!id) return id;
		sim.set_building_civ(id, CIV_GAULS); // structures/gaul/*: a captured CC trains Gallic women
		if (!capturable) sim.set_structure_uncapturable(id);
		for (const auto &b : sim.buildings())
			if (b.id == id) sim.clear_resources_near(x, z, b.half_size + 1.0f);
		return id;
	};
	static const uint8_t kTreasures[] = {TREASURE_FOOD_BARREL, TREASURE_FOOD_BIN, TREASURE_STONE, TREASURE_WOOD, TREASURE_METAL};
	std::vector<Spot> gauls;
	if (lay.villages) {
		const float city_r = 12.0f;
		const int treasures = rng.range_int(8, 12); // gallicCCTreasureCount
		// fortressDanubiusVillage, twice round: the structures among its 26 wall elements.
		enum : uint8_t { V_NONE, V_GATE, V_HUT, V_HOUSE, V_LONGHOUSE, V_TAVERN, V_TEMPLE, V_TOWER };
		static const uint8_t kRing[26] = {V_GATE, V_NONE, V_HUT, V_NONE, V_NONE, V_NONE, V_TOWER, V_NONE, V_TEMPLE, V_NONE, V_NONE,
			V_HOUSE, V_NONE, V_NONE, V_NONE, V_GATE, V_NONE, V_LONGHOUSE, V_NONE, V_NONE, V_NONE, V_TOWER, V_NONE, V_TAVERN, V_NONE, V_NONE};
		const float ring = kSeasPi / 26.0f;
		for (int i = 0; i < 2; ++i) {
			const float cx = lay.village[i][0], cz = lay.village[i][1];
			gauls.push_back({cx, cz});
			if (lay.ritual) {
				// The Celtic ritual: women, warriors and goats in rings round the campfire.
				const float mx = lay.ritual_pos[i][0], mz = lay.ritual_pos[i][1];
				sim.clear_resources_near(mx, mz, lay.ritual_r * 1.3f);
				static const char *const kWarriors[] = {"gaul/infantry_javelineer_b", "gaul/support_healer_b", "gaul/champion_fanatic"};
				struct Ring { float radius; int count; int kind; };
				for (const Ring &r : {Ring{0.6f, 9, 0}, Ring{0.8f, 15, 1}, Ring{1.1f, 7, 2}})
					for (int k = 0; k < r.count; ++k) {
						const float a = start_angle + 2.0f * kSeasPi * (float)k / (float)r.count;
						const float x = mx + det::cos(a) * r.radius * lay.ritual_r, z = mz + det::sin(a) * r.radius * lay.ritual_r;
						if (r.kind == 2) {
							sim.spawn_fauna(x, z, FAUNA_GOAT);
							continue;
						}
						const char *id = r.kind == 0 ? "gaul/support_civilian" : kWarriors[rng.range_int(0, 2)]; // oCivilian
						const uint16_t t = civ_unit_template_by_id(id);
						if (t != CIV_UNIT_TEMPLATE_NONE) sim.spawn_gaia_unit(x, z, (UnitType)civ_unit_template(t).unit_type, id);
					}
			}
			sim.clear_resources_near(cx, cz, city_r + 2.0f);
			building(cx, cz, BLD_TOWN_CENTER, true);
			// placeCustomFortress(..., startAngle + PI): the structures 4 tiles inside the wall.
			const float base = start_angle + kSeasPi;
			for (int k = 0; k < 52; ++k) {
				const uint8_t kind = kRing[k % 26];
				if (kind == V_NONE || kind == V_GATE) continue;
				const float a = base + 2.0f * ring * (float)k;
				const float x = cx + det::cos(a) * (city_r - 4.0f), z = cz + det::sin(a) * (city_r - 4.0f);
				if (kind == V_HUT) building(x, z, BLD_HUT, false);             // celt_hut
				else if (kind == V_HOUSE) building(x, z, BLD_HOUSE, false);    // gaul/house
				else if (kind == V_LONGHOUSE) building(x, z, BLD_LONGHOUSE, false); // celt_longhouse
				else if (kind == V_TAVERN) building(x, z, BLD_TAVERN, false);  // gaul/tavern
				else if (kind == V_TEMPLE) building(x, z, BLD_TEMPLE, false);
				else if (size >= 320.0f && sim.nomad()) building(x, z, BLD_SENTRY_TOWER, false); // sentry_tower
				else if (size >= 320.0f) building(x, z, BLD_TOWER, true); // gaul/defense_tower
				else {
					// palisades_watchtower: a palisade tower piece (no arrows, garrison or territory).
					const uint32_t id = sim.spawn_map_wall_piece(GAIA_PLAYER, WALLSET_PALISADE, Simulation::MAP_WALL_TOWER, x, z, a, 2.0f);
					if (id) sim.clear_resources_near(x, z, 2.5f);
				}
			}
			// The palisade between the four gates.
			for (int g = 0; g < 4; ++g) {
				const int from = (g % 2 ? 15 : 0) + (g / 2) * 26, to = g % 2 ? 26 + (g / 2) * 26 : 15 + (g / 2) * 26;
				const float a0 = base + 2.0f * ring * (float)from + 0.25f, a1 = base + 2.0f * ring * (float)to - 0.25f;
				const int chords = std::max(1, (int)std::ceil((a1 - a0) * city_r / 6.0f));
				for (int c = 0; c < chords; ++c) {
					const float b0 = a0 + (a1 - a0) * (float)c / (float)chords, b1 = a0 + (a1 - a0) * (float)(c + 1) / (float)chords;
					sim.spawn_gaia_wall(cx + det::cos(b0) * city_r, cz + det::sin(b0) * city_r, cx + det::cos(b1) * city_r,
					                    cz + det::sin(b1) * city_r, WALLSET_PALISADE);
				}
			}
			for (int k = 0; k < treasures; ++k) {
				const float r = rng.range_float(-0.8f, 0.8f) * city_r, a = rng.range_float(0.0f, 2.0f * kSeasPi);
				sim.spawn_treasure(cx + det::cos(a) * r, cz + det::sin(a) * r, kTreasures[rng.range_int(0, 4)]);
			}
		}
	}
	// Island mines, towers and outposts (createObjectGroupsByAreas over the islands).
	const auto in_island = seas_dist_outside(hm, lay.islands);
	std::vector<size_t> island_cells;
	for (size_t i = 0; i < in_island.size(); ++i)
		if (lay.islands[i] && in_island[i] > 5.0f) island_cells.push_back(i);
	std::vector<Spot> metal, stone, towers, outposts;
	auto on_islands = [&](int amount, float stay, const auto &ok, const auto &put) {
		if (island_cells.empty()) return;
		for (int placed = 0, tries = 0; placed < amount && tries < amount * 20; ++tries) {
			const size_t i = island_cells[(size_t)rng.range_int(0, (int)island_cells.size() - 1)];
			if (in_island[i] <= stay) continue;
			float x, z;
			seas_world(hm, (int)i, x, z);
			if (!ok(x, z)) continue;
			put(x, z);
			++placed;
		}
	};
	const int island_groups = scale_by_map_size(3, 10, half);
	on_islands(island_groups, 5.0f, [&](float x, float z) { return clear_of(metal, x, z, 50.0f) && clear_of(stone, x, z, 10.0f); },
	           [&](float x, float z) { sim.spawn_resource_world(x, z, RES_GOLD, LARGE_MINE_SUPPLY); metal.push_back({x, z}); });
	on_islands(island_groups, 5.0f, [&](float x, float z) { return clear_of(metal, x, z, 10.0f) && clear_of(stone, x, z, 50.0f); },
	           [&](float x, float z) { sim.spawn_resource_world(x, z, RES_STONE, LARGE_MINE_SUPPLY); stone.push_back({x, z}); });
	on_islands(island_groups, 7.0f,
	           [&](float x, float z) { return clear_of(metal, x, z, 4.0f) && clear_of(stone, x, z, 4.0f) && clear_of(towers, x, z, 20.0f); },
	           [&](float x, float z) { building(x, z, BLD_TOWER, true); towers.push_back({x, z}); });
	on_islands(island_groups, 7.0f, [&](float x, float z) {
		return clear_of(metal, x, z, 4.0f) && clear_of(stone, x, z, 4.0f) && clear_of(towers, x, z, 5.0f) && clear_of(outposts, x, z, 20.0f);
	}, [&](float x, float z) { building(x, z, BLD_OUTPOST, true); outposts.push_back({x, z}); });

	// Trigger points.
	std::vector<uint8_t> water(hm.data.size());
	for (size_t i = 0; i < water.size(); ++i) water[i] = hm.data[i] < metres(water_h, 0.7f);
	const auto in_water = seas_dist_outside(hm, water);
	const auto to_water = seas_dist_inside(hm, water);
	const auto res = sim.resources();
	auto near_resource = [&](float x, float z, float d, bool wood) {
		for (size_t r = 0; r < res.count; ++r)
			if (res.alive[r] && (wood || res.type[r] != RES_WOOD) && std::fabs(res.x[r] - x) < d && std::fabs(res.z[r] - z) < d && det::hypot(res.x[r] - x, res.z[r] - z) < d) return true;
		return false;
	};
	const float base_r = seas_scale(15, 25, half);
	auto far_from_players = [&](float x, float z, float d) {
		for (const auto &sp : spawns)
			if (det::hypot(x - sp.x, z - sp.z) < base_r + d) return false;
		return true;
	};
	auto points = [&](uint8_t ref, int amount, int retry, float spacing, const auto &ok) {
		std::vector<Spot> placed;
		for (int tries = 0; (int)placed.size() < amount && tries < amount * retry; ++tries) {
			const auto p = seas_random_point(hm, rng);
			const int i = seas_vertex(hm, p.first, p.second);
			if (!ok(p.first, p.second, i) || !clear_of(placed, p.first, p.second, spacing)) continue;
			sim.add_trigger_point(ref, p.first, p.second);
			placed.push_back({p.first, p.second});
		}
	};
	// A: ship spawns, 10 into the river; B: ship patrols, 4 into it.
	points('A', scale_by_map_size(10, 75, half), 10, 5.0f, [&](float, float, int i) { return in_water[(size_t)i] > 10.0f; });
	points('B', scale_by_map_size(20, 150, half), 10, 5.0f, [&](float, float, int i) { return in_water[(size_t)i] > 4.0f; });
	// C and D: where the ships unload, on the left and right shore (at most 200, as the
	// script's scaleByMapSize(60, 200) never passes it: TriggerWatch keeps the index in a byte).
	for (int side = 1; side <= 2; ++side) {
		std::vector<size_t> shore;
		for (size_t i = 0; i < lay.shore.size(); ++i)
			if (lay.shore[i] == side) shore.push_back(i);
		std::vector<Spot> placed;
		const int amount = std::min(200, scale_by_map_size(60, 200, half));
		for (int tries = 0; !shore.empty() && (int)placed.size() < amount && tries < amount * 20; ++tries) {
			float x, z;
			seas_world(hm, (int)shore[(size_t)rng.range_int(0, (int)shore.size() - 1)], x, z);
			if (!clear_of(placed, x, z, 4.0f)) continue;
			sim.add_trigger_point(side == 1 ? 'C' : 'D', x, z);
			placed.push_back({x, z});
		}
	}
	// I: the river's direction from the centre.
	sim.add_trigger_point('I', ux, uz);
	// E and F: the siege engines' patrols on each bank; G and H: the village soldiers'.
	auto bank = [&](int i, float x, float z, int side) {
		return !lay.islands[(size_t)i] && (side == 1) == (across(x, z) < 0.0f) && dry(x, z, 0.7f) && hm.compute_slope_world(x, z) < 0.6f;
	};
	for (int side = 1; side <= 2; ++side)
		points(side == 1 ? 'E' : 'F', scale_by_map_size(20, 150, half), 10, 5.0f, [&](float x, float z, int i) {
			return bank(i, x, z, side) && to_water[(size_t)i] > 5.0f && far_from_players(x, z, 10.0f) && clear_of(gauls, x, z, 12.0f + 5.0f) &&
			       !near_resource(x, z, 3.0f, true);
		});
	if (lay.villages)
		for (int side = 1; side <= 2; ++side)
			points(side == 1 ? 'G' : 'H', scale_by_map_size(20, 150, half), 20, 5.0f, [&](float x, float z, int i) {
				return bank(i, x, z, side) && to_water[(size_t)i] > 5.0f && far_from_players(x, z, 15.0f) && clear_of(gauls, x, z, 12.0f) &&
				       !near_resource(x, z, 4.0f, false);
			});

	// The hunt (danubius.js "Creating huntable animals", "Creating violent animals", "Creating
	// animals on islands"): createFood, one try per group. On the banks: scaleByMapSize(5, 20)
	// groups each of 5 sheep, 5 goats, 5-8 rabbits and 4-6 deer, then (not for Nomad) of 1-3
	// wolves, a boar and a bear, avoidClasses(clIsland, 2, clFood, 10, clWater, 5, clPlayer, 16
	// (24 for the predators), clGauls, 5); on the islands, 10 * scaleByMapSize(5, 20) groups
	// each of 4-6 sheep, 4-6 goats and 5-8 rabbits, stayClasses(clIsland, 4), avoidClasses(clRock,
	// 4, clMetal, 4, clFood, 3, clForest, 1, clOutpost, 2, clTower, 2). Left out: the hawks
	// (birds), the hills and paths it also avoids.
	{
		RmsRng hunt(rng.next_u32());
		std::vector<uint8_t> land(lay.islands.size());
		for (size_t i = 0; i < land.size(); ++i) land[i] = !lay.islands[i];
		const auto to_island = seas_dist_inside(hm, lay.islands), in_island = seas_dist_inside(hm, land);
		std::vector<std::pair<float, float>> food, wood;
		const auto res = sim.resources();
		for (size_t i = 0; i < res.count; ++i) {
			if (!res.alive[i]) continue;
			if (res.type[i] == RES_FOOD && res.subtype[i] != RES_SUB_FISH) food.push_back({res.x[i], res.z[i]});
			else if (res.type[i] == RES_WOOD) wood.push_back({res.x[i], res.z[i]});
		}
		const auto fv = sim.fauna();
		for (size_t i = 0; i < fv.count; ++i)
			if (fv.alive[i]) food.push_back({fv.pos_x[i], fv.pos_z[i]});
		auto near_any = [](const std::vector<std::pair<float, float>> &v, float x, float z, float d) {
			for (const auto &[px, pz] : v)
				if ((px - x) * (px - x) + (pz - z) * (pz - z) <= d * d) return true;
			return false;
		};
		auto centre = [&](float &x, float &z) { // g_Map.randomCoordinate(true)
			x = std::floor(hunt.range_float(-half + 3.0f, half - 3.0f));
			z = std::floor(hunt.range_float(-half + 3.0f, half - 3.0f));
		};
		auto in_map = [&](float x, float z) { return std::fabs(x) < half - 3.0f && std::fabs(z) < half - 3.0f; };
		auto banks = [&](float player_gap) {
			return [&, player_gap](float x, float z) {
				if (!in_map(x, z)) return false;
				const size_t v = (size_t)seas_vertex(hm, x, z);
				return to_island[v] > 2.0f && to_water[v] > 5.0f && far_from_players(x, z, player_gap) && clear_of(gauls, x, z, 5.0f + 3.0f) &&
				       !near_any(food, x, z, 10.0f);
			};
		};
		auto islands = [&](float x, float z) {
			if (!in_map(x, z)) return false;
			const size_t v = (size_t)seas_vertex(hm, x, z);
			return lay.islands[v] && in_island[v] >= 4.0f && clear_of(metal, x, z, 4.0f) && clear_of(stone, x, z, 4.0f) &&
			       !near_any(food, x, z, 3.0f) && !near_any(wood, x, z, 1.5f) && clear_of(outposts, x, z, 2.0f + 1.5f) &&
			       clear_of(towers, x, z, 2.0f + 1.5f);
		};
		const float n = seas_scale(5.0f, 20.0f, half);
		for (const ScriptAnimal &a : {ScriptAnimal{FAUNA_SHEEP, 5, 5, 0, 4}, ScriptAnimal{FAUNA_GOAT, 5, 5, 0, 4},
		                              ScriptAnimal{FAUNA_RABBIT, 5, 8, 0, 4}, ScriptAnimal{FAUNA_DEER, 4, 6, 0, 2}})
			place_animal_groups(sim, hunt, {a}, n, 50, centre, banks(16.0f), food, true);
		if (!sim.nomad())
			for (const ScriptAnimal &a : {ScriptAnimal{FAUNA_WOLF, 1, 3, 0, 4}, ScriptAnimal{FAUNA_BOAR, 1, 1, 0, 4},
			                              ScriptAnimal{FAUNA_BEAR_BROWN, 1, 1, 0, 4}})
				place_animal_groups(sim, hunt, {a}, n, 50, centre, banks(24.0f), food, true);
		for (const ScriptAnimal &a : {ScriptAnimal{FAUNA_SHEEP, 4, 6, 0, 4}, ScriptAnimal{FAUNA_GOAT, 4, 6, 0, 4},
		                              ScriptAnimal{FAUNA_RABBIT, 5, 8, 0, 4}})
			place_animal_groups(sim, hunt, {a}, 10.0f * n, 50, centre, islands, food, true);
	}
}

void RmsGenerator::synth_archipelago_0ad(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players,
		const uint8_t *teams) {
	// archipelago.js: sea at -5 m, players on a circle at f = 0.35 each on an
	// island (ChainPlacer(2, floor(scaleByMapSize(5, 10)),
	// floor(scaleByMapSize(25, 60)), first circle scaleByMapSize(22, 31)) at
	// 3 m; scaleByMapSize(1, 5) * randIntInclusive(5, 10) random islands
	// (ChainPlacer(floor(scaleByMapSize(4, 8)), floor(scaleByMapSize(8, 14)),
	// floor(scaleByMapSize(25, 60)), 0.07, spread scaleByMapSize(30, 70))) that
	// may join others; bumps and hills or mountains on the land. Naval.
	water_h = 1.0f;
	const float half = hm.map_half, size = 2.0f * half;
	const float land = metres(water_h, 3.0f);
	hm.init(hm.res, hm.map_half, metres(water_h, -5.0f));
	const float island_r = seas_scale(22, 31, half);
	const float start = rng.range_float(0.0f, 2.0f * kSeasPi);
	const auto locs = t_team_placement.pattern >= 0
		? pattern_locs(hm, rng, t_team_placement.pattern, num_players, teams, 0.35f * size, 0.1f * size, start)
		: seas_circle_players(num_players, start, 0.35f * size, teams);
	std::vector<uint8_t> land_mask(hm.data.size(), 0);
	auto in_map = [&](int i) { return seas_in_circle(hm, i); };
	for (const auto &l : locs) {
		const auto m = seas_chain(hm, rng, l.first, l.second, 2, (int)std::floor(seas_scale(5, 10, half)), (int)std::floor(seas_scale(25, 60, half)),
		                          1e9f, 0.0f, {(int)std::floor(island_r)}, in_map);
		paint_mask_elevation(hm, m, land, 4.0f);
		seas_add(land_mask, m);
	}
	const float islands = seas_scale(1, 5, half) * (float)rng.range_int(5, 10);
	seas_create_areas(hm, rng, islands, 10, [&](float x, float z) {
		const auto m = seas_chain(hm, rng, x, z, (int)std::floor(seas_scale(4, 8, half)), (int)std::floor(seas_scale(8, 14, half)),
		                          (int)std::floor(seas_scale(25, 60, half)), 0.07f, seas_scale(30, 70, half), {}, in_map);
		if (!seas_any(m)) return false;
		paint_mask_elevation(hm, m, land, 4.0f);
		seas_add(land_mask, m);
		return true;
	});
	const auto in_land = seas_dist_outside(hm, land_mask);
	const float pclass = island_r / 3.0f;
	const auto to_player = seas_dist_to_players(hm, locs, pclass);
	seas_bumps(hm, rng, seas_scale(100, 200, half), [&](int i) { return to_player[(size_t)i] > 10.0f && in_land[(size_t)i] > 5.0f; });
	const bool mountains = rng.range_int(0, 1) == 0;
	seas_hills(hm, rng, water_h, seas_scale(1, 4, half) * (float)num_players, mountains, 15.0f, 18.0f,
	           [&](int i) { return to_player[(size_t)i] > 2.0f && land_mask[(size_t)i]; });
	seas_spawns(hm, spawns, locs, island_r * 0.6f, 4.0f, water_h, land);
}

void RmsGenerator::synth_continent(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players,
		const uint8_t *teams) {
	// continent.js (0.28): sea at -5 m; one continent at 3 m round the centre
	// (ChainPlacer(2, floor(scaleByMapSize(5, 12)), floor(scaleByMapSize(60, 700)),
	// Infinity, first circle 0.33 of the map)); players by Team Placement at
	// f = 0.25 (teammates 0.1 apart), each sure of land (ChainPlacer(2,
	// floor(scaleByMapSize(5, 9)), floor(scaleByMapSize(5, 20)), first circle
	// floor(scaleByMapSize(23, 50)))); bumps 5 inside the coast and 10 from the
	// bases; hills or mountains (randBool) 5 inside the coast and 20 from the bases.
	water_h = 1.0f;
	const float half = hm.map_half, size = 2.0f * half;
	const float land = metres(water_h, 3.0f);
	hm.init(hm.res, hm.map_half, metres(water_h, -5.0f));
	auto in_map = [&](int i) { return seas_in_circle(hm, i); };
	std::vector<uint8_t> land_mask(hm.data.size(), 0);
	{
		const auto m = seas_chain(hm, rng, 0.0f, 0.0f, 2, (int)std::floor(seas_scale(5, 12, half)), (int)std::floor(seas_scale(60, 700, half)),
		                          1e9f, 0.0f, {(int)std::floor(0.33f * size)}, in_map);
		paint_mask_elevation(hm, m, land, 4.0f);
		seas_add(land_mask, m);
	}
	const float start = rng.range_float(0.0f, 2.0f * kSeasPi);
	std::vector<std::pair<float, float>> locs;
	if (t_team_placement.pattern >= 0)
		locs = pattern_locs(hm, rng, t_team_placement.pattern, num_players, teams, 0.25f * size, 0.1f * size, start);
	else // playerPlacementCircle: players in slot order
		for (int i = 0; i < num_players; ++i)
			locs.push_back(polar(start + 2.0f * kSeasPi * (float)i / (float)std::max(1, num_players), 0.25f * size));
	const float base_r = std::floor(seas_scale(23, 50, half));
	for (const auto &l : locs) {
		const auto m = seas_chain(hm, rng, l.first, l.second, 2, (int)std::floor(seas_scale(5, 9, half)), (int)std::floor(seas_scale(5, 20, half)),
		                          1e9f, 0.0f, {(int)base_r}, in_map);
		paint_mask_elevation(hm, m, land, 4.0f);
		seas_add(land_mask, m);
	}
	const auto in_land = seas_dist_outside(hm, land_mask);
	const auto to_player = seas_dist_to_players(hm, locs, 5.0f); // clPlayer: the civic centre's disk
	seas_bumps(hm, rng, seas_scale(100, 200, half), [&](int i) { return to_player[(size_t)i] > 10.0f && in_land[(size_t)i] > 5.0f; });
	const bool mountains = rng.range_int(0, 1) == 0;
	seas_hills(hm, rng, water_h, seas_scale(1, 4, half) * (float)num_players, mountains, 15.0f, 18.0f,
	           [&](int i) { return to_player[(size_t)i] > 20.0f && in_land[(size_t)i] > 5.0f; });
	seas_spawns(hm, spawns, locs, base_r * 0.6f, 4.0f, water_h, land);
}

void RmsGenerator::synth_foothills(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players,
		const uint8_t *teams) {
	// foothills.js (0.28): land at 3 m, no water; players by Team Placement at
	// f = randFloat(0.33, 0.42) on a circle, 0.47 for River, 0.33 for
	// Stronghold (teammates 0.1 apart). randIntInclusive(40, 90) foothills
	// (ChainPlacer(12, 28, floor(scaleByMapSize(5, 30)), Infinity, a random
	// spot, first circle 0.01 of the map), set to 6-12 m over an 18-35 tile
	// blend, 34 from the bases and 8 from each other), then
	// randIntInclusive(60, 100) higher hills (ChainPlacer(10, 20,
	// floor(scaleByMapSize(8, 15))), 14-36 m over 18-25 tiles) inside the
	// foothills and 30 from the bases. Both loops re-roll their bound each pass.
	water_h = 1.0f;
	const float half = hm.map_half, size = 2.0f * half;
	hm.init(hm.res, hm.map_half, metres(water_h, 3.0f));
	const float circle_dist = rng.range_float(0.33f, 0.42f);
	const int pattern = t_team_placement.pattern;
	const float dist = (pattern == 4 ? 0.47f : pattern == 3 ? 0.33f : circle_dist) * size;
	const float start = rng.range_float(0.0f, 2.0f * kSeasPi);
	std::vector<std::pair<float, float>> locs;
	if (pattern >= 0)
		locs = pattern_locs(hm, rng, pattern, num_players, teams, dist, 0.1f * size, start);
	else // playerPlacementCircle: players in slot order
		for (int i = 0; i < num_players; ++i)
			locs.push_back(polar(start + 2.0f * kSeasPi * (float)i / (float)std::max(1, num_players), dist));
	const auto to_player = seas_dist_to_players(hm, locs, 5.0f); // clPlayer: the civic centre's disk
	auto in_map = [&](int i) { return seas_in_circle(hm, i); };
	std::vector<uint8_t> hills(hm.data.size(), 0);
	const int first = std::max(1, (int)std::floor(0.01f * size));
	auto spot = [&]() { const float b_ = rng.range_float(-half, half); const float a_ = rng.range_float(-half, half); return std::make_pair(a_, b_); };
	{
		std::vector<float> to_hill(hm.data.size(), 1e9f);
		for (int m = 0; m < rng.range_int(40, 90); ++m) {
			const float elev = (float)rng.range_int(6, 12);
			const auto c = spot();
			const auto area = seas_chain(hm, rng, c.first, c.second, 12, 28, (int)std::floor(seas_scale(5, 30, half)), 1e9f, 0.0f, {first},
			                             [&](int i) { return in_map(i) && to_player[(size_t)i] > 34.0f && to_hill[(size_t)i] > 8.0f; });
			const float blend = (float)rng.range_int(18, 35);
			if (!seas_any(area)) continue;
			paint_mask_elevation(hm, area, metres(water_h, elev), blend);
			record_area(hm, area, AREA_HILL);
			seas_add(hills, area);
			to_hill = seas_dist_inside(hm, hills);
		}
	}
	const auto in_hill = seas_dist_outside(hm, hills);
	for (int m = 0; m < rng.range_int(60, 100); ++m) {
		const float elev = (float)rng.range_int(14, 36);
		const auto c = spot();
		const auto area = seas_chain(hm, rng, c.first, c.second, 10, 20, (int)std::floor(seas_scale(8, 15, half)), 1e9f, 0.0f, {first},
		                             [&](int i) { return in_map(i) && to_player[(size_t)i] > 30.0f && in_hill[(size_t)i] >= 1.0f; });
		const float blend = (float)rng.range_int(18, 25);
		if (!seas_any(area)) continue;
		paint_mask_elevation(hm, area, metres(water_h, elev), blend);
		record_area(hm, area, AREA_HILL);
	}
	seas_spawns(hm, spawns, locs, 14.0f, 6.0f, water_h, metres(water_h, 3.0f));
}

void RmsGenerator::synth_coast_range(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players,
		const uint8_t *teams) {
	// coast_range.js (0.28, balanced for two teams): sea at -5 m; a continent
	// at 3 m (a disk 0.5 of the map across, 0.1 off centre towards startAngle:
	// 2.6 for Circle, 1.6 for River and Stronghold); players on a circle 0.3
	// out from playerAngle (2.435721 + 3.461476 / 2^(n / 1.376771)), on River
	// banks 0.35 apart, or in Strongholds 0.3 out (teammates 0.11 apart);
	// randIntInclusive(20, 34) hills of 4-12 m (ChainPlacer(7, 15,
	// floor(scaleByMapSize(15, 20))), 16 from the bases, 28 inside the coast);
	// a mountain range outside a disk 0.48 across centred opposite the
	// continent: randIntInclusive(120, 240) plateaus of 18-22 m
	// (ChainPlacer(24, 28, floor(scaleByMapSize(5, 30)))), then
	// randIntInclusive(100, 180) peaks of 24-38 m (ChainPlacer(6, 18,
	// floor(scaleByMapSize(8, 15)))) on them; bumps 5 inside the coast.
	water_h = 1.0f;
	const float half = hm.map_half, size = 2.0f * half;
	const float land = metres(water_h, 3.0f);
	hm.init(hm.res, hm.map_half, metres(water_h, -5.0f));
	const int pattern = t_team_placement.pattern;
	const float start = pattern < 0 ? 2.6f : 1.6f;
	// Vector2D(0, d).rotate(a) in the rmgen frame is (d sin a, d cos a).
	const float ox = 0.1f * size * det::sin(start), oz = 0.1f * size * det::cos(start);
	std::vector<uint8_t> land_mask(hm.data.size(), 0), not_mountain(hm.data.size(), 0);
	mask_disk(hm, land_mask, ox, oz, 0.5f * size);
	paint_mask_elevation(hm, land_mask, land, 4.0f);
	std::vector<std::pair<float, float>> locs;
	if (pattern < 0) {
		const float angle = 2.435721f + 3.461476f / det::pow(2.0f, (float)num_players / 1.376771f);
		for (int i = 0; i < num_players; ++i)
			locs.push_back(polar(angle + 2.0f * kSeasPi * (float)i / (float)std::max(1, num_players), 0.3f * size));
	} else {
		locs = pattern_locs(hm, rng, pattern, num_players, teams, (pattern == 4 ? 0.35f : 0.3f) * size, 0.11f * size, start);
	}
	auto in_map = [&](int i) { return seas_in_circle(hm, i); };
	const auto to_player = seas_dist_to_players(hm, locs, 5.0f); // clPlayer: the civic centre's disk
	const auto in_land = seas_dist_outside(hm, land_mask);
	const int first = std::max(1, (int)std::floor(0.01f * size));
	auto spot = [&]() { const float b_ = rng.range_float(-half, half); const float a_ = rng.range_float(-half, half); return std::make_pair(a_, b_); };
	for (int m = 0; m < rng.range_int(20, 34); ++m) {
		const int elev = rng.range_int(4, 12);
		const auto c = spot();
		const auto area = seas_chain(hm, rng, c.first, c.second, 7, 15, (int)std::floor(seas_scale(15, 20, half)), 1e9f, 0.0f, {first},
		                             [&](int i) { return in_map(i) && to_player[(size_t)i] > 16.0f && in_land[(size_t)i] >= 28.0f; });
		if (!seas_any(area)) continue;
		paint_mask_elevation(hm, area, metres(water_h, (float)elev), std::max(1.0f, std::floor((float)elev / 3.0f)));
		record_area(hm, area, AREA_HILL);
	}
	mask_disk(hm, not_mountain, -ox, -oz, 0.48f * size);
	const auto out_not_mountain = seas_dist_inside(hm, not_mountain);
	std::vector<uint8_t> mountain(hm.data.size(), 0);
	{
		std::vector<float> to_mountain(hm.data.size(), 1e9f);
		for (int m = 0; m < rng.range_int(120, 240); ++m) {
			const int elev = rng.range_int(18, 22);
			const auto c = spot();
			const auto area = seas_chain(hm, rng, c.first, c.second, 24, 28, (int)std::floor(seas_scale(5, 30, half)), 1e9f, 0.0f, {first},
			                             [&](int i) { return in_map(i) && out_not_mountain[(size_t)i] > 2.0f && to_mountain[(size_t)i] > 3.0f; });
			const float blend = (float)rng.range_int(18, 30);
			if (!seas_any(area)) continue;
			paint_mask_elevation(hm, area, metres(water_h, (float)elev), blend);
			record_area(hm, area, AREA_MOUNTAIN);
			seas_add(mountain, area);
			to_mountain = seas_dist_inside(hm, mountain);
		}
	}
	const auto in_mountain = seas_dist_outside(hm, mountain);
	for (int m = 0; m < rng.range_int(100, 180); ++m) {
		const int elev = rng.range_int(24, 38);
		const auto c = spot();
		const auto area = seas_chain(hm, rng, c.first, c.second, 6, 18, (int)std::floor(seas_scale(8, 15, half)), 1e9f, 0.0f, {first},
		                             [&](int i) { return in_map(i) && to_player[(size_t)i] > 2.0f && in_mountain[(size_t)i] >= 2.0f; });
		const float blend = (float)rng.range_int(30, 40);
		if (!seas_any(area)) continue;
		paint_mask_elevation(hm, area, metres(water_h, (float)elev), blend);
		record_area(hm, area, AREA_MOUNTAIN);
	}
	seas_bumps(hm, rng, seas_scale(100, 200, half), [&](int i) { return to_player[(size_t)i] > 10.0f && in_land[(size_t)i] > 5.0f; });
	seas_spawns(hm, spawns, locs, 14.0f, 6.0f, water_h, land);
}

void RmsGenerator::synth_islands(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players,
		const uint8_t *teams) {
	// islands.js: sea at -5 m, players on a circle at f = 0.35 each on a round
	// island (ClumpPlacer(diskArea(scaleByMapSize(20, 29)), 0.8, 0.1), 3 m,
	// blend 6) with a dock facing the centre; scaleByMapSize(4, 14) big and
	// scaleByMapSize(6, 54) small islands kept scaleByMapSize(8, 12) off all
	// land; 2 m bumps and scaleByMapSize(4, 13) hills of 18 m on the land
	// outside the player islands. Every island needs ships.
	water_h = 1.0f;
	const float half = hm.map_half, size = 2.0f * half;
	const float land = metres(water_h, 3.0f);
	hm.init(hm.res, hm.map_half, metres(water_h, -5.0f));
	const float island_r = seas_scale(20, 29, half);
	const auto locs = seas_circle_players(num_players, rng.range_float(0.0f, 2.0f * kSeasPi), 0.35f * size, teams);
	std::vector<uint8_t> land_mask(hm.data.size(), 0), player_mask(hm.data.size(), 0);
	auto in_map = [&](int i) { return seas_in_circle(hm, i); };
	for (const auto &l : locs) {
		const auto m = seas_clump(hm, rng, kSeasPi * island_r * island_r, 0.8f, 0.1f, 1e9f, l.first, l.second, in_map);
		paint_mask_elevation(hm, m, land, 6.0f);
		seas_add(land_mask, m);
		seas_add(player_mask, m);
	}
	const float gap = seas_scale(8, 12, half);
	auto to_land = seas_dist_inside(hm, land_mask);
	auto islands = [&](float amount, int min_r, int max_r, int circles) {
		seas_create_areas(hm, rng, amount, 10, [&](float x, float z) {
			const auto m = seas_chain(hm, rng, x, z, min_r, max_r, circles, 0.07f, 0.0f, {},
			                          [&](int i) { return in_map(i) && to_land[(size_t)i] > gap; });
			if (!seas_any(m)) return false;
			paint_mask_elevation(hm, m, land, 6.0f);
			seas_add(land_mask, m);
			to_land = seas_dist_inside(hm, land_mask);
			return true;
		});
	};
	islands(seas_scale(4, 14, half), (int)std::floor(seas_scale(4, 8, half)), (int)std::floor(seas_scale(8, 14, half)), (int)std::floor(seas_scale(25, 60, half)));
	islands(seas_scale(6, 54, half), (int)std::floor(seas_scale(4, 7, half)), (int)std::floor(seas_scale(7, 10, half)), (int)std::floor(seas_scale(16, 40, half)));
	const auto in_land = seas_dist_outside(hm, land_mask);
	const auto to_player = seas_dist_inside(hm, player_mask);
	// Bumps: ClumpPlacer(scaleByMapSize(20, 50), 0.3, 0.06) raising 2 m.
	seas_create_areas(hm, rng, seas_scale(20, 100, half), 10, [&](float x, float z) {
		const auto m = seas_clump(hm, rng, seas_scale(20, 50, half), 0.3f, 0.06f, 1e9f, x, z,
		                          [&](int i) { return to_player[(size_t)i] > 0.0f && in_land[(size_t)i] > 3.0f; });
		if (!seas_any(m)) return false;
		seas_paint_modify(hm, m, 2.0f * kMetre, 2.0f);
		return true;
	});
	seas_hills(hm, rng, water_h, seas_scale(4, 13, half), false, 15.0f, 18.0f,
	           [&](int i) { return to_player[(size_t)i] > 2.0f && land_mask[(size_t)i]; });
	seas_spawns(hm, spawns, locs, island_r * 0.6f, 4.0f, water_h, land);
}

void RmsGenerator::synth_land_grab(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players,
		const uint8_t *teams) {
	// land_grab.js: sea at -5 m; players (sortAllPlayers) on half a circle at
	// f = 0.35 (playerPlacementCustomAngle, start - pi (i + 1) / (n + 1)), each
	// on a round island (ClumpPlacer(diskArea(defaultPlayerBaseRadius()), 0.8,
	// 0.1), 3 m, blend 4) with a dock facing the centre; on the other side a
	// continent (ClumpPlacer(diskArea(0.4 of the map), 0.8, 0.08) 0.38 of the
	// map from the centre, 8 off the islands), its shore roughened by
	// scaleByMapSize(2, 15) x 20 clumps of scaleByMapSize(15, 80) tiles within
	// 6 inside or 3 outside it; 2 m bumps and scaleByMapSize(1, 4) x players
	// 18 m hills (ClumpPlacer(scaleByMapSize(20, 150), 0.2, 0.1), 15 apart, 7
	// inside the continent). Reaching the continent needs ships.
	water_h = 1.0f;
	const float half = hm.map_half, size = 2.0f * half;
	const float land = metres(water_h, 3.0f);
	hm.init(hm.res, hm.map_half, metres(water_h, -5.0f));
	const float start = rng.range_float(0.0f, 2.0f * kSeasPi);
	const float base_r = seas_scale(15, 25, half); // defaultPlayerBaseRadius
	std::vector<int> order((size_t)num_players);
	for (int i = 0; i < num_players; ++i) order[(size_t)i] = i;
	if (teams) std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return teams[a] < teams[b]; });
	std::vector<std::pair<float, float>> locs((size_t)num_players);
	for (int k = 0; k < num_players; ++k)
		locs[(size_t)order[(size_t)k]] = polar(start - kSeasPi * (float)(k + 1) / (float)(num_players + 1), 0.35f * size);
	auto in_map = [&](int i) { return seas_in_circle(hm, i); };
	std::vector<uint8_t> island(hm.data.size(), 0), land_mask(hm.data.size(), 0);
	for (const auto &l : locs) {
		const auto m = seas_clump(hm, rng, kSeasPi * base_r * base_r, 0.8f, 0.1f, 1e9f, l.first, l.second, in_map);
		paint_mask_elevation(hm, m, land, 4.0f);
		seas_add(island, m);
	}
	const auto to_island = seas_dist_inside(hm, island);
	// The continent body, opposite the players.
	const auto cont = polar(start + kSeasPi / 2.0f, 0.38f * size);
	const float cont_r = 0.4f * size;
	{
		const auto m = seas_clump(hm, rng, kSeasPi * cont_r * cont_r, 0.8f, 0.08f, 1e9f, cont.first, cont.second,
		                          [&](int i) { return in_map(i) && to_island[(size_t)i] > 8.0f; });
		paint_mask_elevation(hm, m, land, 4.0f);
		seas_add(land_mask, m);
	}
	// Shore jaggedness: borderClasses(clLand, 6, 3), avoidClasses(clIsland, 8).
	{
		const auto in_land = seas_dist_outside(hm, land_mask);
		const auto to_land = seas_dist_inside(hm, land_mask);
		auto border = [&](int i) {
			return in_map(i) && to_island[(size_t)i] > 8.0f &&
			       (land_mask[(size_t)i] ? in_land[(size_t)i] <= 6.0f : to_land[(size_t)i] <= 3.0f);
		};
		std::vector<uint8_t> added(hm.data.size(), 0);
		seas_create_areas(hm, rng, seas_scale(2, 15, half) * 20.0f, 150, [&](float x, float z) {
			const auto m = seas_clump(hm, rng, seas_scale(15, 80, half), 0.2f, 0.1f, 1e9f, x, z, border);
			if (!seas_any(m)) return false;
			paint_mask_elevation(hm, m, land, 4.0f);
			seas_add(added, m);
			return true;
		});
		seas_add(land_mask, added);
	}
	const auto in_land = seas_dist_outside(hm, land_mask);
	// Bumps: ClumpPlacer(scaleByMapSize(20, 50), 0.3, 0.06), +2 m, 10 off the islands, 3 inside the land.
	seas_create_areas(hm, rng, seas_scale(100, 200, half), 10, [&](float x, float z) {
		const auto m = seas_clump(hm, rng, seas_scale(20, 50, half), 0.3f, 0.06f, 1e9f, x, z,
		                          [&](int i) { return to_island[(size_t)i] > 10.0f && in_land[(size_t)i] > 3.0f; });
		if (!seas_any(m)) return false;
		seas_paint_modify(hm, m, 2.0f * kMetre, 2.0f);
		return true;
	});
	// Hills: 18 m, 15 apart, 7 inside the land.
	std::vector<uint8_t> hills(hm.data.size(), 0);
	std::vector<float> to_hill(hm.data.size(), 1e9f);
	seas_create_areas(hm, rng, seas_scale(1, 4, half) * (float)num_players, 10, [&](float x, float z) {
		const auto m = seas_clump(hm, rng, seas_scale(20, 150, half), 0.2f, 0.1f, 1e9f, x, z, [&](int i) {
			return to_island[(size_t)i] > 10.0f && to_hill[(size_t)i] > 15.0f && in_land[(size_t)i] > 7.0f;
		});
		if (!seas_any(m)) return false;
		paint_mask_elevation(hm, m, metres(water_h, 18.0f), 2.0f);
		record_area(hm, m, AREA_HILL);
		seas_add(hills, m);
		to_hill = seas_dist_inside(hm, hills);
		return true;
	});
	seas_spawns(hm, spawns, locs, base_r * 0.6f, 4.0f, water_h, land);
}

void RmsGenerator::synth_the_nile(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players,
		const uint8_t *teams) {
	// the_nile.js: the Nile 0.1 of the map wide through the centre at a random
	// angle (paintRiver "parallel", bed -3 m, banks 2 m, fade scaleByMapSize(3,
	// 12), meanderShort 12, meanderLong 50), players on both banks
	// (playerPlacementRiver, 0.4 of the map apart), 2 m bumps 2 off the water
	// and 6 off the bases, and round(scaleByMapSize(1, 4) x players / 2) ponds
	// of -7 m (ClumpPlacer(scaleByMapSize(2, 5) x 50, 0.8, 0.1), 25 off the
	// bases, 20 off the river, 10 apart). The script's land is 1 m; RTS0 keeps
	// it at 2 m, the banks' height, so it stays clear of the water line. Its
	// lush, half-lush and dry bank strips (0.04, 0.06 and 0.09 of the map from
	// the river) are one recorded area painted in from its far edge.
	water_h = 1.0f;
	const float half = hm.map_half, size = 2.0f * half;
	const float land = metres(water_h, 2.0f);
	hm.init(hm.res, hm.map_half, land);
	const float angle = rng.range_float(0.0f, 2.0f * kSeasPi);
	const float nx = det::cos(angle), nz = det::sin(angle); // across the river
	const float ux = -nz, uz = nx;                          // along it
	// playerPlacementRiver(angle, 0.4 * size).
	std::vector<std::pair<float, float>> locs((size_t)num_players);
	const bool even = num_players % 2 == 0;
	for (int i = 0; i < num_players; ++i) {
		const bool cur_even = i % 2 == 0;
		const int divident = (even || cur_even) ? (i + 1) % 2 : 0;
		const int divisor = even ? 0 : (cur_even ? 1 : -1);
		const float across = (i % 2 ? 0.2f : -0.2f) * size;
		const float f = ((float)(i - 1 + divident) / 2.0f + 1.0f) / ((float)(num_players + divisor) / 2.0f + 1.0f);
		const float along = (f - 0.5f) * size;
		locs[(size_t)i] = {ux * along + nx * across, uz * along + nz * across};
	}
	group_players_by_area(locs, num_players, teams);
	const float pclass = seas_scale(15, 25, half) / 3.0f;
	const auto to_player = seas_dist_to_players(hm, locs, pclass);
	seas_paint_river(hm, rng, ux * half, uz * half, -ux * half, -uz * half, 0.1f * size, seas_scale(3, 12, half),
	                 metres(water_h, -3.0f), land, 12.0f, 50.0f, true);
	std::vector<uint8_t> water(hm.data.size());
	for (size_t i = 0; i < water.size(); ++i) water[i] = hm.data[i] < metres(water_h, 0.0f);
	const auto to_water = seas_dist_inside(hm, water);
	// The bank strips: the river and everything within 0.09 of the map of it.
	{
		std::vector<uint8_t> banks(hm.data.size());
		for (size_t i = 0; i < banks.size(); ++i) banks[i] = to_water[i] < 0.09f * size;
		record_area(hm, banks, AREA_MARSH);
	}
	seas_create_areas(hm, rng, seas_scale(100, 200, half), 10, [&](float x, float z) {
		const auto m = seas_clump(hm, rng, seas_scale(20, 50, half), 0.3f, 0.06f, 1e9f, x, z,
		                          [&](int i) { return to_water[(size_t)i] > 2.0f && to_player[(size_t)i] > 6.0f; });
		if (!seas_any(m)) return false;
		seas_paint_modify(hm, m, 2.0f * kMetre, 2.0f);
		return true;
	});
	// Ponds.
	std::vector<uint8_t> ponds(hm.data.size(), 0);
	std::vector<float> to_pond(hm.data.size(), 1e9f);
	seas_create_areas(hm, rng, std::round(seas_scale(1, 4, half) * (float)num_players / 2.0f), 10, [&](float x, float z) {
		const auto m = seas_clump(hm, rng, seas_scale(2, 5, half) * 50.0f, 0.8f, 0.1f, 1e9f, x, z, [&](int i) {
			return seas_in_circle(hm, i) && to_player[(size_t)i] > 25.0f && to_water[(size_t)i] > 20.0f && to_pond[(size_t)i] > 10.0f;
		});
		if (!seas_any(m)) return false;
		paint_mask_elevation(hm, m, metres(water_h, -7.0f), 4.0f);
		seas_add(ponds, m);
		to_pond = seas_dist_inside(hm, ponds);
		return true;
	});
	seas_spawns(hm, spawns, locs, 16.0f, 6.0f, water_h, land);
}

void RmsGenerator::synth_cycladic_archipelago(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns,
		int num_players, const uint8_t *teams) {
	// cycladic_archipelago.js: sea at -5 m; max(6, players) islands of
	// scaleByMapSize(15, 40) tiles radius (ClumpPlacer 0.7, 0.1, 3 m, blend
	// 5) on a circle at f = 0.39, players on the ones the script's
	// startingPlaces table picks (all of them from 6 players); one central
	// island of scaleByMapSize(15, 30) per player at f = 0.1-0.16 spread over
	// floor(scaleByMapSize(1, 4)) directions; 2 m bumps and scaleByMapSize(5,
	// 30) hills of 12 m grown from the islands, 15 off the city patches.
	// Every island needs ships.
	water_h = 1.0f;
	const float half = hm.map_half, size = 2.0f * half;
	const float land = metres(water_h, 3.0f);
	hm.init(hm.res, hm.map_half, metres(water_h, -5.0f));
	static const int kStarting[6][6] = {{0}, {0, 3}, {0, 2, 4}, {0, 1, 3, 4}, {0, 1, 2, 3, 4}, {0, 1, 2, 3, 4, 5}};
	const float start = rng.range_float(0.0f, 2.0f * kSeasPi);
	const float island_r = seas_scale(15, 40, half);
	const int island_count = std::max(6, num_players);
	const float central_r = seas_scale(15, 30, half);
	const int central_count = std::max(1, (int)std::floor(seas_scale(1, 4, half)));
	std::vector<std::pair<float, float>> central((size_t)num_players);
	for (int i = 0; i < num_players; ++i) {
		const float r = rng.range_float(0.1f, 0.16f) * size;
		central[(size_t)i] = polar(start + kSeasPi * ((float)i * 2.0f / (float)central_count + rng.range_float(-1.0f, 1.0f) / 8.0f), r);
	}
	std::vector<uint8_t> player_mask(hm.data.size(), 0), coral_mask(hm.data.size(), 0);
	std::vector<std::vector<int>> areas;
	std::vector<std::pair<float, float>> spots;
	auto island = [&](float x, float z, float r, float coral_r, bool player) {
		// Deep ocean rocks: a ring of coral tiles coral_r out (no elevation), kept off earlier rings and player islands.
		const auto coral = seas_clump(hm, rng, kSeasPi * (r + coral_r) * (r + coral_r), 0.7f, 0.1f, 1e9f, x, z,
		                              [&](int i) { return !coral_mask[(size_t)i] && !player_mask[(size_t)i]; });
		if (seas_any(coral)) {
			record_area(hm, coral, AREA_CORAL);
			seas_add(coral_mask, coral);
		}
		const auto m = seas_clump(hm, rng, kSeasPi * r * r, 0.7f, 0.1f, 1e9f, x, z, [&](int i) { return !player_mask[(size_t)i]; });
		paint_mask_elevation(hm, m, land, 5.0f);
		record_area(hm, m, AREA_ISLAND);
		std::vector<int> pts;
		for (size_t i = 0; i < m.size(); ++i) if (m[i]) pts.push_back((int)i);
		if (!pts.empty()) areas.push_back(std::move(pts));
		if (player) seas_add(player_mask, m);
	};
	int n_player = 0;
	for (int i = 0; i < island_count; ++i) {
		const auto p = polar(start + 2.0f * kSeasPi * (float)i / (float)island_count, 0.39f * size);
		const bool is_player = num_players >= 6 || (n_player < num_players && i == kStarting[num_players - 1][n_player]);
		if (is_player) { spots.push_back(p); ++n_player; }
		island(p.first, p.second, island_r, seas_scale(1, 5, half), is_player);
	}
	for (const auto &c : central) island(c.first, c.second, central_r, 2.0f, false);
	// placePlayerBases with sortAllPlayers(): spots in team order.
	std::vector<int> order((size_t)num_players);
	for (int i = 0; i < num_players; ++i) order[(size_t)i] = i;
	if (teams) std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return teams[a] < teams[b]; });
	std::vector<std::pair<float, float>> locs((size_t)num_players);
	for (int k = 0; k < num_players; ++k) locs[(size_t)order[(size_t)k]] = spots[(size_t)k];
	const auto to_city = seas_dist_to_players(hm, locs, 6.0f);
	// createAreasInAreas: centres on a random point of a random island.
	auto in_areas = [&](float amount, int retry, auto place) {
		int placed = 0;
		for (int tries = 0; (float)placed < amount && (float)tries < amount * (float)retry && !areas.empty(); ++tries) {
			const auto &a = areas[(size_t)rng.range_int(0, (int)areas.size() - 1)];
			float x, z;
			seas_world(hm, a[(size_t)rng.range_int(0, (int)a.size() - 1)], x, z);
			if (place(x, z)) ++placed;
		}
	};
	in_areas(seas_scale(25, 75, half), 15, [&](float x, float z) {
		const auto m = seas_clump(hm, rng, seas_scale(20, 60, half), 0.3f, 0.06f, 1e9f, x, z, [&](int i) { return to_city[(size_t)i] > 0.0f; });
		if (!seas_any(m)) return false;
		seas_paint_modify(hm, m, 2.0f * kMetre, 3.0f);
		return true;
	});
	std::vector<uint8_t> hills(hm.data.size(), 0);
	std::vector<float> to_hill(hm.data.size(), 1e9f);
	in_areas(seas_scale(5, 30, half), 15, [&](float x, float z) {
		const auto m = seas_clump(hm, rng, seas_scale(20, 150, half), 0.2f, 0.1f, 1e9f, x, z,
		                          [&](int i) { return to_city[(size_t)i] > 15.0f && to_hill[(size_t)i] > 15.0f; });
		if (!seas_any(m)) return false;
		paint_mask_elevation(hm, m, metres(water_h, 12.0f), 2.0f);
		record_area(hm, m, AREA_HILL);
		seas_add(hills, m);
		to_hill = seas_dist_inside(hm, hills);
		return true;
	});
	seas_spawns(hm, spawns, locs, std::min(14.0f, island_r * 0.7f), 4.0f, water_h, land);
}

void RmsGenerator::synth_snowflake_searocks(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns,
		int num_players, const uint8_t *teams) {
	// snowflake_searocks.js: sea at -5 m, players on a circle at f = 0.35 on
	// rocky islands of 20 m (ClumpPlacer(diskArea(scaleByMapSize(15, 30)),
	// 0.95, 0.6), blend 2), more islands between and in front of them by the
	// script's per-size layouts (tiny: one central island; up to 192 tiles, or
	// many players on mid sizes: one island in front of each player; else a
	// central island, islands between the players, between each player and
	// the centre and at the map border), all joined by 11-tile causeways at
	// 20 m (PathPlacer), so every base is reachable on foot.
	water_h = 1.0f;
	const float half = hm.map_half, size = 2.0f * half;
	const float top = metres(water_h, 20.0f);
	hm.init(hm.res, hm.map_half, metres(water_h, -5.0f));
	const float pr = seas_scale(15, 30, half);
	const float start = rng.range_float(0.0f, 2.0f * kSeasPi);
	const auto locs = seas_circle_players(num_players, start, 0.35f * size, teams);
	const int n = num_players;
	std::vector<std::pair<float, float>> pos;
	std::vector<std::pair<int, int>> links;
	std::vector<float> isl_size;
	auto at = [&](int id) -> std::pair<float, float> & {
		if ((int)pos.size() <= id) { pos.resize((size_t)id + 1, {0.0f, 0.0f}); isl_size.resize((size_t)id + 1, 0.0f); }
		return pos[(size_t)id];
	};
	auto radial = [&](int player, int id, int offset, float dist, float sz) {
		const float a = start + (float)(player * 2 + offset) * kSeasPi / (float)n;
		at(id) = polar(a, dist * size);
		isl_size[(size_t)id] = sz;
	};
	const float between_center_dist = 0.16f, between_center_r = 0.81f, central_r = 0.36f;
	std::vector<int> order; // creation order of the non-player islands
	auto with_center = [&](float tert_dist, float tert_r, float between_dist, float between_r) {
		const int center = 4 * n;
		at(center) = {0.0f, 0.0f};
		isl_size[(size_t)center] = central_r;
		order.push_back(center);
		for (int p = 0; p < n; ++p) {
			const int nb = p + 1 < n ? p + 1 : 0;
			const int between = p + n, bc = p + 2 * n, bc_nb = nb + 2 * n, tert = p + 3 * n;
			links.push_back({between, p});
			links.push_back({between, nb});
			radial(p, between, 1, between_dist, between_r);
			order.push_back(between);
			links.push_back({bc, p});
			links.push_back({bc, center});
			links.push_back({bc, bc_nb});
			radial(p, bc, 0, between_center_dist, between_center_r);
			order.push_back(bc);
			links.push_back({tert, between});
			radial(p, tert, 1, tert_dist, tert_r);
			order.push_back(tert);
		}
	};
	auto without_center = [&]() {
		for (int p = 0; p < n; ++p) {
			const int nb = p + 1 < n ? p + 1 : 0;
			links.push_back({p, nb});
			links.push_back({p, p + n});
			links.push_back({p + n, nb + n});
			radial(p, p + n, 0, between_center_dist, between_center_r);
			order.push_back(p + n);
		}
	};
	auto tiny = [&]() {
		at(n) = {0.0f, 0.0f};
		isl_size[(size_t)n] = 1.0f;
		order.push_back(n);
		for (int p = 0; p < n; ++p) links.push_back({p, n});
	};
	if (size <= 128.0f) tiny();
	else if (size <= 192.0f) without_center();
	else if (size <= 256.0f) { if (n < 6) with_center(0.41f, 0.49f, 0.26f, 1.0f); else without_center(); }
	else if (size <= 320.0f) { if (n < 8) with_center(0.41f, 0.49f, 0.26f, 1.0f); else without_center(); }
	else if (n < 6) with_center(0.41f, 0.49f, 0.24f, 1.0f);
	else with_center(0.41f, 0.36f, 0.28f, 0.81f);
	auto anywhere = [](int) { return true; };
	auto island = [&](float x, float z, float sz) {
		const auto m = seas_clump(hm, rng, sz * kSeasPi * pr * pr, 0.95f, 0.6f, 1e9f, x, z, anywhere);
		paint_mask_elevation(hm, m, top, 2.0f);
	};
	for (int id : order) island(pos[(size_t)id].first, pos[(size_t)id].second, isl_size[(size_t)id]);
	for (int p = 0; p < n; ++p) {
		at(p) = locs[(size_t)p];
		island(locs[(size_t)p].first, locs[(size_t)p].second, 1.0f);
	}
	// The script loops over isConnected[i][j] in island order.
	std::sort(links.begin(), links.end());
	links.erase(std::unique(links.begin(), links.end()), links.end());
	for (const auto &l : links) {
		const auto &a = pos[(size_t)l.first], &b = pos[(size_t)l.second];
		const auto m = seas_path(hm, rng, a.first, a.second, b.first, b.second, 11.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1e9f, anywhere);
		paint_mask_elevation(hm, m, top, 2.0f);
	}
	seas_spawns(hm, spawns, locs, std::min(14.0f, pr * 0.7f), 4.0f, water_h, top);
}

void RmsGenerator::synth_harbor(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players,
		const uint8_t *teams) {
	// harbor.js: land at 2 m, players on a circle at f = 0.38; a central lake
	// (ChainPlacer(2, floor(scaleByMapSize(2, 12)), floor(scaleByMapSize(35,
	// 160)), first circle 0.2 of the map) at -18 m, blend 10, 20 off the
	// bases; on maps of 192 tiles or more a harbor in front of each base
	// (ClumpPlacer(1200, 0.5, 0.5) 2/5 of the way to the centre, lowered 11 m,
	// 15 off the bases); and rocky spines (PathPlacer, scaleByMapSize(14,
	// 0.02 or 0.5) wide, tapered, raised 20 m or 35 m) from f = 0.12 to 0.4
	// between the players, dropped where they would touch a base. The bases
	// meet on foot round the ends of the spines. 0 A.D. metres x 0.25.
	water_h = 1.0f;
	const float half = hm.map_half, size = 2.0f * half;
	const float land = metres(water_h, 2.0f);
	hm.init(hm.res, hm.map_half, land);
	const float start = rng.range_float(0.0f, 2.0f * kSeasPi);
	const auto locs = seas_circle_players(num_players, start, 0.38f * size, teams);
	const float pclass = seas_scale(15, 25, half) / 3.0f;
	const auto to_player = seas_dist_to_players(hm, locs, pclass);
	{
		const auto m = seas_chain(hm, rng, 0.0f, 0.0f, 2, (int)std::floor(seas_scale(2, 12, half)), (int)std::floor(seas_scale(35, 160, half)),
		                          1e9f, 0.0f, {(int)std::floor(0.2f * size)}, [&](int i) { return to_player[(size_t)i] > 20.0f; });
		paint_mask_elevation(hm, m, metres(water_h, -18.0f), 10.0f);
	}
	if (size >= 192.0f) {
		for (const auto &l : locs) {
			const float hx = std::round(l.first - l.first / 2.5f), hz = std::round(l.second - l.second / 2.5f);
			const auto m = seas_clump(hm, rng, 1200.0f, 0.5f, 0.5f, 1e9f, hx, hz, [&](int i) { return to_player[(size_t)i] > 15.0f; });
			seas_paint_modify(hm, m, -11.0f * kMetre, 3.0f);
		}
	}
	const bool small = size <= 192.0f;
	const float spine_w = seas_scale(14.0f, small ? 0.02f : 0.5f, half);
	const float taper = small ? -0.1f : -1.4f, rise = small ? 20.0f : 35.0f;
	const int split = (num_players <= 3 || (size >= 320.0f && num_players <= 4)) ? 2 : 1;
	for (int i = 0; i < num_players * split; ++i) {
		const float a = start + ((float)i + 0.5f) * 2.0f * kSeasPi / (float)(num_players * split);
		const auto s = polar(a, 0.12f * size), e = polar(a, 0.4f * size);
		const auto m = seas_path(hm, rng, s.first, s.second, e.first, e.second, spine_w, 0.6f, 0.1f, 0.4f, taper, 0.0f,
		                         [&](int k) { return to_player[(size_t)k] > 5.0f; });
		if (seas_any(m)) seas_paint_modify(hm, m, rise * kMetre, 3.0f);
	}
	// rmgen2's random hills (its mountains, plateaus and bluffs are left out).
	std::vector<PlayerSpawn> probe;
	for (const auto &l : locs) probe.push_back({0, l.first, l.second, land, 0.0f, 0.0f});
	scatter_dry_hills(hm, rng, probe, scale_by_map_size(1, 4, half) * num_players, pclass + 20.0f, 2.0f, water_h);
	seas_spawns(hm, spawns, locs, 16.0f, 6.0f, water_h, land);
}

// =========================================================================
// 0 A.D. coast maps: Corsica vs Sardinia, Phoenician Levant, Marmara,
// Bahrain, Botswanan Haven, Scythian Rivulet
// =========================================================================

namespace {

// 0 A.D. height in metres (water level 0) -> RTS0 height, for maps whose
// shallows matter. Land is metres() (x 0.25) lifted by up to 0.25 over its
// first metre, so 0 A.D.'s low 1 m plains stay dry land in RTS0's nav grid
// (water below water_h + 0.3) and keep their resources. 0 to -2 m, where 0
// A.D.'s land units still wade (MaxWaterDepth 2), is squeezed into RTS0's
// wadeable shallows; deeper water drops on at 0.25 per metre.
inline float coast_metres(float water_h, float m) {
	if (m >= 0.0f) return water_h + m * kMetre + 0.25f * std::min(m, 1.0f);
	const float wade = wade_height(water_h);
	if (m >= -2.0f) return water_h + (water_h - wade) * m / 2.0f;
	return wade + (m + 2.0f) * kMetre;
}

// The inverse of coast_metres.
inline float coast_unmetres(float water_h, float h) {
	const float d = h - water_h, wade = wade_height(water_h);
	if (d >= 0.0f) return d <= kMetre + 0.25f ? d / (kMetre + 0.25f) : (d - 0.25f) / kMetre;
	if (h >= wade) return 2.0f * d / (water_h - wade);
	return -2.0f + (h - wade) / kMetre;
}

// rmgen scaleByMapSize without rounding (0 A.D. keeps the fraction).
inline float coast_sbms(float lo, float hi, float half) {
	return lo + (hi - lo) * std::clamp((2.0f * half - 128.0f) / 384.0f, 0.0f, 1.0f);
}

// Vector2D.rotate(a) (clockwise by a); rotateAround(a, centre) on centred
// coordinates. Vector2D(r, 0).rotate(-a) is polar(a, r).
inline std::pair<float, float> coast_rot(float x, float z, float a) {
	const float c = det::cos(a), s = det::sin(a);
	return {x * c + z * s, -x * s + z * c};
}

// sortAllPlayers: player indices ordered by game-setup team, so team-mates
// take neighbouring start spots.
std::vector<int> coast_team_order(int num_players, const uint8_t *teams) {
	std::vector<int> order(num_players);
	for (int i = 0; i < num_players; ++i) order[i] = i;
	if (teams) std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return teams[a] < teams[b]; });
	return order;
}

// gaia_terrain.js rndRiver: the river meander curve.
double coast_rnd_river(double f, double seed) {
	double rw = seed;
	for (int i = 0; i <= f; ++i) rw = 10.0 * std::fmod(rw, 1.0);
	const double rr = std::fmod(f, 1.0);
	double ret = (((long long)std::floor(f)) % 2 ? -1.0 : 1.0) * rr * (rr - 1.0);
	switch ((int)std::floor(rw) % 5) {
		case 0: ret *= 2.3 * (rr - 0.5) * (rr - 0.5); break;
		case 1: ret *= 2.6 * (rr - 0.3) * (rr - 0.7); break;
		case 2: ret *= 22.0 * (rr - 0.2) * (rr - 0.3) * (rr - 0.3) * (rr - 0.8); break;
		case 3: ret *= 180.0 * (rr - 0.2) * (rr - 0.2) * (rr - 0.4) * (rr - 0.6) * (rr - 0.6) * (rr - 0.8); break;
		default: ret *= 2.6 * (rr - 0.5) * (rr - 0.7); break;
	}
	return ret;
}

// gaia_terrain.js paintRiver: water `width` wide along start-end, the
// shores meandering by two rndRiver curves (one when `parallel`), each
// vertex jittered by up to `deviation`, the bed at `bed` rising to `land`
// over `fade` tiles at the shores. Only the river's own vertices change.
void coast_paint_river(HeightmapGrid &hm, RmsRng &rng, float sx, float sz, float ex, float ez, float width, float fade,
		float deviation, float bed, float land, float meander_short, float meander_long, bool parallel) {
	const float half = hm.map_half, size = 2.0f * half;
	const float ms = size * meander_short / coast_sbms(35.0f, 160.0f, half);
	const float ml = size * meander_long / coast_sbms(35.0f, 100.0f, half);
	const double seed1 = rng.range_float(2.0f, 3.0f), seed2 = rng.range_float(2.0f, 3.0f);
	const double a1 = rng.range_float(0.0f, 1.0f), a2 = rng.range_float(0.0f, 1.0f);
	auto curve = [&](float frac, double a, double seed) {
		return (float)(ms * coast_rnd_river(a + size * frac / 128.0f, seed) + ml * coast_rnd_river(a + size * frac / 256.0f, seed));
	};
	const float len = det::hypot(ex - sx, ez - sz);
	const float ux = (sx - ex) / len, uz = (sz - ez) / len;
	const float eps = 0.01f;
	const float minx = std::min(sx, ex) - eps, maxx = std::max(sx, ex) + eps;
	const float minz = std::min(sz, ez) - eps, maxz = std::max(sz, ez) + eps;
	for (int z = 0; z < hm.res; ++z)
		for (int x = 0; x < hm.res; ++x) {
			float wx, wz;
			hm.grid_to_world(x, z, wx, wz);
			// distanceOfPointFromLine: (start - end).normalize() x (p - end).
			const float d = ux * (wz - ez) - uz * (wx - ex);
			const float fx = wx + uz * d, fz = wz - ux * d;
			if (fx < minx || fx > maxx || fz < minz || fz > maxz) continue;
			const float frac = det::hypot(fx - sx, fz - sz) / len;
			const float c1 = curve(frac, a1, seed1);
			const float c2 = parallel ? c1 : curve(frac, a2, seed2);
			const float dev = deviation != 0.0f ? deviation * rng.range_float(-1.0f, 1.0f) : 0.0f;
			const float s1 = c1 + d - dev - width / 2.0f, s2 = c2 + d - dev + width / 2.0f;
			if (!(s1 < 0.0f && s2 > 0.0f)) continue;
			float h = bed;
			if (s1 > -fade) h += (land - bed) * (1.0f + s1 / fade);
			else if (s2 < fade) h += (land - bed) * (1.0f - s2 / fade);
			hm.set(x, z, h);
		}
}

// gaia_terrain.js createPassage: a strip from start to end, startWidth to
// endWidth wide, set to the heights at its ends (or start_h / end_h),
// eased into the ground over its outer `smooth` tiles. Marks it in `mark`.
void coast_passage(HeightmapGrid &hm, float sx, float sz, float ex, float ez, float sw, float ew, float smooth,
		std::vector<uint8_t> *mark = nullptr, float start_h = NAN, float end_h = NAN) {
	const float half = hm.map_half;
	const float sh = std::isnan(start_h) ? hm.sample_world(std::clamp(sx, -half, half), std::clamp(sz, -half, half)) : start_h;
	const float eh = std::isnan(end_h) ? hm.sample_world(std::clamp(ex, -half, half), std::clamp(ez, -half, half)) : end_h;
	const float len = det::hypot(ex - sx, ez - sz);
	if (len < 1e-3f) return;
	const float dx = (ex - sx) / len, dz = (ez - sz) / len;
	for (int z = 0; z < hm.res; ++z)
		for (int x = 0; x < hm.res; ++x) {
			float wx, wz;
			hm.grid_to_world(x, z, wx, wz);
			const float along = (wx - sx) * dx + (wz - sz) * dz;
			if (along < 0.0f || along > len) continue;
			const float t = along / len;
			const float hw = (sw + (ew - sw) * t) / 2.0f;
			const float off = std::fabs((wx - sx) * -dz + (wz - sz) * dx);
			if (off > hw) continue;
			const float ph = sh + (eh - sh) * t;
			const float sd = smooth + off - hw;
			float &h = hm.data[(size_t)z * (size_t)hm.res + (size_t)x];
			h = sd > 0.0f ? (h * sd + ph / sd) / (sd + 1.0f / sd) : ph;
			if (mark) (*mark)[(size_t)z * (size_t)hm.res + (size_t)x] = 1;
		}
}

// SmoothElevationPainter(ELEVATION_MODIFY, dh, blend) over `mask`.
void coast_modify_mask(HeightmapGrid &hm, const std::vector<uint8_t> &mask, float dh, float blend) {
	std::vector<uint8_t> outside(mask.size());
	for (size_t i = 0; i < mask.size(); ++i) outside[i] = !mask[i];
	const auto d = distance_to_mask(outside, hm.res, grid_spacing(hm));
	for (size_t i = 0; i < mask.size(); ++i)
		if (mask[i]) hm.data[i] += dh * std::clamp(d[i] / std::max(blend, 1e-3f), 0.0f, 1.0f);
}

// createAreas(ClumpPlacer(area), SmoothElevationPainter(ELEVATION_MODIFY,
// dh_m metres, blend), constraint, count): `count` tries at random spots,
// kept where ok(x, z, r) holds. Heights change in 0 A.D. metres
// (coast_metres), so a bump in 2 m deep water stays at the water line.
template <class Ok>
void coast_bumps(HeightmapGrid &hm, RmsRng &rng, float water_h, int count, float area, float dh_m, float blend, Ok ok) {
	const float half = hm.map_half, r = std::sqrt(area / 3.14159265f), sp = grid_spacing(hm);
	for (int i = 0; i < count; ++i) {
		const float bx = rng.range_float(-half, half), bz = rng.range_float(-half, half);
		if (!ok(bx, bz, r)) continue;
		const int x0 = std::max(0, (int)((bx - r + half) / sp)), x1 = std::min(hm.res - 1, (int)((bx + r + half) / sp) + 1);
		const int z0 = std::max(0, (int)((bz - r + half) / sp)), z1 = std::min(hm.res - 1, (int)((bz + r + half) / sp) + 1);
		for (int z = z0; z <= z1; ++z)
			for (int x = x0; x <= x1; ++x) {
				const float wx = -half + (float)x * sp, wz = -half + (float)z * sp;
				const float d = det::hypot(wx - bx, wz - bz);
				if (d > r) continue;
				float &h = hm.data[(size_t)z * (size_t)hm.res + (size_t)x];
				h = coast_metres(water_h, coast_unmetres(water_h, h) + dh_m * std::clamp((r - d) / std::max(blend, 1e-3f), 0.0f, 1.0f));
			}
	}
}

// createAreas(ChainPlacer(min_r, max_r, circles, fail), painter, constraint,
// count): each try grows a chain of circles from a random spot; vertices in
// `blocked` or within `gap` of an area placed before drop out, and the try
// fails when more than `fail` of them do. `paint` gets each kept area.
template <class Paint>
std::vector<uint8_t> coast_chain_areas(HeightmapGrid &hm, RmsRng &rng, int count, float min_r, float max_r, int circles,
		float fail, const std::vector<uint8_t> &blocked, float gap, Paint paint) {
	const float half = hm.map_half, sp = grid_spacing(hm);
	std::vector<uint8_t> placed(hm.data.size(), 0);
	std::vector<float> to_placed(hm.data.size(), 1e9f);
	for (int i = 0; i < count; ++i) {
		const float b_ = rng.range_float(-half, half); const float a_ = rng.range_float(-half, half); auto m = chain_placer(hm, rng, a_, b_, std::max(1.0f, min_r), std::max(1.0f, max_r),
		                      std::max(1, circles));
		int in = 0, bad = 0;
		for (size_t k = 0; k < m.size(); ++k) {
			if (!m[k]) continue;
			++in;
			if (blocked[k] || to_placed[k] < gap) { m[k] = 0; ++bad; }
		}
		if (!in || bad > (int)((float)in * fail) || bad == in) continue;
		paint(m);
		for (size_t k = 0; k < m.size(); ++k) if (m[k]) placed[k] = 1;
		to_placed = distance_to_mask(placed, hm.res, sp);
	}
	return placed;
}

// Vertices within r of any spawn location.
std::vector<uint8_t> coast_near_spots(const HeightmapGrid &hm, const std::vector<std::pair<float, float>> &locs, float r) {
	std::vector<uint8_t> m(hm.data.size(), 0);
	for (const auto &l : locs) mask_disk(hm, m, l.first, l.second, r);
	return m;
}

inline bool coast_near(const std::vector<std::pair<float, float>> &locs, float x, float z, float r) {
	for (const auto &l : locs)
		if (det::hypot(x - l.first, z - l.second) < r) return true;
	return false;
}

// RTS0's distribute_player_bases lays a base out round its forward
// direction (the map centre). On a coast that can put the mines and the
// woodline in the sea, so each base turns to whichever of 16 directions
// keeps most of that layout on dry land, the one nearest its old facing on
// ties.
void coast_face_inland(const HeightmapGrid &hm, std::vector<RmsGenerator::PlayerSpawn> &spawns, float dry_h) {
	static const float layout[][2] = {
		{8.5f, 0.0f}, {9.5f, 0.0f}, {-6.5f, 6.0f}, {-6.5f, -6.0f}, {5.5f, 6.5f}, {-10.5f, 3.5f}, {-14.5f, 3.5f}, {-10.5f, 7.5f},
		{3.5f, -11.5f}, {-8.5f, -9.5f}, {22.0f, 6.0f}, {18.0f, -16.0f}, {-14.0f, -5.0f}, {-16.5f, -5.0f}, {19.0f, -5.0f}};
	const float half = hm.map_half - 1.0f;
	for (auto &sp : spawns) {
		int best = -1;
		float best_dot = -2.0f, bfx = sp.forward_dir_x, bfz = sp.forward_dir_z;
		for (int k = 0; k < 16; ++k) {
			const float a = (float)k * 0.39269908f, fx = det::cos(a), fz = det::sin(a), rx = -fz, rz = fx;
			int dry = 0;
			for (const auto &p : layout) {
				const float x = sp.x + fx * p[0] + rx * p[1], z = sp.z + fz * p[0] + rz * p[1];
				dry += std::fabs(x) < half && std::fabs(z) < half && hm.sample_world(x, z) >= dry_h;
			}
			const float dot = fx * sp.forward_dir_x + fz * sp.forward_dir_z;
			if (dry > best || (dry == best && dot > best_dot)) { best = dry; best_dot = dot; bfx = fx; bfz = fz; }
		}
		sp.forward_dir_x = bfx;
		sp.forward_dir_z = bfz;
	}
}

// Moves vertices above `line` up by `lift` (eased in over the first 0.15),
// so the low coasts of the heightmap maps (0-15 m, x 0.25, scaled down on
// small maps) stay dry land in RTS0's nav grid.
void coast_lift_land(HeightmapGrid &hm, float line, float lift) {
	for (float &h : hm.data)
		if (h > line) h += lift * smoothstep(0.0f, 0.15f, h - line);
}

const std::vector<uint8_t> &marmara_image() {
	static const std::vector<uint8_t> img = decode_heightmap_image(heightmap_images::marmara_b64, heightmap_images::marmara_size);
	return img;
}

const std::vector<uint8_t> &bahrain_image() {
	static const std::vector<uint8_t> img = decode_heightmap_image(heightmap_images::bahrain_b64, heightmap_images::bahrain_size);
	return img;
}

// marmara.js / bahrain.js up to player placement: LoadHeightmapImage(0,
// max_m), everything at or below 0 m lowered to heightScale(sea_m) with a
// sea_blend-tile blend, SmoothingPainter(1, smooth, 1), and the water
// (<= 0 m), land-in-the-circle and cliff (slope >= 2, 2 tiles from water)
// distance maps. Then the water line drops to setWaterHeight(
// scaleByMapSize(20, 18)), 0 to -2 m, and the land is lifted for RTS0.
struct CoastHeightmap {
	std::vector<float> to_water, to_cliff, to_not_land;
	std::vector<uint8_t> land;
};

CoastHeightmap coast_heightmap(HeightmapGrid &hm, float &water_h, const std::vector<uint8_t> &img, int n, float max_m,
		float sea_m, float sea_blend, float smooth) {
	const float base = 1.0f, half = hm.map_half, size = 2.0f * half, sp = grid_spacing(hm);
	paint_heightmap(hm, img, n, 0, 0, n, 0.0f, max_m, base);
	lower_sea_ground(hm, base + 0.001f, base + sea_m * size / 320.0f * kMetre, sea_blend);
	shepard_smooth(hm, smooth);
	CoastHeightmap out;
	std::vector<uint8_t> water(hm.data.size()), cliff(hm.data.size()), not_land(hm.data.size());
	out.land.assign(hm.data.size(), 0);
	for (size_t i = 0; i < hm.data.size(); ++i) water[i] = hm.data[i] <= base;
	out.to_water = distance_to_mask(water, hm.res, sp);
	const auto slope = slopes_m(hm);
	for (size_t i = 0; i < hm.data.size(); ++i) cliff[i] = out.to_water[i] > 2.0f && slope[i] >= 2.0f;
	out.to_cliff = distance_to_mask(cliff, hm.res, sp);
	for (int z = 0; z < hm.res; ++z)
		for (int x = 0; x < hm.res; ++x) {
			float wx, wz;
			hm.grid_to_world(x, z, wx, wz);
			const size_t i = (size_t)z * (size_t)hm.res + (size_t)x;
			out.land[i] = !water[i] && wx * wx + wz * wz <= half * half;
			not_land[i] = !out.land[i];
		}
	out.to_not_land = distance_to_mask(not_land, hm.res, sp);
	water_h = base + (coast_sbms(20.0f, 18.0f, half) - 20.0f) * kMetre;
	coast_lift_land(hm, water_h, 0.45f);
	return out;
}

// Players at random on `allowed` (playerPlacementRandom), grouped by team,
// each start flattened like "Flatten the initial CC area" and turned inland.
// RTS0 first asks for more room than 0 A.D. (its base layout is wider),
// then tries 0 A.D.'s own constraint `fallback`; when that gives up too (0
// A.D. would retry the whole map) it spreads the players over `fallback`
// by farthest points.
void coast_heightmap_players(HeightmapGrid &hm, RmsRng &rng, float water_h, std::vector<RmsGenerator::PlayerSpawn> &spawns,
		int num_players, const uint8_t *teams, const std::vector<uint8_t> &allowed, const std::vector<uint8_t> &fallback) {
	const float half = hm.map_half;
	std::vector<std::pair<float, float>> locs;
	if (!player_placement_random(hm, rng, allowed, num_players, locs) &&
	    !player_placement_random(hm, rng, fallback, num_players, locs)) {
		std::vector<std::pair<float, float>> cand;
		for (int z = 0; z < hm.res; ++z)
			for (int x = 0; x < hm.res; ++x) {
				if (!fallback[(size_t)z * (size_t)hm.res + (size_t)x]) continue;
				float wx, wz;
				hm.grid_to_world(x, z, wx, wz);
				cand.push_back({wx, wz});
			}
		if ((int)cand.size() < num_players) {
			place_ring_spawns(hm, spawns, num_players, half * 0.6f, rng.range_float(0.0f, 6.2831853f), 6.2831853f, water_h + 1.5f);
			for (const auto &s : spawns) lift_area(hm, s.x, s.z, 20.0f, 6.0f, s.base_height);
			return;
		}
		locs.assign(1, cand[(size_t)rng.range_int(0, (int)cand.size() - 1)]);
		while ((int)locs.size() < num_players) {
			float best = -1.0f;
			std::pair<float, float> pick = cand[0];
			for (const auto &c : cand) {
				float d = 1e9f;
				for (const auto &q : locs) d = std::min(d, det::hypot(c.first - q.first, c.second - q.second));
				if (d > best) { best = d; pick = c; }
			}
			locs.push_back(pick);
		}
	}
	group_players_by_area(locs, num_players, teams);
	const float base_r = (float)scale_by_map_size(15, 25, half) * 0.8f;
	make_heightmap_spawns(hm, spawns, locs, base_r, 6.0f, water_h);
	coast_face_inland(hm, spawns, water_h + 0.45f);
}

} // namespace

void RmsGenerator::synth_corsica(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players,
		const uint8_t *teams) {
	// corsica.js: -8 m sea; two islands of radius 0.46 of the map round two
	// opposite corners (which pair at random), each at 5 m with six sub-
	// islands of 0.09 along its edge, -5 m creeks at 0.6 and -1 m beaches
	// with passages out to 0.65; a +30 m main relief blended over 0.45 of
	// the map, a +9 m plateau (0.4) and, above 150 tiles, a +8 m one (0.2),
	// both with 20-tile passages. Half the players (sorted by team) on each
	// island, on an arc at 0.29 from its corner; bumps on the islands and
	// -5 m dips in the sea. Crossing needs ships. Records the islands, the
	// beach passages (clShore), the plateau passages (clPassage) and each
	// base's CityPatch (clSettlement) for its terrain tree.
	water_h = 1.0f;
	const float half = hm.map_half, size = 2.0f * half, pi = 3.14159265f;
	auto M = [&](float m) { return coast_metres(water_h, m); };
	hm.init(hm.res, half, M(-8.0f));
	const float swap = rng.range_int(0, 1) ? pi / 2.0f : 0.0f;
	const auto c0 = coast_rot(-0.45f * size, -0.45f * size, -swap);
	const std::pair<float, float> centres[2] = {c0, {-c0.first, -c0.second}};
	const float r_beach = 0.65f * size, r_creeks = 0.6f * size, r_island = 0.46f * size, r_level1 = 0.4f * size;
	const float r_player = 0.29f * size, r_level2 = 0.2f * size;
	const float nb_creeks = coast_sbms(7.0f, 9.0f, half), nb_beaches = coast_sbms(6.0f, 10.0f, half);
	const float nb_pass1 = coast_sbms(6.0f, 8.0f, half), nb_pass2 = coast_sbms(2.0f, 4.0f, half);
	const int nb_sub = 5;
	const size_t cells = hm.data.size();
	std::vector<uint8_t> island(cells, 0), passage(cells, 0), shore(cells, 0);
	auto at = [&](int k, float a, float r) {
		const auto p = polar(a, r);
		return std::make_pair(centres[k].first + p.first, centres[k].second + p.second);
	};
	for (int k = 0; k < 2; ++k) {
		const float cx = centres[k].first, cz = centres[k].second;
		{
			std::vector<uint8_t> m(cells, 0);
			mask_disk(hm, m, cx, cz, r_island);
			for (size_t i = 0; i < cells; ++i) if (m[i]) { hm.data[i] = M(5.0f); island[i] = 1; }
			record_area(hm, m, AREA_ISLAND);
		}
		for (int i = 0; i < nb_sub + 1; ++i) {
			const auto p = at(k, pi * ((float)k + (float)i / (float)(nb_sub * 2)) + swap, r_island);
			std::vector<uint8_t> m(cells, 0);
			mask_disk(hm, m, p.first, p.second, 0.09f * size);
			paint_mask_elevation(hm, m, M(5.0f), 1.0f);
			for (size_t j = 0; j < cells; ++j) if (m[j]) island[j] = 1;
			record_area(hm, m, AREA_ISLAND);
		}
		for (int i = 0; i < nb_creeks + 1.0f; ++i) {
			const auto p = at(k, pi * ((float)k + (float)i / (nb_creeks * 2.0f)) + swap, r_creeks);
			const float area = rng.range_int(0, 1) ? rng.range_float(10.0f, 50.0f) : coast_sbms(75.0f, 100.0f, half) + rng.range_float(0.0f, 20.0f);
			std::vector<uint8_t> m(cells, 0);
			mask_disk(hm, m, p.first, p.second, std::sqrt(area / pi));
			for (size_t j = 0; j < cells; ++j) if (m[j]) hm.data[j] = M(-5.0f);
		}
		for (int i = 0; i < nb_beaches + 1.0f; ++i) {
			const float a = pi * ((float)k + (float)i / (nb_beaches * 2.5f) + 1.0f / (nb_beaches * 6.0f) +
			                      rng.range_float(-1.0f, 1.0f) / (nb_beaches * 7.0f)) + swap;
			const auto s = at(k, a, r_island), e = at(k, a, r_beach);
			std::vector<uint8_t> m(cells, 0);
			mask_disk(hm, m, (s.first + 3.0f * e.first) / 4.0f, (s.second + 3.0f * e.second) / 4.0f, std::sqrt(130.0f / pi));
			paint_mask_elevation(hm, m, M(-1.0f), 5.0f);
			coast_passage(hm, s.first, s.second, e.first, e.second, 18.0f, 25.0f, 4.0f, &shore);
		}
		{
			std::vector<uint8_t> m(cells, 0);
			mask_disk(hm, m, cx, cz, r_island);
			coast_modify_mask(hm, m, 30.0f * kMetre, 0.45f * size);
		}
		{
			std::vector<uint8_t> m(cells, 0);
			mask_disk(hm, m, cx, cz, r_level1);
			coast_modify_mask(hm, m, 9.0f * kMetre, 1.0f);
		}
		for (int i = 0; i <= nb_pass1; ++i) {
			const float a = pi * ((float)i / 7.0f + 1.0f / 9.0f + (float)k) + swap;
			const auto s = at(k, a, r_level1 + 10.0f), e = at(k, a, r_level1 - 4.0f);
			coast_passage(hm, s.first, s.second, e.first, e.second, 20.0f, 20.0f, 2.0f, &passage);
		}
		if (size > 150.0f) {
			std::vector<uint8_t> m(cells, 0);
			mask_disk(hm, m, cx, cz, r_level2);
			coast_modify_mask(hm, m, 8.0f * kMetre, 1.0f);
			for (int i = 0; i < nb_pass2; ++i) {
				const float a = pi * ((float)i / (2.0f * nb_pass2) + 1.0f / (4.0f * nb_pass2) + (float)k) + swap;
				const auto s = at(k, a, r_level2 + 3.0f), e = at(k, a, r_level2 - 6.0f);
				coast_passage(hm, s.first, s.second, e.first, e.second, 20.0f, 20.0f, 2.0f, &passage);
			}
		}
	}
	// Players: ceil(n / 2) on the first island, the rest on the second.
	const auto order = coast_team_order(num_players, teams);
	std::vector<std::pair<float, float>> locs(num_players);
	std::vector<int> home(num_players);
	for (int k = 0, p = 0; k < 2; ++k) {
		const int per = k == 0 ? (num_players + 1) / 2 : num_players / 2;
		for (int i = 0; i < per; ++i, ++p) {
			locs[order[p]] = at(k, pi * (((float)i + 0.5f) / (2.0f * (float)per) + (float)k) + swap, r_player);
			home[order[p]] = k;
		}
	}
	// Four bases on a quarter arc of a Tiny map's island would overlap: RTS0
	// spreads such an island's players over its land instead (farthest
	// points, from the first arc spot).
	for (int k = 0; k < 2; ++k) {
		const int per = k == 0 ? (num_players + 1) / 2 : num_players / 2;
		if (per < 2 || 2.0f * r_player * det::sin(pi / (4.0f * (float)per)) >= 24.0f) continue;
		std::vector<std::pair<float, float>> cand;
		for (int z = 0; z < hm.res; ++z)
			for (int x = 0; x < hm.res; ++x) {
				float wx, wz;
				hm.grid_to_world(x, z, wx, wz);
				if (std::fabs(wx) > half - 10.0f || std::fabs(wz) > half - 10.0f) continue;
				if (det::hypot(wx - centres[k].first, wz - centres[k].second) > r_island - 12.0f) continue;
				cand.push_back({wx, wz});
			}
		std::vector<int> mine;
		for (int p = 0; p < num_players; ++p) if (home[p] == k) mine.push_back(p);
		if (cand.empty()) continue;
		std::vector<std::pair<float, float>> chosen{locs[mine[0]]};
		while (chosen.size() < mine.size()) {
			float best = -1.0f;
			std::pair<float, float> pick = cand[0];
			for (const auto &c : cand) {
				float d = 1e9f;
				for (const auto &q : chosen) d = std::min(d, det::hypot(c.first - q.first, c.second - q.second));
				if (d > best) { best = d; pick = c; }
			}
			chosen.push_back(pick);
		}
		for (size_t j = 0; j < mine.size(); ++j) locs[mine[j]] = chosen[j];
	}
	// Bumps on the islands, dips in the sea, both clear of bases and passages.
	{
		const float sp = grid_spacing(hm);
		std::vector<uint8_t> not_island(cells);
		for (size_t i = 0; i < cells; ++i) not_island[i] = !island[i];
		const auto in_island = distance_to_mask(not_island, hm.res, sp);
		const auto to_island = distance_to_mask(island, hm.res, sp);
		const auto to_passage = distance_to_mask(passage, hm.res, sp);
		auto idx = [&](float x, float z) {
			float gx, gz;
			hm.world_to_grid(x, z, gx, gz);
			return (size_t)std::lround(gz) * (size_t)hm.res + (size_t)std::lround(gx);
		};
		const int count = scale_by_map_size(20, 100, half);
		coast_bumps(hm, rng, water_h, count, 70.0f, 2.0f, 3.0f, [&](float x, float z, float r) {
			const size_t i = idx(x, z);
			return in_island[i] > 2.0f + r && to_passage[i] > 2.0f + r && !coast_near(locs, x, z, 18.0f + r);
		});
		coast_bumps(hm, rng, water_h, count, 120.0f, -5.0f, 6.0f, [&](float x, float z, float r) {
			const size_t i = idx(x, z);
			return to_island[i] > 2.0f + r && to_passage[i] > 2.0f + r && !coast_near(locs, x, z, 18.0f + r);
		});
	}
	record_area(hm, shore, AREA_SHORE);
	record_area(hm, passage, AREA_PASSAGE);
	make_heightmap_spawns(hm, spawns, locs, 14.0f, 6.0f, water_h);
	{
		// placePlayerBases' CityPatch: radius 6 round each civic centre.
		std::vector<uint8_t> m(cells, 0);
		for (const auto &sp : spawns) mask_disk(hm, m, sp.x, sp.z, 6.0f);
		record_area(hm, m, AREA_SETTLEMENT);
	}
	for (size_t i = 0; i < spawns.size(); ++i) {
		// Face the island's centre, then turn to keep the layout on land.
		auto &sp = spawns[i];
		const float dx = centres[home[i]].first - sp.x, dz = centres[home[i]].second - sp.z, l = std::max(1e-3f, det::hypot(dx, dz));
		sp.forward_dir_x = dx / l;
		sp.forward_dir_z = dz / l;
	}
	coast_face_inland(hm, spawns, water_h + 0.45f);
}

void RmsGenerator::synth_phoenician_levant(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players,
		const uint8_t *teams) {
	// phoenician_levant.js: land at 1 m; players (sorted by team) on two
	// staggered lines at 0.66 and 0.86 of the map (playerPlacementLine, 0.2
	// wide); the western half is a -3 m sea (paintRiver along the west edge,
	// as wide as the map, meanderShort 20, fade scaleByMapSize(6, 25)); the
	// whole map turned by a random quarter turn. Bumps, scaleByMapSize(1, 4)
	// * players * 3 hills at 15 m, and Cyprus: an island of 0.08 of the map
	// at 6 m, at least 8 tiles out to sea (its mines come in
	// distribute_neutral_resources).
	water_h = 1.0f;
	const float half = hm.map_half, size = 2.0f * half, pi = 3.14159265f;
	auto M = [&](float m) { return coast_metres(water_h, m); };
	const float land = M(1.0f);
	hm.init(hm.res, half, land);
	const float turn = (float)rng.range_int(0, 3) * pi / 2.0f;
	auto world = [&](float x, float z) { return coast_rot(x, z, turn); };
	const auto order = coast_team_order(num_players, teams);
	std::vector<std::pair<float, float>> locs(num_players);
	for (int i = 0; i < num_players; ++i) {
		// Vector2D(f, w).rotate(pi / 2) = (w, -f) about (0.76, 0.5) of the map.
		const float f = ((float)(i + 1) / (float)(num_players + 1) - 0.5f) * size;
		const float w = 0.2f * size * ((float)(i % 2) - 0.5f);
		locs[order[i]] = world(0.26f * size + w, -f);
	}
	{
		const auto s = world(-half, half), e = world(-half, -half);
		coast_paint_river(hm, rng, s.first, s.second, e.first, e.second, size, coast_sbms(6.0f, 25.0f, half), 0.0f, M(-3.0f), land,
		                  20.0f, 0.0f, true);
	}
	const float sp = grid_spacing(hm);
	const size_t cells = hm.data.size();
	std::vector<uint8_t> water(cells);
	for (size_t i = 0; i < cells; ++i) water[i] = hm.data[i] < land - 1e-4f;
	const auto to_water = distance_to_mask(water, hm.res, sp);
	auto idx = [&](float x, float z) {
		float gx, gz;
		hm.world_to_grid(x, z, gx, gz);
		return (size_t)std::lround(gz) * (size_t)hm.res + (size_t)std::lround(gx);
	};
	coast_bumps(hm, rng, water_h, scale_by_map_size(100, 200, half), coast_sbms(20.0f, 50.0f, half), 2.0f, 2.0f,
	            [&](float x, float z, float r) { return to_water[idx(x, z)] > 2.0f + r && !coast_near(locs, x, z, 25.0f + r); });
	{
		std::vector<uint8_t> blocked = coast_near_spots(hm, locs, 25.0f);
		for (size_t i = 0; i < cells; ++i) blocked[i] |= water[i];
		coast_chain_areas(hm, rng, scale_by_map_size(1, 4, half) * num_players * 3, 1.0f, std::floor(coast_sbms(4.0f, 6.0f, half)),
		                  (int)std::floor(coast_sbms(16.0f, 40.0f, half)), 0.5f, blocked, 15.0f,
		                  [&](const std::vector<uint8_t> &m) {
			                  paint_mask_elevation(hm, m, M(15.0f), 2.0f);
			                  record_area(hm, m, AREA_HILL);
		                  });
	}
	// Cyprus: stayClasses(clWater, 8) for the whole island.
	{
		const float r = 0.08f * size;
		std::vector<uint8_t> not_water(cells);
		for (size_t i = 0; i < cells; ++i) not_water[i] = !water[i];
		const auto to_land = distance_to_mask(not_water, hm.res, sp);
		for (int tries = 0; tries < 100; ++tries) {
			const float cx = rng.range_float(-half, half), cz = rng.range_float(-half, half);
			if (to_land[idx(cx, cz)] < r + 8.0f) continue;
			std::vector<uint8_t> m(cells, 0);
			mask_disk(hm, m, cx, cz, r);
			paint_mask_elevation(hm, m, M(6.0f), 8.0f);
			record_area(hm, m, AREA_ISLAND);
			break;
		}
	}
	make_heightmap_spawns(hm, spawns, locs, 14.0f, 6.0f, water_h);
	const auto inland = world(1.0f, 0.0f);
	for (auto &s : spawns) { s.forward_dir_x = inland.first; s.forward_dir_z = inland.second; }
	coast_face_inland(hm, spawns, water_h + 0.45f);
}

void RmsGenerator::synth_marmara(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players,
		const uint8_t *teams) {
	// marmara.js: LoadHeightmapImage("marmara.png", 0, 10), sea ground (<= 0
	// m) lowered to heightScale(scaleByMapSize(-6, -4)) with a
	// scaleByMapSize(1, 3) blend, SmoothingPainter(1, scaleByMapSize(0.1,
	// 0.2), 1), cliffs (slope >= 2, 2 tiles from water), players at random
	// (playerPlacementRandom) 5 from cliffs and scaleByMapSize(6, 25) inside
	// the land; water at setWaterHeight(scaleByMapSize(20, 18)), so 2 m lower
	// on a Giant map and the straits fall dry. RTS0 lifts the land 0.45 out
	// of the water and keeps every base on the largest walkable land.
	const float half = hm.map_half;
	const auto c = coast_heightmap(hm, water_h, marmara_image(), heightmap_images::marmara_size, 10.0f, coast_sbms(-6.0f, -4.0f, half),
	                               coast_sbms(1.0f, 3.0f, half), coast_sbms(0.1f, 0.2f, half));
	const auto region = largest_walkable_region(hm, water_h);
	// RTS0's base layout reaches about 24 tiles out, so starts stay at
	// least 16 inside the land.
	const float inland0 = coast_sbms(6.0f, 25.0f, half), inland = std::max(16.0f, inland0);
	std::vector<uint8_t> allowed(hm.data.size()), fallback(hm.data.size());
	for (size_t i = 0; i < allowed.size(); ++i) {
		fallback[i] = region[i] && c.land[i] && c.to_cliff[i] > 5.0f && c.to_not_land[i] > inland0;
		allowed[i] = fallback[i] && c.to_not_land[i] > inland;
	}
	coast_heightmap_players(hm, rng, water_h, spawns, num_players, teams, allowed, fallback);
}

void RmsGenerator::synth_bahrain(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players,
		const uint8_t *teams) {
	// bahrain.js: LoadHeightmapImage("bahrain.png", 0, 15), sea ground
	// lowered to heightScale(-6) with a 2-tile blend, SmoothingPainter(1,
	// scaleByMapSize(0.1, 0.5), 1); the island of Bahrain is the land in the
	// rectangle 0.4-0.6 of the map wide from the north edge to the centre,
	// kept free of players (5 tiles) for its mines (distribute_neutral_
	// resources); players at random on the Arabian and Qatari coasts half a
	// base radius inside the land; water at setWaterHeight(scaleByMapSize(20,
	// 18)). RTS0 lifts the land 0.45 out of the water. Naval: the coasts are
	// split by the Gulf of Salwa. The starting towers and the treasures are
	// left out.
	const float half = hm.map_half, size = 2.0f * half;
	const auto c = coast_heightmap(hm, water_h, bahrain_image(), heightmap_images::bahrain_size, 15.0f, -6.0f, 2.0f,
	                               coast_sbms(0.1f, 0.5f, half));
	std::vector<uint8_t> island(hm.data.size(), 0);
	for (int z = 0; z < hm.res; ++z)
		for (int x = 0; x < hm.res; ++x) {
			float wx, wz;
			hm.grid_to_world(x, z, wx, wz);
			const size_t i = (size_t)z * (size_t)hm.res + (size_t)x;
			island[i] = c.land[i] && std::fabs(wx) <= 0.1f * size && wz <= 0.0f;
		}
	const auto to_island = distance_to_mask(island, hm.res, grid_spacing(hm));
	const float inland0 = (float)scale_by_map_size(15, 25, half) / 2.0f, inland = std::max(16.0f, inland0); // RTS0: as Marmara
	std::vector<uint8_t> allowed(hm.data.size()), fallback(hm.data.size());
	for (size_t i = 0; i < allowed.size(); ++i) {
		fallback[i] = c.land[i] && to_island[i] > 5.0f && c.to_not_land[i] > inland0;
		allowed[i] = fallback[i] && c.to_not_land[i] > inland;
	}
	coast_heightmap_players(hm, rng, water_h, spawns, num_players, teams, allowed, fallback);
}

void RmsGenerator::synth_botswanan_haven(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players,
		const uint8_t *teams) {
	// botswanan_haven.js: land at 3 m, players (sorted by team) on a circle
	// at f = 0.35, scaleByMapSize(300, 800) +2 m bumps, scaleByMapSize(1, 4)
	// * players * 3 hills at 15 m, two rounds of scaleByMapSize(4, 20) -2 m
	// marshes (ChainPlacer, kept 25 from the bases and scaleByMapSize(7, 16)
	// x 0.8-1.35 from each other; wadeable, as in 0 A.D.) with +1 m bumps
	// inside them. No forests: the trees are stragglers.
	water_h = 1.0f;
	const float half = hm.map_half, size = 2.0f * half;
	auto M = [&](float m) { return coast_metres(water_h, m); };
	hm.init(hm.res, half, M(3.0f));
	const float start = rng.range_float(0.0f, 6.2831853f);
	const auto order = coast_team_order(num_players, teams);
	std::vector<std::pair<float, float>> locs(num_players);
	if (t_team_placement.pattern >= 0)
		locs = pattern_locs(hm, rng, t_team_placement.pattern, num_players, teams, 0.35f * size, 0.1f * size, start);
	else
		for (int i = 0; i < num_players; ++i) locs[order[i]] = polar(start + 6.2831853f * (float)i / (float)num_players, 0.35f * size);
	coast_bumps(hm, rng, water_h, scale_by_map_size(300, 800, half), coast_sbms(20.0f, 50.0f, half), 2.0f, 2.0f,
	            [&](float x, float z, float r) { return !coast_near(locs, x, z, 18.0f + r); });
	const size_t cells = hm.data.size();
	std::vector<uint8_t> hills, water(cells, 0);
	{
		const auto blocked = coast_near_spots(hm, locs, 25.0f);
		hills = coast_chain_areas(hm, rng, scale_by_map_size(1, 4, half) * num_players * 3, 1.0f, std::floor(coast_sbms(4.0f, 6.0f, half)),
		                          (int)std::floor(coast_sbms(16.0f, 40.0f, half)), 0.5f, blocked, 15.0f,
		                          [&](const std::vector<uint8_t> &m) {
			                  paint_mask_elevation(hm, m, M(15.0f), 2.0f);
			                  record_area(hm, m, AREA_HILL);
		                  });
	}
	{
		auto blocked = coast_near_spots(hm, locs, 30.0f);
		for (size_t i = 0; i < cells; ++i) blocked[i] |= hills[i];
		for (int pass = 0; pass < 2; ++pass) {
			const float gap = std::round(coast_sbms(7.0f, 16.0f, half) * rng.range_float(0.8f, 1.35f));
			// Each round keeps its own distance to the marshes before it.
			std::vector<uint8_t> b = blocked;
			const auto to_water = distance_to_mask(water, hm.res, grid_spacing(hm));
			for (size_t i = 0; i < cells; ++i) b[i] |= to_water[i] < gap;
			const auto m = coast_chain_areas(hm, rng, scale_by_map_size(4, 20, half), 1.0f, std::floor(coast_sbms(6.0f, 12.0f, half)),
			                                 (int)std::floor(coast_sbms(15.0f, 60.0f, half)), 0.8f, b, gap,
			                                 [&](const std::vector<uint8_t> &mm) {
				                                 paint_mask_elevation(hm, mm, M(-2.0f), 3.0f);
				                                 record_area(hm, mm, AREA_MARSH);
			                                 });
			for (size_t i = 0; i < cells; ++i) water[i] |= m[i];
		}
	}
	{
		std::vector<uint8_t> dry(cells);
		for (size_t i = 0; i < cells; ++i) dry[i] = !water[i];
		const auto in_water = distance_to_mask(dry, hm.res, grid_spacing(hm));
		const float bump_r = std::sqrt(coast_sbms(20.0f, 50.0f, half) / 3.14159265f);
		const int bumps = scale_by_map_size(50, 100, half);
		for (int i = 0; i < bumps; ++i) {
			const float bx = rng.range_float(-half, half), bz = rng.range_float(-half, half);
			float gx, gz;
			hm.world_to_grid(bx, bz, gx, gz);
			if (in_water[(size_t)std::lround(gz) * (size_t)hm.res + (size_t)std::lround(gx)] < 2.0f + bump_r) continue;
			std::vector<uint8_t> m(cells, 0);
			mask_disk(hm, m, bx, bz, bump_r);
			for (size_t k = 0; k < cells; ++k)
				if (m[k]) hm.data[k] = std::max(hm.data[k], M(-1.0f) - (M(-1.0f) - M(-2.0f)) * std::clamp(1.0f - in_water[k] / 2.0f, 0.0f, 1.0f));
		}
	}
	make_heightmap_spawns(hm, spawns, locs, 14.0f, 6.0f, water_h);
	for (auto &s : spawns) s.base_height = std::max(s.base_height, M(3.0f));
	for (const auto &s : spawns) lift_area(hm, s.x, s.z, 16.0f, 6.0f, s.base_height);
}

void RmsGenerator::synth_scythian_rivulet(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players,
		const uint8_t *teams) {
	// scythian_rivulet.js: land at 1 m; players on both banks
	// (playerPlacementRiver at a random angle, 0.6 of the map apart) of a -2
	// m rivulet 0.1 of the map wide through the centre (paintRiver,
	// meanderShort 40, meanderLong 20, deviation 6, banks at 2 m); four
	// tributaries 10 wide from the rivulet to the map edge; scaleByMapSize(6,
	// 20) -2 m pools; bumps; hills (18 m) or mountains (scaleByMapSize(30,
	// 50) m), scaleByMapSize(3, 15) of them; scaleByMapSize(100, 800) +4 m
	// large bumps. All its water is 2 m deep, so units wade it (as in 0 A.D.).
	water_h = 1.0f;
	const float half = hm.map_half, size = 2.0f * half, pi = 3.14159265f;
	auto M = [&](float m) { return coast_metres(water_h, m); };
	hm.init(hm.res, half, M(1.0f));
	const float angle = rng.range_float(0.0f, 2.0f * pi);
	std::vector<std::pair<float, float>> locs(num_players);
	{
		// playerPlacementRiver: (0.2 + 0.6 * (i % 2), f) of the map, turned.
		const bool even = num_players % 2 == 0;
		for (int i = 0; i < num_players; ++i) {
			const bool cur_even = i % 2 == 0;
			const int divident = (even || cur_even) ? (i + 1) % 2 : 0;
			const int divisor = even ? 0 : (cur_even ? 1 : -1);
			const float f = ((float)(i - 1 + divident) / 2.0f + 1.0f) / ((float)(num_players + divisor) / 2.0f + 1.0f);
			locs[i] = coast_rot((i % 2 ? 0.3f : -0.3f) * size, (f - 0.5f) * size, angle);
		}
		group_players_by_area(locs, num_players, teams);
	}
	{
		const auto s = coast_rot(0.0f, half, angle), e = coast_rot(0.0f, -half, angle);
		coast_paint_river(hm, rng, s.first, s.second, e.first, e.second, 0.1f * size, coast_sbms(3.0f, 14.0f, half), 6.0f, M(-2.0f), M(2.0f),
		                  40.0f, 20.0f, false);
	}
	const size_t cells = hm.data.size();
	const float sp = grid_spacing(hm);
	const float bed = M(-2.0f);
	std::vector<uint8_t> river(cells);
	for (size_t i = 0; i < cells; ++i) river[i] = hm.data[i] < M(1.0f) - 1e-4f;
	// The base footprint RTS0 keeps dry (0 A.D.: the 5-tile civic centre
	// area plus the script's margin).
	const float keep = 24.0f;
	std::vector<uint8_t> water = river;
	{
		const float trib_angle = angle + pi / 2.0f;
		std::vector<uint8_t> tribs(cells, 0);
		for (int i = 0; i < 4; ++i) {
			const float along = (rng.range_float(0.05f, 0.95f) - 0.5f) * size;
			const float sign = rng.range_int(0, 1) ? 1.0f : -1.0f;
			// findLocationInDirectionBasedOnHeight over (0, +-0.05): the
			// rivulet's centre line must be at its bed, then 4 tiles on.
			const auto s0 = coast_rot(along, sign * 0.05f, trib_angle), s1 = coast_rot(along, -sign * 0.05f, trib_angle);
			if (hm.sample_world(s0.first, s0.second) > bed + 1e-3f) continue;
			const float dl = std::max(1e-6f, det::hypot(s1.first - s0.first, s1.second - s0.second));
			const float sx = s0.first + (s1.first - s0.first) / dl * 4.0f, sz = s0.second + (s1.second - s0.second) / dl * 4.0f;
			const auto e = coast_rot(size, 0.0f, trib_angle - sign * rng.range_float(pi / 5.0f, 2.0f * pi - pi / 5.0f));
			// PathPlacer, waviness 0.4: jittered points on the way.
			std::vector<std::pair<float, float>> pts{{sx, sz}};
			const float len = std::max(1.0f, det::hypot(e.first - sx, e.second - sz));
			const float px = -(e.second - sz) / len, pz = (e.first - sx) / len;
			for (int k = 1; k < 6; ++k) {
				const float f = (float)k / 6.0f, j = rng.range_float(-0.1f, 0.1f) * len * 0.4f;
				pts.push_back({sx + (e.first - sx) * f + px * j, sz + (e.second - sz) * f + pz * j});
			}
			pts.push_back(e);
			std::vector<uint8_t> m(cells, 0);
			for (size_t k = 0; k + 1 < pts.size(); ++k) {
				const float ax = pts[k].first, az = pts[k].second, bx = pts[k + 1].first, bz = pts[k + 1].second;
				const float sl = std::max(1e-3f, det::hypot(bx - ax, bz - az));
				for (float t = 0.0f; t <= sl; t += 1.0f) {
					const float cx = ax + (bx - ax) * t / sl, cz = az + (bz - az) * t / sl;
					if (std::fabs(cx) > half + 10.0f || std::fabs(cz) > half + 10.0f) break;
					mask_disk(hm, m, cx, cz, 5.0f);
				}
			}
			const auto to_tribs = distance_to_mask(tribs, hm.res, sp);
			const auto near = coast_near_spots(hm, locs, keep);
			int in = 0;
			for (size_t k = 0; k < cells; ++k) {
				if (m[k] && (near[k] || to_tribs[k] < 3.0f)) m[k] = 0;
				in += m[k];
			}
			if (!in) continue;
			const std::vector<float> before = hm.data;
			paint_mask_elevation(hm, m, bed, 4.0f);
			for (size_t k = 0; k < cells; ++k) {
				hm.data[k] = std::min(hm.data[k], before[k]);
				tribs[k] |= m[k];
				water[k] |= m[k];
			}
		}
	}
	{
		const auto blocked = coast_near_spots(hm, locs, keep);
		const auto m = coast_chain_areas(hm, rng, scale_by_map_size(6, 20, half), 1.0f, std::floor(coast_sbms(2.0f, 5.0f, half)),
		                                 (int)std::floor(coast_sbms(15.0f, 60.0f, half)), 0.8f, blocked, 0.0f,
		                                 [&](const std::vector<uint8_t> &mm) { paint_mask_elevation(hm, mm, bed, 3.0f); });
		for (size_t i = 0; i < cells; ++i) water[i] |= m[i];
	}
	const float bump_area = 3.14159265f * 3.0f * 3.0f;
	coast_bumps(hm, rng, water_h, scale_by_map_size(100, 200, half), bump_area, 2.0f, 2.0f,
	            [&](float x, float z, float r) { return !coast_near(locs, x, z, 16.0f + r); });
	{
		const auto to_water = distance_to_mask(water, hm.res, sp);
		const auto to_river = distance_to_mask(river, hm.res, sp);
		auto blocked = coast_near_spots(hm, locs, keep);
		for (size_t i = 0; i < cells; ++i) blocked[i] |= to_water[i] < 1.0f || to_river[i] < 10.0f;
		const int count = scale_by_map_size(3, 15, half);
		if (rng.range_int(0, 1)) {
			coast_chain_areas(hm, rng, count, 1.0f, std::floor(coast_sbms(4.0f, 6.0f, half)), (int)std::floor(coast_sbms(16.0f, 40.0f, half)),
			                  0.5f, blocked, 15.0f, [&](const std::vector<uint8_t> &m) {
				                  paint_mask_elevation(hm, m, M(18.0f), 2.0f);
				                  record_area(hm, m, AREA_HILL);
			                  });
		} else {
			// createMountain: a cone of circles up to scaleByMapSize(30, 50) m.
			const float peak = std::floor(coast_sbms(30.0f, 50.0f, half));
			const float max_r = std::floor(coast_sbms(6.0f, 12.0f, half));
			coast_chain_areas(hm, rng, count, std::floor(coast_sbms(3.0f, 4.0f, half)), max_r, (int)std::floor(coast_sbms(4.0f, 10.0f, half)),
			                  0.0f, blocked, 15.0f, [&](const std::vector<uint8_t> &m) {
				                  std::vector<uint8_t> outside(m.size());
				                  for (size_t k = 0; k < m.size(); ++k) outside[k] = !m[k];
				                  const auto d = distance_to_mask(outside, hm.res, sp);
				                  for (size_t k = 0; k < m.size(); ++k)
					                  if (m[k]) hm.data[k] += (M(peak) - hm.data[k]) * smoothstep(0.0f, max_r, d[k]);
				                  record_area(hm, m, AREA_MOUNTAIN);
			                  });
		}
	}
	coast_bumps(hm, rng, water_h, scale_by_map_size(100, 800, half), coast_sbms(20.0f, 50.0f, half), 4.0f, 3.0f,
	            [&](float x, float z, float r) { return !coast_near(locs, x, z, 16.0f + r); });
	coast_bumps(hm, rng, water_h, scale_by_map_size(100, 200, half), bump_area, 2.0f, 2.0f,
	            [&](float x, float z, float r) { return !coast_near(locs, x, z, 25.0f + r); });
	make_heightmap_spawns(hm, spawns, locs, 14.0f, 6.0f, water_h);
	coast_face_inland(hm, spawns, water_h + 0.45f);
}

// =========================================================================
// Maps from 0 A.D. maps/random/{ambush,arctic_summer,empire,schwarzwald,
// ratumacos,oasis}.js
// =========================================================================

namespace {

constexpr float kTwoPi = 6.2831853f;

// rmgen2 getRandomDeviation.
float random_deviation(RmsRng &rng, float base, float dev) {
	return base + rng.range_float(-1.0f, 1.0f) * std::min(base, dev);
}

void mask_or(std::vector<uint8_t> &a, const std::vector<uint8_t> &b) {
	for (size_t i = 0; i < a.size(); ++i) a[i] |= b[i];
}

// Vertices within d tiles of a vertex set in m (avoidClasses(m, d)).
std::vector<uint8_t> near_mask(const HeightmapGrid &hm, const std::vector<uint8_t> &m, float d) {
	const auto dist = distance_to_mask(m, hm.res, grid_spacing(hm));
	std::vector<uint8_t> out(m.size());
	for (size_t i = 0; i < m.size(); ++i) out[i] = dist[i] <= d;
	return out;
}

// Vertices of m at least d tiles from any vertex outside it (stayClasses(m, d)).
std::vector<uint8_t> deep_inside(const HeightmapGrid &hm, const std::vector<uint8_t> &m, float d) {
	std::vector<uint8_t> out(m.size());
	for (size_t i = 0; i < m.size(); ++i) out[i] = !m[i];
	const auto dist = distance_to_mask(out, hm.res, grid_spacing(hm));
	for (size_t i = 0; i < m.size(); ++i) out[i] = m[i] && dist[i] >= d;
	return out;
}

// Vertices off the circular map.
std::vector<uint8_t> outside_circle(const HeightmapGrid &hm) {
	std::vector<uint8_t> out(hm.data.size());
	for (int z = 0; z < hm.res; ++z)
		for (int x = 0; x < hm.res; ++x) {
			float wx, wz;
			hm.grid_to_world(x, z, wx, wz);
			out[(size_t)z * (size_t)hm.res + (size_t)x] = wx * wx + wz * wz > hm.map_half * hm.map_half;
		}
	return out;
}

size_t vertex_at(const HeightmapGrid &hm, float x, float z) {
	float gx, gz;
	hm.world_to_grid(x, z, gx, gz);
	return (size_t)std::lround(gz) * (size_t)hm.res + (size_t)std::lround(gx);
}

std::pair<float, float> random_in_circle(RmsRng &rng, float r) {
	const float a = rng.range_float(0.0f, kTwoPi);
	return polar(a, std::sqrt(rng.next_float()) * r);
}

// An area's constraint test with ChainPlacer's failFraction: fails when more
// than `fail_frac` of its vertices are forbidden, else drops those vertices.
bool fit_area(std::vector<uint8_t> &area, const std::vector<uint8_t> &forbid, float fail_frac) {
	int in = 0, bad = 0;
	for (size_t i = 0; i < area.size(); ++i)
		if (area[i]) { ++in; bad += forbid[i]; }
	if (!in || (float)bad > fail_frac * (float)in || bad == in) return false;
	for (size_t i = 0; i < area.size(); ++i)
		if (forbid[i]) area[i] = 0;
	return true;
}

// SmoothElevationPainter(ELEVATION_MODIFY, dh, blend) over `mask`.
void paint_mask_modify(HeightmapGrid &hm, const std::vector<uint8_t> &mask, float dh, float blend) {
	std::vector<uint8_t> outside(mask.size());
	for (size_t i = 0; i < mask.size(); ++i) outside[i] = !mask[i];
	const auto d = distance_to_mask(outside, hm.res, grid_spacing(hm));
	for (size_t i = 0; i < mask.size(); ++i)
		if (mask[i]) hm.data[i] += dh * std::clamp(d[i] / std::max(blend, 1e-3f), 0.0f, 1.0f);
}

// createPassage: a strip from a to b, w0 wide at a and w1 at b, its height
// going from ha to hb, eased into the terrain over `smooth` tiles at its
// sides; vertices at or above `below` are left alone (its HeightConstraint).
void create_passage(HeightmapGrid &hm, float ax, float az, float bx, float bz, float w0, float w1, float smooth,
		float ha, float hb, float below = 1e9f) {
	const float dx = bx - ax, dz = bz - az, len = std::max(1e-3f, det::hypot(dx, dz));
	const float ux = dx / len, uz = dz / len;
	const std::vector<float> src = hm.data;
	for (int z = 0; z < hm.res; ++z)
		for (int x = 0; x < hm.res; ++x) {
			float wx, wz;
			hm.grid_to_world(x, z, wx, wz);
			const float t = ((wx - ax) * ux + (wz - az) * uz) / len;
			if (t < 0.0f || t > 1.0f) continue;
			const float s = std::fabs(-(wx - ax) * uz + (wz - az) * ux);
			const float hw = (w0 + (w1 - w0) * t) / 2.0f;
			if (s > hw) continue;
			const size_t i = (size_t)z * (size_t)hm.res + (size_t)x;
			const float h = src[i];
			if (h >= below) continue;
			const float ph = ha + (hb - ha) * t, sd = smooth + s - hw;
			hm.data[i] = sd > 0.0f ? (h * sd + ph / sd) / (sd + 1.0f / sd) : ph;
		}
}

// Vector2D.rotate (0 A.D.'s sense; RTS0 world x, z = 0 A.D. tile x, y).
std::pair<float, float> rotate_0ad(float x, float y, float a) {
	const float s = det::sin(a), c = det::cos(a);
	return {x * c + y * s, -x * s + y * c};
}

// getTeamsArray: players grouped by team (ascending team number), each
// player without a team alone after them.
std::vector<std::vector<int>> teams_array(int n, const uint8_t *teams) {
	std::vector<std::vector<int>> out;
	for (int t = 1; t < 256; ++t) {
		std::vector<int> g;
		for (int i = 0; i < n; ++i)
			if (teams && teams[i] == t) g.push_back(i);
		if (!g.empty()) out.push_back(g);
	}
	for (int i = 0; i < n; ++i)
		if (!teams || teams[i] == 0) out.push_back({i});
	return out;
}

// placeStronghold: each team round its spot 0.8 * `distance` from the centre,
// members on a circle of grouped / 2 / sin(pi / k) round it (a lone player on
// the spot; RTS0 keeps members at least 20 apart), the spots spaced round the
// circle by the gaps left between the teams' circles.
std::vector<std::pair<float, float>> stronghold_locs(const std::vector<std::vector<int>> &ta, int n, float distance,
		float grouped, float start, float size) {
	std::vector<std::pair<float, float>> locs(n);
	const int T = (int)ta.size();
	if (T == 0) return locs;
	std::vector<float> radius((size_t)T, 0.0f);
	float sum = 0.0f;
	for (int i = 0; i < T; ++i) {
		const int k = (int)ta[i].size();
		if (k > 1) {
			const float gd = k > 4 ? std::max(0.08f * size, grouped) : grouped;
			radius[(size_t)i] = std::max(gd / 2.0f, 10.0f) / det::sin(3.14159265f / (float)k);
		}
		sum += radius[(size_t)i];
	}
	const float between = (distance * kTwoPi - 2.0f * sum) / (float)T;
	float angle = start;
	for (int i = 0; i < T; ++i) {
		angle += (between + radius[(size_t)((i + T - 1) % T)] + radius[(size_t)i]) / distance;
		const auto c = polar(angle, 0.8f * distance);
		const int k = (int)ta[i].size();
		for (int p = 0; p < k; ++p) {
			const auto o = polar(start + (float)(p + 1) * kTwoPi / (float)k, radius[(size_t)i]);
			locs[ta[i][p]] = {c.first + o.first, c.second + o.second};
		}
	}
	return locs;
}

// placeLine: each team on a spoke from `distance` outwards, `grouped` apart
// (RTS0: at least 16 apart).
std::vector<std::pair<float, float>> line_locs(const std::vector<std::vector<int>> &ta, int n, float distance,
		float grouped, float start, float size) {
	std::vector<std::pair<float, float>> locs(n);
	const int T = std::max(1, (int)ta.size());
	const float step = std::max(grouped, 16.0f), limit = 0.45f * size;
	for (int i = 0; i < (int)ta.size(); ++i) {
		const int k = (int)ta[i].size();
		float safe = distance;
		if (distance + (float)k * step > limit) safe = std::max(8.0f, limit - (float)k * step);
		const float a = start + (float)(i + 1) * kTwoPi / (float)T;
		for (int p = 0; p < k; ++p) locs[ta[i][p]] = polar(a, safe + (float)p * step);
	}
	return locs;
}

// Pushes start spots apart until each pair is min_d apart (RTS0's bases
// are larger than 0 A.D.'s), keeping them within lim of the centre.
void spread_locs(std::vector<std::pair<float, float>> &locs, float min_d, float lim) {
	for (int it = 0; it < 300; ++it) {
		bool moved = false;
		for (size_t i = 0; i < locs.size(); ++i)
			for (size_t j = i + 1; j < locs.size(); ++j) {
				float dx = locs[j].first - locs[i].first, dz = locs[j].second - locs[i].second;
				float d = det::hypot(dx, dz);
				if (d >= min_d) continue;
				if (d < 1e-3f) { dx = det::cos((float)(i * 7 + j)); dz = det::sin((float)(i * 7 + j)); d = 1.0f; }
				const float push = (min_d - d) * 0.5f + 0.01f;
				locs[i].first -= dx / d * push; locs[i].second -= dz / d * push;
				locs[j].first += dx / d * push; locs[j].second += dz / d * push;
				moved = true;
			}
		for (auto &l : locs) {
			const float r = det::hypot(l.first, l.second);
			if (r > lim) { l.first *= lim / r; l.second *= lim / r; }
		}
		if (!moved) break;
	}
}

// playerPlacementRiver(angle, width): players alternate between two lines
// `width` apart, spaced along them, the whole turned by `angle`.
std::vector<std::pair<float, float>> river_locs(int n, float width, float angle, float size) {
	std::vector<std::pair<float, float>> locs;
	const bool even = n % 2 == 0;
	const float s = det::sin(angle), c = det::cos(angle);
	for (int i = 0; i < n; ++i) {
		const bool cur_even = i % 2 == 0;
		const int dividend = (even || cur_even) ? (i + 1) % 2 : 0;
		const int divisor = even ? 0 : cur_even ? 1 : -1;
		const float x = width * (float)(i % 2) - 0.5f * width;
		const float y = (((float)(i - 1 + dividend) / 2.0f + 1.0f) / ((float)(n + divisor) / 2.0f + 1.0f) - 0.5f) * size;
		locs.push_back({x * c + y * s, -x * s + y * c}); // Vector2D.rotate
	}
	return locs;
}

// playerPlacementByPattern(pattern: 0 line, 1 radial, 2 randomGroup,
// 3 stronghold, 4 river), then spread to RTS0's base size.
std::vector<std::pair<float, float>> pattern_locs(const HeightmapGrid &hm, RmsRng &rng, int pattern, int n,
		const uint8_t *teams, float distance, float grouped, float start) {
	const float size = 2.0f * hm.map_half;
	const auto ta = teams_array(n, teams);
	std::vector<std::pair<float, float>> locs;
	if (pattern == 0) locs = line_locs(ta, n, distance, grouped, start, size);
	else if (pattern == 4) {
		locs = river_locs(n, distance, start, size);
		group_players_by_area(locs, n, teams);
	}
	else if (pattern == 3) locs = stronghold_locs(ta, n, distance, grouped * 1.4f, start, size); // playerPlacementByPattern's 1.4
	else if (pattern == 2) {
		std::vector<uint8_t> anywhere(hm.data.size(), 1);
		if (player_placement_random(hm, rng, anywhere, n, locs)) group_players_by_area(locs, n, teams);
		else locs.clear();
	}
	if ((int)locs.size() != n) {
		// playerPlacementCircle, teammates next to each other (sortAllPlayers).
		locs.assign(n, {0.0f, 0.0f});
		int slot = 0;
		for (const auto &g : ta)
			for (int p : g) locs[p] = polar(start + (float)slot++ * kTwoPi / (float)n, distance);
	}
	spread_locs(locs, 26.0f, hm.map_half - 16.0f);
	return locs;
}

// The pattern_locs pattern for a Game Setup Team Placement (TEAM_PLACEMENT_*),
// or the map's own random pick when it is Random.
// The last pattern placement_pattern chose, for the bases' starting walls
// (rmgen2 setup.js playerbaseTypes[pattern].walls).
thread_local int g_placement_pattern = -1;

int placement_pattern(uint8_t team_placement, int random_pick) {
	switch (team_placement) {
		case TEAM_PLACEMENT_RADIAL: return g_placement_pattern = 1;
		case TEAM_PLACEMENT_LINE: return g_placement_pattern = 0;
		case TEAM_PLACEMENT_RANDOM_GROUP: return g_placement_pattern = 2;
		case TEAM_PLACEMENT_STRONGHOLD: return g_placement_pattern = 3;
		case TEAM_PLACEMENT_RIVER: return g_placement_pattern = 4;
		default: return g_placement_pattern = random_pick;
	}
}

// The walls 0.28's map script asks placeStartingWalls for (Simulation::StartingWalls): the
// Walls of placePlayerBases' CityPatch, else true; rmgen2's createBases only on maps over 192
// tiles. Maps that lay out no player bases get none.
uint8_t starting_walls_of(MapType map, int map_tiles) {
	switch (map) {
		case MAP_CANTABRIAN_HIGHLANDS: case MAP_CORSICA: case MAP_DANUBIUS: case MAP_FLOOD:
		case MAP_GUADALQUIVIR_RIVER: case MAP_SCHWARZWALD: case MAP_BELGIAN_UPLANDS: case MAP_ISLAND_STRONGHOLD:
		case MAP_EMPIRE: case MAP_HELLS_PASS: case MAP_POMPEII: case MAP_STRONGHOLD: case MAP_FORTRESS:
		case MAP_JEBEL_BARKAL: case MAP_MEDITERRANEAN: case MAP_SURVIVAL_OF_THE_FITTEST: case MAP_MIGRATION: case MAP_LAND_GRAB:
			return Simulation::STARTING_WALLS_NONE;
		case MAP_ARCHIPELAGO_0AD: case MAP_CORINTHIAN_ISTHMUS: case MAP_CYCLADIC_ARCHIPELAGO: case MAP_DODECANESE:
		case MAP_ELEPHANTINE: case MAP_EXTINCT_VOLCANO: case MAP_GEAR: case MAP_HELLAS: case MAP_ISLANDS:
		case MAP_LORRAINE_PLAIN: case MAP_POLAR_SEA: case MAP_RATUMACOS: case MAP_SNOWFLAKE_SEAROCKS:
		case MAP_RIVER_ARCHIPELAGO:
			return Simulation::STARTING_WALLS_TOWERS;
		case MAP_BAHRAIN:
			return map_tiles > 192 ? Simulation::STARTING_WALLS_TOWERS : Simulation::STARTING_WALLS_NONE;
		case MAP_HARBOR: case MAP_LIONS_DEN: case MAP_MARMARA: case MAP_RED_SEA: case MAP_WILD_LAKE:
		case MAP_NGORONGORO: // createBases(..., true)
			return map_tiles > 192 ? Simulation::STARTING_WALLS_FORTRESS : Simulation::STARTING_WALLS_NONE;
		case MAP_AMBUSH: case MAP_FRONTIER:
			// groupedLines and stronghold: false; circle and randomGroup: true.
			return map_tiles > 192 && (g_placement_pattern == 1 || g_placement_pattern == 2)
					? Simulation::STARTING_WALLS_FORTRESS : Simulation::STARTING_WALLS_NONE;
		default:
			return Simulation::STARTING_WALLS_FORTRESS;
	}
}

// Walkable-vertex components (8-connected, NAV_SHALLOW counts as land).
std::vector<int> walk_components(const HeightmapGrid &hm, float water_h) {
	const float walk = wade_height(water_h) + 0.05f;
	const int res = hm.res;
	std::vector<int> comp(hm.data.size(), -1);
	std::vector<int> stack;
	int next = 0;
	for (int s = 0; s < (int)hm.data.size(); ++s) {
		if (comp[s] >= 0 || hm.data[s] < walk) continue;
		comp[s] = next;
		stack.assign(1, s);
		while (!stack.empty()) {
			const int c = stack.back();
			stack.pop_back();
			const int cx = c % res, cz = c / res;
			for (int dz = -1; dz <= 1; ++dz)
				for (int dx = -1; dx <= 1; ++dx) {
					const int nx = cx + dx, nz = cz + dz;
					if (nx < 0 || nz < 0 || nx >= res || nz >= res) continue;
					const int ni = nz * res + nx;
					if (comp[ni] >= 0 || hm.data[ni] < walk) continue;
					comp[ni] = next;
					stack.push_back(ni);
				}
		}
		++next;
	}
	return comp;
}

int component_near(const HeightmapGrid &hm, const std::vector<int> &comp, float x, float z) {
	float gx, gz;
	hm.world_to_grid(x, z, gx, gz);
	const int cx = (int)std::lround(gx), cz = (int)std::lround(gz);
	for (int r = 0; r <= 3; ++r)
		for (int dz = -r; dz <= r; ++dz)
			for (int dx = -r; dx <= r; ++dx) {
				const int nx = cx + dx, nz = cz + dz;
				if (nx < 0 || nz < 0 || nx >= hm.res || nz >= hm.res) continue;
				const int c = comp[(size_t)nz * (size_t)hm.res + (size_t)nx];
				if (c >= 0) return c;
			}
	return -1;
}

bool bases_linked(const HeightmapGrid &hm, float water_h, const std::vector<std::pair<float, float>> &pts) {
	const auto comp = walk_components(hm, water_h);
	const int c0 = pts.empty() ? -1 : component_near(hm, comp, pts[0].first, pts[0].second);
	for (const auto &p : pts)
		if (component_near(hm, comp, p.first, p.second) != c0 || c0 < 0) return false;
	return true;
}

// RTS0 rule: every base reaches every other on foot. Any base cut off by
// water gets a wadeable causeway to the nearest land of the first base's.
void ensure_land_links(HeightmapGrid &hm, float water_h, const std::vector<std::pair<float, float>> &pts) {
	const float ford = wade_height(water_h) + 0.15f;
	for (size_t iter = 0; iter < pts.size() * 2; ++iter) {
		const auto comp = walk_components(hm, water_h);
		const int main = component_near(hm, comp, pts[0].first, pts[0].second);
		size_t k = 1;
		while (k < pts.size() && component_near(hm, comp, pts[k].first, pts[k].second) == main) ++k;
		if (k >= pts.size()) return;
		float best = 1e30f, tx = pts[0].first, tz = pts[0].second;
		for (int z = 0; z < hm.res; ++z)
			for (int x = 0; x < hm.res; ++x) {
				if (comp[(size_t)z * (size_t)hm.res + (size_t)x] != main) continue;
				float wx, wz;
				hm.grid_to_world(x, z, wx, wz);
				const float d = det::hypot(wx - pts[k].first, wz - pts[k].second);
				if (d < best) { best = d; tx = wx; tz = wz; }
			}
		std::vector<uint8_t> m(hm.data.size(), 0);
		const float len = std::max(1e-3f, det::hypot(tx - pts[k].first, tz - pts[k].second));
		for (float s = 0.0f; s <= len + 3.0f; s += 0.5f) {
			const float f = std::min(s, len) / len;
			mask_disk(hm, m, pts[k].first + (tx - pts[k].first) * f, pts[k].second + (tz - pts[k].second) * f, 3.0f);
		}
		for (size_t i = 0; i < m.size(); ++i)
			if (m[i]) hm.data[i] = std::max(hm.data[i], ford);
	}
}

// rmgen2 addElevation (addHills, addMountains, addPlateaus): fill * count
// ChainPlacer areas of radius max(floor(maxSize * offset), minSize) (the
// script's own size clamp), floor(spread * offset) circles, raised by a
// random elevation eased over elevation / steepness tiles, each where
// `forbid_fn()` allows (half the area may fail), 10 tries each.
struct ElevationSpec { float count, min_size, max_size, spread; int min_elev, max_elev; float steepness; };
constexpr ElevationSpec kHills{8.0f, 5.0f, 8.0f, 20.0f, 6, 12, 1.5f};
constexpr ElevationSpec kMountains{8.0f, 2.0f, 4.0f, 100.0f, 100, 120, 4.0f};
constexpr ElevationSpec kPlateaus{15.0f, 2.0f, 4.0f, 200.0f, 20, 30, 8.0f};

template <class F>
void add_elevation(HeightmapGrid &hm, RmsRng &rng, const ElevationSpec &el, float size, float dev, float fill,
		const F &forbid_fn, std::vector<uint8_t> &cls) {
	auto forbid = forbid_fn();
	const float count = fill * el.count;
	for (int i = 0; (float)i < count; ++i) {
		const int elev = rng.range_int(el.min_elev, el.max_elev - 1);
		const float smooth = std::floor((float)elev / el.steepness);
		const float off = random_deviation(rng, size, dev);
		const int pmax = std::min((int)std::floor(el.max_size * off), (int)el.max_size);
		const float r = (float)std::max(pmax, (int)el.min_size);
		const int spread = std::max(1, (int)std::floor(el.spread * off));
		const float psmooth = std::max(std::fabs(std::floor(smooth * off)), 1.0f);
		const int pelev = std::clamp((int)std::floor((float)elev * off), el.min_elev, el.max_elev);
		for (int t = 0; t < 10; ++t) {
			const auto c = random_in_circle(rng, hm.map_half);
			if (forbid[vertex_at(hm, c.first, c.second)]) continue;
			auto area = chain_placer(hm, rng, c.first, c.second, r, r, spread);
			if (!fit_area(area, forbid, 0.5f)) continue;
			paint_mask_modify(hm, area, (float)pelev * kMetre, psmooth);
			mask_or(cls, area);
			forbid = forbid_fn();
			break;
		}
	}
}

constexpr float kSizes[5] = {0.5f, 0.75f, 1.0f, 1.25f, 1.5f};     // g_Sizes
constexpr float kMixes[5] = {0.0f, 0.1f, 0.25f, 0.5f, 0.75f};     // g_Mixes
constexpr float kAmounts[5] = {0.2f, 0.5f, 1.0f, 1.75f, 3.0f};    // g_Amounts

void shuffle_ints(RmsRng &rng, std::vector<int> &v) {
	for (size_t i = v.size(); i > 1; --i) std::swap(v[i - 1], v[(size_t)rng.range_int(0, (int)i - 1)]);
}

// Spawns at `locs` (spawn k is player k % num_players), each base levelled
// at max(its height, min_h) and facing the map centre.
void spawns_at(HeightmapGrid &hm, std::vector<RmsGenerator::PlayerSpawn> &spawns, const std::vector<std::pair<float, float>> &locs,
		int num_players, float min_h, float radius, float blend) {
	spawns.clear();
	for (size_t k = 0; k < locs.size(); ++k) {
		const float sx = locs[k].first, sz = locs[k].second, l = std::max(1e-3f, det::hypot(sx, sz));
		const float bh = std::max(min_h, hm.sample_world(sx, sz));
		spawns.push_back({(uint8_t)(k % (size_t)std::max(1, num_players)), sx, sz, bh, -sx / l, -sz / l});
		hm.level_area(sx, sz, radius, blend, bh);
	}
}

const std::vector<uint8_t> &ratumacos_image() {
	static const std::vector<uint8_t> img = decode_heightmap_image(heightmap_images::ratumacos_b64, heightmap_images::ratumacos_size);
	return img;
}

// Oasis's central oasis radii: water, shore and forest band.
struct OasisRadii { float water, shore, forest; };
OasisRadii oasis_radii(float half) {
	return {(float)scale_by_map_size(7, 50, half), (float)scale_by_map_size(4, 10, half), (float)scale_by_map_size(6, 20, half)};
}

} // namespace

void RmsGenerator::synth_ambush(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players,
		const uint8_t *teams, uint8_t team_placement) {
	// ambush.js (rmgen2): land at 2 m; players by a random pattern (line,
	// radial, randomGroup, stronghold) at f = 0.2-0.35, teammates f =
	// 0.08-0.1 apart; a chain of circles round each base kept free of
	// bluffs; 1.75 * 15 bluffs (ChainPlacer(5d, 7d, 100d), d normal, big
	// or huge) raised 30d m and tilted so they fall from a 45-degree edge
	// line to the ground on the far side; hills on the bluffs; "tons" of
	// hills (6-12 m) off the bluffs; and a ramp from each base towards a
	// bluff. Bluff-top mines and forests are in distribute_land_extras.
	// RTS0 keeps bases 26 apart. 0 A.D. metres x 0.35.
	water_h = 1.0f;
	const float half = hm.map_half, size = 2.0f * half;
	const float land = metres(water_h, 2.0f);
	hm.init(hm.res, half, land);
	const int pattern = placement_pattern(team_placement, rng.range_int(0, 3));
	const float distance = rng.range_float(0.2f, 0.35f) * size, grouped = rng.range_float(0.08f, 0.1f) * size;
	const float start = rng.range_float(0.0f, kTwoPi);
	const auto locs = pattern_locs(hm, rng, pattern, num_players, teams, distance, grouped, start);
	const float base_r = (float)scale_by_map_size(15, 25, half);
	const size_t N = hm.data.size();
	const auto outside = outside_circle(hm);
	std::vector<uint8_t> players(N, 0), ignore(N, 0), bluff(N, 0), hills(N, 0);
	for (const auto &l : locs) {
		mask_disk(hm, players, l.first, l.second, 6.0f);
		mask_or(ignore, chain_placer(hm, rng, l.first, l.second, 3.0f, 6.0f, scale_by_map_size(25, 60, half), base_r));
	}
	// addBluffs.
	const float d = kSizes[2 + rng.range_int(0, 2)];
	const float sp = grid_spacing(hm);
	auto bluff_forbid = [&]() {
		auto f = near_mask(hm, bluff, 8.0f);
		mask_or(f, ignore);
		mask_or(f, outside);
		return f;
	};
	auto forbid = bluff_forbid();
	for (int i = 0; (float)i < 1.75f * 15.0f; ++i) {
		for (int t = 0; t < 10; ++t) {
			const auto c = random_in_circle(rng, half);
			if (forbid[vertex_at(hm, c.first, c.second)]) continue;
			auto area = chain_placer(hm, rng, c.first, c.second, 5.0f * d, 7.0f * d, (int)(100.0f * d));
			if (!fit_area(area, forbid, 0.5f)) continue;
			const int k = rng.range_int(0, 3);
			const float nx = (k & 1) ? 0.70710678f : -0.70710678f, nz = (k & 2) ? 0.70710678f : -0.70710678f;
			float pmin = 1e30f, pmax = -1e30f;
			for (int z = 0; z < hm.res; ++z)
				for (int x = 0; x < hm.res; ++x) {
					if (!area[(size_t)z * (size_t)hm.res + (size_t)x]) continue;
					float wx, wz;
					hm.grid_to_world(x, z, wx, wz);
					pmin = std::min(pmin, wx * nx + wz * nz);
					pmax = std::max(pmax, wx * nx + wz * nz);
				}
			const float L = 0.92f * (pmax - pmin);
			if (L < 4.0f) continue;
			std::vector<uint8_t> out(N);
			for (size_t j = 0; j < N; ++j) out[j] = !area[j];
			const auto din = distance_to_mask(out, hm.res, sp);
			for (int z = 0; z < hm.res; ++z)
				for (int x = 0; x < hm.res; ++x) {
					const size_t j = (size_t)z * (size_t)hm.res + (size_t)x;
					if (!area[j]) continue;
					float wx, wz;
					hm.grid_to_world(x, z, wx, wz);
					const float raised = 2.0f + 30.0f * d * std::clamp(din[j] / 2.0f, 0.0f, 1.0f);
					const float m = std::max(raised * (1.0f - (pmax - (wx * nx + wz * nz)) / L) - 2.0f, 2.0f);
					hm.data[j] = metres(water_h, m);
				}
			mask_or(bluff, area);
			forbid = bluff_forbid();
			break;
		}
	}
	// Hills on the bluffs (any size, mix and amount).
	{
		const float hs = kSizes[rng.range_int(0, 4)], hmix = kMixes[rng.range_int(0, 4)], ham = kAmounts[rng.range_int(0, 4)];
		const auto inside = deep_inside(hm, bluff, 3.0f);
		const auto near_players = near_mask(hm, players, 20.0f);
		add_elevation(hm, rng, kHills, hs, hmix, ham, [&]() {
			auto f = near_mask(hm, hills, 3.0f);
			mask_or(f, near_players);
			for (size_t j = 0; j < N; ++j) f[j] |= !inside[j];
			return f;
		}, hills);
	}
	// Hills off the bluffs: normal or big, normal mix, tons.
	{
		const float hs = rng.range_int(0, 1) ? 1.25f : 1.0f;
		const auto off_bluff = near_mask(hm, bluff, 5.0f);
		const auto near_players = near_mask(hm, players, 20.0f);
		add_elevation(hm, rng, kHills, hs, 0.25f, 3.0f, [&]() {
			auto f = near_mask(hm, hills, 15.0f);
			mask_or(f, off_bluff);
			mask_or(f, near_players);
			return f;
		}, hills);
	}
	// createBluffsPassages: a ramp from 0.7 to 1.7-2 base radii out from
	// each base, ending 12 inside a bluff, no steeper than 1.5 m per tile.
	const auto bluff12 = deep_inside(hm, bluff, 12.0f);
	for (const auto &l : locs) {
		for (int t = 0; t < 80; ++t) {
			const float a = det::atan2(-l.second, -l.first) + 1.5707963f + rng.range_float(-1.0f, 1.0f) * 1.5707963f;
			const auto s = polar(a, base_r * 0.7f), e = polar(a, base_r * rng.range_float(1.7f, 2.0f));
			const float sx = l.first + s.first, sz = l.second + s.second, ex = l.first + e.first, ez = l.second + e.second;
			if (std::fabs(ex) >= half || std::fabs(ez) >= half || !bluff12[vertex_at(hm, ex, ez)]) continue;
			const float hs = hm.sample_world(sx, sz), he = hm.sample_world(ex, ez);
			if ((he - hs) / kMetre / std::max(1.0f, det::hypot(ex - sx, ez - sz)) > 1.5f) continue;
			create_passage(hm, sx, sz, ex, ez, (float)scale_by_map_size(10, 20, half), (float)scale_by_map_size(10, 14, half), 3.0f, hs, he);
			break;
		}
	}
	spawns_at(hm, spawns, locs, num_players, land, 14.0f, 6.0f);
	for (auto &s : spawns) s.base_height = land;
	for (const auto &s : spawns) hm.level_area(s.x, s.z, 16.0f, 6.0f, land);
}

void RmsGenerator::synth_frontier(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players,
		const uint8_t *teams, uint8_t team_placement) {
	// frontier.js: a random base elevation (1-4, now and then up to 29),
	// players by the Team Placement pattern (line, radial, randomGroup,
	// stronghold; random when unset) at f = 0.2-0.35 from the centre,
	// teammates f = 0.08-0.1 apart, then a random mix of bluffs, hills,
	// mountains, plateaus, lakes and valleys.
	// 0 A.D. metres (HEIGHT_PER_METRE) above RTS0's water line: rmgen2 addHills 6-12,
	// addMountains 100-120, addPlateaus 20-30, addLakes 2-15 deep.
	water_h = 1.0f;
	constexpr float m = HEIGHT_PER_METRE;
	int elev = rng.range_int(0, 29);
	if (elev < 25) elev = rng.range_int(1, 4);
	const float base = water_h + (float)elev * m;
	hm.init(hm.res, hm.map_half, base);
	const float half = hm.map_half, size = 2.0f * half;
	hm.apply_fbm(rng, 4, 0.03f, 0.5f, 2.0f, 1.0f * m);
	const int pattern = placement_pattern(team_placement, rng.range_int(0, 3));
	const float distance = rng.range_float(0.2f, 0.35f) * size, grouped = rng.range_float(0.08f, 0.1f) * size;
	const float start = rng.range_float(0.0f, kTwoPi);
	spawns_at(hm, spawns, pattern_locs(hm, rng, pattern, num_players, teams, distance, grouped, start), num_players,
			water_h + 1.0f * m, 14.0f, 6.0f);
	// The feature mix: each kind present or not, as addElements' random amounts.
	if (rng.range_int(0, 1)) scatter_hills(hm, rng, spawns, scale_by_map_size(2, 8, half), 3.0f, 6.0f, 6.0f * m, 12.0f * m, 25.0f);
	if (rng.range_int(0, 1)) scatter_hills(hm, rng, spawns, scale_by_map_size(1, 6, half), 3.0f, 5.0f, 100.0f * m, 120.0f * m, 25.0f);
	if (rng.range_int(0, 1)) {
		const int plateaus = scale_by_map_size(1, 4, half);
		for (int i = 0; i < plateaus; ++i) {
			const float b_ = rng.range_float(0.0f, 0.9f) * half; const float a_ = rng.range_float(0.0f, 6.2831853f); const auto p = polar(a_, b_);
			bool ok = true;
			for (const auto &sp : spawns)
				if (det::hypot(p.first - sp.x, p.second - sp.z) < 35.0f) { ok = false; break; }
			if (ok) {
				const float c_ = rng.range_float(20.0f, 30.0f) * m; const float b_ = rng.range_float(0.0f, 6.2831853f);
				const float a_ = half * rng.range_float(0.05f, 0.09f);
				hm.raise_plateau(p.first, p.second, a_, b_, 0.4f, c_, 3.0f);
			}
		}
	}
	if (rng.range_int(0, 1))
		scatter_ponds(hm, rng, spawns, scale_by_map_size(1, 5, half), half * 0.05f + 3.0f, 25.0f, 15.0f, half * 0.9f,
		              base - water_h + 8.0f * m, water_h);
	hm.blur(1, 0.35f);
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 16.0f, 6.0f, sp.base_height);
}

void RmsGenerator::synth_arctic_summer(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// arctic_summer.js: land at 2 m, players at f = 0.35; scaleByMapSize(1,
	// 240) hills (ChainPlacer(1, scaleByMapSize(4, 6), scaleByMapSize(16,
	// 40)), set to 18 m) kept 35 from bases and 50 from each other;
	// scaleByMapSize(1, 20) lakes at -5 m (ChainPlacer(1, scaleByMapSize(4,
	// 8), scaleByMapSize(40, 180))), bumps, and lots of game (wolves, deer,
	// hares, muskox: see distribute_wildlife). RTS0 keeps lakes 24 from the
	// bases (its base layout is larger) and drops a lake that would cut a
	// base off. Its whales and fish are placed by distribute_fish.
	water_h = 1.0f;
	const float half = hm.map_half;
	const float land = metres(water_h, 2.0f);
	hm.init(hm.res, half, land);
	place_team_spawns(hm, rng, spawns, num_players, 0.7f * half, rng.range_float(0.0f, kTwoPi), land);
	const size_t N = hm.data.size();
	std::vector<uint8_t> players(N, 0), hills(N, 0);
	std::vector<std::pair<float, float>> pts;
	for (const auto &s : spawns) { mask_disk(hm, players, s.x, s.z, 6.0f); pts.push_back({s.x, s.z}); }
	const auto outside = outside_circle(hm);
	{
		const int count = scale_by_map_size(1, 240, half);
		const float max_r = std::floor((float)scale_by_map_size(4, 6, half));
		const int circles = (int)std::floor((float)scale_by_map_size(16, 40, half));
		const auto near_players = near_mask(hm, players, 35.0f);
		auto forbid_fn = [&]() {
			auto f = near_mask(hm, hills, 50.0f);
			mask_or(f, near_players);
			mask_or(f, outside);
			return f;
		};
		auto forbid = forbid_fn();
		for (int placed = 0, t = 0; placed < count && t < count * 10; ++t) {
			const auto c = random_in_circle(rng, half);
			if (forbid[vertex_at(hm, c.first, c.second)]) continue;
			auto area = chain_placer(hm, rng, c.first, c.second, 1.0f, max_r, circles);
			if (!fit_area(area, forbid, 0.5f)) continue;
			paint_mask_elevation(hm, area, metres(water_h, 18.0f), 2.0f);
			record_area(hm, area, AREA_HILL);
			mask_or(hills, area);
			forbid = forbid_fn();
			++placed;
		}
	}
	{
		const int count = scale_by_map_size(1, 20, half);
		const float max_r = std::floor((float)scale_by_map_size(4, 8, half));
		const int circles = (int)std::floor((float)scale_by_map_size(40, 180, half));
		const auto near_players = near_mask(hm, players, 24.0f);
		for (int i = 0; i < count; ++i) {
			const auto c = random_in_circle(rng, half);
			auto area = chain_placer(hm, rng, c.first, c.second, 1.0f, max_r, circles);
			if (!fit_area(area, near_players, 1.0f)) continue;
			const std::vector<float> before = hm.data;
			paint_mask_elevation(hm, area, metres(water_h, -5.0f), 5.0f);
			if (!bases_linked(hm, water_h, pts)) hm.data = before;
			else record_area(hm, area, AREA_LAKE);
		}
	}
	std::vector<PlayerSpawn> probe = spawns;
	dry_bumps(hm, rng, probe, scale_by_map_size(30, 300, half), 12.0f, land - 0.2f);
	for (const auto &s : spawns) hm.level_area(s.x, s.z, 16.0f, 6.0f, s.base_height);
	ensure_land_links(hm, water_h, pts);
}

void RmsGenerator::synth_empire(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players,
		const uint8_t *teams) {
	// empire.js (rmgen2): land at 2 m and two civic centres for every player:
	// teams placed as strongholds at f = 0.37 (members f = 0.04 apart), then
	// again at f = 0.15 turned by pi (pi/2 with 2 teams, 5/4 pi with 4); then
	// in random order "tons" of hills, huge mountains (100-120 m) and huge
	// plateaus (20-30 m, with hills on top). RTS0 keeps the bases 26 apart.
	// 0 A.D. metres x 0.35.
	water_h = 1.0f;
	const float half = hm.map_half, size = 2.0f * half;
	const float land = metres(water_h, 2.0f);
	hm.init(hm.res, half, land);
	const auto ta = teams_array(num_players, teams);
	const float start = rng.range_float(0.0f, kTwoPi);
	auto locs = stronghold_locs(ta, num_players, 0.37f * size, 0.04f * size, start, size);
	float rotation = 3.14159265f;
	if (ta.size() == 2) rotation = 3.14159265f / 2.0f;
	if (ta.size() == 4) rotation = 1.25f * 3.14159265f;
	const auto inner = stronghold_locs(ta, num_players, 0.15f * size, 0.04f * size, start + rotation, size);
	locs.insert(locs.end(), inner.begin(), inner.end());
	spread_locs(locs, 26.0f, half - 14.0f);
	const size_t N = hm.data.size();
	std::vector<uint8_t> players(N, 0), hills(N, 0), mountains(N, 0), plateaus(N, 0);
	for (const auto &l : locs) mask_disk(hm, players, l.first, l.second, 6.0f);
	const auto outside = outside_circle(hm);
	const auto players20 = near_mask(hm, players, 20.0f), players40 = near_mask(hm, players, 40.0f);
	std::vector<int> order{0, 1, 2};
	shuffle_ints(rng, order);
	for (int kind : order) {
		if (kind == 0) {
			const float s = kSizes[rng.range_int(0, 4)], mix = kMixes[rng.range_int(0, 4)];
			add_elevation(hm, rng, kHills, s, mix, 3.0f, [&]() {
				auto f = near_mask(hm, hills, 15.0f);
				mask_or(f, near_mask(hm, mountains, 2.0f));
				mask_or(f, near_mask(hm, plateaus, 5.0f));
				mask_or(f, players20);
				mask_or(f, outside);
				return f;
			}, hills);
		} else {
			const bool plateau = kind == 2;
			const float mix = rng.range_int(0, 1) ? 0.1f : 0.0f;
			auto forbid_fn = [&]() {
				auto f = near_mask(hm, mountains, 25.0f);
				mask_or(f, near_mask(hm, plateaus, 20.0f));
				mask_or(f, plateau ? players40 : players20);
				mask_or(f, outside);
				return f;
			};
			add_elevation(hm, rng, plateau ? kPlateaus : kMountains, 1.5f, mix, 3.0f, forbid_fn, plateau ? plateaus : mountains);
			if (plateau) {
				// 40 hills (ChainPlacer(3, 15, 1), 4-18 m) 7 inside the plateaus.
				const auto inside = deep_inside(hm, plateaus, 7.0f);
				std::vector<uint8_t> phills(N, 0);
				for (int i = 0; i < 40; ++i) {
					const int elev = rng.range_int(4, 18);
					const auto f = near_mask(hm, phills, 7.0f);
					for (int t = 0; t < 10; ++t) {
						const auto c = random_in_circle(rng, half);
						const size_t ci = vertex_at(hm, c.first, c.second);
						if (!inside[ci] || f[ci]) continue;
						std::vector<uint8_t> area(N, 0);
						mask_disk(hm, area, c.first, c.second, rng.range_float(3.0f, 15.0f));
						std::vector<uint8_t> bad(N);
						for (size_t j = 0; j < N; ++j) bad[j] = f[j] || !inside[j];
						if (!fit_area(area, bad, 0.5f)) continue;
						paint_mask_modify(hm, area, (float)elev * kMetre, (float)std::max(1, elev - 2));
						mask_or(phills, area);
						break;
					}
				}
			}
		}
	}
	spawns_at(hm, spawns, locs, num_players, land, 14.0f, 6.0f);
	for (const auto &s : spawns) lift_area(hm, s.x, s.z, 16.0f, 6.0f, s.base_height);
}

void RmsGenerator::synth_ratumacos(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players,
		const uint8_t *teams) {
	// ratumacos.js: the Seine near Rouen painted from ratumacos.png (heights
	// -3..20 on a 320-tile map), SmoothingPainter(1, 0.1, 1), water at 0;
	// scaleByMapSize(5, 12) shallows (createPassage scaleByMapSize(8, 12)
	// wide) across the map at the river angle 0.65 pi + pi/2 (+-20%),
	// players on both banks (playerPlacementRiver(0.65 pi, 0.6 * size)),
	// each base set to heightScale(10). Shallows sit at RTS0's wading depth
	// and a cut-off base gets a ford; the base's tower walls are left out.
	water_h = 1.0f;
	const float half = hm.map_half, size = 2.0f * half;
	auto hscale = [&](float n) { return n * size / 320.0f; };
	const int n = heightmap_images::ratumacos_size;
	paint_heightmap(hm, ratumacos_image(), n, 0, 0, n, -3.0f, 20.0f, water_h);
	shepard_smooth(hm, 0.1f);
	const float river = 0.65f * 3.14159265f;
	const float wade = wade_height(water_h);
	const int shallows = scale_by_map_size(5, 12, half);
	for (int i = 0; i < shallows; ++i) {
		const float x = rng.range_float(0.0f, 1.0f) * size - half;
		const auto a = rotate_0ad(x, -half, river + 1.5707963f * rng.range_float(0.8f, 1.2f));
		const auto b = rotate_0ad(x, half, river + 1.5707963f * rng.range_float(0.8f, 1.2f));
		const float w = (float)scale_by_map_size(8, 12, half);
		create_passage(hm, a.first, a.second, b.first, b.second, w, w, 2.0f, wade, wade, wade);
	}
	// playerPlacementRiver(river, 0.6 * size).
	std::vector<std::pair<float, float>> locs(num_players);
	const float width = 0.6f * size;
	const bool even = num_players % 2 == 0;
	for (int i = 0; i < num_players; ++i) {
		const bool cur_even = i % 2 == 0;
		const int divident = (even || cur_even) ? (i + 1) % 2 : 0;
		const int divisor = even ? 0 : (cur_even ? 1 : -1);
		const float px = width * (float)(i % 2) + (size - width) / 2.0f - half;
		const float f = ((float)(i - 1 + divident) / 2.0f + 1.0f) / ((float)(num_players + divisor) / 2.0f + 1.0f);
		locs[i] = rotate_0ad(px, f * size - half, river);
	}
	group_players_by_area(locs, num_players, teams);
	const float bh = metres(water_h, hscale(10.0f));
	spawns_at(hm, spawns, locs, num_players, bh, 14.0f, 4.0f);
	for (auto &s : spawns) {
		s.base_height = bh;
		hm.level_area(s.x, s.z, 14.0f, 4.0f, bh);
		lift_area(hm, s.x, s.z, 26.0f, 4.0f, metres(water_h, 1.0f));
	}
	ensure_land_links(hm, water_h, locs);
}

void RmsGenerator::synth_oasis_0ad(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// oasis.js: desert at 1 m, players at f = 0.35, each with a small oasis
	// (a palm clump 1.2 base radii away at an angle of pi/3-2pi/3 and a pond
	// at -3 m 6 tiles beyond it); a central oasis (water scaleByMapSize(7,
	// 50), shore scaleByMapSize(4, 10), palm band scaleByMapSize(6, 20)) at
	// -3 m; bumps (+4 m), dunes (+18 m over a 30-tile blend); and on maps
	// over 150 tiles half the time a 4 m path through the oasis. The palms
	// are in distribute_land_extras; RTS0 keeps the ponds 31 from the bases
	// and plants its usual starting woodline instead of the player clump.
	// Heights are metres above the nav water line (water_h + NAV_WATER_MARGIN): the 1 m desert would be
	// shallows at metres(water_h, 1), where the AI can't build.
	water_h = 1.0f;
	const float zero = water_h + NAV_WATER_MARGIN;
	const float half = hm.map_half, size = 2.0f * half;
	const float land = metres(zero, 1.0f), sea = metres(zero, -3.0f);
	hm.init(hm.res, half, land);
	const float start = rng.range_float(0.0f, kTwoPi);
	std::vector<std::pair<float, float>> locs;
	for (int i = 0; i < num_players; ++i) locs.push_back(polar(start + (float)i * kTwoPi / (float)std::max(1, num_players), 0.35f * size));
	const size_t N = hm.data.size();
	std::vector<uint8_t> base_res(N, 0), players(N, 0), oasis(N, 0);
	for (const auto &l : locs) mask_disk(hm, players, l.first, l.second, 6.0f);
	const float base_r = (float)scale_by_map_size(15, 25, half), forest_dist = 1.2f * base_r;
	for (const auto &l : locs) {
		float fa = 0.0f, fx = 0.0f, fz = 0.0f;
		for (int t = 0; t < 50; ++t) {
			fa = 3.14159265f / 3.0f * rng.range_float(1.0f, 2.0f);
			const auto o = polar(fa, forest_dist);
			fx = l.first + o.first; fz = l.second + o.second;
			if (!base_res[vertex_at(hm, fx, fz)]) break;
		}
		std::vector<uint8_t> clump(N, 0);
		mask_disk(hm, clump, fx, fz, std::sqrt(70.0f / 3.14159265f));
		mask_or(base_res, clump);
		{
			const auto o = polar(fa + rng.range_float(1.0f, 5.0f) / 3.0f * 3.14159265f, 6.0f);
			float wx = fx + o.first, wz = fz + o.second;
			// Pushed out to 31 from the base when closer (past its mines).
			const float d = std::max(1e-3f, det::hypot(wx - l.first, wz - l.second));
			if (d < 31.0f) { wx = l.first + (wx - l.first) * 31.0f / d; wz = l.second + (wz - l.second) * 31.0f / d; }
			bool ok = true;
			for (const auto &q : locs) ok &= det::hypot(wx - q.first, wz - q.second) >= 30.9f;
			if (ok) {
				std::vector<uint8_t> pond(N, 0);
				mask_disk(hm, pond, wx, wz, 4.5f);
				for (size_t j = 0; j < N; ++j) pond[j] &= !clump[j];
				paint_mask_elevation(hm, pond, sea, 3.0f);
				record_area(hm, pond, AREA_LAKE); // LayeredPainter([tShore, tWater], [1])
			}
		}
	}
	// The central oasis.
	const auto R = oasis_radii(half);
	const float rt = R.water + R.shore + R.forest;
	{
		std::vector<uint8_t> m(N, 0);
		const float wob = rng.range_float(0.0f, kTwoPi);
		for (int z = 0; z < hm.res; ++z)
			for (int x = 0; x < hm.res; ++x) {
				float wx, wz;
				hm.grid_to_world(x, z, wx, wz);
				const float a = det::atan2(wz, wx);
				const float r = rt * (1.0f + 0.06f * det::sin(3.0f * a + wob) + 0.04f * det::sin(5.0f * a - wob));
				m[(size_t)z * (size_t)hm.res + (size_t)x] = wx * wx + wz * wz <= r * r;
			}
		paint_mask_elevation(hm, m, sea, R.forest + R.shore);
		// LayeredPainter([pOasisForestLight, tWater], [forestDistance]): the palm band's forest floor
		// over the whole oasis, then the water more than forestDistance in from its edge.
		record_area(hm, m, AREA_OASIS);
		const auto in = distance_inside_mask(m, hm.res, grid_spacing(hm));
		std::vector<uint8_t> water(N, 0);
		for (size_t j = 0; j < N; ++j) water[j] = m[j] && in[j] > R.forest;
		record_area(hm, water, AREA_OASIS_WATER);
		oasis = m;
	}
	const auto outside = outside_circle(hm);
	auto bumps = [&](int count, float area, float dh, float blend) {
		std::vector<uint8_t> f = near_mask(hm, players, 10.0f);
		mask_or(f, near_mask(hm, base_res, 6.0f));
		mask_or(f, near_mask(hm, oasis, 4.0f));
		mask_or(f, outside);
		const float r = std::sqrt(area / 3.14159265f);
		for (int i = 0; i < count; ++i)
			for (int t = 0; t < 10; ++t) {
				const auto c = random_in_circle(rng, half);
				if (f[vertex_at(hm, c.first, c.second)]) continue;
				std::vector<uint8_t> m(N, 0);
				mask_disk(hm, m, c.first, c.second, r * rng.range_float(0.85f, 1.15f));
				if (!fit_area(m, f, 1.0f)) continue;
				paint_mask_modify(hm, m, dh * kMetre, blend);
				if (dh > 4.0f) record_area(hm, m, AREA_HILL); // the dunes' TerrainPainter(tDune)
				break;
			}
	};
	bumps(scale_by_map_size(30, 70, half), (float)scale_by_map_size(20, 50, half), 4.0f, 3.0f);
	bumps(scale_by_map_size(15, 50, half), 120.0f, 18.0f, 30.0f);
	if (size > 150.0f && rng.range_int(0, 1)) {
		const float pw = (float)scale_by_map_size(7, 18, half), a = rng.range_float(0.0f, kTwoPi);
		const auto p0 = polar(a, rt + pw), p1 = polar(a + 3.14159265f, rt + pw);
		set_along_segment(hm, p0.first, p0.second, p1.first, p1.second, pw * 0.5f, 5.0f, metres(zero, 4.0f));
	}
	spawns_at(hm, spawns, locs, num_players, land, 14.0f, 6.0f);
	for (const auto &s : spawns) lift_area(hm, s.x, s.z, 18.0f, 4.0f, s.base_height);
}

void RmsGenerator::synth_saharan_oases(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// saharan_oases.js (0.28), in 0 A.D. metres: flat sand at heightLand 1 over water at 0; players by
	// playerPlacementCircle(fractionToTiles(0.35)); one oasis per player, fractionToTiles(scaleByMapSize(0.19,
	// 0.22)) from the centre on the player's bearing: a ClumpPlacer of diskArea(scaleByMapSize(16, 60)) * 0.185
	// tiles lowered by heightOffsetOasis 3 m over 10 tiles, its LayeredPainter rings (sand-grass, palm grass,
	// shore stones, wet stones) in AREA_OASIS. The palms in its rings are distribute_neutral_resources'.
	// coast_metres keeps the 1 m sand dry, and the 2 m floor at the wading depth.
	water_h = 0.0f;
	const float half = hm.map_half, size = 2.0f * half;
	const float land = coast_metres(water_h, 1.0f);
	hm.init(hm.res, half, land);
	place_team_spawns(hm, rng, spawns, num_players, 0.35f * size, rng.range_float(0.0f, 2.0f * kSeasPi), land);
	const float oasis_r = seas_scale(0.19f, 0.22f, half) * size, r = seas_scale(16, 60, half);
	for (const auto &sp : spawns) {
		const float d = std::max(1e-3f, det::hypot(sp.x, sp.z));
		const auto m = seas_clump(hm, rng, kSeasPi * r * r * 0.185f, 0.6f, 0.15f, 1e9f, sp.x / d * oasis_r, sp.z / d * oasis_r,
		                          [](int) { return true; });
		if (!seas_any(m)) continue;
		paint_mask_modify(hm, m, coast_metres(water_h, -2.0f) - land, 10.0f);
		record_area(hm, m, AREA_OASIS);
	}
}

void RmsGenerator::synth_nearestern_badlands(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// neareastern_badlands.js (0.28), in 0 A.D. metres: sand at heightLand 10 over the water at 0; players by
	// playerPlacementByPattern at fractionToTiles(0.35), teammates fractionToTiles(0.1) apart, each in a
	// clPlayer disk of defaultPlayerBaseRadius(); the oasis in the centre (ClumpPlacer of
	// diskArea(scaleByMapSize(14, 40)) lowered by heightOffsetOasis 11 m over 8 tiles, to a bed 1 m under the
	// water); scaleByMapSize(10, 20) level 1 hills of scaleByMapSize(50, 300) tiles (3 from the oasis, 10
	// apart) and scaleByMapSize(15, 25) small ones of (25, 150) (3 apart), each raised 16 m over 1 tile; as
	// many level 2 hills of (25, 150) and level 3 ones of (12, 75) inside them (16 m more each); then
	// scaleByMapSize(100, 200) bumps of (20, 50) tiles, 2 m, clear of the oasis, bases and hills.
	water_h = 0.0f;
	const float half = hm.map_half, size = 2.0f * half;
	const float land = metres(water_h, 10.0f), level = 16.0f * kMetre;
	hm.init(hm.res, half, land);
	place_team_spawns(hm, rng, spawns, num_players, 0.35f * size, rng.range_float(0.0f, 2.0f * kSeasPi), land);
	const auto to_player = seas_dist_to_players(hm, spawn_points(spawns), seas_scale(15, 25, half));
	const auto any = [](int) { return true; };
	const float oasis_r = seas_scale(14, 40, half);
	const auto oasis = seas_clump(hm, rng, kSeasPi * oasis_r * oasis_r, 0.6f, 0.15f, 1e9f, 0.0f, 0.0f, any);
	paint_mask_modify(hm, oasis, -11.0f * kMetre, 8.0f);
	record_area(hm, oasis, AREA_OASIS);
	const auto to_oasis = seas_dist_inside(hm, oasis);
	std::vector<uint8_t> hills(hm.data.size(), 0);
	std::vector<float> to_hill(hm.data.size(), 1e9f);
	auto hill = [&](float amount, float tiles, float gap, int retry) {
		seas_create_areas(hm, rng, amount, retry, [&](float x, float z) {
			const auto m = seas_clump(hm, rng, tiles, 0.25f, 0.1f, 0.5f, x, z, [&](int k) {
				return to_oasis[(size_t)k] > 3.0f && to_player[(size_t)k] > 0.0f && to_hill[(size_t)k] > gap;
			});
			if (!seas_any(m)) return false;
			paint_mask_modify(hm, m, level, 1.0f);
			seas_add(hills, m);
			to_hill = seas_dist_inside(hm, hills);
			return true;
		});
	};
	hill(seas_scale(10, 20, half), seas_scale(50, 300, half), 10.0f, 100);
	hill(seas_scale(15, 25, half), seas_scale(25, 150, half), 3.0f, 100);
	// createAreasInAreas / stayClasses(clHill1, 0): wholly on the level 1 hills.
	for (const float tiles : {seas_scale(25, 150, half), seas_scale(12, 75, half)})
		seas_create_areas(hm, rng, seas_scale(15, 25, half), 50, [&](float x, float z) {
			if (!hills[vertex_at(hm, x, z)]) return false;
			const auto m = seas_clump(hm, rng, tiles, 0.25f, 0.1f, 0.0f, x, z, [&](int k) { return hills[(size_t)k] != 0; });
			if (!seas_any(m)) return false;
			paint_mask_modify(hm, m, level, 1.0f);
			return true;
		});
	seas_create_areas(hm, rng, seas_scale(100, 200, half), 10, [&](float x, float z) {
		const auto m = seas_clump(hm, rng, seas_scale(20, 50, half), 0.3f, 0.06f, 0.0f, x, z, [&](int k) {
			return to_oasis[(size_t)k] > 0.0f && to_player[(size_t)k] > 0.0f && to_hill[(size_t)k] > 2.0f;
		});
		if (!seas_any(m)) return false;
		paint_mask_modify(hm, m, 2.0f * kMetre, 2.0f);
		return true;
	});
	for (auto &sp : spawns) sp.base_height = land;
}

void RmsGenerator::distribute_land_extras(Simulation &sim, const HeightmapGrid &hm, float water_h, const std::vector<PlayerSpawn> &spawns,
		RmsRng &rng, const Config &config) {
	const float half = hm.map_half, size = 2.0f * half;
	auto dist_to_bases = [&](float x, float z) {
		float d = 1e30f;
		for (const auto &s : spawns) d = std::min(d, det::hypot(x - s.x, z - s.z));
		return d;
	};
	if (config.map_type == MAP_AMBUSH) {
		// ambush.js: "tons" of metal and stone mines (20 / 10 apart) and of
		// big forests on the bluffs, 30 and 20 from the bases. Bluff tops are
		// where the ground stands over 14 m.
		const float top = metres(water_h, 14.0f);
		std::vector<std::pair<float, float>> metal, stone, groves;
		auto clear_of = [](const std::vector<std::pair<float, float>> &v, float x, float z, float d) {
			for (const auto &p : v)
				if (det::hypot(p.first - x, p.second - z) < d) return false;
			return true;
		};
		for (int t = 0; t < 6000; ++t) {
			const auto c = random_in_circle(rng, half * 0.95f);
			const float x = c.first, z = c.second;
			if (hm.sample_world(x, z) < top || hm.compute_slope_world(x, z) > 0.7f) continue;
			const int kind = t % 3;
			if (kind < 2) {
				if (dist_to_bases(x, z) < 30.0f) continue;
				auto &own = kind == 0 ? metal : stone;
				const auto &other = kind == 0 ? stone : metal;
				if ((int)own.size() >= 61 || !clear_of(own, x, z, 20.0f) || !clear_of(other, x, z, 10.0f) || !clear_of(groves, x, z, 6.0f)) continue;
				own.push_back({x, z});
				sim.spawn_resource_world(x, z, kind == 0 ? RES_GOLD : RES_STONE, LARGE_MINE_SUPPLY);
			} else {
				if (dist_to_bases(x, z) < 20.0f || (int)groves.size() >= 120 || !clear_of(groves, x, z, 10.0f) ||
				    !clear_of(metal, x, z, 5.0f) || !clear_of(stone, x, z, 5.0f)) continue;
				groves.push_back({x, z});
				const int trees = rng.range_int(8, 14);
				const float gr = rng.range_float(3.0f, 5.0f);
				for (int k = 0; k < trees; ++k) {
					const float b_ = std::sqrt(rng.next_float()) * gr; const float a_ = rng.range_float(0.0f, kTwoPi); const auto p = polar(a_, b_);
					if (hm.sample_world(x + p.first, z + p.second) > top - 1.0f)
						spawn_trees(sim, hm, x + p.first, z + p.second, top - 1.0f);
				}
			}
		}
	} else if (config.map_type == MAP_SCHWARZWALD) {
		// schwarzwald.js's wood: on every tile a tree with chance maxTreeDensity
		// (min(256 (192 + 8 players) / size^2, 1)) x a factor rising from 0 at
		// 15 tiles from a base to 1 at 30 x |size/3 - r| / (size/3) x 0.75, off
		// the water and the low open shore and 1-2 tiles clear of the paths
		// between every base and the centre. RTS0 tests every other tile
		// along each axis and plants four trees for each hit.
		const int n = (int)spawns.size();
		const float max_d = std::min(256.0f * (192.0f + 8.0f * (float)n) / (size * size), 1.0f);
		const float res_r = size / 3.0f;
		const float kMinH = -20.0f, kMaxH = 65535.0f / 92.0f - 20.0f;
		const float range = (kMaxH - kMinH) * (size + 512.0f) / 8192.0f;
		const float open_h = water_h + 0.8f * range / 16.0f * kMetre;
		std::vector<std::pair<float, float>> nodes;
		for (const auto &s : spawns) nodes.push_back({s.x, s.z});
		nodes.push_back({0.0f, 0.0f});
		auto path_dist = [&](float x, float z) {
			float best = 1e30f;
			for (size_t i = 0; i < nodes.size(); ++i)
				for (size_t j = i + 1; j < nodes.size(); ++j) {
					const float ax = nodes[i].first, az = nodes[i].second, dx = nodes[j].first - ax, dz = nodes[j].second - az;
					const float l2 = std::max(1e-3f, dx * dx + dz * dz);
					const float t = std::clamp(((x - ax) * dx + (z - az) * dz) / l2, 0.0f, 1.0f);
					best = std::min(best, det::hypot(x - ax - dx * t, z - az - dz * t));
				}
			return best;
		};
		for (float z = -half + 1.0f; z < half; z += 2.0f)
			for (float x = -half + 1.0f; x < half; x += 2.0f) {
				const float r = det::hypot(x, z);
				if (r > half - 1.0f) continue;
				const float sl = std::clamp((dist_to_bases(x, z) - 15.0f) / 15.0f, 0.0f, 1.0f);
				const float dens = max_d * sl * std::fabs((res_r - r) / res_r) * 0.75f;
				if (rng.next_float() >= dens) continue;
				const bool border = dens < rng.range_float(0.0f, max_d / 3.0f);
				const float tx = x + rng.range_float(-0.5f, 0.5f), tz = z + rng.range_float(-0.5f, 0.5f);
				if (hm.sample_world(tx, tz) < open_h + (border ? 0.0f : 0.2f)) continue;
				if (path_dist(tx, tz) < (border ? 1.0f : 2.0f) + 1.5f) continue;
				spawn_trees(sim, hm, tx, tz, open_h, 4, 0.7f);
			}
	} else if (config.map_type == MAP_MIGRATION) {
		// migration.js's createObjectGroupsDeprecated passes on the continent
		// (clLand: land that is neither water nor a player's island, here the
		// walkable land no base stands on), each a fixed number of tries (groups),
		// off hills (avoidClasses(clHill, 1): RTS0 skips steep ground).
		const size_t N = hm.data.size();
		const int res = hm.res;
		const auto comp = walk_components(hm, water_h);
		std::vector<int> home;
		for (const auto &s : spawns) home.push_back(component_near(hm, comp, s.x, s.z));
		std::vector<uint8_t> not_land(N), not_island(N);
		for (size_t i = 0; i < N; ++i) {
			const bool is_home = comp[i] >= 0 && std::find(home.begin(), home.end(), comp[i]) != home.end();
			not_land[i] = comp[i] < 0 || is_home;
			not_island[i] = !is_home;
		}
		const auto in_land = distance_to_mask(not_land, res, grid_spacing(hm));
		std::vector<std::pair<float, float>> rock, metal, food;
		auto clear_of = [](const std::vector<std::pair<float, float>> &v, float x, float z, float d) {
			for (const auto &p : v)
				if (det::hypot(p.first - x, p.second - z) < d) return false;
			return true;
		};
		// createObjectGroupsDeprecated counts every try as placed (retryPlacing with
		// behaveDeprecated), so it makes exactly `amount` tries at random points of
		// the map's circle and the retry factor does nothing.
		auto groups = [&](const std::vector<float> &inside, float stay, float amount, auto ok, auto place) {
			for (int t = 0; (float)t < amount; ++t) {
				const auto c = random_in_circle(rng, half);
				float gx, gz;
				hm.world_to_grid(c.first, c.second, gx, gz);
				const int ix = std::clamp((int)std::lround(gx), 0, res - 1), iz = std::clamp((int)std::lround(gz), 0, res - 1);
				if (inside[(size_t)iz * (size_t)res + (size_t)ix] <= stay) continue;
				if (hm.compute_slope_world(c.first, c.second) > 0.7f || !ok(c.first, c.second)) continue;
				place(c.first, c.second);
			}
		};
		auto sbms = [&](float lo, float hi) { return lo + (hi - lo) * std::max(0.0f, size - 128.0f) / 384.0f; };
		// Objects of a SimpleGroup: n of them 0..r from (x, z).
		auto scatter = [&](float x, float z, int n, float r0, float r1, auto spawn) {
			for (int k = 0; k < n; ++k) {
				const float b_ = rng.range_float(r0, r1); const float a_ = rng.range_float(0.0f, kTwoPi); const auto p = polar(a_, b_);
				if (hm.sample_world(x + p.first, z + p.second) > water_h + 0.3f) spawn(x + p.first, z + p.second);
			}
		};
		auto stone = [&](float x, float z) { sim.spawn_resource_world(x, z, RES_STONE, MINE_SUPPLY); };
		// Stone mines: 1 large and 0-2 small stones within 4, scaleByMapSize(26, 30),
		// avoidClasses(clRock, 18), stayClasses(clLand, 4).
		groups(in_land, 4.0f, sbms(26.0f, 30.0f), [&](float x, float z) { return clear_of(rock, x, z, 18.0f); },
			[&](float x, float z) {
				rock.push_back({x, z});
				sim.spawn_resource_world(x, z, RES_STONE, LARGE_MINE_SUPPLY);
				scatter(x, z, rng.range_int(0, 2), 2.0f, 4.0f, stone);
			});
		// Metal mines: 1 large, scaleByMapSize(28, 32), avoidClasses(clMetal, 18, clRock, 8).
		groups(in_land, 4.0f, sbms(28.0f, 32.0f),
			[&](float x, float z) { return clear_of(metal, x, z, 18.0f) && clear_of(rock, x, z, 8.0f); },
			[&](float x, float z) {
				metal.push_back({x, z});
				sim.spawn_resource_world(x, z, RES_GOLD, LARGE_MINE_SUPPLY);
			});
		// Small stone quarries: 2-5 small stones 1-3 out, scaleByMapSize(25, 30),
		// avoidClasses(clMetal, 6, clRock, 12).
		groups(in_land, 4.0f, sbms(25.0f, 30.0f),
			[&](float x, float z) { return clear_of(metal, x, z, 6.0f) && clear_of(rock, x, z, 12.0f); },
			[&](float x, float z) {
				rock.push_back({x, z});
				scatter(x, z, rng.range_int(2, 5), 1.0f, 3.0f, stone);
			});
		const BiomeFauna bf = biome_fauna(config.biome, (int)config.seed);
		const int np = (int)spawns.size();
		auto food_ok = [&](float x, float z) { return clear_of(food, x, z, 20.0f); };
		// Deer: 3 x players groups of 5-7 main hunt within 4, avoidClasses(clFood, 20), stayClasses(clLand, 7).
		groups(in_land, 7.0f, 3.0f * (float)np, food_ok, [&](float x, float z) {
			food.push_back({x, z});
			scatter(x, z, rng.range_int(5, 7), 0.0f, 4.0f, [&](float fx, float fz) { sim.spawn_fauna(fx, fz, bf.main_hunt); });
		});
		if (config.biome == BIOME_SAVANNA) {
			// generic/savanna's island fauna: players x 20 groups of 5-7 main hunt,
			// stayClasses(clIsland, 12), avoidClasses(clBaseResource, 6, clIslandHunt, 58).
			const auto in_island = distance_to_mask(not_island, res, grid_spacing(hm));
			std::vector<std::pair<float, float>> island_hunt;
			groups(in_island, 12.0f, 20.0f * (float)np,
				[&](float x, float z) { return clear_of(island_hunt, x, z, 58.0f) && dist_to_bases(x, z) > 28.0f; },
				[&](float x, float z) {
					island_hunt.push_back({x, z});
					scatter(x, z, rng.range_int(5, 7), 0.0f, 4.0f, [&](float fx, float fz) { sim.spawn_fauna(fx, fz, bf.main_hunt); });
				});
		}
		// Sheep: 3 x players groups of 2-3 secondary hunt within 2.
		groups(in_land, 7.0f, 3.0f * (float)np, food_ok, [&](float x, float z) {
			food.push_back({x, z});
			scatter(x, z, rng.range_int(2, 3), 0.0f, 2.0f, [&](float fx, float fz) { sim.spawn_fauna(fx, fz, bf.secondary_hunt); });
		});
		// Fruit bushes: randIntInclusive(1, 4) x players + 2 groups of 5-7 within 4.
		groups(in_land, 7.0f, (float)(rng.range_int(1, 4) * np + 2), food_ok, [&](float x, float z) {
			food.push_back({x, z});
			scatter(x, z, rng.range_int(5, 7), 0.0f, 4.0f, [&](float fx, float fz) { sim.spawn_resource_world(fx, fz, RES_FOOD, 200); });
		});
	} else if (config.map_type == MAP_OASIS_0AD) {
		// oasis.js: the central oasis's palm band (pOasisForestLight, a palm
		// on 2 tiles in 9), on ground units can walk to (some in the shallows,
		// as the script's band reaches into the water).
		const auto R = oasis_radii(half);
		const float rt = R.water + R.shore + R.forest, r0 = rt - R.forest;
		for (float z = -rt; z <= rt; z += 1.0f)
			for (float x = -rt; x <= rt; x += 1.0f) {
				const float r = det::hypot(x, z);
				if (r < r0 || r > rt) continue;
				if (rng.next_float() >= 2.0f / 9.0f) continue;
				const float tx = x + rng.range_float(-0.3f, 0.3f), tz = z + rng.range_float(-0.3f, 0.3f);
				if (hm.sample_world(tx, tz) > wade_height(water_h) + 0.1f) sim.spawn_resource_world(tx, tz, RES_WOOD, TREE_WOOD_SUPPLY);
			}
	}
}

// =========================================================================
// Maps ported from 0 A.D. maps/random/{mediterranean,elephantine,
// island_stronghold,pompeii,latium,unknown}.js. 0 A.D. metres x 0.35 above
// RTS0's water line; one world unit is one tile.
// =========================================================================

namespace {
namespace misc_maps {

constexpr float kTau = 6.2831853f;

// rmgen Vector2D.rotate(angle), in the map's centred x/z frame.
inline std::pair<float, float> rot(float x, float z, float a) {
	const float c = det::cos(a), s = det::sin(a);
	return {x * c + z * s, -x * s + z * c};
}

// scaleByMapSize without rounding.
inline float scale_f(float lo, float hi, float half) {
	return lo + (hi - lo) * std::max(0.0f, 2.0f * half - 128.0f) / 384.0f;
}

const std::vector<uint8_t> &mediterranean_image() {
	static const std::vector<uint8_t> img = decode_heightmap_image(heightmap_images::mediterranean_b64, heightmap_images::mediterranean_size);
	return img;
}

const std::vector<uint8_t> &pompeii_image() {
	static const std::vector<uint8_t> img = decode_heightmap_image(heightmap_images::pompeii_b64, heightmap_images::pompeii_size);
	return img;
}

const std::vector<uint8_t> &elephantine_image() {
	static const std::vector<uint8_t> img = decode_heightmap_image(heightmap_images::elephantine_b64, heightmap_images::elephantine_size);
	return img;
}

// lower_nubia.js "Composing heightmap" (tools/gen_heightmap_images.py bakes it):
// the elevation where the land or the water threshold image is set, else
// minHeight (black, -3 m).
const std::vector<uint8_t> &lower_nubia_image() {
	static const std::vector<uint8_t> img = decode_heightmap_image(heightmap_images::lower_nubia_b64, heightmap_images::lower_nubia_size);
	return img;
}

// sortAllPlayers: the players in random order, then stably sorted by team
// (no team first, as 0 A.D.'s team -1).
std::vector<int> sort_all_players(RmsRng &rng, int n, const uint8_t *teams) {
	std::vector<int> ids(n);
	for (int i = 0; i < n; ++i) ids[i] = i;
	for (int i = n - 1; i > 0; --i) std::swap(ids[i], ids[rng.range_int(0, i)]);
	std::stable_sort(ids.begin(), ids.end(), [&](int a, int b) { return (teams ? teams[a] : 0) < (teams ? teams[b] : 0); });
	return ids;
}

// primeSortPlayers: alternate from both ends so neighbouring teams differ.
std::vector<int> prime_sort(const std::vector<int> &ids) {
	std::vector<int> out;
	const int n = (int)ids.size();
	for (int i = 0; i < n / 2; ++i) {
		out.push_back(ids[i]);
		out.push_back(ids[n - 1 - i]);
	}
	if (n % 2) out.push_back(ids[n / 2]);
	return out;
}

// rmgen Noise2D: periodic gradient noise on a freq x freq lattice, 0..1.
struct Noise2D {
	int freq;
	std::vector<float> gx, gz;
	Noise2D(RmsRng &rng, float f) : freq(std::max(1, (int)std::floor(f))) {
		for (int i = 0; i < freq * freq; ++i) {
			const float a = rng.range_float(0.0f, kTau);
			gx.push_back(det::cos(a));
			gz.push_back(det::sin(a));
		}
	}
	float get(float x, float y) const {
		x *= (float)freq;
		y *= (float)freq;
		int ix = (int)std::floor(x), iy = (int)std::floor(y);
		const float fx = x - (float)ix, fy = y - (float)iy;
		ix = ((ix % freq) + freq) % freq;
		iy = ((iy % freq) + freq) % freq;
		const int ix1 = (ix + 1) % freq, iy1 = (iy + 1) % freq;
		auto dot = [&](int i, int j, float dx, float dy) {
			const size_t k = (size_t)i * (size_t)freq + (size_t)j;
			return gx[k] * dx + gz[k] * dy;
		};
		const float s = dot(ix, iy, fx, fy), t = dot(ix1, iy, fx - 1.0f, fy);
		const float u = dot(ix, iy1, fx, fy - 1.0f), v = dot(ix1, iy1, fx - 1.0f, fy - 1.0f);
		const float ex = fx * fx * (3.0f - 2.0f * fx), ey = fy * fy * (3.0f - 2.0f * fy);
		const float a = s + ex * (t - s), b = u + ex * (v - u);
		return (a + ey * (b - a)) * 0.5f + 0.5f;
	}
};

// Marks the vertices within r of the segment a-b.
void mask_capsule(const HeightmapGrid &hm, std::vector<uint8_t> &mask, float ax, float az, float bx, float bz, float r) {
	const float sp = grid_spacing(hm), half = hm.map_half;
	const int x0 = std::max(0, (int)((std::min(ax, bx) - r + half) / sp)), x1 = std::min(hm.res - 1, (int)((std::max(ax, bx) + r + half) / sp) + 1);
	const int z0 = std::max(0, (int)((std::min(az, bz) - r + half) / sp)), z1 = std::min(hm.res - 1, (int)((std::max(az, bz) + r + half) / sp) + 1);
	const float dx = bx - ax, dz = bz - az, len2 = dx * dx + dz * dz;
	for (int z = z0; z <= z1; ++z)
		for (int x = x0; x <= x1; ++x) {
			const float wx = -half + (float)x * sp, wz = -half + (float)z * sp;
			const float t = len2 > 0.0f ? std::clamp(((wx - ax) * dx + (wz - az) * dz) / len2, 0.0f, 1.0f) : 0.0f;
			const float ex = wx - (ax + dx * t), ez = wz - (az + dz * t);
			if (ex * ex + ez * ez <= r * r) mask[(size_t)z * (size_t)hm.res + (size_t)x] = 1;
		}
}

// ClumpPlacer(area, coherence, smoothness) at (cx, cz): a blob of about
// `area` tiles whose radius wanders by (1 - coherence) round the circle,
// through 1 + 1 / smoothness control values.
std::vector<uint8_t> clump_mask(const HeightmapGrid &hm, RmsRng &rng, float cx, float cz, float area, float coherence, float smoothness) {
	std::vector<uint8_t> mask(hm.data.size(), 0);
	const float r = std::sqrt(std::max(area, 1.0f) / 3.14159265f);
	int ctrl = 1 + (int)std::floor(1.0f / std::max(smoothness, 1.0f / (kTau * r)));
	ctrl = std::max(3, std::min(ctrl, (int)std::floor(kTau * r) + 1));
	std::vector<float> v((size_t)ctrl);
	for (auto &c : v) c = rng.range_float(0.0f, 2.0f);
	const float sp = grid_spacing(hm), half = hm.map_half, rmax = r * (2.0f - coherence) + sp;
	const int x0 = std::max(0, (int)((cx - rmax + half) / sp)), x1 = std::min(hm.res - 1, (int)((cx + rmax + half) / sp) + 1);
	const int z0 = std::max(0, (int)((cz - rmax + half) / sp)), z1 = std::min(hm.res - 1, (int)((cz + rmax + half) / sp) + 1);
	for (int z = z0; z <= z1; ++z)
		for (int x = x0; x <= x1; ++x) {
			const float dx = -half + (float)x * sp - cx, dz = -half + (float)z * sp - cz;
			const float d = det::hypot(dx, dz);
			float t = (det::atan2(dz, dx) / kTau + 0.5f) * (float)ctrl;
			const int i = std::min(ctrl - 1, (int)std::floor(t));
			t -= (float)i;
			auto at = [&](int k) { return v[(size_t)(((k % ctrl) + ctrl) % ctrl)]; };
			const float noise = std::clamp(cubic_weight(at(i - 1), at(i), at(i + 1), at(i + 2), t), 0.0f, 2.0f);
			if (d <= std::max(sp * 0.5f, r * (coherence + (1.0f - coherence) * noise)))
				mask[(size_t)z * (size_t)hm.res + (size_t)x] = 1;
		}
	return mask;
}

// PathPlacer(start, end, width, waviness, smoothness, offset, tapering):
// the straight line bent by cubic-interpolated random offsets of up to
// 1 + len / 4 * offset tiles at 1 + len / 4 * waviness control points, the
// width shrinking by `tapering` towards the end.
std::vector<uint8_t> path_mask(const HeightmapGrid &hm, RmsRng &rng, float ax, float az, float bx, float bz, float width,
		float waviness, float smoothness, float offset, float tapering) {
	std::vector<uint8_t> mask(hm.data.size(), 0);
	const float len = std::max(1e-3f, det::hypot(bx - ax, bz - az));
	const float px = -(bz - az) / len, pz = (bx - ax) / len;
	const int steps = 1 + (int)std::floor(len / 4.0f * waviness), isteps = 1 + (int)std::floor(len / 4.0f * smoothness);
	const float amp = 1.0f + std::floor(len / 4.0f * offset);
	std::vector<float> ctrl((size_t)steps, 0.0f);
	for (int j = 1; j < steps - 1; ++j) ctrl[(size_t)j] = rng.range_float(-amp, amp);
	auto c = [&](int k) { return ctrl[(size_t)(((k % steps) + steps) % steps)]; };
	const int total = steps * isteps;
	float lx = ax, lz = az;
	for (int j = 1; j <= total; ++j) {
		const int s = (j - 1) / isteps, k = (j - 1) % isteps;
		const float noise = j == total ? 0.0f : cubic_weight(c(s - 1), c(s), c(s + 1), c(s + 2), (float)(k + 1) / (float)isteps);
		const float f = (float)j / (float)total;
		const float nx = ax + (bx - ax) * f + px * noise, nz = az + (bz - az) * f + pz * noise;
		mask_capsule(hm, mask, lx, lz, nx, nz, std::max(0.5f, (1.0f - tapering * f) * width * 0.5f));
		lx = nx;
		lz = nz;
	}
	return mask;
}

// rmgen rndRiver: a wave of period 1 with a seeded random shape.
float rnd_river(float f, float seed) {
	float rw = seed;
	for (int i = 0; (float)i <= f; ++i) rw = 10.0f * std::fmod(rw, 1.0f);
	const float r = std::fmod(f, 1.0f);
	float ret = (((int)std::floor(f)) % 2 ? -1.0f : 1.0f) * r * (r - 1.0f);
	switch (((int)std::floor(rw)) % 5) {
		case 0: ret *= 2.3f * (r - 0.5f) * (r - 0.5f); break;
		case 1: ret *= 2.6f * (r - 0.3f) * (r - 0.7f); break;
		case 2: ret *= 22.0f * (r - 0.2f) * (r - 0.3f) * (r - 0.3f) * (r - 0.8f); break;
		case 3: ret *= 180.0f * (r - 0.2f) * (r - 0.2f) * (r - 0.4f) * (r - 0.6f) * (r - 0.6f) * (r - 0.8f); break;
		default: ret *= 2.6f * (r - 0.5f) * (r - 0.7f); break;
	}
	return ret;
}

// rmgen paintRiver (deviation 0, no meanderLong): water `width` wide along
// a-b, the bed at `bed` fading to `land` over `fade` tiles at each shore;
// each shore meanders by its own rndRiver curve (shared when parallel).
// water(i, h) / dry(i) are called for vertices beside the river, in and
// out of the water.
template <class Water, class Dry>
void paint_river(HeightmapGrid &hm, RmsRng &rng, float ax, float az, float bx, float bz, float width, float fade,
		float bed, float land, float meander_short, bool parallel, Water water, Dry dry) {
	const float half = hm.map_half, size = 2.0f * half;
	const float meander = size * meander_short / scale_f(35.0f, 160.0f, half);
	const float seed1 = rng.range_float(2.0f, 3.0f), seed2 = rng.range_float(2.0f, 3.0f);
	const float sa1 = rng.range_float(0.0f, 1.0f), sa2 = rng.range_float(0.0f, 1.0f);
	const float len = std::max(1e-3f, det::hypot(bx - ax, bz - az));
	const float ux = (bx - ax) / len, uz = (bz - az) / len;
	for (int z = 0; z < hm.res; ++z)
		for (int x = 0; x < hm.res; ++x) {
			float wx, wz;
			hm.grid_to_world(x, z, wx, wz);
			const float along = (wx - ax) * ux + (wz - az) * uz;
			if (along < -0.5f || along > len + 0.5f) continue;
			const float dist = ux * (wz - az) - uz * (wx - ax);
			const float frac = std::clamp(along / len, 0.0f, 1.0f);
			const float c1 = meander * rnd_river(sa1 + frac * size / 128.0f, seed1);
			const float c2 = parallel ? c1 : meander * rnd_river(sa2 + frac * size / 128.0f, seed2);
			const float s1 = c1 + dist - width / 2.0f, s2 = c2 + dist + width / 2.0f;
			const size_t i = (size_t)z * (size_t)hm.res + (size_t)x;
			if (s1 < 0.0f && s2 > 0.0f) {
				float h = bed;
				if (s1 > -fade) h += (land - bed) * (1.0f + s1 / fade);
				else if (s2 < fade) h += (land - bed) * (1.0f - s2 / fade);
				hm.data[i] = h;
				water(i, h);
			} else {
				dry(i);
			}
		}
}

// rmgen createPassage with fixed heights: a strip `width` wide from a to b
// set to h (blended over smooth_w at its sides), only where the ground is
// below h. The changed vertices are marked in `mark` (its tileClass).
void passage(HeightmapGrid &hm, float ax, float az, float bx, float bz, float width, float smooth_w, float h,
		std::vector<uint8_t> *mark = nullptr) {
	const float len = std::max(1e-3f, det::hypot(bx - ax, bz - az));
	const float ux = (bx - ax) / len, uz = (bz - az) / len, hw = width / 2.0f;
	for (int z = 0; z < hm.res; ++z)
		for (int x = 0; x < hm.res; ++x) {
			float wx, wz;
			hm.grid_to_world(x, z, wx, wz);
			const float along = (wx - ax) * ux + (wz - az) * uz;
			if (along < 0.0f || along > len) continue;
			const float across = std::fabs(ux * (wz - az) - uz * (wx - ax));
			if (across > hw) continue;
			float &cur = hm.data[(size_t)z * (size_t)hm.res + (size_t)x];
			if (cur >= h) continue;
			if (mark) (*mark)[(size_t)z * (size_t)hm.res + (size_t)x] = 1;
			const float sd = smooth_w + across - hw;
			cur = sd > 0.0f ? (cur * sd + h / sd) / (sd + 1.0f / sd) : h;
		}
}

// Distance from every vertex to the nearest of `pts`.
std::vector<float> dist_to_points(const HeightmapGrid &hm, const std::vector<std::pair<float, float>> &pts) {
	std::vector<float> d(hm.data.size(), 1e9f);
	for (int z = 0; z < hm.res; ++z)
		for (int x = 0; x < hm.res; ++x) {
			float wx, wz;
			hm.grid_to_world(x, z, wx, wz);
			float &v = d[(size_t)z * (size_t)hm.res + (size_t)x];
			for (const auto &p : pts) v = std::min(v, det::hypot(wx - p.first, wz - p.second));
		}
	return d;
}

// Distance to the nearest vertex outside `mask` (0 outside it).
std::vector<float> dist_inside(const HeightmapGrid &hm, const std::vector<uint8_t> &mask) {
	std::vector<uint8_t> out(mask.size());
	for (size_t i = 0; i < mask.size(); ++i) out[i] = !mask[i];
	return distance_to_mask(out, hm.res, grid_spacing(hm));
}

// createAreas / createArea: up to `attempts` placer areas round random
// centres from `centre()`; vertices `ok` rejects are left out, the centre
// must be allowed, and an area with more than `fail` of its vertices
// rejected is dropped. `paint` takes each placed mask; stops after `amount`.
template <class Centre, class Make, class Ok, class Paint>
int create_areas(const HeightmapGrid &hm, int amount, int attempts, float fail, Centre centre, Make make, Ok ok, Paint paint) {
	int placed = 0;
	for (int a = 0; a < attempts && placed < amount; ++a) {
		const auto c = centre();
		float gx, gz;
		hm.world_to_grid(c.first, c.second, gx, gz);
		const int ix = (int)std::lround(gx), iz = (int)std::lround(gz);
		if (ix < 0 || iz < 0 || ix >= hm.res || iz >= hm.res || !ok((size_t)iz * (size_t)hm.res + (size_t)ix)) continue;
		std::vector<uint8_t> m = make(c.first, c.second);
		int in = 0, bad = 0;
		for (size_t i = 0; i < m.size(); ++i) {
			if (!m[i]) continue;
			++in;
			if (!ok(i)) { m[i] = 0; ++bad; }
		}
		if (!in || (float)bad > fail * (float)in || in == bad) continue;
		paint(m);
		++placed;
	}
	return placed;
}

// SmoothElevationPainter(ELEVATION_MODIFY, dh, blend) over `mask`.
void modify_mask_elevation(HeightmapGrid &hm, const std::vector<uint8_t> &mask, float dh, float blend) {
	const auto d = dist_inside(hm, mask);
	for (size_t i = 0; i < mask.size(); ++i)
		if (mask[i]) hm.data[i] += dh * std::clamp(d[i] / std::max(blend, 1e-3f), 0.0f, 1.0f);
}

// RTS0's starting base (distribute_player_bases) faces forward_dir; turn
// each base, starting from facing the centre, to the heading that puts the
// most of its buildings, mines, trees and animals on dry land.
void face_most_land(const HeightmapGrid &hm, std::vector<RmsGenerator::PlayerSpawn> &spawns, float water_h) {
	static const float pts[][2] = {{8.5f, 0.0f}, {-6.5f, 6.0f}, {-6.5f, -6.0f}, {5.5f, 6.5f}, {-10.5f, 3.5f}, {-14.0f, 3.5f},
	                               {3.5f, -11.5f}, {-8.5f, -9.5f}, {22.0f, 6.0f}, {18.0f, -16.0f}, {-14.0f, -5.0f}, {19.0f, -5.0f}};
	const float dry_h = water_h + NAV_WATER_MARGIN + 0.05f, lim = hm.map_half - 1.0f;
	for (auto &sp : spawns) {
		const float base = det::atan2(sp.forward_dir_z, sp.forward_dir_x);
		int best = -1;
		float best_a = base;
		for (int k = 0; k < 16; ++k) {
			const float a = base + (k % 2 ? 1.0f : -1.0f) * (float)((k + 1) / 2) * kTau / 16.0f;
			const float fx = det::cos(a), fz = det::sin(a), rx = -fz, rz = fx;
			int dry = 0;
			for (const auto &p : pts) {
				const float x = sp.x + fx * p[0] + rx * p[1], z = sp.z + fz * p[0] + rz * p[1];
				dry += std::fabs(x) < lim && std::fabs(z) < lim && hm.sample_world(x, z) > dry_h;
			}
			if (dry > best) { best = dry; best_a = a; }
		}
		sp.forward_dir_x = det::cos(best_a);
		sp.forward_dir_z = det::sin(best_a);
	}
}

// Spawns at `locs` (player i at locs[i]) levelled at max(ground, min_h)
// over 14 tiles, with the ground within lift_r raised to that height so the
// starting resources stay dry.
void finish_spawns(HeightmapGrid &hm, std::vector<RmsGenerator::PlayerSpawn> &spawns, const std::vector<std::pair<float, float>> &locs,
		float min_h, float lift_r, float water_h) {
	spawns.clear();
	const float lim = hm.map_half - 16.0f;
	for (int i = 0; i < (int)locs.size(); ++i) {
		const float sx = std::clamp(locs[i].first, -lim, lim), sz = std::clamp(locs[i].second, -lim, lim);
		const float l = std::max(1e-3f, det::hypot(sx, sz));
		const float bh = std::max(min_h, hm.sample_world(sx, sz));
		spawns.push_back({(uint8_t)i, sx, sz, bh, -sx / l, -sz / l});
	}
	for (const auto &s : spawns) hm.level_area(s.x, s.z, 14.0f, 6.0f, s.base_height);
	for (const auto &s : spawns) lift_area(hm, s.x, s.z, lift_r, 4.0f, s.base_height);
	face_most_land(hm, spawns, water_h);
}

// The shared heightmap-image flow of mediterranean.js and pompeii.js:
// random start spots (playerPlacementRandom, grouped by team) on the walkable
// land `stay` tiles inside the land and `cliff_gap` from cliffs (slope 2+,
// 2 tiles from water), each flattened over 0.8 of a base radius.
bool place_on_heightmap(HeightmapGrid &hm, RmsRng &rng, std::vector<RmsGenerator::PlayerSpawn> &spawns, int num_players,
		const uint8_t *teams, float water_h, float stay, float cliff_gap, bool disk_land) {
	const float half = hm.map_half, sp = grid_spacing(hm);
	const size_t n = hm.data.size();
	std::vector<uint8_t> water(n), cliff(n), not_land(n);
	for (size_t i = 0; i < n; ++i) water[i] = hm.data[i] <= water_h;
	const auto to_water = distance_to_mask(water, hm.res, sp);
	const auto slope = slopes_m(hm);
	for (size_t i = 0; i < n; ++i) cliff[i] = to_water[i] > 2.0f && slope[i] >= 2.0f;
	const auto to_cliff = distance_to_mask(cliff, hm.res, sp);
	for (int z = 0; z < hm.res; ++z)
		for (int x = 0; x < hm.res; ++x) {
			float wx, wz;
			hm.grid_to_world(x, z, wx, wz);
			const size_t i = (size_t)z * (size_t)hm.res + (size_t)x;
			not_land[i] = water[i] || (disk_land && wx * wx + wz * wz > half * half);
		}
	const auto to_not_land = distance_to_mask(not_land, hm.res, sp);
	const auto region = largest_walkable_region(hm, water_h);
	std::vector<uint8_t> allowed(n);
	for (size_t i = 0; i < n; ++i) allowed[i] = region[i] && to_cliff[i] > cliff_gap && to_not_land[i] > stay;
	std::vector<std::pair<float, float>> locs;
	if (!player_placement_random(hm, rng, allowed, num_players, locs)) return false;
	group_players_by_area(locs, num_players, teams);
	const float base_r = (float)scale_by_map_size(15, 25, half);
	make_heightmap_spawns(hm, spawns, locs, base_r * 0.8f, 6.0f, water_h);
	face_most_land(hm, spawns, water_h);
	return true;
}

} // namespace misc_maps
} // namespace

void RmsGenerator::synth_ardennes_forest(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// ardennes_forest.js: the map starts at heightHill (50 m) and a central dip of 30 m
	// (ClumpPlacer(diskArea(0.44 of the map)), blend 3) leaves a high rim, roughened by Noise2D(20)
	// (+-20 m above 40 m). Players by pattern at f = 0.35. Then for clump sizes scaleByMapSize(50, 800),
	// (50, 400), (10, 30), (10, 30): hills set to 50 m (avoiding players 8 and other hills 5), the
	// big ones topped by +10 m clumps, and ravines set to 2 m (avoiding players 6), the biggest with
	// 2 m and 40 m clumps inside. Last, +-5 m of the same noise, none within 13 tiles of a base.
	using misc_maps::Noise2D;
	water_h = -10.0f;
	const float half = hm.map_half, size = 2.0f * half, k = kMetre;
	const float h_valley = 2.0f * k, h_land = 30.0f * k, h_ravine_hill = 40.0f * k, h_hill = 50.0f * k, h_offset = 10.0f * k;
	hm.init(hm.res, hm.map_half, h_hill);
	const size_t nv = (size_t)hm.res * (size_t)hm.res;
	const auto anywhere = [](int) { return true; };
	auto disk = [](float r) { return kSeasPi * r * r; };
	paint_mask_elevation(hm, seas_clump(hm, rng, disk(0.44f * size), 0.94f, 0.05f, 0.1f, 0.0f, 0.0f, anywhere), h_land, 3.0f);
	const Noise2D noise0(rng, 20.0f);
	auto frac = [&](int i, float &fx, float &fz) {
		float wx, wz;
		seas_world(hm, i, wx, wz);
		fx = (wx + half) / (size + 1.0f);
		fz = (wz + half) / (size + 1.0f);
	};
	std::vector<uint8_t> cl_hill(nv, 0);
	for (size_t i = 0; i < nv; ++i)
		if (hm.data[i] > h_ravine_hill) {
			cl_hill[i] = 1;
			float fx, fz;
			frac((int)i, fx, fz);
			hm.data[i] += (noise0.get(fx, fz) - 0.5f) * h_ravine_hill;
		}
	place_team_spawns(hm, rng, spawns, num_players, 0.35f * size, rng.range_float(0.0f, 2.0f * kSeasPi), h_land);
	std::vector<std::pair<float, float>> locs;
	for (const auto &s : spawns) locs.emplace_back(s.x, s.z);
	// clPlayer is ClumpPlacer(250) round each base; the base mines sit 16 out (clBaseResource 2).
	const auto d_player = seas_dist_to_players(hm, locs, std::sqrt(250.0f / kSeasPi));
	const auto d_base_res = seas_dist_to_players(hm, locs, 16.0f);
	// createAreasInAreas: centres on a random vertex of a random area.
	auto in_areas = [&](const std::vector<std::vector<uint8_t>> &areas, float amount, int retry, const auto &place) {
		std::vector<std::vector<int>> pts;
		for (const auto &a : areas) {
			pts.emplace_back();
			for (size_t i = 0; i < a.size(); ++i) if (a[i]) pts.back().push_back((int)i);
		}
		int placed = 0;
		for (int tries = 0; (float)placed < amount && (float)tries < amount * (float)retry; ++tries) {
			const auto &p = pts[(size_t)rng.range_int(0, (int)pts.size() - 1)];
			if (p.empty()) continue;
			float wx, wz;
			seas_world(hm, p[(size_t)rng.range_int(0, (int)p.size() - 1)], wx, wz);
			if (place(wx, wz)) ++placed;
		}
	};
	for (const float area : {seas_scale(50, 800, half), seas_scale(50, 400, half), seas_scale(10, 30, half), seas_scale(10, 30, half)}) {
		auto make = [&](float h, float blend, float avoid_player, float amount) {
			std::vector<std::vector<uint8_t>> made;
			seas_create_areas(hm, rng, amount, 10, [&](float x, float z) {
				const auto d_hill = seas_dist_inside(hm, cl_hill);
				auto m = seas_clump(hm, rng, area, 0.1f, 0.2f, 0.1f, x, z, [&](int i) {
					return d_player[(size_t)i] > avoid_player && d_base_res[(size_t)i] > 2.0f && d_hill[(size_t)i] > 5.0f;
				});
				if (!seas_any(m)) return false;
				paint_mask_elevation(hm, m, h, blend);
				seas_add(cl_hill, m);
				made.push_back(std::move(m));
				return true;
			});
			return made;
		};
		const auto mountains = make(h_hill, area < 50.0f ? 2.0f : 4.0f, 8.0f, seas_scale(1, 4, half));
		if (area > 100.0f && !mountains.empty()) {
			const auto inside = seas_dist_outside(hm, cl_hill);
			in_areas(mountains, (float)mountains.size() * 2.0f, 20, [&](float x, float z) {
				const auto m = seas_clump(hm, rng, area * 0.3f, 0.94f, 0.05f, 0.1f, x, z, [&](int i) { return inside[(size_t)i] > 4.0f; });
				if (!seas_any(m)) return false;
				seas_paint_modify(hm, m, h_offset, 3.0f);
				return true;
			});
		}
		const auto ravines = make(h_valley, 2.0f, 6.0f, seas_scale(1, 3, half));
		if (area > 150.0f && !ravines.empty()) {
			// The huts are actors; their clHillDeco gaps are left out.
			in_areas(ravines, (float)ravines.size() * 2.0f, 20, [&](float x, float z) {
				const auto m = seas_clump(hm, rng, area * 0.3f, 0.94f, 0.05f, 0.1f, x, z, [&](int i) { return cl_hill[(size_t)i] != 0; });
				if (!seas_any(m)) return false;
				paint_mask_elevation(hm, m, h_valley, 2.0f);
				return true;
			});
			// borderClasses(clHill, 15, 1): within 15 inside or 1 outside the hills' edge.
			in_areas(ravines, (float)ravines.size() * 2.0f, 50, [&](float x, float z) {
				const auto in_d = seas_dist_outside(hm, cl_hill), out_d = seas_dist_inside(hm, cl_hill);
				const auto m = seas_clump(hm, rng, area * 0.1f, 0.3f, 0.05f, 0.1f, x, z, [&](int i) {
					return cl_hill[(size_t)i] ? in_d[(size_t)i] <= 15.0f : out_d[(size_t)i] <= 1.0f;
				});
				if (!seas_any(m)) return false;
				paint_mask_elevation(hm, m, h_ravine_hill, 2.0f);
				seas_add(cl_hill, m);
				return true;
			});
		}
	}
	// playerNearness: 0 within 13 tiles of a base, rising to 1 at 19.
	for (size_t i = 0; i < nv; ++i) {
		float fx, fz, wx, wz;
		frac((int)i, fx, fz);
		seas_world(hm, (int)i, wx, wz);
		float d = 1e9f;
		for (const auto &l : locs) d = std::min(d, det::hypot(wx - l.first, wz - l.second));
		const float pn = d < 13.0f ? 0.0f : (d < 19.0f ? (d - 13.0f) / 6.0f : 1.0f);
		hm.data[i] += (noise0.get(fx, fz) - 0.5f) * 10.0f * k * pn;
	}
}

void RmsGenerator::synth_mediterranean_0ad(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players,
		const uint8_t *teams) {
	// mediterranean.js: LoadHeightmapImage("mediterranean.png", 0, 40), sea
	// ground (height <= 0) lowered to heightScale(-6) with a 2-tile blend,
	// SmoothingPainter(1, scaleByMapSize(0.3, 0.8), 1), players at random
	// (playerPlacementRandom) scaleByMapSize(8, 25) inside the land of the
	// map's disk, each start flattened over 0.8 base radius. heightScale(n) =
	// n * size / 320 metres. RTS0 keeps every base on the largest walkable
	// land and leaves out the five climate zones (one biome per map).
	using namespace misc_maps;
	water_h = 1.0f;
	const float half = hm.map_half, size = 2.0f * half;
	const int n = heightmap_images::mediterranean_size;
	paint_heightmap(hm, mediterranean_image(), n, 0, 0, n, 0.0f, 40.0f, water_h);
	lower_sea_ground(hm, water_h + 0.001f, metres(water_h, -6.0f * size / 320.0f), 2.0f);
	shepard_smooth(hm, scale_f(0.3f, 0.8f, half));
	if (place_on_heightmap(hm, rng, spawns, num_players, teams, water_h, scale_f(8.0f, 25.0f, half), -1.0f, true)) return;
	place_ring_spawns(hm, spawns, num_players, half * 0.6f, rng.range_float(0.0f, kTau), kTau, water_h + 1.5f);
	for (const auto &s : spawns) lift_area(hm, s.x, s.z, 20.0f, 6.0f, s.base_height);
}

void RmsGenerator::synth_pompeii(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players,
		const uint8_t *teams) {
	// pompeii.js: LoadHeightmapImage("pompeii.png", 0, 140) (the Bay of
	// Naples with Vesuvius), sea ground lowered to heightScale(-30) with a
	// 2-tile blend, SmoothingPainter(1, 0.8, 1), cliffs where the slope is 2
	// or more (2 tiles from water), players at random scaleByMapSize(5, 15)
	// inside the land and 5 from cliffs, each start flattened over 0.8 base
	// radius. RTS0 keeps every base on the largest walkable land; the gaia
	// docks, ruins, statues and lava terrain are left out.
	using namespace misc_maps;
	water_h = 1.0f;
	const float half = hm.map_half, size = 2.0f * half;
	const int n = heightmap_images::pompeii_size;
	paint_heightmap(hm, pompeii_image(), n, 0, 0, n, 0.0f, 140.0f, water_h);
	lower_sea_ground(hm, water_h + 0.001f, metres(water_h, -30.0f * size / 320.0f), 2.0f);
	shepard_smooth(hm, 0.8f);
	if (place_on_heightmap(hm, rng, spawns, num_players, teams, water_h, scale_f(5.0f, 15.0f, half), 5.0f, false)) return;
	place_ring_spawns(hm, spawns, num_players, half * 0.6f, rng.range_float(0.0f, kTau), kTau, water_h + 1.5f);
	for (const auto &s : spawns) lift_area(hm, s.x, s.z, 20.0f, 6.0f, s.base_height);
}

void RmsGenerator::synth_elephantine(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players,
		const uint8_t *teams) {
	// elephantine.js: the Nile at the first cataract from a river outline
	// (LoadHeightmapImage("elephantine.png", -1, 2)), water lowered to -6 m
	// with a 4-tile blend, SmoothingPainter(1, scaleByMapSize(0.1, 0.5), 1),
	// players on both banks (playerPlacementRiver(0.22 pi, 0.62 of the
	// map)) and bumps. Crossing needs ships. RTS0 keeps the land at least 3 m
	// up (the image's 2 m scale to under RTS0's shore on small maps). The
	// island's Gaia temple, pyramid, towers, soldiers and treasures are
	// place_elephantine_entities'. rmgen's z runs up the image, so the
	// script's angles are mirrored in z here.
	using namespace misc_maps;
	water_h = 1.0f;
	const float half = hm.map_half, size = 2.0f * half;
	const int n = heightmap_images::elephantine_size;
	const float land_m = std::max(2.0f * (size + 1.0f) / 321.0f, 3.0f);
	paint_heightmap(hm, elephantine_image(), n, 0, 0, n, -1.0f, land_m * 321.0f / (size + 1.0f), water_h);
	lower_sea_ground(hm, water_h + 0.001f, metres(water_h, -6.0f), 4.0f);
	shepard_smooth(hm, scale_f(0.1f, 0.5f, half));
	// playerPlacementRiver(riverAngle, 0.62 * size).
	const float angle = 0.22f * 3.14159265f, width = 0.62f * size;
	std::vector<std::pair<float, float>> locs(num_players);
	const bool even = num_players % 2 == 0;
	for (int i = 0; i < num_players; ++i) {
		const bool cur_even = i % 2 == 0;
		const int divident = (even || cur_even) ? (i + 1) % 2 : 0;
		const int divisor = even ? 0 : (cur_even ? 1 : -1);
		const float f = ((float)(i - 1 + divident) / 2.0f + 1.0f) / ((float)(num_players + divisor) / 2.0f + 1.0f);
		const auto p = rot(width * (float)(i % 2) - width / 2.0f, (f - 0.5f) * size, angle);
		locs[i] = {p.first, -p.second};
	}
	group_players_by_area(locs, num_players, teams);
	const float land = metres(water_h, land_m);
	std::vector<PlayerSpawn> probe;
	for (const auto &l : locs) probe.push_back({0, l.first, l.second, land, 0.0f, 0.0f});
	dry_bumps(hm, rng, probe, scale_by_map_size(10, 500, half), 20.0f + 10.0f, water_h + 0.5f);
	finish_spawns(hm, spawns, locs, land, 18.0f, water_h);
}

void RmsGenerator::synth_lower_nubia(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players,
		const uint8_t *teams) {
	// lower_nubia.js: the Nile between the first and second cataracts.
	// HeightmapPainter(heightmapCombined, -3, 150) (lower_nubia_image), sea
	// ground (heightScale(-3) and below) set to heightScale(-3) with a 3-tile
	// blend, the Nile passages, SmoothingPainter(1, scaleByMapSize(0.5, 1), 1),
	// players at random (playerPlacementRandom) scaleByMapSize(8, 12) from the
	// Nile and from cliffs, each start flattened over 0.8 base radius.
	// heightScale(n) = n * size / 320 metres. RTS0 keeps every base on the
	// largest walkable land. rmgen's y runs up the image and RTS0's z down it,
	// so the script's points are mirrored in z.
	using namespace misc_maps;
	water_h = 1.0f;
	const float half = hm.map_half, size = 2.0f * half;
	const int n = heightmap_images::lower_nubia_size;
	paint_heightmap(hm, lower_nubia_image(), n, 0, 0, n, -3.0f, 150.0f, water_h);
	const float sea_m = -3.0f * size / 320.0f;
	// The painter's black is -3 (size + 1) / 321 m: on maps over 320 tiles a hair
	// above heightScale(-3), where the script's HeightConstraint would miss the
	// Nile it painted; RTS0 takes the black as sea ground on every size.
	const float sea_top = metres(water_h, std::max(sea_m, -3.0f * (size + 1.0f) / 321.0f + 0.01f));
	const size_t N = hm.data.size();
	std::vector<uint8_t> nile(N);
	for (size_t i = 0; i < N; ++i) nile[i] = hm.data[i] <= sea_top;
	lower_sea_ground(hm, sea_top + 1e-4f, metres(water_h, sea_m), 3.0f);

	// "Creating Nile passages": scaleByMapSize(8, 15) PathPlacer strips
	// scaleByMapSize(5, 7) wide (waviness 0.2, smoothness 5, offset 0.2) across
	// the map at riverAngle 3 pi / 4, at random offsets; on the tiles within 4 of
	// the Nile and scaleByMapSize(15, 25) from earlier passages the ground is
	// blended half way to heightNileForests (heightScale(15),
	// ElevationBlendingPainter), then SmoothingPainter(2, 1, 2).
	const float sp = grid_spacing(hm);
	const auto to_nile = distance_to_mask(nile, hm.res, sp);
	std::vector<uint8_t> passages(N, 0);
	const float forest_h = metres(water_h, 15.0f * size / 320.0f), angle = 3.0f * 3.14159265f / 4.0f;
	const int count = (int)std::ceil(scale_f(8.0f, 15.0f, half));
	for (int k = 0; k < count; ++k) {
		const float xc = rng.range_float(0.0f, 1.0f) * size - half;
		const auto a = rot(xc, -half, angle), b = rot(xc, half, angle);
		auto m = path_mask(hm, rng, a.first, -a.second, b.first, -b.second, scale_f(5.0f, 7.0f, half), 0.2f, 5.0f, 0.2f, 0.0f);
		const auto to_passage = distance_to_mask(passages, hm.res, sp);
		const float avoid = scale_f(15.0f, 25.0f, half);
		for (size_t i = 0; i < N; ++i) {
			m[i] = m[i] && to_nile[i] <= 4.0f && to_passage[i] > avoid;
			if (m[i]) hm.data[i] += 0.5f * (forest_h - hm.data[i]);
		}
		// SmoothingPainter(2, 1, 2): twice, each vertex to the inverse-distance
		// (Manhattan) weighted mean of the vertices within 2 tiles.
		const int r2 = std::max(1, (int)std::lround(2.0f / sp));
		for (int it = 0; it < 2; ++it) {
			const std::vector<float> src = hm.data;
			for (int z = 0; z < hm.res; ++z)
				for (int x = 0; x < hm.res; ++x) {
					const size_t i = (size_t)z * (size_t)hm.res + (size_t)x;
					if (!m[i]) continue;
					float sum = 0.0f, wsum = 0.0f;
					for (int dz = -r2; dz <= r2; ++dz)
						for (int dx = -r2; dx <= r2; ++dx) {
							const int nx = x + dx, nz = z + dz;
							if ((!dx && !dz) || nx < 0 || nz < 0 || nx >= hm.res || nz >= hm.res) continue;
							const float w = 1.0f / (float)(std::abs(dx) + std::abs(dz));
							sum += src[(size_t)nz * (size_t)hm.res + (size_t)nx] * w;
							wsum += w;
						}
					hm.data[i] = sum / wsum;
				}
		}
		for (size_t i = 0; i < N; ++i) passages[i] |= m[i];
	}
	shepard_smooth(hm, scale_f(0.5f, 1.0f, half));
	const float gap = scale_f(8.0f, 12.0f, half);
	if (place_on_heightmap(hm, rng, spawns, num_players, teams, water_h, gap, gap, false)) return;
	place_ring_spawns(hm, spawns, num_players, half * 0.6f, rng.range_float(0.0f, kTau), kTau, water_h + 1.5f);
	for (const auto &s : spawns) lift_area(hm, s.x, s.z, 20.0f, 6.0f, s.base_height);
}

// =========================================================================
// maps/random/jebel_barkal.js: the Kushite city of Napata at the foot of
// Jebel Barkal. rmgen works in tiles from the map's south-west corner with y
// up the map; RTS0's z runs the other way, so a point (x, y) of the script
// is (x, -y) here (centred) and its angles are mirrored.
// =========================================================================

namespace {
namespace jebel {

// jebel_barkal.png (tools/gen_heightmap_images.py) holds the PMP's raw heights 23170..33144 as
// 0..255: 255 is 797 x 9974 / 65535 m above 0 on a 320 tile map (HeightmapPainter maps
// 0..0xFFFF to minHeightSource 3..maxHeightSource 800).
constexpr float kImageSpanMetres = 797.0f * (33144.0f - 23170.0f) / 65535.0f;
constexpr float kPi = 3.14159265f;
constexpr float kRiverAngle = 0.05f * kPi;
constexpr float kPathWidth = 4.0f, kPathWidthCenter = 10.0f, kPathWidthSecondary = 6.0f;

const std::vector<uint8_t> &image() {
	static const std::vector<uint8_t> img = decode_heightmap_image(heightmap_images::jebel_barkal_b64, heightmap_images::jebel_barkal_size);
	return img;
}

// A point in the script's frame, centred on the map.
struct P {
	float x, y;
};
inline P rot_ccw(P p, float a) { // Vector2D.rotate(-a)
	const float c = det::cos(a), s = det::sin(a);
	return {p.x * c - p.y * s, p.x * s + p.y * c};
}
inline P polar_p(P c, float r, float a) { return {c.x + r * det::cos(a), c.y + r * det::sin(a)}; } // c + Vector2D(r, 0).rotate(-a)
inline P round_p(P p, float half) { return {std::round(p.x + half) - half, std::round(p.y + half) - half}; }
inline P from_world(float x, float z) { return {x, -z}; }
inline float world_x(P p) { return p.x; }
inline float world_z(P p) { return -p.y; }

// napata_wall and napata_palisade in tiles: the tower, the segment (both "short" and "medium"
// are the medium piece) and the gate (kush/wall_*: 8, 24 and 36 m; palisades_*: 3, 9, 14 m).
struct WallLengths {
	float tower, segment, gate;
};
inline WallLengths wall_lengths(uint8_t kind) {
	return kind == WALLSET_PALISADE ? WallLengths{0.75f, 2.25f, 3.5f} : WallLengths{2.0f, 6.0f, 9.0f};
}

inline float footprint_half(uint8_t type) { return (float)building_type_info(type).footprint * Simulation::GRID * 0.5f; }

// Slope in 0 A.D. metres per tile (rmgen getSlope: the steepest step to a neighbour).
std::vector<float> slopes(const HeightmapGrid &hm) {
	const int res = hm.res;
	const float sp = grid_spacing(hm);
	std::vector<float> s(hm.data.size(), 0.0f);
	for (int z = 0; z < res; ++z)
		for (int x = 0; x < res; ++x) {
			const float h = hm.get(x, z);
			float m = 0.0f;
			if (x > 0) m = std::max(m, std::fabs(hm.get(x - 1, z) - h));
			if (z > 0) m = std::max(m, std::fabs(hm.get(x, z - 1) - h));
			if (x < res - 1) m = std::max(m, std::fabs(hm.get(x + 1, z) - h));
			if (z < res - 1) m = std::max(m, std::fabs(hm.get(x, z + 1) - h));
			s[(size_t)z * (size_t)res + (size_t)x] = m / sp / kMetre;
		}
	return s;
}

} // namespace jebel
} // namespace

void RmsGenerator::synth_jebel_barkal_0ad(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players,
		const uint8_t *teams, JebelBarkalLayout &out) {
	// jebel_barkal.js: the hill from a 0 A.D. terrain file (LoadMapTerrain("jebel_barkal.pmp"),
	// translated 12 tiles west and scaleByMapSize(12, 25) south), the land below the map
	// centre's height raised to it (the desert), and along the south edge (paintRiver, turned
	// riverAngle 0.05 pi, meanderShort 40) the fertile land 0.33 of the map deep 2 m lower and
	// the Nile 0.1 of the map deep down to -8 m (heights x mapSize / 320 as heightScale).
	// Irrigation canals cross the fertile land, fords between them; players stand on two arcs
	// (playerPlacementArcs at 0.38 of the map, riverAngle - pi/2, 0.05 to 0.55 pi); the
	// pyramids in the north-west; Napata round the hill's foot: a grid of paths on a circle
	// segment (centre 0.3 of the map north, radius hillRadius + 18 per row) with the temples
	// on its inner row, a ritual place, districts between the paths, a wall round it from 192
	// tiles, roads from its gates, and bumps. RTS0 keeps the banks at least at their heights on
	// a 320 tile map (smaller maps would sink the fertile land under RTS0's shore) and the fords
	// wadeable. place_jebel_barkal_entities puts up the city and its soldiers.
	using namespace misc_maps;
	using namespace jebel;
	water_h = 1.0f;
	const float half = hm.map_half, size = 2.0f * half;
	const size_t N = hm.data.size();
	auto sc = [&](float a, float b) { return seas_scale(a, b, half); };
	const float hs = size / 320.0f, hw = std::max(hs, 1.0f);
	const uint8_t difficulty = out.difficulty; // the caller's Trigger Difficulty
	out = JebelBarkalLayout();
	out.difficulty = difficulty;
	const float h_fertile = metres(water_h, 3.0f * hw), h_desert = metres(water_h, 5.0f * hw);
	const float h_passage = std::max(metres(water_h, -1.5f * hw), wade_height(water_h) + 0.02f);
	const float h_canal = metres(water_h, -4.0f * hw), h_sea = metres(water_h, -8.0f * hw);
	const float h_hill = h_desert + 4.0f * hs * kMetre, h_hilltop = h_hill + 90.0f * hs * kMetre;
	out.height_hill = h_hill;
	out.height_hilltop = h_hilltop;
	out.height_archers = (h_hill + h_hilltop) / 2.0f;
	auto vertex_p = [&](size_t i) {
		float x, z;
		seas_world(hm, (int)i, x, z);
		return from_world(x, z);
	};

	// The hill, and the flat desert round it.
	{
		const auto &img = image();
		const int n = heightmap_images::jebel_barkal_size;
		const float to_src = 321.0f / (size + 1.0f), off_y = std::round(sc(-12.0f, -25.0f));
		auto grey = [&](P p) {
			const float sx = (p.x + half) * to_src - 12.0f, sy = (p.y + half) * to_src + off_y;
			if (sx < 0.0f || sy < 0.0f || sx > 320.0f || sy > 320.0f) return 0.0f; // translateHeightmap's default: the lowest
			return sample_image_bicubic(img, n, (sx - 0.5f) / 2.0f, (320.0f - sy - 0.5f) / 2.0f);
		};
		const float centre = grey({0.0f, 0.0f});
		const float per_grey = kImageSpanMetres / 255.0f * (size + 1.0f) / 321.0f * kMetre;
		for (size_t i = 0; i < N; ++i) hm.data[i] = h_desert + std::max(0.0f, grey(vertex_p(i)) - centre) * per_grey;
	}

	// paintRiver along the south edge with both banks meandering alike ("parallel").
	const P r_start = rot_ccw({-half, -half}, kRiverAngle), r_end = rot_ccw({half, -half}, kRiverAngle);
	auto river = [&](float width, float fade, float bed, float land, std::vector<uint8_t> *mark) {
		const double ms = size * 40.0 / sc(35.0f, 160.0f);
		const double seed = rng.range_float(2.0f, 3.0f);
		rng.range_float(2.0f, 3.0f); // the other bank's seed and angle, unused when parallel
		const double ang = rng.range_float(0.0f, 1.0f);
		rng.range_float(0.0f, 1.0f);
		const float len = det::hypot(r_start.x - r_end.x, r_start.y - r_end.y);
		const float ux = (r_start.x - r_end.x) / len, uy = (r_start.y - r_end.y) / len;
		for (size_t i = 0; i < N; ++i) {
			const P p = vertex_p(i);
			const float d = ux * (p.y - r_end.y) - uy * (p.x - r_end.x); // distanceOfPointFromLine
			const float fx = p.x + uy * d, fy = p.y - ux * d;           // its foot on the line
			const float along = -(fx - r_start.x) * ux - (fy - r_start.y) * uy;
			if (along < 0.0f || along > len) continue;
			const double c = ms * seas_rnd_river(ang + size * (along / len) / 128.0, seed);
			const double sd1 = c + d - width / 2.0f, sd2 = c + d + width / 2.0f;
			if (!(sd1 < 0.0 && sd2 > 0.0)) continue;
			double h = bed;
			if (sd1 > -fade) h += (land - bed) * (1.0 + sd1 / fade);
			else if (sd2 < fade) h += (land - bed) * (1.0 - sd2 / fade);
			hm.data[i] = (float)h;
			if (mark) (*mark)[i] = 1;
		}
	};
	std::vector<uint8_t> fertile(N, 0);
	river(0.66f * size, 8.0f, h_fertile, h_desert, &fertile);
	river(0.2f * size, 4.0f, h_sea, h_fertile, nullptr);

	// sortAllPlayers, partitionPlayers (teams on one side each, filled greedily, the players
	// without a team alone; one team splits in half) and playerPlacementArcs.
	std::vector<std::pair<float, float>> locs((size_t)num_players);
	{
		const auto ids = sort_all_players(rng, num_players, teams);
		std::vector<int> team_ids;
		std::vector<std::vector<int>> groups;
		for (int id : ids) {
			const int t = teams ? teams[id] : 0;
			const auto it = std::find(team_ids.begin(), team_ids.end(), t);
			if (it == team_ids.end()) {
				team_ids.push_back(t);
				groups.push_back({id});
			} else {
				groups[(size_t)(it - team_ids.begin())].push_back(id);
			}
		}
		const auto none = std::find(team_ids.begin(), team_ids.end(), 0);
		if (none != team_ids.end()) {
			const size_t k = (size_t)(none - team_ids.begin());
			const std::vector<int> alone = groups[k];
			groups.erase(groups.begin() + (long)k);
			for (int id : alone) groups.push_back({id});
		}
		if (groups.size() == 1) {
			const std::vector<int> all = groups[0];
			const long mid = (long)all.size() / 2;
			groups = {std::vector<int>(all.begin() + mid, all.end()), std::vector<int>(all.begin(), all.begin() + mid)};
		}
		std::stable_sort(groups.begin(), groups.end(), [](const auto &a, const auto &b) { return a.size() > b.size(); });
		std::vector<int> east, west;
		for (const auto &g : groups) {
			auto &side = east.size() > west.size() ? west : east;
			side.insert(side.end(), g.begin(), g.end());
		}
		const float map_angle = kRiverAngle - 0.5f * kPi, r = 0.38f * size;
		auto arc = [&](const std::vector<int> &side, float a0, float a1) {
			const int k = (int)side.size() + 2;
			for (int j = 1; j + 1 < k; ++j) {
				const P p = round_p(polar_p({0.0f, 0.0f}, r, a0 + (a1 - a0) * (float)j / (float)(k - 1)), half);
				locs[(size_t)side[(size_t)j - 1]] = {world_x(p), world_z(p)};
			}
		};
		arc(east, map_angle + 0.05f * kPi, map_angle + 0.55f * kPi);
		arc(west, map_angle - 0.05f * kPi, map_angle - 0.55f * kPi);
	}
	const auto to_player = seas_dist_to_players(hm, locs, 5.0f); // addCivicCenterAreaToClass: a disk of 5

	// Water (no longer fertile) and the desert.
	std::vector<uint8_t> water(N), desert(N);
	for (size_t i = 0; i < N; ++i) {
		water[i] = hm.data[i] < water_h;
		if (water[i]) fertile[i] = 0;
		desert[i] = hm.data[i] < h_hill && !water[i] && !fertile[i];
	}

	// Irrigation canals: 30 tries across the river, each kept 2 from the desert, dropped when
	// within scaleByMapSize(8, 13) of a player or scaleByMapSize(15, 25) of another canal.
	std::vector<uint8_t> canal(N, 0);
	std::vector<float> canal_x; // across the river's frame
	{
		const auto to_desert = seas_dist_inside(hm, desert);
		std::vector<std::vector<uint8_t>> areas;
		for (int k = 0; k < 30; ++k) {
			const float x = rng.range_float(0.0f, 1.0f) * size - half;
			const P a = rot_ccw({x, -half}, kRiverAngle), b = rot_ccw({x, half}, kRiverAngle);
			areas.push_back(seas_path(hm, rng, world_x(a), world_z(a), world_x(b), world_z(b), 3.0f, 0.0f, 10.0f, 0.1f, 0.01f, 1e9f,
			                          [&](int i) { return to_desert[(size_t)i] > 2.0f; }));
		}
		const float player_gap = sc(8.0f, 13.0f), canal_gap = sc(15.0f, 25.0f);
		for (const auto &m : areas) {
			std::vector<size_t> pts;
			for (size_t i = 0; i < N; ++i)
				if (!m.empty() && m[i]) pts.push_back(i);
			if (pts.empty()) continue;
			const auto to_canal = seas_dist_inside(hm, canal);
			bool ok = true;
			for (size_t i : pts) ok = ok && to_player[i] > player_gap && to_canal[i] > canal_gap;
			if (!ok) continue;
			canal_x.push_back(rot_ccw(vertex_p(pts[(size_t)rng.range_int(0, (int)pts.size() - 1)]), -kRiverAngle).x);
			// SmoothElevationPainter(ELEVATION_SET, heightIrrigationCanal, 1) where the ground is
			// between the canal's and the desert's height.
			std::vector<uint8_t> dig(N, 0);
			for (size_t i : pts)
				if (hm.data[i] >= h_canal && hm.data[i] <= h_desert) dig[i] = 1;
			const auto inside = seas_dist_outside(hm, dig);
			for (size_t i = 0; i < N; ++i)
				if (dig[i]) {
					hm.data[i] += (h_canal - hm.data[i]) * std::clamp(inside[i], 0.0f, 1.0f);
					canal[i] = 1;
				}
		}
	}
	// Fords: between each pair of canals a passage parallel to the river, raised to a wadeable
	// depth where it crosses a canal, each at another distance from the bank.
	{
		std::sort(canal_x.begin(), canal_x.end());
		const float width = 0.33f * size;
		const auto to_desert = seas_dist_inside(hm, desert);
		const auto in_fertile = seas_dist_outside(hm, fertile);
		float prev_y = (float)rng.range_int(0, (int)width);
		for (size_t i = 0; i < canal_x.size(); ++i) {
			const float prev = i == 0 ? -half : canal_x[i - 1], next = i + 1 == canal_x.size() ? half : canal_x[i + 1];
			const float x1 = (canal_x[i] + prev) / 2.0f, x2 = (canal_x[i] + next) / 2.0f;
			float y = 0.0f;
			for (int tries = 0; tries < 100; ++tries) {
				y = std::fmod(prev_y + (float)rng.range_int((int)(0.2f * width), (int)(0.8f * width)), width);
				const P pos = round_p(rot_ccw({(x1 + x2) / 2.0f, y - half}, kRiverAngle), half);
				const float h = hm.sample_world(world_x(pos), world_z(pos));
				const size_t v = (size_t)seas_vertex(hm, world_x(pos), world_z(pos));
				if (pos.x * pos.x + pos.y * pos.y < half * half && to_desert[v] > 12.0f && h >= h_canal && h <= h_fertile) break;
			}
			const P a = rot_ccw({x1, y - half}, kRiverAngle), b = rot_ccw({x2, y - half}, kRiverAngle);
			const auto m = seas_path(hm, rng, world_x(a), world_z(a), world_x(b), world_z(b), 10.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1e9f,
			                         [&](int v) { return hm.data[(size_t)v] <= h_passage && in_fertile[(size_t)v] > 2.0f; });
			if (!seas_any(m)) continue;
			for (size_t v = 0; v < N; ++v)
				if (m[v]) hm.data[v] = h_passage;
			prev_y = y;
		}
	}

	// The city grid. layoutKushTemples by map size: Apedemak temples outside (the outer pair
	// from 320 tiles), temples of Amun from 256 tiles, the Ptolemaic wonder in the middle.
	const P pyramids = {0.15f * size - half, 0.75f * size - half};
	out.pyramids[0] = world_x(pyramids);
	out.pyramids[1] = world_z(pyramids);
	out.pyramids_r = sc(5.0f, 14.0f);
	const P grid_centre = round_p(rot_ccw({0.0f, 0.3f * size}, kRiverAngle), half);
	const float hill_r = sc(40.0f, 120.0f);
	struct Temple {
		uint8_t type;
		float path_offset, min_size;
	};
	std::vector<Temple> temples;
	for (const Temple &t : {Temple{BLD_TEMPLE, 9.0f, 320.0f}, Temple{BLD_TEMPLE, 9.0f, 0.0f}, Temple{BLD_TEMPLE_AMUN, 12.0f, 256.0f},
	                        Temple{BLD_WONDER, sc(9.0f, 14.0f), 0.0f}, Temple{BLD_TEMPLE_AMUN, 12.0f, 256.0f}, Temple{BLD_TEMPLE, 9.0f, 320.0f},
	                        Temple{BLD_TEMPLE, 9.0f, 0.0f}})
		if (size >= t.min_size) temples.push_back(t);
	const int gx = (int)temples.size(), gy = (int)std::floor(sc(2.0f, 5.0f));
	const int gxc = gx / 2, gyc = gy / 2;
	const float max_a = std::min(sc(1.0f / 3.0f, 1.0f), 2.0f / 3.0f) * kPi;
	const float start_a = -0.5f * kPi - max_a / 2.0f + kRiverAngle;
	auto grid_r = [&](int y) { return hill_r + 18.0f * (float)y; };
	std::vector<P> g((size_t)(gx * gy));
	out.grid_x = gx;
	out.grid_y = gy;
	for (int y = 0; y < gy; ++y)
		for (int x = 0; x < gx; ++x) {
			const float a = start_a + max_a * (float)x / (float)std::max(1, gx - 1);
			const P p = round_p(polar_p(grid_centre, grid_r(y), a), half);
			g[(size_t)(y * gx + x)] = p;
			out.grid.push_back(world_x(p));
			out.grid.push_back(world_z(p));
			out.grid_angle.push_back(a);
		}
	auto G = [&](int y, int x) { return g[(size_t)(y * gx + x)]; };
	out.path.assign(N, 0);
	out.crossing.assign(N, 0);
	for (const P &p : g) {
		mask_disk(hm, out.path, world_x(p), world_z(p), kPathWidth);
		mask_disk(hm, out.crossing, world_x(p), world_z(p), kPathWidth);
	}
	auto path_line = [&](P a, P b, float width) { mask_capsule(hm, out.path, world_x(a), world_z(a), world_x(b), world_z(b), width / 2.0f); };
	for (int y = 0; y < gy; ++y)
		for (int x = 1; x < gx; ++x) path_line(G(y, x - 1), G(y, x), y == gyc ? kPathWidthSecondary : kPathWidth);
	for (int y = 1; y < gy; ++y)
		for (int x = 0; x < gx; ++x) {
			const int d = std::abs(x - gxc);
			path_line(G(y - 1, x), G(y, x), d == 0 ? kPathWidthCenter : d == 1 ? kPathWidthSecondary : kPathWidth);
		}

	// The temples, pathOffset tiles inward of the inner row, on ground blended towards the
	// desert's height (ElevationBlendingPainter(heightDesert, 0.8)).
	std::vector<uint8_t> temple_mask(N, 0);
	std::vector<P> temple_pos;
	for (int i = 0; i < gx; ++i) {
		const float a = out.grid_angle[(size_t)i];
		const P t = {G(0, i).x - temples[(size_t)i].path_offset * det::cos(a), G(0, i).y - temples[(size_t)i].path_offset * det::sin(a)};
		temple_pos.push_back(t);
		out.temples.push_back(temples[(size_t)i].type);
		out.temple_pos.push_back(world_x(t));
		out.temple_pos.push_back(world_z(t));
		const float fh = footprint_half(temples[(size_t)i].type);
		for (size_t v = 0; v < N; ++v) {
			float x, z;
			seas_world(hm, (int)v, x, z);
			if (std::fabs(x - world_x(t)) <= fh && std::fabs(z - world_z(t)) <= fh) temple_mask[v] = 1;
		}
	}
	for (size_t v = 0; v < N; ++v)
		if (temple_mask[v]) hm.data[v] += 0.8f * (h_desert - hm.data[v]);

	// The ritual place between the wonder, the temple beside it and their paths, sunk like a path.
	{
		const P a = temple_pos[(size_t)(gx / 2 - 1)], b = temple_pos[(size_t)((gx + 1) / 2 - 1)];
		const P c = G(0, gxc), d = G(0, gxc - 1);
		const P r = round_p({(a.x + b.x + c.x + d.x) / 4.0f, (a.y + b.y + c.y + d.y) / 4.0f}, half);
		out.ritual[0] = world_x(r);
		out.ritual[1] = world_z(r);
		out.ritual_angle = (out.grid_angle[(size_t)gxc] + out.grid_angle[(size_t)(gxc - 1)]) / 2.0f + kPi / 2.0f;
		std::vector<uint8_t> m(N, 0);
		mask_disk(hm, m, world_x(r), world_z(r), sc(4.0f, 6.0f));
		for (size_t v = 0; v < N; ++v) m[v] = m[v] && hm.data[v] < h_hill; // avoidClasses(clCliff, 1)
		seas_paint_modify(hm, m, -2.5f * hs * kMetre, 2.0f);
	}
	seas_paint_modify(hm, out.path, -2.5f * hs * kMetre, 1.0f);

	// The districts: each cell of the grid off the paths (ConvexPolygonPlacer), numbered from 1.
	out.city.assign(N, 0);
	for (int y = 1; y < gy; ++y)
		for (int x = 1; x < gx; ++x) {
			const P q[4] = {G(y - 1, x - 1), G(y - 1, x), G(y, x), G(y, x - 1)};
			const uint8_t id = (uint8_t)((y - 1) * (gx - 1) + x);
			for (size_t v = 0; v < N; ++v) {
				if (out.path[v]) continue;
				const P p = vertex_p(v);
				int pos = 0, neg = 0;
				for (int k = 0; k < 4; ++k) {
					const P e0 = q[k], e1 = q[(k + 1) % 4];
					const float cr = (e1.x - e0.x) * (p.y - e0.y) - (e1.y - e0.y) * (p.x - e0.x);
					pos += cr > 0.0f;
					neg += cr < 0.0f;
				}
				if (!pos || !neg) out.city[v] = id;
			}
		}

	// The Napata wall (from 192 tiles: a palisade on Easy, the Kushite wall from Medium): a
	// circular wall in front of the outer row (placeCircularWall, ["tower", "short", "tower",
	// "gate", "tower", "medium", "tower", "short"]) and linear walls from its ends round the back
	// of the temples (placeLinearWall, ["tower", "gate", "tower", "short", "tower", "short",
	// "tower"]) where they keep off the hill and the temples. Each stretch between gates is laid
	// as segments of at most a medium piece, which spawn_gaia_wall joins with towers.
	out.wall = size < 192.0f || out.difficulty < 2 ? 0xFF : out.difficulty < 3 ? (uint8_t)WALLSET_PALISADE : (uint8_t)WALLSET_STONE;
	std::vector<uint8_t> wall_mask(N, 0), gate_mask(N, 0);
	if (out.wall != 0xFF) {
		const WallLengths L = wall_lengths(out.wall);
		const float summand = sc(0.04f, 0.03f) * kPi, w_start = start_a - summand / 2.0f, w_max = max_a + summand;
		const float rf = grid_r(gy - 1) + kPathWidth - 1.0f, rb = hill_r - sc(15.0f, 25.0f);
		auto blocked = [&](P p) {
			const size_t v = (size_t)seas_vertex(hm, world_x(p), world_z(p));
			return hm.data[v] >= h_hill || temple_mask[v];
		};
		auto add_span = [&](P a, P b) {
			if (blocked({(a.x + b.x) / 2.0f, (a.y + b.y) / 2.0f})) return;
			out.wall_spans.insert(out.wall_spans.end(), {world_x(a), world_z(a), world_x(b), world_z(b)});
			mask_capsule(hm, wall_mask, world_x(a), world_z(a), world_x(b), world_z(b), 1.5f);
		};
		auto add_gate = [&](P p, float dir_x, float dir_y) {
			if (blocked(p)) return;
			out.gates.insert(out.gates.end(), {world_x(p), world_z(p), -det::atan2(-dir_y, dir_x)});
			mask_disk(hm, gate_mask, world_x(p), world_z(p), L.gate / 2.0f);
		};
		// The circular front wall, its arc in tiles from w_start.
		{
			const float pattern = 4.0f * L.tower + 3.0f * L.segment + L.gate, total = w_max * rf;
			const int parts = std::max(1, (int)std::ceil((total - L.tower) / pattern));
			const float k = total / ((float)parts * pattern + L.tower);
			auto at = [&](float s) { return polar_p(grid_centre, rf, w_start + s / rf); };
			auto arc_span = [&](float s0, float s1) {
				const int n = std::max(1, (int)std::ceil((s1 - s0) / L.segment));
				for (int j = 0; j < n; ++j) add_span(at(s0 + (s1 - s0) * (float)j / (float)n), at(s0 + (s1 - s0) * (float)(j + 1) / (float)n));
			};
			float s0 = 0.0f;
			for (int p = 0; p < parts; ++p) {
				const float g0 = ((float)p * pattern + 2.0f * L.tower + L.segment) * k, g1 = g0 + L.gate * k;
				arc_span(s0, g0);
				const float a = w_start + (g0 + g1) / 2.0f / rf;
				add_gate(at((g0 + g1) / 2.0f), -det::sin(a), det::cos(a));
				s0 = g1;
			}
			arc_span(s0, total);
		}
		// The side and back walls.
		std::vector<P> line;
		line.push_back(polar_p(grid_centre, rf, w_start));
		for (int x = 0; x < gx; ++x) line.push_back(polar_p(grid_centre, rb, w_start + w_max * (float)x / (float)std::max(1, gx - 1)));
		line.push_back(polar_p(grid_centre, rf, w_start + w_max));
		const float pattern = 4.0f * L.tower + 2.0f * L.segment + L.gate;
		for (size_t i = 1; i < line.size(); ++i) {
			const P a = line[i - 1], b = line[i];
			const float len = det::hypot(b.x - a.x, b.y - a.y);
			if (len < 1e-3f) continue;
			const float dx = (b.x - a.x) / len, dy = (b.y - a.y) / len;
			const int parts = std::max(1, (int)std::ceil(len / pattern));
			const float k = len / ((float)parts * pattern);
			auto at = [&](float s) { return P{a.x + dx * s, a.y + dy * s}; };
			auto line_span = [&](float s0, float s1) {
				if (s1 - s0 < L.tower + 0.5f) return;
				const int n = std::max(1, (int)std::ceil((s1 - s0) / L.segment));
				for (int j = 0; j < n; ++j) add_span(at(s0 + (s1 - s0) * (float)j / (float)n), at(s0 + (s1 - s0) * (float)(j + 1) / (float)n));
			};
			float s0 = 0.0f;
			for (int p = 0; p < parts; ++p) {
				const float g0 = ((float)p * pattern + L.tower) * k, g1 = g0 + L.gate * k;
				line_span(s0, g0);
				add_gate(at((g0 + g1) / 2.0f), dx, dy);
				s0 = g1;
			}
			line_span(s0, len);
		}
		// The ground under the walls raised (avoiding the cliffs), at the gates sunk like a path.
		std::vector<uint8_t> raise(N, 0);
		for (size_t v = 0; v < N; ++v) raise[v] = wall_mask[v] && hm.data[v] < h_hill;
		seas_paint_modify(hm, raise, 2.5f * hs * kMetre, 2.0f);
		const auto to_path = seas_dist_inside(hm, out.path);
		std::vector<uint8_t> sink(N, 0);
		for (size_t g2 = 0; g2 + 2 < out.gates.size(); g2 += 3) {
			std::vector<uint8_t> m(N, 0);
			mask_disk(hm, m, out.gates[g2], out.gates[g2 + 1], kPathWidth);
			for (size_t v = 0; v < N; ++v)
				if (m[v] && !out.path[v] && !out.city[v] && hm.data[v] < h_hill && to_path[v] <= kPathWidth + 1.0f) sink[v] = 1;
		}
		seas_paint_modify(hm, sink, -2.5f * hs * kMetre, 2.0f);
	}

	// Roads from the gates (else from the grid's outer points) out through the desert, at most
	// scaleByMapSize(2, 5), 20 apart and clear of the hill, the city, the pyramids and players.
	std::vector<uint8_t> road(N, 0), pyramid_mask(N, 0);
	mask_disk(hm, pyramid_mask, out.pyramids[0], out.pyramids[1], out.pyramids_r);
	{
		std::vector<std::pair<float, float>> starts;
		if (!out.gates.empty()) {
			for (size_t i = 0; i + 2 < out.gates.size(); i += 3) starts.push_back({out.gates[i], out.gates[i + 1]});
		} else {
			for (int y = 0; y < gy; ++y) starts.push_back({world_x(G(y, 0)), world_z(G(y, 0))});
			for (int y = 0; y < gy; ++y) starts.push_back({world_x(G(y, gx - 1)), world_z(G(y, gx - 1))});
			for (int x = 0; x < gx; ++x) starts.push_back({world_x(G(gy - 1, x)), world_z(G(gy - 1, x))});
		}
		for (size_t i = starts.size(); i > 1; --i) std::swap(starts[i - 1], starts[(size_t)rng.range_int(0, (int)i - 1)]);
		std::vector<uint8_t> hill_or_city(N, 0);
		for (size_t v = 0; v < N; ++v) hill_or_city[v] = hm.data[v] >= h_hill || out.city[v];
		const auto to_pyramid = seas_dist_inside(hm, pyramid_mask), to_hill_city = seas_dist_inside(hm, hill_or_city);
		std::vector<uint8_t> desert_now(N, 0);
		for (size_t v = 0; v < N; ++v) desert_now[v] = desert[v] && hm.data[v] >= water_h;
		const float max_roads = sc(2.0f, 5.0f);
		int roads = 0;
		for (const auto &st : starts) {
			if ((float)roads >= max_roads) break;
			float best = 1e30f, cx = st.first, cz = st.second;
			for (size_t v = 0; v < N; ++v) {
				if (!out.path[v]) continue;
				float x, z;
				seas_world(hm, (int)v, x, z);
				const float d = det::hypot(x - st.first, z - st.second);
				if (d < best) { best = d; cx = x; cz = z; }
			}
			const float dl = std::max(1e-3f, det::hypot(st.first - cx, st.second - cz));
			const float fx = cx + (st.first - cx) / dl * 0.75f * size, fz = cz + (st.second - cz) / dl * 0.75f * size;
			const auto to_road = seas_dist_inside(hm, road);
			for (int tries = 0; tries < 30; ++tries) {
				const auto m = seas_path(hm, rng, fx, fz, st.first, st.second, sc(5.0f, 3.0f), 0.1f, 5.0f, 0.5f, 0.0f, 0.0f, [&](int v) {
					const size_t i = (size_t)v;
					return desert_now[i] && to_hill_city[i] > 0.0f && to_pyramid[i] > 6.0f && to_player[i] > 16.0f && to_road[i] > 20.0f;
				});
				if (!seas_any(m)) continue;
				seas_add(road, m);
				++roads;
				break;
			}
		}
		std::vector<uint8_t> sink(N, 0);
		for (size_t v = 0; v < N; ++v) sink[v] = road[v] && !out.path[v];
		seas_paint_modify(hm, sink, -1.5f * hs * kMetre, 1.0f);
	}

	// createBumps(..., scaleByMapSize(30, 300), 1, 8, 4, 0, 3) away from the city and the river.
	{
		std::vector<uint8_t> hill(N, 0);
		for (size_t v = 0; v < N; ++v) hill[v] = hm.data[v] >= h_hill;
		std::vector<uint8_t> water_now(N, 0);
		for (size_t v = 0; v < N; ++v) water_now[v] = hm.data[v] < water_h;
		std::vector<uint8_t> city_any(N, 0);
		for (size_t v = 0; v < N; ++v) city_any[v] = out.city[v] != 0;
		const auto to_city = seas_dist_inside(hm, city_any), to_water = seas_dist_inside(hm, water_now), to_hill = seas_dist_inside(hm, hill);
		const auto to_path = seas_dist_inside(hm, out.path), to_road = seas_dist_inside(hm, road), to_temple = seas_dist_inside(hm, temple_mask);
		const auto to_pyramid = seas_dist_inside(hm, pyramid_mask), to_wall = seas_dist_inside(hm, wall_mask), to_gate = seas_dist_inside(hm, gate_mask);
		seas_create_areas(hm, rng, sc(30.0f, 300.0f), 10, [&](float x, float z) {
			const auto m = seas_chain(hm, rng, x, z, 1, 8, 4, 0.0f, 0.0f, {}, [&](int v) {
				const size_t i = (size_t)v;
				return to_player[i] > 6.0f && to_city[i] > 0.0f && to_water[i] > 2.0f && to_hill[i] > 0.0f && to_path[i] > 0.0f &&
				       to_road[i] > 0.0f && to_temple[i] > 4.0f && to_pyramid[i] > 8.0f && to_wall[i] > 0.0f && to_gate[i] > 4.0f;
			});
			if (!seas_any(m)) return false;
			seas_paint_modify(hm, m, 3.0f * kMetre, 2.0f);
			return true;
		});
	}

	// The bases, levelled; then the classes the entity pass keeps to.
	finish_spawns(hm, spawns, locs, h_fertile, 18.0f, water_h);
	const auto slope = slopes(hm);
	out.fertile = fertile;
	out.water.assign(N, 0);
	out.desert.assign(N, 0);
	out.hill.assign(N, 0);
	out.hilltop.assign(N, 0);
	out.cliff.assign(N, 0);
	for (size_t v = 0; v < N; ++v) {
		const float h = hm.data[v];
		out.water[v] = h < water_h;
		out.hill[v] = h >= h_hill;
		out.cliff[v] = out.hill[v] && slope[v] >= 2.0f;
		out.hilltop[v] = h >= h_hilltop && slope[v] < 2.0f;
		out.desert[v] = desert[v] && !out.water[v];
		if (out.water[v]) out.fertile[v] = 0;
	}
	// What the forests, mines and animals keep away from (avoidCollisions: the hill, 2 from the
	// paths, 4 from the city, the temples and the walls, 8 from the gates, 10 from the ritual
	// place, 7 from the pyramids and 6 from the roads).
	{
		std::vector<uint8_t> city_any(N, 0), ritual(N, 0);
		for (size_t v = 0; v < N; ++v) city_any[v] = out.city[v] != 0;
		mask_disk(hm, ritual, out.ritual[0], out.ritual[1], sc(4.0f, 6.0f));
		const auto to_path = seas_dist_inside(hm, out.path), to_city = seas_dist_inside(hm, city_any), to_temple = seas_dist_inside(hm, temple_mask);
		const auto to_wall = seas_dist_inside(hm, wall_mask), to_gate = seas_dist_inside(hm, gate_mask), to_ritual = seas_dist_inside(hm, ritual);
		const auto to_pyramid = seas_dist_inside(hm, pyramid_mask), to_road = seas_dist_inside(hm, road);
		out.keep_clear.assign(N, 0);
		for (size_t v = 0; v < N; ++v)
			out.keep_clear[v] = out.hill[v] || to_path[v] <= 2.0f || to_city[v] <= 4.0f || to_temple[v] <= 4.0f || to_wall[v] <= 4.0f ||
			                    to_gate[v] <= 8.0f || to_ritual[v] <= 10.0f || to_pyramid[v] <= 7.0f || to_road[v] <= 6.0f;
	}
}

void RmsGenerator::place_jebel_barkal_entities(Simulation &sim, const HeightmapGrid &hm, float water_h, const std::vector<PlayerSpawn> &spawns,
		RmsRng &rng, const JebelBarkalLayout &lay) {
	// jebel_barkal.js's entities: Napata (the temples and the wonder, the lion path's champion
	// guardians, the ritual place's healers and palms, the districts filled by CityPainter
	// with the Kushite structures of the difficulty, houses, markets and forges uncapturable,
	// and the wall), the pyramids with archers and treasures, the hill's archers and Ptolemaic
	// siege engines, treasures on the hilltop and along the city's paths, and the trigger points
	// jebel_barkal_triggers.js patrols: A at the path crossings, B over the map. The map's
	// generic forests, mines and animals are cleared from the hill and the city (the script's
	// avoidCollisions). Left out: the statues, handcarts, fences and other actors, the hawk,
	// and the stone ring of towers round each base (RTS0's bases have none).
	using namespace jebel;
	(void)spawns;
	const float half = hm.map_half, size = 2.0f * half;
	const size_t N = hm.data.size();
	auto sc = [&](float a, float b) { return seas_scale(a, b, half); };
	auto at = [&](float x, float z) { return (size_t)seas_vertex(hm, x, z); };
	struct Spot {
		float x, z;
	};
	auto clear_of = [](const std::vector<Spot> &v, float x, float z, float d) {
		for (const Spot &s : v)
			if (det::hypot(x - s.x, z - s.z) < d) return false;
		return true;
	};
	auto building = [&](float x, float z, uint8_t type, bool capturable) {
		const uint32_t id = sim.spawn_building_world(x, z, type, GAIA_PLAYER, 1.0f);
		// structures/kush/* and structures/ptol/wonder: a captured camp trains its {native} mercenaries.
		if (id) sim.set_building_civ(id, type == BLD_WONDER ? CIV_PTOLEMIES : CIV_KUSHITES);
		if (id && !capturable) sim.set_structure_uncapturable(id);
		return id;
	};
	auto unit = [&](float x, float z, const char *id) -> uint32_t {
		const uint16_t t = civ_unit_template_by_id(id);
		return t == CIV_UNIT_TEMPLATE_NONE ? 0 : sim.spawn_gaia_unit(x, z, (UnitType)civ_unit_template(t).unit_type, id);
	};
	auto count = [](float amount) { return (int)std::ceil(amount); }; // createObjectGroups places until it reaches amount

	// The generic resources off the hill and out of the city.
	{
		const auto res = sim.resources();
		std::vector<Spot> doomed;
		for (size_t r = 0; r < res.count; ++r)
			if (res.alive[r] && lay.keep_clear[at(res.x[r], res.z[r])]) doomed.push_back({res.x[r], res.z[r]});
		for (const Spot &d : doomed) sim.clear_resources_near(d.x, d.z, 0.01f);
	}

	// Temples and the wonder.
	for (size_t i = 0; i < lay.temples.size(); ++i) building(lay.temple_pos[2 * i], lay.temple_pos[2 * i + 1], lay.temples[i], true);

	// The pyramids: one group of scaleByMapSize(1, 6) to scaleByMapSize(2, 8) large and small
	// pyramids scaleByMapSize(6, 8) to scaleByMapSize(6, 14) round a point of their area.
	std::vector<Spot> pyramids;
	{
		const float ga = rng.range_float(0.0f, 2.0f * kPi), gr = std::sqrt(rng.range_float(0.0f, 1.0f)) * lay.pyramids_r;
		const float gx = lay.pyramids[0] + det::cos(ga) * gr, gz = lay.pyramids[1] + det::sin(ga) * gr;
		const int n = rng.range_int((int)sc(1.0f, 6.0f), (int)sc(2.0f, 8.0f));
		std::vector<float> halves;
		for (int k = 0, tries = 0; k < n && tries < n * 20; ++tries) {
			const uint8_t type = rng.range_int(0, 1) ? BLD_PYRAMID_SMALL : BLD_PYRAMID_LARGE;
			const float d = rng.range_float(sc(6.0f, 8.0f), sc(6.0f, 14.0f)), a = rng.range_float(0.0f, 2.0f * kPi);
			const float x = gx + det::cos(a) * d, z = gz + det::sin(a) * d, h = footprint_half(type);
			bool ok = x * x + z * z < (half - h - 2.0f) * (half - h - 2.0f) && !lay.water[at(x, z)];
			for (size_t j = 0; j < pyramids.size() && ok; ++j)
				ok = det::hypot(x - pyramids[j].x, z - pyramids[j].z) >= std::max(sc(6.0f, 8.0f), h + halves[j] + 1.0f);
			if (!ok) continue;
			if (!building(x, z, type, true)) continue;
			pyramids.push_back({x, z});
			halves.push_back(h);
			++k;
		}
	}

	// The ritual place: healers on a half circle round the statue, palms behind them.
	{
		const float rx = lay.ritual[0], rz = lay.ritual[1];
		auto half_circle = [&](int n, float r, const auto &put) {
			for (int k = 0; k < n; ++k) {
				const float a = lay.ritual_angle + kPi * (float)k / (float)std::max(1, n - 1);
				put(rx + r * det::cos(a), rz - r * det::sin(a));
			}
		};
		half_circle((int)std::lround(sc(2.0f, 10.0f)), sc(2.0f, 3.0f), [&](float x, float z) { unit(x, z, "kush/support_healer_b"); });
		std::vector<Spot> temples;
		for (size_t i = 0; i < lay.temples.size(); ++i) temples.push_back({lay.temple_pos[2 * i], lay.temple_pos[2 * i + 1]});
		half_circle((int)std::lround(sc(6.0f, 16.0f)), sc(4.0f, 5.0f), [&](float x, float z) {
			for (size_t i = 0; i < temples.size(); ++i)
				if (std::fabs(x - temples[i].x) <= footprint_half(lay.temples[i]) + 1.0f &&
						std::fabs(z - temples[i].z) <= footprint_half(lay.temples[i]) + 1.0f)
					return;
			sim.spawn_resource_world(x, z, RES_WOOD, TREE_WOOD_SUPPLY);
		});
	}

	// The guardians: scaleByMapSize(2, 40) Kushite champions in two files along the central
	// path, clear of its crossings.
	const int gx = lay.grid_x, gy = lay.grid_y, gxc = gx / 2;
	auto grid = [&](int y, int x) { return from_world(lay.grid[(size_t)(2 * (y * gx + x))], lay.grid[(size_t)(2 * (y * gx + x) + 1)]); };
	std::vector<Spot> crossings;
	for (int y = 0; y < gy; ++y)
		for (int x = 0; x < gx; ++x) crossings.push_back({world_x(grid(y, x)), world_z(grid(y, x))});
	std::vector<Spot> guardians;
	{
		static const char *const kChampions[] = {"kush/champion_infantry_archer", "kush/champion_infantry_amun", "kush/champion_infantry_apedemak"};
		const P start = grid(0, gxc), end = grid(gy - 1, gxc);
		const float len = det::hypot(end.x - start.x, end.y - start.y), a = lay.grid_angle[(size_t)gxc];
		const float n = sc(2.0f, 40.0f);
		for (int side = 0; side < 2; ++side)
			for (int k = 0; (float)k < n; ++k) {
				// Vector2D(0, pathWidthCenter / 2 * (i - 0.5)).rotate(-angle) beside the path.
				const float off = kPathWidthCenter / 2.0f * ((float)side - 0.5f);
				const P p = {start.x - off * det::sin(a) + len * (float)k / n * det::cos(a) + 0.5f,
				             start.y + off * det::cos(a) + len * (float)k / n * det::sin(a) + 0.5f};
				const float x = world_x(p), z = world_z(p);
				if (!clear_of(crossings, x, z, kPathWidth + 2.0f)) continue;
				unit(x, z, kChampions[rng.range_int(0, 2)]);
				guardians.push_back({x, z});
			}
	}

	// Trigger points A on the path crossings, 8 apart.
	{
		std::vector<Spot> order = crossings, placed;
		for (size_t i = order.size(); i > 1; --i) std::swap(order[i - 1], order[(size_t)rng.range_int(0, (int)i - 1)]);
		const int amount = count(sc(20.0f, 100.0f));
		for (const Spot &c : order) {
			if ((int)placed.size() >= amount) break;
			for (int tries = 0; tries < 30; ++tries) {
				const float a = rng.range_float(0.0f, 2.0f * kPi), r = rng.range_float(0.0f, kPathWidth - 2.0f);
				const float x = c.x + det::cos(a) * r, z = c.z + det::sin(a) * r;
				if (!clear_of(placed, x, z, 8.0f)) continue;
				sim.add_trigger_point('A', x, z);
				placed.push_back({x, z});
				break;
			}
		}
	}

	// The districts (CityPainter): through each district in rows along its angle, at every free
	// spot the first of the shuffled structures that fits and keeps its distance from its kind.
	{
		const uint8_t tower = size >= 256.0f && lay.difficulty >= 3 ? BLD_TOWER : BLD_SENTRY_TOWER;
		struct CityBuilding {
			uint8_t type;
			int difficulty;
			float avoid;
			bool near_path, capturable;
		};
		const CityBuilding all[] = {
			{BLD_HOUSE, 1, 0.0f, false, false},          {BLD_FORTRESS, 3, 25.0f, true, true},        {BLD_TOWN_CENTER, 2, 60.0f, true, true},
			{BLD_ELEPHANT_STABLE, 2, 10.0f, false, true}, {BLD_STABLE, 2, 20.0f, false, true},          {BLD_BARRACKS, 2, 12.0f, false, true},
			{tower, 2, 17.0f, false, true},               {BLD_MARKET, 1, 15.0f, false, false},          {BLD_FORGE, 1, 30.0f, false, false},
			{BLD_CAMP_NOBA, 2, 30.0f, false, true},       {BLD_CAMP_BLEMMYE, 2, 30.0f, false, true},
		};
		std::vector<CityBuilding> layout;
		for (const CityBuilding &b : all)
			if (lay.difficulty >= b.difficulty) layout.push_back(b);
		std::vector<std::vector<Spot>> placed(layout.size());
		const auto to_path = seas_dist_inside(hm, lay.path);
		std::vector<uint8_t> taken(N, 0);
		const int districts = (gx - 1) * (gy - 1);
		for (int d = 1; d <= districts; ++d) {
			const int y = (d - 1) / (gx - 1) + 1, x = (d - 1) % (gx - 1) + 1;
			const float angle = (-lay.grid_angle[(size_t)(y * gx + x - 1)] - lay.grid_angle[(size_t)(y * gx + x)]) / 2.0f;
			std::vector<std::pair<std::pair<float, float>, size_t>> spots;
			for (size_t v = 0; v < N; ++v) {
				if (lay.city[v] != d) continue;
				float wx, wz;
				seas_world(hm, (int)v, wx, wz);
				const P r = rot_ccw(from_world(wx, wz), angle);
				spots.push_back({{r.x, r.y}, v});
			}
			std::sort(spots.begin(), spots.end());
			for (const auto &s : spots) {
				const size_t v = s.second;
				if (taken[v]) continue;
				float sx, sz;
				seas_world(hm, (int)v, sx, sz);
				std::vector<size_t> order(layout.size());
				for (size_t i = 0; i < order.size(); ++i) order[i] = i;
				for (size_t i = order.size(); i > 1; --i) std::swap(order[i - 1], order[(size_t)rng.range_int(0, (int)i - 1)]);
				for (size_t k : order) {
					const CityBuilding &b = layout[k];
					if (b.near_path && to_path[v] > 8.0f) continue;
					// CityPainter puts a structure's corner on the point, its obstruction running on
					// along the row.
					const float h = footprint_half(b.type);
					const P off = rot_ccw(P{h, h}, -angle);
					const float cx = sx + off.x, cz = sz - off.y;
					if (!clear_of(placed[k], cx, cz, b.avoid + h)) continue;
					// The footprint within the district, on free, dry, level ground.
					const int x0 = std::max(0, (int)std::ceil((cx - h + half) / grid_spacing(hm)));
					const int x1 = std::min(hm.res - 1, (int)std::floor((cx + h + half) / grid_spacing(hm)));
					const int z0 = std::max(0, (int)std::ceil((cz - h + half) / grid_spacing(hm)));
					const int z1 = std::min(hm.res - 1, (int)std::floor((cz + h + half) / grid_spacing(hm)));
					bool fits = true;
					for (int gz2 = z0; gz2 <= z1 && fits; ++gz2)
						for (int gx2 = x0; gx2 <= x1 && fits; ++gx2) {
							const size_t u = (size_t)gz2 * (size_t)hm.res + (size_t)gx2;
							fits = lay.city[u] == d && !taken[u] && !lay.water[u] && !lay.cliff[u];
						}
					if (!fits || !building(cx, cz, b.type, b.capturable)) continue;
					for (int gz2 = z0; gz2 <= z1; ++gz2)
						for (int gx2 = x0; gx2 <= x1; ++gx2) taken[(size_t)gz2 * (size_t)hm.res + (size_t)gx2] = 1;
					placed[k].push_back({cx, cz});
					break;
				}
			}
		}
	}

	// The Napata wall and its gates.
	std::vector<Spot> walls;
	if (lay.wall != 0xFF) {
		for (size_t i = 0; i + 3 < lay.wall_spans.size(); i += 4) {
			sim.spawn_gaia_wall(lay.wall_spans[i], lay.wall_spans[i + 1], lay.wall_spans[i + 2], lay.wall_spans[i + 3], lay.wall);
			walls.push_back({(lay.wall_spans[i] + lay.wall_spans[i + 2]) / 2.0f, (lay.wall_spans[i + 1] + lay.wall_spans[i + 3]) / 2.0f});
		}
		for (size_t i = 0; i + 2 < lay.gates.size(); i += 3) {
			sim.spawn_gaia_gate(lay.gates[i], lay.gates[i + 1], lay.gates[i + 2], lay.wall);
			walls.push_back({lay.gates[i], lay.gates[i + 1]});
		}
	}

	// The hill's defenders, scaled by difficulty: groups of Kushite archers on the hilltop
	// and single ones on the upper slope, all near the cliffs, and Ptolemaic siege engines.
	const auto to_cliff = seas_dist_inside(hm, lay.cliff);
	std::vector<Spot> soldiers;
	static const char *const kArchers[] = {"kush/infantry_archer_b", "kush/champion_infantry_archer"};
	auto pick_cells = [&](const auto &ok) {
		std::vector<size_t> cells;
		for (size_t v = 0; v < N; ++v)
			if (ok(v)) cells.push_back(v);
		return cells;
	};
	auto groups_on = [&](const std::vector<size_t> &cells, int amount, int retry, float spacing, const auto &put) {
		for (int placed = 0, tries = 0; !cells.empty() && placed < amount && tries < amount * retry; ++tries) {
			float x, z;
			seas_world(hm, (int)cells[(size_t)rng.range_int(0, (int)cells.size() - 1)], x, z);
			if (!clear_of(soldiers, x, z, spacing)) continue;
			put(x, z);
			++placed;
		}
	};
	const float diff = (float)lay.difficulty / 3.0f;
	const auto hilltop = pick_cells([&](size_t v) { return lay.hilltop[v] && to_cliff[v] > 1.0f && to_cliff[v] <= 5.0f; });
	groups_on(hilltop, count(sc(1.0f, 5.0f) * diff), 250, 0.0f, [&](float x, float z) {
		const int n = rng.range_int((int)sc(4.0f, 10.0f), (int)sc(6.0f, 20.0f));
		for (int k = 0; k < n; ++k) {
			const float a = rng.range_float(0.0f, 2.0f * kPi), r = rng.range_float(1.0f, 4.0f);
			const float ux = x + det::cos(a) * r, uz = z + det::sin(a) * r;
			if (!lay.hill[at(ux, uz)] || lay.cliff[at(ux, uz)]) continue;
			unit(ux, uz, kArchers[rng.range_int(0, 1)]);
			soldiers.push_back({ux, uz});
		}
	});
	const auto slope_cells = pick_cells([&](size_t v) {
		return lay.hill[v] && hm.data[v] >= lay.height_archers && hm.data[v] <= lay.height_hilltop && to_cliff[v] > 1.0f && to_cliff[v] <= 5.0f;
	});
	groups_on(slope_cells, count(sc(8.0f, 100.0f) * diff), 250, 1.0f, [&](float x, float z) {
		unit(x, z, kArchers[rng.range_int(0, 1)]);
		soldiers.push_back({x, z});
	});
	// ptol/siege_lithobolos_unpacked and polybolos_unpacked: their packed rows, standing unpacked.
	groups_on(hilltop, count(sc(1.0f, 6.0f) * diff), 250, 1.0f, [&](float x, float z) {
		sim.set_unit_unpacked(unit(x, z, rng.range_int(0, 1) ? "ptol/siege_polybolos_packed" : "ptol/siege_lithobolos_packed"));
		soldiers.push_back({x, z});
	});

	// Archers and treasures by the pyramids, 7 to 10 from them.
	std::vector<Spot> treasures;
	static const uint8_t kHillTreasures[] = {TREASURE_WOOD, TREASURE_STONE, TREASURE_METAL};
	static const uint8_t kCityTreasures[] = {TREASURE_FOOD_BIN, TREASURE_FOOD_CRATE, TREASURE_FOOD_JARS, TREASURE_WOOD, TREASURE_STONE, TREASURE_METAL};
	if (!pyramids.empty()) {
		const auto by_pyramids = pick_cells([&](size_t v) {
			float x, z;
			seas_world(hm, (int)v, x, z);
			const bool near = !clear_of(pyramids, x, z, 10.0f + footprint_half(BLD_PYRAMID_LARGE));
			return near && clear_of(pyramids, x, z, 7.0f + footprint_half(BLD_PYRAMID_SMALL)) && !lay.water[v] && !lay.hill[v];
		});
		groups_on(by_pyramids, count(sc(3.0f, 8.0f)), 250, 1.0f, [&](float x, float z) {
			unit(x, z, "kush/infantry_archer_b");
			soldiers.push_back({x, z});
		});
		for (int k = 0, tries = 0, n = count(sc(1.0f, 10.0f)); !by_pyramids.empty() && k < n && tries < n * 250; ++tries) {
			float x, z;
			seas_world(hm, (int)by_pyramids[(size_t)rng.range_int(0, (int)by_pyramids.size() - 1)], x, z);
			if (!clear_of(treasures, x, z, 1.0f) || !clear_of(soldiers, x, z, 1.0f)) continue;
			sim.spawn_treasure(x, z, kHillTreasures[rng.range_int(0, 2)]);
			treasures.push_back({x, z});
			++k;
		}
	}
	// Treasures on the hilltop and along the city's paths.
	{
		const auto top = pick_cells([&](size_t v) { return lay.hilltop[v] && to_cliff[v] > 1.0f; });
		for (int k = 0, tries = 0, n = count(sc(8.0f, 35.0f)); !top.empty() && k < n && tries < n * 250; ++tries) {
			float x, z;
			seas_world(hm, (int)top[(size_t)rng.range_int(0, (int)top.size() - 1)], x, z);
			if (!clear_of(treasures, x, z, 1.0f)) continue;
			sim.spawn_treasure(x, z, kHillTreasures[rng.range_int(0, 2)]);
			treasures.push_back({x, z});
			++k;
		}
		std::vector<uint8_t> city_any(N, 0);
		for (size_t v = 0; v < N; ++v) city_any[v] = lay.city[v] != 0;
		const auto to_city = seas_dist_inside(hm, city_any);
		const auto by_city = pick_cells([&](size_t v) { return lay.path[v] && to_city[v] <= 1.0f; });
		for (int k = 0, tries = 0, n = count(sc(2.0f, 60.0f)); !by_city.empty() && k < n && tries < n * 500; ++tries) {
			float x, z;
			seas_world(hm, (int)by_city[(size_t)rng.range_int(0, (int)by_city.size() - 1)], x, z);
			if (!clear_of(treasures, x, z, 2.0f) || !clear_of(guardians, x, z, 4.0f) || !clear_of(walls, x, z, 2.0f)) continue;
			sim.spawn_treasure(x, z, kCityTreasures[rng.range_int(0, 5)]);
			treasures.push_back({x, z});
			++k;
		}
	}

	// The hunt (jebel_barkal.js "Creating rhinos" to "Creating crocodiles"): scaleByMapSize(2, 10)
	// groups each (crocodiles scaleByMapSize(1, 6)), retry factor 50, under avoidCollisions
	// (keep_clear, the cliffs, clPlayer 15, clWater 1, clForest 1, clRock 4, clMetal 4, clFood 6;
	// clPlayer is each base's disk of defaultPlayerBaseRadius), centred in the desert or the
	// fertile land where the script passes that area. Lions keep 20 from the bases; crocodiles
	// stay within 3 of water. No lions or crocodiles for Nomad.
	{
		RmsRng hunt(rng.next_u32());
		const float base_r = sc(15.0f, 25.0f);
		const auto to_water = seas_dist_inside(hm, lay.water), to_cliff = seas_dist_inside(hm, lay.cliff);
		std::vector<std::pair<float, float>> food, wood, mines;
		const auto res = sim.resources();
		for (size_t i = 0; i < res.count; ++i) {
			if (!res.alive[i]) continue;
			if (res.type[i] == RES_FOOD && res.subtype[i] != RES_SUB_FISH) food.push_back({res.x[i], res.z[i]});
			else if (res.type[i] == RES_WOOD) wood.push_back({res.x[i], res.z[i]});
			else if (res.type[i] == RES_GOLD || res.type[i] == RES_STONE) mines.push_back({res.x[i], res.z[i]});
		}
		const auto fv = sim.fauna();
		for (size_t i = 0; i < fv.count; ++i)
			if (fv.alive[i]) food.push_back({fv.pos_x[i], fv.pos_z[i]});
		auto near_any = [](const std::vector<std::pair<float, float>> &v, float x, float z, float d) {
			for (const auto &[px, pz] : v)
				if ((px - x) * (px - x) + (pz - z) * (pz - z) <= d * d) return true;
			return false;
		};
		auto far_from_bases = [&](float x, float z, float d) {
			for (const auto &sp : spawns)
				if (det::hypot(x - sp.x, z - sp.z) < base_r + d) return false;
			return true;
		};
		auto collisions = [&](float x, float z) {
			if (x * x + z * z >= (half - 3.0f) * (half - 3.0f)) return false; // validTilePassable on the circle
			const size_t v = at(x, z);
			if (lay.keep_clear[v] || to_cliff[v] <= 0.5f || to_water[v] <= 1.0f || !far_from_bases(x, z, 15.0f)) return false;
			return !near_any(wood, x, z, 1.5f) && !near_any(mines, x, z, 4.0f) && !near_any(food, x, z, 6.0f);
		};
		// createObjectGroups centres on g_Map.randomCoordinate(true); createObjectGroupsByAreas
		// on a random point of the area.
		auto anywhere = [&](float &x, float &z) {
			const auto p = seas_random_point(hm, hunt);
			x = std::floor(p.first);
			z = std::floor(p.second);
		};
		auto in_area = [&](const std::vector<uint8_t> &area) {
			return [&anywhere, &at, a = &area](float &x, float &z) {
				for (int k = 0; k < 4096; ++k) {
					anywhere(x, z);
					if ((*a)[at(x + 0.5f, z + 0.5f)]) return;
				}
			};
		};
		const float n = sc(2.0f, 10.0f);
		place_animal_groups(sim, hunt, {{FAUNA_RHINOCEROS_WHITE, 1, 1, 0, 1}}, n, 50, in_area(lay.desert), collisions, food);
		place_animal_groups(sim, hunt, {{FAUNA_BOAR, 1, 1, 0, 1}}, n, 50, in_area(lay.fertile), collisions, food); // oWarthog
		place_animal_groups(sim, hunt, {{FAUNA_GIRAFFE, 2, 3, 2, 4}, {FAUNA_GIRAFFE_INFANT, 2, 3, 2, 4}}, n, 50, anywhere, collisions, food);
		// createObjectGroups (not ByAreas): its [areaDesert] lands in the behaveDeprecated slot.
		place_animal_groups(sim, hunt, {{FAUNA_GAZELLE, 5, 7, 2, 4}}, n, 50, anywhere, collisions, food, true);
		if (!sim.nomad())
			place_animal_groups(sim, hunt, {{FAUNA_LION, 1, 2, 2, 4}, {FAUNA_LIONESS, 2, 3, 2, 4}}, n, 50, in_area(lay.desert),
					[&](float x, float z) { return collisions(x, z) && far_from_bases(x, z, 20.0f); }, food);
		place_animal_groups(sim, hunt, {{FAUNA_ELEPHANT_AFRICAN_BUSH, 2, 3, 2, 4}, {FAUNA_ELEPHANT_AFRICAN_INFANT, 2, 3, 2, 4}}, n, 50,
				in_area(lay.desert), collisions, food);
		if (!sim.nomad())
			place_animal_groups(sim, hunt, {{FAUNA_CROCODILE_NILE, 2, 3, 3, 5}}, sc(1.0f, 6.0f), 50, in_area(lay.fertile),
					[&](float x, float z) { return collisions(x, z) && to_water[at(x, z)] <= 3.0f; }, food);
	}

	// Trigger points B where the attackers patrol: away from the city, the hill, water, walls
	// and mines, 15 apart.
	{
		std::vector<uint8_t> city_any(N, 0);
		for (size_t v = 0; v < N; ++v) city_any[v] = lay.city[v] != 0;
		const auto to_city = seas_dist_inside(hm, city_any), to_hill = seas_dist_inside(hm, lay.hill);
		const auto res = sim.resources();
		auto near_resource = [&](float x, float z) {
			for (size_t r = 0; r < res.count; ++r) {
				if (!res.alive[r] || res.subtype[r] == RES_SUB_FISH) continue;
				const float d = res.type[r] == RES_WOOD ? 1.0f : 4.0f;
				if (std::fabs(res.x[r] - x) < d && std::fabs(res.z[r] - z) < d && det::hypot(res.x[r] - x, res.z[r] - z) < d) return true;
			}
			return false;
		};
		std::vector<Spot> placed;
		const int amount = count(sc(20.0f, 100.0f));
		for (int tries = 0; (int)placed.size() < amount && tries < amount * 30; ++tries) {
			const auto p = seas_random_point(hm, rng);
			const size_t v = at(p.first, p.second);
			if (to_city[v] <= 8.0f || to_hill[v] <= 4.0f || lay.water[v] || !clear_of(walls, p.first, p.second, 2.0f + 3.0f) ||
					!clear_of(placed, p.first, p.second, 15.0f) || near_resource(p.first, p.second))
				continue;
			sim.add_trigger_point('B', p.first, p.second);
			placed.push_back({p.first, p.second});
		}
	}
	(void)water_h;
}

void RmsGenerator::synth_island_stronghold(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players,
		const uint8_t *teams) {
	// island_stronghold.js: sea at -10 m; each team (getTeamsArray, players
	// without a team alone) on a circle at f = 0.3, its members packed round
	// the team's spot (f = 0.05; at least 18 apart in RTS0) each on an island
	// (ChainPlacer(2, scaleByMapSize(5, 11), scaleByMapSize(60, 250)) from a
	// 3/4 base radius disk) at 3 m; scaleByMapSize(4, 14) big and
	// scaleByMapSize(6, 55) small islands kept 3 from land and bases;
	// smoothing; bumps; scaleByMapSize(4, 13) hills of 18 m on land away from
	// the bases; more smoothing. Teams meet by ship. RTS0 raises the land
	// within 22 of each base so its starting resources stay dry.
	using namespace misc_maps;
	water_h = 1.0f;
	const float half = hm.map_half, size = 2.0f * half;
	const float land = metres(water_h, 3.0f), hill = metres(water_h, 18.0f);
	hm.init(hm.res, hm.map_half, metres(water_h, -10.0f));
	const size_t N = hm.data.size();
	const float start = rng.range_float(0.0f, kTau);
	// getTeamsArray: teams in team order, then each player without a team.
	const auto ids = sort_all_players(rng, num_players, teams);
	std::vector<std::vector<int>> groups;
	std::vector<int> team_group(256, -1);
	for (int id : ids) {
		const int t = teams ? teams[id] : 0;
		if (t == 0) continue;
		if (team_group[t] < 0) { team_group[t] = (int)groups.size(); groups.push_back({}); }
		groups[(size_t)team_group[t]].push_back(id);
	}
	for (int id : ids)
		if (!teams || teams[id] == 0) groups.push_back({id});
	const int num_teams = std::max(1, (int)groups.size());
	const float base_r = (float)scale_by_map_size(15, 25, half);
	std::vector<std::pair<float, float>> locs((size_t)num_players);
	std::vector<uint8_t> cl_land(N, 0);
	for (int g = 0; g < (int)groups.size(); ++g) {
		const auto tp = polar(start + kTau * (float)g / (float)num_teams, 0.3f * size);
		const int k = (int)groups[(size_t)g].size();
		const float r = k == 1 ? 0.05f * size : std::max(0.05f * size, 9.0f / det::sin(3.14159265f / (float)k));
		for (int m = 0; m < k; ++m) {
			const auto p = polar(start + kTau / (float)k + kTau * (float)m / (float)k, r);
			const float lim = half - 16.0f;
			const std::pair<float, float> pos{std::clamp(std::round(tp.first + p.first), -lim, lim), std::clamp(std::round(tp.second + p.second), -lim, lim)};
			locs[(size_t)groups[(size_t)g][(size_t)m]] = pos;
			const auto mask = chain_placer(hm, rng, pos.first, pos.second, 2.0f, std::floor(scale_f(5.0f, 11.0f, half)),
			                               (int)std::floor(scale_f(60.0f, 250.0f, half)), base_r * 0.75f);
			paint_mask_elevation(hm, mask, land, 2.0f);
			for (size_t i = 0; i < N; ++i) cl_land[i] |= mask[i];
		}
	}
	const auto to_player = dist_to_points(hm, locs);
	auto random_centre = [&]() { const float b_ = std::sqrt(rng.next_float()) * half; const float a_ = rng.range_float(0.0f, kTau); return polar(a_, b_); };
	auto islands = [&](float min_r, float max_r, int circles, float max_dist, int count) {
		auto to_land = distance_to_mask(cl_land, hm.res, grid_spacing(hm));
		create_areas(hm, count, count, 0.07f, random_centre,
			[&](float cx, float cz) {
				auto m = chain_placer(hm, rng, cx, cz, min_r, max_r, circles);
				for (int z = 0; z < hm.res; ++z)
					for (int x = 0; x < hm.res; ++x) {
						float wx, wz;
						hm.grid_to_world(x, z, wx, wz);
						if (det::hypot(wx - cx, wz - cz) > max_dist) m[(size_t)z * (size_t)hm.res + (size_t)x] = 0;
					}
				return m;
			},
			[&](size_t i) { return to_land[i] > 3.0f && to_player[i] > 5.0f + 3.0f; },
			[&](const std::vector<uint8_t> &m) {
				paint_mask_elevation(hm, m, land, 6.0f);
				for (size_t i = 0; i < N; ++i) cl_land[i] |= m[i];
				to_land = distance_to_mask(cl_land, hm.res, grid_spacing(hm));
			});
	};
	islands(std::floor(scale_f(4.0f, 8.0f, half)), std::floor(scale_f(8.0f, 16.0f, half)), (int)std::floor(scale_f(25.0f, 60.0f, half)),
	        scale_f(30.0f, 70.0f, half), scale_by_map_size(4, 14, half));
	islands(std::floor(scale_f(4.0f, 7.0f, half)), std::floor(scale_f(7.0f, 10.0f, half)), (int)std::floor(scale_f(16.0f, 40.0f, half)),
	        scale_f(22.0f, 40.0f, half), scale_by_map_size(6, 55, half));
	for (int i = 0; i < 5; ++i) shepard_smooth(hm, 0.8f);
	std::vector<PlayerSpawn> probe;
	for (const auto &l : locs) probe.push_back({0, l.first, l.second, land, 0.0f, 0.0f});
	dry_bumps(hm, rng, probe, scale_by_map_size(100, 200, half), 5.0f + 20.0f, water_h + 0.5f);
	// Hills on land (0-5 m), 20 from the base resources and 15 from each other.
	{
		std::vector<uint8_t> cl_hill(N, 0);
		auto to_hill = distance_to_mask(cl_hill, hm.res, grid_spacing(hm));
		const float lo = metres(water_h, 0.0f), hi = metres(water_h, 5.0f);
		const int count = scale_by_map_size(4, 13, half);
		create_areas(hm, count, count * 10, 0.5f, random_centre,
			[&](float cx, float cz) {
				return chain_placer(hm, rng, cx, cz, 1.0f, std::floor(scale_f(4.0f, 6.0f, half)), (int)std::floor(scale_f(16.0f, 40.0f, half)));
			},
			[&](size_t i) { return to_player[i] > 23.0f + 20.0f && to_hill[i] > 15.0f && hm.data[i] >= lo && hm.data[i] <= hi; },
			[&](const std::vector<uint8_t> &m) {
				paint_mask_elevation(hm, m, hill, 2.0f);
				for (size_t i = 0; i < N; ++i) cl_hill[i] |= m[i];
				to_hill = distance_to_mask(cl_hill, hm.res, grid_spacing(hm));
			});
	}
	for (int i = 0; i < 3; ++i) shepard_smooth(hm, 0.8f);
	finish_spawns(hm, spawns, locs, land, 22.0f, water_h);
}

void RmsGenerator::synth_latium(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players,
		const uint8_t *teams) {
	// latium.js (a square map): the sea along two opposite edges (paintRiver
	// 0.2 of the map wide centred on each edge, bed -16 m, fade 16), land
	// rising to a 12 m ridge in the middle, plus Noise2D base noise (16 + 8 +
	// 4 m, its dips damped near players and the sea), finer noise on land and
	// cliff noise that lifts up to 19 m plateaus away from the players.
	// Players on two staggered lines across the map (playerPlacementLine,
	// 0.42-0.46 of the map apart, primeSortAllPlayers), each city patch
	// (radius 11) set to 5 m.
	using namespace misc_maps;
	water_h = 1.0f;
	const float half = hm.map_half, size = 2.0f * half;
	const float water_w = 0.1f;
	hm.init(hm.res, hm.map_half, metres(water_h, 0.0f));
	const bool along_z = rng.range_int(0, 1) == 1; // startAngle = pi / 2: the seas lie along the z edges
	const float start = along_z ? 1.5707963f : 0.0f;
	// playerPlacementLine(startAngle + pi / 2, centre, width).
	const float width = rng.range_float(0.42f, 0.46f) * size;
	const auto ids = prime_sort(sort_all_players(rng, num_players, teams));
	std::vector<std::pair<float, float>> locs((size_t)num_players);
	for (int i = 0; i < num_players; ++i)
		locs[(size_t)ids[(size_t)i]] = rot(((float)(i + 1) / (float)(num_players + 1) - 0.5f) * size, width * ((float)(i % 2) - 0.5f), start + 1.5707963f);
	auto nearness = [&](float wx, float wz) {
		float d = 1e9f;
		for (const auto &l : locs) d = std::min(d, det::hypot(wx - l.first, wz - l.second));
		return d < 13.0f ? 0.0f : (d < 19.0f ? (d - 13.0f) / 6.0f : 1.0f);
	};
	const Noise2D n0(rng, scale_f(4, 16, half)), n1(rng, scale_f(8, 32, half)), n2(rng, scale_f(15, 60, half));
	const Noise2D n2a(rng, scale_f(20, 80, half)), n2b(rng, scale_f(35, 140, half));
	const Noise2D n3(rng, scale_f(4, 16, half)), n4(rng, scale_f(6, 24, half)), n5(rng, scale_f(11, 44, half));
	const float bed = -16.0f, hill = 12.0f, fade = 16.0f;
	for (int z = 0; z < hm.res; ++z)
		for (int x = 0; x < hm.res; ++x) {
			float wx, wz;
			hm.grid_to_world(x, z, wx, wz);
			const float fx = (wx + half) / (size + 1.0f), fz = (wz + half) / (size + 1.0f);
			const float pn = nearness(wx, wz);
			const float c = along_z ? fz : fx;
			// The sea: paintRiver's riverbed profile (metres), shore at f = 0.1.
			const float to_edge = std::min(c, 1.0f - c) * size, s1 = to_edge - water_w * size;
			const bool in_water = s1 < 0.0f;
			const float dist_water = in_water ? 0.0f : (0.5f - water_w - std::fabs(c - 0.5f));
			float h = in_water ? bed + (0.0f - bed) * std::max(0.0f, 1.0f + s1 / fade) : hill * (1.0f - std::fabs(c - 0.5f) / (0.5f - water_w));
			float base_noise = 16.0f * n0.get(fx, fz) + 8.0f * n1.get(fx, fz) + 4.0f * n2.get(fx, fz) - (16.0f + 8.0f + 4.0f) / 2.0f;
			if (base_noise < 0.0f) {
				base_noise *= pn;
				base_noise *= std::max(0.1f, dist_water / (0.5f - water_w));
			}
			const float old_h = h;
			h += base_noise;
			if (old_h > 0.0f) h += (0.4f * n2a.get(fx, fz) + 0.2f * n2b.get(fx, fz)) * std::min(old_h / 10.0f, 1.0f);
			if (h > -10.0f) {
				float cn = (n3.get(fx, fz) + 0.5f * n4.get(fx, fz)) / 1.5f;
				if (h < 1.0f) cn *= 1.0f - 0.3f * ((h - 1.0f) / -10.0f);
				cn += 0.05f * dist_water / (0.5f - water_w);
				if (cn > 0.6f) {
					const float u = 0.8f * (cn - 0.6f);
					cn += u * n5.get(fx, fz);
					cn /= 1.0f + u;
				}
				cn -= 0.59f;
				cn *= pn;
				if (cn > 0.0f) h += 19.0f * std::min(cn, 0.045f) / 0.045f;
			}
			hm.set(x, z, metres(water_h, h));
		}
	// City patches: radius 11 at heightPlayer 5 (blend 2).
	for (const auto &l : locs) hm.level_area(l.first, l.second, 11.0f, 2.0f, metres(water_h, 5.0f));
	finish_spawns(hm, spawns, locs, metres(water_h, 5.0f), 16.0f, water_h);
}

void RmsGenerator::synth_unknown(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players,
		const uint8_t *teams, uint8_t landscape_choice) {
	// unknown.js: one of its 13 landscapes at random (Archipelago, Continent,
	// CentralSea, Isthmus, CentralRiverLand, CentralRiverNaval,
	// RiversAndLake, EdgeSeas, Gulf, Lakes, Passes, Lowlands, Mainland) on a
	// -5 m sea floor with land at 3 m, then bumps and 18 m hills on the flat
	// land away from the bases. Several landscapes split the players by
	// water (ships needed). RTS0 sets the script's -2 m shallows at wading
	// depth and raises the land within 18 of each base so its starting
	// resources stay dry; the starting walls and treasures are left out.
	using namespace misc_maps;
	water_h = 1.0f;
	const float half = hm.map_half, size = 2.0f * half;
	const float land = metres(water_h, 3.0f), wade = wade_height(water_h);
	hm.init(hm.res, hm.map_half, metres(water_h, -5.0f));
	const size_t N = hm.data.size();
	const float sp = grid_spacing(hm);
	const int n = num_players;
	std::vector<uint8_t> cl_land(N, 0), cl_peninsula(N, 0), cl_water(N, 0);
	std::vector<int> ids = sort_all_players(rng, n, teams);
	std::vector<std::pair<float, float>> slots; // start spots, slot k for player ids[k]
	bool grouped = false;                        // slots already indexed by player
	float territory = 0.0f;                      // markPlayerArea("large") radius
	auto random_centre = [&]() { const float b_ = std::sqrt(rng.next_float()) * half; const float a_ = rng.range_float(0.0f, kTau); return polar(a_, b_); };
	auto paint_land = [&](const std::vector<uint8_t> &m, float blend) {
		paint_mask_elevation(hm, m, land, blend);
		for (size_t i = 0; i < N; ++i) cl_land[i] |= m[i];
	};
	auto set_all = [&](float m) { std::fill(hm.data.begin(), hm.data.end(), metres(water_h, m)); };
	auto circle = [&](float r, float start) {
		slots.clear();
		for (int i = 0; i < n; ++i) slots.push_back(polar(start + kTau * (float)i / (float)n, r));
	};
	auto river_players = [&](float angle, float width) {
		// playerPlacementRiver, grouped by team (indexed by player).
		slots.assign((size_t)n, {0.0f, 0.0f});
		const bool even = n % 2 == 0;
		for (int i = 0; i < n; ++i) {
			const bool cur_even = i % 2 == 0;
			const int divident = (even || cur_even) ? (i + 1) % 2 : 0;
			const int divisor = even ? 0 : (cur_even ? 1 : -1);
			const float f = ((float)(i - 1 + divident) / 2.0f + 1.0f) / ((float)(n + divisor) / 2.0f + 1.0f);
			slots[(size_t)i] = rot(width * (float)(i % 2) - width / 2.0f, (f - 0.5f) * size, angle);
		}
		group_players_by_area(slots, n, teams);
		grouped = true;
	};
	auto mark_large = [&]() { territory = scale_f(17.0f, 29.0f, half) / 3.0f; };
	// Distance to the civic centres (clPlayer, a radius 5 disk) and to the
	// large player territory; recomputed once the slots are known.
	std::vector<float> to_cc;
	auto refresh_players = [&]() { to_cc = dist_to_points(hm, slots); };
	auto avoid_cc = [&](size_t i, float d) { return to_cc.empty() || to_cc[i] > 5.0f + d; };
	auto avoid_territory = [&](size_t i, float d) { return territory <= 0.0f || to_cc.empty() || to_cc[i] > territory + d; };
	// centralRiverCoordinates(angle): across the map through the centre.
	auto river_ends = [&](float angle) {
		const auto a = rot(-(half - 1.0f), 0.0f, angle), b = rot(half - 1.0f, 0.0f, angle);
		return std::make_pair(a, b);
	};
	auto shore_jaggedness = [&](float water_m, std::vector<uint8_t> &border, float shore_dist, bool inwards) {
		const int count = scale_by_map_size(7, 130, half) * 2;
		for (int pass = 0; pass < 2; ++pass) {
			if (!pass && !inwards) continue;
			std::vector<uint8_t> outside(N);
			for (size_t i = 0; i < N; ++i) outside[i] = !border[i];
			const auto d_in = distance_to_mask(outside, hm.res, sp), d_out = distance_to_mask(border, hm.res, sp);
			const auto d_pen = distance_to_mask(cl_peninsula, hm.res, sp);
			// borderClasses(border, d, d): within d inside or outside its edge.
			auto ok = [&](size_t i) {
				const bool band = border[i] ? d_in[i] <= shore_dist : d_out[i] <= shore_dist;
				return band && avoid_cc(i, 20.0f) && d_pen[i] > 20.0f;
			};
			std::vector<std::pair<float, float>> cand;
			for (int z = 0; z < hm.res; ++z)
				for (int x = 0; x < hm.res; ++x)
					if (ok((size_t)z * (size_t)hm.res + (size_t)x)) {
						float wx, wz;
						hm.grid_to_world(x, z, wx, wz);
						cand.push_back({wx, wz});
					}
			if (cand.empty()) continue;
			create_areas(hm, count, count * 3, 1e9f,
				[&]() { return cand[(size_t)rng.range_int(0, (int)cand.size() - 1)]; },
				[&](float cx, float cz) { return chain_placer(hm, rng, cx, cz, 2.0f, std::floor(scale_f(4.0f, 6.0f, half)), 15); },
				ok,
				[&](const std::vector<uint8_t> &m) {
					paint_mask_elevation(hm, m, pass ? land : metres(water_h, water_m), 4.0f);
					for (size_t i = 0; i < N; ++i)
						if (m[i]) cl_land[i] = pass ? 1 : 0;
				});
		}
	};
	auto extensions_or_islands = [&]() {
		const int rnd = rng.range_int(1, 3);
		const int count = scale_by_map_size(2, 5, half) * rng.range_int(8, 14);
		if (rnd == 1) {
			auto to_land = distance_to_mask(cl_land, hm.res, sp);
			const float side = (float)rng.range_int(scale_by_map_size(8, 15, half), scale_by_map_size(15, 23, half));
			const float ff = rng.range_float(0.0f, 0.2f);
			create_areas(hm, count, count * 10, ff, random_centre,
				[&](float cx, float cz) { return clump_mask(hm, rng, cx, cz, side * side, 0.8f, 0.1f); },
				[&](size_t i) { return to_land[i] > 3.0f && avoid_cc(i, 3.0f); },
				[&](const std::vector<uint8_t> &m) {
					paint_land(m, 4.0f);
					to_land = distance_to_mask(cl_land, hm.res, sp);
				});
		} else if (rnd == 2) {
			create_areas(hm, count, count * 10, 0.07f, random_centre,
				[&](float cx, float cz) {
					return chain_placer(hm, rng, cx, cz, std::floor(scale_f(4.0f, 7.0f, half)), std::floor(scale_f(7.0f, 10.0f, half)),
					                    (int)std::floor(scale_f(16.0f, 40.0f, half)));
				},
				[&](size_t) { return true; },
				[&](const std::vector<uint8_t> &m) { paint_land(m, 4.0f); });
		}
	};
	auto central_sea_or_isthmus = [&](bool isthmus) {
		const float start = rng.range_float(0.0f, kTau);
		const auto ends = river_ends(start);
		const float width = (scale_f(0.27f, 0.42f, half) + rng.range_float(0.0f, 0.08f)) * size;
		paint_river(hm, rng, ends.first.first, ends.first.second, ends.second.first, ends.second.second, width, scale_f(3.0f, 12.0f, half),
		            metres(water_h, -3.0f), land, 20.0f, false,
		            [&](size_t i, float h) { if (h < water_h) cl_water[i] = 1; },
		            [&](size_t i) { hm.data[i] = metres(water_h, 3.1f); cl_land[i] = 1; });
		river_players(start + 1.5707963f, 0.6f * size);
		refresh_players();
		if (isthmus) {
			const auto ie = river_ends(start + 1.5707963f);
			const float b_ = (float)rng.range_int(100, 140); const float a_ = (float)rng.range_int(16, 24); const float w = scale_f(a_, b_, half);
			const auto m = path_mask(hm, rng, ie.first.first, ie.first.second, ie.second.first, ie.second.second, w, 0.5f,
			                         3.0f * scale_f(1.0f, 4.0f, half), 0.1f, 0.01f);
			paint_land(m, 4.0f);
			for (size_t i = 0; i < N; ++i) if (m[i]) cl_water[i] = 0;
		}
		extensions_or_islands();
	};
	auto central_river = [&](bool shallows) {
		set_all(3.0f);
		const float start = rng.range_float(0.0f, kTau);
		river_players(start + 1.5707963f, 0.5f * size);
		mark_large();
		refresh_players();
		const float bed = metres(water_h, -4.0f);
		const auto ends = river_ends(start);
		{
			auto m = path_mask(hm, rng, ends.first.first, ends.first.second, ends.second.first, ends.second.second,
			                   scale_f(14.0f, 24.0f, half), 0.5f, scale_f(3.0f, 12.0f, half), 0.1f, 0.01f);
			for (size_t i = 0; i < N; ++i) if (m[i] && !avoid_territory(i, 4.0f)) m[i] = 0;
			paint_mask_elevation(hm, m, bed, 4.0f);
		}
		// Water spots at both ends keep the banks apart at the map edge.
		for (const auto &e : {ends.first, ends.second}) {
			auto m = clump_mask(hm, rng, e.first, e.second, 3.14159265f * det::pow(scale_f(5.0f, 10.0f, half), 2.0f), 0.95f, 0.6f);
			for (size_t i = 0; i < N; ++i) if (m[i] && !avoid_territory(i, 8.0f)) m[i] = 0;
			paint_mask_elevation(hm, m, bed, 2.0f);
		}
		std::vector<uint8_t> shallow(N, 0);
		if (shallows) {
			const int k = rng.range_int(1, scale_by_map_size(4, 8, half));
			for (int i = 0; i <= k; ++i) {
				const float loc = (rng.range_float(0.15f, 0.85f) - 0.5f) * size;
				const auto a = rot(loc, -half, start), b = rot(loc, half, start);
				passage(hm, a.first, a.second, b.first, b.second, scale_f(8.0f, 12.0f, half), 2.0f, wade, &shallow);
			}
		}
		if (rng.range_int(0, 2) < 2) {
			const auto to_shallow = distance_to_mask(shallow, hm.res, sp);
			// createTributaryRivers(startAngle, randIntInclusive(8, scaleByMapSize(12, 16)), scaleByMapSize(10, 20), -4, [-6, -1.5], pi / 5).
			const int rivers = rng.range_int(8, scale_by_map_size(12, 16, half));
			const float rw = scale_f(10.0f, 20.0f, half);
			std::vector<uint8_t> trib(N, 0);
			const float lo = metres(water_h, -6.0f), hi = metres(water_h, -1.5f);
			for (int r = 0; r < rivers; ++r) {
				const float along = (rng.range_float(0.05f, 0.95f) - 0.5f) * size;
				const float sign = rng.range_int(0, 1) ? 1.0f : -1.0f;
				// findLocationInDirectionBasedOnHeight(-6, -1.5, offset 4) from the
				// river's centre line (the search span is 0.1 tiles: one point).
				const auto c0 = rot(along, 0.0f, start);
				const float h0 = hm.sample_world(c0.first, c0.second);
				if (h0 < lo || h0 > hi) continue;
				const auto st = rot(along, -sign * 4.0f, start);
				const float sx = std::round(st.first), sz = std::round(st.second);
				const auto e = polar(-(start - sign * rng.range_float(0.6283185f, kTau - 0.6283185f)), size);
				const auto to_trib = distance_to_mask(trib, hm.res, sp);
				auto m = path_mask(hm, rng, sx, sz, std::round(e.first), std::round(e.second), rw, 0.4f, scale_f(3.0f, 12.0f, half), 0.1f, 0.05f);
				bool ok = true;
				for (size_t i = 0; i < N && ok; ++i)
					if (m[i]) ok = avoid_territory(i, 3.0f) && to_trib[i] > 3.0f && to_shallow[i] > 2.0f;
				if (!ok) continue;
				paint_mask_elevation(hm, m, bed, 4.0f);
				for (size_t i = 0; i < N; ++i) trib[i] |= m[i];
				auto pm = clump_mask(hm, rng, e.first, e.second, 3.14159265f * rw * rw / 4.0f, 0.95f, 0.6f);
				for (size_t i = 0; i < N; ++i) if (pm[i] && !avoid_territory(i, 3.0f)) pm[i] = 0;
				paint_mask_elevation(hm, pm, bed, 3.0f);
			}
			for (float zf : {0.25f, 0.75f}) {
				const auto a = rot(-half, (zf - 0.5f) * size, start), b = rot(half, (zf - 0.5f) * size, start);
				passage(hm, a.first, a.second, b.first, b.second, scale_f(8.0f, 12.0f, half), 2.0f, wade);
			}
		}
	};
	// Game Setup Landscape (unknown.json): a random group picks one of its
	// items with the map's RNG, so every peer picks the same.
	static constexpr int kLand[] = {1, 3, 4, 7, 8, 9, 10, 11, 12}; // Continent .. Mainland
	static constexpr int kNaval[] = {2, 0, 6};                        // Central Sea, Archipelago, Lake and rivers
	int landscape;
	if (landscape_choice == UNKNOWN_LANDSCAPE_RANDOM_LAND) landscape = kLand[rng.range_int(0, 8)];
	else if (landscape_choice == UNKNOWN_LANDSCAPE_RANDOM_NAVAL) landscape = kNaval[rng.range_int(0, 2)];
	else if (landscape_choice >= UNKNOWN_LANDSCAPE_FIRST && landscape_choice < UNKNOWN_LANDSCAPE_FIRST + UNKNOWN_LANDSCAPE_COUNT)
		landscape = landscape_choice - UNKNOWN_LANDSCAPE_FIRST;
	else landscape = rng.range_int(0, 12);
	g_unknown_landscape = landscape; // distribute_relics: only Archipelago places starting treasures
	switch (landscape) {
		case 0: { // Archipelago
			circle(0.35f * size, rng.range_float(0.0f, kTau));
			mark_large();
			refresh_players();
			const float island = 3.14159265f * det::pow(scale_f(17.0f, 29.0f, half), 2.0f);
			for (const auto &s : slots) paint_mask_elevation(hm, clump_mask(hm, rng, s.first, s.second, island, 0.8f, 0.1f), land, 4.0f);
			const int kind = rng.range_int(1, 3);
			if (kind == 1) {
				const int count = scale_by_map_size(2, 5, half) * rng.range_int(8, 14);
				for (int i = 0; i < count; ++i) {
					const auto c = random_centre();
					paint_land(clump_mask(hm, rng, c.first, c.second, island * rng.range_float(0.8f, 1.2f), 0.8f, 0.1f), 4.0f);
				}
				// Shore jaggedness: small puddles of land along the shores.
				std::vector<uint8_t> outside(N);
				for (size_t i = 0; i < N; ++i) outside[i] = !cl_land[i];
				const auto d_in = distance_to_mask(outside, hm.res, sp), d_out = distance_to_mask(cl_land, hm.res, sp);
				auto ok = [&](size_t i) { return cl_land[i] ? d_in[i] <= 6.0f : d_out[i] <= 3.0f; };
				const int puddles = scale_by_map_size(12, 130, half) * 2;
				create_areas(hm, puddles, puddles * 150, 1e9f, random_centre,
					[&](float cx, float cz) { return clump_mask(hm, rng, cx, cz, scale_f(15.0f, 80.0f, half), 0.2f, 0.1f); },
					ok, [&](const std::vector<uint8_t> &m) { paint_land(m, 4.0f); });
			} else {
				auto to_land = distance_to_mask(cl_land, hm.res, sp);
				const float gap = kind == 2 ? 3.0f : (float)rng.range_int(8, 16);
				auto ok = [&](size_t i) { return to_land[i] > gap && avoid_territory(i, 3.0f); };
				auto paint = [&](const std::vector<uint8_t> &m) {
					paint_land(m, 4.0f);
					to_land = distance_to_mask(cl_land, hm.res, sp);
				};
				if (kind == 2) {
					const float f1 = rng.range_float(0.6f, 1.4f), ff1 = rng.range_float(0.0f, 0.2f);
					const int c1 = scale_by_map_size(6, 10, half) * rng.range_int(8, 14);
					create_areas(hm, c1, c1 * 10, ff1, random_centre,
						[&](float cx, float cz) { return clump_mask(hm, rng, cx, cz, island * f1, 0.8f, 0.1f); }, ok, paint);
					const float f2 = rng.range_float(0.3f, 0.7f);
					const int c2 = scale_by_map_size(2, 6, half) * rng.range_int(6, 15);
					create_areas(hm, c2, c2 * 25, 0.07f, random_centre,
						[&](float cx, float cz) { return clump_mask(hm, rng, cx, cz, island * f2, 0.8f, 0.1f); },
						[&](size_t i) { return to_land[i] > 3.0f && avoid_territory(i, 3.0f); },
						[&](const std::vector<uint8_t> &m) { paint_mask_elevation(hm, m, land, 6.0f); for (size_t i = 0; i < N; ++i) cl_land[i] |= m[i]; to_land = distance_to_mask(cl_land, hm.res, sp); });
				} else {
					const float f1 = rng.range_float(0.8f, 1.2f);
					const int c1 = scale_by_map_size(2, 5, half) * rng.range_int(8, 14);
					create_areas(hm, c1, c1 * 10, 1e9f, random_centre,
						[&](float cx, float cz) { return clump_mask(hm, rng, cx, cz, island * f1, 0.8f, 0.1f); }, ok, paint);
				}
			}
			break;
		}
		case 1: { // Continent
			circle(0.25f * size, rng.range_float(0.0f, kTau));
			refresh_players();
			for (const auto &s : slots)
				paint_land(chain_placer(hm, rng, s.first, s.second, 2.0f, std::floor(scale_f(5.0f, 9.0f, half)),
				                        (int)std::floor(scale_f(5.0f, 20.0f, half)), std::floor(scale_f(23.0f, 50.0f, half))), 4.0f);
			paint_land(clump_mask(hm, rng, 0.0f, 0.0f, 3.14159265f * det::pow(0.38f * size, 2.0f), 0.9f, 0.09f), 4.0f);
			if (rng.range_int(0, 2) == 0) {
				// A peninsula: half the map not ringed by water.
				const float a = rng.range_float(0.0f, kTau);
				const auto p1 = polar(a, 0.25f * size), p2 = polar(a, 0.35f * size);
				paint_land(clump_mask(hm, rng, p1.first, p1.second, 3.14159265f * det::pow(0.38f * size, 2.0f), 0.9f, 0.09f), 4.0f);
				const auto pen = clump_mask(hm, rng, p2.first, p2.second, 3.14159265f * det::pow(0.33f * size, 2.0f), 0.9f, 0.01f);
				for (size_t i = 0; i < N; ++i) cl_peninsula[i] |= pen[i];
			}
			shore_jaggedness(-5.0f, cl_land, 7.0f, true);
			break;
		}
		case 2: // CentralSea
		case 3: // Isthmus
			central_sea_or_isthmus(landscape == 3);
			break;
		case 4: // CentralRiverLand
		case 5: // CentralRiverNaval
			central_river(landscape == 4);
			break;
		case 6: { // RiversAndLake
			set_all(3.0f);
			const float start = rng.range_float(0.0f, kTau);
			circle(0.35f * size, start);
			refresh_players();
			const float wl = metres(water_h, -4.0f);
			if (rng.range_int(0, 3) < 3) {
				const auto m = clump_mask(hm, rng, 0.0f, 0.0f, 3.14159265f * det::pow(0.17f * size, 2.0f), 0.7f, 0.1f);
				paint_mask_elevation(hm, m, wl, 4.0f);
				for (size_t i = 0; i < N; ++i) cl_water[i] |= m[i];
				shore_jaggedness(-4.0f, cl_water, 3.0f, true);
			}
			for (int i = 0; i < n; ++i) {
				const auto r = polar(start + 3.14159265f / (float)n + kTau * (float)i / (float)n, 0.5f * size);
				auto m = path_mask(hm, rng, 0.0f, 0.0f, r.first, r.second, scale_f(14.0f, 24.0f, half), 0.4f, 3.0f * scale_f(1.0f, 3.0f, half), 0.2f, 0.05f);
				bool ok = true;
				for (size_t k = 0; k < N && ok; ++k) if (m[k]) ok = avoid_cc(k, 5.0f);
				if (ok) paint_mask_elevation(hm, m, wl, 4.0f);
				auto e = clump_mask(hm, rng, r.first, r.second, 3.14159265f * det::pow(scale_f(4.0f, 22.0f, half), 2.0f), 0.95f, 0.6f);
				for (size_t k = 0; k < N; ++k) if (e[k] && !avoid_cc(k, 5.0f)) e[k] = 0;
				paint_mask_elevation(hm, e, wl, 0.0f);
			}
			paint_mask_elevation(hm, clump_mask(hm, rng, 0.0f, 0.0f, 3.14159265f * det::pow(0.04f * size, 2.0f), 0.7f, 0.1f), wl, 4.0f);
			break;
		}
		case 7: { // EdgeSeas
			set_all(3.0f);
			const float start = rng.range_float(0.0f, kTau);
			const float width = 0.2f * size;
			for (int i = 0; i < n; ++i)
				slots.push_back(rot(((float)(i + 1) / (float)(n + 1) - 0.5f) * size, width * ((float)(i % 2) - 0.5f), start + 1.5707963f));
			refresh_players();
			const int sides = rng.range_int(0, 2);
			for (int s = 0; s < 2; ++s) {
				if ((sides == 0 && s == 1) || (sides == 1 && s == 0)) continue;
				const float a = (s ? 3.14159265f : 0.0f) + start;
				const auto p0 = rot(-half, -half, a), p1 = rot(-half, half, a);
				paint_river(hm, rng, p0.first, p0.second, p1.first, p1.second, scale_f(80.0f, rng.range_float(270.0f, 320.0f), half),
				            scale_f(2.0f, 8.0f, half), metres(water_h, -4.0f), land, 20.0f, true, [](size_t, float) {}, [](size_t) {});
			}
			extensions_or_islands();
			for (size_t i = 0; i < N; ++i)
				if (hm.data[i] >= water_h && hm.data[i] <= metres(water_h, 3.12f)) cl_land[i] = 1;
			shore_jaggedness(-4.0f, cl_land, 7.0f, false);
			break;
		}
		case 8: { // Gulf
			set_all(3.0f);
			const float start = rng.range_float(0.0f, kTau);
			for (int i = 0; i < n; ++i)
				slots.push_back(polar(start + 2.0f / 3.0f * 3.14159265f * (-1.0f + (n == 1 ? 1.0f : 2.0f * (float)i / (float)(n - 1))), 0.35f * size));
			mark_large();
			refresh_players();
			const float base_r = (float)scale_by_map_size(15, 25, half);
			const float parts[3][2] = {{0.16f, 0.0f}, {0.2f, 0.2f}, {0.22f, 0.49f}};
			for (const auto &g : parts) {
				const auto c = polar(start, -g[1] * size);
				auto m = clump_mask(hm, rng, std::round(c.first), std::round(c.second), 3.14159265f * det::pow(g[0] * size, 2.0f), 0.7f, 0.05f);
				for (size_t i = 0; i < N; ++i) if (m[i] && !avoid_territory(i, base_r)) m[i] = 0;
				paint_mask_elevation(hm, m, metres(water_h, -3.0f), 4.0f);
			}
			break;
		}
		case 9: { // Lakes
			set_all(3.0f);
			circle(0.35f * size, rng.range_float(0.0f, kTau));
			mark_large();
			refresh_players();
			const bool apart = rng.range_int(0, 1) == 1;
			std::vector<uint8_t> lakes(N, 0);
			auto to_lake = distance_to_mask(lakes, hm.res, sp);
			const int count = scale_by_map_size(5, 16, half);
			create_areas(hm, count, count * 10, 1e9f, random_centre,
				[&](float cx, float cz) { return clump_mask(hm, rng, cx, cz, scale_f(160.0f, 700.0f, half), 0.2f, 0.1f); },
				[&](size_t i) { return avoid_territory(i, 12.0f) && (!apart || to_lake[i] > 8.0f); },
				[&](const std::vector<uint8_t> &m) {
					paint_mask_elevation(hm, m, metres(water_h, -5.0f), 5.0f);
					for (size_t i = 0; i < N; ++i) lakes[i] |= m[i];
					if (apart) to_lake = distance_to_mask(lakes, hm.res, sp);
				});
			break;
		}
		case 10: { // Passes
			set_all(3.0f);
			const float start = rng.range_float(0.0f, kTau);
			circle(0.35f * size, start);
			refresh_players();
			const float mountain = metres(water_h, 24.0f);
			for (int i = 0; i < n; ++i) {
				const auto r = polar(start + 3.14159265f / (float)n + kTau * (float)i / (float)n, 0.5f * size);
				auto m = path_mask(hm, rng, 0.0f, 0.0f, r.first, r.second, scale_f(14.0f, 24.0f, half), 0.4f, 3.0f * scale_f(1.0f, 3.0f, half), 0.2f, 0.05f);
				bool ok = true;
				for (size_t k = 0; k < N && ok; ++k) if (m[k]) ok = avoid_cc(k, 5.0f);
				if (ok) paint_mask_elevation(hm, m, mountain, 1.0f);
				auto e = clump_mask(hm, rng, r.first, r.second, 3.14159265f * det::pow(scale_f(4.0f, 22.0f, half), 2.0f), 0.95f, 0.6f);
				for (size_t k = 0; k < N; ++k) if (e[k] && !avoid_cc(k, 5.0f)) e[k] = 0;
				paint_mask_elevation(hm, e, mountain, 0.0f);
			}
			if (n > 1) {
				std::vector<std::pair<float, float>> passes;
				if (n == 2)
					for (int i = 0; i < 6; ++i) passes.push_back(polar(start + kTau * (float)i / 6.0f, 0.35f * size));
				for (int i = 0; i < n; ++i) {
					const auto a = n == 2 ? passes[(size_t)(3 * i + 1)] : slots[(size_t)i];
					const auto b = n == 2 ? passes[(size_t)(3 * i + 2)] : slots[(size_t)((i + 1) % n)];
					const auto m = path_mask(hm, rng, a.first, a.second, b.first, b.second, scale_f(14.0f, 24.0f, half), 0.4f,
					                         3.0f * scale_f(1.0f, 3.0f, half), 0.2f, 0.05f);
					paint_mask_elevation(hm, m, land, 2.0f);
				}
			}
			if (rng.range_int(0, 4) < 2)
				paint_mask_elevation(hm, clump_mask(hm, rng, 0.0f, 0.0f, 3.14159265f * det::pow(0.1f * size, 2.0f), 0.7f, 0.1f), metres(water_h, -4.0f), 3.0f);
			else
				paint_mask_elevation(hm, clump_mask(hm, rng, 0.0f, 0.0f, 3.14159265f * det::pow(0.05f * size, 2.0f), 0.7f, 0.1f), mountain, 4.0f);
			break;
		}
		case 11: { // Lowlands
			set_all(30.0f);
			const float start = rng.range_float(0.0f, kTau);
			circle(0.35f * size, start);
			refresh_players();
			int valleys = n;
			const int s = (int)std::lround(size);
			if ((s >= 128 && n <= 2) || (s >= 192 && n <= 3) || (s >= 320 && n <= 4) || (s >= 384 && n <= 5) || (s >= 448 && n <= 6)) valleys *= 2;
			for (int i = 0; i < valleys; ++i) {
				const auto v = polar(start + kTau * (float)i / (float)valleys, 0.35f * size);
				paint_land(clump_mask(hm, rng, v.first, v.second, 3.14159265f * det::pow(scale_f(18.0f, 32.0f, half), 2.0f), 0.65f, 0.1f), 2.0f);
				paint_mask_elevation(hm, path_mask(hm, rng, 0.0f, 0.0f, v.first, v.second, scale_f(14.0f, 24.0f, half), 0.4f,
				                                   3.0f * scale_f(1.0f, 3.0f, half), 0.2f, 0.05f), land, 4.0f);
			}
			paint_mask_elevation(hm, clump_mask(hm, rng, 0.0f, 0.0f, 3.14159265f * det::pow(0.18f * size, 2.0f), 0.7f, 0.1f), land, 4.0f);
			break;
		}
		default: // Mainland
			set_all(3.0f);
			circle(0.35f * size, rng.range_float(0.0f, kTau));
			refresh_players();
			break;
	}
	// Players by slot: slot k is player ids[k] unless the placement grouped them already.
	std::vector<std::pair<float, float>> locs((size_t)n);
	for (int k = 0; k < n; ++k) locs[(size_t)(grouped ? k : ids[(size_t)k])] = slots[(size_t)k];
	refresh_players();
	// clLand: heights 0-3.12 m.
	const float land_lo = water_h, land_hi = metres(water_h, 3.12f);
	std::vector<uint8_t> flat(N);
	for (size_t i = 0; i < N; ++i) flat[i] = hm.data[i] >= land_lo && hm.data[i] <= land_hi;
	{
		// Bumps: +2 m, 2 from water, 10 from the bases, 3 inside the land.
		const auto in_land = dist_inside(hm, flat);
		std::vector<uint8_t> wet(N);
		for (size_t i = 0; i < N; ++i) wet[i] = hm.data[i] < water_h;
		const auto to_wet = distance_to_mask(wet, hm.res, sp);
		const int bumps = rng.range_int(0, scale_by_map_size(1, 2, half) * 200);
		create_areas(hm, bumps, bumps * 10, 1e9f, random_centre,
			[&](float cx, float cz) { return clump_mask(hm, rng, cx, cz, scale_f(20.0f, 50.0f, half), 0.3f, 0.06f); },
			[&](size_t i) { return to_wet[i] > 2.0f && avoid_cc(i, 10.0f) && in_land[i] > 3.0f; },
			[&](const std::vector<uint8_t> &m) { modify_mask_elevation(hm, m, metres(water_h, 2.0f) - water_h, 2.0f); });
	}
	{
		// Hills: 18 m, 15 from the bases, 6-18 apart, on the land.
		std::vector<uint8_t> hills(N, 0);
		auto to_hill = distance_to_mask(hills, hm.res, sp);
		const float gap = (float)rng.range_int(6, 18);
		const int a_ = rng.range_int(0, scale_by_map_size(4, 8, half)); const int count = a_ * rng.range_int(1, scale_by_map_size(4, 9, half));
		create_areas(hm, count, count * 10, 1e9f, random_centre,
			[&](float cx, float cz) { return clump_mask(hm, rng, cx, cz, scale_f(20.0f, 150.0f, half), 0.2f, 0.1f); },
			[&](size_t i) { return avoid_cc(i, 15.0f) && to_hill[i] > gap && flat[i]; },
			[&](const std::vector<uint8_t> &m) {
				paint_mask_elevation(hm, m, metres(water_h, 18.0f), 2.0f);
				for (size_t i = 0; i < N; ++i) hills[i] |= m[i];
				to_hill = distance_to_mask(hills, hm.res, sp);
			});
	}
	(void)cl_water;
	finish_spawns(hm, spawns, locs, land, 18.0f, water_h);
}

void RmsGenerator::synth_migration(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players,
		const uint8_t *teams, uint8_t team_placement) {
	// migration.js (a circular map): sea at -5 m; players placed by
	// playerPlacementByPattern (circle at f = 0.42, river 0.65 of the map
	// apart, or stronghold: teams at f = 0.44, members f = 0.06 apart), each
	// on an island (ClumpPlacer(diskArea(defaultPlayerBaseRadius() * 1.75),
	// 0.8, 0.1), 3 m, blend 4); the middle continent (ClumpPlacer(diskArea(
	// f = 0.3), 0.8, 0.08) at the centre, 3 m, blend 4) kept 22 off the
	// islands; scaleByMapSize(2, 15) * 20 shore-jaggedness clumps of
	// scaleByMapSize(10, 65) tiles on its border (6 in, 3 out), 16 off the
	// islands; scaleByMapSize(100, 200) 2 m bumps 3 inside it and 10 off the
	// islands; scaleByMapSize(1, 4) * players hills of 18 m (ClumpPlacer(
	// scaleByMapSize(20, 150), 0.2, 0.1), blend 2) 12 inside it, 50 apart.
	// Players reach the continent and each other by ship. RTS0 keeps each
	// start 16 in from the edge and 26 from the others (pattern_locs) and
	// raises the land within 22 of it so the starting resources stay dry.
	water_h = 1.0f;
	const float half = hm.map_half, size = 2.0f * half;
	const float land = metres(water_h, 3.0f);
	hm.init(hm.res, hm.map_half, metres(water_h, -5.0f));
	const size_t N = hm.data.size();
	const float start = rng.range_float(0.0f, kTwoPi);
	// g_MapSettings.PlayerPlacement: one of the JSON's PlayerPlacements
	// (0 circle, 1 river, 2 stronghold), picked at random unless Game Setup
	// chose Circle, River or Stronghold.
	int pattern = team_placement == TEAM_PLACEMENT_RADIAL ? 0 : team_placement == TEAM_PLACEMENT_RIVER ? 1
			: team_placement == TEAM_PLACEMENT_STRONGHOLD ? 2 : -1;
	if (pattern < 0) pattern = rng.range_int(0, 2);
	static const float kTeamDist[3] = {0.42f, 0.65f, 0.44f}, kPlayerDist[3] = {0.1f, 0.1f, 0.06f};
	std::vector<std::pair<float, float>> locs;
	if (pattern == 1) {
		// playerPlacementRiver(startAngle, 0.65 * size): two rows across the map.
		const float width = kTeamDist[1] * size;
		const float nx = det::cos(start), nz = det::sin(start), ux = -nz, uz = nx;
		locs.assign((size_t)num_players, {0.0f, 0.0f});
		const bool even = num_players % 2 == 0;
		for (int i = 0; i < num_players; ++i) {
			const bool cur_even = i % 2 == 0;
			const int divident = (even || cur_even) ? (i + 1) % 2 : 0;
			const int divisor = even ? 0 : (cur_even ? 1 : -1);
			const float across = (i % 2 ? 0.5f : -0.5f) * width;
			const float f = ((float)(i - 1 + divident) / 2.0f + 1.0f) / ((float)(num_players + divisor) / 2.0f + 1.0f);
			const float along = (f - 0.5f) * size;
			locs[(size_t)i] = {ux * along + nx * across, uz * along + nz * across};
		}
		group_players_by_area(locs, num_players, teams);
		spread_locs(locs, 26.0f, half - 16.0f);
	} else if (pattern == 0) {
		// playerPlacementCircle(0.42 * size, startAngle): player i (getPlayerIDs order,
		// teams not sorted together) at startAngle + i * 2 pi / n.
		for (int i = 0; i < num_players; ++i) locs.push_back(polar(start + kTwoPi * (float)i / (float)num_players, kTeamDist[0] * size));
		spread_locs(locs, 26.0f, half - 16.0f);
	} else {
		// placeStronghold(getTeamsArray(), 0.44 * size, 0.06 * size * 1.4, startAngle):
		// each team's stronghold radius is groupedDistance / 2 / sin(pi / members) (0
		// alone), the teams spaced round the circle by their radii, each team's spot
		// at 0.8 of the distance, member p at startAngle + (p + 1) * 2 pi / members.
		const auto ta = teams_array(num_players, teams);
		const int T = (int)ta.size();
		const float dist = kTeamDist[2] * size, grouped = kPlayerDist[2] * size * 1.4f;
		std::vector<float> sr((size_t)T, 0.0f);
		float sum = 0.0f;
		for (int i = 0; i < T; ++i) {
			const int k = (int)ta[(size_t)i].size();
			sr[(size_t)i] = k == 1 ? 0.0f : grouped / 2.0f / det::sin(kSeasPi / (float)k);
			sum += sr[(size_t)i];
		}
		const float between = (dist * kTwoPi - 2.0f * sum) / (float)T;
		locs.assign((size_t)num_players, {0.0f, 0.0f});
		float a = start;
		for (int i = 0; i < T; ++i) {
			a += (between + sr[(size_t)((i + T - 1) % T)] + sr[(size_t)i]) / dist;
			const auto tp = polar(a, dist * 0.8f);
			const int k = (int)ta[(size_t)i].size();
			for (int p = 0; p < k; ++p) {
				const auto o = polar(start + (float)(p + 1) * kTwoPi / (float)k, sr[(size_t)i]);
				locs[(size_t)ta[(size_t)i][(size_t)p]] = {tp.first + o.first, tp.second + o.second};
			}
		}
		spread_locs(locs, 26.0f, half - 16.0f);
	}
	auto in_map = [&](int i) { return seas_in_circle(hm, i); };
	// Player islands (clIsland).
	const float island_r = seas_scale(15.0f, 25.0f, half) * 1.75f;
	std::vector<uint8_t> cl_island(N, 0);
	for (const auto &l : locs) {
		const auto m = seas_clump(hm, rng, kSeasPi * island_r * island_r, 0.8f, 0.1f, 1e9f, l.first, l.second, in_map);
		if (!seas_any(m)) continue;
		paint_mask_elevation(hm, m, land, 4.0f);
		record_area(hm, m, AREA_PLAYER_ISLAND);
		seas_add(cl_island, m);
	}
	const auto to_island = seas_dist_inside(hm, cl_island);
	// The continent body (clLand).
	std::vector<uint8_t> cl_land(N, 0);
	{
		const float r = 0.3f * size;
		const auto m = seas_clump(hm, rng, kSeasPi * r * r, 0.8f, 0.08f, 1e9f, 0.0f, 0.0f,
		                          [&](int i) { return in_map(i) && to_island[(size_t)i] > 22.0f; });
		if (seas_any(m)) {
			paint_mask_elevation(hm, m, land, 4.0f);
			record_area(hm, m, AREA_ISLAND);
			seas_add(cl_land, m);
		}
	}
	// Shore jaggedness: borderClasses(clLand, 6, 3), avoidClasses(clIsland, 16), 150 tries per clump.
	if (seas_any(cl_land)) {
		auto in_land = seas_dist_outside(hm, cl_land);
		auto to_land = seas_dist_inside(hm, cl_land);
		auto border = [&](int i) {
			return in_map(i) && to_island[(size_t)i] > 16.0f && (cl_land[(size_t)i] ? in_land[(size_t)i] <= 6.0f : to_land[(size_t)i] <= 3.0f);
		};
		seas_create_areas(hm, rng, seas_scale(2.0f, 15.0f, half) * 20.0f, 150, [&](float x, float z) {
			const auto m = seas_clump(hm, rng, seas_scale(10.0f, 65.0f, half), 0.2f, 0.1f, 1e9f, x, z, border);
			if (!seas_any(m)) return false;
			paint_mask_elevation(hm, m, land, 4.0f);
			seas_add(cl_land, m);
			in_land = seas_dist_outside(hm, cl_land);
			to_land = seas_dist_inside(hm, cl_land);
			return true;
		});
	}
	const auto in_land = seas_dist_outside(hm, cl_land);
	// Bumps: ClumpPlacer(scaleByMapSize(20, 50), 0.3, 0.06) raising 2 m.
	seas_create_areas(hm, rng, seas_scale(100.0f, 200.0f, half), 10, [&](float x, float z) {
		const auto m = seas_clump(hm, rng, seas_scale(20.0f, 50.0f, half), 0.3f, 0.06f, 1e9f, x, z,
		                          [&](int i) { return to_island[(size_t)i] > 10.0f && in_land[(size_t)i] > 3.0f; });
		if (!seas_any(m)) return false;
		seas_paint_modify(hm, m, 2.0f * kMetre, 2.0f);
		return true;
	});
	// Hills: LayeredPainter([tCliff, tHill], [2]), 18 m, 50 apart.
	{
		std::vector<uint8_t> cl_hill(N, 0);
		std::vector<float> to_hill(N, 1e9f);
		seas_create_areas(hm, rng, seas_scale(1.0f, 4.0f, half) * (float)num_players, 10, [&](float x, float z) {
			const auto m = seas_clump(hm, rng, seas_scale(20.0f, 150.0f, half), 0.2f, 0.1f, 1e9f, x, z, [&](int i) {
				return to_island[(size_t)i] > 10.0f && to_hill[(size_t)i] > 50.0f && in_land[(size_t)i] > 12.0f;
			});
			if (!seas_any(m)) return false;
			paint_mask_elevation(hm, m, metres(water_h, 18.0f), 2.0f);
			record_area(hm, m, AREA_HILL);
			seas_add(cl_hill, m);
			to_hill = seas_dist_inside(hm, cl_hill);
			return true;
		});
	}
	misc_maps::finish_spawns(hm, spawns, locs, land, 22.0f, water_h);
}

// =========================================================================
// Map from 0 A.D. maps/random/ngorongoro.js (rmgen2): the crater from a real
// elevation map, in three zones (lowlands, highlands, eden), each with its
// own mines, hunt and baobabs. One world unit is one tile.
// =========================================================================

namespace {
namespace ngorongoro {

const std::vector<uint8_t> &image() {
	static const std::vector<uint8_t> img = decode_heightmap_image(heightmap_images::ngorongoro_b64, heightmap_images::ngorongoro_size);
	return img;
}

// The RTS0 height of the script's 0 m (the image's black). The map has no water.
constexpr float kGround = 0.0f;
constexpr float kNoWater = -10.0f;

// heightScale(n) = n * mapSettings.Size / 320 metres.
inline float height_scale(float n, float half) { return n * 2.0f * half / 320.0f; }

// The script's tile classes from the heights: eden is DiskPlacer(fractionToTiles(0.14))
// round the centre under heightScale(60), the highlands heightScale(45) and up
// outside eden, mountain the slopes of 2 m per tile and more (SlopeConstraint(2)).
struct Zones {
	std::vector<uint8_t> eden, highlands, mountain;
};

Zones zones(const HeightmapGrid &hm) {
	const float half = hm.map_half, eden_r = 0.14f * 2.0f * half;
	const float eden_h = kGround + height_scale(60.0f, half) * kMetre, high_h = kGround + height_scale(45.0f, half) * kMetre;
	const size_t n = hm.data.size();
	Zones z{std::vector<uint8_t>(n, 0), std::vector<uint8_t>(n, 0), std::vector<uint8_t>(n, 0)};
	const auto slope = slopes_m(hm);
	for (int vz = 0; vz < hm.res; ++vz)
		for (int vx = 0; vx < hm.res; ++vx) {
			float wx, wz;
			hm.grid_to_world(vx, vz, wx, wz);
			const size_t i = (size_t)vz * (size_t)hm.res + (size_t)vx;
			z.eden[i] = wx * wx + wz * wz <= eden_r * eden_r && hm.data[i] < eden_h;
			z.highlands[i] = !z.eden[i] && hm.data[i] >= high_h;
			z.mountain[i] = slope[i] >= 2.0f;
		}
	return z;
}

} // namespace ngorongoro
} // namespace

void RmsGenerator::synth_ngorongoro(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players,
		const uint8_t *teams) {
	// ngorongoro.js: LoadHeightmapImage("ngorongoro.png", 0, 150) (GEBCO
	// elevations round the crater), SmoothingPainter(1, scaleByMapSize(0.1,
	// 0.5), 1), the eden, highlands and cliff (mountain) classes, players at
	// random (playerPlacementRandom) 5 from cliffs, highlands and eden and a
	// base radius inside the map, each start flattened over 0.8 base radius
	// with a 6-tile blend. The highland patches are AREA_HIGHLANDS (2 off the
	// cliffs, as the script's addLayeredPatches); the zones' mines, hunt and
	// baobabs are place_ngorongoro_entities'.
	using namespace misc_maps;
	using namespace ngorongoro;
	water_h = kNoWater;
	const float half = hm.map_half;
	const int n = heightmap_images::ngorongoro_size;
	paint_heightmap(hm, image(), n, 0, 0, n, 0.0f, 150.0f, kGround);
	shepard_smooth(hm, scale_f(0.1f, 0.5f, half));
	const Zones z = zones(hm);
	const size_t N = hm.data.size();
	{
		std::vector<uint8_t> up(N, 0);
		const auto near_cliff = near_mask(hm, z.mountain, 2.0f);
		for (size_t i = 0; i < N; ++i) up[i] = (z.eden[i] || z.highlands[i]) && !near_cliff[i];
		record_area(hm, up, AREA_HIGHLANDS);
	}
	// avoidClasses(mountain, 5, highlands, 5, eden, 5), stayClasses(land, defaultPlayerBaseRadius()).
	const float base_r = (float)scale_by_map_size(15, 25, half);
	const auto near_m = near_mask(hm, z.mountain, 5.0f), near_h = near_mask(hm, z.highlands, 5.0f), near_e = near_mask(hm, z.eden, 5.0f);
	std::vector<uint8_t> allowed(N, 0);
	for (int vz = 0; vz < hm.res; ++vz)
		for (int vx = 0; vx < hm.res; ++vx) {
			float wx, wz;
			hm.grid_to_world(vx, vz, wx, wz);
			const size_t i = (size_t)vz * (size_t)hm.res + (size_t)vx;
			allowed[i] = !near_m[i] && !near_h[i] && !near_e[i] && det::hypot(wx, wz) <= half - base_r;
		}
	std::vector<std::pair<float, float>> locs;
	if (player_placement_random(hm, rng, allowed, num_players, locs)) {
		group_players_by_area(locs, num_players, teams);
		make_heightmap_spawns(hm, spawns, locs, base_r * 0.8f, 6.0f, water_h);
		face_most_land(hm, spawns, water_h);
		return;
	}
	place_ring_spawns(hm, spawns, num_players, half * 0.6f, rng.range_float(0.0f, kTau), kTau, kGround + 1.0f);
	for (const auto &s : spawns) lift_area(hm, s.x, s.z, 20.0f, 6.0f, s.base_height);
}

void RmsGenerator::place_ngorongoro_entities(Simulation &sim, const HeightmapGrid &hm, const std::vector<PlayerSpawn> &spawns, RmsRng &rng,
		const Config &config) {
	// ngorongoro.js's addElements passes, zone by zone (lowlands, highlands,
	// eden), each list shuffled as the script's shuffleArray does. rmgen2
	// createObjectGroupsDeprecated makes `amount` tries at random spots on the
	// map, a group placed only where all of its objects fit (30 retries,
	// 1 tile apart) on tiles the constraint allows. addForests plants nothing
	// on the savanna biome; addLayeredPatches are the terrain areas and
	// addDecoration's actors are left out. The classes the script avoids are
	// the objects placed here (metal, rock, forest, animals), the cliffs, the
	// zones and the bases' CityPatch (a third of the base radius).
	using namespace ngorongoro;
	const float half = hm.map_half;
	const Zones zn = zones(hm);
	const auto d_eden = distance_to_mask(zn.eden, hm.res, grid_spacing(hm));
	const auto d_high = distance_to_mask(zn.highlands, hm.res, grid_spacing(hm));
	const auto d_mtn = distance_to_mask(zn.mountain, hm.res, grid_spacing(hm));
	const auto in_eden = misc_maps::dist_inside(hm, zn.eden), in_high = misc_maps::dist_inside(hm, zn.highlands);
	const float city_r = (float)scale_by_map_size(15, 25, half) / 3.0f;
	enum { CL_METAL, CL_ROCK, CL_FOREST, CL_ANIMALS, CL_COUNT };
	std::vector<std::pair<float, float>> cls[CL_COUNT];
	// avoidClasses distances (negative: not avoided) and the zone to stay 2 inside.
	struct Rule {
		float mountain, player, metal, rock, forest, animals;
		bool lowlands; // avoidClasses(eden, 2, highlands, 2)
		int stay;      // 0 none, 1 stayClasses(highlands, 2), 2 stayClasses(eden, 2)
	};
	auto allows = [&](const Rule &r, float x, float z) {
		const size_t v = vertex_at(hm, x, z);
		if (r.mountain >= 0.0f && d_mtn[v] <= r.mountain) return false;
		if (r.lowlands && (d_eden[v] <= 2.0f || d_high[v] <= 2.0f)) return false;
		if (r.stay == 1 && in_high[v] <= 2.0f) return false;
		if (r.stay == 2 && in_eden[v] <= 2.0f) return false;
		for (const auto &s : spawns)
			if (det::hypot(x - s.x, z - s.z) <= r.player + city_r) return false;
		const float d[CL_COUNT] = {r.metal, r.rock, r.forest, r.animals};
		for (int c = 0; c < CL_COUNT; ++c) {
			if (d[c] < 0.0f) continue;
			for (const auto &p : cls[c])
				if ((p.first - x) * (p.first - x) + (p.second - z) * (p.second - z) <= d[c] * d[c]) return false;
		}
		return true;
	};
	// SimpleObject(template, minCount, maxCount, minDistance, maxDistance).
	struct Obj {
		uint8_t res;   // RES_* or 0xFF for fauna
		int amount;    // resource supply
		uint8_t fauna;
		float min_n, max_n, min_d, max_d;
	};
	auto rand_int_inclusive = [&](float lo, float hi) { return (int)std::floor(lo + rng.next_float() * (hi + 1.0f - lo)); };
	// createObjectGroupsDeprecated(new SimpleGroup(objs, true, class), 0, constraint, amount).
	auto groups = [&](const std::vector<Obj> &objs, int cl, const Rule &r, float amount) {
		const float pass_r = half - 3.0f; // MAP_BORDER_WIDTH
		struct Spec { float x, z; const Obj *o; };
		std::vector<Spec> specs;
		for (int k = 0; (float)k < amount; ++k) {
			const auto c = random_in_circle(rng, pass_r);
			const float cx = std::floor(c.first + half) - half + 0.5f, cz = std::floor(c.second + half) - half + 0.5f;
			specs.clear();
			bool ok = true;
			for (const Obj &o : objs) {
				const int count = rand_int_inclusive(o.min_n, o.max_n);
				int retries = 0;
				for (int i = 0; i < count && ok; ++i)
					while (true) {
						const float b_ = rng.range_float(o.min_d, o.max_d); const float a_ = rng.range_float(0.0f, kTwoPi); const auto p = polar(a_, b_);
						const float x = cx + p.first, zz = cz + p.second;
						bool fits = x * x + zz * zz < pass_r * pass_r;
						for (const Spec &s : specs) fits = fits && (s.x - x) * (s.x - x) + (s.z - zz) * (s.z - zz) >= 1.0f;
						fits = fits && allows(r, std::floor(x + half) - half + 0.5f, std::floor(zz + half) - half + 0.5f);
						if (fits) { specs.push_back({x, zz, &o}); break; }
						if (retries++ > 30) { ok = false; break; }
					}
				if (!ok) break;
			}
			if (!ok) continue;
			for (const Spec &s : specs) {
				if (s.o->res == 0xFF) sim.spawn_fauna(s.x, s.z, s.o->fauna);
				else sim.spawn_resource_world(s.x, s.z, s.o->res, s.o->amount);
				cls[cl].push_back({std::floor(s.x + half) - half + 0.5f, std::floor(s.z + half) - half + 0.5f});
			}
		}
	};
	// g_Sizes normal 1, big 1.25, huge 1.5; g_Mixes same 0, similar 0.1, unique 0.75;
	// g_Amounts scarce 0.2, few 0.5, many 1.75, tons 3.
	auto small_metal = [&](const Rule &r, float size, float mix, float fill) {
		// addSmallMetal: gaia/ore/tropical_small (1000), 2-5 x dev at dev-3 dev, 1 + 20 fill groups.
		const float d = random_deviation(rng, size, mix);
		groups({{RES_GOLD, MINE_SUPPLY, 0, 2.0f * d, 5.0f * d, d, 3.0f * d}}, CL_METAL, r, 1.0f + 20.0f * fill);
	};
	auto metal = [&](const Rule &r, float size, float mix, float fill) {
		// addMetal: one gaia/ore/savanna_large (5000) within 4 offset, 1 + 20 fill groups.
		const float o = random_deviation(rng, size, mix);
		groups({{RES_GOLD, LARGE_MINE_SUPPLY, 0, o, o, 0.0f, 4.0f * o}}, CL_METAL, r, 1.0f + 20.0f * fill);
	};
	auto stone = [&](const Rule &r, float size, float mix, float fill) {
		// addStone: savanna stone, a large mine with 0-2 small rocks, then 2-5 small rocks, 1 + 20 fill groups each.
		const float o = random_deviation(rng, size, mix);
		groups({{RES_STONE, MINE_SUPPLY, 0, 0.0f, 2.0f * o, 0.0f, 4.0f * o}, {RES_STONE, LARGE_MINE_SUPPLY, 0, o, o, 0.0f, 4.0f * o}},
			CL_ROCK, r, 1.0f + 20.0f * fill);
		groups({{RES_STONE, MINE_SUPPLY, 0, 2.0f * o, 5.0f * o, o, 3.0f * o}}, CL_ROCK, r, 1.0f + 20.0f * fill);
	};
	auto animals = [&](const Rule &r, float size, float mix, float fill, uint8_t main, uint8_t secondary) {
		// addAnimals: 5-7 x offset of the main huntable animal within 4 offset and 2-3 x offset of the
		// secondary within 2 offset, floor(30 fill) groups of each.
		const float o = random_deviation(rng, size, mix);
		groups({{0xFF, 0, main, 5.0f * o, 7.0f * o, 0.0f, 4.0f * o}}, CL_ANIMALS, r, std::floor(30.0f * fill));
		groups({{0xFF, 0, secondary, 2.0f * o, 3.0f * o, 0.0f, 2.0f * o}}, CL_ANIMALS, r, std::floor(30.0f * fill));
	};
	auto stragglers = [&](const Rule &r, float size, float mix, float fill) {
		// addStragglerTrees on the savanna: fill at least 2 and size at least 1; tree1-tree4 (all
		// baobabs here), floor(40 max(1, (players - 3) / 2) fill / 4) fill groups of each, 3-5 offset
		// trees 2 offset + 1 to 3 offset + 2 out.
		fill = std::max(fill, 2.0f);
		size = std::max(size, 1.0f);
		const float bonus = std::max(1.0f, ((float)config.num_players - 3.0f) / 2.0f);
		const float o = random_deviation(rng, size, mix);
		const float count = std::floor(40.0f * bonus * fill / 4.0f) * fill;
		for (int t = 0; t < 4; ++t)
			groups({{RES_WOOD, TREE_WOOD_SUPPLY, 0, 3.0f * o, 5.0f * o, 2.0f * o + 1.0f, 3.0f * o + 2.0f}}, CL_FOREST, r, count);
	};
	// One addElements list: its entries in the script's order, shuffled when it shuffles them.
	auto run = [&](std::vector<std::function<void()>> list, bool shuffle) {
		std::vector<int> order(list.size());
		for (size_t i = 0; i < order.size(); ++i) order[i] = (int)i;
		if (shuffle) shuffle_ints(rng, order);
		for (int i : order) list[(size_t)i]();
	};
	const auto none = [] {}; // addForests: no forests on generic/savanna
	// Render lowlands (setBiomeLowlands: giraffes and zebras).
	{
		const Rule mines{6.0f, 30.0f, 10.0f, 20.0f, 3.0f, -1.0f, true, 0};
		run({[&] { small_metal(mines, 1.0f, 0.0f, 0.5f); }, [&] { stone(mines, 1.0f, 0.0f, 0.2f); }, none}, true);
		const Rule hunt{6.0f, 20.0f, 2.0f, 2.0f, 2.0f, 20.0f, true, 0};
		const Rule trees{6.0f, 12.0f, 2.0f, 2.0f, 4.0f, -1.0f, true, 0};
		run({[&] { animals(hunt, 1.25f, 0.1f, 1.75f, FAUNA_GIRAFFE, FAUNA_ZEBRA); },
		     [&] { animals(hunt, 1.0f, 0.75f, 1.75f, FAUNA_GIRAFFE, FAUNA_ZEBRA); },
		     [&] { stragglers(trees, 1.25f, 0.0f, 1.75f); }}, true);
	}
	// Render highlands (setBiomeHighlands: lionesses and lions), staying 2 inside them.
	{
		const Rule metal_r{3.0f, 30.0f, 20.0f, 10.0f, 3.0f, -1.0f, false, 1};
		const Rule stone_r{3.0f, 30.0f, 10.0f, 20.0f, 3.0f, -1.0f, false, 1};
		run({[&] { small_metal(metal_r, 1.0f, 0.0f, 1.75f); }, [&] { stone(stone_r, 1.0f, 0.0f, 1.75f); }, none}, true);
		const Rule hunt{3.0f, 20.0f, 2.0f, 2.0f, 2.0f, 20.0f, false, 1};
		const Rule trees{3.0f, 12.0f, 2.0f, 2.0f, 2.0f, -1.0f, false, 1};
		run({[&] { animals(hunt, 1.5f, 0.0f, 1.75f, FAUNA_LIONESS, FAUNA_LION); }, [&] { stragglers(trees, 1.5f, 0.0f, 1.75f); }}, true);
	}
	// Render eden (setBiomeEden: white rhinos and African bush elephants), staying 2 inside it.
	{
		const Rule metal_r{2.0f, 20.0f, 3.0f, 3.0f, 3.0f, -1.0f, false, 2};
		const Rule stone_r{2.0f, 30.0f, 3.0f, 3.0f, 3.0f, -1.0f, false, 2};
		run({[&] { metal(metal_r, 1.0f, 0.0f, 3.0f); }, [&] { metal(metal_r, 1.0f, 0.0f, 3.0f); },
		     [&] { small_metal(metal_r, 1.0f, 0.0f, 3.0f); }, [&] { stone(stone_r, 1.0f, 0.0f, 0.5f); }, none}, true);
		const Rule hunt{3.0f, 20.0f, 2.0f, 2.0f, 2.0f, 2.0f, false, 2};
		const Rule trees{8.0f, 12.0f, 2.0f, 2.0f, 2.0f, -1.0f, false, 2};
		run({[&] { animals(hunt, 1.0f, 0.0f, 1.75f, FAUNA_RHINOCEROS_WHITE, FAUNA_ELEPHANT_AFRICAN_BUSH); },
		     [&] { stragglers(trees, 1.5f, 0.0f, 0.2f); }}, true);
	}
}

// =========================================================================
// Map ported from 0 A.D. maps/random/river_archipelago.js: narrow strips of
// land across the map, sea between them, Gaia Mauryans on the outermost ones.
// =========================================================================

void RmsGenerator::synth_river_archipelago(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players,
		const uint8_t *teams, RiverArchipelagoLayout &out) {
	// river_archipelago.js (a circular map): sea ground at -8 m; six strips of
	// land at 3 m across the map, turned by a random startAngle, each from
	// scaleByMapSize(20, 100) ChainPlacer(1, scaleByMapSize(3, 7), scaleByMapSize(30, 60))
	// areas (SmoothElevationPainter blend 3) round random spots along the strip
	// at 0-0.06, 0.12-0.23 and 0.33-0.43 of the map and the mirror of those, or
	// at 0.03-0.09, 0.14-0.25 and 0.36-0.46 with the two middle strips' circles
	// up to scaleByMapSize(3, 8) when connectPlayers; overlapping areas leave
	// shallow land bridges between the strips. Players on the two middle strips
	// (playerPlacementLine, sortAllPlayers or primeSortAllPlayers), each on a
	// ChainPlacer(1, 6, 40) territory from a scaleByMapSize(15, 20) disk at 3 m.
	// Then the Gaia groups on the outermost strips (kept for
	// place_river_archipelago_entities), scaleByMapSize(1, 5) hills of 25 m
	// (cliff then grass, 3 wide) off the territories, water and Gaia, and
	// scaleByMapSize(20, 150) bumps of 4 m. The strips stop at the map's circle.
	using namespace misc_maps;
	water_h = 1.0f;
	const float half = hm.map_half, size = 2.0f * half;
	const float land = metres(water_h, 3.0f);
	hm.init(hm.res, hm.map_half, metres(water_h, -8.0f));
	const size_t N = hm.data.size();
	const float sp = grid_spacing(hm);
	const auto off_map = outside_circle(hm);
	auto in_map = [&](int i) { return !off_map[(size_t)i]; };
	// g_Map.randomCoordinate on a circular map: a tile within size / 2 - border of the centre
	// (border 3 for object groups, 0 for createAreas).
	auto random_spot = [&](float border) {
		const auto c = random_in_circle(rng, half - border);
		return std::make_pair(std::floor(c.first) + 0.5f, std::floor(c.second) + 0.5f);
	};
	auto random_tile = [&]() { return random_spot(3.0f); };
	auto random_area_centre = [&]() { return random_spot(0.0f); };
	const float start = rng.range_float(0.0f, kTau);
	const bool connect = rng.range_int(0, 1) == 1;
	using Span = std::pair<float, float>;
	std::vector<Span> widths = connect ? std::vector<Span>{{0.03f, 0.09f}, {0.14f, 0.25f}, {0.36f, 0.46f}}
	                                   : std::vector<Span>{{0.0f, 0.06f}, {0.12f, 0.23f}, {0.33f, 0.43f}};
	const std::vector<Span> left = widths;
	for (int i = 2; i >= 0; --i) widths.push_back({1.0f - left[(size_t)i].second, 1.0f - left[(size_t)i].first});
	std::vector<std::vector<uint8_t>> strip(widths.size(), std::vector<uint8_t>(N, 0));
	const int chains = (int)std::ceil(scale_f(20.0f, 100.0f, half));
	for (size_t i = 0; i < widths.size(); ++i) {
		const bool player_strip = i == 2 || i == 3;
		const float max_r = std::floor(scale_f(3.0f, connect && player_strip ? 8.0f : 7.0f, half));
		const int circles = (int)std::floor(scale_f(30.0f, 60.0f, half));
		for (int j = 0; j < chains; ++j) {
			const float along = rng.range_float(-half, half);
			const float across = (rng.range_float(widths[i].first, widths[i].second) - 0.5f) * size;
			const auto p = rot(along, across, start);
			const float cx = std::round(p.first), cz = std::round(p.second);
			const auto m = seas_chain(hm, rng, cx, cz, 1, (int)max_r, circles, 1e9f, 0.0f, {}, in_map);
			if (m.empty()) continue; // ChainPlacer starts on the map only
			paint_mask_elevation(hm, m, land, 3.0f);
			mask_or(strip[i], m);
		}
	}
	// playerPlacementLine(startAngle, mapCenter, fractionToTiles(1 - 0.36 - 0.46)) (or 1 - 0.33 - 0.43):
	// player slot i ((i + 1) / (n + 1) - 0.5) of the map along, alternately either side.
	const float line_w = (1.0f - left[2].first - left[2].second) * size;
	const auto sorted = sort_all_players(rng, num_players, teams);
	const auto ids = rng.range_int(0, 1) ? sorted : prime_sort(sorted);
	std::vector<std::pair<float, float>> locs((size_t)num_players);
	for (int i = 0; i < num_players; ++i) {
		const auto p = rot(((float)(i + 1) / (float)(num_players + 1) - 0.5f) * size, line_w * ((float)(i % 2) - 0.5f), start);
		locs[(size_t)ids[(size_t)i]] = {std::round(p.first), std::round(p.second)};
	}
	// "Ensuring player territory": ChainPlacer(1, 6, 40, 1, position, 0, [playerRadius]) at 3 m, blend 4.
	const float player_r = scale_f(15.0f, 20.0f, half);
	std::vector<uint8_t> territory(N, 0);
	for (const auto &l : locs) {
		const auto m = seas_chain(hm, rng, l.first, l.second, 1, 6, 40, 1.0f, 0.0f, {(int)std::floor(player_r)}, in_map);
		if (m.empty()) continue;
		paint_mask_elevation(hm, m, land, 4.0f);
		mask_or(territory, m);
	}
	// clWater: below the water line once the land is in.
	std::vector<uint8_t> water(N);
	for (size_t k = 0; k < N; ++k) water[k] = hm.data[k] < water_h;
	const auto to_water = distance_to_mask(water, hm.res, sp);

	// "Creating gaia": on each outermost strip, scaleByMapSize(1, 8) passes of createObjectGroups
	// of scaleByMapSize(5, 10) groups (retry factor 50) of 8-12 spearmen and 8-12 archers 2-3
	// tiles from the centre and 2-3 elephant archers 4-5 out, every unit on its strip (1 in),
	// 2 from water, off the player territories and 15 from the units of earlier groups. With
	// SimpleGroup's avoidSelf (every unit of the group 1 from the others) and SimpleObject's 30
	// misses, the archers almost never fit in the ring the spearmen already hold, so in 0.28 (and
	// here) the groups nearly always fail and the strips stay empty.
	out.gaia.clear();
	for (size_t side : {(size_t)0, widths.size() - 1}) {
		const auto inside = deep_inside(hm, strip[side], 1.0f);
		auto allowed = [&](float x, float z) {
			const float tx = std::floor(x) + 0.5f, tz = std::floor(z) + 0.5f;
			if (tx * tx + tz * tz >= half * half) return false;
			const size_t k = vertex_at(hm, tx, tz);
			if (!inside[k] || to_water[k] <= 2.0f || territory[k]) return false;
			for (const auto &g : out.gaia)
				if (det::hypot(g.x - tx, g.z - tz) <= 15.0f) return false;
			return true;
		};
		const int passes = (int)std::ceil(scale_f(1.0f, 8.0f, half));
		const int amount = (int)std::ceil(scale_f(5.0f, 10.0f, half));
		struct Obj { int lo, hi; float dmin, dmax; uint8_t kind; };
		static const Obj kObjs[3] = {{8, 12, 2.0f, 3.0f, 0}, {8, 12, 2.0f, 3.0f, 1}, {2, 3, 4.0f, 5.0f, 2}};
		for (int pass = 0; pass < passes; ++pass) {
			int good = 0, bad = 0;
			while (good < amount && bad <= amount * 50) {
				const auto c = random_tile();
				std::vector<RiverArchipelagoLayout::GaiaUnit> group;
				bool ok = true;
				for (const Obj &o : kObjs) {
					// SimpleObject.place: each unit at least 1 from the group's others (avoidSelf), 30 misses allowed.
					int fails = 0;
					// The script's loop rolls randIntInclusive(min, max) again before each unit.
					for (int u = 0; ok && u < rng.range_int(o.lo, o.hi); ++u)
						for (;;) {
							const float b_ = rng.range_float(o.dmin, o.dmax); const float a_ = rng.range_float(0.0f, kTau); const auto d = polar(a_, b_);
							const float x = c.first + d.first, z = c.second + d.second;
							bool free = allowed(x, z);
							for (size_t q = 0; free && q < group.size(); ++q) free = det::hypot(group[q].x - x, group[q].z - z) >= 1.0f;
							if (free) {
								group.push_back({x, z, o.kind});
								break;
							}
							if (fails++ > 30) {
								ok = false;
								break;
							}
						}
					if (!ok) break;
				}
				if (!ok) {
					++bad;
					continue;
				}
				out.gaia.insert(out.gaia.end(), group.begin(), group.end());
				++good;
			}
		}
	}
	std::vector<uint8_t> gaia_mask(N, 0);
	for (const auto &g : out.gaia) gaia_mask[vertex_at(hm, g.x, g.z)] = 1;
	const auto to_gaia = distance_to_mask(gaia_mask, hm.res, sp);

	// "Creating hills": ChainPlacer(1, scaleByMapSize(4, 6), scaleByMapSize(16, 40), 0.1) at 25 m
	// (blend 3), off the territories, 5 from other hills, 1 from the Gaia and 2 from water.
	std::vector<uint8_t> hills(N, 0);
	{
		auto to_hill = distance_to_mask(hills, hm.res, sp);
		const int count = (int)std::ceil(scale_f(1.0f, 5.0f, half));
		auto ok = [&](size_t k) { return !off_map[k] && !territory[k] && to_hill[k] > 5.0f && to_gaia[k] > 1.0f && to_water[k] > 2.0f; };
		create_areas(hm, count, count * 10, 0.1f, random_area_centre,
			[&](float cx, float cz) {
				return seas_chain(hm, rng, cx, cz, 1, (int)std::floor(scale_f(4.0f, 6.0f, half)), (int)std::floor(scale_f(16.0f, 40.0f, half)), 0.1f,
				                  0.0f, {}, [&](int i) { return ok((size_t)i); });
			},
			ok,
			[&](const std::vector<uint8_t> &m) {
				paint_mask_elevation(hm, m, metres(water_h, 25.0f), 3.0f);
				record_area(hm, m, AREA_HILL);
				mask_or(hills, m);
				to_hill = distance_to_mask(hills, hm.res, sp);
			});
	}
	// createBumps(avoidClasses(clPlayer, 8, clWater, 2), scaleByMapSize(20, 150), 2, 8, 4, 1, 4):
	// ChainPlacer(2, 8, 4) areas raised 4 m (blend 2), 8 from the civic centres (clPlayer, a
	// radius 5 disk).
	{
		const auto to_cc = dist_to_points(hm, locs);
		const int count = (int)std::ceil(scale_f(20.0f, 150.0f, half));
		auto ok = [&](size_t k) { return !off_map[k] && to_cc[k] > 5.0f + 8.0f && to_water[k] > 2.0f; };
		create_areas(hm, count, count * 10, 1.0f, random_area_centre,
			[&](float cx, float cz) { return seas_chain(hm, rng, cx, cz, 2, 8, 4, 1.0f, 0.0f, {}, [&](int i) { return ok((size_t)i); }); },
			ok,
			[&](const std::vector<uint8_t> &m) { modify_mask_elevation(hm, m, metres(water_h, 4.0f) - water_h, 2.0f); });
	}
	out.territory = std::move(territory);
	out.hill = std::move(hills);
	finish_spawns(hm, spawns, locs, land, std::floor(player_r), water_h);
}

void RmsGenerator::place_river_archipelago_entities(Simulation &sim, const HeightmapGrid &hm, float water_h, const std::vector<PlayerSpawn> &spawns,
		RmsRng &rng, const RiverArchipelagoLayout &layout) {
	// river_archipelago.js's objects after the bases, in its order: the Gaia Mauryans, the
	// forests and straggler trees, stone and metal mines, deer, boar, tigers and berry bushes
	// (its fish are distribute_fish's). The generic groves, mines and wildlife packs are off
	// for this map; its grass and dirt patches are script_terrains.h's. Left out: the decorative
	// rocks, grass tufts and bushes (actors).
	using namespace misc_maps;
	const float half = hm.map_half;
	const size_t N = hm.data.size();
	const float sp = grid_spacing(hm);
	const int np = std::max(1, (int)spawns.size());
	struct Pt { float x, z; };
	auto clear_of = [](const std::vector<Pt> &v, float x, float z, float d) {
		for (const Pt &p : v)
			if (det::hypot(p.x - x, p.z - z) <= d) return false;
		return true;
	};
	std::vector<uint8_t> water(N);
	for (size_t k = 0; k < N; ++k) water[k] = hm.data[k] < water_h;
	const auto to_water = distance_to_mask(water, hm.res, sp);
	const auto to_hill = distance_to_mask(layout.hill, hm.res, sp);
	constexpr size_t kOff = (size_t)-1;
	auto vertex = [&](float x, float z) { return x * x + z * z < half * half ? vertex_at(hm, x, z) : kOff; };
	auto random_tile = [&]() {
		const auto c = random_in_circle(rng, half - 3.0f);
		return Pt{std::floor(c.first) + 0.5f, std::floor(c.second) + 0.5f};
	};
	auto cc_clear = [&](float x, float z, float d) { // avoidClasses(clPlayer, d): the civic centre's radius 5 disk
		for (const auto &s : spawns)
			if (det::hypot(s.x - x, s.z - z) <= 5.0f + d) return false;
		return true;
	};
	// The bases' starting resources (clBaseResource): all there is before this pass.
	std::vector<Pt> base_res;
	{
		const auto rv = sim.resources();
		for (size_t i = 0; i < rv.count; ++i)
			if (rv.alive[i]) base_res.push_back({rv.x[i], rv.z[i]});
	}

	// The Gaia groups synth_river_archipelago placed (clGaia).
	static const char *const kGaia[3] = {"maur/infantry_spearman_b", "maur/infantry_archer_b", "maur/elephant_archer_b"};
	std::vector<Pt> gaia;
	std::vector<uint8_t> gaia_mask(N, 0);
	for (const auto &g : layout.gaia) {
		const uint16_t t = civ_unit_template_by_id(kGaia[g.kind]);
		if (t != CIV_UNIT_TEMPLATE_NONE) sim.spawn_gaia_unit(g.x, g.z, (UnitType)civ_unit_template(t).unit_type, kGaia[g.kind]);
		gaia.push_back({g.x, g.z});
		gaia_mask[vertex_at(hm, g.x, g.z)] = 1;
	}
	const auto to_gaia = distance_to_mask(gaia_mask, hm.res, sp);

	// "Creating forests": getTreeCounts(1000, 4000, 0.7); three kinds (toona and the two palms,
	// which plant_flora tells apart), each of floor(forestTrees / (scaleByMapSize(3, 6) *
	// numPlayers) / 3) ChainPlacer(1, scaleByMapSize(3, 5), forestTrees / (num *
	// scaleByMapSize(2, 4)), 0.5) areas 12 from the civic centres, 6 from other forests, off
	// hills, 1 from the Gaia and 2 from water. LayeredPainter([[tGrass x4, pForest],
	// [tGrass x3, pForest]], [2]) with pForest [floor + tree, floor]: a tree on one tile in 10
	// of the outer 2 tiles, one in 8 inside.
	const float total_trees = scale_f(1000.0f, 4000.0f, half);
	const float forest_trees = 0.7f * total_trees, straggler_trees = 0.3f * total_trees;
	std::vector<uint8_t> forest(N, 0);
	auto to_forest = distance_to_mask(forest, hm.res, sp);
	{
		const float forest_size = forest_trees / (scale_f(3.0f, 6.0f, half) * (float)np);
		const int num = (int)std::floor(forest_size / 3.0f);
		const int circles = num > 0 ? (int)std::floor(forest_trees / ((float)num * std::floor(scale_f(2.0f, 4.0f, half)))) : 0;
		const float max_r = std::floor(scale_f(3.0f, 5.0f, half));
		std::vector<std::pair<float, float>> ccs;
		for (const auto &s : spawns) ccs.push_back({s.x, s.z});
		const auto to_cc = dist_to_points(hm, ccs);
		auto ok = [&](size_t k) {
			float wx, wz;
			hm.grid_to_world((int)(k % (size_t)hm.res), (int)(k / (size_t)hm.res), wx, wz);
			return wx * wx + wz * wz < half * half && to_cc[k] > 5.0f + 12.0f && to_forest[k] > 6.0f && !layout.hill[k] && to_gaia[k] > 1.0f &&
			       to_water[k] > 2.0f;
		};
		for (int kind = 0; kind < 3 && num > 0; ++kind)
			create_areas(hm, num, num * 10, 0.5f,
				[&]() {
					const auto c = random_in_circle(rng, half); // createAreas: randomCoordinate(false)
					return std::make_pair(std::floor(c.first) + 0.5f, std::floor(c.second) + 0.5f);
				},
				[&](float cx, float cz) {
					return seas_chain(hm, rng, cx, cz, 1, (int)max_r, std::max(1, circles), 0.5f, 0.0f, {}, [&](int i) { return ok((size_t)i); });
				},
				ok,
				[&](const std::vector<uint8_t> &m) {
					const auto in = dist_inside(hm, m);
					int x0 = hm.res, x1 = -1, z0 = hm.res, z1 = -1;
					for (size_t k = 0; k < N; ++k)
						if (m[k]) {
							const int x = (int)(k % (size_t)hm.res), z = (int)(k / (size_t)hm.res);
							x0 = std::min(x0, x);
							x1 = std::max(x1, x);
							z0 = std::min(z0, z);
							z1 = std::max(z1, z);
						}
					const float lo_x = std::floor(-half + ((float)x0 - 0.5f) * sp), hi_x = -half + ((float)x1 + 0.5f) * sp;
					const float lo_z = std::floor(-half + ((float)z0 - 0.5f) * sp), hi_z = -half + ((float)z1 + 0.5f) * sp;
					for (float z = lo_z + 0.5f; z < hi_z; z += 1.0f)
						for (float x = lo_x + 0.5f; x < hi_x; x += 1.0f) {
							const size_t k = vertex(x, z);
							if (k == kOff || !m[k]) continue;
							if (rng.next_float() < (in[k] <= 2.0f ? 0.1f : 0.125f)) sim.spawn_resource_world(x, z, RES_WOOD, TREE_WOOD_SUPPLY);
						}
					mask_or(forest, m);
					to_forest = distance_to_mask(forest, hm.res, sp);
				});
	}
	// createStragglerTrees([toona, palm_tropic, palm_tropical], ...): createObjectGroupsDeprecated
	// of SimpleObject(tree, 1, 1, 0, 3), floor(stragglerTrees / 3) tries of each (a miss counts as
	// placed), 5 from water, 1 from forests and other stragglers, 1 from hills, 8 from the civic
	// centres, 4 from the base resources and 1 from the Gaia.
	std::vector<Pt> stragglers;
	{
		const int each = (int)std::floor(straggler_trees / 3.0f);
		for (int kind = 0; kind < 3; ++kind)
			for (int t = 0; t < each; ++t) {
				const Pt c = random_tile();
				for (int fails = 0; fails <= 31; ++fails) {
					const float b_ = rng.range_float(0.0f, 3.0f); const float a_ = rng.range_float(0.0f, kTau); const auto d = polar(a_, b_);
					const Pt p{c.x + d.first, c.z + d.second};
					const float tx = std::floor(p.x) + 0.5f, tz = std::floor(p.z) + 0.5f;
					const size_t k = vertex(tx, tz);
					if (k == kOff || to_water[k] <= 5.0f || to_forest[k] <= 1.0f || to_hill[k] <= 1.0f || !cc_clear(tx, tz, 8.0f) ||
					    !clear_of(base_res, tx, tz, 4.0f) || !clear_of(gaia, tx, tz, 1.0f) || !clear_of(stragglers, tx, tz, 1.0f)) continue;
					sim.spawn_resource_world(p.x, p.z, RES_WOOD, TREE_WOOD_SUPPLY);
					stragglers.push_back(p);
					break;
				}
			}
	}
	auto forest_clear = [&](size_t k, float x, float z, float d) { return to_forest[k] > d && clear_of(stragglers, x, z, d); };

	// createObjectGroups of a SimpleGroup with avoidSelf: `amount` groups (retry factor `retry`)
	// round random tiles, each object's entities dmin-dmax from the centre, all the group's 1
	// apart, every one on a tile `ok` allows (SimpleObject.place: 30 misses, then the group fails).
	struct Obj { int lo, hi; float dmin, dmax; };
	auto groups = [&](int amount, int retry, std::initializer_list<Obj> objs, auto ok, auto spawn) {
		int good = 0, bad = 0;
		while (good < amount && bad <= amount * retry) {
			const Pt c = random_tile();
			std::vector<std::pair<Pt, int>> placed;
			bool fit = true;
			int oi = 0;
			for (const Obj &o : objs) {
				int fails = 0;
				// SimpleObject.place rolls randIntInclusive(min, max) again before each entity.
				for (int u = 0; fit && u < rng.range_int(o.lo, o.hi); ++u)
					for (;;) {
						const float b_ = rng.range_float(o.dmin, o.dmax); const float a_ = rng.range_float(0.0f, kTau); const auto d = polar(a_, b_);
						const Pt p{c.x + d.first, c.z + d.second};
						const float tx = std::floor(p.x) + 0.5f, tz = std::floor(p.z) + 0.5f;
						const size_t k = vertex(tx, tz);
						bool free = k != kOff && ok(k, tx, tz);
						for (size_t q = 0; free && q < placed.size(); ++q)
							free = det::hypot(placed[q].first.x - p.x, placed[q].first.z - p.z) >= 1.0f;
						if (free) {
							placed.push_back({p, oi});
							break;
						}
						if (fails++ > 30) {
							fit = false;
							break;
						}
					}
				if (!fit) break;
				++oi;
			}
			if (!fit) {
				++bad;
				continue;
			}
			for (const auto &[p, o] : placed) spawn(p, o);
			++good;
		}
	};
	auto land_ok = [&](size_t k, float x, float z, float water_d) {
		return to_water[k] > water_d && forest_clear(k, x, z, 1.0f) && !layout.territory[k] && to_gaia[k] > 1.0f && to_hill[k] > 1.0f;
	};
	// Stone: scaleByMapSize(4, 16) groups of 0-2 small stones and a large one (0-4 out), 3 from
	// water and 10 from other rocks; small stone: scaleByMapSize(2, 8) groups of 2-5 small stones
	// (1-3 out), 4 from water; metal: scaleByMapSize(4, 16) large metal mines, 4 from water, 10
	// from other metal and 5 from rocks. Each 1 from forests, hills and the Gaia, off the
	// territories, retry factor 50.
	std::vector<Pt> rock, metal, food;
	auto stone = [&](int small_obj) {
		return [&, small_obj](Pt p, int o) {
			sim.spawn_resource_world(p.x, p.z, RES_STONE, o == small_obj ? MINE_SUPPLY : LARGE_MINE_SUPPLY);
			rock.push_back(p);
		};
	};
	groups((int)std::ceil(scale_f(4.0f, 16.0f, half)), 50, {Obj{0, 2, 0.0f, 4.0f}, Obj{1, 1, 0.0f, 4.0f}},
		[&](size_t k, float x, float z) { return land_ok(k, x, z, 3.0f) && clear_of(rock, x, z, 10.0f); }, stone(0));
	groups((int)std::ceil(scale_f(2.0f, 8.0f, half)), 50, {Obj{2, 5, 1.0f, 3.0f}},
		[&](size_t k, float x, float z) { return land_ok(k, x, z, 4.0f) && clear_of(rock, x, z, 10.0f); }, stone(0));
	groups((int)std::ceil(scale_f(4.0f, 16.0f, half)), 50, {Obj{1, 1, 0.0f, 4.0f}},
		[&](size_t k, float x, float z) { return land_ok(k, x, z, 4.0f) && clear_of(metal, x, z, 10.0f) && clear_of(rock, x, z, 5.0f); },
		[&](Pt p, int) {
			sim.spawn_resource_world(p.x, p.z, RES_GOLD, LARGE_MINE_SUPPLY);
			metal.push_back(p);
		});
	// Deer: 3 x numPlayers groups of 5-7; boar: 2 x numPlayers groups of 2-4; tigers: 2 x
	// numPlayers; berry bushes: randIntInclusive(1, 4) x numPlayers groups of 5-7 (all 0-4 out,
	// retry factor 20). Each 4 from water, off forests and the territories, 1 from the Gaia and
	// hills, 20 from other food (10 for the bushes); all but the deer 4 from the mines. A
	// group's own animals join clFood once it stands.
	auto hunt_ok = [&](float food_d, bool mines) {
		return [&, food_d, mines](size_t k, float x, float z) {
			return to_water[k] > 4.0f && forest_clear(k, x, z, 0.5f) && !layout.territory[k] && to_gaia[k] > 1.0f && to_hill[k] > 1.0f &&
			       clear_of(food, x, z, food_d) && (!mines || (clear_of(rock, x, z, 4.0f) && clear_of(metal, x, z, 4.0f)));
		};
	};
	auto animal = [&](uint8_t ftype) {
		return [&, ftype](Pt p, int) {
			sim.spawn_fauna(p.x, p.z, ftype);
			food.push_back(p);
		};
	};
	groups(3 * np, 20, {Obj{5, 7, 0.0f, 4.0f}}, hunt_ok(20.0f, false), animal(FAUNA_DEER));
	groups(2 * np, 20, {Obj{2, 4, 0.0f, 4.0f}}, hunt_ok(20.0f, true), animal(FAUNA_BOAR));
	groups(2 * np, 20, {Obj{1, 1, 0.0f, 4.0f}}, hunt_ok(20.0f, true), animal(FAUNA_TIGER));
	groups(rng.range_int(1, 4) * np, 20, {Obj{5, 7, 0.0f, 4.0f}}, hunt_ok(10.0f, true), [&](Pt p, int) {
		sim.spawn_resource_world(p.x, p.z, RES_FOOD, 200); // gaia/fruit/berry_01
		food.push_back(p);
	});
}

namespace {
// Eases the heights along a route of navcells (cell indices in res x res) wherever it crosses one
// steeper than land units may (LAND_MAX_TERRAIN_SLOPE): each steep stretch, and as much of the
// route on either side as a ramp of half that slope needs, gets heights falling evenly from one
// end to the other, flat across a corridor 2.5 units each side of the route.
void ease_route(HeightmapGrid &hm, const NavGrid &g, const std::vector<float> &slope, const std::vector<int32_t> &route) {
	const int res = g.res;
	const float cell = 2.0f * hm.map_half / (float)res;
	constexpr float RAMP = 0.5f * LAND_MAX_TERRAIN_SLOPE; // along the route, leaving room for its corners
	const int m = (int)route.size();
	std::vector<float> cx(m), cz(m), h(m), along(m);
	for (int i = 0; i < m; ++i) {
		cx[i] = ((float)(route[i] % res) + 0.5f) * cell - hm.map_half;
		cz[i] = ((float)(route[i] / res) + 0.5f) * cell - hm.map_half;
		h[i] = hm.sample_world(cx[i], cz[i]);
		along[i] = (float)i * cell;
	}
	for (int i = 0; i < m;) {
		if (!(slope[route[i]] > LAND_MAX_TERRAIN_SLOPE)) { ++i; continue; }
		int a = i, c = i;
		while (c + 1 < m && (slope[route[c + 1]] > LAND_MAX_TERRAIN_SLOPE ||
		                     (c + 3 < m && slope[route[c + 3]] > LAND_MAX_TERRAIN_SLOPE)))
			++c;
		a = std::max(0, a - 1);
		c = std::min(m - 1, c + 1);
		while (std::fabs(h[c] - h[a]) > RAMP * (along[c] - along[a]) && (a > 0 || c < m - 1)) {
			a = std::max(0, a - 1);
			c = std::min(m - 1, c + 1);
		}
		const float full = 1.25f * cell, edge = 2.5f * cell;
		const float x0 = *std::min_element(cx.begin() + a, cx.begin() + c + 1) - edge;
		const float x1 = *std::max_element(cx.begin() + a, cx.begin() + c + 1) + edge;
		const float z0 = *std::min_element(cz.begin() + a, cz.begin() + c + 1) - edge;
		const float z1 = *std::max_element(cz.begin() + a, cz.begin() + c + 1) + edge;
		const float step = 2.0f * hm.map_half / (float)(hm.res - 1);
		const int vx0 = std::max(0, (int)std::floor((x0 + hm.map_half) / step)), vx1 = std::min(hm.res - 1, (int)std::ceil((x1 + hm.map_half) / step));
		const int vz0 = std::max(0, (int)std::floor((z0 + hm.map_half) / step)), vz1 = std::min(hm.res - 1, (int)std::ceil((z1 + hm.map_half) / step));
		const float span = std::max(along[c] - along[a], 1e-3f);
		for (int vz = vz0; vz <= vz1; ++vz)
			for (int vx = vx0; vx <= vx1; ++vx) {
				const float wx = -hm.map_half + (float)vx * step, wz = -hm.map_half + (float)vz * step;
				int near = -1;
				float nd = edge;
				for (int k = a; k <= c; ++k) {
					const float d = det::hypot(wx - cx[k], wz - cz[k]);
					if (d < nd) { nd = d; near = k; }
				}
				if (near < 0) continue;
				const float target = h[a] + (h[c] - h[a]) * (along[near] - along[a]) / span;
				const float t = nd <= full ? 1.0f : 1.0f - smoothstep(full, edge, nd);
				const float cur = hm.get(vx, vz);
				hm.set(vx, vz, cur + (target - cur) * t);
			}
		i = c + 1;
	}
}

// RTS0's safety net for MaxTerrainSlope, which every land unit obeys: a port's hills, ridges or
// bumps can come out steeper or wider than the 0 A.D. script makes them and wall a base off where
// the script leaves a way through. Where the gentlest land route from the first base to another
// crosses a navcell too steep, ease_route turns that stretch into a ramp. Water stays as it is: a
// base the sea cuts off stays on its island.
void ease_routes_between_bases(HeightmapGrid &hm, const std::vector<RmsGenerator::PlayerSpawn> &spawns, float water_h) {
	if (spawns.size() < 2) return;
	const int res = Simulation::grid_res_for_map(hm.map_half);
	constexpr float NONE = std::numeric_limits<float>::infinity();
	for (int round = 0; round < 8; ++round) {
		NavGrid g;
		g.init(hm.map_half, res);
		g.set_water_from_heights(hm.data, hm.res, water_h);
		if (!g.any_steep()) return;
		std::vector<float> slope((size_t)res * (size_t)res);
		for (int y = 0; y < res; ++y)
			for (int x = 0; x < res; ++x) slope[(size_t)y * res + x] = g.compute_cell_slope(x, y, hm.data, hm.res);
		// The route whose steepest navcell is least steep (a bottleneck Dijkstra over land
		// navcells). (slope, cell) keys are all distinct, so every standard library's heap pops
		// them in the same order.
		std::vector<float> best((size_t)res * (size_t)res, NONE);
		std::vector<int32_t> prev((size_t)res * (size_t)res, -1);
		using Key = std::pair<float, int32_t>;
		std::priority_queue<Key, std::vector<Key>, std::greater<Key>> open;
		const auto [sx, sy] = g.world_to_cell(spawns[0].x, spawns[0].z);
		best[(size_t)sy * res + sx] = 0.0f;
		open.push({0.0f, sy * res + sx});
		while (!open.empty()) {
			const auto [d, k] = open.top();
			open.pop();
			if (d > best[(size_t)k]) continue;
			const int x = k % res, y = k / res;
			static const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
			for (const auto &dir : dirs) {
				const int nx = x + dir[0], ny = y + dir[1];
				if (!g.cells.inside(nx, ny)) continue;
				const uint8_t c = g.cells.at(nx, ny);
				if (c != NAV_PASSABLE && c != NAV_SHALLOW) continue;
				const int32_t n = ny * res + nx;
				const float nd = std::max(d, slope[(size_t)n]);
				if (nd < best[(size_t)n]) {
					best[(size_t)n] = nd;
					prev[(size_t)n] = k;
					open.push({nd, n});
				}
			}
		}
		bool eased = false;
		for (size_t b = 1; b < spawns.size(); ++b) {
			const auto [bx, by] = g.world_to_cell(spawns[b].x, spawns[b].z);
			const int32_t t = by * res + bx;
			if (best[(size_t)t] == NONE || !(best[(size_t)t] > LAND_MAX_TERRAIN_SLOPE)) continue;
			std::vector<int32_t> route;
			for (int32_t k = t; k >= 0; k = prev[(size_t)k]) route.push_back(k);
			ease_route(hm, g, slope, route);
			eased = true;
		}
		if (!eased) return;
	}
}
} // namespace

// =========================================================================
// Main RmsGenerator Entry Point
// =========================================================================

// =========================================================================
// 0 A.D. maps/random/wall_demo.js (0.28): RandomMap(0, "grass1") and Gaia's
// walls in rmgen-common wall_builder.js's every placer, one row of each
// across the map for each of g_WallStyles' styles (wall_builder.h): placeWall,
// placeFortress("tiny"), placeGenericFortress, placeCircularWall,
// placePolygonalWall, placeIrregularPolygonalWall and placeLinearWall, an
// obelisk at the centre of each of the middle five. No player entities.
// =========================================================================
static void place_wall_demo(Simulation &sim, float map_half, RmsRng &rng) {
	const double size = 2.0 * map_half; // g_Map.getSize()
	const double border = 5.0, other = 10.0; // distToMapBorder, distToOtherWalls
	const double buildable = size - 2.0 * border;
	const int n = WALL_STYLE_COUNT; // wallStyleList.length
	const double pi = kWallPi<double>;
	double py = border; // position.y (position.x stays distToMapBorder)
	// rmgen tile coordinates to RTS0 world coordinates.
	auto wx = [&](double x) { return x - map_half; };
	const uint8_t gaia = GAIA_PLAYER; // playerID 0
	const uint8_t obelisk = scenery_type_by_name("obelisk");
	auto place_obelisk = [&](double x, double y, double angle) { // placeEntityPassable("structures/obelisk", ...)
		if (sim.wall_builder_passable(wx(x), wx(y))) sim.spawn_scenery((float)wx(x), (float)wx(y), obelisk, (float)(pi / 2.0 - angle), 0.25f);
	};
	auto column = [&](int style) { return border + style * buildable / n; };

	// Custom wall placement (placeWall), bending elements and structures included.
	const std::vector<WallElementName> custom = wall_element_names({"start", "long", "tower", "tower", "tower", "medium",
			"outpost", "medium", "cornerOut", "medium", "cornerIn", "medium", "house", "end", "entryTower", "start", "short",
			"barracks", "gate", "tower", "medium", "fort", "medium", "end"});
	for (int s = 0; s < n; ++s)
		sim.place_wall(wx(column(s)), wx(py), custom, (uint8_t)s, gaia, pi / 16.0 * det::sin(s * pi / 4.0));
	py += 80.0 + other;

	// Default fortress placement (placeFortress "tiny"), centred on an obelisk.
	const double fortress_r = 15.0;
	for (int s = 0; s < n; ++s) {
		const double o = s * pi / 32.0, x = column(s) + fortress_r, y = py + fortress_r;
		place_obelisk(x, y, o);
		sim.place_fortress(wx(x), wx(y), "tiny", (uint8_t)s, gaia, o);
	}
	py += 2.0 * fortress_r + other;

	// 'Generic' fortress placement (placeGenericFortress, the Iberian wall circuit).
	double radius = std::min((size - py - other) / 3.0, (buildable / n - other) / 2.0);
	for (int s = 0; s < n; ++s) {
		const double x = column(s) + radius, y = py + radius;
		place_obelisk(x, y, 0.0);
		sim.place_generic_fortress(gaia, (float)wx(x), (float)wx(y), (float)radius, rng.next_u32(), 0.5f, 3, 100, (uint8_t)s);
	}
	py += 2.0 * radius + other;

	// Circular wall placement: full circles and arcs of a half and three quarters.
	radius = std::min((size - py - other) / 3.0, (buildable / n - other) / 2.0);
	const std::vector<WallElementName> circle_part = wall_element_names({"tower", "medium", "house"});
	for (int s = 0; s < n; ++s) {
		const double x = column(s) + radius, y = py + radius, o = s * pi / 16.0;
		const double max_angle = pi / 2.0 * (s % 3 + 2);
		place_obelisk(x, y, o);
		sim.place_circular_wall(wx(x), wx(y), radius, circle_part, (uint8_t)s, gaia, o, max_angle);
	}
	py += 2.0 * radius + other;

	// Regular polygonal wall placement, 3 to 8 corners, the first side left open.
	radius = std::min((size - py - other) / 2.0, (buildable / n - other) / 2.0);
	const std::vector<WallElementName> polygon_part = wall_element_names({"medium", "tower"});
	const WallElementName corner = wall_element_name("tower");
	for (int s = 0; s < n; ++s) {
		const double x = column(s) + radius, y = py + radius, o = s * pi / 16.0;
		place_obelisk(x, y, o);
		sim.place_polygonal_wall(wx(x), wx(y), radius, polygon_part, corner, (uint8_t)s, gaia, o, s % 6 + 3, true);
	}
	py += 2.0 * radius + other;

	// Irregular polygonal wall placement (irregularity 0.5), the first side left open.
	radius = std::min((size - py - other) / 2.0, (buildable / n - other) / 2.0);
	for (int s = 0; s < n; ++s) {
		const double x = column(s) + radius, y = py + radius, o = s * pi / 16.0;
		place_obelisk(x, y, o);
		sim.place_irregular_polygonal_wall(wx(x), wx(y), radius, corner, (uint8_t)s, gaia, o, s % 6 + 3, 0.5,
				true, rng.next_u32());
	}
	py += 2.0 * radius + other;

	// Linear wall placement: numWallsPerStyle straight walls a style, each longer than the last.
	const double max_length = size - py - border - other;
	const int per_style = (int)std::floor(buildable / other / n);
	const std::vector<WallElementName> line_part = wall_element_names({"tower", "medium"});
	for (int s = 0; s < n; ++s)
		for (int w = 0; w < per_style; ++w) {
			const double x = border + (s * per_style + w) * buildable / n / per_style;
			const double y1 = py + (w + 1) * max_length / per_style;
			sim.place_linear_wall(wx(x), wx(py), wx(x), wx(y1), line_part, (uint8_t)s, gaia);
		}
}

// =========================================================================
// 0 A.D. rmgen heightmap.js (0.28) on a HeightmapGrid, in metres: setBaseTerrainDiamondSquare,
// splashErodeMap, rescaleHeightmap and getStartLocationsByHeightmap, for the scripts that build
// their land from them (caledonian_meadows.js).
// =========================================================================
namespace {

// setBaseTerrainDiamondSquare(lo, hi, init, smoothness): the seed grid doubled until it covers
// the grid, each new vertex the mean of its old neighbours plus a random offset scaled by where
// that mean lies in [lo, hi] (low ground stays flat, high ground grows rough); the offset
// shrinks by 2^smoothness a round. Then the middle is cut out.
void diamond_square_028(HeightmapGrid &hm, RmsRng &rng, float lo, float hi, std::vector<std::vector<float>> cur, float smoothness) {
	const float range = hi - lo;
	float offset = range / 2.0f;
	std::vector<std::vector<float>> next;
	auto rough = [&](float v) { return v + (v - lo) / range * rng.range_float(-offset, offset); };
	while ((int)cur.size() < hm.res) {
		const int ow = (int)cur.size(), n = 2 * ow - 1;
		next.assign((size_t)n, std::vector<float>((size_t)n, 0.0f));
		std::vector<std::vector<uint8_t>> set((size_t)n, std::vector<uint8_t>((size_t)n, 0));
		for (int x = 0; x < n; ++x) // square
			for (int y = 0; y < n; ++y) {
				if (x % 2 == 0 && y % 2 == 0) {
					next[x][y] = cur[x / 2][y / 2];
				} else if (x % 2 == 1 && y % 2 == 1) {
					next[x][y] = rough((cur[(x - 1) / 2][(y - 1) / 2] + cur[(x + 1) / 2][(y - 1) / 2] +
							cur[(x - 1) / 2][(y + 1) / 2] + cur[(x + 1) / 2][(y + 1) / 2]) / 4.0f);
				} else {
					continue;
				}
				set[x][y] = 1;
			}
		for (int x = 0; x < n; ++x) // diamond, with heightmap.js's border cases as written
			for (int y = 0; y < n; ++y) {
				if (set[x][y]) continue;
				float v;
				if (x > 0 && x + 1 < n - 1 && y > 0 && y + 1 < n - 1)
					v = (next[x + 1][y] + next[x][y + 1] + next[x - 1][y] + next[x][y - 1]) / 4.0f;
				else if (x < n - 1 && y > 0 && y < n - 1) // left border
					v = (next[x + 1][y] + next[x][y + 1] + next[x][y - 1]) / 3.0f;
				else if (x > 0 && y > 0 && y < n - 1) // right border
					v = (next[x][y + 1] + next[x - 1][y] + next[x][y - 1]) / 3.0f;
				else if (x > 0 && x < n - 1 && y < n - 1) // bottom border
					v = (next[x + 1][y] + next[x][y + 1] + next[x - 1][y]) / 3.0f;
				else if (x > 0 && x < n - 1 && y > 0) // top border
					v = (next[x + 1][y] + next[x - 1][y] + next[x][y - 1]) / 3.0f;
				else
					continue;
				next[x][y] = rough(v);
			}
		cur.swap(next);
		offset /= det::pow(2.0f, smoothness);
	}
	const int shift = ((int)cur.size() - hm.res) / 2;
	for (int z = 0; z < hm.res; ++z)
		for (int x = 0; x < hm.res; ++x) hm.set(x, z, cur[(size_t)(x + shift)][(size_t)(z + shift)]);
}

// splashErodeMap(strength): each vertex, in turn, drains toward its downhill neighbours (the
// gradients taken once, the map wrapped round at its edges), at most `strength` in all.
void splash_erode_028(HeightmapGrid &hm, float strength) {
	const int n = hm.res;
	auto at = [&](int x, int y) -> float & { return hm.data[(size_t)y * (size_t)n + (size_t)x]; };
	std::vector<float> gx((size_t)n * (size_t)n), gy((size_t)n * (size_t)n);
	for (int x = 0; x < n; ++x)
		for (int y = 0; y < n; ++y) {
			gx[(size_t)x * n + y] = at((x + 1) % n, y) - at(x, y);
			gy[(size_t)x * n + y] = at(x, (y + 1) % n) - at(x, y);
		}
	for (int x = 0; x < n; ++x) {
		const int nx = (x + 1) % n, px = (x + n - 1) % n;
		for (int y = 0; y < n; ++y) {
			const int ny = (y + 1) % n, py = (y + n - 1) % n;
			const float slopes[4] = {-gx[(size_t)x * n + y], -gy[(size_t)x * n + y], gx[(size_t)px * n + y], gy[(size_t)x * n + py]};
			float sum = 0.0f;
			for (float s : slopes) if (s > 0.0f) sum += s;
			float drain[4] = {0.0f, 0.0f, 0.0f, 0.0f}, total = 0.0f;
			for (int i = 0; i < 4; ++i)
				if (slopes[i] > 0.0f) { drain[i] = std::min(strength * slopes[i] / sum, slopes[i]); total += drain[i]; }
			at(x, y) -= total;
			at(nx, y) += drain[0];
			at(x, ny) += drain[1];
			at(px, y) += drain[2];
			at(x, py) += drain[3];
		}
	}
}

// rescaleHeightmap(lo, hi).
void rescale_028(HeightmapGrid &hm, float lo, float hi) {
	const auto [mn, mx] = std::minmax_element(hm.data.begin(), hm.data.end());
	const float a = *mn, b = *mx;
	for (float &h : hm.data) h = lo + (h - a) / std::max(b - a, 1e-6f) * (hi - lo);
}

// SmoothingPainter(size, strength, iterations) over the whole map (MapBoundsPlacer), or over the
// vertices in `only`: each vertex, in turn and in place, moves `strength` of the way to the mean
// of the heights in a box `size` tiles out, weighted by 1 / Manhattan distance.
void smoothing_painter_028(HeightmapGrid &hm, float size, float strength, int iterations, const std::vector<uint8_t> *only = nullptr) {
	const int n = hm.res, brush = std::max(1, (int)std::lround(std::floor(size) / grid_spacing(hm)));
	for (int it = 0; it < iterations; ++it)
		for (int x = 0; x < n; ++x)
			for (int z = 0; z < n; ++z) {
				if (only && !(*only)[(size_t)z * (size_t)n + (size_t)x]) continue;
				float sum = 0.0f, wsum = 0.0f;
				for (int dx = -brush; dx <= brush; ++dx)
					for (int dz = -brush; dz <= brush; ++dz) {
						const int px = x + dx, pz = z + dz, d = std::abs(dx) + std::abs(dz);
						if (!d || px < 0 || pz < 0 || px >= n || pz >= n) continue;
						sum += hm.get(px, pz) / (float)d;
						wsum += 1.0f / (float)d;
					}
				hm.set(x, z, strength * sum / wsum + (1.0f - strength) * hm.get(x, z));
			}
}

// getStartLocationsByHeightmap({lo, hi}, 1000, border): of 1000 random draws of a tile for each
// player among those in the height band (border tiles from the edge, and from the circle's edge
// on a circular map), the draw whose closest two are farthest apart; sorted round the map centre
// (groupPlayersCycle). Empty when no tile qualifies.
std::vector<std::pair<int, int>> start_locations_028(const HeightmapGrid &hm, RmsRng &rng, float lo, float hi, int num_players, int border) {
	const float half = hm.map_half;
	const int tiles = (int)std::lround(2.0f * half);
	std::vector<std::pair<int, int>> valid;
	for (int x = border; x < tiles - border; ++x)
		for (int y = border; y < tiles - border; ++y) {
			const float h = hm.sample_world((float)x - half, (float)y - half);
			if (t_circular_map && !(det::hypot((float)x - half, (float)y - half) < half - (float)border)) continue;
			if (h >= lo && h <= hi) valid.push_back({x, y});
		}
	std::vector<std::pair<int, int>> best;
	float best_d = 0.0f;
	for (int t = 0; t < 1000 && !valid.empty(); ++t) {
		std::vector<std::pair<int, int>> draw;
		for (int p = 0; p < num_players; ++p) draw.push_back(valid[rng.next_u32() % (uint32_t)valid.size()]);
		float d = std::numeric_limits<float>::infinity();
		for (int a = 0; a + 1 < num_players; ++a)
			for (int b = a + 1; b < num_players; ++b)
				d = std::min(d, det::hypot((float)(draw[a].first - draw[b].first), (float)(draw[a].second - draw[b].second)));
		if (d > best_d) { best_d = d; best = draw; }
	}
	if ((int)best.size() < num_players) return {};
	std::sort(best.begin(), best.end(), [&](const auto &a, const auto &b) {
		return det::atan2((float)a.second - half, (float)a.first - half) < det::atan2((float)b.second - half, (float)b.first - half);
	});
	return best;
}

// The spawns at those tiles, facing the centre, each base's disk of `radius` tiles set to its
// own height over a `blend`-tile edge (SmoothElevationPainter(ELEVATION_SET, height, blend)).
void smoothed_start_spawns_028(HeightmapGrid &hm, std::vector<RmsGenerator::PlayerSpawn> &spawns,
		const std::vector<std::pair<int, int>> &starts, float radius, float blend) {
	const float half = hm.map_half;
	spawns.clear();
	for (size_t i = 0; i < starts.size(); ++i) {
		const float sx = (float)starts[i].first - half, sz = (float)starts[i].second - half, l = std::max(1e-3f, det::hypot(sx, sz));
		const float bh = hm.sample_world(sx, sz);
		spawns.push_back({(uint8_t)i, sx, sz, bh, -sx / l, -sz / l});
		std::vector<uint8_t> disk(hm.data.size(), 0);
		for (int z = 0; z < hm.res; ++z)
			for (int x = 0; x < hm.res; ++x) {
				float wx, wz;
				hm.grid_to_world(x, z, wx, wz);
				disk[(size_t)z * (size_t)hm.res + (size_t)x] = det::hypot(wx - sx, wz - sz) <= radius;
			}
		paint_mask_elevation(hm, disk, bh, blend);
	}
}

// playerPlacementArcs(sortAllPlayers(), centre, r, map_angle, a0, a1): partitionPlayers splits the
// players into two sides (each team on one side, filled greedily, the largest team first; the
// players without a team alone; a single team in half), the east side spread evenly between
// map_angle + a0 and map_angle + a1 (playerPlacementArc, both ends left out), the west side between
// map_angle - a0 and map_angle - a1, rounded to tiles.
std::vector<std::pair<float, float>> placement_arcs_028(const HeightmapGrid &hm, RmsRng &rng, int n, const uint8_t *teams,
		float r, float map_angle, float a0, float a1) {
	const float half = hm.map_half;
	const auto ids = misc_maps::sort_all_players(rng, n, teams);
	std::vector<int> team_ids;
	std::vector<std::vector<int>> groups;
	for (int id : ids) {
		const int t = teams ? teams[id] : 0;
		const auto it = std::find(team_ids.begin(), team_ids.end(), t);
		if (it == team_ids.end()) {
			team_ids.push_back(t);
			groups.push_back({id});
		} else {
			groups[(size_t)(it - team_ids.begin())].push_back(id);
		}
	}
	const auto none = std::find(team_ids.begin(), team_ids.end(), 0);
	if (none != team_ids.end()) {
		const size_t k = (size_t)(none - team_ids.begin());
		const std::vector<int> alone = groups[k];
		groups.erase(groups.begin() + (long)k);
		for (int id : alone) groups.push_back({id});
	}
	if (groups.size() == 1) {
		const std::vector<int> all = groups[0];
		const long mid = (long)all.size() / 2;
		groups = {std::vector<int>(all.begin() + mid, all.end()), std::vector<int>(all.begin(), all.begin() + mid)};
	}
	std::stable_sort(groups.begin(), groups.end(), [](const auto &a, const auto &b) { return a.size() > b.size(); });
	std::vector<int> east, west;
	for (const auto &g : groups) {
		auto &side = east.size() > west.size() ? west : east;
		side.insert(side.end(), g.begin(), g.end());
	}
	std::vector<std::pair<float, float>> locs((size_t)n);
	auto arc = [&](const std::vector<int> &side, float from, float to) {
		const int k = (int)side.size() + 2;
		for (int j = 1; j + 1 < k; ++j) {
			const float a = from + (to - from) * (float)j / (float)(k - 1);
			locs[(size_t)side[(size_t)j - 1]] = {std::round(half + r * det::cos(a)) - half, std::round(half + r * det::sin(a)) - half};
		}
	};
	arc(east, map_angle + a0, map_angle + a1);
	arc(west, map_angle - a0, map_angle - a1);
	return locs;
}

} // namespace

void RmsGenerator::synth_wild_lake(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// wild_lake.js (0.28), in metres: diamond-square land (smoothness 0.8) over heightRange
	// (MIN_HEIGHT and MAX_HEIGHT x (size + 512) / 1024 / 5) from a seed of middle height with a
	// low patch in the centre (5x5 with one low vertex below 256 tiles, 6x6 with four, 8x8 with
	// four from 384), 5 rounds of splashErodeMap(0.1), SmoothingPainter(1, 0.5, ceil(size / 128)
	// + 1), rescaled; the water a fifth of the way up. Players at getStartLocationsByHeightmap's
	// spots between heighLimits 3 and 4 (3/8 and 4/8 of the way from the water to the top), 30
	// from the border, grouped in a cycle, each smoothed to its height over 20 tiles.
	const float half = hm.map_half, size = 2.0f * half;
	const float scale = (size + 512.0f) / 1024.0f / 5.0f;
	const float min_h = -20.0f * scale, max_h = (65535.0f / 92.0f - 20.0f) * scale; // MIN_HEIGHT, MAX_HEIGHT
	const float sea = min_h + (max_h - min_h) / 5.0f; // heightSeaGroundAdjusted
	const float lo = min_h, md = (min_h + max_h) / 2.0f;
	const int seed_n = size < 256.0f ? 5 : size >= 384.0f ? 8 : 6;
	std::vector<std::vector<float>> seed((size_t)seed_n, std::vector<float>((size_t)seed_n, md));
	if (seed_n == 5) seed[2][2] = lo;
	else for (int a = seed_n / 2 - 1; a <= seed_n / 2; ++a) for (int b = seed_n / 2 - 1; b <= seed_n / 2; ++b) seed[a][b] = lo;
	diamond_square_028(hm, rng, min_h, max_h, seed, 0.8f);
	for (int i = 0; i < 5; ++i) splash_erode_028(hm, 0.1f);
	smoothing_painter_028(hm, 1.0f, 0.5f, (int)std::ceil(size / 128.0f) + 1);
	rescale_028(hm, min_h, max_h);
	auto limit = [&](float r) { return sea + r * (max_h - sea); };
	const auto starts = start_locations_028(hm, rng, limit(3.0f / 8.0f), limit(4.0f / 8.0f), num_players, 30);
	water_h = 1.0f;
	for (float &h : hm.data) h = water_h + (h - sea) * kMetre;
	if (starts.empty()) {
		place_ring_spawns(hm, spawns, num_players, half * 0.66f, rng.range_float(0.0f, 6.2831853f), 6.2831853f, water_h + 1.5f);
		return;
	}
	smoothed_start_spawns_028(hm, spawns, starts, 20.0f, 20.0f);
	for (auto &s : spawns) s.base_height = std::max(water_h + 0.5f, hm.sample_world(s.x, s.z));
}

void RmsGenerator::synth_schwarzwald(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// schwarzwald.js: diamond-square terrain (setBaseTerrainDiamondSquare,
	// smoothness 0.5) grown from a 3x3 seed high at the rim and low in the
	// middle, between MIN_HEIGHT and MAX_HEIGHT * (size + 512) / 8192, then
	// SmoothingPainter(1, 0.8, 5) and rescaled; the water line lies a fifth
	// of the way up the height range, so a lake fills the middle. Players
	// at 5/8-3/4 of the map radius with jittered angles, each base smoothed
	// to its own height; paths link every base and the centre, and the wood
	// density field of the script (see distribute_land_extras) fills the
	// rest. In 0 A.D. metres (kMetre) above the water line.
	water_h = 1.0f;
	const float half = hm.map_half, size = 2.0f * half;
	const float kMinH = -20.0f, kMaxH = 65535.0f / 92.0f - 20.0f;
	const float lo = kMinH * (size + 512.0f) / 8192.0f, hi = kMaxH * (size + 512.0f) / 8192.0f;
	const float sea = lo + 0.2f * (hi - lo);
	// setBaseTerrainDiamondSquare(lo, hi, [[hi, hi, hi], [hi, lo, hi], [hi, hi, hi]]), smoothness 0.5.
	diamond_square_028(hm, rng, lo, hi, {{hi, hi, hi}, {hi, lo, hi}, {hi, hi, hi}}, 0.5f);
	smoothing_painter_028(hm, 1.0f, 0.8f, 5);
	rescale_028(hm, lo, hi);
	for (float &h : hm.data) h = water_h + (h - sea) * kMetre;
	// Players.
	const float base_r = 15.0f;
	const float rmin = std::min(half - 1.5f * base_r, 0.625f * half), rmax = std::min(half - base_r, 0.75f * half);
	const float start = rng.range_float(0.0f, kTwoPi), step = kTwoPi / (float)std::max(1, num_players);
	std::vector<std::pair<float, float>> locs;
	for (int i = 0; i < num_players; ++i) {
		const float r = rng.range_float(rmin, rmax);
		locs.push_back(polar(start + (float)i * step + rng.range_float(0.0f, step / 4.0f), r));
	}
	spawns_at(hm, spawns, locs, num_players, metres(water_h, 3.0f), 10.0f, 12.0f);
	for (const auto &s : spawns) {
		lift_area(hm, s.x, s.z, 16.0f, 4.0f, s.base_height);
		lift_area(hm, s.x, s.z, 26.0f, 4.0f, metres(water_h, 1.0f));
	}
	ensure_land_links(hm, water_h, locs);
}

void RmsGenerator::synth_pyrenean_sierra(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players,
		const uint8_t *teams) {
	// pyrenean_sierra.js (0.28), in 0 A.D. map metres (the water at heightWaterLevel 8, 12 below
	// SEA_LEVEL): land at heightBase -6 with randFloat(-1, 1) and scaleByMapSize(1, 3) x (cos(x /
	// s) + sin(z / s)) of noise, s = scaleByMapSize(5, 30); players on playerPlacementArcs at
	// fractionToTiles(0.35), the arcs 0.1 pi to 0.9 pi either side of oceanAngle (startAngle +-
	// pi / 12). The Pyrenees: a range fractionToTiles(0.68) long through the centre, square to
	// startAngle, of 100 x scaleByMapSize(1, 10) peaks scaleByMapSize(15, 55) wide, each line of
	// heights (heightPassage + 25 + randFloat(0, 10) + randFloat(-9, 9)) x sigmoid (flattened
	// towards the ends) over the noise, the range wobbling scaleByMapSize(1, 12); a 7-tile
	// passage at heightPassage scaleByMapSize(25, 40) across it at 0.35 and 0.65 of its length,
	// scaleByMapSize(8, 50) out to each side; SmoothingPainter(1, 0.3, 1) within a tile of the
	// range (above 15 m); two oceans of diskArea(fractionToTiles(0.18)) (ClumpPlacer 0.9, 0.05)
	// at -22 m, fractionToTiles(0.48) out at oceanAngle and opposite, SmoothingPainter(5, 0.9, 1)
	// within 5 tiles of them; scaleByMapSize(5, 35) hills (ClumpPlacer(scaleByMapSize(60, 120),
	// 0.3, 0.06), 7 m over 4 tiles) 5 from the water, 20 from the players and 2 from the range.
	// The range's lines are sampled at RTS0's vertices; the passages are 9 tiles wide, not 7, so
	// their flat floor spans RTS0's 2-tile nav cells (7 leaves 3 tiles, and the sides often don't
	// connect); the hills leave out their 2 m of random elevation, and keep off the bases by the
	// players' 20 alone (clBaseResource 6 lies inside it).
	const float half = hm.map_half, size = 2.0f * half;
	const size_t N = hm.data.size();
	auto sc = [&](float a, float b) { return seas_scale(a, b, half); };
	const float sea = 8.0f - 20.0f, h_passage = sc(25, 40), h_mountain = h_passage + 25.0f;
	const float start_angle = rng.range_float(0.0f, kTwoPi);
	const float ocean_angle = start_angle + rng.range_float(-1.0f, 1.0f) * kSeasPi / 12.0f;
	const float m_len = 0.68f * size, m_width = sc(15, 55), peaks = 100.0f * sc(1, 10);
	const float m_offset = rng.range_float(-1.0f, 1.0f) * sc(1, 12), pass_len = sc(8, 50);

	const float amp = sc(1, 3), per = sc(5, 30);
	std::vector<float> base(N);
	for (size_t i = 0; i < N; ++i) {
		float wx, wz;
		seas_world(hm, (int)i, wx, wz);
		const float r = rng.range_float(-1.0f, 1.0f);
		base[i] = -6.0f + r + amp * (det::cos((wx + half) / per) + det::sin((wz + half) / per));
	}
	hm.data = base;
	const auto locs = placement_arcs_028(hm, rng, num_players, teams, 0.35f * size, ocean_angle,
	                                     0.1f * kSeasPi, 0.9f * kSeasPi);

	// The range, from mountainStart (the centre less half its length) along mountainDirection.
	const auto vec = rotate_0ad(m_len, 0.0f, -start_angle);
	const float sx = -vec.first / 2.0f, sz = -vec.second / 2.0f, dx = vec.first / m_len, dz = vec.second / m_len;
	for (int p = 0; (float)p < peaks; ++p) {
		const float pos_f = (float)p / peaks, peak_h = rng.range_float(0.0f, 10.0f);
		const auto off = rotate_0ad(m_offset, 0.0f, -pos_f * kSeasPi * 4.0f);
		const float flat = (0.2f - std::max(0.0f, std::fabs(0.5f - pos_f) - 0.3f)) * 5.0f;
		for (float d = 0.0f; d < m_width; d += 1.0f / 3.0f) {
			const float rest = 2.0f * (1.0f - d / m_width);
			const float r1 = rng.range_float(0.9f, 1.1f);
			const float r2 = rng.range_float(0.9f, 1.1f);
			const float r3 = rng.range_float(0.9f, 1.1f);
			const float sig_x = -(rest - 1.9f) - 4.0f * (rest - r1) * (rest - r2) * (rest - r3);
			for (int dir : {-1, 1}) {
				const auto w = rotate_0ad(d, 0.0f, -start_angle - (float)dir * kSeasPi / 2.0f);
				const float px = std::round(half + sx + dx * pos_f * m_len + off.first + w.first) - half;
				const float pz = std::round(half + sz + dz * pos_f * m_len + off.second + w.second) - half;
				const float jitter = rng.range_float(-9.0f, 9.0f);
				// Only the tile at a vertex sets it.
				const int v = seas_vertex(hm, px, pz);
				float vx, vz;
				seas_world(hm, v, vx, vz);
				if (std::fabs(vx - px) > 0.5f || std::fabs(vz - pz) > 0.5f) continue;
				hm.data[(size_t)v] = base[(size_t)v] + (h_mountain + peak_h + jitter) * flat / (1.0f + det::exp(sig_x));
			}
		}
	}
	std::vector<uint8_t> range(N);
	for (size_t i = 0; i < N; ++i) range[i] = hm.data[i] > 15.0f; // heightPyreneans, ExcludeMin
	record_area(hm, range, AREA_MOUNTAIN); // clPyrenneans, for its height and slope bands

	// The passages: startHeight heightPassage, endHeight the ground at their end.
	const float pvx = -dz * pass_len, pvz = dx * pass_len; // mountainDirection.perpendicular()
	for (float at : {0.35f, 0.65f})
		for (int dir : {1, -1}) {
			const float ax = sx + vec.first * at, az = sz + vec.second * at;
			const float bx = ax + pvx * (float)dir, bz = az + pvz * (float)dir;
			const float hb = hm.sample_world(std::round(half + bx) - half, std::round(half + bz) - half);
			create_passage(hm, ax, az, bx, bz, 9.0f, 9.0f, 2.0f, h_passage, hb);
		}
	auto near = [&](const std::vector<uint8_t> &m, float r) {
		const auto d = seas_dist_inside(hm, m);
		std::vector<uint8_t> out(N);
		for (size_t i = 0; i < N; ++i) out[i] = d[i] <= r;
		return out;
	};
	const auto near_range = near(range, 1.0f);
	smoothing_painter_028(hm, 1.0f, 0.3f, 1, &near_range);

	// The oceans.
	std::vector<uint8_t> water(N, 0);
	for (int k = 0; k < 2; ++k) {
		const float a = ocean_angle + kSeasPi * (float)k, r = 0.48f * size, cr = 0.18f * size;
		const auto m = seas_clump(hm, rng, kSeasPi * cr * cr, 0.9f, 0.05f, 1e9f, r * det::cos(a), r * det::sin(a), [](int) { return true; });
		for (size_t i = 0; i < N; ++i)
			if (!m.empty() && m[i]) hm.data[i] = -22.0f;
		if (!m.empty()) seas_add(water, m);
	}
	const auto near_water = near(water, 5.0f);
	smoothing_painter_028(hm, 5.0f, 0.9f, 1, &near_water);
	record_area(hm, water, AREA_LAKE); // clWater, for its shore bands

	// The hills.
	const auto to_water = seas_dist_inside(hm, water), to_range = seas_dist_inside(hm, range);
	const auto to_player = seas_dist_to_players(hm, locs, 5.0f); // addCivicCenterAreaToClass: a disk of 5
	seas_create_areas(hm, rng, sc(5, 35), 10, [&](float x, float z) {
		const auto m = seas_clump(hm, rng, sc(60, 120), 0.3f, 0.06f, 1e9f, x, z, [&](int i) {
			return to_water[(size_t)i] > 5.0f && to_player[(size_t)i] > 20.0f && to_range[(size_t)i] > 2.0f;
		});
		if (!seas_any(m)) return false;
		seas_paint_modify(hm, m, 7.0f, 4.0f);
		record_area(hm, m, AREA_HILL);
		return true;
	});

	water_h = 1.0f;
	for (float &h : hm.data) h = coast_metres(water_h, h - sea);
	seas_spawns(hm, spawns, locs, 16.0f, 6.0f, water_h, coast_metres(water_h, 1.0f));
}

// The paths caledonian_meadows.js lays between the players (clPath), a tile grid for its
// resource spots (place_caledonian_spots) to keep off.
thread_local std::vector<uint8_t> t_caledonian_paths;

void RmsGenerator::synth_caledonian_meadows(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// caledonian_meadows.js, in metres: diamond-square land from a flat 2x2 seed (smoothness 0.8)
	// over heightRange (MIN_HEIGHT and MAX_HEIGHT x (size + 256) / 768 / 4), 5 rounds of
	// splashErodeMap(0.1), rescaled to the range; the water a fifth of the way up it. Players
	// at getStartLocationsByHeightmap's spots between heighLimits 4 and 5 (1/8 and 3/8 of the
	// way from the water to the top), 30 from the border (and the circle's edge), grouped in a cycle; each smoothed to
	// its height over 35 tiles, and RandomPathPlacer paths (4 wide) between them blended 0.4
	// toward the player height and smoothed (SmoothingPainter(5, 1, 1)) within 5 of them.
	const float half = hm.map_half, size = 2.0f * half;
	const float scale = (size + 256.0f) / 768.0f / 4.0f;
	const float min_h = -20.0f * scale, max_h = (65535.0f / 92.0f - 20.0f) * scale; // MIN_HEIGHT, MAX_HEIGHT
	const float sea = min_h + (max_h - min_h) / 5.0f; // heightSeaGroundAdjusted
	const float med = (min_h + max_h) / 2.0f;
	diamond_square_028(hm, rng, min_h, max_h, {{med, med}, {med, med}}, 0.8f);
	for (int i = 0; i < 5; ++i) splash_erode_028(hm, 0.1f);
	rescale_028(hm, min_h, max_h);
	auto limit = [&](float r) { return sea + r * (max_h - sea); }; // heighLimits 3-10
	const float sp = grid_spacing(hm);

	// getStartLocationsByHeightmap({L4, L5}, 1000, 30), grouped in a cycle.
	const auto best = start_locations_028(hm, rng, limit(2.0f / 8.0f), limit(3.0f / 8.0f), num_players, 30);
	const int tiles = (int)std::lround(size);

	// From here on RTS0 heights: the water at 1, a metre kMetre.
	water_h = 1.0f;
	for (float &h : hm.data) h = water_h + (h - sea) * kMetre;
	const float player_h = water_h + ((limit(2.0f / 8.0f) + limit(3.0f / 8.0f)) / 2.0f - sea) * kMetre; // playerHeight
	if (best.empty()) {
		place_ring_spawns(hm, spawns, num_players, half * 0.66f, rng.range_float(0.0f, 6.2831853f), 6.2831853f, water_h + 1.5f);
		t_caledonian_paths.clear();
		return;
	}
	// DiskPlacer(35) with SmoothElevationPainter(ELEVATION_SET, its height, 35).
	smoothed_start_spawns_028(hm, spawns, best, 35.0f, 35.0f);

	// RandomPathPlacer(start, next start, 4, 2, false): from 2 tiles out, a step of a tile a
	// turn, within a quarter turn either way of the straight line, until 2 from the end
	// (at most 2 x the map size steps), a 4-tile disk at each.
	std::vector<uint8_t> &path = t_caledonian_paths;
	path.assign((size_t)tiles * (size_t)tiles, 0);
	for (int i = 0; i < num_players && num_players > 1; ++i) {
		const float ax = (float)best[i].first, ay = (float)best[i].second;
		const float bx = (float)best[(i + 1) % num_players].first, by = (float)best[(i + 1) % num_players].second;
		const float len = std::max(1e-3f, det::hypot(bx - ax, by - ay));
		float px = std::round(ax + (bx - ax) / len * 2.0f), py = std::round(ay + (by - ay) / len * 2.0f);
		const float dir = det::atan2(by - py, bx - px);
		for (int steps = 0; (px - bx) * (px - bx) + (py - by) * (py - by) >= 4.0f && steps++ < 2 * tiles;) {
			const float a = dir + 1.5707963f * rng.range_float(-1.0f, 1.0f);
			px = std::round(px + det::cos(a));
			py = std::round(py + det::sin(a));
			for (int dy = -4; dy <= 4; ++dy)
				for (int dx = -4; dx <= 4; ++dx) {
					const int tx = (int)px + dx, ty = (int)py + dy;
					if (dx * dx + dy * dy >= 16 || tx < 0 || ty < 0 || tx >= tiles || ty >= tiles) continue;
					path[(size_t)ty * (size_t)tiles + (size_t)tx] = 1;
				}
		}
	}
	auto vertex_tile = [&](int x, int z) {
		float wx, wz;
		hm.grid_to_world(x, z, wx, wz);
		const int tx = std::clamp((int)std::floor(wx + half), 0, tiles - 1), ty = std::clamp((int)std::floor(wz + half), 0, tiles - 1);
		return (size_t)ty * (size_t)tiles + (size_t)tx;
	};
	// ElevationBlendingPainter(playerHeight, 0.4) on the path.
	std::vector<uint8_t> on_path(hm.data.size(), 0);
	for (int z = 0; z < hm.res; ++z)
		for (int x = 0; x < hm.res; ++x) {
			const size_t i = (size_t)z * (size_t)hm.res + (size_t)x;
			if (!path[vertex_tile(x, z)]) continue;
			on_path[i] = 1;
			hm.data[i] = 0.4f * player_h + 0.6f * hm.data[i];
		}
	// SmoothingPainter(5, 1, 1) within 5 tiles of the path: each vertex, in turn, set to the
	// mean of the heights in a box 5 tiles out, weighted by 1 / Manhattan distance.
	const auto near = distance_to_mask(on_path, hm.res, sp);
	const int brush = std::max(1, (int)std::lround(5.0f / sp));
	for (int z = 0; z < hm.res; ++z)
		for (int x = 0; x < hm.res; ++x) {
			if (near[(size_t)z * (size_t)hm.res + (size_t)x] > 5.0f) continue;
			float sum = 0.0f, wsum = 0.0f;
			for (int dz = -brush; dz <= brush; ++dz)
				for (int dx = -brush; dx <= brush; ++dx) {
					const int nx = x + dx, nz = z + dz, d = std::abs(dx) + std::abs(dz);
					if (!d || nx < 0 || nz < 0 || nx >= hm.res || nz >= hm.res) continue;
					sum += hm.get(nx, nz) / (float)d;
					wsum += 1.0f / (float)d;
				}
			hm.set(x, z, sum / wsum);
		}
	for (auto &sp_ : spawns) sp_.base_height = std::max(water_h + 0.5f, hm.sample_world(sp_.x, sp_.z));
}

// =========================================================================
// 0 A.D. maps/random/caledonian_meadows.js (0.28): the resource spots. getPointsByHeight picks
// vertices between the middles of heighLimits 3-4 and 5-6 (shore and low ground, player height
// and high ground), off the paths, 30 tiles from the players and 20 from each other and the border, in
// 2 x mapSize tries; spot i is (i % 5, or % 4 under Nomad) a stone mine, a metal mine, a fenced
// Gaia farmstead with sheep, a grove and a camp of treasures and Gaia soldiers. The decorations
// (geology and flora actors, the campfire, eyecandy props) and the grove and path terrains
// are left out; the spots keep off the paths (t_caledonian_paths).
// =========================================================================
static void place_caledonian_spots(Simulation &sim, const HeightmapGrid &hm, float water_h,
		const std::vector<RmsGenerator::PlayerSpawn> &spawns, uint32_t seed) {
	RmsRng rng(seed ^ 0x43414C45u); // 'CALE': a stream of its own, so the map's other placements stay
	constexpr double pi = kWallPi<double>;
	const float half = hm.map_half, size = 2.0f * half;
	const float scale = (size + 256.0f) / 768.0f / 4.0f;
	const float min_h = -20.0f * scale, max_h = (65535.0f / 92.0f - 20.0f) * scale; // synth_caledonian_meadows'
	const float top = water_h + (max_h - (min_h + (max_h - min_h) / 5.0f)) * kMetre;
	auto limit = [&](float r) { return water_h + r * (top - water_h); }; // heighLimits above the sea
	const std::vector<uint8_t> &path = t_caledonian_paths;
	const int tiles = (int)std::lround(size);
	auto on_path = [&](int x, int z) {
		x = std::clamp(x, 0, tiles - 1);
		z = std::clamp(z, 0, tiles - 1);
		return !path.empty() && path[(size_t)z * (size_t)tiles + (size_t)x];
	};
	const float lo = 0.5f * (limit(1.0f / 8.0f) + limit(2.0f / 8.0f)), hi = 0.5f * (limit(3.0f / 8.0f) + limit(4.0f / 8.0f));
	const int min_d = 20, verts = (int)std::lround(size) + 1;
	std::vector<std::pair<float, float>> valid;
	for (int x = min_d; x < verts - min_d; ++x)
		for (int z = min_d; z < verts - min_d; ++z) {
			if (on_path(x - 1, z) || on_path(x, z - 1) || on_path(x + 1, z) || on_path(x, z + 1)) continue; // avoidClasses(clPath)
			const float wx = (float)x - half, wz = (float)z - half, h = hm.sample_world(wx, wz);
			if (h > lo && h < hi && (!sim.circular_map() || half - det::hypot(wx, wz) >= (float)min_d)) valid.push_back({wx, wz});
		}
	struct Taken { float x, z, d; };
	std::vector<Taken> taken;
	for (const auto &sp : spawns) taken.push_back({sp.x, sp.z, 30.0f});
	std::vector<std::pair<float, float>> spots;
	for (int t = 0; t < 2 * (int)size && !valid.empty(); ++t) {
		const auto p = valid[rng.next_u32() % (uint32_t)valid.size()];
		bool ok = true;
		for (const Taken &q : taken)
			if (!(det::hypot(q.x - p.first, q.z - p.second) > std::max((float)min_d, q.d))) { ok = false; break; }
		if (!ok) continue;
		spots.push_back(p);
		taken.push_back({p.first, p.second, (float)min_d});
	}

	// g_WallStyles.other: fence_long and fence_short as readyWallElement reads them (WallPiece
	// Length 12 and 6 m, Orientation 0.5), the script's bench, sheep, foodBin and farmstead,
	// and turn_x, a zero-length bend of x pi. The fences, each also reversed (flatMap):
	// D food bin, H farmstead, B bench, S sheep, F fence, f short fence, t/T/u turn 0.25/0.5/-0.5.
	static const char *const kFences[5] = {"DHBtStFtStFtStF", "DHFtStFtStBSFtStF", "DHTBuftStFtStFtStfSF",
			"DHTfuBtStFtStFtStfSF", "DHFtStBSFtStfSFtStfSF"};
	auto element = [&](char c) {
		WallElement e;
		switch (c) {
			case 'F': e.angle = 0.5 * pi; e.length = 3.0; break;
			case 'f': e.angle = 0.5 * pi; e.length = 1.5; break;
			case 'B': case 'D': e.angle = 0.5 * pi; e.length = 1.5; break;
			case 'S': e.indent = 0.75; break;
			case 'H': e.angle = pi; e.indent = -3.0; break;
			case 't': e.bend = 0.25 * pi; break;
			case 'T': e.bend = 0.5 * pi; break;
			case 'u': e.bend = -0.5 * pi; break;
			default: break;
		}
		return e;
	};
	const uint8_t fence_long = scenery_type_by_name("fence_long"), fence_short = scenery_type_by_name("fence_short");
	const uint8_t bench = scenery_type_by_name("bench");
	auto place_fence = [&](float x, float z) {
		const int pick = rng.range_int(0, 9); // pickRandom of the ten
		std::string wall = kFences[pick / 2];
		if (pick & 1) std::reverse(wall.begin(), wall.end());
		const double o = rng.range_float(0.0f, 6.2831853f); // randomAngle
		std::vector<WallElement> elements;
		for (char c : wall) elements.push_back(element(c));
		// placeCustomFortress: getCenterToFirstElement of the wall laid from the origin.
		double cx = 0.0, cz = 0.0;
		const auto origin = wall_alignment<double>(0.0, 0.0, elements, 0.0, 0.0);
		for (const auto &a : origin) { cx -= a.x / (double)origin.size(); cz -= a.y / (double)origin.size(); }
		const double sx = x + cx * det::cos(o) - cz * det::sin(o), sz = z + cx * det::sin(o) + cz * det::cos(o);
		const auto al = wall_alignment<double>(sx, sz, elements, 0.0, o);
		for (size_t i = 0; i < al.size(); ++i) {
			const float px = (float)al[i].x, pz = (float)al[i].y;
			const float rot = (float)(pi / 2.0 - al[i].angle); // exportEntityList's Position angle
			if (!sim.wall_builder_passable(px, pz)) continue;
			switch (wall[i]) {
				case 'F': sim.spawn_scenery(px, pz, fence_long, rot, 0.25f); break;
				case 'f': sim.spawn_scenery(px, pz, fence_short, rot, 0.25f); break;
				case 'B': sim.spawn_scenery(px, pz, bench, rot, 0.25f); break;
				case 'S': sim.spawn_fauna(px, pz, FAUNA_SHEEP); break;
				case 'D': sim.spawn_treasure(px, pz, TREASURE_FOOD_BIN); break;
				case 'H': sim.set_building_civ(sim.spawn_building_world(px, pz, BLD_FARMSTEAD, GAIA_PLAYER, 1.0f, rot), CIV_BRITONS); break;
				default: break;
			}
		}
	};
	// placeGrove: a Gaia Gaulish outpost or an oak, then 20-30 objects 2-5 out, every third an
	// actor (moss rocks, bushes, ferns) and the rest bush_temperate or euro_beech trees.
	auto place_grove = [&](float x, float z) {
		if (rng.range_int(0, 1) == 0)
			sim.set_building_civ(sim.spawn_building_world(x, z, BLD_OUTPOST, GAIA_PLAYER, 1.0f, rng.range_float(0.0f, 6.2831853f)), CIV_GAULS);
		else
			sim.spawn_resource_world(x, z, RES_WOOD, TREE_WOOD_SUPPLY);
		const int n = rng.range_int(20, 30);
		for (int i = 0; i < n; ++i) {
			const float a = 6.2831853f / (float)n * rng.range_float((float)i, (float)(i + 1)), d = rng.range_float(2.0f, 5.0f);
			const float px = x + det::cos(a) * d, pz = z + det::sin(a) * d;
			if (i % 3 != 0 && sim.wall_builder_passable(px, pz)) sim.spawn_resource_world(px, pz, RES_WOOD, TREE_WOOD_SUPPLY);
		}
	};
	// placeCamp: 5-11 objects 1-3 out, each one of 16: a metal or a standing-stone treasure,
	// a Gaia slinger or javelineer (Briton or Gaul) or a Naked Fanatic, or one of nine props.
	static const char *const kCampUnits[5] = {"brit/infantry_slinger_b", "brit/infantry_javelineer_b",
			"gaul/infantry_slinger_b", "gaul/infantry_javelineer_b", "gaul/champion_fanatic"};
	auto place_camp = [&](float x, float z) {
		const int n = rng.range_int(5, 11);
		for (int i = 0; i < n; ++i) {
			const float a = 6.2831853f / (float)n * rng.range_float((float)i, (float)(i + 1)), d = rng.range_float(1.0f, 3.0f);
			const float px = x + det::cos(a) * d, pz = z + det::sin(a) * d;
			const int k = rng.range_int(0, 15);
			if (k >= 7 || !sim.wall_builder_passable(px, pz)) continue;
			if (k < 2) {
				sim.spawn_treasure(px, pz, k == 0 ? TREASURE_METAL : TREASURE_STONE);
			} else {
				const uint16_t t = civ_unit_template_by_id(kCampUnits[k - 2]);
				if (t != CIV_UNIT_TEMPLATE_NONE) sim.spawn_gaia_unit(px, pz, (UnitType)civ_unit_template(t).unit_type, kCampUnits[k - 2]);
			}
		}
	};

	sim.begin_bulk_spawn();
	const int kinds = sim.nomad() ? 4 : 5;
	for (size_t i = 0; i < spots.size(); ++i) {
		const float x = spots[i].first, z = spots[i].second;
		switch ((int)(i % (size_t)kinds)) {
			case 0: sim.spawn_resource_world(x, z, RES_STONE, LARGE_MINE_SUPPLY); break; // gaia/rock/temperate_large_02
			case 1: sim.spawn_resource_world(x, z, RES_GOLD, LARGE_MINE_SUPPLY); break;  // gaia/ore/temperate_large
			case 2: place_fence(x, z); break;
			case 3: place_grove(x, z); break;
			case 4: place_camp(x, z); break;
		}
	}
	sim.end_bulk_spawn();
}

bool RmsGenerator::generate(Simulation &sim, const Config &config) {
	sim.reset();
	t_circular_map = sim.circular_map();

	t_map_areas.clear();
	RmsRng rng(config.seed ? config.seed : 1337);
	// Team Placement: Random, or a pattern this map doesn't offer, takes one it
	// does (gamesettings pickRandomItems), drawn apart from the map's own stream.
	uint8_t team_placement = config.team_placement;
	t_team_placement = {};
	sim.set_played_team_placement(TEAM_PLACEMENT_RANDOM);
	if (const uint8_t offered = map_team_placements(config.map_type)) {
		if (team_placement >= TEAM_PLACEMENT_COUNT || !((offered >> team_placement) & 1)) {
			RmsRng pick((config.seed ? config.seed : 1337) ^ 0x7EA3B1A5u);
			int k = pick.range_int(0, std::popcount(offered) - 1);
			for (uint8_t t = 0; t < TEAM_PLACEMENT_COUNT; ++t)
				if (((offered >> t) & 1) && k-- == 0) { team_placement = t; break; }
		}
		sim.set_played_team_placement(team_placement); // the pattern in play, for Match Settings and the summary
		t_team_placement.pattern = team_placement == TEAM_PLACEMENT_RADIAL ? -1 : placement_pattern(team_placement, 0);
		std::copy(std::begin(config.teams), std::end(config.teams), t_team_placement.teams);
	}
	HeightmapGrid hm;
	hm.init(config.heightmap_res, config.map_half, 0.0f);

	float water_h = -1.0f;
	std::vector<PlayerSpawn> spawns;
	DanubiusLayout danubius;
	JebelBarkalLayout jebel_barkal;
	RiverArchipelagoLayout river_archipelago;

	const auto &minfo = map_type_info(config.map_type);
	sim.set_map_half(config.map_half);
	sim.set_map_preset(config.map_type);
	sim.set_biome(config.biome);
	sim.set_map_info(minfo.id, minfo.name, minfo.biome);

	switch (config.map_type) {
		case MAP_RANDOM_CONTINENTAL:
			synth_continental(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_RANDOM_ARCHIPELAGO:
			synth_archipelago(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_RANDOM_OASIS:
			synth_oasis(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_RANDOM_HIGHLANDS:
			synth_highlands(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_RANDOM_VALLEY:
			synth_valley(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_RANDOM_MEDITERRANEAN:
			synth_mediterranean(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_ACROPOLIS:
			synth_acropolis(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_SPARTAN_VALLEY:
			synth_spartan_valley(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_SAHARA:
			synth_sahara(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_DANUBIUS:
			synth_danubius_0ad(hm, rng, water_h, spawns, config.num_players, config.teams, danubius);
			break;
		case MAP_SAHARAN_OASES:
			synth_saharan_oases(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_ANATOLIAN_PLATEAU:
			synth_anatolian_plateau(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_JEBEL_BARKAL:
			jebel_barkal.difficulty = config.trigger_difficulty;
			synth_jebel_barkal_0ad(hm, rng, water_h, spawns, config.num_players, config.teams, jebel_barkal);
			break;
		case MAP_MAINLAND:
			synth_mainland(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_ALPINE_LAKES:
			synth_alpine_lakes(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_LAKE:
			synth_lake(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_GULF_OF_BOTHNIA:
			synth_gulf_of_bothnia(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_ARDENNES_FOREST:
			synth_ardennes_forest(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_NEARESTERN_BADLANDS:
			synth_nearestern_badlands(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_RIVERS:
			synth_rivers(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_CANYON:
			synth_canyon(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_CORINTHIAN_ISTHMUS:
			synth_corinthian_isthmus(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_BELGIAN_UPLANDS:
			synth_belgian_uplands(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_LORRAINE_PLAIN:
			synth_lorraine_plain(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_HYRCANIAN_SHORES:
			synth_hyrcanian_shores(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_NORTHERN_LIGHTS:
			synth_northern_lights(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_KERALA:
			synth_kerala(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_POLAR_SEA:
			synth_polar_sea(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_SAHEL:
			synth_sahel(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_AFRICAN_PLAINS:
			synth_african_plains(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_ALPINE_VALLEY:
			synth_alpine_valley(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_SYRIA:
			synth_syria(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_CANTABRIAN_HIGHLANDS:
			synth_cantabrian_highlands(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_PYRENEAN_SIERRA:
			synth_pyrenean_sierra(hm, rng, water_h, spawns, config.num_players, config.teams);
			break;
		case MAP_SAHEL_WATERING_HOLES:
			synth_sahel_watering_holes(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_PERSIAN_HIGHLANDS:
			synth_persian_highlands(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_VOLCANIC_LANDS:
			synth_volcanic_lands(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_LIONS_DEN:
			synth_lions_den(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_HELLS_PASS:
			synth_hells_pass(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_DEEP_FOREST:
			synth_deep_forest(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_INDIA:
			synth_india(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_GUADALQUIVIR_RIVER:
			synth_guadalquivir_river(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_FIELDS_OF_MEROE:
			synth_fields_of_meroe(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_GEAR:
			synth_gear(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_ATLAS_MOUNTAINS:
			synth_atlas_mountains(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_FRONTIER:
			synth_frontier(hm, rng, water_h, spawns, config.num_players, config.teams, team_placement);
			break;
		case MAP_WILD_LAKE:
			synth_wild_lake(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_STRONGHOLD:
			synth_stronghold(hm, rng, water_h, spawns, config.num_players, config.teams);
			break;
		case MAP_CALEDONIAN_MEADOWS:
			synth_caledonian_meadows(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_RHINE_MARSHLANDS:
			synth_rhine_marshlands(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_RED_SEA:
			synth_red_sea(hm, rng, water_h, spawns, config.num_players, config.teams);
			break;
		case MAP_HELLAS:
			synth_hellas(hm, rng, water_h, spawns, config.num_players, config.teams);
			break;
		case MAP_ENGLISH_CHANNEL:
			synth_english_channel(hm, rng, water_h, spawns, config.num_players, config.teams);
			break;
		case MAP_DODECANESE:
			synth_dodecanese(hm, rng, water_h, spawns, config.num_players, config.teams);
			break;
		case MAP_FORTRESS:
			synth_fortress(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_FLOOD:
			synth_flood(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_EXTINCT_VOLCANO:
			synth_extinct_volcano(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_SURVIVAL_OF_THE_FITTEST:
			synth_survival_of_the_fittest(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_AEGEAN_SEA:
			synth_aegean_sea(hm, rng, water_h, spawns, config.num_players, config.teams);
			break;
		case MAP_FOOTHILLS:
			synth_foothills(hm, rng, water_h, spawns, config.num_players, config.teams);
			break;
		case MAP_COAST_RANGE:
			synth_coast_range(hm, rng, water_h, spawns, config.num_players, config.teams);
			break;
		case MAP_CONTINENT:
			synth_continent(hm, rng, water_h, spawns, config.num_players, config.teams);
			break;
		case MAP_ARCHIPELAGO_0AD:
			synth_archipelago_0ad(hm, rng, water_h, spawns, config.num_players, config.teams);
			break;
		case MAP_ISLANDS:
			synth_islands(hm, rng, water_h, spawns, config.num_players, config.teams);
			break;
		case MAP_LAND_GRAB:
			synth_land_grab(hm, rng, water_h, spawns, config.num_players, config.teams);
			break;
		case MAP_THE_NILE:
			synth_the_nile(hm, rng, water_h, spawns, config.num_players, config.teams);
			break;
		case MAP_CYCLADIC_ARCHIPELAGO:
			synth_cycladic_archipelago(hm, rng, water_h, spawns, config.num_players, config.teams);
			break;
		case MAP_SNOWFLAKE_SEAROCKS:
			synth_snowflake_searocks(hm, rng, water_h, spawns, config.num_players, config.teams);
			break;
		case MAP_HARBOR:
			synth_harbor(hm, rng, water_h, spawns, config.num_players, config.teams);
			break;
		case MAP_CORSICA:
			synth_corsica(hm, rng, water_h, spawns, config.num_players, config.teams);
			break;
		case MAP_PHOENICIAN_LEVANT:
			synth_phoenician_levant(hm, rng, water_h, spawns, config.num_players, config.teams);
			break;
		case MAP_MARMARA:
			synth_marmara(hm, rng, water_h, spawns, config.num_players, config.teams);
			break;
		case MAP_BAHRAIN:
			synth_bahrain(hm, rng, water_h, spawns, config.num_players, config.teams);
			break;
		case MAP_BOTSWANAN_HAVEN:
			synth_botswanan_haven(hm, rng, water_h, spawns, config.num_players, config.teams);
			break;
		case MAP_SCYTHIAN_RIVULET:
			synth_scythian_rivulet(hm, rng, water_h, spawns, config.num_players, config.teams);
			break;
		case MAP_AMBUSH:
			synth_ambush(hm, rng, water_h, spawns, config.num_players, config.teams, team_placement);
			break;
		case MAP_ARCTIC_SUMMER:
			synth_arctic_summer(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_EMPIRE:
			synth_empire(hm, rng, water_h, spawns, config.num_players, config.teams);
			break;
		case MAP_SCHWARZWALD:
			synth_schwarzwald(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_RATUMACOS:
			synth_ratumacos(hm, rng, water_h, spawns, config.num_players, config.teams);
			break;
		case MAP_OASIS_0AD:
			synth_oasis_0ad(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_MEDITERRANEAN:
			synth_mediterranean_0ad(hm, rng, water_h, spawns, config.num_players, config.teams);
			break;
		case MAP_ELEPHANTINE:
			synth_elephantine(hm, rng, water_h, spawns, config.num_players, config.teams);
			break;
		case MAP_ISLAND_STRONGHOLD:
			synth_island_stronghold(hm, rng, water_h, spawns, config.num_players, config.teams);
			break;
		case MAP_POMPEII:
			synth_pompeii(hm, rng, water_h, spawns, config.num_players, config.teams);
			break;
		case MAP_LATIUM:
			synth_latium(hm, rng, water_h, spawns, config.num_players, config.teams);
			break;
		case MAP_LOWER_NUBIA:
			synth_lower_nubia(hm, rng, water_h, spawns, config.num_players, config.teams);
			break;
		case MAP_UNKNOWN:
			synth_unknown(hm, rng, water_h, spawns, config.num_players, config.teams, config.landscape);
			break;
		case MAP_MIGRATION:
			synth_migration(hm, rng, water_h, spawns, config.num_players, config.teams, team_placement);
			break;
		case MAP_NGORONGORO:
			synth_ngorongoro(hm, rng, water_h, spawns, config.num_players, config.teams);
			break;
		case MAP_RIVER_ARCHIPELAGO:
			synth_river_archipelago(hm, rng, water_h, spawns, config.num_players, config.teams, river_archipelago);
			break;
		case MAP_WALL_DEMO:
			// RandomMap(0, "grass1"): flat land, 0.28's water 0.1 m below it. RTS0 keeps the
			// land a metre over the nav water line so its plane can't show through the grass
			// and the ground is dry land, not shallows.
			water_h = 1.0f;
			hm.init(hm.res, hm.map_half, metres(water_h + NAV_WATER_MARGIN, 1.0f));
			break;
		case MAP_NEW_RMS_TEST: {
			// new_rms_test.js: RandomMap(0, "grass1_spring") (flat, a metre over the nav water
			// line as on Wall Demo) and playerPlacementCircle(fractionToTiles(0.39)): getPlayerIDs
			// order, a random start angle, positions rounded to whole tiles.
			water_h = 1.0f;
			hm.init(hm.res, hm.map_half, metres(water_h + NAV_WATER_MARGIN, 1.0f));
			spawns.clear();
			const float half = hm.map_half, r = 0.78f * half, start = rng.range_float(0.0f, 6.2831853f);
			for (int i = 0; i < config.num_players; ++i) {
				const float a = start + 6.2831853f * (float)i / (float)config.num_players;
				const float sx = std::round(half + r * det::cos(a)) - half, sz = std::round(half + r * det::sin(a)) - half;
				const float d = std::max(1e-3f, det::hypot(sx, sz));
				spawns.push_back({(uint8_t)i, sx, sz, hm.sample_world(sx, sz), -sx / d, -sz / d});
			}
			break;
		}
		case MAP_PELOPONNESE:
		case MAP_AEGEAN_ARCHIPELAGO:
		default:
			synth_peloponnese(hm, rng, water_h, spawns, config.num_players);
			break;
	}

	// Survival of the Fittest's cliffs are meant to keep the bases apart; the waves come over them.
	if (config.map_type != MAP_SURVIVAL_OF_THE_FITTEST && config.map_type != MAP_WALL_DEMO && config.map_type != MAP_NEW_RMS_TEST)
		ease_routes_between_bases(hm, spawns, water_h);

	// Transfer heightmap to simulation
	sim.set_terrain_data(hm.data, hm.res, water_h);

	// Survival of the Fittest places only its own entities: civic centres, infantry, the
	// treasure seekers, trigger points and the central forests (no mines, hunt or relics).
	if (config.map_type == MAP_SURVIVAL_OF_THE_FITTEST) {
		place_survival_entities(sim, hm, spawns, rng, config);
		return true;
	}
	// Wall Demo places its walls alone: no player bases, resources or animals.
	if (config.map_type == MAP_WALL_DEMO) {
		place_wall_demo(sim, hm.map_half, rng);
		return true;
	}
	// New RMS Test: placePlayerBases with no base functions gives only
	// placeCivDefaultStartingEntities (civic centre, StartEntities, Iberian walls), and
	// placePlayersNomad the nomad groups. No resources, animals or relics.
	if (config.map_type == MAP_NEW_RMS_TEST) {
		if (sim.nomad()) {
			place_nomad_units(sim, hm, spawns, rng);
			return true;
		}
		for (const auto &sp : spawns) {
			sim.spawn_building_world(sp.x, sp.z, BLD_TOWN_CENTER, sp.team, 1.0f);
			place_start_entities(sim, hm, sp.team, sp.x, sp.z);
		}
		const int map_tiles = (int)std::lround(2.0f * hm.map_half);
		for (const auto &sp : spawns)
			sim.place_starting_walls(sp.team, sp.x, sp.z, map_tiles, Simulation::STARTING_WALLS_FORTRESS, -0.7853982f, rng.next_u32());
		return true;
	}

	// Distribute balanced player bases and starter clusters
	distribute_player_bases(sim, hm, spawns, rng, config);
	const size_t base_resources = sim.resources().count, base_fauna = sim.fauna().count;

	// Distribute neutral resources, groves, and quarries
	distribute_neutral_resources(sim, hm, water_h, spawns, rng, config);
	if (config.map_type == MAP_NGORONGORO) place_ngorongoro_entities(sim, hm, spawns, rng, config);
	if (config.map_type == MAP_CALEDONIAN_MEADOWS) place_caledonian_spots(sim, hm, water_h, spawns, config.seed);

	// Distribute roaming wildlife packs
	distribute_wildlife(sim, hm, water_h, spawns, rng, config);

	// Distribute sacred relics
	distribute_relics(sim, hm, water_h, spawns, rng, config);

	// Fish in open water, for fishing boats (Elephantine places its own, place_elephantine_fish;
	// lower_nubia.js places none)
	if (config.map_type != MAP_ELEPHANTINE && config.map_type != MAP_LOWER_NUBIA) distribute_fish(sim, hm, water_h, config);

	// Entities only the trigger maps have (Gaia towers, central mines)
	place_trigger_map_entities(sim, hm, water_h, spawns, rng, config);
	if (config.map_type == MAP_DANUBIUS) place_danubius_entities(sim, hm, water_h, spawns, rng, danubius);
	if (config.map_type == MAP_JEBEL_BARKAL) place_jebel_barkal_entities(sim, hm, water_h, spawns, rng, jebel_barkal);
	if (config.map_type == MAP_RIVER_ARCHIPELAGO) place_river_archipelago_entities(sim, hm, water_h, spawns, rng, river_archipelago);

	// The rest of the mines, fruit and hunt the map's 0 A.D. script places
	top_up_resources(sim, hm, water_h, spawns, config, base_resources, base_fauna);

	if (sim.nomad()) place_nomad_units(sim, hm, spawns, rng);

	// The Iberians' Starting Walls (player.js placeStartingWalls), last so the trees and bushes
	// under them go (template_gaia_flora's DeleteUponConstruction).
	const int map_tiles = (int)std::lround(2.0f * hm.map_half);
	const uint8_t walls = starting_walls_of(config.map_type, map_tiles);
	if (walls != Simulation::STARTING_WALLS_NONE)
		for (const auto &sp : spawns)
			sim.place_starting_walls(sp.team, sp.x, sp.z, map_tiles, walls, -0.7853982f, rng.next_u32());

	return true;
}

// The map script's own terrains and plants, unless game setup chose the biome.
static const ScriptTerrains *own_terrains(const Simulation &sim, uint8_t type) {
	return sim.map_biome_pick(type) < 0 ? script_terrains(map_type_info(type).id) : nullptr;
}

void RmsGenerator::plant_flora(Simulation &sim, uint32_t seed, uint8_t type) {
	const ScriptTerrains *own = own_terrains(sim, type);
	const BiomeFlora *f = own ? &own->flora : biome_flora(sim.current_map_biome().c_str());
	if (!f) return;
	auto hash = [&](int x, int z, uint32_t salt) {
		uint32_t h = seed * 0x9E3779B1u ^ salt * 0x85EBCA77u ^ (uint32_t)x * 0xC2B2AE3Du ^ (uint32_t)z * 0x27D4EB2Fu;
		h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
		return h;
	};
	auto options = [](const uint8_t (*set)[2]) { int n = 0; while (n < 3 && set[n][0] != FLORA_DEFAULT) ++n; return n; };
	// A set with no trees (polar_sea places none) leaves every plant to the viewer.
	if (options(f->tree12) == 0 || options(f->tree45) == 0 || f->tree3[0] == FLORA_DEFAULT) return;
	// setupBiome_<biome>'s pickRandom, once per map.
	const uint32_t o12 = hash(0, 0, 1) % (uint32_t)options(f->tree12), o45 = hash(0, 0, 2) % (uint32_t)options(f->tree45);
	const uint8_t *t12 = f->tree12[o12], *t45 = f->tree45[o45];
	int n3 = 0;
	while (n3 < 5 && f->tree3[n3] != FLORA_DEFAULT) ++n3;
	const uint32_t o3 = hash(0, 0, 3) % (uint32_t)n3;
	const uint8_t t3 = f->tree3[o3];
	// Each slot's template wood (0: TREE_WOOD_SUPPLY), parallel to t12, t45 and t3.
	const uint16_t *w12 = f->tree12_wood[o12], *w45 = f->tree45_wood[o45];
	const uint16_t w3 = f->tree3_wood[o3];
	// Each slot's template MaxGatherers (tree_slot_gatherers.h), parallel to t12, t45 and t3.
	const char *slot_name = own ? own->map_id : sim.current_map_biome().c_str();
	const int g12[2] = {tree_slot_gatherers(slot_name, own, 0, (int)o12, 0), tree_slot_gatherers(slot_name, own, 0, (int)o12, 1)};
	const int g45[2] = {tree_slot_gatherers(slot_name, own, 2, (int)o45, 0), tree_slot_gatherers(slot_name, own, 2, (int)o45, 1)};
	const int g3 = tree_slot_gatherers(slot_name, own, 1, (int)o3, 0);

	const auto rv = sim.resources();
	// Stragglers whose slot is a fruit tree (temperate's gaia/fruit/apple): food supplies, spawned after the pass.
	std::vector<std::pair<float, float>> fruit_trees;
	uint8_t fruit_kind = FLORA_DEFAULT;
	uint16_t fruit_food = 0;
	// placePlayerBaseTrees: each player's starting trees, 11-13 tiles from its start plus their
	// group's spread, are the script's tree1 ("Trees": {"template": oTree1}), not stragglers
	// (createStragglerTrees keeps off the player areas).
	constexpr float BASE_TREE_REACH = 18.0f;
	std::vector<std::pair<float, float>> starts;
	for (const auto &b : sim.buildings())
		if (is_civic_centre(b.type) && b.team < MAX_PLAYERS) starts.emplace_back(b.x, b.z);
	auto base_tree = [&](float x, float z) {
		for (const auto &[sx, sz] : starts)
			if ((x - sx) * (x - sx) + (z - sz) * (z - sz) < BASE_TREE_REACH * BASE_TREE_REACH) return true;
		return false;
	};
	// Trees per 2-tile cell, to tell forests from stragglers.
	std::unordered_map<uint64_t, int> cells;
	auto cell_key = [](int cx, int cz) { return (uint64_t)(uint32_t)cx << 32 | (uint32_t)cz; };
	for (size_t r = 0; r < rv.count; ++r)
		if (rv.type[r] == RES_WOOD && rv.subtype[r] == RES_SUB_NONE)
			++cells[cell_key((int)std::floor(rv.x[r] * 0.5f), (int)std::floor(rv.z[r] * 0.5f))];
	for (size_t r = 0; r < rv.count; ++r) {
		const int tx = (int)std::floor(rv.x[r]), tz = (int)std::floor(rv.z[r]);
		if (rv.type[r] == RES_FOOD && rv.subtype[r] == RES_SUB_NONE) {
			// An unnamed fruit bush is the biome's fruitBush, at its template's Max (gaia/fruit/date
			// 400, berries and grapes 200); one the generator named (a script's apples) keeps its kind.
			if (rv.flora[r] == FLORA_DEFAULT) {
				sim.set_resource_flora(rv.id[r], f->fruit);
				if (rv.amount[r] == 200 && (f->fruit == FLORA_DATE || f->fruit == FLORA_APPLE)) sim.set_resource_amount(rv.id[r], 400);
			}
			continue;
		}
		if (rv.type[r] != RES_WOOD || rv.subtype[r] != RES_SUB_NONE) continue;
		const int cx = (int)std::floor(rv.x[r] * 0.5f), cz = (int)std::floor(rv.z[r] * 0.5f);
		int near = -1; // the tree itself
		for (int dz = -1; dz <= 1; ++dz)
			for (int dx = -1; dx <= 1; ++dx) {
				auto it = cells.find(cell_key(cx + dx, cz + dz));
				if (it != cells.end()) near += it->second;
			}
		const uint32_t h = hash(tx, tz, 4);
		uint8_t kind;
		uint16_t wood;
		int gatherers;
		if (near >= 4) {
			// createDefaultForests: each forest is either a tree1/tree2 or a tree4/tree5 mix.
			const bool second = hash((int)std::floor(rv.x[r] / 12.0f), (int)std::floor(rv.z[r] / 12.0f), 5) & 1u;
			kind = (second ? t45 : t12)[h & 1u];
			wood = (second ? w45 : w12)[h & 1u];
			gatherers = (second ? g45 : g12)[h & 1u];
		} else if (type == MAP_LOWER_NUBIA && own) {
			// lower_nubia.js's lone trees are its stragglers (acacia, dead tree, bush_badlands), not palms.
			kind = f->tree3[h % (uint32_t)n3];
			wood = f->tree3_wood[h % (uint32_t)n3];
			gatherers = tree_slot_gatherers(slot_name, own, 1, (int)(h % (uint32_t)n3), 0);
		} else if (base_tree(rv.x[r], rv.z[r])) {
			kind = t12[0];
			wood = w12[0];
			gatherers = g12[0];
		} else {
			// createStragglerTrees: [tree1, tree2, tree4, tree3].
			const uint8_t pick[4] = {t12[0], t12[1], t45[0], t3};
			const uint16_t pick_wood[4] = {w12[0], w12[1], w45[0], w3};
			kind = pick[h % 4u];
			const int pick_gatherers[4] = {g12[0], g12[1], g45[0], g3};
			wood = pick_wood[h % 4u];
			gatherers = pick_gatherers[h % 4u];
			if (flora_is_fruit(kind)) {
				fruit_trees.emplace_back(rv.x[r], rv.z[r]);
				fruit_kind = kind;
				fruit_food = wood;
				continue;
			}
		}
		sim.set_resource_flora(rv.id[r], kind);
		// The template's ResourceSupply/Max, for a tree the generator spawned with TREE_WOOD_SUPPLY
		// (the Survival of the Fittest forests keep their own 800).
		if (wood != 0 && rv.amount[r] == TREE_WOOD_SUPPLY) sim.set_resource_amount(rv.id[r], wood);
		if (gatherers != 8) sim.set_resource_max_gatherers(rv.id[r], gatherers);
	}
	for (const auto &[x, z] : fruit_trees) {
		sim.clear_resources_near(x, z, 0.001f);
		const uint32_t id = sim.spawn_resource_world(x, z, RES_FOOD, fruit_food);
		if (id) sim.set_resource_flora(id, fruit_kind);
	}
}

void RmsGenerator::keep_resources_passable(Simulation &sim, uint8_t type) {
	const auto &H = sim.terrain_heights();
	const int hr = sim.terrain_res(), tr = hr - 1;
	if (tr <= 0 || H.size() < (size_t)hr * (size_t)hr) return;
	const float half = sim.map_half(), tile = 2.0f * half / (float)tr;
	const float dry_h = sim.water_height() + 0.6f;
	// 0.28's passability per terrain tile: highest corner minus lowest over the tile.
	std::vector<uint8_t> steep((size_t)tr * (size_t)tr, 0);
	for (int j = 0; j < tr; ++j)
		for (int i = 0; i < tr; ++i) {
			const float a = H[(size_t)j * hr + i], b = H[(size_t)j * hr + i + 1];
			const float c = H[(size_t)(j + 1) * hr + i], d = H[(size_t)(j + 1) * hr + i + 1];
			const float sl = (std::max(std::max(a, b), std::max(c, d)) - std::min(std::min(a, b), std::min(c, d))) / tile;
			steep[(size_t)j * tr + i] = sl > LAND_MAX_TERRAIN_SLOPE;
		}
	auto tidx = [&](float v) { return std::clamp((int)std::floor((v + half) / tile), 0, tr - 1); };
	auto steep_at = [&](float x, float z) { return steep[(size_t)tidx(z) * tr + tidx(x)] != 0; };
	// A tile a gatherer can stand beside: within r of (x, z).
	auto open_near = [&](float x, float z, float r) {
		for (int j = tidx(z - r); j <= tidx(z + r); ++j)
			for (int i = tidx(x - r); i <= tidx(x + r); ++i)
				if (!steep[(size_t)j * tr + i]) return true;
		return false;
	};
	// rmgen's MAP_BORDER_WIDTH (RandomMap.js:220 randomCoordinate, MapGenerator.cpp:126):
	// entities stay 3 tiles in from the edge on every map; the ring/frame past_map_edge bars
	// is impassable, so whatever starts there moves in across it.
	const float inner = half - MAP_EDGE_DIST - 1.5f;
	auto in_ring = [&](float x, float z) {
		if (sim.circular_map()) return x * x + z * z > inner * inner;
		return std::fabs(x) > inner || std::fabs(z) > inner;
	};
	auto pull_in = [&](float &x, float &z) {
		if (sim.circular_map()) {
			const float d = std::max(1e-3f, det::hypot(x, z)), k = (inner - 0.5f) / d;
			x *= k;
			z *= k;
		} else {
			x = std::clamp(x, -(inner - 0.5f), inner - 0.5f);
			z = std::clamp(z, -(inner - 0.5f), inner - 0.5f);
		}
	};
	const auto fv = sim.fauna();
	for (size_t f = 0; f < fv.count; ++f) {
		float x = fv.pos_x[f], z = fv.pos_z[f];
		if (!fv.alive[f] || !in_ring(x, z)) continue;
		pull_in(x, z);
		sim.set_fauna_position(fv.id[f], x, z);
	}
	const auto rv = sim.resources();
	for (size_t r = 0; r < rv.count; ++r) {
		float x = rv.x[r], z = rv.z[r];
		if (!rv.alive[r] || !in_ring(x, z)) continue;
		pull_in(x, z);
		sim.set_resource_position(rv.id[r], x, z);
	}
	std::vector<std::pair<float, float>> taken;
	taken.reserve(rv.count);
	for (size_t r = 0; r < rv.count; ++r)
		if (rv.alive[r]) taken.push_back({rv.x[r], rv.z[r]});
	auto spot_ok = [&](float x, float z, float gap, size_t self) {
		if (std::fabs(x) > half - 2.0f || std::fabs(z) > half - 2.0f) return false;
		if (in_ring(x, z)) return false;
		if (sim.get_terrain_height(x, z) < dry_h) return false;
		for (float dz : {-tile, 0.0f, tile})
			for (float dx : {-tile, 0.0f, tile})
				if (steep_at(x + dx, z + dz)) return false;
		for (const auto &b : sim.buildings())
			if (det::hypot(x - b.x, z - b.z) < 7.0f) return false;
		for (size_t k = 0; k < taken.size(); ++k)
			if (k != self && det::hypot(x - taken[k].first, z - taken[k].second) < gap) return false;
		return true;
	};
	size_t live = 0;
	for (size_t r = 0; r < rv.count; ++r) {
		if (!rv.alive[r]) continue;
		const size_t self = live++;
		const uint8_t ty = rv.type[r];
		const bool mine = ty == RES_GOLD || ty == RES_STONE;
		const bool bush = ty == RES_FOOD && rv.subtype[r] == RES_SUB_NONE;
		const bool tree = ty == RES_WOOD;
		if (!mine && !bush && !tree) continue;
		const float x = rv.x[r], z = rv.z[r];
		if (tree ? open_near(x, z, 1.5f) : !steep_at(x, z)) continue;
		// The nearest spot outwards in rings of half a tile, up to 6 units away.
		const float gap = mine ? 3.0f : 1.0f;
		bool moved = false;
		for (float rad = 0.5f * tile; rad <= 6.0f && !moved; rad += 0.5f * tile) {
			const int n = std::max(8, (int)(6.2831853f * rad / (0.5f * tile)));
			for (int k = 0; k < n; ++k) {
				const float a = 6.2831853f * (float)k / (float)n;
				const float nx = x + det::cos(a) * rad, nz = z + det::sin(a) * rad;
				if (!spot_ok(nx, nz, gap, self)) continue;
				sim.set_resource_position(rv.id[r], nx, nz);
				taken[self] = {nx, nz};
				moved = true;
				break;
			}
		}
	}
	// 0.28's scripts place mines, fruit and hunt on the land the bases share (stayClasses(clLand),
	// avoidClasses(clHill, ...)): one walled off from every civic centre by cliffs moves to the
	// nearest spot of that land. Water counts as open here (islands are the sea's business), and
	// Survival of the Fittest walls its bases in on purpose.
	if (type == MAP_SURVIVAL_OF_THE_FITTEST) return;
	std::vector<uint8_t> reach((size_t)tr * (size_t)tr, 0);
	std::vector<int> todo;
	for (const auto &b : sim.buildings()) {
		if (b.type != BLD_TOWN_CENTER || b.team >= MAX_PLAYERS) continue;
		// Seed from the open tiles round the centre's footprint.
		for (int j = tidx(b.z - 8.0f); j <= tidx(b.z + 8.0f); ++j)
			for (int i = tidx(b.x - 8.0f); i <= tidx(b.x + 8.0f); ++i) {
				const size_t t = (size_t)j * tr + i;
				if (steep[t] || reach[t]) continue;
				reach[t] = 1;
				todo.push_back((int)t);
			}
	}
	if (todo.empty()) return;
	auto ring_tile = [&](int i, int j) { return in_ring(-half + ((float)i + 0.5f) * tile, -half + ((float)j + 0.5f) * tile); };
	while (!todo.empty()) {
		const int t = todo.back();
		todo.pop_back();
		const int i = t % tr, j = t / tr;
		const int di[4] = {1, -1, 0, 0}, dj[4] = {0, 0, 1, -1};
		for (int k = 0; k < 4; ++k) {
			const int ni = i + di[k], nj = j + dj[k];
			if (ni < 0 || nj < 0 || ni >= tr || nj >= tr) continue;
			const size_t n = (size_t)nj * tr + ni;
			if (reach[n] || steep[n] || ring_tile(ni, nj)) continue;
			reach[n] = 1;
			todo.push_back((int)n);
		}
	}
	auto reached_near = [&](float x, float z, float r) {
		for (int j = tidx(z - r); j <= tidx(z + r); ++j)
			for (int i = tidx(x - r); i <= tidx(x + r); ++i)
				if (reach[(size_t)j * tr + i]) return true;
		return false;
	};
	auto reached_at = [&](float x, float z) { return reach[(size_t)tidx(z) * tr + tidx(x)] != 0; };
	// The nearest spot outwards, up to 16 units away, that passes ok.
	auto nearest = [&](float x, float z, const auto &ok, float &ox, float &oz) {
		for (float rad = 0.5f * tile; rad <= 16.0f; rad += 0.5f * tile) {
			const int n = std::max(8, (int)(6.2831853f * rad / (0.5f * tile)));
			for (int k = 0; k < n; ++k) {
				const float a = 6.2831853f * (float)k / (float)n;
				ox = x + det::cos(a) * rad;
				oz = z + det::sin(a) * rad;
				if (ok(ox, oz)) return true;
			}
		}
		return false;
	};
	live = 0;
	for (size_t r = 0; r < rv.count; ++r) {
		if (!rv.alive[r]) continue;
		const size_t self = live++;
		const uint8_t ty = rv.type[r];
		if (!(ty == RES_GOLD || ty == RES_STONE || (ty == RES_FOOD && rv.subtype[r] == RES_SUB_NONE))) continue;
		const float x = rv.x[r], z = rv.z[r];
		if (reached_near(x, z, 1.5f)) continue;
		const float gap = ty == RES_FOOD ? 1.0f : 3.0f;
		float nx, nz;
		if (nearest(x, z, [&](float px, float pz) { return reached_at(px, pz) && spot_ok(px, pz, gap, self); }, nx, nz)) {
			sim.set_resource_position(rv.id[r], nx, nz);
			taken[self] = {nx, nz};
		}
	}
	for (size_t f = 0; f < fv.count; ++f) {
		if (!fv.alive[f] || fv.owner[f] != GAIA_PLAYER) continue;
		const float x = fv.pos_x[f], z = fv.pos_z[f];
		if (sim.get_terrain_height(x, z) < dry_h || reached_near(x, z, 1.0f)) continue; // fish and whales stay at sea
		float nx, nz;
		if (nearest(x, z, [&](float px, float pz) { return reached_at(px, pz) && sim.get_terrain_height(px, pz) >= dry_h; }, nx, nz))
			sim.set_fauna_position(fv.id[f], nx, nz);
	}
}

void RmsGenerator::set_mine_gatherers(Simulation &sim, uint8_t type) {
	// 0.28's maps/random scripts that place their quarries as g_Gaia.stoneLarge (rmgen2's
	// gaia.js included); the others name their own rock templates, all at 24 gatherers.
	switch (type) {
	case MAP_AFRICAN_PLAINS: case MAP_ALPINE_LAKES: case MAP_ALPINE_VALLEY: case MAP_ARCHIPELAGO_0AD:
	case MAP_BAHRAIN: case MAP_CANTABRIAN_HIGHLANDS: case MAP_CANYON: case MAP_COAST_RANGE: case MAP_CONTINENT:
	case MAP_CORINTHIAN_ISTHMUS: case MAP_FLOOD: case MAP_FOOTHILLS: case MAP_GEAR: case MAP_GULF_OF_BOTHNIA:
	case MAP_HELLAS: case MAP_HYRCANIAN_SHORES: case MAP_ISLAND_STRONGHOLD: case MAP_ISLANDS: case MAP_LAKE:
	case MAP_LAND_GRAB: case MAP_MAINLAND: case MAP_MIGRATION: case MAP_NEARESTERN_BADLANDS:
	case MAP_PERSIAN_HIGHLANDS: case MAP_RATUMACOS: case MAP_RIVERS: case MAP_SNOWFLAKE_SEAROCKS: case MAP_UNKNOWN:
	case MAP_WILD_LAKE: case MAP_AMBUSH: case MAP_EMPIRE: case MAP_FRONTIER: case MAP_HARBOR: case MAP_HELLS_PASS:
	case MAP_LIONS_DEN: case MAP_MARMARA: case MAP_NGORONGORO: case MAP_RED_SEA: case MAP_STRONGHOLD: {
		const std::string &biome = sim.current_map_biome();
		if (biome != "temperate" && biome != "alpine") return;
		break;
	}
	case MAP_CALEDONIAN_MEADOWS: break; // placeMine(pos, "gaia/rock/temperate_large_02")
	default: return;
	}
	const auto rv = sim.resources();
	for (size_t r = 0; r < rv.count; ++r)
		if (rv.alive[r] && rv.type[r] == RES_STONE && rv.subtype[r] == RES_SUB_NONE && rv.amount[r] == LARGE_MINE_SUPPLY)
			sim.set_resource_max_gatherers(rv.id[r], 48);
}

void RmsGenerator::paint_terrain(Simulation &sim, uint32_t seed, uint8_t type) {
	const float half = sim.map_half();
	const int res = std::clamp((int)std::lround(2.0f * half), 1, Simulation::MAX_TERRAIN_TILE_RES);
	const float cell = 2.0f * half / (float)res;
	const ScriptTerrains *own = own_terrains(sim, type);
	const TerrainBiome &b = own ? own->terrain : terrain_biome(sim.current_map_biome().c_str());
	Simulation::TerrainTiles t;
	t.res = res;
	t.tiles.assign((size_t)res * (size_t)res, 0);
	if (type == MAP_WALL_DEMO) { // wall_demo.js: RandomMap(0, "grass1") and nothing painted on it
		t.names = {"grass1"};
		sim.set_terrain_tiles(std::move(t));
		return;
	}
	if (type == MAP_NEW_RMS_TEST) { // new_rms_test.js: RandomMap(0, "grass1_spring"), nothing painted
		t.names = {"grass1_spring"};
		sim.set_terrain_tiles(std::move(t));
		return;
	}
	auto index_of = [&](const char *name) -> uint8_t {
		for (size_t i = 0; i < t.names.size(); ++i)
			if (t.names[i] == name) return (uint8_t)i;
		t.names.push_back(name);
		return (uint8_t)(t.names.size() - 1);
	};
	auto hash = [&](int x, int z, uint32_t salt) {
		uint32_t h = seed * 0x9E3779B1u ^ salt * 0x85EBCA77u ^ (uint32_t)x * 0xC2B2AE3Du ^ (uint32_t)z * 0x27D4EB2Fu;
		h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
		return h;
	};
	// rmgen's TerrainPainter with an array: one of the names per tile.
	auto pick = [&](const char *const *list, int n, int x, int z, uint32_t salt) {
		int k = 0;
		while (k < n && list[k]) ++k;
		return index_of(k ? list[hash(x, z, salt) % (uint32_t)k] : b.main[0]);
	};
	// Smooth value noise for clumped patches (createPatches' blobs), about `scale` tiles across.
	auto noise = [&](float x, float z, float scale, uint32_t salt) {
		const float fx = x / scale, fz = z / scale;
		const int x0 = (int)std::floor(fx), z0 = (int)std::floor(fz);
		const float tx = fx - (float)x0, tz = fz - (float)z0;
		auto v = [&](int a, int c) { return (float)(hash(a, c, salt) & 0xFFFF) / 65535.0f; };
		const float sx = tx * tx * (3.0f - 2.0f * tx), sz = tz * tz * (3.0f - 2.0f * tz);
		const float a = v(x0, z0) + (v(x0 + 1, z0) - v(x0, z0)) * sx;
		const float c = v(x0, z0 + 1) + (v(x0 + 1, z0 + 1) - v(x0, z0 + 1)) * sx;
		return a + (c - a) * sz;
	};
	// Masks: forest floor round trees, dirt round mines, road round civic centres.
	std::vector<uint8_t> forest(t.tiles.size(), 0), mine(t.tiles.size(), 0), road(t.tiles.size(), 0);
	auto stamp = [&](std::vector<uint8_t> &m, float x, float z, float r, uint8_t v) {
		const int cx = (int)std::floor((x + half) / cell), cz = (int)std::floor((z + half) / cell);
		const int rr = (int)std::ceil(r / cell);
		for (int dz = -rr; dz <= rr; ++dz)
			for (int dx = -rr; dx <= rr; ++dx) {
				const int tx = cx + dx, tz = cz + dz;
				if (tx < 0 || tz < 0 || tx >= res || tz >= res) continue;
				const float wx = -half + ((float)tx + 0.5f) * cell - x, wz = -half + ((float)tz + 0.5f) * cell - z;
				if (wx * wx + wz * wz > r * r) continue;
				uint8_t &d = m[(size_t)tz * (size_t)res + (size_t)tx];
				d = std::max(d, v);
			}
	};
	const auto rv = sim.resources();
	for (size_t i = 0; i < rv.count; ++i) {
		if (!rv.alive[i]) continue;
		const uint8_t sub = rv.subtype ? rv.subtype[i] : 0;
		if (rv.type[i] == RES_WOOD) { stamp(forest, rv.x[i], rv.z[i], 2.5f, 1); stamp(forest, rv.x[i], rv.z[i], 1.5f, 2); }
		else if ((rv.type[i] == RES_GOLD || rv.type[i] == RES_STONE) && sub != RES_SUB_FISH) stamp(mine, rv.x[i], rv.z[i], 2.5f, 1);
	}
	// placePlayerBase's CityPatch: road inside a third of the base radius
	// (defaultPlayerBaseRadius, 15-25 tiles), wild road round it.
	const float base_r = 15.0f + 10.0f * std::clamp((2.0f * half - 128.0f) / (512.0f - 128.0f), 0.0f, 1.0f);
	for (const auto &bd : sim.buildings())
		if (is_civic_centre(bd.type)) { stamp(road, bd.x, bd.z, base_r / 3.0f + 1.5f, 1); stamp(road, bd.x, bd.z, base_r / 3.0f * 0.6f, 2); }
	const float water = sim.water_height();
	const bool has_water = water > -0.5f;
	const float metre = HEIGHT_PER_METRE;
	std::vector<float> metres_up(t.tiles.size(), 0.0f), slope_up(t.tiles.size(), 0.0f);
	const bool own_patches = own && own->patches[0].layer[0][0];
	// A script paints its own shores (its height bands and areas), with no shore blend to 3 m; one that
	// paints its lakes' shores (an AREA_LAKE or AREA_OASIS row) has no shore band either: oasis.js's and
	// saharan_oases.js's land stands 1 m up.
	const bool own_bands = own && own->bands[0].terrain[0];
	bool own_lakes = false;
	if (own)
		for (const AreaLayers &l : own->areas) own_lakes = own_lakes || ((l.kind == AREA_LAKE || l.kind == AREA_OASIS) && l.layer[0][0]);
	for (int tz = 0; tz < res; ++tz)
		for (int tx = 0; tx < res; ++tx) {
			const size_t j = (size_t)tz * (size_t)res + (size_t)tx;
			const float x = -half + ((float)tx + 0.5f) * cell, z = -half + ((float)tz + 0.5f) * cell;
			const float h = sim.get_terrain_height(x, z);
			const float e = 0.5f * cell;
			const float gx = (sim.get_terrain_height(x + e, z) - sim.get_terrain_height(x - e, z)) / (2.0f * e);
			const float gz = (sim.get_terrain_height(x, z + e) - sim.get_terrain_height(x, z - e)) / (2.0f * e);
			const float slope = std::sqrt(gx * gx + gz * gz); // RTS0 height units per unit, as 0 A.D. metres per 4 m tile x 0.25
			uint8_t v;
			// Under water, the shore band up to 1.5 m above it, the blend to 3 m (a script map paints its own shores).
			if (has_water && h < water) v = index_of(b.water);
			else if (has_water && h < water + 1.5f * metre && !own_bands && !own_lakes) v = index_of(b.shore);
			// 0 A.D. rmgen paints cliffs on slopes of 2 m per tile and steeper, hills from 1.25.
			else if (slope >= 2.0f * metre) v = pick(b.cliff, 2, tx, tz, 1);
			else if (slope >= 1.25f * metre) v = pick(b.hill, 2, tx, tz, 2);
			else if (road[j] == 2) v = index_of(b.road);
			else if (road[j] == 1) v = index_of(b.road_wild);
			else if (forest[j] == 2) v = index_of(b.forest[(hash(tx, tz, 3) & 1) && b.forest[1] ? 1 : 0]);
			else if (forest[j] == 1) v = (hash(tx, tz, 4) & 1) ? index_of(b.forest[0]) : pick(b.main, 4, tx, tz, 5);
			else if (mine[j]) v = pick(b.dirt, 2, tx, tz, 6);
			else if (has_water && h < water + 3.0f * metre && !own) v = index_of(b.shore_blend);
			else {
				// createPatches over the main terrain: [main, tier1], [tier1, tier2],
				// [tier2, tier3] clumps, then tier4 and dirt patches.
				const float n1 = noise(x, z, 9.0f, 7), n2 = noise(x, z, 6.0f, 8), n3 = noise(x, z, 5.0f, 9);
				if (own_patches) v = pick(b.main, 4, tx, tz, 11);
				else if (n1 > 0.78f) v = index_of(b.tier[2] ? b.tier[2] : b.main[0]);
				else if (n1 > 0.70f) v = index_of(b.tier[1] ? b.tier[1] : b.main[0]);
				else if (n1 > 0.62f) v = index_of(b.tier[0] ? b.tier[0] : b.main[0]);
				else if (n2 > 0.80f) v = index_of(b.tier[3] ? b.tier[3] : b.main[0]);
				else if (n3 > 0.86f) v = pick(b.dirt, 2, tx, tz, 10);
				else v = pick(b.main, 4, tx, tz, 11);
			}
			// To the millimetre, so land the synth set to a whole metre stays on it.
			slope_up[j] = slope / metre;
			metres_up[j] = std::round((own && own->coast ? coast_unmetres(water, h) : (h - water) / metre) * 1000.0f) / 1000.0f;
			t.tiles[j] = v;
		}
	// The script's own height bands and areas, unless a road, forest floor or mine dirt took the tile.
	auto free_tile = [&](size_t j) { return road[j] == 0 && forest[j] == 0 && mine[j] == 0; };
	// Chebyshev distances (in tiles) to the areas of each kind a band keeps near, made on first use.
	std::vector<uint16_t> near_d[32];
	auto near_area = [&](uint8_t kind, size_t j) -> uint16_t {
		std::vector<uint16_t> &d = near_d[kind & 31];
		if (d.empty()) {
			d.assign(t.tiles.size(), 0xFFFF);
			std::vector<uint32_t> q;
			if (kind == AREA_SEA) {
				for (uint32_t k = 0; k < (uint32_t)t.tiles.size(); ++k)
					if (metres_up[k] < 0.0f) { d[k] = 0; q.push_back(k); }
			}
			for (const MapArea &a : t_map_areas)
				if (a.kind == kind)
					for (uint32_t k : a.tiles)
						if (d[k]) { d[k] = 0; q.push_back(k); }
			for (size_t k = 0; k < q.size(); ++k) {
				const int x = (int)(q[k] % (uint32_t)res), z = (int)(q[k] / (uint32_t)res);
				for (int dz = -1; dz <= 1; ++dz)
					for (int dx = -1; dx <= 1; ++dx) {
						const int nx = x + dx, nz = z + dz;
						if (nx < 0 || nz < 0 || nx >= res || nz >= res) continue;
						const uint32_t n = (uint32_t)(nz * res + nx);
						if (d[n] != 0xFFFF) continue;
						d[n] = (uint16_t)(d[q[k]] + 1);
						q.push_back(n);
					}
			}
		}
		return d[j];
	};
	const float band_scale = std::clamp(((float)res - 128.0f) / (512.0f - 128.0f), 0.0f, 1.0f);
	auto band_matches = [&](const HeightBand &band, size_t j) {
		const float lo = band.hi512 != 0.0f ? band.lo + (band.lo512 - band.lo) * band_scale : band.lo;
		const float hi = band.hi512 != 0.0f ? band.hi + (band.hi512 - band.hi) * band_scale : band.hi;
		if (metres_up[j] < lo || metres_up[j] >= hi) return false;
		// rmgen's getSlope averages the height steps to the 8 neighbours: about 0.77 of the gradient.
		const float slope_028 = 0.77f * slope_up[j];
		if (band.slope_hi > band.slope_lo && (slope_028 < band.slope_lo || slope_028 >= band.slope_hi)) return false;
		if (band.near && near_area(band.near, j) > band.near_dist) return false;
		if (band.far && near_area(band.far, j) <= band.far_dist) return false;
		return true;
	};
	auto paint_bands = [&](bool late) {
		if (late && own->tree) {
			for (size_t j = 0; j < t.tiles.size(); ++j) {
				if (!free_tile(j)) continue;
				for (const HeightBand &band : own->bands) {
					if (!band.terrain[0] || !band.late || !band_matches(band, j)) continue;
					if (band.terrain[0][0]) t.tiles[j] = pick(band.terrain, 6, (int)(j % (size_t)res), (int)(j / (size_t)res), 12);
					break;
				}
			}
			return;
		}
		for (const HeightBand &band : own->bands) {
			if (!band.terrain[0] || band.late != late) continue;
			for (size_t j = 0; j < t.tiles.size(); ++j)
				if (free_tile(j) && band_matches(band, j))
					t.tiles[j] = pick(band.terrain, 6, (int)(j % (size_t)res), (int)(j / (size_t)res), 12);
		}
	};
	if (own) {
		paint_bands(false);
		// The script's patches (PatchSet), with rmgen's ChainPlacer on the tile grid.
		auto paint_patches = [&](auto &paint_layers) {
			if (!own_patches) return;
			uint64_t state = (uint64_t)seed * 0x9E3779B97F4A7C15ull ^ 0x5041544348ull;
			auto next = [&]() {
				state += 0x9E3779B97F4A7C15ull;
				uint64_t z = state;
				z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
				z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
				return z ^ (z >> 31);
			};
			auto rand_int = [&](int lo, int hi) { return lo + (int)(next() % (uint64_t)(hi - lo + 1)); };
			const float scale = std::clamp(((float)res - 128.0f) / (512.0f - 128.0f), 0.0f, 1.0f);
			auto sbms = [&](const float *lohi) { return lohi[0] + (lohi[1] - lohi[0]) * scale; };
			// Chebyshev distances (in tiles) to water, hills, forest floor and civic centres.
			auto distance_to = [&](auto in_class) {
				std::vector<uint16_t> d(t.tiles.size(), 0xFFFF);
				std::vector<uint32_t> q;
				for (size_t j = 0; j < d.size(); ++j)
					if (in_class(j)) { d[j] = 0; q.push_back((uint32_t)j); }
				for (size_t k = 0; k < q.size(); ++k) {
					const int x = (int)(q[k] % (uint32_t)res), z = (int)(q[k] / (uint32_t)res);
					for (int dz = -1; dz <= 1; ++dz)
						for (int dx = -1; dx <= 1; ++dx) {
							const int nx = x + dx, nz = z + dz;
							if (nx < 0 || nz < 0 || nx >= res || nz >= res) continue;
							const uint32_t n = (uint32_t)(nz * res + nx);
							if (d[n] != 0xFFFF) continue;
							d[n] = (uint16_t)(d[q[k]] + 1);
							q.push_back(n);
						}
				}
				return d;
			};
			std::vector<uint8_t> hill(t.tiles.size(), 0), player(t.tiles.size(), 0), wet(t.tiles.size(), 0);
			for (const MapArea &a : t_map_areas) {
				if (a.kind == AREA_HILL || a.kind == AREA_MOUNTAIN || a.kind == AREA_VOLCANO)
					for (uint32_t j : a.tiles) hill[j] = 1;
				// A lake's or oasis's whole area is the script's clWater (clOasis), its dry rings too.
				if (a.kind == AREA_LAKE || a.kind == AREA_OASIS)
					for (uint32_t j : a.tiles) wet[j] = 1;
			}
			for (const auto &bd : sim.buildings())
				if (is_civic_centre(bd.type)) {
					const int cx = (int)std::floor((bd.x + half) / cell), cz = (int)std::floor((bd.z + half) / cell);
					for (int dz = -5; dz <= 5; ++dz)
						for (int dx = -5; dx <= 5; ++dx)
							if (dx * dx + dz * dz < 25 && cx + dx >= 0 && cz + dz >= 0 && cx + dx < res && cz + dz < res)
								player[(size_t)(cz + dz) * (size_t)res + (size_t)(cx + dx)] = 1;
				}
			const auto to_water = distance_to([&](size_t j) { return metres_up[j] < 0.0f || wet[j]; });
			const auto to_hill = distance_to([&](size_t j) { return hill[j] || slope_up[j] >= 1.25f; });
			const auto to_forest = distance_to([&](size_t j) { return forest[j] != 0; });
			const auto to_player = distance_to([&](size_t j) { return player[j] != 0; });
			std::vector<uint8_t> patched(t.tiles.size(), 0), got(t.tiles.size(), 0), near(t.tiles.size(), 0);
			for (const MapArea &a : t_map_areas)
				if (a.kind == AREA_GRASS) // syria.js clGrass: its patches keep off the grass round the bases
					for (uint32_t j : a.tiles) patched[j] = 1;
			std::vector<uint32_t> points, touched;
			std::vector<std::pair<int, int>> edges;
			for (const PatchSet &ps : own->patches) {
				if (!ps.layer[0][0]) continue;
				int layers = 0;
				while (layers < 4 && ps.layer[layers][0]) ++layers;
				auto far = [&](const std::vector<uint16_t> &d, int8_t r, size_t j) { return r < 0 || d[j] > (uint16_t)r; };
				// Tiles within avoid_patch (Chebyshev) of a patch: marked once per patched tile, not per test.
				const int pr = ps.avoid_patch;
				std::fill(near.begin(), near.end(), 0);
				auto mark_near = [&](uint32_t j) {
					const int x = (int)(j % (uint32_t)res), z = (int)(j / (uint32_t)res);
					for (int nz = std::max(0, z - pr); nz <= std::min(res - 1, z + pr); ++nz)
						for (int nx = std::max(0, x - pr); nx <= std::min(res - 1, x + pr); ++nx) near[(size_t)nz * (size_t)res + (size_t)nx] = 1;
				};
				if (pr >= 0)
					for (size_t j = 0; j < patched.size(); ++j)
						if (patched[j]) mark_near((uint32_t)j);
				auto near_patch = [&](int x, int z) { return pr >= 0 && near[(size_t)z * (size_t)res + (size_t)x]; };
				auto borders_free = [&](int x, int z) {
					auto free_at = [&](int nx, int nz) {
						return nx >= 0 && nz >= 0 && nx < res && nz < res && !got[(size_t)nz * (size_t)res + (size_t)nx];
					};
					return free_at(x - 1, z) || free_at(x + 1, z) || free_at(x, z - 1) || free_at(x, z + 1);
				};
				auto allows = [&](int x, int z) {
					const size_t j = (size_t)z * (size_t)res + (size_t)x;
					return far(to_water, ps.avoid_water, j) && far(to_hill, ps.avoid_hill, j) && far(to_forest, ps.avoid_forest, j) &&
					       far(to_player, ps.avoid_player, j) && !near_patch(x, z);
				};
				const int count = (int)sbms(ps.count);
				const int max_r = (int)std::floor(3.0f + 2.0f * scale);
				for (const auto &size_lohi : ps.size) {
					if (size_lohi[1] <= 0.0f) continue;
					const int circles = (int)sbms(size_lohi);
					// createAreas: `count` areas, giving up after count * 10 failures.
					for (int placed = 0, failed = 0; placed < count && failed < count * 10;) {
						const int cx = rand_int(0, res - 1), cz = rand_int(0, res - 1);
						points.clear();
						touched.clear();
						edges.assign(1, {cx, cz});
						int seen = 0, bad = 0;
						if (allows(cx, cz))
							for (int c = 0; c < circles && !edges.empty(); ++c) {
								// The chain grows from the area's edge tiles: new tiles join the list, and a
								// listed tile with no free neighbour left is dropped when drawn.
								std::pair<int, int> at;
								bool found = false;
								while (!edges.empty()) {
									const size_t e = (size_t)rand_int(0, (int)edges.size() - 1);
									at = edges[e];
									if (points.empty() || borders_free(at.first, at.second)) { found = true; break; }
									edges[e] = edges.back();
									edges.pop_back();
								}
								if (!found) break;
								const int r = rand_int(1, max_r);
								for (int z = std::max(0, at.second - r); z <= std::min(res - 1, at.second + r); ++z)
									for (int x = std::max(0, at.first - r); x <= std::min(res - 1, at.first + r); ++x) {
										if ((x - at.first) * (x - at.first) + (z - at.second) * (z - at.second) >= r * r) continue;
										++seen;
										if (!allows(x, z)) { ++bad; continue; }
										const size_t j = (size_t)z * (size_t)res + (size_t)x;
										if (got[j] == 0) {
											points.push_back((uint32_t)j);
											touched.push_back((uint32_t)j);
											edges.push_back({x, z});
										}
										got[j] = 1;
									}
							}
						for (uint32_t j : touched) got[j] = 0;
						if (points.empty() || bad > seen / 2) { ++failed; continue; }
						paint_layers(points, &ps.layer[0][0], 8, layers, ps.width, 30);
						for (uint32_t j : points) {
							patched[j] = 1;
							if (pr >= 0) mark_near(j);
						}
						++placed;
					}
				}
			}
		};
		// LayeredPainter: 8-neighbour steps in from the area's edge, 1 on the edge.
		std::vector<uint8_t> in(t.tiles.size(), 0);
		std::vector<uint16_t> dist(t.tiles.size(), 0);
		std::vector<uint32_t> queue;
		auto paint_layers = [&](const std::vector<uint32_t> &tiles, const char *const *layer, int stride, int layers,
		                        const uint8_t *widths, uint32_t salt) {
			for (uint32_t j : tiles) in[j] = 1;
			queue.clear();
			for (uint32_t j : tiles) {
				const int x = (int)(j % (uint32_t)res), z = (int)(j / (uint32_t)res);
				bool edge = false;
				for (int dz = -1; dz <= 1 && !edge; ++dz)
					for (int dx = -1; dx <= 1 && !edge; ++dx) {
						const int nx = x + dx, nz = z + dz;
						edge = nx >= 0 && nz >= 0 && nx < res && nz < res && !in[(size_t)nz * (size_t)res + (size_t)nx];
					}
				dist[j] = edge ? 1 : 0;
				if (edge) queue.push_back(j);
			}
			for (size_t q = 0; q < queue.size(); ++q) {
				const uint32_t j = queue[q];
				const int x = (int)(j % (uint32_t)res), z = (int)(j / (uint32_t)res);
				for (int dz = -1; dz <= 1; ++dz)
					for (int dx = -1; dx <= 1; ++dx) {
						const int nx = x + dx, nz = z + dz;
						if (nx < 0 || nz < 0 || nx >= res || nz >= res) continue;
						const uint32_t k = (uint32_t)(nz * res + nx);
						if (!in[k] || dist[k]) continue;
						dist[k] = (uint16_t)(dist[j] + 1);
						queue.push_back(k);
					}
			}
			for (uint32_t j : tiles) {
				// An area with no edge inside the map (it covers it) counts as one wide ring.
				const int d = dist[j] ? dist[j] : 1;
				int i = 0, width = 0;
				for (; i < layers - 1; ++i) {
					width += widths[i];
					if (width >= d) break;
				}
				if (free_tile(j))
					t.tiles[j] = pick(layer + i * stride, stride, (int)(j % (uint32_t)res), (int)(j / (uint32_t)res), salt + (uint32_t)i);
			}
			for (uint32_t j : tiles) { in[j] = 0; dist[j] = 0; }
		};
		for (const MapArea &a : t_map_areas) {
			const AreaLayers *paint = nullptr;
			for (const AreaLayers &l : own->areas)
				if (l.kind == a.kind && l.layer[0][0]) { paint = &l; break; }
			if (!paint) continue;
			int layers = 0;
			while (layers < 6 && paint->layer[layers][0]) ++layers;
			paint_layers(a.tiles, &paint->layer[0][0], 6, layers, paint->width, 13);
		}
		paint_patches(paint_layers);
		paint_bands(true);
	}
	// A tile's terrain blends over neighbours of lower priority, which the renderer
	// takes as the name's index. rmgen gives each terrain the index of its first use,
	// so paint order decides: the main terrain first, then water and shores, player
	// bases, cliffs and hills, forests and the patches on top.
	std::vector<const char *> order(b.main, b.main + 4);
	for (const char *n : {b.water, b.shore_blend, b.shore}) order.push_back(n);
	if (own) {
		for (const HeightBand &band : own->bands)
			if (!band.late) order.insert(order.end(), band.terrain, band.terrain + 6);
		for (const MapArea &a : t_map_areas)
			for (const AreaLayers &l : own->areas)
				if (l.kind == a.kind)
					for (const auto &layer : l.layer) order.insert(order.end(), layer, layer + 6);
		for (const PatchSet &ps : own->patches)
			for (const auto &layer : ps.layer) order.insert(order.end(), layer, layer + 8);
		for (const HeightBand &band : own->bands)
			if (band.late) order.insert(order.end(), band.terrain, band.terrain + 6);
	}
	for (const char *n : {b.road_wild, b.road}) order.push_back(n);
	for (const char *const *l : {b.cliff, b.hill, b.forest}) order.insert(order.end(), l, l + 2);
	order.insert(order.end(), b.tier, b.tier + 4);
	order.insert(order.end(), b.dirt, b.dirt + 2);
	auto rank = [&](const std::string &n) {
		for (size_t i = 0; i < order.size(); ++i)
			if (order[i] && n == order[i]) return i;
		return order.size();
	};
	std::vector<uint8_t> by_rank(t.names.size());
	for (size_t i = 0; i < by_rank.size(); ++i) by_rank[i] = (uint8_t)i;
	std::stable_sort(by_rank.begin(), by_rank.end(), [&](uint8_t a, uint8_t c) { return rank(t.names[a]) < rank(t.names[c]); });
	std::vector<uint8_t> remap(t.names.size());
	std::vector<std::string> names(t.names.size());
	for (size_t i = 0; i < by_rank.size(); ++i) {
		remap[by_rank[i]] = (uint8_t)i;
		names[i] = t.names[by_rank[i]];
	}
	for (uint8_t &v : t.tiles) v = remap[v];
	t.names = std::move(names);
	sim.set_terrain_tiles(std::move(t));
	t_map_areas.clear();
}

void RmsGenerator::distribute_fish(Simulation &sim, const HeightmapGrid &hm, float water_h, const Config &config) {
	// 0 A.D. rmgen (e.g. archipelago.js, mediterranean.js): groups of 2-3 fish,
	// scaleByMapSize(15, 50) of them, in water at least 2 tiles from land and 20
	// tiles from other food. One RTS0 world unit is one tile. A separate stream
	// keeps every other placement on the map unchanged.
	const float half = hm.map_half;
	const float size = 2.0f * half;
	auto sbms = [&](float lo, float hi) { return lo + (hi - lo) * std::clamp((size - 128.0f) / (512.0f - 128.0f), 0.0f, 1.0f); };
	// One createObjectGroups / createFood pass: its group count, fish per group,
	// stayClasses(clWater, water) margin, avoidClasses(clFood, spacing) and,
	// for Corsica's shore fish, land within shore tiles. `retry` is createObjectGroups'
	// retryFactor; 0 is createObjectGroupsDeprecated (createFood), which makes exactly
	// `groups` tries and keeps the ones that fit. `fauna` places that sea creature (a
	// whale) instead of a fish; whales mark clFood like fish, so passes keep their order.
	struct FishPass {
		int groups, min_n, max_n;
		int water;
		float spacing, spread;
		int shore;
		int retry = 40;
		uint8_t fauna = 0xFF;
		// rmgen2 addFish's SimpleObjects: a fractional count range drawn by randIntInclusive
		// (count_hi > 0 replaces min_n/max_n), and members spread_min-spread off the tile.
		float count_lo = 0.0f, count_hi = 0.0f, spread_min = 0.0f;
	};
	std::vector<FishPass> passes;
	const int players = std::max(1, config.num_players);
	RmsRng rng(config.seed ^ 0x46495348u); // 'FISH'
	// rmgen2 addFish(constraint, size, deviation, fill): groupOffset = size +- deviation
	// (getRandomDeviation), then floor(40 * fill) tries each of groupOffset-2x fish 0-2x
	// off the tile and 2x-4x fish 10x-20x off it (createObjectGroupsDeprecated).
	auto add_fish = [&](float size, float deviation, float fill, int water, float spacing, uint8_t fauna) {
		const float off = size + rng.range_float(-1.0f, 1.0f) * std::min(size, deviation);
		const int tries = (int)std::floor(40.0f * fill);
		FishPass near{tries, 0, 0, water, spacing, 2.0f * off, 0, 0, fauna, off, 2.0f * off, 0.0f};
		FishPass far{tries, 0, 0, water, spacing, 20.0f * off, 0, 0, fauna, 2.0f * off, 4.0f * off, 10.0f * off};
		passes.push_back(near);
		passes.push_back(far);
	};
	switch (config.map_type) {
		case MAP_GULF_OF_BOTHNIA:
			// 0.28 gulf_of_bothnia.js (unfrozen): createFood of 2-3 fish,
			// 4*scaleByMapSize(fish.min, fish.max) with the winter biome's 5-30,
			// stayClasses(clWater, 2), avoidClasses(clFood, 12).
			passes.push_back({(int)std::lround(4.0f * sbms(5.0f, 30.0f)), 2, 3, 2, 12.0f, 2.0f, 0, 0});
			break;
		case MAP_LAND_GRAB:
			// 0.28 land_grab.js: 60 * numPlayers groups of 2-3 fish 2 off the land, 14 apart.
			passes.push_back({60 * players, 2, 3, 2, 14.0f, 2.0f, 0, 0});
			break;
		case MAP_THE_NILE:
			// 0.28 the_nile.js: scaleByMapSize(60, 80) groups of 1-2 tilapia 4 into the river,
			// 12 apart, then 30 more 3 into the ponds, 4 apart.
			passes.push_back({(int)std::lround(sbms(60.0f, 80.0f)), 1, 2, 4, 12.0f, 1.0f, 0, 100});
			passes.push_back({30, 1, 2, 3, 4.0f, 1.0f, 0, 50});
			break;
		case MAP_LATIUM:
			// 0.28 latium.js: scaleByMapSize(45, 65) single fish, stayClasses(clWater, 4),
			// avoidClasses(clFood, 10).
			passes.push_back({(int)std::lround(sbms(45.0f, 65.0f)), 1, 1, 4, 10.0f, 1.0f, 0, 10});
			break;
		case MAP_RIVER_ARCHIPELAGO:
			// 0.28 river_archipelago.js: scaleByMapSize(20, 100) groups of 2-3 fish (0-2 apart)
			// in water 4 tiles from land, avoidClasses(clFood, 15).
			passes.push_back({(int)std::lround(sbms(20.0f, 100.0f)), 2, 3, 4, 15.0f, 2.0f, 0, 20});
			break;
		case MAP_CORSICA:
			// 0.28 corsica.js: 50 * numPlayers groups of 2-3 fish in water 3 tiles
			// from land, 8 apart, then 70 * numPlayers shore fish 6 apart. The
			// script's clShore is its landing beaches' passages; RTS0 takes water
			// within 6 tiles of land for it.
			passes.push_back({50 * players, 2, 3, 3, 8.0f, 2.0f, 0, 0});
			passes.push_back({70 * players, 2, 3, 3, 6.0f, 2.0f, 6, 0});
			break;
		case MAP_MIGRATION:
			// 0.28 migration.js: createObjectGroupsDeprecated of 2-3 fish,
			// scaleByMapSize(500, 700) tries, avoidClasses(clLand, 2, clPlayer, 2, clFood, 10).
			passes.push_back({(int)std::ceil(sbms(500.0f, 700.0f)), 2, 3, 2, 10.0f, 2.0f, 0, 0});
			break;
		// 0.28's water maps each run one createFood of 2-3 fish (0-2 apart): N groups,
		// stayClasses(clWater, w) or avoidClasses(clLand, w), avoidClasses(clFood, d).
		case MAP_CONTINENT: // continent.js: 50 * numPlayers, clLand 2, clFood 10
			passes.push_back({50 * players, 2, 3, 2, 10.0f, 2.0f, 0, 0});
			break;
		case MAP_LAKE: // lake.js: 65 * numPlayers, clWater 4, clFood 12
			passes.push_back({65 * players, 2, 3, 4, 12.0f, 2.0f, 0, 0});
			break;
		case MAP_RIVERS: // rivers.js: 35 * numPlayers, clWater 6, clFood 8
			passes.push_back({35 * players, 2, 3, 6, 8.0f, 2.0f, 0, 0});
			break;
		case MAP_ARCHIPELAGO_0AD: // archipelago.js: 35 * numPlayers, clLand 3, clFood 15
			passes.push_back({35 * players, 2, 3, 3, 15.0f, 2.0f, 0, 0});
			break;
		case MAP_COAST_RANGE: // coast_range.js: 70 * numPlayers, clLand 2, clFood 7
			passes.push_back({70 * players, 2, 3, 2, 7.0f, 2.0f, 0, 0});
			break;
		case MAP_ENGLISH_CHANNEL: // english_channel.js: scaleByMapSize(30, 45) * numPlayers, clWater 4, clFood 6
			passes.push_back({(int)std::lround(sbms(30.0f, 45.0f) * (float)players), 2, 3, 4, 6.0f, 2.0f, 0, 0});
			break;
		case MAP_AEGEAN_SEA: // aegean_sea.js: 25 * scaleByMapSize(15, 20), clWater 5, clFood 10
			passes.push_back({(int)std::lround(25.0f * sbms(15.0f, 20.0f)), 2, 3, 5, 10.0f, 2.0f, 0, 0});
			break;
		case MAP_ISLANDS: // islands.js: 40 * numPlayers, clLand 4, clFood 14
			passes.push_back({40 * players, 2, 3, 4, 14.0f, 2.0f, 0, 0});
			break;
		case MAP_ALPINE_LAKES: // alpine_lakes.js: 20 * numPlayers, clWater 2, clFood 8
			passes.push_back({20 * players, 2, 3, 2, 8.0f, 2.0f, 0, 0});
			break;
		case MAP_ARCTIC_SUMMER:
			// arctic_summer.js: createFood of 3 * numPlayers single fin whales, then as many
			// humpbacks (0-3 off the tile), stayClasses(clWater, 6), avoidClasses(clFood, 20);
			// then 25 * numPlayers groups of 2-3 fish, clWater 6, clFood 10.
			passes.push_back({3 * players, 1, 1, 6, 20.0f, 3.0f, 0, 0, FAUNA_WHALE_FIN});
			passes.push_back({3 * players, 1, 1, 6, 20.0f, 3.0f, 0, 0, FAUNA_WHALE_HUMPBACK});
			passes.push_back({25 * players, 2, 3, 6, 10.0f, 2.0f, 0, 0});
			break;
		case MAP_POLAR_SEA:
			// polar_sea.js: createFood of scaleByMapSize(1, 6) * 3 groups of 1-2 fin whales,
			// then as many humpbacks (0-2 apart), clWater 6, clFood 20; then 100 groups of
			// 2-3 fish, clWater 6, clFood 12. (Its clHill 5 lies on land, past clWater 6.)
			passes.push_back({(int)std::ceil(sbms(1.0f, 6.0f) * 3.0f), 1, 2, 6, 20.0f, 2.0f, 0, 0, FAUNA_WHALE_FIN});
			passes.push_back({(int)std::ceil(sbms(1.0f, 6.0f) * 3.0f), 1, 2, 6, 20.0f, 2.0f, 0, 0, FAUNA_WHALE_HUMPBACK});
			passes.push_back({100, 2, 3, 6, 12.0f, 2.0f, 0, 0});
			break;
		case MAP_CYCLADIC_ARCHIPELAGO:
			// cycladic_archipelago.js: scaleByMapSize(100, 250) single fish 0-3 off the tile,
			// stayClasses(clWater, 1), clFood 8; then scaleByMapSize(10, 40) humpbacks the same,
			// also avoiding the islands (clIsland, clPlayer) by 4.
			passes.push_back({(int)std::ceil(sbms(100.0f, 250.0f)), 1, 1, 1, 8.0f, 3.0f, 0, 0});
			passes.push_back({(int)std::ceil(sbms(10.0f, 40.0f)), 1, 1, 4, 8.0f, 3.0f, 0, 0, FAUNA_WHALE_HUMPBACK});
			break;
		case MAP_ISLAND_STRONGHOLD:
			// island_stronghold.js: 25 * numPlayers groups of 2-3 fish, avoidClasses(clLand, 4,
			// clFood, 20), retryFactor 60; then scaleByMapSize(5, 20) single humpbacks 0-3 off
			// the tile, clLand 4, clFood 8.
			passes.push_back({25 * players, 2, 3, 4, 20.0f, 2.0f, 0, 0});
			passes.push_back({(int)std::ceil(sbms(5.0f, 20.0f)), 1, 1, 4, 8.0f, 3.0f, 0, 0, FAUNA_WHALE_HUMPBACK});
			break;
		case MAP_MEDITERRANEAN:
			// mediterranean.js: addFish "normal" size, "similar" mix, "many" amount, stay
			// clWater 4, avoid fish 10; then fin whales the same way, "small", "same",
			// "scarce", stay clWater 7, avoid fish 2 (its desert and steppe classes are
			// never painted, so their avoidance holds everywhere).
			add_fish(1.0f, 0.1f, 1.75f, 4, 10.0f, 0xFF);
			add_fish(0.75f, 0.0f, 0.2f, 7, 2.0f, FAUNA_WHALE_FIN);
			break;
		default:
			passes.push_back({(int)std::lround(sbms(15.0f, 50.0f)), 2, 3, 2, 20.0f, 2.0f, 0});
			break;
	}
	// rmgen works on tiles: a constraint is tested at a position's tile (floor), over the
	// tiles whose index lies within its radius (TileClass.countInRadius). clWater is the
	// tiles under the water plane.
	const int tiles = (int)std::lround(size);
	auto in_map = [&](int tx, int tz) { return tx >= 0 && tz >= 0 && tx < tiles && tz < tiles; };
	auto wet = [&](int tx, int tz) { return hm.sample_world((float)tx + 0.5f - half, (float)tz + 0.5f - half) < water_h; };
	auto stay_water = [&](int tx, int tz, int r) { // stayClasses(clWater, r) / avoidClasses(clLand, r)
		for (int dz = -r; dz <= r; ++dz)
			for (int dx = -r; dx <= r; ++dx)
				if (dx * dx + dz * dz <= r * r && in_map(tx + dx, tz + dz) && !wet(tx + dx, tz + dz)) return false;
		return true;
	};
	auto near_land = [&](int tx, int tz, int r) {
		for (int dz = -r; dz <= r; ++dz)
			for (int dx = -r; dx <= r; ++dx)
				if (dx * dx + dz * dz <= r * r && in_map(tx + dx, tz + dz) && !wet(tx + dx, tz + dz)) return true;
		return false;
	};
	std::vector<std::pair<int, int>> food; // clFood: each fish's tile
	const bool circle = sim.circular_map();
	constexpr float BORDER = 3.0f; // MAP_BORDER_WIDTH: randomCoordinate(true) and validTilePassable
	for (const FishPass &f : passes) {
		auto allows = [&](float x, float z) {
			const int tx = (int)std::floor(x + half), tz = (int)std::floor(z + half);
			if (!in_map(tx, tz) || std::fabs(x) > half - BORDER || std::fabs(z) > half - BORDER) return false;
			// Past the edge ships can't sail, and keep_resources_passable would pull a fish there onto the shore.
			if (sim.past_map_edge(x, z)) return false;
			if (!stay_water(tx, tz, f.water)) return false;
			if (f.shore > 0 && !near_land(tx, tz, f.water + f.shore)) return false;
			const int d = (int)f.spacing;
			for (const auto &p : food)
				if ((p.first - tx) * (p.first - tx) + (p.second - tz) * (p.second - tz) <= d * d) return false;
			return true;
		};
		const int max_fail = f.groups * std::max(1, f.retry);
		int placed = 0, tries = 0, bad = 0;
		while (f.retry == 0 ? tries < f.groups : placed < f.groups && bad <= max_fail) {
			++tries;
			// g_Map.randomCoordinate(true), floored to a tile.
			float cx, cz;
			if (circle) {
				const auto c = random_in_circle(rng, half - BORDER);
				cx = c.first;
				cz = c.second;
			} else {
				cx = rng.range_float(-half + BORDER, half - BORDER);
				cz = rng.range_float(-half + BORDER, half - BORDER);
			}
			cx = std::floor(cx + half) - half;
			cz = std::floor(cz + half) - half;
			// SimpleGroup.place (avoidSelf): each fish 0-spread from the tile's centre, 1 apart,
			// on a tile the constraint allows; 30 failed spots and the whole group is dropped.
			std::vector<std::pair<float, float>> group;
			int n = f.min_n == f.max_n ? f.min_n : rng.range_int(f.min_n, f.max_n);
			if (f.count_hi > 0.0f) {
				const float u = rng.range_float(0.0f, 1.0f);
				n = (int)std::floor(f.count_lo + u * (f.count_hi + 1.0f - f.count_lo)); // randIntInclusive
			}
			int retries = 0;
			bool ok = true;
			for (int k = 0; k < n && ok; ++k)
				for (;;) {
					// One draw per statement: compilers order a call's arguments differently.
					const float dist = rng.range_float(f.spread_min, f.spread);
					const float ang = rng.range_float(0.0f, kTwoPi);
					const auto o = polar(ang, dist);
					const float fx = cx + 0.5f + o.first, fz = cz + 0.5f + o.second;
					bool apart = true;
					for (const auto &g : group)
						if ((g.first - fx) * (g.first - fx) + (g.second - fz) * (g.second - fz) < 1.0f) { apart = false; break; }
					if (apart && allows(fx, fz)) {
						group.push_back({fx, fz});
						break;
					}
					if (retries++ > 30) { ok = false; break; }
				}
			if (!ok) {
				++bad;
				continue;
			}
			++placed;
			for (const auto &g : group) {
				if (f.fauna != 0xFF) sim.spawn_fauna(g.first, g.second, f.fauna);
				else sim.spawn_resource_world(g.first, g.second, RES_FOOD, FISH_FOOD_SUPPLY, RES_SUB_FISH);
				food.push_back({(int)std::floor(g.first + half), (int)std::floor(g.second + half)});
			}
		}
	}
}

void RmsGenerator::top_up_resources(Simulation &sim, const HeightmapGrid &hm, float water_h, const std::vector<PlayerSpawn> &spawns,
		const Config &config, size_t base_resources, size_t base_fauna) {
	// 0 A.D.'s scripts each place their own Gaia mines, fruit and hunt (createBalancedMines,
	// createMines, createFood, rmgen2 addMetal/addStone/addBerries/addAnimals). Their counts
	// outside the player bases, run per map size and player count, are in
	// resource_counts_data.h; whatever the passes above already placed counts toward them,
	// and the rest goes on open land away from the bases, mines and food apart. A separate
	// stream keeps every other placement on the map unchanged.
	const MapResourceCounts *rc = map_resource_counts(map_type_info(config.map_type).id);
	if (!rc) return;
	const float half = hm.map_half, tiles = 2.0f * half;
	auto target = [&](const uint16_t (&grid)[4][3]) { return map_resource_count(grid, tiles, config.num_players); };
	auto sbms = [&](float lo, float hi) { return lo + (hi - lo) * std::clamp((tiles - 128.0f) / 384.0f, 0.0f, 1.0f); };

	// Taken ground: every resource (trees included) on a 2-unit grid, so nothing lands on another.
	const int cells = std::max(1, (int)std::ceil(tiles / 2.0f));
	std::vector<uint8_t> taken((size_t)cells * (size_t)cells, 0);
	auto cell = [&](float x, float z) {
		const int cx = std::clamp((int)((x + half) / 2.0f), 0, cells - 1), cz = std::clamp((int)((z + half) / 2.0f), 0, cells - 1);
		return (size_t)cz * (size_t)cells + (size_t)cx;
	};
	auto free_at = [&](float x, float z) {
		for (int dz = -1; dz <= 1; ++dz)
			for (int dx = -1; dx <= 1; ++dx)
				if (taken[cell(x + 2.0f * (float)dx, z + 2.0f * (float)dz)]) return false;
		return true;
	};
	int have_ml = 0, have_ms = 0, have_sl = 0, have_ss = 0, have_fruit = 0, have_hunt = 0;
	std::vector<std::pair<float, float>> mines, food;
	const auto rv = sim.resources();
	for (size_t i = 0; i < rv.count; ++i) {
		if (!rv.alive[i]) continue;
		taken[cell(rv.x[i], rv.z[i])] = 1;
		const bool ruins = rv.subtype[i] == RES_SUB_RUINS, large = rv.amount[i] >= 2000;
		if ((rv.type[i] == RES_GOLD || rv.type[i] == RES_STONE) && !ruins) {
			mines.push_back({rv.x[i], rv.z[i]});
			if (i < base_resources) continue;
			if (rv.type[i] == RES_GOLD) ++(large ? have_ml : have_ms);
			else ++(large ? have_sl : have_ss);
		} else if (rv.type[i] == RES_FOOD && rv.subtype[i] == RES_SUB_NONE) {
			food.push_back({rv.x[i], rv.z[i]});
			if (i >= base_resources) ++have_fruit;
		}
	}
	const auto fv = sim.fauna();
	for (size_t i = 0; i < fv.count; ++i) {
		if (!fv.alive[i] || fauna_type_info(fv.fauna_type[i]).food_amount <= 0) continue;
		food.push_back({fv.pos_x[i], fv.pos_z[i]});
		if (i >= base_fauna && !fauna_type_info(fv.fauna_type[i]).is_naval()) ++have_hunt; // whales are not land hunt
	}

	RmsRng rng(config.seed ^ 0x52535243u); // 'RSRC'
	// Kept clear of every civic centre: past 0.28's placeStartingWalls ring (about 20
	// tiles out on Iberian bases), which goes up after these.
	constexpr float kBaseGap = 24.0f;
	auto apart = [](const std::vector<std::pair<float, float>> &v, float x, float z, float d) {
		for (const auto &p : v)
			if ((p.first - x) * (p.first - x) + (p.second - z) * (p.second - z) < d * d) return false;
		return true;
	};
	auto open_land = [&](float x, float z) {
		return x > -half + 3.0f && x < half - 3.0f && z > -half + 3.0f && z < half - 3.0f &&
		       hm.sample_world(x, z) >= water_h + 0.4f && hm.compute_slope_world(x, z) <= 0.7f && free_at(x, z);
	};
	// A spot on open land base_gap from every civic centre and spacing from the others in
	// near; when none turns up, the spacing halves (twice) before giving up.
	auto pick = [&](float base_gap, const std::vector<std::pair<float, float>> &near, float spacing, float &ox, float &oz) {
		for (float sp = spacing; sp >= spacing * 0.25f; sp *= 0.5f)
			for (int attempt = 0; attempt < 300; ++attempt) {
				const float x = rng.range_float(-half, half), z = rng.range_float(-half, half);
				if (!open_land(x, z)) continue;
				bool near_base = false;
				for (const auto &sp_ : spawns)
					near_base |= (sp_.x - x) * (sp_.x - x) + (sp_.z - z) * (sp_.z - z) < base_gap * base_gap;
				if (near_base || !apart(near, x, z, sp)) continue;
				ox = x;
				oz = z;
				return true;
			}
		return false;
	};
	auto place = [&](float x, float z, uint8_t type, int32_t amount, uint8_t flora = 0) {
		if (!open_land(x, z)) return false;
		sim.spawn_resource_world(x, z, type, amount, RES_SUB_NONE, 0, flora);
		taken[cell(x, z)] = 1;
		return true;
	};

	// Mines: the large ones alone and far apart, the small ones in clusters of 2-3
	// (createBalancedMines' SimpleObject(oSmall, 2, 3, 0, 2)).
	struct MinePass { uint8_t type; int want; int32_t amount; bool large; };
	const MinePass passes[4] = {
		{RES_GOLD, target(rc->metal_large) - have_ml, rc->metal_large_amount, true},
		{RES_STONE, target(rc->stone_large) - have_sl, rc->stone_large_amount, true},
		{RES_GOLD, target(rc->metal_small) - have_ms, rc->metal_small_amount, false},
		{RES_STONE, target(rc->stone_small) - have_ss, rc->stone_small_amount, false},
	};
	for (const MinePass &mp : passes) {
		for (int left = mp.want; left > 0;) {
			float x, z;
			if (!pick(kBaseGap, mines, mp.large ? sbms(14.0f, 28.0f) : sbms(10.0f, 20.0f), x, z)) break;
			mines.push_back({x, z});
			const int n = mp.large ? 1 : std::min(left, rng.range_int(2, 3));
			int placed = 0;
			for (int k = 0; k < n; ++k) {
				const float a = 6.2831853f * (float)k / (float)n + rng.range_float(0.0f, 1.0f);
				placed += place(k ? x + det::cos(a) * 1.8f : x, k ? z + det::sin(a) * 1.8f : z, mp.type, mp.amount);
			}
			left -= std::max(placed, 1);
		}
	}

	// Fruit: berry bushes in clusters of 5-7 within 4 tiles (createFood's SimpleObject(oFruitBush,
	// 5, 7, 0, 4) in most 0.28 scripts), fruit trees in clusters of 2-4, each kind by its share of
	// the script's fruit.
	// A script on a generic biome was measured on temperate, whose tree3 is the apple
	// (rmbiome/generic/temperate.json). On any other generic biome tree3 is a plain tree, so
	// its fruit here is the biome's fruitBush alone, left unnamed for plant_flora, without
	// the apple share.
	const bool generic = own_terrains(sim, config.map_type) == nullptr && config.biome != BIOME_TEMPERATE;
	int fruit_want = target(rc->fruit);
	const ResourceKindShare *bush = &rc->fruit_kinds[0];
	if (generic) {
		for (const ResourceKindShare &k : rc->fruit_kinds)
			if (k.name && !std::strcmp(k.name, "apple")) fruit_want = fruit_want * (100 - k.percent) / 100;
			else if (k.name) bush = &k;
	}
	int fruit_left = fruit_want - have_fruit;
	while (fruit_left > 0 && rc->fruit_kinds[0].name) {
		const ResourceKindShare *k = &rc->fruit_kinds[0];
		if (generic) k = bush;
		else if (rc->fruit_kinds[1].name && rng.range_int(1, 100) > rc->fruit_kinds[0].percent) k = &rc->fruit_kinds[1];
		if (generic && !std::strcmp(k->name, "apple")) break;
		uint8_t flora = generic ? (uint8_t)FLORA_BERRY : flora_kind_by_name(k->name);
		// Game setup chose temperate: its apples are the biome's tree3 (fruit/apple).
		if (!generic && !own_terrains(sim, config.map_type) && !std::strcmp(k->name, "apple"))
			if (const BiomeFlora *bf = biome_flora("temperate"))
				for (uint8_t t : bf->tree3)
					if (flora_is_fruit(t)) { flora = t; break; }
		float x, z;
		if (flora == FLORA_COUNT || !pick(kBaseGap, food, 10.0f, x, z)) break;
		food.push_back({x, z});
		const bool berries = flora == FLORA_BERRY;
		const int n = std::min(fruit_left, berries ? rng.range_int(5, 7) : rng.range_int(2, 4));
		int placed = 0;
		for (int i = 0; i < n; ++i) {
			const float a = rng.range_float(0.0f, 6.2831853f);
			const float r = i ? rng.range_float(1.0f, berries ? 4.0f : 3.0f) : 0.0f;
			placed += place(x + det::cos(a) * r, z + det::sin(a) * r, RES_FOOD, generic ? 200 : k->amount, generic ? FLORA_DEFAULT : flora);
		}
		fruit_left -= std::max(placed, 1);
	}

	// Hunt: each herd one of the script's species by share; a generic-biome script's animals
	// are the biome's (main hunt twice as often as the secondary), as when the game setup picks
	// another biome. 0.28's createFood herds: the main hunt 5-7 within 4 tiles, the secondary
	// 2-3 within 2 (mainland.js and the like); dangerous animals alone or in pairs.
	const bool own_kinds = rc->hunt_kinds[0].name && sim.map_biome_pick(config.map_type) < 0;
	int hunt_left = target(rc->hunt) - have_hunt;
	for (int herd = 0; hunt_left > 0; ++herd) {
		uint8_t ftype = FAUNA_COUNT;
		bool main_hunt = true; // the script's largest share, or the biome's main hunt
		if (own_kinds) {
			int roll = rng.range_int(1, 100);
			int kind = 0;
			for (const auto &k : rc->hunt_kinds) {
				if (!k.name) break;
				ftype = fauna_type_by_name(k.name);
				main_hunt = kind++ == 0;
				if ((roll -= k.percent) <= 0) break;
			}
		}
		if (ftype >= FAUNA_COUNT) {
			const BiomeFauna bf = biome_fauna(config.biome, herd);
			main_hunt = herd % 3 != 2;
			ftype = main_hunt ? bf.main_hunt : bf.secondary_hunt;
		}
		const FaunaTypeInfo &fi = fauna_type_info(ftype);
		float x, z;
		if (!pick(kBaseGap, food, 10.0f, x, z)) break;
		food.push_back({x, z});
		const int n = std::min(hunt_left, fi.is_dangerous() ? rng.range_int(1, 2) : main_hunt ? rng.range_int(5, 7) : rng.range_int(2, 3));
		for (int i = 0; i < n; ++i) {
			const float a = 6.2831853f * (float)i / (float)n, r = rng.range_float(1.0f, main_hunt ? 4.0f : 2.0f);
			sim.spawn_fauna(x + det::cos(a) * r, z + det::sin(a) * r, ftype);
		}
		hunt_left -= n;
	}
}

} // namespace rtso
