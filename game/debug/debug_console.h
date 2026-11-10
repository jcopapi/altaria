#pragma once
#include <deque>
#include <godot_cpp/classes/line_edit.hpp>
#include <godot_cpp/classes/panel_container.hpp>
#include <godot_cpp/classes/rich_text_label.hpp>
#include <godot_cpp/variant/callable.hpp>
namespace altaria {
class DebugConsole {
    godot::PanelContainer *panel = nullptr;
    godot::RichTextLabel *output = nullptr;
    std::deque<godot::String> lines, history;
    int history_cursor = 0;

  public:
    godot::LineEdit *input = nullptr;
    void create(godot::Node *parent, const godot::Callable &submit);
    bool is_open() const {
        return panel && panel->is_visible();
    }
    void set_open(bool open);
    void print(const godot::String &text);
    void remember(const godot::String &text);
    void navigate(int delta);
};
} // namespace altaria
