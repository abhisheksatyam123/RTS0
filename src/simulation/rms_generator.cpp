#include "rms_generator.h"
#include "simulation.h"
#include "heightmap_images.h"
#include <cmath>
#include <algorithm>
#include <vector>
#include <cstring>
#include <limits>
#include <queue>

namespace rtso {

namespace {

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

		scale *= std::pow(0.5f, roughness);
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

			float angle = std::atan2(dz, dx);
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
	float cos_a = std::cos(angle_rad);
	float sin_a = std::sin(angle_rad);
	float k = 6.2831853f / wavelength;

	for (int z = 0; z < res; ++z) {
		float wz = -map_half + z * step;
		for (int x = 0; x < res; ++x) {
			float wx = -map_half + x * step;

			// Rotate coordinates to dune wind direction
			float u = wx * cos_a + wz * sin_a;
			float v = -wx * sin_a + wz * cos_a;

			// Primary sinusoidal dune wave with windward gentle slope and leeward steep slip-face
			float phase = u * k + 0.35f * std::sin(v * k * 0.4f);
			float wave = std::sin(phase);
			float shaped_wave = (wave > 0.0f) ? std::pow(wave, 0.85f) : -std::pow(-wave, 1.2f);

			// Secondary cross-ripple
			float secondary = std::sin(v * k * 1.8f + u * k * 0.5f) * secondary_amp;

			add(x, z, shaped_wave * amplitude + secondary);
		}
	}
}

void HeightmapGrid::raise_sandstone_mesa(float center_wx, float center_wz, float radius_base, float radius_top, float height_gain, float ramp1_angle_rad, float ramp2_angle_rad, float ramp_width_rad) {
	float step = (map_half * 2.0f) / (float)(res - 1);

	for (int z = 0; z < res; ++z) {
		float wz = -map_half + z * step;
		for (int x = 0; x < res; ++x) {
			float wx = -map_half + x * step;
			float dx = wx - center_wx;
			float dz = wz - center_wz;
			float dist = std::sqrt(dx * dx + dz * dz);

			if (dist > radius_base) continue;

			float angle = std::atan2(dz, dx);

			auto check_ramp = [&](float ramp_angle) {
				float diff = std::abs(angle - ramp_angle);
				while (diff > 3.14159265f) diff = std::abs(diff - 6.2831853f);
				return diff;
			};

			float diff1 = check_ramp(ramp1_angle_rad);
			float diff2 = check_ramp(ramp2_angle_rad);
			float min_ramp_diff = std::min(diff1, diff2);

			if (min_ramp_diff < ramp_width_rad && dist > radius_top * 0.4f) {
				// Access ramp sector: natural slope up the cliff face
				float t_dist = (dist - radius_top * 0.4f) / (radius_base - radius_top * 0.4f);
				float ramp_h = height_gain * (1.0f - t_dist);
				float lateral_blend = 1.0f - (min_ramp_diff / ramp_width_rad);
				add(x, z, ramp_h * lateral_blend + height_gain * (1.0f - lateral_blend) * (1.0f - smoothstep(radius_top, radius_base, dist)));
			} else if (dist <= radius_top) {
				// Mesa summit plateau
				add(x, z, height_gain);
			} else {
				// Sheer sandstone cliff face
				float t = smoothstep(radius_top, radius_base, dist);
				float cliff_drop = std::pow(1.0f - t, 0.7f);
				add(x, z, height_gain * cliff_drop);
			}
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
// Places n gaia/tree trees (TREE_WOOD_SUPPLY wood each) for one tree spot: the
// first on the spot, the rest around it at `spread`, on ground above min_h. RTS0's
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
		const float tx = x + std::cos(a) * spread, tz = z + std::sin(a) * spread;
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
		float sx = std::cos(angle) * spawn_r;
		float sz = std::sin(angle) * spawn_r;

		// Face center
		float fx = -std::cos(angle);
		float fz = -std::sin(angle);

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

	// 1. Home Islands for Players
	float spawn_r = half * 0.56f;
	for (int i = 0; i < num_players; ++i) {
		float angle = (6.2831853f * i) / (float)num_players + 0.35f;
		float sx = std::cos(angle) * spawn_r;
		float sz = std::sin(angle) * spawn_r;
		float fx = -std::cos(angle);
		float fz = -std::sin(angle);

		// Raise player island
		hm.level_area(sx, sz, 16.0f, 12.0f, 4.5f);
		spawns.push_back({(uint8_t)i, sx, sz, 4.5f, fx, fz});
	}

	// 2. Neutral Islands (Center & Intermediary)
	hm.level_area(0.0f, 0.0f, 14.0f, 10.0f, 4.0f); // Rich central relic island

	for (int i = 0; i < num_players; ++i) {
		float angle = (6.2831853f * (i + 0.5f)) / (float)num_players + 0.35f;
		float nx = std::cos(angle) * (half * 0.65f);
		float nz = std::sin(angle) * (half * 0.65f);
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
		float sx = std::cos(angle) * spawn_r;
		float sz = std::sin(angle) * spawn_r;
		float fx = -std::cos(angle);
		float fz = -std::sin(angle);
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
		float sx = std::cos(angle) * spawn_r;
		float sz = std::sin(angle) * spawn_r;
		float fx = -std::cos(angle);
		float fz = -std::sin(angle);

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
			float row = (float)(i / 2) - (float)(num_players / 4);
			float sx = side * half * 0.58f;
			float sz = row * 22.0f;
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
		float sx = std::cos(angle) * spawn_r;
		float sz = std::sin(angle) * spawn_r;
		float fx = -std::cos(angle);
		float fz = -std::sin(angle);
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
		float sx = std::cos(angle) * spawn_r;
		float sz = std::sin(angle) * spawn_r;
		float fx = -std::cos(angle);
		float fz = -std::sin(angle);
		float ramp_angle = std::atan2(fz, fx); // Ramp slopes down towards center

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
		float sx = std::cos(angle) * spawn_r;
		float sz = std::sin(angle) * spawn_r;
		float fx = -std::cos(angle);
		float fz = -std::sin(angle);
		float bh = hm.sample_world(sx, sz);

		hm.level_area(sx, sz, 14.0f, 6.0f, bh);
		spawns.push_back({(uint8_t)i, sx, sz, bh, fx, fz});
	}
}

void RmsGenerator::synth_peloponnese(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	water_h = 0.5f;
	synth_continental(hm, rng, water_h, spawns, num_players);
}

void RmsGenerator::synth_danubius(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	water_h = 0.5f;
	hm.init(hm.res, hm.map_half, 2.6f);

	float half = hm.map_half;

	// 1. Base rolling temperate river valley terrain with fBm
	hm.apply_fbm(rng, 4, 0.028f, 0.52f, 2.1f, 3.8f);
	hm.diamond_square(rng, 1.8f, 0.60f);

	// Perimeter gentle falloff and smoothing come before the channels, so the
	// blur can't sink the fords below wading depth (bases were cut off), and the
	// valley floor is lifted so the water is the two channels, not a lake.
	lift_noise_to_land(hm, water_h, 0.95f, 5);
	hm.apply_radial_falloff(0.85f, 0.98f, 1.0f);
	hm.blur(2, 0.42f);

	// 2. Dual-channel river branches around central island fortress
	// Channel 1: North Branch
	std::vector<std::pair<float, float>> north_branch = {
		{-half * 0.92f, -half * 0.10f},
		{-half * 0.55f, -half * 0.24f},
		{-half * 0.22f, -half * 0.38f},
		{0.0f,          -half * 0.42f},
		{half * 0.22f,  -half * 0.38f},
		{half * 0.55f,  -half * 0.24f},
		{half * 0.92f,  -half * 0.10f}
	};

	// Channel 2: South Branch
	std::vector<std::pair<float, float>> south_branch = {
		{-half * 0.92f,  half * 0.10f},
		{-half * 0.55f,  half * 0.24f},
		{-half * 0.22f,  half * 0.38f},
		{0.0f,           half * 0.42f},
		{half * 0.22f,   half * 0.38f},
		{half * 0.55f,   half * 0.24f},
		{half * 0.92f,   half * 0.10f}
	};

	// Carve both channels with central walkable fords
	float ford_dist_n = half * 0.90f;
	float ford_dist_s = half * 0.90f;
	hm.carve_spline_path(north_branch, 4.2f, 2.2f, water_h, ford_dist_n, 8.0f);
	hm.carve_spline_path(south_branch, 4.2f, 2.2f, water_h, ford_dist_s, 8.0f);

	// 3. Central Fortress Island & Sandbars
	hm.level_area(0.0f, 0.0f, half * 0.18f, 6.0f, 4.5f);
	// Sandbars upstream and downstream
	hm.level_area(-half * 0.30f, 0.0f, 4.5f, 4.0f, 1.2f);
	hm.level_area( half * 0.30f, 0.0f, 4.5f, 4.0f, 1.2f);


	// Re-level central fortress island summit
	hm.level_area(0.0f, 0.0f, half * 0.14f, 4.0f, 4.5f);

	// 5. Opposing riverbank player spawns
	spawns.clear();
	if (num_players == 2) {
		float sx0 = 0.0f, sz0 = -half * 0.62f;
		float sx1 = 0.0f, sz1 =  half * 0.62f;
		float bh0 = std::max(water_h + 1.5f, hm.sample_world(sx0, sz0));
		float bh1 = std::max(water_h + 1.5f, hm.sample_world(sx1, sz1));
		hm.level_area(sx0, sz0, 14.0f, 6.0f, bh0);
		hm.level_area(sx1, sz1, 14.0f, 6.0f, bh1);
		spawns.push_back({0, sx0, sz0, bh0, 0.0f, 1.0f});
		spawns.push_back({1, sx1, sz1, bh1, 0.0f, -1.0f});
	} else {
		for (int i = 0; i < num_players; ++i) {
			float side = (i % 2 == 0) ? -1.0f : 1.0f;
			float col = (float)(i / 2) - (float)(num_players / 4);
			float sx = col * 24.0f;
			float sz = side * half * 0.62f;
			float fx = 0.0f;
			float fz = -side;
			float bh = std::max(water_h + 1.5f, hm.sample_world(sx, sz));
			hm.level_area(sx, sz, 14.0f, 6.0f, bh);
			spawns.push_back({(uint8_t)i, sx, sz, bh, fx, fz});
		}
	}
}

void RmsGenerator::synth_saharan_oases(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	water_h = 0.0f;
	hm.init(hm.res, hm.map_half, 2.8f);

	float half = hm.map_half;

	// 1. Sinusoidal Sand Dunes Generation
	hm.apply_sinusoidal_dunes(0.45f, 22.0f, 2.4f, 0.6f);
	hm.apply_fbm(rng, 3, 0.022f, 0.55f, 2.0f, 2.5f);
	hm.terrace(1.4f, 0.35f);

	// 2. Freshwater Oases System (Central Grand Oasis + 4 Quadrant Satellite Oases)
	float central_oasis_r = half * 0.16f;
	hm.carve_crater_lake(0.0f, 0.0f, central_oasis_r, 6.0f, 3.2f, water_h);

	float sat_r = half * 0.44f;
	float sat_radius = half * 0.10f;
	hm.carve_crater_lake(-sat_r, -sat_r * 0.65f, sat_radius, 5.0f, 2.8f, water_h);
	hm.carve_crater_lake( sat_r, -sat_r * 0.65f, sat_radius, 5.0f, 2.8f, water_h);
	hm.carve_crater_lake(-sat_r,  sat_r * 0.65f, sat_radius, 5.0f, 2.8f, water_h);
	hm.carve_crater_lake( sat_r,  sat_r * 0.65f, sat_radius, 5.0f, 2.8f, water_h);

	// 3. Perimeter High Dunes Ridge
	hm.apply_border_falloff(half * 0.20f, 6.5f);
	hm.blur(1, 0.35f);

	// 4. Symmetrical Player Spawns in Desert Ring
	spawns.clear();
	float spawn_r = half * 0.56f;
	for (int i = 0; i < num_players; ++i) {
		float angle = (6.2831853f * i) / (float)num_players + 0.392f;
		float sx = std::cos(angle) * spawn_r;
		float sz = std::sin(angle) * spawn_r;
		float fx = -std::cos(angle);
		float fz = -std::sin(angle);
		float bh = std::max(water_h + 1.5f, hm.sample_world(sx, sz));

		spawns.push_back({(uint8_t)i, sx, sz, bh, fx, fz});
		hm.level_area(sx, sz, 14.0f, 6.0f, bh);
	}
}

void RmsGenerator::synth_anatolian_plateau(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	water_h = -20.0f; // High inland dry plateau
	hm.init(hm.res, hm.map_half, 8.0f); // High base steppe elevation

	float half = hm.map_half;

	// 1. High Steppe Undulating Noise
	hm.apply_fbm(rng, 4, 0.024f, 0.50f, 2.0f, 3.0f);
	hm.apply_ridged_noise(rng, 3, 0.035f, 0.45f, 2.0f, 2.5f);

	// 2. Perimeter Canyon Ravines & Slicing Fissures
	hm.carve_canyon_network(rng, half * 0.22f, 4.5f, 9.0f);

	// 3. Rocky Terracing & Erosion
	hm.terrace(1.8f, 0.20f);
	hm.thermal_erosion(8, 0.55f, 0.20f);
	hm.blur(1, 0.35f);

	// 4. Steppe Player Bases on the high plateau
	spawns.clear();
	float spawn_r = half * 0.48f;
	for (int i = 0; i < num_players; ++i) {
		float angle = (6.2831853f * i) / (float)num_players + 0.52f;
		float sx = std::cos(angle) * spawn_r;
		float sz = std::sin(angle) * spawn_r;
		float fx = -std::cos(angle);
		float fz = -std::sin(angle);
		float bh = std::max(6.0f, hm.sample_world(sx, sz));

		hm.level_area(sx, sz, 15.0f, 6.0f, bh);
		spawns.push_back({(uint8_t)i, sx, sz, bh, fx, fz});
	}
}

void RmsGenerator::synth_jebel_barkal(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	water_h = 0.0f;
	hm.init(hm.res, hm.map_half, 2.0f); // Savanna base level

	float half = hm.map_half;

	// 1. Savanna Rolling Base Terrain
	hm.apply_fbm(rng, 3, 0.026f, 0.52f, 2.0f, 2.2f);

	// 2. Flanking Nile River along southern border
	std::vector<std::pair<float, float>> nile_river = {
		{-half * 0.95f, -half * 0.72f},
		{-half * 0.45f, -half * 0.76f},
		{0.0f,          -half * 0.80f},
		{half * 0.45f,  -half * 0.74f},
		{half * 0.95f,  -half * 0.70f}
	};
	hm.carve_spline_path(nile_river, 5.0f, 2.5f, water_h);

	// 3. Iconic Central Sandstone Mesa (Jebel Barkal)
	// Base radius 18m, Summit top radius 11m, Height gain +13.5m, North & South access ramps
	float ramp1_angle = 3.14159265f * 0.5f;  // North ramp
	float ramp2_angle = -3.14159265f * 0.5f; // South ramp
	hm.raise_sandstone_mesa(0.0f, half * 0.06f, 18.0f, 11.0f, 13.5f, ramp1_angle, ramp2_angle, 0.55f);

	// Flat high-ground summit plateau
	hm.level_area(0.0f, half * 0.06f, 9.0f, 2.5f, 15.5f);

	// 4. Surrounding Savanna Outcrops & Hillocks
	hm.raise_plateau(-half * 0.48f,  half * 0.42f, 8.0f, 0.0f, 0.5f, 4.0f, 3.0f);
	hm.raise_plateau( half * 0.48f,  half * 0.42f, 8.0f, 3.14f, 0.5f, 4.0f, 3.0f);

	hm.blur(1, 0.32f);
	// Re-level summit for clean unit combat
	hm.level_area(0.0f, half * 0.06f, 8.5f, 2.0f, 15.5f);

	// 5. Surrounding Savanna Player Spawns facing Jebel Barkal
	spawns.clear();
	float spawn_r = half * 0.56f;
	for (int i = 0; i < num_players; ++i) {
		float angle = (6.2831853f * i) / (float)num_players + 0.40f;
		float sx = std::cos(angle) * spawn_r;
		float sz = std::sin(angle) * spawn_r + half * 0.06f;
		float fx = -std::cos(angle);
		float fz = -std::sin(angle);
		float bh = std::max(water_h + 1.2f, hm.sample_world(sx, sz));

		hm.level_area(sx, sz, 14.0f, 6.0f, bh);
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
		float sx = std::cos(angle) * radius;
		float sz = std::sin(angle) * radius;
		float bh = std::max(min_h, hm.sample_world(sx, sz));
		spawns.push_back({(uint8_t)i, sx, sz, bh, -std::cos(angle), -std::sin(angle)});
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

void RmsGenerator::synth_mainland(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// mainland.js: one land mass, no water, gentle hills.
	water_h = -10.0f;
	hm.init(hm.res, hm.map_half, 2.0f);
	hm.apply_fbm(rng, 4, 0.018f, 0.5f, 2.0f, 3.0f);
	hm.blur(2, 0.4f);
	place_ring_spawns(hm, spawns, num_players, hm.map_half * 0.62f, rng.range_float(0.0f, 6.2831853f), 6.2831853f, 0.0f);
}

void RmsGenerator::synth_alpine_lakes(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// alpine_lakes.js: ridged mountains with lakes in the low ground.
	water_h = 1.0f;
	hm.init(hm.res, hm.map_half, 3.0f);
	const float half = hm.map_half;
	hm.apply_ridged_noise(rng, 4, 0.020f, 0.5f, 2.1f, 12.0f);
	hm.apply_fbm(rng, 3, 0.035f, 0.5f, 2.0f, 2.5f);
	const int lakes = scale_by_map_size(3, 9, half);
	for (int i = 0; i < lakes; ++i) {
		// Lakes sit between the bases: off the spawn ring, inside the border.
		float a = rng.range_float(0.0f, 6.2831853f);
		float r = rng.next_float() < 0.5f ? rng.range_float(0.0f, half * 0.35f) : rng.range_float(half * 0.80f, half * 0.88f);
		hm.carve_crater_lake(std::cos(a) * r, std::sin(a) * r, half * rng.range_float(0.07f, 0.11f), 6.0f, 3.0f, water_h);
	}
	hm.thermal_erosion(6, 0.7f, 0.2f);
	hm.blur(1, 0.35f);
	place_ring_spawns(hm, spawns, num_players, half * 0.60f, rng.range_float(0.0f, 6.2831853f), 6.2831853f, 4.0f);
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 16.0f, 6.0f, sp.base_height);
}

void RmsGenerator::synth_lake(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// lake.js: a large central lake, players around it.
	water_h = 1.0f;
	hm.init(hm.res, hm.map_half, 3.0f);
	const float half = hm.map_half;
	hm.apply_fbm(rng, 4, 0.022f, 0.5f, 2.0f, 2.5f);
	hm.carve_crater_lake(0.0f, 0.0f, half * 0.34f, half * 0.10f, 4.0f, water_h);
	hm.blur(1, 0.4f);
	place_ring_spawns(hm, spawns, num_players, half * 0.68f, rng.range_float(0.0f, 6.2831853f), 6.2831853f, water_h + 1.5f);
}

void RmsGenerator::synth_gulf_of_bothnia(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// gulf_of_bothnia.js: a gulf enters from the north (+z) edge to the
	// map centre; players stand on a horseshoe around it.
	water_h = 1.0f;
	hm.init(hm.res, hm.map_half, 3.0f);
	const float half = hm.map_half;
	hm.apply_fbm(rng, 4, 0.024f, 0.5f, 2.0f, 3.0f);
	const float wobble = rng.range_float(-0.12f, 0.12f) * half;
	std::vector<std::pair<float, float>> gulf = {
		{wobble, half * 1.10f}, {-wobble * 0.5f, half * 0.55f}, {wobble * 0.5f, half * 0.15f}, {0.0f, -half * 0.05f}};
	hm.carve_spline_path(gulf, half * 0.24f, 4.0f, water_h);
	hm.carve_crater_lake(0.0f, -half * 0.05f, half * 0.20f, half * 0.08f, 4.0f, water_h);
	hm.blur(1, 0.4f);
	// Horseshoe from the north-east round the south to the north-west.
	place_ring_spawns(hm, spawns, num_players, half * 0.64f, 3.14159265f * 0.5f + 0.95f, 6.2831853f - 1.9f, water_h + 1.5f);
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 16.0f, 6.0f, sp.base_height);
}

void RmsGenerator::synth_ardennes_forest(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// ardennes_forest.js: hilly forest; distribute_neutral_resources adds
	// extra groves for this map.
	water_h = 0.0f;
	hm.init(hm.res, hm.map_half, 3.0f);
	const float half = hm.map_half;
	hm.apply_fbm(rng, 5, 0.026f, 0.55f, 2.0f, 6.0f);
	// A brook winding across the map, fordable in the middle.
	std::vector<std::pair<float, float>> brook = {
		{-half * 1.05f, rng.range_float(-0.3f, 0.3f) * half}, {-half * 0.3f, rng.range_float(-0.2f, 0.2f) * half},
		{half * 0.3f, rng.range_float(-0.2f, 0.2f) * half}, {half * 1.05f, rng.range_float(-0.3f, 0.3f) * half}};
	hm.carve_spline_path(brook, 3.0f, 1.5f, water_h, half * 0.9f, 10.0f);
	hm.blur(1, 0.4f);
	place_ring_spawns(hm, spawns, num_players, half * 0.60f, rng.range_float(0.0f, 6.2831853f), 6.2831853f, water_h + 1.5f);
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 16.0f, 6.0f, sp.base_height);
}

void RmsGenerator::synth_nearestern_badlands(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// neareastern_badlands.js: eroded desert with canyons and small oases.
	water_h = 0.0f;
	hm.init(hm.res, hm.map_half, 4.0f);
	const float half = hm.map_half;
	hm.apply_ridged_noise(rng, 4, 0.028f, 0.5f, 2.0f, 6.0f);
	hm.carve_canyon_network(rng, half * 0.10f, 4.0f, 3.0f);
	const int oases = scale_by_map_size(2, 6, half);
	for (int i = 0; i < oases; ++i) {
		float a = rng.range_float(0.0f, 6.2831853f);
		float r = rng.range_float(0.0f, half * 0.30f);
		hm.carve_crater_lake(std::cos(a) * r, std::sin(a) * r, half * 0.05f + 2.0f, 4.0f, 2.0f, water_h);
	}
	hm.terrace(2.0f, 0.3f);
	hm.blur(1, 0.35f);
	place_ring_spawns(hm, spawns, num_players, half * 0.60f, rng.range_float(0.0f, 6.2831853f), 6.2831853f, 4.0f);
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 16.0f, 6.0f, sp.base_height);
}

// =========================================================================
// Maps modelled on 0 A.D. maps/random/{rivers,canyon,corinthian_isthmus,
// belgian_uplands,lorraine_plain,hyrcanian_shores}.js
// =========================================================================

namespace {
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

void RmsGenerator::synth_rivers(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// rivers.js: a lake in the middle, one river from it to the map edge
	// between each pair of neighbouring players, each with a ford.
	water_h = 1.0f;
	hm.init(hm.res, hm.map_half, 3.0f);
	const float half = hm.map_half;
	hm.apply_fbm(rng, 4, 0.022f, 0.5f, 2.0f, 2.0f);
	const float start = rng.range_float(0.0f, 6.2831853f);
	const int n = std::max(2, num_players);
	hm.carve_crater_lake(0.0f, 0.0f, half * 0.14f, half * 0.05f, 3.0f, water_h);
	std::vector<std::pair<float, float>> fords;
	for (int i = 0; i < n; ++i) {
		const float a = start + 6.2831853f * ((float)i + 0.5f) / (float)n;
		const float bend = rng.range_float(-0.15f, 0.15f);
		std::vector<std::pair<float, float>> river;
		for (int k = 0; k <= 4; ++k) {
			const float r = half * (0.10f + 0.25f * (float)k);
			const float ak = a + bend * std::sin((float)k * 1.3f);
			river.push_back({std::cos(ak) * r, std::sin(ak) * r});
		}
		hm.carve_spline_path(river, 3.0f + half * 0.02f, 2.5f, water_h);
		// The ford sits between a third and a half of the way to the edge.
		const float t = rng.range_float(1.4f, 2.2f);
		const int k = (int)t;
		const float u = t - (float)k;
		fords.push_back({river[k].first + (river[k + 1].first - river[k].first) * u,
		                 river[k].second + (river[k + 1].second - river[k].second) * u});
	}
	// Twice the river's half-width, so the ford spans its blended banks too.
	for (const auto &f : fords) place_ford(hm, f.first, f.second, 2.0f * (3.0f + half * 0.02f), water_h);
	hm.blur(1, 0.4f);
	place_ring_spawns(hm, spawns, num_players, half * 0.60f, start, 6.2831853f, water_h + 1.5f);
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 16.0f, 6.0f, sp.base_height);
}

void RmsGenerator::synth_canyon(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// canyon.js: high rocky ground; each base sits in a gorge that leads
	// to a central basin. No water.
	water_h = -10.0f;
	hm.init(hm.res, hm.map_half, 14.0f);
	const float half = hm.map_half;
	hm.apply_ridged_noise(rng, 3, 0.03f, 0.5f, 2.0f, 4.0f);
	const float floor_h = 2.0f;
	const float start = rng.range_float(0.0f, 6.2831853f);
	const float ring_r = half * 0.62f;
	hm.level_area(0.0f, 0.0f, half * 0.22f, half * 0.08f, floor_h);
	for (int i = 0; i < num_players; ++i) {
		const float a = start + 6.2831853f * (float)i / (float)std::max(1, num_players);
		const float sx = std::cos(a) * ring_r, sz = std::sin(a) * ring_r;
		hm.level_area(sx, sz, 18.0f, 8.0f, floor_h);
		// A gorge from the base to the basin, with a slight bend.
		const float mid = a + rng.range_float(-0.25f, 0.25f);
		std::vector<std::pair<float, float>> gorge = {
			{sx, sz}, {std::cos(mid) * ring_r * 0.55f, std::sin(mid) * ring_r * 0.55f}, {0.0f, 0.0f}};
		hm.carve_spline_path(gorge, 4.0f + half * 0.03f, 1.0f, floor_h + 1.0f);
	}
	hm.thermal_erosion(4, 0.8f, 0.2f);
	hm.blur(1, 0.35f);
	place_ring_spawns(hm, spawns, num_players, ring_r, start, 6.2831853f, floor_h);
}

void RmsGenerator::synth_corinthian_isthmus(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// corinthian_isthmus.js: two lands (north and south) joined by a narrow
	// isthmus in the middle; the sea fills the gulfs on either side of it.
	water_h = 1.0f;
	hm.init(hm.res, hm.map_half, 3.5f);
	const float half = hm.map_half;
	hm.apply_fbm(rng, 4, 0.022f, 0.5f, 2.0f, 2.5f);
	const float isthmus = half * 0.10f;
	const float gulf = half * 0.17f;
	const float phase = rng.range_float(0.0f, 6.2831853f);
	const float step = (half * 2.0f) / (float)(hm.res - 1);
	for (int z = 0; z < hm.res; ++z) {
		const float wz = -half + z * step;
		for (int x = 0; x < hm.res; ++x) {
			const float wx = -half + x * step;
			const float w = gulf * (1.0f + 0.25f * std::sin(wx / half * 4.0f + phase));
			// 1 inside the gulfs, 0 on land, smooth at the shore and at the isthmus.
			const float t = (1.0f - smoothstep(w * 0.7f, w * 1.3f, std::fabs(wz))) *
			                smoothstep(isthmus, isthmus + half * 0.10f, std::fabs(wx));
			if (t > 0.0f) {
				const float cur = hm.get(x, z);
				hm.set(x, z, cur + (water_h - 2.5f - cur) * t);
			}
		}
	}
	hm.blur(1, 0.4f);
	place_side_spawns(hm, spawns, num_players, true, half * 0.58f, half * 0.62f, water_h + 1.5f);
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 16.0f, 6.0f, sp.base_height);
}

