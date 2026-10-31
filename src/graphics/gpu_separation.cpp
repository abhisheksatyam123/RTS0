#include "graphics/gpu_separation.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/rd_shader_file.hpp>
#include <godot_cpp/classes/rd_shader_source.hpp>
#include <godot_cpp/classes/rd_shader_spirv.hpp>
#include <godot_cpp/classes/rd_uniform.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <cstring>

namespace rtso {

GpuSeparation &GpuSeparation::instance() {
	static GpuSeparation s_instance;
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

	godot::Ref<godot::RDShaderSPIRV> spirv;
	godot::ResourceLoader *rl = godot::ResourceLoader::get_singleton();
	if (rl) {
		godot::Ref<godot::RDShaderFile> shader_file = rl->load("res://shaders/compute/separation.glsl");
		if (shader_file.is_valid()) {
			spirv = shader_file->get_spirv();
		}
	}

	if (spirv.is_null() || !spirv->get_stage_compile_error(godot::RenderingDevice::SHADER_STAGE_COMPUTE).is_empty()) {
		godot::Ref<godot::FileAccess> f = godot::FileAccess::open("res://shaders/compute/separation.glsl", godot::FileAccess::READ);
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

	if (spirv.is_null()) return false;

	shader_ = rd->shader_create_from_spirv(spirv);
	if (!shader_.is_valid()) return false;

	pipeline_ = rd->compute_pipeline_create(shader_);
	return pipeline_.is_valid();
}

void GpuSeparation::cleanup() {
	godot::RenderingServer *rs = godot::RenderingServer::get_singleton();
	if (rs) {
		godot::RenderingDevice *rd = rs->get_rendering_device();
		if (rd) {
			if (pos_buffer_.is_valid()) { rd->free_rid(pos_buffer_); pos_buffer_ = godot::RID(); }
			if (force_buffer_.is_valid()) { rd->free_rid(force_buffer_); force_buffer_ = godot::RID(); }
			if (pipeline_.is_valid()) { rd->free_rid(pipeline_); pipeline_ = godot::RID(); }
			if (shader_.is_valid()) { rd->free_rid(shader_); shader_ = godot::RID(); }
		}
	}
	pos_buffer_ = godot::RID();
	force_buffer_ = godot::RID();
	pipeline_ = godot::RID();
	shader_ = godot::RID();
	buffer_capacity_ = 0;
	attempted_init_ = false;
}

struct GpuSeparationParams {
	uint32_t count;
	float cellSize;
	float minDist;
	float strength;
};

bool GpuSeparation::compute_forces(size_t count, const float *pos_xz, float *out_forces,
                                   float cell_size, float min_dist, float strength) {
	if (!is_enabled() || count == 0 || !pos_xz || !out_forces) return false;
	if (!ensure_pipeline()) return false;

	godot::RenderingDevice *rd = godot::RenderingServer::get_singleton()->get_rendering_device();
	if (!rd) return false;

	const size_t bytes_needed = count * 2 * sizeof(float);
	if (buffer_capacity_ < count) {
		if (pos_buffer_.is_valid()) rd->free_rid(pos_buffer_);
		if (force_buffer_.is_valid()) rd->free_rid(force_buffer_);
		size_t alloc_count = count < 1024 ? 1024 : (count * 3 / 2);
		size_t alloc_bytes = alloc_count * 2 * sizeof(float);
		pos_buffer_ = rd->storage_buffer_create((uint32_t)alloc_bytes);
		force_buffer_ = rd->storage_buffer_create((uint32_t)alloc_bytes);
		buffer_capacity_ = alloc_count;
	}

	if (!pos_buffer_.is_valid() || !force_buffer_.is_valid()) return false;

	godot::PackedByteArray pos_bytes;
	pos_bytes.resize((int64_t)bytes_needed);
	std::memcpy(pos_bytes.ptrw(), pos_xz, bytes_needed);
	rd->buffer_update(pos_buffer_, 0, (uint32_t)bytes_needed, pos_bytes);

	godot::Ref<godot::RDUniform> u_pos;
	u_pos.instantiate();
	u_pos->set_uniform_type(godot::RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER);
	u_pos->set_binding(0);
	u_pos->add_id(pos_buffer_);

	godot::Ref<godot::RDUniform> u_force;
	u_force.instantiate();
	u_force->set_uniform_type(godot::RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER);
	u_force->set_binding(1);
	u_force->add_id(force_buffer_);

	godot::TypedArray<godot::RDUniform> uniforms;
	uniforms.push_back(u_pos);
	uniforms.push_back(u_force);

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
	if ((size_t)out_bytes.size() < bytes_needed) {
		rd->free_rid(uniform_set);
		return false;
	}

	std::memcpy(out_forces, out_bytes.ptr(), bytes_needed);
	rd->free_rid(uniform_set);
	return true;
}

} // namespace rtso
