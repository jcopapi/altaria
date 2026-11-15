extends "res://tests/cartoon_runtime.gd"

func run() -> void:
    game=load("res://scenes/main.tscn").instantiate()
    root.add_child(game)
    await process_frame
    check(game.start_world("Teleport and collider test",1337),"Load cave exploration")
    find_player()
    actor.position=Vector3(32,259,32)
    await create_timer(.35).timeout
    await settle()
    var before: Dictionary=game.get_debug_state()
    check(before.terrain_colliders==0,"Startup requests zero terrain colliders")
    for x in range(28,38):
        for z in range(28,38):
            check(game.set_voxel(x,250,z,1),"Place platform spanning tile boundaries")
    check(game.set_voxel(32,256,32,1),"Place falling block at tile corner")
    check(game.release_voxel(32,256,32,false),"Start Jolt block")
    await create_timer(.35).timeout
    await settle()
    await create_timer(2).timeout
    var dropped: Dictionary=game.get_debug_state()
    check(dropped.terrain_colliders>=4 and dropped.terrain_colliders<dropped.lod0,"Only the four required nearby Jolt tiles are requested")
    check(abs(dropped.physics_positions[0].y-251)<.15,"Corner block lands on the platform")
    actor.position=Vector3(16000,210,16000)
    await process_frame
    await process_frame
    await process_frame
    var jumped: Dictionary=game.get_debug_state()
    check(jumped.voxel_bytes<before.voxel_bytes/2,"Long jump immediately drops stale resident voxel volumes")
    for node in game.find_children("*","StaticBody3D",true,false):
        if node.get_parent().name.begins_with("Terrain_") and node.global_position.length()<100:
            check(node.collision_layer==0 and node.collision_mask==0,"Queued old terrain node has no physics interaction")
    print("EXPLORATION_TELEPORT before_voxels=",before.voxel_bytes," after_voxels=",jumped.voxel_bytes," requested_colliders=",dropped.terrain_colliders)
    root.remove_child(game)
    game.queue_free()
    for i in range(4):await process_frame
    print("EXPLORATION_TELEPORT_PASS" if failures.is_empty() else "EXPLORATION_TELEPORT_FAIL "+str(failures))
    quit(0 if failures.is_empty() else 1)
