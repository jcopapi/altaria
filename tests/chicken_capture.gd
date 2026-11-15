extends SceneTree
# Spawns chickens in front of the player, lets them wander, captures PNGs to $HUD_CAPTURE_DIR
# and checks they stay grounded (no falling through, no floating).
# Run without --headless: ~/.local/bin/gdstudio --path . -s res://tests/chicken_capture.gd

var game: Node
var out_dir := "/tmp/altaria-chicken"
var failures: Array[String] = []

func _initialize() -> void:
	var env_dir := OS.get_environment("HUD_CAPTURE_DIR")
	if env_dir != "":
		out_dir = env_dir
	run.call_deferred()

func check(ok: bool, what: String) -> void:
	print(("PASS " if ok else "FAIL ") + what)
	if not ok:
		failures.append(what)

func capture(name: String) -> void:
	var console = game.get_console()
	if console and console.is_enabled():
		console.set_enabled(false)
	await RenderingServer.frame_post_draw
	DirAccess.make_dir_recursive_absolute(out_dir)
	root.get_texture().get_image().save_png(out_dir.path_join(name + ".png"))
	print("CAPTURED ", name)

func grounded_report(label: String) -> Dictionary:
	var list: Array = game.get_creature_positions("altaria:chicken")
	var grounded := 0
	var lowest := INF
	for c in list:
		grounded += 1 if c.on_ground else 0
		lowest = minf(lowest, c.position.y)
	print(label, " chickens=", list.size(), " grounded=", grounded, " lowest_y=", lowest)
	return {"count": list.size(), "grounded": grounded, "lowest": lowest}

func run() -> void:
	game = load("res://scenes/main.tscn").instantiate()
	root.add_child(game)
	await process_frame
	game.start_world("Chickens", int(OS.get_environment("CHICKEN_SEED")) if OS.get_environment("CHICKEN_SEED") != "" else 4242)
	for i in range(3000):
		await create_timer(0.02).timeout
		var st: Dictionary = game.get_debug_state()
		if st.get("pending", 1) == 0 and st.get("tiles", 0) == st.get("wanted", -1) and st.get("lod3", 0) > 0:
			break
	game.get_console().set_enabled(false)
	game.set_debug_overlay(false)
	var player: Node3D = game.find_children("*", "FlyingPlayer", true, false)[0]
	player.set_view(0.0, -0.45)
	await create_timer(0.5).timeout
	print("CMD ", game.execute_command("spawn chicken 6"))
	print("CMD ", game.execute_command("spawn altaria:chicken"))
	print("CMD ", game.execute_command("spawn pig 3"))
	await create_timer(0.6).timeout
	var start := grounded_report("t=0.6s")
	check(start.count >= 5, "spawn chicken places chickens")
	await capture("chicken_spawn")
	await create_timer(4.0).timeout
	await capture("chicken_wander_4s")
	var mid := grounded_report("t=4.6s")
	await create_timer(8.0).timeout
	await capture("chicken_wander_12s")
	var late := grounded_report("t=12.6s")
	check(late.count >= start.count, "no chicken lost (fell through / despawned)")
	check(late.lowest > start.lowest - 6.0, "chickens did not fall through the terrain")
	check(late.grounded >= late.count - 1, "chickens stand on the ground (not floating)")
	var pigs: Array = game.get_creature_positions("altaria:pig")
	var pigs_grounded := 0
	for p in pigs:
		pigs_grounded += 1 if p.on_ground else 0
	print("pigs=", pigs.size(), " grounded=", pigs_grounded)
	check(pigs.size() >= 3 and pigs_grounded >= pigs.size() - 1, "pigs spawn and stay grounded")
	var st: Dictionary = game.get_debug_state()
	print("DEBUG creatures ", st.get("creatures"))
	print("CMD ", game.execute_command("spawn clear"))
	check(game.get_creature_count() == 0, "spawn clear removes creatures")
	print("CHICKEN_CAPTURE_PASS" if failures.is_empty() else "CHICKEN_CAPTURE_FAIL " + str(failures))
	quit(0 if failures.is_empty() else 1)
