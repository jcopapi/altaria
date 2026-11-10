# Full grass block, transition overlays & distance-independent silhouette outlines — 2026-11-09

Native C++ GDExtension, GDstudio 0.4.1 / Godot 4.7.1, Linux x86_64.

- `./build.sh`: successful.
- `./tests/run_core_tests.sh`: **155,994 checks passed**. `altaria:grass` is a
  full block using `grass_top_overlay.png` (texture layer 3) directly on its own
  faces without a self-overlay; different solid blocks directly below grass
  receive the grass side transition overlay (`grass_side_overlay.png`, layer 2).
  3D grass tufts and bushes are removed. Procedural caps are slabs at LOD0;
  distant LODs retain cubes.
- `./tests/run_runtime_tests.sh`: **RUNTIME_SMOKE_PASS**. Six LODs, generation,
  editing, overlays/paint, saves, movement, console and in-session regeneration.
- `./tests/run_environment_tests.sh`: **ENVIRONMENT_RUNTIME_PASS**. Zero 3D grass
  or bush geometry, Jolt tree felling, physical block collisions and saves.
- `./tests/run_cartoon_tests.sh`: **CARTOON_RUNTIME_PASS**.
- `./tests/run_subviewport_tests.sh`: **SUBVIEWPORT_RUNTIME_PASS**.
- `./tests/run_ramp_tests.sh`: **RAMP_RUNTIME_PASS**. Manual ramp placement,
  fractional slab support, corner updates, contacts, physics conversion and save
  restoration.
- `./tests/run_silhouette_visual.sh`: **SILHOUETTE_VISUAL_PASS**, actual Vulkan
  Forward+. Day/night fixtures separate broad silhouettes from internal block
  faces, isolated cubes and blacklisted leaves. Silhouettes are outlined at close
  range right in front of the player as well as at medium and long distances.
- `./tests/run_cartoon_visual.sh`: **CARTOON_VISUAL_PASS**. Nearby and mid-range
  silhouette outlines, distant pixelation, biome/day-night fog, and full grass
  block + transition overlay rendering pass.
- `./tests/run_foliage_visual.sh`: **FOLIAGE_VISUAL_PASS**. Confirms zero 3D
  grass/bush instances and verifies physical block UV anchoring and selection.

## Visual measurements

1280×720 output / 640×360 scene with default nearest pixelation. Fixtures use
fixed geometry and lighting; measurements exclude the background outside each
fixture. Values are mean normalized RGB differences with outline on versus off.

| Comparison | Difference |
|---|---:|
| Isolated cube, day / night | 0.0 / 0.0 |
| Individual faces inside a broad silhouette, day / night | 0.0 / 0.0 |
| Broad silhouette boundary, day / night | 0.020783 / 0.016219 |
| Blacklisted leaf surface, day / night | 0.0 / 0.0 |
| Nearby broad surface (`silhouette_near`) | 0.000563 |
| Nearby podium / hills (`outline_on` vs `outline_off`) | 0.001242 |
| Coplanar beach/water boundary | 0.003268 |

The shader no longer uses normal creases to draw edges. Depth discontinuities
must exceed the landform threshold and have a broad supporting footprint. The
plane filter rejects half-block steps and perspective gradients, scaling up for
coarse distant LODs. Reserved water roughness identifies shorelines without
texture/color-edge noise. Decorations skip the contour kernel. Light rims
multiply existing color, avoiding added bright seams at night.

Outlines now work independently of distance (`outline_distance_start: 0`,
`outline_distance_end: 0`), outlining hills in front of the player as well as
distant terrain while clamping the close-range screen-space support footprint.
Leaves retain their distance blacklist.

## Performance

The retired procedural ramp pass and its larger sampling halo were removed.
Generation again samples a 34×34 column halo, applies original slab caps and
retains cube LODs. Manual connections still update only a local neighborhood.
The existing 2 ms upload budget remains soft: individual engine mesh/collider
uploads cannot be interrupted and may cause streaming spikes.

The final streaming benchmark used seed 1337, an RTX 4050 Laptop GPU, Vulkan
Forward+, 1280×720 output / 640×360 scene and a 6,144 m view, with all normal effects
enabled. Samples start after generation and warmup. Flight traverses about 485 m;
edits repeatedly remove nearby grass supports.

| Scenario | Frames | Median | p95 | p99 | Maximum | Frames >16.67 ms |
|---|---:|---:|---:|---:|---:|---:|
| Fast flight / streaming | 720 | 5.952 ms | 15.008 ms | 17.515 ms | 20.018 ms | 18 |
| Block edits | 360 | 6.036 ms | 8.633 ms | 13.418 ms | 13.817 ms | 0 |

Neither sampled phase exceeded 33.33 ms. Lifetime upload peaks, including startup
and settling, were 17.876 ms aggregate; independent mesh/collider/foliage peaks
were 13.452 / 15.016 / 7.203 ms. These maxima must not be added together. Results
vary with hardware, cache state and desktop activity; streaming spikes remain.

## Configuration and reproduction

`data/generation.json`: `surface_shapes: true` enables the original slab caps.
There is no procedural Terrain/ramp setting. Existing generation snapshots with
either retired mode migrate on load; manual block edits remain intact.

`data/environment.json`: `outline_min_depth_gap` (4 m),
`outline_min_feature_size` (3 m), `outline_shoreline_strength` (0.65), plus the
existing intensity, width and distance fade. Water and blacklisted faces use the
existing normal/roughness buffer; no extra mask camera or full-scene pass.

Run wrappers from the project root; tests use isolated temporary user data.
Visual tests require a graphical session. Captures are in
`/tmp/altaria-cartoon-preview`. Selected images are copied to
`game/temp/slabs-world-preview.png`, `game/temp/slabs-night-preview.png` and
`game/temp/silhouette-preview.png`.
