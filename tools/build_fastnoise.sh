#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$project_dir"
if [[ ! -x .venv/bin/cmake || ! -f native/third_party/FastNoise2/CMakeLists.txt ]]; then
    echo "Run ./tools/setup_cpp.sh to install the pinned C++ dependencies." >&2
    exit 1
fi
.venv/bin/cmake -S native/third_party/FastNoise2 -B native/build/fastnoise \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DFASTNOISE2_TOOLS=OFF -DFASTNOISE2_TESTS=OFF -DFASTNOISE2_UTILITY=OFF \
    -DFASTNOISE2_STRICT_FP=ON -DCMAKE_POLICY_VERSION_MINIMUM=3.10 \
    -DCMAKE_INSTALL_PREFIX="$project_dir/native/build/fastnoise-install"
.venv/bin/cmake --build native/build/fastnoise --parallel "${JOBS:-2}"
.venv/bin/cmake --install native/build/fastnoise
