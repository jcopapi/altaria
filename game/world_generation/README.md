# Voxel generation, biomes and surface layers

F5 creates a fresh world with a random seed immediately. Open the console with F1:
`world new` generates another random world; `world new 1337` reproduces a seed.
The previous world is saved. Esc opens the saved-world menu.

## Data files

| File | Change here |
| --- | --- |
| `data/generation.json` | Terrain shapes, mountain-region frequency, LOD distances, fallback fog and worker/upload budgets |
| `data/biomes.json` | Biome distribution, region size, climate/altitude cooling, terrain multipliers, grass tint, fog palettes/ranges and pebble density |
| `data/environment.json` | Cartoon contour, distance pixelation, fog transition, sky, lighting, wind, temperature and physics budgets |
| `data/blocks.json` | Stable block IDs, collision flags, fallback colors and texture-array slots |
| `data/overlays.json` | Independent surface/decorative layers and their top/side texture slots |
| `../player/data/entities.json` | Player and entity definitions |

`world new` reloads these files. Worlds snapshot their terrain and biome settings,
so changing defaults does not reshape existing saved worlds. Generator versions 1
and 2 are supported; version 1 keeps the original terrain algorithm. New worlds
use version 2.

## Generation passes

The bounded worker pool processes each requested tile through explicit passes:

1. **Biome regions** (`biome_stage.cpp`): deterministic, jittered Voronoi regions;
   optional temperature/humidity-based classification for the production layout.
2. **Terrain** (`Generator::columns`): continental shape, sparse mountain-region
   mask, ridged ranges, erosion masks, valleys, shelves and horizontal domain warp.
3. **Surface materials**: altitude cooling, snow/desert/grass assignment, soil/stone
   layers and biome tint. Height-driven cooling can turn warm highlands into snow.
4. **Voxels and overlays**: density deformation, saved block/appearance edits and
   deterministic pebble scatter on supporting surfaces.
5. **Meshing and upload**: exposed-face greedy meshing on worker threads; a bounded
   number of mesh uploads per frame on the main thread.

`stats` reports the latest uploaded tile's pass timings, total initial-view generation time,
first-tile latency, queued jobs, six LOD counts, texture layers and decorations.
Erosion is a procedural mask, not a hydraulic simulation.

## Five initial biomes

- `normal`: green meadow grass and rolling terrain.
- `snow`: snow-covered surfaces, including transition edges.
- `desert`: sand, flatter terrain and small hills; strongly reduced mountains.
- `jungle`: deeper, richer green grass, denser trees and shrubs.
- `enchanted`: purple grass, using the same grass block and tint shader.

For testing, `testing_layout: true` and `scale: 260` make biomes frequent.
Increase `scale` (for example, to 1500–3000) for larger regions. Set
`testing_layout: false` for temperature/humidity-driven region selection.
`blend_width` controls transitions. `altitude_cooling`, `cold_start_altitude` and
`snow_temperature` control elevation-driven cold surfaces. Each definition has
independent terrain/mountain multipliers, grass RGB, saturation preservation,
brightness and pebble density. New biome variants can be added to the data array.

Mountain ranges use `mountain_region_scale`, `mountain_threshold` and
`mountain_blend`, independently of their height/detail. Raising the threshold
makes mountains less frequent; `mountain_amplitude` controls their height.

## Voxel appearance through the horizon

All six LODs retain voxel volumes and block materials. Generation uses full cubes
and the original half-height slab caps at LOD0. Cliffs and distant LODs retain
voxel faces. Procedural Terrain caps and ramps have been removed; saved generation
snapshots migrate back to slabs. Authored shapes remain readable for compatibility.

| LOD | Tile width | Horizontal sample | Vertical sample |
| --- | --- | --- | --- |
| 0 | 32 m | 1 m | 1 m |
| 1 | 64 m | 2 m | 2 m |
| 2 | 128 m | 4 m | 4 m |
| 3 | 256 m | 8 m | 4 m |
| 4 | 512 m | 16 m | 4 m |
| 5 | 1024 m | 32 m | 4 m |

The default horizon is **6144 m**. Distance reduces sampling detail while retaining
stepped, square faces and block textures. `near_distance`, `middle_distance`,
`coarse_distance`, `distant_distance` and `horizon_distance` control quadtree
subdivision. They must be ascending. `view_distance` supports up to 8192 m.

