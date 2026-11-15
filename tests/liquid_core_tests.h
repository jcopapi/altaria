#pragma once
#include "game/world_generation/liquid/liquid_system.h"
#include "game/world_generation/voxel_core.h"
#include <iostream>
#include <map>
#include <vector>

static void test_liquid_system() {
    auto r = registry();
    // Ensure lava block exists in test registry
    if (r.lava == 0) {
        BlockDef lava_block;
        lava_block.id = 13;
        lava_block.key = "altaria:lava";
        lava_block.name = "Lava";
        lava_block.solid = false;
        lava_block.liquid = true;
        lava_block.lava = true;
        std::string err;
        r.add(lava_block, err);
        r.lava = 13;
    }

    Settings cfg;
    auto gen = std::make_shared<Generator>(cfg, r, 1337);

    // Grid mock for backing world queries and edits in isolated simulation
    std::map<std::tuple<int, int, int>, BlockId> mock_world;
    auto query = [&](int x, int y, int z) -> BlockId {
        auto it = mock_world.find({x, y, z});
        return it != mock_world.end() ? it->second : r.air;
    };
    auto edit = [&](int x, int y, int z, BlockId b) {
        mock_world[{x, y, z}] = b;
    };

    LiquidSimulation sim;
    sim.configure(r, gen, query, edit);

    // 1. Data-driven liquid definitions
    const LiquidDef *water_def = sim.get_def(r.water);
    require(water_def != nullptr, "Water liquid definition must be registered");
    require(water_def->viscosity == 1.0f, "Water has standard viscosity 1.0");
    require(water_def->tick_delay == 1, "Water tick delay is 1");
    require(water_def->max_spread_rate == 64, "Water spread rate is 64");
    require(water_def->current_push_strength >= 9.0f, "Water current has strong push strength");

    const LiquidDef *lava_def = sim.get_def(r.lava);
    require(lava_def != nullptr, "Lava liquid definition must be registered");
    require(lava_def->viscosity > water_def->viscosity, "Lava is more viscous than water");
    require(lava_def->tick_delay > water_def->tick_delay, "Lava flows slower (higher tick delay) than water");
    require(lava_def->max_spread_rate < water_def->max_spread_rate, "Lava spread rate is lower than water");
    require(lava_def->damage_per_second > 0.0f, "Lava deals damage");

    // 2. Downward gravity flow and mass conservation
    // Place solid floor at y = 0 and containment walls at edges
    for (int x = -5; x <= 5; ++x) {
        for (int z = -5; z <= 5; ++z) {
            mock_world[{x, 0, z}] = r.stone;
            if (std::abs(x) == 5 || std::abs(z) == 5) {
                for (int y = 1; y <= 4; ++y)
                    mock_world[{x, y, z}] = r.stone;
            }
        }
    }
    // Place 255 units of water at (0, 3, 0)
    sim.set_cell(0, 3, 0, r.water, 255, false);
    require(sim.get_amount(0, 3, 0) == 255, "Initial water placed at y=3");
    require(sim.active_count() > 0, "Unsettled water enters active queue");

    // Step simulation: water should fall downwards toward y=1 (above stone floor at y=0)
    for (int step = 0; step < 10; ++step) {
        sim.step(0.05f);
    }
    require(sim.get_amount(0, 3, 0) < 255, "Water has drained from top cell");
    require(sim.get_amount(0, 1, 0) > 0, "Water reached the floor at y=1");

    int total_volume = sim.total_dynamic_volume();
    require(total_volume == 255, "Mass conservation holds in vertical flow and spreading");

    // 3. Water vs Lava flow speed difference
    // Fresh simulation for comparison
    LiquidSimulation speed_sim;
    std::map<std::tuple<int, int, int>, BlockId> shaft_world;
    auto shaft_query = [&](int x, int y, int z) -> BlockId {
        auto it = shaft_world.find({x, y, z});
        return it != shaft_world.end() ? it->second : r.air;
    };
    auto shaft_edit = [&](int x, int y, int z, BlockId b) { shaft_world[{x, y, z}] = b; };
    speed_sim.configure(r, gen, shaft_query, shaft_edit);

    // Water shaft at (0, y, 0), Lava shaft at (10, y, 0)
    speed_sim.set_cell(0, 5, 0, r.water, 255, false);
    speed_sim.set_cell(10, 5, 0, r.lava, 255, false);

    // Run 1 tick: water moves on tick 1 (delay 1), lava stays put on tick 1 (delay 4)
    speed_sim.step(0.05f);
    require(speed_sim.get_amount(0, 4, 0) > 0, "Water flows downwards on tick 1");
    require(speed_sim.get_amount(10, 4, 0) == 0, "Lava waits due to viscosity / tick delay 4");

    // 4. Contact Reaction: Water + Lava solidifies into Stone
    LiquidSimulation react_sim;
    std::map<std::tuple<int, int, int>, BlockId> react_world;
    auto react_query = [&](int x, int y, int z) -> BlockId {
        auto it = react_world.find({x, y, z});
        return it != react_world.end() ? it->second : r.air;
    };
    auto react_edit = [&](int x, int y, int z, BlockId b) { react_world[{x, y, z}] = b; };
    react_sim.configure(r, gen, react_query, react_edit);

    // Place water at (0, 1, 0) and lava at (1, 1, 0) on stone floor at y=0
    react_world[{0, 0, 0}] = r.stone;
    react_world[{1, 0, 0}] = r.stone;
    react_sim.set_cell(0, 1, 0, r.water, 255, false);
    react_sim.set_cell(1, 1, 0, r.lava, 255, false);

    for (int step = 0; step < 10; ++step) {
        react_sim.step(0.05f);
    }
    // One or both contact cells should turn to stone
    bool stone_created = (react_world.count({0, 1, 0}) && react_world[{0, 1, 0}] == r.stone) ||
                         (react_world.count({1, 1, 0}) && react_world[{1, 1, 0}] == r.stone);
    require(stone_created, "Water and lava contact reaction creates stone");

    // 5. Settled Sleep State (Zero CPU when static)
    LiquidSimulation sleep_sim;
    std::map<std::tuple<int, int, int>, BlockId> basin_world;
    auto basin_query = [&](int x, int y, int z) -> BlockId {
        auto it = basin_world.find({x, y, z});
        return it != basin_world.end() ? it->second : r.air;
    };
    auto basin_edit = [&](int x, int y, int z, BlockId b) { basin_world[{x, y, z}] = b; };
    sleep_sim.configure(r, gen, basin_query, basin_edit);

    // Build a 1x1 basin enclosed on 5 sides
    basin_world[{0, 0, 0}] = r.stone; // bottom
    basin_world[{1, 1, 0}] = r.stone; // east
    basin_world[{-1, 1, 0}] = r.stone; // west
    basin_world[{0, 1, 1}] = r.stone; // north
    basin_world[{0, 1, -1}] = r.stone; // south

    sleep_sim.set_cell(0, 1, 0, r.water, 255, false);
    require(sleep_sim.active_count() == 1, "Basin water begins active");

    // Run steps until it settles
    for (int step = 0; step < 15; ++step) {
        sleep_sim.step(0.05f);
    }
    require(sleep_sim.is_settled(0, 1, 0), "Contained water enters settled sleep state");
    require(sleep_sim.active_count() == 0, "Active queue empty for settled liquid (zero CPU)");

    // Waking up neighbor
    sleep_sim.wake_neighbors(0, 1, 0);
    require(sleep_sim.active_count() > 0, "Waking neighbor reactivates cell");

    // 6. World Generation: No mountain-climbing water & procedural river currents
    LiquidSimulation world_sim;
    world_sim.configure(r, gen, [gen](int x, int y, int z) { return gen->sample(x, y, z); }, nullptr);

    bool found_mountain = false;
    bool found_river = false;
    for (int z = -1000; z <= 1000; z += 50) {
        for (int x = -1000; x <= 1000; x += 50) {
            auto col = gen->column(x, z);
            if (col.mountain > 0.6f && col.height > cfg.sea_level + 20) {
                found_mountain = true;
                // Sample 3D voxels up the mountain: no water must exist above sea_level
                for (int y = cfg.sea_level + 1; y <= int(col.height) + 5; ++y) {
                    BlockId b = gen->sample(x, y, z);
                    require(b != r.water, "No water climbs mountains above sea level");
                }
            }
            if (col.river > 0.45f && col.height < float(cfg.sea_level - 1)) {
                int water_y = int(col.height) + 1;
                if (gen->sample(x, water_y, z) == r.water) {
                    found_river = true;
                    auto [fx, fz] = gen->river_current(x, z);
                    float mag = std::sqrt(fx * fx + fz * fz);
                    require(mag > 0.0f, "River channel has active downstream flow vector");
                    // Check that liquid simulation current_at queries this river current
                    auto curr = world_sim.current_at(float(x), float(water_y), float(z));
                    float cur_mag = std::sqrt(curr[0] * curr[0] + curr[2] * curr[2]);
                    require(cur_mag > 0.0f, "Liquid current incorporates river flow vector");
                }
            }
        }
    }
    require(found_mountain, "Found mountain peak for waterfall check");
    require(found_river, "Found river channel for downstream current check");

    std::cout << "PASS liquid system tests: Cellular Automata, Viscosity, Reaction, Sleep, and River Currents.\n";
}
