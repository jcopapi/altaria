#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
engine="${GODOT:-$HOME/.local/bin/gdstudio}"
test_root="$(mktemp -d /tmp/altaria-cave-frame.XXXXXX)"
args=(--headless)
if [[ "${1:-}" == "--rendered" ]]; then
  args=(--rendering-driver vulkan)
elif [[ "$#" != 0 ]]; then
  echo "Usage: $0 [--rendered]" >&2
  exit 2
fi
python3 tests/create_cave_fixture.py "$test_root/project" hydrology
echo "Cave benchmark output: $test_root"
sha256sum bin/libaltaria.linux.template_debug.x86_64.so \
 "$test_root/project/game/world_generation/data/caves.json" \
 "$test_root/project/game/world_generation/data/cave_biomes.json" \
 "$test_root/project/game/world_generation/data/generation.json" > "$test_root/inputs.sha256"
env XDG_DATA_HOME="$test_root/data" XDG_CONFIG_HOME="$test_root/config" XDG_CACHE_HOME="$test_root/cache" \
 ALTARIA_CAVE_FRAMES="$test_root/frames.csv" \
 /usr/bin/time -v -o "$test_root/process.txt" timeout 150s "$engine" --disable-crash-handler "${args[@]}" \
 --path "$test_root/project" --script res://tests/cave_frame_benchmark.gd --log-file "$test_root/runtime.log"
grep -qE 'CAVE_FRAME_BENCHMARK_PASS' "$test_root/runtime.log"
if grep -qE 'ERROR:|ObjectDB instances were leaked|Leaked instance|Pages in use' "$test_root/runtime.log"; then exit 1; fi
