#include "altaria_game.h"
#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/font.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/item_list.hpp>
#include <godot_cpp/classes/line_edit.hpp>
#include <godot_cpp/classes/panel_container.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/classes/theme.hpp>
#include <godot_cpp/classes/v_box_container.hpp>

namespace godot {
namespace {
Ref<StyleBoxFlat> cheat_style(Color background, Color border, int margin) {
    Ref<StyleBoxFlat> style;
    style.instantiate();
    style->set_bg_color(background);
    style->set_border_color(border);
    style->set_border_width_all(1);
    for (Side side : {SIDE_LEFT, SIDE_TOP, SIDE_RIGHT, SIDE_BOTTOM})
        style->set_content_margin(side, margin);
    return style;
}
}

void AltariaGame::build_cheat_inventory() {
    const bool pt = language == "pt_BR";
    auto panel = memnew(PanelContainer);
    cheat_inventory = panel;
    panel->set_name("CheatInventory");
    overlay->add_child(panel);
    panel->set_anchors_and_offsets_preset(Control::PRESET_CENTER);
    panel->set_offset(SIDE_LEFT, -220);
    panel->set_offset(SIDE_RIGHT, 220);
    panel->set_offset(SIDE_TOP, -188);
    panel->set_offset(SIDE_BOTTOM, 188);
    panel->set_texture_filter(CanvasItem::TEXTURE_FILTER_NEAREST);
    panel->hide();

    Ref<Theme> theme;
    theme.instantiate();
    theme->set_default_font(ResourceLoader::get_singleton()->load(
        "res://addons/crabbyconsole/fonts/roboto/static/RobotoMono-Regular.ttf"));
    theme->set_default_font_size(14);
    const Color shell(.26, .28, .25), edge(.49, .51, .47), inset(.15, .17, .15);
    theme->set_stylebox("panel", "PanelContainer", cheat_style(shell, edge, 10));
    theme->set_stylebox("panel", "ItemList", cheat_style(inset, edge, 6));
    theme->set_stylebox("normal", "LineEdit", cheat_style(inset, edge, 6));
    theme->set_stylebox("normal", "Button", cheat_style(Color(.34, .36, .33), edge, 6));
    theme->set_constant("v_separation", "ItemList", 6);
    panel->set_theme(theme);

    auto content = memnew(VBoxContainer);
    content->add_theme_constant_override("separation", 8);
    panel->add_child(content);
    auto header = memnew(HBoxContainer);
    content->add_child(header);
    auto title = memnew(Label);
    cheat_title = title;
    title->set_text("Build Mode");
    title->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    header->add_child(title);
    auto close = memnew(Button);
    close->set_text("×");
    close->set_tooltip_text(pt ? "Fechar (Esc)" : "Close (Esc)");
    close->connect("pressed", callable_mp(this, &AltariaGame::close_cheat_inventory));
    header->add_child(close);

    cheat_search = memnew(LineEdit);
    cheat_search->set_name("BlockSearch");
    cheat_search->set_placeholder(pt ? "Buscar bloco por nome ou ID…" : "Search by block name or ID…");
    cheat_search->set_clear_button_enabled(true);
    cheat_search->connect("text_changed", callable_mp(this, &AltariaGame::refresh_cheat_blocks));
    content->add_child(cheat_search);
    cheat_blocks = memnew(ItemList);
    cheat_blocks->set_name("BlockResults");
    cheat_blocks->set_v_size_flags(Control::SIZE_EXPAND_FILL);
    cheat_blocks->set_fixed_icon_size(Vector2i(24, 24));
    cheat_blocks->set_allow_reselect(true);
    cheat_blocks->connect("item_selected", callable_mp(this, &AltariaGame::select_cheat_block));
    content->add_child(cheat_blocks);
    cheat_empty = memnew(Label);
    cheat_empty->set_text(pt ? "Nenhum bloco encontrado." : "No matching blocks.");
    content->add_child(cheat_empty);
}

bool AltariaGame::cheat_inventory_open() const {
    return cheat_inventory && cheat_inventory->is_visible();
}

void AltariaGame::show_cheat_inventory() {
    cheat_picker_purpose = CheatPickerPurpose::Normal;
    if (cheat_title)
        cheat_title->set_text("Build Mode");
    cheat_search->clear();
    refresh_cheat_blocks("");
    cheat_inventory->show();
    if (selection)
        selection->hide();
    set_game_input(false);
    cheat_search->grab_focus();
    cheat_search->edit();
}

void AltariaGame::show_wand_block_picker(CheatPickerPurpose purpose) {
    cheat_picker_purpose = purpose;
    const bool pt = language == "pt_BR";
    if (cheat_title) {
        if (purpose == CheatPickerPurpose::WandReplaceFrom)
            cheat_title->set_text(String::utf8(pt ? "Replace (1/2) — Qual bloco substituir?"
                                                  : "Replace (1/2) — Replace which block?"));
        else if (purpose == CheatPickerPurpose::WandReplaceTo)
            cheat_title->set_text(String::utf8(pt ? "Replace (2/2) — Substituir por qual?"
                                                  : "Replace (2/2) — Replace with which?"));
        else
            cheat_title->set_text(String::utf8(pt ? "Varinha — Selecionar Bloco"
                                                  : "Wand — Pick Block"));
    }
    cheat_search->clear();
    refresh_cheat_blocks("");
    cheat_inventory->show();
    if (selection)
        selection->hide();
    set_game_input(false);
    cheat_search->grab_focus();
    cheat_search->edit();
}

void AltariaGame::close_cheat_inventory() {
    cheat_picker_purpose = CheatPickerPurpose::Normal;
    if (!cheat_inventory_open())
        return;
    cheat_search->release_focus();
    cheat_inventory->hide();
    set_game_input(world && !is_console_open());
}

void AltariaGame::refresh_cheat_blocks(const String &query) {
    cheat_blocks->clear();
    if (!world)
        return;
    const auto &registry = world->registry();
    const String filter = query.strip_edges().to_lower();
    if (cheat_picker_purpose == CheatPickerPurpose::Normal) {
        struct ToolEntry {
            const char *en;
            const char *pt;
            const char *key;
            int meta;
        };
        constexpr ToolEntry tools[3] = {
            {"Stone Pickaxe", "Picareta de Pedra", "altaria:stone_pickaxe", -1},
            {"Stone Axe", "Machado de Pedra", "altaria:stone_axe", -2},
            {"World Wand", "Varinha do Mundo", "altaria:world_wand", -3},
        };
        for (const auto &t : tools) {
            const String en(t.en), pt(t.pt), key(t.key);
            if (!filter.is_empty() && !en.to_lower().contains(filter) && !pt.to_lower().contains(filter) &&
                !key.to_lower().contains(filter))
                continue;
            const String name = language == "pt_BR" ? pt : en;
            const int row = cheat_blocks->add_item(name + String("  ·  ") + key);
            cheat_blocks->set_item_metadata(row, t.meta);
        }
    } else {
        if (cheat_picker_purpose == CheatPickerPurpose::WandReplaceFrom) {
            const String en = "Any solid block (*)";
            const String pt = String::utf8("Qualquer bloco sólido (*)");
            if (filter.is_empty() || en.to_lower().contains(filter) || pt.to_lower().contains(filter) ||
                filter == "*") {
                const int row = cheat_blocks->add_item((language == "pt_BR" ? pt : en) + String::utf8("  ·  *"));
                cheat_blocks->set_item_metadata(row, -100);
            }
        }
        const String air_en = "Air", air_pt = "Ar", air_key = "altaria:air";
        if (filter.is_empty() || air_en.to_lower().contains(filter) || air_pt.to_lower().contains(filter) ||
            air_key.contains(filter)) {
            const int row = cheat_blocks->add_item(
                (language == "pt_BR" ? air_pt : air_en) + String::utf8("  ·  ") + air_key);
            cheat_blocks->set_item_metadata(row, -101);
        }
    }
    for (const auto &block : registry.blocks) {
        if (block.key.empty() || !block.placeable || block.id == registry.air)
            continue;
        const String english(block.name.c_str()), portuguese(block.name_pt.c_str()), key(block.key.c_str());
        if (!filter.is_empty() && !english.to_lower().contains(filter) &&
            !portuguese.to_lower().contains(filter) && !key.to_lower().contains(filter) &&
            String::num_int64(block.id) != filter)
            continue;
        Ref<Texture2D> icon;
        if (block.texture_layer >= 0 && block.texture_layer < int(registry.textures.size())) {
            const auto &path = registry.textures[block.texture_layer].path;
            if (!path.empty())
                icon = ResourceLoader::get_singleton()->load(String(path.c_str()));
        }
        const String name = language == "pt_BR" ? portuguese : english;
        const int row = cheat_blocks->add_item(name + String("  ·  ") + key, icon);
        cheat_blocks->set_item_metadata(row, int(block.id));
    }
    cheat_empty->set_text(language == "pt_BR" ? "Nenhum bloco encontrado." : "No matching blocks.");
    cheat_empty->set_visible(cheat_blocks->get_item_count() == 0);
}

void AltariaGame::select_cheat_block(int index) {
    if (!cheat_inventory_open() || index < 0 || index >= cheat_blocks->get_item_count())
        return;
    const int meta = int(cheat_blocks->get_item_metadata(index));
    // Keep the click that chose the block from reaching world interaction.
    get_viewport()->set_input_as_handled();
    if (cheat_picker_purpose != CheatPickerPurpose::Normal && world) {
        const auto &registry = world->registry();
        const bool is_any = (meta == -100);
        const bool is_air = (meta == -101);
        const altaria::BlockId bid = (meta > 0) ? altaria::BlockId(meta) : registry.air;
        const String bname = is_any
            ? String("*")
            : (is_air ? String(language == "pt_BR" ? "Ar" : "Air")
                      : String::utf8((language == "pt_BR" ? registry.get(bid).name_pt : registry.get(bid).name).c_str()));

        if (cheat_picker_purpose == CheatPickerPurpose::WandReplaceFrom) {
            wand_replace_target = is_any || is_air ? 0 : bid;
            wand_replace_target_is_air = is_air;
            refresh_wand_hud();
            show_wand_block_picker(CheatPickerPurpose::WandReplaceTo);
            return;
        }
        wand_material = is_air ? registry.air : bid;
        wand_material_is_air = is_air;
        refresh_wand_hud();
        if (arc_hotbar) {
            const String msg = (cheat_picker_purpose == CheatPickerPurpose::WandReplaceTo)
                ? (String(language == "pt_BR" ? "Substituir -> " : "Replace -> ") + bname)
                : (String("Brush: ") + bname);
            arc_hotbar->show_wand_feedback(msg);
        }
        close_cheat_inventory();
        return;
    }
    if (meta < 0) {
        int slot_idx = -1;
        const char *tid = (meta == -1) ? "pickaxe" : ((meta == -2) ? "axe" : "world_wand");
        if (!inventory.give_tool(tid, &slot_idx)) {
            cheat_empty->set_text(language == "pt_BR" ? "Inventário cheio." : "Inventory is full.");
            cheat_empty->show();
            return;
        }
        if (slot_idx >= 0) {
            select_hotbar_slot(slot_idx);
            if (arc_hotbar)
                arc_hotbar->trigger_pickup_anim(slot_idx);
        }
        close_cheat_inventory();
        return;
    }
    const altaria::BlockId id = altaria::BlockId(meta);
    if (!collect_block(id, 64)) {
        cheat_empty->set_text(language == "pt_BR" ? "Inventário cheio." : "Inventory is full.");
        cheat_empty->show();
        return;
    }
    close_cheat_inventory();
}

String AltariaGame::cheat_inventory_command(const std::vector<std::string> &args) {
    if (args.size() > 2)
        return "Usage: buildmode [on|off]";
    if (args.size() == 2) {
        const String value = String(args[1].c_str()).to_lower();
        if (value != "on" && value != "off")
            return "Usage: buildmode [on|off]";
        cheat_enabled = value == "on";
    } else {
        cheat_enabled = !cheat_enabled;
    }
    if (!cheat_enabled)
        close_cheat_inventory();
    if (arc_hotbar)
        arc_hotbar->set_unlimited_blocks(cheat_enabled);
    return cheat_enabled ? "Build mode: ON. Press T in-game." : "Build mode: OFF.";
}
}
