extends SceneTree
var game: Node
var failures: Array[String]=[]
func check(value: bool,message: String) -> void:
    if not value:
        failures.append(message)
        push_error(message)
func _initialize() -> void:run.call_deferred()
func settle() -> void:
    var start:=Time.get_ticks_msec()
    var stable:=0
    while Time.get_ticks_msec()-start<45000:
        await process_frame
        var state: Dictionary=game.get_debug_state()
        if Time.get_ticks_msec()-start>400 and state.wanted>0 and state.pending==0 and state.tiles==state.wanted:
            stable+=1
            if stable>=3:return
        else:stable=0
    check(false,"Streaming completes")
func run() -> void:
    root.size=Vector2i(1280,720)
    game=load("res://scenes/main.tscn").instantiate()
    root.add_child(game)
    await process_frame
    check(game.start_world("SubViewport and edit stability",1337),"Start regression world")
    var actor: Node3D=game.find_children("*","FlyingPlayer",true,false)[0]
    var camera: Camera3D=actor.find_children("*","Camera3D",true,false)[0]
    actor.set_physics_process(false)
    actor.position=Vector3(256.5,86,128.5)
    game.execute_command("time pause")
    game.execute_command("time 9")
    Input.mouse_mode=Input.MOUSE_MODE_VISIBLE
    await settle()
    var viewport: SubViewport=game.find_child("WorldRenderViewport",true,false)
    var image: TextureRect=game.find_child("NearestWorldImage",true,false)
    var render_camera: Camera3D=game.find_child("WorldRenderCamera",true,false)
    check(viewport!=null and image!=null and render_camera!=null,"Dedicated scene SubViewport and presentation")
    if viewport and image and render_camera:
        check(viewport.world_3d==camera.get_world_3d(),"Render camera shares the authoritative physics world")
        check(camera.get_viewport()==root,"Input and logical camera keep original viewport coordinates")
        check(image.texture_filter==CanvasItem.TEXTURE_FILTER_NEAREST,"Upscaling uses nearest filtering")
        check(viewport.size.x==ceili(root.get_visible_rect().size.x/2) and viewport.size.y==ceili(root.get_visible_rect().size.y/2),"Whole scene renders at half resolution")
        actor.position+=Vector3(1,2,3)
        camera.rotation=Vector3(-.3,.2,0)
        await process_frame
        await process_frame
        check(render_camera.global_transform.is_equal_approx(camera.global_transform),"Render camera follows mouse/view without offset")
        root.size=Vector2i(1000,600)
        await create_timer(.1).timeout
        check(viewport.size.x==ceili(root.get_visible_rect().size.x/2),"Viewport follows window resize")
        game.execute_command("pixelation off")
        await create_timer(.2).timeout
        check(viewport.size==Vector2i(root.get_visible_rect().size),"Debug toggle restores native scene resolution")
        game.execute_command("pixelation on")
    actor.position=Vector3(256.5,86,128.5)
    await settle()
    # Observe every frame while a populated chunk rebuilds.
    var before: Dictionary=game.get_debug_state()
    check(before.grass_instances==0 and before.bushes==0,"3D grass and bushes are removed")
    var baseline_tiles: int=before.tiles
    var edits:=0
    for z in range(128,131):
        for y in range(140,40,-1):
            if game.get_voxel(256,y,z)==3:
                check(game.set_voxel(256,y,z,0),"Break grass support")
                edits+=1
                break
    var minimum_tiles:=baseline_tiles
    var start:=Time.get_ticks_msec()
    while Time.get_ticks_msec()-start<1500:
        await process_frame
        minimum_tiles=mini(minimum_tiles,int(game.get_debug_state().tiles))
    await settle()
    var after: Dictionary=game.get_debug_state()
    check(edits==3,"Three actual grass blocks were broken")
    check(minimum_tiles==baseline_tiles,"Breaking blocks never drops resident terrain chunks")
    check(after.grass_instances==0 and after.bushes==0,"No 3D grass or bushes generated after edits")
    print("EDIT_TILE_STABILITY baseline_tiles=",baseline_tiles," minimum_tiles=",minimum_tiles," final_tiles=",after.tiles)
    var themes: Dictionary={}
    for biome in ["normal","jungle","enchanted"]:
        game.execute_command("biome "+biome)
        game.execute_command("time 9")
        await create_timer(.2).timeout
        themes[biome]=game.get_debug_state().sky_zenith
    var meadow: Color=themes.normal
    var jungle: Color=themes.jungle
    var fantasy: Color=themes.enchanted
    check(jungle.g>meadow.g+.04,"Jungle changes the sky dome toward teal")
    check(fantasy.r>meadow.r+.05 and fantasy.g<meadow.g-.04,"Fantasy changes the full sky dome toward purple")
    game.execute_command("biome normal")
    game.execute_command("time 2")
    await create_timer(.2).timeout
    var night: Color=game.get_debug_state().sky_zenith
    check(night.r>night.g and night.b>night.r,"Meadow sky becomes purple at night")
    print("SUBVIEWPORT_RUNTIME_PASS" if failures.is_empty() else "SUBVIEWPORT_RUNTIME_FAIL "+str(failures))
    quit(0 if failures.is_empty() else 1)
