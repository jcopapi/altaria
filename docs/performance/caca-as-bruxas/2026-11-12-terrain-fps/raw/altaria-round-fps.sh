#!/usr/bin/env bash
set -eu
output="$1"
for run in 1 2 3; do
    state="$(mktemp -d /tmp/altaria-round-fps.XXXXXX)"
    env ALTARIA_BENCH_OUT="$output-$run" XDG_DATA_HOME="$state/data" XDG_CONFIG_HOME="$state/config" XDG_CACHE_HOME="$state/cache" timeout 80s "$HOME/.local/bin/gdstudio" --path "$PWD" --disable-crash-handler --max-fps 0 --script /tmp/altaria-round-fps.gd > "$output-$run.log" 2>&1
    rg 'Vulkan|ROUND_FPS|ERROR' "$output-$run.log"
done
