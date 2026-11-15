# Native procedural caves

The ordinary project generates caves in LOD0–2 with vertical bounds -704..320.
The game creates a disposable world at startup and with `world new`.

`data/caves.json` sets the four target depths below local terrain: 44, 132, 300
and 525 m. A region's principal route winds between these levels. The third
level reserves a room of `landmark_archetype`, set to `giant` by default.
Surface openings include hillside mouths, shafts and ravines. An opening is
planned only when the local route connects to the landmark. Use `cave entrances`
to find the openings in the loaded region.

Regions are 224 m wide. `branch_chance` adds short side passages that end in
small alcoves. `natural_entrances` looks for a horizontal approach through a
hillside first. It also gives the ravines bends and the shafts sloping sides.

`flooded_basin_chance` controls which catchments may open into surface water.
Other catchments look for dry entrances. Inland lakes use the deepest layer by
default, leaving dry upper galleries for exploration.

`biome_variation` controls the underground rock palette. Uniform stone lets
chunks represent deep solid rock implicitly. `exploration_formations` selects
a small set of supported formations from `data/cave_biomes.json` for the
default profile. The solid-color blocks used by these formations are listed in
[Assets](ASSETS.md).

## Session edits

Persistence means **RAM state within the current session**. Generated geometry is
reproducible; `WorldModel` owns authoritative sparse block and appearance edits.
Workers generate a chunk, carve caves, choose materials, then apply those edits
last. Evicting a rendered chunk does not delete the edits. A new world clears them.
There are no cave world files, migrations, old-world readers or disk journals.
The original request's save/load clauses are superseded by the user's clarification.

## Implemented foundation

- `cave_plan.*`: deterministic seed/dimension/region graphs, configurable chamber
  profiles, curved tubes, aligned shafts, optional entrances and isolated pockets.
- `cave_field.*`: ellipsoid/capsule carving, bounded detail, roof/floor guards,
  spatial candidate bins, immutable query ownership and an LRU plan cache.
- `cave_runtime.*`: batched FastNoise roof atlases and shared scalar/bulk queries.
  Workers build cold plans; loaded game queries do not initiate planning. Movement
  blocks at unknown chunks and ray interaction stops at unknown terrain.
- `cave_data.cpp` and `data/caves.json`: checked declarative configuration. New
  archetypes declare an ID, depths, min/max radii and occurrence weight.
- `cave_depth.*` and `cave_biomes.*`: four blended depth bands and nine configurable
  underground biomes. Climate, geology, water and declining surface influence
  select the rock and environmental conditions independently of surface regions.
- `cave_hydrology.*`: deterministic 3×3-region catchments keep a single liquid
  plane across chunk borders. Inland lakes, tidal entrances and rare lava basins
  use existing liquid blocks. Selected wet galleries between chambers carry a
  small current. A thin packed provenance mask keeps the generated liquid static.
- `terrain_mesh.cpp`: logical carving and liquid assignment in LOD0–2 before
  greedy mesh generation and edits. Liquid faces respect the actual plane at
  coarse LOD and never enter the Jolt triangle collider.
- English debug command `cave` reports a resident network; `cave tp <0..3>` visits
  an existing chamber, and `cave environment` reports the current climate, basin,
  liquid, immersion, pressure, oxygen and visibility. These reads use loaded
  volumes and never start a cold plan. No diving equipment or damage system is
  activated by those hooks.
- `cave map` toggles a line view of the resident network: depth-colored chamber
  outlines and passages, biome marks in room centers, water/lava planes and the
  current chunk boundary. Leave the view off for performance comparisons.
- `cave formations` lists nearby geological proposals and their supported
  voxel counts. `cave feature <index>` visits their chamber and aims toward them.

