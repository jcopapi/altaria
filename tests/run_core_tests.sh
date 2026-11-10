#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
mkdir -p tests/bin
g++ -std=c++17 -O2 -pthread -DFASTNOISE_STATIC_LIB -I. -Inative/build/fastnoise-install/include \
  tests/core_tests.cpp game/world_generation/voxel_core.cpp game/world_generation/biome_stage.cpp game/world_generation/surface_state.cpp game/world_generation/terrain_mesh.cpp game/world_generation/block_shapes.cpp game/world_generation/ramp_shapes.cpp game/world_generation/construction_core.cpp game/world_generation/vegetation.cpp game/world_generation/environment_core.cpp \
  native/build/fastnoise-install/lib/libFastNoise.a -o tests/bin/core_tests
./tests/bin/core_tests
