# Debug console

F1/backtick opens the plain console. Enter executes; Up/Down navigate history;
Esc closes it. Player movement stops while typing. Commands are always in English.

| Command | Behavior |
| --- | --- |
| `world new [seed]` | Save current world; generate a new random or explicit-seed world during play; reload data files |
| `fullbright [on|off]` | Toggle terrain lighting/shadows, retaining horizon fog |
| `paint #RRGGBB [preserve] [brightness]` | Tint the targeted block using the shared HSV shader; preserve: 0–1, brightness: 0–2 |
| `paint reset` | Restore the target's procedural color |
| `overlay grass` | Add a surface overlay without changing the base voxel |
| `overlay pebbles1` | Add a pebble decoration above the supporting block |
| `overlay clear` / `overlay reset` | Remove overlays or restore procedural/default overlays |
| `biome` | Show current biome, temperature and mountain-region mask |
| `biome normal` / `snow` / `desert` / `jungle` / `enchanted` | Teleport to the nearest matching biome surface |
| `stats` | LOD counts, pass/generation timings, geometry, textures, decorations, memory and edits |
| `seed` | Active seed |
| `fly [on|off]` | Flight / walking |
| `speed 30` | Flight speed (1–150) |
| `tp 0 200 0` | Teleport |
| `block altaria:sand` | Select a placeable block |
| `spawn altaria:marker` | Spawn a data-defined placeholder entity |
| `save` | Commit world/player metadata |
| `help` | List commands |

`paint at x y z #RRGGBB [preserve] [brightness]` and
`overlay at x y z key` target an explicit coordinate for debugging.

Block and appearance changes append to their journals immediately. `world new`
keeps console history/fullbright state and saves the previous world. Generation
completion prints first-tile and total current-view timings; `stats` also exposes
biome/terrain/surface/meshing pass timings.

Environment/build commands (English):

- `shape cube|slab|vertical_slab|ramp|stairs|roof|pillar|mini_pillar|terrain` selects the held form/tool.
- `terrain [at x y z] [on|off|toggle]` applies/removes the independent smooth modifier.
- `shape at x y z <name> [rotation 0..3]` changes an existing material's form.
- `time <0..24|pause|resume>` sets/pauses/resumes the continuous day cycle.
- `weather clear|rain|storm` changes weather lighting and temperature.
- `wind <0..3>` changes wind animation and apparent temperature.
- `outline on|off` toggles the thin world contour.
- `pixelation on|off` toggles low-resolution scene rendering and subtle extra distance pixelation; UI stays sharp.
- `godrays on|off` toggles volumetric sunlight and screen-space light shafts.
- `fog` reports the current color, distances and automatic/manual mode.
- `fog #RRGGBB [start end]` overrides fog color and optionally its distances in metres.
- `fog biome` restores blended biome/day-night fog.
- `temperature` reports ambient, block contribution and apparent temperature in °C.
- `light #RRGGBB [energy .1..20] [radius 1..64]` places a persistent colored light (max 16).
- `cut [at x y z]` cuts a connected upper tree into one Jolt compound body.
- `physics [at x y z]` releases one material/shape into Jolt.
- `physics clear` removes all physical debris from the current world.

Hold R + mouse wheel to select shape, Q to rotate, G to release a targeted block.
Left-click wood to cut a tree, or fallen tree parts to detach them. Single physical
blocks are removed on left-click. Existing block placement and paint commands work
with all forms.

Right-click placement uses the selected face's actual height, snapped to eighths
of a metre. Cubes touch slabs directly; independently painted/materialed slabs can
share a single terrain cell. Selection, removal and ordinary `paint` / `overlay`
commands address the exact constructed piece. The `at x y z` variants address
integer terrain cells. Construction changes append to `construction.log` and
survive world reloads. Manual fog and visual toggles are saved with the world.

Ramp placement on a slab creates a half-rise ramp above its actual top. Side
placement creates a filled ramp from slab height to ordinary block height.
Perpendicular neighbors produce inner/outer corners; removing a neighbor updates
the joint. These connections work between construction and world-cell ramps.
`outline` traces broad silhouettes and shorelines independently of distance by
default (`outline_distance_start: 0`, `outline_distance_end: 0`), suppressing
internal block seams and isolated cubes while outlining nearby hills as well as
distant landforms. Procedural surfaces use full blocks and slabs; ramps are a
building form.

**T** toggles Terrain on the target. With `shape terrain`, right-click applies or
removes it and the preview shows the affected neighbors. Smooth triangles use the
original owner for selection, paint and mining. `stats` exposes `terrain_patches`,
`terrain_pending`, `terrain_triangles`, `terrain_max_build_ms`,
`terrain_max_upload_ms` and `terrain_max_refresh_ms`. See [Terrain](../world_generation/terrain/README.md).

Terrain applies only to an exposed cube/floor slab with a normal base underneath.
It is a surface cap, up to one block high. Ordinary construction replaces the cap
from the base plane; mining it frees the cell and leaves the base ordinary.
