extends SceneTree
var game: Node
var actor: Node3D
var camera: Camera3D
var failures: Array[String]=[]
const OUTPUT="/tmp/altaria-cartoon-preview"
func check(v: bool,message: String) -> void:
    if not v:
        failures.append(message)
        push_error(message)
func _initialize() -> void:run.call_deferred()
func settle() -> void:
    root.grab_focus()
    var start:=Time.get_ticks_msec()
    var stable:=0
    for i in range(6000):
        await process_frame
        var s: Dictionary=game.get_debug_state()
        if i<3 or i%180==0:print("CARTOON_FRAME ",i," tiles=",s.tiles," pending=",s.pending)
        if Time.get_ticks_msec()-start>400 and s.wanted>0 and s.pending==0 and s.tiles==s.wanted:
            stable+=1
            if stable>=3:return
        else:stable=0
        if Time.get_ticks_msec()-start>45000:break
    check(false,"Visual streaming timeout")
func capture(name: String) -> Image:
    await RenderingServer.frame_post_draw
    var img:=root.get_texture().get_image()
    DirAccess.make_dir_recursive_absolute(OUTPUT)
    check(img.save_png(OUTPUT+"/"+name+".png")==OK,"Save "+name)
    return img
func aim(eye: Vector3,target: Vector3) -> void:
    actor.position=eye-Vector3(0,1.65,0)
    camera.global_position=eye
    camera.look_at(target,Vector3.UP)
func hide_ui() -> void:
    for c in game.find_children("*","Control",true,false):
        if c.get_parent() is CanvasLayer and c.get_parent().layer==20:c.hide()
func difference(a: Image,b: Image,rect: Rect2i) -> float:
    rect=rect.intersection(Rect2i(Vector2i.ZERO,a.get_size()))
    var total:=0.0
    var count:=0
    for y in range(rect.position.y,rect.end.y,3):
        for x in range(rect.position.x,rect.end.x,3):
            var ca:=a.get_pixel(x,y)
            var cb:=b.get_pixel(x,y)
            total+=abs(ca.r-cb.r)+abs(ca.g-cb.g)+abs(ca.b-cb.b)
            count+=3
    return total/max(1,count)
