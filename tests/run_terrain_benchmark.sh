#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
engine="${GODOT:-$HOME/.local/bin/gdstudio}"
test_root="$(mktemp -d /tmp/altaria-terrain-benchmark.XXXXXX)"
env XDG_DATA_HOME="$test_root/data" XDG_CONFIG_HOME="$test_root/config" XDG_CACHE_HOME="$test_root/cache" \
 timeout 180s "$engine" --disable-crash-handler --max-fps 120 --path "$PWD" --script res://tests/stream_benchmark.gd > "$test_root/runtime.log" 2>&1
cat "$test_root/runtime.log"
rg --quiet STREAM_BENCHMARK_PASS "$test_root/runtime.log"
if rg --quiet 'ERROR:|SCRIPT ERROR:' "$test_root/runtime.log"; then exit 1; fi
