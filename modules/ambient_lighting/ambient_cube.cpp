#include "ambient_cube.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/resources/3d/world_3d.h"
#include "servers/rendering/rendering_server.h"

namespace {
// The queued callable owns its immutable payload, never the publishing scene node.
class VolumeSnapshot : public RefCounted {
public:
	AmbientLightingState::Volume volume;
	bool remove = false;
};
void apply_volume_snapshot(const Ref<RefCounted> &p_payload) {
	const VolumeSnapshot *payload = static_cast<const VolumeSnapshot *>(p_payload.ptr());
	auto &state = AmbientLightingState::get();
	MutexLock lock(state.mutex);
	if (payload->remove) {
		if (state.volumes.erase(payload->volume.owner)) {
			state.revision++;
		}
	} else {
		state.volumes.insert(payload->volume.owner, payload->volume);
		state.revision++;
	}
}
void queue_volume_snapshot(const AmbientLightingState::Volume &p_volume, bool p_remove) {
	RenderingServer *server = RenderingServer::get_singleton();
	if (!server) {
		return;
	}
	Ref<VolumeSnapshot> payload;
	payload.instantiate();
	payload->volume = p_volume;
	payload->remove = p_remove;
	// Ordering with existing Environment/light commands is preserved at the render queue boundary.
	server->call_on_render_thread(callable_mp_static(&apply_volume_snapshot).bind(Ref<RefCounted>(payload)));
}
} // namespace

