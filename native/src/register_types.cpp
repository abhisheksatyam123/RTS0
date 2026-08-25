#include "register_types.h"

#include <gdextension_interface.h>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include "nodes/sim_node.h"
#include "nodes/main_node.h"
#include "nodes/camera_rig.h"
#include "nodes/selection_rect.h"
#include "nodes/debug_overlay.h"
#include "nodes/minimap_view.h"
#include "nodes/world_view_node.h"

using namespace godot;

void initialize_rtso_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	GDREGISTER_CLASS(RtsoSimNode);
	GDREGISTER_CLASS(RtsoMain);
	GDREGISTER_CLASS(RtsoCameraRig);
	GDREGISTER_CLASS(RtsoSelectionRect);
	GDREGISTER_CLASS(RtsoDebugOverlay);
	GDREGISTER_CLASS(RtsoMiniMapView);
	GDREGISTER_CLASS(RtsoWorldView);
}

void uninitialize_rtso_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
}

extern "C" {
GDExtensionBool GDE_EXPORT rtso_library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address,
		GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
	godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);
	init_obj.register_initializer(initialize_rtso_module);
	init_obj.register_terminator(uninitialize_rtso_module);
	init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
	return init_obj.init();
}
}
