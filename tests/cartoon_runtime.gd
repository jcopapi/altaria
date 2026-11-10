extends SceneTree
var game: Node
var actor: Node3D
var camera: Camera3D
var failures: Array[String]=[]
func check(value: bool,message: String) -> void:
    if not value:
        failures.append(message)
        push_error(message)
func _initialize() -> void:
    run.call_deferred()
func settle() -> void:
    for i in range(1800):
        await create_timer(.02).timeout
        var s: Dictionary=game.get_debug_state()
        if s.wanted>0 and s.pending==0 and s.tiles==s.wanted:return
    check(false,"World work completes within 36 seconds")
func find_player() -> void:
    actor=game.find_children("*","FlyingPlayer",true,false)[0]
    camera=actor.find_children("*","Camera3D",true,false)[0]
    actor.set_physics_process(false)
    actor.set_process_unhandled_input(false)
func aim(eye: Vector3,target: Vector3) -> void:
    actor.position=eye-Vector3(0,1.65,0)
    camera.global_position=eye
    camera.look_at(target,Vector3.UP)
func click(button: MouseButton) -> void:
    var event:=InputEventMouseButton.new()
    event.button_index=button
    event.pressed=true
    Input.parse_input_event(event)
func block_by_id(id: int) -> Dictionary:
    for b in game.get_built_blocks():
        if b.id==id:return b
    return {}
