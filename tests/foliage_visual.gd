extends SceneTree
var game: Node
var actor: Node3D
var camera: Camera3D
var failures: Array[String]=[]
const OUTPUT="/tmp/altaria-foliage-preview"
func check(condition: bool,message: String) -> void:
    if not condition:
        failures.append(message)
        push_error(message)
func _initialize() -> void:
    run.call_deferred()
func terrain_ready() -> void:
    # process_frame fires before native _process. Allow a complete streaming
    # tick after teleports instead of accepting the previous frame's idle queue.
    var started:=Time.get_ticks_msec()
    var deadline:=Time.get_ticks_msec()+45000
    var stable:=0
    while Time.get_ticks_msec()<deadline:
        await process_frame
        var s: Dictionary=game.get_debug_state()
        if Time.get_ticks_msec()-started>400 and s.wanted>0 and s.pending==0 and s.tiles==s.wanted:
            stable+=1
            if stable>=3:return
        else:stable=0
    check(false,"Streaming settles within 45 seconds")
func find_player() -> void:
    actor=game.find_children("*","FlyingPlayer",true,false)[0]
    camera=actor.find_children("*","Camera3D",true,false)[0]
    actor.set_physics_process(false)
    actor.set_process_unhandled_input(false)
func capture(name: String) -> Image:
    await RenderingServer.frame_post_draw
    var image:=root.get_texture().get_image()
    DirAccess.make_dir_recursive_absolute(OUTPUT)
    check(image.save_png(OUTPUT+"/"+name+".png")==OK,"Capture "+name)
    return image
func ground(x: int,z: int) -> float:
    for y in range(300,-63,-1):
        if game.get_voxel(x,y,z) not in [0,5,9,10]:
            return y+(.5 if game.get_voxel_appearance(x,y,z).shape=="slab" else 1.0)
    return 43
func track(body: Node3D) -> void:
    var transform:=body.global_transform
    var eye: Vector3=transform*Vector3(.5,.7,3.5)
    actor.position=eye-Vector3(0,1.65,0)
    camera.global_position=eye
    camera.look_at(transform*Vector3(.5,.5,.5),transform.basis.y)
func pixel_difference(a: Image,b: Image) -> float:
    var center:=Vector2i(a.get_width()/2,a.get_height()/2)
    var total:=0.0
    for y in range(center.y-28,center.y+28):
        for x in range(center.x-28,center.x+28):
            var ca:=a.get_pixel(x,y)
            var cb:=b.get_pixel(x,y)
            total+=(abs(ca.r-cb.r)+abs(ca.g-cb.g)+abs(ca.b-cb.b))/3.0
    return total/(56*56)
