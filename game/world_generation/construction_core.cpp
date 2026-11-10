#include "construction_core.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>
namespace altaria {
static Point add(Point a, Point b) {
    return {a[0] + b[0], a[1] + b[1], a[2] + b[2]};
}
static Point sub(Point a, Point b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
static float dot(Point a, Point b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
static Point cross(Point a, Point b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
std::pair<Point, Point> shape_bounds(uint8_t shape) {
    Point lo{1, 1, 1}, hi{0, 0, 0};
    for (auto &face : shape_faces(shape))
        for (auto p : face.points)
            for (int j = 0; j < 3; j++) {
                lo[j] = std::min(lo[j], p[j]);
                hi[j] = std::max(hi[j], p[j]);
            }
    return {lo, hi};
}
static bool convex_overlap(const std::vector<Point> &a, Point pa, const std::vector<Point> &b, Point pb,
                           const std::vector<ShapeFace> &fa, const std::vector<ShapeFace> &fb) {
    auto separated = [&](Point axis) {
        float length = std::sqrt(dot(axis, axis));
        if (length < 1e-6f)
            return false;
        for (auto &v : axis)
            v /= length;
        float amin = 1e20f, amax = -1e20f, bmin = 1e20f, bmax = -1e20f;
        for (auto p : a) {
            float v = dot(p, axis);
            amin = std::min(amin, v);
            amax = std::max(amax, v);
        }
        for (auto p : b) {
            float v = dot(add(p, sub(pb, pa)), axis);
            bmin = std::min(bmin, v);
            bmax = std::max(bmax, v);
        }
        return amax <= bmin + .00005f || bmax <= amin + .00005f;
    };
    for (auto f : fa)
        if (separated(f.normal))
            return false;
    for (auto f : fb)
        if (separated(f.normal))
            return false;
    for (size_t i = 0; i < a.size(); i++)
        for (size_t j = i + 1; j < a.size(); j++)
            for (size_t k = 0; k < b.size(); k++)
                for (size_t l = k + 1; l < b.size(); l++)
                    if (separated(cross(sub(a[j], a[i]), sub(b[l], b[k]))))
                        return false;
    return true;
}
bool shapes_overlap(uint8_t a, Point pa, uint8_t b, Point pb) {
    auto aa = shape_bounds(a), bb = shape_bounds(b);
    for (int i = 0; i < 3; i++)
        if (pa[i] + aa.second[i] <= pb[i] + bb.first[i] + .00005f ||
            pb[i] + bb.second[i] <= pa[i] + aa.first[i] + .00005f)
            return false;
    auto ah = shape_hulls(a), bh = shape_hulls(b);
    for (auto &h : ah)
        for (auto &k : bh)
            if (convex_overlap(h, pa, k, pb, shape_faces(a), shape_faces(b)))
                return true;
    return false;
}
IVec3 snap_placement(Point hit, Point normal, uint8_t shape, Point preferred) {
    int axis = 0;
    for (int i = 1; i < 3; i++)
        if (std::abs(normal[i]) > std::abs(normal[axis]))
            axis = i;
    if (std::abs(normal[axis]) < .001f)
        return {};
    float minimum = 1e10f;
    for (auto &face : shape_faces(shape))
        for (auto p : face.points)
            minimum = std::min(minimum, dot(p, normal));
    float plane = dot(hit, normal) - minimum;
    for (int i = 0; i < 3; i++)
        if (i != axis) {
            preferred[i] = std::round(preferred[i] * 8) / 8;
            plane -= preferred[i] * normal[i];
        }
    preferred[axis] = plane / normal[axis];
    return {int(std::round(preferred[0] * 8)), int(std::round(preferred[1] * 8)),
            int(std::round(preferred[2] * 8))};
}
void ConstructionStore::index(const BuiltBlock &block, bool remove) {
    auto bounds = shape_bounds(block.shape());
    auto p = block.position();
    for (int z = int(std::floor(p[2] + bounds.first[2]));
         z < int(std::ceil(p[2] + bounds.second[2] - .0001f)); z++)
        for (int y = int(std::floor(p[1] + bounds.first[1]));
             y < int(std::ceil(p[1] + bounds.second[1] - .0001f)); y++)
            for (int x = int(std::floor(p[0] + bounds.first[0]));
                 x < int(std::ceil(p[0] + bounds.second[0] - .0001f)); x++) {
                IVec3 cell{x, y, z};
                if (remove) {
                    auto it = cells.find(cell);
                    if (it != cells.end()) {
                        auto &v = it->second;
                        v.erase(std::remove(v.begin(), v.end(), block.id), v.end());
                        if (v.empty())
                            cells.erase(it);
                    }
                } else
                    cells[cell].push_back(block.id);
            }
}
void ConstructionStore::put(BuiltBlock block) {
    auto found = blocks.find(block.id);
    Point old=block.position();
    if (found != blocks.end())
    {
        old=found->second.position();
        index(found->second, true);
    }
    block.connected_shape=255;
    next_id = std::max(next_id, block.id + 1);
    blocks[block.id] = block;
    index(block, false);
    refresh_connections(old);
    refresh_connections(block.position());
    ++revision;
}
void ConstructionStore::erase(uint64_t id) {
    auto found = blocks.find(id);
    if (found != blocks.end()) {
        auto p=found->second.position();
        index(found->second, true);
        blocks.erase(found);
        refresh_connections(p);
        ++revision;
    }
}
uint8_t ConstructionStore::neighbor_ramp(int x8,int z8,float plane,uint64_t exclude,const BuiltBlock *preview) const {
    auto matches=[&](const BuiltBlock &n)->uint8_t {
        if(n.id==exclude || n.origin8.x!=x8 || n.origin8.z!=z8)return 0;
        auto code=pack_shape(n.appearance.shape,n.appearance.rotation);auto r=ramp_spec(code);
        return r.valid && std::abs(n.position()[1]+r.low()-plane)<.001f ? code : uint8_t(0);
    };
    if(preview) {auto code=matches(*preview);if(ramp_spec(code).valid)return code;}
    float x=x8/8.f,z=z8/8.f;
    for(auto id:nearby({x-.01f,plane-1.01f,z-.01f},{x+1.01f,plane+1.01f,z+1.01f})) {
        if(preview && id==preview->id)continue;
        auto code=matches(blocks.at(id));if(ramp_spec(code).valid)return code;
    }
    if(terrain_lookup && x8%8==0 && z8%8==0)for(int y:{int(std::floor(plane)),int(std::floor(plane))-1}) {
        auto code=terrain_lookup(int(x),y,int(z)).second;auto r=ramp_spec(code);
        if(r.valid && std::abs(y+r.low()-plane)<.001f)return code;
    }
    return 0;
}
uint8_t ConstructionStore::connection_shape(const BuiltBlock &b,const BuiltBlock *preview) const {
    auto authored=pack_shape(b.appearance.shape,b.appearance.rotation);
    if (shape_type(authored) == 8) {
        auto pos = b.position();
        return connect_terrain([&](int dx, int dy, int dz) {
            Point q{pos[0] + dx, pos[1] + dy, pos[2] + dz};
            if (preview && preview->id != b.id) {
                auto pp = preview->position();
                if (std::abs(pp[0] - q[0]) < .125f && std::abs(pp[1] - q[1]) < .125f &&
                    std::abs(pp[2] - q[2]) < .125f)
                    return true;
            }
            for (auto id : nearby({q[0] + .1f, q[1] + .1f, q[2] + .1f}, {q[0] + .9f, q[1] + .9f, q[2] + .9f})) {
                if (id == b.id || (preview && id == preview->id))
                    continue;
                return true;
            }
            if (terrain_lookup) {
                int tx = int(std::floor(q[0] + .5f)), ty = int(std::floor(q[1] + .5f)),
                    tz = int(std::floor(q[2] + .5f));
                auto [tid, tshape] = terrain_lookup(tx, ty, tz);
                if (tid != 0 && tshape != 255)
                    return true;
            }
            return false;
        });
    }
    auto source=ramp_spec(authored);
    if(!source.valid)return authored;
    float plane=b.position()[1]+source.low();
    return connect_ramp(authored,[&](int dx,int dz) {return neighbor_ramp(b.origin8.x+dx*8,b.origin8.z+dz*8,plane,b.id,preview);});
}
void ConstructionStore::refresh_connections(Point p) {
    for(auto id:nearby({p[0]-1.01f,p[1]-1.51f,p[2]-1.01f},{p[0]+2.01f,p[1]+2.51f,p[2]+2.01f})) {
        auto &b=blocks.at(id); auto before=b.shape(); auto after=connection_shape(b);
        if(before!=after) {
            auto origin=b.position(); auto bounds=shape_bounds(after);
            for(auto other:nearby(add(origin,bounds.first),add(origin,bounds.second)))
                if(other!=id && shapes_overlap(after,origin,blocks.at(other).shape(),blocks.at(other).position())) {after=before;break;}
            if(terrain_lookup && after!=before)
                for(int z=int(std::floor(origin[2]));z<int(std::ceil(origin[2]+1-.0001f));z++)
                    for(int y=int(std::floor(origin[1]));y<int(std::ceil(origin[1]+1-.0001f));y++)
                        for(int x=int(std::floor(origin[0]));x<int(std::ceil(origin[0]+1-.0001f));x++) {
                            auto code=terrain_lookup(x,y,z).second;
                            if(code!=255 && shapes_overlap(after,origin,code,{float(x),float(y),float(z)})) {after=before;}
                        }
        }
        b.connected_shape=after;
        if(before!=after)shape_changes.insert(id);
    }
}
std::vector<uint64_t> ConstructionStore::nearby(Point lo, Point hi) const {
    std::unordered_set<uint64_t> found;
    for (int z = int(std::floor(lo[2])); z <= int(std::floor(hi[2])); z++)
        for (int y = int(std::floor(lo[1])); y <= int(std::floor(hi[1])); y++)
            for (int x = int(std::floor(lo[0])); x <= int(std::floor(hi[0])); x++) {
                auto it = cells.find({x, y, z});
                if (it != cells.end())
                    found.insert(it->second.begin(), it->second.end());
            }
    return {found.begin(), found.end()};
}
bool ConstructionStore::collides(Point lo, Point hi, const Registry &r) const {
    for (auto id : nearby(lo, hi)) {
        const auto &b = blocks.at(id);
        if (!r.get(b.block).solid || b.appearance.terrain>0)
            continue;
        auto p = b.position();
        if (shape_intersects(b.shape(), sub(lo, p), sub(hi, p)))
            return true;
    }
    return false;
}
bool ConstructionStore::can_place(const BuiltBlock &b, const Registry &r, const Settings &s,
                                  const std::function<std::pair<BlockId, uint8_t>(int, int, int)> &terrain,
                                  std::string &error) const {
    auto p = b.position();
    auto new_shape=connection_shape(b,&b);
    auto bounds = shape_bounds(new_shape);
    auto lo = add(p, bounds.first), hi = add(p, bounds.second);
    if (!r.valid(b.block) || b.block == r.air || !r.get(b.block).placeable || !valid_shape_id(b.appearance.shape) ||
        b.appearance.rotation > 3) {
        error = "Invalid building material or form";
        return false;
    }
    if (std::abs(int64_t(b.origin8.x)) > 8000000 || std::abs(int64_t(b.origin8.z)) > 8000000 ||
        lo[1] <= s.min_y || hi[1] > s.max_y) {
        error = "Building outside world bounds";
        return false;
    }
    std::vector<std::pair<const BuiltBlock*,uint8_t>> affected{{&b,new_shape}};
    for(auto id:nearby({p[0]-2.01f,p[1]-2.01f,p[2]-2.01f},{p[0]+3.01f,p[1]+3.01f,p[2]+3.01f}))
        if(id!=b.id)affected.push_back({&blocks.at(id),connection_shape(blocks.at(id),&b)});
    for(size_t i=0;i<affected.size();i++)for(size_t j=i+1;j<affected.size();j++) {
        auto [a,sa]=affected[i]; auto [other,sb]=affected[j];
        if(i>0 && sa==a->shape() && sb==other->shape())continue;
        if(shapes_overlap(sa,a->position(),sb,other->position())) {error="Building pieces intersect";return false;}
    }
    for(size_t i=1;i<affected.size();i++) {
        auto [other,shape]=affected[i]; if(shape==other->shape())continue;
        auto p=other->position();auto bounds=shape_bounds(shape);auto a=add(p,bounds.first),b=add(p,bounds.second);
        for(int z=int(std::floor(a[2]));z<int(std::ceil(b[2]-.0001f));z++)
            for(int y=int(std::floor(a[1]));y<int(std::ceil(b[1]-.0001f));y++)
                for(int x=int(std::floor(a[0]));x<int(std::ceil(b[0]-.0001f));x++) {
                    auto [id,code]=terrain(x,y,z);
                    if(r.get(id).solid && shapes_overlap(shape,p,code,{float(x),float(y),float(z)})) {error="Ramp connection intersects terrain";return false;}
                }
    }
    for (int z = int(std::floor(lo[2])); z < int(std::ceil(hi[2] - .0001f)); z++)
        for (int y = int(std::floor(lo[1])); y < int(std::ceil(hi[1] - .0001f)); y++)
            for (int x = int(std::floor(lo[0])); x < int(std::ceil(hi[0] - .0001f)); x++) {
                auto [id, shape] = terrain(x, y, z);
                if (r.get(id).solid && shapes_overlap(new_shape, p, shape, {float(x), float(y), float(z)})) {
                    error = "Building intersects terrain";
                    return false;
                }
            }
    return true;
}
} // namespace altaria
