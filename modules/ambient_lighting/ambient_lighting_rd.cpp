#include "ambient_lighting_rd.h"

#include "core/config/project_settings.h"
#include "core/templates/sort_array.h"
#include "servers/rendering/renderer_rd/framebuffer_cache_rd.h"
#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"
#include "servers/rendering/storage/environment_storage.h"

#include <cstring>

namespace {
constexpr int MAX_VOLUMES = 32;
struct GPUVolume {
	float world_to_local[16];
	float minimum_energy[4];
	float size_edge[4];
	uint32_t receiver_mask[4];
	float sampling_minimum[4];
	float sampling_size[4];
	float coefficients[48][4];
};
struct Header {
	uint32_t count = 0;
	uint32_t pad[3] = {};
};
static_assert(sizeof(GPUVolume) == 912);
struct VolumeOrder {
	bool operator()(const AmbientLightingState::Volume &a, const AmbientLightingState::Volume &b) const {
		return a.priority != b.priority ? a.priority > b.priority : uint64_t(a.owner) < uint64_t(b.owner);
	}
};
} //namespace
RID AmbientLightingRD::get_cube_buffer(RID p_scenario) {
	auto *rd = RD::get_singleton();
	if (empty_buffer.is_null()) {
		Vector<uint8_t> data;
		data.resize(sizeof(Header) + sizeof(GPUVolume));
		memset(data.ptrw(), 0, data.size());
		empty_buffer = rd->storage_buffer_create(data.size(), data);
	}
	Vector<AmbientLightingState::Volume> volumes;
	uint64_t revision;
	{
		auto &state = AmbientLightingState::get();
		MutexLock lock(state.mutex);
		revision = state.revision;
		// Collect retired worlds on the render thread. No GPU resource is freed by scene nodes.
		Vector<RID> retired;
		for (const auto &buffer : buffers) {
			bool found = false;
			for (const auto &v : state.volumes) {
				if (v.value.scenario == buffer.key) {
					found = true;
					break;
				}
			}
			if (!found) {
				retired.push_back(buffer.key);
			}
		}
		for (RID scenario : retired) {
			rd->free_rid(buffers[scenario].rid);
			buffers.erase(scenario);
		}
		if (auto *cached = buffers.getptr(p_scenario); cached && cached->revision == revision) {
			return cached->rid;
		}
		for (const auto &v : state.volumes) {
			if (v.value.scenario == p_scenario) {
				volumes.push_back(v.value);
			}
		}
	}
	if (volumes.is_empty()) {
		return empty_buffer;
	}
	ERR_FAIL_COND_V_MSG(volumes.size() > MAX_VOLUMES, empty_buffer, "Ambient Cube world exceeds 32 volumes; refusing incomplete lighting. Partition worlds or extend the reviewed limit.");
	volumes.sort_custom<VolumeOrder>();
	Vector<uint8_t> data;
	data.resize(sizeof(Header) + MAX_VOLUMES * sizeof(GPUVolume));
	memset(data.ptrw(), 0, data.size());
	Header header;
	header.count = volumes.size();
	memcpy(data.ptrw(), &header, sizeof(header));
	for (int i = 0; i < volumes.size(); i++) {
		const auto &v = volumes[i];
		GPUVolume gpu = {};
		RendererRD::MaterialStorage::store_transform(v.world_to_local, gpu.world_to_local);
		for (int a = 0; a < 3; a++) {
			gpu.minimum_energy[a] = v.bounds.position[a];
			gpu.size_edge[a] = v.bounds.size[a];
			gpu.sampling_minimum[a] = v.sampling_bounds.position[a];
			gpu.sampling_size[a] = v.sampling_bounds.size[a];
		}
		gpu.receiver_mask[0] = v.receiver_mask;
		gpu.minimum_energy[3] = v.energy;
		gpu.size_edge[3] = v.edge_blend;
		for (int c = 0; c < 48; c++) {
			gpu.coefficients[c][0] = v.coefficients[c].r;
			gpu.coefficients[c][1] = v.coefficients[c].g;
			gpu.coefficients[c][2] = v.coefficients[c].b;
		}
		memcpy(data.ptrw() + sizeof(Header) + i * sizeof(GPUVolume), &gpu, sizeof(gpu));
	}
	auto &buffer = buffers[p_scenario];
	if (buffer.rid.is_null()) {
		buffer.rid = rd->storage_buffer_create(data.size(), data);
	} else {
		rd->buffer_update(buffer.rid, 0, data.size(), data.ptr());
	}
	buffer.revision = revision;
	return buffer.rid;
}
bool AmbientLightingRD::get_occlusion(RID p_environment, AmbientLightingState::Occlusion &r_settings) {
	auto *environment = RendererEnvironmentStorage::get_singleton();
	if (!bool(GLOBAL_GET("rendering/renderer/mobile_ssao")) || p_environment.is_null() || !environment->is_environment(p_environment) || !environment->environment_get_ssao_enabled(p_environment)) {
		return false;
	}
	r_settings.radius = environment->environment_get_ssao_radius(p_environment);
	r_settings.intensity = environment->environment_get_ssao_intensity(p_environment);
	r_settings.power = environment->environment_get_ssao_power(p_environment);
	r_settings.horizon = environment->environment_get_ssao_horizon(p_environment);
	r_settings.sharpness = environment->environment_get_ssao_sharpness(p_environment);
	r_settings.detail = environment->environment_get_ssao_detail(p_environment);
	r_settings.direct_affect = environment->environment_get_ssao_direct_light_affect(p_environment);
	ERR_FAIL_COND_V_MSG(!Math::is_finite(r_settings.radius) || r_settings.radius <= 0 || !Math::is_finite(r_settings.intensity) || r_settings.intensity < 0 || !Math::is_finite(r_settings.power) || r_settings.power <= 0 || !Math::is_finite(r_settings.detail) || r_settings.detail < 0 || !Math::is_finite(r_settings.horizon) || r_settings.horizon < 0 || r_settings.horizon > 1 || !Math::is_finite(r_settings.sharpness) || r_settings.sharpness < 0 || r_settings.sharpness > 1 || !Math::is_finite(r_settings.direct_affect) || r_settings.direct_affect < 0 || r_settings.direct_affect > 1, false, "Mobile SSAO parameters are outside their valid finite ranges.");
	if (environment->environment_get_ssao_ao_channel_affect(p_environment) != 0.0f) {
		WARN_PRINT_ONCE("Mobile SSAO does not support AO channel affect; material AO and SSAO combine by minimum for indirect light.");
	}
	return true;
}
void AmbientLightingRD::_initialize_ssao() {
	if (ssao_version.is_valid()) {
		return;
	}
	Vector<String> versions;
	versions.push_back("\n");
	versions.push_back("\n#define FILTER_PASS\n");
	ssao_shader.initialize(versions);
	ssao_version = ssao_shader.version_create();
	for (int i = 0; i < 2; i++) {
		ssao_pipelines[i] = RD::get_singleton()->compute_pipeline_create(ssao_shader.version_get_shader(ssao_version, i));
	}
	RD::SamplerState state;
	state.min_filter = RD::SAMPLER_FILTER_NEAREST;
	state.mag_filter = RD::SAMPLER_FILTER_NEAREST;
	state.repeat_u = RD::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE;
	state.repeat_v = RD::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE;
	sampler = RD::get_singleton()->sampler_create(state);
}
RID AmbientLightingRD::get_depth_framebuffer(const Ref<RenderSceneBuffersRD> &p_buffers) {
	auto format = p_buffers->get_depth_format(false, false, false);
	p_buffers->create_texture(SNAME("ambient_lighting"), SNAME("depth"), format, RD::TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | RD::TEXTURE_USAGE_SAMPLING_BIT);
	Vector<RID> attachments;
	attachments.push_back(p_buffers->get_texture(SNAME("ambient_lighting"), SNAME("depth")));
	return FramebufferCacheRD::get_singleton()->get_cache(attachments[0]);
}
RID AmbientLightingRD::render_occlusion(const RenderDataRD *p_data, const AmbientLightingState::Occlusion &p_settings) {
	_initialize_ssao();
	auto *rd = RD::get_singleton();
	auto rb = p_data->render_buffers;
	Size2i full = rb->get_internal_size();
	Size2i half((full.x + 1) / 2, (full.y + 1) / 2);
	uint32_t usage = RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_SAMPLING_BIT;
	RID raw = rb->create_texture(SNAME("ambient_lighting"), SNAME("raw"), RD::DATA_FORMAT_R16G16B16A16_SFLOAT, usage, RD::TEXTURE_SAMPLES_1, half);
	RID filtered = rb->create_texture(SNAME("ambient_lighting"), SNAME("filtered"), RD::DATA_FORMAT_R16G16B16A16_SFLOAT, usage);
	RID depth = rb->get_texture(SNAME("ambient_lighting"), SNAME("depth"));
	struct Push {
		float inverse_projection[16];
		float projection_scale[2];
		int32_t size[2];
		float radius, intensity, bias, direct_affect;
		float power, horizon, sharpness, detail;
	} push = {};
	static_assert(sizeof(Push) == 112);
	Projection correction;
	correction.set_depth_correction(p_data->scene_data->flip_y);
	correction.add_jitter_offset(p_data->scene_data->taa_jitter);
	Projection projection = correction * p_data->scene_data->cam_projection;
	RendererRD::MaterialStorage::store_camera(projection.inverse(), push.inverse_projection);
	push.projection_scale[0] = Math::abs(projection[0][0]);
	push.projection_scale[1] = Math::abs(projection[1][1]);
	push.size[0] = full.x;
	push.size[1] = full.y;
	push.radius = p_settings.radius;
	push.intensity = p_settings.intensity;
	push.bias = p_settings.bias;
	push.direct_affect = p_settings.direct_affect;
	push.power = p_settings.power;
	push.horizon = p_settings.horizon;
	push.sharpness = p_settings.sharpness;
	push.detail = p_settings.detail;
	for (int mode = 0; mode < 2; mode++) {
		RID shader = ssao_shader.version_get_shader(ssao_version, mode);
		LocalVector<RD::Uniform> uniforms;
		RD::Uniform u;
		u.uniform_type = RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE;
		u.binding = 0;
		u.append_id(sampler);
		u.append_id(depth);
		uniforms.push_back(u);
		u = RD::Uniform();
		u.uniform_type = RD::UNIFORM_TYPE_IMAGE;
		u.binding = 1;
		u.append_id(mode ? filtered : raw);
		uniforms.push_back(u);
		if (mode) {
			u = RD::Uniform();
			u.uniform_type = RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE;
			u.binding = 2;
			u.append_id(sampler);
			u.append_id(raw);
			uniforms.push_back(u);
		}
		RID set = UniformSetCacheRD::get_singleton()->get_cache_vec(shader, 0, uniforms);
		auto list = rd->compute_list_begin();
		rd->compute_list_bind_compute_pipeline(list, ssao_pipelines[mode]);
		rd->compute_list_bind_uniform_set(list, set, 0);
		rd->compute_list_set_push_constant(list, &push, sizeof(push));
		Size2i target = mode ? full : half;
		rd->compute_list_dispatch_threads(list, target.x, target.y, 1);
		rd->compute_list_end();
	}
	return filtered;
}
AmbientLightingRD::~AmbientLightingRD() {
	auto *rd = RD::get_singleton();
	for (const auto &entry : buffers) {
		rd->free_rid(entry.value.rid);
	}
	if (empty_buffer.is_valid()) {
		rd->free_rid(empty_buffer);
	}
	if (sampler.is_valid()) {
		rd->free_rid(sampler);
	}
	if (ssao_version.is_valid()) {
		ssao_shader.version_free(ssao_version);
	}
}
