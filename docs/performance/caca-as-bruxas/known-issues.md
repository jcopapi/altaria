# Histórico de problemas conhecidos

| ID | Estado | Evidência | Próxima ação |
|---|---|---|---|
| CAB-001 | Aberto; limitação estática confirmada | tests/render_benchmark.gd:5–12,53–54 e tests/stream_benchmark.gd:32,62–63: timeout não impede PASS | Reproduzir timeout isolado; exigir falha e descartar amostras antes de corrigir |
| CAB-002 | Hipótese de viés | tests/stream_benchmark.gd:36–40 move por frame | Comparar percurso por tempo; não alegar gargalo real |

Para cada atualização registre operação, revisão/hash, reprodução, responsável, decisão e condição para reabrir. Não reabrir investigação resolvida sem nova evidência.

CAV-001 — 2026-11-10-cave-generation: prototype region-key collisions, NaN footprint
probe and INT_MAX neighbor overflow independently identified by JUIZ/CAÇADOR.
Coordinator corrected ordered hashing/probe validation/coordinate bounds; targeted
regressions pass. Reopen if distinct supported region tuples yield equal IDs or
non-finite/out-of-range input reaches planning. Remaining topology constraints
are phase-2 work listed in docs/caves/PLAN.md, not a claim of completed caves.

CAV-002 — 2026-11-10-cave-generation: repeated disposable worlds exposed orphaned
RayCast3D/Debugray/Soundplayer nodes from the newly added spatial_audio_3d addon.
Verbose Godot shutdown reported 383 objects and 54 mesh/instance RIDs. Two addon
exit handlers removed children without freeing them. Coordinator added queue_free
after both removals; independent CAÇADOR reviewed ownership. Native cave session
runtime and ordinary ephemeral runtime then passed without shutdown leaks. Reopen
on leaked instances after repeated world generation; do not suppress the logs.

CAV-003 — 2026-11-10-cave-generation: mixed archetypes on smooth hills could omit a
shallow hub while retaining an orphan side room. First-room-based pruning then
deleted deeper canonical hubs still referenced by neighbors: 63 missing portals
over three roof fixtures and a physically isolated, unflagged 13,601-voxel room.
Coordinator now admits side rooms only with a valid hub and seeds pruning from
every canonical local hub. Exact smooth-hills regression passes. Independent
post-fix six-neighbor sweep: 576 connected passages, 258 matching portals,
18 complete connected chamber cases, 13,040,367 voxel visits, zero missing portals.
Reopen on a cross-region endpoint absent from its owner or an unintended orphan
room. This finite fixture sweep does not prove every possible terrain profile.

CAV-004 — 2026-11-10-cave-generation: interpolating cached endpoint climate winners
erased midpoint niches and made the third-choice boundary jump 40°C. Extremely
large finite depth values also produced invalid midpoint arithmetic. Exact
point scoring, all-weight environment aggregation and depth bounds passed
native and independent UBSan fixtures.

CAV-005 — 2026-11-10-cave-generation: loaded regions could have carved meshes with
no debug plan after LOD2 pressure, and a 1 MiB cache could make 64 copies of one
live plan. Loaded volumes pin their local plans and a weak live index reuses
unadmitted plans. Independent fixture now shows one live object for 64 requests;
simultaneous cold construction can still allocate transient duplicates.

CAV-006 — 2026-11-10-cave-generation: an entrance at seed1337 region (1,-2) ended
under a cap of solid blocks. Explicit entrance paths now use an upper mouth
bound instead of the lower planning atlas and can pierce the roof throughout
the planned passage. Actual terrain entrance test passes in 25 regions.

CAV-007 — 2026-11-10-cave-generation: sea-level freshwater touched a carved
entrance voxel without joining its basin. Independent sweep reproduced
(-1357,42,1892) water adjacent to (-1358,42,1892) air, seed1337 basin(-2,2).
Probe now tests a shared voxel face against exact entrance detail and base
rock. Both cells are water at the same level in the versioned regression.
Reopen on a disagreement between actual opening contact and basin source.

CAV-008 — 2026-11-10-cave-generation: raw crystal/fossil/root geometry could leave
small disconnected voxel islands. A worker-only support pass now removes pieces
without a cardinal path to actual host rock after cave/corridor clipping.
Coordinator: 600 placements, 137,624 retained / 1,423 pruned. Independent judge:
264 placements, 369 components, 69,373 retained / 326 pruned, including host,
corridor and water clipping; concurrent queries publish one shared mask.
Tall pillars may use the supported vertical world span, rather than the 256-cell
horizontal mask limit. Independent 304.558 m pillar retained 5,038 voxels.

