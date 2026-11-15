# Cave registries and execution

The cave system uses the project's native voxel generator, FastNoise2 and Jolt.
It does not depend on Terrain/Voxel Tools or create a second terrain engine.
The source request is preserved in REQUEST.md; session-only persistence and
cave-first priority supersede its original disk-save clauses.

## Configuration

| File | Purpose |
| --- | --- |
| game/world_generation/data/caves.json | Enable, region size, four target depths, blends, archetypes, opening probability, hydrology, caches, detail and cave LOD limit |
| game/world_generation/data/cave_biomes.json | Underground climate/rock rules, formations and future resource abundance metadata |
| game/world_generation/data/blocks.json | Existing solid/liquid block IDs and materials, including cave rocks and decorative crystal/fungus/fossil blocks |
| game/world_generation/data/generation.json | World floor/ceiling, workers and streaming/upload budgets |

Settings are loaded for each new test world. Existing sessions are immutable;
use `world new <seed>` to test a change. No migrations or old-world reader exist.

## Add an archetype

Append a unique definition to `caves.json.archetypes`, then set its occurrence
weight and depth range. Radii are three components, in metres along X/Y/Z:

```json
{"id":"example:wide_hall","min_depth":1,"max_depth":2,
 "radius_min":[35,14,28],"radius_max":[60,28,52],"weight":0.5}
```

The planner fits the shape into available host rock. A room that cannot fit is
omitted, not clamped against another depth category. Graph hubs and passages have
canonical IDs across regions and optional infernal levels.

## Add a biome

Append a definition with a unique ID, existing solid rock key, four depth weights,
and optional heat/humidity/geology/geothermal ranges. Ranges are normalized 0–1;
`range_feather` controls soft edges. The configured fallback must cover every
depth and climate. Use the `formations` and `resource_rules` lists to link
registered definitions. A water-only biome declares `requires_water: true`.
Material references to air, water, lava, bedrock, wood or foliage are rejected.

Surface frost, desert sediment and forest humidity/root influence decline to zero
with depth. Independent deep climate continues below that limit. Temperature,
oxygen, visibility and environmental damage factors are hooks; they do not add
equipment or a new player damage system.

## Add a geological formation

Register a rule in the top-level `formations` array, then reference its ID from
one or more biome definitions. Available shapes are `stalactite`, `stalagmite`,
`pillar`, `rubble`, `crystal`, `geode`, `fossil`, `root`, `fungus`, `vent`
and `ice_sheet`. Several IDs may reuse a shape with different materials/sizes.
The default catalogue uses this for volcanic spires, frozen drips, forest roots,
sediment piles and corrupted formations.

```json
{"id":"example:blue_crystal","kind":"crystal",
 "rock":"altaria:crystal_stone","accent":"altaria:crystal_tip",
 "count":3,"chance":0.65,"radius":[3,5],"height":[6,12],"water":"any"}
```

`water` is `any`, `dry` or `wet`. Optional `archetype` restricts a rule to
a chamber profile. Minimum frost, rooted, sedimentary and geothermal factors
provide surface/geological conditions; frost/root/sediment rules may also apply
outside a biome's explicit list. Each room retains at most twelve deterministic
placements, with seed-derived angles, offsets and sizes.

Formations are ordinary logical blocks. Their solids are carved/filled in the
same scalar and chunk paths, meshed normally and included in collision. Player
edits override them. They are not decorations drawn over inaccessible rock.

Geometrical proposals are qualified on native workers. A six-neighbour traversal
retains only pieces connected to actual uncarved host rock after geological
detail, natural-arch and corridor clipping. Ice can rest on water. Every planned
tunnel and the central chamber column remain free. Results are immutable,
cached and reused at chunk borders. Very thin details may simplify at coarse
LOD; the nearby logical representation remains full resolution.

## Future resources

The ordinary catalogue leaves top-level `resource_rules` empty. See README.md
for an example. Register namespaced resource IDs and relative abundance, depth,
geology/geothermal, water and regional variation. Link the rule ID from biomes.
The optional `BiomeStage::resource_abundance` query has no placement caller.
No veins, ore blocks/items, mining progression or metallurgy are created here.

## Worker pipeline

1. Validate data/material references before starting jobs.
2. Batch surface/climate columns; reuse the conservative roof atlas for planning.
3. Query cached region graphs and spatially indexed chamber/tunnel primitives.
4. Reuse a catchment water table and qualified formation support masks.
5. Carve host solids, assign static fluid and formations, then classify rocks.
6. Apply session block/paint/shape edits last.
7. Greedily mesh visible faces. For close Jolt collision, merge coplanar solid
   faces independently of textures/paint and queue compact rectangles. Copy
   fractional/sloped triangle positions exactly.
8. Upload render/collision on the main thread, preserving old chunks until
   replacements cover them. Unknown chunks block movement/interaction.

Cold planning, support traversals and fluid planning do not run in normal
main-thread block/debug queries. `cave`, `cave environment`, `cave map`,
`cave formations` and `cave feature <index>` inspect resident data.

## Memory and performance

The cave cache budget is split into 5/8 region plans, 1/4 roof atlas and 1/8
support masks. Each cache also has a count limit. Hydrology has a separate
count-bounded compact table. Telemetry is an owned-storage estimate: active
queries, worker scratch, queued meshes, Godot/Jolt/GPU allocation and process RSS
must be measured separately.

Support construction uses bounded per-feature scratch and single publication
for concurrent requests. A tall pillar may span the supported vertical world;
it is not limited to the horizontal mask dimension. Solid and liquid data are
retained for nearby chunks, including hidden rock for editing.

Use `tests/run_cave_frame_benchmark.sh` for an isolated headless workload and
`--rendered` for Vulkan with an available display/GPU. It measures wall-clock
frame intervals with a time-based path, fails on timeouts/errors, and records
process peak RSS. Its streaming path intentionally passes through rock as a
stress case. Fullbright screenshots happen after timed samples. These are not
a promise of a fixed FPS on other hardware.
