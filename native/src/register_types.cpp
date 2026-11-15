#include "altaria_game.h"
#include "arc_hotbar.h"
#include "crab_console_bridge.h"
#include "diorama_minimap.h"
#include "stamina_ring.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>

namespace {

void initialize_altaria(godot::ModuleInitializationLevel level) {
    if (level == godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
        GDREGISTER_CLASS(godot::GameLog);
        GDREGISTER_CLASS(godot::CrabConsole);
        GDREGISTER_CLASS(godot::CrabConsoleDebugDrawer);
        GDREGISTER_CLASS(godot::AltariaGame);
        GDREGISTER_CLASS(godot::ArcHotbar);
        GDREGISTER_CLASS(godot::CrosshairDot);
        GDREGISTER_CLASS(godot::StaminaRing);
        GDREGISTER_CLASS(godot::DioramaMinimap);
        GDREGISTER_CLASS(godot::VoxelWorld);
        GDREGISTER_CLASS(godot::FlyingPlayer);
        GDREGISTER_CLASS(godot::WorldAtmosphere);
        GDREGISTER_CLASS(godot::PhysicsBlocks);
        GDREGISTER_CLASS(godot::BuiltWorld);
    }
}

void uninitialize_altaria(godot::ModuleInitializationLevel) {}

} // namespace

extern "C" {

GDExtensionBool GDE_EXPORT altaria_library_init(GDExtensionInterfaceGetProcAddress get_proc_address,
                                                GDExtensionClassLibraryPtr library,
                                                GDExtensionInitialization *initialization) {
    godot::GDExtensionBinding::InitObject init(get_proc_address, library, initialization);
    init.register_initializer(initialize_altaria);
    init.register_terminator(uninitialize_altaria);
    init.set_minimum_library_initialization_level(godot::MODULE_INITIALIZATION_LEVEL_SCENE);
    return init.init();
}
}
