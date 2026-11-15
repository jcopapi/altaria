instance uniform ivec4 seam_edges = ivec4(0);
instance uniform float terrain_lod = 0.0;

bool terrain_edge_replaced(float packed, vec2 tile_xz) {
    int edge = int(mod(floor(packed / 1024.0), 8.0)) - 1;
    if (edge < 0 || edge >= 4) {
        return false;
    }
    float along = edge < 2 ? tile_xz.x : tile_xz.y;
    int bit = 1 << clamp(int(floor(along * exp2(-terrain_lod))), 0, 31);
    return (edge == 0 && (seam_edges.x & bit) != 0) ||
           (edge == 1 && (seam_edges.y & bit) != 0) ||
           (edge == 2 && (seam_edges.z & bit) != 0) ||
           (edge == 3 && (seam_edges.w & bit) != 0);
}