Radial shader fog follows the viewer's biome, with day/night colors and smooth
transitions across borders. Each biome sets `fog_day`, `fog_night`, `fog_start`
and `fog_end`. Meadow defaults to blue/purple at 1800–5600 m; Desert uses yellow at
55–550 m; Snowfields uses white at 80–780 m; Enchanted uses purple at 180–2200 m.
Jungle has a green-blue haze at 450–2600 m. Rain/storm shorten these ranges.
Fog becomes opaque before the outer render boundary (94% of view distance),
including the screen corners. Camera far clipping extends beyond that boundary.
Fullbright changes lighting, not the fog.
Boundary skirts conceal LOD height differences. Water keeps the same sea surface
height across LODs. The system currently switches meshes without geomorphing.

### Cartoon contour and distance pixelation

The scene renders through `WorldRenderViewport`, a SubViewport at half resolution
by default (640×360 for a 1280×720 viewport). A dedicated camera follows the player
camera in the same World3D. `NearestWorldImage` presents that texture with nearest
filtering; the UI stays at the original resolution. Simulation, collision and
mouse input retain their original world/viewport coordinates.

`shaders/cartoon_world.gdshader` outlines broad depth silhouettes and land/water
boundaries before nearest upscaling. It ignores normal creases, block textures and
small depth steps. A footprint measured in metres rejects isolated cubes and
small protrusions. Plane-distance filtering rejects perspective gradients on flat
ground/water, with a distance-scaled threshold for coarse LOD steps. Shorelines
use a water roughness marker rather than color edges, including at night.
The contour and its highlight work across all distances by default
(`outline_distance_start: 0`, `outline_distance_end: 0`), outlining nearby hills
in front of the player as well as distant landforms before fading into horizon
fog. Highlights multiply the existing color, avoiding bright seams in night scenes.

All visible geometry and the sky share the low-resolution raster. A subtle extra
coarsening grows smoothly with distance from 48 to 2400 metres, independently of
terrain LOD. Samples must remain on the same depth/normal surface, preserving
silhouettes and aerial perspective. Block texture scale and mipmaps remain intact
across LOD boundaries. Terrain retains its voxel silhouettes and slab caps. The
postprocess uses Forward+'s normal/roughness buffer.

Tune these defaults in `data/environment.json`:

| Setting | Default | Meaning |
|---|---:|---|
| `outline_enabled` | true | Enable world contours |
| `outline_width` | 1 | Neighbor offset in render pixels (2 display pixels at default scale) |
| `outline_strength` | 0.9 | Contour intensity |
| `outline_highlight` | 0.3 | Light-facing contour highlight intensity |
| `outline_distance_start` | 0 | Distance below which outlines are invisible (0 = active at all distances) |
| `outline_distance_end` | 0 | Distance where outlines reach full strength |
| `outline_min_depth_gap` | 4 | Minimum landform depth discontinuity in metres; scales up at long range |
| `outline_min_feature_size` | 3 | Surface footprint in metres for rejecting small individual blocks |
| `outline_shoreline_strength` | 0.65 | Additional contour strength on broad land/water boundaries |
| `outline_blacklist_distance` | 48 | Distance where blacklisted materials begin losing their outline (fully faded 16 m later) |
| `pixelation_enabled` | true | Enable scene pixelation and extra distant coarsening |
| `render_pixel_size` | 2 | Integer nearest upscaling factor for the scene |
| `pixelation_start` | 48 | Distance in metres where extra coarsening begins |
| `pixelation_end` | 2400 | Distance in metres where it reaches full strength |
| `pixelation_max` | 2 | Extra distant raster cell width in render pixels |
| `fog_transition` | 1.2 | Biome fog transition response time in seconds |
| `sky_biome_strength` | 0.85 | Biome palette influence across the sky dome and clouds |

Console: `outline on|off`, `pixelation on|off`, `fog`,
`fog #RRGGBB [start end]`, and `fog biome`. Manual fog and effect toggles persist
with the world; `fog biome` returns to automatic colors and distances.
Presentation values are read from the current environment file on every world
load, including existing worlds. Terrain/climate/physics retain their snapshots.
The sky now tints the zenith and cloud shading as well as the horizon. The night
palette keeps readable ambient/moon lighting and preserves the biome's hue.