void RmsGenerator::synth_belgian_uplands(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// belgian_uplands.js: steep wooded hills and valleys, no water.
	water_h = -10.0f;
	hm.init(hm.res, hm.map_half, 6.0f);
	const float half = hm.map_half;
	hm.apply_fbm(rng, 5, 0.030f, 0.55f, 2.0f, 9.0f);
	hm.apply_ridged_noise(rng, 2, 0.015f, 0.5f, 2.0f, 4.0f);
	hm.thermal_erosion(4, 0.9f, 0.2f);
	hm.blur(1, 0.35f);
	place_ring_spawns(hm, spawns, num_players, half * 0.62f, rng.range_float(0.0f, 6.2831853f), 6.2831853f, 0.0f);
}

void RmsGenerator::synth_lorraine_plain(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// lorraine_plain.js: a flat plain with a river from north to south and a
	// few fords; players on the west and east banks.
	water_h = 1.0f;
	hm.init(hm.res, hm.map_half, 2.5f);
	const float half = hm.map_half;
	hm.apply_fbm(rng, 3, 0.020f, 0.5f, 2.0f, 1.2f);
	std::vector<std::pair<float, float>> river;
	for (int k = 0; k <= 6; ++k) {
		const float z = -half * 1.05f + half * 2.1f * (float)k / 6.0f;
		river.push_back({rng.range_float(-0.08f, 0.08f) * half, z});
	}
	hm.carve_spline_path(river, 3.0f + half * 0.025f, 2.0f, water_h);
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
	place_side_spawns(hm, spawns, num_players, false, half * 0.58f, half * 0.62f, water_h + 1.5f);
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 16.0f, 6.0f, sp.base_height);
}

void RmsGenerator::synth_hyrcanian_shores(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// hyrcanian_shores.js: the sea along the south (-z) edge, a coastal strip
	// with the players, and wooded highlands rising to the north.
	water_h = 1.0f;
	hm.init(hm.res, hm.map_half, 3.0f);
	const float half = hm.map_half;
	hm.apply_fbm(rng, 4, 0.024f, 0.5f, 2.0f, 2.5f);
	const float phase = rng.range_float(0.0f, 6.2831853f);
	const float step = (half * 2.0f) / (float)(hm.res - 1);
	for (int z = 0; z < hm.res; ++z) {
		const float wz = -half + z * step;
		for (int x = 0; x < hm.res; ++x) {
			const float wx = -half + x * step;
			const float coast = -half * (0.62f + 0.06f * std::sin(wx / half * 3.0f + phase));
			const float cur = hm.get(x, z);
			if (wz < coast + half * 0.10f) {
				const float t = smoothstep(coast + half * 0.10f, coast - half * 0.06f, wz);
				hm.set(x, z, cur + (water_h - 3.0f - cur) * t);
			} else {
				// Highlands: up to +8 towards the north edge.
				hm.add(x, z, 8.0f * smoothstep(half * 0.30f, half * 0.95f, wz));
			}
		}
	}
	hm.blur(1, 0.4f);
	spawns.clear();
	// An ellipse squeezed onto the coastal strip.
	const float start = rng.range_float(0.0f, 6.2831853f);
	for (int i = 0; i < num_players; ++i) {
		const float a = start + 6.2831853f * (float)i / (float)std::max(1, num_players);
		const float sx = std::cos(a) * half * 0.64f;
		const float sz = -half * 0.05f + std::sin(a) * half * 0.30f;
		const float bh = std::max(water_h + 1.5f, hm.sample_world(sx, sz));
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
			const float line = edge + wobble * (0.6f * std::sin(along / half * 3.1f + p1) + 0.4f * std::sin(along / half * 7.3f + p2));
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
		const float px = std::cos(a) * d, pz = std::sin(a) * d;
		bool ok = true;
		for (const auto &sp : spawns)
			if (std::hypot(px - sp.x, pz - sp.z) < clear + r) { ok = false; break; }
		for (const auto &p : placed)
			if (std::hypot(px - p.first, pz - p.second) < gap + 2.0f * r) { ok = false; break; }
		if (!ok) continue;
		hm.carve_crater_lake(px, pz, r, r * 0.5f, depth, water_h);
		placed.push_back({px, pz});
		++i;
	}
}

// Raises a mountain ridge of half-width `w` to height `h` along a polyline,
// leaving a passage of length `pass_len` centred `pass_at` along it.
void raise_ridge(HeightmapGrid &hm, const std::vector<std::pair<float, float>> &pts, float w, float h,
		float pass_at, float pass_len) {
	const float half = hm.map_half;
	const float step = (half * 2.0f) / (float)(hm.res - 1);
	float minx = 1e9f, maxx = -1e9f, minz = 1e9f, maxz = -1e9f;
	for (const auto &p : pts) {
		minx = std::min(minx, p.first); maxx = std::max(maxx, p.first);
		minz = std::min(minz, p.second); maxz = std::max(maxz, p.second);
	}
	const float reach = w * 2.0f;
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
				const float d = std::hypot(wx - (ax + bx * t), wz - (az + bz * t));
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
	// f = 0.45. Scarce wood (distribute_neutral_resources).
	water_h = 1.0f;
	hm.init(hm.res, hm.map_half, 3.0f);
	const float half = hm.map_half;
	hm.apply_fbm(rng, 3, 0.02f, 0.5f, 2.0f, 1.5f);
	flood_edge(hm, rng, 1, half * 0.38f, half * 0.05f, half * 0.04f, water_h - 4.0f);
	// Ice floes: islands in the sea (0 A.D.: scaleByMapSize(10, 80) islands).
	const int floes = scale_by_map_size(4, 16, half);
	for (int i = 0; i < floes; ++i) {
		const float fx = rng.range_float(-0.85f, 0.85f) * half;
		const float fz = rng.range_float(0.62f, 0.90f) * half;
		lift_area(hm, fx, fz, half * rng.range_float(0.03f, 0.06f), 2.0f, water_h + 1.0f);
	}
	place_line_spawns(hm, spawns, num_players, true, 0.0f, -half * 0.10f, half * 0.66f, -half * 0.28f, 0.0f, 1.0f, water_h + 1.5f);
	scatter_ponds(hm, rng, spawns, scale_by_map_size(1, 4, half) * std::max(1, num_players / 2), half * 0.07f + 2.0f,
	              20.0f, 20.0f, half * 0.85f, 3.0f, water_h);
	hm.blur(1, 0.4f);
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 16.0f, 6.0f, sp.base_height);
}

void RmsGenerator::synth_kerala(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// kerala.js: the sea along the west edge (shore at f = 0.31), players on
	// a north-south line at f = 0.55, mountains east of f = 0.69.
	water_h = 1.0f;
	hm.init(hm.res, hm.map_half, 3.0f);
	const float half = hm.map_half;
	hm.apply_fbm(rng, 4, 0.024f, 0.5f, 2.0f, 2.0f);
	flood_edge(hm, rng, 0, -half * 0.38f, half * 0.06f, half * 0.04f, water_h - 4.0f);
	// Western Ghats: hills east of f = 0.69 (0 A.D. heightHill 25).
	const int hills = scale_by_map_size(6, 24, half);
	for (int i = 0; i < hills; ++i) {
		const float hx = rng.range_float(0.42f, 0.98f) * half;
		const float hz = rng.range_float(-0.95f, 0.95f) * half;
		hm.raise_plateau(hx, hz, half * rng.range_float(0.05f, 0.10f), 0.0f, 0.0f, rng.range_float(6.0f, 11.0f), 6.0f);
	}
	place_line_spawns(hm, spawns, num_players, false, half * 0.10f, 0.0f, half * 0.68f, -half * 0.24f, -1.0f, 0.0f, water_h + 1.5f);
	hm.blur(1, 0.4f);
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 16.0f, 6.0f, sp.base_height);
}

void RmsGenerator::synth_polar_sea(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// polar_sea.js: icy land with a central sea (f = 0.17 across) and small
	// lakes, players on a circle at f = 0.35, glacier hills. 0 A.D. has no
	// wood here at all; RTS0 keeps a few groves so the AI can build.
	water_h = 1.0f;
	hm.init(hm.res, hm.map_half, 3.0f);
	const float half = hm.map_half;
	hm.apply_fbm(rng, 4, 0.03f, 0.5f, 2.0f, 2.0f);
	hm.carve_crater_lake(0.0f, 0.0f, half * 0.30f, half * 0.08f, 5.0f, water_h);
	place_ring_spawns(hm, spawns, num_players, half * 0.70f, rng.range_float(0.0f, 6.2831853f), 6.2831853f, water_h + 1.5f);
	scatter_ponds(hm, rng, spawns, scale_by_map_size(10, 16, half) / 2, half * 0.05f + 2.0f, 20.0f, 12.0f, half * 0.90f, 3.0f, water_h);
	hm.blur(1, 0.4f);
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 16.0f, 6.0f, sp.base_height);
}

