extends SceneTree
var game: Node
var actor: Node3D
var camera: Camera3D
var failed:=false
func _initialize() -> void:
    run.call_deferred()
func ready_terrain() -> void:
    for i in range(1600):
        await create_timer(.02).timeout
        var state: Dictionary=game.get_debug_state()
        if state.pending==0 and state.tiles==state.wanted:return
    failed=true
    push_error("Visual streaming timeout")
func capture(name: String) -> void:
    await RenderingServer.frame_post_draw
    DirAccess.make_dir_recursive_absolute("/tmp/altaria-environment-preview")
    if root.get_texture().get_image().save_png("/tmp/altaria-environment-preview/"+name+".png")!=OK:failed=true
func ground(x: int,z: int) -> float:
    for y in range(300,-63,-1):
        if game.get_voxel(x,y,z) not in [0,5,9,10]:
            return y+(.5 if game.get_voxel_appearance(x,y,z).shape=="slab" else 1)
    return 43
func run() -> void:
    game=load("res://scenes/main.tscn").instantiate()
    root.add_child(game)
    await process_frame
    game.start_world("Atmosphere visual",1337)
    game.execute_command("time pause")
    game.execute_command("time 9")
    actor=game.find_children("*","FlyingPlayer",true,false)[0]
    camera=game.find_children("*","Camera3D",true,false)[0]
    actor.set_physics_process(false)
    actor.set_process_unhandled_input(false)
    Input.mouse_mode=Input.MOUSE_MODE_VISIBLE
    for control in game.find_children("*","Control",true,false):
        if control.get_parent() is CanvasLayer and control.get_parent().layer==20:control.hide()
    await ready_terrain()
    await create_timer(.7).timeout
    await capture("day_horizon")
    game.execute_command("biome normal")
    var origin:=actor.position
    var g:=ground(floori(origin.x),floori(origin.z))
    actor.position=Vector3(origin.x+.5,g+.2,origin.z+.5)
    actor.rotation.y=0
    camera.rotation.x=-.16
    await ready_terrain()
    await create_timer(.6).timeout
    await capture("grass_wind_a")
    await create_timer(.85).timeout
    await capture("grass_wind_b")
    var grass_view:=actor.transform
    var grass_camera:=camera.rotation
    game.execute_command("time 19")
    await create_timer(.3).timeout
    await capture("sunset")
    game.execute_command("time 2")
    await create_timer(.3).timeout
    await capture("night_ground")
    game.execute_command("light #9b55ff 5 12")
    camera.rotation.y=.5
    game.execute_command("light #ff9c55 5 9")
    camera.rotation=grass_camera
    await create_timer(.3).timeout
    await capture("colored_lights")
    var moon_direction:=Vector3(-.5,.866,.35).normalized()
    camera.look_at(camera.global_position+moon_direction*100,Vector3.UP)
    await create_timer(.3).timeout
    await capture("moon_stars")
    camera.rotation=grass_camera
    game.execute_command("time 10")
    # Same material, eight forms with grass overlays and paint.
    var px:=floori(origin.x)
    var pz:=floori(origin.z)
    var py:=ceili(g)+8
    var names=["cube","slab","vertical_slab","ramp","stairs","roof","pillar","mini_pillar"]
    for z in range(pz-5,pz+3):
        for x in range(px-2,px+16):game.set_voxel(x,py-1,z,1)
    for i in range(names.size()):
        game.set_voxel(px+i*2,py,pz-2,3)
        game.shape_voxel(px+i*2,py,pz-2,names[i],0)
        if i>3:game.paint_voxel(px+i*2,py,pz-2,"#b362df",.55,1.3)
    actor.position=Vector3(px+7,py+4,pz+8)
    actor.rotation.y=0
    camera.rotation=Vector3(-.38,0,0)
    await ready_terrain()
    await create_timer(.5).timeout
    await capture("block_shapes")
    # Deterministic tree fixture, real Jolt fall and a screenshot of the result.
    var tx:=px+7
    var tz:=pz-1
    for y in range(py+1,py+8):game.set_voxel(tx,y,tz,9)
    for y in range(py+5,py+9):
        for dz in range(-2,3):
            for dx in range(-2,3):
                if dx!=0 or dz!=0:game.set_voxel(tx+dx,y,tz+dz,10)
    actor.position=Vector3(tx+11,py+6,tz+13)
    camera.look_at(Vector3(tx,py+4,tz),Vector3.UP)
    await ready_terrain()
    await create_timer(.4).timeout
    await capture("tree_before")
    game.release_voxel(tx,py+2,tz,true)
    await ready_terrain()
    await create_timer(5).timeout
    await capture("tree_fallen")
    print("ENVIRONMENT_VISUAL_STATS ",game.get_debug_state())
    print("ENVIRONMENT_VISUAL_PASS" if not failed else "ENVIRONMENT_VISUAL_FAIL")
    quit(1 if failed else 0)
