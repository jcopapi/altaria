#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
engine="${GODOT:-$HOME/.local/bin/gdstudio}"
test_root="$(mktemp -d /tmp/altaria-silhouette-visual.XXXXXX)"
env XDG_DATA_HOME="$test_root/data" XDG_CONFIG_HOME="$test_root/config" XDG_CACHE_HOME="$test_root/cache" \
    timeout 150s "$engine" --disable-crash-handler --max-fps 120 --path "$PWD" \
    --script res://tests/silhouette_visual.gd --log-file "$test_root/runtime.log"
grep -qE 'SILHOUETTE_VISUAL_PASS' "$test_root/runtime.log"
if grep -qE 'ERROR:|SCRIPT ERROR:|SILHOUETTE_VISUAL_FAIL' "$test_root/runtime.log"; then exit 1; fi
