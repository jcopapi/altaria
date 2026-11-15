extends SceneTree
# Visual capture of the gameplay HUD (hotbar, minimap, crosshair, hands, break fx).
# Run without --headless; PNGs land in $HUD_CAPTURE_DIR (default /tmp/altaria-hud).

var game: Node
var out_dir := "/tmp/altaria-hud"

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
	print("CAPTURED ", name, " t=", Time.get_ticks_msec())

func wait_for_terrain() -> void:
	for i in range(6000):
		await create_timer(0.02).timeout
		var state: Dictionary = game.get_debug_state()
		if i % 100 == 0:
			print("WAIT ", i, " ", state.get("pending", -1), " ", state.get("tiles", -1), "/", state.get("wanted", -1))
		if state.get("pending", 1) == 0 and state.get("tiles", 0) == state.get("wanted", -1) and state.get("lod3", 0) > 0:
			return

func key(code: Key, pressed: bool) -> void:
	var e := InputEventKey.new()
	e.keycode = code
	e.physical_keycode = code
	e.pressed = pressed
	Input.parse_input_event(e)

func mouse(button: MouseButton, pressed: bool) -> void:
	var e := InputEventMouseButton.new()
	e.button_index = button
	e.pressed = pressed
	Input.parse_input_event(e)

func wait(t: float) -> void:
	await create_timer(t).timeout

func run() -> void:
	game = load("res://scenes/main.tscn").instantiate()
	root.add_child(game)
	await process_frame
	game.start_world("HUD capture", 4242)
	await wait_for_terrain()
	game.set_debug_overlay(false)
	game.execute_command("time set 10")
	await wait(1.5)
	close_console()
	await capture("01_hud_idle")
	game.select_hotbar_slot(0)
	await wait(0.5)
	await capture("02_pickaxe")
	game.collect_block_to_hotbar(1, 32)
	game.collect_block_to_hotbar(3, 12)
	await wait(0.12)
	await capture("02b_pickup")
	await wait(0.18)
	game.select_hotbar_slot(2)
	await wait(0.6)
	await capture("03_block_selected")
	game.select_hotbar_slot(0)
	await wait(0.3)
	# look down at the ground in front so hits land on a block
	var player = game.get_child(0).find_child("*", true, false) if false else null
	for n in game.find_children("*", "FlyingPlayer", true, false):
		player = n
	if player:
		player.set_view(player.rotation.y, -0.9)
	await wait(0.2)
	mouse(MOUSE_BUTTON_LEFT, true)
	await wait(0.24)
	await capture("04_swing")
	await wait(0.03)
	await capture("04b_impact")
	await wait(0.9)
	await capture("05_break_stage")
	mouse(MOUSE_BUTTON_LEFT, false)
	await wait(0.4)
	key(KEY_ALT, true)
	await wait(0.35)
	game.select_hotbar_slot(2)
	await wait(0.35)
	await capture("06_alt_menu")
	key(KEY_ALT, false)
	await wait(0.3)
	game.toggle_full_map()
	await wait(1.0)
	await capture("07_full_map")
	game.toggle_full_map()
	await wait(4.0)
	await capture("08_idle_dimmed")
	game.execute_command("time set 21")
	await wait(1.0)
	await capture("09_night")
	print("HUD_CAPTURE_DONE")
	quit()
