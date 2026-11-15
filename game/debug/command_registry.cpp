#include "command_registry.h"
#include "native/src/altaria_game.h"
#include "native/src/crab_console_bridge.h"
#include "game/world_generation/voxel_world.h"
#include "game/player/flying_player.h"
#include "game/world_generation/world_atmosphere.h"
#include "game/world_generation/physics_blocks.h"
#include "game/world_generation/block_shapes.h"
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>

namespace altaria {

std::string CommandRegistry::permission_to_string(CommandPermission p) {
    switch (p) {
        case CommandPermission::Safe: return "Safe";
        case CommandPermission::Dev: return "Dev";
        case CommandPermission::Cheat: return "Cheat";
    }
    return "Unknown";
}

bool CommandRegistry::string_to_permission(const std::string &str, CommandPermission &out) {
    std::string s = CommandParser::to_lower(CommandParser::trim(str));
    if (s == "safe" || s == "0") {
        out = CommandPermission::Safe;
        return true;
    }
    if (s == "dev" || s == "developer" || s == "1") {
        out = CommandPermission::Dev;
        return true;
    }
    if (s == "cheat" || s == "cheats" || s == "all" || s == "2") {
        out = CommandPermission::Cheat;
        return true;
    }
    return false;
}

CommandRegistry &CommandRegistry::get_singleton() {
    static CommandRegistry instance;
    return instance;
}

CommandRegistry::CommandRegistry() {
    register_all_commands();
}

void CommandRegistry::register_command(const CommandDef &def) {
    auto ptr = std::make_shared<CommandDef>(def);
    commands[def.id] = ptr;
    for (const auto &alias : def.aliases) {
        alias_map[alias] = def.id;
    }
}

const CommandDef *CommandRegistry::find_command(const std::string &name) const {
    std::string lower = CommandParser::to_lower(name);
    auto it = commands.find(lower);
    if (it != commands.end()) {
        return it->second.get();
    }
    auto ait = alias_map.find(lower);
    if (ait != alias_map.end()) {
        auto cit = commands.find(ait->second);
        if (cit != commands.end()) {
            return cit->second.get();
        }
    }
    return nullptr;
}

const CommandDef *CommandRegistry::find_command_or_parent(const std::string &name) const {
    const CommandDef *cmd = find_command(name);
    if (cmd) return cmd;
    auto tokens = CommandParser::tokenize(name);
    if (!tokens.empty()) {
        cmd = find_command(tokens[0]);
    }
    return cmd;
}

void CommandRegistry::add_history(const std::string &cmd) {
    if (cmd.empty()) return;
    if (!history.empty() && history.back() == cmd) return;
    history.push_back(cmd);
    if (history.size() > 100) {
        history.erase(history.begin());
    }
}

std::vector<std::string> CommandRegistry::autocomplete(const std::string &prefix) const {
    std::vector<std::string> results;
    std::string p = CommandParser::to_lower(CommandParser::trim(prefix));
    if (p.empty()) return results;

    static const std::vector<std::string> subcommands = {
        "chunk info", "chunk list", "chunk queue", "chunk stats", "chunk reload",
        "world seed", "world info", "world new", "world new default", "world new plain", "world new structure",
        "world new biome",
        "buildmode on", "buildmode off", "bm on", "bm off", "structure pos1", "structure pos2", "structure clear", "structure fill",
        "item pickaxe", "item axe", "item world_wand", "item clear",
        "resolution list", "resolution 1920x1080", "resolution 1600x900", "resolution 1280x720",
        "structure fill air", "structure replace", "structure walls", "structure hollow",
        "structure copy", "structure paste", "structure stack", "structure undo", "structure export",
        "light blue", "light place", "light follow", "light follow on", "light follow off", "log export 30s",
        "biome info", "biome list", "biome normal", "biome snow", "biome desert", "biome jungle", "biome enchanted",
        "time get", "time set 12", "time pause", "time resume",
        "weather get", "weather set clear", "weather set rain", "weather set storm",
        "wind 0", "wind 1.0", "wind 2.0",
        "temperature", "spawnpoint get", "spawnpoint set", "tp spawn",
        "debug overlay on", "debug overlay off", "debug perf on", "debug perf off",
        "debug world on", "debug world off", "debug chunks on", "debug chunks off",
        "debug physics on", "debug physics off",
        "perf on", "perf off",
        "benchmark help", "benchmark run gen", "benchmark run flight", "benchmark run teleport", "benchmark report",
        "permission safe", "permission dev", "permission cheat",
        "fly on", "fly off", "noclip on", "noclip off", "god on", "god off",
        "fullbright on", "fullbright off", "outline on", "outline off", "pixelation on", "pixelation off", "godrays on", "godrays off",
        "fog biome",
        "shape cube", "shape slab", "shape vertical_slab", "shape ramp", "shape stairs", "shape roof", "shape pillar", "shape mini_pillar", "shape terrain",
        "physics clear",
        "cave environment", "cave entrances", "cave tp 0", "cave tp 1", "cave tp 2", "cave tp 3"
    };

    for (const auto &kv : commands) {
        if (kv.first.rfind(p, 0) == 0) {
            results.push_back(kv.first);
        }
    }
    for (const auto &kv : alias_map) {
        if (kv.first.rfind(p, 0) == 0) {
            if (std::find(results.begin(), results.end(), kv.first) == results.end()) {
                results.push_back(kv.first);
            }
        }
    }
    for (const auto &sc : subcommands) {
        if (sc.rfind(p, 0) == 0) {
            if (std::find(results.begin(), results.end(), sc) == results.end()) {
                results.push_back(sc);
            }
        }
    }
    if (p.rfind("world new b", 0) == 0) {
        for (const char *b : {"world new biome normal", "world new biome snow", "world new biome desert",
                              "world new biome jungle", "world new biome enchanted"}) {
            if (std::string(b).rfind(p, 0) == 0 && std::find(results.begin(), results.end(), b) == results.end()) {
                results.push_back(b);
            }
        }
    }
    std::sort(results.begin(), results.end());
    return results;
}

std::vector<std::string> CommandRegistry::suggest(const std::string &input, int max_suggestions) const {
    std::string lower = CommandParser::to_lower(CommandParser::trim(input));
    std::vector<std::pair<int, std::string>> scored;
    for (const auto &kv : commands) {
        int dist = CommandParser::levenshtein_distance(lower, kv.first);
        if (dist <= 3) {
            scored.push_back({dist, kv.first});
        }
    }
    for (const auto &kv : alias_map) {
        int dist = CommandParser::levenshtein_distance(lower, kv.first);
        if (dist <= 3) {
            scored.push_back({dist, kv.first});
        }
    }
    std::sort(scored.begin(), scored.end(), [](const auto &a, const auto &b) {
        return a.first < b.first;
    });

    std::vector<std::string> out;
    for (const auto &item : scored) {
        if (std::find(out.begin(), out.end(), item.second) == out.end()) {
            out.push_back(item.second);
            if ((int)out.size() >= max_suggestions) break;
        }
    }
    return out;
}

std::vector<const CommandDef *> CommandRegistry::get_all_commands() const {
    std::vector<const CommandDef *> out;
    for (const auto &kv : commands) {
        out.push_back(kv.second.get());
    }
    std::sort(out.begin(), out.end(), [](const CommandDef *a, const CommandDef *b) {
        return a->id < b->id;
    });
    return out;
}

std::vector<const CommandDef *> CommandRegistry::get_commands_in_category(const std::string &category) const {
    std::vector<const CommandDef *> out;
    for (const auto &kv : commands) {
        if (kv.second->category == category) {
            out.push_back(kv.second.get());
        }
    }
    std::sort(out.begin(), out.end(), [](const CommandDef *a, const CommandDef *b) {
        return a->id < b->id;
    });
    return out;
}

std::string CommandRegistry::execute(godot::AltariaGame *game, const std::string &line) {
    std::string trimmed = CommandParser::trim(line);
    if (trimmed.empty()) return "";

    if (trimmed.front() == '/' || trimmed.front() == ':') {
        trimmed = CommandParser::trim(trimmed.substr(1));
    }
    if (trimmed.empty()) return "";

    add_history(trimmed);

    auto tokens = CommandParser::tokenize(trimmed);
    if (tokens.empty()) return "";

    std::string verb = CommandParser::to_lower(tokens[0]);

    // Check compound 2-word commands first (e.g. "physics stats", "render stats", "chunk reload")
    const CommandDef *cmd = nullptr;
    if (tokens.size() >= 2) {
        std::string compound = verb + " " + CommandParser::to_lower(tokens[1]);
        cmd = find_command(compound);
        if (cmd) {
            tokens.erase(tokens.begin(), tokens.begin() + 2);
            tokens.insert(tokens.begin(), compound);
        }
    }
    if (!cmd) {
        cmd = find_command(verb);
    }

    if (!cmd) {
        auto suggestions = suggest(verb, 3);
        std::string err = "Unknown command: '" + verb + "'.";
        if (!suggestions.empty()) {
            err += " Did you mean: ";
            for (size_t i = 0; i < suggestions.size(); i++) {
                if (i > 0) err += ", ";
                err += "'" + suggestions[i] + "'";
            }
            err += "?";
        } else {
            err += " Type 'help' for available commands.";
        }
        return err;
    }

    // Check permissions
    if (cmd->permission > current_permission) {
        return "Permission denied: '" + cmd->id + "' requires " + permission_to_string(cmd->permission) +
               " mode (current: " + permission_to_string(current_permission) + "). Use 'permission " +
               permission_to_string(cmd->permission) + "' to unlock.";
    }

    return cmd->handler(game, tokens);
}

void CommandRegistry::register_all_commands() {
    // ==========================================
    // CATEGORY: GENERAL
    // ==========================================
    register_command({
        "help", "General", "Display list of commands or help for a specific command.",
        "help [command]", CommandPermission::Safe, {"?"},
        [this](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            if (args.size() > 1) {
                std::string target = CommandParser::to_lower(args[1]);
                const CommandDef *c = args.size() > 2
                    ? find_command(target + " " + CommandParser::to_lower(args[2])) : nullptr;
                if (!c)
                    c = find_command(target);
                if (!c) {
                    return "Command '" + args[1] + "' not found. Type 'help' for full list.";
                }
                std::ostringstream ss;
                ss << "Command: " << c->id << "\n"
                   << "Category: " << c->category << "\n"
                   << "Usage: " << c->usage << "\n"
                   << "Permission: " << permission_to_string(c->permission) << "\n"
                   << "Description: " << c->description;
                if (!c->aliases.empty()) {
                    ss << "\nAliases: ";
                    for (size_t i = 0; i < c->aliases.size(); i++) {
                        if (i > 0) ss << ", ";
                        ss << c->aliases[i];
                    }
                }
                return ss.str();
            }

            std::ostringstream ss;
            ss << "Commands\n";
            std::vector<std::string> categories = {"General", "Player", "World", "Chunks", "Performance", "Visual", "Tools"};
            for (const auto &cat : categories) {
                auto list = get_commands_in_category(cat);
                if (list.empty()) continue;
                ss << "\n[" << cat << "]:\n  ";
                for (size_t i = 0; i < list.size(); i++) {
                    if (i > 0) ss << ", ";
                    ss << list[i]->id;
                }
            }
            ss << "\n\nType 'help <command>' for detailed usage.";
            return ss.str();
        }
    });

    register_command({
        "clear", "General", "Clear console history text.",
        "clear", CommandPermission::Safe, {"cls"},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            if (game && game->get_console()) {
                game->get_console()->clear_output();
            }
            return "Console cleared.";
        }
    });

