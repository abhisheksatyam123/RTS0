#ifndef RTSO_GPU_SEPARATION_H
#define RTSO_GPU_SEPARATION_H

#include <cstddef>
#include <cstdint>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/variant/rid.hpp>

namespace rtso {

class GpuSeparation {
public:
	static GpuSeparation &instance();

	bool is_available() const;
	bool is_enabled() const { return enabled_ && is_available(); }
	void set_enabled(bool e) { enabled_ = e; }

	// Dispatches the compute shader if available.
	// pos_xz: interleaved float array [x0, z0, x1, z1, ...] of length count * 2.
	// out_forces: float array [fx0, fz0, fx1, fz1, ...] of length count * 2.
	bool compute_forces(size_t count, const float *pos_xz, float *out_forces,
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
};

} // namespace rtso

#endif // RTSO_GPU_SEPARATION_H
