extends "res://tests/cartoon_runtime.gd"
func key(code: Key,pressed: bool) -> void:
    var event:=InputEventKey.new()
    event.keycode=code
    event.physical_keycode=code
    event.pressed=pressed
    Input.parse_input_event(event)
func ray(origin: Vector3,mask: int) -> Dictionary:
    return camera.get_world_3d().direct_space_state.intersect_ray(PhysicsRayQueryParameters3D.create(origin,origin+Vector3.DOWN*20,mask))
func mesh_signature(mesh: Mesh,transform: Transform3D=Transform3D.IDENTITY) -> Array[String]:
    var values: Array[String]=[]
    for p in mesh.get_faces():values.append(str(Vector3i((transform*p*10000).round())))
    values.sort()
    return values
func wait_preview() -> void:
    for i in range(500):
        await create_timer(.01).timeout
        var state: Dictionary=game.get_debug_state()
        if state.get("terrain_preview_ready",false) and state.get("terrain_preview_pending",1)==0:
            await process_frame
            await process_frame
            return
    check(false,"Actual Transvoxel preview completes")
func run() -> void:
    game=load("res://scenes/main.tscn").instantiate()
    root.add_child(game)
    await process_frame
    check(game.start_world("Terrain actual gameplay",1337),"Start gameplay world")
    find_player()
    game.execute_command("time pause")
    actor.position=Vector3(0,235,5)
    await settle()
    var ids: Array[int]=[]
    for x in range(4):
        for y in range(x+1):
            for z in range(3):
                var id: int=game.place_block(Vector3(x,220+y,z),3,"cube",0)
                check(id>0,"Construct ordinary staircase cube")
                ids.append(id)
    await settle()
    aim(Vector3(-2,223,3),Vector3(.4,220.6,1.5))
    await create_timer(.1).timeout
    key(KEY_T,true)
    await process_frame
    key(KEY_T,false)
    await process_frame
    var applied:=0
    for id in ids:
        if block_by_id(id).terrain:applied+=1
    check(applied==1,"Actual T input applies modifier to selected original cube")
    game.execute_command("shape terrain")
    aim(Vector3(1.4,224,4),Vector3(1.4,221.6,1.5))
    await wait_preview()
    var preview_state: Dictionary=game.get_debug_state()
    check(preview_state.terrain_preview_cells>=2,"Preview includes the selected source and affected Terrain neighbor")
    var selection: MeshInstance3D=game.find_child("BlockSelection",true,false)
    var predicted:=mesh_signature(selection.mesh,selection.global_transform)
    applied=0
    for id in ids:
        if block_by_id(id).terrain:applied+=1
    check(applied==1,"Preview never mutates original source blocks")
    click(MOUSE_BUTTON_RIGHT)
    await settle()
    await physics_frame
    await physics_frame
    var actual: Array[String]=[]
    for node in game.find_children("TerrainTransvoxelPatch*","MeshInstance3D",true,false):actual.append_array(mesh_signature(node.mesh,node.global_transform))
    actual.sort()
    print("TERRAIN_PREVIEW_SIGNATURE predicted_vertices=",predicted.size()," actual_vertices=",actual.size())
    check(not predicted.is_empty() and predicted==actual,"RMB produces the exact Transvoxel geometry shown by the neighbor-aware preview")
    game.execute_command("shape cube")
    for id in ids:check(game.terrain_built(id,1),"Continue smoothing constructed stairs")
    await settle()
    await physics_frame
    await physics_frame
    var state: Dictionary=game.get_debug_state()
    print("TERRAIN_GAMEPLAY_STATE ",state.terrain_patches," patches ",state.terrain_triangles," triangles max_worker_ms=",state.terrain_max_build_ms)
    check(state.terrain_triangles>200,"Actual scene contains generated Transvoxel triangles")
    for id in ids:
        var b:=block_by_id(id)
        check(b.terrain and b.original_shape=="cube" and b.block==3,"Source cubes retain IDs, shapes and material")
    var origin:=Vector3(.9,225,1.5)
    var mesh_hit: Dictionary=game.terrain_hit(origin,Vector3.DOWN,20)
    var collider_hit:=ray(origin,16)
    check(not mesh_hit.is_empty() and not collider_hit.is_empty(),"Rendered surface and Jolt collider are both selectable")
    if not mesh_hit.is_empty() and not collider_hit.is_empty():
        print("TERRAIN_CONTACT mesh=",mesh_hit.position," jolt=",collider_hit.position)
        check(mesh_hit.position.distance_to(collider_hit.position)<.002,"Ray selection and Jolt use the same mesh")
        check(mesh_hit.position.y>221.1 and mesh_hit.position.y<221.8,"Staircase becomes a continuous inclined surface")
        check(mesh_hit.built_id in ids,"Smooth triangle selects an original construction piece")
    aim(Vector3(.9,224,1.6),Vector3(.9,221,1.5))
    await create_timer(.1).timeout
    check(game.get_debug_state().selection_built_id in ids,"Gameplay selection follows the smooth surface")
    game.execute_command("fly off")
    actor.position=Vector3(.9,225,1.5)
    actor.set_physics_process(true)
    await create_timer(1).timeout
    print("TERRAIN_PLAYER_CONTACT ",actor.position," grounded=",game.get_debug_state().grounded)
    check(game.get_debug_state().grounded,"Player can stand on generated terrain")
    check(actor.position.y>221.35 and actor.position.y<222.0,"Player collides with slope, without an invisible full-cube top")
    actor.set_rotation(Vector3(0,-PI/2,0))
    camera.rotation=Vector3.ZERO
    var start:=actor.position
    key(KEY_W,true)
    await create_timer(.3).timeout
    key(KEY_W,false)
    await create_timer(.1).timeout
    print("TERRAIN_PLAYER_WALK ",start," -> ",actor.position)
    check(actor.position.x>start.x+.6 and actor.position.y>start.y+.5,"Player walks up the actual smooth incline")
    actor.set_physics_process(false)
    game.execute_command("fly on")
    actor.position=Vector3(0,235,5)
    check(game.save_world(),"Save modified staircase")
    var path: String=game.get_world_path()
    check(game.open_world(path),"Reload actual smooth staircase")
    find_player()
    await settle()
    check(not game.terrain_hit(origin,Vector3.DOWN,20).is_empty(),"Smooth geometry reconstructs after save reload")
    for id in ids:check(game.terrain_built(id,0),"Remove Terrain without removing original block")
    await settle()
    await physics_frame
    await physics_frame
    check(game.get_debug_state().terrain_triangles==0,"All smooth geometry is removed")
    var original:=ray(origin,4)
    check(not original.is_empty() and absf(original.position.y-221)<.02,"Original cubic staircase and collision are recovered")
    for id in ids:
        var b:=block_by_id(id)
        check(not b.terrain and b.original_shape=="cube" and b.block==3,"Original construction blocks survive removal")
    # Real world cells straddle the native 32 m chunk edge and the 8 m smooth patch edge.
    for x in [31,32]:
        check(game.set_voxel(x,220,20,3),"World boundary cube")
        check(game.execute_command("terrain at %d 220 20 on"%x)=="Terrain modifier updated.","Apply same modifier to world cells")
    actor.position=Vector3(31,228,24)
    await settle()
    await physics_frame
    var left: Dictionary=game.terrain_hit(Vector3(31.999,225,20.5),Vector3.DOWN,20)
    var right: Dictionary=game.terrain_hit(Vector3(32.001,225,20.5),Vector3.DOWN,20)
    check(not left.is_empty() and not right.is_empty(),"Terrain joins across chunk and patch boundaries")
    if not left.is_empty() and not right.is_empty():check(absf(left.position.y-right.position.y)<.01,"Boundary surface has no step or gap")
    check(game.get_voxel(31,220,20)==3 and game.get_voxel(32,220,20)==3,"World IDs retained under smooth surface")
    aim(Vector3(31.5,223,20.6),Vector3(31.5,220.5,20.5))
    await create_timer(.1).timeout
    check(game.execute_command("paint #bc44ef 0.3 1.1")=="Paint applied.","Paint targets the original owner through smooth geometry")
    await settle()
    var painted: Dictionary=game.get_voxel_appearance(31,220,20)
    check(painted.painted and painted.color=="#bc44ef","Smooth source retains editable material appearance")
    click(MOUSE_BUTTON_LEFT)
    await settle()
    check(not game.get_voxel_appearance(31,220,20).terrain,"Mining clears the independent modifier on air")
    check(game.get_voxel(31,220,20)==0 and game.get_voxel(32,220,20)==3,"Actual mining removes the selected smooth source and updates its neighbor")
    check(game.set_voxel(31,220,20,3),"Replace mined source")
    await settle()
    check(not game.get_voxel_appearance(31,220,20).terrain,"Replacement starts as an ordinary cube")
    print("TERRAIN_GAMEPLAY_PASS" if failures.is_empty() else "TERRAIN_GAMEPLAY_FAIL "+str(failures))
    quit(0 if failures.is_empty() else 1)
