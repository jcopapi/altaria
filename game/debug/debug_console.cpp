#include "debug_console.h"
#include <algorithm>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
namespace altaria {
using namespace godot;
void DebugConsole::create(Node *parent, const Callable &submit) {
    panel = memnew(PanelContainer);
    panel->set_position(Vector2(12, 12));
    panel->set_size(Vector2(680, 300));
    Ref<StyleBoxFlat> background;
    background.instantiate();
    background->set_bg_color(Color(.035, .035, .035, 1));
    panel->add_theme_stylebox_override("panel", background);
    parent->add_child(panel);
    auto box = memnew(VBoxContainer);
    panel->add_child(box);
    output = memnew(RichTextLabel);
    output->set_custom_minimum_size(Vector2(660, 250));
    output->set_scroll_follow(true);
    output->set_selection_enabled(true);
    box->add_child(output);
    input = memnew(LineEdit);
    input->set_placeholder("Command (help lists commands)");
    box->add_child(input);
    input->connect("text_submitted", submit);
    panel->hide();
    print("Console ready. Type help. Commands are in English.");
}
void DebugConsole::set_open(bool open) {
    panel->set_visible(open);
    if (open)
        input->grab_focus();
    else
        input->release_focus();
}
void DebugConsole::print(const String &text) {
    lines.push_back(text);
    while (lines.size() > 150)
        lines.pop_front();
    String all;
    for (auto &line : lines)
        all += line + "\n";
    output->set_text(all);
}
void DebugConsole::remember(const String &text) {
    if (history.empty() || history.back() != text)
        history.push_back(text);
    while (history.size() > 64)
        history.pop_front();
    history_cursor = int(history.size());
    input->clear();
}
void DebugConsole::navigate(int delta) {
    history_cursor = std::clamp(history_cursor + delta, 0, int(history.size()));
    input->set_text(history_cursor < int(history.size()) ? history[history_cursor] : String());
    input->set_caret_column(input->get_text().length());
}
} // namespace altaria
