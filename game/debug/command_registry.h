#pragma once

#include "command_parser.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>

namespace godot {
class AltariaGame;
}

namespace altaria {

enum class CommandPermission {
    Safe = 0,
    Dev = 1,
    Cheat = 2
};

struct CommandDef {
    std::string id;
    std::string category;
    std::string description;
    std::string usage;
    CommandPermission permission = CommandPermission::Safe;
    std::vector<std::string> aliases;
    std::function<std::string(godot::AltariaGame *game, const std::vector<std::string> &args)> handler;
};

class CommandRegistry {
private:
    std::unordered_map<std::string, std::shared_ptr<CommandDef>> commands;
    std::unordered_map<std::string, std::string> alias_map; // alias -> primary id
    std::vector<std::string> history;
    CommandPermission current_permission = CommandPermission::Cheat; // default full developer mode
    std::string last_benchmark_report;

    CommandRegistry();
    void register_all_commands();

public:
    static CommandRegistry &get_singleton();

    void register_command(const CommandDef &def);
    const CommandDef *find_command(const std::string &name) const;
    const CommandDef *find_command_or_parent(const std::string &name) const;

    void set_permission(CommandPermission perm) { current_permission = perm; }
    CommandPermission get_permission() const { return current_permission; }
    static std::string permission_to_string(CommandPermission p);
    static bool string_to_permission(const std::string &str, CommandPermission &out);

    std::vector<std::string> autocomplete(const std::string &prefix) const;
    std::vector<std::string> suggest(const std::string &input, int max_suggestions = 3) const;

    const std::vector<std::string> &get_history() const { return history; }
    void add_history(const std::string &cmd);
    void clear_history() { history.clear(); }

    void set_last_benchmark_report(const std::string &rep) { last_benchmark_report = rep; }
    const std::string &get_last_benchmark_report() const { return last_benchmark_report; }

    std::string execute(godot::AltariaGame *game, const std::string &line);
    std::vector<const CommandDef *> get_all_commands() const;
    std::vector<const CommandDef *> get_commands_in_category(const std::string &category) const;
};

} // namespace altaria
