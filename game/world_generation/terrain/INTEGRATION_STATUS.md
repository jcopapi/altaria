# Terrain acceptance and validation — 2026-11-10

The requested Terrain workflow is implemented and the acceptance checks below pass.
The live game now uses the native Voxel Tools Transvoxel integration. The earlier
profile-only bridge has been replaced. The original GDScript mesh fixtures are
retained for dependency smoke checks, not used for gameplay.

## Acceptance evidence

- Ordinary constructed staircase -> actual T/RMB modifier -> continuous incline.
- Neighbor-aware preview and applied mesh have identical vertex multisets
  (522 vertices in the interaction fixture).
- Rendered-mesh selection and Jolt ray contact agree within 0.002 m.
- Actual player movement lands on the incline and walks uphill, without invisible
  source-cube tops. Example: `(0.9, 221.7687, 1.5)` to `(2.7, 223.4901, 1.5)`.
- Removing Terrain restores the original source cubes and their collision.
- Source IDs/materials/forms/rotation/paint remain independent from the modifier.
- S3/B2 apply/remove/reload and mixed S2/B1 compatibility pass.
- Actual mining and paint select the source through the smooth mesh.
- World cells 31/32 connect across native chunk and smooth patch boundaries.
- All ten geometry cases plus negative fractional origins pass the final native
  field/mesher path: finite/nondegenerate faces, closed combined volume,
  deterministic input ordering, exact independently meshed chunk seams.
- Procedural generation shares the same field, patches, collision and tools.
  Explicit off survives reload; flight eviction/return rebuilds the saved surface.
- Vulkan day/night captures verify grass/stone texture IDs and grass transitions.
  Removed 3D grass/bush counts remain zero.
- Cartoon/fractional construction and manual ramp/corner regression runners pass.
- General runtime smoke passes, including repeated world switches and generation
  from the console. A worker RenderingServer readback deadlock found by this test
  was fixed with the small upstream CPU-array binding.

## Main files

| Area | Files |
| --- | --- |
| Dependency | `tools/setup_voxel_tools.sh`, `native/patches/voxel_cpu_arrays.py`, `native/dependencies.json`, `addons/zylann.voxel/` |
| Native field/mesher | `smooth_field.*`, `terrain_mesher_bridge.*` |
| Local streaming, preview, exact rays | `terrain_surface.*` |
| Modifier and journals | `voxel_core.h`, `surface_state.cpp`, `built_world.*`, `construction_core.cpp` |
| World sources and readiness | `terrain_mesh.cpp`, `voxel_world.*`, `world_data.cpp`, `data/generation.json` |
| Gameplay / registration | `native/src/altaria_game.*`, `native/src/register_types.cpp` |
| Collision / textures | `physics_blocks.*`, `shaders/terrain.gdshader` |
| Controls and docs | root `README.md`, world/player/debug guides, this directory's `README.md` |

## Test commands

```
./build.sh
./tests/run_terrain_modifier_tests.sh   # 3,230 checks
./tests/run_voxel_tools_tests.sh        # probe, native geometry, journals
./tests/run_terrain_gameplay.sh         # actual input, preview, walking, mining
./tests/run_terrain_procedural.sh       # inheritance, overrides, eviction, return
./tests/run_runtime_tests.sh            # world lifecycle and existing features
./tests/run_terrain_gameplay_visual.sh  # graphical Vulkan captures
./tests/run_terrain_scale.sh            # large construction / local rebuild scope
./tests/run_terrain_benchmark.sh        # graphical flight and real source edits
```

Tests create isolated temporary user data. No existing user world is altered.
Captures copied to ignored `game/temp/terrain_*.png` show ordinary stairs,
smoothed day/night geometry and the natural surface.

## Optimization and remaining limits

8 m patches use four SDF samples/metre; one worker and at most eight in-flight
builds. Version checks reject stale work. Uploads are limited to one patch/frame,
retirements to two/frame. Source dependency tracking and batched native chunk
invalidation avoid scanning/rebuilding the entire world per edit. Natural
residency is 32 m by default, configurable in the world's generation snapshot.
The original voxel LODs cover the distance beyond it.

Godot mesh upload and Jolt shape creation still have indivisible costs. Initial
loading, whole-world replacement, teleports and fast flight can still spike; no
zero-stutter guarantee is made. See the Terrain README for the complete limits:
Linux debug binary only, conservative original construction occupancy, original
forms restored when detached into dynamic physics, and no multiplayer transport.

The existing broad `run_core_tests.sh` has a separate mountain-stream/waterfall
assertion failure recorded before this integration. Terrain-specific passes do
not imply that this older suite is entirely green.

## Measured on this machine

GDstudio 4.7.1, Linux, NVIDIA RTX 4050 Laptop, 1280×720 presentation / 640×360 world,
Forward+ Vulkan, 120 FPS cap. One automated run (not a universal hardware target):

| Workload | Samples | p50 | p95 | p99 | Maximum |
| --- | ---: | ---: | ---: | ---: | ---: |
| Fast flight with streaming | 720 | 8.34 ms | 18.86 ms | 23.82 ms | 28.17 ms |
| Eight confirmed natural-block edits with active Terrain | 360 | 8.28 ms | 9.84 ms | 12.49 ms | 14.01 ms |

The flight run still exceeds a 16.67 ms frame budget in 68/720 frames. No frame in
that run exceeded 33.33 ms; this does not guarantee future runs cannot. The edit
fixture retains roughly 169,605 Terrain triangles. The large construction fixture
retains 1,824 original cubes, 32 patches and 36,240 triangles; removing one modifier
rebuilt only three patches. Its maximum observed patch upload was 6.23 ms and
worker build 19.63 ms. Off-thread timings are reported separately from frame times.

`tests/run_terrain_benchmark.sh` checks that eight real source blocks were mined;
an earlier benchmark location had no active surface and was corrected before
recording the table. A real mining return-value/reset failure uncovered during
that correction was fixed and is covered by both native and gameplay regressions.
