#include "rms_generator.h"
#include "simulation.h"
#include "heightmap_images.h"
#include <cmath>
#include <algorithm>
#include <vector>
#include <cstring>

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

				float bed_h = is_ford ? (water_height - 0.20f) : (water_height - bed_depth);
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

void RmsGenerator::synth_continental(HeightmapGrid &hm, RmsRng &rng, float &water_h, std::vector<PlayerSpawn> &spawns, int num_players) {
	water_h = 0.5f;
	hm.init(hm.res, hm.map_half, 2.5f);

	// 1. Base rolling terrain with multi-octave fBm
	hm.apply_fbm(rng, 4, 0.030f, 0.52f, 2.1f, 5.0f);
	hm.diamond_square(rng, 2.5f, 0.65f);

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

		hm.raise_plateau(sx, sz, 14.0f, ramp_angle, 0.45f, 7.5f, 3.5f);
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

	// 4. Perimeter gentle falloff
	hm.apply_radial_falloff(0.85f, 0.98f, 1.0f);
	hm.blur(2, 0.42f);

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
	for (const auto &f : fords) place_ford(hm, f.first, f.second, 5.0f + half * 0.02f, water_h);
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
		place_ford(hm, fx, fz, 5.0f + half * 0.025f, water_h);
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
	for (int i = 0; i < num_players; ++i) {
		const float a = start + 6.2831853f * (float)i / (float)std::max(1, num_players);
		// The ramp points back towards the centre, give or take pi / 8.
		const float ramp = a + 3.14159265f * (1.0f + rng.range_float(-1.0f, 1.0f) / 8.0f);
		hm.raise_plateau(std::cos(a) * ring_r, std::sin(a) * ring_r, 19.0f, ramp, 0.3f, 7.0f, 6.0f);
	}
	place_ring_spawns(hm, spawns, num_players, ring_r, start, 6.2831853f, water_h + 1.5f);
	scatter_ponds(hm, rng, spawns, scale_by_map_size(1, 4, half) * std::max(1, num_players) / 2,
	              half * 0.05f + 3.0f, 30.0f, 12.0f, half * 0.95f, 3.0f, water_h);
	hm.blur(1, 0.3f);
	for (const auto &sp : spawns) lift_area(hm, sp.x, sp.z, 15.0f, 3.0f, sp.base_height);
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
	for (const auto &f : fords) place_ford(hm, f.first, f.second, 5.0f + half * 0.02f, water_h);
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
		place_ford(hm, ux * s * half, uz * s * half, river_w + 4.0f, water_h);
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
		place_ford(hm, ox + dx * f * half, oz + dz * f * half, (float)scale_by_map_size(10, 15, half), water_h);
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

// 0 A.D. metres above the water line -> RTS0 height (x 0.35 above water_h).
constexpr float kMetre = 0.35f;

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

void RmsGenerator::distribute_player_bases(Simulation &sim, const HeightmapGrid &hm, const std::vector<PlayerSpawn> &spawns, RmsRng &rng, const Config &config) {
	(void)config;
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

		// 2. Starting Barracks & Houses
		sim.spawn_building_world(px + fx * 8.5f, pz + fz * 8.5f, BLD_BARRACKS, team, 1.0f);
		sim.spawn_building_world(px - fx * 6.5f + rx * 6.0f, pz - fz * 6.5f + rz * 6.0f, BLD_HOUSE, team, 1.0f);
		sim.spawn_building_world(px - fx * 6.5f - rx * 6.0f, pz - fz * 6.5f - rz * 6.0f, BLD_HOUSE, team, 1.0f);

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

		// 4. Primary Food: Starter Berry Bushes (5 bushes, 450 food each)
		float berry_cx = px + fx * 5.5f + rx * 6.5f;
		float berry_cz = pz + fz * 5.5f + rz * 6.5f;
		for (int b = 0; b < 5; ++b) {
			float ba = (6.2831853f * b) / 5.0f;
			float bx = berry_cx + std::cos(ba) * 1.5f;
			float bz = berry_cz + std::sin(ba) * 1.5f;
			sim.spawn_resource_world(bx, bz, RES_FOOD, 450);
		}

		// 5. Primary Forest: Dense Woodline (10 trees, 800 wood each)
		float forest_cx = px - fx * 10.5f + rx * 3.5f;
		float forest_cz = pz - fz * 10.5f + rz * 3.5f;
		for (int t = 0; t < 10; ++t) {
			float ta = -1.2f + (2.4f * t) / 9.0f;
			float tx = forest_cx + std::cos(ta) * 4.0f + rng.range_float(-0.4f, 0.4f);
			float tz = forest_cz + std::sin(ta) * 4.0f + rng.range_float(-0.4f, 0.4f);
			sim.spawn_resource_world(tx, tz, RES_WOOD, 800);
		}

		// 6. Primary Gold Mine (3 Gold Rocks, 1200 gold each)
		float gold_cx = px + fx * 3.5f - rx * 11.5f;
		float gold_cz = pz + fz * 3.5f - rz * 11.5f;
		for (int g = 0; g < 3; ++g) {
			float ga = (6.2831853f * g) / 3.0f;
			float gx = gold_cx + std::cos(ga) * 1.8f;
			float gz = gold_cz + std::sin(ga) * 1.8f;
			sim.spawn_resource_world(gx, gz, RES_GOLD, 1200);
		}

		// 7. Primary Stone Mine (2 Stone Rocks, 1200 stone each)
		float stone_cx = px - fx * 8.5f - rx * 9.5f;
		float stone_cz = pz - fz * 8.5f - rz * 9.5f;
		for (int s = 0; s < 2; ++s) {
			float sx = stone_cx + (s == 0 ? -1.2f : 1.2f);
			float sz = stone_cz + (s == 0 ? -0.8f : 0.8f);
			sim.spawn_resource_world(sx, sz, RES_STONE, 1200);
		}

		// 8. Secondary Forward Gold Mine (Expansion)
		float sec_gold_cx = px + fx * 22.0f + rx * 6.0f;
		float sec_gold_cz = pz + fz * 22.0f + rz * 6.0f;
		for (int g = 0; g < 3; ++g) {
			float ga = (6.2831853f * g) / 3.0f;
			sim.spawn_resource_world(sec_gold_cx + std::cos(ga) * 1.8f, sec_gold_cz + std::sin(ga) * 1.8f, RES_GOLD, 1500);
		}

		// 9. Secondary Stone Mine (Flank)
		float sec_stone_cx = px + fx * 18.0f - rx * 16.0f;
		float sec_stone_cz = pz + fz * 18.0f - rz * 16.0f;
		for (int s = 0; s < 2; ++s) {
			sim.spawn_resource_world(sec_stone_cx + (s == 0 ? -1.3f : 1.3f), sec_stone_cz, RES_STONE, 1500);
		}

		// 10. Starter Wildlife: Passive Deer Herd (4 deer)
		float deer_cx = px - fx * 14.0f - rx * 5.0f;
		float deer_cz = pz - fz * 14.0f - rz * 5.0f;
		for (int d = 0; d < 4; ++d) {
			float da = (6.2831853f * d) / 4.0f;
			sim.spawn_fauna(deer_cx + std::cos(da) * 2.5f, deer_cz + std::sin(da) * 2.5f, FAUNA_DEER);
		}

		// 11. Starter Dangerous Prey: 2 Wild Boars
		float boar_cx = px + fx * 19.0f - rx * 5.0f;
		float boar_cz = pz + fz * 19.0f - rz * 5.0f;
		sim.spawn_fauna(boar_cx - 1.5f, boar_cz, FAUNA_BOAR);
		sim.spawn_fauna(boar_cx + 1.5f, boar_cz, FAUNA_BOAR);
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

	// Map-specific landmark resource clusters
	if (config.map_type == MAP_DANUBIUS) {
		// Central Island Fortress Resources
		sim.spawn_resource_world(-2.5f, 0.0f, RES_GOLD, 1600);
		sim.spawn_resource_world( 2.5f, 0.0f, RES_GOLD, 1600);
		sim.spawn_resource_world(0.0f, -2.5f, RES_STONE, 1600);
		sim.spawn_resource_world(0.0f,  2.5f, RES_STONE, 1600);
		// Oak grove on fortress island
		for (int t = 0; t < 8; ++t) {
			float a = (6.2831853f * t) / 8.0f;
			sim.spawn_resource_world(std::cos(a) * 5.0f, std::sin(a) * 5.0f, RES_WOOD, 850);
		}
		// Sandbar resources
		sim.spawn_resource_world(-half * 0.30f, 0.0f, RES_STONE, 1200);
		sim.spawn_resource_world( half * 0.30f, 0.0f, RES_STONE, 1200);
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
				if (hm.sample_world(tx, tz) > water_h + 0.15f) {
					sim.spawn_resource_world(tx, tz, RES_WOOD, 800);
				}
			}
			// Oasis date/grape berry bushes
			for (int b = 0; b < 3; ++b) {
				float ba = (6.2831853f * b) / 3.0f + 0.5f;
				float bx = ox + std::cos(ba) * (o_rad + 2.0f);
				float bz = oz + std::sin(ba) * (o_rad + 2.0f);
				if (hm.sample_world(bx, bz) > water_h + 0.15f) {
					sim.spawn_resource_world(bx, bz, RES_FOOD, 450);
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
			sim.spawn_resource_world(mx - 1.2f, mz, mtype, 1600);
			sim.spawn_resource_world(mx + 1.2f, mz, mtype, 1600);
		}
	} else if (config.map_type == MAP_JEBEL_BARKAL) {
		// Summit of Jebel Barkal high-ground treasure & quarries
		float mesa_z = half * 0.06f;
		sim.spawn_resource_world(-2.5f, mesa_z, RES_GOLD, 1800);
		sim.spawn_resource_world( 2.5f, mesa_z, RES_GOLD, 1800);
		sim.spawn_resource_world( 0.0f, mesa_z + 2.5f, RES_STONE, 1800);
		sim.spawn_resource_world( 0.0f, mesa_z - 2.5f, RES_STONE, 1800);
		for (int t = 0; t < 6; ++t) {
			float a = (6.2831853f * t) / 6.0f;
			sim.spawn_resource_world(std::cos(a) * 4.5f, mesa_z + std::sin(a) * 4.5f, RES_WOOD, 800);
		}
		// Surrounding savanna quarries along base
		sim.spawn_resource_world(-half * 0.48f,  half * 0.42f, RES_STONE, 1500);
		sim.spawn_resource_world( half * 0.48f,  half * 0.42f, RES_STONE, 1500);
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
			if (hm.sample_world(tx, tz) > water_h + 0.2f) {
				sim.spawn_resource_world(tx, tz, RES_WOOD, 800);
			}
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

		uint8_t res_type = (m % 2 == 0) ? RES_GOLD : RES_STONE;
		int rocks = rng.range_int(2, 4);
		for (int r = 0; r < rocks; ++r) {
			float a = (6.2831853f * r) / (float)rocks;
			sim.spawn_resource_world(mx + std::cos(a) * 1.6f, mz + std::sin(a) * 1.6f, res_type, 1400);
		}
	}
}

void RmsGenerator::distribute_wildlife(Simulation &sim, const HeightmapGrid &hm, float water_h, const std::vector<PlayerSpawn> &spawns, RmsRng &rng, const Config &config) {
	(void)config;
	float half = hm.map_half;
	int num_packs = scale_by_map_size(10, 40, half);

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

		uint8_t ftype = (uint8_t)(p % FAUNA_COUNT);
		int pack_size = (ftype == FAUNA_DEER || ftype == FAUNA_SHEEP) ? rng.range_int(3, 5) : rng.range_int(2, 3);

		for (int i = 0; i < pack_size; ++i) {
			float a = (6.2831853f * i) / (float)pack_size;
			float r = rng.range_float(1.5f, 3.5f);
			sim.spawn_fauna(px + std::cos(a) * r, pz + std::sin(a) * r, ftype);
		}
	}
}

