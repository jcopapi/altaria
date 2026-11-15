extends "res://tests/cartoon_runtime.gd"
func measure(label: String, eye: Vector3, target: Vector3) -> void:
    aim(eye,target)
    await create_timer(.7).timeout
    await settle()
    await create_timer(.6).timeout
    var samples: Array[float] = []
    var previous := Time.get_ticks_usec()
    for i in range(180):
        await process_frame
        var now := Time.get_ticks_usec()
        samples.append(float(now - previous) / 1000.0)
        previous = now
    samples.sort()
    var state: Dictionary = game.get_debug_state()
    print("ROUND_FPS ",label," median=",samples[90]," p95=",samples[171]," p99=",samples[178]," max=",samples[-1],
        " fps=",1000.0/samples[90]," triangles=",state.get("triangles",0)," tiles=",state.tiles)
    await RenderingServer.frame_post_draw
    root.get_texture().get_image().save_png(OS.get_environment("ALTARIA_BENCH_OUT") + "-" + label + ".png")

func run() -> void:
    root.size = Vector2i(1280,720)
    DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
    game=load("res://scenes/main.tscn").instantiate()
    root.add_child(game)
    await process_frame
    await RenderingServer.frame_post_draw
    game.start_world("Terrain FPS benchmark",1337)
    find_player()
    game.execute_command("time pause")
    game.execute_command("time set 12")
    game.execute_command("wind 0")
    await measure("river",Vector3(265.5,56,13.5),Vector3(282.5,42,17))
    await measure("clouds",Vector3(556.5,164,-865.5),Vector3(650,166,-959))
    quit(0 if failures.is_empty() else 1)
