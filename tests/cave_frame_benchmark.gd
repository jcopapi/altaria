extends "res://tests/cartoon_runtime.gd"

var csv: FileAccess

func wait_for_tiles() -> bool:
    var deadline:=Time.get_ticks_msec()+40000
    # Allow the native streaming tick to publish the new wanted set first.
    await create_timer(.2).timeout
    while Time.get_ticks_msec()<deadline:
        await create_timer(.025).timeout
        var state: Dictionary=game.get_debug_state()
        if state.wanted>0 and state.tiles==state.wanted and state.pending==0:return true
    return false

func measure(label: String, seconds: float, origin: Vector3, velocity: Vector3) -> void:
    var samples: Array[float]=[]
    var start:=Time.get_ticks_usec()
    var before:=start
    var elapsed:=0.0
    while elapsed<seconds:
        actor.position=origin+velocity*elapsed
        await process_frame
        var now:=Time.get_ticks_usec()
        elapsed=(now-start)*.000001
        var frame_ms: float=(now-before)*.001
        before=now
        samples.append(frame_ms)
        csv.store_csv_line(PackedStringArray([label,str(elapsed),str(frame_ms)]))
    samples.sort()
    var state: Dictionary=game.get_debug_state()
    print("CAVE_FRAME ",label," count=",samples.size(),
        " p50_ms=",samples[int((samples.size()-1)*.50)],
        " p95_ms=",samples[int((samples.size()-1)*.95)],
        " p99_ms=",samples[int((samples.size()-1)*.99)],
        " max_ms=",samples.back()," pending=",state.pending,
        " tiles=",state.tiles," voxel_bytes=",state.get("voxel_bytes",0),
        " collider_triangles=",state.get("collider_triangles",0),
        " collider_ms_max=",state.get("max_collider_ms",0),
        " upload_ms_max=",state.get("max_upload_ms",0))

func run() -> void:
    var report:=OS.get_environment("ALTARIA_CAVE_FRAMES")
    csv=FileAccess.open(report,FileAccess.WRITE)
    if csv==null:
        push_error("Set ALTARIA_CAVE_FRAMES to a writable output path")
        quit(1)
        return
    csv.store_csv_line(PackedStringArray(["scenario","elapsed_seconds","frame_ms"]))
    Engine.max_fps=120
    if DisplayServer.get_name()!="headless":
        DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
    game=load("res://scenes/main.tscn").instantiate()
    root.add_child(game)
    await process_frame
    if not game.start_world("Cave benchmark",1337):
        quit(1)
        return
    find_player()
    game.execute_command("time pause")
    var lake:=Vector3(-111.5,-164.5,83.5)
    actor.position=lake
    if not await wait_for_tiles():
        push_error("Cave benchmark startup timed out")
        quit(1)
        return
    var initial: Dictionary=game.get_debug_state()
    if game.get_voxel(-112,-166,83)!=5:
        push_error("Cave benchmark lake fixture is not available")
        quit(1)
        return
    print("CAVE_BENCHMARK mode=",DisplayServer.get_name()," seed=1337 first_tile_ms=",
        initial.get("first_tile_ms",0)," ready_ms=",initial.get("world_ready_ms",0))
    await measure("stationary_lake",2.0,lake,Vector3.ZERO)
    # A time-based underground streaming stress path; the camera can pass
    # through solid rock. Geometry and stream work are identical per run.
    await measure("streaming_stress",5.0,lake,Vector3(100,0,20))
    actor.position=lake
    if not await wait_for_tiles():
        push_error("Cave benchmark return timed out")
        quit(1)
        return
    if not game.set_voxel(-112,-165,83,1):
        push_error("Cave benchmark edit failed")
        quit(1)
        return
    await measure("lake_remesh",2.0,lake,Vector3.ZERO)
    if not await wait_for_tiles():
        push_error("Cave benchmark edit did not finish")
        quit(1)
        return
    csv.close()
    if DisplayServer.get_name()!="headless":
        # These fullbright debug captures happen after all timed samples.
        game.execute_command("fullbright on")
        aim(lake+Vector3(0,3,4),lake+Vector3(-16,-3,-22))
        await create_timer(.15).timeout
        await RenderingServer.frame_post_draw
        var output:=report.get_base_dir()
        root.get_texture().get_image().save_png(output.path_join("cave-lake.png"))
        game.execute_command("cave map")
        await create_timer(.15).timeout
        await RenderingServer.frame_post_draw
        root.get_texture().get_image().save_png(output.path_join("cave-map.png"))
    root.remove_child(game)
    game.queue_free()
    for i in range(4):await process_frame
    print("CAVE_FRAME_BENCHMARK_PASS")
    quit()
