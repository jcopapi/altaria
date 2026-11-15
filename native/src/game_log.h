#pragma once

#include <deque>
#include <mutex>
#include <godot_cpp/classes/logger.hpp>
#include <godot_cpp/classes/script_backtrace.hpp>

namespace godot {
class VoxelWorld;
class GameLog : public Logger {
    GDCLASS(GameLog, Logger)

    struct Entry {
        uint64_t tick;
        String text;
    };
    std::mutex mutex;
    std::deque<Entry> entries;
    uint64_t last_sample = 0;
    void append(const String &text);
    void trim(uint64_t now);

protected:
    static void _bind_methods() {}

public:
    void _log_message(const String &message, bool error) override;
    void _log_error(const String &function, const String &file, int32_t line, const String &code,
                    const String &rationale, bool editor_notify, int32_t type,
                    const TypedArray<Ref<ScriptBacktrace>> &backtraces) override;
    void sample(double delta, const VoxelWorld &world);
    String export_recent(const String &duration);
};
}
