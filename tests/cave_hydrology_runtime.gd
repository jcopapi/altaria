extends "res://tests/cartoon_runtime.gd"

func run() -> void:
    game=load("res://scenes/main.tscn").instantiate()
    root.add_child(game)
    await process_frame
    check(game.start_world("Cave hydrology test",1337),"Start hydrology fixture")
    find_player()
    actor.position=Vector3(-111.5,120,83.5)
    await settle()
    var x: int=-112
    var z: int=83
    var probe: Dictionary=game.get_cave_environment(Vector3(x+.5,-200,z+.5))
    check(probe.get("loaded",false) and probe.get("basin_source","")=="lake","Worker basin loaded in real Godot world")
    var surface: int=probe.get("fluid_surface_y",-165)
    var wet: Dictionary=game.get_cave_environment(Vector3(x+.5,surface-2.5,z+.5))
    var air: Dictionary=game.get_cave_environment(Vector3(x+.5,surface+.5,z+.5))
    check(wet.get("fluid","")=="water" and wet.oxygen_factor==0 and wet.pressure_kpa>120,"Loaded underwater sample exposes immersion, pressure and oxygen")
    check(air.get("fluid","")=="dry" and air.pressure_kpa<102,"Air pocket shares the cave with water")
    check(game.get_voxel(x,surface-1,z)==5 and game.get_voxel(x,surface,z)==0,"Lake water and air are real voxels")
    check(wet.get("river_courses",0)>0,"Basin has a flowing gallery connecting water pockets")
    var ground:=surface-2
    while ground>=-448 and game.get_voxel(x,ground,z)==5:ground-=1
    check(ground>=-448 and game.get_voxel(x,ground,z)!=0,"Find rock below lake")
    if ground>=-448:
        actor.position=Vector3(x+6,surface+4,z)
        check(game.set_voxel(x,surface+2,z,1),"Place drop fixture above water")
        check(game.release_voxel(x,surface+2,z,false),"Release drop into lake")
        await settle()
        await create_timer(2.0).timeout
        var positions: Array=game.get_debug_state().physics_positions
        print("CAVE_WATER_JOLT surface=",surface," rock=",ground," body=",positions)
        check(positions.size()==1 and abs(positions[0].y-ground-1)<.16,"Jolt body passes water mesh and lands on rock")
        game.execute_command("physics clear")
        check(game.paint_voxel(x,ground,z,"#aabbcc",.4,1.0),"Paint submerged rock")
        check(game.set_voxel(x,surface-1,z,0),"Edit a reservoir voxel")
        await settle()
        var pos:=Vector3(x+.5,surface-1+.5,z+.5)
        check(game.get_voxel(x,surface-1,z)==0 and game.get_voxel(x,surface-2,z)==5,"Reservoir stays settled around an edit")
        check(game.get_cave_environment(pos).fluid=="dry","Authoritative edit removes static provenance")
        actor.position=Vector3(3300,140,3300)
        # Wait for the streaming tick to retire the original chunk before
        # relying on the queue status of the new view.
        for i in range(1800):
            if not game.get_cave_environment(pos).loaded:break
            await create_timer(.02).timeout
        check(not game.get_cave_environment(pos).loaded,"Reservoir chunk was evicted")
        await settle()
        actor.position=pos+Vector3(0,3,0)
        for i in range(1800):
            if game.get_cave_environment(pos).loaded:break
            await create_timer(.02).timeout
        await settle()
        check(game.get_cave_environment(pos).loaded,"Reservoir chunk was regenerated")
        check(game.get_voxel(x,surface-1,z)==0 and game.get_voxel(x,surface-2,z)==5,"Pool and edit survive chunk eviction")
        check(game.get_voxel_appearance(x,ground,z).painted,"Submerged paint survives chunk eviction")
    root.remove_child(game)
    game.queue_free()
    for i in range(4):await process_frame
    print("CAVE_HYDRO_RUNTIME_PASS" if failures.is_empty() else "CAVE_HYDRO_RUNTIME_FAIL "+str(failures))
    quit(0 if failures.is_empty() else 1)
