# Voxel generation, biomes and surface layers

F5 opens the console. Enter `world new` to generate a world with a random seed
or `world new 1337` to reproduce a seed.
Block and appearance edits persist for the current session as chunks unload and reload.

## Data files

| File | Change here |
| --- | --- |
| `data/generation.json` | Continental scale, ocean depth, river and valley widths, beach height, mountains, LOD and upload budgets |
| `data/biomes.json` | Biome distribution, climate/altitude cooling, terrain multipliers, grass tint and fog palettes |
| `data/environment.json` | Cartoon contour, distance pixelation, fog transition, sky, lighting, wind, temperature and physics budgets |
| `data/blocks.json` | Stable block IDs, collision flags, fallback colors and texture-array slots |
| `data/resources.json` | Ore and pebble deposits by host, depth, chance per exposed block, piece count, size, tint, exposed faces and embedding. `enabled: false` pauses these deposits and automatic pebble decals. |
| `data/overlays.json` | Independent surface/decorative layers and their top/side texture slots |
| `../player/data/entities.json` | Player and entity definitions |

`world new` reloads these files and uses the current version 3 surface profile.
`continental_scale` controls the distance between continents, and `ocean_depth`
controls the depth of ordinary open-ocean basins. With `deeper_waterbeds`,
the coastal shelf remains shallow, river pools vary in depth, and offshore
trenches can reach the first layer above bedrock. `river_scale` controls the
spacing of drainage corridors.
`valley_width` shapes their wide floodplains and `river_width` controls their
water channels. `beach_height` caps the altitude of sand on low coastal slopes.
`meadow_hill_amplitude` controls the relief of small hills in lowland meadows.
`mountain_ledges` introduces rock steps of 3, 4 and 5 blocks at 75, 111 and
151 blocks above sea level in mountain ranges.

## Generation passes

The bounded worker pool processes each requested tile through explicit passes:

1. **Biome regions** (`biome_stage.cpp`): deterministic, jittered Voronoi regions;
   optional temperature/humidity-based classification for the production layout.
2. **Terrain** (`Generator::columns`): ocean basins, coastal slopes, rolling
   foothills, cordilleras, erosion and horizontal domain warp. Gradual valley
   floors feed shallow river channels at the existing water level.
3. **Surface materials**: altitude cooling, snow/desert/grass assignment, soil/stone
   layers and biome tint. Height-driven cooling can turn warm highlands into snow.
   Snow cover rises smoothly across cold biome borders, the climate snow line and
   `snow_height`; in the half-covered band a drift noise leaves snow patches on
   the grass and grass patches on the snow. Grass colour follows the biome
   weight, so it changes gradually across a border.
4. **Voxels and overlays**: density deformation, cave carving, grass transition
   overlays and edits from the current session.
5. **Meshing and upload**: exposed-face greedy meshing on worker threads, which
   also convert each surface to engine arrays. The main thread hands over a
   bounded number of tiles per frame (`upload_budget`, `upload_time_budget_ms`,
   `upload_vertex_budget`); a tile that would exceed the vertex budget after
   another upload waits for the next frame. Canopy meshes (leaf cards and
   canopy shadows) follow their tile one frame later, so the two costs never
   stack in one frame.

Frame pacing is judged by the worst frames, not the average: the debug state
and the HUD report `frame_worst_ms` and `frame_p99_ms` over the last 600 frames,
plus `frame_spikes` (frames slower than 33.4 ms since start), `upload_vertices`,
`max_upload_vertices` and `deferred_uploads`.
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
and brightness. New biome variants can be added to the data array.

Lowlands and foothills get distinct bell-shaped hills from `hill_chance`,
`hill_spacing`, `hill_height_min`/`hill_height_max` (blocks) and
`hill_width_min`/`hill_width_max` (base radius per block of height). Each hill has
its own elongation and lobed outline, smaller hills are more common, overlapping
hills keep the higher surface, and the effect fades near coasts and mountain
ranges. `hill_spacing` must be at least `hill_height_max × hill_width_max + 3`.
Worlds saved without these keys keep their original relief.

Mountain ranges use `mountain_region_scale`, `mountain_threshold` and
`mountain_blend`, independently of their height/detail. Raising the threshold
makes mountains less frequent; `mountain_amplitude` controls their height.

## Voxel appearance through the horizon

