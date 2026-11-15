"""Create a disposable project view; never modify the user's generation defaults."""
import json
from pathlib import Path
import re
import shutil
import sys

source = Path(__file__).resolve().parents[1]
target = Path(sys.argv[1]).resolve()
target.mkdir(parents=True, exist_ok=True)
for name in ("bin", "scenes", "tests", "addons", "sounds", "icon.svg", "default_bus_layout.tres"):
    (target / name).symlink_to(source / name, target_is_directory=(source / name).is_dir())
for folder in ("game", "game/world_generation"):
    dest = target / folder
    dest.mkdir(parents=True, exist_ok=True)
    excluded = "world_generation" if folder == "game" else "data"
    for entry in (source / folder).iterdir():
        if entry.name != excluded:
            (dest / entry.name).symlink_to(entry, target_is_directory=entry.is_dir())
data = target / "game/world_generation/data"
shutil.copytree(source / "game/world_generation/data", data)
caves = json.loads((data / "caves.json").read_text())
mode = sys.argv[2] if len(sys.argv) > 2 else "legacy"
if mode != "reform":
    caves.update(enabled=True, biome_variation=True, infernal_chance=1,
                 hydrology_basin_regions=0, depths=[24, 90, 190, 340],
                 depth_transition_widths=[20, 30, 44], entrance_chance=0.45,
                 flooded_basin_chance=1, underground_lake_depth=0, region_size=256,
                 branch_chance=0, natural_entrances=False, exploration_formations=[])
    caves.pop("landmark_archetype", None)
if mode == "hydrology":
    caves.update(hydrology_basin_regions=3, entrance_chance=0,
                 underground_lake_chance=1, underground_lava_chance=0)
# The fixture world is shallower (min_y -448) than the underworld; these tests
# cover the planned chamber graph, and the noise caves have their own suite.
caves["noise"] = dict(caves.get("noise", {}), enabled=False)
(data / "caves.json").write_text(json.dumps(caves, indent=2))
settings = json.loads((data / "generation.json").read_text())
if mode != "reform":
    settings.update(generator_version=2, min_y=-448, vegetation=False, adaptive_streaming=False,
                    continental_scale=1800, mountain_scale=480, mountain_amplitude=210,
                    warp_amplitude=55, cliff_strength=10, terrace_strength=0.18, terrace_height=18)
(data / "generation.json").write_text(json.dumps(settings, indent=2))
# Unrelated editor/grass/console addons are outside this native integration
# fixture; the ordinary project runner still exercises its actual autoloads.
project = (source / "project.godot").read_text()
project = re.sub(r'^(SimpleGrass|CrabbyConsole)=.*\n', '', project, flags=re.M)
(target / "project.godot").write_text(project)
cache = target / ".godot"
cache.mkdir()
for name in ("extension_list.cfg", "global_script_class_cache.cfg", "uid_cache.bin"):
    shutil.copyfile(source / ".godot" / name, cache / name)
(cache / "imported").symlink_to(source / ".godot/imported", target_is_directory=True)
print(target)
