# Cave generation implementation plan

Status: implemented and validated. See COMPLETION_AUDIT.md for the final gates.
Baseline: eff109c. Latest user clarification: worlds remain disposable; persistence
means preserving this session's edits across chunk eviction/regeneration. Do not
restore world-save menus, migrations or old-world compatibility. No ores are placed.

## Architecture audit / decisions

The current game uses C++ GDExtension on GDstudio/Godot 4.7.1, FastNoise2 1.1.1,
Jolt, data-defined block/biome registries, asynchronous native tile workers and
six LODs. Terrain/Voxel Tools were removed and are not dependencies of this goal.
The generator has no floating-island main-world subsystem; caves must only carve
existing rock. A dimension key will salt plans for future reuse elsewhere.

At audit baseline, tiles spanned 32x32 horizontal columns and -64..320 vertically;
LOD0 retains dense voxel data with a one-cell halo. Edits are sparse authoritative
maps with immutable region snapshots. Construction has separate fractional IDs.
Ordinary faces are greedily meshed; partial block forms use explicit geometry.
Near tiles upload render/collision on the main thread under soft budgets. Distant
LOD meshes cover a ~6 km horizon. No disk world persistence is active.

Critical integration constraint: Generator::terrain_sample and make_volume must
call the same cave evaluator. Sampling a cave only during meshing would leave
invisible solids in raycasts/collision and differ after reload. Carving must run
before surface material classification, and authoritative edits must run last.

## Implementation sequence and gates

1. Audit/plan (this document and operation report). Gate: identify generation,
   representation, chunks, biomes, meshes/collision, streaming, threads, edits,
   registries and existing optimizations. Independent read-only investigators.
2. Global planning: immutable deterministic regional chamber graphs. Stable IDs,
   canonical cross-region connectors, spanning-tree connections plus optional
   loops, deliberate isolated side chambers, entrances, vertical links. Bounded
   cache and per-chunk relevant primitive query. Gate: seed/order/negative-region,
   graph reachability and boundary portal tests. No scene access in workers.
3. Geometry integration: ellipsoid chambers + curved capsule tunnels/shafts,
   bounded geological warp, host-rock and roof constraints. Same point evaluator
   for scalar and bulk paths; retain all solid logical cells. Gate: chamber/tunnel
   carving, seams, sample/volume equality, mine/build and edit-last authority.
4. Four depth bands: normal/deep/abyssal/infernal transitions relative to host roof
   and world floor; enough vertical room for monumental chambers, without widening
   every distant tile's voxel allocation. Decide vertical subregions from measured
   memory; do not cut world/view quality to claim optimization. Gate: all levels,
   transitions, mountain headroom, rare infernal regions and finite bounds.
5. Registries: configurable archetypes, geological parameters and underground
   biomes with depth/heat/moisture/geology/water/surface weights. Freeze/desert/
   forest/tropical effects decay with depth; independent fungus/crystal/fossil/
   flooded/abyssal/corrupted/hydrothermal/infernal combinations. Block definitions
   and feature records for ice, sediment, moss, roots, basalt, crystal, pillars,
   collapse rubble, fossils and anomalies. Gate: data validation, occurrence,
   materials, combinations, editability and extension without generator branches.
6. Hydrology: graph/basin water levels, ocean-access connections, isolated lakes,
   connected river passages, flooded tunnels, air pockets, geothermal lava/thermal
   pockets and sealed dry/wet boundaries. Static deterministic fluid assignment,
   using existing water material/rendering, not a full fluid simulation. Gate:
   cross-chunk levels, ocean access, containment/no leaks and mixed dry/wet rooms.
7. Future resources: declarative biome+depth+region abundance weights and mod IDs,
   with no ore placement, new ore items, textures, veins, metallurgy or recipes.
   Environmental sample hooks expose oxygen/pressure/temperature/visibility for
   future diving equipment, without implementing equipment mechanics.
8. Session persistence: carve result then apply sparse edits and construction.
   Gate: mine/build, flight eviction/return, regenerating same region preserves
   session edits; `world new` intentionally clears them. No old-world migration.
9. Debug: English console commands/map overlays for graph, chambers, connections,
   depth/biomes/flooding/chunk bounds, generation/cache/mesh/collision metrics.
   Gate: inspect actual generated network and teleport to each type/depth/biome.
10. Performance/final validation: repeated deterministic cold/warm planning,
    per-chunk carving/mesh/collider time, peak memory/cache occupancy and real
    Vulkan frame-time traces during underground flight/edits. Independent hunter
    and judge inspect quality, connectivity, hydration and edits. Same scene and
    time-based path for comparisons, invalidate timed-out samples. Document missing
    GPU/VRAM counters honestly; no fabricated gain claim.

## Ownership and current work