All six LODs retain voxel volumes and block materials. World generation uses
only cubes and slabs: nearby one-block steps get slabs, and everything else,
including distant tiles, stays greedy cube meshes. Authored shapes remain
available for construction.

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

Surface fog follows the viewer's biome. Each biome supplies day and night colors
and its own distance range. Rain and storms shorten that range. Adjacent terrain
resolutions meet on the same plane using the solid and liquid spans of both
tiles. The ground and its seams swap together once replacement tiles are ready.
Trees and grass crossfade through complementary pixel opacity. Water retains the
same surface height across LODs and varies from blue to turquoise in world space.
Its opacity increases with the depth of the rendered riverbed or seabed.

Adaptive streaming keeps full surface detail near the player and extends it in
the direction of travel. Enclosed caves stream full-resolution blocks within a
local depth window. Diffuse daylight travels through connected openings and
weakens with distance, including where a near-surface opening crosses a chunk
boundary. A tree blocks direct sunlight while the ground below still receives
daylight from the sky. Solid terrain blocks this fill in closed caves, and the
fill goes dark at night. Biome fog and atmospheric rays
apply above ground. Local colored lights cast shadows and can extend the
full-resolution streaming range. Use `light follow` for a camera-mounted lamp,
`light follow #62a6ff` to recolor it, and `light follow off` to turn it off. The night sky remains visible outdoors while unlit ground
becomes nearly black. Collision, selection and dynamic liquids check whether
their height is available. Active rigid bodies can request detailed collision
tiles independently of the visual radius.
Session edits live independently of the streamed geometry.

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
across LOD boundaries. Terrain retains its voxel silhouettes. The
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

Sand, stone, dirt, grass, gravel, trunks and leaves use PNG assets in
`game/assets/blocks/`. Gravel appears in riverbeds and along shallow coasts.
Trunks use `log1.png` on the sides and `log1_top.png` on the ends. Leaves use
`leaves.png` with alpha cutouts. The grass block uses the opaque grass tile,
and `grass.png` supplies the blade sprites. The renderer normalizes texture
array layers to 16×16 and uses nearest filtering with mipmaps.

Add a block with a unique numeric ID and namespaced key:

```json
{"id":9,"key":"altaria:basalt","name":"Basalt","color":[0.22,0.24,0.26],"texture_layer":0}
```

Layer 0 is white, so it uses the block's fallback `color`. Textured blocks use the
original texture colors. To add a custom texture, append a slot to `textures` and
reference that array index with `texture_layer`. Set `texture_end_layer` to use a
different layer for upward and downward faces. Set `alpha_cutout` for textures
with transparent pixels. The new block appears in block selection automatically.

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

`overlay grass`, `overlay clear` and `overlay reset` operate on the targeted
block. `clear` removes its applied layers. `reset` restores procedural layers.
Applied overlays are tracked separately from the block type during the session.

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
values are snapshotted in each new world's generation metadata. Clouds occupy
a moving volume at a fixed height above sea level. Scene depth hides clouds
behind solid terrain and the highest mountains reach above the cloud layer.

`data/generation.json` controls voxel trees through `vegetation`, `tree_spacing`
and `tree_lod_max` (0–2). Candidates sit on a grid of half `tree_spacing`; a
low-frequency forest patch field (170 m and 53 m octaves) gathers them into woods,
widened by biome humidity, while open ground keeps only rare solitary trees.
Trees keep the round, tall (spire), open and broad forms, but each one builds its
own branch layout: branches leave the trunk through one-sided stubs or forks and
grow flat, upturned (elbow + vertical shoots), stepped (out, up, out) or drooping.
The joints are dedicated mini-pillar shapes: `branch_elbow_up`, `branch_elbow_down`,
`branch_tee_up` and `branch_stub` (codes 113–126). Every branch tip carries a leaf
blob (dense core, ragged shell) and spires add a tiered cone, keeping the whole
tree within 248 parts (`max_parts` 256) so it still detaches as one body.
Branches and leaves stay within 5 blocks of the trunk. Shrubs generate
as separate groups of leaf blocks. The visible canopy uses overlapping textured cards.
A separate opaque mesh casts the canopy's shadow.
The middle LODs use a thin trunk under a leaf cube with subtle world-space noise.
More distant LODs keep the canopy through the terrain view range. Each exposed grass block in
the near mesh grows nine tufts by default. The blades sample the supporting
block's texture, paint and world-space color variation. Purple grass generates as separate blocks on top
of mountain rock and dry cave floors. Grass and rock share a world-space color noise function.