`data/blocks.json` has an explicit `outline_distance_blacklist`, initially
`["altaria:leaves"]`. Add stable namespaced block keys to extend it. The material
marks these faces in a reserved roughness band of the existing normal buffer;
there is no extra mask viewport or per-block render pass. Both contour and light
rim fade with distance on blacklisted materials.

### Godrays

`godrays on|off` controls both parts of the effect. Native low-density volumetric
fog receives the sun's existing shadows, producing shafts between tree canopies
and terrain. Its range is limited to 96 m, with a 64×32 base froxel configuration
in `project.godot`; GI and ambient injection are disabled. The existing screen
pass adds bounded radial scattering, masked by depth and cloud brightness/chroma.
The sun transmits through thin pixel clouds and adds a warm silver lining.

Tune `godrays_strength`, `godrays_samples` (8–32, default 16), `godrays_reach`,
`godrays_density`, `godrays_length` and `godrays_light_energy` in the environment
file. `godrays_volumetric: false` keeps only the screen effect for a lower GPU
cost. Fullbright disables both, and deep night disables sun scattering. The
screen component fades when the sun leaves the view; the volumetric component
uses actual shadow occlusion within its local range.

Hidden faces are culled. Equal-material/tint faces are greedily merged; texture
arrays and mipmaps are shared. Only nearby full-resolution voxel volumes are
retained. No separate node or collision body is created for each voxel or pebble.
Per-region immutable edit snapshots and tile versions prevent stale jobs from
overwriting newer edits. Queue size and mesh uploads per frame are bounded.

## Blocks and textures

Sand (`altaria:sand`, hotbar 4), stone (`altaria:stone`), dirt (`altaria:dirt`),
grass (`altaria:grass`) and pebble layers all use the supplied 16×16 PNG assets
in `game/assets/blocks/` (`sand.png`, `stone.png`, `dirt.png`,
`grass_top_overlay.png`, `grass_side_overlay.png`, and `pebbles*.png`). Snow,
wood and leaves use generated textures. Texture-array layers are mipmapped and
sampled with nearest pixel filtering. Imported image layers are normalized to
16×16.

Add a block with a unique numeric ID and namespaced key:

```json
{"id":9,"key":"altaria:basalt","name":"Basalt","color":[0.22,0.24,0.26],"texture_layer":0}
```

Layer 0 is white, so it uses the block's fallback `color`. Textured blocks use the
original texture colors. To add a custom texture, append a slot to `textures` and
reference that array index with `texture_layer`. The new block appears in block
selection automatically. Mouse wheel selects beyond the first eight hotbar slots.
Never renumber/delete IDs already referenced by saved worlds. The loader validates
both block and overlay ID manifests.

## Grass blocks and transition overlays

`altaria:grass` is a full tintable block using `grass_top_overlay.png` (texture
layer 3) directly on all of its faces rather than dirt with a self-overlay.
Blocks can define `"transition_overlay": <overlay_id>` in `data/blocks.json`:

- Grass transition (`altaria:grass` overlay ID 1, `"kind": "transition"`): uses
  the alpha-masked `grass_side_overlay.png` on the vertical side faces of any
  different solid block immediately below a grass block (for example, dirt or
  stone beneath grass), composited in the opaque terrain pass with matching
  biome/paint HSV tint and world-space grass variation. Other blocks can reuse
  `transition_overlay` to project their own edge transitions onto connected blocks.
- Pebbles: `altaria:pebbles1`, `altaria:pebbles2`, `altaria:pebbles3` are separate
  alpha-cutout surface decorations. Placement/rotation is deterministic by seed
  and coordinate; density is set per biome. They have no collision or fluid physics.

The supporting block's ID/collision stays unchanged. A transition/surface overlay and a pebble
decoration can coexist. `overlay grass`, `overlay pebbles2`, `overlay clear` and
`overlay reset` operate on the targeted block. `clear` removes both layers at that
coordinate; `reset` restores procedural/default layers. A manually selected overlay
is saved independently of the block type. Small decorative sprites are rendered
only at detailed LOD; the terrain and its block surfaces remain visible farther out.

## Shared HSV coloring

