extends SceneTree
var game: Node
func _initialize() -> void:
    run.call_deferred()
func settle() -> void:
    for i in range(1500):
        await process_frame
        var s: Dictionary=game.get_debug_state()
        if s.wanted>0 and s.pending==0 and s.tiles==s.wanted:return
    push_error("Benchmark streaming timeout")
func measure(label: String) -> void:
    await settle()
    await create_timer(2.0).timeout
    var samples: Array[float]=[]
    var previous:=Time.get_ticks_usec()
    for i in range(180):
        await process_frame
        var now:=Time.get_ticks_usec()
        samples.append((now-previous)/1000.0)
        previous=now
    samples.sort()
    print("RENDER_BENCHMARK view=",label," median_ms=",samples[90]," p95_ms=",samples[171]," fps=",Engine.get_frames_per_second()," stats=",game.get_debug_state())
    await RenderingServer.frame_post_draw
    DirAccess.make_dir_recursive_absolute("/tmp/altaria-foliage-preview")
    root.get_texture().get_image().save_png("/tmp/altaria-foliage-preview/benchmark_"+label+".png")
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
    game.start_world("Render benchmark",1337)
    var actor: Node3D=game.find_children("*","FlyingPlayer",true,false)[0]
    var camera: Camera3D=actor.find_children("*","Camera3D",true,false)[0]
    actor.set_physics_process(false)
    actor.set_process_unhandled_input(false)
    Input.mouse_mode=Input.MOUSE_MODE_VISIBLE
    root.grab_focus()
    await measure("horizon")
    var ground:=43.0
    for y in range(300,-63,-1):
        if game.get_voxel(256,y,128) not in [0,5,9,10]:
            ground=y+(.5 if game.get_voxel_appearance(256,y,128).shape=="slab" else 1.0)
            break
    actor.position=Vector3(256.5,ground+.25,128.5)
    actor.rotation=Vector3.ZERO
    camera.rotation=Vector3(-.2,0,0)
    await measure("dense_grass")
    print("RENDER_BENCHMARK_PASS")
    quit()
