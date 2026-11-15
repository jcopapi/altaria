#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
engine="${GODOT:-$HOME/.local/bin/gdstudio}"
test_root="$(mktemp -d /tmp/altaria-cartoon-visual.XXXXXX)"
echo "Test data: $test_root"
env XDG_DATA_HOME="$test_root/data" XDG_CONFIG_HOME="$test_root/config" XDG_CACHE_HOME="$test_root/cache" \
    timeout 180s "$engine" --disable-crash-handler --max-fps 120 --path "$PWD" \
    --script res://tests/cartoon_visual.gd --log-file "$test_root/runtime.log"
grep -qE 'CARTOON_VISUAL_PASS' "$test_root/runtime.log"
if grep -qE 'ERROR:|SCRIPT ERROR:|CARTOON_VISUAL_FAIL' "$test_root/runtime.log"; then
    echo "Unexpected engine errors in $test_root/runtime.log" >&2
    exit 1
fi
