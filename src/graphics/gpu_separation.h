#ifndef RTSO_GPU_SEPARATION_H
#define RTSO_GPU_SEPARATION_H

#include <cstddef>
#include <cstdint>
#if __has_include(<godot_cpp/classes/rendering_device.hpp>)
#define RTSO_HAS_GODOT_RD 1
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/variant/rid.hpp>
#endif

namespace rtso {

class GpuSeparation {
public:
	using AvailFn = bool (*)();
	using DispatchFn = bool (*)(size_t count, const float *pos_xz, const float *radii, float *out_forces,
	                            float cell_size, float min_dist, float strength);

	static GpuSeparation &instance();
	static void register_live_hooks(AvailFn avail, DispatchFn dispatch);
	static bool live_available();
	static bool live_dispatch(size_t count, const float *pos_xz, const float *radii, float *out_forces,
	                          float cell_size, float min_dist, float strength);

#ifdef RTSO_HAS_GODOT_RD
	bool is_available() const;
	bool is_enabled() const { return enabled_ && is_available(); }
	void set_enabled(bool e) { enabled_ = e; }

	// Dispatches the compute shader if available.
	// pos_xz: interleaved float array [x0, z0, x1, z1, ...] of length count * 2.
	// out_forces: float array [fx0, fz0, fx1, fz1, ...] of length count * 2.
	bool compute_forces(size_t count, const float *pos_xz, float *out_forces,
	                    float cell_size, float min_dist, float strength);
	bool compute_forces_with_radii(size_t count, const float *pos_xz, const float *radii, float *out_forces,
	                               float cell_size, float min_dist, float strength);

	void cleanup();
	~GpuSeparation() = default;

private:
	GpuSeparation() = default;
	bool ensure_pipeline();

	bool enabled_ = true;
	bool attempted_init_ = false;
	godot::RID shader_;
	godot::RID pipeline_;
	godot::RID pos_buffer_;
	godot::RID force_buffer_;
	size_t buffer_capacity_ = 0;
#endif
};

} // namespace rtso

#endif // RTSO_GPU_SEPARATION_H
