#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."

engine="${GODOT:-$HOME/.local/bin/gdstudio}"
if [[ ! -x "$engine" ]]; then
    echo "Set GODOT to a Godot/GDstudio 4.7 executable." >&2
    exit 1
fi

test_root="$(mktemp -d /tmp/altaria-benchmark-test.XXXXXX)"
echo "Benchmark workspace: $test_root"

mkdir -p docs/performance/caca-as-bruxas/2026-11-10-caca-as-bruxas-auditoria/raw/

env XDG_DATA_HOME="$test_root/data" XDG_CONFIG_HOME="$test_root/config" \
    XDG_CACHE_HOME="$test_root/cache" "$engine" --disable-crash-handler --headless \
    --path "$PWD" --script res://tests/benchmarks_runtime.gd --log-file "$test_root/benchmark.log"

if grep -q '\[BENCHMARK_COMPLETE\]' "$test_root/benchmark.log"; then
    echo "Benchmark successfully completed!"
    cat docs/performance/caca-as-bruxas/2026-11-10-caca-as-bruxas-auditoria/raw/benchmark_results.json
    rm -rf "$test_root"
    exit 0
else
    echo "Benchmark failed! Log output:"
    cat "$test_root/benchmark.log"
    exit 1
fi
