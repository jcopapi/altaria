#pragma once

#include <deque>
#include <vector>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/canvas_layer.hpp>
#include <godot_cpp/classes/rich_text_label.hpp>
#include <godot_cpp/classes/line_edit.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace godot {

class CrabConsoleDebugDrawer : public Node {
    GDCLASS(CrabConsoleDebugDrawer, Node)

protected:
    static void _bind_methods();

public:
    CrabConsoleDebugDrawer() = default;
    ~CrabConsoleDebugDrawer() override = default;
};

class CrabConsole : public Node {
    GDCLASS(CrabConsole, Node)

    Ref<Texture2D> logo;
    CanvasLayer *canvas_layer = nullptr;
    RichTextLabel *history_label = nullptr;
    LineEdit *line_edit = nullptr;

    std::deque<String> history_entries;
    int history_index = -1;
    Dictionary custom_commands;

    RichTextLabel *autocomplete_label = nullptr;
    std::vector<String> current_suggestions;
    int autocomplete_index = -1;

    static CrabConsole *singleton;

    void _on_line_edit_text_submitted(const String &text);
    void _on_line_edit_text_changed(const String &text);
    void trim_output();

protected:
    static void _bind_methods();

public:
    CrabConsole();
    ~CrabConsole() override;

    static CrabConsole *get_singleton() { return singleton; }

    void set_logo(const Ref<Texture2D> &p_logo) { logo = p_logo; }
    Ref<Texture2D> get_logo() const { return logo; }

    void _ready() override;
    void _input(const Ref<InputEvent> &event) override;

    void toggle_enabled();
    void set_enabled(bool enabled);
    bool get_enabled() const;
    bool is_enabled() const { return get_enabled(); }

    void print(const String &text);
    void printerr(const String &text);
    void print_rich(const String &text);
    void clear_output();
    void log(const String &text) { print(text); }

    void add_custom_command(const String &name, const Callable &callable);
    void execute(const String &command);
    void update_autocomplete(const String &text);
    void apply_autocomplete();
};

} // namespace godot
