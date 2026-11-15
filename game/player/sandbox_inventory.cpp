#include "sandbox_inventory.h"
#include "game/world_generation/block_shapes.h"
#include <algorithm>

namespace altaria {

SandboxInventory::SandboxInventory() {
    register_defaults();
}

void SandboxInventory::register_defaults() {
    AltMenuDefinition shapes;
    shapes.id = "block_shapes";
    shapes.title_en = "Block Forms";
    shapes.title_pt = "Formas do Bloco";
    shapes.options = {
        {"cube", "Block", "Bloco", "Full solid block", "Bloco inteiro", int(BlockShape::Cube)},
        {"slab", "Floor", "Piso", "Horizontal half slab", "Laje horizontal", int(BlockShape::Slab)},
        {"vertical_slab", "Wall", "Parede", "Vertical half wall", "Parede fina", int(BlockShape::VerticalSlab)},
        {"pillar", "Pillar", "Pilar", "Square structural column", "Coluna estrutural", int(BlockShape::Pillar)},
        {"mini_pillar", "Mini Pillar", "Minipilar", "Slender post / beam", "Poste fino", int(BlockShape::MiniPillar)},
        {"stairs", "Stairs", "Escada", "Stepped stair block", "Degrau de escada", int(BlockShape::Stairs)},
        {"ramp", "Ramp", "Rampa", "Smooth sloped wedge", "Rampa inclinada", int(BlockShape::Ramp)},
        {"roof", "Roof", "Telhado", "Pitched roof slope", "Telhado inclinado", int(BlockShape::Roof)},
    };
    register_alt_menu(std::move(shapes));

    AltMenuDefinition wand;
    wand.id = "wand_modes";
    wand.title_en = "World Wand";
    wand.title_pt = "Varinha do Mundo";
    wand.options = {
        {"select", "Select Area", "Selecionar Área",
         "LMB sets corner 1, RMB sets corner 2 (or V / B)",
         "Esq marca o 1º canto, Dir marca o 2º canto (ou V / B)",
         int(WandMode::Select)},
        {"fill", "Fill", "Preencher",
         "Fills the area: LMB applies, RMB copies block, T chooses",
         "Preenche a área: Esq aplica, Dir copia bloco, T escolhe",
         int(WandMode::Fill)},
        {"replace", "Replace", "Substituir",
         "Swaps blocks in area: LMB applies, RMB sets target, T chooses",
         "Troca blocos na área: Esq aplica, Dir define alvo, T escolhe",
         int(WandMode::Replace)},
        {"walls", "Walls", "Paredes",
         "Builds only outer walls: LMB applies, RMB copies block, T chooses",
         "Cria só as 4 paredes: Esq aplica, Dir copia bloco, T escolhe",
         int(WandMode::Walls)},
        {"hollow", "Hollow", "Oco",
         "Builds a hollow shell: LMB applies, RMB copies block, T chooses",
         "Cria caixa oca por dentro: Esq aplica, Dir copia, T escolhe",
         int(WandMode::Hollow)},
        {"copy_paste", "Copy / Paste", "Copiar / Colar",
         "LMB copies the area, RMB pastes at aim, Q rotates 90°",
         "Esq copia a área, Dir cola onde mirar, Q gira 90°",
         int(WandMode::CopyPaste)},
        {"stack", "Stack", "Empilhar",
         "LMB repeats the area toward aim; RMB clears the selection",
         "Esq repete a área na direção da mira; Dir limpa a seleção",
         int(WandMode::Stack)},
        {"erase", "Clear / Undo", "Limpar / Desfazer",
         "LMB clears blocks in the area; RMB undoes last action (Ctrl+Z)",
         "Esq apaga os blocos da área; Dir desfaz a última ação (Ctrl+Z)",
         int(WandMode::ClearUndo)},
    };
    register_alt_menu(std::move(wand));
}

void SandboxInventory::clear() {
    for (auto &s : slots)
        s.clear();
    selected_slot = 0;
}

void SandboxInventory::register_alt_menu(AltMenuDefinition definition) {
    alt_menus[definition.id] = std::move(definition);
}

const AltMenuDefinition *SandboxInventory::find_alt_menu(const std::string &id) const {
    auto it = alt_menus.find(id);
    return it == alt_menus.end() ? nullptr : &it->second;
}

bool SandboxInventory::collect_block(BlockId block, int amount, const std::string &alt_menu, int *out_slot) {
    if (block == 0 || amount <= 0)
        return false;
    for (int i = 0; i < HOTBAR_SLOT_COUNT; ++i) {
        if (slots[i].has_block() && slots[i].block == block) {
            slots[i].count += amount;
            if (out_slot)
                *out_slot = i;
            return true;
        }
    }
    for (int i = 0; i < HOTBAR_SLOT_COUNT; ++i) {
        if (slots[i].empty()) {
            slots[i].block = block;
            slots[i].tool_id.clear();
            slots[i].count = amount;
            slots[i].shape_id = int(BlockShape::Cube);
            slots[i].alt_menu_id = alt_menu;
            if (out_slot)
                *out_slot = i;
            return true;
        }
    }
    return false;
}

bool SandboxInventory::give_tool(const std::string &tool_id, int *out_slot) {
    if (tool_id.empty())
        return false;
    int existing = find_tool_slot(tool_id);
    if (existing >= 0) {
        if (out_slot)
            *out_slot = existing;
        return true;
    }
    for (int i = 0; i < HOTBAR_SLOT_COUNT; ++i) {
        if (slots[i].empty()) {
            slots[i].block = 0;
            slots[i].tool_id = tool_id;
            slots[i].count = 1;
            slots[i].shape_id = 0;
            slots[i].alt_menu_id = (tool_id == "world_wand") ? "wand_modes" : std::string();
            if (out_slot)
                *out_slot = i;
            return true;
        }
    }
    return false;
}

int SandboxInventory::give_and_select(BlockId block, int min_count, const std::string &alt_menu) {
    if (block == 0)
        return selected_slot;
    int existing = find_slot(block);
    if (existing >= 0) {
        slots[existing].count = std::max(slots[existing].count, min_count);
        selected_slot = existing;
        return existing;
    }
    int target = slots[selected_slot].empty() ? selected_slot : -1;
    if (target < 0) {
        for (int i = 0; i < HOTBAR_SLOT_COUNT; ++i) {
            if (slots[i].empty()) {
                target = i;
                break;
            }
        }
    }
    if (target < 0) {
        for (int i = 0; i < HOTBAR_SLOT_COUNT; ++i) {
            if (!slots[i].is_tool()) {
                target = i;
                break;
            }
        }
    }
    if (target < 0)
        target = selected_slot;
    slots[target].block = block;
    slots[target].tool_id.clear();
    slots[target].count = std::max(1, min_count);
    slots[target].shape_id = int(BlockShape::Cube);
    slots[target].alt_menu_id = alt_menu;
    selected_slot = target;
    return target;
}

void SandboxInventory::set_active_shape(int shape) {
    if (slots[selected_slot].has_block() || !slots[selected_slot].alt_menu_id.empty())
        slots[selected_slot].shape_id = shape;
}

bool SandboxInventory::consume_selected(int amount, bool *out_emptied) {
    if (out_emptied)
        *out_emptied = false;
    if (amount <= 0)
        return true;
    auto &s = slots[selected_slot];
    if (s.is_tool())
        return true;
    if (s.empty() || s.count < amount)
        return false;
    s.count -= amount;
    if (s.count <= 0) {
        s.clear();
        if (out_emptied)
            *out_emptied = true;
    }
    return true;
}

void SandboxInventory::select_slot(int index) {
    selected_slot = std::clamp(index, 0, HOTBAR_SLOT_COUNT - 1);
}

void SandboxInventory::cycle_slot(int delta) {
    selected_slot = ((selected_slot + delta) % HOTBAR_SLOT_COUNT + HOTBAR_SLOT_COUNT) % HOTBAR_SLOT_COUNT;
}

const HotbarSlot &SandboxInventory::slot(int index) const {
    static const HotbarSlot kEmpty{};
    if (index < 0 || index >= HOTBAR_SLOT_COUNT)
        return kEmpty;
    return slots[index];
}

const HotbarSlot &SandboxInventory::active_slot() const {
    return slots[selected_slot];
}

int SandboxInventory::find_slot(BlockId block) const {
    for (int i = 0; i < HOTBAR_SLOT_COUNT; ++i) {
        if (slots[i].has_block() && slots[i].block == block)
            return i;
    }
    return -1;
}

int SandboxInventory::find_tool_slot(const std::string &tool_id) const {
    for (int i = 0; i < HOTBAR_SLOT_COUNT; ++i) {
        if (slots[i].is_tool() && slots[i].tool_id == tool_id)
            return i;
    }
    return -1;
}

} // namespace altaria
