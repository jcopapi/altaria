#include "game/world_generation/construction_core.h"
static void test_cartoon_building() {
    using Point = altaria::Point;
    auto r = registry();
    Settings settings;
    ConstructionStore store;
    std::string error;
    auto empty = [](int, int, int) { return std::make_pair(BlockId(0), uint8_t(0)); };
    for (int shape = 0; shape < 8; shape++)
        for (int turn = 0; turn < 4; turn++) {
            uint8_t value = shape | (turn << 3);
            require(shapes_overlap(value, {0, 220, 0}, value, {0, 220, 0}),
                    "shape SAT detects identical intersecting shapes");
            require(!shapes_overlap(value, {0, 220, 0}, value, {2, 220, 0}),
                    "shape SAT accepts separated forms");
            require(!shapes_overlap(value, {0, 220, 0}, value, {0, 221, 0}),
                    "touching horizontal faces are not intersections");
        }
    auto origin = snap_placement({.5f, 220.5f, .5f}, {0, 1, 0}, 0, {0, 220, 0});
    require(origin == IVec3{0, 1764, 0}, "cube origin follows real slab top without a half-cell gap");
    auto mini = snap_placement({.625f, 220.5f, .5f}, {1, 0, 0}, 0, {0, 220, 0});
    require(mini.x == 5, "mini-pillar side attachment preserves eighth-unit contact");
    BuiltBlock first{1, {-8, 1760, -264}, r.grass, Appearance()};
    first.appearance.shape = 1;
    BuiltBlock second = first;
    second.id = 2;
    second.origin8.y += 4;
    second.block = r.stone;
    require(store.can_place(first, r, settings, empty, error), "first slab placement valid");
    store.put(first);
    require(store.can_place(second, r, settings, empty, error),
            "second material slab can occupy upper half of the same terrain cell");
    store.put(second);
    require(store.blocks.size() == 2, "independent pieces share a cell without replacing each other");
    require(store.collides({-.75f, 220.7f, -32.75f}, {-.25f, 220.8f, -32.25f}, r),
            "player query sees upper fractional slab");
    require(!store.collides({-.75f, 221.01f, -32.75f}, {-.25f, 221.1f, -32.25f}, r),
            "space above stacked slabs remains empty");
    BuiltBlock duplicate = second;
    duplicate.id = 3;
    require(!store.can_place(duplicate, r, settings, empty, error),
            "actual intersecting pieces are rejected");
    auto third = second;
    third.id = 3;
    third.origin8.y += 4;
    third.appearance.shape = 0;
    require(store.can_place(third, r, settings, empty, error),
            "full cube stacks directly on two independent slabs");
    store.put(third);
    store.erase(2);
    require(store.blocks.count(1) && store.blocks.count(3) && !store.blocks.count(2),
            "removal changes only selected piece");
    auto terrain_slab = [&](int x, int y, int z) {
        return std::make_pair(x == 0 && y == 220 && z == 0 ? r.grass : r.air, uint8_t(1));
    };
    BuiltBlock block{4, {0, 1764, 0}, r.stone, Appearance()};
    block.appearance.shape = 0;
    require(store.can_place(block, r, settings, terrain_slab, error),
            "fractional piece can overlap voxel cell bounds without intersecting its slab geometry");
    block.origin8.y = 1763;
    require(!store.can_place(block, r, settings, terrain_slab, error),
            "penetration into procedural slab is rejected");
    auto fogs = default_biomes();
    require(fogs[2].fog_end < fogs[0].fog_end * .2f && fogs[1].fog_end < fogs[0].fog_end * .2f,
            "desert and snow fog occlude closer than meadow");
    Settings cfg;
    BiomeStage stage(cfg, 1337);
    bool seen[5] = {};
    for (int z = -1200; z <= 1200; z += 64)
        for (int x = -1200; x <= 1200; x += 64) {
            auto sample = stage.regions({float(x)}, {float(z)})[0];
            if (sample.primary_weight < .999f)
                continue;
            seen[sample.primary] = true;
            auto day = biome_fog(cfg, 1337, float(x), float(z), 1),
                 night = biome_fog(cfg, 1337, float(x), float(z), 0);
            auto &b = cfg.biomes[sample.primary];
            require(std::abs(day.color.r - b.fog_day.r) < .001f &&
                        std::abs(night.color.b - b.fog_night.b) < .001f,
                    "fog follows biome day/night palette");
            require(std::abs(day.end - (b.fog_end * sample.primary_weight +
                                        cfg.biomes[sample.secondary].fog_end * (1 - sample.primary_weight))) <
                        .01f,
                    "fog follows configured biome visibility");
        }
    for (bool value : seen)
        require(value, "all fog biome profiles sampled");
}
