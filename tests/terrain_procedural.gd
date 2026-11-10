extends "res://tests/cartoon_runtime.gd"
var frame_ms: Array[float]=[]
var last_usec:=0
func _process(_delta: float) -> bool:
    var now:=Time.get_ticks_usec()
    if last_usec>0:frame_ms.append((now-last_usec)/1000.0)
    last_usec=now
    return false
func run() -> void:
    game=load("res://scenes/main.tscn").instantiate()
    root.add_child(game)
    await process_frame
    check(game.start_world("Procedural Terrain",1337),"Start procedural world")
    find_player()
    game.execute_command("time pause")
    actor.position=Vector3(0,225,0)
    await settle()
    var top: int=-999
    for y in range(200,-60,-1):
        var id: int=game.get_voxel(0,y,0)
        if id>0 and id!=4 and id!=6 and id!=7:top=y;break
    check(top>-60,"Find an original natural terrain source")
    actor.position=Vector3(0,top+8,0)
    frame_ms.clear()
    await settle()
    await physics_frame
    var s: Dictionary=game.get_debug_state()
    print("PROCEDURAL_TERRAIN sources=",s.terrain_resident_sources," candidates=",s.terrain_natural_cells," patches=",s.terrain_patches," triangles=",s.terrain_triangles," worker_max_ms=",s.terrain_max_build_ms," upload_max_ms=",s.terrain_max_upload_ms," refresh_max_ms=",s.terrain_max_refresh_ms)
    check(s.terrain_resident_sources>20 and s.terrain_triangles>100,"Procedural world uses the same live Transvoxel patches")
    var origin:=Vector3(.5,top+8,.5)
    var hit: Dictionary=game.terrain_hit(origin,Vector3.DOWN,20)
    check(not hit.is_empty(),"Natural smooth terrain is selectable through the common mesh path")
    if not hit.is_empty():
        var cell: Vector3i=hit.cell
        var block: int=game.get_voxel(cell.x,cell.y,cell.z)
        check(game.get_voxel_appearance(cell.x,cell.y,cell.z).terrain,"Generated source inherits Terrain")
        check(game.execute_command("terrain at %d %d %d off"%[cell.x,cell.y,cell.z])=="Terrain modifier updated.","Same tool removes generated Terrain")
        await settle()
        check(not game.get_voxel_appearance(cell.x,cell.y,cell.z).terrain and game.get_voxel(cell.x,cell.y,cell.z)==block,"Explicit off restores the original natural block")
        check(game.save_world(),"Save procedural override")
        var path: String=game.get_world_path()
        check(game.open_world(path),"Reload procedural override")
        find_player()
        await settle()
        check(not game.get_voxel_appearance(cell.x,cell.y,cell.z).terrain,"Explicit off prevails over generation after reload")
        check(game.execute_command("terrain at %d %d %d on"%[cell.x,cell.y,cell.z])=="Terrain modifier updated.","Same tool re-applies natural modifier")
        await settle()
        var near: int=game.get_debug_state().terrain_resident_sources
        actor.position=Vector3(800,300,800)
        await settle()
        check(game.get_debug_state().terrain_resident_sources<near,"Flight evicts distant smooth geometry")
        actor.position=Vector3(0,top+8,0)
        await settle()
        check(game.get_voxel_appearance(cell.x,cell.y,cell.z).terrain,"Returning keeps the saved source modifier")
        check(not game.terrain_hit(origin,Vector3.DOWN,20).is_empty(),"Returning reconstructs the natural mesh")
        check(game.set_voxel(cell.x,cell.y,cell.z,0),"Mining a natural modified block returns success")
        check(not game.get_voxel_appearance(cell.x,cell.y,cell.z).terrain,"Mining persists the modifier reset on air")
        check(not game.paint_voxel(cell.x,cell.y,cell.z,"#ff0000",.5,1.0),"Unrelated paint changes on air remain rejected")
    frame_ms.sort()
    if not frame_ms.is_empty():print("TERRAIN_FRAME_MS samples=",frame_ms.size()," p50=",frame_ms[int(frame_ms.size()*.5)]," p95=",frame_ms[int(frame_ms.size()*.95)]," p99=",frame_ms[int(frame_ms.size()*.99)]," max=",frame_ms.back())
    frame_ms.clear()
    await create_timer(2).timeout
    frame_ms.sort()
    print("TERRAIN_IDLE_FRAME_MS p95=",frame_ms[int(frame_ms.size()*.95)]," p99=",frame_ms[int(frame_ms.size()*.99)]," max=",frame_ms.back())
    print("TERRAIN_PROCEDURAL_PASS" if failures.is_empty() else "TERRAIN_PROCEDURAL_FAIL "+str(failures))
    quit(0 if failures.is_empty() else 1)
