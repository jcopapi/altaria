extends "res://tests/cartoon_runtime.gd"

var output: String

func settle_view() -> void:
    await create_timer(.35).timeout
    await settle()

func capture(label: String) -> void:
    if DisplayServer.get_name()=="headless": return
    await create_timer(.2).timeout
    await RenderingServer.frame_post_draw
    check(root.get_texture().get_image().save_png(output.path_join(label+".png"))==OK,"Capture "+label)

func run() -> void:
    output=OS.get_environment("ALTARIA_EXPLORATION_OUTPUT")
    game=load("res://scenes/main.tscn").instantiate()
    root.add_child(game)
    await process_frame
    check(game.start_world("Cave exploration",1337),"Load final cave profiles and blocks")
    if not failures.is_empty():
        quit(1)
        return
    find_player()
    Input.mouse_mode=Input.MOUSE_MODE_VISIBLE
    game.execute_command("time pause")
    actor.position=Vector3(100,220,100)
    await settle_view()
    var initial: Dictionary=game.get_debug_state()
    check(initial.terrain_colliders==0 and initial.player_collision_ready,"LOD0 player movement is ready without eager Jolt bodies")
    var mouth: Dictionary={}
    var entrance: Dictionary={}
    var feature: Dictionary={}
    var feature_score: int=-1
    var deepest: Dictionary={}
    for rz in range(-2,3):
        for rx in range(-2,3):
            actor.position=Vector3(rx*224+112,220,rz*224+112)
            var state: Dictionary=game.get_debug_state()
            for entry in state.cave_entrances:
                entrance=entry
                if entry.kind=="mouth":mouth=entry
            for c in state.cave_chambers:
                if c.center.y < -410:deepest=c
            for f in game.get_cave_formations():
                if f.get("supported_voxels",0)>0 and f.kind in ["fungus","root","rubble","stalactite"]:
                    var score: int=f.supported_voxels+(1000 if f.kind in ["fungus","root"] else 0)
                    if score>feature_score:
                        feature_score=score
                        feature=f
                        for c in state.cave_chambers:
                            if c.id==f.room:feature["room_center"]=c.center
    check(not entrance.is_empty(),"A surface opening is present")
    check(not mouth.is_empty(),"Find a sideways hillside opening")
    check(not deepest.is_empty(),"Find the lower layer")
    if not mouth.is_empty():
        var p: Vector3=mouth.position
        var towards:=Vector3.ZERO
        var best:=-INF
        for i in range(8):
            var direction:=Vector3(cos(i*TAU/8),0,sin(i*TAU/8))
            var env: Dictionary=game.get_cave_environment(p+direction*14)
            if env.surface_height>best:
                best=env.surface_height
                towards=direction
        aim(p-towards*10+Vector3.UP*4,p+towards*6)
        await settle_view()
        print("EXPLORATION_MOUTH ",p)
        await capture("hillside-mouth")
    if not feature.is_empty():
        var p:=Vector3(feature.sample)+Vector3(.5,.5,.5)
        actor.position=p+Vector3(0,8,0)
        await settle_view()
        var id: int=game.get_voxel(int(floor(p.x)),int(floor(p.y)),int(floor(p.z)))
        check(id in [1,14,24,27,28],"Supported formation exists in logical voxels")
        var colored: Dictionary={}
        var cell:=Vector3i(p.floor())
        for z in range(cell.z-4,cell.z+5):
            for x in range(cell.x-4,cell.x+5):
                for y in range(cell.y-4,cell.y+6):
                    var nearby: int=game.get_voxel(x,y,z)
                    if nearby in [14,24,27,28]:colored[nearby]=true
        check(not colored.is_empty(),"Formation contains an actual solid-color cave material")
        print("EXPLORATION_FORMATION ",feature.kind," block=",id," colored=",colored," supported=",feature.supported_voxels)
        if feature.has("room_center"):
            var eye: Vector3=feature.room_center
            if eye.distance_to(p)>1:
                var lamp:=OmniLight3D.new()
                lamp.omni_range=60
                lamp.omni_attenuation=.4
                lamp.light_energy=4.0
                camera.add_child(lamp)
                aim(eye,p)
                await settle_view()
                await capture("cave-formation")
                lamp.queue_free()
    else:
        check(false,"Find supported exploration formation in streamed regions")
    if not deepest.is_empty():
        var p: Vector3=deepest.center
        actor.position=p
        await settle_view()
        var environment: Dictionary=game.get_cave_environment(p)
        print("EXPLORATION_DEPTH y=",p.y," below=",environment.below_surface)
        check(environment.loaded and environment.below_surface>410 and environment.below_surface<600,"Deep cave is shallower and still streamed")
    aim(Vector3(100,258,100),Vector3(102,251,102))
    await settle_view()
    for x in range(98,105):
        for z in range(98,105):
            check(game.set_voxel(x,250,z,1),"Build test platform")
    check(game.set_voxel(102,256,102,1),"Place drop fixture")
    check(game.release_voxel(102,256,102,false),"Release rigid body")
    await settle_view()
    await create_timer(2.0).timeout
    var active: Dictionary=game.get_debug_state()
    var bodies: Array=active.physics_positions
    check(bodies.size()==1 and abs(bodies[0].y-251)<.15,"On-demand Jolt collider supports fallen block")
    check(active.terrain_colliders>0 and active.terrain_colliders<active.lod0,"Only requested terrain colliders were built")
    print("EXPLORATION_JOLT colliders=",active.terrain_colliders," lod0=",active.lod0," body=",bodies)
    root.remove_child(game)
    game.queue_free()
    for i in range(4):await process_frame
    print("EXPLORATION_RUNTIME_PASS" if failures.is_empty() else "EXPLORATION_RUNTIME_FAIL "+str(failures))
    quit(0 if failures.is_empty() else 1)
