#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
engine="${GODOT:-$HOME/.local/bin/gdstudio}"
test_root="$(mktemp -d /tmp/altaria-cave-hydrology.XXXXXX)"
python3 tests/create_cave_fixture.py "$test_root/project" hydrology
env XDG_DATA_HOME="$test_root/data" XDG_CONFIG_HOME="$test_root/config" XDG_CACHE_HOME="$test_root/cache" \
 timeout 150s "$engine" --disable-crash-handler --headless --path "$test_root/project" \
 --script res://tests/cave_hydrology_runtime.gd --log-file "$test_root/runtime.log"
grep -qE 'CAVE_HYDRO_RUNTIME_PASS' "$test_root/runtime.log"
if grep -qE 'ERROR:|CAVE_HYDRO_RUNTIME_FAIL|ObjectDB instances were leaked|Leaked instance|Pages in use' "$test_root/runtime.log"; then exit 1; fi
