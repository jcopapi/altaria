extends "res://tests/cartoon_runtime.gd"
func run() -> void:
    game=load("res://scenes/main.tscn").instantiate()
    root.add_child(game)
    await process_frame
    check(game.start_world("Large Terrain construction",1337),"Open scale fixture")
    find_player()
    actor.position=Vector3(-5,240,5)
    await settle()
    var ids: Array[int]=[]
    # 1,824 original cubes, modified through the same construction API.
    for x in range(32):
        for z in range(8):
            for y in range(mini(x+1,8)):
                var id: int=game.place_block(Vector3(x,220+y,z),3,"terrain",0)
                check(id>0,"Place source cube in large connected structure")
                ids.append(id)
    await settle()
    var s: Dictionary=game.get_debug_state()
    check(s.placed_blocks==1824 and s.terrain_triangles>1000,"Large source structure reaches the live renderer")
    var untouched_uploads: int=s.terrain_uploads
    # One local change must not rebuild all structure patches.
    check(game.terrain_built(ids[0],0),"Remove one local modifier")
    await settle()
    s=game.get_debug_state()
    var local_uploads: int=s.terrain_uploads-untouched_uploads
    check(local_uploads<=8,"Local change rebuilds no more than eight neighboring patches")
    check(game.get_built_blocks().size()==1824,"All source identities survive local rebuild")
    print("TERRAIN_SCALE sources=",s.placed_blocks," patches=",s.terrain_patches," triangles=",s.terrain_triangles," local_uploads=",local_uploads," upload_max_ms=",s.terrain_max_upload_ms," worker_max_ms=",s.terrain_max_build_ms)
    print("TERRAIN_SCALE_PASS" if failures.is_empty() else "TERRAIN_SCALE_FAIL "+str(failures))
    quit(0 if failures.is_empty() else 1)