AmbientLightingState &AmbientLightingState::get() {
	static AmbientLightingState state;
	return state;
}
AmbientCubeField::AmbientCubeField() {
	coefficients.resize(48);
	coefficients.fill(Color(0, 0, 0, 1));
}
void AmbientCubeField::set_coefficients(const PackedColorArray &p_values) {
	ERR_FAIL_COND_MSG(p_values.size() != 48, "Ambient Cube requires eight corners of six linear RGB directions.");
	for (const Color &c : p_values) {
		ERR_FAIL_COND_MSG(!Math::is_finite(c.r) || !Math::is_finite(c.g) || !Math::is_finite(c.b) || c.r < 0 || c.g < 0 || c.b < 0, "Irradiance must be finite, nonnegative linear RGB.");
	}
	coefficients = p_values;
	emit_changed();
}
Color AmbientCubeField::sample(const Vector3 &p_position, const Vector3 &p_normal) const {
	Vector3 t = p_position.clamp(Vector3(), Vector3(1, 1, 1));
	Vector3 n = p_normal.normalized();
	Color result(0, 0, 0, 0);
	for (int d = 0; d < 6; d++) {
		int axis = d / 2;
		float weight = ((n[axis] >= 0) == (d % 2 == 0)) ? n[axis] * n[axis] : 0;
		Color a = coefficients[d].lerp(coefficients[6 + d], t.x).lerp(coefficients[12 + d].lerp(coefficients[18 + d], t.x), t.y);
		Color b = coefficients[24 + d].lerp(coefficients[30 + d], t.x).lerp(coefficients[36 + d].lerp(coefficients[42 + d], t.x), t.y);
		result += a.lerp(b, t.z) * weight;
	}
	result.a = 1;
	return result;
}
void AmbientCubeField::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_coefficients", "values"), &AmbientCubeField::set_coefficients);
	ClassDB::bind_method(D_METHOD("get_coefficients"), &AmbientCubeField::get_coefficients);
	ClassDB::bind_method(D_METHOD("sample", "normalized_position", "normal"), &AmbientCubeField::sample);
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_COLOR_ARRAY, "coefficients"), "set_coefficients", "get_coefficients");
}
AmbientCubeVolume3D::AmbientCubeVolume3D() {
	set_notify_transform(true);
}
AmbientCubeVolume3D::~AmbientCubeVolume3D() {
	_remove();
}
void AmbientCubeVolume3D::_remove() {
	AmbientLightingState::Volume volume;
	volume.owner = get_instance_id();
	queue_volume_snapshot(volume, true);
}
void AmbientCubeVolume3D::_publish() {
	if (!is_inside_tree() || !enabled || field.is_null() || !is_visible_in_tree()) {
		_remove();
		return;
	}
	Transform3D pose = get_global_transform();
	Basis b = pose.basis;
	bool invalid = !pose.is_finite() || b.determinant() <= 0 || !b.is_orthogonal() || !Math::is_equal_approx(b.get_column(0).length(), b.get_column(1).length()) || !Math::is_equal_approx(b.get_column(1).length(), b.get_column(2).length());
	if (invalid) {
		_remove();
		ERR_FAIL_MSG("Ambient Cube volumes require rotation and positive uniform scale; shear is unsupported.");
	}
	AmbientLightingState::Volume v;
	v.owner = get_instance_id();
	v.scenario = get_world_3d()->get_scenario();
	v.world_to_local = pose.affine_inverse();
	v.bounds = bounds;
	v.sampling_bounds = sampling_bounds.size == Vector3() ? bounds : sampling_bounds;
	v.energy = energy;
	v.edge_blend = edge_blend;
	v.priority = priority;
	v.receiver_mask = receiver_mask;
	v.coefficients = field->get_coefficients();
	if (blend_field.is_valid() && blend > 0) {
		PackedColorArray other = blend_field->get_coefficients();
		for (int i = 0; i < 48; i++) {
			v.coefficients.set(i, v.coefficients[i].lerp(other[i], blend));
		}
	}
	queue_volume_snapshot(v, false);
}
void AmbientCubeVolume3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_WORLD:
		case NOTIFICATION_TRANSFORM_CHANGED:
		case NOTIFICATION_VISIBILITY_CHANGED:
			_publish();
			break;
		case NOTIFICATION_EXIT_WORLD:
			_remove();
			break;
	}
}
void AmbientCubeVolume3D::set_field(const Ref<AmbientCubeField> &p_field) {
	Callable changed = callable_mp(this, &AmbientCubeVolume3D::_publish);
	if (field.is_valid()) {
		field->disconnect_changed(changed);
	}
	field = p_field;
	if (field.is_valid()) {
		field->connect_changed(changed, CONNECT_REFERENCE_COUNTED);
	}
	_publish();
}
void AmbientCubeVolume3D::set_blend_field(const Ref<AmbientCubeField> &p_field) {
	Callable changed = callable_mp(this, &AmbientCubeVolume3D::_publish);
	if (blend_field.is_valid()) {
		blend_field->disconnect_changed(changed);
	}
	blend_field = p_field;
	if (blend_field.is_valid()) {
		blend_field->connect_changed(changed, CONNECT_REFERENCE_COUNTED);
	}
	_publish();
}
void AmbientCubeVolume3D::set_bounds(const AABB &p_bounds) {
	ERR_FAIL_COND(!p_bounds.is_finite() || p_bounds.size.x <= 0 || p_bounds.size.y <= 0 || p_bounds.size.z <= 0);
	bounds = p_bounds;
	_publish();
}
void AmbientCubeVolume3D::set_sampling_bounds(const AABB &p_bounds) {
	ERR_FAIL_COND(!p_bounds.is_finite() || (p_bounds.size != Vector3() && (p_bounds.size.x <= 0 || p_bounds.size.y <= 0 || p_bounds.size.z <= 0)));
	sampling_bounds = p_bounds;
	_publish();
}
void AmbientCubeVolume3D::set_energy(float p_value) {
	ERR_FAIL_COND(!Math::is_finite(p_value) || p_value < 0);
	energy = p_value;
	_publish();
}
void AmbientCubeVolume3D::set_blend(float p_value) {
	ERR_FAIL_COND(!Math::is_finite(p_value) || p_value < 0 || p_value > 1);
	blend = p_value;
	_publish();
}
void AmbientCubeVolume3D::set_edge_blend(float p_value) {
	ERR_FAIL_COND(!Math::is_finite(p_value) || p_value < 0);
	edge_blend = p_value;
	_publish();
}
void AmbientCubeVolume3D::set_receiver_mask(uint32_t p_value) {
	receiver_mask = p_value;
	_publish();
}
void AmbientCubeVolume3D::set_priority(int p_value) {
	priority = p_value;
	_publish();
}
void AmbientCubeVolume3D::set_enabled(bool p_value) {
	enabled = p_value;
	_publish();
}
void AmbientCubeVolume3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_receiver_mask", "value"), &AmbientCubeVolume3D::set_receiver_mask);
	ClassDB::bind_method(D_METHOD("get_receiver_mask"), &AmbientCubeVolume3D::get_receiver_mask);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "receiver_mask", PROPERTY_HINT_LAYERS_3D_RENDER), "set_receiver_mask", "get_receiver_mask");
	ClassDB::bind_method(D_METHOD("set_sampling_bounds", "value"), &AmbientCubeVolume3D::set_sampling_bounds);
	ClassDB::bind_method(D_METHOD("get_sampling_bounds"), &AmbientCubeVolume3D::get_sampling_bounds);
	ADD_PROPERTY(PropertyInfo(Variant::AABB, "sampling_bounds"), "set_sampling_bounds", "get_sampling_bounds");
	ClassDB::bind_method(D_METHOD("set_field", "value"), &AmbientCubeVolume3D::set_field);
	ClassDB::bind_method(D_METHOD("get_field"), &AmbientCubeVolume3D::get_field);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "field", PROPERTY_HINT_RESOURCE_TYPE, "AmbientCubeField"), "set_field", "get_field");
	ClassDB::bind_method(D_METHOD("set_blend_field", "value"), &AmbientCubeVolume3D::set_blend_field);
	ClassDB::bind_method(D_METHOD("get_blend_field"), &AmbientCubeVolume3D::get_blend_field);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "blend_field", PROPERTY_HINT_RESOURCE_TYPE, "AmbientCubeField"), "set_blend_field", "get_blend_field");
	ClassDB::bind_method(D_METHOD("set_bounds", "value"), &AmbientCubeVolume3D::set_bounds);
	ClassDB::bind_method(D_METHOD("get_bounds"), &AmbientCubeVolume3D::get_bounds);
	ADD_PROPERTY(PropertyInfo(Variant::AABB, "bounds"), "set_bounds", "get_bounds");
	ClassDB::bind_method(D_METHOD("set_energy", "value"), &AmbientCubeVolume3D::set_energy);
	ClassDB::bind_method(D_METHOD("get_energy"), &AmbientCubeVolume3D::get_energy);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "energy"), "set_energy", "get_energy");
	ClassDB::bind_method(D_METHOD("set_blend", "value"), &AmbientCubeVolume3D::set_blend);
	ClassDB::bind_method(D_METHOD("get_blend"), &AmbientCubeVolume3D::get_blend);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "blend", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_blend", "get_blend");
	ClassDB::bind_method(D_METHOD("set_edge_blend", "value"), &AmbientCubeVolume3D::set_edge_blend);
	ClassDB::bind_method(D_METHOD("get_edge_blend"), &AmbientCubeVolume3D::get_edge_blend);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "edge_blend"), "set_edge_blend", "get_edge_blend");
	ClassDB::bind_method(D_METHOD("set_priority", "value"), &AmbientCubeVolume3D::set_priority);
	ClassDB::bind_method(D_METHOD("get_priority"), &AmbientCubeVolume3D::get_priority);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "priority"), "set_priority", "get_priority");
	ClassDB::bind_method(D_METHOD("set_enabled", "value"), &AmbientCubeVolume3D::set_enabled);
	ClassDB::bind_method(D_METHOD("is_enabled"), &AmbientCubeVolume3D::is_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "is_enabled");
}
