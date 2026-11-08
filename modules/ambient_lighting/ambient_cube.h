#pragma once
#include "core/io/resource.h"
#include "core/os/mutex.h"
#include "core/templates/hash_map.h"
#include "scene/3d/node_3d.h"

// Linear irradiance, corner index x + 2*y + 4*z; directions +X,-X,+Y,-Y,+Z,-Z.
class AmbientCubeField : public Resource {
	GDCLASS(AmbientCubeField, Resource);
	PackedColorArray coefficients;

protected:
	static void _bind_methods();

public:
	AmbientCubeField();
	void set_coefficients(const PackedColorArray &p_values);
	PackedColorArray get_coefficients() const { return coefficients; }
	Color sample(const Vector3 &p_position, const Vector3 &p_normal) const;
};

// CPU snapshots only. RenderingDevice resources belong to AmbientLightingRD.
struct AmbientLightingState {
	struct Volume {
		ObjectID owner;
		RID scenario;
		Transform3D world_to_local;
		AABB bounds;
		AABB sampling_bounds;
		PackedColorArray coefficients;
		float energy = 1.0f;
		float edge_blend = 0.0f;
		int priority = 0;
		uint32_t receiver_mask = 0xFFFFFFFF;
	};
	struct Occlusion {
		float radius = 0.22f;
		float intensity = 1.0f;
		float bias = 0.015f;
		float direct_affect = 0.0f;
		float power = 1.5f;
		float horizon = 0.06f;
		float sharpness = 0.98f;
		float detail = 0.5f;
	};
	Mutex mutex;
	uint64_t revision = 1;
	HashMap<ObjectID, Volume> volumes;
	static AmbientLightingState &get();
};

class AmbientCubeVolume3D : public Node3D {
	GDCLASS(AmbientCubeVolume3D, Node3D);
	Ref<AmbientCubeField> field;
	Ref<AmbientCubeField> blend_field;
	AABB bounds = AABB(Vector3(), Vector3(1, 1, 1));
	AABB sampling_bounds;
	float energy = 1.0f;
	float blend = 0.0f;
	float edge_blend = 0.0f;
	int priority = 0;
	uint32_t receiver_mask = 0xFFFFFFFF;
	bool enabled = true;
	void _publish();
	void _remove();

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	AmbientCubeVolume3D();
	~AmbientCubeVolume3D();
	void set_field(const Ref<AmbientCubeField> &p_field);
	Ref<AmbientCubeField> get_field() const { return field; }
	void set_blend_field(const Ref<AmbientCubeField> &p_field);
	Ref<AmbientCubeField> get_blend_field() const { return blend_field; }
	void set_bounds(const AABB &p_bounds);
	AABB get_bounds() const { return bounds; }
	void set_sampling_bounds(const AABB &p_bounds);
	AABB get_sampling_bounds() const { return sampling_bounds; }
	void set_energy(float p_value);
	float get_energy() const { return energy; }
	void set_blend(float p_value);
	float get_blend() const { return blend; }
	void set_edge_blend(float p_value);
	float get_edge_blend() const { return edge_blend; }
	void set_receiver_mask(uint32_t p_value);
	uint32_t get_receiver_mask() const { return receiver_mask; }
	void set_priority(int p_value);
	int get_priority() const { return priority; }
	void set_enabled(bool p_value);
	bool is_enabled() const { return enabled; }
};
