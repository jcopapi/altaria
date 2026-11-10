extends "res://tests/cartoon_visual.gd"
var fixtures: Node3D
func box(p: Vector3,size: Vector3,color: Color,roughness: float=.94) -> void:
    var node:=MeshInstance3D.new()
    var mesh:=BoxMesh.new()
    mesh.size=size
    var mat:=StandardMaterial3D.new()
    mat.albedo_color=color
    mat.roughness=roughness
    mat.specular_mode=BaseMaterial3D.SPECULAR_DISABLED
    mesh.material=mat
    node.mesh=mesh
    node.position=p
    fixtures.add_child(node)
func screen_rect(center: Vector3,half: Vector2,margin: float=0.0) -> Rect2i:
    var a:=camera.unproject_position(center+Vector3(-half.x,half.y,0))
    var b:=camera.unproject_position(center+Vector3(half.x,-half.y,0))
    return Rect2i(Vector2i(a-Vector2.ONE*margin),Vector2i(b-a+Vector2.ONE*margin*2))
func compare(label: String) -> Array[Image]:
    game.execute_command("outline off")
    await create_timer(.15).timeout
    var original:=await capture(label+"_off")
    game.execute_command("outline on")
    await create_timer(.15).timeout
    var inked:=await capture(label+"_on")
    return [original,inked]
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
    game.start_world("Slabs and silhouette contours",1337)
    actor=game.find_children("*","FlyingPlayer",true,false)[0]
    camera=actor.find_children("*","Camera3D",true,false)[0]
    actor.set_physics_process(false)
    actor.set_process_unhandled_input(false)
    Input.mouse_mode=Input.MOUSE_MODE_VISIBLE
    root.grab_focus()
    hide_ui()
    game.execute_command("time pause")
    game.execute_command("wind 0")
    game.execute_command("godrays off")
    game.execute_command("pixelation on")
    game.execute_command("fog #86a8d8 4000 5700")
    aim(Vector3(256,110,140),Vector3(290,79,30))
    await settle()
    game.execute_command("time 9")
    await capture("slabs_landscape_day")
    game.execute_command("time 22")
    await create_timer(.4).timeout
    await capture("slabs_landscape_night")
    fixtures=Node3D.new()
    game.add_child(fixtures)
    # Broad silhouette with small face/normal changes inside it.
    box(Vector3(-15,206,220),Vector3(16,12,1),Color(.45,.48,.52))
    for y in range(12):
        for x in range(16):
            if (x+y)%2==0:box(Vector3(-22.5+x,200.5+y,221),Vector3.ONE,Color(.45,.48,.52))
    box(Vector3(0,206,220),Vector3.ONE,Color(.45,.48,.52))
    box(Vector3(15,206,220),Vector3(12,12,1),Color(.25,.6,.2),.82)
    aim(Vector3(0,206,380),Vector3(0,206,220))
    await settle()
    game.set_process(false)
    game.find_child("BlockSelection",true,false).hide()
    for hour in [9,22]:
        game.execute_command("time "+str(hour))
        await create_timer(.3).timeout
        var images:=await compare("silhouettes_"+str(hour))
        var isolated:=difference(images[0],images[1],screen_rect(Vector3(0,206,220),Vector2(.5,.5)))
        var interior:=difference(images[0],images[1],screen_rect(Vector3(-15,206,221),Vector2(4,3)))
        var boundary:=difference(images[0],images[1],screen_rect(Vector3(-15,206,221),Vector2(8,6),4))
        var leaves:=difference(images[0],images[1],screen_rect(Vector3(15,206,220),Vector2(6,6)))
        print("SILHOUETTE_METRICS hour=",hour," isolated=",isolated," interior=",interior," boundary=",boundary," leaves=",leaves)
        check(isolated<.0001,"Single block has no contour at "+str(hour))
        check(interior<.0001,"Individual block faces have no interior seams at "+str(hour))
        check(boundary>.0002,"Broad silhouette remains outlined at "+str(hour))
        check(leaves<.0001,"Leaf blacklist remains respected at "+str(hour))
    game.set_process(true)
    aim(Vector3(-15,206,236),Vector3(-15,206,220))
    await settle()
    game.set_process(false)
    game.find_child("BlockSelection",true,false).hide()
    var near_images:=await compare("silhouette_near")
    var near_delta:=difference(near_images[0],near_images[1],screen_rect(Vector3(-15,206,221),Vector2(8,6),4))
    print("NEAR_SILHOUETTE_DIFFERENCE ",near_delta)
    check(near_delta>.0002,"Large silhouettes remain outlined at close range")
    # Same-height, broad land/water boundary: no depth or normal crease needed.
    fixtures.hide()
    var shore:=Node3D.new()
    game.add_child(shore)
    fixtures=shore
    box(Vector3(-8,190,270),Vector3(16,1,32),Color(.84,.74,.49))
    box(Vector3(8,190,270),Vector3(16,1,32),Color(.13,.38,.55),.68)
    aim(Vector3(0,240,410),Vector3(0,190.5,270))
    game.set_process(true)
    await settle()
    game.set_process(false)
    game.find_child("BlockSelection",true,false).hide()
    var shoreline_images:=await compare("shoreline")
    var center:=camera.unproject_position(Vector3(0,190.5,270))
    var delta:=difference(shoreline_images[0],shoreline_images[1],Rect2i(Vector2i(center-Vector2(6,20)),Vector2i(12,40)))
    print("SHORELINE_DIFFERENCE ",delta)
    check(delta>.0001,"Broad beach/water margin receives a contour")
    print("SILHOUETTE_VISUAL_PASS" if failures.is_empty() else "SILHOUETTE_VISUAL_FAIL "+str(failures))
    quit(0 if failures.is_empty() else 1)
