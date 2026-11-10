class_name TerrainSdfField
extends RefCounted
## Immutable input for Voxel Tools workers. Original blocks remain authoritative.
const RESOLUTION := 4
const ROUNDING := 0.22
var blocks: Dictionary
var modified: Dictionary
func _init(source: Dictionary={}, modifiers: Dictionary={}) -> void:
    blocks=source.duplicate(true)
    modified=modifiers.duplicate(true)
    blocks.make_read_only()
    modified.make_read_only()
func distance_at(p: Vector3) -> float:
    var cell:=Vector3i(p.floor())
    var result:=4.0
    var cubic:=4.0
    for z in range(cell.z-1,cell.z+2):
        for y in range(cell.y-1,cell.y+2):
            for x in range(cell.x-1,cell.x+2):
                var key:=Vector3i(x,y,z)
                if not blocks.has(key) or blocks[key]==0:continue
                var radius:=ROUNDING if modified.has(key) else 0.0
                var q: Vector3=(p-Vector3(key)-Vector3.ONE*.5).abs()-Vector3.ONE*(.5-radius)
                var d:=q.max(Vector3.ZERO).length()+minf(maxf(q.x,maxf(q.y,q.z)),0.0)-radius
                # Smooth only modified-to-modified unions. Cubic neighbors keep
                # their exact boundary; original block/material data is untouched.
                if not modified.has(key):
                    cubic=minf(cubic,d)
                elif result<3.0:
                    var h:=maxf(0.18-absf(result-d),0.0)/0.18
                    result=minf(result,d)-h*h*.045
                else:result=minf(result,d)
    return minf(result,cubic)
