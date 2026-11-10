#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
engine="${GODOT:-$HOME/.local/bin/gdstudio}"
test_root="$(mktemp -d /tmp/altaria-voxel-tools.XXXXXX)"
for script in voxel_tools_probe terrain_transvoxel terrain_native_mesher terrain_native_geometry terrain_modifier_runtime; do
    env XDG_DATA_HOME="$test_root/data" XDG_CONFIG_HOME="$test_root/config" XDG_CACHE_HOME="$test_root/cache" \
        timeout 120s "$engine" --headless --path "$PWD" --script "res://tests/$script.gd" > "$test_root/$script.log" 2>&1
    cat "$test_root/$script.log"
    if rg --quiet 'ERROR:|SCRIPT ERROR:|FAIL' "$test_root/$script.log"; then exit 1; fi
done
rg --quiet 'VOXEL_TOOLS_PROBE_PASS' "$test_root/voxel_tools_probe.log"
rg --quiet 'TERRAIN_TRANSVOXEL_PASS' "$test_root/terrain_transvoxel.log"

rg --quiet NATIVE_TERRAIN_MESHER_PASS "$test_root/terrain_native_mesher.log"
rg --quiet TERRAIN_MODIFIER_RUNTIME_PASS "$test_root/terrain_modifier_runtime.log"

rg --quiet TERRAIN_NATIVE_GEOMETRY_PASS "$test_root/terrain_native_geometry.log"