`shaders/block_tint.gdshaderinc` adapts the supplied HSV shader to 3D. The same
function handles biome grass colors and individual player paint. It replaces hue,
blends target/original saturation, preserves texture brightness variations and
keeps source alpha. Compositing is done in sRGB, followed by one linear conversion
for lighting. Biome tint and world-space noise affect `tintable` blocks (`altaria:grass`)
and grass transition overlays while keeping underlying dirt's authored color.
Explicit paint recolors the base block and visible overlays.

Looking at a block within reach:

```text
paint #e13943
paint #326dff 0.35 1.15
paint #ffffff 0 1
paint reset
```

The optional values are saturation preservation (0–1) and brightness (0–2).
For reproducible debug tests, `paint at x y z #RRGGBB [preserve brightness]` and
`overlay at x y z key` are also supported. RGB/tint parameters and both overlay
slots are attached to voxel coordinates and survive world/chunk reloads.

## Persistence and multiplayer boundary

World folders under `user://worlds/<world-id>/` contain:

- `world.json`: generation/biome snapshot, stable ID manifests, player transform,
  entity placeholders and metadata; committed through temporary-file rename.
- `edits.log`: checksummed block placement/removal records, including explicit air.
- `surfaces.log`: checksummed RGB/tint/overlay/decoration records.
- `construction.log`: checksummed independent fractional pieces, materials,
  transforms, shapes, paint and overlays, keyed by stable piece IDs.

Journals append immediately. Interrupted final records are truncated on load;
other corruption stops loading. Journal compaction is a future extension.

The Godot-independent core accepts typed, sequenced `EditCommand` and
`SurfaceCommand` updates, validates IDs/coordinates/parameters and rejects replayed
sequences. Rendering consumes versioned snapshots. A future LAN/online authority
can use the same state model, but networking/authentication/replication are not
implemented. Native debug APIs must not be exposed directly as unvalidated RPCs.

## Verification

```bash
./tests/run_core_tests.sh
./tests/run_runtime_tests.sh
```

Tests cover deterministic biomes, sparse mountains, desert flatness, altitude snow,
voxel normals at all LODs, coverage through the fog boundary, meshing, raycasts,
paint/overlay attributes, persistence, player controls and in-session regeneration.
`tests/biome_visual.gd` captures each biome and a painted-block strip in a separate
visual test session. The game's runtime systems are C++; GDScript is used only by
the test harnesses.

## Vegetation, atmosphere and block shapes

`data/environment.json` controls the day length/start hour, sunlight/moonlight,
ambient energy, pixel cloud scale/coverage, wind, grass color noise, Celsius lapse
rate, weather temperature deltas, and physical body/part/distance budgets. These
values are snapshotted in each new world's generation metadata.

`data/generation.json` controls voxel tree vegetation as well as terrain:
`vegetation`, `tree_spacing`, `tree_lod_max` (0–2), and `surface_shapes`. Tree
placement uses a seeded, jittered grid and per-biome `tree_density`. Canopies are
voxel geometry through LOD2 by default. Tree definitions currently use simple
wood/leaf cubes with generated bark/leaf textures. 3D grass tufts and surface
bushes have been removed; grass blocks use `grass_top_overlay.png` directly with
world-space color noise (`grass_noise.gdshaderinc`) and project a transition
side overlay onto connected lower blocks.

The eight shapes are `cube`, `slab`, `vertical_slab`, `ramp`, `stairs`, `roof`,
`pillar` and `mini_pillar`. Hold **R + wheel** to choose a form for the held material;
**Q** rotates it by 90 degrees. The terrain pass generates half-height surface
caps. Other forms are available for building through the same material system.
Shape and rotation are stored independently of block IDs in the surface journal.
The reader supports the original paint journal and the new checksummed S2 records.
Exact shape geometry is shared by meshing, selection rays, player AABB collision
and convex Jolt colliders. Player movement also uses exact Jolt shape queries
against detached physical parts. Walking supports small step-up transitions.

### Ramp/slab transitions and corners

`ramp` remains a single held form. On top of a slab it occupies the upper half of
the cell, rising from the slab's surface to normal block height. On a slab's side
it includes the lower filled half and rises from floor height to full height. Side
placement points away from the floor; top placement respects Q rotation. Materials,
paint and individual piece IDs remain independent.

At a shared elevation and rise, perpendicular ramp neighbors form `inner_left`,
`inner_right`, `outer_left` or `outer_right` corners. Straight continuations guard
against unwanted corners. Placement, removal and orientation changes recalculate
only a local neighborhood. Geometry, grass roots, selection, player collision and
Jolt convex hulls share the same straight/corner definitions. Connections are
re-derived from authored placements on reload and persist when released into Jolt.

