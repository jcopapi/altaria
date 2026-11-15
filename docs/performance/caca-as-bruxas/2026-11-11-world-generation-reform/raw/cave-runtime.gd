extends "res://tests/cartoon_runtime.gd"

var csv: FileAccess
var output: String
var mode: String
var eye := Vector3(100, 135, 100)

func samples(label: String, origin: Vector3, velocity: Vector3, seconds: float) -> void:
    var begin := Time.get_ticks_usec()
    var previous := begin
    var elapsed := 0.0
    var values: Array[float] = []
    while elapsed < seconds:
        actor.position = origin + velocity * elapsed
        await process_frame
        var now := Time.get_ticks_usec()
        elapsed = (now - begin) * 0.000001
        var frame_ms := (now - previous) * 0.001
        previous = now
        values.append(frame_ms)
        csv.store_csv_line(PackedStringArray([label, str(elapsed), str(frame_ms)]))
    values.sort()
    var stats: Dictionary = game.get_debug_state()
    print("REFORM_FRAME ", label, " n=", values.size(), " p50=", values[int((values.size()-1)*.5)],
        " p95=", values[int((values.size()-1)*.95)], " p99=", values[int((values.size()-1)*.99)],
        " max=", values.back(), " voxel_bytes=", stats.get("voxel_bytes",0), " pending=", stats.pending,
        " build=", stats.get("average_build_ms",0))

func capture(label: String) -> void:
    if DisplayServer.get_name() == "headless":
        return
    await create_timer(.35).timeout
    await RenderingServer.frame_post_draw
    check(root.get_texture().get_image().save_png(output.path_join(label + ".png")) == OK, "Save " + label)

func run() -> void:
    output = OS.get_environment("ALTARIA_REFORM_OUTPUT")
    mode = OS.get_environment("ALTARIA_REFORM_MODE")
    Engine.max_fps = 120
    if DisplayServer.get_name() != "headless":
        DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
    csv = FileAccess.open(output.path_join("frames.csv"), FileAccess.WRITE)
    csv.store_csv_line(PackedStringArray(["scenario","elapsed_seconds","frame_ms"]))
    game = load("res://scenes/main.tscn").instantiate()
    root.add_child(game)
    await process_frame
    check(game.start_world("Cave reform validation", 1337), "Start world with live JSON configuration")
    if not failures.is_empty():
        quit(1)
        return
    find_player()
    Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
    game.execute_command("time pause")
    aim(eye + Vector3(0, 1.65, 0), eye + Vector3(20, -30, 50))
    await create_timer(.3).timeout
    await settle()
    var initial: Dictionary = game.get_debug_state()
    print("REFORM_INITIAL first_tile_ms=", initial.get("first_tile_ms",0), " ready_ms=", initial.get("world_ready_ms",0),
        " voxel_bytes=", initial.get("voxel_bytes",0), " tiles=", initial.tiles)
    await samples("stationary", eye, Vector3.ZERO, 2.0)
    await samples("streaming", eye, Vector3(85,0,20), 4.0)
    csv.close()
    if mode == "reform":
        var openings: Dictionary = {}
        var hall: Dictionary = {}
        var deep: Dictionary = {}
        for p in [Vector3(100, 135, 100), Vector3(-112, 135, 83), Vector3(100, 135, -150), Vector3(-160, 135, -150), Vector3(100, 135, 400)]:
            actor.position = p
            await create_timer(.25).timeout
            await settle()
            var state: Dictionary = game.get_debug_state()
            for entry in state.cave_entrances:
                if not openings.has(entry.kind): openings[entry.kind] = entry
            for c in state.cave_chambers:
                if c.depth == 2 and c.archetype == "giant" and game.get_cave_environment(c.center).get("fluid", "") == "dry":
                    hall = c
                if c.depth == 3 and c.center.y < -510 and deep.is_empty():
                    deep = c
            if openings.size() >= 2 and not hall.is_empty() and not deep.is_empty(): break
        check(not openings.is_empty(), "Find a real surface entrance")
        check(not hall.is_empty(), "Find a giant landmark")
        check(not deep.is_empty(), "Find a cave below old flight limit")
        print("REFORM_LANDMARKS entrances=", openings, " hall=", hall, " deep=", deep)
        for kind in openings:
            var lip: Vector3 = openings[kind].position
            aim(lip + Vector3(-17, 21, 18), lip + Vector3(0,-7,0))
            await create_timer(.25).timeout
            await settle()
            await capture(kind)
        if not hall.is_empty():
            game.execute_command("fullbright off")
            var lamp := SpotLight3D.new()
            lamp.spot_range = 125
            lamp.spot_angle = 76
            lamp.light_energy = 3.2
            lamp.light_color = Color(1, .88, .69)
            camera.add_child(lamp)
            var center: Vector3 = hall.center
            aim(center + Vector3(-hall.radius.x*.45, 7, -hall.radius.z*.26), center + Vector3(hall.radius.x*.6, -8, 0))
            await create_timer(.25).timeout
            await settle()
            await capture("landmark")
            var c := Vector3i(center.round())
            var id: int = game.get_voxel(c.x,c.y,c.z)
            check(id == 0 or id == 5, "Loaded landmark center is air or water")
        if not deep.is_empty():
            var c := Vector3i(deep.center.round())
            var cmd := "tp %s %s %s" % [c.x,c.y,c.z]
            check(game.execute_command(cmd).begins_with("Teleported"), "tp reaches the new depths")
            actor.set_physics_process(true)
            await create_timer(.35).timeout
            check(actor.position.y < -510, "Flight no longer clamps at the old depth")
            actor.set_physics_process(false)
            await settle()
            var id: int = game.get_voxel(c.x,c.y,c.z)
            check(id == 0 or id == 5 or id == 13, "Deep chamber voxel is nonsolid")
            var env: Dictionary = game.get_cave_environment(Vector3(c))
            check(env.loaded and env.below_surface > 600 and env.depth == "infernal", "Live streamed cave reaches more than 600 m below surface")
            print("REFORM_DEEPEST ", c, " block=", id, " below_surface=", env.get("below_surface",0), " fluid=",env.get("fluid",""))
            check(game.get_voxel(c.x,-896,c.z) == 7, "Implicit substrate keeps bedrock")
            check(game.get_voxel(c.x,-880,c.z) == 1, "Implicit substrate remains solid stone")
            check(game.set_voxel(c.x,-880,c.z,0), "Mine inside implicit substrate")
            await create_timer(.25).timeout
            await settle()
            check(game.get_voxel(c.x,-880,c.z) == 0 and game.get_voxel(c.x,-881,c.z) == 1, "Deep edit remesh preserves surrounding stone")
    root.remove_child(game)
    game.queue_free()
    for i in range(4): await process_frame
    print("CAVE_REFORM_RUNTIME_PASS" if failures.is_empty() else "CAVE_REFORM_RUNTIME_FAIL " + str(failures))
    quit(0 if failures.is_empty() else 1)
