#pragma once
#include "smooth_field.h"
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
namespace godot {
// Adapter to the installed Voxel Tools API; no custom polygonization.
class TerrainMesherBridge : public RefCounted {
    GDCLASS(TerrainMesherBridge,RefCounted)
protected:
    static void _bind_methods();
public:
    static altaria::Surface build_surface(const altaria::SmoothField &field,Vector3 origin,Vector3i cells,int resolution);
    Dictionary build(const Array &cells,Vector3 origin,Vector3i size,int resolution=4);
};
}
