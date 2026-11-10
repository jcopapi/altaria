extends SceneTree

var game: Node
var failures: Array[String] = []

func check(condition: bool, message: String) -> void:
    if not condition:
        failures.append(message)
        push_error(message)

func _initialize() -> void:
    run.call_deferred()

func capture(name: String) -> void:
    if DisplayServer.get_name() == "headless":
        return
    await RenderingServer.frame_post_draw
    var directory := "/tmp/altaria-preview"
    DirAccess.make_dir_recursive_absolute(directory)
    check(root.get_texture().get_image().save_png(directory.path_join(name + ".png")) == OK, "Capture " + name)

func wait_for_terrain() -> void:
    for i in range(1000):
        await create_timer(0.02).timeout
        var state: Dictionary = game.get_debug_state()
        if state.get("pending", 1) == 0 and state.get("tiles", 0) == state.get("wanted", -1) and state.get("lod3", 0) > 0:
            return
    check(false, "Terrain streaming finished within 20 seconds")

func physical_key(code: Key, pressed: bool) -> void:
    var event := InputEventKey.new()
    event.keycode = code
    event.physical_keycode = code
    event.pressed = pressed
    Input.parse_input_event(event)

func run() -> void:
    game = load("res://scenes/main.tscn").instantiate()
    root.add_child(game)
    await process_frame
    check(game.get_debug_state().language == "en", "English is default")
    check(game.get_debug_state().world_open, "Play immediately creates a random world")
    var escape := InputEventKey.new()
    escape.keycode = KEY_ESCAPE
    escape.pressed = true
    Input.parse_input_event(escape)
    await process_frame
    await capture("menu")
    check(game.start_world("Runtime test", 1337), "Create world")
    await wait_for_terrain()
    var state: Dictionary = game.get_debug_state()
    print("RUNTIME_STATS ", state)
    check(state.get("lod0", 0) > 0 and state.get("lod1", 0) > 0 and state.get("lod2", 0) > 0 and state.get("lod3", 0) > 0, "All LODs streaming")
    check(state.get("lod4", 0) > 0 and state.get("lod5", 0) > 0, "Distant voxel horizon has six LOD levels")
    check(state.get("view_distance", 0) >= 6000, "Large world view distance")
    check(state.get("decorations", 0) > 0 and state.get("overlay_quads", 0) > 0 and state.get("texture_layers", 0) >= 10, "Assets and procedural surface layers loaded")
    check(state.get("triangles", 0) > 1000, "Terrain geometry uploaded")
    await create_timer(0.8).timeout
    await capture("world")
    check(game.execute_command("fullbright on") == "fullbright: ON", "Fullbright on")
    check(game.get_debug_state().fullbright, "Fullbright material state")
    check(game.execute_command("fullbright off") == "fullbright: OFF", "Fullbright off")
    check(game.execute_command("fullbright nonsense").begins_with("Usage:"), "Invalid command rejected")
    check(game.execute_command("speed nan").begins_with("Usage:"), "Non-finite input rejected")
    check(game.execute_command("fly off") == "fly: OFF", "Walking mode")
    check(game.execute_command("fly on") == "fly: ON", "Flight mode")
    check(game.execute_command("block altaria:clay") == "Selected altaria:clay", "Data-driven block selected")
    check(game.execute_command("spawn altaria:marker") == "Entity spawned.", "Data-driven entity spawned")
    check(game.set_voxel(-1, 290, -33, 8), "Place at negative chunk boundary")
    check(game.set_voxel(-1, 100, -33, 0), "Remove block")
    check(game.execute_command("paint at -1 290 -33 #cc33ff 0.4 1.25") == "Paint applied.", "Paint console command")
    check(game.get_voxel_appearance(-1, 290, -33).color == "#cc33ff", "Individual HSV tint stored")
    check(game.execute_command("overlay at -1 290 -33 grass") == "Overlay applied.", "Grass layer added over clay base")
    check(game.execute_command("overlay at -1 290 -33 pebbles2") == "Overlay applied.", "Decoration added over surface overlay")
    check(game.get_voxel(-1, 290, -33) == 8, "Overlay does not replace underlying block")
    check(game.get_voxel_appearance(-1, 290, -33).overlay_id == 1 and game.get_voxel_appearance(-1, 290, -33).decoration_id == 3, "Independent overlay and decoration slots")
    check(game.execute_command("paint at -1 290 -33 #zzzzzz").begins_with("Invalid"), "Invalid color rejected")
    check(not game.set_voxel(0, -64, 0, 0), "Bedrock protected")
    check(game.save_world(), "Save metadata")
    var path: String = game.get_world_path()
    check(game.open_world(path), "Reopen saved world")
    check(game.get_voxel(-1, 290, -33) == 8, "Placement survives reopening")
    check(game.get_voxel(-1, 100, -33) == 0, "Removal survives reopening")
    var appearance: Dictionary = game.get_voxel_appearance(-1, 290, -33)
    check(appearance.painted and appearance.color == "#cc33ff" and appearance.overlay_id == 1 and appearance.decoration_id == 3, "Paint and stacked overlays survive reopening")
    # Jump across the origin into another LOD partition, then return to edited chunks.
    check(game.execute_command("tp -600 230 -600") == "Teleported.", "Teleport across negative world coordinates")
    await wait_for_terrain()
    check(game.execute_command("tp -1 310 -33") == "Teleported.", "Return to edit")
    await wait_for_terrain()
    check(game.get_voxel(-1, 290, -33) == 8, "Placement survives unload and regeneration")
    check(game.get_voxel(-1, 100, -33) == 0, "Air edit survives unload and regeneration")
    check(game.get_debug_state().get("edits", 0) == 2, "Only sparse edits stored")
    if DisplayServer.get_name() == "headless":
        game.execute_command("fly off")
        game.execute_command("tp -0.5 291.02 -32.5")
        await create_timer(0.2).timeout
        check(game.get_debug_state().grounded, "Voxel body lands on placed block")
        physical_key(KEY_SPACE, true)
        await create_timer(0.08).timeout
        check(game.get_debug_state().jump_count == 1, "First jump from physical key")
        physical_key(KEY_SPACE, false)
        await create_timer(0.06).timeout
        physical_key(KEY_SPACE, true)
        await create_timer(0.06).timeout
        check(game.get_debug_state().jump_count == 2, "Second jump while airborne")
        physical_key(KEY_SPACE, false)
        await create_timer(0.03).timeout
        physical_key(KEY_SPACE, true)
        await create_timer(0.03).timeout
        check(game.get_debug_state().jump_count == 2, "Third airborne jump rejected")
        physical_key(KEY_SPACE, false)
        physical_key(KEY_F, true)
        await process_frame
        physical_key(KEY_F, false)
        await process_frame
        check(game.get_debug_state().flying, "F toggles flight")
        var before: Vector3 = game.get_debug_state().player_position
        physical_key(KEY_W, true)
        await create_timer(0.2).timeout
        physical_key(KEY_W, false)
        var after: Vector3 = game.get_debug_state().player_position
        check(before.distance_to(after) > 0.3, "Flying movement from physical input")
    for biome in ["normal", "snow", "desert", "jungle", "enchanted"]:
        check(game.execute_command("biome " + biome).begins_with("Teleported"), "Locate biome " + biome)
        check(game.get_debug_state().biome == biome, "Biome classification " + biome)
        if biome == "snow":
            check(game.get_debug_state().surface_block_id == 6, "Snowfields have actual snow material")
        elif biome == "desert":
            check(game.get_debug_state().surface_block_id == 4, "Desert uses sand material")
        else:
            check(game.get_debug_state().surface_block_id == 3, "Colored biomes share the same grass block")
    # Exercise the actual console input and command-submit signal.
    var key := InputEventKey.new()
    key.keycode = KEY_F1
    key.pressed = true
    Input.parse_input_event(key)
    await process_frame
    var command_input: LineEdit
    for node in game.find_children("*", "LineEdit", true, false):
        if node.placeholder_text.begins_with("Command"):
            command_input = node
    check(command_input != null and command_input.is_visible_in_tree(), "F1 opens console")
    if command_input:
        command_input.text = "fullbright on"
        command_input.text_submitted.emit("fullbright on")
    check(game.get_debug_state().fullbright, "Console executes command")
    await create_timer(0.3).timeout
    await capture("console")
    var previous_world: String = game.get_world_path()
    check(game.execute_command("world new 12345").begins_with("Generating new world."), "Generate via console during play")
    check(game.get_debug_state().seed == 12345, "Explicit console seed is reproducible")
    check(game.get_world_path() != previous_world, "Console generation creates a separate world")
    check(game.get_debug_state().fullbright, "Debug lighting survives console regeneration")
    check(command_input.is_visible_in_tree(), "Console stays open while generating another world")
    await wait_for_terrain()
    check(game.get_debug_state().first_tile_ms >= 0 and game.get_debug_state().world_ready_ms > 0, "Generation timing is measured")
    check(game.execute_command("world new").begins_with("Generating new world."), "Console generates random seed when omitted")
    await wait_for_terrain()
    check(game.open_world(previous_world), "Previous world retained after regeneration")
    check(game.get_voxel(-1, 290, -33) == 8 and game.get_voxel(-1, 100, -33) == 0, "Previous edits retained after console regeneration")
    if failures.is_empty():
        print("RUNTIME_SMOKE_PASS")
        quit(0)
    else:
        print("RUNTIME_SMOKE_FAIL ", failures)
        quit(1)
