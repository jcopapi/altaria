#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
engine="${GODOT:-$HOME/.local/bin/gdstudio}"
test_root="$(mktemp -d /tmp/altaria-cartoon-test.XXXXXX)"
echo "Test data: $test_root"
env XDG_DATA_HOME="$test_root/data" XDG_CONFIG_HOME="$test_root/config" XDG_CACHE_HOME="$test_root/cache" \
 timeout 120s "$engine" --disable-crash-handler --headless --path "$PWD" --script res://tests/cartoon_runtime.gd --log-file "$test_root/runtime.log"
rg --quiet 'CARTOON_RUNTIME_PASS' "$test_root/runtime.log"
if rg --quiet 'ERROR:|SCRIPT ERROR:|CARTOON_RUNTIME_FAIL' "$test_root/runtime.log"; then exit 1; fi