The eight shapes are `cube`, `slab`, `vertical_slab`, `ramp`, `stairs`, `roof`,
`pillar` and `mini_pillar`. Hold **R + wheel** to choose a form for the held material;
**Q** rotates it by 90 degrees. A mini-pillar attached to a side face lies along
the face's horizontal axis. These forms are available for building through the
same material system.
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

Constructed ramps connect in both directions with edited world ramps. Sparse derived world connections enter immutable worker snapshots, retaining
paint and overlays; they are not extra authoritative journal edits. Edited world
ramps are also reconnected after voxel/surface edits during meshing. Unaffected
chunks do not rebuild, and adjacent construction regions are dirtied when needed.

The Terrain shape (rounded auto-connecting caps) has been removed. Its codes
(shape 8, packed 192..254) are still accepted when loading old saves and read
as plain cubes. A lone one-block rock column between open cave air renders as
a pillar.

Underground, sight ends where light does (`VoxelWorld::underground_sight`,
56 m without a lamp, further with stronger lights): fog closes to darkness at
that distance, the background is fogged too, and full-resolution streaming
reaches only 40 m beyond it. Inside the underworld a hot orange haze (like the
desert's fog, 34–190 m) replaces the darkness, the stream ring widens to
208 m, embers and ash drift around the viewer, the post pass adds a faint
heat shimmer, and the roof glows with ember cracks. Rock warms toward red
with depth.

SSAO is off by default (`ssao_enabled` in `environment.json`): it darkened
every concave corner, from LOD steps to narrow cave gaps, for a full-screen pass.

Silhouette outlines are off by default (`outline_silhouettes` in
`environment.json`); water shorelines keep theirs. Pebbles and ores use the
materials and outlines baked into their glTF models. The `deposit_model` field
in `blocks.json` selects the model, and `resources.json` controls where deposits
spawn. Instances share meshes within each full-detail chunk.

Thin shapes (Pillar, MiniPillar) don't block diffuse daylight, so dripstone
casts no square dark patches. `tests/run_cap_hole_tests.sh` ray-casts from
cave air beside generated shapes to catch holes in the surface.

Full-resolution tiles upload their ground and liquids as stacked 32 m sections
(`sections.*`), one mesh instance each, so the renderer culls every section
against the view instead of a tile's whole column. Triangles that cross a
section boundary are clipped; the geometry is otherwise identical. In caves
this drew 20–45% fewer triangles. A Minecraft-style flood fill through open
space was tried on top and dropped: caves here are connected enough that it
hid nothing the frustum didn't already, and its turn-back rule left holes.

Workers pack each surface straight into the renderer's own compressed vertex
buffers (`pack_surface` in `voxel_world.cpp`): 16-bit positions within the
surface bounds, an octahedral tangent frame, 16-bit UVs, RGBA8 tint as
`COLOR`, base color and brightness as half floats in `CUSTOM0`, the flag bit
field and side top as 32-bit floats in `CUSTOM1.xy`, and painted, overlay
tint and both texture layers as RGBA8 in `CUSTOM2` (layers stored as
`layer / 255` and `(overlay + 1) / 255`). The main thread hands the bytes to
an `ArrayMesh` without converting anything.

Main-thread streaming work is kept small:

- `workers: 0` sizes the generation pool to the machine (hardware threads
  minus two, 2 to 8). Upload and pool tuning is not part of a saved world.
- LOD seams build on a background thread from a snapshot of the visible
  tiles; the main thread only swaps the mesh.
- The skylight halo samples caves in bulk (one cave query and one noise fill
  per tile) instead of per cell.
- Coarse tiles (LOD 1–2) skip the noise caves, which never reach the surface.
- Past LOD 3 only a share of far tree crowns is drawn (half at LOD 4, a
  quarter at LOD 5), widened to keep the forest's cover.
- Lava and root glow lights show only within 72 m of the viewer, and the
  underworld's lava ocean adds none (its glow is in the terrain shader).
- `profile` in the console lists main-thread scopes (average, worst, calls);
  `profile reset` clears them. The same data is in the debug state.

With `surface_shapes: true`, full-detail tiles soften one-block steps above sea
level so the player can walk instead of jumping. An air cell beside a
neighbour exactly one block higher gets a bottom slab of the ground block, a
half step. Gentle interior ground away from edges keeps the half-height cap
from `Generator::surface_shape`. Neighbours just outside the halo are read from
the bare terrain, so tiles agree at their edges. Coarse LODs stay cubic.

Daylit ground has no side shading. The terrain shader lights every daylit
solid face as if it pointed up, at every LOD: the sun's toon diffuse, the
0.72 side factor and the daylight sampled in front of a side all match the
tops. The surface reads as one rounded form instead of individual blocks.
Faces without daylight (caves) fade back to their real normals, so lamps and
the underworld still shape cave walls. Side faces store any exposure of 4/15
or more as full daylight, matching the shader; this barely changes the face
count (about 0.2% fewer vertices), since most sides were already fully lit.

Deep water hides its floor. The water shader darkens toward an abyss colour
and turns opaque past about 15 m of water. Coarse tiles (step > 1) drop
seabed faces more than 16 m below the surface (`deep_water_cutoff`), and the
far heightfield raises submerged ground to that depth. Ripples, caustics and
crests fade out beyond about 46 m, so distant water stays calm.

New default worlds spawn through `Generator::find_spawn`, which:
- looks for dry plains in a temperate biome (Meadow/Enchanted), within 2 km of
  the origin and away from rivers and lakes;
- requires heights within 1.5 blocks across a 25 m square;
- requires both a cold and a hot region within 2.6 km.
The biome temperature wave places temperate bands between cold and hot ones, so
the nearby land always varies. The search takes about 35 ms. If it finds
nothing, the game falls back to the old scenic spot.

### Organic caves and the underworld

`caves.json` `noise` adds caves alongside the planned chamber graph
(`caves/cave_noise.*`). A 4 m lattice of noise channels is interpolated
per cell, identically in tile fills and scalar `Generator::terrain_sample`:

- **Halls**: irregular 3D blobs with broad rock pillars and one-block mini
  columns. They grow more frequent toward the underworld (`cheese_threshold`
  near the surface, `deep_threshold` at the underworld roof over
  `transition_height`).
- **Tunnels**: winding, branching passages whose width varies from crawl
  size to wide galleries.
- **Crawlspaces** (flat slots) and **fissures** (tall narrow cracks) dominate
  different regions (`region_scale`), so each area has its own character.
- **Lava seas**: enclosed bowls below fixed `lava_seas` levels, 100–200 m
  across, with rock pillars rising out of the lava. Other caves are kept out of
  each bowl so lava never meets open air sideways. Any other cave crossing a
  sea's area fills with its lava below the sea level, so the sea fills the
  whole cavern; a rock ring and floor (40 m down) keep that lava enclosed, and
  the bowl always keeps at least 3.5 m of air over its lava.
- **Underworld**: between `hell_floor` and `hell_ceiling` (with noise
  amplitudes) the whole layer is open: a hellstone floor, a roof, giant flared
  pillars, and a lava ocean below `hell_lava`.

- **Descent pillars**: `descent_chance` of the underworld's giant pillars are
  hollow, with a spiral ledge (one turn per 9 m) around a central drop. Each
  starts in a landing chamber `descent_height` above the underworld roof and
  ends at a doorway on the underworld floor, with windows over the lava sea
  on the way. The roof is otherwise sealed (`roof_seal`), so the descent is a
  deliberate passage and gives the stream time to load the layer below.
- **Lava falls** pour from the underworld roof (`lava_fall_chance`).

No noise cave reaches within `surface_margin` of the terrain. In full-resolution
tiles every vertical run of dry cave air may grow stalactites (Pillar blocks
ending in a MiniPillar tip), stalagmites or, in low gaps, a thin floor-to-roof
column. Dripstone is longer and denser in the underworld. Noise lava is static
until disturbed.
Manual slabs and ramps remain available. Older saved modifier flags are
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
and physical corners. It also verifies that authored edits survive reloads. `run_ramp_visual.sh`
captures the slab world and manually built ramp joins.
`run_silhouette_visual.sh` compares broad silhouettes, individual internal faces,
an isolated cube and blacklisted leaves in day/night lighting, plus nearby fade
and a coplanar beach/water boundary in an actual Vulkan render.
