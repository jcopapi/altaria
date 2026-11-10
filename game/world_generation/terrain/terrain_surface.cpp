#include "terrain_surface.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/concave_polygon_shape3d.hpp>
namespace godot {
using namespace altaria;
static Vector3 vector(Point p) {return Vector3(p[0],p[1],p[2]);}
static Point point(Vector3 p) {return {p.x,p.y,p.z};}
TerrainSurface::Key TerrainSurface::region(Vector3 p) {
    return {int(std::floor(p.x/8))*8,int(std::floor(p.y/8))*8,int(std::floor(p.z/8))*8};
}
TerrainSurface::~TerrainSurface() {
    {std::lock_guard lock(mutex);stopping=true;jobs.clear();}
    ready.notify_all();if(worker.joinable())worker.join();
}
void TerrainSurface::configure(VoxelWorld *w,BuiltWorld *b) {
    world=w;building=b;
    world->set_terrain_callback([this](Vector3 p){changed_at(p);});
    building->set_terrain_callback([this](Vector3 p){changed_at(p);});
    worker=std::thread([this]{run();});
}
void TerrainSurface::changed_at(Vector3 p) {++change_epoch;changed=true;events.insert({int(std::floor(p.x)),int(std::floor(p.y)),int(std::floor(p.z))});}
void TerrainSurface::refresh() {
    auto refresh_started=std::chrono::steady_clock::now();
    std::unordered_set<Key,CoordHash> next;
    auto add=[&](Vector3 p) {
        auto lo=region(p-Vector3(1,1,1)),hi=region(p+Vector3(2,2,2));
        for(int z=lo.z;z<=hi.z;z+=8)for(int y=lo.y;y<=hi.y;y+=8)for(int x=lo.x;x<=hi.x;x+=8)next.insert({x,y,z});
    };
    auto sources=std::make_shared<std::unordered_set<Key,CoordHash>>();
    auto viewer=world->viewer_position();float near=world->settings().near_distance+64;
    float natural_distance=world->settings().terrain_distance;
    // Quantized residency is deterministic and avoids rebuilding on every
    // sub-metre movement. Procedural modifiers use the identical patch pipeline.
    Vector3 center(viewer_region.x+4,viewer_region.y+4,viewer_region.z+4);
    for(auto p:world->nearby_natural_terrain(center,natural_distance))if(world->terrain_enabled(p)) {
        sources->insert(p);add(Vector3(p.x,p.y,p.z));
    }
    for(auto &[p,a]:world->authored_surfaces())if(a.terrain>0 && world->registry().get(world->block_at(p.x,p.y,p.z)).solid) {
        float dx=p.x-viewer.x,dz=p.z-viewer.z;
        if(dx*dx+dz*dz<=near*near){sources->insert(p);add(Vector3(p.x,p.y,p.z));}
    }
    built_dependencies.clear();
    for(auto &[id,b]:building->construction().blocks)if(b.appearance.terrain>0) {
        auto p=vector(b.position());
        if(p.distance_to(viewer)<=world->settings().view_distance+32) {
            add(p);auto lo=region(p-Vector3(1,1,1)),hi=region(p+Vector3(2,2,2));
            for(int z=lo.z;z<=hi.z;z+=8)for(int y=lo.y;y<=hi.y;y+=8)for(int x=lo.x;x<=hi.x;x+=8)built_dependencies[{x,y,z}].push_back(id);
        }
        else building->clear_terrain_meshed(id);
    }
    std::vector<Key> removed;
    auto changed_source=[&](Key p) {
        auto lo=region(Vector3(p.x-2,p.y-2,p.z-2)),hi=region(Vector3(p.x+3,p.y+3,p.z+3));
        for(int z=lo.z;z<=hi.z;z+=8)for(int y=lo.y;y<=hi.y;y+=8)for(int x=lo.x;x<=hi.x;x+=8) {
            Key k{x,y,z};if(next.count(k) || active.count(k)){dirty.insert(k);++versions[k];}
        }
    };
    if(world_sources)for(auto p:*world_sources)if(!sources->count(p)){removed.push_back(p);changed_source(p);}
    for(auto p:*sources)if(!world_sources || !world_sources->count(p))changed_source(p);
    world->update_terrain_meshed({},removed);
    world_sources=std::move(sources);
    source_dependencies.clear();
    for(auto p:*world_sources) {
        auto lo=region(Vector3(p.x-1,p.y-1,p.z-1)),hi=region(Vector3(p.x+2,p.y+2,p.z+2));
        for(int z=lo.z;z<=hi.z;z+=8)for(int y=lo.y;y<=hi.y;y+=8)for(int x=lo.x;x<=hi.x;x+=8)source_dependencies[{x,y,z}].push_back(p);
    }
    for(auto key:next)if(!active.count(key)) {dirty.insert(key);++versions[key];}
    for(auto p:events) {
        auto lo=region(Vector3(p.x-2,p.y-2,p.z-2)),hi=region(Vector3(p.x+3,p.y+3,p.z+3));
        for(int z=lo.z;z<=hi.z;z+=8)for(int y=lo.y;y<=hi.y;y+=8)for(int x=lo.x;x<=hi.x;x+=8) {
            Key k{x,y,z};if(next.count(k) || active.count(k)) {dirty.insert(k);++versions[k];}
        }
    }
    for(auto key:active)if(!next.count(key)) {
        if(patches.count(key))retired.insert(key);
        dirty.erase(key);++versions[key];
    }
    for(auto key:next)retired.erase(key);
    active=std::move(next);events.clear();changed=false;
    max_refresh_ms=std::max(max_refresh_ms,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-refresh_started).count());
}
void TerrainSurface::commit_sources(Key uploaded) {
    auto complete=[&](Vector3 p) {
        auto lo=region(p-Vector3(1,1,1)),hi=region(p+Vector3(2,2,2));
        for(int z=lo.z;z<=hi.z;z+=8)for(int y=lo.y;y<=hi.y;y+=8)for(int x=lo.x;x<=hi.x;x+=8) {
            Key key{x,y,z};auto it=patches.find(key);
            if(it==patches.end() || it->second.version!=versions[key])return false;
        }
        return true;
    };
    // Keep the previous cube/collider until every part of its replacement is
    // ready. Readiness is presentation state and never enters either journal.
    std::vector<Key> ready_sources;
    for(auto p:source_dependencies[uploaded])if(!world->terrain_meshed(p) && complete(Vector3(p.x,p.y,p.z)))ready_sources.push_back(p);
    world->update_terrain_meshed(ready_sources,{});
    for(auto id:built_dependencies[uploaded])if(auto b=building->get(id);b && b->appearance.terrain>0 && !building->terrain_meshed(id) && complete(vector(b->position())))building->set_terrain_meshed(id);
}
TerrainSurface::Job TerrainSurface::snapshot(Key key,uint64_t version) const {
    Job job;job.world_sources=world_sources;job.key=key;job.version=version;job.generator=world->generator_snapshot();job.edits=world->edit_snapshot();
    // Horizontal region snapshots already include the complete vertical range.
    job.styles=world->style_snapshot({int(std::floor((key.x-2)/32.f))*32,int(std::floor((key.z-2)/32.f))*32,1});
    job.volumes=world->volume_snapshot(key);
    Point lo{float(key.x-2),float(key.y-2),float(key.z-2)},hi{float(key.x+10),float(key.y+10),float(key.z+10)};
    auto &store=building->construction();
    for(auto id:store.nearby(lo,hi)) {
        const auto &b=store.blocks.at(id);auto p=b.position();auto bounds=shape_bounds(pack_shape(b.appearance.shape,b.appearance.rotation));
        SmoothCell c;c.owner=id;c.block=b.block;c.appearance=b.appearance;c.modified=b.appearance.terrain>0;c.anchor=p;
        for(int a=0;a<3;a++){c.origin[a]=p[a]+bounds.first[a];c.size[a]=bounds.second[a]-bounds.first[a];}
        job.built.push_back(c);
    }
    return job;
}
TerrainSurface::Result TerrainSurface::build(Job job) {
    auto started=std::chrono::steady_clock::now();Result result;result.key=job.key;result.version=job.version;result.preview=job.preview;
    auto &g=*job.generator;const auto &r=g.blocks();const auto &cfg=g.settings();
    std::vector<SmoothCell> sources=std::move(job.built);
    for(int z=job.key.z-2;z<job.key.z+10;z++)for(int x=job.key.x-2;x<job.key.x+10;x++) {
        TileKey tile{int(std::floor(x/32.f))*32,int(std::floor(z/32.f))*32,0};
        auto loaded=job.volumes.find(tile);
        auto volume=loaded==job.volumes.end() ? nullptr : loaded->second.get();
        Column col=volume ? volume->columns[(z-tile.z+1)*34+x-tile.x+1] : g.column(x,z);
        for(int y=job.key.y-2;y<job.key.y+10;y++) {
            if(y<cfg.min_y || y>=cfg.max_y)continue;
            IVec3 p{x,y,z};auto edit=job.edits->find(p);
            BlockId id=edit!=job.edits->end() ? edit->second : volume ? volume->at(x-tile.x,y-cfg.min_y,z-tile.z) : g.sample(x,y,z);
            const auto &b=r.get(id);if(!b.solid || b.foliage)continue;
            auto style=job.styles->find(p);Appearance a=style==job.styles->end() ? Appearance() : style->second;
            uint8_t shape=a.shape!=255 ? pack_shape(a.shape,a.rotation) : edit!=job.edits->end() ? 0 : g.surface_shape(x,y,z,col);
            auto bounds=shape_bounds(shape);SmoothCell c;c.anchor={float(x),float(y),float(z)};c.block=id;c.appearance=a;c.modified=a.terrain==0 ? false : (a.terrain>0 || (job.world_sources && job.world_sources->count(p)));c.column=col;
            for(int axis=0;axis<3;axis++){c.origin[axis]=c.anchor[axis]+bounds.first[axis];c.size[axis]=bounds.second[axis]-bounds.first[axis];}
            sources.push_back(c);
        }
    }
    for(auto &c:sources)if(c.owner)c.column=g.column(int(std::floor(c.anchor[0])),int(std::floor(c.anchor[2])));
    SmoothField field(std::move(sources));Vector3 origin(job.key.x,job.key.y,job.key.z);
    auto raw=TerrainMesherBridge::build_surface(field,origin,Vector3i(8,8,8),4);
    std::unordered_map<const SmoothCell*,uint32_t> owners;
    std::unordered_map<const SmoothCell*,Surface> styles;
    for(size_t i=0;i<raw.indices.size();i+=3) {
        const auto &a=raw.vertices[raw.indices[i]],&b=raw.vertices[raw.indices[i+1]],&c=raw.vertices[raw.indices[i+2]];
        Vector3 p(a.x,a.y,a.z),q(b.x,b.y,b.z),v(c.x,c.y,c.z),center=(p+q+v)/3;
        if(!field.modified_surface(point(center)))continue;
        auto source=field.owner_at(point(center));if(!source)continue;
        auto it=owners.find(source);uint32_t owner;
        if(it==owners.end()){owner=result.owners.size();owners[source]=owner;result.owners.push_back({vector(source->anchor),source->owner,source->block});}
        else owner=it->second;
        result.triangles.push_back({p,q,v,owner});
        auto material=styles.find(source);
        if(material==styles.end()) {
            int transition=0;const SmoothCell *upper=nullptr;
            if(source->appearance.overlay==65535 && r.get(source->block).overlay==0)
                for(const auto &candidate:field.cells()) {
                    if(candidate.block==source->block || r.get(candidate.block).transition_overlay<=0)continue;
                    if(std::abs(candidate.origin[1]-source->origin[1]-source->size[1])>.001f)continue;
                    float x=source->origin[0]+source->size[0]*.5f,z=source->origin[2]+source->size[2]*.5f;
                    if(x<candidate.origin[0] || x>=candidate.origin[0]+candidate.size[0] || z<candidate.origin[2] || z>=candidate.origin[2]+candidate.size[2])continue;
                    transition=r.get(candidate.block).transition_overlay;upper=&candidate;break;
                }
            Surface style;append_block_surface(style,{0,0,0},0,source->block,source->appearance,source->column,r,transition);
            if(upper && upper->appearance.painted && !source->appearance.painted) {
                auto tint=unpack_rgb(upper->appearance.tint);
                for(auto &v:style.vertices)if(v.overlay>=0) {v.tr=tint.r;v.tg=tint.g;v.tb=tint.b;v.preserve=upper->appearance.preserve/1000.f;v.brightness=upper->appearance.brightness/1000.f;}
            }
            material=styles.emplace(source,std::move(style)).first;
        }
        for(int j=0;j<3;j++) {
            const auto &original=raw.vertices[raw.indices[i+j]];
            int face=std::abs(original.ny)>.5 ? (original.ny>0?3:2) : std::abs(original.nx)>.5 ? (original.nx>0?1:0) : original.nz>0?5:4;
            auto vertex=material->second.vertices[face*4];
            vertex.x=original.x-origin.x;vertex.y=original.y-origin.y;vertex.z=original.z-origin.z;
            vertex.nx=original.nx;vertex.ny=original.ny;vertex.nz=original.nz;
            vertex.wind=source->anchor[1]+source->size[1];
            vertex.u=source->owner ? 1.f : 0.f; // provenance for the existing distant voxel LOD renderer
            result.surface.indices.push_back(result.surface.vertices.size());result.surface.vertices.push_back(vertex);
        }
    }
    result.milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();return result;
}
void TerrainSurface::run() {
    while(true) {
        Job job;
        {std::unique_lock lock(mutex);ready.wait(lock,[&]{return stopping || !jobs.empty();});if(stopping)return;job=std::move(jobs.front());jobs.pop_front();}
        auto result=build(std::move(job));
        {std::lock_guard lock(mutex);if(!stopping)completed.push_back(std::move(result));}
    }
}
void TerrainSurface::upload(Result result) {
    auto upload_started=std::chrono::steady_clock::now();
    auto &patch=patches[result.key];auto previous=patch.node;
    auto node=memnew(MeshInstance3D);node->set_name("TerrainTransvoxelPatch_"+String::num_int64(result.key.x)+"_"+String::num_int64(result.key.y)+"_"+String::num_int64(result.key.z)+"_v"+String::num_int64(result.version));
    node->set_position(Vector3(result.key.x,result.key.y,result.key.z));node->set_mesh(world->surface_mesh(result.surface));
    node->set_visibility_range_end(world->settings().view_distance);
    node->set_instance_shader_parameter("smooth_patch",true);
    if(!result.surface.indices.empty()) {
        PackedVector3Array faces;faces.resize(result.surface.indices.size());auto fp=faces.ptrw();
        for(size_t i=0;i<result.surface.indices.size();i++){auto &v=result.surface.vertices[result.surface.indices[i]];fp[i]=Vector3(v.x,v.y,v.z);}
        Ref<ConcavePolygonShape3D> shape;shape.instantiate();shape->set_faces(faces);
        auto body=memnew(StaticBody3D);body->set_collision_layer(16);body->set_collision_mask(2);
        auto collision=memnew(CollisionShape3D);collision->set_shape(shape);body->add_child(collision);node->add_child(body);
    }
    add_child(node);if(previous)previous->queue_free();
    patch.node=node;patch.version=result.version;max_build_ms=std::max(max_build_ms,result.milliseconds);patch.geometry=std::move(result);++uploads;commit_sources(patch.geometry.key);
    max_upload_ms=std::max(max_upload_ms,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-upload_started).count());
}
void TerrainSurface::_process(double) {
    if(!world)return;
    auto viewer=region(world->viewer_position());if(!(viewer==viewer_region)){viewer_region=viewer;changed=true;}
    if(natural_epoch!=world->natural_terrain_epoch()){natural_epoch=world->natural_terrain_epoch();changed=true;}
    if(changed)refresh();
    int retired_this_frame=0;
    for(auto it=retired.begin();it!=retired.end() && retired_this_frame<2;) {
        auto patch=patches.find(*it);bool replaced=true;
        if(patch!=patches.end()) {
            for(auto &o:patch->second.geometry.owners) {
                bool ready=o.built ? building->source_rendered(o.position) : world->source_rendered({int(std::floor(o.position.x)),int(std::floor(o.position.y)),int(std::floor(o.position.z))});
                if(!ready){replaced=false;break;}
            }
            if(replaced){patch->second.node->queue_free();patches.erase(patch);++retired_this_frame;}
        }
        if(replaced)it=retired.erase(it);else ++it;
    }
    for(auto &[key,patch]:patches) {
        bool visible=world->near_visible(key);
        if(patch.world_visible!=visible) {patch.node->set_instance_shader_parameter("smooth_world_visible",visible);patch.world_visible=visible;}
    }
    Result result;bool available=false;
    {std::lock_guard lock(mutex);if(!completed.empty()){result=std::move(completed.front());completed.pop_front();available=true;}}
    if(available) {
        if(result.preview) {
            if(result.version==preview_version) {
                int base=preview_surface.vertices.size();
                for(auto v:result.surface.vertices) {v.x+=result.key.x;v.y+=result.key.y;v.z+=result.key.z;preview_surface.vertices.push_back(v);}
                for(auto index:result.surface.indices)preview_surface.indices.push_back(base+index);
                if(++preview_received==preview_expected)preview_mesh=world->surface_mesh(preview_surface);
            }
        } else {
            pending.erase(result.key);
            if(active.count(result.key) && versions[result.key]==result.version)upload(std::move(result));
            else if(active.count(result.key))dirty.insert(result.key);
        }
    }
    world->set_smooth_colliders_active(!patches.empty());
    int queued=0;
    while(queued<2 && pending.size()<8) {
        auto best=dirty.end();float distance=1e30f;
        for(auto it=dirty.begin();it!=dirty.end();++it)if(!pending.count(*it) && active.count(*it)) {
            float d=Vector3(it->x+4,it->y+4,it->z+4).distance_squared_to(world->viewer_position());
            if(d<distance){best=it;distance=d;}
        }
        if(best==dirty.end())break;
        auto key=*best;auto job=snapshot(key,versions[key]);
        {std::lock_guard lock(mutex);jobs.push_back(std::move(job));}
        pending.insert(key);dirty.erase(best);++queued;ready.notify_one();
    }
}
static float ray_triangle(Vector3 origin,Vector3 direction,Vector3 a,Vector3 b,Vector3 c) {
    auto e1=b-a,e2=c-a,h=direction.cross(e2);float determinant=e1.dot(h);if(std::abs(determinant)<1e-8f)return -1;
    float inv=1/determinant;auto s=origin-a;float u=inv*s.dot(h);if(u<-.00001f || u>1.00001f)return -1;
    auto q=s.cross(e1);float v=inv*direction.dot(q);if(v<-.00001f || u+v>1.00001f)return -1;
    return inv*e2.dot(q);
}
TerrainSurface::Hit TerrainSurface::pick(Vector3 origin,Vector3 direction,float reach) const {
    Hit hit;
    for(auto &[key,patch]:patches) {
        AABB bounds(Vector3(key.x,key.y,key.z),Vector3(8,8,8));
        float nearest=0;for(int axis=0;axis<3;axis++){float d=std::max(0.f,std::max(float(bounds.position[axis]-origin[axis]),float(origin[axis]-bounds.position[axis]-8)));nearest+=d*d;}
        if(nearest>reach*reach)continue;
        if(!bounds.has_point(origin) && !bounds.intersects_ray(origin,direction))continue;
        for(auto &triangle:patch.geometry.triangles) {
            float t=ray_triangle(origin,direction,triangle.a,triangle.b,triangle.c);
            if(t<0 || t>reach || t>=hit.distance)continue;
            auto &owner=patch.geometry.owners[triangle.owner];
            if(owner.built) {auto source=building->get(owner.built);if(!source || source->appearance.terrain<=0)continue;}
            else {
                IVec3 source{int(std::floor(owner.position.x)),int(std::floor(owner.position.y)),int(std::floor(owner.position.z))};
                if(!world->terrain_enabled(source) || !world->registry().get(world->block_at(source.x,source.y,source.z)).solid)continue;
            }
            hit.hit=true;hit.distance=t;hit.built=owner.built;hit.block=owner.block;
            hit.cell={int(std::floor(owner.position.x)),int(std::floor(owner.position.y)),int(std::floor(owner.position.z))};
            hit.normal=(triangle.b-triangle.a).cross(triangle.c-triangle.a).normalized();
            if(hit.normal.dot(direction)>0)hit.normal=-hit.normal;
        }
    }
    return hit;
}
Ref<ArrayMesh> TerrainSurface::selection_mesh(uint64_t built,IVec3 cell) const {
    if(cached_uploads==uploads && cached_owner==built && cached_cell==cell)return cached_selection;
    Surface surface;
    for(auto &[key,patch]:patches)for(auto &t:patch.geometry.triangles) {
        auto &o=patch.geometry.owners[t.owner];if(o.built!=built)continue;
        if(!built && (int(std::floor(o.position.x))!=cell.x || int(std::floor(o.position.y))!=cell.y || int(std::floor(o.position.z))!=cell.z))continue;
        auto n=(t.b-t.a).cross(t.c-t.a).normalized();
        for(auto p:{t.a,t.b,t.c}) {surface.indices.push_back(surface.vertices.size());surface.vertices.push_back({p.x,p.y,p.z,n.x,n.y,n.z,0,0,1,1,1});}
    }
    cached_selection=world->surface_mesh(surface);cached_uploads=uploads;cached_owner=built;cached_cell=cell;
    return cached_selection;
}
Ref<ArrayMesh> TerrainSurface::preview(uint64_t built,IVec3 cell) {
    if(preview_valid && preview_epoch==change_epoch && preview_owner==built && preview_cell==cell)return preview_mesh;
    Vector3 position(cell.x,cell.y,cell.z);Appearance appearance;BlockId block;uint8_t shape;
    if(built) {
        auto source=building->get(built);if(!source)return {};
        position=vector(source->position());appearance=source->appearance;block=source->block;shape=pack_shape(appearance.shape,appearance.rotation);
    } else {appearance=world->appearance_at(cell);block=world->block_at(cell.x,cell.y,cell.z);shape=world->base_shape_at(cell.x,cell.y,cell.z);}
    if(!world->registry().get(block).solid)return {};
    bool enabled=built ? appearance.terrain<=0 : !world->terrain_enabled(cell);
    preview_epoch=change_epoch;preview_owner=built;preview_cell=cell;preview_valid=true;++preview_version;
    preview_mesh.unref();preview_surface={};preview_received=0;preview_cells=1;
    std::unordered_set<Key,CoordHash> regions;
    auto include=[&](Vector3 p) {
        auto lo=region(p-Vector3(1,1,1)),hi=region(p+Vector3(2,2,2));
        for(int z=lo.z;z<=hi.z;z+=8)for(int y=lo.y;y<=hi.y;y+=8)for(int x=lo.x;x<=hi.x;x+=8)regions.insert({x,y,z});
    };
    include(position);
    // Include complete neighboring surfaces, even when a neighbor crosses a
    // different patch edge than the selected block. Avoid a clipped preview.
    if(world_sources)for(auto p:*world_sources)if(std::abs(p.x-position.x)<=2 && std::abs(p.y-position.y)<=2 && std::abs(p.z-position.z)<=2)include(Vector3(p.x,p.y,p.z));
    for(auto &[id,b]:building->construction().blocks)if(b.appearance.terrain>0) {
        auto delta=(vector(b.position())-position).abs();if(delta.x<=2 && delta.y<=2 && delta.z<=2)include(vector(b.position()));
    }
    std::vector<Job> requests;
    std::unordered_set<uint64_t> affected_built;
    std::unordered_set<IVec3,CoordHash> affected_world;
    for(auto key:regions) {
        auto job=snapshot(key,preview_version);job.preview=true;
        if(built) {
            for(auto &c:job.built)if(c.owner==built){c.modified=enabled;c.appearance.terrain=enabled?1:0;}
        } else {
            auto styles=std::make_shared<AppearanceMap>(*job.styles);auto a=appearance;a.terrain=enabled?1:0;(*styles)[cell]=a;job.styles=styles;
        }
        for(auto &c:job.built)if(c.modified)affected_built.insert(c.owner);
        for(auto &[p,a]:*job.styles)if(a.terrain>0 && std::abs(p.x-position.x)<=2 && std::abs(p.y-position.y)<=2 && std::abs(p.z-position.z)<=2)affected_world.insert(p);
        requests.push_back(std::move(job));
    }
    preview_cells=std::max(1,int(affected_built.size()+affected_world.size()));
    if(!enabled) {
        appearance.terrain=0;auto column=world->generator()->column(int(std::floor(position.x)),int(std::floor(position.z)));
        append_block_surface(preview_surface,point(position),shape,block,appearance,column,world->registry());
    }
    preview_expected=requests.size();
    {std::lock_guard lock(mutex);
        jobs.erase(std::remove_if(jobs.begin(),jobs.end(),[](const Job &job){return job.preview;}),jobs.end());
        for(auto &job:requests)jobs.push_back(std::move(job));
    }
    ready.notify_one();return preview_mesh;
}
Dictionary TerrainSurface::stats() const {
    Dictionary d;size_t triangles=0;for(auto &[key,p]:patches)triangles+=p.geometry.triangles.size();
    d["terrain_natural_cells"]=int64_t(world->natural_terrain_cells().size());d["terrain_resident_sources"]=int64_t(world_sources ? world_sources->size() : 0);
    d["terrain_patches"]=int64_t(patches.size());d["terrain_pending"]=int64_t(pending.size()+dirty.size()+retired.size());
    d["terrain_preview_pending"]=preview_expected-preview_received;d["terrain_preview_cells"]=preview_cells;
    d["terrain_preview_ready"]=preview_mesh.is_valid();d["terrain_preview_version"]=int64_t(preview_version);
    d["terrain_triangles"]=int64_t(triangles);d["terrain_uploads"]=uploads;d["terrain_max_build_ms"]=max_build_ms;d["terrain_max_upload_ms"]=max_upload_ms;d["terrain_max_refresh_ms"]=max_refresh_ms;return d;
}
}
