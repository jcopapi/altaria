# Integration audit — eff109c

Independent read-only investigators: `/root/cave_architecture_audit` (CARTOGRAFO)
and `/root/cave_runtime_audit` (MAQUINISTA/VIGIA), Codex-native, model inherited and
not exposed by tool metadata. Their investigation covered all ten requested areas.
The user's later clarification supersedes any investigator suggestion to restore
disk saves: preserve edits through session streaming only.

| Requested area | Current evidence | Integration consequence |
| --- | --- | --- |
| Generation | `voxel_core.cpp:525` terrain_sample and `terrain_mesh.cpp:34` make_volume | Share cave evaluator; never carve only the mesh |
| Voxels/volumes | `voxel_core.h:254`, `terrain_mesh.cpp:40` dense BlockId uint16 | Preserve solid logical materials and edits |
| Chunks | `terrain_mesh.cpp:40`, `voxel_core.cpp:673` 32x32 full-height XZ tiles | Macro plans use independent coordinates and shared halos |
| Biomes | `voxel_core.h:52`, `world_data.cpp:24` surface data registry | Separate underground registry; surface influence fades with depth |
| Mesh/collision | `terrain_mesh.cpp:298`, `voxel_world.cpp:230` greedy geometry and exact Jolt triangles | Reuse meshing; audit vertical submeshes for internal visibility/cost |
| Streaming | `voxel_world.cpp:436` keeps coverage until replacement; two retirements/frame | Preserve edit state outside resident mesh set |
| Threads | `voxel_core.cpp:706`, `voxel_world.cpp:495` pure worker snapshots/main upload | Immutable plans, bounded cache/results, no scene access in workers |
| Player changes | `voxel_core.cpp:604`, `terrain_mesh.cpp:159` edit maps applied last | Carving, liquids and decoration must not overwrite session edits |
| Registries | `world_data.cpp` blocks, overlays, entities, biomes | Extend data declarations instead of central biome switch chains |
| Optimizations | `terrain_mesh.cpp:298`, `voxel_world.cpp:495`, `voxel_core.cpp:673` greedy/culling/6 LODs/soft uploads | Measure cave face growth and queued bytes; do not lower view quality silently |

## Confirmed integration hazards

- `Generator::material` (`voxel_core.cpp:568`) maps exposed blocks to surface
  material. A new underground floor must get explicit cave material, not grass.
- Water uses `max(sea_level,c.water_top)` in scalar and volume paths. Underground
  water needs planned basins/closures, not automatic sea-level fill everywhere.
- Height range -64..320 is 384 cells per near volume. 34x386x34x2 is ~0.85 MiB
  just for IDs; ~55 MiB for 64 volumes, excluding geometry. Expanding depth by
  changing min_y alone increases memory/scan cost. Current validator permits
  floor >=-512 but total span <=768, so -512..320 is invalid.
- Completed queue cap is 12 results by count, not bytes. Cave face counts can
  multiply memory/collider upload cost. Stale work is rejected after generation.
- Outside loaded volumes, character `block_at` queries call the generator on the
  main thread. They must not plan a whole region or wait on a busy cache lock.

These are static findings/derived memory estimates, not measured FPS bottlenecks.
Baseline headless functional test passed (raw log in operation). GPU, CPU profiles,
VRAM, cave timings and final performance comparison remain unmeasured.
