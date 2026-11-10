#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
mkdir -p tests/bin
g++ -std=c++17 -O2 -pthread -DFASTNOISE_STATIC_LIB -I. -Inative/build/fastnoise-install/include \
 tests/terrain_modifier_tests.cpp game/world_generation/terrain/smooth_field.cpp \
 game/world_generation/voxel_core.cpp game/world_generation/biome_stage.cpp game/world_generation/surface_state.cpp \
 game/world_generation/terrain_mesh.cpp game/world_generation/block_shapes.cpp game/world_generation/ramp_shapes.cpp \
 game/world_generation/vegetation.cpp native/build/fastnoise-install/lib/libFastNoise.a -o tests/bin/terrain_modifier_tests
./tests/bin/terrain_modifier_tests
