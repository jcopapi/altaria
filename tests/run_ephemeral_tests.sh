#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
engine="${GODOT:-$HOME/.local/bin/gdstudio}"
display_args=(--headless)
if [[ "${1:-}" == "--rendered" ]]; then
    display_args=(--rendering-driver vulkan)
elif [[ "$#" != 0 ]]; then
    echo "Usage: $0 [--rendered]" >&2
    exit 2
fi
test_root="$(mktemp -d /tmp/altaria-ephemeral-test.XXXXXX)"
echo "Test data: $test_root"
env XDG_DATA_HOME="$test_root/data" XDG_CONFIG_HOME="$test_root/config" XDG_CACHE_HOME="$test_root/cache" \
 timeout 120s "$engine" --disable-crash-handler "${display_args[@]}" --path "$PWD" --script res://tests/ephemeral_runtime.gd --log-file "$test_root/runtime.log"
grep -q 'EPHEMERAL_RUNTIME_PASS' "$test_root/runtime.log"
if grep -q -E 'ERROR:|SCRIPT ERROR:|EPHEMERAL_RUNTIME_FAIL' "$test_root/runtime.log"; then exit 1; fi
