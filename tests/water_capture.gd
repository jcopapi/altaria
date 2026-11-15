extends SceneTree
# Digs a stepped pool (1..20 m deep), fills it with water and captures it to $HUD_CAPTURE_DIR.

var game: Node
var out_dir := "/tmp/altaria-water"

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
	game.start_world("Water", 4242)
	for i in range(3000):
		await create_timer(0.02).timeout
		var st: Dictionary = game.get_debug_state()
		if st.get("pending", 1) == 0 and st.get("tiles", 0) == st.get("wanted", -1) and st.get("lod3", 0) > 0:
			break
	game.set_debug_overlay(false)
	game.execute_command("time set 11")
	var player: Node3D = game.find_children("*", "FlyingPlayer", true, false)[0]
	var p := player.global_position
	var sx := int(floor(p.x)) - 10
	var sz := int(floor(p.z)) - 24
	var top := int(floor(p.y)) + 1
	# Columns get deeper from left (1 m) to right (20 m); rim of stone keeps water in.
	for x in range(-1, 22):
		for z in range(-1, 14):
			var rim := x < 0 or x > 20 or z < 0 or z > 12
			var depth := clampi(x, 1, 20)
			for y in range(top - 22, top + 1):
				if rim:
					game.set_voxel(sx + x, y, sz + z, 1)
				elif y > top - 1 - depth and y < top:
					game.set_voxel(sx + x, y, sz + z, 5)
				elif y < top:
					game.set_voxel(sx + x, y, sz + z, 2)
				else:
					game.set_voxel(sx + x, y, sz + z, 0)
	player.set_flying(true)
	await create_timer(2.0).timeout
	player.global_position = Vector3(sx + 10, top + 9, sz + 26)
	player.set_view(0.0, -0.42)
	await create_timer(2.0).timeout
	await capture("water_pool")
	player.global_position = Vector3(sx + 10, top + 22, sz + 6)
	player.set_view(0.0, -1.35)
	await create_timer(1.5).timeout
	await capture("water_top")
	quit()
