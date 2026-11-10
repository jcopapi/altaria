extends SceneTree
const Field=preload("res://game/world_generation/terrain/sdf_field.gd")
const Patch=preload("res://game/world_generation/terrain/transvoxel_patch.gd")
var failures: Array[String]=[]
func check(ok: bool,message: String) -> void:
    if not ok:failures.append(message);push_error(message)
func _initialize() -> void:
    var cases: Dictionary={"isolated":[Vector3i.ZERO],"line":[Vector3i.ZERO,Vector3i.RIGHT,Vector3i(2,0,0)],"platform":[],"stairs":[],"inner":[Vector3i.ZERO,Vector3i.RIGHT,Vector3i.BACK],"outer":[Vector3i.ZERO,Vector3i.RIGHT,Vector3i(1,0,1)],"wall":[],"irregular":[Vector3i.ZERO,Vector3i(1,1,0),Vector3i(1,0,0),Vector3i(0,1,1)],"cubic_boundary":[Vector3i.ZERO,Vector3i.RIGHT],"chunk_boundary":[Vector3i(7,0,0),Vector3i(8,0,0)]}
    for z in range(3):
        for x in range(3):cases.platform.append(Vector3i(x,0,z))
    for x in range(4):
        for y in range(x+1):cases.stairs.append(Vector3i(x,y,0))
    for x in range(3):
        for y in range(3):cases.wall.append(Vector3i(x,y,0))
    for label in cases:
        var blocks: Dictionary={}
        var modifiers: Dictionary={}
        for p in cases[label]:blocks[p]=3;modifiers[p]=true
        if label=="cubic_boundary":modifiers.erase(Vector3i.RIGHT)
        var field=Field.new(blocks,modifiers)
        var origin:=Vector3i(-1,-1,-1)
        var size:=Vector3i(6,6,5)
        if label=="chunk_boundary":origin=Vector3i(6,-1,-1);size=Vector3i(4,4,4)
        var mesh: ArrayMesh=Patch.build(field,origin,size)
        var faces:=mesh.get_faces()
        check(faces.size()>0,label+" produces a real Transvoxel surface")
        var edges: Dictionary={}
        for i in range(0,faces.size(),3):
            check(faces[i].is_finite() and faces[i+1].is_finite() and faces[i+2].is_finite(),label+" finite vertices")
            check((faces[i+1]-faces[i]).cross(faces[i+2]-faces[i]).length_squared()>1e-14,label+" nondegenerate triangles")
            for edge in [[0,1],[1,2],[2,0]]:
                var a:=str(Vector3i((faces[i+edge[0]]*10000).round()))
                var b:=str(Vector3i((faces[i+edge[1]]*10000).round()))
                var key:=a+":"+b if a<b else b+":"+a
                edges[key]=edges.get(key,0)+1
        var open_edges:=0
        for count in edges.values():
            if count!=2:open_edges+=1
        check(open_edges==0,label+" closed manifold surface")
        check(blocks.size()==cases[label].size(),label+" source identity retained")
        print("TRANSVOXEL_CASE ",label," triangles=",faces.size()/3)
    print("TERRAIN_TRANSVOXEL_PASS" if failures.is_empty() else "TERRAIN_TRANSVOXEL_FAIL "+str(failures.size()))
    quit(0 if failures.is_empty() else 1)
