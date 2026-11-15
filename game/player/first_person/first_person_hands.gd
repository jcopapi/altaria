extends Control

@export var relaxed_hand: Texture2D
@export var closed_hand: Texture2D
@export var hold_hand: Texture2D

const tools := {
	"pickaxe": preload("res://game/assets/items/stone_pickaxe.gltf"),
	"axe": preload("res://game/assets/items/stone_axe.gltf"),
	"world_wand": preload("res://game/assets/items/world_wand.gltf"),
}
const pickaxe_grip := Vector3(0.0, -0.24, 0.0)
const axe_grip := Vector3(0.0, -0.25, 0.0)
const wand_grip := Vector3(0.0, -0.34, 0.0)
const wand_scale := Vector3(0.46, 0.46, 0.46)
# Head leans forward into the world (handle top tilted away from the camera).
@export var pickaxe_angles := Vector3(-24.0, -48.0, 16.0)
@export var axe_angles := Vector3(-26.0, -54.0, 15.0)
@export var wand_angles := Vector3(-22.0, -40.0, 14.0)
const DEFAULT_HAND_SCALE := Vector2(14.5, 14.5)

var equipped := {
	"left": "",
	"right": "",
}
var running := false

@onready var left_root: Control = $Left
@onready var right_root: Control = $Right
@onready var left_run: Node2D = $Left/Run
@onready var right_run: Node2D = $Right/Run
@onready var left_action: Node2D = $Left/Run/Action
@onready var right_action: Node2D = $Right/Run/Action
@onready var left_viewport: SubViewport = $Left/Run/Action/ToolViewport
@onready var right_viewport: SubViewport = $Right/Run/Action/ToolViewport
@onready var left_cam: Camera3D = $Left/Run/Action/ToolViewport/Camera3D
@onready var right_cam: Camera3D = $Right/Run/Action/ToolViewport/Camera3D
@onready var left_key_light: DirectionalLight3D = $Left/Run/Action/ToolViewport/KeyLight
@onready var right_key_light: DirectionalLight3D = $Right/Run/Action/ToolViewport/KeyLight
@onready var left_grip: Node3D = $Left/Run/Action/ToolViewport/Grip
@onready var right_grip: Node3D = $Right/Run/Action/ToolViewport/Grip
@onready var left_display: Sprite2D = $Left/Run/Action/ToolDisplay
@onready var right_display: Sprite2D = $Right/Run/Action/ToolDisplay
@onready var left_sprite: Sprite2D = $Left/Run/Action/HandSprite
@onready var right_sprite: Sprite2D = $Right/Run/Action/HandSprite
@onready var locomotion: AnimationPlayer = $Locomotion
@onready var actions: AnimationPlayer = $Actions

# Secondary support hand for custom two-handed grip on pickaxe/axe shaft
var _right_support_hand: Sprite2D = null
var _left_support_hand: Sprite2D = null

# 3D held block preview inside Right ToolViewport
var _block_pivot: Node3D = null
var _block_mesh_inst: MeshInstance3D = null
var _held_block_id: int = 0
var _held_shape_id: int = 0
var _held_rot: int = 0
var _block_pop: float = 0.0
var _block_base_scale: float = 0.4
var _block_base_rot := Vector3.ZERO
var _block_bob_phase: float = 0.0

# Base offsets from first_person_hands.tscn
var _base_left_offset := Vector2(0.0, -144.0)
var _base_right_offset := Vector2(0.0, -144.0)
var _cur_left_offset := Vector2(0.0, -144.0)
var _cur_right_offset := Vector2(0.0, -144.0)

# Motion / procedural bobbing / jump / fall / climb state
var _move_state: String = "WALK"
var _horiz_speed: float = 0.0
var _vert_speed: float = 0.0
var _is_climbing: bool = false
var _is_swimming: bool = false
var _bob_phase: float = 0.0
var _swim_phase: float = 0.0
var _sway_offset := Vector2.ZERO
var _pitch_offset: float = 0.0
var _prev_cam_rot := Vector3.ZERO
var _cam_rot_initialized: bool = false
var _jump_impulse: float = 0.0
var _land_impulse: float = 0.0
var _was_airborne: bool = false

# World-space target projection for block placement and breaking
var _action_kind: String = ""
var _action_time: float = 0.0
var _action_duration: float = 0.56
var _combo_step: int = 0
var _next_combo_step: int = 0
var _combo_reset_timer: float = 0.0
var _combo_start_pos := Vector2.ZERO
var _combo_start_rot: float = 0.0
var _last_action_offset_l := Vector2.ZERO
var _last_action_offset_r := Vector2.ZERO
var _last_action_rot_l: float = 0.0
var _last_action_rot_r: float = 0.0
var _impact_fired: bool = false
var _world_target_screen := Vector2.ZERO
var _world_target_dist: float = 3.0
var _world_target_valid: bool = false
var _wand_spin_nodes: Array[Node3D] = []
var _wand_spin_angle: float = 0.0
var _wand_spin_boost: float = 0.0

# Swing tuning (fractions of _action_duration)
const SWING_DURATION := 0.52
const SWING_IMPACT := 0.40
const HITSTOP_TIME := 0.06
# Where the tool head / knuckles sit relative to the hand root at the strike pose.
@export var tool_strike_anchor := Vector2(-140.0, -180.0)
@export var fist_strike_anchor := Vector2(-40.0, -70.0)
var _hitstop: float = 0.0
var _impact_kick: float = 0.0
var _swing_variant: float = 0.0

# World-space handholds for climbing blocks
var _climb_hold_left_screen := Vector2.ZERO
var _climb_hold_right_screen := Vector2.ZERO
var _climb_reaching_left: bool = false
var _climb_reaching_right: bool = false
var _climb_holds_valid: bool = false
var _grip_pulse_l: float = 0.0
var _grip_pulse_r: float = 0.0

# Tool grip layout (right-hand space; the left hand mirrors X).
var TOOL_SCREEN_ROT := -0.36
const TOOL_DISPLAY_POS := Vector2(-46.0, -86.0)
const TOOL_DISPLAY_SCALE := 3.0
const GRIP_HAND_SCALE := 10.0
const MAIN_GRIP_ALONG := 64.0
const SUPPORT_GRIP_ALONG := -30.0
const BLOCK_PALM_POS := Vector2(-104.0, -16.0)
const BLOCK_DISPLAY_SCALE := 1.65
const ONE_HAND_TOOL_POS := Vector2(-32.0, -76.0)
const ONE_HAND_TOOL_SCALE := 2.75
const LAYOUT_SPEED := 13.0
const CENTER_FADE_ALPHA := 0.55
# The tool head stops this many pixels short of the crosshair so it reads as hitting the block face.
const STRIKE_SHORT_PX := 80.0
var _layout := {}
var _layout_snap := true

