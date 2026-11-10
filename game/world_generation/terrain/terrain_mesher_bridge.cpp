#include "terrain_mesher_bridge.h"
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <cstring>
#include <cmath>
namespace godot {
void TerrainMesherBridge::_bind_methods() {
    ClassDB::bind_method(D_METHOD("build","cells","origin","size","resolution"),&TerrainMesherBridge::build,DEFVAL(4));
}
altaria::Surface TerrainMesherBridge::build_surface(const altaria::SmoothField &field,Vector3 origin,Vector3i cells,int resolution) {
    altaria::Surface out;
    if(!ClassDB::class_exists("VoxelMesherTransvoxel")) {
        UtilityFunctions::push_error("Terrain requires the Voxel Tools GDExtension.");return out;
    }
    Ref<Resource> mesher=Object::cast_to<Resource>(ClassDB::instantiate("VoxelMesherTransvoxel"));
    Ref<RefCounted> buffer=Object::cast_to<RefCounted>(ClassDB::instantiate("VoxelBuffer"));
    mesher->set("transitions_enabled",false);
    int before=mesher->call("get_minimum_padding"),after=mesher->call("get_maximum_padding");
    Vector3i size=cells*resolution+Vector3i(before+after,before+after,before+after);
    buffer->call("create",size.x,size.y,size.z);
    buffer->call("set_channel_depth",1,2); // CHANNEL_SDF, DEPTH_32_BIT, verified by the installed-class probe.
    PackedByteArray bytes;
    bytes.resize(int64_t(size.x)*size.y*size.z*sizeof(float));
    auto data=bytes.ptrw();
    for(int z=0;z<size.z;z++)for(int x=0;x<size.x;x++)for(int y=0;y<size.y;y++) {
        Vector3 p=origin+Vector3(x-before,y-before,z-before)/resolution;
        float sdf=field.distance({p.x,p.y,p.z})*resolution;
        size_t index=y+size.y*(x+size.x*z); // Voxel Tools ZXY layout.
        std::memcpy(data+index*sizeof(float),&sdf,sizeof(float));
    }
    // One transfer per region instead of crossing the extension boundary for
    // every density sample. Meshing can run on the existing chunk workers.
    buffer->call("set_channel_from_byte_array",1,bytes);
    if(!mesher->has_method("build_mesh_arrays")) {
        UtilityFunctions::push_error("Voxel Tools CPU bridge missing: run tools/setup_voxel_tools.sh");return out;
    }
    Array surfaces=mesher->call("build_mesh_arrays",buffer);
    for(int surface=0;surface<surfaces.size();surface++) {
        Array arrays=surfaces[surface];
        PackedVector3Array vertices=arrays[Mesh::ARRAY_VERTEX],normals=arrays[Mesh::ARRAY_NORMAL];
        PackedInt32Array indices=arrays[Mesh::ARRAY_INDEX];
        int offset=out.vertices.size();
        for(int i=0;i<vertices.size();i++) {
            Vector3 p=origin+vertices[i]/resolution,n=normals[i];
            out.vertices.push_back({p.x,p.y,p.z,n.x,n.y,n.z,0,0,1,1,1});
        }
        for(int i=0;i<indices.size();i+=3) {
            auto a=vertices[indices[i]],b=vertices[indices[i+1]],c=vertices[indices[i+2]];
            if((b-a).cross(c-a).length_squared()<1e-14f)continue;
            for(int j=0;j<3;j++)out.indices.push_back(offset+indices[i+j]);
        }
    }
    return out;
}
Dictionary TerrainMesherBridge::build(const Array &input,Vector3 origin,Vector3i size,int resolution) {
    Dictionary result;
    if(resolution<2 || resolution>8 || size.x<1 || size.y<1 || size.z<1 || size.x>32 || size.y>32 || size.z>32 || !origin.is_finite())return result;
    std::vector<altaria::SmoothCell> cells;
    for(int i=0;i<input.size();i++) {
        Dictionary d=input[i]; Vector3 p=d.get("position",Vector3()),s=d.get("size",Vector3(1,1,1));
        if(!p.is_finite() || !s.is_finite() || s.x<=0 || s.y<=0 || s.z<=0 || s.x>1 || s.y>1 || s.z>1)return {};
        altaria::SmoothCell c;c.origin={p.x,p.y,p.z};c.anchor=c.origin;c.size={s.x,s.y,s.z};c.modified=d.get("terrain",false);
        c.owner=int64_t(d.get("owner",i+1));c.block=int(d.get("block",1));cells.push_back(c);
    }
    altaria::SmoothField field(std::move(cells));auto surface=build_surface(field,origin,size,resolution);
    Ref<ArrayMesh> mesh;mesh.instantiate();
    if(!surface.indices.empty()) {
        PackedVector3Array vertices,normals;PackedInt32Array indices;
        vertices.resize(surface.vertices.size());normals.resize(surface.vertices.size());indices.resize(surface.indices.size());
        auto v=vertices.ptrw(),n=normals.ptrw();auto ix=indices.ptrw();
        for(size_t i=0;i<surface.vertices.size();i++){auto p=surface.vertices[i];v[i]=Vector3(p.x,p.y,p.z);n[i]=Vector3(p.nx,p.ny,p.nz);}
        std::copy(surface.indices.begin(),surface.indices.end(),ix);
        Array arrays;arrays.resize(Mesh::ARRAY_MAX);arrays[Mesh::ARRAY_VERTEX]=vertices;arrays[Mesh::ARRAY_NORMAL]=normals;arrays[Mesh::ARRAY_INDEX]=indices;
        mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES,arrays);
    }
    PackedInt64Array owners;
    PackedInt32Array blocks;
    PackedVector3Array owner_positions;
    owners.resize(surface.indices.size()/3);blocks.resize(owners.size());owner_positions.resize(owners.size());
    auto op=owners.ptrw();auto bp=blocks.ptrw();auto pp=owner_positions.ptrw();
    for(size_t i=0;i<surface.indices.size();i+=3) {
        std::array<float,3> center{};
        for(int j=0;j<3;j++) {const auto &v=surface.vertices[surface.indices[i+j]];center[0]+=v.x/3;center[1]+=v.y/3;center[2]+=v.z/3;}
        auto owner=field.owner_at(center);
        op[i/3]=owner ? owner->owner : 0;bp[i/3]=owner ? owner->block : 0;
        pp[i/3]=owner ? Vector3(owner->origin[0],owner->origin[1],owner->origin[2]) : Vector3();
    }
    result["mesh"]=mesh;result["owners"]=owners;result["blocks"]=blocks;result["owner_positions"]=owner_positions;
    result["triangles"]=int64_t(surface.indices.size()/3);result["sources"]=input.size();return result;
}
}
