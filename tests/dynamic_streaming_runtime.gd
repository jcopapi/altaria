extends "res://tests/cartoon_runtime.gd"

var output: String
var csv: FileAccess

func pause_until_ready() -> void:
    await create_timer(.7).timeout
    await settle()

func sample(label: String, origin: Vector3, velocity: Vector3, duration: float) -> void:
    var begin := Time.get_ticks_usec()
    var previous := begin
    var elapsed := 0.0
    while elapsed < duration:
        actor.position = origin + velocity * elapsed
        await process_frame
        var now := Time.get_ticks_usec()
        elapsed = (now - begin) * .000001
        csv.store_csv_line(PackedStringArray([label, str(elapsed), str((now - previous) * .001)]))
        previous = now

func snapshot(label: String) -> void:
    var s: Dictionary = game.get_debug_state()
    var out := {"case":label, "ready_ms":s.world_ready_ms, "fog_end":s.fog_end,
        "tiles":s.tiles, "wanted":s.wanted, "lod0":s.lod0, "lod1":s.lod1,
        "lod2":s.lod2, "lod5":s.lod5, "voxel_bytes":s.voxel_bytes, "triangles":s.triangles,
        "pending":s.pending, "collision":s.player_collision_ready, "upload_max":s.max_mesh_upload_ms,
        "drawn_primitives":Performance.get_monitor(Performance.RENDER_TOTAL_PRIMITIVES_IN_FRAME),
        "stream_radius":s.get("stream_radius",s.view_distance),
        "stream_underground":s.get("stream_underground",false),
        "cancelled_jobs":s.get("stream_cancelled_jobs",0)}
    print("STREAM ", JSON.stringify(out))

func capture(label: String) -> void:
    if OS.get_environment("ALTARIA_STREAM_CAPTURE") != "1":
        return
    await RenderingServer.frame_post_draw
    check(root.get_texture().get_image().save_png(output.path_join(label + ".png"))==OK,
        "Capture " + label)

func run() -> void:
    output = OS.get_environment("ALTARIA_STREAM_OUTPUT")
    var cap := OS.get_environment("ALTARIA_STREAM_CAP")
    Engine.max_fps = int(cap) if not cap.is_empty() else 120
    DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
    csv = FileAccess.open(output.path_join("frames.csv"), FileAccess.WRITE)
    csv.store_csv_line(PackedStringArray(["scenario","elapsed_seconds","frame_ms"]))
    game = load("res://scenes/main.tscn").instantiate()
    root.add_child(game)
    await process_frame
    check(game.start_world("Streaming benchmark",1337),"Start seed 1337")
    find_player()
    Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
    game.execute_command("time 10")
    game.execute_command("time pause")
    game.execute_command("biome normal")
    var origin := Vector3(100,200,100)
    aim(origin+Vector3.UP*1.65,origin+Vector3(70,-25,18))
    await pause_until_ready()
    snapshot("normal_start")
    await capture("surface")
    check(game.set_voxel(100,200,103,26),"Place marker on surface pass")
    await sample("slow",origin,Vector3(5,0,1),3.0)
    snapshot("slow_end")
    var flight_start := actor.position
    await sample("flight",flight_start,Vector3(85,0,20),5.0)
    snapshot("flight_end")
    await pause_until_ready()
    game.execute_command("biome desert")
    await create_timer(1.2).timeout
    await pause_until_ready()
    snapshot("desert")
    await sample("desert",actor.position,Vector3.ZERO,2.0)
    game.execute_command("biome normal")
    var below := Vector3(-214.5,-678.5,-92.5)
    if OS.get_environment("ALTARIA_STREAM_REAL_CAVE") == "1":
        actor.position = origin
        await pause_until_ready()
        var chambers: Array = game.get_debug_state().cave_chambers
        check(not chambers.is_empty(),"Seed has a planned cave in the starting region")
        for chamber in chambers:
            var center: Vector3 = chamber.center
            if center.y < -170:
                below = center + Vector3.ONE * .5
                break
        check(below != Vector3(-214.5,-678.5,-92.5),"Pick an actual deep chamber")
        var lamp := OmniLight3D.new()
        lamp.omni_range = 70
        lamp.light_energy = 3
        camera.add_child(lamp)
    aim(below+Vector3.UP*1.65,below+Vector3(40,2,2))
    var depth_start := Time.get_ticks_usec()
    await pause_until_ready()
    print("STREAM_DEPTH_READY_MS ",(Time.get_ticks_usec()-depth_start)*.001)
    snapshot("deep")
    await capture("cave")
    var p := Vector3i(below.floor())
    var id: int = game.get_voxel(p.x,p.y,p.z)
    print("STREAM_DEPTH_ENV ",game.get_cave_environment(below)," eye=",below," voxel=",id)
    if OS.get_environment("ALTARIA_STREAM_REAL_CAVE") == "1":
        check(id == 0 or id == 5 or id == 22,"Deep chamber center is open or flooded")
        check(game.get_debug_state().player_collision_ready,"Deep band supplies local collision")
        check(game.set_voxel(p.x,p.y,p.z,26),"Place deep session edit")
        await pause_until_ready()
        check(game.get_voxel(p.x,p.y,p.z)==26,"Deep edit appears in streamed volume")
        if OS.get_environment("ALTARIA_STREAM_VALIDATE_BANDS") == "1":
            var lower := below - Vector3(0,120,0)
            check(not game.get_cave_environment(lower).loaded,"Far lower band is unavailable")
            actor.position = lower
            await pause_until_ready()
            check(game.get_cave_environment(lower).loaded,"Vertical-only teleport loads the lower band")
            check(game.get_debug_state().player_collision_ready,"Lower band enables player collision")
            check(not game.get_cave_environment(below).loaded,"Old band is released")
            actor.position = below
            await pause_until_ready()
            check(game.get_voxel(p.x,p.y,p.z)==26,"Deep edit survives vertical band replacement")
        check(game.set_voxel(p.x,p.y,p.z,id),"Remove deep session marker")
        await pause_until_ready()
    await sample("deep",below,Vector3.ZERO,2.0)
    aim(origin+Vector3.UP*1.65,origin+Vector3(70,-25,18))
    await pause_until_ready()
    snapshot("returned")
    check(game.get_voxel(100,200,103)==26,"Session edit survives unloading")
    check(game.get_debug_state().player_collision_ready,"Returning player has nearby voxel collision")
    if OS.get_environment("ALTARIA_STREAM_REAL_CAVE") == "1":
        aim(Vector3(112,201.65,112),Vector3(112,200,200))
        await pause_until_ready()
        for x in range(183,188):
            for z in range(110,115):
                check(game.set_voxel(x,200,z,1),"Place support beyond the idle detail radius")
        check(game.set_voxel(185,206,112,1),"Place falling voxel")
        check(game.release_voxel(185,206,112,false),"Release a voxel outside idle LOD0")
        await create_timer(1.5).timeout
        var physical: Dictionary=game.get_debug_state()
        check(physical.physics_frozen==0 and physical.physics_positions.size()==1,
            "An active body requests its own collider outside the idle LOD0 radius")
    csv.close()
    game.queue_free()
    for i in range(4): await process_frame
    print("DYNAMIC_STREAMING_PASS" if failures.is_empty() else "DYNAMIC_STREAMING_FAIL")
    quit(0 if failures.is_empty() else 1)
