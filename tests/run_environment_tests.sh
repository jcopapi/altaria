#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
engine="${GODOT:-$HOME/.local/bin/gdstudio}"
test_root="$(mktemp -d /tmp/altaria-environment-test.XXXXXX)"
echo "Test data: $test_root"
env XDG_DATA_HOME="$test_root/data" XDG_CONFIG_HOME="$test_root/config" XDG_CACHE_HOME="$test_root/cache" \
    "$engine" --disable-crash-handler --headless --path "$PWD" --script res://tests/environment_runtime.gd --log-file "$test_root/runtime.log"
grep -qE 'ENVIRONMENT_RUNTIME_PASS' "$test_root/runtime.log"
if grep -qE 'ERROR:|SCRIPT ERROR:|ENVIRONMENT_RUNTIME_FAIL' "$test_root/runtime.log"; then
    echo "Unexpected engine errors in $test_root/runtime.log" >&2
    exit 1
fi
