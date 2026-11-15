extends "res://tests/cartoon_runtime.gd"

func press_key(code: Key, character := 0) -> void:
    var event := InputEventKey.new()
    event.keycode = code
    event.unicode = character
    event.pressed = true
    Input.parse_input_event(event)
    event = event.duplicate()
    event.pressed = false
    Input.parse_input_event(event)

func run() -> void:
    print("[TEST] Starting Console & Debug Runtime Tests...")
    game = load("res://scenes/main.tscn").instantiate()
    root.add_child(game)
    await process_frame
    check(not game.get_debug_state().world_open, "Startup waits for a world command")
    check(game.execute_command("resolution list").contains("1920x1080"), "List highlights Full HD")
    check(game.execute_command("resolution 1x1").begins_with("Invalid"), "Reject invalid window sizes")
    check(game.execute_command("resolution 1600x900").ends_with("1600x900"), "Set custom window size at startup")
    check(game.execute_command("resolution").ends_with("1600x900"), "Report the current window size")
    check(game.execute_command("resolution 1920x1080").ends_with("1920x1080"), "Set Full HD")
    var preferences = JSON.parse_string(FileAccess.get_file_as_string("user://preferences.json"))
    check(preferences is Dictionary and preferences.get("resolution") == "1920x1080", "Persist the chosen resolution")
    var startup_console = game.get_console()
    check(startup_console != null and startup_console.is_enabled(), "Startup opens the console")
    if startup_console:
        var startup_input: LineEdit = startup_console.find_child("ConsoleLineEdit", true, false)
        check(startup_input.has_focus() and startup_input.is_editing(), "Startup is ready for keyboard input")
        press_key(KEY_H, "h".unicode_at(0))
        await process_frame
        check(startup_input.text == "h", "The first key is typed without clicking")
        startup_input.clear()
        press_key(KEY_ESCAPE)
        await process_frame
        check(not startup_console.is_enabled(), "Escape closes the console")
        press_key(KEY_ESCAPE)
        await process_frame
        check(startup_console.is_enabled() and startup_input.has_focus(), "Escape reopens the focused console")
    check(game.start_world("Console Test World", 12345), "Start console test world")
    var loading: Control = game.find_child("WorldLoading", true, false)
    check(loading != null and loading.visible, "World generation shows the loading panel")
    find_player()
    await settle()
    for frame in range(30):
        if not loading.visible:
            break
        await create_timer(0.02).timeout
    check(not loading.visible, "Loading hides when initial streaming is ready")
    for deposit_name in ["pebble", "ore"]:
        var batches := game.find_children(deposit_name + "_deposit*", "MultiMeshInstance3D", true, false)
        check(batches.any(func(batch: MultiMeshInstance3D) -> bool:
            return batch.multimesh.instance_count > 0 and batch.multimesh.mesh != null),
              "World spawns batched %s models" % deposit_name)

    # 1. General commands
    var help_out: String = game.execute_command("help")
    check(help_out.begins_with("Commands"), "help output lists command categories")
    var help_tp: String = game.execute_command("help tp")
    check(help_tp.contains("Usage: tp <x> <y> <z>"), "help tp shows detailed usage")

    var echo_out: String = game.execute_command("echo hello altaria")
    check(echo_out == "hello altaria", "echo returns argument string")

    var ver_out: String = game.execute_command("version")
    check(ver_out.contains("Altaria") and ver_out.contains("Jolt"), "version contains Altaria & Jolt")

    # 2. Player commands
    var fly_off: String = game.execute_command("fly off")
    check(fly_off.contains("OFF") and not actor.is_flying(), "fly off sets player flying to false")
    var fly_on: String = game.execute_command("fly on")
    check(fly_on.contains("ON") and actor.is_flying(), "fly on sets player flying to true")

    var noclip_on: String = game.execute_command("noclip on")
    check(noclip_on.contains("ON") and actor.is_noclip(), "noclip on enables noclip")
    var noclip_off: String = game.execute_command("noclip off")
    check(noclip_off.contains("OFF") and not actor.is_noclip(), "noclip off disables noclip")

    var god_on: String = game.execute_command("god on")
    check(god_on.contains("ON") and actor.is_god(), "god on enables god mode")
    var god_off: String = game.execute_command("god off")
    check(god_off.contains("OFF") and not actor.is_god(), "god off disables god mode")

    var speed_set: String = game.execute_command("speed 45")
    check(speed_set.contains("45") and is_equal_approx(actor.get_speed(), 45.0), "speed set to 45")

    var grav_set: String = game.execute_command("gravity 15")
    check(grav_set.contains("15") and is_equal_approx(actor.get_gravity(), 15.0), "gravity set to 15")

    var reset_out: String = game.execute_command("resetplayer")
    check(reset_out.contains("restored") and not actor.is_noclip() and not actor.is_god(), "resetplayer restores defaults")

    var tp_out: String = game.execute_command("tp 10 150 20")
    check(tp_out.contains("Teleported"), "tp teleports player")
    check(actor.position.is_equal_approx(Vector3(10, 150, 20)), "player position updated to (10, 150, 20)")

    var pos_out: String = game.execute_command("pos")
    check(pos_out.contains("Position: X=10") and pos_out.contains("Z=20"), "pos reports current coordinates")

    # 3. World commands
    var seed_out: String = game.execute_command("world seed")
    check(seed_out.contains("12345"), "world seed reports 12345")

    var world_info: String = game.execute_command("world info")
    check(world_info.contains("Min Y") and world_info.contains("Max Y"), "world info reports world boundaries")

    var time_set: String = game.execute_command("time set 14.5")
    check(time_set.contains("14.5"), "time set sets hour to 14.5")
    var time_pause: String = game.execute_command("time pause")
    check(time_pause.contains("paused"), "time pause pauses time")
    var time_resume: String = game.execute_command("time resume")
    check(time_resume.contains("resumed"), "time resume resumes time")

    var weather_set: String = game.execute_command("weather set rain")
    check(weather_set.contains("rain"), "weather set to rain")
    var weather_get: String = game.execute_command("weather get")
    check(weather_get.contains("rain"), "weather get reports rain")

    var wind_set: String = game.execute_command("wind 1.5")
    check(wind_set.contains("1.5"), "wind set to 1.5")

    var temp_out: String = game.execute_command("temperature")
    check(temp_out.contains("Temperature at") and temp_out.contains("Air:"), "temperature reports thermal metrics")

    var biome_info: String = game.execute_command("biome info")
    check(biome_info.contains("Biome:"), "biome info reports active biome")
    var biome_list: String = game.execute_command("biome list")
    check(biome_list.contains("Registered biomes"), "biome list lists all biomes")

    # 4. Chunk commands
    var chunk_stats: String = game.execute_command("chunk stats")
    check(chunk_stats.contains("Total Tiles:") and chunk_stats.contains("LOD0..5"), "chunk stats reports chunk breakdown")

    var chunk_list: String = game.execute_command("chunk list")
    check(chunk_list.contains("Loaded Chunks"), "chunk list lists coordinates")

    var chunk_queue: String = game.execute_command("chunk queue")
    check(chunk_queue.contains("Chunk Pipeline Queues"), "chunk queue reports pipeline")

    var chunk_info: String = game.execute_command("chunk info 0 0")
    check(chunk_info.contains("Triangles:") or chunk_info.contains("not loaded"), "chunk info executes safely")

    # 5. Performance commands
    var fps_out: String = game.execute_command("fps")
    check(fps_out.contains("FPS:"), "fps reports engine framerate")

    var perf_out: String = game.execute_command("perf")
    check(perf_out.contains("Performance Telemetry:"), "perf reports performance breakdown")

    var mem_out: String = game.execute_command("mem")
    check(mem_out.contains("Memory Breakdown:"), "mem reports memory breakdown")

    var render_out: String = game.execute_command("render stats")
    check(render_out.contains("Draw Calls:") or render_out.contains("Rendering Pipeline"), "render stats reports rendering metrics")

    var physics_out: String = game.execute_command("physics stats")
    check(physics_out.contains("Jolt Physics 3D"), "physics stats reports Jolt status")

    var bench_run: String = game.execute_command("benchmark run gen")
    check(bench_run.contains("BENCHMARK REPORT") and bench_run.contains("COMPLETED"), "benchmark run executes successfully")

    var bench_rep: String = game.execute_command("benchmark report")
    check(bench_rep.contains("BENCHMARK REPORT"), "benchmark report returns recorded data")

    # 6. Visual and Debug commands
    var fullbright_on: String = game.execute_command("fullbright on")
    check(fullbright_on.contains("ON"), "fullbright on enables fullbright")
    var fullbright_off: String = game.execute_command("fullbright off")
    check(fullbright_off.contains("OFF"), "fullbright off disables fullbright")

    check(game.execute_command("light follow") == "Following light on.", "Follow toggles the personal lamp")
    check(game.execute_command("help light follow").contains("Usage: light follow"), "Follow has its own help")
    check(game.execute_command("light follow blue") == "Following light updated.", "A color name updates the lamp")
    var lamp: OmniLight3D = camera.get_node_or_null("ExplorationLight")
    check(lamp != null and lamp.light_color.is_equal_approx(Color.BLUE), "The lamp takes the named color")
    check(game.execute_command("light") == "Colored light placed." and lamp.visible,
          "Bare light places a fixed light and leaves the lamp on")
    check(game.execute_command("light follow off") == "Following light off." and not lamp.visible,
          "Follow off turns off the lamp")
    check(game.execute_command("light blue") == "Colored light placed.", "A named color places a fixed light")
    var lights := game.find_children("*", "OmniLight3D", true, false)
    check(lights.any(func(light: OmniLight3D) -> bool:
        return (light != lamp and light.light_color.is_equal_approx(Color.BLUE) and
            light.global_position.is_equal_approx(camera.global_position))), "The fixed light is placed at the viewpoint")
    check(game.execute_command("light place red") == "Colored light placed.", "A fixed light accepts a color name")
    check(game.execute_command("light place") == "Colored light placed.", "A fixed light has defaults")
    check(game.execute_command("light #af55ff 3 12") == "Colored light placed.", "Legacy fixed lights still work")
    print("LOG_OLD_MARKER")
    await create_timer(1.1).timeout
    print("LOG_RECENT_MARKER")
    push_warning("LOG_WARNING_MARKER")
    check(game.execute_command("log export 0s").begins_with("Usage:"), "Log export validates duration")
    var log_result: String = game.execute_command("log export 1s")
    check(log_result.begins_with("Exported log to "), "Log export reports its path")
    if log_result.begins_with("Exported log to "):
        var log_text := FileAccess.get_file_as_string(log_result.trim_prefix("Exported log to "))
        check(log_text.contains("LOG_RECENT_MARKER") and not log_text.contains("LOG_OLD_MARKER"),
              "Export contains messages inside the requested window")
        check(log_text.contains("LOG_WARNING_MARKER") and log_text.contains("frame_ms="),
              "Export includes engine warnings and performance samples")

    var debug_overlay: String = game.execute_command("debug overlay on")
    check(debug_overlay.contains("ON") and game.is_debug_overlay(), "debug overlay on sets overlay visible")

    # 7. Suggestions and error tolerance
    var typo_out: String = game.execute_command("flyy")
    check(typo_out.contains("Did you mean: 'fly'"), "typo 'flyy' suggests 'fly'")

    var typo_noclp: String = game.execute_command("noclp")
    check(typo_noclp.contains("Did you mean: 'noclip'"), "typo 'noclp' suggests 'noclip'")

    # 8. Permissions
    var perm_safe: String = game.execute_command("permission safe")
    check(perm_safe.contains("Safe"), "permission mode set to Safe")
    var cheat_denied: String = game.execute_command("noclip on")
    check(cheat_denied.contains("Permission denied"), "cheat command denied in Safe mode")
    var perm_cheat: String = game.execute_command("permission cheat")
    check(perm_cheat.contains("Cheat"), "permission mode set back to Cheat")
    var cheat_allowed: String = game.execute_command("noclip on")
    check(cheat_allowed.contains("ON"), "cheat command allowed in Cheat mode")
    game.execute_command("noclip off")

    # 9. Console Focus & Autocomplete UI verification
    var console_node = game.get_console()
    if console_node:
        console_node.set_enabled(true)
        await create_timer(0.05).timeout
        var line_edit: LineEdit = console_node.find_child("ConsoleLineEdit", true, false)
        var auto_label: RichTextLabel = console_node.find_child("AutocompleteLabel", true, false)
        check(line_edit != null, "Console line edit found")
        if line_edit and auto_label:
            check(line_edit.has_focus(), "Line edit has focus when console is enabled")
            line_edit.text = "chun"
            console_node.update_autocomplete("chun")
            await create_timer(0.05).timeout
            check(auto_label.is_visible(), "Autocomplete label is visible when typing prefix")
            check(auto_label.text.contains("chunk"), "Autocomplete label suggests 'chunk'")
            # Test TAB autocomplete
            console_node.apply_autocomplete()
            check(line_edit.text.begins_with("chunk"), "TAB autocompletes 'chunk'")
            console_node.update_autocomplete("world new ")
            check(auto_label.text.contains("world new plain") and auto_label.text.contains("world new structure"),
                  "Autocomplete lists the world presets")
        check(game.execute_command("world new preset invalid").contains("Unknown preset"), "Unknown preset is rejected")
        check(game.execute_command("world seed").contains("12345"), "Invalid preset keeps the current world")
        check(game.execute_command("world new preset plain 42").contains("(plain)"), "Create a flat world with a seed")
        check(console_node.is_enabled() and line_edit.has_focus(), "World changes retain console input focus")
        check(game.execute_command("world info").contains("Preset: plain"), "World info reports the active preset")
        find_player()
        await settle()
        var flat_state: Dictionary = game.get_debug_state()
        print("PLAIN_PRESET tiles=", flat_state.tiles, " ready_ms=", flat_state.world_ready_ms)
        check(flat_state.tiles < 100 and flat_state.workers == 2, "Plain uses a small terrain workload")
        for cell in [Vector2i(0, 0), Vector2i(-1, 31), Vector2i(32, 32), Vector2i(300, -700)]:
            check(game.get_voxel(cell.x, 65, cell.y) == 0 and game.get_voxel(cell.x, 64, cell.y) == 3 and
                  game.get_voxel(cell.x, 63, cell.y) == 2, "Plain has the same grass and soil elevation")

        check(game.execute_command("world new 72 --preset structure").contains("(structure)"), "Create a structure world")
        find_player()
        var structure_state: Dictionary = game.get_debug_state()
        print("STRUCTURE_PRESET tiles=", structure_state.tiles, " ready_ms=", structure_state.world_ready_ms)
        check(not loading.visible and structure_state.world_ready_ms >= 0 and structure_state.tiles == 0 and
              structure_state.wanted == 0 and structure_state.workers == 0, "Structure starts immediately without streaming")
        for y in [-704, -300, 42, 64, 319]:
            check(game.get_voxel(0, y, 0) == 0, "Structure starts empty at all elevations")
        check(game.get_built_blocks().size() == 1, "Structure has one construction anchor")
        var hands: Control = game.find_child("FirstPersonHands", true, false)
        check(hands != null and not hands.is_visible_in_tree(), "Hands hide while the console is open")
        check(game.execute_command("item pickaxe").begins_with("Equipped pickaxe"), "Equip the right-hand pickaxe")
        check(game.execute_command("item axe left").begins_with("Equipped axe"), "Equip the left-hand axe")
        var right_grip: Node3D = hands.get_node("Right/Run/Action/ToolViewport/Grip")
        var left_grip: Node3D = hands.get_node("Left/Run/Action/ToolViewport/Grip")
        check(right_grip.get_child_count() == 1 and left_grip.get_child_count() == 1, "Each hand has a 3D model")
        var right_sprite: Sprite2D = hands.get_node("Right/Run/Action/HandSprite")
        check(right_sprite.texture == hands.get("closed_hand"), "Equipped hand uses the closed sprite")
        press_key(KEY_ESCAPE)
        await process_frame
        check(hands.is_visible_in_tree(), "Hands are visible during gameplay")
        hands.call("set_running", true)
        check(hands.get_node("Locomotion").current_animation == "run", "Sprint plays the run cycle")
        var cheat_panel: Control = game.find_child("CheatInventory", true, false)
        press_key(KEY_T)
        await process_frame
        check(not cheat_panel.visible, "Build Mode starts disabled")
        check(game.execute_command("buildmode on").contains("ON"), "Enable Build Mode")
        check(game.execute_command("cheat inventory on").contains("Unknown command"), "Legacy cheatinventory command removed")
        check(game.execute_command("bm off").contains("OFF") and game.execute_command("bm on").contains("ON"), "bm alias toggles Build Mode")
        press_key(KEY_T)
        await process_frame
        var search: LineEdit = cheat_panel.find_child("BlockSearch", true, false)
        var results: ItemList = cheat_panel.find_child("BlockResults", true, false)
        check(cheat_panel.visible and search.has_focus() and results.item_count > 1, "T opens searchable inventory")
        search.text = "altaria:planks"
        search.text_changed.emit(search.text)
        check(results.item_count == 1, "Filter blocks by registry key")
        var previously_held: Dictionary = game.get_active_hotbar_slot()
        if results.item_count == 1:
            results.item_selected.emit(0)
        check(not cheat_panel.visible and not console_node.is_enabled(), "Choosing a block closes inventory")
        check(game.get_active_hotbar_slot() == previously_held, "Grant keeps the active slot")
        var planks_slot := -1
        for slot in game.get_hotbar_slots():
            if slot.key == "altaria:planks":
                planks_slot = slot.slot
                check(slot.count == 64, "Cheat Inventory inserts a stack into the player inventory")
        check(planks_slot >= 0, "Planks are in the hotbar")
        press_key(KEY_T)
        await process_frame
        press_key(KEY_ESCAPE)
        await process_frame
        check(not cheat_panel.visible and not console_node.is_enabled(), "Escape closes inventory without opening console")
        if planks_slot >= 0:
            press_key(KEY_1 + planks_slot)
            await process_frame
        aim(Vector3(.5, 65.5, .5), Vector3(.5, 65.5, -8))
        click(MOUSE_BUTTON_RIGHT)
        await process_frame
        check(game.get_built_blocks().size() == 2, "Right click places a block in the void")
        check(hands.get_node("Actions").current_animation == "place", "Placement plays the hand animation")
        aim(Vector3(.5, 65.5, .5), Vector3(.5, 65.5, 8))
        await process_frame
        click(MOUSE_BUTTON_LEFT)
        await process_frame
        check(hands.get_node("Actions").current_animation == "break_right", "Breaking swings the equipped hand")
        check(game.execute_command("item clear").begins_with("Cleared") and right_grip.get_child_count() == 0,
              "Clear removes only the right tool")
        check(right_sprite.texture == hands.get("hold_hand"), "Held block hand uses the holding sprite")
        hands.call("set_held_block", null, 0, 0, 0)
        check(right_sprite.texture == hands.get("relaxed_hand"), "Empty idle hand uses the relaxed sprite")
        hands.call("set_running", true)
        check(right_sprite.texture == hands.get("closed_hand"), "Empty hand closes during sprint")
        check(game.get_active_hotbar_slot().count == 64, "Build Mode keeps the held stack unchanged")
        check(game.execute_command("buildmode off").contains("OFF"), "Disable Build Mode")
        aim(Vector3(.5, 65.5, .5), Vector3(.5, 62.5, -3.5))
        press_key(KEY_V)
        await process_frame
        var outline: MeshInstance3D = game.find_child("StructureSelection", true, false)
        check(outline.visible and outline.position == Vector3(0, 62, -4), "V marks the aimed block")
        check(game.execute_command("structure fill stone").contains("both corners"), "Fill needs two corners")
        press_key(KEY_B)
        await process_frame
        check(outline.custom_aabb.size == Vector3.ONE, "B can select the same block as the first corner")
        check(game.execute_command("structure pos2 1 63 -3").begins_with("Corner 2"), "Mark the other corner")
        check(outline.custom_aabb.size == Vector3(2, 2, 2), "Outline covers both inclusive corners")
        check(game.execute_command("structure fill").contains("8 cells with altaria:planks"),
              "Fill uses the inventory block")
        check(game.get_voxel(0, 62, -4) != 0 and game.get_built_blocks().size() == 1,
              "Fill replaces construction inside the selected area")
        var offset_piece: int = game.place_block(Vector3(2.125, 62, -3), 1, "slab", 1)
        check(offset_piece > 0, "Place a fractional structure piece")
        game.execute_command("structure pos2 3 63 -3")
        check(game.execute_command("structure export ../invalid").contains("export name"), "Reject export paths")
        check(game.execute_command("structure export test_structure").begins_with("Exported 9 blocks"),
              "Export contains voxel fills and construction pieces")
        var exported = JSON.parse_string(FileAccess.get_file_as_string("user://structures/test_structure.json"))
        check(exported is Dictionary and exported.get("version") == 1 and exported.get("size") == [4.0, 2.0, 2.0],
              "Export JSON has a version and relative dimensions")
        if exported is Dictionary:
            var pieces: Array = exported["blocks"]
            check(pieces[0]["block"] == "altaria:planks" and pieces[0]["position"] == [0.0, 0.0, 0.0],
                  "Voxel uses a stable key and relative position")
            check(pieces[-1]["position"] == [2.125, 0.0, 1.0] and pieces[-1]["shape"] == "slab" and
                  pieces[-1]["rotation"] == 1, "Export retains fractional offsets and shape rotation")
        game.execute_command("structure pos2 2 63 -3")
        check(game.execute_command("structure fill air").contains("crosses") and game.get_voxel(0, 62, -4) != 0,
              "Boundary check leaves all blocks intact")
        game.execute_command("structure pos2 3 63 -3")
        check(game.execute_command("structure fill air").contains("16 cells") and game.get_voxel(0, 62, -4) == 0,
              "Air clears both construction and grid blocks")
        game.execute_command("structure clear")
        check(not outline.visible, "Clearing selection hides the outline")
        check(game.place_block(Vector3(5, 62, -4), 1, "slab", 0) > 0, "Place pillar support")
        game.execute_command("block altaria:stone")
        game.execute_command("shape pillar")
        aim(Vector3(5.8, 65, -1), Vector3(5.8, 62.5, -3.5))
        await create_timer(.05).timeout
        var preview: MeshInstance3D = game.find_child("PlacementPreview", true, false)
        check(preview.visible and preview.position.is_equal_approx(Vector3(5, 62.5, -4)),
              "Preview shows the centered fractional attachment")
        var prebuild_count: int = game.get_built_blocks().size()
        click(MOUSE_BUTTON_RIGHT)
        await process_frame
        var pulses := game.find_children("PlacementPulse*", "MeshInstance3D", true, false)
        var pulse: MeshInstance3D = pulses.back() if not pulses.is_empty() else null
        check(pulse != null and pulse.scale.y < .98 and pulse.scale.x > 1.0 and
              pulse.material_overlay.albedo_color.r == 1.0 and pulse.material_overlay.albedo_color.a > .5,
              "Placement starts squished with a white flash")
        check(game.get_built_blocks().size() == prebuild_count + 1 and
              pulse != null, "Placement creates the block and a brief pulse")
        check(game.get_active_hotbar_slot().count == 63, "Disabling Cheat Inventory restores block consumption")
        check(game.get_built_blocks().any(func(piece: Dictionary) -> bool:
            return piece.shape == "pillar" and piece.position.is_equal_approx(Vector3(5, 62.5, -4))),
              "Pillar centers on a slab even when aiming off-center")
        game.execute_command("shape stairs")
        aim(Vector3(10.5, 65.5, 5.5), Vector3(18.5, 65.5, 5.5))
        await process_frame
        click(MOUSE_BUTTON_RIGHT)
        await process_frame
        aim(Vector3(10.5, 65.5, 5.5), Vector3(10.5, 65.5, -2.5))
        await process_frame
        click(MOUSE_BUTTON_RIGHT)
        await process_frame
        check(game.get_built_blocks().any(func(piece: Dictionary) -> bool:
            return piece.shape == "stairs" and piece.rotation == 0), "Stairs face east when looking east")
        check(game.get_built_blocks().any(func(piece: Dictionary) -> bool:
            return piece.shape == "stairs" and piece.rotation == 3), "Stairs face north when looking north")
        check(game.place_block(Vector3(20, 66, 0), 1, "cube", 0) > 0, "Place stair underside support")
        aim(Vector3(20.5, 63, 2), Vector3(20.5, 66, .5))
        await create_timer(.05).timeout
        check(preview.visible and preview.position.is_equal_approx(Vector3(20, 65, 0)),
              "Preview anchors stairs below an underside")
        click(MOUSE_BUTTON_RIGHT)
        await process_frame
        check(game.get_built_blocks().any(func(piece: Dictionary) -> bool:
            return (piece.shape == "stairs" and piece.position.is_equal_approx(Vector3(20, 65, 0)) and
                   piece.shape_code >= 58 and piece.shape_code < 62)), "Underside stairs are upside down")
        check(game.place_block(Vector3(22, 66, 0), 1, "cube", 0) > 0, "Place ramp underside support")
        game.execute_command("shape ramp")
        aim(Vector3(22.5, 63, 2), Vector3(22.5, 66, .5))
        await create_timer(.05).timeout
        check(preview.visible and preview.position.is_equal_approx(Vector3(22, 65, 0)),
              "Preview anchors the inverted ramp below the support")
        click(MOUSE_BUTTON_RIGHT)
        await process_frame
        check(game.get_built_blocks().any(func(piece: Dictionary) -> bool:
            return (piece.shape == "ramp" and piece.position.is_equal_approx(Vector3(22, 65, 0)) and
                    piece.shape_code >= 78 and piece.shape_code < 82)), "Underside ramp is upside down")
        check(game.place_block(Vector3(25, 62, 0), 1, "cube", 0) > 0, "Place wall support")
        game.execute_command("shape vertical_slab")
        aim(Vector3(25.5, 66, 1.2), Vector3(25.5, 63, .2))
        await create_timer(.05).timeout
        var first_wall_position := preview.position
        check(preview.visible, "Wall previews on the near half of the support")
        click(MOUSE_BUTTON_RIGHT)
        await process_frame
        aim(Vector3(25.5, 66, 1.8), Vector3(25.5, 63, .8))
        await create_timer(.05).timeout
        check(preview.visible and preview.position.y == first_wall_position.y and
              is_equal_approx(preview.position.z - first_wall_position.z, .5),
              "Wall previews another attachment on the same support")
        var second_wall_position := preview.position
        click(MOUSE_BUTTON_RIGHT)
        await process_frame
        check(game.get_built_blocks().any(func(piece: Dictionary) -> bool:
            return piece.shape == "vertical_slab" and piece.position.is_equal_approx(second_wall_position)),
              "The second wall occupies the previewed half")
        check(game.set_voxel(2, 65, 2, 1), "Structure accepts voxel edits")
        for frame in range(8):
            await process_frame
        check(game.get_voxel(2, 65, 2) == 1 and game.get_voxel(2, -704, 2) == 0 and
              game.get_debug_state().tiles == 1, "Only the occupied edited tile receives a mesh")
        check(game.set_voxel(2, 65, 2, 0), "Remove the voxel edit")
        for frame in range(3):
            await process_frame
        check(game.get_debug_state().tiles == 0, "Cleared tiles release their mesh")
        check(game.execute_command("world new 1337").contains("(default)"), "Seed-only command opens the normal world")
        check(game.execute_command("world new").contains("(default)") and
              game.execute_command("world info").contains("Preset: default"),
              "Bare world new picks the default preset and a seed")

    print("CONSOLE_RUNTIME_PASS" if failures.is_empty() else "CONSOLE_RUNTIME_FAIL " + str(failures))
    if failures.is_empty():
        game.execute_command("quit")
    else:
        quit(1)