void RmsGenerator::synth_sahel(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// sahel.js: open, nearly flat savanna, players on a circle at f = 0.35,
	// one to three water holes away from the bases; little wood, plenty of
	// mines and hunt.
	water_h = 0.5f;
	hm.init(hm.res, hm.map_half, 2.0f);
	const float half = hm.map_half;
	hm.apply_fbm(rng, 3, 0.02f, 0.5f, 2.0f, 1.0f);
	place_ring_spawns(hm, spawns, num_players, half * 0.70f, rng.range_float(0.0f, 6.2831853f), 6.2831853f, water_h + 1.5f);
	scatter_ponds(hm, rng, spawns, scale_by_map_size(1, 3, half), half * 0.07f + 2.0f, 24.0f, 20.0f, half * 0.60f, 3.0f, water_h);
	hm.blur(1, 0.4f);
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 16.0f, 6.0f, sp.base_height);
}

void RmsGenerator::synth_african_plains(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// african_plains.js: lush plains with many watering holes (4-10) and
	// hills (6-16), players on a circle at f = 0.35.
	water_h = 1.0f;
	hm.init(hm.res, hm.map_half, 3.0f);
	const float half = hm.map_half;
	hm.apply_fbm(rng, 3, 0.022f, 0.5f, 2.0f, 1.5f);
	place_ring_spawns(hm, spawns, num_players, half * 0.70f, rng.range_float(0.0f, 6.2831853f), 6.2831853f, water_h + 1.5f);
	const int hills = scale_by_map_size(6, 16, half);
	for (int i = 0, tries = 0; i < hills && tries < hills * 40; ++tries) {
		const float a = rng.range_float(0.0f, 6.2831853f);
		const float d = std::sqrt(rng.next_float()) * half * 0.9f;
		const float hx = std::cos(a) * d, hz = std::sin(a) * d;
		bool ok = true;
		for (const auto &sp : spawns)
			if (std::hypot(hx - sp.x, hz - sp.z) < 30.0f) { ok = false; break; }
		if (!ok) continue;
		hm.raise_plateau(hx, hz, half * rng.range_float(0.04f, 0.07f), 0.0f, 0.0f, rng.range_float(4.0f, 7.0f), 5.0f);
		++i;
	}
	scatter_ponds(hm, rng, spawns, scale_by_map_size(4, 10, half), half * 0.05f + 2.5f, 22.0f, 8.0f, half * 0.90f, 3.0f, water_h);
	hm.blur(1, 0.4f);
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 16.0f, 6.0f, sp.base_height);
}

void RmsGenerator::synth_alpine_valley(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// alpine_valley.js: players on a circle at f = 0.35 in deep valleys;
	// a mountain range from the map edge (f = 0.49) towards the centre
	// (f = 0.18) between each pair of neighbours, with a passage through it.
	water_h = -10.0f;
	hm.init(hm.res, hm.map_half, 3.0f);
	const float half = hm.map_half;
	hm.apply_fbm(rng, 3, 0.03f, 0.5f, 2.0f, 2.0f);
	const float start = rng.range_float(0.0f, 6.2831853f);
	const int n = std::max(2, num_players);
	const float w = (float)scale_by_map_size(9, 15, half) * 0.5f;
	const float pass = (float)scale_by_map_size(10, 15, half) + 4.0f;
	for (int i = 0; i < n; ++i) {
		const float a = start + 6.2831853f * ((float)i + 0.5f) / (float)n;
		const float jig = 3.14159265f / (float)n * 0.4f * (rng.next_float() < 0.5f ? -1.0f : 1.0f);
		std::vector<std::pair<float, float>> ridge = {
			{std::cos(a) * half * 0.98f, std::sin(a) * half * 0.98f},
			{std::cos(a + jig) * half * 0.68f, std::sin(a + jig) * half * 0.68f},
			{std::cos(a) * half * 0.36f, std::sin(a) * half * 0.36f}};
		const float len = half * 0.62f;
		raise_ridge(hm, ridge, w, 16.0f, len * rng.range_float(0.35f, 0.65f), pass);
	}
	hm.thermal_erosion(3, 0.9f, 0.2f);
	hm.blur(1, 0.35f);
	place_ring_spawns(hm, spawns, num_players, half * 0.70f, start, 6.2831853f, 0.0f);
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 16.0f, 6.0f, sp.base_height);
}

// =========================================================================
// Maps modelled on 0 A.D. maps/random/{syria,cantabrian_highlands,
// pyrenean_sierra,sahel_watering_holes,persian_highlands,volcanic_lands}.js
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
		const float hx = std::cos(a) * d, hz = std::sin(a) * d;
		const float r = rng.range_float(r0, r1);
		bool ok = true;
		for (const auto &sp : spawns)
			if (std::hypot(hx - sp.x, hz - sp.z) < clear + r) { ok = false; break; }
		if (!ok) continue;
		hm.raise_plateau(hx, hz, r, 0.0f, 0.0f, rng.range_float(h0, h1), r * 0.6f);
		++i;
	}
}
} // namespace

void RmsGenerator::synth_syria(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// syria.js: dry plains with slightly rolling highlands, players on a
	// circle at f = 0.35, scaleByMapSize(1, 3) * 3 hills per player.
	water_h = -10.0f;
	hm.init(hm.res, hm.map_half, 2.0f);
	const float half = hm.map_half;
	hm.apply_fbm(rng, 4, 0.03f, 0.5f, 2.0f, 1.5f);
	place_ring_spawns(hm, spawns, num_players, half * 0.70f, rng.range_float(0.0f, 6.2831853f), 6.2831853f, 0.0f);
	scatter_hills(hm, rng, spawns, scale_by_map_size(1, 3, half) * 3 * std::max(1, num_players),
	              half * 0.03f + 2.0f, half * 0.06f + 3.0f, 3.0f, 8.0f, 20.0f);
	hm.blur(1, 0.35f);
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 16.0f, 6.0f, sp.base_height);
}

void RmsGenerator::synth_cantabrian_highlands(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// cantabrian_highlands.js: each player starts on a hill (heightHill 20)
	// ringed by cliffs, with one ramp facing the map centre; players on a
	// circle at f = 0.35, scaleByMapSize(1, 4) lakes per player.
	water_h = 1.0f;
	hm.init(hm.res, hm.map_half, 3.0f);
	const float half = hm.map_half;
	hm.apply_fbm(rng, 4, 0.025f, 0.5f, 2.0f, 2.0f);
	const float start = rng.range_float(0.0f, 6.2831853f);
	const float ring_r = half * 0.70f;
	// Bases are levelled before the hills go up: levelling a base on its hill (14 units and a
	// 6-unit blend) would fill the upper half of its ramp back in, leaving a cliff.
	place_ring_spawns(hm, spawns, num_players, ring_r, start, 6.2831853f, water_h + 1.5f);
	for (int i = 0; i < num_players; ++i) {
		const float a = start + 6.2831853f * (float)i / (float)std::max(1, num_players);
		// The ramp points back towards the centre, give or take pi / 8.
		const float ramp = a + 3.14159265f * (1.0f + rng.range_float(-1.0f, 1.0f) / 8.0f);
		hm.raise_plateau(std::cos(a) * ring_r, std::sin(a) * ring_r, 19.0f, ramp, 0.3f, 7.0f, 6.0f);
	}
	scatter_ponds(hm, rng, spawns, scale_by_map_size(1, 4, half) * std::max(1, num_players) / 2,
	              half * 0.05f + 3.0f, 30.0f, 12.0f, half * 0.95f, 3.0f, water_h);
	hm.blur(1, 0.3f);
	// The hilltop inside the ramp's start (half the hill's radius).
	for (auto &sp : spawns) {
		sp.base_height = hm.sample_world(sp.x, sp.z);
		lift_area(hm, sp.x, sp.z, 8.0f, 1.5f, sp.base_height);
	}
}

void RmsGenerator::synth_pyrenean_sierra(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// pyrenean_sierra.js: a mountain range f = 0.68 long through the centre
	// splits the two teams, with passes at 0.35 and 0.65 of its length.
	water_h = -10.0f;
	hm.init(hm.res, hm.map_half, 3.0f);
	const float half = hm.map_half;
	hm.apply_fbm(rng, 4, 0.025f, 0.5f, 2.0f, 2.5f);
	const float len = half * 0.68f;
	const float w = (float)scale_by_map_size(15, 55, half) * 0.25f;
	const float wob = rng.range_float(-0.06f, 0.06f) * half;
	const float pass = (float)scale_by_map_size(8, 50, half) * 0.5f + 6.0f;
	raise_ridge(hm, {{-len, wob}, {-len * 0.5f, -wob}, {0.0f, wob * 0.5f}}, w, 18.0f, len * 0.70f, pass);
	raise_ridge(hm, {{0.0f, wob * 0.5f}, {len * 0.5f, -wob}, {len, wob}}, w, 18.0f, len * 0.30f, pass);
	hm.thermal_erosion(3, 0.9f, 0.2f);
	hm.blur(1, 0.35f);
	place_side_spawns(hm, spawns, num_players, true, half * 0.60f, half * 0.62f, 0.0f);
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 16.0f, 6.0f, sp.base_height);
}

void RmsGenerator::synth_sahel_watering_holes(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// sahel_watering_holes.js: between each pair of neighbours a line of
	// water runs from f = 0.15 to f = 0.49, with a pool at each end and
	// shallows across the middle; players on a circle at f = 0.35.
	water_h = 1.0f;
	hm.init(hm.res, hm.map_half, 3.0f);
	const float half = hm.map_half;
	hm.apply_fbm(rng, 3, 0.022f, 0.5f, 2.0f, 1.5f);
	const float start = rng.range_float(0.0f, 6.2831853f);
	const int n = std::max(2, num_players);
	std::vector<std::pair<float, float>> fords;
	for (int i = 0; i < n; ++i) {
		const float a = start + 6.2831853f * ((float)i + 0.5f) / (float)n;
		const float c = std::cos(a), s = std::sin(a);
		hm.carve_spline_path({{c * half * 0.30f, s * half * 0.30f}, {c * half * 0.98f, s * half * 0.98f}},
		                     2.5f + half * 0.02f, 2.5f, water_h);
		hm.carve_crater_lake(c * half * 0.30f, s * half * 0.30f, (float)scale_by_map_size(5, 30, half) * 0.5f + 2.0f, 3.0f, 3.0f, water_h);
		hm.carve_crater_lake(c * half * 0.98f, s * half * 0.98f, (float)scale_by_map_size(5, 22, half) * 0.5f + 2.0f, 3.0f, 3.0f, water_h);
		fords.push_back({c * half * 0.64f, s * half * 0.64f});
	}
	for (const auto &f : fords) place_ford(hm, f.first, f.second, 2.0f * (2.5f + half * 0.02f), water_h);
	hm.blur(1, 0.4f);
	place_ring_spawns(hm, spawns, num_players, half * 0.70f, start, 6.2831853f, water_h + 1.5f);
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 16.0f, 6.0f, sp.base_height);
}

void RmsGenerator::synth_persian_highlands(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// persian_highlands.js: high dry land (heightLand 10) with a sunken
	// central plateau (-10) rich in minerals, ringed by rocky hills;
	// players on a circle at f = 0.35. No water.
	water_h = -10.0f;
	hm.init(hm.res, hm.map_half, 8.0f);
	const float half = hm.map_half;
	hm.apply_fbm(rng, 4, 0.03f, 0.5f, 2.0f, 1.5f);
	hm.level_area(0.0f, 0.0f, half * 0.26f, half * 0.12f, 3.0f);
	place_ring_spawns(hm, spawns, num_players, half * 0.70f, rng.range_float(0.0f, 6.2831853f), 6.2831853f, 0.0f);
	scatter_hills(hm, rng, spawns, scale_by_map_size(20, 80, half) / 2, 2.5f, 4.5f, 3.0f, 6.0f, 18.0f);
	hm.blur(1, 0.35f);
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 16.0f, 6.0f, sp.base_height);
}

void RmsGenerator::synth_volcanic_lands(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// volcanic_lands.js: charred land round a smoking volcano in the centre
	// (rmgen createVolcano: a cone with a crater), scaleByMapSize(2, 8)
	// hills per player, players on a circle at f = 0.35, dead trees.
	water_h = -10.0f;
	hm.init(hm.res, hm.map_half, 2.0f);
	const float half = hm.map_half;
	hm.apply_fbm(rng, 4, 0.03f, 0.5f, 2.0f, 1.5f);
	const float cone_r = half * 0.30f, crater_r = half * 0.07f, peak = 14.0f;
	const float step = (half * 2.0f) / (float)(hm.res - 1);
	for (int z = 0; z < hm.res; ++z) {
		const float wz = -half + z * step;
		for (int x = 0; x < hm.res; ++x) {
			const float wx = -half + x * step;
			const float d = std::sqrt(wx * wx + wz * wz);
			if (d >= cone_r) continue;
			// A concave cone up to the crater rim, then a bowl inside it.
			const float t = 1.0f - d / cone_r;
			const float h = d > crater_r ? peak * t * t : peak * 0.55f + (peak - peak * 0.55f) * (d / crater_r) * 0.6f;
			hm.add(x, z, h);
		}
	}
	place_ring_spawns(hm, spawns, num_players, half * 0.70f, rng.range_float(0.0f, 6.2831853f), 6.2831853f, 0.0f);
	scatter_hills(hm, rng, spawns, scale_by_map_size(2, 8, half) * std::max(1, num_players) / 2,
	              half * 0.03f + 2.0f, half * 0.05f + 3.0f, 3.0f, 7.0f, 22.0f);
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
			const float d = std::hypot(wx - (ax + dx * t), wz - (az + dz * t));
			if (d >= reach) continue;
			const float k = d <= w ? 1.0f : 1.0f - smoothstep(w, reach, d);
			const float cur = hm.get(x, z);
			hm.set(x, z, cur + (h - cur) * k);
		}
	}
}

// Point at angle a, distance r from the centre.
inline std::pair<float, float> polar(float a, float r) { return {std::cos(a) * r, std::sin(a) * r}; }
} // namespace

void RmsGenerator::synth_lions_den(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// lions_den.js createSunkenTerrain: high ground (heightHill 50) with a
	// sunken valley ring (f = 0.29, heightValley 0) round a central hill
	// (f = 0.21). Each player's den (heightDen 15) sits in the high ground at
	// f = 0.4, with ramps (heightPath 10) down to an expansion cut into the
	// central hill and round to both neighbours; more expansions lie in the
	// valley between players. 0 A.D. metres x 0.35.
	water_h = -10.0f;
	const float hill = 17.5f, den = 5.25f, path = 3.5f, valley = 0.0f;
	hm.init(hm.res, hm.map_half, hill);
	const float half = hm.map_half;
	const int n = std::max(1, num_players);
	hm.apply_fbm(rng, 3, 0.03f, 0.5f, 2.0f, 1.0f);
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
	// teams here, so each player is a team. 0 A.D. metres x 0.35.
	water_h = -10.0f;
	hm.init(hm.res, hm.map_half, 1.0f);
	const float half = hm.map_half;
	const int n = std::max(1, num_players);
	hm.apply_fbm(rng, 4, 0.025f, 0.5f, 2.0f, 1.5f);
	const float start = rng.range_float(0.0f, 6.2831853f);
	const float spine_w = (float)scale_by_map_size(14, n >= 5 ? 4 : 8, half) * 0.5f;
	for (int i = 0; i < n; ++i) {
		const float a = start + ((float)i + 0.5f) * 6.2831853f / (float)n;
		const auto s = polar(a, half * 0.15f), e = polar(a, half * 0.84f);
		// PathPlacer waviness: bend the spine through a jittered midpoint.
		const auto m = polar(a + rng.range_float(-0.06f, 0.06f), half * 0.5f);
		raise_ridge(hm, {s, m, e}, spine_w, 10.5f, -1.0f, 0.0f);
	}
	place_ring_spawns(hm, spawns, num_players, half * 0.40f, start, 6.2831853f, 0.0f);
	scatter_hills(hm, rng, spawns, scale_by_map_size(1, 4, half), 3.0f, 5.0f, 2.0f, 4.0f, 30.0f);
	hm.blur(1, 0.35f);
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 16.0f, 6.0f, sp.base_height);
}

void RmsGenerator::synth_deep_forest(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// deep_forest.js: a forest filling the map, players at 5/8 to 3/4 of the
	// map radius (angles jittered by a quarter of the spacing), forest paths
	// (heightPath -2) linking every base to each other and to a central
	// hill, and a height field of a middle hill (size/64 * (1 + cos(3/2 pi
	// r/R))) plus rolling hills (5 * (1 + sin(x/10) sin(z/10))). Dense wood
	// comes from distribute_neutral_resources. 0 A.D. metres x 0.35.
	water_h = -10.0f;
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
				set_along_segment(hm, pos[i].first, pos[i].second, pos[j].first, pos[j].second, 3.0f, 2.0f, -0.7f);
	const float step = (half * 2.0f) / (float)(hm.res - 1);
	for (int z = 0; z < hm.res; ++z) {
		const float wz = -half + z * step;
		for (int x = 0; x < hm.res; ++x) {
			const float wx = -half + x * step;
			const float r = std::hypot(wx, wz);
			const float middle = (half / 32.0f) * (1.0f + std::cos(1.5f * 3.14159265f * std::min(r, half) / half));
			const float hills = 5.0f * (1.0f + std::sin(wx / 10.0f) * std::sin(wz / 10.0f));
			hm.add(x, z, (middle + hills + 1.0f) * 0.35f);
		}
	}
	spawns.clear();
	for (int i = 0; i < n && i < num_players; ++i) {
		const float sx = pos[i].first, sz = pos[i].second, r = std::max(1e-3f, std::hypot(sx, sz));
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
	// 800) small bumps, players on a circle at f = 0.35.
	water_h = 1.0f;
	hm.init(hm.res, hm.map_half, 2.5f);
	const float half = hm.map_half;
	hm.apply_fbm(rng, 4, 0.05f, 0.5f, 2.0f, 0.8f);
	const float lake_r = (float)scale_by_map_size(15, 40, half);
	const int blobs = std::max(2, scale_by_map_size(2, 16, half) / 2);
	for (int i = 0; i < blobs; ++i) {
		const auto c = polar(rng.range_float(0.0f, 6.2831853f), lake_r * std::sqrt(rng.next_float()) * 0.7f);
		hm.carve_crater_lake(c.first, c.second, lake_r * rng.range_float(0.35f, 0.6f) + 2.0f, 4.0f, 3.0f, water_h);
	}
	place_ring_spawns(hm, spawns, num_players, half * 0.70f, rng.range_float(0.0f, 6.2831853f), 6.2831853f, water_h + 1.5f);
	hm.blur(1, 0.4f);
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 16.0f, 6.0f, sp.base_height);
}

