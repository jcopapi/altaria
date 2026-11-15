extends SceneTree
# Frame-by-frame capture of first-person hand animations (swing, place, climb).
# Run without --headless; PNGs land in $HUD_CAPTURE_DIR (default /tmp/altaria-hands).

var game: Node
var player: Node
var out_dir := "/tmp/altaria-hands"

func _initialize() -> void:
	var env_dir := OS.get_environment("HUD_CAPTURE_DIR")
	if env_dir != "":
		out_dir = env_dir
	run.call_deferred()

func close_console() -> void:
	var console = game.get_console()
	if console and console.is_enabled():
		console.set_enabled(false)

func capture(name: String) -> void:
	close_console()
	await RenderingServer.frame_post_draw
	DirAccess.make_dir_recursive_absolute(out_dir)
	root.get_texture().get_image().save_png(out_dir.path_join(name + ".png"))
	print("CAPTURED ", name)

func wait(t: float) -> void:
	await create_timer(t).timeout

func mouse(button: MouseButton, pressed: bool) -> void:
	var e := InputEventMouseButton.new()
	e.button_index = button
	e.pressed = pressed
	Input.parse_input_event(e)

func key(code: Key, pressed: bool) -> void:
	var e := InputEventKey.new()
	e.keycode = code
	e.physical_keycode = code
	e.pressed = pressed
	Input.parse_input_event(e)

func swing_sequence(prefix: String, times: Array) -> void:
	mouse(MOUSE_BUTTON_LEFT, true)
	var elapsed := 0.0
	for t in times:
		await wait(float(t) - elapsed)
		elapsed = float(t)
		await capture("%s_%03d" % [prefix, int(float(t) * 1000.0)])
	mouse(MOUSE_BUTTON_LEFT, false)
	await wait(0.8)

func run() -> void:
	game = load("res://scenes/main.tscn").instantiate()
	root.add_child(game)
	await process_frame
	game.start_world("Hand capture", 4242)
	for i in range(3000):
		await create_timer(0.02).timeout
		var st: Dictionary = game.get_debug_state()
		if st.get("pending", 1) == 0 and st.get("tiles", 0) == st.get("wanted", -1) and st.get("lod3", 0) > 0:
			break
	game.set_debug_overlay(false)
	game.execute_command("time set 10")
	for n in game.find_children("*", "FlyingPlayer", true, false):
		player = n
	await wait(1.0)
	close_console()
	var mode := OS.get_environment("HAND_MODE")
	game.select_hotbar_slot(0)
	await wait(0.6)
	if mode == "idle":
		player.set_view(player.rotation.y, 0.1)
		await wait(0.5)
		var hands: Node = game.find_children("*", "", true, false).filter(func(n): return n.name == "FirstPersonHands")[0]
		if OS.get_environment("HAND_HIDE") == "1":
			for n in hands.find_children("*", "Sprite2D", true, false):
				if n.name != "ToolDisplay":
					n.visible = false
		await capture("idle_tool")
		var variants := OS.get_environment("HAND_VARIANTS")
		if variants != "":
			var vi := 0
			for v in variants.split(";"):
				var f := v.split(",")
				hands.pickaxe_angles = Vector3(float(f[0]), float(f[1]), float(f[2]))
				hands.axe_angles = hands.pickaxe_angles
				hands.TOOL_SCREEN_ROT = float(f[3])
				game.select_hotbar_slot(0)
				await wait(0.6)
				hands._update_sprites()
				await wait(0.3)
				await capture("variant_%d" % vi)
				game.select_hotbar_slot(1)
				await wait(0.6)
				await capture("variant_axe_%d" % vi)
				vi += 1
			game.select_hotbar_slot(0)
		game.collect_block_to_hotbar(1, 16)
		game.select_hotbar_slot(2)
		await wait(0.7)
		await capture("idle_block")
		game.select_hotbar_slot(0)
		for i in range(8):
			await wait(0.05)
			await capture("switch_%02d" % i)
		await wait(0.5)
		game.select_hotbar_slot(4)
		await wait(0.7)
		await capture("idle_empty")
	if mode == "" or mode == "swing":
		player.set_view(player.rotation.y, -1.0)
		await wait(0.3)
		await swing_sequence("near", [0.06, 0.12, 0.17, 0.20, 0.215, 0.25, 0.31, 0.40, 0.50])
		player.set_view(player.rotation.y, -0.72)
		await wait(0.3)
		await swing_sequence("far", [0.17, 0.215, 0.26])
		game.select_hotbar_slot(4)
		await wait(0.4)
		player.set_view(player.rotation.y, -1.0)
		await wait(0.3)
		await swing_sequence("fist", [0.12, 0.215, 0.30])
	if mode == "" or mode == "place":
		game.collect_block_to_hotbar(1, 16)
		game.select_hotbar_slot(2)
		await wait(0.5)
		player.set_view(player.rotation.y, -0.8)
		await wait(0.3)
		mouse(MOUSE_BUTTON_RIGHT, true)
		await wait(0.06)
		await capture("place_060")
		await wait(0.08)
		await capture("place_140")
		mouse(MOUSE_BUTTON_RIGHT, false)
		await wait(0.3)
	if mode == "" or mode == "climb":
		game.select_hotbar_slot(4)
		player.set_view(0.0, 0.0)
		await wait(0.3)
		var p: Vector3 = player.global_position
		var bx := int(floor(p.x))
		var bz := int(floor(p.z)) - 2
		var by := int(floor(p.y))
		for y in range(by - 1, by + 12):
			for x in range(bx - 2, bx + 3):
				game.set_voxel(x, y, bz, 1)
		await wait(0.6)
		key(KEY_W, true)
		await wait(0.25)
		key(KEY_SPACE, true)
		await wait(0.12)
		key(KEY_SPACE, false)
		for i in range(14):
			await wait(0.13)
			await capture("climb_%02d" % i)
		key(KEY_W, false)
	print("HAND_CAPTURE_DONE")
	quit()