# Item switch: both hands dip and wobble while the new item settles in.
var _switch_t: float = 1.0
var _switch_amp: float = 1.0
var _item_changed := false

# Dynamic environment lighting tint
var _target_light := Color(1.0, 1.0, 1.0, 1.0)
var _current_light := Color(1.0, 1.0, 1.0, 1.0)

func _ready() -> void:
	left_display.texture = left_viewport.get_texture()
	right_display.texture = right_viewport.get_texture()

	_base_left_offset = Vector2(left_root.offset_left, left_root.offset_top)
	_base_right_offset = Vector2(right_root.offset_left, right_root.offset_top)
	_cur_left_offset = _base_left_offset
	_cur_right_offset = _base_right_offset

	# Secondary support hand for custom two-handed tool grip (inverted relative to main hand)
	_right_support_hand = Sprite2D.new()
	_right_support_hand.name = "SupportHandSprite"
	_right_support_hand.texture = closed_hand
	_right_support_hand.scale = Vector2(10.5, 10.5)
	_right_support_hand.flip_h = true
	_right_support_hand.position = Vector2(-6.0, 42.0)
	_right_support_hand.rotation = 0.14
	_right_support_hand.visible = false
	right_action.add_child(_right_support_hand)

	_left_support_hand = Sprite2D.new()
	_left_support_hand.name = "SupportHandSprite"
	_left_support_hand.texture = closed_hand
	_left_support_hand.scale = Vector2(10.5, 10.5)
	_left_support_hand.flip_h = false
	_left_support_hand.position = Vector2(6.0, 42.0)
	_left_support_hand.rotation = -0.14
	_left_support_hand.visible = false
	left_action.add_child(_left_support_hand)

	# Create 3D block holder inside Right ToolViewport
	_block_pivot = Node3D.new()
	_block_pivot.name = "BlockPivot"
	_block_pivot.visible = false
	right_viewport.add_child(_block_pivot)

	_block_mesh_inst = MeshInstance3D.new()
	_block_mesh_inst.name = "HeldBlockMesh"
	_block_pivot.add_child(_block_mesh_inst)

	_setup_display_outline(right_display)
	_setup_display_outline(left_display)

	# Clone environments so lighting modulation stays independent
	if left_cam and left_cam.environment:
		left_cam.environment = left_cam.environment.duplicate()
	if right_cam and right_cam.environment:
		right_cam.environment = right_cam.environment.duplicate()

	visibility_changed.connect(_update_viewports)
	_update_sprites()
	_update_viewports()
	_layout_snap = false

func reset_pose() -> void:
	_action_kind = ""
	_action_time = 0.0
	_combo_step = 0
	_next_combo_step = 0
	_combo_reset_timer = 0.0
	_combo_start_pos = Vector2.ZERO
	_combo_start_rot = 0.0
	_last_action_offset_l = Vector2.ZERO
	_last_action_offset_r = Vector2.ZERO
	_last_action_rot_l = 0.0
	_last_action_rot_r = 0.0
	_impact_fired = false
	_world_target_valid = false
	_climb_holds_valid = false
	_is_climbing = false
	_is_swimming = false
	_swim_phase = 0.0
	_cur_left_offset = _base_left_offset
	_cur_right_offset = _base_right_offset
	if left_root:
		_set_root_offset(left_root, _base_left_offset)
		left_root.scale = Vector2.ONE
		left_root.rotation = 0.0
	if right_root:
		_set_root_offset(right_root, _base_right_offset)
		right_root.scale = Vector2.ONE
		right_root.rotation = 0.0
	_reset_grip_angles()
	_update_sprites()
	_update_viewports()

func _base_tool_angles(hand: String, tool_name: String) -> Vector3:
	var angles := wand_angles if tool_name == "world_wand" else (pickaxe_angles if tool_name == "pickaxe" else axe_angles)
	if hand == "left":
		angles.y = -angles.y
		angles.z = -angles.z
	return angles

func _reset_grip_angles() -> void:
	var r_eq: String = equipped["right"]
	var l_eq: String = equipped["left"]
	if right_grip:
		right_grip.position = Vector3.ZERO
		if r_eq != "":
			right_grip.rotation_degrees = _base_tool_angles("right", r_eq)
	if left_grip:
		left_grip.position = Vector3.ZERO
		if l_eq != "":
			left_grip.rotation_degrees = _base_tool_angles("left", l_eq)

func _fire_break_impact() -> void:
	var n: Node = get_parent()
	while n != null:
		if n.has_method("on_hand_break_impact"):
			n.call("on_hand_break_impact")
			return
		n = n.get_parent()

func set_tool(hand: String, tool_name: String) -> void:
	var target := hand.to_lower()
	if target != "left" and target != "right":
		return
	var clean := tool_name.to_lower()
	if clean != "" and not tools.has(clean):
		return

	if equipped[target] != clean:
		_trigger_switch(1.0)
	equipped[target] = clean
	if target == "right":
		_wand_spin_nodes.clear()
	var grip := left_grip if target == "left" else right_grip
	for child in grip.get_children():
		grip.remove_child(child)
		child.queue_free()

	if clean != "":
		var model: Node3D = tools[clean].instantiate()
		if clean == "world_wand":
			model.position = wand_grip
			model.scale = wand_scale
			_collect_wand_spin_nodes(model)
		else:
			model.position = pickaxe_grip if clean == "pickaxe" else axe_grip
		grip.position = Vector3.ZERO
		grip.rotation_degrees = _base_tool_angles(target, clean)
		grip.add_child(model)

	_update_sprites()
	_update_viewports()

func _collect_wand_spin_nodes(node: Node) -> void:
	if node is Node3D:
		var lname := String(node.name).to_lower()
		if lname == "sphere" or lname == "sphere_outline":
			_wand_spin_nodes.append(node)
	for c in node.get_children():
		_collect_wand_spin_nodes(c)

func _trigger_switch(amp: float) -> void:
	if _layout_snap:
		return
	_switch_t = 0.0
	_switch_amp = amp
	_item_changed = true

