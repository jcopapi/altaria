extends SceneTree
var game: Node
var failures: Array[String] = []
func check(value: bool, message: String) -> void:
    if not value:
        failures.append(message)
        push_error(message)
func _initialize() -> void:
    run.call_deferred()
func settle() -> void:
    for i in range(1800):
        await create_timer(.02).timeout
        var s: Dictionary = game.get_debug_state()
        if s.pending == 0 and s.tiles == s.wanted:
            return
    check(false, "Streaming completes within 36 seconds")
func run() -> void:
    game=load("res://scenes/main.tscn").instantiate()
    root.add_child(game)
    await process_frame
    check(ProjectSettings.get_setting("physics/3d/physics_engine") == "Jolt Physics", "Project uses Jolt backend")
    print("PHYSICS_SERVER ", PhysicsServer3D.get_class())
    check(game.start_world("Environment integration",1337),"Create feature test world")
    game.execute_command("time pause")
    game.execute_command("tp 256 102 128")
    var player: Node3D
    for node in game.get_node("WorldSession").get_children():
        if node.get_class()=="FlyingPlayer": player=node
    check(player!=null,"Native player found")
    await settle()
    var initial: Dictionary=game.get_debug_state()
    check(initial.grass_quads>0,"Grass quads are generated near the player")
    check(initial.terrain_colliders==0 and initial.player_collision_ready,"Player terrain is ready without eager Jolt colliders")
    game.execute_command("time 14")
    var day: Dictionary=game.get_temperature(Vector3(256,230,128))
    game.execute_command("time 2")
    var night: Dictionary=game.get_temperature(Vector3(256,230,128))
    check(day.ambient_c>night.ambient_c+5,"Biome daytime is warmer than nighttime")
    game.execute_command("weather storm")
    var storm: Dictionary=game.get_temperature(Vector3(256,230,128))
    check(storm.ambient_c<night.ambient_c-5,"Weather changes real Celsius temperature")
    check(game.execute_command("time nan").begins_with("Usage"),"Invalid time rejected")
    check(game.execute_command("wind 9").begins_with("Usage"),"Unbounded wind rejected")
    game.execute_command("weather clear")
    game.execute_command("time 14")
    check(game.set_voxel(256,231,128,11),"Place data-driven cold ice block")
    var cold: Dictionary=game.get_temperature(Vector3(256.5,231.5,128.5))
    check(cold.block_delta_c < -9,"Ice provides local cold influence")
    check(game.release_voxel(256,231,128,false),"Cold block becomes physical")
    check(game.get_temperature(Vector3(256.5,231.5,128.5)).block_delta_c < -9,"Physical ice still contributes local cold")
    game.execute_command("physics clear")
    game.execute_command("light #af55ff 3 12")
    check(game.get_debug_state().colored_lights==1,"Colored light command creates actual light")
    # Flat test deck, clear of all generated terrain.
    for z in range(116,141):
        for x in range(244,269):
            check(game.set_voxel(x,220,z,1),"Place physics test deck")
    var names=["cube","slab","vertical_slab","ramp","stairs","roof","pillar","mini_pillar"]
    for i in range(names.size()):
        check(game.set_voxel(248+i,223,125,3),"Place shaped grass material")
        check(game.shape_voxel(248+i,223,125,names[i],i%4),"Apply block shape and rotation")
        check(game.paint_voxel(248+i,223,125,"#67bb43",.5,1.3),"Paint shaped block")
        check(game.get_voxel_appearance(248+i,223,125).shape==names[i],"Shape remains independent of material")
    check(not game.shape_voxel(248,223,125,"invalid",0),"Invalid shape rejected")
    check(not game.shape_voxel(248,223,125,"slab",4),"Invalid rotation rejected")
    check(game.set_voxel(252,225,128,3),"Place walkable slab")
    check(game.shape_voxel(252,225,128,"slab",0),"Slab collision setup")
    await settle()
    game.execute_command("fly off")
    game.execute_command("tp 252.5 226.1 128.5")
    await create_timer(.8).timeout
    check(game.get_debug_state().grounded and abs(game.get_debug_state().player_position.y-225.5)<.015,"Player lands at real half-block height")
    game.execute_command("fly on")
    game.execute_command("tp 256 240 128")
    # Known tree fixture exercises connected wood+canopy conversion and cut stump.
    for y in range(221,228): game.set_voxel(256,y,132,9)
    for y in range(225,229):
        for z in range(130,135):
            for x in range(254,259):
                if x!=256 or z!=132: game.set_voxel(x,y,z,10)
    check(game.release_voxel(256,222,132,true),"Cut connected tree into one compound Jolt body")
    check(game.get_voxel(256,221,132)==9 and game.get_voxel(256,222,132)==0,"Cut preserves stump and journals removed upper tree")
    var cut_state: Dictionary=game.get_debug_state()
    check(cut_state.physics_bodies==1 and cut_state.physics_parts>50,"Whole tree uses one body with many detachable parts")
    var before: Vector3=cut_state.physics_positions[0]
    check(game.set_voxel(260,229,128,1),"Place falling stone")
    check(game.release_voxel(260,229,128,false),"Release single block into Jolt")
    await settle()
    await create_timer(6).timeout
    var fallen: Dictionary=game.get_debug_state()
    print("FALLEN_STATE ", fallen)
    check(fallen.physics_frozen==0,"Bodies activate after current terrain colliders upload")
    check(fallen.terrain_colliders>0 and fallen.terrain_colliders<fallen.lod0,"Jolt colliders are built where debris needs them")
    check(before.distance_to(fallen.physics_positions[0])>.2 and fallen.physics_rotations[0].length()>.2,"Tree moves and topples under Jolt")
    check(fallen.physics_positions[1].y>220.9 and fallen.physics_positions[1].y<221.3,"Falling block lands on voxel collision mesh")
    # Walking uses exact Jolt queries against rotated physical blocks as well as terrain.
    var stone_position: Vector3=fallen.physics_positions[1]
    player.rotation=Vector3.ZERO
    game.execute_command("fly off")
    player.position=stone_position+Vector3(.5,.03,3)
    var forward=InputEventKey.new()
    forward.keycode=KEY_W
    forward.physical_keycode=KEY_W
    forward.pressed=true
    Input.parse_input_event(forward)
    await create_timer(.8).timeout
    forward.pressed=false
    Input.parse_input_event(forward)
    await process_frame
    check(player.position.z>stone_position.z+1.15,"Player walking collides with fallen Jolt block")
    game.execute_command("fly on")
    # Real interaction detaches one part from the moving tree compound.
    var physics_node: Node3D
    for node in game.get_node("WorldSession").get_children():
        if node.get_class()=="PhysicsBlocks": physics_node=node
    var bodies=physics_node.get_children()
    check(bodies.size()==2,"Physical debris scene matches bounded records")
    # Aim at the single body and remove it through the normal LMB code path.
    var body: RigidBody3D=bodies[1]
    var target: Vector3=body.global_transform*Vector3(.5,.5,.5)
    player.position=target+Vector3(0,-1.65,4)
    player.rotation=Vector3.ZERO
    var camera: Camera3D=player.find_children("*","Camera3D",true,false)[0]
    camera.rotation=Vector3.ZERO
    var mouse=InputEventMouseButton.new()
    mouse.button_index=MOUSE_BUTTON_LEFT
    mouse.pressed=true
    await create_timer(.04).timeout
    check(game.get_debug_state().selection_visible and game.get_debug_state().selection_physical,"Physical block displays selected-cell highlight")
    Input.parse_input_event(mouse)
    await process_frame
    check(game.get_debug_state().physics_bodies==1,"LMB removes selected physical block")
    # Aim at canopy attached to the fallen tree; extraction creates a separate body.
    var tree: RigidBody3D=physics_node.get_children()[0]
    var trunk_target: Vector3=tree.global_transform*Vector3(.5,5.5,.5)
    player.position=trunk_target+Vector3(0,.35,4)
    camera.look_at(trunk_target,Vector3.UP)
    var count_before: int=game.get_debug_state().physics_bodies
    Input.parse_input_event(mouse)
    await process_frame
    check(game.get_debug_state().physics_bodies==count_before+1,"LMB detaches part from tree into another Jolt body")
    game.execute_command("weather rain")
    game.execute_command("time 19")
    game.execute_command("wind 1.2")
    check(game.save_world(),"Save climate and moving tree")
    var path: String=game.get_world_path()
    var saved: Dictionary=game.get_debug_state()
    check(game.open_world(path),"Reopen feature world")
    var loaded: Dictionary=game.get_debug_state()
    check(loaded.weather=="rain" and abs(loaded.hour-19)<.01 and not loaded.running,"Time weather and pause persist")
    check(loaded.colored_lights==1,"Colored light persists")
    check(loaded.physics_bodies==saved.physics_bodies and loaded.physics_parts==saved.physics_parts,"Tree parts and transforms persist")
    check(loaded.physics_positions[0].distance_to(saved.physics_positions[0])<.1,"Rigid body world transform persists")
    for i in range(names.size()):
        var a: Dictionary=game.get_voxel_appearance(248+i,223,125)
        check(a.shape==names[i] and a.rotation==i%4 and a.painted and a.color=="#67bb43","Shape and paint survive saved-world restart")
    print("ENVIRONMENT_STATS ", loaded)
    if failures.is_empty():
        print("ENVIRONMENT_RUNTIME_PASS")
        quit(0)
    else:
        print("ENVIRONMENT_RUNTIME_FAIL ", failures)
        quit(1)
