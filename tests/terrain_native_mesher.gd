extends SceneTree
var failures: Array[String]=[]
func check(ok: bool,message: String) -> void:
    if not ok:failures.append(message);push_error(message)
func _initialize() -> void:
    var backend=ClassDB.instantiate("TerrainMesherBridge")
    var cells: Array=[]
    for x in range(4):
        for y in range(x+1):cells.append({"position":Vector3(x,y,0),"terrain":true,"block":3,"owner":cells.size()+1})
    var start:=Time.get_ticks_usec()
    var result: Dictionary=backend.build(cells,Vector3(-1,-1,-1),Vector3i(6,6,3),4)
    print("NATIVE_TRANSVOXEL triangles=",result.triangles," elapsed_ms=",(Time.get_ticks_usec()-start)/1000.0)
    var mesh: ArrayMesh=result.mesh
    if mesh==null:
        check(false,"Native mesh resource survives its return to Godot")
        quit(1)
        return
    check(result.owners.size()==result.triangles,"Each triangle carries an original block owner")
    for owner in result.owners:check(owner>=1 and owner<=10,"Owner points to an original source block")
    for block in result.blocks:check(block==3,"Triangle retains original block material ID")
    check(mesh.get_surface_count()>0,"Native bulk SDF produces Transvoxel mesh")
    var faces:=mesh.get_faces()
    var edges: Dictionary={}
    for i in range(0,faces.size(),3):
        check(faces[i].is_finite(),"Native mesh coordinates finite")
        check((faces[i+1]-faces[i]).cross(faces[i+2]-faces[i]).length_squared()>1e-14,"Native mesh has no degenerate triangles")
        for edge in [[0,1],[1,2],[2,0]]:
            var a:=str(Vector3i((faces[i+edge[0]]*10000).round()))
            var b:=str(Vector3i((faces[i+edge[1]]*10000).round()))
            var key:=a+":"+b if a<b else b+":"+a
            edges[key]=edges.get(key,0)+1
    var open_edges:=0
    for count in edges.values():
        if count!=2:open_edges+=1
    check(open_edges==0,"Native staircase is closed")
    check(cells.size()==10 and cells[0].block==3,"Meshing preserves authoritative block identity")
    print("NATIVE_TERRAIN_MESHER_PASS" if failures.is_empty() else "NATIVE_TERRAIN_MESHER_FAIL")
    quit(0 if failures.is_empty() else 1)