func set_held_block(mesh: ArrayMesh, block_id: int, shape_id: int, rot: int) -> void:
	if block_id != _held_block_id and equipped["right"] == "":
		# Bigger dip when the hand layout itself changes (empty <-> block).
		_trigger_switch(0.6 if (block_id > 0 and _held_block_id > 0) else 1.0)
	elif shape_id != _held_shape_id and block_id > 0:
		_trigger_switch(0.35)
	_held_block_id = block_id
	_held_shape_id = shape_id
	_held_rot = rot
	if _block_mesh_inst == null or _block_pivot == null:
		return
	_block_mesh_inst.mesh = mesh
	if mesh != null and block_id > 0:
		var aabb := mesh.get_aabb()
		var center := aabb.position + aabb.size * 0.5
		_block_mesh_inst.position = -center
		# Centered inside ToolViewport; ToolDisplay is placed at the palm and drawn underneath the hand
		_block_pivot.position = Vector3(0.0, 0.0, 0.0)
		# Fit the block comfortably inside the cupped hand while keeping half-blocks proportional.
		var longest := maxf(aabb.size.x, maxf(aabb.size.y, aabb.size.z))
		_block_base_scale = 0.25 / maxf(longest, 0.30)
		_block_pivot.scale = Vector3.ONE * _block_base_scale
		var yaw := -0.78 + float(rot & 3) * (PI * 0.5)
		# Tilted toward the camera so the lit top face and both sides show.
		_block_base_rot = Vector3(0.50, yaw, 0.06)
		_block_pivot.rotation = _block_base_rot
		_block_pop = 1.0
	_update_sprites()
	_update_viewports()

func set_environment_light(light_color: Color) -> void:
	_target_light = Color(
		clampf(light_color.r, 0.08, 1.35),
		clampf(light_color.g, 0.08, 1.35),
		clampf(light_color.b, 0.08, 1.35),
		1.0
	)

func _apply_environment_light(delta: float) -> void:
	var t := clampf(delta * 10.0, 0.0, 1.0)
	_current_light = _current_light.lerp(_target_light, t)
	# Brief bright flash on the striking hand/tool at the moment of impact.
	var flash := _current_light.lerp(Color(1.6, 1.55, 1.45, 1.0), _impact_kick * 0.45)
	if left_display:
		left_display.modulate = flash
	if right_display:
		right_display.modulate = flash
	if left_sprite:
		left_sprite.modulate = _current_light
	if right_sprite:
		right_sprite.modulate = _current_light
	if _right_support_hand:
		_right_support_hand.modulate = _current_light
	if _left_support_hand:
		_left_support_hand.modulate = _current_light
	if left_key_light:
		left_key_light.light_color = _current_light
	if right_key_light:
		right_key_light.light_color = _current_light
	if left_cam and left_cam.environment:
		left_cam.environment.ambient_light_color = _current_light
	if right_cam and right_cam.environment:
		right_cam.environment.ambient_light_color = _current_light
		# The held block uses the terrain shader, which gets no sky fill in this
		# private world: lift ambient/key so it reads like the block in the world.
		var block_shown := _block_pivot != null and _block_pivot.visible
		right_cam.environment.ambient_light_energy = 1.35 if block_shown else 0.75
		if right_key_light:
			right_key_light.light_energy = 1.7 if block_shown else 1.1

func set_running(active: bool) -> void:
	if running == active and locomotion.current_animation == ("run" if active else "idle"):
		return
	running = active
	locomotion.play("run" if active else "idle")
	_update_sprites()

func _begin_break_swing() -> void:
	_action_kind = "break"
	_action_time = 0.0
	_action_duration = SWING_DURATION
	_hitstop = 0.0
	_swing_variant = randf_range(-1.0, 1.0)
	_combo_step = _next_combo_step
	_next_combo_step = 1 - _combo_step
	_combo_reset_timer = 0.45
	_combo_start_pos = _last_action_offset_r
	_combo_start_rot = _last_action_rot_r
	_impact_fired = false
	actions.stop()
	var r_eq: String = equipped["right"]
	var l_eq: String = equipped["left"]
	actions.play("break_left" if r_eq == "" and l_eq != "" else "break_right")

func play_action(action: StringName) -> void:
	var act_str := String(action)
	if act_str == "place":
		if not (_action_kind == "place" and _action_time < 0.03 and _world_target_valid):
			_world_target_valid = false
			_action_kind = "place"
			_action_time = 0.0
			_action_duration = 0.32
		actions.stop()
		actions.play("place")
	elif act_str == "break":
		if not (_action_kind == "break" and _action_time < 0.03 and _world_target_valid):
			_world_target_valid = false
			_begin_break_swing()

func play_world_action(action: StringName, screen_target: Vector2, world_distance: float) -> void:
	var act_str := String(action)
	_world_target_screen = screen_target
	_world_target_dist = clampf(world_distance, 0.8, 12.0)
	_world_target_valid = true
	if act_str == "place":
		if equipped["right"] == "world_wand":
			_wand_spin_boost = 1.0
		_action_kind = "place"
		_action_time = 0.0
		_action_duration = 0.32
		actions.stop()
		actions.play("place")
	elif act_str == "break":
		_begin_break_swing()

func update_world_target(screen_target: Vector2, valid: bool) -> void:
	if valid:
		_world_target_screen = screen_target
	_world_target_valid = valid

func update_climb_world_holds(left_screen: Vector2, right_screen: Vector2, reaching_left: bool, reaching_right: bool, valid: bool) -> void:
	_climb_holds_valid = valid
	if valid:
		# A hand that just finished reaching latches on: squash + small pull-down.
		if _climb_reaching_left and not reaching_left:
			_grip_pulse_l = 1.0
		if _climb_reaching_right and not reaching_right:
			_grip_pulse_r = 1.0
		_climb_hold_left_screen = left_screen
		_climb_hold_right_screen = right_screen
		_climb_reaching_left = reaching_left
		_climb_reaching_right = reaching_right

func update_motion_state(state_name: String, horiz_speed: float, vert_speed: float, _cam_local_pos: Vector3, cam_rot: Vector3, climbing: bool) -> void:
	var prev_climbing := _is_climbing
	var prev_swimming := _is_swimming
	_move_state = state_name
	_horiz_speed = horiz_speed
	_vert_speed = vert_speed
	_is_climbing = climbing or state_name == "CLIMB" or state_name == "MANTLE"
	_is_swimming = (state_name == "SWIM") and not _is_climbing

	var airborne_now := (state_name == "FALL" or state_name == "GLIDE" or absf(vert_speed) > 1.5) and not _is_swimming
	if not _was_airborne and airborne_now and vert_speed > 2.2:
		_jump_impulse = 1.0
	elif _was_airborne and not airborne_now and not _is_climbing:
		_land_impulse = clampf(-vert_speed / 12.0, 0.25, 1.0)
	_was_airborne = airborne_now

	if not _cam_rot_initialized:
		_prev_cam_rot = cam_rot
		_cam_rot_initialized = true
	var dyaw := wrapf(cam_rot.y - _prev_cam_rot.y, -PI, PI)
	var dpitch := cam_rot.x - _prev_cam_rot.x
	_prev_cam_rot = cam_rot
	_sway_offset.x = clampf(_sway_offset.x - dyaw * 95.0, -28.0, 28.0)
	_sway_offset.y = clampf(_sway_offset.y + dpitch * 85.0, -24.0, 24.0)
	_pitch_offset = clampf(-cam_rot.x * 16.0, -22.0, 22.0)

	if prev_climbing != _is_climbing or prev_swimming != _is_swimming:
		_update_sprites()
		_update_viewports()