CAV-009 — 2026-11-10-cave-generation: pre-existing wedge face triangle 7 chose
front-face winding from a degenerate first triangle. The shared quad emitter now
falls back to the second triangle. Independent 88 partial variants / 952
non-degenerate triangles and 54 packed CollisionRect cases pass.

CAV-010 — 2026-11-10-cave-generation: isolated rooms could touch expanded local
passages or normal rooms of another region. Isolation now runs after entrances,
reserves 3 m for detail plus voxel contact, checks normal geometry in all eight
neighbors and arbitrates adjacent isolated candidates deterministically.
Independent sweep: 120 seeds, four profiles, 2,275 isolated rooms and 193,062
voxel positions, no residual contact. Exact seed95 and seed5 cases are versioned.

CAV-011 — 2026-11-10-cave-generation: skirts extended surface material to the world
floor and sealed underground air/water intervals; rendered captures showed a
green wall through an infernal chamber. Skirts now preserve solid intervals,
separate deep rock from the surface band and use a shared far closure in full
and trimmed volumes. Exact 22-attribute far equivalence and synthetic water/air
opening checks pass. Classifying trimmed row0 and a double-precision threshold
subtraction preserve material and nearly aligned cutoff equality.

CAV-012 — 2026-11-10-cave-generation: rendered Vulkan test exposed the lens addon's
copied private SceneData layout (shader5792 vs engine5696 bytes), flooding uniform
errors. Lens now binds its own 16-byte viewport/time record and checks dispatch/
depth-sample bounds. Subsequent RTX4050 rendered runs passed with no shader/RID
errors. Early failed driver/UBO runs remain rejected, not benchmark evidence.

CAV-013 — 2026-11-11-world-generation-reform: opening a single river or ocean
contact flooded eight sampled landmark halls in five catchments. The live
planner now prefers dry entrances in most catchments and puts inland lakes in
the deepest layer. The final native sweep found 85 dry landmarks among 94 and
the rendered seed 1337 check confirmed a dry landmark and a cave at Y=-679.
Reopen if dry catchments accept an entrance with actual face contact to surface
water or if the dry landmark distribution collapses.

CAV-014 — 2026-11-11-world-generation-reform: compact cave chunks reduced
resident voxel buffers from 78.3 MB to 36.9 MB in the matched test. Streaming
P99 still exceeded the previous profile (22.47 ms vs 14.23–15.40 ms in the
last candidate and two baseline runs). A prior candidate had a 253.62 ms
outlier. Open for frame-level attribution of CPU, GPU, worker queues and
main-thread uploads on the same time-based flight path.

CAV-014 update — 2026-11-11-cave-exploration-performance: matched Vulkan
flight showed P99 9.31–9.38 ms with no eager Jolt collider work, against
16.97–38.12 ms in the baseline repetitions. The final queue still had
19–24 jobs and startup took 5.20–5.83 s versus 3.86–4.77 s. Keep open for
streaming throughput and startup upload peaks.

CAV-014 update — 2026-11-11-dynamic-streaming: seed 1337 at 1280×720
uncapped Vulkan flight improved median P99 from 5.61 ms to 3.57 ms.
The worst frame rose from 9.47 ms to 19.99 ms, with repeatable outliers
near 2.3 seconds of the test flight. Investigate individual mesh uploads and
rendering-server stalls at that position. Adaptive streaming remains enabled
for the verified reductions in resident geometry, voxel buffers and startup.

CAB-003 — 2026-11-12-terrain-fps: rounded Terrain emitted subdivisions on flat
bands and degenerate triangles. The compact mesh reduced the fixed hillside
scene from 5.44 M to 3.21 M reported triangles and median frame time from about
9.03 to 4.70 ms at the same view distance. The separate cloud-height camera
still had a 12.38–13.24 ms final P99. See the operation report for all runs.

CAB-004 — 2026-11-12-terrain-fps: the midpoint test for overlapping branch
prisms discarded exposed trunk and arm faces. Fork rendering now retains those
faces. Both orientations pass a rendered-surface regression. Reopen on a
visible opening between a trunk and its branches.
