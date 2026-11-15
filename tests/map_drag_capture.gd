extends SceneTree
# Opens the full map, drags it around and reports frame times; saves before/after PNGs.
# Run without --headless; PNGs land in $HUD_CAPTURE_DIR (default /tmp/altaria-map).

var game: Node
var out_dir := "/tmp/altaria-map"

func _initialize() -> void:
	var env_dir := OS.get_environment("HUD_CAPTURE_DIR")
	if env_dir != "":
		out_dir = env_dir
	run.call_deferred()

func capture(name: String) -> void:
	await RenderingServer.frame_post_draw
	DirAccess.make_dir_recursive_absolute(out_dir)
	root.get_texture().get_image().save_png(out_dir.path_join(name + ".png"))
	print("CAPTURED ", name)

func measure(n: int) -> String:
	var worst := 0.0
	var total := 0.0
	var last := Time.get_ticks_usec()
	for i in range(n):
		await process_frame
		var now := Time.get_ticks_usec()
		var ft := (now - last) / 1000.0
		last = now
		worst = maxf(worst, ft)
		total += ft
	return "avg=%.2fms worst=%.2fms" % [total / n, worst]

func run() -> void:
	game = load("res://scenes/main.tscn").instantiate()
	root.add_child(game)
	await process_frame
	game.start_world("Map drag", 4242)
	for i in range(3000):
		await create_timer(0.02).timeout
		var st: Dictionary = game.get_debug_state()
		if st.get("pending", 1) == 0 and st.get("tiles", 0) == st.get("wanted", -1) and st.get("lod3", 0) > 0:
			break
	game.set_debug_overlay(false)
	var console = game.get_console()
	if console and console.is_enabled():
		console.set_enabled(false)
	await create_timer(1.0).timeout
	print("BASE_CLOSED ", await measure(90))
	game.toggle_full_map()
	await create_timer(0.6).timeout
	await capture("map_open")
	print("BASE_OPEN_IDLE ", await measure(90))
	var center := Vector2(root.get_visible_rect().size) * 0.5
	var press := InputEventMouseButton.new()
	press.button_index = MOUSE_BUTTON_LEFT
	press.pressed = true
	press.position = center
	Input.parse_input_event(press)
	var worst := 0.0
	var total := 0.0
	var frames := 0
	var last := Time.get_ticks_usec()
	for i in range(120):
		var mm := InputEventMouseMotion.new()
		mm.relative = Vector2(6.0 * cos(i * 0.05), 4.0)
		mm.position = center
		Input.parse_input_event(mm)
		await process_frame
		var now := Time.get_ticks_usec()
		var ft := (now - last) / 1000.0
		last = now
		worst = maxf(worst, ft)
		total += ft
		frames += 1
	await capture("map_dragging")
	press = press.duplicate()
	press.pressed = false
	Input.parse_input_event(press)
	await create_timer(0.5).timeout
	await capture("map_released")
	print("DRAG_FRAMES avg=%.2fms worst=%.2fms" % [total / frames, worst])
	quit()
