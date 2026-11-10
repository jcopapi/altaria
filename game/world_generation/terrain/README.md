# Terrain surface caps

Terrain is a local surface block that rounds transitions between ordinary
surfaces. It occupies its original block cell and requires an ordinary solid
base directly underneath. It does not turn buried blocks or the whole world
into a deformable volume.

The cap height is limited to **one block**, confirmed for this implementation.
`terrain_max_height` in `data/generation.json` accepts 0.25..1 block; its default
is 1. Original half-height slab sources retain their smaller bounds. Materials,
paint, source positions and construction IDs remain independent from the cap.

## Play

- **T** toggles Terrain on an exposed cube/slab with a supporting base.
- `shape terrain` + right-click previews/toggles the same surface cap.
- `terrain at x y z on|off|toggle` addresses a world source cell.
- Building an ordinary block on Terrain replaces the cap in its own cell. The
  block starts at the normal base plane, ignoring the curved surface's hit height.
  It does not float on top of the cap. Independently built caps retain their ID
  when replaced; the selected new material/form becomes an ordinary piece.
- Mining the cap frees that complete cell. The base underneath remains ordinary,
  without receiving another automatic Terrain modifier.
- Disabling the modifier, rather than mining, restores the original source form.

A cap cannot be applied to a floating block or a buried block. The supported
source forms are cubes and floor slabs. Other existing construction forms remain
available as ordinary blocks. A capped piece whose base is removed falls back to
its original cubic/slab geometry rather than smoothing the newly exposed volume.

## Geometry and ownership

`smooth_field.*` reconstructs a bounded surface over each cap's footprint. Shared
edge/corner samples use adjacent surface heights, interpolated with smooth curves
through a 3x3 local lattice. This rounds folds and joins one-block transitions;
it never spreads occupancy into another cell or smooths the support below.

`terrain_surface.*` supplies only caps and their immediate normal contact collar
to the field. Buried blocks retain the original voxel renderer and collision.
The surface field goes through **Voxel Tools VoxelBuffer.CHANNEL_SDF** (Float32)
and **VoxelMesherTransvoxel**. No replacement polygonizer was implemented.
`native/patches/voxel_cpu_arrays.py` exposes the upstream mesher's CPU output,
avoiding a RenderingServer readback/deadlock on background workers.

The same generated triangles drive rendering, Jolt, ray selection and the preview.
Each cap triangle retains the original source owner and material. Ordinary texture
layers, HSV paint and grass side transitions remain supported.

Smoothing uses 8 m patches, a common four-samples/metre lattice and immutable source
snapshots. Jobs are local/versioned, capped at eight in flight; at most one patch
is uploaded and two retired per frame. No whole-world remesh is needed for an edit.
These are soft budgets: an individual engine/Jolt operation can still spike.

## Generation and saves

Procedural candidates are taken from the **original exposed top layer before
player edits**, requiring solid support beneath. Only those source cells inherit
Terrain. Applying an air edit therefore cannot recursively promote deeper cells.
The same source rule and cap geometry are used for procedural and player Terrain.

`procedural_terrain`, `terrain_distance` (default 32 m) and `terrain_max_height`
are snapshotted per world. `world new` reads changed defaults. Distant world LODs
continue using ordinary blocks/slabs. Explicit off overrides and mined cells are
saved; bases, replacement blocks and fractional construction retain their normal
persistence. S3/B2 readers remain compatible with S2/B1 records. Older buried or
unsupported Terrain flags remain readable, but do not generate a surface cap.

## Dependency / verification

Official Voxel Tools 1.7.1-dev, commit
`dfb0f81c7fd766f68d78715b2138f4e336cd9737`, GDExtension API 4.5, tested on
GDstudio / Godot 4.7.1, Linux x86_64. `tools/setup_voxel_tools.sh` reproduces the
installed binary using its isolated SDK, system C++ runtime and Linux export map.
Only Linux debug binaries are currently provided. See `native/dependencies.json`
and `licenses/VoxelTools-MIT.txt`.

`VoxelTerrain`/`VoxelLodTerrain` were investigated, but the game retains its
existing block journals, fractional construction store and voxel LOD streaming.
Voxel Tools owns the smooth polygonization; the small native adapter preserves
source identity and integrates its output with those existing systems.

```
./tests/run_terrain_surface.sh          # current gameplay contract
./tests/run_terrain_modifier_tests.sh  # native bounds, field, journals
./tests/run_voxel_tools_tests.sh       # dependency / cap geometry / compatibility
./tests/run_terrain_scale.sh           # 2,080 sources, 256 surface caps
./tests/run_cartoon_tests.sh           # existing fractional construction/physics
./tests/run_runtime_tests.sh           # world lifecycle
./tests/run_terrain_gameplay_visual.sh # Vulkan appearance
```

The current cap acceptance fixture supersedes the previous volumetric staircase
fixture. Its old behavior and baseline are preserved in Git commit `54ae8f6`.
See `INTEGRATION_STATUS.md` for current validation. No multiplayer transport or
non-Linux export was added by this change. Detached dynamic debris uses the
preserved original block form, outside the connected cap surface.
