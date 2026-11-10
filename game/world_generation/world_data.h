#pragma once
#include "environment_core.h"
#include "voxel_core.h"
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
namespace altaria {
godot::Dictionary read_json(const godot::String &path, godot::String &error);
godot::Dictionary load_generation_data(godot::String &error);
godot::Dictionary load_block_data(godot::String &error);
bool write_json_atomic(const godot::String &path, const godot::Dictionary &value, godot::String &error);
bool parse_environment(const godot::Dictionary &data, EnvironmentSettings &settings, godot::String &error);
bool parse_settings(const godot::Dictionary &data, Settings &settings, godot::String &error);
bool parse_registry(const godot::Dictionary &data, Registry &registry, godot::String &error);
} // namespace altaria
