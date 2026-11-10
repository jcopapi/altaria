# Terrain surface correction — 2026-11-10

The user's clarified contract replaces the earlier volumetric modifier:
Terrain is a bounded surface cap over ordinary support, rounding folds between
surfaces. The user confirmed a one-block maximum height. It owns a complete
logical cell, so mining it frees that cell; its newly exposed base stays ordinary.
Building on a cap fills/replaces the cap's cell from the base plane.

## Git

A local repository on `main` was created before implementation. Commit `54ae8f6`
contains the complete prior project baseline. No remote repository was published.

## Implemented

- Surface-only curved reconstruction, with shared edge/corner heights.
- Configurable `terrain_max_height` (0.25..1, default 1).
- Exposed-source and normal-base checks for both world and constructed caps.
- Original procedural surface candidates computed before player edits; no
  recursive smoothing of blocks uncovered by mining.
- Only caps and their immediate contact collar enter the SDF field. Normal
  supporting/deep blocks retain original geometry and collision.
- Construction on Terrain replaces its cell, ignoring the curved hit height.
  World source edits and built source replacement both preserve the base below.
- Exact mesh/Jolt contact, source ownership, preview, local updates and compatible
  S3/B2 persistence retained. Voxel Tools Transvoxel still performs polygonization.

## Acceptance / regression evidence

`run_terrain_surface.sh` passed: unsupported/buried application rejected; cap
height <=1; mesh/Jolt contacts equal; world and fractional built construction
fill the original cap cell; support remains unchanged; mining frees a whole cell;
reload preserves these results; a one-block transition joins across x31/x32;
actual player walking rises from `(31.6,221.005,21.5)` to approximately
`(32.9,221.783,21.5)`; procedural mining leaves its normal base unchanged.

`run_terrain_modifier_tests.sh`: 3,233 checks passed for the bounded cap kernel,
deterministic ordering, height configuration, original ordinary volume and
modifier/journal behavior. `run_voxel_tools_tests.sh` covers installed classes,
CPU array binding, native cap/boundary geometry and S2/B1 compatibility.

`run_terrain_scale.sh` passed with 2,080 original pieces, only 256 exposed caps,
20 patches and 12,764 triangles. One modifier edit rebuilt three patches. Observed
patch upload maximum was 2.36 ms and worker build maximum 10.79 ms in that fixture;
these are not full frame-time guarantees. The previous volumetric benchmark
numbers are historical and must not be presented as measurements of this revision.

Existing cartoon/fractional construction, manual ramp/corner, general world-lifecycle
and Vulkan visual checks passed. Removing a cap support also restores ordinary
selectable geometry; construction/preview do not use an absent smooth mesh. Native cap geometry and mixed S2/B1 persistence passed.
All runners use temporary user data, leaving personal worlds untouched.

## Main changed files

- `smooth_field.*`: bounded cap field, replacing volumetric occupancy spreading.
- `terrain_surface.*`: base/exposure rules, surface-only snapshots and preview.
- `terrain_mesh.cpp`: original top-layer procedural policy before edits.
- `voxel_core.h`, `world_data.cpp`, `data/generation.json`: height setting.
- `built_world.*`, `native/src/altaria_game.*`: base-anchored cap replacement,
  placement guard, input/console integration.
- Terrain runtime/native/scale/visual fixtures and root/world/player/debug guides.

## Limits

Cubes and floor slabs can receive caps. Distant LODs remain voxel/slab geometry;
large cliffs retain their ordinary blocks beneath the rounded rim. Unsupported
legacy flags are readable but dormant. Soft upload budgets can still have spikes.
Only Linux debug is validated, and dynamic debris restores its original form.
The older broad core suite has an independent waterfall-generation assertion
failure already documented before this change; targeted passes do not imply
that the entire older suite is green.
