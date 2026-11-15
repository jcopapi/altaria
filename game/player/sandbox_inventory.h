#pragma once
#include "game/world_generation/voxel_core.h"
#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace altaria {

constexpr int HOTBAR_SLOT_COUNT = 9;

enum class WandMode : int {
    Select = 0,
    Fill = 1,
    Replace = 2,
    Walls = 3,
    Hollow = 4,
    CopyPaste = 5,
    Stack = 6,
    ClearUndo = 7,
};

struct HotbarSlot {
    BlockId block = 0;
    std::string tool_id;
    int count = 0;
    int shape_id = 0;
    std::string alt_menu_id = "block_shapes";

    bool is_tool() const {
        return !tool_id.empty() && count > 0;
    }
    bool has_block() const {
        return block != 0 && tool_id.empty() && count > 0;
    }
    bool empty() const {
        return (block == 0 && tool_id.empty()) || count <= 0;
    }
    void clear() {
        block = 0;
        tool_id.clear();
        count = 0;
        shape_id = 0;
        alt_menu_id = "block_shapes";
    }
};

struct AltMenuOption {
    std::string id;
    std::string label_en;
    std::string label_pt;
    std::string desc_en;
    std::string desc_pt;
    int shape_id = -1;
};

struct AltMenuDefinition {
    std::string id;
    std::string title_en;
    std::string title_pt;
    std::vector<AltMenuOption> options;
};

// Manages the 9-slot sandbox hotbar and extensible Alt radial sub-menus.
class SandboxInventory {
    std::array<HotbarSlot, HOTBAR_SLOT_COUNT> slots{};
    int selected_slot = 0;
    std::unordered_map<std::string, AltMenuDefinition> alt_menus;

    void register_defaults();

  public:
    SandboxInventory();

    void clear();
    void register_alt_menu(AltMenuDefinition definition);
    const AltMenuDefinition *find_alt_menu(const std::string &id) const;

    // Collects a full block into a matching stack or the first free hotbar slot.
    bool collect_block(BlockId block, int amount = 1, const std::string &alt_menu = "block_shapes",
                       int *out_slot = nullptr);

    // Places a tool ("pickaxe", "axe", or "world_wand") into a matching or free hotbar slot.
    bool give_tool(const std::string &tool_id, int *out_slot = nullptr);

    // Grants/selects a block from debug commands or the debug tools into the 9-slot hotbar.
    int give_and_select(BlockId block, int min_count = 64, const std::string &alt_menu = "block_shapes");

    bool consume_selected(int amount = 1, bool *out_emptied = nullptr);

    int selected() const {
        return selected_slot;
    }
    void select_slot(int index);
    void cycle_slot(int delta);

    const HotbarSlot &slot(int index) const;
    const HotbarSlot &active_slot() const;
    void set_active_shape(int shape);
    int active_shape() const {
        return active_slot().shape_id;
    }
    bool has_active_block() const {
        return active_slot().has_block();
    }
    bool has_active_tool() const {
        return active_slot().is_tool();
    }
    std::string active_tool() const {
        return active_slot().is_tool() ? active_slot().tool_id : std::string();
    }
    int find_slot(BlockId block) const;
    int find_tool_slot(const std::string &tool_id) const;
};

} // namespace altaria
