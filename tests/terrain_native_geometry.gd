extends SceneTree
var failures: Array[String]=[]
func check(ok: bool,message: String) -> void:
    if not ok:failures.append(message);push_error(message)
func signature(mesh: Mesh) -> Array[String]:
    var values: Array[String]=[]
    for p in mesh.get_faces():values.append(str(Vector3i((p*10000).round())))
    values.sort()
    return values
func _initialize() -> void:
    var backend=ClassDB.instantiate("TerrainMesherBridge")
    var cases: Dictionary={"isolated":[Vector3.ZERO],"line":[Vector3.ZERO,Vector3.RIGHT,Vector3(2,0,0)],"platform":[],"stairs":[],"inner":[Vector3.ZERO,Vector3.RIGHT,Vector3.BACK],"outer":[Vector3.ZERO,Vector3.RIGHT,Vector3(1,0,1)],"cubic_boundary":[Vector3.ZERO,Vector3.RIGHT],"chunk_boundary":[Vector3(31,0,0),Vector3(32,0,0)],"negative_fractional":[Vector3(-1.125,-.5,-.875),Vector3(-.125,-.5,-.875)]}
    for z in range(3):
        for x in range(3):cases.platform.append(Vector3(x,0,z))
    for x in range(4):
        for z in range(3):cases.stairs.append(Vector3(x,x,z))
    for label in cases:
        var sources: Array=[]
        for p in cases[label]:
            sources.append({"position":p,"terrain":not(label=="cubic_boundary" and p==Vector3.RIGHT),"block":3,"owner":sources.size()+1})
            # Only the top cell is a cap. All supporting volume stays cubic.
            if label=="negative_fractional":sources.append({"position":p+Vector3.DOWN,"terrain":false,"block":3,"owner":sources.size()+1})
            for y in ([] if label=="negative_fractional" else range(-1,int(p.y))):
                sources.append({"position":Vector3(p.x,y,p.z),"terrain":false,"block":3,"owner":sources.size()+1})
        var origin:=Vector3(-2,-3,-2)
        var size:=Vector3i(8,8,8)
        if label=="chunk_boundary":origin=Vector3(30,-3,-2);size=Vector3i(4,6,5)
        var result: Dictionary=backend.build(sources,origin,size,4)
        var mesh: ArrayMesh=result.mesh
        var faces:=mesh.get_faces()
        check(faces.size()>0,label+" real native Transvoxel surface")
        var edges: Dictionary={}
        for i in range(0,faces.size(),3):
            check(faces[i].is_finite() and faces[i+1].is_finite() and faces[i+2].is_finite(),label+" finite vertices")
            check((faces[i+1]-faces[i]).cross(faces[i+2]-faces[i]).length_squared()>1e-14,label+" nondegenerate triangles")
            for edge in [[0,1],[1,2],[2,0]]:
                var a:=str(Vector3i((faces[i+edge[0]]*10000).round()))
                var b:=str(Vector3i((faces[i+edge[1]]*10000).round()))
                var k:=a+":"+b if a<b else b+":"+a
                edges[k]=edges.get(k,0)+1
        var open_edges:=0
        for count in edges.values():
            if count!=2:open_edges+=1
        check(open_edges==0,label+" closed combined smooth/cubic volume")
        sources.reverse()
        var repeated: Dictionary=backend.build(sources,origin,size,4)
        check(signature(mesh)==signature(repeated.mesh),label+" deterministic under input ordering")
        if label=="chunk_boundary":
            var left: Dictionary=backend.build(sources,origin,Vector3i(2,6,5),4)
            var right: Dictionary=backend.build(sources,origin+Vector3(2,0,0),Vector3i(2,6,5),4)
            var combined:=signature(left.mesh)
            combined.append_array(signature(right.mesh))
            combined.sort()
            check(combined==signature(mesh),"Independent adjacent chunks match the full native mesh exactly")
        print("NATIVE_GEOMETRY ",label," triangles=",faces.size()/3," boundary_edges=",open_edges)
    print("TERRAIN_NATIVE_GEOMETRY_PASS" if failures.is_empty() else "TERRAIN_NATIVE_GEOMETRY_FAIL "+str(failures))
    quit(0 if failures.is_empty() else 1)
