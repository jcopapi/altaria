extends "res://tests/cartoon_visual.gd"
func run() -> void:
    root.size=Vector2i(1280,720)
    root.position=Vector2i(60,60)
    root.always_on_top=true
    root.grab_focus()
    await process_frame
    await create_timer(.2).timeout
    game=load("res://scenes/main.tscn").instantiate()
    root.add_child(game)
    await process_frame
    game.start_world("Slab world and manual ramp validation",1337)
    actor=game.find_children("*","FlyingPlayer",true,false)[0]
    camera=actor.find_children("*","Camera3D",true,false)[0]
    actor.set_physics_process(false)
    actor.set_process_unhandled_input(false)
    Input.mouse_mode=Input.MOUSE_MODE_VISIBLE
    root.grab_focus()
    hide_ui()
    game.execute_command("time pause")
    game.execute_command("time 9")
    game.execute_command("wind 0")
    game.execute_command("godrays off")
    aim(Vector3(256,110,140),Vector3(290,79,30))
    await settle()
    await capture("slab_world")
    # Slab -> adjacent raised ramp -> ordinary cube, using actual RMB placement.
    game.set_voxel(252,150,128,3)
    game.shape_voxel(252,150,128,"slab",0)
    game.execute_command("block altaria:grass")
    game.execute_command("shape ramp")
    aim(Vector3(255,150.25,128.5),Vector3(252.5,150.25,128.5))
    await create_timer(.1).timeout
    var click:=InputEventMouseButton.new()
    click.button_index=MOUSE_BUTTON_RIGHT
    click.pressed=true
    Input.parse_input_event(click)
    await create_timer(.1).timeout
    actor.position=Vector3(260,155,137)
    check(game.place_block(Vector3(254,150,128),3,"cube",0)>0,"Normal cube closes the ramp transition")
    check(game.place_block(Vector3(256,150,128),3,"ramp",0)>0,"Outer corner source")
    check(game.place_block(Vector3(257,150,128),3,"ramp",1)>0,"Outer corner neighbor")
    check(game.place_block(Vector3(260,150,128),3,"ramp",0)>0,"Inner corner source")
    check(game.place_block(Vector3(259,150,128),3,"ramp",1)>0,"Inner corner neighbor")
    aim(Vector3(262,156,138),Vector3(256.5,150.5,128.5))
    await settle()
    game.find_child("BlockSelection",true,false).hide()
    game.set_process(false)
    await process_frame
    await capture("ramp_joins")
    game.set_process(true)
    print("RAMP_VISUAL_PASS" if failures.is_empty() else "RAMP_VISUAL_FAIL "+str(failures))
    quit(0 if failures.is_empty() else 1)