void RmsGenerator::synth_guadalquivir_river(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// guadalquivir_river.js: a continent (radius f = 0.49 round a point at
	// f = 0.2 from the centre) in the sea, a river (f = 0.07 wide) through
	// the map centre from the sea, with shallows 0.3-0.4, 0.5-0.6 and
	// 0.7-0.8 along it. Players on arcs of radius f = 0.35 round the
	// continent's centre, split between the two banks.
	water_h = 1.0f;
	hm.init(hm.res, hm.map_half, 3.0f);
	const float half = hm.map_half;
	hm.apply_fbm(rng, 3, 0.025f, 0.5f, 2.0f, 1.0f);
	const float rot = rng.range_float(0.0f, 6.2831853f);
	const float ux = std::cos(rot), uz = std::sin(rot); // towards the continent's centre and along the river
	const float ccx = ux * half * 0.40f, ccz = uz * half * 0.40f;
	const float land_r = half * 0.98f;
	const float step = (half * 2.0f) / (float)(hm.res - 1);
	const float p1 = rng.range_float(0.0f, 6.2831853f), p2 = rng.range_float(0.0f, 6.2831853f);
	for (int z = 0; z < hm.res; ++z) {
		const float wz = -half + z * step;
		for (int x = 0; x < hm.res; ++x) {
			const float wx = -half + x * step;
			const float a = std::atan2(wz - ccz, wx - ccx);
			const float r = land_r * (1.0f + 0.05f * std::sin(a * 5.0f + p1) + 0.03f * std::sin(a * 11.0f + p2));
			const float d = std::hypot(wx - ccx, wz - ccz) - r;
			if (d <= -6.0f) continue;
			const float t = smoothstep(-6.0f, 6.0f, d);
			const float cur = hm.get(x, z);
			hm.set(x, z, cur + ((water_h - 4.0f) - cur) * t);
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
	hm.carve_spline_path(river, river_w, 3.0f, water_h);
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
		const float sx = ccx + std::cos(a) * rp, sz = ccz + std::sin(a) * rp;
		const float bh = std::max(water_h + 1.5f, hm.sample_world(sx, sz));
		const float r = std::max(1e-3f, std::hypot(sx, sz));
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
	// river and 30 from each other; sand dunes and acacia groves.
	water_h = 1.0f;
	hm.init(hm.res, hm.map_half, 3.0f);
	const float half = hm.map_half;
	hm.apply_fbm(rng, 4, 0.03f, 0.5f, 2.0f, 1.0f);
	const float ra = 3.14159265f / 5.0f;
	const float dx = -std::sin(ra), dz = std::cos(ra); // along the river
	const float ox = std::cos(ra) * -0.5f * half, oz = std::sin(ra) * -0.5f * half; // a point on it
	const float river_w = (float)scale_by_map_size(12, 36, half) * 0.5f;
	// Dunes (heightDunes 11), raised before the river so it cuts through them.
	scatter_hills(hm, rng, {}, scale_by_map_size(2, 8, half), half * 0.04f + 2.0f, half * 0.07f + 3.0f, 2.0f, 4.0f, 0.0f);
	hm.carve_spline_path({{ox - dx * 1.5f * half, oz - dz * 1.5f * half}, {ox, oz}, {ox + dx * 1.5f * half, oz + dz * 1.5f * half}},
	                     river_w, 3.5f, water_h);
	hm.blur(1, 0.4f);
	for (float f : {rng.range_float(-0.7f, -0.5f), rng.range_float(0.5f, 0.7f)})
		place_ford(hm, ox + dx * f * half, oz + dz * f * half, std::max((float)scale_by_map_size(10, 15, half), 1.7f * river_w), water_h);
	// playerPlacementRandom: rejection-sample positions clear of the river.
	spawns.clear();
	auto river_dist = [&](float x, float z) { return std::fabs((x - ox) * dz - (z - oz) * dx); };
	float min_gap = half * 0.9f;
	int tries = 0;
	for (int i = 0; i < num_players && tries < 4000; ++tries) {
		if (tries > 0 && tries % 200 == 0) min_gap *= 0.85f;
		const auto p = polar(rng.range_float(0.0f, 6.2831853f), std::sqrt(rng.next_float()) * half * 0.80f);
		if (river_dist(p.first, p.second) < river_w + 18.0f + 14.0f) continue;
		bool ok = true;
		for (const auto &sp : spawns)
			if (std::hypot(p.first - sp.x, p.second - sp.z) < std::max(30.0f, min_gap)) { ok = false; break; }
		if (!ok) continue;
		const float bh = std::max(water_h + 1.5f, hm.sample_world(p.first, p.second));
		const float r = std::max(1e-3f, std::hypot(p.first, p.second));
		spawns.push_back({(uint8_t)i, p.first, p.second, bh, -p.first / r, -p.second / r});
		hm.level_area(p.first, p.second, 14.0f, 6.0f, bh);
		++i;
	}
	// Too crowded for random spots: fall back to a circle on the dry side.
	if ((int)spawns.size() < num_players)
		place_ring_spawns(hm, spawns, num_players, half * 0.70f, ra, 6.2831853f, water_h + 1.5f);
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
	// every path. 0 A.D. metres x 0.35 above RTS0's water line.
	water_h = 1.0f;
	const float sea = water_h - 3.0f, land = water_h + 1.5f, ring = water_h + 1.8f, hill = water_h + 7.0f;
	hm.init(hm.res, hm.map_half, land);
	const float half = hm.map_half;
	const int n = std::max(1, num_players);
	hm.apply_fbm(rng, 3, 0.03f, 0.5f, 2.0f, 0.6f);
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
	hm.level_area(0.0f, 0.0f, half * 0.34f, 3.0f, water_h - 1.0f);
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

void RmsGenerator::synth_atlas_mountains(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// atlas_mountains.js: rugged land (heightLand 3) with bumps and
	// scaleByMapSize(20, 120) mountains (rmgen createMountains, up to 30 m)
	// kept 20 tiles from the bases, players on a circle at f = 0.35, scarce
	// wood. 0 A.D. metres x 0.35.
	water_h = -10.0f;
	hm.init(hm.res, hm.map_half, 1.0f);
	const float half = hm.map_half;
	hm.apply_fbm(rng, 4, 0.05f, 0.5f, 2.0f, 1.2f);
	place_ring_spawns(hm, spawns, num_players, half * 0.70f, rng.range_float(0.0f, 6.2831853f), 6.2831853f, 0.0f);
	scatter_hills(hm, rng, spawns, scale_by_map_size(20, 120, half), 3.0f, 6.5f, 6.0f, 10.5f, 20.0f);
	hm.blur(1, 0.3f);
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 16.0f, 6.0f, sp.base_height);
}

void RmsGenerator::synth_frontier(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// frontier.js: a random base elevation (1-4, now and then up to 29),
	// players by a random pattern (line, radial, randomGroup, stronghold)
	// at f = 0.2-0.35 from the centre, then a random mix of bluffs, hills,
	// mountains, plateaus, lakes and valleys. RTS0 knows no teams here, so
	// line, radial and stronghold all put one player per spoke (a circle).
	// 0 A.D. metres x 0.35 above RTS0's water line.
	water_h = 1.0f;
	int elev = rng.range_int(0, 29);
	if (elev < 25) elev = rng.range_int(1, 4);
	const float base = water_h + 1.0f + (float)elev * 0.35f;
	hm.init(hm.res, hm.map_half, base);
	const float half = hm.map_half;
	hm.apply_fbm(rng, 4, 0.03f, 0.5f, 2.0f, 1.5f);
	const float dist = rng.range_float(0.4f, 0.7f) * half;
	const float start = rng.range_float(0.0f, 6.2831853f);
	if (rng.range_int(0, 3) == 2) {
		// randomGroup: random spots at least 40 apart.
		spawns.clear();
		for (int i = 0, tries = 0; i < num_players && tries < 2000; ++tries) {
			const auto p = polar(rng.range_float(0.0f, 6.2831853f), std::sqrt(rng.next_float()) * half * 0.75f);
			bool ok = true;
			for (const auto &sp : spawns)
				if (std::hypot(p.first - sp.x, p.second - sp.z) < std::max(40.0f, half * 0.6f)) { ok = false; break; }
			if (!ok) continue;
			const float r = std::max(1e-3f, std::hypot(p.first, p.second));
			const float bh = std::max(water_h + 1.5f, hm.sample_world(p.first, p.second));
			spawns.push_back({(uint8_t)i, p.first, p.second, bh, -p.first / r, -p.second / r});
			hm.level_area(p.first, p.second, 14.0f, 6.0f, bh);
			++i;
		}
		if ((int)spawns.size() < num_players)
			place_ring_spawns(hm, spawns, num_players, dist, start, 6.2831853f, water_h + 1.5f);
	} else {
		place_ring_spawns(hm, spawns, num_players, dist, start, 6.2831853f, water_h + 1.5f);
	}
	// The feature mix: each kind present or not, as addElements' random amounts.
	if (rng.range_int(0, 1)) scatter_hills(hm, rng, spawns, scale_by_map_size(2, 8, half), 3.0f, 6.0f, 2.0f, 4.0f, 25.0f);
	if (rng.range_int(0, 1)) scatter_hills(hm, rng, spawns, scale_by_map_size(1, 6, half), 3.0f, 5.0f, 7.0f, 10.5f, 25.0f);
	if (rng.range_int(0, 1)) {
		const int plateaus = scale_by_map_size(1, 4, half);
		for (int i = 0; i < plateaus; ++i) {
			const auto p = polar(rng.range_float(0.0f, 6.2831853f), rng.range_float(0.0f, 0.9f) * half);
			bool ok = true;
			for (const auto &sp : spawns)
				if (std::hypot(p.first - sp.x, p.second - sp.z) < 35.0f) { ok = false; break; }
			if (ok) hm.raise_plateau(p.first, p.second, half * rng.range_float(0.05f, 0.09f), rng.range_float(0.0f, 6.2831853f), 0.4f, 6.0f, 3.0f);
		}
	}
	if (rng.range_int(0, 1))
		scatter_ponds(hm, rng, spawns, scale_by_map_size(1, 5, half), half * 0.05f + 3.0f, 25.0f, 15.0f, half * 0.9f,
		              base - water_h + 2.0f, water_h);
	hm.blur(1, 0.35f);
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 16.0f, 6.0f, sp.base_height);
}

void RmsGenerator::synth_wild_lake(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// wild_lake.js: diamond-square terrain seeded with a low patch in the
	// middle (the lake) and a water level set so about a fifth of the map
	// is under water; bases are smoothed before painting by height.
	// RTS0 puts the players on a circle at f = 0.33 rather than 0 A.D.'s
	// search for start spots in a height band.
	const float half = hm.map_half;
	hm.init(hm.res, hm.map_half, 0.0f);
	hm.diamond_square(rng, 6.0f, 1.0f);
	hm.level_area(0.0f, 0.0f, half * 0.18f, half * 0.18f, -6.0f);
	hm.blur(2, 0.5f);
	std::vector<float> sorted = hm.data;
	std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 5, sorted.end());
	// Shift the land so that level sits at RTS0's usual water line (1.0).
	const float shift = 1.0f - sorted[sorted.size() / 5];
	for (float &h : hm.data) h += shift;
	water_h = 1.0f;
	place_ring_spawns(hm, spawns, num_players, half * 0.66f, rng.range_float(0.0f, 6.2831853f), 6.2831853f, water_h + 1.5f);
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 20.0f, 6.0f, sp.base_height);
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
	// team each stand alone. 0 A.D. metres x 0.35.
	water_h = -10.0f;
	hm.init(hm.res, hm.map_half, 6.0f);
	const float half = hm.map_half;
	hm.apply_fbm(rng, 4, 0.03f, 0.5f, 2.0f, 1.5f);
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
		const float cx = std::cos(ta) * dist, cz = std::sin(ta) * dist;
		const int k = (int)groups[g].size();
		// Keep teammates' bases from overlapping (18 apart at least).
		const float r = k == 1 ? 0.0f : std::max(grouped, 9.0f / std::sin(3.14159265f / (float)k));
		for (int m = 0; m < k; ++m) {
			const float ma = ta + (float)m * 6.2831853f / (float)k;
			float sx = cx + std::cos(ma) * r, sz = cz + std::sin(ma) * r;
			sx = std::clamp(sx, -half + 16.0f, half - 16.0f);
			sz = std::clamp(sz, -half + 16.0f, half - 16.0f);
			const float l = std::max(1e-3f, std::hypot(sx, sz));
			const int pid = groups[g][m];
			spawns[pid] = {(uint8_t)pid, sx, sz, hm.sample_world(sx, sz), -sx / l, -sz / l};
		}
	}
	// The feature mix (addElements with random amounts).
	if (rng.range_int(0, 1)) scatter_hills(hm, rng, spawns, scale_by_map_size(2, 8, half), 4.0f, 7.0f, 2.0f, 4.0f, 30.0f);
	if (rng.range_int(0, 1)) scatter_hills(hm, rng, spawns, scale_by_map_size(1, 6, half), 3.0f, 5.0f, 7.0f, 10.5f, 25.0f);
	if (rng.range_int(0, 1)) {
		const int plateaus = scale_by_map_size(1, 4, half);
		for (int i = 0; i < plateaus; ++i) {
			const auto p = polar(rng.range_float(0.0f, 6.2831853f), rng.range_float(0.0f, 0.9f) * half);
			bool ok = true;
			for (const auto &sp : spawns)
				if (std::hypot(p.first - sp.x, p.second - sp.z) < 35.0f) { ok = false; break; }
			if (ok) hm.raise_plateau(p.first, p.second, half * rng.range_float(0.05f, 0.09f), rng.range_float(0.0f, 6.2831853f), 0.4f, 6.0f, 3.0f);
		}
	}
	if (rng.range_int(0, 1)) {
		// Valleys: dry hollows down towards 0 A.D.'s valley floor.
		const int valleys = scale_by_map_size(1, 5, half);
		for (int i = 0; i < valleys; ++i) {
			const auto p = polar(rng.range_float(0.0f, 6.2831853f), rng.range_float(0.0f, 0.9f) * half);
			bool ok = true;
			for (const auto &sp : spawns)
				if (std::hypot(p.first - sp.x, p.second - sp.z) < 30.0f) { ok = false; break; }
			if (ok) hm.level_area(p.first, p.second, half * rng.range_float(0.04f, 0.08f), 5.0f, 2.0f);
		}
	}
	for (auto &sp : spawns) {
		sp.base_height = hm.sample_world(sp.x, sp.z);
		hm.level_area(sp.x, sp.z, 14.0f, 6.0f, sp.base_height);
	}
	hm.blur(1, 0.35f);
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 16.0f, 6.0f, sp.base_height);
}

