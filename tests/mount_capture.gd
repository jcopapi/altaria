extends SceneTree
# Princess Bee mount: spawn as entity, mount/dismount, fly with the real key bindings.
# Run without --headless: ~/.local/bin/gdstudio --path . -s res://tests/mount_capture.gd
# PNGs go to $HUD_CAPTURE_DIR (default /tmp/altaria-mount).

var game: Node
var player: Node3D
var out_dir := "/tmp/altaria-mount"
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

func key(code: Key, pressed: bool) -> void:
	var ev := InputEventKey.new()
	ev.physical_keycode = code
	ev.keycode = code
	ev.pressed = pressed
	Input.parse_input_event(ev)

func hold(codes: Array, seconds: float) -> void:
	for c in codes:
		key(c, true)
	var t := 0.0
	while t < seconds:
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
		await process_frame
		t += get_root().get_process_delta_time()
	for c in codes:
		key(c, false)
	await process_frame

func bee() -> Dictionary:
	var list: Array = game.get_creature_positions("altaria:princess_bee")
	return list[0] if list.size() > 0 else {}

func run() -> void:
	game = load("res://scenes/main.tscn").instantiate()
	root.add_child(game)
	await process_frame
	game.start_world("Mount", 4242)
	for i in range(3000):
		await create_timer(0.02).timeout
		var st: Dictionary = game.get_debug_state()
		if st.get("pending", 1) == 0 and st.get("tiles", 0) == st.get("wanted", -1) and st.get("lod3", 0) > 0:
			break
	game.get_console().set_enabled(false)
	game.set_debug_overlay(false)
	player = game.find_children("*", "FlyingPlayer", true, false)[0]
	player.set_view(0.0, -0.25)
	Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
	await create_timer(0.3).timeout

	# 1. Summon as a wandering entity, it hovers above the grass.
	print("CMD ", game.execute_command("spawn bee"))
	await create_timer(2.5).timeout
	var b := bee()
	check(not b.is_empty(), "spawn bee creates a Princess Bee entity")
	await capture("bee_entity")

	# 2. Walk up and mount it with the (no-arg) mount, same as pressing E.
	player.position = b.position + Vector3(0, 0.2, 2.5)
	await create_timer(0.2).timeout
	print("CMD ", game.execute_command("mount"))
	check(game.get_debug_state().riding == "altaria:princess_bee", "mount rides the nearby bee")
	await process_frame
	await process_frame
	b = bee()
	check(player.position.distance_to(b.position) < 2.5 and player.position.y > b.position.y + 1.0, "rider sits on the saddle")

	# 3. Fly with the real bindings: W + Space climbs forward, Shift sprints.
	var start: Vector3 = player.position
	await hold([KEY_W, KEY_SPACE], 1.5)
	var climbed: Vector3 = player.position
	check(climbed.y > start.y + 3.0, "Space climbs (dy=%.1f)" % (climbed.y - start.y))
	check(Vector2(climbed.x - start.x, climbed.z - start.z).length() > 4.0, "W flies forward")
	player.set_view(player.rotation.y, -0.35)
	await capture("bee_riding")
	var before_sprint: Vector3 = player.position
	await hold([KEY_W], 1.0)
	var walk_dist := Vector2(player.position.x - before_sprint.x, player.position.z - before_sprint.z).length()
	var before_fast: Vector3 = player.position
	await hold([KEY_W, KEY_SHIFT], 1.0)
	var sprint_dist := Vector2(player.position.x - before_fast.x, player.position.z - before_fast.z).length()
	check(sprint_dist > walk_dist * 1.4, "Shift sprints (%.1f vs %.1f m/s)" % [sprint_dist, walk_dist])

	# 4. Ctrl descends until the bee touches the ground (no falling through).
	var high: float = player.position.y
	await hold([KEY_CTRL], 4.0)
	check(player.position.y < high - 3.0, "Ctrl descends")
	b = bee()
	check(b.on_ground and b.position.y > start.y - 6.0, "bee lands on the terrain (y=%.1f)" % b.position.y)
	await capture("bee_landed")

	# 5. Double-tap Ctrl to dismount.
	key(KEY_CTRL, true); await process_frame; key(KEY_CTRL, false)
	for i in 4: await process_frame
	key(KEY_CTRL, true); await process_frame; key(KEY_CTRL, false)
	await process_frame
	check(game.get_debug_state().riding == "", "double-tap Ctrl dismounts")
	await create_timer(1.0).timeout

	# 6. `mount princess bee` summons a second bee already mounted.
	print("CMD ", game.execute_command("mount princess bee"))
	check(game.get_debug_state().riding == "altaria:princess_bee", "mount <name> summons it already ridden")
	check(game.get_creature_count("altaria:princess_bee") == 2, "two bees exist")
	print("CMD ", game.execute_command("dismount"))
	check(game.get_debug_state().riding == "", "dismount command")
	print("MOUNT_CAPTURE_PASS" if failures.is_empty() else "MOUNT_CAPTURE_FAIL " + str(failures))
	quit(0 if failures.is_empty() else 1)
