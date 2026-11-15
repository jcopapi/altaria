#include "crab_console_bridge.h"
#include "altaria_game.h"
#include "game/debug/command_registry.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <algorithm>

namespace godot {

void CrabConsoleDebugDrawer::_bind_methods() {}

CrabConsole *CrabConsole::singleton = nullptr;

CrabConsole::CrabConsole() {
    if (!singleton) {
        singleton = this;
    }
}

CrabConsole::~CrabConsole() {
    if (singleton == this) {
        singleton = nullptr;
    }
}

void CrabConsole::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_logo", "logo"), &CrabConsole::set_logo);
    ClassDB::bind_method(D_METHOD("get_logo"), &CrabConsole::get_logo);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "logo", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_logo", "get_logo");

    ClassDB::bind_method(D_METHOD("toggle_enabled"), &CrabConsole::toggle_enabled);
    ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &CrabConsole::set_enabled);
    ClassDB::bind_method(D_METHOD("get_enabled"), &CrabConsole::get_enabled);
    ClassDB::bind_method(D_METHOD("is_enabled"), &CrabConsole::is_enabled);

    ClassDB::bind_method(D_METHOD("print", "text"), &CrabConsole::print);
    ClassDB::bind_method(D_METHOD("printerr", "text"), &CrabConsole::printerr);
    ClassDB::bind_method(D_METHOD("print_rich", "text"), &CrabConsole::print_rich);
    ClassDB::bind_method(D_METHOD("log", "text"), &CrabConsole::log);

    ClassDB::bind_method(D_METHOD("add_custom_command", "name", "callable"), &CrabConsole::add_custom_command);
    ClassDB::bind_method(D_METHOD("execute", "command"), &CrabConsole::execute);
    ClassDB::bind_method(D_METHOD("apply_autocomplete"), &CrabConsole::apply_autocomplete);
    ClassDB::bind_method(D_METHOD("update_autocomplete", "text"), &CrabConsole::update_autocomplete);
    ClassDB::bind_method(D_METHOD("_on_line_edit_text_submitted", "text"), &CrabConsole::_on_line_edit_text_submitted);
    ClassDB::bind_method(D_METHOD("_on_line_edit_text_changed", "text"), &CrabConsole::_on_line_edit_text_changed);
}

void CrabConsole::_ready() {
    canvas_layer = Object::cast_to<CanvasLayer>(get_node_or_null("ConsoleCanvasLayer"));
    history_label = Object::cast_to<RichTextLabel>(get_node_or_null("%ConsoleHistory"));
    line_edit = Object::cast_to<LineEdit>(get_node_or_null("%ConsoleLineEdit"));
    autocomplete_label = Object::cast_to<RichTextLabel>(get_node_or_null("%AutocompleteLabel"));
    if (line_edit) {
        line_edit->connect("text_submitted", Callable(this, "_on_line_edit_text_submitted"));
        line_edit->connect("text_changed", Callable(this, "_on_line_edit_text_changed"));
    }
    if (auto close = Object::cast_to<Button>(get_node_or_null("%CloseConsole")))
        close->connect("pressed", Callable(this, "set_enabled").bind(false));

    print("Type help for commands. Tab completes commands.");

}

void CrabConsole::_input(const Ref<InputEvent> &event) {
    Ref<InputEventKey> key = event;
    if (key.is_null() || !key->is_pressed() || key->is_echo()) {
        return;
    }

    Key keycode = key->get_keycode();
    if (keycode == Key::KEY_NONE) {
        keycode = key->get_physical_keycode();
    }

    if (keycode == Key::KEY_QUOTELEFT || keycode == Key::KEY_F1 || keycode == Key::KEY_ESCAPE ||
        (keycode == Key::KEY_SLASH && !get_enabled())) {
        toggle_enabled();
        get_viewport()->set_input_as_handled();
        return;
    }

    if (get_enabled()) {
        if (keycode == Key::KEY_TAB) {
            apply_autocomplete();
            get_viewport()->set_input_as_handled();
            return;
        }

        if (keycode == Key::KEY_UP) {
            if (!history_entries.empty()) {
                if (history_index < 0) {
                    history_index = (int)history_entries.size() - 1;
                } else if (history_index > 0) {
                    history_index--;
                }
                if (line_edit && history_index >= 0 && history_index < (int)history_entries.size()) {
                    line_edit->set_text(history_entries[history_index]);
                    line_edit->set_caret_column(line_edit->get_text().length());
                    update_autocomplete(history_entries[history_index]);
                }
            }
            get_viewport()->set_input_as_handled();
            return;
        }

        if (keycode == Key::KEY_DOWN) {
            if (!history_entries.empty() && history_index >= 0) {
                history_index++;
                if (history_index < (int)history_entries.size()) {
                    if (line_edit) {
                        line_edit->set_text(history_entries[history_index]);
                        line_edit->set_caret_column(line_edit->get_text().length());
                        update_autocomplete(history_entries[history_index]);
                    }
                } else {
                    history_index = -1;
                    if (line_edit) {
                        line_edit->clear();
                        if (autocomplete_label) autocomplete_label->set_visible(false);
                    }
                }
            }
            get_viewport()->set_input_as_handled();
            return;
        }
    }
}

void CrabConsole::toggle_enabled() {
    set_enabled(!get_enabled());
}

