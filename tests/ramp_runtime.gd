extends "res://tests/cartoon_runtime.gd"
func run() -> void:
    game=load("res://scenes/main.tscn").instantiate()
    root.add_child(game)
    await process_frame
    check(game.start_world("Connected ramp regression",1337),"Start ramp world")
    find_player()
    game.execute_command("time pause")
    game.execute_command("tp 0 225 3")
    await settle()
    check(game.execute_command("shape terrain")=="Selected shape: terrain","Terrain available in shape selection")
    # General 3D Terrain now has its own full acceptance test. Here verify that
    # its independent state coexists with the original manual ramp connections.
    check(game.set_voxel(20,220,0,1) and game.paint_voxel(20,220,0,"#44aa88",.4,1.1),"Painted original cube")
    check(game.execute_command("terrain at 20 220 0 on")=="Terrain modifier updated.","Enable independent Terrain modifier")
    await settle()
    var original: Dictionary=game.get_voxel_appearance(20,220,0)
    check(original.terrain and original.painted and game.get_voxel(20,220,0)==1,"Terrain preserves original material and paint")
    check(not game.terrain_hit(Vector3(20.5,224,.5),Vector3.DOWN,8).is_empty(),"Terrain uses the integrated Transvoxel surface")
    for x in [0,5]:
        check(game.set_voxel(x,220,0,3),"Floor material fixture")
        check(game.shape_voxel(x,220,0,"slab",0),"Half-height floor fixture")
    game.execute_command("block altaria:stone")
    game.execute_command("shape ramp")
    aim(Vector3(.5,224,3),Vector3(.5,220.5,.5))
    await create_timer(.1).timeout
    click(MOUSE_BUTTON_RIGHT)
    await create_timer(.1).timeout
    var half: Dictionary={}
    for b in game.get_built_blocks():
        if b.position.is_equal_approx(Vector3(0,220.5,0)):half=b
    check(not half.is_empty(),"RMB ramp rests directly on floor")
    if not half.is_empty():check(half.ramp_rise==.5 and half.ramp_low==0,"Ramp clicked on floor rises only half a block")
    check(game.place_block(Vector3(1,220,0),1,"cube",0)>0,"Normal block touches high end of slab-top ramp")
    aim(Vector3(8,220.25,.5),Vector3(5.5,220.25,.5))
    await create_timer(.1).timeout
    click(MOUSE_BUTTON_RIGHT)
    await create_timer(.1).timeout
    var raised: Dictionary={}
    for b in game.get_built_blocks():
        if b.position.is_equal_approx(Vector3(6,220,0)):raised=b
    check(not raised.is_empty(),"RMB side of floor places adjacent ramp at correct base")
    if not raised.is_empty():check(raised.ramp_low==.5 and raised.ramp_rise==.5,"Side ramp joins floor height to ordinary full-block height")
    actor.position=Vector3(8,224,3)
    check(game.place_block(Vector3(7,220,0),1,"cube",0)>0,"Full cube joins raised ramp")
    actor.position=Vector3(6.5,223,.5)
    game.execute_command("fly off")
    actor.set_physics_process(true)
    await create_timer(1).timeout
    check(game.get_debug_state().grounded and absf(actor.position.y-220.9)<.025,"Player lands on exact raised-ramp slope")
    actor.set_physics_process(false)
    game.execute_command("fly on")
    actor.position=Vector3(0,235,4)
    var outer: int=game.place_block(Vector3(0,230,0),3,"ramp",0)
    var front: int=game.place_block(Vector3(1,230,0),3,"ramp",1)
    check(outer>0 and front>0 and block_by_id(outer).ramp_corner=="outer_right","Perpendicular front ramp produces convex corner")
    aim(Vector3(.2,233,3),Vector3(.2,230.2,.6))
    await create_timer(.1).timeout
    check(game.get_debug_state().selection_built_id==outer,"Corner ramp has exact ray selection")
    check(game.remove_built_block(front),"Remove corner neighbor")
    check(block_by_id(outer).ramp_corner=="straight","Neighbor removal restores straight ramp")
    var inner: int=game.place_block(Vector3(4,230,0),3,"ramp",0)
    var back: int=game.place_block(Vector3(3,230,0),3,"ramp",1)
    check(inner>0 and back>0 and block_by_id(inner).ramp_corner=="inner_right","Perpendicular rear ramp produces concave corner")
    check(game.set_voxel(10,230,10,3) and game.shape_voxel(10,230,10,"ramp",0),"Terrain-cell ramp fixture")
    var terrain_front: int=game.place_block(Vector3(11,230,10),3,"ramp",1)
    check(terrain_front>0 and game.get_voxel_appearance(10,230,10).ramp_corner=="outer_right","Construction updates the adjacent world ramp too")
    game.save_world()
    var path: String=game.get_world_path()
    check(game.open_world(path),"Reload connected ramps")
    find_player()
    check(game.get_voxel_appearance(20,220,0).terrain and game.get_voxel_appearance(20,220,0).painted,"Independent Terrain state and paint persist beside manual ramps")
    check(block_by_id(inner).ramp_corner=="inner_right","Corner re-derived after journal reload")
    check(game.get_voxel_appearance(10,230,10).ramp_corner=="outer_right","World/construction connection re-derived after reload")
    check(game.remove_built_block(terrain_front) and game.get_voxel_appearance(10,230,10).ramp_corner=="straight","World ramp returns to straight after construction neighbor removal")
    if not half.is_empty():check(block_by_id(half.id).ramp_rise==.5,"Half ramp persists")
    if not raised.is_empty():check(block_by_id(raised.id).ramp_low==.5,"Raised ramp persists")
    aim(Vector3(4.8,233,3),Vector3(4.8,230.8,.2))
    await create_timer(.1).timeout
    check(game.execute_command("physics")=="Released into Jolt physics.","Corner ramp converts using connected geometry")
    await create_timer(.05).timeout
    check(game.get_debug_state().physics_parts==1 and game.get_debug_state().selection_physical,"Physical corner keeps selection")
    game.save_world()
    check(game.open_world(path),"Reload physical corner")
    check(game.get_debug_state().physics_parts==1,"Physical ramp corner persists")
    # Both retired procedural surface modes migrate to the original slab caps.
    var legacy_path: String=game.get_world_path()
    for flag in ["surface_ramps","terrain_smoothing"]:
        check(game.start_world("Migration staging",17),"Leave world before editing isolated test metadata")
        var meta: Dictionary=JSON.parse_string(FileAccess.get_file_as_string(legacy_path+"/world.json"))
        meta.generation[flag]=true
        meta.generation.surface_shapes=true
        var file:=FileAccess.open(legacy_path+"/world.json",FileAccess.WRITE)
        file.store_string(JSON.stringify(meta))
        file.close()
        check(game.open_world(legacy_path),"Open legacy "+flag+" generation snapshot")
        find_player()
        actor.position=Vector3(256,120,128)
        await settle()
        var slabs:=0
        var slopes:=0
        for z in range(120,145,2):
            for x in range(240,273,2):
                for y in range(140,42,-1):
                    if game.get_voxel(x,y,z)!=3:continue
                    var a: Dictionary=game.get_voxel_appearance(x,y,z)
                    if a.shape=="slab":slabs+=1
                    if a.shape=="ramp":slopes+=1
                    break
        check(slabs>20 and slopes==0,"Legacy world regenerates with slabs and no procedural ramps")
        check(game.get_voxel_appearance(10,230,10).shape=="ramp","Authored block edits survive generator rollback")
        check(game.save_world(),"Save migrated surface mode")
        var migrated: Dictionary=JSON.parse_string(FileAccess.get_file_as_string(legacy_path+"/world.json"))
        check(not migrated.generation.has("surface_ramps") and not migrated.generation.has("terrain_smoothing"),"Retired generator flags removed from saved metadata")
    print("RAMP_RUNTIME_PASS" if failures.is_empty() else "RAMP_RUNTIME_FAIL "+str(failures))
    quit(0 if failures.is_empty() else 1)
