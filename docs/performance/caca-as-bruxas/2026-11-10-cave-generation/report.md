# Cave generation operation

## Identification / recognition
2026-11-10, America/Sao_Paulo. Coordinator: root Codex native. Baseline eff109c,
clean tracked tree at start. Engine command returned
4.7.1.stable.mono.gdstudio.a450b93bf. Project declares Forward+, Jolt and 1280x720;
current world normally uses a lower-resolution presentation viewport. FastNoise2
and godot-cpp installed; Voxel Tools and Terrain removed in the baseline. RAM
observation: 15,690 MiB total, 6,911 MiB available, swap already used. Do not run
concurrent GPU benchmarks/compiles. GPU/driver not yet revalidated for this operation.

## Baseline
`tests/run_ephemeral_tests.sh` launched with isolated XDG directories, seed 1337
for the second world, headless; raw log will be retained. Functional baseline only,
not GPU/frame-time evidence. Full timed-path manifest and three cold/warm repeated
runs remain required before comparing optimized integrated cave generation.

## Actual delegation
- /root/cave_architecture_audit: CARTOGRAFO, read-only; native spawn confirmed.
- /root/cave_runtime_audit: MAQUINISTA/VIGIA, read-only; native spawn confirmed.
Model override: none; inherited identity not confirmed by tool metadata. No external
provider ran. Two workers respect local max_workers=2 / max_total_agents=3.

## Plan / ownership
See docs/caves/PLAN.md. Coordinator is sole writer. No structural integration has
been performed before the audit gate. Cave planner prototypes and tests follow
the recorded plan. All requested phases remain in scope.

## Validation / review / final benchmark
Pending integrated implementation. Hunter and judge not assigned yet; no final
approval or speedup claimed. Latest user instruction keeps worlds disposable and
requires edit continuity within the session, not old-world save compatibility.

## Known issues / remaining phases
CAB-001: old benchmark timeout can still print PASS; do not rely on it.
CAB-002: old flight path moves per frame; unsuitable for a controlled comparison.
All cave feature/visual/hydrology/frame-time completion gates remain open.
Portolan's exact cave project could not yet be identified from the supplied name;
no algorithm, source code or performance claim is attributed to a guessed project.

## Audit gate and initial planner result
The two independent audits completed; details and all ten requested audit areas
are recorded in docs/caves/AUDIT.md. Both highlighted duplicate scalar/bulk
sampling, underground grass misclassification and sea-level flooding hazards.
The runtime audit additionally identified full-height geometry/collider cost,
count-only result queues and synchronous generator queries.

User clarification: no disk persistence, save menus, restarts/migrations. Session
edits must survive unloading/regeneration. Any earlier investigator suggestion
of disk-format work is explicitly rejected for this objective.

Coordinator implemented only a pure standalone regional planning prototype in
caves/cave_plan.* after the plan. Tests pass for order independence, seed/dimension
salts, four labelled levels, host-roof/floor bounds on a flat fixture, hub graph
reachability and identical curved cross-region portals (including negatives).
No cave carving, registry/hydrology or gameplay integration is claimed yet.
Independent JUIZ/CAÇADOR followups were dispatched to the same two native agents,
which did not implement this prototype. Their review is pending. No final gate or
performance gain has been approved. The measured functional baseline passed;
planning geometry checks do not establish tunnel traversability in terrain.

## Independent prototype review result
Both reviewers found region-key collisions from commutative XOR. JUIZ additionally
reproduced an ignored NaN corner probe and INT_MAX neighbor overflow. Coordinator
fixed these with ordered hash folding, validation of every roof probe and supported
world-coordinate bounds. Added both collision examples, NaN and extreme input to
regressions; native tests pass after fixes. Review has not approved complete phase 2:
terrain-constrained curves, thin-rock omission, entrance/isolated-room policy,
vertical shaft geometry and bounded cache/index remain open. The prototype is
still deliberately disconnected from runtime generation.

Production build exposed Godot's `-fno-exceptions` constraint. Prototype validation
now returns explicit invalid plans rather than throwing; its test runner also
uses `-fno-exceptions` to enforce the actual build contract. Native tests pass.

## Expanded planner and initial chunk integration
The coordinator added configured archetypes, thin-host omission, sampled roof-
constrained capsule paths, aligned vertical shafts, entrances, optional infernal
rooms and explicit isolated pockets. A 32m spatial index and count/estimated-byte
bounded LRU cache keep immutable plans alive for queries after eviction. The byte
estimate covers owned storage; it is not process RSS and excludes retained queries.