Constructed ramps connect in both directions with procedural and edited world
ramps. Sparse derived world connections enter immutable worker snapshots, retaining
paint and overlays; they are not extra authoritative journal edits. Edited world
ramps are also reconnected after voxel/surface edits during meshing. Unaffected
chunks do not rebuild, and adjacent construction regions are dirtied when needed.

World generation uses `surface_shapes: true` for original half-height slab caps.
Terrain smoothing is removed. Only ordinary blocks and slabs are generated.
Manual ramps remain available for construction. Older saved modifier flags are
ignored, and obsolete profile forms are read as ordinary cubes while retaining
material, paint and position. Old generation options are stripped on load.

### Streaming frame budgets

`upload_time_budget_ms` (default 2 ms) stops additional uploads once the current
frame's upload work reaches the budget. It is a soft budget: an individual engine
mesh/collider upload cannot be interrupted. `upload_budget` still limits the number
of uploads. The visibility reconciliation compares only retiring LODs and runs when
coverage changes, rather than scanning every pair of chunks on vegetation/edit
uploads. Sky radiance uses a small incremental cache; unused sky reflections are
disabled. Ordinary direct/colored lighting remains available.

`stats` includes upload time and separate mesh/collider/foliage peak timings for
profiling. `tests/stream_benchmark.gd` records median, p95, p99, maximum and slow-frame
counts during fast flight and block edits. See `VALIDATION.md` for measurements.

### Fractional construction

Terrain generation retains integer cells. Player construction uses independent
pieces snapped to an eighth of a metre, so it can share the space within one
terrain cell without replacing that cell's material or lower slab. Right-click
solves placement against the actual selected face: a cube on a slab starts at its
half-height surface; another slab can fill the remaining half. Smaller forms and
rotations use their actual hulls. Placement rejects geometry/player overlap and
requires contact with the selected support.

`construction_core` indexes pieces by occupied cells, with exact convex overlap,
ray selection and player collision. `BuiltWorld` groups static render meshes and
nearby Jolt triangle colliders into 32-metre regions; it does not create a node or
rigid body for each piece. Dirty regions rebuild at a bounded rate. Constructed
grass pieces use the same shared grass MultiMesh and supporting-block tint.

Target selection, left-click removal, `paint`, `overlay` and G/`physics` operate on
the exact piece, preserving its fractional transform and texture anchor through
Jolt conversion. Separate stacked slabs can retain different materials and paint.
`construction.log` restores their stable IDs, geometry and appearance on reload.

Native debug bindings are `place_block(position, block_id, shape, rotation)`,
`get_built_blocks()` and `remove_built_block(id)`. The first returns a piece ID, or
zero on rejection. Existing `get_voxel`, `set_voxel` and `at x y z` commands still
address integer terrain cells. Multiplayer can replicate piece IDs and placements
alongside terrain edits; network transport/authority is future work.

## Jolt trees and physical blocks

The engine's built-in **Jolt Physics** backend is already selected in
`project.godot`. No separate physics extension or second simulation is required.
Only LOD0 meshes get static triangle colliders; water, grass and distant LODs are
excluded. Collider uploads follow the same edit versions as visible meshes.

Left-clicking wood releases the connected upper trunk/canopy, preserving the stump.
A tree is one sleeping-capable compound rigid body, with convex hulls for its wood;
attached leaves share the render mesh and mass, without individual colliders.
Physical blocks keep texture projection in object space, so texels and overlay
faces rotate/translate with the block. A persistent texture anchor preserves the
pattern through save/load and when a piece is detached from a compound tree.
Selection highlights the exact selected cell/form and follows the rigid body's
transform, including rotation. Normal extrusion prevents coplanar highlight faces
on ramps and shortened slabs. Returning to a static target resets that rotation.
Left-click its fallen parts to detach another physical block. Left-click a single
physical block to remove it. **G** releases an ordinary targeted block, preserving
its material, form, paint and overlays. The same flow works for custom solid blocks.

