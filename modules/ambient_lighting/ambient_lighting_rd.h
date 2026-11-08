#pragma once
#include "ambient_cube.h"
#include "mobile_ssao.glsl.gen.h"

#include "servers/rendering/renderer_rd/storage_rd/render_data_rd.h"

class AmbientLightingRD {
	struct CubeBuffer {
		RID rid;
		uint64_t revision = 0;
	};
	HashMap<RID, CubeBuffer> buffers;
	RID empty_buffer;
	MobileSsaoShaderRD ssao_shader;
	RID ssao_version;
	RID ssao_pipelines[2];
	RID sampler;
	void _initialize_ssao();

public:
	RID get_cube_buffer(RID p_scenario);
	bool get_occlusion(RID p_environment, AmbientLightingState::Occlusion &r_settings);
	RID get_depth_framebuffer(const Ref<RenderSceneBuffersRD> &p_buffers);
	RID render_occlusion(const RenderDataRD *p_data, const AmbientLightingState::Occlusion &p_settings);
	~AmbientLightingRD();
};
