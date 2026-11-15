extends SceneTree
# Measures how walkable the generated surface is around spawn: for every pair of
# neighbouring columns, the height difference the player has to climb.
# step <= 0.5 walks, 1.0 needs a jump, >1 is a cliff. Also counts "slab piles":
# slabs whose uphill neighbour is another slab (a slab-only ramp, no full block between).

var game: Node

func _initialize() -> void:
	run.call_deferred()

func run() -> void:
	game = load("res://scenes/main.tscn").instantiate()
	root.add_child(game)
	await process_frame
	var seed_v := int(OS.get_environment("STAIR_SEED")) if OS.get_environment("STAIR_SEED") != "" else 4242
	game.start_world("Stairs", seed_v)
	for i in range(3000):
		await create_timer(0.02).timeout
		var st: Dictionary = game.get_debug_state()
		if st.get("pending", 1) == 0 and st.get("tiles", 0) == st.get("wanted", -1) and st.get("lod3", 0) > 0:
			break
	var player: Node3D = game.find_children("*", "FlyingPlayer", true, false)[0]
	var p := player.global_position
	var R := 60
	var cx := int(floor(p.x))
	var cz := int(floor(p.z))
	var cy := int(floor(p.y))
	var walk := {}
	var is_slab := {}
	for x in range(cx - R, cx + R + 1):
		for z in range(cz - R, cz + R + 1):
			for y in range(cy + 40, cy - 40, -1):
				var b: int = game.get_voxel(x, y, z)
				if b in [0, 5, 9, 10, 13, 28]:
					continue
				var st: int = game.get_shape_type(x, y, z)
				var h := float(y) + (0.5 if st == 1 else 1.0)
				walk[Vector2i(x, z)] = h
				is_slab[Vector2i(x, z)] = st == 1
				break
	var bins := {"flat": 0, "half": 0, "jump": 0, "cliff": 0}
	var piles := 0
	var slabs := 0
	for k in walk.keys():
		for d in [Vector2i(1, 0), Vector2i(0, 1)]:
			var n: Vector2i = k + d
			if not walk.has(n):
				continue
			var s := absf(walk[k] - walk[n])
			if s < 0.25: bins["flat"] += 1
			elif s <= 0.5: bins["half"] += 1
			elif s <= 1.0: bins["jump"] += 1
			else: bins["cliff"] += 1
		if is_slab[k]:
			slabs += 1
			for d in [Vector2i(1, 0), Vector2i(-1, 0), Vector2i(0, 1), Vector2i(0, -1)]:
				var n: Vector2i = k + d
				if walk.has(n) and is_slab[n] and absf(walk[n] - walk[k] - 1.0) < 0.1:
					piles += 1
					if OS.get_environment("STAIR_DUMP") != "":
						print("PILE ", k - Vector2i(cx, cz), " h=", walk[k])
					break
	if OS.get_environment("STAIR_GRID") != "":
		var g := OS.get_environment("STAIR_GRID").split(",")
		var gx := cx + int(g[0])
		var gz := cz + int(g[1])
		for z in range(gz - 5, gz + 6):
			var row := ""
			for x in range(gx - 7, gx + 8):
				var k := Vector2i(x, z)
				row += ("%5.1f%s" % [walk.get(k, -1.0), "s" if is_slab.get(k, false) else " "])
			print("GRID ", row)
	if OS.get_environment("STAIR_COLS") != "":
		var g := OS.get_environment("STAIR_COLS").split(",")
		var row := ""
		for x in range(cx + int(g[0]) - 7, cx + int(g[0]) + 8):
			player.global_position = Vector3(x + 0.5, 200, cz + int(g[1]) + 0.5)
			var info: String = game.execute_command("biome info")
			var i := info.find("Height: ")
			row += info.substr(i + 8, 6) + " "
		print("COLS ", row)
	var total := 0
	for v in bins.values(): total += v
	if OS.get_environment("STAIR_SHOT") != "":
		# Walk up the slope east of (8,-55) holding W, no jumping; report the climb.
		var start := Vector2i(cx + 6, cz - 55)
		player.global_position = Vector3(start.x + 0.5, walk.get(start, cy) + 0.1, start.y + 0.5)
		player.set_view(-PI * 0.5, -0.25)
		var console = game.get_console()
		if console and console.is_enabled():
			console.set_enabled(false)
		game.set_debug_overlay(false)
		player.set_active(true)
		await create_timer(0.8).timeout
		var y0 := player.global_position.y
		var e := InputEventKey.new()
		e.keycode = KEY_W
		e.physical_keycode = KEY_W
		e.pressed = true
		Input.parse_input_event(e)
		await create_timer(2.5).timeout
		var dist := player.global_position.x - (start.x + 0.5)
		print("WALK dx=%.1f dy=%.1f jumps=%d pos=%s state=%s" % [dist, player.global_position.y - y0, player.get_jump_count(), player.global_position, player.get_movement_state_name()])
		await RenderingServer.frame_post_draw
		root.get_texture().get_image().save_png(OS.get_environment("STAIR_SHOT"))
	print("STAIR_METRICS pairs=%d flat=%.1f%% half=%.1f%% jump=%.1f%% cliff=%.1f%% slabs=%d slab_piles=%d (%.1f%%)" % [
		total, 100.0 * bins["flat"] / total, 100.0 * bins["half"] / total, 100.0 * bins["jump"] / total,
		100.0 * bins["cliff"] / total, slabs, piles, 100.0 * piles / maxf(1.0, slabs)])
	quit()
