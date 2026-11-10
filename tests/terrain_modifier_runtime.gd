extends "res://tests/cartoon_runtime.gd"
func run() -> void:
    game=load("res://scenes/main.tscn").instantiate()
    root.add_child(game)
    await process_frame
    check(game.start_world("Independent Terrain journal",1337),"Start Terrain persistence world")
    if not game.get_debug_state().get("world_open",false):
        check(false,"Runtime world opened successfully")
        quit(1)
        return
    find_player()
    actor.position=Vector3(0,240,4)
    game.set_voxel(0,229,0,2)
    game.set_voxel(0,230,0,3)
    game.shape_voxel(0,230,0,"slab",3)
    game.paint_voxel(0,230,0,"#7135a8",.4,1.1)
    check(game.execute_command("terrain at 0 230 0 on")=="Terrain modifier updated.","Apply independent world modifier")
    var a: Dictionary=game.get_voxel_appearance(0,230,0)
    check(a.terrain and a.original_shape=="slab","World modifier retains source shape")
    game.place_block(Vector3(5,229,0),1,"cube",0)
    var piece: int=game.place_block(Vector3(5,230,0),1,"slab",2)
    check(piece>0 and game.terrain_built(piece,1),"Apply Terrain to built piece")
    var b:=block_by_id(piece)
    check(b.terrain and b.original_shape=="slab" and b.rotation==2,"Built modifier retains original form/rotation")
    check(game.save_world(),"Persist S3/B2 modifiers")
    var path: String=game.get_world_path()
    # Mix an original B1 creation record with the new B2 updates. Default
    # modifier state has the original checksum, preserving old save readers.
    var journal_path:=path+"/construction.log"
    var lines:=FileAccess.get_file_as_string(journal_path).split("\n",false)
    var converted:=false
    for i in range(lines.size()):
        var columns:=lines[i].split(" ",false)
        if columns.size()==17 and columns[0]=="B2" and columns[15]=="0":
            columns[0]="B1"
            columns.remove_at(15)
            lines[i]=" ".join(columns)
            converted=true
            break
    check(converted,"Fixture contains a legacy-compatible construction record")
    var file:=FileAccess.open(journal_path,FileAccess.WRITE)
    file.store_string("\n".join(lines)+"\n")
    file.close()
    check(game.open_world(path),"Restore independent modifiers")
    check(game.get_voxel_appearance(0,230,0).terrain,"World modifier restored")
    check(block_by_id(piece).terrain,"Built modifier restored")
    check(game.execute_command("terrain at 0 230 0 off")=="Terrain modifier updated.","Remove world modifier")
    check(game.terrain_built(piece,0),"Remove built modifier")
    a=game.get_voxel_appearance(0,230,0);b=block_by_id(piece)
    check(not a.terrain and a.original_shape=="slab" and a.rotation==3,"World original rotation restored without replacement")
    check(not b.terrain and b.original_shape=="slab" and b.rotation==2,"Built original form and rotation restored")
    game.save_world();game.open_world(path)
    check(not game.get_voxel_appearance(0,230,0).terrain and not block_by_id(piece).terrain,"Removal persists")
    print("TERRAIN_MODIFIER_RUNTIME_PASS" if failures.is_empty() else "TERRAIN_MODIFIER_RUNTIME_FAIL "+str(failures))
    quit(0 if failures.is_empty() else 1)