func _setup_display_outline(display: Sprite2D) -> void:
	if display == null:
		return
	var sh := Shader.new()
	sh.code = """
shader_type canvas_item;
uniform bool outline_enabled = false;
uniform vec4 outline_color : source_color = vec4(0.05, 0.04, 0.07, 1.0);
uniform float outline_width = 1.6;
varying vec4 v_modulate;

void vertex() {
	v_modulate = COLOR;
}

void fragment() {
	vec4 col = texture(TEXTURE, UV);
	if (!outline_enabled) {
		COLOR = col * v_modulate;
	} else {
		vec2 px = TEXTURE_PIXEL_SIZE * outline_width;
		float a = col.a;
		float n = 0.0;
		n = max(n, texture(TEXTURE, UV + vec2( px.x,  0.0)).a);
		n = max(n, texture(TEXTURE, UV + vec2(-px.x,  0.0)).a);
		n = max(n, texture(TEXTURE, UV + vec2( 0.0,  px.y)).a);
		n = max(n, texture(TEXTURE, UV + vec2( 0.0, -px.y)).a);
		n = max(n, texture(TEXTURE, UV + vec2( px.x,  px.y)).a);
		n = max(n, texture(TEXTURE, UV + vec2(-px.x,  px.y)).a);
		n = max(n, texture(TEXTURE, UV + vec2( px.x, -px.y)).a);
		n = max(n, texture(TEXTURE, UV + vec2(-px.x, -px.y)).a);
		if (a < 0.45 && n > 0.35) {
			COLOR = vec4(outline_color.rgb * v_modulate.rgb, outline_color.a * v_modulate.a * smoothstep(0.25, 0.65, n));
		} else {
			vec4 lit = col * v_modulate;
			if (a >= 0.45 && a < 0.90) {
				lit.rgb = mix(outline_color.rgb * v_modulate.rgb, lit.rgb, (a - 0.45) / 0.45);
			}
			COLOR = lit;
		}
	}
}
"""
	var mat := ShaderMaterial.new()
	mat.shader = sh
	display.material = mat

func _set_display_outline(display: Sprite2D, enabled: bool) -> void:
	if display and display.material is ShaderMaterial:
		(display.material as ShaderMaterial).set_shader_parameter("outline_enabled", enabled)

func _is_two_handed_right() -> bool:
	return false

func _is_two_handed_left() -> bool:
	return false

# Layout targets (position, rotation, scale) per sprite. Layout changes (block <-> tool,
# two-handed grip on/off) glide toward these instead of snapping.
func _lay(node: Node2D, pos: Vector2, rot: float, scl: Vector2) -> void:
	if node == null:
		return
	_layout[node] = [pos, rot, scl]
	if _layout_snap:
		node.position = pos
		node.rotation = rot
		node.scale = scl

func _tween_layout(dt: float) -> void:
	var k := 1.0 - exp(-dt * LAYOUT_SPEED)
	for node in _layout.keys():
		if not is_instance_valid(node):
			_layout.erase(node)
			continue
		var t: Array = _layout[node]
		node.position = node.position.lerp(t[0], k)
		node.rotation = lerp_angle(node.rotation, t[1], k)
		node.scale = node.scale.lerp(t[2], k)

# A sprite that just became visible starts from where another hand/item was, so
# hands travel to the new grip instead of popping into place.
func _hand_off(from: Node2D, to: Node2D) -> void:
	if from == null or to == null or _layout_snap:
		return
	to.global_position = from.global_position
	to.global_rotation = from.global_rotation
	to.global_scale = from.global_scale

func _grow_in(display: Sprite2D, was_visible: bool) -> void:
	# Item swaps in from below the hand, small, then settles at full size.
	if display == null or was_visible or _layout_snap or not _layout.has(display):
		return
	var t: Array = _layout[display]
	display.position = t[0] + Vector2(24.0, 120.0)
	display.scale = t[2] * 0.2
	display.rotation = t[1] + 0.6

