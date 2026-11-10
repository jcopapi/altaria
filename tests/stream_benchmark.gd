extends "res://tests/cartoon_visual.gd"
func report(name: String,samples: Array[float]) -> void:
    samples.sort()
    var slow:=0
    var very_slow:=0
    for value in samples:
        if value>16.67:slow+=1
        if value>33.33:very_slow+=1
    print("STREAM_BENCHMARK ",name," frames=",samples.size()," median_ms=",samples[samples.size()/2]," p95_ms=",samples[int(samples.size()*.95)]," p99_ms=",samples[int(samples.size()*.99)]," max_ms=",samples[-1]," over_16ms=",slow," over_33ms=",very_slow," max_upload_ms=",game.get_debug_state().max_upload_ms)
    var s: Dictionary=game.get_debug_state()
    print("UPLOAD_PHASES mesh_ms=",s.max_mesh_upload_ms," collider_ms=",s.max_collider_ms)
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
    game.start_world("Streaming frame-time benchmark",1337)
    actor=game.find_children("*","FlyingPlayer",true,false)[0]
    camera=actor.find_children("*","Camera3D",true,false)[0]
    actor.set_physics_process(false)
    actor.set_process_unhandled_input(false)
    Input.mouse_mode=Input.MOUSE_MODE_VISIBLE
    root.grab_focus()
    game.execute_command("time pause")
    game.execute_command("time 8")
    aim(Vector3(256,100,128),Vector3(500,82,0))
    await settle()
    await create_timer(2).timeout
    var samples: Array[float]=[]
    var previous:=Time.get_ticks_usec()
    for i in range(720):
        actor.position+=Vector3(.65,0,.18)
        await process_frame
        var now:=Time.get_ticks_usec()
        samples.append((now-previous)/1000.0)
        previous=now
    report("fast_flight",samples)
    await settle()
    var edit_cells: Array[Vector3i]=[]
    var maximum_top: int=-64
    for x in range(256,260):
        for y in range(319,-63,-1):
            if game.get_voxel(x,y,128) in [1,2,3,4,6,8]:
                edit_cells.append(Vector3i(x,y,128))
                edit_cells.append(Vector3i(x,y-1,128))
                maximum_top=maxi(maximum_top,y)
                break
    check(edit_cells.size()==8,"Benchmark has eight real natural source cells")
    aim(Vector3(256,maximum_top+8,140),Vector3(258,maximum_top,128))
    await settle()
    await create_timer(1).timeout
    samples.clear()
    previous=Time.get_ticks_usec()
    var edits:=0
    for i in range(360):
        if i%45==0 and edits<edit_cells.size():
            var p:=edit_cells[edits]
            check(game.get_voxel(p.x,p.y,p.z)>0 and game.set_voxel(p.x,p.y,p.z,0),"Benchmark mines a real solid source")
            edits+=1
        await process_frame
        var now:=Time.get_ticks_usec()
        samples.append((now-previous)/1000.0)
        previous=now
    print("STREAM_ACTUAL_EDITS ",edits)
    check(edits==8,"Eight source edits completed")
    report("block_edits",samples)
    print("STREAM_BENCHMARK_PASS" if failures.is_empty() else "STREAM_BENCHMARK_FAIL "+str(failures))
    quit(0 if failures.is_empty() else 1)
