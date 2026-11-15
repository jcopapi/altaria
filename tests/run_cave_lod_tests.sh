#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
mkdir -p tests/bin
# The fixture includes terrain_mesh.cpp to inspect its generated volumes.
g++ -std=c++17 -fno-exceptions -O2 -pthread -DFASTNOISE_STATIC_LIB -I. -Inative/build/fastnoise-install/include \
  tests/cave_lod_tests.cpp game/world_generation/voxel_core.cpp game/world_generation/biome_stage.cpp \
  game/world_generation/surface_state.cpp game/world_generation/block_shapes.cpp game/world_generation/ramp_shapes.cpp \
  game/world_generation/vegetation.cpp game/world_generation/caves/*.cpp native/build/fastnoise-install/lib/libFastNoise.a \
  -o tests/bin/cave_lod_tests
./tests/bin/cave_lod_tests
