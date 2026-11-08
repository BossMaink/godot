#include "register_types.h"

#include "ambient_cube.h"

#include "core/config/project_settings.h"
#include "core/object/class_db.h"
void initialize_ambient_lighting_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SERVERS) {
		GLOBAL_DEF("rendering/renderer/mobile_ssao", false);
	}
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(AmbientCubeField);
		GDREGISTER_CLASS(AmbientCubeVolume3D);
	}
}
void uninitialize_ambient_lighting_module(ModuleInitializationLevel p_level) {}