Bodies wait for current nearby terrain collision before activating. Distant bodies
freeze and resume with their velocities preserved; the body/part budgets reject
new conversions when full. `physics clear` explicitly deletes existing debris.
Transforms, velocities and parts are saved with the world and restored after load.
Terrain removals are recorded in the existing edit journal before conversion.
Physics is local simulation for now; future multiplayer should replicate the
server's snapshots/commands rather than expect client Jolt simulations to agree.

## Gameplay temperature

Biome `temperature` remains the normalized noise/climate classification value.
Gameplay uses separate `day_temperature_c` and `night_temperature_c` in
`data/biomes.json`. Values blend across biome borders and vary continuously through
midnight, with a daily high at 14:00 and low at 02:00. Higher altitude cools the air.
`weather rain` / `weather storm` change air temperature, cloud cover and direct light.
Wind changes blade motion and the apparent temperature.

Blocks optionally define `thermal_delta_c` and `thermal_radius` (max 8 metres).
Snow/ice cool their surroundings; ember warms them. Distance uses quadratic falloff.
The strongest cold and warm sources are combined, so a wall of snow cannot add
unlimited cooling. Physical blocks retain this effect after being detached.
`temperature` reports `ambient_c`, `block_delta_c` and `feels_like_c` separately.
This provides the environment query for future survival, clothing or entity logic;
health damage and equipment are not part of this prototype.

The sky shader supplies day/night gradients, sun, textured procedural moon, stars
and drifting pixelated clouds. Toon diffuse/specular lighting, SSAO, gentle glow and warm
sun/cool moon colors preserve support for ordinary colored Godot lights. Environment
updates run at 10 Hz; blade animation runs every rendered frame. Color lights placed
with `light` are limited to 16 and saved. These debug lights have no shadows.

## Environment validation

```bash
./tests/run_core_tests.sh
./tests/run_runtime_tests.sh
./tests/run_environment_tests.sh
./tests/run_cartoon_tests.sh
./tests/run_subviewport_tests.sh
./tests/run_ramp_tests.sh
./tests/run_ramp_visual.sh
./tests/run_silhouette_visual.sh
# Optional real Vulkan visual pass (isolated user data recommended):
GDSTUDIO --path . --script res://tests/environment_visual.gd
```

New core cases cover all eight rotated forms, exact rays/collision, vegetation
consistency, paint on stairs, journal compatibility, and temperature factors.
The integration test exercises half-height player landing, a falling block landing
on terrain, a toppling tree, actual mouse-based part extraction, physical cold blocks,
and climate/shape/debris persistence. The visual script captures grass at two wind
times, daytime/nighttime, moon/clouds, colored lights, the shape palette and a real
falling tree. Shader references and license details are in `licenses/ShaderReferences.txt`.

`./tests/run_foliage_visual.sh` runs `tests/foliage_visual.gd` with isolated user data. It adds a Vulkan regression for dense whole-cell coverage,
compact MultiMesh buffers, nearby-only residency, high-flight unload/return,
physical selection transforms, and texture stability under arbitrary translation,
rotation and saved-world restart. `tests/render_benchmark.gd` measures both a
horizon view and a close view filled with dense grass.

`./tests/run_cartoon_visual.sh` captures outline on/off, distant pixelation on/off,
day/night and biome fog, and compares a painted blade against its exact support
texel in a real Vulkan session. `run_cartoon_tests.sh` covers actual mouse placement
on slabs, fractional selection/painting/removal, player and Jolt contacts, fog
transitions/overrides and saved-world restoration. See `VALIDATION.md` for results.
`./tests/run_subviewport_tests.sh` adds camera/resize/input-space checks and a
frame-by-frame regression for chunk foliage stability while breaking blocks.

The instanced renderer follows the [Godot MultiMesh optimization guide](https://docs.godotengine.org/en/stable/tutorials/performance/using_multimesh.html), with separate spatial batches to retain useful culling.

`run_ramp_tests.sh` exercises actual floor clicks, both ramp heights, all corner
updates, player contact, construction/world connections, mesh rebuilds, save/reload
and physical corners. It also verifies that legacy Terrain/ramp generation
snapshots return to slabs while authored edits survive. `run_ramp_visual.sh`
captures the slab world and manually built ramp joins.
`run_silhouette_visual.sh` compares broad silhouettes, individual internal faces,
an isolated cube and blacklisted leaves in day/night lighting, plus nearby fade
and a coplanar beach/water boundary in an actual Vulkan render.
