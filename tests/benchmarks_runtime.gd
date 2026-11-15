extends "res://tests/cartoon_runtime.gd"

func run() -> void:
    print("[BENCHMARK] Starting Altaria Automated Benchmarks...")
    game = load("res://scenes/main.tscn").instantiate()
    root.add_child(game)
    await process_frame

    var results: Dictionary = {}
    var os = OS
    var initial_mem_mb = os.get_static_memory_usage() / (1024 * 1024)

    # -------------------------------------------------------------
    # SCENARIO 1: World Generation (Seed 424242)
    # -------------------------------------------------------------
    print("[BENCHMARK] Scenario 1: Initial World Generation...")
    var t0 = Time.get_ticks_msec()
    check(game.start_world("BenchWorld", 424242), "Start benchmark world")
    find_player()
    await settle()
    var t_gen_total = Time.get_ticks_msec() - t0
    var s: Dictionary = game.get_debug_state()

    var gen_metrics = {
        "scenario": "world_generation",
        "seed": 424242,
        "first_tile_ms": float(s.get("first_tile_ms", 0)),
        "world_ready_ms": float(s.get("world_ready_ms", 0)),
        "total_elapsed_ms": t_gen_total,
        "tiles_loaded": int(s.get("tiles", 0)),
        "triangles": int(s.get("triangles", 0)),
        "voxel_memory_kb": int(s.get("voxel_bytes", 0)) / 1024
    }
    results["world_generation"] = gen_metrics

    # -------------------------------------------------------------
    # SCENARIO 2: Flight Streaming (60 steps along X axis)
    # -------------------------------------------------------------
    print("[BENCHMARK] Scenario 2: High-Speed Flight Streaming...")
    actor.set_flying(true)
    actor.set_speed(80.0)
    var flight_frame_times: Array[float] = []
    for step in range(60):
        var step_start = Time.get_ticks_usec()
        actor.position += Vector3(8.0, 0, 0)
        await create_timer(0.016).timeout
        var step_ms = (Time.get_ticks_usec() - step_start) / 1000.0
        flight_frame_times.append(step_ms)

    flight_frame_times.sort()
    var flight_avg = 0.0
    for ft in flight_frame_times: flight_avg += ft
    flight_avg /= flight_frame_times.size()
    var flight_p95 = flight_frame_times[int(flight_frame_times.size() * 0.95)]
    var flight_p99 = flight_frame_times[int(flight_frame_times.size() * 0.99)]

    var flight_metrics = {
        "scenario": "flight_streaming",
        "sample_count": flight_frame_times.size(),
        "frame_time_avg_ms": flight_avg,
        "frame_time_p95_ms": flight_p95,
        "frame_time_p99_ms": flight_p99,
        "max_frame_time_ms": flight_frame_times.back(),
        "fps_estimate": 1000.0 / flight_avg if flight_avg > 0 else 0
    }
    results["flight_streaming"] = flight_metrics

    # -------------------------------------------------------------
    # SCENARIO 3: Teleport to Uncached Distant Coordinates
    # -------------------------------------------------------------
    print("[BENCHMARK] Scenario 3: Teleport Peak Stress (5000, 200, 5000)...")
    var tp_t0 = Time.get_ticks_msec()
    game.execute_command("tp 5000 200 5000")
    await create_timer(0.2).timeout
    var tp_pending = int(game.get_debug_state().get("pending", 0))
    await settle()
    var tp_time = Time.get_ticks_msec() - tp_t0

    var tp_metrics = {
        "scenario": "teleport_stress",
        "target": [5000, 200, 5000],
        "peak_pending_queue": tp_pending,
        "settle_time_ms": tp_time,
        "tiles_at_destination": int(game.get_debug_state().get("tiles", 0))
    }
    results["teleport_stress"] = tp_metrics

    # -------------------------------------------------------------
    # SCENARIO 4: Chunk Modification & Remesh
    # -------------------------------------------------------------
    print("[BENCHMARK] Scenario 4: Chunk Modification & Remesh...")
    var mod_t0 = Time.get_ticks_usec()
    game.set_voxel(5000, 180, 5000, 3)
    game.shape_voxel(5000, 180, 5000, "slab", 0)
    await create_timer(0.1).timeout
    var mod_time = (Time.get_ticks_usec() - mod_t0) / 1000.0

    var mod_metrics = {
        "scenario": "chunk_modification",
        "modification_time_ms": mod_time,
        "revision": int(game.get_debug_state().get("revision", 0)),
        "edits": int(game.get_debug_state().get("edits", 0))
    }
    results["chunk_modification"] = mod_metrics

    # -------------------------------------------------------------
    # SCENARIO 5: Memory Usage Summary
    # -------------------------------------------------------------
    var final_mem_mb = os.get_static_memory_usage() / (1024 * 1024)
    var peak_mem_mb = os.get_static_memory_peak_usage() / (1024 * 1024)
    var voxel_mem_kb = int(game.get_debug_state().get("voxel_bytes", 0)) / 1024

    var mem_metrics = {
        "scenario": "memory_telemetry",
        "initial_static_mb": initial_mem_mb,
        "final_static_mb": final_mem_mb,
        "peak_static_mb": peak_mem_mb,
        "voxel_bytes_kb": voxel_mem_kb
    }
    results["memory_telemetry"] = mem_metrics

    # Export to JSON
    var json_str = JSON.stringify(results, "  ")
    var out_file = FileAccess.open("docs/performance/caca-as-bruxas/2026-11-10-caca-as-bruxas-auditoria/raw/benchmark_results.json", FileAccess.WRITE)
    if out_file:
        out_file.store_string(json_str)
        out_file.close()

    # Export to CSV
    var csv_file = FileAccess.open("docs/performance/caca-as-bruxas/2026-11-10-caca-as-bruxas-auditoria/raw/benchmark_results.csv", FileAccess.WRITE)
    if csv_file:
        csv_file.store_line("scenario,metric,value")
        for sc in results.keys():
            var d: Dictionary = results[sc]
            for m in d.keys():
                csv_file.store_line(str(sc) + "," + str(m) + "," + str(d[m]))
        csv_file.close()

    print("[BENCHMARK_COMPLETE]")
    print(json_str)
    quit(0)