void CrabConsole::set_enabled(bool enabled) {
    if (!canvas_layer)
        return;
    canvas_layer->set_visible(enabled);
    if (auto game = AltariaGame::get_active_game())
        game->on_console_visibility_changed(enabled);
    if (!line_edit)
        return;
    if (enabled) {
        line_edit->grab_focus();
        line_edit->edit();
        line_edit->set_caret_column(line_edit->get_text().length());
        history_index = -1;
        update_autocomplete(line_edit->get_text());
    } else {
        line_edit->release_focus();
        if (autocomplete_label)
            autocomplete_label->hide();
    }
}

bool CrabConsole::get_enabled() const {
    return canvas_layer && canvas_layer->is_visible();
}

void CrabConsole::print(const String &text) {
    UtilityFunctions::print(text);
    if (history_label) {
        history_label->add_text(text + String("\n"));
        trim_output();
    }
}

void CrabConsole::printerr(const String &text) {
    UtilityFunctions::printerr(text);
    if (history_label) {
        history_label->push_color(Color(.98f, .60f, .50f));
        history_label->add_text(text + String("\n"));
        history_label->pop();
        trim_output();
    }
}

void CrabConsole::print_rich(const String &text) {
    UtilityFunctions::print_rich(text);
    if (history_label) {
        history_label->append_text(text + String("\n"));
        trim_output();
    }
}

void CrabConsole::trim_output() {
    constexpr int max_paragraphs = 600;
    while (history_label->get_paragraph_count() > max_paragraphs)
        if (!history_label->remove_paragraph(0))
            break;
}

void CrabConsole::clear_output() {
    if (history_label)
        history_label->clear();
}

void CrabConsole::add_custom_command(const String &name, const Callable &callable) {
    custom_commands[name] = callable;
}

void CrabConsole::_on_line_edit_text_submitted(const String &text) {
    String trimmed = text.strip_edges();
    if (line_edit) {
        line_edit->clear();
    }
    if (autocomplete_label) {
        autocomplete_label->set_visible(false);
        autocomplete_label->set_text("");
    }
    current_suggestions.clear();
    autocomplete_index = -1;

    if (trimmed.is_empty()) {
        return;
    }

    history_entries.push_back(trimmed);
    if (history_entries.size() > 100) {
        history_entries.pop_front();
    }
    history_index = -1;

    if (history_label) {
        history_label->push_color(Color(.96f, .96f, .88f));
        history_label->add_text("> " + trimmed + "\n");
        history_label->pop();
        trim_output();
    }
    execute(trimmed);
}

void CrabConsole::_on_line_edit_text_changed(const String &text) {
    update_autocomplete(text);
}

void CrabConsole::apply_autocomplete() {
    if (!current_suggestions.empty() && line_edit) {
        autocomplete_index = (autocomplete_index + 1) % (int)current_suggestions.size();
        String completion = current_suggestions[autocomplete_index];
        line_edit->set_text(completion + " ");
        line_edit->set_caret_column(line_edit->get_text().length());
        update_autocomplete(completion + " ");
    }
}

void CrabConsole::update_autocomplete(const String &text) {
    String clean = text.strip_edges();
    if (clean.begins_with("/") || clean.begins_with(":")) {
        clean = clean.substr(1).strip_edges();
    }
    if (clean.is_empty()) {
        if (autocomplete_label) {
            autocomplete_label->set_visible(false);
            autocomplete_label->set_text("");
        }
        current_suggestions.clear();
        autocomplete_index = -1;
        return;
    }

    std::string prefix = clean.utf8().get_data();
    auto matches = altaria::CommandRegistry::get_singleton().autocomplete(prefix);
    if (text.ends_with(" ")) {
        matches.erase(std::remove(matches.begin(), matches.end(), prefix), matches.end());
    }
    if (matches.empty() && clean.find(" ") < 0 && !text.ends_with(" ")) {
        matches = altaria::CommandRegistry::get_singleton().suggest(prefix, 4);
    }

    current_suggestions.clear();
    autocomplete_index = -1;

    if (matches.empty()) {
        if (autocomplete_label) {
            autocomplete_label->set_visible(false);
            autocomplete_label->set_text("");
        }
        return;
    }

    String suggestions;
    const int max_show = std::min(int(matches.size()), 4);
    for (int i = 0; i < max_show; ++i) {
        current_suggestions.emplace_back(matches[i].c_str());
        if (i > 0)
            suggestions += "\n";
        suggestions += current_suggestions.back();
    }
    if (autocomplete_label) {
        autocomplete_label->set_text(suggestions);
        autocomplete_label->show();
    }
}

void CrabConsole::execute(const String &command) {
    String cmd = command.strip_edges();
    if (cmd.begins_with(":")) {
        cmd = cmd.substr(1).strip_edges();
    } else if (cmd.begins_with("/")) {
        cmd = cmd.substr(1).strip_edges();
    }

    PackedStringArray parts = cmd.split(" ", false);
    if (parts.is_empty()) {
        return;
    }

    String verb = parts[0].to_lower();

    if (custom_commands.has(verb)) {
        Callable callable = custom_commands[verb];
        Array args;
        for (int i = 1; i < parts.size(); i++) {
            args.push_back(parts[i]);
        }
        callable.callv(args);
        return;
    }

    AltariaGame *game = AltariaGame::get_active_game();
    if (game) {
        String result = game->execute_command(cmd);
        if (!result.is_empty()) {
            print(result);
        }
        return;
    }

    printerr("Unknown command: " + cmd + ". Type :help for commands.");
}

} // namespace godot
