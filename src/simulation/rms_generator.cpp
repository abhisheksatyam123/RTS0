#include "rms_generator.h"
#include "simulation.h"
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
	int num_neutral_mines = std::max(4, config.num_players * 2);
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
	int num_packs = 10;

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
	int num_relics = 6;

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

	return true;
}

} // namespace rtso
