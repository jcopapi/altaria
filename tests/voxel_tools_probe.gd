extends SceneTree
func _initialize() -> void:
    for name in ["VoxelTerrain","VoxelLodTerrain","VoxelMesherTransvoxel","VoxelBuffer","VoxelToolTerrain"]:
        print("VOXEL_CLASS ",name," ",ClassDB.class_exists(name))
        if not ClassDB.class_exists(name):quit(1);return
    var mesher=ClassDB.instantiate("VoxelMesherTransvoxel")
    if not mesher.has_method("build_mesh_arrays"):
        push_error("Voxel Tools CPU array bridge missing")
        quit(1)
        return
    var buffer=ClassDB.instantiate("VoxelBuffer")
    print("SDF=",ClassDB.class_get_integer_constant("VoxelBuffer","CHANNEL_SDF")," F32=",ClassDB.class_get_integer_constant("VoxelBuffer","DEPTH_32_BIT"))
    print("VOXEL_TOOLS_PROBE_PASS")
    quit()
