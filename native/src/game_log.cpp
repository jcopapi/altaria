#include "game_log.h"
#include "game/world_generation/voxel_world.h"
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/time.hpp>

namespace godot {
namespace {
constexpr uint64_t retention_ms = 300000;
constexpr size_t max_entries = 12000;
}

void GameLog::trim(uint64_t now) {
    while (!entries.empty() && (now - entries.front().tick > retention_ms || entries.size() > max_entries))
        entries.pop_front();
}

void GameLog::append(const String &text) {
    std::lock_guard<std::mutex> lock(mutex);
    const auto now = Time::get_singleton()->get_ticks_msec();
    entries.push_back({now, text.strip_edges().left(2048)});
    trim(now);
}

void GameLog::_log_message(const String &message, bool error) {
    append((error ? "[stderr] " : "[game] ") + message);
}

void GameLog::_log_error(const String &function, const String &file, int32_t line, const String &code,
                          const String &rationale, bool, int32_t type,
                          const TypedArray<Ref<ScriptBacktrace>> &) {
    append((type == ERROR_TYPE_WARNING ? "[warning] " : "[error] ") +
           (rationale.is_empty() ? code : rationale) + String(" @ ") + file +
           String(":") + String::num_int64(line) + String(" ") + function);
}

void GameLog::sample(double delta, const VoxelWorld &world) {
    const auto now = Time::get_singleton()->get_ticks_msec();
    const double ms = delta * 1000.;
    if (ms < 33.3 && now - last_sample < 1000)
        return;
    last_sample = now;
    const auto queue = world.get_chunk_queue_info();
    const auto preset = world.settings().preset;
    const char *name = preset == altaria::WorldPreset::Structure ? "structure" :
                       preset == altaria::WorldPreset::Plain ? "plain" : "default";
    append(String(ms >= 33.3 ? "[slow frame] " : "[performance] ") + String(name) +
           String(" frame_ms=") + String::num(ms, 2) +
           String(" loaded_chunks=") + String::num_int64(int(queue["loaded_tiles"])) +
           String(" wanted_chunks=") + String::num_int64(int(queue["wanted"])) +
           String(" pending_meshes=") + String::num_int64(int(queue["pending"])) +
           String(" pending_vegetation=") + String::num_int64(int(queue["vegetation_pending"])));
}

String GameLog::export_recent(const String &duration) {
    const String seconds = duration.to_lower().trim_suffix("s");
    if (!seconds.is_valid_int() || seconds.to_int() < 1 || seconds.to_int() > int64_t(retention_ms / 1000))
        return "Usage: log export <1–300s>";
    const uint64_t window = seconds.to_int() * 1000;
    uint64_t now;
    std::deque<Entry> selected;
    {
        std::lock_guard<std::mutex> lock(mutex);
        now = Time::get_singleton()->get_ticks_msec();
        trim(now);
        for (const auto &entry : entries)
            if (now - entry.tick <= window)
                selected.push_back(entry);
    }
    const String folder = "user://logs";
    if (DirAccess::make_dir_recursive_absolute(folder) != OK)
        return "Could not create the log export folder.";
    const String stamp = Time::get_singleton()->get_datetime_string_from_system(true).replace(":", "-");
    const String path = folder.path_join("altaria-" + stamp + String("-") + String::num_uint64(now) + String(".log"));
    auto file = FileAccess::open(path, FileAccess::WRITE);
    if (file.is_null())
        return "Could not write the log export.";
    file->store_line("Altaria | requested window: " + seconds + String("s | timestamps relative to export"));
    for (const auto &entry : selected)
        file->store_line("-" + String::num((now - entry.tick) / 1000., 3) + String("s ") + entry.text);
    file->flush();
    if (file->get_error() != OK)
        return "Could not finish writing the log export.";
    return "Exported log to " + ProjectSettings::get_singleton()->globalize_path(path);
}
}
