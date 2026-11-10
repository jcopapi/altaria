class_name TerrainTransvoxelPatch
extends RefCounted
## Direct Voxel Tools adapter, also used for geometry previews and seam tests.
const Field=preload("res://game/world_generation/terrain/sdf_field.gd")
static func build(field: RefCounted, origin: Vector3i, cells: Vector3i) -> ArrayMesh:
    assert(ClassDB.class_exists("VoxelMesherTransvoxel"),"Voxel Tools GDExtension is required")
    var mesher=ClassDB.instantiate("VoxelMesherTransvoxel")
    mesher.transitions_enabled=false
    var before: int=mesher.get_minimum_padding()
    var after: int=mesher.get_maximum_padding()
    var buffer=ClassDB.instantiate("VoxelBuffer")
    var size:=cells*Field.RESOLUTION+Vector3i.ONE*(before+after)
    buffer.create(size.x,size.y,size.z)
    # CHANNEL_SDF=1, DEPTH_32_BIT=2 in the installed VoxelBuffer API.
    buffer.set_channel_depth(1,2)
    for z in range(size.z):
        for x in range(size.x):
            for y in range(size.y):
                var p:=Vector3(origin)+Vector3(Vector3i(x,y,z)-Vector3i.ONE*before)/Field.RESOLUTION
                buffer.set_voxel_f(field.distance_at(p)*Field.RESOLUTION,x,y,z,1)
    var source: ArrayMesh=mesher.build_mesh(buffer,[])
    if source==null:return ArrayMesh.new()
    var mesh:=ArrayMesh.new()
    for surface in range(source.get_surface_count()):
        var arrays:=source.surface_get_arrays(surface)
        var points: PackedVector3Array=arrays[Mesh.ARRAY_VERTEX]
        for i in range(points.size()):points[i]=points[i]/Field.RESOLUTION+Vector3(origin)
        arrays[Mesh.ARRAY_VERTEX]=points
        mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES,arrays,[],{},source.surface_get_format(surface))
    return mesh
