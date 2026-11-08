// Shared Mobile irradiance provider. No material-source rewriting.
#ifdef AMBIENT_LIGHTING_MODULE
layout(set = 1, binding = 27) uniform sampler2D mobile_ambient_occlusion;
struct AmbientCubeVolume {
	mat4 world_to_local;
	vec4 bounds_min_energy;
	vec4 bounds_size_edge;
	uvec4 receiver_mask;
	vec4 sampling_minimum;
	vec4 sampling_size;
	vec4 coefficients[48];
};
layout(set = 1, binding = 26, std430) restrict readonly buffer AmbientCubeBuffer {
	uvec4 header;
	AmbientCubeVolume volumes[];
}
ambient_cube;
vec3 ambient_cube_evaluate(vec3 fallback, vec3 world_position, vec3 world_normal, uint receiver_layers) {
	// Volumes arrive in descending priority, then stable owner order. No implicit cross-wall blend.
	for (uint i = 0u; i < ambient_cube.header.x; i++) {
		if ((ambient_cube.volumes[i].receiver_mask.x & receiver_layers) == 0u) {
			continue;
		}
		vec3 local_position = (ambient_cube.volumes[i].world_to_local * vec4(world_position, 1.0)).xyz;
		vec3 p = local_position - ambient_cube.volumes[i].bounds_min_energy.xyz;
		vec3 size = ambient_cube.volumes[i].bounds_size_edge.xyz;
		if (any(lessThan(p, vec3(-0.0001))) || any(greaterThan(p, size + vec3(0.0001)))) {
			continue;
		}
		vec3 t = clamp((local_position - ambient_cube.volumes[i].sampling_minimum.xyz) / ambient_cube.volumes[i].sampling_size.xyz, 0.0, 1.0);
		vec3 n = normalize(mat3(ambient_cube.volumes[i].world_to_local) * world_normal);
		vec3 w = n * n, e = vec3(0.0);
		for (int d = 0; d < 6; d++) {
			int a = d / 2;
			float weight = ((n[a] >= 0.0) == ((d % 2) == 0)) ? w[a] : 0.0;
			vec3 c0 = mix(ambient_cube.volumes[i].coefficients[d].rgb, ambient_cube.volumes[i].coefficients[6 + d].rgb, t.x);
			vec3 c1 = mix(ambient_cube.volumes[i].coefficients[12 + d].rgb, ambient_cube.volumes[i].coefficients[18 + d].rgb, t.x);
			vec3 c2 = mix(ambient_cube.volumes[i].coefficients[24 + d].rgb, ambient_cube.volumes[i].coefficients[30 + d].rgb, t.x);
			vec3 c3 = mix(ambient_cube.volumes[i].coefficients[36 + d].rgb, ambient_cube.volumes[i].coefficients[42 + d].rgb, t.x);
			e += mix(mix(c0, c1, t.y), mix(c2, c3, t.y), t.z) * weight;
		}
		float edge = ambient_cube.volumes[i].bounds_size_edge.w;
		vec3 distance_to_edge = min(p, size - p);
		float weight = edge > 0.0 ? clamp(min(distance_to_edge.x, min(distance_to_edge.y, distance_to_edge.z)) / edge, 0.0, 1.0) : 1.0;
		return edge > 0.0 ? mix(fallback, e * ambient_cube.volumes[i].bounds_min_energy.w, weight) : e * ambient_cube.volumes[i].bounds_min_energy.w;
	}
	return fallback;
}
#endif
