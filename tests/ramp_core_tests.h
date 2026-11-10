static void test_ramp_connections() {
    require(SHAPE_COUNT == 9 && shape_id("terrain") == 8 && std::string(shape_name(8)) == "terrain",
            "Terrain registered as selectable surface modifier");
    for(int scale=0;scale<3;scale++)for(int corner=0;corner<5;corner++)for(int turn=0;turn<4;turn++) {
        auto shape=ramp_shape(RampScale(scale),RampCorner(corner),turn);
        Appearance a;unpack_shape(shape,a);
        require(pack_shape(a.shape,a.rotation)==shape,"Ramp variants preserve rotation through serialization");
        for(float x:{.15f,.45f,.8f})for(float z:{.1f,.55f,.9f}) {
            float top=shape_top(shape,x,z);
            require(std::abs(shape_ray(shape,{x,2,z},{0,-1,0},3)-(2-top))<.0002f,"Straight and corner ramp rays match visible top");
            require(!shape_intersects(shape,{x-.001f,top+.01f,z-.001f},{x+.001f,top+.02f,z+.001f}),"No invisible cube above ramp variants");
            if(top>.03f)require(shape_intersects(shape,{x-.001f,top-.02f,z-.001f},{x+.001f,top-.01f,z+.001f}),"Ramp collision matches cap");
        }
    }
    // Verify all 81 Terrain corner-height combinations (codes 32..112) for raycast, top height, collision, and rotational symmetry.
    for (int c0 = 0; c0 < 3; c0++)
        for (int c1 = 0; c1 < 3; c1++)
            for (int c2 = 0; c2 < 3; c2++)
                for (int c3 = 0; c3 < 3; c3++) {
                    uint8_t tshape = terrain_shape({c0, c1, c2, c3});
                    require(shape_type(tshape) == 8 && terrain_spec(tshape).valid, "Terrain profile code is valid");
                    for (float x : {.2f, .5f, .8f})
                        for (float z : {.2f, .5f, .8f}) {
                            float top = shape_top(tshape, x, z);
                            require(std::abs(shape_ray(tshape, {x, 2, z}, {0, -1, 0}, 3) - (2 - top)) < .001f,
                                    "Terrain raycast matches exact triangulated surface top");
                            require(!shape_intersects(tshape, {x - .001f, top + .015f, z - .001f},
                                                      {x + .001f, top + .025f, z + .001f}),
                                    "No phantom collision above Terrain surface");
                            if (top > .04f)
                                require(shape_intersects(tshape, {x - .001f, top - .025f, z - .001f},
                                                         {x + .001f, top - .015f, z + .001f}),
                                        "Terrain collision matches surface cap");
                        }
                }
    // Verify connect_terrain geometric cases: isolated block, 2x2 platform, 3x3 platform, staircase-to-ramp, and 4-way corner symmetry.
    uint8_t isolated = connect_terrain([](int dx, int dy, int dz) { return dx == 0 && dy == 0 && dz == 0; });
    require(isolated == terrain_shape({1, 1, 1, 1}), "Isolated Terrain block bevels all four corners to 0.5");
    uint8_t center3x3 = connect_terrain([](int, int dy, int) { return dy == 0; });
    require(center3x3 == terrain_shape({2, 2, 2, 2}), "Interior of 3x3 Terrain platform stays flat at 1.0");
    uint8_t corner2x2 = connect_terrain([](int dx, int dy, int dz) { return dy == 0 && dx >= 0 && dz >= 0; });
    require(corner2x2 == terrain_shape({0, 1, 2, 1}), "2x2 Terrain platform raises shared center vertex to 1.0");
    uint8_t stair_step = connect_terrain([](int dx, int dy, int) {
        return (dy == 0 && dx >= 0) || (dy == 1 && dx >= 1);
    });
    require(stair_step == terrain_shape({0, 0, 2, 2}),
            "Staircase Terrain step connects low edge at 0.0 to upper step at 1.0 as a continuous ramp");
    uint8_t outer_corner = connect_terrain([](int dx, int dy, int dz) {
        return (dy == 0 && dx >= 0 && dz >= 0) || (dy == 1 && dx >= 1 && dz >= 1);
    });
    require(outer_corner == terrain_shape({0, 0, 2, 0}),
            "Diagonal upper neighbor creates smooth outer corner ramp");
    uint8_t inner_corner = connect_terrain([](int dx, int dy, int dz) {
        return (dy == 0 && (dx >= 0 || dz >= 0)) || (dy == 1 && (dx >= 1 || dz >= 1));
    });
    require(inner_corner == terrain_shape({0, 2, 2, 2}),
            "Two perpendicular upper neighbors create smooth inner corner ramp");
    // Rotational symmetry check for inner and outer corners across all 4 orientations:
    uint8_t outer_rot[4] = {terrain_shape({2, 0, 0, 0}), terrain_shape({0, 2, 0, 0}),
                            terrain_shape({0, 0, 2, 0}), terrain_shape({0, 0, 0, 2})};
    float center_top = shape_top(outer_rot[0], .5f, .5f);
    for (int rot = 1; rot < 4; rot++)
        require(std::abs(shape_top(outer_rot[rot], .5f, .5f) - center_top) < .0001f,
                "Outer corner diagonal split is rotationally symmetric at cell center");

    auto r=registry(); Settings cfg; std::string error;
    auto empty=[](int,int,int){return std::make_pair(BlockId(0),uint8_t(0));};
    for(int turn=0;turn<4;turn++)for(int side:{1,3})for(int direction:{-1,1}) {
        ConstructionStore store;
        BuiltBlock a{1,{0,1760,0},r.grass,Appearance()};a.appearance.shape=3;a.appearance.rotation=turn;
        store.put(a);
        const int dx[]={1,0,-1,0},dz[]={0,1,0,-1};
        auto b=a;b.id=2;b.origin8.x+=dx[turn]*direction*8;b.origin8.z+=dz[turn]*direction*8;b.appearance.rotation=(turn+side)&3;
        require(store.can_place(b,r,cfg,empty,error),"Perpendicular adjacent ramp placement allowed");
        store.put(b);
        auto result=ramp_spec(store.blocks.at(1).shape());
        require(result.corner==(direction>0 ? (side==1?RampCorner::OuterRight:RampCorner::OuterLeft) : (side==1?RampCorner::InnerRight:RampCorner::InnerLeft)),"Ramps form correct inner/outer left/right corner in every direction");
        store.erase(2);
        require(ramp_spec(store.blocks.at(1).shape()).corner==RampCorner::Straight,"Ramp returns to straight after neighbor removal");
    }
    // A floor and a quarter-volume ramp form one complete support cell.
    ConstructionStore floor;
    BuiltBlock slab{1,{0,1760,0},r.grass,Appearance()};slab.appearance.shape=1;floor.put(slab);
    BuiltBlock ramp{2,{0,1764,0},r.stone,Appearance()};ramp.appearance.shape=ramp_shape(RampScale::Half,RampCorner::Straight);
    require(floor.can_place(ramp,r,cfg,empty,error),"Half ramp sits directly on a slab without overlap");floor.put(ramp);
    require(floor.collides({.85f,220.9f,.4f},{.9f,220.92f,.5f},r),"Slab-top ramp reaches ordinary block height");
    require(!floor.collides({.1f,220.7f,.4f},{.15f,220.72f,.5f},r),"Low ramp end leaves the correct empty wedge");
    cfg.surface_shapes=true;
    Generator gen(cfg,r,1337);auto edits=std::make_shared<EditMap>();auto surfaces=std::make_shared<AppearanceMap>();
    auto a=build_tile({256,128,0},0,gen,edits,surfaces,false),b=build_tile({288,128,0},0,gen,edits,surfaces,false);
    int slabs=0;
    for(auto &entry:a.volume->shapes) {slabs+=entry.second==1;require(entry.second==1,"Procedural surface only emits slabs");}
    require(slabs>20,"Slab-only surface generation preserved");
    Surface water_surface;
    append_block_surface(water_surface,{0,220,0},0,r.water,Appearance(),gen.column(0,0),r);
    require(!water_surface.vertices.empty() && water_surface.vertices.front().decal==2,
            "Water carries a shoreline region marker independent of texture or lighting");
    for(int level=1;level<6;level++) {
        auto distant=build_tile({0,0,level},0,gen,edits,surfaces,false);
        require(!distant.volume || distant.volume->shapes.empty(),"Distant LODs retain original cube generation without ramps or Terrain");
    }
    for(int z=0;z<32;z++)for(int y=0;y<a.volume->height;y++) {
        size_t ia=((z+1)*(a.volume->height+2)+(y+1))*34+33,ib=((z+1)*(b.volume->height+2)+(y+1))*34+1;
        auto sa=a.volume->shapes.find(ia),sb=b.volume->shapes.find(ib);
        require((sa==a.volume->shapes.end())==(sb==b.volume->shapes.end()),"Ramp chunk halo presence agrees");
        if(sa!=a.volume->shapes.end())require(sa->second==sb->second,"Ramp chunk halo geometry agrees");
    }
    // Cross-chunk Terrain connectivity and reversibility test (x=31 in chunk 0 and x=32 in chunk 32):
    {
        auto tedits = std::make_shared<EditMap>();
        auto tsurfaces = std::make_shared<AppearanceMap>();
        (*tedits)[{31, 220, 10}] = r.stone;
        (*tedits)[{32, 220, 10}] = r.stone;
        (*tedits)[{32, 221, 10}] = r.stone;
        Appearance ta;
        ta.shape = 8; // BlockShape::Terrain
        ta.painted = true;
        ta.tint = 0x33aa77;
        (*tsurfaces)[{31, 220, 10}] = ta;
        (*tsurfaces)[{32, 220, 10}] = ta;
        (*tsurfaces)[{32, 221, 10}] = ta;
        auto chunk_left = build_tile({0, 0, 0}, 1, gen, tedits, tsurfaces, false);
        auto chunk_right = build_tile({32, 0, 0}, 1, gen, tedits, tsurfaces, false);
        int iy = 220 - cfg.min_y;
        size_t idx_left = ((10 + 1) * (chunk_left.volume->height + 2) + (iy + 1)) * 34 + (31 + 1);
        size_t idx_right_halo = ((10 + 1) * (chunk_right.volume->height + 2) + (iy + 1)) * 34 + (-1 + 1);
        require(chunk_left.volume->shapes.count(idx_left) && chunk_right.volume->shapes.count(idx_right_halo),
                "Terrain shapes exist across chunk boundary and halo");
        require(chunk_left.volume->shapes.at(idx_left) == chunk_right.volume->shapes.at(idx_right_halo) &&
                    chunk_left.volume->shapes.at(idx_left) == terrain_shape({0, 0, 2, 2}),
                "Cross-chunk Terrain step resolves identically in both chunks");
        // Revert Terrain back to Cube (shape = 0) and verify neighbor recalculation:
        Appearance cube_app = ta;
        cube_app.shape = 0;
        (*tsurfaces)[{31, 220, 10}] = cube_app;
        auto reverted = build_tile({0, 0, 0}, 2, gen, tedits, tsurfaces, false);
        require(!reverted.volume->shapes.count(idx_left) && reverted.volume->at(31, iy, 10) == r.stone,
                "Removing Terrain restores cubic geometry while preserving block material");
    }
    // Hierarchical procedural geography verification:
    {
        std::vector<float> gx, gz;
        for (int z = -1600; z <= 1600; z += 64)
            for (int x = -1600; x <= 1600; x += 64) {
                gx.push_back(float(x));
                gz.push_back(float(z));
            }
        auto cols = gen.columns(gx, gz);
        int plains = 0, foothills = 0, plateaus = 0, valleys = 0, chains = 0, oceans = 0, exposed_rock = 0,
            eroded_dirt = 0, border_rivers = 0, transition_zones = 0, buildable_zones = 0, adventure_zones = 0,
            terrace_zones = 0, arch_candidates = 0, cold_hot_adjacent = 0, beaches = 0, lakes = 0,
            stepped_banks = 0, waterfalls = 0;
        float max_peak = -1000.f;
        int best_idx = -1, best_arch_idx = -1, best_waterfall_idx = -1;
        float max_arch = 0.f;
        int grid_w = 51; // -1600..1600 step 64 -> 51x51
        for (size_t i = 0; i < cols.size(); i++) {
            const auto &c = cols[i];
            plains += (c.region == 0);
            foothills += (c.region == 1);
            plateaus += (c.region == 2 || c.plateau > .55f);
            valleys += (c.region == 3 || c.valley > .48f);
            chains += (c.region == 4 && c.chain_axis > .35f);
            oceans += (c.region == 5 || (c.continentalness < .24f && c.height <= cfg.sea_level));
            exposed_rock += (c.surface == r.stone);
            eroded_dirt += (c.surface == r.dirt);
            border_rivers += (c.river > .38f && c.transition_biome >= 0 && c.height <= cfg.sea_level);
            transition_zones += (c.transition_biome >= 0);
            buildable_zones += (c.buildable > .45f && c.slope < .16f);
            adventure_zones += (c.adventure > .55f && c.slope > .35f);
            terrace_zones += (c.terrace_step > .35f);
            arch_candidates += (c.arch_mask > .18f);
            beaches += (c.beach_mask > .18f && c.surface == r.sand);
            lakes += (c.lake_mask > .25f && c.height <= cfg.sea_level);
            stepped_banks += (c.river_bank_step > .25f && c.height > cfg.sea_level);
            if (c.waterfall_mask > .25f && c.water_top > cfg.sea_level + 4) {
                waterfalls++;
                if (best_waterfall_idx < 0)
                    best_waterfall_idx = int(i);
            }
            if (c.arch_mask > max_arch) {
                max_arch = c.arch_mask;
                best_arch_idx = int(i);
            }
            if (c.height > max_peak) {
                max_peak = c.height;
                best_idx = int(i);
            }
            // Verify Minecraft-style proximity climate: Snowfields (biome 1) and Desert (biome 2) never touch!
            int gx_i = int(i) % grid_w, gz_i = int(i) / grid_w;
            if (gx_i + 1 < grid_w) {
                const auto &nx = cols[i + 1];
                if ((c.biome == 1 && nx.biome == 2 && c.height < cfg.cold_start_altitude &&
                     nx.height < cfg.cold_start_altitude) ||
                    (c.biome == 2 && nx.biome == 1 && c.height < cfg.cold_start_altitude &&
                     nx.height < cfg.cold_start_altitude))
                    cold_hot_adjacent++;
            }
            if (gz_i + 1 < grid_w) {
                const auto &nz = cols[i + grid_w];
                if ((c.biome == 1 && nz.biome == 2 && c.height < cfg.cold_start_altitude &&
                     nz.height < cfg.cold_start_altitude) ||
                    (c.biome == 2 && nz.biome == 1 && c.height < cfg.cold_start_altitude &&
                     nz.height < cfg.cold_start_altitude))
                    cold_hot_adjacent++;
            }
        }
        require(plains > 35 && foothills > 25 && plateaus > 12 && valleys > 25 && chains > 18 && oceans > 15,
                "Hierarchical generator produces continental oceans, plains, foothills, plateaus, deep valleys, and connected mountain chains");
        require(exposed_rock > 15 && eroded_dirt > 5,
                "Geological material rules expose stone on cliffs/crests and dirt on eroded slopes");
        require(border_rivers > 5 && transition_zones > 50,
                "Rivers actively separate distinct biomes and submerge below sea_level to flow into the ocean");
        require(beaches > 8 && lakes > 5 && stepped_banks > 10 && waterfalls > 3 && best_waterfall_idx >= 0,
                "World generates sandy coastal beaches, river-connected lakes, stepped climbable riverbanks, and mountain waterfalls");
        // Verify that high-altitude mountain waterfall streams emit actual water blocks above sea_level:
        int wx_wf = int(gx[best_waterfall_idx]), wz_wf = int(gz[best_waterfall_idx]);
        auto wf_col = gen.column(wx_wf, wz_wf);
        require(gen.terrain_sample(wx_wf, wf_col.water_top, wz_wf) == r.water && wf_col.water_top > cfg.sea_level + 4,
                "Mountain stream and waterfall columns place elevated water blocks above sea_level");
        require(cold_hot_adjacent == 0,
                "Proximity climate rule prevents freezing snow biomes from spawning adjacent to hot deserts");
        require(buildable_zones > 25 && adventure_zones > 25 && terrace_zones > 20,
                "World provides both flat buildable regions, rugged adventurous regions, and geological terraces");
        require(arch_candidates > 0 && best_arch_idx >= 0,
                "Adventurous ridges form natural rock arch candidates");
        // Verify 3D natural arch carving leaves an air opening underneath a solid stone bridge:
        bool carved_3d_arch = false;
        int ax_center = int(gx[best_arch_idx]), az_center = int(gz[best_arch_idx]);
        for (int dz = -24; dz <= 24 && !carved_3d_arch; dz += 2)
            for (int dx = -24; dx <= 24 && !carved_3d_arch; dx += 2) {
                auto ac = gen.column(ax_center + dx, az_center + dz);
                if (ac.arch_mask < .18f)
                    continue;
                bool seen_air = false, seen_bridge = false;
                for (int y = cfg.sea_level + 8; y <= int(ac.height); y++) {
                    if (gen.natural_arch_carved(ac, ax_center + dx, y, az_center + dz))
                        seen_air = true;
                    else if (seen_air && gen.terrain_sample(ax_center + dx, y, az_center + dz) == r.stone)
                        seen_bridge = true;
                }
                if (seen_air && seen_bridge)
                    carved_3d_arch = true;
            }
        require(carved_3d_arch, "3D natural arch carves an open window beneath a solid stone bridge");
        require(max_peak > 240.f && best_idx >= 0,
                "Mountain chains reach giant dramatic altitudes above 240 blocks");
        // Verify that mountain summits are thick and broad ("fat" massifs) rather than thin needles:
        int px = int(gx[best_idx]), pz = int(gz[best_idx]);
        int broad_neighbors = 0;
        for (int dz = -24; dz <= 24; dz += 8)
            for (int dx = -24; dx <= 24; dx += 8) {
                if (dx == 0 && dz == 0)
                    continue;
                if (gen.column(px + dx, pz + dz).height >= max_peak - 22.f)
                    broad_neighbors++;
            }
        require(broad_neighbors >= 20,
                "Mountain summits have broad, voluminous shoulders instead of thin needle tips");
    }
    // Legacy saved profile IDs remain readable, without returning to the selector.
    require(valid_shape_id(44) && std::isfinite(shape_top(44,.4f,.3f)),"Legacy saved profiles remain readable");
    auto changed=std::make_shared<EditMap>();auto authored=std::make_shared<AppearanceMap>();
    (*changed)[{0,220,0}]=r.grass;(*changed)[{1,220,0}]=r.grass;
    Appearance first;first.shape=3;Appearance second=first;second.rotation=1;
    (*authored)[{0,220,0}]=first;(*authored)[{1,220,0}]=second;
    auto joined=build_tile({0,0,0},0,gen,changed,authored,false);
    size_t index=((1)*(joined.volume->height+2)+(220-cfg.min_y+1))*34+1;
    require(ramp_spec(joined.volume->shapes.at(index)).corner==RampCorner::OuterRight,"Meshing connects explicitly edited world ramps");
    (*changed)[{1,220,0}]=r.air;authored->erase({1,220,0});
    auto removed=build_tile({0,0,0},0,gen,changed,authored,false);
    require(ramp_spec(removed.volume->shapes.at(index)).corner==RampCorner::Straight,"Meshing removes stale world corners after voxel edits");
    first.shape=ramp_shape(RampScale::Full,RampCorner::OuterRight);first.derived_ramp=true;(*authored)[{0,220,0}]=first;
    auto bridged=build_tile({0,0,0},0,gen,changed,authored,false);
    require(ramp_spec(bridged.volume->shapes.at(index)).corner==RampCorner::OuterRight,"Meshing preserves the resolved construction bridge snapshot");
    WorldModel model(std::make_shared<Generator>(cfg,r,1337));
    require(model.apply({1,1,{0,220,0},r.grass},error),"Ramp validation fixture");
    require(!model.apply_surface({1,2,{0,220,0},first},error),"Render-only ramp connections cannot enter authoritative edits");
}
