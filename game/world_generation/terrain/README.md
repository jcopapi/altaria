# Terrain modifier — Voxel Tools integration

Terrain is independent state on each original world cell or construction piece.
It preserves material, original shape/rotation, paint, position and stable piece
ID. Apply with **T**, or select `shape terrain` and right-click. Both controls
toggle the modifier. The preview includes the selected source and affected
neighbors. `terrain at x y z on|off|toggle` addresses a world cell. Removing Terrain
restores its original form. Mining and paint address the original triangle owner.

## Installed components and architecture

Official `Zylann/godot_voxel`, commit
`dfb0f81c7fd766f68d78715b2138f4e336cd9737` (1.7.1-dev), GDExtension edition,
compiled against API 4.5 and tested in GDstudio 0.4.1 / Godot 4.7.1, Linux x86_64.

The live C++ pipeline uses **VoxelBuffer.CHANNEL_SDF**, Float32 samples, queried
mesher padding and **VoxelMesherTransvoxel**. Its polygonization is entirely
Voxel Tools. `native/patches/voxel_cpu_arrays.py` exposes the mesher's existing CPU
surface arrays to the extension boundary. This small binding avoids creating
ArrayMesh and reading it back on a worker: that path can deadlock a world switch
when the main thread joins a worker waiting for RenderingServer. Uploads remain
on the main thread. No alternative mesher was implemented.

`VoxelTerrain`, `VoxelLodTerrain` and `VoxelToolTerrain` were investigated and
probed, but are not runtime nodes in this integration. The existing native game
already owns sparse block journals, fractional construction IDs, six block LODs
and streaming. A second terrain store would duplicate authority and could not
preserve those source identities by itself. The hybrid bridge retains those
systems and supplies immutable local SDF regions to Voxel Tools. Its fixed
resolution needs no Transvoxel LOD transition shader; distant world rendering
continues to use the existing voxel LODs.

The dependency uses a separate SDK (`native/third_party/voxel-godot-cpp`), the
system C++ runtime, and a Linux export map exposing only `voxel_library_init`.
Do not share generated SDK objects with the application's API 4.7 build.
Reproduce with `tools/setup_voxel_tools.sh`; revisions and adaptations are recorded
in `native/dependencies.json`, with the upstream MIT license in `licenses/`.

## Code and data flow

- `smooth_field.*`: deterministic, spatially indexed occupancy reconstruction.
  Tensor tent weights turn adjoining staircase cubes into an inclined volume.
  Normal neighbors contribute exact box SDFs, keeping the cubic interface sealed.
  The field is three-dimensional, including walls, corners and fractional sources.
- `terrain_mesher_bridge.*`: one bulk ZXY Float32 transfer per region into
  VoxelBuffer, then the upstream Transvoxel CPU output. Degenerate faces are
  rejected and world-space coordinates use a common four-samples/metre lattice.
- `terrain_surface.*`: immutable block/construction snapshots, 8 m patches, one
  background worker, versioned local invalidation, at most eight pending builds,
  nearest-first submissions, one upload and at most two retirements per frame.
  Dependencies commit source readiness locally, coalescing native chunk updates.
- `voxel_world.*`, `built_world.*`: keep source cubes/collision until all replacement
  patches are ready. Readiness is transient and never enters authoritative saves.
- `terrain.gdshader`: original texture layers, biome/HSV paint and adjacent grass
  transition overlays apply to smooth triangles; material IDs remain flat per face.
- `AltariaGame`: T/RMB, console, selection/mining, exact triangle rays and preview.
  Jolt static collision uses precisely the same triangles as rendering. Player
  walking queries that collision rather than the source cube's old AABB.

S3 surface and B2 construction journals store a tri-state modifier independently
of original forms: -1 inherits generation, 0 explicitly off, 1 on. Readers retain
S2/B1 compatibility and migrate legacy Terrain shapes. The original metadata is
not replaced by a baked ramp profile. Existing author/sequence validation remains
in charge; LAN/online transport is not implemented.

## Procedural terrain and performance controls

New worlds snapshot `procedural_terrain: true` and `terrain_distance: 32` from
`data/generation.json`. The generator marks exposed natural solid cells and a
supporting layer, excluding trees, leaves and authored replacement blocks. These
sources use exactly the same field, mesher, ownership and tools as construction.
An explicit off edit survives save/reload and overrides generation. Old worlds
without this setting keep their previous generation until configured explicitly.

Natural smoothing is resident inside a quantized 32 m sphere around the viewer;
the ordinary voxel/slab LOD renderer covers the distance beyond it. Authored world
modifiers prewarm through the detailed voxel range plus 64 m. Construction patches
remain available through the existing view distance. Moving away and returning
reconstructs deterministic geometry without losing edits. Increasing the natural
radius raises triangle count and rebuild cost; 8..96 m is accepted.

Uploads/collider creation are soft budgets: a single Godot/Jolt operation cannot
be interrupted. There can still be frame spikes during initial loading, a full
world switch or a long teleport. Stats expose terrain build/upload/refresh maxima,
pending work, source counts and triangle counts. Do not treat off-thread mesh
build time as a frame-time benchmark.

## Verification

Use isolated temporary user data through these runners:

```
./tests/run_terrain_modifier_tests.sh
./tests/run_voxel_tools_tests.sh
./tests/run_terrain_gameplay.sh
./tests/run_terrain_procedural.sh
./tests/run_runtime_tests.sh
./tests/run_terrain_scale.sh
./tests/run_terrain_gameplay_visual.sh  # graphical Vulkan session
```

Native geometry fixtures cover all ten requested configurations plus negative
fractional origins; they check finite/nondegenerate, closed combined volumes,
input-order determinism and exact independent chunk seams. The gameplay test
builds ordinary stairs, applies actual T/RMB, compares preview/applied vertices,
walks uphill, compares Jolt/selection contact, reloads, removes the modifier,
restores source cubes, paints and mines across a native chunk seam. The procedural
test covers inheritance, explicit off/reload, reapplication and flight eviction/
return. Vulkan captures check day/night texture identity and side transitions.

## Current limits

- Only the Linux x86_64 debug dependency binary is supplied; other platforms and
  release exports need their matching Voxel Tools build.
- Distant natural LODs retain their block/slab shapes rather than smooth SDF LODs.
- Placement keeps the original construction occupancy rules; filling a carved
  corner inside another piece's original occupied cell remains conservative.
- Releasing a connected Terrain source into dynamic Jolt physics restores its
  preserved original form. Neighbor-dependent smooth geometry is not baked into
  movable debris. Ordinary tree/block physics remains supported.
- The older broad core suite has an independent mountain-stream/waterfall
  assertion failure; this is tracked separately from the Terrain checks.

The original GDScript `sdf_field.gd` / `transvoxel_patch.gd` remain initial fixtures;
they are not the live gameplay field or mesher bridge.
