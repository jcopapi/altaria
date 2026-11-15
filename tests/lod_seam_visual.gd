extends "res://tests/cartoon_runtime.gd"

var sampled_frames := 0
var fading_frames := 0
var background_samples := 0

func sample_terrain() -> void:
    await RenderingServer.frame_post_draw
    var image := root.get_texture().get_image()
    var state: Dictionary = game.get_debug_state()
    sampled_frames += 1
    if state.lod_fading:
        fading_frames += 1
    var gaps := 0
    # The top strip can see past the loaded horizon with fog disabled.
    for y in range(24, image.get_height() - 8, 2):
        for x in range(8, image.get_width() - 8, 2):
            var color := image.get_pixel(x, y)
            if color.r > .69 and color.g < .045 and color.b < .045:
                gaps += 1
    background_samples += gaps
    if gaps > 0:
        print("LOD_SEAM_GAP frame=", sampled_frames, " samples=", gaps, " eye=", camera.global_position)

func fly_route(reverse: bool) -> void:
    var start := Vector3(556.5, 256.26, -865.5)
    # On the return trip the loaded horizon is closer behind the flight path.
    var direction := Vector3(120, -285 if reverse else -235, -100)
    for i in range(100):
        var t := (99 - i if reverse else i) / 99.0
        var eye := start + Vector3(t * 384.0, 8.0 * sin(t * TAU), 160.0 * sin(t * PI))
        aim(eye, eye + direction.rotated(Vector3.UP, t * .85))
        await sample_terrain()
        await create_timer(.04).timeout

func run() -> void:
    root.size = Vector2i(1000, 600)
    game = load("res://scenes/main.tscn").instantiate()
    root.add_child(game)
    await process_frame
    await RenderingServer.frame_post_draw
    check(game.start_world("LOD seam visual", 1337), "Open fixed terrain")
    if not failures.is_empty():
        quit(1)
        return
    find_player()
    game.execute_command("time pause")
    game.execute_command("biome enchanted")
    for layer in game.find_children("*", "CanvasLayer", true, false):
        if layer.layer == 20:
            layer.hide()
    var start := Vector3(556.5, 256.26, -865.5)
    aim(start, start + Vector3(120, -235, -100))
    await settle()
    game.execute_command("fullbright on")
    var world_environment := game.find_children("*", "WorldEnvironment", true, false)[0] as WorldEnvironment
    world_environment.environment.background_mode = Environment.BG_COLOR
    world_environment.environment.background_color = Color.RED
    await create_timer(.45).timeout
    await sample_terrain()
    await fly_route(false)
    await settle()
    await fly_route(true)
    await settle()
    await sample_terrain()
    check(fading_frames > 0, "The route exercises LOD transitions")
    check(background_samples == 0, "Opaque ground covers the background throughout the LOD swaps")
    print("LOD_SEAM_VISUAL samples=", sampled_frames, " fading=", fading_frames, " gaps=", background_samples)
    quit(0 if failures.is_empty() else 1)
