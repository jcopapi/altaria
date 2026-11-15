#include "voxel_core.h"
#include "block_shapes.h"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
namespace altaria {
static IVec3 region(IVec3 p) {
    return {int(std::floor(double(p.x) / 32)), 0, int(std::floor(double(p.z) / 32))};
}
void WorldModel::restore_surfaces(AppearanceMap data) {
    appearances = std::move(data);
    surface_regions.clear();
    for (auto &e : appearances)
        surface_regions[region(e.first)].push_back(e.first);
    revision += appearances.size();
}
Appearance WorldModel::appearance_at(IVec3 p) const {
    auto it = appearances.find(p);
    return it == appearances.end() ? Appearance() : it->second;
}
std::shared_ptr<const AppearanceMap> WorldModel::surfaces_for(const TileKey &t) const {
    auto out = std::make_shared<AppearanceMap>();
    int pad = 1 << t.level, minx = t.x - pad, maxx = t.x + t.size() + pad, minz = t.z - pad,
        maxz = t.z + t.size() + pad;
    for (int z = int(std::floor(double(minz) / 32)); z <= int(std::floor(double(maxz) / 32)); z++)
        for (int x = int(std::floor(double(minx) / 32)); x <= int(std::floor(double(maxx) / 32)); x++) {
            auto it = surface_regions.find({x, 0, z});
            if (it == surface_regions.end())
                continue;
            for (auto &p : it->second)
                if (p.x >= minx && p.x < maxx && p.z >= minz && p.z < maxz)
                    (*out)[p] = appearances.at(p);
        }
    return out;
}
static bool valid_surface(const Appearance &a, const Registry &r) {
    auto overlay = [&](uint16_t id) {
        return id == 65535 || id == 0 || (id < r.overlays.size() && !r.overlays[id].key.empty());
    };
    return !a.derived_ramp && a.tint <= 0xffffff && a.preserve <= 1000 && a.brightness <= 2000 && overlay(a.overlay) &&
           overlay(a.decoration) && (a.shape == 255 || valid_shape_id(a.shape)) && a.rotation < 4;
}
bool WorldModel::validate_surface(const SurfaceCommand &c, std::string &error,std::optional<BlockId> support) const {
    const auto &s = generator->settings();
    auto it = sequences.find(c.actor);
    if (!c.actor || !c.sequence || (it != sequences.end() && c.sequence <= it->second)) {
        error = "Stale surface edit sequence";
        return false;
    }
    if (c.position.y <= s.min_y || c.position.y >= s.max_y || std::abs(int64_t(c.position.x)) > 1000000 ||
        std::abs(int64_t(c.position.z)) > 1000000) {
        error = "Surface edit outside world bounds";
        return false;
    }
    if (!valid_surface(c.appearance, generator->blocks())) {
        error = "Invalid paint or overlay definition";
        return false;
    }
    BlockId block=support ? *support : block_at(c.position.x, c.position.y, c.position.z);
    if (!generator->blocks().valid(block) || block == generator->blocks().air) {
        error = "Paint and overlays need a supporting block";
        return false;
    }
    return true;
}
bool WorldModel::apply_surface(const SurfaceCommand &c, std::string &error,std::optional<BlockId> support) {
    if (!validate_surface(c, error,support))
        return false;
    if (!appearances.count(c.position))
        surface_regions[region(c.position)].push_back(c.position);
    appearances[c.position] = c.appearance;
    sequences[c.actor] = c.sequence;
    ++revision;
    return true;
}
} // namespace altaria
