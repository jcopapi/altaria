#pragma once
#include "game/world_generation/voxel_core.h"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace altaria {

struct LiquidReaction {
    std::string with_liquid;
    std::string result_block_key = "altaria:stone";
    BlockId result_block = 0;
};

struct LiquidDef {
    int id = 0;
    std::string key = "water";
    std::string block_key = "altaria:water";
    BlockId block_id = 0;
    std::string name = "Water";
    std::string name_pt = "Água";
    float viscosity = 1.0f;
    int tick_delay = 1;
    uint8_t max_spread_rate = 64;
    float current_push_strength = 9.5f;
    float density = 1000.0f;
    float damage_per_second = 0.0f;
    std::vector<LiquidReaction> reactions;
};

// Each dynamic cell in the simulation tracks liquid type, volume (0..255), and settled sleep state
struct LiquidCell {
    BlockId type = 0;
    uint8_t amount = 0;    // 0 = empty, 255 = full block
    bool settled = true;   // true = sleeping (zero CPU)
    uint8_t sleep_ticks = 0;
    float flow_x = 0.0f;
    float flow_y = 0.0f;
    float flow_z = 0.0f;
};

class LiquidSimulation {
    Registry registry;
    std::unordered_map<BlockId, LiquidDef> liquid_defs;
    std::unordered_map<std::string, BlockId> liquid_keys;
    
    // Sparse map for dynamic / modified liquid cells
    std::unordered_map<IVec3, LiquidCell, CoordHash> dynamic_cells;
    
    // Active queue of unsettled cells to process
    std::vector<IVec3> active_queue;
    std::unordered_set<IVec3, CoordHash> in_active_queue;
    
    uint64_t tick_counter = 0;
    
    std::function<BlockId(int, int, int)> world_query;
    std::function<void(int, int, int, BlockId)> world_edit;
    std::function<bool(int,int,int)> static_liquid;
    std::shared_ptr<const Generator> generator;

    static constexpr uint8_t MAX_LIQUID = 255;
    static constexpr uint8_t MIN_LIQUID = 1;

    uint8_t calculate_vertical_flow(uint8_t remaining, uint8_t bottom_amount, uint8_t max_spread) const;

public:
    LiquidSimulation();
    
    void configure(const Registry &reg, std::shared_ptr<const Generator> gen,
                   std::function<BlockId(int, int, int)> query,
                   std::function<void(int, int, int, BlockId)> edit,
                   std::function<bool(int,int,int)> settled_reservoir={});
                   
    bool load_definitions_json(const std::string &json_str);
    
    const LiquidDef *get_def(BlockId type) const;
    const LiquidDef *get_def_by_key(const std::string &key) const;
    bool is_liquid(BlockId type) const;
    
    // Query cell state
    uint8_t get_amount(int x, int y, int z) const;
    BlockId get_type(int x, int y, int z) const;
    bool is_settled(int x, int y, int z) const;
    
    // Placement / removal:
    void set_cell(int x, int y, int z, BlockId type, uint8_t amount, bool settled = false);
    void remove_cell(int x, int y, int z);
    
    // Waking up cells into active queue:
    void wake_up(int x, int y, int z);
    void wake_neighbors(int x, int y, int z);
    void wake_region(IVec3 minimum, IVec3 maximum);
    
    // Step simulation: cellular automata tick
    void step(float delta, int max_updates_per_step = 2048);
    
    // Query liquid current / force at 3D world position (combines CA flow with river current)
    std::array<float, 3> current_at(float x, float y, float z) const;
    
    // Check if an AABB intersects liquid and compute submergence
    bool intersects_liquid(float min_x, float min_y, float min_z,
                           float max_x, float max_y, float max_z,
                           BlockId &out_type, float &out_submergence) const;

    size_t active_count() const { return active_queue.size(); }
    size_t dynamic_count() const { return dynamic_cells.size(); }
    int total_dynamic_volume() const;
    uint64_t ticks() const { return tick_counter; }
};

} // namespace altaria
