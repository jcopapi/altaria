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
            require(shape_intersects(packed, {.49f, .01f, .49f}, {.51f, .09f, .51f}) ==
                        (shape != int(BlockShape::Roof)),
                    "The roof alone leaves the center base open");
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
    wood.texture_layer = 9;
    wood.texture_end_layer = 5;
    require(r.add(wood, error), "wood fixture");
    BlockDef leaves;
    leaves.id = 10;
    leaves.key = "altaria:leaves";
    leaves.foliage = true;
    leaves.texture_layer = 10;
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
    size_t open_grass = 0;
    const auto &grass_volume = *grass_mesh.volume;
    for (int z = 0; z < 32; ++z)
        for (int x = 0; x < 32; ++x)
            for (int y = 0; y < grass_volume.height; ++y)
                if (grass_volume.at(x, y, z) == r.grass && grass_volume.at(x, y + 1, z) == r.air)
                    ++open_grass;
    require(open_grass > 0 && grass_mesh.grass.size() == open_grass * cfg.grass_tufts,
            "every exposed grass block grows the configured dense patch");
    require(std::any_of(grass_mesh.grass.begin(), grass_mesh.grass.end(), [](const GrassInstance &tuft) {
                return std::abs(tuft.lean_x) > .05f || std::abs(tuft.lean_z) > .05f;
            }), "Grass near exposed edges leans outward");
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
    const auto sites = forest.tree_sites(-96, -96, 96, 96, true);
    const auto &tree = sites.front();
    const auto [tx, ty, tz] = tree.base;
    const auto parts = forest.tree_parts(tree);
    bool pillar = false, branch = false, painted_leaf = false;
    for (const auto &part : parts) {
        pillar |= part.first == IVec3{tx, ty + 1, tz} && shape_type(part.shape) == int(BlockShape::Pillar);
        branch |= part.second == wood.id && part.shape == horizontal_mini_pillar(tree.branch_axis);
        painted_leaf |= part.first == IVec3{tx, ty + tree.height + 1, tz} && part.appearance == tree.canopy;
    }
    require(pillar && branch && painted_leaf, "tree geometry and palette are shared by its voxel parts");
    for (int axis : {0, 1}) {
        Surface junction;
        append_block_surface(junction, {0, 0, 0}, branch_fork(axis), wood.id, {}, {}, r);
        const auto has_side = [&](int sign) {
            return std::any_of(junction.vertices.begin(), junction.vertices.end(), [&](const Vertex &v) {
                const float facing = axis == 0 ? v.nx : v.nz;
                const float coordinate = axis == 0 ? v.x : v.z;
                return facing * sign > .99f && coordinate == (sign > 0 ? .75f : .25f) && v.y == 1.f;
            });
        };
        require(has_side(-1) && has_side(1), "Branches retain the exposed trunk faces at both fork orientations");
    }
    unsigned forms = 0;
    int shortest = 99, tallest = 0, fewest_leaves = 999, most_leaves = 0, most_branches = 0;
    for (const auto &site : sites) {
        forms |= 1u << int(site.form);
        shortest = std::min(shortest, site.height);
        tallest = std::max(tallest, site.height);
        int leaves_count = 0, branches = 0;
        const auto site_parts = forest.tree_parts(site);
        std::unordered_set<IVec3, CoordHash> wood_set, leaf_set;
        for (const auto &part : site_parts) {
            leaves_count += part.second == leaves.id;
            branches += part.shape == horizontal_mini_pillar(0) || part.shape == horizontal_mini_pillar(1);
            (part.second == leaves.id ? leaf_set : wood_set).insert(part.first);
        }
        // Every branch ends inside the canopy and no leaf floats on its own.
        constexpr int nx[6] = {-1, 1, 0, 0, 0, 0}, ny[6] = {0, 0, 0, 0, 1, -1}, nz[6] = {0, 0, -1, 1, 0, 0};
        const auto [bx, by, bz] = site.base;
        for (const auto &w : wood_set) {
            int wood_neighbours = 0;
            for (int k = 0; k < 6; ++k)
                wood_neighbours += wood_set.count({w.x + nx[k], w.y + ny[k], w.z + nz[k]}) > 0;
            if (wood_neighbours != 1 || w.y <= by + 1)
                continue;
            for (int k = 0; k < 5; ++k) {
                const IVec3 q{w.x + nx[k], w.y + ny[k], w.z + nz[k]};
                require(wood_set.count(q) || leaf_set.count(q) || q.y >= forest.settings().max_y,
                        "Every branch tip is wrapped by leaves on its sides and top");
            }
            require(std::abs(w.x - bx) < 5 && std::abs(w.z - bz) < 5, "Branches leave room for their leaf cap");
        }
        std::unordered_set<IVec3, CoordHash> reached(wood_set);
        std::vector<IVec3> frontier(wood_set.begin(), wood_set.end());
        while (!frontier.empty()) {
            const auto p = frontier.back();
            frontier.pop_back();
            for (int k = 0; k < 6; ++k) {
                const IVec3 q{p.x + nx[k], p.y + ny[k], p.z + nz[k]};
                if (leaf_set.count(q) && reached.insert(q).second)
                    frontier.push_back(q);
            }
        }
        require(reached.size() == wood_set.size() + leaf_set.size(), "Tree leaves are all attached to the crown");
        fewest_leaves = std::min(fewest_leaves, leaves_count);
        most_leaves = std::max(most_leaves, leaves_count);
        most_branches = std::max(most_branches, branches);
    }
    require(forms == 15u, "Seeded trees include round, tall, open and broad crowns");
    require(tallest - shortest >= 5 && most_leaves > 2 * fewest_leaves && most_branches > 4,
            "Seeded trees vary height, foliage count and branch structure");
    const auto shrubs = forest.shrub_sites(-96, -96, 96, 96, true);
    require(!shrubs.empty(), "Shrubs have a separate population from trees");
    const auto shrub = forest.tree_parts(shrubs.front());
    require(!shrub.empty() && std::all_of(shrub.begin(), shrub.end(), [&](const auto &part) {
                return part.second == leaves.id;
            }), "Generated shrubs contain leaf blocks without trunks");

    auto tree_paint = std::make_shared<AppearanceMap>();
    Appearance canopy_paint = tree.canopy;
    canopy_paint.tint = 0xae3bcc;
    (*tree_paint)[{tx, ty + tree.height + 1, tz}] = canopy_paint;
    const auto proxy_color = unpack_rgb(canopy_paint.tint);
    for (int level : {1, 3}) {
        const int size = 32 << level;
        const TileKey key{int(std::floor(tx / float(size))) * size, int(std::floor(tz / float(size))) * size, level};
        const auto proxy = build_tile(key, 0, forest, std::make_shared<EditMap>(), tree_paint, false);
        int canopy_vertices = 0;
        bool bark = false;
        for (const auto &v : proxy.solid.vertices) {
            canopy_vertices += v.painted > .5f && v.tr == proxy_color.r && v.tg == proxy_color.g && v.tb == proxy_color.b;
            bark |= v.texture == wood.texture_layer;
        }
        require(canopy_vertices >= (level >= 3 ? 20 : 24) && bark == (level == 1), // far crowns drop their underside
                "medium proxy is a stick under painted crown boxes and far proxy is just the boxes");
    }
    // Each form's proxy keeps the real crown's footprint, proportions and lean.
    unsigned proxy_forms = 0;
    float form_aspect[5] = {};
    for (const auto &site : sites) {
        if (proxy_forms & (1u << int(site.form)))
            continue;
        proxy_forms |= 1u << int(site.form);
        float lx0 = 1e9f, lx1 = -1e9f, ly0 = 1e9f, ly1 = -1e9f, lz0 = 1e9f, lz1 = -1e9f, mx = 0, mz = 0;
        int count = 0;
        IVec3 leaf_cell{};
        for (const auto &part : forest.tree_parts(site)) {
            if (part.second != leaves.id)
                continue;
            leaf_cell = part.first;
            lx0 = std::min(lx0, float(part.first.x)); lx1 = std::max(lx1, part.first.x + 1.f);
            ly0 = std::min(ly0, float(part.first.y)); ly1 = std::max(ly1, part.first.y + 1.f);
            lz0 = std::min(lz0, float(part.first.z)); lz1 = std::max(lz1, part.first.z + 1.f);
            mx += part.first.x + .5f; mz += part.first.z + .5f;
            ++count;
        }
        mx /= float(count); mz /= float(count);
        auto paint_one = std::make_shared<AppearanceMap>();
        Appearance marked = site.canopy;
        marked.tint = 0x13d7a1;
        marked.painted = true;
        (*paint_one)[leaf_cell] = marked;
        const auto mark = unpack_rgb(marked.tint);
        const TileKey key{int(std::floor(site.base.x / 64.f)) * 64, int(std::floor(site.base.z / 64.f)) * 64, 1};
        const auto proxy = build_tile(key, 0, forest, std::make_shared<EditMap>(), paint_one, false);
        float px0 = 1e9f, px1 = -1e9f, py0 = 1e9f, py1 = -1e9f, pz0 = 1e9f, pz1 = -1e9f;
        std::vector<const Vertex *> marked_vertices;
        for (const auto &v : proxy.solid.vertices)
            if (v.painted > .5f && v.tr == mark.r && v.tg == mark.g && v.tb == mark.b) {
                marked_vertices.push_back(&v);
                px0 = std::min(px0, v.x + key.x); px1 = std::max(px1, v.x + key.x);
                py0 = std::min(py0, v.y); py1 = std::max(py1, v.y);
                pz0 = std::min(pz0, v.z + key.z); pz1 = std::max(pz1, v.z + key.z);
            }
        require(!marked_vertices.empty(), "every tree form has a proxy");
        const float eps = .01f;
        require(px0 >= lx0 - eps && px1 <= lx1 + eps && pz0 >= lz0 - eps && pz1 <= lz1 + eps &&
                    py1 - py0 <= ly1 - ly0 + eps,
                "proxy crown stays inside the real foliage bounds");
        const float real_aspect = ((lx1 - lx0) + (lz1 - lz0)) * .5f / (ly1 - ly0);
        const float proxy_aspect = ((px1 - px0) + (pz1 - pz0)) * .5f / (py1 - py0);
        require(px1 - px0 >= .6f * (lx1 - lx0) && pz1 - pz0 >= .6f * (lz1 - lz0) && py1 - py0 >= .6f * (ly1 - ly0) &&
                    proxy_aspect > real_aspect * .75f && proxy_aspect < real_aspect * 1.33f,
                "proxy crown keeps the real crown's size and proportions");
        require(std::abs((px0 + px1) * .5f - mx) < 1.f && std::abs((pz0 + pz1) * .5f - mz) < 1.f,
                "proxy crown follows the real crown's lean");
        if (site.form == TreeForm::Spire) {
            float top_width = 0, bottom_width = 0;
            for (const auto *v : marked_vertices) {
                if (v->y == py1)
                    top_width = std::max(top_width, std::abs(v->x + key.x - site.base.x - .5f));
                if (v->y == py0)
                    bottom_width = std::max(bottom_width, std::abs(v->x + key.x - site.base.x - .5f));
            }
            require(top_width < bottom_width * .8f, "spire proxy tiers narrow towards the tip");
        }
        form_aspect[int(site.form)] = proxy_aspect;
    }
    require(proxy_forms == 15u, "proxy shapes were checked for every tree form");
    require(form_aspect[int(TreeForm::Broad)] > 1.5f * form_aspect[int(TreeForm::Spire)],
            "broad proxies stay squat next to slender spire proxies");
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
    paint.shape = 9;
    WorldModel model(std::make_shared<Generator>(cfg, r, 1337));
    require(!model.validate_surface({1, 1, {0, 60, 0}, paint}, error),
            "invalid shape rejected before journal append");
}
