#include "register_types.h"

#include <gdextension_interface.h>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include "presentation/sim_node.h"
#include "presentation/main_node.h"
#include "presentation/camera_rig.h"
#include "presentation/selection_rect.h"
#include "presentation/debug_overlay.h"
#include "presentation/minimap_view.h"
#include "presentation/world_view_node.h"
#include "presentation/asset_viewer.h"
#include "presentation/safe_select.h"
#include "presentation/hotkey_config.h"
#include "presentation/reference_data.h"
#include "presentation/user_config.h"
#include "graphics/assets/model_library.h"
#include "graphics/gpu_separation.h"
#include "core/job_system.h"
#include "core/diagnostics/logger.h"

#include <algorithm>
#include <cstdio>
#include <thread>

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
	GDREGISTER_CLASS(RtsoAssetViewer);
	GDREGISTER_CLASS(RtsoSafeSelect);
	GDREGISTER_CLASS(RtsoHotkeyConfig);
	GDREGISTER_CLASS(RtsoReference);
	GDREGISTER_CLASS(RtsoUserConfig);

	rtso::JobSystem::instance().start();
	char buf[160];
	std::snprintf(buf, sizeof(buf), "sim workers=%u (+main, width=%u) hw_concurrency=%u",
			rtso::JobSystem::instance().worker_count(),
			rtso::JobSystem::instance().parallel_width(),
			std::max(1u, std::thread::hardware_concurrency()));
	rtso::Logger::info("jobs", buf);
}

void uninitialize_rtso_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	rtso::GpuSeparation::instance().cleanup();
	rtso::JobSystem::instance().stop();
	rtso::ModelLibrary::clear();
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