void RmsGenerator::synth_caledonian_meadows(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// caledonian_meadows.js: diamond-square terrain from a flat 2x2 seed
	// (roughness 0.8), water level set so about a fifth is under water, and
	// players at start locations whose height lies between 4/8 and 6/8 of
	// the way from the water up to the top (getStartLocationsByHeightmap,
	// 1000 tries, 30 apart), grouped in a cycle (groupPlayersCycle).
	const float half = hm.map_half;
	hm.init(hm.res, hm.map_half, 0.0f);
	hm.diamond_square(rng, 7.0f, 0.8f);
	hm.blur(2, 0.5f);
	std::vector<float> sorted = hm.data;
	std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 5, sorted.end());
	const float shift = 1.0f - sorted[sorted.size() / 5];
	for (float &h : hm.data) h += shift;
	water_h = 1.0f;
	const float top = *std::max_element(hm.data.begin(), hm.data.end());
	const float lo = water_h + (top - water_h) * 4.0f / 8.0f, hi = water_h + (top - water_h) * 6.0f / 8.0f;
	std::vector<std::pair<float, float>> cand;
	for (int i = 0; i < 1000; ++i) {
		const float x = rng.range_float(-0.85f, 0.85f) * half, z = rng.range_float(-0.85f, 0.85f) * half;
		const float h = hm.sample_world(x, z);
		if (h >= lo && h <= hi) cand.push_back({x, z});
	}
	// Spread the picks: each next start is the candidate farthest from those
	// already taken, and at least 30 from all of them.
	std::vector<std::pair<float, float>> picks;
	if (!cand.empty()) picks.push_back(cand[0]);
	while ((int)picks.size() < num_players) {
		float best = -1.0f; int bi = -1;
		for (int i = 0; i < (int)cand.size(); ++i) {
			float d = 1e9f;
			for (const auto &p : picks) d = std::min(d, std::hypot(cand[i].first - p.first, cand[i].second - p.second));
			if (d > best) { best = d; bi = i; }
		}
		if (bi < 0 || best < std::max(30.0f, half * 0.35f)) break;
		picks.push_back(cand[bi]);
	}
	if ((int)picks.size() < num_players) {
		place_ring_spawns(hm, spawns, num_players, half * 0.66f, rng.range_float(0.0f, 6.2831853f), 6.2831853f, water_h + 1.5f);
	} else {
		// groupPlayersCycle: order the starts round the map centre.
		std::sort(picks.begin(), picks.end(), [](const auto &a, const auto &b) {
			return std::atan2(a.second, a.first) < std::atan2(b.second, b.first);
		});
		spawns.clear();
		for (int i = 0; i < num_players; ++i) {
			const float sx = picks[i].first, sz = picks[i].second, l = std::max(1e-3f, std::hypot(sx, sz));
			const float bh = std::max(water_h + 1.5f, hm.sample_world(sx, sz));
			spawns.push_back({(uint8_t)i, sx, sz, bh, -sx / l, -sz / l});
			hm.level_area(sx, sz, 14.0f, 6.0f, bh);
		}
	}
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 20.0f, 6.0f, sp.base_height);
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
	place_ring_spawns(hm, spawns, num_players, half * 0.70f, rng.range_float(0.0f, 6.2831853f), 6.2831853f, line + 0.25f);
	std::vector<std::pair<float, float>> marshes;
	const float gap = (float)scale_by_map_size(7, 16, half);
	const int per_round = scale_by_map_size(4, 20, half);
	for (int round = 0; round < 7; ++round) {
		for (int i = 0, tries = 0; i < per_round && tries < per_round * 20; ++tries) {
			const auto c = polar(rng.range_float(0.0f, 6.2831853f), std::sqrt(rng.next_float()) * half * 0.95f);
			const float r = (float)scale_by_map_size(4, 9, half) * rng.range_float(0.8f, 1.35f);
			bool ok = true;
			for (const auto &sp : spawns)
				if (std::hypot(c.first - sp.x, c.second - sp.z) < 20.0f + 14.0f + r) { ok = false; break; }
			for (const auto &m : marshes)
				if (std::hypot(c.first - m.first, c.second - m.second) < gap + 2.0f * r) { ok = false; break; }
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

// 0 A.D. metres above the water line -> RTS0 height (grid_nav.h HEIGHT_PER_METRE above water_h).
constexpr float kMetre = HEIGHT_PER_METRE;

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
// `mask` is set (0 on such vertices), by a two-pass 3-4 chamfer transform.
std::vector<float> distance_to_mask(const std::vector<uint8_t> &mask, int res, float spacing) {
	const float inf = 1e9f, a = spacing, b = spacing * 1.41421356f;
	std::vector<float> d(mask.size());
	for (size_t i = 0; i < mask.size(); ++i) d[i] = mask[i] ? 0.0f : inf;
	auto at = [&](int x, int z) -> float & { return d[(size_t)z * (size_t)res + (size_t)x]; };
	for (int z = 0; z < res; ++z)
		for (int x = 0; x < res; ++x) {
			float &c = at(x, z);
			if (x > 0) c = std::min(c, at(x - 1, z) + a);
			if (z > 0) {
				c = std::min(c, at(x, z - 1) + a);
				if (x > 0) c = std::min(c, at(x - 1, z - 1) + b);
				if (x + 1 < res) c = std::min(c, at(x + 1, z - 1) + b);
			}
		}
	for (int z = res - 1; z >= 0; --z)
		for (int x = res - 1; x >= 0; --x) {
			float &c = at(x, z);
			if (x + 1 < res) c = std::min(c, at(x + 1, z) + a);
			if (z + 1 < res) {
				c = std::min(c, at(x, z + 1) + a);
				if (x + 1 < res) c = std::min(c, at(x + 1, z + 1) + b);
				if (x > 0) c = std::min(c, at(x - 1, z + 1) + b);
			}
		}
	return d;
}

float grid_spacing(const HeightmapGrid &hm) { return 2.0f * hm.map_half / (float)(hm.res - 1); }

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
				team_dist += std::hypot(a.first - b.first, a.second - b.second);
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
		const float sx = locs[i].first, sz = locs[i].second, l = std::max(1e-3f, std::hypot(sx, sz));
		const float bh = std::max(water_h + 1.0f, hm.sample_world(sx, sz));
		spawns.push_back({(uint8_t)i, sx, sz, bh, -sx / l, -sz / l});
		hm.level_area(sx, sz, radius, blend, bh);
	}
}

} // namespace

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
	std::vector<uint8_t> outside(mask.size());
	for (size_t i = 0; i < mask.size(); ++i) outside[i] = !mask[i];
	const auto d = distance_to_mask(outside, hm.res, grid_spacing(hm));
	for (size_t i = 0; i < mask.size(); ++i) {
		if (!mask[i]) continue;
		const float t = std::clamp(d[i] / std::max(blend, 1e-3f), 0.0f, 1.0f);
		hm.data[i] += (h - hm.data[i]) * t;
	}
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
			if (std::hypot(hx - sp.x, hz - sp.z) < clear + r) { ok = false; break; }
		for (int k = 0; ok && k < 12; ++k) {
			const auto p = polar((float)k * 0.5235988f, r + water_gap);
			ok = hm.sample_world(hx + p.first, hz + p.second) > water_h + 0.5f;
		}
		if (!ok || hm.sample_world(hx, hz) <= water_h + 0.5f) continue;
		hm.raise_plateau(hx, hz, r, 0.0f, 0.0f, 18.0f * kMetre, r * 0.6f);
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
			if (std::hypot(bx - sp.x, bz - sp.z) < clear) { ok = false; break; }
		if (!ok) continue;
		const float r = rng.range_float(2.0f, 4.0f), h = rng.range_float(1.0f, 2.0f) * kMetre;
		const float sp = grid_spacing(hm);
		for (float dz = -r; dz <= r; dz += sp)
			for (float dx = -r; dx <= r; dx += sp) {
				const float d = std::hypot(dx, dz);
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
	// stays walkable. Crossing the channel needs ships. 0 A.D. metres x 0.35.
	water_h = 1.0f;
	const float half = hm.map_half, size = 2.0f * half;
	const float land = metres(water_h, 3.0f), bed = metres(water_h, -4.0f);
	hm.init(hm.res, hm.map_half, land + 0.1f * kMetre);
	const float angle = rng.range_float(0.0f, 6.2831853f);
	const float ux = std::cos(angle), uz = std::sin(angle); // along the channel
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
			const float d = std::fabs(wx * nx + wz * nz + 2.0f * std::sin(t * 0.08f + wobble));
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
		const float sx = ux * t0, sz = uz * t0, ex = std::cos(a) * size, ez = std::sin(a) * size;
		// PathPlacer waviness 0.4: a few jittered points on the way.
		std::vector<std::pair<float, float>> pts{{sx, sz}};
		const float len = std::hypot(ex - sx, ez - sz), px = -(ez - sz) / len, pz = (ex - sx) / len;
		for (int k = 1; k < 6; ++k) {
			const float f = (float)k / 6.0f, j = rng.range_float(-0.1f, 0.1f) * len * 0.4f;
			pts.push_back({sx + (ex - sx) * f + px * j, sz + (ez - sz) * f + pz * j});
		}
		pts.push_back({ex, ez});
		std::vector<uint8_t> mask(hm.data.size(), 0);
		for (size_t k = 0; k + 1 < pts.size(); ++k) {
			const float ax = pts[k].first, az = pts[k].second, bx = pts[k + 1].first, bz = pts[k + 1].second;
			const float sl = std::hypot(bx - ax, bz - az);
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
					if (std::hypot(wx - l.first, wz - l.second) < keep) { mask[(size_t)z * (size_t)hm.res + (size_t)x] = 0; break; }
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
	// 128 tiles apart. Islands otherwise need ships. 0 A.D. metres x 0.35.
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
	// More islands, at most 20% of each closer than 6 to another.
	const float sp = grid_spacing(hm);
	const int islands = scale_by_map_size(25, 80, half);
	const int circles = (int)std::floor((float)scale_by_map_size(10, 35, half));
	const float max_r = std::floor((float)scale_by_map_size(8, 10, half));
	for (int i = 0, tries = 0; i < islands && tries < islands * 4; ++tries) {
		auto dist = distance_to_mask(island, hm.res, sp);
		const auto m = chain_placer(hm, rng, rng.range_float(-half, half), rng.range_float(-half, half), 6.0f, max_r, circles);
		int in = 0, bad = 0;
		for (size_t k = 0; k < m.size(); ++k)
			if (m[k]) { ++in; bad += dist[k] < 6.0f; }
		if (!in || bad > in / 5) continue;
		std::vector<uint8_t> paint(m.size(), 0);
		for (size_t k = 0; k < m.size(); ++k) paint[k] = m[k] && dist[k] >= 6.0f;
		paint_mask_elevation(hm, paint, land, 4.0f);
		for (size_t k = 0; k < m.size(); ++k) if (paint[k]) island[k] = 1;
		++i;
	}
	// Undersea bumps.
	{
		const auto dist = distance_to_mask(island, hm.res, sp);
		const int bumps = scale_by_map_size(10, 50, half);
		for (int i = 0; i < bumps; ++i) {
			const auto m = chain_placer(hm, rng, rng.range_float(-half, half), rng.range_float(-half, half), 1.0f,
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
				if (std::hypot(hx - l.first, hz - l.second) < 20.0f + 12.0f + r) { ok = false; break; }
			for (int k = 0; ok && k < 12; ++k) {
				const auto p = polar((float)k * 0.5235988f, r + 1.0f);
				ok = hm.sample_world(hx + p.first, hz + p.second) > water_h + 0.5f;
			}
			if (!ok) continue;
			hm.raise_plateau(hx, hz, r, 0.0f, 0.0f, 5.0f * kMetre, 2.0f);
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
			for (const auto &b : bridges) near |= std::hypot(b.first - s.first, b.second - s.second) < blen * 8.0f;
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
	// scaleByMapSize(1, 4) * players hills. 0 A.D. metres x 0.35.
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
			if (std::hypot(lx - sp.x, lz - sp.z) < 21.0f + 7.0f + lake_r + 3.0f) { ok = false; break; }
		for (const auto &p : placed)
			if (std::hypot(lx - p.first, lz - p.second) < 20.0f + 2.0f * lake_r) { ok = false; break; }
		if (!ok) continue;
		std::vector<uint8_t> m(hm.data.size(), 0);
		mask_disk(hm, m, lx, lz, lake_r + 3.0f);
		paint_mask_elevation(hm, m, metres(water_h, -4.0f), 3.0f);
		placed.push_back({lx, lz});
		++i;
	}
	dry_bumps(hm, rng, spawns, scale_by_map_size(100, 200, half), 26.0f, land - 0.2f);
	scatter_dry_hills(hm, rng, spawns, scale_by_map_size(1, 4, half) * num_players, 26.0f, 5.0f, water_h);
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 26.0f, 4.0f, sp.base_height);
}

namespace {

// fortress.js placeCustomFortress with the "Spahbod" wall list: a square of
// stone walls round the civic centre, wall towers at the corners and beside
// the two gates, and a market, barracks, stable and temple set into the four
// sides (one each). RTS0 walls are axis aligned: the gates go on the two
// sides facing towards and away from the map centre. 2-unit wall pieces on
// a 2-unit lattice, R (odd, 13-21) out from the centre.
// Wall half-size for base i: 21, shrunk (odd, down to 13) when a neighbour
// stands close (tiny maps, many players).
float fortress_radius(const std::vector<RmsGenerator::PlayerSpawn> &spawns, size_t i) {
	float cheb = 1e9f;
	for (size_t j = 0; j < spawns.size(); ++j)
		if (j != i) cheb = std::min(cheb, std::max(std::fabs(spawns[j].x - spawns[i].x), std::fabs(spawns[j].z - spawns[i].z)));
	float R = 21.0f;
	while (R > 13.0f && 2.0f * R + 4.0f > cheb) R -= 2.0f;
	return R;
}

void place_fortress_walls(Simulation &sim, const std::vector<RmsGenerator::PlayerSpawn> &spawns, size_t self) {
	const RmsGenerator::PlayerSpawn &sp = spawns[self];
	const float R = fortress_radius(spawns, self);
	// Pieces that would fall inside a neighbour's fortress are left out, so
	// two overlapping fortresses open into each other instead of sealing.
	auto inside_other = [&](float x, float z) {
		for (size_t j = 0; j < spawns.size(); ++j) {
			if (j == self) continue;
			const float r = fortress_radius(spawns, j) + 2.0f;
			if (std::fabs(x - spawns[j].x) < r && std::fabs(z - spawns[j].z) < r) return true;
		}
		return false;
	};
	auto spawn = [&](float x, float z, uint8_t type) {
		if (!inside_other(x, z)) sim.spawn_building_world(x, z, type, sp.team, 1.0f);
	};
	const bool gates_on_z = std::fabs(sp.forward_dir_z) >= std::fabs(sp.forward_dir_x);
	struct Side { float nx, nz; uint8_t building; bool gate; };
	const Side z_gates[4] = {{0.0f, 1.0f, BLD_MARKET, true}, {1.0f, 0.0f, BLD_BARRACKS, false}, {0.0f, -1.0f, BLD_STABLE, true}, {-1.0f, 0.0f, BLD_TEMPLE, false}};
	const Side x_gates[4] = {{1.0f, 0.0f, BLD_MARKET, true}, {0.0f, -1.0f, BLD_BARRACKS, false}, {-1.0f, 0.0f, BLD_STABLE, true}, {0.0f, 1.0f, BLD_TEMPLE, false}};
	for (const Side &s : gates_on_z ? z_gates : x_gates) {
		const float tx = -s.nz, tz = s.nx;
		auto at = [&](float along, float out) { return std::make_pair(sp.x + s.nx * out + tx * along, sp.z + s.nz * out + tz * along); };
		// The side's building straddles the wall line 12 along, inside it.
		const float b_along = -2.0f * std::floor((R + 3.0f) / 4.0f); // -12 at R = 21
		const auto b = at(b_along, R - 1.0f);
		spawn(b.first, b.second, s.building);
		for (float along = -R + 2.0f; along <= R - 2.0f; along += 2.0f) {
			if (std::fabs(along - b_along) < 2.0f) continue;
			uint8_t type = BLD_WALL_SEGMENT;
			if (std::fabs(std::fabs(along) - 5.0f) < 0.5f) type = BLD_WALL_TOWER;
			else if (s.gate && std::fabs(along) < 2.0f) type = BLD_GATE;
			const auto p = at(along, R);
			spawn(p.first, p.second, type);
		}
	}
	for (int c = 0; c < 4; ++c) spawn(sp.x + (c & 1 ? R : -R), sp.z + (c & 2 ? R : -R), BLD_WALL_TOWER);
}

} // namespace

// =========================================================================
// 0 A.D. trigger maps: maps/random/flood.js and extinct_volcano.js. Their
// rising water is a map trigger (triggers.h; the ported flood_triggers.js and
// extinct_volcano_triggers.js are in simulation.cpp). Heights are 0 A.D.'s
// metres above the water times kTriggerMapHeight, the same factor the
// triggers use for the water's rise, so the hills drown in 0 A.D.'s order.
// =========================================================================

namespace {
constexpr float kTriggerMapHeight = 0.5f;
}

void RmsGenerator::synth_flood(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// flood.js: sea ground -2, a home island at 2 for each player on a circle
	// at f = 0.38, and a mainland at 6 wherever the ground is 40 tiles clear
	// of every home island, with hills (set to 6-20) and mountains (+15-29)
	// on it. The water starts at 0 and rises (flood_triggers.js).
	const float k = kTriggerMapHeight;
	water_h = 0.0f;
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
		for (const auto &sp : spawns) d = std::min(d, std::hypot(wx - sp.x, wz - sp.z));
		return d;
	};
	for (int z = 0; z < hm.res; ++z) {
		const float wz = -half + z * step;
		for (int x = 0; x < hm.res; ++x) {
			const float wx = -half + x * step;
			const float r = std::hypot(wx, wz);
			const float t = smoothstep(clear, clear + shore, island_dist(wx, wz)) * (1.0f - smoothstep(half - 22.0f, half - 16.0f, r));
			if (t <= 0.0f) continue;
			const float cur = hm.get(x, z);
			hm.set(x, z, std::max(cur, cur + (6.0f * k - cur) * t));
		}
	}
	// Hills (ELEVATION_SET 6-20) and mountains (ELEVATION_MODIFY 15-29) on the
	// mainland: 6 tiles inside its shore and 40 clear of the home islands.
	auto on_mainland = [&](float wx, float wz, float r) {
		return island_dist(wx, wz) > clear + shore + r && std::hypot(wx, wz) < half - 22.0f - r &&
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
	water_h = 0.0f;
	hm.init(hm.res, hm.map_half, 1.0f * k);
	const float half = hm.map_half;
	const float player_r = (float)scale_by_map_size(15, 25, half); // defaultPlayerBaseRadius()
	place_ring_spawns(hm, spawns, num_players, 0.35f * 2.0f * half, rng.range_float(0.0f, 6.2831853f), 6.2831853f, 25.0f * k);
	for (auto &sp : spawns) {
		sp.base_height = 25.0f * k;
		// createMountain's omnidirectional ramp, then the flattened Civic Centre area.
		hm.level_area(sp.x, sp.z, 14.0f, player_r, 25.0f * k);
	}
	// createVolcano(mapCenter, ..., ELEVATION_SET): each ring inside the last.
	const float rings[5][2] = {
		{(float)scale_by_map_size(18, 25, half), 15.0f}, {(float)scale_by_map_size(16, 23, half), 25.0f},
		{(float)scale_by_map_size(10, 15, half), 45.0f}, {(float)scale_by_map_size(8, 11, half), 62.0f},
		{(float)scale_by_map_size(4, 6, half), 42.0f}};
	for (int i = 0; i < 5; ++i)
		hm.level_area(0.0f, 0.0f, std::max(1.0f, rings[i][0] - (i == 4 ? 1.0f : 3.0f)), i == 4 ? 1.0f : 3.0f, rings[i][1] * k);
	const float volcano_r = rings[0][0];
	auto clear_of = [&](float x, float z, float r, float gap) {
		for (const auto &sp : spawns)
			if (std::hypot(x - sp.x, z - sp.z) < player_r + 14.0f + r + gap) return false;
		return std::hypot(x, z) > volcano_r + r + gap && std::hypot(x, z) < half - 4.0f - r;
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
			if (std::hypot(lx - l.first, lz - l.second) < 2.0f * r + 12.0f) { ok = false; break; }
		if (!ok) continue;
		hm.level_area(lx, lz, r, 3.0f, -4.0f * k);
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
			if (std::hypot(hx - p.first, hz - p.second) < 2.0f * r + 15.0f) { ok = false; break; }
		if (!ok) continue;
		hm.level_area(hx, hz, r, 2.0f, 18.0f * k);
		placed.push_back({hx, hz});
		++i;
	}
	for (const auto &sp : spawns) hm.level_area(sp.x, sp.z, 14.0f, 4.0f, sp.base_height);
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
				const float x = std::cos(a) * d, z = std::sin(a) * d;
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
			if (std::hypot(x, z) > half * 0.92f) continue;
			const float h = hm.sample_world(x, z);
			if (h < water_h + 1.0f * kTriggerMapHeight + 0.5f || h > 18.0f * kTriggerMapHeight - 1.0f) continue;
			if (hm.compute_slope_world(x, z) > 0.6f) continue;
			bool ok = true;
			for (const auto &sp : spawns)
				if (std::hypot(x - sp.x, z - sp.z) < (float)scale_by_map_size(15, 25, half) + 24.0f) { ok = false; break; }
			for (const auto &t : towers)
				if (std::hypot(x - t.first, z - t.second) < 60.0f) { ok = false; break; }
			if (!ok) continue;
			sim.spawn_building_world(x, z, BLD_TOWER, GAIA_PLAYER, 1.0f);
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
			bool ok = hm.compute_slope_world(x, z) <= 0.6f;
			for (int k = 0; ok && k < 9; ++k) {
				const float a = 0.7853982f * (float)k, r = k == 8 ? 0.0f : 2.0f;
				ok = hm.sample_world(x + std::cos(a) * r, z + std::sin(a) * r) > water_h;
			}
			for (size_t k = 0; ok && k < spawns.size(); ++k) ok = std::hypot(x - spawns[k].x, z - spawns[k].z) >= base;
			for (size_t k = 0; ok && k < mines.size(); ++k) ok = std::hypot(x - mines[k].first, z - mines[k].second) >= 4.0f;
			for (size_t k = 0; ok && k < points.size(); ++k) ok = std::hypot(x - points[k].first, z - points[k].second) >= 20.0f;
			if (!ok) continue;
			sim.add_trigger_point('A', x, z);
			points.push_back({x, z});
		}
	}
}

