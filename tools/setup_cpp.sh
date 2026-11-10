#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
# Requirements on Ubuntu/Pop!_OS: g++, git, python3-venv, curl.
if [[ ! -x .venv/bin/python ]]; then
    python3 -m venv .venv
fi
touch .venv/.gdignore
.venv/bin/python -m pip install -r native/requirements-build.txt
mkdir -p native/third_party
if [[ ! -f native/godot-cpp/SConstruct ]]; then
    git clone --depth 1 --branch 10.0.0-stable \
        https://github.com/godotengine/godot-cpp.git native/godot-cpp
fi
if [[ ! -f native/third_party/FastNoise2/CMakeLists.txt ]]; then
    git clone https://github.com/Auburn/FastNoise2.git native/third_party/FastNoise2
    git -C native/third_party/FastNoise2 checkout --detach c35d6041ce7efc05bc84e26d762f6a38d2dcc71e
fi
./tools/build_fastnoise.sh
./build.sh
