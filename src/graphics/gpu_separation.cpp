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
#include <cstring>
#include <vector>

namespace rtso {

namespace {
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
			if (los_in_buffer_.is_valid()) { rd->free_rid(los_in_buffer_); los_in_buffer_ = godot::RID(); }
			if (los_out_buffer_.is_valid()) { rd->free_rid(los_out_buffer_); los_out_buffer_ = godot::RID(); }
			if (los_pipeline_.is_valid()) { rd->free_rid(los_pipeline_); los_pipeline_ = godot::RID(); }
			if (los_shader_.is_valid()) { rd->free_rid(los_shader_); los_shader_ = godot::RID(); }
		}
	}
	los_in_buffer_ = godot::RID();
	los_out_buffer_ = godot::RID();
	los_pipeline_ = godot::RID();
	los_shader_ = godot::RID();
	los_res_capacity_ = 0;
	attempted_los_init_ = false;
}

struct GpuLosParams {
	uint32_t res;
	uint32_t fr;
};

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
