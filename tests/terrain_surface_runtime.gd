extends "res://tests/cartoon_runtime.gd"
func run() -> void:
    game=load("res://scenes/main.tscn").instantiate()
    root.add_child(game)
    await process_frame
    check(game.start_world("Surface cap Terrain",1337),"Start cap world")
    find_player()
    actor.position=Vector3(0,235,5)
    game.execute_command("time pause")
    await settle()
    # A cap owns one cell and needs a normal block directly underneath.
    check(game.set_voxel(10,220,0,3),"Unsupported source")
    check(game.execute_command("terrain at 10 220 0 on")=="Invalid supporting block.","Unsupported Terrain is rejected")
    for x in [0,3,12]:
        check(game.set_voxel(x,219,0,2) and game.set_voxel(x,220,0,3),"Normal base and cap cell")
    check(game.set_voxel(12,221,0,1),"Cover buried source")
    check(game.execute_command("terrain at 12 220 0 on")=="Invalid supporting block.","Buried cells cannot become Terrain")
    for x in [0,3]:check(game.execute_command("terrain at %d 220 0 on"%x)=="Terrain modifier updated.","Apply surface cap")
    await settle()
    await physics_frame
    var hit: Dictionary=game.terrain_hit(Vector3(.25,224,.5),Vector3.DOWN,8)
    check(not hit.is_empty(),"Cap has a selectable curved surface")
    if not hit.is_empty():
        check(hit.position.y>=220 and hit.position.y<=221.001 and hit.has_base and hit.base_position==Vector3(0,219,0),"Cap stays within one block above its normal base")
        var jolt: Dictionary=camera.get_world_3d().direct_space_state.intersect_ray(PhysicsRayQueryParameters3D.create(Vector3(.25,224,.5),Vector3(.25,218,.5),16))
        check(not jolt.is_empty() and jolt.position.distance_to(hit.position)<.002,"Cap collision matches its visible surface")
    check(not game.get_voxel_appearance(0,219,0).terrain,"Supporting block stays normal")
    # Actual construction punches through the cap and fills the source cell.
    game.execute_command("block altaria:stone")
    game.execute_command("shape cube")
    aim(Vector3(3.25,224,2),Vector3(3.25,220.5,.5))
    await create_timer(.1).timeout
    click(MOUSE_BUTTON_RIGHT)
    await settle()
    check(game.get_voxel(3,220,0)==1 and not game.get_voxel_appearance(3,220,0).terrain,"Construction replaces the Terrain cell with an ordinary block")
    check(game.get_voxel(3,219,0)==2,"Construction preserves the base underneath")
    check(game.get_voxel(3,221,0)==0,"Construction does not float on the curved height")
    # Losing the support restores ordinary selectable geometry.
    check(game.set_voxel(15,219,0,2) and game.set_voxel(15,220,0,3),"Support removal fixture")
    check(game.execute_command("terrain at 15 220 0 on")=="Terrain modifier updated.","Apply supported cap before removing its base")
    await settle()
    check(game.set_voxel(15,219,0,0),"Remove cap support")
    await settle()
    aim(Vector3(15.5,224,2),Vector3(15.5,220.8,.5))
    await create_timer(.1).timeout
    var selection: MeshInstance3D=game.find_child("BlockSelection",true,false)
    check(game.get_debug_state().selection_visible and selection.mesh!=null and not selection.mesh.get_faces().is_empty(),"Unsupported cap falls back to ordinary selectable geometry")
    # The same behavior on independently placed/fractional construction pieces.
    var base: int=game.place_block(Vector3(5,219.5,0),1,"slab",0)
    var cap: int=game.place_block(Vector3(5,220,0),3,"cube",0)
    check(base>0 and cap>0 and game.terrain_built(cap,1),"Terrain on a normal fractional base")
    await settle()
    aim(Vector3(5.25,224,2),Vector3(5.25,220.5,.5))
    await create_timer(.1).timeout
    click(MOUSE_BUTTON_RIGHT)
    await settle()
    check(block_by_id(cap).position==Vector3(5,220,0) and block_by_id(cap).block==1 and not block_by_id(cap).terrain,"Built replacement retains its source ID and fills from the base plane")
    check(block_by_id(base).position==Vector3(5,219.5,0) and not block_by_id(base).terrain,"Fractional supporting slab stays normal")
    # Breaking the cap frees the whole cell; no newly exposed base is smoothed.
    check(game.set_voxel(0,220,0,0),"Mine a cap")
    await settle()
    check(game.get_voxel(0,220,0)==0 and game.get_voxel(0,219,0)==2,"Mining frees a complete block cell")
    check(not game.get_voxel_appearance(0,219,0).terrain and game.terrain_hit(Vector3(.5,224,.5),Vector3.DOWN,8).is_empty(),"Exposed base remains outside Terrain computation")
    check(game.save_world(),"Save cap removal and construction")
    var path: String=game.get_world_path()
    check(game.open_world(path),"Reload caps")
    find_player()
    await settle()
    check(game.get_voxel(0,220,0)==0 and game.get_voxel(3,220,0)==1 and not game.get_voxel_appearance(0,219,0).terrain,"Cap removal and replacement persist")
    check(block_by_id(cap).block==1 and not block_by_id(cap).terrain,"Built replacement persists")
    # Shared corner lattice joins an actual one-block terrain step.
    for x in [31,32]:
        for z in range(3):
            for y in range(218,220+(x-31)):check(game.set_voxel(x,y,20+z,2),"Stepped ordinary support")
            var y: int=220+(x-31)
            check(game.set_voxel(x,y,20+z,3),"Step surface source")
            check(game.execute_command("terrain at %d %d %d on"%[x,y,20+z])=="Terrain modifier updated.","Only step top receives Terrain")
    actor.position=Vector3(31,228,24)
    await settle()
    var a: Dictionary=game.terrain_hit(Vector3(31.999,225,21.5),Vector3.DOWN,10)
    var b: Dictionary=game.terrain_hit(Vector3(32.001,225,21.5),Vector3.DOWN,10)
    check(not a.is_empty() and not b.is_empty() and absf(a.position.y-b.position.y)<.01,"Surface bridge joins across a chunk boundary")
    game.execute_command("fly off")
    actor.position=Vector3(31.6,225,21.5)
    actor.set_physics_process(true)
    await create_timer(1).timeout
    check(game.get_debug_state().grounded,"Player stands on surface cap")
    actor.rotation=Vector3(0,-PI/2,0)
    camera.rotation=Vector3.ZERO
    var start:=actor.position
    var event:=InputEventKey.new()
    event.keycode=KEY_W;event.physical_keycode=KEY_W;event.pressed=true
    Input.parse_input_event(event)
    await create_timer(.22).timeout
    event=InputEventKey.new();event.keycode=KEY_W;event.physical_keycode=KEY_W;event.pressed=false
    Input.parse_input_event(event)
    await create_timer(.1).timeout
    print("CAP_WALK ",start," -> ",actor.position)
    check(actor.position.x>start.x+.6 and actor.position.y>start.y+.5,"Player walks across the curved bridge")
    actor.set_physics_process(false)
    game.execute_command("fly on")
    # Procedural original surface: mining cannot recursively create more caps.
    actor.position=Vector3(100,225,0)
    await settle()
    var top: int=0
    for y in range(200,-60,-1):
        if game.get_voxel(100,y,0) in [1,2,3,4,6,8]:top=y;break
    actor.position=Vector3(100,top+6,0)
    await settle()
    var natural: Dictionary=game.terrain_hit(Vector3(100.5,top+6,.5),Vector3.DOWN,12)
    check(not natural.is_empty(),"Procedural world uses surface caps")
    if not natural.is_empty():
        var p: Vector3i=natural.cell
        var before: int=game.get_voxel(p.x,p.y-1,p.z)
        check(not game.get_voxel_appearance(p.x,p.y-1,p.z).terrain,"Procedural base is ordinary before mining")
        check(game.set_voxel(p.x,p.y,p.z,0),"Mine procedural cap")
        await settle()
        check(game.get_voxel(p.x,p.y,p.z)==0 and game.get_voxel(p.x,p.y-1,p.z)==before and not game.get_voxel_appearance(p.x,p.y-1,p.z).terrain,"Procedural mining exposes unchanged ordinary volume")
    print("TERRAIN_SURFACE_RUNTIME_PASS" if failures.is_empty() else "TERRAIN_SURFACE_RUNTIME_FAIL "+str(failures))
    quit(0 if failures.is_empty() else 1)
