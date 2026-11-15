extends SceneTree
# Places a few shaped blocks, runs `hl floor`, captures before/after PNGs to $HUD_CAPTURE_DIR.

var game: Node
var out_dir := "/tmp/altaria-hl"

func _initialize() -> void:
	var env_dir := OS.get_environment("HUD_CAPTURE_DIR")
	if env_dir != "":
		out_dir = env_dir
	run.call_deferred()

func capture(name: String) -> void:
	var console = game.get_console()
	if console and console.is_enabled():
		console.set_enabled(false)
	await RenderingServer.frame_post_draw
	DirAccess.make_dir_recursive_absolute(out_dir)
	root.get_texture().get_image().save_png(out_dir.path_join(name + ".png"))
	print("CAPTURED ", name)

func run() -> void:
	game = load("res://scenes/main.tscn").instantiate()
	root.add_child(game)
	await process_frame
	game.start_world("Highlight", 4242)
	for i in range(3000):
		await create_timer(0.02).timeout
		var st: Dictionary = game.get_debug_state()
		if st.get("pending", 1) == 0 and st.get("tiles", 0) == st.get("wanted", -1) and st.get("lod3", 0) > 0:
			break
	game.set_debug_overlay(false)
	var player: Node3D = game.find_children("*", "FlyingPlayer", true, false)[0]
	player.set_view(0.0, -0.35)
	var p := player.global_position
	var y := floorf(p.y) + 1.0
	for i in range(5):
		game.place_block(Vector3(floorf(p.x) - 2 + i, y, floorf(p.z) - 5), 1, "slab", 0)
		game.place_block(Vector3(floorf(p.x) - 2 + i, y, floorf(p.z) - 7), 1, "stairs", 0)
		game.place_block(Vector3(floorf(p.x) - 2 + i, y + 1, floorf(p.z) - 9), 1, "slab", 0)
	await create_timer(1.0).timeout
	await capture("hl_before")
	print("CMD ", game.execute_command("hl floor"))
	await create_timer(0.5).timeout
	await capture("hl_floor")
	print("CMD ", game.execute_command("hl stairs"))
	await create_timer(0.5).timeout
	await capture("hl_stairs")
	for q in ["ramp", "grass", "floor"]:
		var t0 := Time.get_ticks_msec()
		print("CMD ", game.execute_command("hl " + q), " in ", Time.get_ticks_msec() - t0, " ms")
		await create_timer(0.5).timeout
		await capture("hl_world_" + q)
	print("CMD ", game.execute_command("hl off"))
	quit()
