# Altaria

C++ voxel world prototype for **GDstudio / Godot 4.7**, using GDExtension and
**FastNoise2**, six voxel LODs through a ~6 km fogged horizon, five configurable
biomes, independent overlays and persistent per-block HSV paint. Cartoon silhouette
contours, a low-resolution SubViewport with nearest upscaling, subtle extra
distance pixelation and colored biome sky/fog shape the world view.
Dense biome-tinted grass blocks use the grass-top texture directly and project a
side transition overlay onto connected lower blocks; stone and sand use their
dedicated 16×16 block textures. Construction supports eight block forms and
fractional stacking, including cubes directly on slabs. Voxel trees use Jolt
physics; the procedural sky and gameplay temperatures follow the day cycle and
biomes. Runtime UI defaults to English; Portuguese is available in the menu.

World generation uses ordinary full blocks and slab caps.
3D grass tufts and bushes remain disabled. Silhouette contours and biome lighting
continue to use the existing voxel world renderer.

## Play

Dependencies and the Linux x86_64 debug library are already installed locally.
Open `project.godot` in GDstudio and press **F5**. The console opens with the
command field focused. Enter `world new` and wait for initial chunk loading.
Use WASD and the mouse to explore. F toggles flight/walking; Space supports double
jump on foot. Left/right click remove/place blocks. **Esc** and **F1** toggle the
console. Press **Tab** to complete commands and **Enter** to run them. Use
`fullbright` to toggle full lighting. Look at a block and run
`paint #cc33ff` to tint it, or `overlay pebbles2` to add a decoration.
`biome enchanted` teleports to the purple-grass biome. Run `world new` for the
default terrain with a random seed, `world new plain 12345` for a flat world with
a chosen seed, or `world new structure` for a building space with a stone
anchor. In Structure, right-click in empty space to place additional blocks.
A new world replaces the current session. The HUD and console show first-tile
and total generation times.

Run `cheat inventory on`, close the console and press **T** to open **Cheat Inventory**.
Search by block name, registry key or numeric ID. Click a block to add 64 to the player inventory.

In Structure, aim and press **V** and **B** to mark the two corners of an area.
Use `structure fill [block|air]` to fill or clear it and
`structure export <name>` to save a shareable JSON file. The console prints
the exported file's path. See the [structure command and JSON reference](game/debug/README.md#structure).

Hold **R + wheel** to choose a block shape. Placement follows the camera's
direction, and **Q** adds a quarter-turn. Pillars align with the clicked face
and center on their support. Left-click wood
to cut a tree; click fallen parts to detach them. **G** releases any solid block
into physics. Try `time 19`, `weather storm` or `temperature`. Use `light` or
`light blue` to place a stationary light at your viewpoint. Use `light follow`
to toggle a light that moves with you. Run `log export 30s` to save recent game
messages, errors and frame timings for troubleshooting.
`outline off` and `pixelation off` toggle the visual effects. `fog #b08adc 80 900`
overrides fog color/range; `fog biome` returns to automatic biome/day-night fog.
Try `shape ramp` for manual construction and `godrays on|off`.

## Edit game data

- **Terrain, LOD and vegetation density / render radius:** [`generation.json`](game/world_generation/data/generation.json)
- **Contours, pixelation, sky, lighting, wind and physics budgets:** [`environment.json`](game/world_generation/data/environment.json)
- **Biome distribution / colors / fog / day-night temperatures:** [`biomes.json`](game/world_generation/data/biomes.json)
- **Surface / decoration overlays:** [`overlays.json`](game/world_generation/data/overlays.json)
- **Blocks / colors:** [`blocks.json`](game/world_generation/data/blocks.json)
- **Player and entities:** [`entities.json`](game/player/data/entities.json)

See the [generation guide](game/world_generation/README.md),
[player controls](game/player/README.md) and [console commands](game/debug/README.md).
Each run starts a fresh world. Edits and construction exist only in memory;
world saving and loading are disabled during development.

## Build

Stop a running game before rebuilding C++:

```bash
./build.sh
# Build an optimized Linux export library when needed:
./build.sh target=template_release
# Optionally cap compiler parallelism (default 4):
JOBS=2 ./build.sh
```

`native/src/` owns the application/menu lifecycle. Game systems live in the
user-created `game/world_generation/`, `game/debug/` and `game/player/` folders.
The build includes C++ files in these folders recursively, and generates
`compile_commands.json` for C++ editors.

On another Ubuntu/Pop!_OS machine, install GCC/G++, Git and `python3-venv`, then run:

```bash
./tools/setup_cpp.sh
```

This fetches pinned dependencies, installs the local Python build tools, compiles
FastNoise2/FastSIMD, then builds the extension. Network access is needed for setup.
Dependency revisions and licenses are recorded in `native/dependencies.json`.
For exports, include `game/**/*.json` and `licenses/*.txt` in the export include filter.
The Linux extension uses the system `libstdc++` runtime. Other platforms require
corresponding toolchains, libraries and `.gdextension` entries.

## Tests

```bash
./tests/run_core_tests.sh
GODOT="$HOME/.local/bin/gdstudio" ./tests/run_runtime_tests.sh
./tests/run_environment_tests.sh
./tests/run_cartoon_tests.sh
./tests/run_subviewport_tests.sh
# Real Vulkan visual checks in a graphical session:
./tests/run_cartoon_visual.sh
./tests/run_ramp_tests.sh
./tests/run_ramp_visual.sh
./tests/run_silhouette_visual.sh
```

Core tests cover determinism, greedy meshing, chunk borders, LOD coverage, raycasts,
authoritative edit validation and persistence. The Godot smoke test checks the
integrated native game in isolated temporary user data.

The architecture separates authoritative voxel state from rendering and player
input for future LAN/online multiplayer. **Network play is not implemented yet.**

Third-party sources: [FastNoise2](https://github.com/Auburn/FastNoise2),
[FastSIMD](https://github.com/Auburn/FastSIMD),
[godot-cpp](https://github.com/godotengine/godot-cpp),
[EnTT](https://github.com/skypjack/entt) (entity-component-system for creatures).

Current development check: `./tests/run_ephemeral_tests.sh`. Older persistence
fixtures are obsolete while world saving is disabled.
