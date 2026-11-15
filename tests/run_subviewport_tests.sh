#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
engine="${GODOT:-$HOME/.local/bin/gdstudio}"
test_root="$(mktemp -d /tmp/altaria-subviewport-test.XXXXXX)"
echo "Test data: $test_root"
env XDG_DATA_HOME="$test_root/data" XDG_CONFIG_HOME="$test_root/config" XDG_CACHE_HOME="$test_root/cache" \
    timeout 100s "$engine" --disable-crash-handler --headless --path "$PWD" \
    --script res://tests/subviewport_runtime.gd --log-file "$test_root/runtime.log"
grep -qE 'SUBVIEWPORT_RUNTIME_PASS' "$test_root/runtime.log"
if grep -qE 'ERROR:|SCRIPT ERROR:|SUBVIEWPORT_RUNTIME_FAIL' "$test_root/runtime.log"; then exit 1; fi