func run() -> void:
    root.size=Vector2i(1280,720)
    root.position=Vector2i(60,60)
    root.always_on_top=true
    root.grab_focus()
    await process_frame
    await create_timer(.15).timeout
    game=load("res://scenes/main.tscn").instantiate()
    root.add_child(game)
    await process_frame
    game.start_world("Cartoon art validation",1337)
    actor=game.find_children("*","FlyingPlayer",true,false)[0]
    camera=actor.find_children("*","Camera3D",true,false)[0]
    actor.set_physics_process(false)
    actor.set_process_unhandled_input(false)
    Input.mouse_mode=Input.MOUSE_MODE_VISIBLE
    hide_ui()
    game.execute_command("time pause")
    game.execute_command("time 9")
    game.execute_command("wind 0")
    root.grab_focus()
    print("CARTOON_START_WAIT scale=",Engine.time_scale," paused=",paused)
    await settle()
    # Layered pedestal emphasizes subtle contour strokes and zero-gap slabs.
    for dz in range(-2,3):
        for dx in range(-2,3):check(game.place_block(Vector3(256+dx,100,126+dz),11,"slab",0)>0,"Lower podium slab")
    for dz in range(-1,2):
        for dx in range(-1,2):check(game.place_block(Vector3(256+dx,100.5,126+dz),11,"slab",0)>0,"Upper podium slab without gap")
    check(game.place_block(Vector3(256,101,126),8,"pillar",0)>0,"Pedestal centerpiece")
    aim(Vector3(258,103.2,133),Vector3(256.5,100.7,126.5))
    await settle()
    game.execute_command("outline off")
    await create_timer(.2).timeout
    game.set_process(false)
    game.find_child("BlockSelection",true,false).hide()
    await process_frame
    var original:=await capture("outline_off")
    game.set_process(true)
    game.execute_command("outline on")
    await create_timer(.2).timeout
    game.set_process(false)
    game.find_child("BlockSelection",true,false).hide()
    await process_frame
    var inked:=await capture("outline_on")
    game.set_process(true)
    var ink_change:=difference(original,inked,Rect2i(300,220,700,420))
    print("OUTLINE_PIXEL_DIFFERENCE ",ink_change)
    check(ink_change>.001,"Nearby hills and podium silhouettes are outlined independent of distance")
    aim(Vector3(256,145,240),Vector3(256.5,100.7,126.5))
    await settle()
    game.execute_command("outline off")
    await create_timer(.2).timeout
    var mid_original:=await capture("outline_mid_off")
    game.execute_command("outline on")
    await create_timer(.2).timeout
    var mid_inked:=await capture("outline_mid_on")
    var mid_change:=difference(mid_original,mid_inked,Rect2i(180,180,900,500))
    print("MID_DISTANCE_OUTLINE_DIFFERENCE ",mid_change)
    check(mid_change>.001,"Contour remains visible at more than 100 metres")
    # The horizon's raster is coarsened while foreground and sky stay sharp.
    game.execute_command("fog #86a8d8 4000 5700")
    game.execute_command("outline off")
    aim(Vector3(256,190,140),Vector3(400,105,-500))
    await settle()
    game.execute_command("pixelation off")
    await create_timer(.2).timeout
    var sharp:=await capture("lod_sharp")
    game.execute_command("pixelation on")
    await create_timer(.2).timeout
    var pixels:=await capture("lod_pixelated")
    var pixel_change:=difference(sharp,pixels,Rect2i(0,300,1280,400))
    print("DISTANT_PIXELATION_DIFFERENCE ",pixel_change)
    check(pixel_change>.0002,"Distant LOD geometry receives visible pixelation")
    game.execute_command("fog biome")
    game.execute_command("outline on")
    await create_timer(.3).timeout
    await capture("distance_toon")
    for biome in ["normal","desert","snow","enchanted"]:
        game.execute_command("biome "+biome)
        game.execute_command("time 9")
        camera.rotation.x=-.2
        await settle()
        await create_timer(.3).timeout
        await capture("fog_"+biome+"_day")
        if biome=="normal":
            game.execute_command("time 2")
            await create_timer(.3).timeout
            await capture("fog_normal_night")
    # Material color regression: full grass block and transition side overlay on
    # the dirt block directly below it, with no 3D grass instances.
    game.execute_command("fog #86a8d8 4000 5700")
    game.execute_command("time 9")
    game.execute_command("fullbright on")
    game.execute_command("outline off")
    game.execute_command("pixelation off")
    game.set_voxel(256,229,128,2)
    game.set_voxel(256,230,128,3)
    game.paint_voxel(256,230,128,"#58b938",.65,1.25)
    aim(Vector3(256.5,231.5,131.5),Vector3(256.5,230.2,128.5))
    await settle()
    var batches:=game.find_children("GrassInstances","MultiMeshInstance3D",true,false)
    check(batches.is_empty(),"No 3D grass MultiMesh instances are created")
    hide_ui()
    var selection: MeshInstance3D=game.find_child("BlockSelection",true,false)
    selection.hide()
    await create_timer(.1).timeout
    var grass_view:=await capture("grass_block_and_transition")
    var screen_scale:=Vector2(grass_view.get_size())/root.get_visible_rect().size
    var grass_pixel:=Vector2i(camera.unproject_position(Vector3(256.5,230.5,129.0))*screen_scale)
    var grass_color:=grass_view.get_pixelv(grass_pixel)
    check(grass_color.g>grass_color.r and grass_color.g>grass_color.b,"Grass block face is green-tinted using grass top texture")
    print("CARTOON_VISUAL_PASS" if failures.is_empty() else "CARTOON_VISUAL_FAIL "+str(failures))
    quit(0 if failures.is_empty() else 1)