void RmsGenerator::distribute_relics(Simulation &sim, const HeightmapGrid &hm, float water_h, const std::vector<PlayerSpawn> &spawns, RmsRng &rng, const Config &config) {
	float half = hm.map_half;
	int num_relics = scale_by_map_size(6, 12, half);

	// Center relic if land is above water
	float center_h = hm.sample_world(0.0f, 0.0f);
	if (config.map_type == MAP_JEBEL_BARKAL) {
		// Place sacred landmark relic on summit of Jebel Barkal
		sim.spawn_relic(0.0f, half * 0.06f, RES_GOLD, 500);
	} else if (config.map_type == MAP_DANUBIUS) {
		// Place sacred relic on Island Fortress
		sim.spawn_relic(0.0f, 0.0f, RES_GOLD, 450);
	} else if (center_h > water_h + 0.2f) {
		bool near_player = false;
		for (const auto &sp : spawns) {
			float d = std::sqrt(sp.x * sp.x + sp.z * sp.z);
			if (d < 18.0f) { near_player = true; break; }
		}
		if (!near_player) {
			sim.spawn_relic(0.0f, 0.0f, RES_GOLD, 350);
		}
	}

	for (int i = 0; i < num_relics; ++i) {
		float rx = rng.range_float(-half * 0.65f, half * 0.65f);
		float rz = rng.range_float(-half * 0.65f, half * 0.65f);

		bool near_player = false;
		for (const auto &sp : spawns) {
			float d = std::sqrt((rx - sp.x) * (rx - sp.x) + (rz - sp.z) * (rz - sp.z));
			if (d < 20.0f) { near_player = true; break; }
		}
		if (near_player) continue;

		float h = hm.sample_world(rx, rz);
		if (h < water_h + 0.3f) continue;

		uint8_t rtype = (uint8_t)(i % RES_COUNT);
		int amount = rng.range_int(250, 400);

		sim.spawn_relic(rx, rz, rtype, amount);
	}
}

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
		case MAP_PELOPONNESE:
		case MAP_AEGEAN_ARCHIPELAGO:
		default:
			synth_peloponnese(hm, rng, water_h, spawns, config.num_players);
			break;
	}

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
