# Debug console

F1/backtick opens the plain console. Enter executes; Up/Down navigate history;
Esc closes it. Player movement stops while typing. Commands are always in English.

Use `resolution 1920x1080` to set and save the window size. `resolution` shows
the current size and `resolution list` shows presets. The default is 1920×1080.

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
| `item pickaxe\|axe\|clear [left\|right]` | Equip or clear a first-person tool. Defaults to the right hand. |
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

- `shape cube|slab|vertical_slab|ramp|stairs|roof|pillar|mini_pillar` selects the held form/tool.
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
- `light [color [energy .1..20] [radius 1..192]]` places a fixed light at the current viewpoint (max 16). Colors accept names and hex values.
- `light follow [on|off|color [energy] [radius]]` toggles or configures the personal light.
- `log export 30s` saves recent messages, engine errors and warnings, slow frames and chunk queue samples to `user://logs`. The console prints the full path. Requests can cover up to 300 seconds of available session history.
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

## Structure

Start with `world new structure`. The stone anchor is removable. Right-click
in open space to place a piece four blocks along the crosshair.

| Control | Behavior |
| --- | --- |
| `cheat inventory on` / `cheat inventory off` | Enable or disable **T** for the Cheat Inventory. Click a block to add 64 to a matching stack or an empty slot. |
| **V** / **B** | Mark corner 1 or 2 on the aimed block, or four blocks away in open space |
| `structure pos1 [x y z]` / `structure pos2 [x y z]` | Mark by aim or by integer coordinates |
| `structure clear` | Clear both corners |
| `structure fill [block]` | Fill up to 4,096 cells with cubes of the held or named block, such as `altaria:stone` |
| `structure fill air` | Clear the selected blocks |
| `structure export <name>` | Write a non-empty selection of up to 262,144 cells to `user://structures/<name>.json` |

Both corners are inclusive. A fill replaces construction pieces inside the
area. If a piece crosses the outline, enlarge the selection to include it.

Export version 1 uses `format: "altaria.structure"` and a `size` array in
`[x, y, z]` order. Each entry in `blocks` has:

- `block`, the namespaced registry key.
- `position`, an `[x, y, z]` offset from the minimum corner. Construction
  pieces keep their fractional block offsets and are included whole when
  their bounds intersect the selection.
- `kind`, either `voxel` or `construction`.
- `shape`, `shape_code` and `rotation` (quarter turns). For authored shapes,
  `authored_shape`, `authored_shape_id` and `authored_rotation` preserve the input to the connection rules.
  `shape_code` stores the resolved geometry, including ramp variants.
- Optional `paint` data and `overlay` or `decoration` keys. An empty key
  clears that layer. An absent key uses the block default.

Empty cells have no entries. Exporting the same name replaces its JSON file.