Runtime roof sampling batches FastNoise terrain columns into immutable 8m atlases,
bounded by region count. Cave scalar and bulk volume evaluation now share the same
field and plan query; only LOD0/1 is currently integrated. The feature remains
disabled in its JSON until later phase gates. No old world readers/migrations or
disk persistence were added. Edits remain authoritative RAM snapshots applied last.

Integration fixture passed scalar/bulk equality, a complete shared halo, dry voids,
underground rock instead of grass, and destruction/construction preserved after
disposing and regenerating chunk volumes. A fresh model has zero edits. This
directly tests the user's session-only meaning of persistence.

An exploratory constant-roof CPU microbenchmark and real-noise CPU tile test were
run. They are not frame/GPU benchmarks or an accepted game speedup. AABB rejection
reduced candidate distance evaluations without changing the fixture's carved count;
controlled repeat runs and integrated frame measurements are still required.

Independent reviewers were briefly unavailable due to provider capacity. Both
were retriggered for the current integration when available; no approval is assumed.
One has since confirmed old collision/NaN/overflow regressions pass UBSan and is
reviewing the new adapter. Their pending findings remain acceptance gates.

Concurrent unrelated workspace edits added a liquid system, block assets and a
console adapter. These are preserved and are not cave-task deliverables. Full
build initially failed on ambiguous String concatenation in the new console; the
coordinator made only two explicit String newline conversions to unblock build.

## Current review and runtime evidence
JUIZ reran planner/field/integration with UBSan and -fno-exceptions: PASS. Its
additional LOD1 fixture compared 470,456 subterranean scalar/bulk samples, including
halos and negative coordinates; 4,956 were air near chambers. It found no remaining
reproducible blocker in this foundation, while explicitly leaving full topology,
LOD transitions and the later feature phases open. CAÇADOR reran field/integration
after the roof-budget, extreme-coordinate and entrance-shell fixes: PASS.

The first integration exposed two real main-thread/streaming hazards: treating an
unloaded chunk as known air without a player gate, and paint validation invoking
cold Generator samples through WorldModel. The coordinator added explicit unknown-
chunk collision/ray gates and passes authoritative loaded/edited paint support.
Both reviewers verified the corrected paths. Scalar/bulk entrance exposure now
agrees using explicit carved-above classification; the native integration test
compares the entire mouth chunk's shallow shell. Extreme coordinates are rejected
before arithmetic in both terrain_sample and sample (including vegetation=true).

A disposable headless Godot fixture now enables caves at -384..320, visits a room,
places/paints a block, travels to (-5000,180,-5000), returns, removes it, repeats the
trip, and starts a new world. All assertions pass. Invalid roof grid, depth array,
boolean and fractional integer JSON fields are also rejected. No world directory
is created. The fixture excludes unrelated console/grass autoloads; the ordinary
ephemeral runner separately retains them and passed too. Neither proves GPU/visual
quality or stable frame-time performance. Ordinary project still emits six invalid
SimpleGrass shader UID fallback warnings, preserved in the raw log.

Strict runtime runners initially rejected real shutdown leaks even after assertions
printed PASS. Verbose output identified spatial_audio_3d exit handlers detaching
children without freeing them (CAV-002). Two queue_free calls corrected ownership;
independent review found no double-free concern. Both runtime runners subsequently
exited cleanly with no leak/error logs. The cave runner now rejects leak warnings
as well as ERROR/FAIL and timeouts. No failed run is counted as accepted evidence.

Broad native core suite: PASS, 234,925 checks (including the concurrently added
liquid suite). Production native extension builds. The cave goal remains active:
this is a staged integration and session-persistence gate, not a final delivery.

The expanded Godot fixture also passed real native-player landing on an interior
floor at (154,-328,172), a Jolt block resting near y=-327.02 with no pending work,
and fractional slab/cube construction inside the cave. Player physics is enabled
specifically for the landing check. A first Jolt assertion failed because the test
had moved the viewer hundreds of meters vertically away, intentionally freezing
distant debris; keeping the viewer near the tested floor corrected that fixture
error. The final full runner passed and exited cleanly. This is one fixed-seed
collision fixture; it does not establish all caves' physical connectivity or GPU
visual behavior.

