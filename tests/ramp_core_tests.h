static void test_ramp_connections() {
    require(SHAPE_COUNT == 8 && shape_id("terrain") < 0, "Terrain is no longer a shape");
    for (int code : {8, 192, 208, 254})
        require(valid_shape_id(code) && shape_type(uint8_t(code == 8 ? pack_shape(code) : code)) == int(BlockShape::Cube) &&
                    shape_top(uint8_t(code == 8 ? pack_shape(code) : code), .5f, .5f) == 1.f,
                "Retired Terrain codes in old saves read as cubes");
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
    auto r=registry(); Settings cfg; std::string error;
    const auto inverted = upside_down_ramp(pack_shape(int(BlockShape::Ramp)));
    const auto inverted_turn = upside_down_ramp(pack_shape(int(BlockShape::Ramp), 1));
    const auto inverted_corner = connect_ramp(inverted, [&](int x, int z) {
        return x == 1 && z == 0 ? inverted_turn : uint8_t(0);
    });
    require(ramp_spec(inverted_corner).inverted && ramp_spec(inverted_corner).corner == RampCorner::OuterRight,
            "Inverted ramps connect to other inverted ramps");
    require(connect_ramp(inverted, [](int x, int z) { return x == 1 && z == 0 ? uint8_t(11) : uint8_t(0); }) ==
                inverted, "Inverted ramps ignore upright ramps");
    require(std::abs(shape_ray(inverted, {.25f, -1, .5f}, {0, 1, 0}, 3) - 1.75f) < .001f &&
            !shape_intersects(inverted, {.2f, .1f, .4f}, {.3f, .6f, .6f}),
            "Inverted ramp leaves the space below its underside open");
    for (int turn = 0; turn < 4; ++turn) {
        for (uint8_t tile : {pack_shape(int(BlockShape::Roof), turn), uint8_t(40 + turn), uint8_t(98 + turn)}) {
            const float top = shape_top(tile, .39f, .61f);
            const float underside = shape_ray(tile, {.39f, -1, .61f}, {0, 1, 0}, 3) - 1;
            require(std::abs(top - underside - .125f) < .001f &&
                    !shape_intersects(tile, {.389f, underside - .1f, .609f}, {.391f, underside - .02f, .611f}),
                    "Roof tile and corners have a thin, open underside");
        }
    }
    auto empty=[](int,int,int){return std::make_pair(BlockId(0),uint8_t(0));};
    for (auto form : {BlockShape::Stairs, BlockShape::Roof}) {
        ConstructionStore store;
        BuiltBlock first{1, {0, 1760, 0}, r.stone, Appearance()};
        first.appearance.shape = int(form);
        store.put(first);
        auto second = first;
        second.id = 2;
        second.origin8.x = 8;
        second.appearance.rotation = 1;
        require(store.can_place(second, r, cfg, empty, error), "Perpendicular neighbor can be placed");
        store.put(second);
        const auto corner = store.blocks.at(first.id).shape();
        require(corner != first.shape() && shape_type(corner) == int(form), "Perpendicular neighbor creates a corner");
        Appearance resolved;
        unpack_shape(corner, resolved);
        require(pack_shape(resolved.shape, resolved.rotation) == corner, "Corner survives shape serialization");
        for (float x : {.13f, .43f, .83f})
            for (float z : {.18f, .58f, .93f})
                require(std::abs(shape_ray(corner, {x, 2, z}, {0, -1, 0}, 3) -
                                 (2 - shape_top(corner, x, z))) < .001f, "Corner ray follows the visible face");
        store.erase(second.id);
        require(store.blocks.at(first.id).shape() == first.shape(), "Corner returns to straight after removal");
    }
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
    require(slabs>20,"Surface generation places slabs");
    Surface water_surface;
    append_block_surface(water_surface,{0,220,0},0,r.water,Appearance(),gen.column(0,0),r);
    require(!water_surface.vertices.empty() && (int(water_surface.vertices.front().decal) & 3) == 2,
            "Water carries a shoreline region marker independent of texture or lighting");
    for(int level=1;level<6;level++) {
        auto distant=build_tile({0,0,level},0,gen,edits,surfaces,false);
        require(!distant.volume || distant.volume->shapes.empty(),"Distant LODs retain plain cube generation");
    }
    for(int z=0;z<32;z++)for(int y=0;y<a.volume->height;y++) {
        size_t ia=((z+1)*(a.volume->height+2)+(y+1))*34+33,ib=((z+1)*(b.volume->height+2)+(y+1))*34+1;
        auto sa=a.volume->shapes.find(ia),sb=b.volume->shapes.find(ib);
        require((sa==a.volume->shapes.end())==(sb==b.volume->shapes.end()),"Ramp chunk halo presence agrees");
        if(sa!=a.volume->shapes.end())require(sa->second==sb->second,"Ramp chunk halo geometry agrees");
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
            stepped_banks = 0;
        float max_peak = -1000.f;
        int best_idx = -1, best_arch_idx = -1;
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
        require(beaches > 8 && lakes > 5 && stepped_banks > 10,
                "World generates sandy coastal beaches, river-connected lakes, and stepped climbable riverbanks");
        // Verify that naturally generated liquids stay fixed at or below sea_level (no elevated water climbing mountains)
        // and river channels carry downstream current vectors:
        int mountain_elevated_water = 0;
        int river_currents = 0;
        for (size_t i = 0; i < cols.size(); i++) {
            const auto &c = cols[i];
            if (c.river > .25f && (std::abs(c.river_flow_x) > .01f || std::abs(c.river_flow_z) > .01f))
                river_currents++;
            if (c.mountain > .4f && c.height > cfg.sea_level + 15) {
                int wx = int(gx[i]), wz = int(gz[i]);
                if (gen.terrain_sample(wx, int(c.height) + 1, wz) == r.water || c.water_top > cfg.sea_level)
                    mountain_elevated_water++;
            }
        }
        require(mountain_elevated_water == 0,
                "Naturally generated liquids remain fixed at or below sea_level with no mountain climbing water");
        require(river_currents > 10,
                "River channels provide downstream current vectors toward the ocean");
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
