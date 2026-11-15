#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

if [[ ! -x "$project_dir/.venv/bin/scons" ]]; then
    echo "SCons não encontrado em .venv. Consulte as instruções no README.md." >&2
    exit 1
fi

if [[ ! -f "$project_dir/native/godot-cpp/SConstruct" ]]; then
    echo "godot-cpp não encontrado. Consulte as instruções no README.md." >&2
    exit 1
fi

if [[ ! -f "$project_dir/native/third_party/entt/entt/entt.hpp" ]]; then
    echo "EnTT não encontrado. Rode ./tools/setup_cpp.sh (consulte o README.md)." >&2
    exit 1
fi

if [[ ! -f "$project_dir/native/build/fastnoise-install/lib/libFastNoise.a" ]]; then
    "$project_dir/tools/build_fastnoise.sh"
fi

exec "$project_dir/.venv/bin/python" -m SCons \
    -C "$project_dir/native" \
    -j "${JOBS:-4}" \
    compiledb=yes compiledb_file=../compile_commands.json "$@"
