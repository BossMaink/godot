#[compute]
#version 450
#VERSION_DEFINES
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(set = 0, binding = 0) uniform sampler2D depth_texture;
layout(rgba16f, set = 0, binding = 1) uniform restrict writeonly image2D destination;
#ifdef FILTER_PASS
layout(set = 0, binding = 2) uniform sampler2D raw_texture;
#endif
layout(push_constant, std430) uniform Params {
	mat4 inverse_projection;
	vec2 projection_scale;
	ivec2 size;
	float radius;
	float intensity;
	float bias;
	float direct_affect;
	float power;
	float horizon;
	float sharpness;
	float detail;
}
params;
float depth_at(ivec2 p) {
	return texelFetch(depth_texture, clamp(p, ivec2(0), params.size - 1), 0).r;
}
vec3 position_at(ivec2 p) {
	p = clamp(p, ivec2(0), params.size - 1);
	vec4 v = params.inverse_projection * vec4((vec2(p) + 0.5) / vec2(params.size) * 2.0 - 1.0, depth_at(p), 1.0);
	return v.xyz / v.w;
}
void main() {
	ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
#ifdef FILTER_PASS
	if (any(greaterThanEqual(pixel, params.size))) {
		return;
	}
	if (depth_at(pixel) <= 0.0) {
		imageStore(destination, pixel, vec4(1.0, 1.0, 0.0, 1.0));
		return;
	}
	float z = -position_at(pixel).z;
	float sum = 0.0, weights = 0.0;
	ivec2 half_size = (params.size + 1) / 2;
	for (int y = -1; y <= 1; y++) {
		for (int x = -1; x <= 1; x++) {
			ivec2 q = clamp(pixel / 2 + ivec2(x, y), ivec2(0), half_size - 1);
			vec4 sample_ao = texelFetch(raw_texture, q, 0);
			float weight = exp(-abs(sample_ao.b - z) / max(0.005, params.radius * mix(0.3, 0.03, params.sharpness))) * exp(-float(x * x + y * y) * 0.6);
			sum += sample_ao.r * weight;
			weights += weight;
		}
	}
	float ao = weights > 0.0001 ? sum / weights : 1.0;
	imageStore(destination, pixel, vec4(ao, mix(1.0, ao, params.direct_affect), z, 1.0));
#else
	if (any(greaterThanEqual(pixel, (params.size + 1) / 2))) {
		return;
	}
	// Nearest covered sample in each 2x2 tile, using reversed Z.
	ivec2 p = pixel * 2;
	float depth = depth_at(p);
	for (int y = 0; y < 2; y++) {
		for (int x = 0; x < 2; x++) {
			ivec2 q = min(pixel * 2 + ivec2(x, y), params.size - 1);
			float d = depth_at(q);
			if (d > depth) {
				p = q;
				depth = d;
			}
		}
	}
	if (depth <= 0.0) {
		imageStore(destination, pixel, vec4(1.0, 1.0, 0.0, 1.0));
		return;
	}
	vec3 center = position_at(p);
	vec3 left = position_at(p - ivec2(1, 0)), right = position_at(p + ivec2(1, 0));
	vec3 up = position_at(p - ivec2(0, 1)), down = position_at(p + ivec2(0, 1));
	vec3 dx = abs(left.z - center.z) < abs(right.z - center.z) ? center - left : right - center;
	vec3 dy = abs(up.z - center.z) < abs(down.z - center.z) ? center - up : down - center;
	vec3 cross_normal = cross(dx, dy);
	if (dot(cross_normal, cross_normal) < 1e-16) {
		imageStore(destination, pixel, vec4(1.0, 1.0, -center.z, 1.0));
		return;
	}
	vec3 normal = normalize(cross_normal);
	if (abs(params.inverse_projection[2][3]) > 0.00001 ? dot(normal, center) > 0.0 : normal.z < 0.0) {
		normal = -normal;
	}
	// Perspective division is encoded in inverse_projection; orthographic w is one.
	// Derive projected radius by projecting a view-space displacement through inverse scale.
	vec2 projected_radius = params.projection_scale * params.radius * vec2(params.size) * 0.5;
	// For perspective projection inverse_projection[2][3] is nonzero; ortho is zero.
	if (abs(params.inverse_projection[2][3]) > 0.00001) {
		projected_radius /= max(-center.z, 0.001);
	}
	projected_radius = min(projected_radius, vec2(96.0));
	float occlusion = 0.0, count = 0.0;
	for (int d = 0; d < 8; d++) {
		float angle = (float(d) + 0.5) * 0.78539816339;
		vec2 direction = vec2(cos(angle), sin(angle));
		float horizon = 0.0;
		float detail_sum = 0.0;
		for (int step = 1; step <= 4; step++) {
			ivec2 q = p + ivec2(round(direction * projected_radius * (float(step) / 4.0)));
			if (any(lessThan(q, ivec2(0))) || any(greaterThanEqual(q, params.size)) || depth_at(q) <= 0.0) {
				continue;
			}
			vec3 delta = position_at(q) - center;
			float distance_squared = dot(delta, delta);
			if (distance_squared < 1e-8 || distance_squared > params.radius * params.radius) {
				continue;
			}
			float contribution = max(0.0, (dot(normal, delta) - params.bias) * inversesqrt(distance_squared) - params.horizon);
			detail_sum += contribution;
			horizon = max(horizon, contribution * (1.0 - distance_squared / (params.radius * params.radius)));
		}
		occlusion += mix(horizon, detail_sum * 0.25, (params.detail / (1.0 + params.detail)) * 0.5);
		count += 1.0;
	}
	float ao = pow(clamp(1.0 - params.intensity * occlusion / max(count, 1.0) * 2.0, 0.0, 1.0), params.power);
	imageStore(destination, pixel, vec4(ao, 1.0, -center.z, 1.0));
#endif
}
