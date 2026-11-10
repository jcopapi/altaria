extends "res://tests/cartoon_visual.gd"
func capture_scene(label: String) -> Image:
    hide_ui()
    game.find_child("BlockSelection",true,false).hide()
    game.set_process(false)
    var result:=await capture(label)
    game.set_process(true)
    return result
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
    game.start_world("Voxel Tools Terrain appearance",1337)
    actor=game.find_children("*","FlyingPlayer",true,false)[0]
    camera=actor.find_children("*","Camera3D",true,false)[0]
    actor.set_physics_process(false)
    actor.set_process_unhandled_input(false)
    root.grab_focus()
    Input.mouse_mode=Input.MOUSE_MODE_VISIBLE
    game.execute_command("time pause")
    game.execute_command("time 9")
    game.execute_command("wind 0")
    game.execute_command("godrays off")
    game.execute_command("fog #86a8d8 4000 5700")
    actor.position=Vector3(268,161,139)
    await settle()
    var ids: Array[int]=[]
    for x in range(256,264):
        for z in range(126,132):check(game.place_block(Vector3(x,148,z),1,"cube",0)>0,"Ordinary stone foundation")
    for x in range(4):
        for y in range(x+1):
            for z in range(127,130):
                var id: int=game.place_block(Vector3(256+x,149+y,z),3,"cube",0)
                check(id>0,"Build grass staircase")
                ids.append(id)
    for z in range(127,130):
        for y in range(149,153):
            var id: int=game.place_block(Vector3(262,y,z),3 if y==152 else 1,"cube",0)
            check(id>0,"Mixed material cliff and grass cap")
            ids.append(id)
    aim(Vector3(266,157,139),Vector3(259,150,128))
    await settle()
    await capture_scene("terrain_gameplay_cubes")
    for id in ids:check(game.terrain_built(id,1),"Apply actual Terrain modifier")
    await settle()
    await capture_scene("terrain_gameplay_smooth_day")
    var grass:=0
    var stone:=0
    var transitions:=0
    for node in game.find_children("TerrainTransvoxelPatch*","MeshInstance3D",true,false):
        if node.mesh.get_surface_count()==0:continue
        var arrays: Array=node.mesh.surface_get_arrays(0)
        var uv: PackedVector2Array=arrays[Mesh.ARRAY_TEX_UV2]
        var indices: PackedInt32Array=arrays[Mesh.ARRAY_INDEX]
        for p in uv:
            if absf(p.x-3)<.01:grass+=1
            if absf(p.x-7)<.01:stone+=1
            if absf(p.y-2)<.01:transitions+=1
        for i in range(0,indices.size(),3):
            check(uv[indices[i]].x==uv[indices[i+1]].x and uv[indices[i]].x==uv[indices[i+2]].x,"Material identity stays constant inside each smooth triangle")
    print("TERRAIN_TEXTURES grass=",grass," stone=",stone," transitions=",transitions)
    check(grass>30 and stone>30,"New grass and stone textures reach the actual smooth meshes")
    check(transitions>12,"Grass side transition reaches the neighboring smooth stone")
    check(game.get_debug_state().grass_instances==0 and game.get_debug_state().bushes==0,"Removed vegetation remains absent")
    game.execute_command("shape terrain")
    for i in range(300):
        await create_timer(.01).timeout
        if game.get_debug_state().terrain_preview_ready and game.get_debug_state().terrain_preview_pending==0:break
    await process_frame
    await process_frame
    hide_ui()
    await capture("terrain_gameplay_preview")
    game.execute_command("shape cube")
    game.execute_command("time 22")
    await create_timer(.4).timeout
    await capture_scene("terrain_gameplay_smooth_night")
    for id in ids:check(game.terrain_built(id,0),"Restore source cubes")
    await settle()
    game.execute_command("time 9")
    await create_timer(.2).timeout
    await capture_scene("terrain_gameplay_restored")
    # The natural world uses the identical modifier/texture path.
    actor.position=Vector3(0,225,0)
    await settle()
    var natural_top:=60
    for y in range(200,-60,-1):
        var block: int=game.get_voxel(0,y,0)
        if block in [1,2,3,4,6,8]:natural_top=y;break
    aim(Vector3(8,natural_top+7,12),Vector3(0,natural_top-1,0))
    await settle()
    check(game.get_debug_state().terrain_resident_sources>20,"Natural world presents the same Terrain geometry")
    await capture_scene("terrain_procedural_surface")
    print("TERRAIN_GAMEPLAY_VISUAL_PASS" if failures.is_empty() else "TERRAIN_GAMEPLAY_VISUAL_FAIL "+str(failures))
    quit(0 if failures.is_empty() else 1)
