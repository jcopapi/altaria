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
    return a.terrain >= -1 && a.terrain <= 1 && !a.derived_ramp && a.tint <= 0xffffff && a.preserve <= 1000 && a.brightness <= 2000 && overlay(a.overlay) &&
           overlay(a.decoration) && (a.shape == 255 || valid_shape_id(a.shape)) && a.rotation < 4;
}
bool WorldModel::validate_surface(const SurfaceCommand &c, std::string &error) const {
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
    if (block_at(c.position.x, c.position.y, c.position.z) == generator->blocks().air) {
        // Mining first records air, then clears the independent modifier. Permit
        // that exact metadata-only reset, while still rejecting paint/shape or
        // overlay changes on empty cells. The off override must survive replay.
        auto cleared=appearance_at(c.position);cleared.terrain=0;
        if(c.appearance.terrain!=0 || !(c.appearance==cleared)) {
            error = "Paint and overlays need a supporting block";
            return false;
        }
    }
    return true;
}
bool WorldModel::apply_surface(const SurfaceCommand &c, std::string &error) {
    if (!validate_surface(c, error))
        return false;
    if (!appearances.count(c.position))
        surface_regions[region(c.position)].push_back(c.position);
    appearances[c.position] = c.appearance;
    sequences[c.actor] = c.sequence;
    ++revision;
    return true;
}
static uint64_t mix_surface(uint64_t n) {
    n = (n ^ (n >> 30)) * 0xbf58476d1ce4e5b9ULL;
    n = (n ^ (n >> 27)) * 0x94d049bb133111ebULL;
    return n ^ (n >> 31);
}
static uint64_t checksum(const SurfaceCommand &c) {
    auto &a = c.appearance;
    return mix_surface(c.actor) ^ mix_surface(c.sequence + 17) ^ mix_surface(uint32_t(c.position.x)) ^
           mix_surface(uint64_t(uint32_t(c.position.y)) + 31) ^
           mix_surface(uint64_t(uint32_t(c.position.z)) + 47) ^ mix_surface(a.tint) ^
           mix_surface((uint64_t(a.preserve) << 32) | a.brightness) ^
           mix_surface((uint64_t(a.overlay) << 32) | a.decoration) ^ mix_surface(a.painted ? 71 : 89) ^
           ((a.shape == 255 && a.rotation == 0)
                ? 0
                : mix_surface(0x53485000ULL + unsigned(a.shape) + (unsigned(a.rotation) << 8))) ^
           (a.terrain == -1 ? 0 : mix_surface(0x54455200ULL + unsigned(a.terrain)));
}
bool SurfaceJournal::append(const SurfaceCommand &c, std::string &error) const {
    auto f = std::fopen(filename.c_str(), "ab");
    if (!f) {
        error = "Cannot open surface journal";
        return false;
    }
    auto &a = c.appearance;
    int n = std::fprintf(f, "S3 %llu %llu %d %d %d %u %u %u %u %u %u %u %u %u %llu\n",
                         (unsigned long long)c.actor, (unsigned long long)c.sequence, c.position.x,
                         c.position.y, c.position.z, a.tint, unsigned(a.preserve), unsigned(a.brightness),
                         unsigned(a.overlay), unsigned(a.decoration), unsigned(a.painted), unsigned(a.shape),
                         unsigned(a.rotation), unsigned(a.terrain+1), (unsigned long long)checksum(c));
    bool ok = n > 0 && std::fflush(f) == 0;
    if (std::fclose(f) != 0)
        ok = false;
    if (!ok)
        error = "Cannot persist paint/overlay edit";
    return ok;
}
bool SurfaceJournal::load(AppearanceMap &out, const Registry &r, const Settings &s,
                          std::string &error) const {
    auto f = std::fopen(filename.c_str(), "rb");
    if (!f) {
        if (errno == ENOENT)
            return true;
        error = "Cannot read surface journal";
        return false;
    }
    char line[512];
    uintmax_t valid_bytes = 0;
    while (std::fgets(line, sizeof(line), f)) {
        size_t len = std::strlen(line);
        if (!len || line[len - 1] != '\n') {
            bool torn = std::feof(f);
            std::fclose(f);
            if (!torn) {
                error = "Oversized surface journal record";
                return false;
            }
            std::error_code ec;
            std::filesystem::resize_file(filename, valid_bytes, ec);
            if (ec) {
                error = "Cannot recover interrupted surface edit";
                return false;
            }
            return true;
        }
        valid_bytes += len;
        SurfaceCommand c;
        unsigned long long actor, seq, sum;
        unsigned tint, preserve, brightness, overlay, deco, painted, shape = 255, rotation = 0, terrain = 0;
        char extra;
        bool v3 = std::strncmp(line, "S3 ", 3) == 0;
        bool v2 = std::strncmp(line, "S2 ", 3) == 0;
        int fields;
        if (v3)
            fields = std::sscanf(line, "S3 %llu %llu %d %d %d %u %u %u %u %u %u %u %u %u %llu %c", &actor, &seq,
                                 &c.position.x, &c.position.y, &c.position.z, &tint, &preserve, &brightness,
                                 &overlay, &deco, &painted, &shape, &rotation, &terrain, &sum, &extra);
        else if (v2)
            fields = std::sscanf(line, "S2 %llu %llu %d %d %d %u %u %u %u %u %u %u %u %llu %c", &actor, &seq,
                                 &c.position.x, &c.position.y, &c.position.z, &tint, &preserve, &brightness,
                                 &overlay, &deco, &painted, &shape, &rotation, &sum, &extra);
        else
            fields = std::sscanf(line, "%llu %llu %d %d %d %u %u %u %u %u %u %llu %c", &actor, &seq,
                                 &c.position.x, &c.position.y, &c.position.z, &tint, &preserve, &brightness,
                                 &overlay, &deco, &painted, &sum, &extra);
        if (fields != (v3 ? 15 : v2 ? 14 : 12) || terrain > 2 || (shape != 255 && !valid_shape_id(int(shape))) || rotation >= 4 || !actor || !seq ||
            tint > 0xffffff || preserve > 1000 || brightness > 2000 || overlay > 65535 || deco > 65535 ||
            painted > 1) {
            std::fclose(f);
            error = "Corrupt surface journal";
            return false;
        }
        c.actor = actor;
        c.sequence = seq;
        c.appearance = {tint,           uint16_t(preserve), uint16_t(brightness), uint16_t(overlay),
                        uint16_t(deco), painted != 0,       uint8_t(shape),       uint8_t(rotation)};
        c.appearance.terrain = int8_t(terrain)-1;
        if (checksum(c) != sum || !valid_surface(c.appearance, r) || c.position.y <= s.min_y ||
            c.position.y >= s.max_y || std::abs(int64_t(c.position.x)) > 1000000 ||
            std::abs(int64_t(c.position.z)) > 1000000) {
            std::fclose(f);
            error = "Invalid surface journal record";
            return false;
        }
        // Old shape-8 commands replaced the source shape. Preserve the only
        // recoverable legacy source (a cube); S3 stores the modifier separately.
        if (!v3 && c.appearance.shape == 8) {
            c.appearance.shape = 0;
            c.appearance.terrain = 1;
        }
        out[c.position] = c.appearance;
    }
    bool ok = !std::ferror(f);
    std::fclose(f);
    if (!ok)
        error = "Could not read surface journal";
    return ok;
}
} // namespace altaria
