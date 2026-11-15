extends SceneTree
var game: Node
var failed := false
func _initialize() -> void:
    run.call_deferred()
func terrain_ready() -> void:
    for i in range(1500):
        await create_timer(0.02).timeout
        var state: Dictionary = game.get_debug_state()
        if state.get("pending", 1) == 0 and state.get("tiles", 0) == state.get("wanted", -1) and state.get("lod5", 0) > 0:
            return
    failed = true
    push_error("Terrain did not become ready")
func capture(name: String) -> void:
    await RenderingServer.frame_post_draw
    DirAccess.make_dir_recursive_absolute("/tmp/altaria-biome-preview")
    if root.get_texture().get_image().save_png("/tmp/altaria-biome-preview/" + name + ".png") != OK:
        failed = true
func run() -> void:
    game = load("res://scenes/main.tscn").instantiate()
    root.add_child(game)
    await process_frame
    game.start_world("Biome preview", 1337)
    var actor: Node3D = game.find_children("*", "FlyingPlayer", true, false)[0]
    var camera: Camera3D = game.find_children("*", "Camera3D", true, false)[0]
    actor.set_physics_process(false)
    actor.set_process_unhandled_input(false)
    Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
    await terrain_ready()
    await create_timer(1.0).timeout
    await capture("horizon")
    print("VISUAL_STATS ", game.get_debug_state())
    for biome in ["normal", "snow", "desert", "jungle", "enchanted"]:
        var result: String = game.execute_command("biome " + biome)
        if not result.begins_with("Teleported"):
            push_error(result)
            failed = true
            continue
        var position := actor.position
        var ground := -64
        for y in range(318, -64, -1):
            var id: int = game.get_voxel(floori(position.x), y, floori(position.z))
            if id != 0 and id != 5:
                ground = y
                break
        actor.position = Vector3(position.x + 0.5, ground + 3.0, position.z + 0.5)
        camera.rotation.x = -0.62
        await terrain_ready()
        await create_timer(0.5).timeout
        await capture(biome)
        print("BIOME_PREVIEW ", biome, " ", game.get_debug_state())
    # A small strip demonstrates independent paint on the same grass block ID.
    game.execute_command("biome normal")
    var position := actor.position
    var x := floori(position.x)
    var z := floori(position.z)
    var y := ceili(position.y) + 4
    for dz in range(8):
        for dx in range(10):
            game.set_voxel(x + dx, y, z - dz, 3)
            if dx < 3:
                game.paint_voxel(x + dx, y, z - dz, "#e13943", 0.35, 1.15)
            elif dx < 6:
                game.paint_voxel(x + dx, y, z - dz, "#326dff", 0.35, 1.15)
            else:
                game.paint_voxel(x + dx, y, z - dz, "#c548ff", 0.35, 1.15)
    actor.position = Vector3(x + 4.5, y + 5, z + 3)
    actor.rotation.y = 0
    camera.rotation.x = -0.70
    await terrain_ready()
    await create_timer(0.6).timeout
    await capture("paint")
    print("BIOME_VISUAL_PASS" if not failed else "BIOME_VISUAL_FAIL")
    quit(1 if failed else 0)
