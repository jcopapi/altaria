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
if [[ ! -f native/third_party/entt/entt/entt.hpp ]]; then
    # EnTT (header-only ECS), pinned release; sha256 is recorded in native/dependencies.json.
    mkdir -p native/third_party/entt/entt
    curl -fsSL -o native/third_party/entt/entt/entt.hpp \
        https://raw.githubusercontent.com/skypjack/entt/v3.16.0/single_include/entt/entt.hpp
    curl -fsSL -o native/third_party/entt/LICENSE \
        https://raw.githubusercontent.com/skypjack/entt/v3.16.0/LICENSE
    echo "3272191cf3d46cec92368f918695502dc8d174c92e0b8b4a5d53fed28e240c9f  native/third_party/entt/entt/entt.hpp" | sha256sum -c -
fi
./tools/build_fastnoise.sh
./build.sh
