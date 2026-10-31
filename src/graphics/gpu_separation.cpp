#include "graphics/gpu_separation.h"
#include "presentation/fog_mirage.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/rd_shader_file.hpp>
#include <godot_cpp/classes/rd_shader_source.hpp>
#include <godot_cpp/classes/rd_shader_spirv.hpp>
#include <godot_cpp/classes/rd_uniform.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <cmath>
#include <cstring>
#include <vector>

namespace rtso {

namespace {
constexpr size_t kSpatialBuckets = 4096; // 64x64 periodic grid

bool default_avail_hook() {
	return GpuSeparation::instance().is_enabled();
}
bool default_dispatch_hook(size_t count, const float *pos_xz, const float *radii, float *out_forces,
                           float cell_size, float min_dist, float strength) {
	return GpuSeparation::instance().compute_forces_with_radii(count, pos_xz, radii, out_forces, cell_size, min_dist, strength);
}
bool default_los_hook(const uint8_t *fow, int res, std::vector<uint8_t> &out) {
	return GpuSeparation::instance().compute_los_bitmap(fow, res, out);
}

godot::Ref<godot::RDShaderSPIRV> load_or_compile_compute(godot::RenderingDevice *rd, const char *res_path) {
	godot::Ref<godot::RDShaderSPIRV> spirv;
	godot::ResourceLoader *rl = godot::ResourceLoader::get_singleton();
	if (rl) {
		godot::Ref<godot::RDShaderFile> shader_file = rl->load(res_path);
		if (shader_file.is_valid()) {
			spirv = shader_file->get_spirv();
		}
	}
	if (spirv.is_null() || !spirv->get_stage_compile_error(godot::RenderingDevice::SHADER_STAGE_COMPUTE).is_empty()) {
		godot::Ref<godot::FileAccess> f = godot::FileAccess::open(res_path, godot::FileAccess::READ);
		if (f.is_valid()) {
			godot::String code = f->get_as_text();
			code = code.replace("#[compute]", "").strip_edges();
			godot::Ref<godot::RDShaderSource> ss;
			ss.instantiate();
			ss->set_stage_source(godot::RenderingDevice::SHADER_STAGE_COMPUTE, code);
			ss->set_language(godot::RenderingDevice::SHADER_LANGUAGE_GLSL);
			spirv = rd->shader_compile_spirv_from_source(ss);
		}
	}
	return spirv;
}
} // namespace

GpuSeparation &GpuSeparation::instance() {
	static GpuSeparation s_instance;
	static bool registered = []() {
		GpuSeparation::register_live_hooks(&default_avail_hook, &default_dispatch_hook);
		LosShading::register_gpu_hook(&default_los_hook);
		return true;
	}();
	(void)registered;
	return s_instance;
}

bool GpuSeparation::is_available() const {
	godot::RenderingServer *rs = godot::RenderingServer::get_singleton();
	if (!rs) return false;
	return rs->get_rendering_device() != nullptr;
}

bool GpuSeparation::ensure_pipeline() {
	if (pipeline_.is_valid()) return true;
	if (attempted_init_) return false;
	attempted_init_ = true;

	godot::RenderingServer *rs = godot::RenderingServer::get_singleton();
	if (!rs) return false;
	godot::RenderingDevice *rd = rs->get_rendering_device();
	if (!rd) return false;

	godot::Ref<godot::RDShaderSPIRV> spirv = load_or_compile_compute(rd, "res://shaders/compute/separation.glsl");
	if (spirv.is_null()) return false;

	shader_ = rd->shader_create_from_spirv(spirv);
	if (!shader_.is_valid()) return false;

	pipeline_ = rd->compute_pipeline_create(shader_);
	return pipeline_.is_valid();
}

bool GpuSeparation::ensure_los_pipeline() {
	if (los_pipeline_.is_valid()) return true;
	if (attempted_los_init_) return false;
	attempted_los_init_ = true;

	godot::RenderingServer *rs = godot::RenderingServer::get_singleton();
	if (!rs) return false;
	godot::RenderingDevice *rd = rs->get_rendering_device();
	if (!rd) return false;

	godot::Ref<godot::RDShaderSPIRV> spirv = load_or_compile_compute(rd, "res://shaders/compute/los_fow.glsl");
	if (spirv.is_null()) return false;

	los_shader_ = rd->shader_create_from_spirv(spirv);
	if (!los_shader_.is_valid()) return false;

	los_pipeline_ = rd->compute_pipeline_create(los_shader_);
	return los_pipeline_.is_valid();
}

void GpuSeparation::cleanup() {
	godot::RenderingServer *rs = godot::RenderingServer::get_singleton();
	if (rs) {
		godot::RenderingDevice *rd = rs->get_rendering_device();
		if (rd) {
			if (pos_buffer_.is_valid()) { rd->free_rid(pos_buffer_); pos_buffer_ = godot::RID(); }
			if (force_buffer_.is_valid()) { rd->free_rid(force_buffer_); force_buffer_ = godot::RID(); }
			if (radius_buffer_.is_valid()) { rd->free_rid(radius_buffer_); radius_buffer_ = godot::RID(); }
			if (cell_head_buffer_.is_valid()) { rd->free_rid(cell_head_buffer_); cell_head_buffer_ = godot::RID(); }
			if (cell_next_buffer_.is_valid()) { rd->free_rid(cell_next_buffer_); cell_next_buffer_ = godot::RID(); }
			if (pipeline_.is_valid()) { rd->free_rid(pipeline_); pipeline_ = godot::RID(); }
			if (shader_.is_valid()) { rd->free_rid(shader_); shader_ = godot::RID(); }

			if (los_in_buffer_.is_valid()) { rd->free_rid(los_in_buffer_); los_in_buffer_ = godot::RID(); }
			if (los_out_buffer_.is_valid()) { rd->free_rid(los_out_buffer_); los_out_buffer_ = godot::RID(); }
			if (los_pipeline_.is_valid()) { rd->free_rid(los_pipeline_); los_pipeline_ = godot::RID(); }
			if (los_shader_.is_valid()) { rd->free_rid(los_shader_); los_shader_ = godot::RID(); }
		}
	}
	pos_buffer_ = godot::RID();
	force_buffer_ = godot::RID();
	radius_buffer_ = godot::RID();
	cell_head_buffer_ = godot::RID();
	cell_next_buffer_ = godot::RID();
	pipeline_ = godot::RID();
	shader_ = godot::RID();
	buffer_capacity_ = 0;
	attempted_init_ = false;

	los_in_buffer_ = godot::RID();
	los_out_buffer_ = godot::RID();
	los_pipeline_ = godot::RID();
	los_shader_ = godot::RID();
	los_res_capacity_ = 0;
	attempted_los_init_ = false;
}

struct GpuSeparationParams {
	uint32_t count;
	float cellSize;
	float minDist;
	float strength;
};

struct GpuLosParams {
	uint32_t res;
	uint32_t fr;
};

bool GpuSeparation::compute_forces(size_t count, const float *pos_xz, float *out_forces,
                                   float cell_size, float min_dist, float strength) {
	return compute_forces_with_radii(count, pos_xz, nullptr, out_forces, cell_size, min_dist, strength);
}

bool GpuSeparation::compute_forces_with_radii(size_t count, const float *pos_xz, const float *radii, float *out_forces,
                                              float cell_size, float min_dist, float strength) {
	if (!is_enabled() || count == 0 || !pos_xz || !out_forces) return false;
	if (!ensure_pipeline()) return false;

	godot::RenderingDevice *rd = godot::RenderingServer::get_singleton()->get_rendering_device();
	if (!rd) return false;

	const size_t pos_bytes_needed = count * 2 * sizeof(float);
	const size_t rad_bytes_needed = count * sizeof(float);
	const size_t head_bytes_needed = kSpatialBuckets * sizeof(int32_t);
	const size_t next_bytes_needed = count * sizeof(int32_t);

	if (!cell_head_buffer_.is_valid()) {
		cell_head_buffer_ = rd->storage_buffer_create((uint32_t)head_bytes_needed);
	}
	if (buffer_capacity_ < count) {
		if (pos_buffer_.is_valid()) rd->free_rid(pos_buffer_);
		if (force_buffer_.is_valid()) rd->free_rid(force_buffer_);
		if (radius_buffer_.is_valid()) rd->free_rid(radius_buffer_);
		if (cell_next_buffer_.is_valid()) rd->free_rid(cell_next_buffer_);
		size_t alloc_count = count < 1024 ? 1024 : (count * 3 / 2);
		pos_buffer_ = rd->storage_buffer_create((uint32_t)(alloc_count * 2 * sizeof(float)));
		force_buffer_ = rd->storage_buffer_create((uint32_t)(alloc_count * 2 * sizeof(float)));
		radius_buffer_ = rd->storage_buffer_create((uint32_t)(alloc_count * sizeof(float)));
		cell_next_buffer_ = rd->storage_buffer_create((uint32_t)(alloc_count * sizeof(int32_t)));
		buffer_capacity_ = alloc_count;
	}

	if (!pos_buffer_.is_valid() || !force_buffer_.is_valid() || !radius_buffer_.is_valid() ||
	    !cell_head_buffer_.is_valid() || !cell_next_buffer_.is_valid()) {
		return false;
	}

	// Build O(N) 64x64 spatial hash linked list on CPU
	thread_local std::vector<int32_t> cell_head;
	thread_local std::vector<int32_t> cell_next;
	thread_local std::vector<float> fallback_radii;
	cell_head.assign(kSpatialBuckets, -1);
	cell_next.resize(count);
	const float inv_cell = (cell_size > 0.0001f) ? (1.0f / cell_size) : 0.25f;
	for (size_t i = 0; i < count; ++i) {
		const int cx = (int)std::floor(pos_xz[i * 2 + 0] * inv_cell);
		const int cz = (int)std::floor(pos_xz[i * 2 + 1] * inv_cell);
		const size_t bucket = (size_t)(((cx & 63) | ((cz & 63) << 6)));
		cell_next[i] = cell_head[bucket];
		cell_head[bucket] = (int32_t)i;
	}
	const float *rad_ptr = radii;
	if (!rad_ptr) {
		fallback_radii.assign(count, 0.5f * min_dist);
		rad_ptr = fallback_radii.data();
	}

	godot::PackedByteArray pos_bytes, rad_bytes, head_bytes, next_bytes;
	pos_bytes.resize((int64_t)pos_bytes_needed);
	std::memcpy(pos_bytes.ptrw(), pos_xz, pos_bytes_needed);
	rd->buffer_update(pos_buffer_, 0, (uint32_t)pos_bytes_needed, pos_bytes);

	rad_bytes.resize((int64_t)rad_bytes_needed);
	std::memcpy(rad_bytes.ptrw(), rad_ptr, rad_bytes_needed);
	rd->buffer_update(radius_buffer_, 0, (uint32_t)rad_bytes_needed, rad_bytes);

	head_bytes.resize((int64_t)head_bytes_needed);
	std::memcpy(head_bytes.ptrw(), cell_head.data(), head_bytes_needed);
	rd->buffer_update(cell_head_buffer_, 0, (uint32_t)head_bytes_needed, head_bytes);

	next_bytes.resize((int64_t)next_bytes_needed);
	std::memcpy(next_bytes.ptrw(), cell_next.data(), next_bytes_needed);
	rd->buffer_update(cell_next_buffer_, 0, (uint32_t)next_bytes_needed, next_bytes);

	auto make_uniform = [](int binding, const godot::RID &buf) {
		godot::Ref<godot::RDUniform> u;
		u.instantiate();
		u->set_uniform_type(godot::RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER);
		u->set_binding(binding);
		u->add_id(buf);
		return u;
	};

	godot::TypedArray<godot::RDUniform> uniforms;
	uniforms.push_back(make_uniform(0, pos_buffer_));
	uniforms.push_back(make_uniform(1, force_buffer_));
	uniforms.push_back(make_uniform(2, radius_buffer_));
	uniforms.push_back(make_uniform(3, cell_head_buffer_));
	uniforms.push_back(make_uniform(4, cell_next_buffer_));

	godot::RID uniform_set = rd->uniform_set_create(uniforms, shader_, 0);
	if (!uniform_set.is_valid()) return false;

	GpuSeparationParams params;
	params.count = (uint32_t)count;
	params.cellSize = cell_size;
	params.minDist = min_dist;
	params.strength = strength;

	godot::PackedByteArray push_bytes;
	push_bytes.resize(sizeof(GpuSeparationParams));
	std::memcpy(push_bytes.ptrw(), &params, sizeof(GpuSeparationParams));

	int64_t cl = rd->compute_list_begin();
	rd->compute_list_bind_compute_pipeline(cl, pipeline_);
	rd->compute_list_bind_uniform_set(cl, uniform_set, 0);
	rd->compute_list_set_push_constant(cl, push_bytes, sizeof(GpuSeparationParams));
	const uint32_t groups = ((uint32_t)count + 63u) / 64u;
	rd->compute_list_dispatch(cl, groups, 1, 1);
	rd->compute_list_end();

	rd->submit();
	rd->sync();

	godot::PackedByteArray out_bytes = rd->buffer_get_data(force_buffer_);
	if ((size_t)out_bytes.size() < pos_bytes_needed) {
		rd->free_rid(uniform_set);
		return false;
	}

	std::memcpy(out_forces, out_bytes.ptr(), pos_bytes_needed);
	rd->free_rid(uniform_set);
	return true;
}

bool GpuSeparation::compute_los_bitmap(const uint8_t *fow, int res, std::vector<uint8_t> &out) {
	if (!is_enabled() || !fow || res <= 0 || (res & 1) != 0) return false;
	if (!ensure_los_pipeline()) return false;

	godot::RenderingDevice *rd = godot::RenderingServer::get_singleton()->get_rendering_device();
	if (!rd) return false;

	const size_t in_bytes = ((size_t)res * (size_t)res + 3u) & ~size_t(3u);
	const int fr = res * 2;
	const size_t out_bytes = ((size_t)fr * (size_t)fr + 3u) & ~size_t(3u);
	if (los_res_capacity_ < (size_t)res) {
		if (los_in_buffer_.is_valid()) rd->free_rid(los_in_buffer_);
		if (los_out_buffer_.is_valid()) rd->free_rid(los_out_buffer_);
		los_in_buffer_ = rd->storage_buffer_create((uint32_t)in_bytes);
		los_out_buffer_ = rd->storage_buffer_create((uint32_t)out_bytes);
		los_res_capacity_ = (size_t)res;
	}
	if (!los_in_buffer_.is_valid() || !los_out_buffer_.is_valid()) return false;

	godot::PackedByteArray in_arr, zero_arr;
	in_arr.resize((int64_t)in_bytes);
	std::memset(in_arr.ptrw(), 0, in_bytes);
	std::memcpy(in_arr.ptrw(), fow, (size_t)res * (size_t)res);
	rd->buffer_update(los_in_buffer_, 0, (uint32_t)in_bytes, in_arr);

	zero_arr.resize((int64_t)out_bytes);
	std::memset(zero_arr.ptrw(), 0, out_bytes);
	rd->buffer_update(los_out_buffer_, 0, (uint32_t)out_bytes, zero_arr);

	godot::Ref<godot::RDUniform> u_in, u_out;
	u_in.instantiate();
	u_in->set_uniform_type(godot::RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER);
	u_in->set_binding(0);
	u_in->add_id(los_in_buffer_);

	u_out.instantiate();
	u_out->set_uniform_type(godot::RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER);
	u_out->set_binding(1);
	u_out->add_id(los_out_buffer_);

	godot::TypedArray<godot::RDUniform> uniforms;
	uniforms.push_back(u_in);
	uniforms.push_back(u_out);

	godot::RID uniform_set = rd->uniform_set_create(uniforms, los_shader_, 0);
	if (!uniform_set.is_valid()) return false;

	GpuLosParams params{(uint32_t)res, (uint32_t)fr};
	godot::PackedByteArray push_bytes;
	push_bytes.resize(sizeof(GpuLosParams));
	std::memcpy(push_bytes.ptrw(), &params, sizeof(GpuLosParams));

	int64_t cl = rd->compute_list_begin();
	rd->compute_list_bind_compute_pipeline(cl, los_pipeline_);
	rd->compute_list_bind_uniform_set(cl, uniform_set, 0);
	rd->compute_list_set_push_constant(cl, push_bytes, sizeof(GpuLosParams));
	const uint32_t gx = ((uint32_t)fr + 7u) / 8u;
	const uint32_t gy = ((uint32_t)fr + 7u) / 8u;
	rd->compute_list_dispatch(cl, gx, gy, 1);
	rd->compute_list_end();

	rd->submit();
	rd->sync();

	godot::PackedByteArray res_bytes = rd->buffer_get_data(los_out_buffer_);
	rd->free_rid(uniform_set);
	const size_t wanted = (size_t)fr * (size_t)fr;
	if ((size_t)res_bytes.size() < wanted) return false;
	out.resize(wanted);
	std::memcpy(out.data(), res_bytes.ptr(), wanted);
	return true;
}

} // namespace rtso