Adjust region size, depth transitions and lake/lava chances and fill fractions
in `game/world_generation/data/caves.json`. `hydrology_basin_regions` accepts 2–4
(0 disables cave fluids and catchment partitions). Geometry at catchment borders
stays inside a rock reserve. Inland water is limited to sea level because the
surface arch generator does not report its spill points; the plan's source
describes the catchment aquifer, not ocean graph reachability of every chamber.
Add a biome to `data/cave_biomes.json` with unique ID, existing solid rock key,
four depth weights and optional climate ranges. A biome with `requires_water`
is eligible in loaded cave water and directly submerged cave floors.

Future resource rules live in the top-level `resource_rules` array of
`data/cave_biomes.json`. Link a rule ID from any biome's `resource_rules` array.
The optional metadata query `BiomeStage::resource_abundance` returns a relative
weight after depth, biome blend, geology, geothermal, water and deterministic
regional variation. An example definition:

```json
{
  "id": "example:deep_aquatic",
  "resource": "example:future_mineral",
  "abundance": 1.5,
  "depth_weights": [0, 1, 0.5, 0],
  "region_scale": 720,
  "regional_variation": 0.35,
  "requires_water": true
}
```

The ordinary catalogue leaves this array empty. Resource IDs are independent
of the block registry; these rules do not place voxels or register items.

`cache_bytes` is split into 5/8 plan storage, 1/4 roof atlas storage and 1/8
formation support masks. Hydrology has a separate count-bounded compact cache. Metrics
estimate resident owned storage; they do **not** claim to bound process RSS, worker
scratch buffers, active Queries or one retained scalar query per native thread.

## Checks

| Runner | Scope |
| --- | --- |
| `tests/run_cave_plan_tests.sh` | Seed/order/portals, graph references, bounds, omission, profiles, entrances and shafts |
| `tests/run_cave_field_tests.sh` | Carving, host guards, concurrent bounded cache, eviction lifetime, shared macroregion seam |
| `tests/run_cave_connectivity_tests.sh` | Six-neighbor voxel flood fill of paths and full chambers under three varying roof profiles, with matching canonical portals |
| `tests/run_cave_integration_tests.sh` | Real terrain scalar/bulk equality, dense entrance shell, chunk halo, dry rock, edit-last regeneration, reset and invalid/extreme config |
| `tests/run_cave_entrance_tests.sh` | Explicit paths reach real terrain openings, including the previously sealed slope |
| `tests/run_cave_hydrology_tests.sh` | Lake, lava, graded river, coast contact, exact LOD caps, all halos, and authoritative edits |
| `tests/run_cave_formation_tests.sh` | Eleven geometry families, supported components, tall pillars, actual world placements, all three LODs and edits |
| `tests/run_cave_runtime_tests.sh` | Disposable Godot project with caves enabled: player/Jolt interior collisions, slab/cube construction, actual eviction/return for blocks/removals/paint, no cold paint planning, invalid JSON rejection and fresh-world reset |
| `tests/run_cave_hydrology_runtime_tests.sh` | Disposable Godot lake: submerged air/pressure, Jolt passing through water, static fluid and edit continuity after eviction |
| `tests/run_ephemeral_tests.sh` | Actual ordinary project has no save/load API and starts fresh test worlds |

The native cave runtime fixture copies configuration into `/tmp`, uses -448..320
for four-band headroom, and excludes unrelated grass/console autoloads. It does not
change the user's generation defaults. Its runner fails on timeout, runtime errors
or leaked instances. The separate ordinary-project runner retains all autoloads.

Independent reviewers also ran the pure suites with UBSan and `-fno-exceptions`.
An additional read-only reviewer fixture compared 470,456 subterranean LOD1
samples, including negative coordinates and halos. That excludes the surface band,
where different voxel steps intentionally classify exposure differently.

## Validation and limits

See `COMPLETION_AUDIT.md` for the current requirement gates and
`REGISTRIES.md` for configuration/extension instructions. Native CPU measurements
and rendered Vulkan measurements are recorded separately in the operation report.
