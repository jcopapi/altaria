# Player and entities

Change `data/entities.json` for movement speed, sprint multiplier, body dimensions,
eye height, gravity, jump impulse, reach and mouse sensitivity.

The player starts in creative flight with voxel collisions:

- WASD: move; mouse: look; Shift: sprint.
- Space / Ctrl: ascend / descend while flying.
- F: toggle flying / walking. On foot, press Space to jump and press it again while
  airborne for a double jump.
- Left mouse: remove a targeted block. Right mouse: place the selected block.
- Number keys 1–8 or mouse wheel: select a block.
- F1 or backtick: console; Esc: close console or show the world menu.

Menu/console input disables player movement and releases the cursor. Placement
inside the player body is rejected. Base bedrock cannot be edited.

The `entities` array supports data-defined placeholder entities using the
`static_box` controller. Add a unique key, `size` and `color`, then run
`spawn namespace:key` in the console. Their IDs and positions are saved with the
world. New behaviors require registering another C++ controller, while new
variants using an existing controller only need a JSON entry. Placeholder boxes
currently have no AI or physics; the player uses the `flying_player` controller.

Block forms and physics: hold **R + wheel** to choose the held block's shape,
**Q** rotates it, and **G** releases a targeted block into Jolt. Left-click wood to
cut a tree; click a fallen tree part to detach it. Walking collision follows the
actual slab/ramp/stair/pillar geometry and supports small step-up transitions.
The HUD reports apparent environment temperature in °C.