    register_command({
        "history", "General", "Show recent command history.",
        "history", CommandPermission::Safe, {},
        [this](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            if (history.empty()) return "Command history is empty.";
            std::ostringstream ss;
            ss << "Recent commands:\n";
            for (size_t i = 0; i < history.size(); i++) {
                ss << " " << (i + 1) << ": " << history[i] << "\n";
            }
            return ss.str();
        }
    });

    register_command({
        "echo", "General", "Print text back to console.",
        "echo <text>", CommandPermission::Safe, {},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            if (args.size() < 2) return "";
            std::string msg;
            for (size_t i = 1; i < args.size(); i++) {
                if (i > 1) msg += " ";
                msg += args[i];
            }
            return msg;
        }
    });

    register_command({
        "version", "General", "Print engine, game, and subsystem version info.",
        "version", CommandPermission::Safe, {"ver"},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            std::ostringstream ss;
            ss << "Altaria Sandbox RPG v0.5.1 (Godot 4.7.1 GDExtension C++)\n"
               << "Physics: Jolt Physics 3D\n"
               << "Terrain: FastNoise2 1.1.1 (SIMD Voxel Terrain Extraction)";
            return ss.str();
        }
    });

    register_command({
        "quit", "General", "Exit game safely.",
        "quit", CommandPermission::Safe, {"exit"},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            if (game && game->get_tree()) {
                game->quit_game();
                return "Exiting...";
            }
            return "Unable to quit.";
        }
    });

    register_command({
        "permission", "General", "Get or set command permission mode (Safe, Dev, Cheat).",
        "permission [safe|dev|cheat]", CommandPermission::Safe, {"perm"},
        [this](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            if (args.size() == 1) {
                return "Current permission mode: " + permission_to_string(current_permission);
            }
            CommandPermission next;
            if (!string_to_permission(args[1], next)) {
                return "Usage: permission <safe|dev|cheat>";
            }
            set_permission(next);
            return "Permission mode changed to: " + permission_to_string(next);
        }
    });

    // ==========================================
    // CATEGORY: PLAYER
    // ==========================================
    register_command({
        "noclip", "Player", "Toggle or set noclip collision mode (N keybind when enabled).",
        "noclip [on|off]", CommandPermission::Cheat, {},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            if (!game || !game->get_player()) return "No player active.";
            auto *p = game->get_player();
            bool val = !p->is_noclip();
            bool explicit_off = false;
            if (args.size() >= 2) {
                if (!CommandParser::parse_bool(args[1], val)) {
                    return "Usage: noclip [on|off]";
                }
                explicit_off = !val;
            }
            p->set_noclip(val);
            if (val && !p->is_flying()) {
                p->set_flying(true);
            }
            game->set_noclip_hotkey_enabled(!explicit_off);
            return "Noclip: " + std::string(val ? "ON" : "OFF") +
                   (!explicit_off ? " (Press N to toggle)" : "");
        }
    });

    register_command({
        "fly", "Player", "Toggle or set flying mode (F keybind when enabled).",
        "fly [on|off]", CommandPermission::Safe, {},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            if (!game || !game->get_player()) return "No player active.";
            auto *p = game->get_player();
            bool val = !p->is_flying();
            bool explicit_off = false;
            if (args.size() >= 2) {
                if (!CommandParser::parse_bool(args[1], val)) {
                    return "Usage: fly [on|off]";
                }
                explicit_off = !val;
            }
            p->set_flying(val);
            game->set_fly_hotkey_enabled(!explicit_off);
            return "Flight mode: " + std::string(val ? "ON" : "OFF") +
                   (!explicit_off ? " (Press F to toggle)" : "");
        }
    });

    register_command({
        "freecam", "Player", "Toggle freecam / free fly mode.",
        "freecam [on|off]", CommandPermission::Dev, {},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            if (!game || !game->get_player()) return "No player active.";
            auto *p = game->get_player();
            bool val = !p->is_flying() || !p->is_noclip();
            if (args.size() >= 2) {
                if (!CommandParser::parse_bool(args[1], val)) {
                    return "Usage: freecam [on|off]";
                }
            }
            p->set_flying(val);
            p->set_noclip(val);
            return "Freecam: " + std::string(val ? "ON" : "OFF");
        }
    });

    register_command({
        "tp", "Player", "Teleport player to coordinates or spawn.",
        "tp <x> <y> <z> | tp spawn", CommandPermission::Cheat, {"teleport"},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            if (!game || !game->get_player()) return "No player active.";
            if (args.size() == 2 && CommandParser::to_lower(args[1]) == "spawn") {
                auto meta = game->get_game_metadata();
                godot::Array sp = meta.get("spawn", godot::Array());
                if (sp.size() == 3) {
                    game->get_player()->set_position(godot::Vector3(sp[0], sp[1], sp[2]));
                    return "Teleported to world spawnpoint.";
                }
                game->get_player()->set_position(godot::Vector3(0, 100, 0));
                return "Teleported to default spawn (0, 100, 0).";
            }
            if (args.size() != 4) {
                return "Usage: tp <x> <y> <z> | tp spawn";
            }
            float x, y, z;
            if (!CommandParser::parse_float(args[1], x) ||
                !CommandParser::parse_float(args[2], y) ||
                !CommandParser::parse_float(args[3], z)) {
                return "Invalid coordinates. Numbers required.";
            }
            auto *world = game->get_world();
            if (!world) return "No world loaded.";
            if (std::abs(x) > 999990 || std::abs(z) > 999990 || y < world->settings().min_y + 1 || y > 3000) {
                return "Coordinates outside world limits.";
            }
            game->get_player()->set_position(godot::Vector3(x, y, z));
            return "Teleported to (" + std::to_string((int)x) + ", " + std::to_string((int)y) + ", " + std::to_string((int)z) + ").";
        }
    });

    register_command({
        "pos", "Player", "Show current player position and orientation.",
        "pos", CommandPermission::Safe, {"where", "coords"},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            if (!game || !game->get_player()) return "No player active.";
            auto p = game->get_player()->get_position();
            auto rot = game->get_player()->get_rotation();
            int cx = (int)std::floor(p.x / 32.0f);
            int cz = (int)std::floor(p.z / 32.0f);
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(2);
            ss << "Position: X=" << p.x << ", Y=" << p.y << ", Z=" << p.z << "\n"
               << "Chunk: (" << cx << ", " << cz << ")\n"
               << "Yaw: " << rot.y << " rad, Pitch: " << game->get_player()->get_pitch() << " rad";
            return ss.str();
        }
    });

    register_command({
        "speed", "Player", "Get or set flight speed.",
        "speed [value 1..150]", CommandPermission::Cheat, {},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            if (!game || !game->get_player()) return "No player active.";
            if (args.size() == 1) {
                return "Current flight speed: " + std::to_string((int)game->get_player()->get_speed());
            }
            float sp = 0;
            if (!CommandParser::parse_float(args[1], sp) || sp < 1.0f || sp > 150.0f) {
                return "Usage: speed <1..150>";
            }
            game->get_player()->set_speed(sp);
            return "Flight speed set to: " + std::to_string((int)sp);
        }
    });

    register_command({
        "gravity", "Player", "Get or set player gravity.",
        "gravity [value 0..100]", CommandPermission::Cheat, {},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            if (!game || !game->get_player()) return "No player active.";
            if (args.size() == 1) {
                return "Current gravity: " + std::to_string((int)game->get_player()->get_gravity());
            }
            float g = 0;
            if (!CommandParser::parse_float(args[1], g) || g < 0.0f || g > 100.0f) {
                return "Usage: gravity <0..100>";
            }
            game->get_player()->set_gravity(g);
            return "Gravity set to: " + std::to_string((int)g);
        }
    });

    register_command({
        "god", "Player", "Toggle player god mode.",
        "god [on|off]", CommandPermission::Cheat, {"godmode"},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            if (!game || !game->get_player()) return "No player active.";
            bool val = !game->get_player()->is_god();
            if (args.size() >= 2) {
                if (!CommandParser::parse_bool(args[1], val)) {
                    return "Usage: god [on|off]";
                }
            }
            game->get_player()->set_god(val);
            return "God mode: " + std::string(val ? "ON" : "OFF");
        }
    });

    register_command({
        "resetplayer", "Player", "Reset player debug parameters to default values.",
        "resetplayer", CommandPermission::Safe, {},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            if (!game || !game->get_player()) return "No player active.";
            game->get_player()->reset_debug_parameters();
            game->set_fly_hotkey_enabled(false);
            game->set_noclip_hotkey_enabled(false);
            return "Player debug parameters restored to defaults.";
        }
    });

    // ==========================================
    // CATEGORY: WORLD
    // ==========================================
    register_command({
        "world", "World", "World management commands (seed, info, new).",
        "world seed | world info | world new [default|plain|structure] [biome <key>] [seed]", CommandPermission::Safe, {},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            if (!game) return "No game active.";
            if (args.size() < 2) return "Usage: world seed | world info | world new [default|plain|structure] [biome <key>] [seed]";
            std::string sub = CommandParser::to_lower(args[1]);
            if (sub == "seed") {
                auto meta = game->get_game_metadata();
                return "World Seed: " + std::to_string((int64_t)meta.get("seed", 0));
            }
            if (sub == "info") {
                if (!game->get_world()) return "No world loaded.";
                auto meta = game->get_game_metadata();
                const auto &s = game->get_world()->settings();
                std::ostringstream ss;
                ss << "World Name: " << godot::String(meta.get("name", "World")).utf8().get_data() << "\n"
                   << "Seed: " << (int64_t)meta.get("seed", 0) << "\n"
                   << "Preset: " << godot::String(godot::Dictionary(meta["generation"]).get("preset", "default")).utf8().get_data() << "\n"
                   << (s.single_biome.empty() ? "" : ("Single Biome: " + s.single_biome + "\n"))
                   << "Min Y: " << s.min_y << ", Max Y: " << s.max_y << "\n"
                   << "Sea Level: " << s.sea_level << "\n"
                   << "Chunk Span: 32 m\n"
                   << "View Distance: " << s.view_distance << " m";
                return ss.str();
            }
            if (sub == "new") {
                int64_t seed = 0;
                std::string preset;
                std::string biome;
                bool has_seed = false, random_seed = true;
                auto is_known_biome = [&](const std::string &token) {
                    if (game->get_world()) {
                        for (const auto &b : game->get_world()->settings().biomes)
                            if (token == CommandParser::to_lower(b.key))
                                return true;
                    }
                    return token == "normal" || token == "snow" || token == "desert" ||
                           token == "jungle" || token == "enchanted";
                };
                for (size_t i = 2; i < args.size(); ++i) {
                    std::string value = CommandParser::to_lower(args[i]);
                    const bool preset_option = value == "preset" || value == "--preset";
                    const bool biome_option = value == "biome" || value == "--biome" || value == "single_biome";
                    if (biome_option) {
                        if (++i == args.size())
                            return "Specify a biome key (e.g. normal, snow, desert, jungle, enchanted).";
                        if (!biome.empty()) return "Specify only one biome.";
                        biome = CommandParser::to_lower(args[i]);
                        continue;
                    }
                    if (preset_option) {
                        if (++i == args.size())
                            return "Choose a preset: default, plain or structure.";
                        value = CommandParser::to_lower(args[i]);
                    }
                    if (value == "default" || value == "plain" || value == "structure") {
                        if (!preset.empty()) return "Specify only one preset.";
                        preset = value;
                    } else if (!preset_option && is_known_biome(value)) {
                        if (!biome.empty()) return "Specify only one biome.";
                        biome = value;
                    } else {
                        if (preset_option) return "Unknown preset. Choose default, plain or structure.";
                        if (has_seed) return "Specify only one seed.";
                        has_seed = true;
                        random_seed = value == "random";
                        if (!random_seed && (!CommandParser::parse_int(value, seed) || seed < INT32_MIN || seed > INT32_MAX))
                            return "Enter a signed 32-bit seed, random, a preset (default, plain, structure), or biome <key>.";
                    }
                }
                if (preset.empty()) preset = "default";
                if (random_seed) seed = godot::UtilityFunctions::randi_range(INT32_MIN, INT32_MAX);
                bool keep_console = game->is_console_open();
                bool keep_fullbright = game->get_world() && game->get_world()->is_fullbright();
                if (!game->start_world("World " + godot::String::num_int64(seed), seed, preset.c_str(), biome.c_str())) {
                    return "Could not generate world.";
                }
                if (keep_fullbright && game->get_world()) {
                    game->get_world()->set_fullbright(true);
                }
                if (keep_console && game->get_console()) {
                    game->get_console()->set_enabled(true);
                }
                return "Generating new world (" + preset +
                       (biome.empty() ? "" : ", biome=" + biome) + "). Seed: " + std::to_string(seed);
            }
            return "Usage: world seed | world info | world new [default|plain|structure] [biome <key>] [seed]";
        }
    });

    register_command({
        "seed", "World", "Print current world seed.",
        "seed", CommandPermission::Safe, {},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            if (!game) return "No game active.";
            auto meta = game->get_game_metadata();
            return "Seed: " + std::to_string((int64_t)meta.get("seed", 0));
        }
    });

    register_command({
        "biome", "World", "Biome query and teleportation.",
        "biome info | biome list | biome <name>", CommandPermission::Safe, {},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            if (!game || !game->get_world() || !game->get_player()) return "No world or player active.";
            const auto &biomes = game->get_world()->settings().biomes;
            if (args.size() == 1 || (args.size() == 2 && CommandParser::to_lower(args[1]) == "info")) {
                auto p = game->get_player()->get_position();
                auto col = game->get_world()->generator()->column((int)std::floor(p.x), (int)std::floor(p.z));
                std::ostringstream ss;
                ss << std::fixed << std::setprecision(2);
                ss << "Biome: " << biomes[col.biome].name << " (" << biomes[col.biome].key << ")\n"
                   << "Column Temp: " << col.temperature << ", Mountain: " << col.mountain
                   << ", Height: " << col.height;
                return ss.str();
            }
            if (args.size() == 2 && CommandParser::to_lower(args[1]) == "list") {
                std::ostringstream ss;
                ss << "Registered biomes (" << biomes.size() << "):\n";
                for (size_t i = 0; i < biomes.size(); i++) {
                    ss << " " << (i + 1) << ". " << biomes[i].name << " [" << biomes[i].key << "]\n";
                }
                return ss.str();
            }
            if (args.size() == 2) {
                std::string target = CommandParser::to_lower(args[1]);
                int wanted = -1;
                for (size_t i = 0; i < biomes.size(); i++) {
                    if (CommandParser::to_lower(biomes[i].key) == target ||
                        CommandParser::to_lower(biomes[i].name) == target) {
                        wanted = (int)i;
                        break;
                    }
                }
                if (wanted < 0) return "Unknown biome: '" + args[1] + "'. Use 'biome list' to see all.";

                auto origin = game->get_player()->get_position();
                float step = std::max(32.f, game->get_world()->settings().biome_scale / 8.f);
                float radius = std::min(16000.f, std::max(1024.f, game->get_world()->settings().biome_scale * 6.f));
                std::vector<float> xs, zs;
                for (float dz = -radius; dz <= radius; dz += step) {
                    for (float dx = -radius; dx <= radius; dx += step) {
                        xs.push_back(std::floor(std::clamp(origin.x + dx, -999000.f, 999000.f)));
                        zs.push_back(std::floor(std::clamp(origin.z + dz, -999000.f, 999000.f)));
                    }
                }
                auto cols = game->get_world()->generator()->columns(xs, zs);
                float best_dist = 1e30f;
                int found = -1;
                for (size_t i = 0; i < cols.size(); i++) {
                    if (cols[i].biome == wanted) {
                        float dx = xs[i] - origin.x, dz = zs[i] - origin.z;
                        float d = dx * dx + dz * dz;
                        if (d < best_dist) {
                            best_dist = d;
                            found = (int)i;
                        }
                    }
                }
                if (found < 0) {
                    return "No matching biome '" + args[1] + "' nearby within search radius (" + std::to_string((int)radius) + "m).";
                }
                game->get_player()->set_position(godot::Vector3(xs[found], cols[found].height + 24.0f, zs[found]));
                return "Teleported to biome: " + biomes[wanted].name + " at (" +
                       std::to_string((int)xs[found]) + ", " + std::to_string((int)zs[found]) + ").";
            }
            return "Usage: biome info | biome list | biome <name>";
        }
    });

    register_command({
        "time", "World", "Query or control world time.",
        "time get | time set <0..24> | time pause | time resume", CommandPermission::Safe, {},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            if (!game || !game->get_atmosphere()) return "No atmosphere active.";
            auto *atm = game->get_atmosphere();
            if (args.size() == 1 || (args.size() == 2 && CommandParser::to_lower(args[1]) == "get")) {
                float h = atm->state()["hour"];
                return "World hour: " + std::to_string(h);
            }
            std::string sub = CommandParser::to_lower(args[1]);
            if (sub == "pause") {
                atm->set_running(false);
                return "Time paused.";
            }
            if (sub == "resume") {
                atm->set_running(true);
                return "Time resumed.";
            }
            if (sub == "set" && args.size() >= 3) {
                float h = 0;
                if (!CommandParser::parse_float(args[2], h) || !atm->set_hour(h)) {
                    return "Usage: time set <0..24>";
                }
                return "World time set to: " + std::to_string(h) + "h";
            }
            float h = 0;
            if (CommandParser::parse_float(args[1], h)) {
                if (atm->set_hour(h)) return "World time set to: " + std::to_string(h) + "h";
            }
            return "Usage: time get | time set <hour> | time pause | time resume";
        }
    });

    register_command({
        "weather", "World", "Query or change weather.",
        "weather get | weather set <clear|rain|storm>", CommandPermission::Safe, {},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            if (!game || !game->get_atmosphere()) return "No atmosphere active.";
            auto *atm = game->get_atmosphere();
            if (args.size() == 1 || (args.size() == 2 && CommandParser::to_lower(args[1]) == "get")) {
                godot::String w = atm->state()["weather"];
                return "Current weather: " + std::string(w.utf8().get_data());
            }
            if (args.size() >= 2) {
                std::string type = args[1];
                if (type == "set" && args.size() >= 3) type = args[2];
                if (atm->set_weather(godot::String(type.c_str()))) {
                    return "Weather set to: " + type;
                }
            }
            return "Usage: weather get | weather set <clear|rain|storm>";
        }
    });

    register_command({
        "wind", "World", "Set wind intensity.",
        "wind <0..3>", CommandPermission::Safe, {},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            if (!game || !game->get_atmosphere()) return "No atmosphere active.";
            if (args.size() != 2) return "Usage: wind <0..3>";
            float w = 0;
            if (!CommandParser::parse_float(args[1], w) || !game->get_atmosphere()->set_wind(w)) {
                return "Usage: wind <0..3>";
            }
            return "Wind intensity set to: " + std::to_string(w);
        }
    });

    register_command({
        "temperature", "World", "Query climate and temperature at position.",
        "temperature [x y z]", CommandPermission::Safe, {"temp"},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            if (!game || !game->get_player()) return "No player active.";
            godot::Vector3 p = game->get_player()->get_position();
            if (args.size() == 4) {
                float x, y, z;
                if (CommandParser::parse_float(args[1], x) &&
                    CommandParser::parse_float(args[2], y) &&
                    CommandParser::parse_float(args[3], z)) {
                    p = godot::Vector3(x, y, z);
                }
            }
            auto d = game->get_temperature(p);
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(1);
            ss << "Temperature at (" << p.x << ", " << p.y << ", " << p.z << "):\n"
               << "Air: " << float(d["air_c"]) << " °C, Feels like: " << float(d["feels_like_c"]) << " °C\n"
               << "Wind chill: " << float(d["wind_chill_c"]) << " °C, Sun gain: +" << float(d["solar_gain_c"]) << " °C";
            return ss.str();
        }
    });

    register_command({
        "spawnpoint", "World", "Get or set world spawn coordinates.",
        "spawnpoint get | spawnpoint set", CommandPermission::Dev, {},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            if (!game || !game->get_player()) return "No player active.";
            if (args.size() > 1 && CommandParser::to_lower(args[1]) == "set") {
                auto p = game->get_player()->get_position();
                auto meta = game->get_game_metadata();
                godot::Array sp;
                sp.push_back(p.x);
                sp.push_back(p.y);
                sp.push_back(p.z);
                meta["spawn"] = sp;
                game->set_game_metadata(meta);
                return "Spawnpoint set to: (" + std::to_string((int)p.x) + ", " +
                       std::to_string((int)p.y) + ", " + std::to_string((int)p.z) + ")";
            }
            auto meta = game->get_game_metadata();
            godot::Array sp = meta.get("spawn", godot::Array());
            if (sp.size() == 3) {
                return "Current spawnpoint: (" + std::to_string((int)float(sp[0])) + ", " +
                       std::to_string((int)float(sp[1])) + ", " + std::to_string((int)float(sp[2])) + ")";
            }
            return "Spawnpoint not explicitly set (defaults to 0, 100, 0).";
        }
    });

    // ==========================================
    // CATEGORY: CHUNKS
    // ==========================================
    register_command({
        "chunk", "Chunks", "Chunk inspection and manipulation commands.",
        "chunk info [x z] | chunk list | chunk queue | chunk stats | chunk reload [x z]", CommandPermission::Dev, {},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            if (!game || !game->get_world()) return "No world loaded.";
            auto *w = game->get_world();
            if (args.size() < 2) {
                return "Usage: chunk info [x z] | chunk list | chunk queue | chunk stats | chunk reload [x z]";
            }
            std::string sub = CommandParser::to_lower(args[1]);

            if (sub == "info") {
                int cx = 0, cz = 0;
                if (args.size() >= 4) {
                    int64_t x = 0, z = 0;
                    if (CommandParser::parse_int(args[2], x) && CommandParser::parse_int(args[3], z)) {
                        cx = (int)x;
                        cz = (int)z;
                    }
                } else if (game->get_player()) {
                    auto p = game->get_player()->get_position();
                    cx = (int)std::floor(p.x / 32.0f);
                    cz = (int)std::floor(p.z / 32.0f);
                }
                godot::Dictionary info;
                if (!w->get_tile_info(cx, cz, 0, info)) {
                    return "Chunk (" + std::to_string(cx) + ", " + std::to_string(cz) + ") is not loaded at LOD0.";
                }
                std::ostringstream ss;
                ss << "Chunk (" << cx << ", " << cz << ") LOD0 Info:\n"
                   << "Triangles: " << (int)info["triangles"] << "\n"
                   << "Has Mesh: " << (bool(info["has_mesh"]) ? "yes" : "no") << "\n"
                   << "Has Collider: " << (bool(info["has_collider"]) ? "yes" : "no") << "\n"
                   << "Has Volume: " << (bool(info["has_volume"]) ? "yes" : "no") << "\n"
                   << "Grass Quads: " << (int)info["grass_quads"] << "\n"
                   << "Bushes: " << (int)info["bush_count"] << "\n"
                   << "Mesh Version: " << (int64_t)info["version"];
                return ss.str();
            }

            if (sub == "list") {
                auto keys = w->get_loaded_tile_keys();
                std::ostringstream ss;
                ss << "Loaded Chunks (" << keys.size() << " total):\n";
                int shown = 0;
                for (int i = 0; i < keys.size() && shown < 24; i++) {
                    godot::Dictionary d = keys[i];
                    ss << " (" << (int)d["x"] << ", " << (int)d["z"] << " L" << (int)d["level"] << ")";
                    shown++;
                    if (shown % 6 == 0) ss << "\n";
                }
                if (keys.size() > 24) {
                    ss << "\n ... and " << (keys.size() - 24) << " more.";
                }
                return ss.str();
            }

            if (sub == "queue") {
                auto q = w->get_chunk_queue_info();
                std::ostringstream ss;
                ss << "Chunk Pipeline Queues:\n"
                   << "Pending Generation: " << (int)q["pending"] << "\n"
                   << "Vegetation Pending: " << (int)q["vegetation_pending"] << "\n"
                   << "Wanted Chunks: " << (int)q["wanted"] << "\n"
                   << "Active Tiles: " << (int)q["loaded_tiles"];
                return ss.str();
            }

            if (sub == "stats") {
                auto s = w->stats();
                std::ostringstream ss;
                ss << "Chunk System Stats:\n"
                   << "Total Tiles: " << (int)s["tiles"] << "\n"
                   << "LOD0..5: [" << (int)s["lod0"] << ", " << (int)s["lod1"] << ", "
                   << (int)s["lod2"] << ", " << (int)s["lod3"] << ", "
                   << (int)s["lod4"] << ", " << (int)s["lod5"] << "]\n"
                   << "Triangles: " << (int)s["triangles"] << "\n"
                   << "Terrain Colliders: " << (int)s["terrain_colliders"] << "\n"
                   << "Voxel Data Memory: " << ((int64_t)s["voxel_bytes"] / 1024) << " KB\n"
                   << "Last Mesh Time: " << float(s["mesh_ms"]) << " ms\n"
                   << "Last Upload Time: " << float(s["upload_ms"]) << " ms";
                return ss.str();
            }

            if (sub == "reload") {
                int cx = 0, cz = 0;
                if (args.size() >= 4) {
                    int64_t x = 0, z = 0;
                    if (CommandParser::parse_int(args[2], x) && CommandParser::parse_int(args[3], z)) {
                        cx = (int)x;
                        cz = (int)z;
                    }
                } else if (game->get_player()) {
                    auto p = game->get_player()->get_position();
                    cx = (int)std::floor(p.x / 32.0f);
                    cz = (int)std::floor(p.z / 32.0f);
                }
                if (w->reload_tile(cx, cz, 0)) {
                    return "Reload requested for chunk (" + std::to_string(cx) + ", " + std::to_string(cz) + ") LOD0.";
                }
                return "Failed to reload chunk (" + std::to_string(cx) + ", " + std::to_string(cz) + ").";
            }

            return "Usage: chunk info [x z] | chunk list | chunk queue | chunk stats | chunk reload [x z]";
        }
    });

    // ==========================================
    // CATEGORY: PERFORMANCE
    // ==========================================
    register_command({
        "fps", "Performance", "Show current FPS and instantaneous frame time.",
        "fps", CommandPermission::Safe, {},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            int fps = godot::Engine::get_singleton()->get_frames_per_second();
            double ms = fps > 0 ? (1000.0 / fps) : 0.0;
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(2);
            ss << "FPS: " << fps << " (Frame Time: " << ms << " ms)";
            return ss.str();
        }
    });

    register_command({
        "perf", "Performance", "Toggle or view performance statistics.",
        "perf [on|off]", CommandPermission::Safe, {},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            if (!game) return "No game active.";
            if (args.size() >= 2) {
                bool val = true;
                if (!CommandParser::parse_bool(args[1], val)) {
                    return "Usage: perf [on|off]";
                }
                game->set_debug_perf(val);
                return "Performance HUD panel: " + std::string(val ? "ON" : "OFF");
            }
            int fps = godot::Engine::get_singleton()->get_frames_per_second();
            double ms = fps > 0 ? (1000.0 / fps) : 0.0;
            godot::Dictionary s;
            if (game->get_world()) s = game->get_world()->stats();
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(2);
            ss << "Performance Telemetry:\n"
               << "FPS: " << fps << " (" << ms << " ms)\n"
               << "Mesh Generation Time: " << float(s.get("mesh_ms", 0)) << " ms\n"
               << "Upload Time: " << float(s.get("upload_ms", 0)) << " ms (Peak: " << float(s.get("max_upload_ms", 0)) << " ms)\n"
               << "Triangles: " << (int)s.get("triangles", 0) << "\n"
               << "Grass Quads: " << (int)s.get("grass_quads", 0);
            return ss.str();
        }
    });

    register_command({
        "mem", "Performance", "Show memory usage and allocation breakdown.",
        "mem", CommandPermission::Safe, {"memory"},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            godot::OS *os = godot::OS::get_singleton();
            int64_t static_mem = os ? os->get_static_memory_usage() : 0;
            int64_t peak_mem = os ? os->get_static_memory_peak_usage() : 0;
            int64_t voxel_bytes = 0;
            if (game && game->get_world()) {
                auto s = game->get_world()->stats();
                voxel_bytes = (int64_t)s.get("voxel_bytes", 0);
            }
            std::ostringstream ss;
            ss << "Memory Breakdown:\n"
               << "Static Engine Memory: " << (static_mem / (1024 * 1024)) << " MB\n"
               << "Peak Static Memory: " << (peak_mem / (1024 * 1024)) << " MB\n"
               << "Voxel Chunk Data: " << (voxel_bytes / 1024) << " KB (" << (voxel_bytes / (1024 * 1024)) << " MB)";
            return ss.str();
        }
    });

    register_command({
        "render stats", "Performance", "Show rendering pipeline statistics.",
        "render stats", CommandPermission::Safe, {},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            godot::RenderingServer *rs = godot::RenderingServer::get_singleton();
            if (!rs) return "RenderingServer not available.";
            int64_t draw_calls = rs->get_rendering_info(godot::RenderingServer::RENDERING_INFO_TOTAL_DRAW_CALLS_IN_FRAME);
            int64_t prims = rs->get_rendering_info(godot::RenderingServer::RENDERING_INFO_TOTAL_PRIMITIVES_IN_FRAME);
            int64_t video_mem = rs->get_rendering_info(godot::RenderingServer::RENDERING_INFO_VIDEO_MEM_USED);
            std::ostringstream ss;
            ss << "Rendering Pipeline Stats:\n"
               << "Draw Calls: " << draw_calls << "\n"
               << "Primitives / Triangles: " << prims << "\n"
               << "Video Memory: " << (video_mem / (1024 * 1024)) << " MB";
            return ss.str();
        }
    });

    register_command({
        "physics stats", "Performance", "Show physics engine and collider statistics.",
        "physics stats", CommandPermission::Safe, {},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            if (!game) return "No game active.";
            int jolt_bodies = 0;
            if (game->get_physics_blocks()) {
                jolt_bodies = (int)game->get_physics_blocks()->stats()["physics_bodies"];
            }
            int colliders = 0;
            if (game->get_world()) {
                colliders = (int)game->get_world()->stats().get("terrain_colliders", 0);
            }
            std::ostringstream ss;
            ss << "Physics Engine Stats (Jolt Physics 3D):\n"
               << "Active Jolt Rigid Bodies: " << jolt_bodies << "\n"
               << "Terrain Chunk Colliders (ConcaveMesh): " << colliders;
            return ss.str();
        }
    });

    register_command({
        "benchmark", "Performance", "Run or inspect reproducible performance benchmarks.",
        "benchmark help | benchmark run [preset] | benchmark report", CommandPermission::Dev, {"bench"},
        [this](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            if (!game) return "No game active.";
            if (args.size() < 2 || args[1] == "help") {
                return "Benchmark commands:\n"
                       "  benchmark run [gen|flight|teleport] - Run an automated benchmark scenario.\n"
                       "  benchmark report - View latest benchmark results.\n"
                       "  benchmark help - Show this help.";
            }
            if (args[1] == "report") {
                if (last_benchmark_report.empty()) {
                    return "No benchmark report available yet. Run 'benchmark run' first.";
                }
                return last_benchmark_report;
            }
            if (args[1] == "run") {
                std::string preset = "gen";
                if (args.size() >= 3) preset = args[2];
                // Record benchmark metrics from active systems
                godot::Dictionary s;
                if (game->get_world()) s = game->get_world()->stats();
                int fps = godot::Engine::get_singleton()->get_frames_per_second();
                double ft = fps > 0 ? (1000.0 / fps) : 0.0;
                int tiles = int(s.get("tiles", 0));
                int tris = int(s.get("triangles", 0));
                double first_tile = double(s.get("first_tile_ms", 0));
                double world_ready = double(s.get("world_ready_ms", 0));
                double mesh_ms = double(s.get("mesh_ms", 0));
                double upload_ms = double(s.get("upload_ms", 0));

                std::ostringstream ss;
                ss << "=== BENCHMARK REPORT (" << preset << ") ===\n"
                   << "FPS: " << fps << " (Frame Time: " << ft << " ms)\n"
                   << "Active Tiles: " << tiles << ", Rendered Triangles: " << tris << "\n"
                   << "First Tile Gen: " << first_tile << " ms\n"
                   << "World Ready Time: " << world_ready << " ms\n"
                   << "Average Mesh Time: " << mesh_ms << " ms\n"
                   << "Chunk Upload Time: " << upload_ms << " ms\n"
                   << "Status: COMPLETED";
                last_benchmark_report = ss.str();
                return last_benchmark_report;
            }
            return "Usage: benchmark help | benchmark run [preset] | benchmark report";
        }
    });

    // ==========================================
    // CATEGORY: VISUAL & TOOLS
    // ==========================================
    register_command({
        "debug", "Visual", "Toggle specific debug panels or overlays.",
        "debug overlay|perf|world|chunks|physics [on|off]", CommandPermission::Dev, {},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            if (!game) return "No game active.";
            if (args.size() < 2) return "Usage: debug <overlay|perf|world|chunks|physics> [on|off]";
            std::string target = CommandParser::to_lower(args[1]);
            bool val = true;
            bool has_val = (args.size() >= 3 && CommandParser::parse_bool(args[2], val));

            if (target == "overlay") {
                if (!has_val) val = !game->is_debug_overlay();
                game->set_debug_overlay(val);
                return "Debug Overlay: " + std::string(val ? "ON" : "OFF");
            }
            if (target == "perf" || target == "performance") {
                if (!has_val) val = !game->is_debug_perf();
                game->set_debug_perf(val);
                return "Debug Perf Panel: " + std::string(val ? "ON" : "OFF");
            }
            if (target == "world" || target == "biome" || target == "terrain") {
                if (!has_val) val = !game->is_debug_world();
                game->set_debug_world(val);
                return "Debug World Panel: " + std::string(val ? "ON" : "OFF");
            }
            if (target == "chunks") {
                if (!has_val) val = !game->is_debug_chunks();
                game->set_debug_chunks(val);
                return "Debug Chunks Panel: " + std::string(val ? "ON" : "OFF");
            }
            if (target == "physics" || target == "collisions") {
                if (!has_val) val = !game->is_debug_physics();
                game->set_debug_physics(val);
                return "Debug Physics Panel: " + std::string(val ? "ON" : "OFF");
            }
            return "Usage: debug <overlay|perf|world|chunks|physics> [on|off]";
        }
    });

    register_command({
        "hl", "Visual", "Highlight every placed block of a shape (floor, wall, stairs...) or material, for screenshots.",
        "hl <floor|wall|stairs|ramp|roof|pillar|post|cube|all|block_id> | hl off", CommandPermission::Safe, {},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            if (!game) return "No game.";
            if (args.size() < 2) return "Usage: hl <floor|wall|stairs|ramp|roof|pillar|post|cube|all|block_id> | hl off";
            return game->highlight_blocks(args[1]);
        }
    });

    register_command({
        "fullbright", "Visual", "Toggle fullbright lighting.",
        "fullbright [on|off]", CommandPermission::Safe, {},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            if (!game || !game->get_world()) return "No world active.";
            bool val = !game->get_world()->is_fullbright();
            if (args.size() >= 2) {
                if (!CommandParser::parse_bool(args[1], val)) return "Usage: fullbright [on|off]";
            }
            game->get_world()->set_fullbright(val);
            if (game->get_sun()) game->get_sun()->set_shadow(!val);
            return "Fullbright: " + std::string(val ? "ON" : "OFF");
        }
    });

    register_command({
        "fog", "Visual", "Adjust fog parameters or reset to biome.",
        "fog <#RRGGBB> [start end] | fog biome", CommandPermission::Safe, {},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            if (!game || !game->get_atmosphere()) return "No atmosphere active.";
            auto *atm = game->get_atmosphere();
            if (args.size() == 1) {
                return "Fog " + std::string(godot::String(atm->state()["fog_mode"]).utf8().get_data()) + " | " +
                       std::string(godot::String(atm->state()["fog_color"].stringify()).utf8().get_data());
            }
            if (args.size() == 2 && CommandParser::to_lower(args[1]) == "biome") {
                atm->reset_fog();
                return "Biome fog restored.";
            }
            float start = atm->state()["fog_start"], end = atm->state()["fog_end"];
            if (args.size() == 4) {
                if (!CommandParser::parse_float(args[2], start) || !CommandParser::parse_float(args[3], end)) {
                    return "Usage: fog <#RRGGBB> [start end] | fog biome";
                }
            }
            if (!godot::Color::html_is_valid(godot::String(args[1].c_str()))) {
                return "Invalid HTML color code.";
            }
            if (!atm->set_fog(godot::Color::html(godot::String(args[1].c_str())), start, end)) {
                return "Failed to set fog.";
            }
            return "Fog updated.";
        }
    });

    register_command({
        "outline", "Visual", "Toggle cartoon outline post-processing.",
        "outline [on|off]", CommandPermission::Safe, {},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            if (!game || !game->get_atmosphere()) return "No atmosphere active.";
            bool val = true;
            if (args.size() >= 2 && !CommandParser::parse_bool(args[1], val)) {
                return "Usage: outline <on|off>";
            }
            game->get_atmosphere()->set_outline(val);
            return "Outline: " + std::string(val ? "ON" : "OFF");
        }
    });

    register_command({
        "pixelation", "Visual", "Toggle retro pixelation post-processing.",
        "pixelation [on|off]", CommandPermission::Safe, {},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            if (!game || !game->get_atmosphere()) return "No atmosphere active.";
            bool val = true;
            if (args.size() >= 2 && !CommandParser::parse_bool(args[1], val)) {
                return "Usage: pixelation <on|off>";
            }
            game->get_atmosphere()->set_pixelation(val);
            return "Pixelation: " + std::string(val ? "ON" : "OFF");
        }
    });

    register_command({
        "godrays", "Visual", "Toggle volumetric sun godrays.",
        "godrays [on|off]", CommandPermission::Safe, {},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            if (!game || !game->get_atmosphere()) return "No atmosphere active.";
            bool val = true;
            if (args.size() >= 2 && !CommandParser::parse_bool(args[1], val)) {
                return "Usage: godrays <on|off>";
            }
            game->get_atmosphere()->set_godrays(val);
            return "Godrays: " + std::string(val ? "ON" : "OFF");
        }
    });

    // Tools & Interactive Commands forward to existing implementations when appropriate
    register_command({
        "paint", "Tools", "Paint targeted voxel or coordinates with color.",
        "paint <#RRGGBB|reset> [preserve 0..1] [brightness 0..2] | paint at x y z <#RRGGBB|reset>", CommandPermission::Dev, {},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            if (!game) return "No game active.";
            std::string line = "paint";
            for (size_t i = 1; i < args.size(); i++) line += " " + args[i];
            return game->execute_legacy_command(godot::String(line.c_str())).utf8().get_data();
        }
    });

    register_command({
        "overlay", "Tools", "Apply surface overlay decal to voxel or built piece.",
        "overlay <key|clear|reset> | overlay at x y z <key|clear|reset>", CommandPermission::Dev, {},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            if (!game) return "No game active.";
            std::string line = "overlay";
            for (size_t i = 1; i < args.size(); i++) line += " " + args[i];
            return game->execute_legacy_command(godot::String(line.c_str())).utf8().get_data();
        }
    });

    register_command({
        "resolution", "Visual", "Show or change the game window resolution.",
        "resolution [list|WIDTHxHEIGHT]", CommandPermission::Safe, {},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            return game ? game->resolution_command(args).utf8().get_data() : "No game active.";
        }
    });
    register_command({
        "shape", "Tools", "Select or place a block shape.",
        "shape <cube|slab|vertical_slab|ramp|stairs|roof|pillar|mini_pillar|terrain> | shape at x y z <name> [rot]", CommandPermission::Dev, {},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            if (!game) return "No game active.";
            std::string line = "shape";
            for (size_t i = 1; i < args.size(); i++) line += " " + args[i];
            return game->execute_legacy_command(godot::String(line.c_str())).utf8().get_data();
        }
    });

    register_command({
        "cut", "Tools", "Cut voxel or tree branch into Jolt physics object.",
        "cut [at x y z]", CommandPermission::Cheat, {},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            if (!game) return "No game active.";
            std::string line = "cut";
            for (size_t i = 1; i < args.size(); i++) line += " " + args[i];
            return game->execute_legacy_command(godot::String(line.c_str())).utf8().get_data();
        }
    });

    register_command({
        "physics", "Tools", "Release target block into Jolt physics or clear debris.",
        "physics [at x y z] | physics clear", CommandPermission::Cheat, {},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            if (!game) return "No game active.";
            std::string line = "physics";
            for (size_t i = 1; i < args.size(); i++) line += " " + args[i];
            return game->execute_legacy_command(godot::String(line.c_str())).utf8().get_data();
        }
    });

    const auto light_command = [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
        if (!game) return "No game active.";
        std::string line = args[0] == "light follow" ? "light follow" : "light";
        for (size_t i = 1; i < args.size(); i++) line += " " + args[i];
        return game->execute_legacy_command(godot::String(line.c_str())).utf8().get_data();
    };
    register_command({
        "light", "Tools", "Place a stationary light at your current viewpoint.",
        "light [color [energy] [radius]]", CommandPermission::Dev, {"lamp"}, light_command
    });
    register_command({
        "light follow", "Tools", "Toggle or configure a light that follows the player.",
        "light follow [on|off|color [energy] [radius]]", CommandPermission::Dev, {}, light_command
    });

    register_command({
        "log", "Tools", "Export recent game messages, errors and performance samples.",
        "log export <1–300s>", CommandPermission::Safe, {},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            if (!game) return "No game active.";
            if (args.size() != 3 || CommandParser::to_lower(args[1]) != "export")
                return "Usage: log export <1–300s>";
            return game->export_game_log(godot::String(args[2].c_str())).utf8().get_data();
        }
    });

    register_command({
        "buildmode", "Tools", "Toggle Build Mode and its searchable block picker (T).",
        "buildmode [on|off]", CommandPermission::Safe, {"bm"},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            return game ? game->cheat_inventory_command(args).utf8().get_data() : "No game active.";
        }
    });
    register_command({
        "structure", "Tools", "Select, fill, replace, copy, stack, undo, or export a structure area.",
        "structure pos1|pos2 [x y z] | clear | fill [block|air] | replace <from> [to] | walls [block] | hollow [block] | copy | paste [x y z] | stack [n] | undo | export <name>", CommandPermission::Safe, {},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            return game ? game->structure_command(args).utf8().get_data() : "No game active.";
        }
    });
    register_command({
        "item", "Tools", "Equip a tool in either first-person hand.",
        "item pickaxe|axe|world_wand|clear [left|right]", CommandPermission::Dev, {},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            return game ? game->item_command(args).utf8().get_data() : "No game active.";
        }
    });
    register_command({
        "block", "Tools", "Select active hotbar block type.",
        "block <namespace:key>", CommandPermission::Dev, {},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            if (!game) return "No game active.";
            std::string line = "block";
            for (size_t i = 1; i < args.size(); i++) line += " " + args[i];
            return game->execute_legacy_command(godot::String(line.c_str())).utf8().get_data();
        }
    });

    register_command({
        "spawn", "Tools", "Spawn creatures (e.g. chicken) or entity templates into the world.",
        "spawn <creature> [count] | spawn clear | spawn <namespace:key>", CommandPermission::Cheat, {},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            if (!game) return "No game active.";
            std::string line = "spawn";
            for (size_t i = 1; i < args.size(); i++) line += " " + args[i];
            return game->execute_legacy_command(godot::String(line.c_str())).utf8().get_data();
        }
    });

    register_command({
        "mount", "Tools", "Summon a mount already ridden, or ride the closest one (also key E).",
        "mount <name> | mount | mount off", CommandPermission::Cheat, {"ride"},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            if (!game) return "No game active.";
            std::string line = "mount";
            for (size_t i = 1; i < args.size(); i++) line += " " + args[i];
            return game->execute_legacy_command(godot::String(line.c_str())).utf8().get_data();
        }
    });

    register_command({
        "dismount", "Tools", "Get off the current mount (or double-tap crouch).",
        "dismount", CommandPermission::Cheat, {},
        [](godot::AltariaGame *game, const std::vector<std::string> &) -> std::string {
            if (!game) return "No game active.";
            return game->execute_legacy_command("dismount").utf8().get_data();
        }
    });

    register_command({
        "cave", "Tools", "Inspect or teleport to underground cave systems.",
        "cave | cave environment | cave map | cave entrances | cave tp <0..3> | cave formations | cave feature <index>", CommandPermission::Dev, {},
        [](godot::AltariaGame *game, const std::vector<std::string> &args) -> std::string {
            if (!game) return "No game active.";
            std::string line = "cave";
            for (size_t i = 1; i < args.size(); i++) line += " " + args[i];
            return game->execute_legacy_command(godot::String(line.c_str())).utf8().get_data();
        }
    });
}

} // namespace altaria