func _update_sprites() -> void:
	if left_sprite == null or right_sprite == null:
		return
	var r_eq: String = equipped["right"]
	var l_eq: String = equipped["left"]

	var two_hand_r := _is_two_handed_right()
	var two_hand_l := _is_two_handed_left()
	var show_held_block := (r_eq == "" and _held_block_id > 0 and _block_mesh_inst != null and _block_mesh_inst.mesh != null and not _is_climbing)

	left_sprite.texture = closed_hand if (running or l_eq != "" or _is_climbing) else relaxed_hand
	if show_held_block and hold_hand != null:
		right_sprite.texture = hold_hand
	else:
		right_sprite.texture = closed_hand if (running or r_eq != "" or _is_climbing) else relaxed_hand

	_set_display_outline(left_display, false)
	_set_display_outline(right_display, show_held_block)

	if _block_pivot:
		_block_pivot.visible = show_held_block
	if right_grip:
		right_grip.visible = (r_eq != "" and not _is_climbing)
	if left_grip:
		left_grip.visible = (l_eq != "" and not _is_climbing)

	var was_left_vis := left_sprite.visible
	var was_right_vis := right_sprite.visible
	var was_sup_r := _right_support_hand != null and _right_support_hand.visible
	var was_sup_l := _left_support_hand != null and _left_support_hand.visible
	var was_disp_l := left_display.visible
	var was_disp_r := right_display.visible

	# Tool grip: the shaft runs diagonally (head toward the screen centre) through
	# both fists. The support hand sits high on the shaft, the main hand near the butt.
	var tool_rot := TOOL_SCREEN_ROT
	var shaft_down := Vector2(0.0, 1.0).rotated(tool_rot)

	if two_hand_r:
		var d := TOOL_DISPLAY_POS
		_lay(right_display, d, tool_rot, Vector2.ONE * TOOL_DISPLAY_SCALE)
		right_display.z_index = 0
		right_sprite.flip_h = false
		_lay(right_sprite, d + shaft_down * MAIN_GRIP_ALONG + Vector2(6.0, 0.0), tool_rot - 0.30, Vector2.ONE * GRIP_HAND_SCALE)
		right_sprite.visible = true
		if _right_support_hand:
			_right_support_hand.flip_h = true
			_lay(_right_support_hand, d + shaft_down * SUPPORT_GRIP_ALONG + Vector2(-6.0, 0.0), tool_rot + 0.30, Vector2.ONE * GRIP_HAND_SCALE)
			_right_support_hand.visible = true
			if not was_sup_r:
				_hand_off(left_sprite, _right_support_hand)
		left_sprite.visible = false
		if _left_support_hand:
			_left_support_hand.visible = false
	elif two_hand_l:
		var d := Vector2(-TOOL_DISPLAY_POS.x, TOOL_DISPLAY_POS.y)
		var sd := Vector2(-shaft_down.x, shaft_down.y)
		_lay(left_display, d, -tool_rot, Vector2.ONE * TOOL_DISPLAY_SCALE)
		left_display.z_index = 0
		left_sprite.flip_h = true
		_lay(left_sprite, d + sd * MAIN_GRIP_ALONG + Vector2(-6.0, 0.0), -tool_rot + 0.30, Vector2.ONE * GRIP_HAND_SCALE)
		left_sprite.visible = true
		if _left_support_hand:
			_left_support_hand.flip_h = false
			_lay(_left_support_hand, d + sd * SUPPORT_GRIP_ALONG + Vector2(6.0, 0.0), -tool_rot - 0.30, Vector2.ONE * GRIP_HAND_SCALE)
			_left_support_hand.visible = true
			if not was_sup_l:
				_hand_off(right_sprite, _left_support_hand)
		right_sprite.visible = false
		if _right_support_hand:
			_right_support_hand.visible = false
	else:
		# Standard independent hands (one-handed tool, holding a 3D block, climbing, or empty)
		if l_eq != "":
			_lay(left_display, Vector2(-ONE_HAND_TOOL_POS.x, ONE_HAND_TOOL_POS.y), -tool_rot, Vector2.ONE * ONE_HAND_TOOL_SCALE)
		else:
			_lay(left_display, Vector2(22.0, -18.0), 0.0, Vector2(2.1, 2.1))
		left_display.z_index = 0
		left_sprite.flip_h = true
		_lay(left_sprite, Vector2.ZERO, 0.0, DEFAULT_HAND_SCALE)
		left_sprite.visible = true
		if not was_left_vis and was_sup_r:
			_hand_off(_right_support_hand, left_sprite)

		if show_held_block:
			# Block sits behind the cupped holding hand, tucked inside the fingers/palm.
			_lay(right_display, BLOCK_PALM_POS, 0.0, Vector2.ONE * BLOCK_DISPLAY_SCALE)
			right_display.z_index = -1
		elif r_eq != "":
			_lay(right_display, ONE_HAND_TOOL_POS, tool_rot, Vector2.ONE * ONE_HAND_TOOL_SCALE)
			right_display.z_index = 0
		else:
			_lay(right_display, Vector2(-22.0, -18.0), 0.0, Vector2(2.1, 2.1))
			right_display.z_index = 0

		right_sprite.flip_h = false
		_lay(right_sprite, Vector2.ZERO, 0.0, DEFAULT_HAND_SCALE)
		right_sprite.visible = true
		if not was_right_vis and was_sup_l:
			_hand_off(_left_support_hand, right_sprite)

		if _right_support_hand:
			_right_support_hand.visible = false
		if _left_support_hand:
			_left_support_hand.visible = false

	left_display.visible = (l_eq != "" and not _is_climbing)
	right_display.visible = ((r_eq != "" or show_held_block) and not _is_climbing)
	_grow_in(left_display, was_disp_l and not _item_changed)
	_grow_in(right_display, was_disp_r and not _item_changed)
	_item_changed = false

func _update_viewports() -> void:
	if left_viewport == null or right_viewport == null:
		return
	var active := is_visible_in_tree()
	var l_eq: String = equipped["left"]
	var r_eq: String = equipped["right"]
	var show_held_block := (r_eq == "" and _held_block_id > 0 and _block_mesh_inst != null and _block_mesh_inst.mesh != null and not _is_climbing)

	left_viewport.render_target_update_mode = (
		SubViewport.UPDATE_WHEN_VISIBLE if active and l_eq != "" else SubViewport.UPDATE_DISABLED
	)
	right_viewport.render_target_update_mode = (
		SubViewport.UPDATE_WHEN_VISIBLE if active and (r_eq != "" or show_held_block) else SubViewport.UPDATE_DISABLED
	)

func _ease_out_cubic(t: float) -> float:
	var i := 1.0 - clampf(t, 0.0, 1.0)
	return 1.0 - i * i * i

func _ease_in_cubic(t: float) -> float:
	var c := clampf(t, 0.0, 1.0)
	return c * c * c

func _ease_in_out(t: float) -> float:
	var c := clampf(t, 0.0, 1.0)
	return c * c * (3.0 - 2.0 * c)

func _ease_out_back(t: float) -> float:
	var c := clampf(t, 0.0, 1.0) - 1.0
	return 1.0 + 2.70158 * c * c * c + 1.70158 * c * c

func _depth_scale() -> float:
	# Hand/tool shrink toward far blocks so the strike visibly reaches into the world.
	return clampf(1.75 / maxf(_world_target_dist, 1.0), 0.40, 0.95)