Only the coordinator writes production code. Investigators are read-only.
- Coordinator: docs/caves/**, operation report, caves/**; later generator/JSON/
  debug/test files reserved sequentially after the phase gates.
- cave_architecture_audit: read-only generation/meshing/registries investigation.
- cave_runtime_audit: read-only workers/streaming/limits/state investigation.
- Independent hunter/judge: assigned after an integrated candidate exists.

No optimization is accepted from file size or a single FPS sample. Existing known
CAB-001 benchmark-timeout and CAB-002 frame-driven-path issues must be excluded or
corrected before relying on old benchmark runners. The broad core suite now passes
234,925 checks in the current workspace; this is not a final cave performance gate.

## Current progress
Audit gate recorded in AUDIT.md; functional headless baseline passes. Pure planner
tests now cover archetypes, omitted thin-rock rooms, constrained paths, true shafts,
optional infernal levels, entrances and intentionally isolated chambers. The field
uses indexed ellipsoids/capsules and immutable byte/count-bounded plan caching.
Queries retain their data through eviction. Small FastNoise3D detail deforms the
field, rather than replacing structural planning with noise.

Scalar/bulk LOD0–2 integration is enabled by default in `caves.json`, with live
bounds -448..320 and a bounded vertical render window for far LODs. The worker-only runtime batches
surface noise into a bounded roof atlas and shares scalar tile queries per thread.
The runtime world does not initiate cold cave planning for unloaded block probes;
explicit unknown-chunk movement/ray gates wait for loaded voxels. Both
paths carve logical solid voxels before materials; exposed deep floors remain
rock and voids are filled only by the hydrology plan. Player edits are applied last.

`tests/run_cave_integration_tests.sh` passes scalar/bulk equality, shared chunk
halos, dry rock floors, edit-last authority through volume disposal/regeneration,
and clean state for a fresh WorldModel. This is a CPU integration fixture, not
proof of player collisions, rendering, or frame-time stability. The fixture uses
-384..320; the live default remains -64..320. No disk file is used or needed.

`caves.json` now declares archetypes, four depth targets, frequencies, roof grid,
cache budgets and detail. Underground biome/hydrology/formation configuration is
still pending. Coarse LODs above 1 currently omit cave interiors: streaming a
player underground and rendering transitions still need a dedicated gate before
enabling the feature by default. Deeper bounds, independent biomes, hydrology,
debug visualization, complete formations and final measured runtime remain open.

Independent prototype review found and reproduced commutative region-ID collisions,
non-finite corner sampling and extreme-coordinate overflow. These were corrected
with sequential key hashing, per-probe validation and explicit world-region bounds;
regression tests pass. An expanded independent voxel sweep reproduced and fixed
CAV-003 (an omitted shallow hub could erase deeper hubs while neighbors retained
their portals). Canonical hubs now survive, with attached side rooms only. Post-
fix six-neighbor flood fill passes 576 passages/258 portals and 18 full chamber
cases over three varying roof profiles. Real Generator validation also passes
seven connector cases and two full chambers using 39 generated LOD0 volumes,
including negative coordinates. Runtime player/Jolt landing and fractional cave
construction pass in the fixed-seed Godot fixture.

Actual-voxel roof clipping remains required in addition to atlas probes; these
finite tests cannot prove every possible terrain/seed. Full streaming/LOD visual
seams, total peak memory and real frame-time gates remain open.

Concurrent workspace changes (liquids, assets, player, console and rendering) are
owned elsewhere. Preserve them; do not commit the complete dirty tree as cave work.
The cave water/lava pass reuses the existing block/liquid registry and player
liquid query; generated reservoirs stay static.

## Current stage: depth and underground biomes
Coordinator owns new cave_depth.*, cave_biomes.*, cave_biome_data.cpp, the cave
JSON catalog and narrow Generator/material/debug integration. Depth and biome
pure suites now pass. Surface climate precursors are taken before visual biome
dithering. Column caches only spatial/geological multipliers; the actual point's
depth and climate select biomes. This preserves intermediate climatic niches and
makes surface influence exactly zero at sufficient depth. Environmental properties
include every biome contribution before summarizing the dominant material pair.
One shared rock helper replaces deep-rock and carved-floor bypasses in scalar and
bulk. Material references are resolved and checked before workers can start.

Native equality, four-depth bounds and loaded JSON validation passed, with real
runtime climate samples and deep collision fixtures. Geological rules are now
resolved into supported voxel formations, with scalar/chunk equality.

## Hydrology

The coordinator owns the new catchment solver, opening probe, volume provenance,
liquid mesher integration and cave environment hooks. Partitioned plans keep
canonical inter-region portals within each catchment. A border reserve keeps
neighboring catchments from touching through cave voxels. A shared static plane
is selected by exact exterior water contact or a seeded underground depression.
Lakes/lava cannot rise above sea level; selected water galleries connect lower
chamber areas and carry a limited current. Basins are aquifers, so one source
does not prove passage reachability for each isolated chamber.

Godot checks have demonstrated a water/air boundary in a real loaded chamber,
submerged pressure and oxygen hooks, Jolt passing through the water, and edited
liquid/paint preserved through eviction. Native checks cover actual coastal
connections, two fluid types, exact coarse caps, negative-coordinate basin
borders, concurrent planning and local edits. Geological placement and resource
metadata are integrated. The final native/runtime/visual and performance gates
are recorded in COMPLETION_AUDIT.md and the operation report.
