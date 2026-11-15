# Cave goal acceptance audit

Source: REQUEST.md, identical to the pasted request. The user's later directions
replace disk save/load with session edit continuity and defer the surface redesign.
Accepted after the final native and runtime gates, independent geometry and
hydrology audits, ordinary Vulkan smoke test and three clean measured RTX4050
trials. The operation's raw/final-manifest.json records the accepted library,
source/config hashes and exact required markers.

| Request | Concrete implementation | Evidence |
| --- | --- | --- |
| 1: complete modular caves, main world/dimension separation | Native cave modules, region/dimension salts, checked JSON, same existing voxel engine | PLAN.md, AUDIT.md, REGISTRIES.md, seed/dimension tests |
| Phase1: ten-point technical audit and plan before structural work | Existing generation, volumes, chunks, biomes, meshes, workers, edits, registries and optimizations inspected | AUDIT.md and operation report, independent native agents |
| 2: hierarchical deterministic structural/geometry stages | Region graph, canonical portals, ellipsoids/capsules, constrained roof/floor, bounded FastNoise detail, cached indexed queries | planner/field suites, 49-region order fixture |
| 3: normal/giant/rift/shaft/collapse/special archetypes | Six default room profiles, curved tunnels, depth shafts, rubble, geode/crystal/fossil/pillars | catalogue; formation suite exercises every geometry family in the actual Generator |
| 4: connected main graphs, alternative paths, intentional isolated pockets | Hub links/optional loops, canonical neighbouring geometry, terrain omission, isolated-room clearance against own/eight-neighbour geometry | 576 passage /258 portal flood fill, real Generator chambers, independent 120-seed isolation sweep |
| 5: four configurable depths, rare geothermal/infernal | Continuous depth weights, decreasing surface influence, coherent geothermal field, live floor -448 | depth suite; live four-band Godot distribution |
| 6: surface influence | Frost/ice, desert sandstone/sediment, moss/roots, humid vegetation and volcanic influence metadata; fade at depth | biome climate/fade tests; frost/sediment/root formation conditions |
| 7: independent underground biomes/combinations | Limestone, fungal, crystal, fossil, flooded, abyssal, corrupted, hydrothermal, infernal; point climate/depth/material selection | loaded JSON validation and distribution, frozen-crystal/abyssal-thermal samples |
| 8: extensible declared biomes | IDs, depth/climate/geology/water, materials, formation/entity/resource extension lists | cave_biome_data.cpp, BiomeStage validation, REGISTRIES.md |
| 9: fully/partly flooded rooms, lakes, tunnels and river galleries | Consistent catchment table, source chamber depression, shared wet passages/current, water below and air above, ocean and rare lava | hydrology suite, real coastal mouths, submerged/dry room checks, real Godot lake |
| 10: detect sources, depressions/levels, contain liquids, chunk continuity | Exact noisy opening/host-water face contact, protected catchment borders, one immutable table, static provenance, exact coarse liquid caps | independent 75-basin source sweep and 91-tile non-entrance shoreline probe; all four halos |
| 11: diving preparation | Loaded fluid/immersion, oxygen, pressure, temperature, visibility, environmental-damage/current hooks | cave environment API and Godot submerged water/air assertions; no equipment or damage system added |
| 12: future ore configuration only | Namespaced rule/resource IDs, relative abundance, depth/geology/geothermal/water and coherent regional variation, biome links | positive/negative rule tests; no production placement caller; ordinary rules array empty |
| 13: asynchronous generation/cache/culling/LOD | Native workers, immutable indexed plans/atlases/support, per-bin carving, near LOD0–2 and bounded far render window | concurrency/lifetime/byte-budget fixtures; scalar/LOD/far geometry comparisons; measured CPU traces |
| 14: hidden faces culled, solid logical data retained | Greedy exterior/interior faces, full near volume, static liquid separated from Jolt, compact texture-independent collision rectangles | visible-face/coverage tests; Jolt floor and procedural slab fixtures |
| 15: preprocessing/local generation/streaming split | Region/hydrology/support plans versus chunk carving/material/mesh and main upload; ready gates and dynamic eviction | native worker code, loaded-only debug probes, CharacterBody unloaded teleport regression |
| 16: player modifications | Authoritative sparse RAM edits after generation; ordinary blocks, fractional construction, paint and removals survive eviction | native formation edit-last, Godot actual eviction/return/reset; no world directory |
| 17: modular architecture/registries | Separate planning, field, depth, biome, hydrology, formations, data parsing, world model and render adapters | REGISTRIES.md and source modules |
| 18: debug networks/chambers/connections/depth/biomes/flood/chunks/times | cave map, cave/formation inspection/teleports, cave environment, existing metrics overlay | Godot map on/off and no-cold-plan assertions; Vulkan map capture |
| 19: deterministic/geometry/biome/hydro/edit/performance tests | Versioned native and disposable Godot runners fail on errors/timeouts/leaks; raw traces and source/config hashes | 17 accepted native/runtime markers; three accepted GPU traces and process RSS samples |
| 20: incremental execution | Audit → graph → field → depth/biomes → fluids → metadata/forms → integration/optimization/validation | PLAN.md and chronological operation report |
| 21: final completion criteria | Every preceding row is supported by current-state evidence, including final native/runtime/rendered runs | Final manifest, requirement matrix, independent reviews and operation report |
| 22: delivery artifacts | Summary/architecture/files, extension examples, test results, timings/memory and known limits | README.md, REGISTRIES.md, operation report and final response |

## Deliberate limits

- Edits persist for the current session. New tests intentionally start fresh; no
  disk world files, migrations or old-world compatibility are part of this goal.
- Aquifers have one level per finite catchment, and inland fluids are capped at
  sea level until the surface arch system reports arbitrary high spill points.
  The aquifer source does not claim graph reachability of every isolated room.
- Liquid generation is static. It uses current blocks/rendering/player queries;
  it does not implement a complete fluid simulation or diving equipment.
- Nearby logical geometry remains full resolution; thin decorative details may
  simplify at distant LOD. CPU fixtures do not establish GPU performance.
- Native/profile sweeps are finite evidence with documented seeds/conditions.
  Frame measurements are workload/hardware-specific, not a universal FPS guarantee.
- The river/ocean/beach/asset redesign in DEFERRED_SURFACE.md is a separate task.
