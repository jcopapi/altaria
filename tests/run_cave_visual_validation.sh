#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
engine="${GODOT:-$HOME/.local/bin/gdstudio}"
test_root="$(mktemp -d /tmp/altaria-cave-visual.XXXXXX)"
python3 tests/create_cave_fixture.py "$test_root/project" hydrology
echo "Cave visual output: $test_root"
env XDG_DATA_HOME="$test_root/data" XDG_CONFIG_HOME="$test_root/config" XDG_CACHE_HOME="$test_root/cache" \
 ALTARIA_CAVE_PREVIEW="$test_root/images" \
 timeout 180s "$engine" --disable-crash-handler --rendering-driver vulkan \
 --path "$test_root/project" --script res://tests/cave_visual_validation.gd --log-file "$test_root/runtime.log"
grep -qE 'CAVE_VISUAL_PASS' "$test_root/runtime.log"
if grep -qE 'ERROR:|SCRIPT ERROR:|CAVE_VISUAL_FAIL|ObjectDB instances were leaked|Leaked instance|Pages in use' "$test_root/runtime.log"; then exit 1; fi
