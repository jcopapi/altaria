#!/usr/bin/env bash
# Cross-compiles the native extension for Windows with MinGW (posix threads)
# and exports build/Altaria-windows.zip. Needs: sudo apt install mingw-w64,
# and the Godot 4.7.1 mono export templates in ~/.local/share/GDstudio/export_templates.
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$project_dir"
shim=native/build/mingw-posix/bin
mkdir -p "$shim"
for tool in g++ gcc gcc-ar; do ln -sf "/usr/bin/x86_64-w64-mingw32-$tool-posix" "$shim/x86_64-w64-mingw32-$tool"; done
ln -sf /usr/bin/x86_64-w64-mingw32-ranlib "$shim/x86_64-w64-mingw32-ranlib"
if [[ ! -f native/build/fastnoise-win-install/lib/libFastNoise.a ]]; then
    cat > native/build/mingw-toolchain.cmake <<'CMAKE'
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc-posix)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++-posix)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
CMAKE
    .venv/bin/cmake -S native/third_party/FastNoise2 -B native/build/fastnoise-win \
        -DCMAKE_TOOLCHAIN_FILE="$project_dir/native/build/mingw-toolchain.cmake" -DCMAKE_BUILD_TYPE=Release \
        -DFASTNOISE2_TOOLS=OFF -DFASTNOISE2_TESTS=OFF -DFASTNOISE2_UTILITY=OFF -DFASTNOISE2_STRICT_FP=ON \
        -DBUILD_SHARED_LIBS=OFF -DCMAKE_POLICY_VERSION_MINIMUM=3.10 \
        -DCMAKE_INSTALL_PREFIX="$project_dir/native/build/fastnoise-win-install"
    .venv/bin/cmake --build native/build/fastnoise-win --parallel "${JOBS:-4}"
    .venv/bin/cmake --install native/build/fastnoise-win
fi
.venv/bin/python -m SCons -C native -j "${JOBS:-4}" platform=windows target=template_release arch=x86_64 \
    use_mingw=yes mingw_prefix="$project_dir/native/build/mingw-posix"
mkdir -p build/windows
~/.local/bin/gdstudio --headless --path "$project_dir" --export-release "Windows Desktop" build/windows/Altaria.exe
(cd build && rm -f Altaria-windows.zip && zip -q -r Altaria-windows.zip windows)
echo "Built build/Altaria-windows.zip"