func run() -> void:
    root.size=Vector2i(1280,720)
    root.position=Vector2i(60,60)
    root.always_on_top=true
    root.grab_focus()
    game=load("res://scenes/main.tscn").instantiate()
    root.add_child(game)
    await process_frame
    check(game.start_world("Foliage and physical UV regression",1337),"Create regression world")
    game.execute_command("time pause")
    game.execute_command("time 9")
    find_player()
    print("FOLIAGE_VISUAL_PLAYER_READY")
    Input.mouse_mode=Input.MOUSE_MODE_VISIBLE
    for control in game.find_children("*","Control",true,false):
        if control.get_parent() is CanvasLayer and control.get_parent().layer==20:control.hide()
    print("FOLIAGE_VISUAL_CONTROLS_READY")
    actor.position=Vector3(256.5,102,128.5)
    root.grab_focus()
    print("FOLIAGE_VISUAL_WAITING_TERRAIN")
    await terrain_ready()
    print("FOLIAGE_VISUAL_TERRAIN_READY")
    actor.position=Vector3(256.5,ground(256,128)+.25,128.5)
    print("FOLIAGE_VISUAL_GROUND_READY")
    actor.rotation=Vector3.ZERO
    camera.rotation=Vector3(-.20,0,0)
    await terrain_ready()
    await create_timer(.4).timeout
    var s: Dictionary=game.get_debug_state()
    print("FOLIAGE_STATS ",s)
    check(s.vegetation_tiles>0 and s.grass_instances>0,"Exposed grass has animated vegetation")
    var batches=game.find_children("GrassInstances","MultiMeshInstance3D",true,false)
    check(not batches.is_empty(),"Grass uses MultiMesh instances")
    await capture("terrain_grass_blocks")
    # Isolate a textured cube. A camera tracking its transform must see the same
    # texels after arbitrary translation and rotation, including after save/load.
    check(game.set_voxel(256,230,128,9),"Create bark-textured block")
    check(game.release_voxel(256,230,128,false),"Create physical texture fixture")
    var physics_node: Node=game.find_children("*","PhysicsBlocks",true,false)[0]
    physics_node.set_physics_process(false)
    var body: RigidBody3D=physics_node.get_children()[0]
    body.freeze=true
    body.global_transform=Transform3D(Basis.from_euler(Vector3(.08,.12,.03)),Vector3(256,230,128))
    game.execute_command("fullbright on")
    track(body)
    await terrain_ready()
    await create_timer(.2).timeout
    var selection: MeshInstance3D=game.find_child("BlockSelection",true,false)
    check(selection.visible and game.get_debug_state().selection_physical,"Physical cell is highlighted")
    var local: Transform3D=body.global_transform.affine_inverse()*selection.global_transform
    check(local.origin.distance_to(Vector3(-.003,-.003,-.003))<.002 and local.basis.is_equal_approx(Basis.IDENTITY.scaled(Vector3.ONE*1.006)),"Physical selection follows exact cell transform")
    var original:=await capture("physical_texture_before")
    body.global_transform=Transform3D(Basis.from_euler(Vector3(.63,1.21,.36)),Vector3(256.37,230.21,128.53))
    track(body)
    await create_timer(.15).timeout
    check(selection.visible and game.get_debug_state().selection_physical,"Highlight stays visible while physical block rotates")
    local=body.global_transform.affine_inverse()*selection.global_transform
    check(local.origin.distance_to(Vector3(-.003,-.003,-.003))<.002 and local.basis.is_equal_approx(Basis.IDENTITY.scaled(Vector3.ONE*1.006)),"Highlight rotates with physical block")
    var moved:=await capture("physical_texture_moved")
    var difference:=pixel_difference(original,moved)
    print("PHYSICAL_TEXTURE_TRANSLATION_ROTATION_DIFF ",difference)
    check(difference<.018,"Texture stays attached to translating/rotating block")
    var mesh: MeshInstance3D=body.get_children()[0]
    var anchor: Vector3=mesh.get_instance_shader_parameter("texture_anchor")
    check(anchor==Vector3(256,230,128),"Texture projection anchor is stable")
    game.save_world()
    var path: String=game.get_world_path()
    check(game.open_world(path),"Reload physical texture fixture")
    find_player()
    physics_node=game.find_children("*","PhysicsBlocks",true,false)[0]
    physics_node.set_physics_process(false)
    body=physics_node.get_children()[0]
    body.freeze=true
    game.execute_command("fullbright on")
    track(body)
    await terrain_ready()
    await create_timer(.2).timeout
    var restored:=await capture("physical_texture_restored")
    var restart_difference:=pixel_difference(moved,restored)
    print("PHYSICAL_TEXTURE_RESTART_DIFF ",restart_difference)
    check(restart_difference<.018,"Texture mapping survives saved-world restart")
    mesh=body.get_children()[0]
    check(mesh.get_instance_shader_parameter("texture_anchor")==anchor,"Stored texture anchor survives restart")
    # Returning from a rotated rigid body to static selection must clear its rotation.
    game.set_voxel(260,230,128,1)
    actor.position=Vector3(260.5,228.85,132)
    actor.rotation=Vector3.ZERO
    camera.rotation=Vector3.ZERO
    await create_timer(.15).timeout
    var state: Dictionary=game.get_debug_state()
    check(state.selection_visible and not state.selection_physical,"Static highlight works after physical highlight")
    check(state.selection_transform.basis.is_equal_approx(Basis.IDENTITY.scaled(Vector3.ONE*1.006)),"Static highlight clears previous rigid-body rotation")
    # Every non-cube form must be selectable in a rotated physical body too.
    var forms=["slab","vertical_slab","ramp","stairs","roof","pillar","mini_pillar"]
    var points=[Vector3(.5,.25,.5),Vector3(.25,.5,.5),Vector3(.65,.35,.5),Vector3(.75,.75,.5),Vector3(.5,.65,.5),Vector3(.5,.5,.5),Vector3(.5,.5,.5)]
    for i in range(forms.size()):
        var x:=264+i*2
        game.set_voxel(x,230,128,1)
        game.shape_voxel(x,230,128,forms[i],0)
        check(game.release_voxel(x,230,128,false),"Release "+forms[i]+" selection fixture")
        var parts=physics_node.get_children()
        body=parts[parts.size()-1]
        body.freeze=true
        body.rotation=Vector3(.1,.4,.2)
        var t:=body.global_transform
        var eye: Vector3=t*(points[i]+Vector3(0,.2,3))
        actor.position=eye-Vector3(0,1.65,0)
        camera.global_position=eye
        camera.look_at(t*points[i],t.basis.y)
        await create_timer(.06).timeout
        var current: Dictionary=game.get_debug_state()
        check(current.selection_visible and current.selection_physical,"Rotated physical "+forms[i]+" has selection")
        if forms[i]=="slab" or forms[i]=="ramp":await capture("physical_selection_"+forms[i])
    var outline: MeshInstance3D=game.find_child("BlockSelection",true,false)
    check(outline.material_override.is_grow_enabled() and outline.material_override.get_grow()>0.0,"Highlight extrudes along face normals to avoid coplanar ramp/slab flicker")
    print("FOLIAGE_VISUAL_PASS" if failures.is_empty() else "FOLIAGE_VISUAL_FAIL "+str(failures))
    quit(0 if failures.is_empty() else 1)