## Mixed-profile connectivity correction
An expanded independent voxel flood-fill found CAV-003: if a shallow tall hub did
not fit its roof, an unrelated surviving side room could become the pruning root.
The planner removed legitimate deeper hubs that neighbors still referenced, and
left an unintended isolated 13,601-voxel side chamber. The initial normal-profile
graph tests did not cover this combination. Pre-fix sweep found 63 missing portals
across smooth hills, mountain valleys and steep smooth roofs.

The coordinator now admits side rooms only at levels with a valid hub, and seeds
pruning from every canonical local hub. Added the exact mixed-profile hills case
to the versioned planner suite. Independent post-fix sweep: 576 connected passages,
258 canonical portals, 21 entrances, 75 shafts and 18 fully connected chamber cases;
13,040,367 voxel visits, zero missing/mismatched portals. Passages were flood-filled
with zero detail and uniform maximum shrink (+0.75), which bounds the runtime's
smaller detail. The test fails explicitly on missing portals. Its source is now
versioned as tests/cave_connectivity_tests.cpp for repeatability. It is a finite
analytic-roof sweep, not proof for every world seed or terrain shape.

Independent real Generator followup also passed: seven local/cross-region/shaft/
entrance connectors and two full giant/normal chambers, 410,565 voxel visits,
39 actual LOD0 volumes, zero field candidates remaining solid and zero water.
Fixed seed1337, regions (0,0) and (-1,-1), supported bounds -448..320 (span768).
The archived fixture includes terrain_mesh.cpp directly and a local flood-fill
helper; do not compile terrain_mesh.cpp twice. Source and logs are in raw/.

Final constant-roof CPU microbenchmark (g++17 -O2 -fno-exceptions, i7-13620H):
three independent cache instances, seed1337, default archetypes. Planner0.082–0.103ms,
cold32³query2.07–2.53ms, warm query0.030–0.033ms, 32,768 field samples4.17–4.97ms;
12,624 carved samples in every run, 67 query primitives, nine resident plans,
1,487,886 estimated plan-cache bytes. It has a constant roof and no Runtime atlas,
meshing, Godot, collision upload, rendering or frame tracing. No before/after game
speedup or stable-FPS claim follows. Full integrated metrics remain a later gate.

## Depth, hydrology and local worker optimization

Live settings now generate four depths in -448..320 at LOD0–2, with nine
declarative underground biomes and surface climate influence. The far render-only
window passes exact geometry/material equivalence for eight fixture tiles;
voxel allocation falls from 3,588,224 to 737,528 bytes in those tiles, excluding
columns, other worker scratch and total RSS. Loaded local plans are pinned and
uncached live requests deduplicate. Actual surface openings pass the 25-region
entrance fixture. CAV-004 through CAV-006 summarize the corrected failures.

Catchments reuse the native workers and the existing water/lava blocks. A static
plane, not a dynamic per-voxel simulation, fills planned cave voids. Shared
levels are clipped correctly at all three cave LODs. The native fixture includes
a water level at -166, a lava level at -325, a graded gallery, 8 entrances with
4 ocean contacts, four halos, negative regions and edit-last provenance. The
independent reviewer reproduced CAV-007 with actual exterior water adjacency;
the exact opening-SDF probe now passes 75 catchments (67 wet, 0 mismatches).
Gdstudio passes the submerged air/pressure fixture, real Jolt fall through the
lake surface to y=-175.02 above rock at -176, and paint/removal after eviction.
Ordinary ephemeral startup also passed, with the pre-existing addon UID warnings.

The worker now reuses each vertical candidate bin and skips empty bins. Five
sequential runs before and after on the same integration tile and host measured
median cave field 59.5168 → 53.2838 ms, total tile 118.02 → 110.835 ms. It is a
single native workload, not a rendered frame-time claim. The independent
arithmetic sweep covered 547,213 rows in 1,731 min-Y/step combinations, plus
negative X/Z cells. Actual LOD fixture still matches 33,827 scalar samples and
all four halos, and the exact far geometry suite passes.

At that checkpoint the ordinary catalogue had no resource rules or placed ores;
formation geometry and final rendered/profile gates were still open. The sandbox
could not access GPU device nodes. The later approved rendered path below
resolved that environment limitation.

## Final integrated formations, isolation and render correctness

