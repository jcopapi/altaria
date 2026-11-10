"""Expose the upstream CPU output without a RenderingServer readback.

The normal script build_mesh shortcut creates ArrayMesh and retrieving its arrays
on a worker can wait on the main render queue. Joining that worker during a world
switch then deadlocks. This binding only forwards VoxelMesher::build's existing
surface arrays; it changes no Transvoxel geometry, allocator or meshing algorithm.
"""
from pathlib import Path
import sys
source = Path(sys.argv[1])
p = source / 'meshers/voxel_mesher.h'
s = p.read_text()
if '_b_build_mesh_arrays' not in s:
    s = s.replace('protected:\n\tRef<Mesh> _b_build_mesh', 'protected:\n\tArray _b_build_mesh_arrays(Ref<godot::VoxelBuffer> voxels);\n\tRef<Mesh> _b_build_mesh')
    p.write_text(s)
p = source / 'meshers/voxel_mesher.cpp'
s = p.read_text()
if 'Array VoxelMesher::_b_build_mesh_arrays' not in s:
    marker = 'void VoxelMesher::_bind_methods() {'
    method = '''// Altaria: CPU-only script bridge to the existing native mesher output.
Array VoxelMesher::_b_build_mesh_arrays(Ref<godot::VoxelBuffer> voxels) {
    ERR_FAIL_COND_V(voxels.is_null(), Array());
    Output output;
    Input input{ voxels->get_buffer(), nullptr, Vector3i(), 0, false, false, false };
    build(output, input);
    Array arrays;
    for (const Output::Surface &surface : output.surfaces) {
        if (!surface.arrays.is_empty() && is_surface_triangulated(surface.arrays))
            arrays.push_back(surface.arrays);
    }
    return arrays;
}

'''
    s = s.replace(marker, method + marker + '\n\tClassDB::bind_method(D_METHOD("build_mesh_arrays", "voxel_buffer"), &VoxelMesher::_b_build_mesh_arrays);')
    p.write_text(s)
