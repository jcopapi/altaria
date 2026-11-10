#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
revision=dfb0f81c7fd766f68d78715b2138f4e336cd9737
source_dir="$PWD/native/third_party/godot_voxel"
if [[ ! -d "$source_dir/.git" ]]; then
    git clone https://github.com/Zylann/godot_voxel.git "$source_dir"
fi
git -C "$source_dir" checkout --detach "$revision"
sdk_dir="$PWD/native/third_party/voxel-godot-cpp"
if [[ ! -d "$sdk_dir/.git" ]]; then
    git clone --no-hardlinks "$PWD/native/godot-cpp" "$sdk_dir"
fi
python3 - "$source_dir" "$sdk_dir" <<'PYCODE'
import sys
from pathlib import Path
source, sdk=map(Path,sys.argv[1:])
p=source/'SConstruct';s=p.read_text()
marker='# Altaria Linux integration:'
if marker not in s:
    i=s.index('# TODO GDX: Adding our variables')
    s=s[:i]+"""# Altaria Linux integration: shared runtime, private extension symbols.
if env["platform"] == "linux":
    env["LINKFLAGS"] = [f for f in env["LINKFLAGS"] if f not in ("-static-libgcc", "-static-libstdc++")]
    env.Append(LINKFLAGS=["-Wl,--exclude-libs,ALL", "-Wl,--version-script=" + os.path.abspath("altaria_exports.map")])

"""+s[i:]
    p.write_text(s)
(source/'altaria_exports.map').write_text('{ global: voxel_library_init; local: *; };\n')
p=sdk/'SConstruct';s=p.read_text()
if '# Long absolute paths' not in s:
    s=s.replace('library = env.GodotCPP()', """# Long absolute paths: use GNU ar response files.
if env["platform"] == "linux":
    env["ARCOM"] = "${TEMPFILE('$AR $ARFLAGS $TARGET $SOURCES')}"
library = env.GodotCPP()""")
    p.write_text(s)
PYCODE
python3 native/patches/voxel_cpu_arrays.py "$source_dir"
GODOT_CPP_PATH="$sdk_dir" .venv/bin/scons -C "$source_dir" platform=linux target=template_debug -j"${JOBS:-4}"
mkdir -p addons/zylann.voxel/bin
cp "$source_dir/project/addons/zylann.voxel/bin/libvoxel.linux.template_debug.x86_64.so" addons/zylann.voxel/bin/
cp "$source_dir/LICENSE.md" licenses/VoxelTools-MIT.txt
cat > addons/zylann.voxel/voxel.gdextension <<'CFG'
[configuration]
entry_symbol = "voxel_library_init"
compatibility_minimum = "4.5"
[libraries]
linux.debug.x86_64 = "res://addons/zylann.voxel/bin/libvoxel.linux.template_debug.x86_64.so"
CFG
