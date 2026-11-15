extends "res://tests/cartoon_runtime.gd"

func run() -> void:
    game=load("res://scenes/main.tscn").instantiate()
    root.add_child(game)
    await process_frame
    check(game.start_world("Cave hall visual check",1337),"Start world")
    find_player()
    Input.mouse_mode=Input.MOUSE_MODE_VISIBLE
    game.execute_command("time pause")
    var lamp := OmniLight3D.new()
    lamp.omni_range=85
    lamp.omni_attenuation=.30
    lamp.light_energy=4.0
    lamp.light_color=Color(1,.94,.82)
    camera.add_child(lamp)
    var found:=false
    for region in [Vector2i.ZERO,Vector2i(3,3),Vector2i(3,-3),Vector2i(-3,3),Vector2i(-3,-3),Vector2i(6,3)]:
        actor.position=Vector3(region.x*256+128,200,region.y*256+128)
        await create_timer(.25).timeout
        await settle()
        var state: Dictionary=game.get_debug_state()
        for hall in state.cave_chambers:
            if hall.depth!=2 or hall.archetype!="giant":continue
            var center: Vector3=hall.center
            var eye: Vector3=center+Vector3(-hall.radius.x*.35,1,-hall.radius.z*.26)
            aim(eye,center+Vector3(10,-7,8))
            await create_timer(.25).timeout
            await settle()
            var p:=Vector3i(eye.floor())
            var sample:=int(game.get_voxel(p.x,p.y,p.z))
            var env: Dictionary=game.get_cave_environment(eye)
            print("CAVE_VISUAL_PROBE ",p," voxel=",sample," fluid=",env.get("fluid",""),
                " basin=",env.get("basin_source","")," loaded=",env.get("loaded",false))
            if sample!=0 or not env.get("loaded",false):continue
            await create_timer(.3).timeout
            await RenderingServer.frame_post_draw
            var path:=OS.get_environment("ALTARIA_REFORM_OUTPUT").path_join("dry-hall.png")
            check(root.get_texture().get_image().save_png(path)==OK,"Capture dry hall")
            found=true
            break
        if found:break
    check(found,"Find dry giant hall for visual inspection")
    root.remove_child(game)
    game.queue_free()
    for i in range(4):await process_frame
    print("DRY_HALL_PASS" if failures.is_empty() else "DRY_HALL_FAIL")
    quit(0 if failures.is_empty() else 1)