func run() -> void:
    game=load("res://scenes/main.tscn").instantiate()
    root.add_child(game)
    await process_frame
    check(game.start_world("Cartoon building integration",1337),"Open fixed world")
    find_player()
    game.execute_command("time pause")
    game.execute_command("tp 0 225 3")
    await settle()
    for x in [0,3]:
        check(game.set_voxel(x,220,0,3),"Create procedural-grid support fixture")
        check(game.shape_voxel(x,220,0,"slab",0),"Half-height support")
    game.execute_command("block altaria:stone")
    game.execute_command("shape cube")
    aim(Vector3(.5,225,3),Vector3(.5,220.5,.5))
    await create_timer(.05).timeout
    click(MOUSE_BUTTON_RIGHT)
    await create_timer(.05).timeout
    var blocks: Array=game.get_built_blocks()
    check(blocks.size()==1,"RMB creates an independently stored construction piece")
    var cube_id: int=blocks[0].id if blocks.size()>0 else 0
    check(cube_id>0 and block_by_id(cube_id).position.is_equal_approx(Vector3(0,220.5,0)),"Full block touches the actual slab top without floating")
    check(game.get_voxel(0,220,0)==3,"Supporting slab material is preserved")
    check(game.get_voxel_appearance(0,220,0).shape=="slab","Supporting slab form is preserved")
    game.execute_command("block altaria:clay")
    game.execute_command("shape slab")
    aim(Vector3(3.5,225,3),Vector3(3.5,220.5,.5))
    await create_timer(.04).timeout
    click(MOUSE_BUTTON_RIGHT)
    await create_timer(.05).timeout
    var first_id: int=0
    for b in game.get_built_blocks():
        if b.position.is_equal_approx(Vector3(3,220.5,0)):first_id=b.id
    check(first_id>0,"Two slabs can share one ordinary voxel cell")
    aim(Vector3(3.5,225,3),Vector3(3.5,221,.5))
    await create_timer(.04).timeout
    click(MOUSE_BUTTON_RIGHT)
    await create_timer(.05).timeout
    var second_id: int=0
    for b in game.get_built_blocks():
        if b.position.is_equal_approx(Vector3(3,221,0)):second_id=b.id
    check(second_id>0,"Slab stacking continues with half-unit spacing")
    check(game.place_block(Vector3(0,220.5,0),1,"cube",0)==0,"Real geometry overlap is rejected")
    aim(Vector3(3.5,220.8,4),Vector3(3.5,220.75,.5))
    await create_timer(.05).timeout
    check(game.get_debug_state().selection_built_id==first_id,"Ray selects exact lower construction slab")
    check(game.execute_command("paint #c933ff 0.4 1.2")=="Paint applied.","Paint applies to selected fractional piece")
    check(block_by_id(first_id).painted and block_by_id(first_id).color==0xc933ff,"Construction stores independent tint")
    check(game.execute_command("overlay grass")=="Overlay applied.","Overlays apply to fractional pieces")
    # Use native player movement against fractional geometry.
    game.execute_command("fly off")
    actor.position=Vector3(.5,222.1,.5)
    actor.set_physics_process(true)
    await create_timer(.8).timeout
    check(game.get_debug_state().grounded and abs(actor.position.y-221.5)<.02,"Player lands on actual cube above slab")
    actor.set_physics_process(false)
    game.execute_command("fly on")
    actor.position=Vector3(.5,225,3)
    await settle()
    check(game.set_voxel(0,225,0,1),"Create Jolt drop fixture")
    check(game.release_voxel(0,225,0,false),"Release stone above fractional construction")
    await settle()
    await create_timer(2.0).timeout
    var positions: Array=game.get_debug_state().physics_positions
    check(positions.size()==1 and abs(positions[0].y-221.5)<.08,"Jolt block collides with fractionally placed construction")
    game.execute_command("physics clear")
    check(game.place_block(Vector3(-1,220.5,-33),1,"slab",0)>0,"Fractional placement across negative region boundaries")
    # Fog follows the current biome, day/night, and dynamic manual overrides.
    var ends: Dictionary={}
    for biome in ["normal","desert","snow","enchanted"]:
        game.execute_command("biome "+biome)
        game.execute_command("time 9")
        await create_timer(.25).timeout
        var state: Dictionary=game.get_debug_state()
        var color: Color=state.fog_color
        ends[biome]=state.fog_end
        if biome=="normal":check(color.b>color.r and color.b>color.g,"Day meadow fog is blue")
        elif biome=="desert":check(color.r>color.b and color.g>color.b,"Desert fog is yellow")
        elif biome=="snow":check(color.r>.7 and color.g>.8,"Snow fog is pale white")
        else:check(color.r>color.g and color.b>color.g,"Enchanted fog is fantasy purple")
    check(ends.desert<ends.normal*.3 and ends.snow<ends.normal*.3,"Desert and snow shorten visibility")
    game.execute_command("biome normal")
    game.execute_command("time 2")
    await create_timer(.25).timeout
    var night: Color=game.get_debug_state().fog_color
    check(night.b>night.r and night.r>night.g,"Night meadow fog becomes purple")
    check(game.execute_command("fog #ff3355 20 300")=="Fog updated.","Dynamic fog command")
    game.execute_command("outline off")
    game.execute_command("pixelation off")
    await create_timer(.15).timeout
    check(game.get_debug_state().fog_mode=="manual" and abs(game.get_debug_state().fog_end-300)<.01,"Manual fog reaches requested distance")
    check(not game.get_debug_state().outline_enabled and not game.get_debug_state().pixelation_enabled,"Render effects can be toggled")
    game.save_world()
    var path: String=game.get_world_path()
    var count: int=game.get_built_blocks().size()
    check(game.open_world(path),"Reopen fractional construction world")
    find_player()
    check(game.get_built_blocks().size()==count,"All independent pieces survive reload")
    check(block_by_id(first_id).position.is_equal_approx(Vector3(3,220.5,0)) and block_by_id(first_id).color==0xc933ff,"Fractional position and individual paint persist")
    check(game.get_debug_state().fog_mode=="manual" and not game.get_debug_state().outline_enabled,"Manual fog/render toggles persist")
    game.execute_command("fog biome")
    await create_timer(.15).timeout
    check(game.get_debug_state().fog_mode=="biome","Automatic fog can be restored")
    aim(Vector3(3.5,220.8,4),Vector3(3.5,220.75,.5))
    await create_timer(.05).timeout
    click(MOUSE_BUTTON_LEFT)
    await create_timer(.05).timeout
    check(block_by_id(first_id).is_empty() and not block_by_id(second_id).is_empty(),"LMB removes only selected slab, preserving stacked pieces")
    # Release the half-offset cube into physics using the normal command target.
    aim(Vector3(.5,221,4),Vector3(.5,221,.5))
    await create_timer(.05).timeout
    check(game.execute_command("physics")=="Released into Jolt physics.","Fractional construction converts to physics")
    check(block_by_id(cube_id).is_empty(),"Converted piece leaves the construction store")
    game.save_world()
    check(game.open_world(path),"Reopen removal/physics changes")
    check(block_by_id(first_id).is_empty() and block_by_id(cube_id).is_empty(),"Removals persist without resurrecting source pieces")
    print("CARTOON_RUNTIME_PASS" if failures.is_empty() else "CARTOON_RUNTIME_FAIL "+str(failures))
    quit(0 if failures.is_empty() else 1)
