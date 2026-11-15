#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
mkdir -p tests/bin
g++ -std=c++17 -fno-exceptions -O2 -Wall -Wextra -Werror -I. tests/cave_plan_tests.cpp game/world_generation/caves/cave_plan.cpp -o tests/bin/cave_plan_tests
./tests/bin/cave_plan_tests
