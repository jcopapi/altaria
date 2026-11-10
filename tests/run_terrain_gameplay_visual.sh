#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
engine="${GODOT:-$HOME/.local/bin/gdstudio}"
test_root="$(mktemp -d /tmp/altaria-terrain-gameplay-visual.XXXXXX)"
env XDG_DATA_HOME="$test_root/data" XDG_CONFIG_HOME="$test_root/config" XDG_CACHE_HOME="$test_root/cache" \
 timeout 180s "$engine" --disable-crash-handler --max-fps 120 --path "$PWD" --script res://tests/terrain_gameplay_visual.gd > "$test_root/runtime.log" 2>&1
cat "$test_root/runtime.log"
rg --quiet TERRAIN_GAMEPLAY_VISUAL_PASS "$test_root/runtime.log"
if rg --quiet 'ERROR:|SCRIPT ERROR:|TERRAIN_GAMEPLAY_VISUAL_FAIL' "$test_root/runtime.log"; then exit 1; fi