# One swing: anticipation -> whip strike landing the head on the block ->
# recoil -> spring home. Alternating swings vary the angle so mining has rhythm.
func _swing_pose(u: float, to_block: Vector2, depth: float, has_tool: bool) -> Dictionary:
	var side := 1.0 if _combo_step == 0 else -1.0
	var v := _swing_variant
	var windup := Vector2(26.0 + 12.0 * side, -58.0 - 12.0 * v) if has_tool else Vector2(40.0, 30.0)
	var windup_rot := (0.34 + 0.06 * side) if has_tool else 0.30
	var strike_rot := (-0.38 + 0.10 * side) if has_tool else -0.12
	var anchor := tool_strike_anchor if has_tool else fist_strike_anchor
	# The anchor rotates and scales with the hand root, so solve for the root
	# position that puts the head exactly on the block.
	var windup_head := windup + anchor.rotated(windup_rot) * 1.06
	var approach := (to_block - windup_head).normalized()
	var hit_point := to_block - approach * STRIKE_SHORT_PX * depth if has_tool else to_block
	var strike := hit_point - anchor.rotated(strike_rot) * depth
	var recoil := strike + (windup - strike).normalized() * 30.0 * depth
	# Wind-up rotates the tool in the screen plane (head raised back over the hand).
	var grip_wind := Vector3(-14.0, 4.0 * side, -46.0) if has_tool else Vector3.ZERO
	var grip_hit := Vector3(42.0, -6.0 * side, 34.0) if has_tool else Vector3.ZERO
	var out := {"pos": Vector2.ZERO, "rot": 0.0, "scale": 1.0, "squash": 0.0, "grip": Vector3.ZERO, "grip_pos": Vector3.ZERO}
	var t_wind := 0.30
	if u < t_wind:
		# Anticipation: pull up/back toward the camera, slowing at the top.
		var w := _ease_out_cubic(u / t_wind)
		out["pos"] = _combo_start_pos.lerp(windup, w)
		out["rot"] = lerpf(_combo_start_rot, windup_rot, w)
		out["scale"] = lerpf(1.0, 1.06, w)
		out["squash"] = -0.35 * w
		out["grip"] = grip_wind * w
		out["grip_pos"] = Vector3(0.0, 0.04, -0.06) * w
	elif u < SWING_IMPACT:
		# Strike: accelerating whip along a curved arc into the block.
		var s := (u - t_wind) / (SWING_IMPACT - t_wind)
		var e := _ease_in_cubic(s)
		var arc_dir := (strike - windup).orthogonal().normalized() * side
		out["pos"] = windup.lerp(strike, e) + arc_dir * sin(s * PI) * 26.0
		out["rot"] = lerpf(windup_rot, strike_rot, e)
		out["scale"] = lerpf(1.06, depth, e)
		out["squash"] = lerpf(-0.35, 0.0, e)
		out["grip"] = grip_wind.lerp(grip_hit, e)
		out["grip_pos"] = Vector3(0.0, 0.04, -0.06).lerp(Vector3(0.0, 0.06, 0.14), e)
	elif u < 0.56:
		# Contact + recoil: squash on the block, then bounce back along the swing.
		var r := (u - SWING_IMPACT) / (0.56 - SWING_IMPACT)
		var e := _ease_out_cubic(r)
		out["pos"] = strike.lerp(recoil, e)
		out["rot"] = lerpf(strike_rot, strike_rot + 0.16, e)
		out["scale"] = depth
		out["squash"] = (1.0 - r) * 1.0
		out["grip"] = grip_hit.lerp(grip_hit * 0.55, e)
		out["grip_pos"] = Vector3(0.0, 0.06, 0.14).lerp(Vector3(0.0, 0.03, 0.06), e)
	else:
		# Return with a small overshoot so it settles like a spring.
		var r := (u - 0.56) / 0.44
		var e := _ease_out_back(r)
		out["pos"] = recoil.lerp(Vector2.ZERO, e)
		out["rot"] = lerpf(strike_rot + 0.16, 0.0, e)
		out["scale"] = lerpf(depth, 1.0, e)
		out["squash"] = 0.0
		out["grip"] = (grip_hit * 0.55).lerp(Vector3.ZERO, e)
		out["grip_pos"] = Vector3(0.0, 0.03, 0.06).lerp(Vector3.ZERO, e)
	return out

func _animate_held_block(dt: float) -> void:
	if _block_pivot == null or not _block_pivot.visible:
		return
	# Swap-in pop (overshooting scale) plus a slow idle sway so the block feels held, not pasted.
	_block_pop = move_toward(_block_pop, 0.0, dt * 3.2)
	_block_bob_phase = wrapf(_block_bob_phase + dt * (2.4 if running else 1.1), 0.0, TAU)
	var t := 1.0 - _block_pop
	var overshoot := 1.0 + sin(t * PI) * 0.16 * _block_pop if _block_pop > 0.0 else 1.0
	var grow := lerpf(0.55, 1.0, 1.0 - pow(_block_pop, 3.0))
	_block_pivot.scale = Vector3.ONE * _block_base_scale * grow * overshoot
	var sway := sin(_block_bob_phase) * 0.05
	_block_pivot.rotation = _block_base_rot + Vector3(cos(_block_bob_phase * 0.5) * 0.025, sway + _block_pop * 0.9, 0.0)

func _animate_wand_orb(dt: float) -> void:
	if _wand_spin_nodes.is_empty():
		return
	_wand_spin_boost = move_toward(_wand_spin_boost, 0.0, dt * 3.8)
	var spin_speed := 1.45 + _wand_spin_boost * 6.5
	_wand_spin_angle = wrapf(_wand_spin_angle + dt * spin_speed, 0.0, TAU)
	var tilt := Basis(Vector3(0.0, 0.0, 1.0), deg_to_rad(14.0))
	var spin := Basis(Vector3.UP, _wand_spin_angle)
	for node in _wand_spin_nodes:
		if is_instance_valid(node):
			node.basis = tilt * spin

func _set_root_offset(ctrl: Control, off: Vector2) -> void:
	ctrl.offset_left = off.x
	ctrl.offset_right = off.x
	ctrl.offset_top = off.y
	ctrl.offset_bottom = off.y

