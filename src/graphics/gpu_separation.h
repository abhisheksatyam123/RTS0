#ifndef RTSO_GPU_SEPARATION_H
#define RTSO_GPU_SEPARATION_H

#include <cstddef>
#include <cstdint>
#include <vector>
#if __has_include(<godot_cpp/classes/rendering_device.hpp>) && __has_include(<gdextension_interface.h>)
#define RTSO_HAS_GODOT_RD 1
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/variant/rid.hpp>
#endif

namespace rtso {

class GpuSeparation {
public:
	// GPU offload for the fog-of-war LOS bitmap (LosShading). The simulation never uses the GPU: every
	// lockstep peer, replay and save must reach the same state, and a GPU's rounding differs from another's
	// and from the CPU's.
	static GpuSeparation &instance();

#ifdef RTSO_HAS_GODOT_RD
	bool is_available() const;
	bool is_enabled() const { return enabled_ && is_available(); }
	void set_enabled(bool e) { enabled_ = e; }

	// Dispatches shaders/compute/los_fow.glsl (2x bilinear upsample + 7-tap binomial blur).
	bool compute_los_bitmap(const uint8_t *fow, int res, std::vector<uint8_t> &out);

	void cleanup();
	~GpuSeparation() = default;

private:
	GpuSeparation() = default;
	bool ensure_los_pipeline();

	bool enabled_ = false; // Opt-in only: lockstep simulation must stay bit-identical across CPU/GPU/headless peers
	bool attempted_los_init_ = false;
	godot::RID los_shader_;
	godot::RID los_pipeline_;
	godot::RID los_in_buffer_;
	godot::RID los_out_buffer_;
	size_t los_res_capacity_ = 0;
#endif
};

} // namespace rtso

#endif // RTSO_GPU_SEPARATION_H
