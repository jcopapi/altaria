#pragma once
#include "../built_world.h"
#include "terrain_mesher_bridge.h"
#include <condition_variable>
#include <deque>
#include <mutex>
namespace godot {
class TerrainSurface : public Node3D {
    GDCLASS(TerrainSurface,Node3D)
public:
    struct Hit { bool hit=false; uint64_t built=0; altaria::IVec3 cell{}; altaria::BlockId block=0; float distance=1e30f; Vector3 normal; };
private:
    using Key=altaria::IVec3;
    struct Owner { Vector3 position; uint64_t built; altaria::BlockId block; };
    struct Triangle { Vector3 a,b,c; uint32_t owner; };
    struct Job {
        Key key; uint64_t version; bool preview=false;
        std::shared_ptr<const altaria::Generator> generator;
        std::shared_ptr<const altaria::EditMap> edits;
        std::shared_ptr<const altaria::AppearanceMap> styles;
        std::vector<altaria::SmoothCell> built;
        std::shared_ptr<const std::unordered_set<Key,altaria::CoordHash>> world_sources;
        std::unordered_map<altaria::TileKey,std::shared_ptr<const altaria::Volume>,altaria::TileHash> volumes;
    };
    struct Result { Key key; uint64_t version; bool preview=false; altaria::Surface surface; std::vector<Triangle> triangles; std::vector<Owner> owners; double milliseconds=0; };
    struct Patch { MeshInstance3D *node=nullptr; uint64_t version=0; bool world_visible=true; Result geometry; };
    VoxelWorld *world=nullptr;
    BuiltWorld *building=nullptr;
    std::unordered_map<Key,Patch,altaria::CoordHash> patches;
    std::unordered_map<Key,uint64_t,altaria::CoordHash> versions;
    std::unordered_set<Key,altaria::CoordHash> active,dirty,pending,events,retired;
    std::mutex mutex; std::condition_variable ready; std::thread worker;
    std::deque<Job> jobs; std::deque<Result> completed; bool stopping=false,changed=true;
    std::shared_ptr<const std::unordered_set<Key,altaria::CoordHash>> world_sources;
    std::unordered_map<Key,std::vector<Key>,altaria::CoordHash> source_dependencies;
    std::unordered_map<Key,std::vector<uint64_t>,altaria::CoordHash> built_dependencies;
    uint64_t natural_epoch=0;
    Key viewer_region{INT32_MAX,INT32_MAX,INT32_MAX};
    double max_build_ms=0,max_upload_ms=0,max_refresh_ms=0; int uploads=0;
    uint64_t change_epoch=1,preview_epoch=0,preview_version=0,preview_owner=0;
    Key preview_cell{}; bool preview_valid=false;
    int preview_expected=0,preview_received=0,preview_cells=0;
    altaria::Surface preview_surface; Ref<ArrayMesh> preview_mesh;
    mutable Ref<ArrayMesh> cached_selection; mutable uint64_t cached_owner=0;
    mutable Key cached_cell{}; mutable int cached_uploads=-1;
    static Key region(Vector3 p);
    void refresh();
    void commit_sources(Key uploaded);
    Job snapshot(Key key,uint64_t version) const;
    static Result build(Job job);
    void run();
    void upload(Result result);
protected:
    static void _bind_methods() {}
public:
    ~TerrainSurface();
    void configure(VoxelWorld *w,BuiltWorld *b);
    void changed_at(Vector3 p);
    void _process(double delta) override;
    Hit pick(Vector3 origin,Vector3 direction,float reach) const;
    Ref<ArrayMesh> selection_mesh(uint64_t built,altaria::IVec3 cell) const;
    Ref<ArrayMesh> preview(uint64_t built,altaria::IVec3 cell);
    Dictionary stats() const;
};
}