The catalogue now has eleven geometry families and data rules for drips, pillars,
collapse rubble, crystal clusters/geodes, fossils, roots/fungi, thermal vents,
volcanic spires, ice sheets and surface-conditioned sediment/forest roots.
Ordinary block IDs represent these formations. Actual Generator scalar/LOD0–2
and all-halo checks pass, including edit-last removal. Cold support traversals
run on workers and retain only components connected to real host rock after
terrain/corridor clipping. Independent support/concurrent-publication, tall-pillar
and packed-collider proofs passed; see CAV-008/009.

The final isolation audit found and fixed detail-expanded local contacts and a
neighbouring giant side room that was absent from the local graph. The independent
120-seed/four-profile sweep accepted 2,275 isolated rooms and 193,062 voxel
positions. A separate real Generator coastal probe with entries disabled checked
91 eligible tiles across three seeds: 763,209 newly carved cells and 70,242
base-rock/water faces, with no unplanned lateral contact. These are finite tests,
not a proof of every seed.

Vulkan captures exposed skirts carrying grass down through underground voids.
Skirts now preserve solid runs/air/liquid gaps and use rock below the surface band.
Trimmed row0 material and double-precision cutoff arithmetic preserve exact full/
trim geometry and all 22 vertex attributes. The before/after vent captures in
images/ show the artificial wall removed. Inspection images use a temporary lamp
only in the test, not a product light or a timed benchmark setting.

Concurrent player work replaced the custom body with a CharacterBody capsule.
The cave test now measures its actual lowest collision point, and the adapter
waits for current/predicted-position terrain colliders. The unloaded teleport,
real cave-floor landing, Jolt fall and exact procedural slab height pass. The
foreign controller was preserved; only the stream-ready hook is cave integration.

The first approved RTX4050 test exposed a separate pre-existing lens UBO mismatch.
Its shader now receives its own 16-byte viewport/time record, without copying
Godot's private SceneData. The subsequent rendered tests passed without shader
or RID errors. Failed display/UBO/import attempts were rejected. The normal
project was also tested in Vulkan with all autoloads and real configuration:
fresh worlds, edits/build/paint/reset and no saved-world directory all passed.
Six existing SimpleGrass UID fallback warnings remain visible in that log.

## Accepted final GPU measurements

GDstudio/Godot 4.7.1, Vulkan Forward+, NVIDIA RTX4050 Laptop GPU. Three sequential
trials; fixed cave fixture seed1337, 120 FPS cap, project window configured
1280×720 (actual desktop client/captures1280×684), with the inherited pixel-size2
SubViewport at640×342. Time-based 100 m/s underground stress path. No native builds or
other test jobs ran concurrently. The fixture disables unrelated grass/console
autoloads and vegetation; the ordinary Vulkan smoke test above retains them.
Source-library/config SHA256 files accompany each raw trace. All three used the
same library hash, verified against the final compiled extension.

| Scenario | Median p50 | Median p95 | Median p99 | Highest frame over all trials |
| --- | ---: | ---: | ---: | ---: |
| Stationary lake | 8.161 ms | 9.281 ms | 9.407 ms | 9.668 ms |
| Underground streaming stress | 8.150 ms | 11.896 ms | 17.147 ms | 27.599 ms |
| Lake edit/remesh | 8.118 ms | 9.312 ms | 9.492 ms | 17.401 ms |

Startup first tile: 510.5–548.5 ms; all fixture LODs ready: 6.241–6.332 s.
Peak process RSS: 1,571.5–1,582.0 MiB (median1,573.3 MiB), including Godot and
driver/process allocations, not GPU VRAM. Near voxel payload at the initial
settled point: 99,693,440 bytes; this excludes columns/meshes/caches. Largest
observed collider upload: 22.162 ms; total upload: 28.286 ms. Soft upload budgets
cannot preempt one Jolt shape build, so isolated long frames remain under stress.

Earlier controlled native optimizations (bin reuse and material-independent
colliders) have their own before/after traces. They are not an unmeasured GPU
speedup claim. Final Vulkan figures describe the current feature set; they do
not promise a constant FPS or characterize the full foliage workload.

## Final deliverable boundaries

REGISTRIES.md documents archetype/biome/formation/resource extension. Resource
rules remain empty in the ordinary catalogue and have no placement caller.
Session modifications survive chunk eviction; new tests start fresh. No Terrain,
old-world compatibility, ore generator, metallurgy, equipment or new fluid
simulation was added. The surface-generation/assets request remains deferred.