void RmsGenerator::distribute_player_bases(Simulation &sim, const HeightmapGrid &hm, const std::vector<PlayerSpawn> &spawns, RmsRng &rng, const Config &config) {
	const bool fortress = config.map_type == MAP_FORTRESS;
	// 0 A.D.'s base placers (rmgen's placePlayerBase*) keep every starting
	// resource on land and away from water (avoidClasses(clWater, ...)).
	// A cluster that would fall in water, or across a strait on other land,
	// is turned around the civic centre at the same distance, then pulled
	// back towards it, until it is dry (Continental seed 42 had its
	// forward gold under water).
	const MapType mt = config.map_type;
	const float dry_h = sim.water_height() + 0.6f;
	auto dry = [&](float px, float pz, float &cx, float &cz, float spread) {
		auto wet = [&](float x, float z) {
			if (hm.sample_world(x, z) < dry_h) return true;
			for (int k = 0; k < 8; ++k) {
				const float a = 0.7853982f * (float)k;
				if (hm.sample_world(x + std::cos(a) * spread, z + std::sin(a) * spread) < dry_h) return true;
			}
			// Dry land across a strait (another island) does not count.
			const float l = std::hypot(x - px, z - pz);
			for (float t = 0.0f; t < l; t += 0.5f)
				if (hm.sample_world(px + (x - px) * t / l, pz + (z - pz) * t / l) < dry_h) return true;
			return false;
		};
		if (!wet(cx, cz)) return;
		const float d = std::hypot(cx - px, cz - pz);
		if (d < 1e-3f) return;
		const float a0 = std::atan2(cz - pz, cx - px);
		const float ox = cx, oz = cz;
		for (float r = d; r >= 7.0f; r -= 0.5f)
			for (int k = 0; k <= 12; ++k) {
				// 0, +15, -15, +30, ... degrees, up to 90 either side.
				const float a = a0 + 0.2617994f * (float)((k + 1) / 2) * ((k & 1) ? 1.0f : -1.0f);
				const float tx = px + std::cos(a) * r, tz = pz + std::sin(a) * r;
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
		uint8_t team = sp.team;
		float px = sp.x;
		float pz = sp.z;
		float fx = sp.forward_dir_x;
		float fz = sp.forward_dir_z;
		float rx = -fz;
		float rz = fx;

		// 1. Town Center
		sim.spawn_building_world(px, pz, BLD_TOWN_CENTER, team, 1.0f);

		// 2. Starting Barracks & Houses (Fortress: the walls and their buildings)
		if (!fortress) {
			sim.spawn_building_world(px + fx * 8.5f, pz + fz * 8.5f, BLD_BARRACKS, team, 1.0f);
			sim.spawn_building_world(px - fx * 6.5f + rx * 6.0f, pz - fz * 6.5f + rz * 6.0f, BLD_HOUSE, team, 1.0f);
			sim.spawn_building_world(px - fx * 6.5f - rx * 6.0f, pz - fz * 6.5f - rz * 6.0f, BLD_HOUSE, team, 1.0f);
		}

		// 3. Starting Citizen-Soldiers & Villagers
		// 6 Villagers in a ring around TC
		for (int v = 0; v < 6; ++v) {
			float a = (6.2831853f * v) / 6.0f;
			float vx = px + std::cos(a) * 4.5f;
			float vz = pz + std::sin(a) * 4.5f;
			sim.spawn_unit(vx, vz, team, UNIT_VILLAGER, 100.0f);
		}

		// Military Escort
		sim.spawn_unit(px + fx * 6.0f + rx * 2.0f, pz + fz * 6.0f + rz * 2.0f, team, UNIT_SWORDSMAN, 160.0f);
		sim.spawn_unit(px + fx * 6.0f - rx * 2.0f, pz + fz * 6.0f - rz * 2.0f, team, UNIT_SWORDSMAN, 160.0f);
		sim.spawn_unit(px + fx * 5.0f + rx * 4.5f, pz + fz * 5.0f + rz * 4.5f, team, UNIT_ARCHER, 90.0f);
		sim.spawn_unit(px + fx * 5.0f - rx * 4.5f, pz + fz * 5.0f - rz * 4.5f, team, UNIT_ARCHER, 90.0f);
		sim.spawn_unit(px + fx * 9.5f, pz + fz * 9.5f, team, UNIT_CAVALRY, 180.0f);

		// fortress.js gives no starting mines, trees or berries, only
		// treasures (which RTS0 has no entity for yet).
		if (fortress) {
			place_fortress_walls(sim, spawns, (size_t)(&sp - spawns.data()));
			continue;
		}

		// islands.js: a dock on the shore towards the map centre
		// (findLocationInDirectionBasedOnHeight from the base to the centre).
		if (mt == MAP_ISLANDS) {
			const float d = std::hypot(px, pz);
			for (float t = 8.0f; t < d; t += 0.5f) {
				const float dx = px - px / d * t, dz = pz - pz / d * t;
				if (hm.sample_world(dx, dz) >= sim.water_height() + NAV_WATER_MARGIN) continue;
				sim.spawn_building_world(dx, dz, BLD_DOCK, team, 1.0f);
				break;
			}
		}

		// 4. Primary Food: Starter Berry Bushes (5 gaia/fruit/berry_01 bushes, Max 200 food each)
		float berry_cx = px + fx * 5.5f + rx * 6.5f;
		float berry_cz = pz + fz * 5.5f + rz * 6.5f;
		dry(px, pz, berry_cx, berry_cz, 2.5f);
		for (int b = 0; b < 5; ++b) {
			float ba = (6.2831853f * b) / 5.0f;
			float bx = berry_cx + std::cos(ba) * 1.5f;
			float bz = berry_cz + std::sin(ba) * 1.5f;
			sim.spawn_resource_world(bx, bz, RES_FOOD, 200);
		}

		// 5. Primary Forest: Dense Woodline (10 spots of two 200-wood trees)
		float forest_cx = px - fx * 10.5f + rx * 3.5f;
		float forest_cz = pz - fz * 10.5f + rz * 3.5f;
		dry(px, pz, forest_cx, forest_cz, 5.0f);
		for (int t = 0; t < 10; ++t) {
			float ta = -1.2f + (2.4f * t) / 9.0f;
			float tx = forest_cx + std::cos(ta) * 4.0f + rng.range_float(-0.4f, 0.4f);
			float tz = forest_cz + std::sin(ta) * 4.0f + rng.range_float(-0.4f, 0.4f);
			spawn_trees(sim, hm, tx, tz, dry_h);
		}

		// 6-7. rmgen placePlayerBaseMines: one large metal mine and one large
		// stone mine (template_gaia_ore_large / _rock_large, 5000 each).
		float gold_cx = px + fx * 3.5f - rx * 11.5f;
		float gold_cz = pz + fz * 3.5f - rz * 11.5f;
		dry(px, pz, gold_cx, gold_cz, 2.8f);
		sim.spawn_resource_world(gold_cx, gold_cz, RES_GOLD, LARGE_MINE_SUPPLY);

		float stone_cx = px - fx * 8.5f - rx * 9.5f;
		float stone_cz = pz - fz * 8.5f - rz * 9.5f;
		dry(px, pz, stone_cx, stone_cz, 2.5f);
		sim.spawn_resource_world(stone_cx, stone_cz, RES_STONE, LARGE_MINE_SUPPLY);

		// 8. Secondary Forward Gold Mine (Expansion): small metal rocks, 1000 each
		float sec_gold_cx = px + fx * 22.0f + rx * 6.0f;
		float sec_gold_cz = pz + fz * 22.0f + rz * 6.0f;
		dry(px, pz, sec_gold_cx, sec_gold_cz, 2.8f);
		for (int g = 0; g < 3; ++g) {
			float ga = (6.2831853f * g) / 3.0f;
			sim.spawn_resource_world(sec_gold_cx + std::cos(ga) * 1.8f, sec_gold_cz + std::sin(ga) * 1.8f, RES_GOLD, MINE_SUPPLY);
		}

		// 9. Secondary Stone Mine (Flank): small stone rocks, 1000 each
		float sec_stone_cx = px + fx * 18.0f - rx * 16.0f;
		float sec_stone_cz = pz + fz * 18.0f - rz * 16.0f;
		dry(px, pz, sec_stone_cx, sec_stone_cz, 2.5f);
		for (int s = 0; s < 2; ++s) {
			sim.spawn_resource_world(sec_stone_cx + (s == 0 ? -1.3f : 1.3f), sec_stone_cz, RES_STONE, MINE_SUPPLY);
		}

		const BiomeFauna bf = biome_fauna(config.biome, (int)config.seed);
		// 10. Starter animals near the base (rmbiome startingAnimal, e.g. chickens)
		float deer_cx = px - fx * 14.0f - rx * 5.0f;
		float deer_cz = pz - fz * 14.0f - rz * 5.0f;
		dry(px, pz, deer_cx, deer_cz, 3.5f);
		for (int d = 0; d < 4; ++d) {
			float da = (6.2831853f * d) / 4.0f;
			sim.spawn_fauna(deer_cx + std::cos(da) * 2.5f, deer_cz + std::sin(da) * 2.5f, bf.starting);
		}

		// 11. Starter hunt: 2 of the biome's main huntable animal
		float boar_cx = px + fx * 19.0f - rx * 5.0f;
		float boar_cz = pz + fz * 19.0f - rz * 5.0f;
		dry(px, pz, boar_cx, boar_cz, 2.5f);
		sim.spawn_fauna(boar_cx - 1.5f, boar_cz, bf.main_hunt);
		sim.spawn_fauna(boar_cx + 1.5f, boar_cz, bf.main_hunt);
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
	else if (config.map_type == MAP_CANYON) num_groves = num_groves / 2;
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

	// Map-specific landmark resource clusters
	if (config.map_type == MAP_DANUBIUS) {
		// Central Island Fortress Resources
		sim.spawn_resource_world(-2.5f, 0.0f, RES_GOLD, MINE_SUPPLY);
		sim.spawn_resource_world( 2.5f, 0.0f, RES_GOLD, MINE_SUPPLY);
		sim.spawn_resource_world(0.0f, -2.5f, RES_STONE, MINE_SUPPLY);
		sim.spawn_resource_world(0.0f,  2.5f, RES_STONE, MINE_SUPPLY);
		// Oak grove on fortress island
		for (int t = 0; t < 8; ++t) {
			float a = (6.2831853f * t) / 8.0f;
			spawn_trees(sim, hm, std::cos(a) * 5.0f, std::sin(a) * 5.0f, water_h + 0.3f);
		}
		// Sandbar resources
		sim.spawn_resource_world(-half * 0.30f, 0.0f, RES_STONE, MINE_SUPPLY);
		sim.spawn_resource_world( half * 0.30f, 0.0f, RES_STONE, MINE_SUPPLY);
	} else if (config.map_type == MAP_SAHARAN_OASES) {
		// Date palm tree rings and berry groves around each oasis
		std::vector<std::pair<float, float>> oases_centers = {
			{0.0f, 0.0f},
			{-half * 0.44f, -half * 0.44f * 0.65f},
			{ half * 0.44f, -half * 0.44f * 0.65f},
			{-half * 0.44f,  half * 0.44f * 0.65f},
			{ half * 0.44f,  half * 0.44f * 0.65f}
		};
		for (size_t oi = 0; oi < oases_centers.size(); ++oi) {
			float ox = oases_centers[oi].first;
			float oz = oases_centers[oi].second;
			float o_rad = (oi == 0) ? (half * 0.16f + 1.5f) : (half * 0.10f + 1.2f);
			int num_palms = (oi == 0) ? 14 : 8;
			for (int p = 0; p < num_palms; ++p) {
				float a = (6.2831853f * p) / (float)num_palms + rng.range_float(-0.2f, 0.2f);
				float tx = ox + std::cos(a) * o_rad;
				float tz = oz + std::sin(a) * o_rad;
				if (hm.sample_world(tx, tz) > water_h + 0.15f) spawn_trees(sim, hm, tx, tz, water_h + 0.15f);
			}
			// Oasis date bushes (gaia/fruit/date, Max 400)
			for (int b = 0; b < 3; ++b) {
				float ba = (6.2831853f * b) / 3.0f + 0.5f;
				float bx = ox + std::cos(ba) * (o_rad + 2.0f);
				float bz = oz + std::sin(ba) * (o_rad + 2.0f);
				if (hm.sample_world(bx, bz) > water_h + 0.15f) {
					sim.spawn_resource_world(bx, bz, RES_FOOD, 400);
				}
			}
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
	} else if (config.map_type == MAP_JEBEL_BARKAL) {
		// Summit of Jebel Barkal high-ground treasure & quarries
		float mesa_z = half * 0.06f;
		sim.spawn_resource_world(-2.5f, mesa_z, RES_GOLD, MINE_SUPPLY);
		sim.spawn_resource_world( 2.5f, mesa_z, RES_GOLD, MINE_SUPPLY);
		sim.spawn_resource_world( 0.0f, mesa_z + 2.5f, RES_STONE, MINE_SUPPLY);
		sim.spawn_resource_world( 0.0f, mesa_z - 2.5f, RES_STONE, MINE_SUPPLY);
		for (int t = 0; t < 6; ++t) {
			float a = (6.2831853f * t) / 6.0f;
			spawn_trees(sim, hm, std::cos(a) * 4.5f, mesa_z + std::sin(a) * 4.5f, water_h + 0.3f);
		}
		// Surrounding savanna quarries along base
		sim.spawn_resource_world(-half * 0.48f,  half * 0.42f, RES_STONE, LARGE_MINE_SUPPLY);
		sim.spawn_resource_world( half * 0.48f,  half * 0.42f, RES_STONE, LARGE_MINE_SUPPLY);
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
			for (const auto &q : placed) crowded |= std::hypot(p.first - q.first, p.second - q.second) < 8.0f;
			if (crowded) continue;
			placed.push_back(p);
			const uint8_t type = levant ? (g % 3 == 1 ? RES_GOLD : RES_STONE) : (rng.range_int(0, 1) ? RES_GOLD : RES_STONE);
			if (levant && g % 3 == 2) {
				// The small stone group: three template_gaia_rock rocks.
				for (int r = 0; r < 3; ++r) {
					const float a = 6.2831853f * (float)r / 3.0f;
					sim.spawn_resource_world(p.first + std::cos(a) * 1.6f, p.second + std::sin(a) * 1.6f, type, MINE_SUPPLY);
				}
			} else {
				sim.spawn_resource_world(p.first, p.second, type, LARGE_MINE_SUPPLY);
			}
			++g;
		}
	}

	// 1. Organic Forest Clumps
	for (int g = 0; g < num_groves; ++g) {
		float gx = rng.range_float(-half * 0.82f, half * 0.82f);
		float gz = rng.range_float(-half * 0.82f, half * 0.82f);

		// Exclude player starting radii
		bool near_player = false;
		for (const auto &sp : spawns) {
			float d = std::sqrt((gx - sp.x) * (gx - sp.x) + (gz - sp.z) * (gz - sp.z));
			if (d < 16.0f) { near_player = true; break; }
		}
		if (near_player) continue;

		// Check terrain suitability
		float h = hm.sample_world(gx, gz);
		if (h < water_h + 0.3f) continue;
		if (hm.compute_slope_world(gx, gz) > 0.85f) continue; // Skip cliffs

		int trees_in_grove = rng.range_int(6, 12);
		float grove_r = rng.range_float(3.0f, 5.5f);
		for (int t = 0; t < trees_in_grove; ++t) {
			float a = rng.range_float(0.0f, 6.2831853f);
			float r = std::sqrt(rng.next_float()) * grove_r;
			float tx = gx + std::cos(a) * r;
			float tz = gz + std::sin(a) * r;
			if (hm.sample_world(tx, tz) > water_h + 0.2f) spawn_trees(sim, hm, tx, tz, water_h + 0.2f);
		}
	}

	// 2. Contested Neutral Gold & Stone Quarries
	int num_neutral_mines = std::max(4, config.num_players * 2) * scale_by_map_size(1, 4, half);
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
			sim.spawn_resource_world(mx + std::cos(a) * 1.6f, mz + std::sin(a) * 1.6f, res_type, MINE_SUPPLY);
		}
	}

	distribute_land_extras(sim, hm, water_h, spawns, rng, config);
}

