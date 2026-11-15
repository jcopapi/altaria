extends "res://tests/cartoon_runtime.gd"

var output: String
var mode: String
var csv: FileAccess

func capture(label: String, eye: Vector3, target: Vector3) -> void:
    aim(eye,target)
    await create_timer(.35).timeout
    await settle()
    await create_timer(.15).timeout
    await RenderingServer.frame_post_draw
    check(root.get_texture().get_image().save_png(output.path_join(label + ".png")) == OK, "Capture " + label)

func measure(label: String, position: Vector3, velocity: Vector3, seconds: float) -> void:
    var values: Array[float] = []
    var begin := Time.get_ticks_usec()
    var previous := begin
    var elapsed := 0.0
    while elapsed < seconds:
        actor.position = position + velocity * elapsed
        await process_frame
        var now := Time.get_ticks_usec()
        elapsed = (now - begin) * .000001
        var ms := (now - previous) * .001
        previous = now
        csv.store_csv_line(PackedStringArray([label,str(elapsed),str(ms)]))
        values.append(ms)
    values.sort()
    var state: Dictionary = game.get_debug_state()
    print("SURFACE_FRAME ", label, " n=",values.size()," p50=",values[int((values.size()-1)*.5)],
        " p95=",values[int((values.size()-1)*.95)]," p99=",values[int((values.size()-1)*.99)],
        " max=",values.back()," voxels=",state.voxel_bytes," pending=",state.pending,
        " mesh_ms=",state.mesh_ms," max_upload_ms=",state.max_upload_ms," max_mesh_upload_ms=",state.max_mesh_upload_ms," max_collider_ms=",state.max_collider_ms)

func run() -> void:
    output = OS.get_environment("ALTARIA_SURFACE_OUTPUT")
    mode = OS.get_environment("ALTARIA_SURFACE_MODE")
    Engine.max_fps=120
    DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
    csv=FileAccess.open(output.path_join("frames.csv"),FileAccess.WRITE)
    csv.store_csv_line(PackedStringArray(["scenario","elapsed_seconds","frame_ms"]))
    game=load("res://scenes/main.tscn").instantiate()
    root.add_child(game)
    await process_frame
    check(game.start_world("Surface reform",1337),"Load real generation and block catalog")
    find_player()
    Input.mouse_mode=Input.MOUSE_MODE_VISIBLE
    game.execute_command("time pause")
    var origin:=Vector3(100,200,100)
    aim(origin+Vector3(0,1.65,0),origin+Vector3(20,-30,50))
    await create_timer(.3).timeout
    await settle()
    var state: Dictionary = game.get_debug_state()
    print("SURFACE_READY ms=",state.world_ready_ms," first=",state.first_tile_ms," voxels=",state.voxel_bytes,
        " tiles=",state.tiles," decorations=",state.decorations," textures=",state.texture_layers)
    check(state.lod0>0 and state.lod5>0,"Six voxel LOD levels stream")
    if mode.begins_with("after"):
        check(state.decorations==0 and state.texture_layers==11,"Pebbles removed and new atlas loaded")
    await measure("stationary",origin,Vector3.ZERO,2.0)
    await measure("streaming",origin,Vector3(85,0,20),4.0)
    csv.close()
    if mode.ends_with("visual"):
        await capture("old-river-site",Vector3(-61,194,157),Vector3(-88,76,170))
        await capture("old-pillar-site",Vector3(-68,219,193),Vector3(-75,100,142))
        if mode.begins_with("after"):
            await capture("river-valley",Vector3(325,140,91),Vector3(272,41,16))
            await capture("rocky-coast",Vector3(-1030,162,1110),Vector3(-753,130,1080))
            aim(Vector3(1,250,8),Vector3(2,244,2))
            await create_timer(.3).timeout
            await settle()
            for x in range(5):
                for z in range(4):
                    check(game.set_voxel(x,243,z,26),"Place gravel from the block registry")
            for y in range(244,247):
                check(game.set_voxel(2,y,2,9),"Place textured trunk")
            check(game.set_voxel(2,247,2,10),"Place cutout leaves")
            check(game.set_voxel(4,244,1,9),"Place removable trunk")
            await capture("block-textures",Vector3(5,250,8),Vector3(2,245,1))
            check(game.get_voxel(1,243,1)==26,"Gravel persists after remesh")
            check(game.release_voxel(4,244,1,false),"Textured trunk enters physics")
    root.remove_child(game)
    game.queue_free()
    for i in range(4): await process_frame
    print("SURFACE_RUNTIME_PASS" if failures.is_empty() else "SURFACE_RUNTIME_FAIL " + str(failures))
    quit(0 if failures.is_empty() else 1)
