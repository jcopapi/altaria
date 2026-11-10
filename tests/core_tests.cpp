#include "game/world_generation/voxel_core.h"
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace altaria;
static int checks = 0;
static void require(bool condition, const char *message) {
    checks++;
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
static Registry registry() {
    Registry r;
    std::string e;
    const char *names[] = {"air", "stone", "dirt", "grass", "sand", "water", "snow", "bedrock"};
    for (int i = 0; i < 8; i++) {
        BlockDef b;
        b.id = i;
        b.key = names[i];
        b.name = names[i];
        b.solid = i != 0 && i != 5;
        b.water = i == 5;
        require(r.add(b, e), "valid registry fixture");
    }
    r.overlays.resize(5);
    r.overlays[1].id = 1;
    r.overlays[1].key = "grass";
    r.overlays[1].side = 2;
    r.overlays[1].tintable = true;
    for (int i = 2; i < 5; i++) {
        r.overlays[i].id = i;
        r.overlays[i].key = "pebbles" + std::to_string(i);
        r.overlays[i].decal = true;
        r.overlays[i].top = i + 2;
    }
    r.blocks[r.grass].texture_layer = 3;
    r.blocks[r.grass].tintable = true;
    r.blocks[r.grass].transition_overlay = 1;
    return r;
}
#include "environment_core_tests.h"
#include "cartoon_core_tests.h"
#include "ramp_core_tests.h"
int main() {
    test_environment_features();
    test_cartoon_building();
    test_ramp_connections();
    Registry r = registry();
    Settings s;
    auto gen = std::make_shared<Generator>(s, r, 1337);
    Generator same(s, r, 1337), different(s, r, 7331);
    bool changed = false;
    float min = 1000, max = -1000, plains_min = 1000, plains_max = -1000;
    int plains = 0, mountains = 0, sample_count = 0;
    bool observed[5] = {};
    double desert_slopes = 0, normal_slopes = 0;
    int desert_count = 0, normal_count = 0;
    for (int z = -1500; z <= 1500; z += 100)
        for (int x = -1500; x <= 1500; x += 100) {
            auto a = gen->column(x, z), b = same.column(x, z);
            require(a.height == b.height, "seed deterministic");
            changed |= a.height != different.column(x, z).height;
            min = std::min(min, a.height);
            max = std::max(max, a.height);
            observed[a.biome] = true;
            if (a.biome == 1)
                require(a.surface == r.snow,
                        "snow biome always has a snow surface, including climate transitions");
            mountains += a.mountain > .5f;
            sample_count++;
            auto neighbor = gen->column(x + 8, z);
            if (neighbor.biome == a.biome) {
                if (a.biome == 2) {
                    desert_slopes += std::abs(a.height - neighbor.height);
                    desert_count++;
                }
                if (a.biome == 0) {
                    normal_slopes += std::abs(a.height - neighbor.height);
                    normal_count++;
                }
            }
            if (a.cliff < .1f)
                plains++;
        }
    require(changed, "different seeds differ");
    require(max - min > 60, "dramatic altitude range");
    require(plains > 0, "erosion preserves smooth regions");
    for (bool b : observed)
        require(b, "all five testing biomes are common");
    std::cout << "Mountain strong-mask coverage " << mountains << "/" << sample_count << std::endl;
    require(mountains < double(sample_count) * .35, "mountain ranges are regionally sparse");
    require(desert_count > 0 && normal_count > 0 &&
                desert_slopes / desert_count < normal_slopes / normal_count,
            "desert terrain is flatter than normal terrain");
    Settings high = s, low = s;
    high.base_height = 250;
    low.base_height = 50;
    high.continental_amplitude = low.continental_amplitude = high.mountain_amplitude =
        low.mountain_amplitude = high.detail_amplitude = low.detail_amplitude = high.valley_depth =
            low.valley_depth = 0;
    high.terrace_strength = low.terrace_strength = 0;
    Generator high_gen(high, r, 1337), low_gen(low, r, 1337);
    bool altitude_tested = false;
    for (int z = -1000; z <= 1000 && !altitude_tested; z += 100)
        for (int x = -1000; x <= 1000 && !altitude_tested; x += 100) {
            auto l = low_gen.column(x, z);
            if (l.biome == 0 && l.temperature > .5 && l.temperature < .7) {
                auto h = high_gen.column(x, z);
                require(h.temperature < l.temperature && h.surface == r.snow,
                        "altitude cooling creates snow at high elevation");
                altitude_tested = true;
            }
        }
    require(altitude_tested, "found warm lower biome for altitude test");
    auto edits = std::make_shared<EditMap>();
    (*edits)[{-1, 280, -1}] = r.stone;
    (*edits)[{0, 100, 0}] = r.air;
    auto mesh = build_tile({-32, -32, 0}, 0, *gen, edits);
    require(mesh.volume->at(31, 280 - s.min_y, 31) == r.stone, "placed voxel in negative chunk");
    require(mesh.volume->at(32, 100 - s.min_y, 32) == r.air, "edit reaches neighbor halo");
    for (int z = 0; z < 32; z += 7)
        for (int x = 0; x < 32; x += 7)
            for (int y = -60; y < 300; y += 13) {
                if (x == 31 && z == 31 && y == 280)
                    continue;
                require(mesh.volume->at(x, y - s.min_y, z) == gen->sample(-32 + x, y, -32 + z),
                        "generated volume matches authoritative sampler");
            }
    auto far = build_tile({512, 512, 3}, 0, *gen, edits);
    require(far.texture.empty(),
            "distant LOD uses shared block textures instead of smooth heightfield baking");
    require(!far.solid.vertices.empty() && !far.volume, "far terrain needs no voxel volume");
    for (const auto *surface : {&mesh.solid, &far.solid})
        for (size_t i = 0; i < surface->indices.size(); i += 3) {
            auto a = surface->vertices[surface->indices[i]], b = surface->vertices[surface->indices[i + 1]],
                 c = surface->vertices[surface->indices[i + 2]];
            float ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z, vx = c.x - a.x, vy = c.y - a.y,
                  vz = c.z - a.z;
            float dot = (uy * vz - uz * vy) * a.nx + (uz * vx - ux * vz) * a.ny + (ux * vy - uy * vx) * a.nz;
            require(dot <= .001f, "mesh winding is clockwise relative to normals");
        }
    for (int level = 0; level < 6; level++) {
        auto tile = build_tile({0, 0, level}, 0, *gen, edits);
        for (const auto &v : tile.solid.vertices)
            require(std::abs(v.nx) + std::abs(v.ny) + std::abs(v.nz) == 1 &&
                        (std::abs(v.nx) == 1 || std::abs(v.ny) == 1 || std::abs(v.nz) == 1),
                    "every LOD has only axis-aligned voxel faces");
    }
    Settings flat;
    flat.generator_version = 1;
    flat.min_y = 0;
    flat.max_y = 64;
    flat.base_height = 8;
    flat.continental_amplitude = flat.mountain_amplitude = flat.detail_amplitude = flat.valley_depth =
        flat.cliff_strength = flat.terrace_strength = 0;
    flat.sea_level = 2;
    Registry flat_blocks = r;
    flat_blocks.grass = flat_blocks.stone;
    Generator fg(flat, flat_blocks, 1);
    auto fm = build_tile({0, 0, 0}, 0, fg, std::make_shared<EditMap>());
    require(fm.solid.indices.size() == 12, "greedy mesher collapses flat terrain to top and bottom quads");
    auto tiles = select_tiles(-15, 97, s);
    bool levels[6] = {};
    for (size_t i = 0; i < tiles.size(); i++) {
        levels[tiles[i].level] = true;
        for (size_t j = i + 1; j < tiles.size(); j++)
            require(!overlaps(tiles[i], tiles[j]), "LOD partition has no overlapping tiles");
    }
    for (bool l : levels)
        require(l, "all six LOD levels present");
    for (int z = -800; z < 800; z += 29)
        for (int x = -800; x < 800; x += 29) {
            int covering = 0;
            for (auto &t : tiles)
                if (x >= t.x && x < t.x + t.size() && z >= t.z && z < t.z + t.size())
                    covering++;
            require(covering == 1, "LOD partition has no coverage holes");
        }
    for (int z = -5400; z <= 5400; z += 317)
        for (int x = -5400; x <= 5400; x += 317) {
            float dx = x + 15, dz = z - 97;
            if (dx * dx + dz * dz >= s.view_distance * s.view_distance * .94f * .94f)
                continue;
            int count = 0;
            for (const auto &t : tiles)
                if (x >= t.x && x < t.x + t.size() && z >= t.z && z < t.z + t.size())
                    count++;
            require(count == 1, "terrain coverage extends continuously to the radial fog boundary");
        }
    Settings ocean = s;
    ocean.base_height = 0;
    ocean.sea_level = 80;
    ocean.continental_amplitude = ocean.mountain_amplitude = ocean.detail_amplitude = ocean.valley_depth =
        ocean.cliff_strength = ocean.terrace_strength = 0;
    Generator ocean_gen(ocean, r, 1);
    for (int level = 0; level < 6; level++) {
        auto water = build_tile({0, 0, level}, 0, ocean_gen, std::make_shared<EditMap>());
        bool top = false;
        for (const auto &v : water.water.vertices)
            if (v.ny > .5) {
                top = true;
                require(v.y == 81, "water surface stays at the same height across LOD boundaries");
            }
        require(top, "ocean surface exists at every LOD");
    }
    auto floating = std::make_shared<EditMap>();
    (*floating)[{0, 130, 0}] = r.water;
    auto floating_mesh = build_tile({0, 0, 0}, 0, ocean_gen, floating);
    bool placed_water = false;
    for (const auto &v : floating_mesh.water.vertices)
        if (v.ny > .5 && v.y == 131)
            placed_water = true;
    require(placed_water, "placed water blocks above sea level retain their own voxel geometry");
    auto query = [&](int x, int y, int z) { return x == -2 && y == 2 && z == 0 ? r.stone : r.air; };
    auto hit = raycast({.5, 2.5, .5}, {-1, 0, 0}, 9, query, r);
    require(hit.hit && hit.cell.x == -2 && hit.previous.x == -1,
            "DDA handles negative boundaries and placement neighbor");
    require(!raycast({.5, 2.5, .5}, {0, 1, 0}, 9, query, r).hit, "DDA handles zero direction components");
    WorldModel model(gen);
    std::string error;
    require(model.apply({1, 1, {10, 120, 10}, r.air}, error), "authoritative air removal accepted");
    require(model.block_at(10, 120, 10) == r.air, "air edits override generation");
    require(!model.apply({1, 1, {11, 120, 10}, r.stone}, error), "replayed command rejected");
    require(!model.apply({1, 2, {1, s.min_y, 1}, r.air}, error), "bedrock edits rejected");
    require(!model.apply({1, 2, {1, 100, 1}, 4095}, error), "unknown block rejected");
    auto old_snapshot = model.snapshot();
    require(model.apply({1, 2, {-1, 120, -33}, r.stone}, error), "new authoritative edit accepted");
    require(!old_snapshot->count({-1, 120, -33}), "in-flight snapshots are immutable");
    auto region = model.snapshot_for({-32, -64, 0});
    require(region->size() == 1 && region->at({-1, 120, -33}) == r.stone,
            "regional snapshots include negative-coordinate edits without distant edits");
    Appearance paint;
    paint.painted = true;
    paint.tint = 0xff2266;
    paint.preserve = 450;
    paint.brightness = 1250;
    paint.overlay = 1;
    paint.decoration = 2;
    require(model.apply_surface({1, 3, {-1, 120, -33}, paint}, error),
            "surface command applies without changing base block");
    require(model.block_at(-1, 120, -33) == r.stone, "paint is independent from voxel type");
    auto surfaces = model.surfaces_for({-32, -64, 0});
    require(surfaces->size() == 1 && surfaces->at({-1, 120, -33}) == paint,
            "appearance snapshot is spatially indexed");
    auto painted_mesh = build_tile({-32, -64, 0}, 5, *gen, model.snapshot_for({-32, -64, 0}), surfaces);
    bool colored = false;
    for (const auto &v : painted_mesh.solid.vertices)
        if (v.painted > .5f && v.tr > .99f && std::abs(v.tg - 34.f / 255) < .001f)
            colored = true;
    require(colored, "individual paint reaches mesh shader attributes");
    SurfaceCommand invalid{1, 4, {-1, 120, -33}, paint};
    invalid.appearance.brightness = 2001;
    require(!model.apply_surface(invalid, error), "invalid brightness rejected");
    TerrainWorkers workers(gen, 2);
    workers.submit({0, 0, 3}, 4, model.snapshot());
    MeshResult done;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    bool received = false;
    while (std::chrono::steady_clock::now() < deadline) {
        if (workers.pop(done)) {
            received = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(received && done.version == 4, "background worker returns versioned job");
    std::cout << "PASS " << checks << " checks. Altitude range " << min << ".." << max << "; fine mesh "
              << mesh.milliseconds << " ms; far mesh " << far.milliseconds << " ms; tiles " << tiles.size()
              << ".\n";
}
