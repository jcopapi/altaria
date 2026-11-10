#include "game/world_generation/block_shapes.h"
#include "game/world_generation/environment_core.h"
static void test_environment_features() {
    using P = std::array<float, 3>;
    for (int shape = 0; shape < 8; shape++)
        for (int rotation = 0; rotation < 4; rotation++) {
            uint8_t packed = shape | (rotation << 3);
            require(shape_id(shape_name(shape)) == shape, "shape names round-trip");
            auto hulls = shape_hulls(packed);
            require(!hulls.empty() && hulls.size() <= 2, "one or two convex hulls per block shape");
            for (auto &hull : hulls)
                for (auto &p : hull)
                    for (float c : p)
                        require(c >= 0 && c <= 1, "all shape vertices remain within cell");
            require(shape_intersects(packed, {.49f, .01f, .49f}, {.51f, .09f, .51f}),
                    "all shapes include center base");
            float d = shape_ray(packed, {.5f, 2, .5f}, {0, -1, 0}, 4);
            require(std::abs(d - (2 - shape_top(packed, .5f, .5f))) < .0001f,
                    "ray and geometry height agree in every rotation");
        }
    require(!shape_intersects(1, {0, .6f, 0}, {1, .9f, 1}), "slab has no invisible full-cube collision");
    require(shape_ray(1, {-1, .75f, .5f}, {1, 0, 0}, 4) < 0,
            "horizontal ray crosses empty upper half of slab");
    require(shape_ray(2, {.75f, 2, .5f}, {0, -1, 0}, 4) < 0, "vertical slab ray respects empty side");
    require(std::abs(shape_top(3, .25f, .5f) - .25f) < .001f, "ramp is a continuous slope");
    require(shape_top(4, .25f, .5f) == .5f && shape_top(4, .75f, .5f) == 1, "stairs have two actual treads");
    auto r = registry();
    auto query = [&](int x, int y, int z) -> BlockId {
        if (y == 0 && z == 0 && x == 0)
            return r.grass;
        if (y == 0 && z == 0 && x == 1)
            return r.stone;
        return r.air;
    };
    auto hit = raycast_shapes(
        {-1, .75f, .5f}, {1, 0, 0}, 4, query, [](int x, int, int) -> uint8_t { return x == 0 ? 1 : 0; }, r);
    require(hit.hit && hit.cell.x == 1, "DDA passes through empty part of slab and hits following cube");
    Settings cfg;
    for (auto &b : cfg.biomes) {
        b.day_c = 30;
        b.night_c = 10;
    }
    BlockDef ice;
    ice.id = 8;
    ice.key = "altaria:ice";
    ice.thermal_delta = -10;
    ice.thermal_radius = 4;
    std::string error;
    require(r.add(ice, error), "thermal block fixture");
    Generator gen(cfg, r, 1337);
    EnvironmentSettings env;
    auto empty = [](int, int, int) -> BlockId { return 0; };
    auto day = temperature_at(gen, env, {0, 42, 0}, 14, Weather::Clear, 0, empty);
    auto night = temperature_at(gen, env, {0, 42, 0}, 2, Weather::Clear, 0, empty);
    require(std::abs(day.ambient - 30) < .001 && std::abs(night.ambient - 10) < .001,
            "independent biome day/night Celsius values");
    auto high = temperature_at(gen, env, {0, 1042, 0}, 14, Weather::Clear, 0, empty);
    require(std::abs((day.ambient - high.ambient) - 6.5f) < .001,
            "altitude applies configured Celsius lapse rate");
    auto storm = temperature_at(gen, env, {0, 42, 0}, 14, Weather::Storm, 2, empty);
    require(storm.ambient == day.ambient + env.storm_delta && storm.feels_like < storm.ambient,
            "weather and wind affect gameplay temperature");
    auto cold = [](int x, int y, int z) -> BlockId { return x == 0 && y == 42 && z == 0 ? 8 : 0; };
    auto close = temperature_at(gen, env, {.5f, 42.5f, .5f}, 14, Weather::Clear, 0, cold);
    auto far = temperature_at(gen, env, {10, 42.5f, .5f}, 14, Weather::Clear, 0, cold);
    require(close.blocks == -10 && far.blocks == 0, "cold block has bounded radial influence");
    auto wall = temperature_at(gen, env, {.5f, 42.5f, .5f}, 14, Weather::Clear, 0,
                               [](int, int, int) -> BlockId { return 8; });
    require(wall.blocks >= -10, "many cold blocks cannot accumulate unbounded temperature");
    auto wrap_a = temperature_at(gen, env, {0, 42, 0}, 0, Weather::Clear, 0, empty);
    auto wrap_b = temperature_at(gen, env, {0, 42, 0}, 24, Weather::Clear, 0, empty);
    require(std::abs(wrap_a.ambient - wrap_b.ambient) < .0001, "temperature continuous at midnight");
    BlockDef wood;
    wood.id = 9;
    wood.key = "altaria:wood";
    wood.wood = true;
    require(r.add(wood, error), "wood fixture");
    BlockDef leaves;
    leaves.id = 10;
    leaves.key = "altaria:leaves";
    leaves.foliage = true;
    require(r.add(leaves, error), "leaf fixture");
    cfg.vegetation = true;
    cfg.surface_shapes = true;
    for (auto &b : cfg.biomes)
        b.tree_density = .8f;
    Generator forest(cfg, r, 1337), same(cfg, r, 1337);
    auto trees = forest.trees(-96, -96, 96, 96);
    require(!trees.empty(), "deterministic voxel tree population");
    require(trees == same.trees(-96, -96, 96, 96), "tree placement independent of stream order");
    EditMap tree_cells;
    for (auto &part : trees)
        tree_cells[part.first] = part.second;
    int observed = 0;
    for (auto &part : tree_cells)
        if (part.second == wood.id && ++observed < 8)
            require(forest.sample(part.first.x, part.first.y, part.first.z) == wood.id,
                    "unloaded voxel query agrees with tree generation");
    auto mesh = build_tile({-32, -32, 0}, 0, forest, std::make_shared<EditMap>());
    require(!mesh.volume->shapes.empty(), "surface rounding generates real half-height caps");
    auto grass_mesh = build_tile({256, 128, 0}, 0, forest, std::make_shared<EditMap>());
    require(grass_mesh.grass.empty() && grass_mesh.bushes.vertices.empty() && grass_mesh.bush_count == 0,
            "3D grass tufts and bushes are removed");
    auto stack_edits = std::make_shared<EditMap>();
    (*stack_edits)[{4, 290, 4}] = r.dirt;
    (*stack_edits)[{4, 291, 4}] = r.grass;
    (*stack_edits)[{8, 290, 4}] = r.grass;
    (*stack_edits)[{8, 291, 4}] = r.grass;
    auto transition_mesh = build_tile({0, 0, 0}, 0, forest, stack_edits);
    bool grass_block_textured = false, dirt_transition_side = false, grass_under_grass_clean = true;
    for (const auto &v : transition_mesh.solid.vertices) {
        if (v.x >= 4 && v.x <= 5 && v.z >= 4 && v.z <= 5 && v.y >= 291 && v.y <= 292) {
            grass_block_textured |= (v.texture == 3 && v.overlay < 0 && v.painted > .5f && v.overlay_tint > .5f);
        }
        if (v.x >= 4 && v.x <= 5 && v.z >= 4 && v.z <= 5 && v.y >= 290 && v.y <= 291 && std::abs(v.ny) < .5f) {
            dirt_transition_side |= (v.overlay == 2 && v.overlay_tint > .5f && v.painted < .5f);
        }
        if (v.x >= 8 && v.x <= 9 && v.z >= 4 && v.z <= 5 && v.y >= 290 && v.y <= 291 && std::abs(v.ny) < .5f) {
            if (v.overlay >= 0)
                grass_under_grass_clean = false;
        }
    }
    require(grass_block_textured, "grass block uses grass top texture on its own faces without self-overlay");
    require(dirt_transition_side, "block directly below grass receives the grass side transition overlay");
    require(grass_under_grass_clean, "transition overlay only applies to a different connected block below");
    auto far_mesh = build_tile({-256, -256, 3}, 0, forest, std::make_shared<EditMap>());
    require(far_mesh.grass.empty(), "far terrain has no grass geometry cost");
    for (auto &part : tree_cells)
        if (part.first.x >= -32 && part.first.x < 0 && part.first.z >= -32 && part.first.z < 0) {
            auto p = part.first;
            auto id = mesh.volume->at(p.x + 32, p.y - cfg.min_y, p.z + 32);
            if (forest.terrain_sample(p.x, p.y, p.z) == r.air)
                require(id == part.second, "voxel volume and unloaded tree query agree");
        }
    auto shaped = std::make_shared<AppearanceMap>();
    Appearance paint;
    paint.shape = 4;
    paint.rotation = 3;
    paint.painted = true;
    paint.tint = 0xcc33ff;
    paint.overlay = 1;
    (*shaped)[{0, 290, 0}] = paint;
    auto edits = std::make_shared<EditMap>();
    (*edits)[{0, 290, 0}] = r.stone;
    auto shaped_mesh = build_tile({0, 0, 0}, 0, forest, edits, shaped);
    bool tint = false, top_grass = false, side = false;
    for (auto &v : shaped_mesh.solid.vertices)
        if (v.y >= 290 && v.y <= 291 && v.x <= 1 && v.z <= 1) {
            tint |= v.painted > 0 && std::abs(v.tr - .8f) < .001;
            side |= v.overlay == 2;
        }
    for (auto &v : transition_mesh.solid.vertices)
        if (v.x >= 4 && v.x <= 5 && v.z >= 4 && v.z <= 5 && v.y >= 291 && v.y <= 292 && v.ny > .5f)
            top_grass |= v.texture == 3;
    require(tint && top_grass && side, "paint, grass block top texture, and side transition layers survive stairs and rotation");
    auto path = std::filesystem::temp_directory_path() / "altaria-shapes-surface.log";
    std::filesystem::remove(path);
    {
        std::ofstream legacy(path);
        legacy << "1 1 0 290 0 16777215 350 1000 65535 65535 0 1875088176103671054\n";
    }
    SurfaceJournal journal(path.string());
    AppearanceMap legacy_surfaces;
    require(journal.load(legacy_surfaces, r, cfg, error) && legacy_surfaces.at({0, 290, 0}).shape == 255,
            "original unversioned paint journal remains readable");
    require(journal.append({1, 1, {0, 290, 0}, paint}, error), "shape surface persisted");
    AppearanceMap restored;
    require(journal.load(restored, r, cfg, error) && restored.at({0, 290, 0}) == paint,
            "shape rotation paint and overlay persist together");
    std::filesystem::remove(path);
    paint.shape = 9;
    WorldModel model(std::make_shared<Generator>(cfg, r, 1337));
    require(!model.validate_surface({1, 1, {0, 60, 0}, paint}, error),
            "invalid shape rejected before journal append");
}