void RmsGenerator::distribute_wildlife(Simulation &sim, const HeightmapGrid &hm, float water_h, const std::vector<PlayerSpawn> &spawns, RmsRng &rng, const Config &config) {
	float half = hm.map_half;
	int num_packs = scale_by_map_size(10, 40, half);
	if (config.map_type == MAP_ARCTIC_SUMMER || config.map_type == MAP_RATUMACOS) num_packs *= 2;

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
			sim.spawn_fauna(px + std::cos(a) * r, pz + std::sin(a) * r, ftype);
		}
	}
}

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
			if (std::hypot(x - sp.x, z - sp.z) < d) return false;
		return true;
	};
	// SimpleObject(template, n, n, 0, spread): n treasures within `spread` of (x, z).
	auto cluster = [&](float x, float z, uint8_t type, int n, float spread) {
		for (int k = 0; k < n; ++k) {
			const float a = rng.range_float(0.0f, 6.2831853f), r = rng.range_float(0.0f, spread);
			const float tx = x + std::cos(a) * r, tz = z + std::sin(a) * r;
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
		for (const auto &sp : spawns)
			for (int k = 0; k < n; ++k) {
				const float a = rng.range_float(0.0f, 6.2831853f), r = rng.range_float(10.0f, 13.0f);
				const float tx = sp.x + std::cos(a) * r, tz = sp.z + std::sin(a) * r;
				if (dry(tx, tz)) sim.spawn_treasure(tx, tz, type);
			}
	};

	switch (config.map_type) {
		case MAP_ATLAS_MOUNTAINS: // atlas_mountains.js: 3 x players groups of 2-3 food bins, then of wood
			scatter(TREASURE_FOOD_BIN, 3 * np, 2, 3, 18.0f);
			scatter(TREASURE_WOOD, 3 * np, 2, 3, 18.0f);
			break;
		case MAP_CANYON: // canyon.js: 3-8 food bins and as many wood treasures within 7 of the centre
			for (int i = 0, n = rng.range_int(3, 8); i < n; ++i)
				for (uint8_t t : {TREASURE_FOOD_BIN, TREASURE_WOOD}) cluster(0.0f, 0.0f, t, 1, 7.0f);
			break;
		case MAP_POLAR_SEA: // polar_sea.js: 14 wood treasures in each base
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
			for (const auto &sp : spawns) {
				const float tx = sp.x + sp.forward_dir_x * 6.0f, tz = sp.z + sp.forward_dir_z * 6.0f;
				if (dry(tx, tz)) sim.spawn_treasure(tx, tz, TREASURE_FOOD_BIN);
			}
			break;
		case MAP_DEEP_FOREST: // deep_forest.js: the unfinished Greek temple on the central hill
			sim.spawn_ruins(0.0f, 0.0f, ruins_type_by_name("unfinished_greek_temple"));
			break;
		// RTS0 landmarks: a rich treasure on the summit of Jebel Barkal and on the Danubius island fort.
		case MAP_JEBEL_BARKAL: sim.spawn_treasure(0.0f, half * 0.06f, TREASURE_METAL_PERSIAN_BIG); break;
		case MAP_DANUBIUS: sim.spawn_treasure(0.0f, 0.0f, TREASURE_METAL_PERSIAN_BIG); break;
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
		const float dx = std::sin(a), dz = std::cos(a);
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
	const float len = std::max(1e-3f, std::hypot(bx - ax, bz - az));
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
	std::vector<uint8_t> outside(mask.size());
	for (size_t i = 0; i < mask.size(); ++i) outside[i] = !mask[i];
	const auto d = distance_to_mask(outside, hm.res, grid_spacing(hm));
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
		for (const auto &p : pts) d[i] = std::min(d[i], std::hypot(wx - p.first, wz - p.second) - r);
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

// paintRiver (gaia_terrain.js), banks meandering independently, no
// deviation: the water between the banks gets `bed`, rising to `land` over
// `fade` tiles at each bank.
void seas_paint_river(HeightmapGrid &hm, RmsRng &rng, float sx, float sz, float ex, float ez, float width, float fade,
		float bed, float land, float meander_short, float meander_long) {
	const float half = hm.map_half, size = 2.0f * half;
	const double ms = size * meander_short / seas_scale(35.0f, 160.0f, half);
	const double ml = size * meander_long / seas_scale(35.0f, 100.0f, half);
	const double seed1 = rng.range_float(2.0f, 3.0f), seed2 = rng.range_float(2.0f, 3.0f);
	const double ang1 = rng.range_float(0.0f, 1.0f), ang2 = rng.range_float(0.0f, 1.0f);
	auto curve = [&](double frac, double a, double seed) {
		return ms * seas_rnd_river(a + size * frac / 128.0, seed) + ml * seas_rnd_river(a + size * frac / 256.0, seed);
	};
	const float len = std::hypot(sx - ex, sz - ez);
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
// floor(scaleByMapSize(16, 40)), 0.5)) or createMountains (cones of
// scaleByMapSize(30, 50) m from circles of floor(scaleByMapSize(3, 4)) to
// floor(scaleByMapSize(6, 12)) tiles), avoiding each other by `spacing`.
// `allow` is the script's other constraints.
template <class Allow>
void seas_hills(HeightmapGrid &hm, RmsRng &rng, float water_h, float amount, bool mountains, float spacing, float hill_m,
		const Allow &allow) {
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
		if (mountains) paint_mask_elevation(hm, m, metres(water_h, std::floor(seas_scale(30, 50, half))), (float)std::floor(seas_scale(6, 12, half)));
		else paint_mask_elevation(hm, m, metres(water_h, hill_m), 2.0f);
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

} // namespace

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
	const float nx = std::cos(angle), nz = std::sin(angle); // across the sea
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
		seas_add(islands, m);
		to_island = seas_dist_inside(hm, islands);
		return true;
	});
	seas_spawns(hm, spawns, locs, 16.0f, 6.0f, water_h, land);
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
	const auto locs = seas_circle_players(num_players, rng.range_float(0.0f, 2.0f * kSeasPi), 0.35f * size, teams);
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
	std::vector<uint8_t> player_mask(hm.data.size(), 0);
	std::vector<std::vector<int>> areas;
	std::vector<std::pair<float, float>> spots;
	auto island = [&](float x, float z, float r, bool player) {
		const auto m = seas_clump(hm, rng, kSeasPi * r * r, 0.7f, 0.1f, 1e9f, x, z, [&](int i) { return !player_mask[(size_t)i]; });
		paint_mask_elevation(hm, m, land, 5.0f);
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
		island(p.first, p.second, island_r, is_player);
	}
	for (const auto &c : central) island(c.first, c.second, central_r, false);
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
	// meet on foot round the ends of the spines. 0 A.D. metres x 0.35.
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
// shallows matter. Land is metres() (x 0.35) lifted by up to 0.25 over its
// first metre, so 0 A.D.'s low 1 m plains stay dry land in RTS0's nav grid
// (water below water_h + 0.3) and keep their resources. 0 to -2 m, where 0
// A.D.'s land units still wade (MaxWaterDepth 2), is squeezed into RTS0's
// wadeable shallows; deeper water drops on at 0.35 per metre.
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
	const float c = std::cos(a), s = std::sin(a);
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
	const float len = std::hypot(ex - sx, ez - sz);
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
			const float frac = std::hypot(fx - sx, fz - sz) / len;
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
	const float len = std::hypot(ex - sx, ez - sz);
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
				const float d = std::hypot(wx - bx, wz - bz);
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
		auto m = chain_placer(hm, rng, rng.range_float(-half, half), rng.range_float(-half, half), std::max(1.0f, min_r), std::max(1.0f, max_r),
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
		if (std::hypot(x - l.first, z - l.second) < r) return true;
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
			const float a = (float)k * 0.39269908f, fx = std::cos(a), fz = std::sin(a), rx = -fz, rz = fx;
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
// so the low coasts of the heightmap maps (0-15 m, x 0.35, scaled down on
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
				for (const auto &q : locs) d = std::min(d, std::hypot(c.first - q.first, c.second - q.second));
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
	// corsica.js: -8 m sea; two islands of radius 0.4 of the map round two
	// opposite corners (which pair at random), each at 5 m with six sub-
	// islands of 0.09 along its edge, -5 m creeks at 0.52 and -1 m beaches
	// with passages out to 0.57; a +30 m main relief blended over 0.45 of
	// the map, a +9 m plateau (0.35) and, above 150 tiles, a +8 m one (0.2),
	// both with passages. Half the players (sorted by team) on each island,
	// on an arc at 0.25 from its corner; bumps on the islands and -5 m dips
	// in the sea. Crossing needs ships.
	water_h = 1.0f;
	const float half = hm.map_half, size = 2.0f * half, pi = 3.14159265f;
	auto M = [&](float m) { return coast_metres(water_h, m); };
	hm.init(hm.res, half, M(-8.0f));
	const float swap = rng.range_int(0, 1) ? pi / 2.0f : 0.0f;
	const auto c0 = coast_rot(-0.45f * size, -0.45f * size, -swap);
	const std::pair<float, float> centres[2] = {c0, {-c0.first, -c0.second}};
	const float r_beach = 0.57f * size, r_creeks = 0.52f * size, r_island = 0.4f * size, r_level1 = 0.35f * size;
	const float r_player = 0.25f * size, r_level2 = 0.2f * size;
	const float nb_creeks = coast_sbms(6.0f, 15.0f, half), nb_beaches = coast_sbms(2.0f, 5.0f, half);
	const float nb_pass1 = coast_sbms(4.0f, 8.0f, half), nb_pass2 = coast_sbms(2.0f, 4.0f, half);
	const int nb_sub = 5;
	const size_t cells = hm.data.size();
	std::vector<uint8_t> island(cells, 0), passage(cells, 0);
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
		}
		for (int i = 0; i < nb_sub + 1; ++i) {
			const auto p = at(k, pi * ((float)k + (float)i / (float)(nb_sub * 2)) + swap, r_island);
			std::vector<uint8_t> m(cells, 0);
			mask_disk(hm, m, p.first, p.second, 0.09f * size);
			paint_mask_elevation(hm, m, M(5.0f), 1.0f);
			for (size_t j = 0; j < cells; ++j) if (m[j]) island[j] = 1;
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
			coast_passage(hm, s.first, s.second, e.first, e.second, 18.0f, 25.0f, 4.0f);
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
			coast_passage(hm, s.first, s.second, e.first, e.second, 10.0f, 6.0f, 3.0f, &passage);
		}
		if (size > 150.0f) {
			std::vector<uint8_t> m(cells, 0);
			mask_disk(hm, m, cx, cz, r_level2);
			coast_modify_mask(hm, m, 8.0f * kMetre, 1.0f);
			for (int i = 0; i < nb_pass2; ++i) {
				const float a = pi * ((float)i / (2.0f * nb_pass2) + 1.0f / (4.0f * nb_pass2) + (float)k) + swap;
				const auto s = at(k, a, r_level2 + 3.0f), e = at(k, a, r_level2 - 6.0f);
				coast_passage(hm, s.first, s.second, e.first, e.second, 4.0f, 6.0f, 2.0f, &passage);
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
		if (per < 2 || 2.0f * r_player * std::sin(pi / (4.0f * (float)per)) >= 24.0f) continue;
		std::vector<std::pair<float, float>> cand;
		for (int z = 0; z < hm.res; ++z)
			for (int x = 0; x < hm.res; ++x) {
				float wx, wz;
				hm.grid_to_world(x, z, wx, wz);
				if (std::fabs(wx) > half - 10.0f || std::fabs(wz) > half - 10.0f) continue;
				if (std::hypot(wx - centres[k].first, wz - centres[k].second) > r_island - 12.0f) continue;
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
				for (const auto &q : chosen) d = std::min(d, std::hypot(c.first - q.first, c.second - q.second));
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
	make_heightmap_spawns(hm, spawns, locs, 14.0f, 6.0f, water_h);
	for (size_t i = 0; i < spawns.size(); ++i) {
		// Face the island's centre, then turn to keep the layout on land.
		auto &sp = spawns[i];
		const float dx = centres[home[i]].first - sp.x, dz = centres[home[i]].second - sp.z, l = std::max(1e-3f, std::hypot(dx, dz));
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
		                  [&](const std::vector<uint8_t> &m) { paint_mask_elevation(hm, m, M(15.0f), 2.0f); });
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
	for (int i = 0; i < num_players; ++i) locs[order[i]] = polar(start + 6.2831853f * (float)i / (float)num_players, 0.35f * size);
	coast_bumps(hm, rng, water_h, scale_by_map_size(300, 800, half), coast_sbms(20.0f, 50.0f, half), 2.0f, 2.0f,
	            [&](float x, float z, float r) { return !coast_near(locs, x, z, 18.0f + r); });
	const size_t cells = hm.data.size();
	std::vector<uint8_t> hills, water(cells, 0);
	{
		const auto blocked = coast_near_spots(hm, locs, 25.0f);
		hills = coast_chain_areas(hm, rng, scale_by_map_size(1, 4, half) * num_players * 3, 1.0f, std::floor(coast_sbms(4.0f, 6.0f, half)),
		                          (int)std::floor(coast_sbms(16.0f, 40.0f, half)), 0.5f, blocked, 15.0f,
		                          [&](const std::vector<uint8_t> &m) { paint_mask_elevation(hm, m, M(15.0f), 2.0f); });
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
			                                 [&](const std::vector<uint8_t> &mm) { paint_mask_elevation(hm, mm, M(-2.0f), 3.0f); });
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
			const float dl = std::max(1e-6f, std::hypot(s1.first - s0.first, s1.second - s0.second));
			const float sx = s0.first + (s1.first - s0.first) / dl * 4.0f, sz = s0.second + (s1.second - s0.second) / dl * 4.0f;
			const auto e = coast_rot(size, 0.0f, trib_angle - sign * rng.range_float(pi / 5.0f, 2.0f * pi - pi / 5.0f));
			// PathPlacer, waviness 0.4: jittered points on the way.
			std::vector<std::pair<float, float>> pts{{sx, sz}};
			const float len = std::max(1.0f, std::hypot(e.first - sx, e.second - sz));
			const float px = -(e.second - sz) / len, pz = (e.first - sx) / len;
			for (int k = 1; k < 6; ++k) {
				const float f = (float)k / 6.0f, j = rng.range_float(-0.1f, 0.1f) * len * 0.4f;
				pts.push_back({sx + (e.first - sx) * f + px * j, sz + (e.second - sz) * f + pz * j});
			}
			pts.push_back(e);
			std::vector<uint8_t> m(cells, 0);
			for (size_t k = 0; k + 1 < pts.size(); ++k) {
				const float ax = pts[k].first, az = pts[k].second, bx = pts[k + 1].first, bz = pts[k + 1].second;
				const float sl = std::max(1e-3f, std::hypot(bx - ax, bz - az));
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
			                  0.5f, blocked, 15.0f, [&](const std::vector<uint8_t> &m) { paint_mask_elevation(hm, m, M(18.0f), 2.0f); });
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
	const float dx = bx - ax, dz = bz - az, len = std::max(1e-3f, std::hypot(dx, dz));
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
	const float s = std::sin(a), c = std::cos(a);
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

// placeStronghold: each team round its spot on a circle at `distance`,
// members `grouped` from it (RTS0 keeps them at least 20 apart).
std::vector<std::pair<float, float>> stronghold_locs(const std::vector<std::vector<int>> &ta, int n, float distance,
		float grouped, float start, float size) {
	std::vector<std::pair<float, float>> locs(n);
	const int T = std::max(1, (int)ta.size());
	for (int i = 0; i < (int)ta.size(); ++i) {
		const auto c = polar(start + (float)(i + 1) * kTwoPi / (float)T, distance);
		const int k = (int)ta[i].size();
		float gd = k > 4 ? std::max(0.08f * size, grouped) : grouped;
		gd = k == 1 ? 0.0f : std::max(gd, 10.0f / std::sin(3.14159265f / (float)k));
		for (int p = 0; p < k; ++p) {
			const auto o = polar(start + (float)(p + 1) * kTwoPi / (float)k, gd);
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
				float d = std::hypot(dx, dz);
				if (d >= min_d) continue;
				if (d < 1e-3f) { dx = std::cos((float)(i * 7 + j)); dz = std::sin((float)(i * 7 + j)); d = 1.0f; }
				const float push = (min_d - d) * 0.5f + 0.01f;
				locs[i].first -= dx / d * push; locs[i].second -= dz / d * push;
				locs[j].first += dx / d * push; locs[j].second += dz / d * push;
				moved = true;
			}
		for (auto &l : locs) {
			const float r = std::hypot(l.first, l.second);
			if (r > lim) { l.first *= lim / r; l.second *= lim / r; }
		}
		if (!moved) break;
	}
}

// playerPlacementByPattern(pattern: 0 line, 1 radial, 2 randomGroup,
// 3 stronghold), then spread to RTS0's base size.
std::vector<std::pair<float, float>> pattern_locs(const HeightmapGrid &hm, RmsRng &rng, int pattern, int n,
		const uint8_t *teams, float distance, float grouped, float start) {
	const float size = 2.0f * hm.map_half;
	const auto ta = teams_array(n, teams);
	std::vector<std::pair<float, float>> locs;
	if (pattern == 0) locs = line_locs(ta, n, distance, grouped, start, size);
	else if (pattern == 3) locs = stronghold_locs(ta, n, distance, grouped, start, size);
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
				const float d = std::hypot(wx - pts[k].first, wz - pts[k].second);
				if (d < best) { best = d; tx = wx; tz = wz; }
			}
		std::vector<uint8_t> m(hm.data.size(), 0);
		const float len = std::max(1e-3f, std::hypot(tx - pts[k].first, tz - pts[k].second));
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
		const float sx = locs[k].first, sz = locs[k].second, l = std::max(1e-3f, std::hypot(sx, sz));
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
		const uint8_t *teams) {
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
	const int pattern = rng.range_int(0, 3);
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
			const float a = std::atan2(-l.second, -l.first) + 1.5707963f + rng.range_float(-1.0f, 1.0f) * 1.5707963f;
			const auto s = polar(a, base_r * 0.7f), e = polar(a, base_r * rng.range_float(1.7f, 2.0f));
			const float sx = l.first + s.first, sz = l.second + s.second, ex = l.first + e.first, ez = l.second + e.second;
			if (std::fabs(ex) >= half || std::fabs(ez) >= half || !bluff12[vertex_at(hm, ex, ez)]) continue;
			const float hs = hm.sample_world(sx, sz), he = hm.sample_world(ex, ez);
			if ((he - hs) / kMetre / std::max(1.0f, std::hypot(ex - sx, ez - sz)) > 1.5f) continue;
			create_passage(hm, sx, sz, ex, ez, (float)scale_by_map_size(10, 20, half), (float)scale_by_map_size(10, 14, half), 3.0f, hs, he);
			break;
		}
	}
	spawns_at(hm, spawns, locs, num_players, land, 14.0f, 6.0f);
	for (auto &s : spawns) s.base_height = land;
	for (const auto &s : spawns) hm.level_area(s.x, s.z, 16.0f, 6.0f, land);
}

void RmsGenerator::synth_arctic_summer(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// arctic_summer.js: land at 2 m, players at f = 0.35; scaleByMapSize(1,
	// 240) hills (ChainPlacer(1, scaleByMapSize(4, 6), scaleByMapSize(16,
	// 40)), set to 18 m) kept 35 from bases and 50 from each other;
	// scaleByMapSize(1, 20) lakes at -5 m (ChainPlacer(1, scaleByMapSize(4,
	// 8), scaleByMapSize(40, 180))), bumps, and lots of game (wolves, deer,
	// hares, muskox: see distribute_wildlife). RTS0 keeps lakes 24 from the
	// bases (its base layout is larger) and drops a lake that would cut a
	// base off; whales and the starting wood treasures are left out.
	water_h = 1.0f;
	const float half = hm.map_half;
	const float land = metres(water_h, 2.0f);
	hm.init(hm.res, half, land);
	place_ring_spawns(hm, spawns, num_players, 0.7f * half, rng.range_float(0.0f, kTwoPi), kTwoPi, land);
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

void RmsGenerator::synth_schwarzwald(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	// schwarzwald.js: diamond-square terrain (setBaseTerrainDiamondSquare,
	// smoothness 0.5) grown from a 3x3 seed high at the rim and low in the
	// middle, between MIN_HEIGHT and MAX_HEIGHT * (size + 512) / 8192, then
	// SmoothingPainter(1, 0.8, 5) and rescaled; the water line lies a fifth
	// of the way up the height range, so a lake fills the middle. Players
	// at 5/8-3/4 of the map radius with jittered angles, each base smoothed
	// to its own height; paths link every base and the centre, and the wood
	// density field of the script (see distribute_land_extras) fills the
	// rest. 0 A.D. metres x 0.35 above the water line.
	water_h = 1.0f;
	const float half = hm.map_half, size = 2.0f * half;
	const float kMinH = -20.0f, kMaxH = 65535.0f / 92.0f - 20.0f;
	const float lo = kMinH * (size + 512.0f) / 8192.0f, hi = kMaxH * (size + 512.0f) / 8192.0f;
	const float sea = lo + 0.2f * (hi - lo);
	// setBaseTerrainDiamondSquare(lo, hi, [[hi, hi, hi], [hi, lo, hi], [hi, hi, hi]]).
	std::vector<std::vector<float>> g{{hi, hi, hi}, {hi, lo, hi}, {hi, hi, hi}};
	const float range = hi - lo;
	float offset = range / 2.0f;
	while ((int)g.size() < hm.res) {
		const int ow = (int)g.size(), nw = 2 * ow - 1;
		std::vector<std::vector<float>> ng(nw, std::vector<float>(nw, 0.0f));
		std::vector<std::vector<uint8_t>> set(nw, std::vector<uint8_t>(nw, 0));
		for (int x = 0; x < nw; ++x)
			for (int y = 0; y < nw; ++y) {
				if (x % 2 == 0 && y % 2 == 0) { ng[x][y] = g[x / 2][y / 2]; set[x][y] = 1; }
				else if (x % 2 == 1 && y % 2 == 1) {
					float v = (g[(x - 1) / 2][(y - 1) / 2] + g[(x + 1) / 2][(y - 1) / 2] + g[(x - 1) / 2][(y + 1) / 2] + g[(x + 1) / 2][(y + 1) / 2]) / 4.0f;
					v += (v - lo) / range * rng.range_float(-offset, offset);
					ng[x][y] = v;
					set[x][y] = 1;
				}
			}
		for (int x = 0; x < nw; ++x)
			for (int y = 0; y < nw; ++y) {
				if (set[x][y]) continue;
				float sum = 0.0f;
				int n = 0;
				if (x + 1 < nw) { sum += ng[x + 1][y]; ++n; }
				if (x > 0) { sum += ng[x - 1][y]; ++n; }
				if (y + 1 < nw) { sum += ng[x][y + 1]; ++n; }
				if (y > 0) { sum += ng[x][y - 1]; ++n; }
				float v = sum / (float)n;
				v += (v - lo) / range * rng.range_float(-offset, offset);
				ng[x][y] = v;
				set[x][y] = 1;
			}
		g.swap(ng);
		offset /= std::pow(2.0f, 0.5f);
	}
	const int shift = ((int)g.size() - hm.res) / 2;
	for (int z = 0; z < hm.res; ++z)
		for (int x = 0; x < hm.res; ++x) hm.set(x, z, g[x + shift][z + shift]);
	for (int i = 0; i < 5; ++i) shepard_smooth(hm, 0.8f);
	{
		const auto mm = std::minmax_element(hm.data.begin(), hm.data.end());
		const float a = *mm.first, b = std::max(*mm.second, a + 1e-3f);
		for (float &h : hm.data) h = water_h + (lo + (h - a) / (b - a) * range - sea) * kMetre;
	}
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
	water_h = 1.0f;
	const float half = hm.map_half, size = 2.0f * half;
	const float land = metres(water_h, 1.0f), sea = metres(water_h, -3.0f);
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
			const float d = std::max(1e-3f, std::hypot(wx - l.first, wz - l.second));
			if (d < 31.0f) { wx = l.first + (wx - l.first) * 31.0f / d; wz = l.second + (wz - l.second) * 31.0f / d; }
			bool ok = true;
			for (const auto &q : locs) ok &= std::hypot(wx - q.first, wz - q.second) >= 30.9f;
			if (ok) {
				std::vector<uint8_t> pond(N, 0);
				mask_disk(hm, pond, wx, wz, 4.5f);
				for (size_t j = 0; j < N; ++j) pond[j] &= !clump[j];
				paint_mask_elevation(hm, pond, sea, 3.0f);
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
				const float a = std::atan2(wz, wx);
				const float r = rt * (1.0f + 0.06f * std::sin(3.0f * a + wob) + 0.04f * std::sin(5.0f * a - wob));
				m[(size_t)z * (size_t)hm.res + (size_t)x] = wx * wx + wz * wz <= r * r;
			}
		paint_mask_elevation(hm, m, sea, R.forest + R.shore);
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
				break;
			}
	};
	bumps(scale_by_map_size(30, 70, half), (float)scale_by_map_size(20, 50, half), 4.0f, 3.0f);
	bumps(scale_by_map_size(15, 50, half), 120.0f, 18.0f, 30.0f);
	if (size > 150.0f && rng.range_int(0, 1)) {
		const float pw = (float)scale_by_map_size(7, 18, half), a = rng.range_float(0.0f, kTwoPi);
		const auto p0 = polar(a, rt + pw), p1 = polar(a + 3.14159265f, rt + pw);
		set_along_segment(hm, p0.first, p0.second, p1.first, p1.second, pw * 0.5f, 5.0f, metres(water_h, 4.0f));
	}
	spawns_at(hm, spawns, locs, num_players, land, 14.0f, 6.0f);
	for (const auto &s : spawns) lift_area(hm, s.x, s.z, 18.0f, 4.0f, s.base_height);
}

