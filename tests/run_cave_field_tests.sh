#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
mkdir -p tests/bin
g++ -std=c++17 -fno-exceptions -O2 -Wall -Wextra -Werror -pthread -I. tests/cave_field_tests.cpp game/world_generation/caves/cave_plan.cpp game/world_generation/caves/cave_field.cpp game/world_generation/caves/cave_formations.cpp -o tests/bin/cave_field_tests
./tests/bin/cave_field_tests
