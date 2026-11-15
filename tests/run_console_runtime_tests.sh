#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."

engine="${GODOT:-$HOME/.local/bin/gdstudio}"
if [[ ! -x "$engine" ]]; then
    echo "Set GODOT to a Godot/GDstudio 4.7 executable." >&2
    exit 1
fi

test_root="$(mktemp -d /tmp/altaria-console-test.XXXXXX)"
echo "Test data: $test_root"

env XDG_DATA_HOME="$test_root/data" XDG_CONFIG_HOME="$test_root/config" \
    XDG_CACHE_HOME="$test_root/cache" "$engine" --disable-crash-handler --headless \
    --path "$PWD" --script res://tests/console_runtime.gd --log-file "$test_root/runtime.log"

if grep -q 'CONSOLE_RUNTIME_PASS' "$test_root/runtime.log"; then
    echo "CONSOLE_RUNTIME_PASS: All console & debug tests passed successfully!"
    rm -rf "$test_root"
    exit 0
else
    echo "Console runtime tests failed! Log output:"
    cat "$test_root/runtime.log"
    exit 1
fi