void RmsGenerator::distribute_land_extras(Simulation &sim, const HeightmapGrid &hm, float water_h, const std::vector<PlayerSpawn> &spawns,
		RmsRng &rng, const Config &config) {
	const float half = hm.map_half, size = 2.0f * half;
	auto dist_to_bases = [&](float x, float z) {
		float d = 1e30f;
		for (const auto &s : spawns) d = std::min(d, std::hypot(x - s.x, z - s.z));
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
				if (std::hypot(p.first - x, p.second - z) < d) return false;
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
					const auto p = polar(rng.range_float(0.0f, kTwoPi), std::sqrt(rng.next_float()) * gr);
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
					best = std::min(best, std::hypot(x - ax - dx * t, z - az - dz * t));
				}
			return best;
		};
		for (float z = -half + 1.0f; z < half; z += 2.0f)
			for (float x = -half + 1.0f; x < half; x += 2.0f) {
				const float r = std::hypot(x, z);
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
	} else if (config.map_type == MAP_OASIS_0AD) {
		// oasis.js: the central oasis's palm band (pOasisForestLight, a palm
		// on 2 tiles in 9), on ground units can walk to (some in the shallows,
		// as the script's band reaches into the water).
		const auto R = oasis_radii(half);
		const float rt = R.water + R.shore + R.forest, r0 = rt - R.forest;
		for (float z = -rt; z <= rt; z += 1.0f)
			for (float x = -rt; x <= rt; x += 1.0f) {
				const float r = std::hypot(x, z);
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
	const float c = std::cos(a), s = std::sin(a);
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
			gx.push_back(std::cos(a));
			gz.push_back(std::sin(a));
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
			const float d = std::hypot(dx, dz);
			float t = (std::atan2(dz, dx) / kTau + 0.5f) * (float)ctrl;
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
	const float len = std::max(1e-3f, std::hypot(bx - ax, bz - az));
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
	const float len = std::max(1e-3f, std::hypot(bx - ax, bz - az));
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
	const float len = std::max(1e-3f, std::hypot(bx - ax, bz - az));
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
			for (const auto &p : pts) v = std::min(v, std::hypot(wx - p.first, wz - p.second));
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
		const float base = std::atan2(sp.forward_dir_z, sp.forward_dir_x);
		int best = -1;
		float best_a = base;
		for (int k = 0; k < 16; ++k) {
			const float a = base + (k % 2 ? 1.0f : -1.0f) * (float)((k + 1) / 2) * kTau / 16.0f;
			const float fx = std::cos(a), fz = std::sin(a), rx = -fz, rz = fx;
			int dry = 0;
			for (const auto &p : pts) {
				const float x = sp.x + fx * p[0] + rx * p[1], z = sp.z + fz * p[0] + rz * p[1];
				dry += std::fabs(x) < lim && std::fabs(z) < lim && hm.sample_world(x, z) > dry_h;
			}
			if (dry > best) { best = dry; best_a = a; }
		}
		sp.forward_dir_x = std::cos(best_a);
		sp.forward_dir_z = std::sin(best_a);
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
		const float l = std::max(1e-3f, std::hypot(sx, sz));
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
	// up (the image's 2 m scale to under RTS0's shore on small maps) and
	// leaves out the gaia Kushite temple, pyramid, towers, soldiers and
	// treasures on the island (RTS0 has no gaia player; the trigger script
	// that garrisons them is out of scope). rmgen's z runs up the image, so
	// the script's angles are mirrored in z here.
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
		const float r = k == 1 ? 0.05f * size : std::max(0.05f * size, 9.0f / std::sin(3.14159265f / (float)k));
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
	auto random_centre = [&]() { return polar(rng.range_float(0.0f, kTau), std::sqrt(rng.next_float()) * half); };
	auto islands = [&](float min_r, float max_r, int circles, float max_dist, int count) {
		auto to_land = distance_to_mask(cl_land, hm.res, grid_spacing(hm));
		create_areas(hm, count, count, 0.07f, random_centre,
			[&](float cx, float cz) {
				auto m = chain_placer(hm, rng, cx, cz, min_r, max_r, circles);
				for (int z = 0; z < hm.res; ++z)
					for (int x = 0; x < hm.res; ++x) {
						float wx, wz;
						hm.grid_to_world(x, z, wx, wz);
						if (std::hypot(wx - cx, wz - cz) > max_dist) m[(size_t)z * (size_t)hm.res + (size_t)x] = 0;
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
		for (const auto &l : locs) d = std::min(d, std::hypot(wx - l.first, wz - l.second));
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
		const uint8_t *teams) {
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
	auto random_centre = [&]() { return polar(rng.range_float(0.0f, kTau), std::sqrt(rng.next_float()) * half); };
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
			const float w = scale_f((float)rng.range_int(16, 24), (float)rng.range_int(100, 140), half);
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
			auto m = clump_mask(hm, rng, e.first, e.second, 3.14159265f * std::pow(scale_f(5.0f, 10.0f, half), 2.0f), 0.95f, 0.6f);
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
	const int landscape = rng.range_int(0, 12);
	switch (landscape) {
		case 0: { // Archipelago
			circle(0.35f * size, rng.range_float(0.0f, kTau));
			mark_large();
			refresh_players();
			const float island = 3.14159265f * std::pow(scale_f(17.0f, 29.0f, half), 2.0f);
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
			paint_land(clump_mask(hm, rng, 0.0f, 0.0f, 3.14159265f * std::pow(0.38f * size, 2.0f), 0.9f, 0.09f), 4.0f);
			if (rng.range_int(0, 2) == 0) {
				// A peninsula: half the map not ringed by water.
				const float a = rng.range_float(0.0f, kTau);
				const auto p1 = polar(a, 0.25f * size), p2 = polar(a, 0.35f * size);
				paint_land(clump_mask(hm, rng, p1.first, p1.second, 3.14159265f * std::pow(0.38f * size, 2.0f), 0.9f, 0.09f), 4.0f);
				const auto pen = clump_mask(hm, rng, p2.first, p2.second, 3.14159265f * std::pow(0.33f * size, 2.0f), 0.9f, 0.01f);
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
				const auto m = clump_mask(hm, rng, 0.0f, 0.0f, 3.14159265f * std::pow(0.17f * size, 2.0f), 0.7f, 0.1f);
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
				auto e = clump_mask(hm, rng, r.first, r.second, 3.14159265f * std::pow(scale_f(4.0f, 22.0f, half), 2.0f), 0.95f, 0.6f);
				for (size_t k = 0; k < N; ++k) if (e[k] && !avoid_cc(k, 5.0f)) e[k] = 0;
				paint_mask_elevation(hm, e, wl, 0.0f);
			}
			paint_mask_elevation(hm, clump_mask(hm, rng, 0.0f, 0.0f, 3.14159265f * std::pow(0.04f * size, 2.0f), 0.7f, 0.1f), wl, 4.0f);
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
				auto m = clump_mask(hm, rng, std::round(c.first), std::round(c.second), 3.14159265f * std::pow(g[0] * size, 2.0f), 0.7f, 0.05f);
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
				auto e = clump_mask(hm, rng, r.first, r.second, 3.14159265f * std::pow(scale_f(4.0f, 22.0f, half), 2.0f), 0.95f, 0.6f);
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
				paint_mask_elevation(hm, clump_mask(hm, rng, 0.0f, 0.0f, 3.14159265f * std::pow(0.1f * size, 2.0f), 0.7f, 0.1f), metres(water_h, -4.0f), 3.0f);
			else
				paint_mask_elevation(hm, clump_mask(hm, rng, 0.0f, 0.0f, 3.14159265f * std::pow(0.05f * size, 2.0f), 0.7f, 0.1f), mountain, 4.0f);
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
				paint_land(clump_mask(hm, rng, v.first, v.second, 3.14159265f * std::pow(scale_f(18.0f, 32.0f, half), 2.0f), 0.65f, 0.1f), 2.0f);
				paint_mask_elevation(hm, path_mask(hm, rng, 0.0f, 0.0f, v.first, v.second, scale_f(14.0f, 24.0f, half), 0.4f,
				                                   3.0f * scale_f(1.0f, 3.0f, half), 0.2f, 0.05f), land, 4.0f);
			}
			paint_mask_elevation(hm, clump_mask(hm, rng, 0.0f, 0.0f, 3.14159265f * std::pow(0.18f * size, 2.0f), 0.7f, 0.1f), land, 4.0f);
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
		const int count = rng.range_int(0, scale_by_map_size(4, 8, half)) * rng.range_int(1, scale_by_map_size(4, 9, half));
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
					const float d = std::hypot(wx - cx[k], wz - cz[k]);
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

bool RmsGenerator::generate(Simulation &sim, const Config &config) {
	sim.reset();

	RmsRng rng(config.seed ? config.seed : 1337);
	HeightmapGrid hm;
	hm.init(config.heightmap_res, config.map_half, 0.0f);

	float water_h = -1.0f;
	std::vector<PlayerSpawn> spawns;

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
			synth_danubius(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_SAHARAN_OASES:
			synth_saharan_oases(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_ANATOLIAN_PLATEAU:
			synth_anatolian_plateau(hm, rng, water_h, spawns, config.num_players);
			break;
		case MAP_JEBEL_BARKAL:
			synth_jebel_barkal(hm, rng, water_h, spawns, config.num_players);
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
			synth_pyrenean_sierra(hm, rng, water_h, spawns, config.num_players);
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
			synth_frontier(hm, rng, water_h, spawns, config.num_players);
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
		case MAP_AEGEAN_SEA:
			synth_aegean_sea(hm, rng, water_h, spawns, config.num_players, config.teams);
			break;
		case MAP_ARCHIPELAGO_0AD:
			synth_archipelago_0ad(hm, rng, water_h, spawns, config.num_players, config.teams);
			break;
		case MAP_ISLANDS:
			synth_islands(hm, rng, water_h, spawns, config.num_players, config.teams);
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
			synth_ambush(hm, rng, water_h, spawns, config.num_players, config.teams);
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
		case MAP_UNKNOWN:
			synth_unknown(hm, rng, water_h, spawns, config.num_players, config.teams);
			break;
		case MAP_PELOPONNESE:
		case MAP_AEGEAN_ARCHIPELAGO:
		default:
			synth_peloponnese(hm, rng, water_h, spawns, config.num_players);
			break;
	}

	ease_routes_between_bases(hm, spawns, water_h);

	// Transfer heightmap to simulation
	sim.set_terrain_data(hm.data, hm.res, water_h);

	// Distribute balanced player bases and starter clusters
	distribute_player_bases(sim, hm, spawns, rng, config);

	// Distribute neutral resources, groves, and quarries
	distribute_neutral_resources(sim, hm, water_h, spawns, rng, config);

	// Distribute roaming wildlife packs
	distribute_wildlife(sim, hm, water_h, spawns, rng, config);

	// Distribute sacred relics
	distribute_relics(sim, hm, water_h, spawns, rng, config);

	// Fish in open water, for fishing boats
	distribute_fish(sim, hm, water_h, config);

	// Entities only the trigger maps have (Gaia towers, central mines)
	place_trigger_map_entities(sim, hm, water_h, spawns, rng, config);

	return true;
}

void RmsGenerator::distribute_fish(Simulation &sim, const HeightmapGrid &hm, float water_h, const Config &config) {
	// 0 A.D. rmgen (e.g. archipelago.js, mediterranean.js): groups of 2-3 fish,
	// scaleByMapSize(15, 50) of them, in water at least 2 tiles from land and 20
	// tiles from other food. One RTS0 world unit is one tile. A separate stream
	// keeps every other placement on the map unchanged.
	const float half = hm.map_half;
	const float size = 2.0f * half;
	const int groups = (int)std::lround(15.0f + 35.0f * std::clamp((size - 128.0f) / (512.0f - 128.0f), 0.0f, 1.0f));
	RmsRng rng(config.seed ^ 0x46495348u); // 'FISH'
	auto open_water = [&](float x, float z) {
		if (x < -half + 2.0f || x > half - 2.0f || z < -half + 2.0f || z > half - 2.0f) return false;
		for (int dz = -2; dz <= 2; ++dz)
			for (int dx = -2; dx <= 2; ++dx)
				if (hm.sample_world(x + (float)dx, z + (float)dz) >= water_h - 0.3f) return false;
		return true;
	};
	std::vector<std::pair<float, float>> placed;
	for (int attempt = 0; attempt < groups * 40 && (int)placed.size() < groups; ++attempt) {
		const float x = rng.range_float(-half, half), z = rng.range_float(-half, half);
		if (!open_water(x, z)) continue;
		bool crowded = false;
		for (const auto &p : placed)
			if ((p.first - x) * (p.first - x) + (p.second - z) * (p.second - z) < 20.0f * 20.0f) { crowded = true; break; }
		if (crowded) continue;
		placed.push_back({x, z});
		const int n = rng.range_int(2, 3);
		for (int k = 0; k < n; ++k) {
			const float fx = x + rng.range_float(-2.0f, 2.0f), fz = z + rng.range_float(-2.0f, 2.0f);
			if (open_water(fx, fz)) sim.spawn_resource_world(fx, fz, RES_FOOD, FISH_FOOD_SUPPLY, RES_SUB_FISH);
		}
	}
}

} // namespace rtso
