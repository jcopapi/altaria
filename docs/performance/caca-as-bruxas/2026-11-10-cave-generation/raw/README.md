# Evidence notes

`*-before-failed*` files and `audio-shutdown-before-failed-verbose.log` are rejected
runs. A printed PASS in the old portal sweep or a runtime assertion summary does
not override missing portals, runtime errors or leaked instances.

Accepted staged results: planner.log, field.log, integration.log, connectivity.log,
cave-runtime.log, ephemeral-current.log, core.log and native-build.log. The ordinary
ephemeral run preserves six SimpleGrass shader UID fallback warnings; no errors or
leaks remain. The native cave fixture excludes unrelated grass/console autoloads.
Neither headless runner validates GPU visuals or frame-time performance.

`constant-roof-cpu.csv` is a three-run pure CPU microbenchmark, not an FPS comparison.
Its manifest records seed, flags, CPU and source hashes. It excludes atlas, actual
terrain noise, meshing, collisions and rendering. Runtime resident cache estimates
exclude worker scratch and Queries retaining evicted plans.

The independent real Generator fixture was archived with a relative helper include
and an explicit return in the renamed helper main. Run from the project root:

```bash
g++ -std=c++17 -fno-exceptions -O2 -pthread -DFASTNOISE_STATIC_LIB \
  -I. -Inative/build/fastnoise-install/include \
  docs/performance/caca-as-bruxas/2026-11-10-cave-generation/raw/cave-generator-connectivity-judge.cpp \
  game/world_generation/voxel_core.cpp game/world_generation/biome_stage.cpp \
  game/world_generation/surface_state.cpp game/world_generation/block_shapes.cpp \
  game/world_generation/ramp_shapes.cpp game/world_generation/vegetation.cpp \
  game/world_generation/caves/*.cpp native/build/fastnoise-install/lib/libFastNoise.a \
  -o /tmp/altaria-independent-generator-connectivity
/tmp/altaria-independent-generator-connectivity
```

It includes terrain_mesh.cpp to access its private volume builder; do not compile
that source again in this command. Its 39 volumes use supported -448..320 bounds.
This is a fixed-seed finite sample, not proof of every world seed or topology.