func _process(delta: float) -> void:
	var dt := clampf(delta, 0.0, 0.20)
	_apply_environment_light(dt)
	_animate_held_block(dt)
	_animate_wand_orb(dt)
	_tween_layout(dt)
	_switch_t = minf(1.0, _switch_t + dt / 0.42)

	_sway_offset = _sway_offset.lerp(Vector2.ZERO, clampf(dt * 10.0, 0.0, 1.0))
	_impact_kick = move_toward(_impact_kick, 0.0, dt * 9.0)
	_grip_pulse_l = move_toward(_grip_pulse_l, 0.0, dt * 6.0)
	_grip_pulse_r = move_toward(_grip_pulse_r, 0.0, dt * 6.0)
	_jump_impulse = move_toward(_jump_impulse, 0.0, dt * 4.5)
	_land_impulse = move_toward(_land_impulse, 0.0, dt * 5.0)

	if _horiz_speed > 0.25 and not _is_climbing:
		var freq := 8.5 if running else 5.2
		_bob_phase = wrapf(_bob_phase + dt * freq, 0.0, TAU)
	else:
		_bob_phase = wrapf(_bob_phase + dt * 1.6, 0.0, TAU)

	var bob_amp := clampf(_horiz_speed / 7.0, 0.0, 1.25) if (not _is_climbing and not _is_swimming) else 0.0
	var bob_l := Vector2(
		cos(_bob_phase) * 7.0 * bob_amp,
		absf(sin(_bob_phase)) * 9.5 * bob_amp + sin(_bob_phase) * 2.5 * bob_amp
	)
	var bob_r := Vector2(
		-cos(_bob_phase) * 7.0 * bob_amp,
		absf(cos(_bob_phase)) * 9.5 * bob_amp + cos(_bob_phase) * 2.5 * bob_amp
	)

	# Falling effect: never blocks animations; simply pulls the hands slightly UP (-Y on screen) while falling
	var air_offset := Vector2.ZERO
	if not _is_climbing and not _is_swimming:
		if _vert_speed < -0.25:
			var fall_pull := clampf((-_vert_speed - 0.25) / 14.0, 0.0, 1.0)
			air_offset.y = -fall_pull * 34.0
		elif _vert_speed > 0.25:
			var jump_lag := clampf(_vert_speed / 12.0, 0.0, 1.0)
			air_offset.y = jump_lag * 10.0
		air_offset.y += _land_impulse * 16.0 - _jump_impulse * 12.0

	var target_l := _base_left_offset + bob_l + _sway_offset + air_offset + Vector2(0.0, _pitch_offset * 0.35)
	var target_r := _base_right_offset + bob_r + _sway_offset + air_offset + Vector2(0.0, _pitch_offset * 0.35)
	var target_scale_l := Vector2.ONE
	var target_scale_r := Vector2.ONE
	var target_rot_l := 0.0
	var target_rot_r := 0.0

	if _is_two_handed_right():
		target_r += Vector2(-24.0, -8.0)
	elif _is_two_handed_left():
		target_l += Vector2(24.0, -8.0)

	# Item switch: quick dip down, a little wobble, spring back up.
	var switch_dip := Vector2.ZERO
	var switch_rot := 0.0
	if _switch_t < 1.0:
		var st := _switch_t
		var env := sin(st * PI)
		switch_dip = Vector2(0.0, 52.0 * env * _switch_amp)
		switch_rot = sin(st * TAU * 1.5) * (1.0 - st) * 0.16 * _switch_amp
	target_l += switch_dip
	target_r += switch_dip
	target_rot_l -= switch_rot
	target_rot_r += switch_rot

	var vp_size := size
	if vp_size.x < 10.0 or vp_size.y < 10.0:
		vp_size = get_viewport_rect().size

	var left_rest_screen := Vector2(vp_size.x * left_root.anchor_left, vp_size.y) + _base_left_offset
	var right_rest_screen := Vector2(vp_size.x * right_root.anchor_left, vp_size.y) + _base_right_offset

	if _is_swimming:
		var swim_mag := sqrt(_horiz_speed * _horiz_speed + _vert_speed * _vert_speed)
		var actively_swimming := swim_mag > 0.55
		var swim_freq := clampf(4.6 + swim_mag * 0.48, 4.6, 8.8) if actively_swimming else 2.2
		_swim_phase = wrapf(_swim_phase + dt * swim_freq, 0.0, TAU)
		var stroke_amp := clampf(swim_mag / 6.0, 0.35, 1.30) if actively_swimming else 0.32
		if _is_two_handed_right():
			var s_r := sin(_swim_phase)
			var c_r := cos(_swim_phase)
			target_r += Vector2(c_r * 24.0 * stroke_amp, s_r * 18.0 * stroke_amp - 8.0)
			target_rot_r += c_r * 0.14 * stroke_amp
			target_scale_r = Vector2.ONE * (1.0 - maxf(0.0, -s_r) * 0.08 * stroke_amp)
		elif _is_two_handed_left():
			var s_l := sin(_swim_phase)
			var c_l := cos(_swim_phase)
			target_l += Vector2(-c_l * 24.0 * stroke_amp, s_l * 18.0 * stroke_amp - 8.0)
			target_rot_l -= c_l * 0.14 * stroke_amp
			target_scale_l = Vector2.ONE * (1.0 - maxf(0.0, -s_l) * 0.08 * stroke_amp)
		else:
			# Alternating crawl/breaststroke paddles in open water
			var phase_l := _swim_phase
			var phase_r := _swim_phase + (PI if actively_swimming else 0.0)
			var reaches_l := -cos(phase_l) # >0 extending forward, <0 pulling back
			var reaches_r := -cos(phase_r)
			var sweep_l := sin(phase_l)   # >0 outward sweep
			var sweep_r := sin(phase_r)
			target_l += Vector2(58.0 * stroke_amp - sweep_l * 46.0 * stroke_amp, -reaches_l * 46.0 * stroke_amp - 12.0)
			target_r += Vector2(-58.0 * stroke_amp + sweep_r * 46.0 * stroke_amp, -reaches_r * 46.0 * stroke_amp - 12.0)
			target_rot_l = sweep_l * 0.28 * stroke_amp + 0.14
			target_rot_r = -sweep_r * 0.28 * stroke_amp - 0.14
			target_scale_l = Vector2.ONE * clampf(1.0 - reaches_l * 0.11 * stroke_amp, 0.82, 1.10)
			target_scale_r = Vector2.ONE * clampf(1.0 - reaches_r * 0.11 * stroke_amp, 0.82, 1.10)

	# 1. World-simulated climbing: move hands directly onto the projected 3D block handholds
	if _is_climbing and _climb_holds_valid:
		# The arc comes from the world-space reach; here we only add the latch jolt.
		var reach_arc_l := Vector2(0.0, 14.0 * _grip_pulse_l)
		var reach_arc_r := Vector2(0.0, 14.0 * _grip_pulse_r)
		left_sprite.texture = relaxed_hand if _climb_reaching_left else closed_hand
		right_sprite.texture = relaxed_hand if _climb_reaching_right else closed_hand

		var clamped_l := Vector2(
			clampf(_climb_hold_left_screen.x, vp_size.x * 0.08, vp_size.x * 0.62),
			clampf(_climb_hold_left_screen.y, vp_size.y * 0.18, vp_size.y * 0.92)
		)
		var clamped_r := Vector2(
			clampf(_climb_hold_right_screen.x, vp_size.x * 0.38, vp_size.x * 0.92),
			clampf(_climb_hold_right_screen.y, vp_size.y * 0.18, vp_size.y * 0.92)
		)
		target_l = _base_left_offset + (clamped_l - left_rest_screen) - left_run.position - left_action.position + reach_arc_l
		target_r = _base_right_offset + (clamped_r - right_rest_screen) - right_run.position - right_action.position + reach_arc_r
		target_rot_l = (0.18 if not _climb_reaching_left else -0.22) - 0.10 * _grip_pulse_l
		target_rot_r = (-0.18 if not _climb_reaching_right else 0.22) + 0.10 * _grip_pulse_r
		# Reaching hands open and stretch toward the wall (smaller = further away).
		target_scale_l = Vector2(1.0 + 0.10 * _grip_pulse_l, 1.0 - 0.12 * _grip_pulse_l) * (0.90 if _climb_reaching_left else 1.0)
		target_scale_r = Vector2(1.0 + 0.10 * _grip_pulse_r, 1.0 - 0.12 * _grip_pulse_r) * (0.90 if _climb_reaching_right else 1.0)

	# Holding hands are locked to their grip; only the reaching hand eases.
	var follow_speed := 42.0 if _is_climbing else (16.0 if _is_swimming else 14.0)
	var blend := clampf(dt * follow_speed, 0.0, 1.0)
	_cur_left_offset = _cur_left_offset.lerp(target_l, blend)
	_cur_right_offset = _cur_right_offset.lerp(target_r, blend)

	var action_offset_l := Vector2.ZERO
	var action_offset_r := Vector2.ZERO

	# 2. World interaction: placing and tool swings travel to the real block on
	# screen, shrink with its distance and land with a hit-stop, then spring home.
	if _action_kind != "":
		if _hitstop > 0.0:
			_hitstop = maxf(0.0, _hitstop - dt)
		else:
			_action_time += dt
		var u := clampf(_action_time / maxf(_action_duration, 0.01), 0.0, 1.0)
		if u >= 1.0:
			_action_kind = ""
			_world_target_valid = false
			_reset_grip_angles()
		else:
			var clamped_target := vp_size * 0.5
			if _world_target_valid:
				clamped_target = Vector2(
					clampf(_world_target_screen.x, vp_size.x * 0.10, vp_size.x * 0.90),
					clampf(_world_target_screen.y, vp_size.y * 0.10, vp_size.y * 0.90)
				)
			var cur_screen_r := Vector2(vp_size.x * right_root.anchor_left, vp_size.y) + _cur_right_offset + right_run.position
			var cur_screen_l := Vector2(vp_size.x * left_root.anchor_left, vp_size.y) + _cur_left_offset + left_run.position
			var depth := _depth_scale()

			if _action_kind == "place":
				# Reach (ease-out) -> press the block in with a squash -> spring back.
				var palm_anchor := Vector2(-42.0, -10.0)
				var to_block := clamped_target - cur_screen_r - right_action.position
				var reach := 0.0
				var squash := 0.0
				if u < 0.42:
					reach = _ease_out_cubic(u / 0.42)
				elif u < 0.58:
					reach = 1.0
					squash = sin((u - 0.42) / 0.16 * PI)
				else:
					reach = 1.0 - _ease_in_out((u - 0.58) / 0.42)
				var sc := lerpf(1.0, depth, reach)
				action_offset_r = (to_block - palm_anchor * sc) * reach + Vector2(0.0, 10.0 * squash)
				target_scale_r = Vector2(sc * (1.0 + 0.10 * squash), sc * (1.0 - 0.12 * squash))
				target_rot_r = -0.22 * reach + 0.08 * squash
			elif _action_kind == "break":
				var r_eq: String = equipped["right"]
				var l_eq: String = equipped["left"]
				var use_left := (r_eq == "" and l_eq != "")
				var has_tool := (l_eq != "") if use_left else (r_eq != "")
				var base_scr := cur_screen_l if use_left else cur_screen_r
				var mirror := -1.0 if use_left else 1.0
				# Work in "right hand" space; the left hand mirrors X.
				var to_block := clamped_target - base_scr
				to_block.x *= mirror
				var pose := _swing_pose(u, to_block, depth, has_tool)
				if u >= SWING_IMPACT and not _impact_fired:
					_impact_fired = true
					_impact_kick = 1.0
					if _world_target_valid:
						_hitstop = HITSTOP_TIME
					_fire_break_impact()
				var pos: Vector2 = pose["pos"]
				var shake := Vector2.ZERO
				if _impact_kick > 0.0:
					shake = Vector2(randf_range(-1.0, 1.0), randf_range(-1.0, 1.0)) * 5.0 * _impact_kick
				var squash: float = pose["squash"]
				var sc: float = pose["scale"]
				var scale_v := Vector2(sc * (1.0 + 0.09 * squash), sc * (1.0 - 0.11 * squash))
				var grip_delta: Vector3 = pose["grip"]
				var grip_pos: Vector3 = pose["grip_pos"]
				if use_left:
					action_offset_l = Vector2(-pos.x, pos.y) + shake
					target_rot_l = -float(pose["rot"])
					target_scale_l = scale_v
					if left_grip and l_eq != "":
						left_grip.position = Vector3(-grip_pos.x, grip_pos.y, grip_pos.z)
						left_grip.rotation_degrees = _base_tool_angles("left", l_eq) + Vector3(grip_delta.x, -grip_delta.y, -grip_delta.z)
				else:
					action_offset_r = pos + shake
					target_rot_r = pose["rot"]
					target_scale_r = scale_v
					if right_grip and r_eq != "":
						right_grip.position = grip_pos
						right_grip.rotation_degrees = _base_tool_angles("right", r_eq) + grip_delta
		_last_action_offset_l = action_offset_l
		_last_action_offset_r = action_offset_r
		_last_action_rot_l = target_rot_l
		_last_action_rot_r = target_rot_r
	else:
		_reset_grip_angles()
		if _combo_reset_timer > 0.0:
			_combo_reset_timer = maxf(0.0, _combo_reset_timer - dt)
			if _combo_reset_timer <= 0.0:
				_next_combo_step = 0
		var decay := clampf(dt * 14.0, 0.0, 1.0)
		_last_action_offset_l = _last_action_offset_l.lerp(Vector2.ZERO, decay)
		_last_action_offset_r = _last_action_offset_r.lerp(Vector2.ZERO, decay)
		_last_action_rot_l = lerpf(_last_action_rot_l, 0.0, decay)
		_last_action_rot_r = lerpf(_last_action_rot_r, 0.0, decay)
		action_offset_l = _last_action_offset_l
		action_offset_r = _last_action_offset_r
		target_rot_l += _last_action_rot_l
		target_rot_r += _last_action_rot_r

	_set_root_offset(left_root, _cur_left_offset + action_offset_l)
	_set_root_offset(right_root, _cur_right_offset + action_offset_r)
	# Hands drifting over the middle of the screen fade a little so they never hide the target.
	var cx := vp_size.x * 0.5
	var half_w := maxf(vp_size.x * 0.5, 1.0)
	var lx := vp_size.x * left_root.anchor_left + left_root.offset_left
	var rx := vp_size.x * right_root.anchor_left + right_root.offset_left
	left_root.modulate.a = lerpf(CENTER_FADE_ALPHA, 1.0, smoothstep(0.18, 0.50, absf(lx - cx) / half_w))
	right_root.modulate.a = lerpf(CENTER_FADE_ALPHA, 1.0, smoothstep(0.18, 0.50, absf(rx - cx) / half_w))
	var pose_blend := 1.0 if _action_kind != "" else blend
	left_root.scale = left_root.scale.lerp(target_scale_l, pose_blend)
	right_root.scale = right_root.scale.lerp(target_scale_r, pose_blend)
	left_root.rotation = lerpf(left_root.rotation, target_rot_l, pose_blend)
	right_root.rotation = lerpf(right_root.rotation, target_rot_r, pose_blend)
