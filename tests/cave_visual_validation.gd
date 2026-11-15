extends "res://tests/cartoon_runtime.gd"
func run() -> void:
    game=load("res://scenes/main.tscn").instantiate()
    root.add_child(game)
    await process_frame
    check(game.start_world("Cave visual validation",1337),"Start cave visual world")
    find_player()
    for node in actor.find_children("*","Node",true,false):
        node.set_physics_process(false)
        node.set_process(false)
        node.set_process_unhandled_input(false)
    Input.mouse_mode=Input.MOUSE_MODE_VISIBLE
    actor.position=Vector3(-112,120,83)
    await settle()
    var features: Array=game.get_cave_formations()
    check(features.size()>0,"Formation catalog is available in a resident region")
    # A temporary inspection lamp is test-only. It makes concave geometry
    # visible without changing product lighting or timed benchmark samples.
    var lamp:=SpotLight3D.new()
    lamp.name="TestInspectionLamp"
    lamp.spot_range=110
    lamp.spot_angle=75
    lamp.light_energy=2.2
    camera.add_child(lamp)
    var output:=OS.get_environment("ALTARIA_CAVE_PREVIEW")
    DirAccess.make_dir_recursive_absolute(output)
    var captured: Dictionary={}
    for feature in features:
        if captured.size()>=4:break
        if feature.kind=="ice_sheet" or captured.has(feature.kind):continue
        check(game.execute_command("cave feature "+str(feature.index)).begins_with("Inspecting"),"Formation inspection command")
        await settle()
        var current: Dictionary={}
        for candidate in game.get_cave_formations():
            if candidate.id==feature.id:current=candidate;break
        if current.is_empty() or not current.get("support_ready",false) or current.get("supported_voxels",0)<10:continue
        var center: Vector3=game.get_debug_state().player_position
        var target: Vector3=feature.base+Vector3(0,feature.vertical*feature.height*.45,0)
        camera.global_position=center+Vector3(0,1,0)
        camera.look_at(target,Vector3.UP)
        game.execute_command("fullbright off")
        await create_timer(.2).timeout
        await RenderingServer.frame_post_draw
        var filename: String=feature.kind+".png"
        check(root.get_texture().get_image().save_png(output.path_join(filename))==OK,"Capture "+feature.kind)
        print("CAVE_VISUAL_CAPTURE kind=",feature.kind," rule=",feature.rule," supported=",current.supported_voxels," file=",output.path_join(filename))
        captured[feature.kind]=true
    check(captured.size()>=2,"Render more than one geological profile")
    root.remove_child(game)
    game.queue_free()
    for i in range(4):await process_frame
    print("CAVE_VISUAL_PASS" if failures.is_empty() else "CAVE_VISUAL_FAIL "+str(failures))
    quit(0 if failures.is_empty() else 1)
