static void test_waterbeds_and_mountain_ledges() {
    Registry r=registry();
    std::string error;
    BlockDef clay, gravel;
    clay.key="altaria:clay"; clay.id=8;
    gravel.key="altaria:gravel"; gravel.id=9;
    require(r.add(clay,error) && r.add(gravel,error),"Register waterbed sediments");

    Settings s;
    s.generator_version=3;
    s.min_y=-704;s.continental_scale=5300;s.mountain_scale=680;s.mountain_amplitude=204;
    s.warp_amplitude=34;s.cliff_strength=6;s.river_width=.028f;s.meadow_hill_amplitude=9;
    s.smooth_lowland_relief=true;s.deeper_waterbeds=true;s.mountain_ledges=true;s.ocean_depth=120;
    Generator g(s,r,1337);
    const auto check_bed=[&](int x,int z,BlockId sediment,bool ocean) {
        const auto c=g.column(x,z);
        const int floor=int(std::floor(c.height));
        require(c.surface==sediment && c.sediment_depth>=2 && (ocean?c.region==5:c.river>.85f && c.region!=5),
                "Waterbody has the expected bed sediment");
        require(g.terrain_sample(c,x,floor,z)==sediment &&
                g.terrain_sample(c,x,floor+1,z)==r.water &&
                g.terrain_sample(c,x,s.sea_level,z)==r.water,
                "Sediment, water column and water surface agree");
        return floor;
    };
    const int beach=check_bed(-152,-224,r.sand,true);
    require(s.sea_level-beach<=4,"The coastal shelf remains shallow and sandy");
    const int offshore_clay=check_bed(-272,-200,clay.id,true);
    const int offshore_gravel=check_bed(-272,-224,gravel.id,true);
    require(s.sea_level-std::max(offshore_clay,offshore_gravel)>75,
            "The open ocean has deep clay and gravel beds");
    const int abyss=check_bed(-248,-320,clay.id,true);
    require(abyss==s.min_y+1 && g.terrain_sample(-248,s.min_y,-320)==r.bedrock,
            "An abyss reaches the lowest placeable layer above bedrock");
    const TileKey abyss_tile{-256,-320,0};
    const auto tile=build_tile(abyss_tile,0,g,std::make_shared<EditMap>());
    require(tile.volume->at(8,abyss-tile.volume->min_y,0)==clay.id &&
            tile.volume->at(8,s.sea_level-tile.volume->min_y,0)==r.water &&
            tile.volume->at(8,0,0)==r.bedrock,
            "A streamed abyss retains its sediment, bedrock and continuous sea level");

    const int shallows=check_bed(64,136,gravel.id,false);
    const int pool_clay=check_bed(352,-896,clay.id,false);
    const int pool_gravel=check_bed(448,-896,gravel.id,false);
    require(s.sea_level-shallows<=6 && shallows-std::max(pool_clay,pool_gravel)>=12,
            "Riverbeds vary between shallow stretches and much deeper pools");

    const auto top=[&](int x,int z) {
        const auto c=g.column(x,z);
        const int y=int(std::floor(c.height));
        require(r.get(g.terrain_sample(c,x,y,z)).solid && g.terrain_sample(c,x,y+1,z)==r.air,
                "Alpine ledge heights match the solid voxels");
        return y;
    };
    require(std::abs(top(-147,92)-top(-146,92))==3 &&
            std::abs(top(-148,116)-top(-148,117))==4 &&
            std::abs(top(-148,139)-top(-148,140))==5,
            "Adjacent mountain columns form three, four and five block ledges by altitude");

    s.mixed_sediments=true;
    Generator mixed(s,r,1337);
    const auto check_patch=[&](int x,int z,BlockId before,BlockId after) {
        const auto original=g.column(x,z), changed=mixed.column(x,z);
        const int y=int(std::floor(changed.height));
        require(original.surface==before && changed.surface==after &&
                original.height==changed.height && original.water_top==changed.water_top,
                "Sediment patches blend contacts without moving the bank or waterline");
        require(mixed.terrain_sample(changed,x,y,z)==after,"The blended surface is a real material block");
        return y;
    };
    const int bank=check_patch(266,-6,r.grass,gravel.id);
    require(mixed.terrain_sample(266,bank-2,-6)==r.dirt,"Dry gravel patches sit over bank soil");
    check_patch(269,-8,r.grass,r.sand);
    const int sandbar=check_patch(276,4,gravel.id,r.sand);
    require(mixed.column(352,-896).surface!=r.sand && mixed.column(448,-896).surface!=r.sand,
            "Deep river pools retain their original sediment types");
    const int riverbed=check_patch(285,4,gravel.id,r.dirt);
    check_patch(-130,-257,r.sand,gravel.id);
    check_patch(-150,-251,gravel.id,r.sand);
    check_patch(-305,-335,clay.id,gravel.id);
    check_patch(-296,-334,gravel.id,clay.id);
    const auto river_tile=build_tile({256,0,0},0,mixed,std::make_shared<EditMap>());
    require(river_tile.volume->at(29,riverbed-river_tile.volume->min_y,4)==r.dirt &&
            river_tile.volume->at(29,riverbed+1-river_tile.volume->min_y,4)==r.water,
            "The streamed riverbed retains the dirt patch under the water");
    require(river_tile.volume->at(20,sandbar-river_tile.volume->min_y,4)==r.sand &&
            river_tile.volume->at(20,sandbar+1-river_tile.volume->min_y,4)==r.water,
            "The streamed sandbar extends into the river shallows");
}

static void test_resource_deposits() {
    auto r = registry();
    std::string error;
    BlockDef ore;
    ore.id = 32;
    ore.key = "altaria:ore";
    require(r.add(ore, error), "Register the shared ore block");
    Settings settings;
    settings.resources.push_back({ore.key, {"stone", "dirt", "grass", "snow", "sand"},
                                  {.7f, .3f, .2f}, -2.f, 2.f, 1.f, .2f, 4});
    Generator generator(settings, r, 1337);
    const auto edits = std::make_shared<EditMap>();
    const TileKey key{0, 0, 0};
    const auto tile = build_tile(key, 0, generator, edits);
    const auto distant = build_tile({0, 0, 1}, 0, generator, edits);
    require(std::none_of(distant.solid.vertices.begin(), distant.solid.vertices.end(), [](const Vertex &v) {
        return (int(v.decal) & 524288) != 0;
    }), "Resources do not generate distant meshes");
    require(!tile.volume->resources.empty(), "Exposed host blocks generate real resource pieces");
    const auto reference_grass = [&] {
        auto volume = *tile.volume;
        volume.resources.clear();
        return build_vegetation(key, 0, generator, volume);
    }();
    require(tile.grass.size() < reference_grass.grass.size(),
            "Surface deposits clear nearby grass in both terrain and vegetation passes");
    const auto first = tile.volume->resources.front();
    auto count = [&](const MeshResult &mesh) {
        return std::count_if(mesh.volume->resources.begin(), mesh.volume->resources.end(),
                             [&](const ResourcePiece &piece) { return piece.support == first.support; });
    };
    require(count(tile) == 4 && first.dimensions[0] < 1.f, "Several small pieces share a support block");
    const auto depleted = std::make_shared<ResourceDepletions>();
    (*depleted)[first.support] = 1u << first.slot;
    const auto mined = build_tile(key, 1, generator, edits, std::make_shared<AppearanceMap>(), false, {}, depleted);
    require(count(mined) == 3 && std::none_of(mined.volume->resources.begin(), mined.volume->resources.end(),
                [&](const ResourcePiece &piece) { return piece.support == first.support && piece.slot == first.slot; }),
            "The mined slot stays gone when the tile is rebuilt");
    (*edits)[first.support] = r.air;
    const auto broken = build_tile(key, 2, generator, edits);
    require(count(broken) == 0, "Breaking the host clears the entire deposit");

    settings.resources = {{ore.key, {"stone"}, {.7f, .3f, .2f},
                           8.f, 80.f, 1.f, .39f, 4, true, .28f}};
    Generator cavern(settings, r, 1337);
    const int floor = int(std::floor(cavern.column(12, 12).height)) - 24;
    auto carving = std::make_shared<EditMap>();
    for (int z = 8; z < 17; ++z)
        for (int x = 8; x < 17; ++x)
            for (int y = floor + 1; y < floor + 6; ++y)
                (*carving)[{x, y, z}] = r.air;
    const IVec3 pillar{12, floor + 1, 12};
    carving->erase(pillar);
    const auto chamber = build_tile(key, 0, cavern, carving);
    bool floor_deposit = false, wall_deposit = false, ceiling_deposit = false;
    bool pillar_top = false, pillar_side = false;
    int pillar_pieces = 0;
    for (const auto &piece : chamber.volume->resources) {
        const auto &p = piece.support;
        const auto &n = piece.exposed_face;
        if (!carving->count({p.x + n.x, p.y + n.y, p.z + n.z}))
            continue;
        floor_deposit |= n.y == 1;
        wall_deposit |= n.y == 0;
        ceiling_deposit |= n.y == -1;
        if (p == pillar) {
            ++pillar_pieces;
            pillar_top |= n.y == 1;
            pillar_side |= n.y == 0;
        }
        const int coordinates[] = {p.x - key.x, p.y, p.z - key.z};
        const int face_axes[] = {n.x, n.y, n.z};
        for (int axis = 0; axis < 3; ++axis) {
            if (face_axes[axis] != 0)
                continue;
            float extent = 0.f;
            for (int local = 0; local < 3; ++local)
                extent += std::abs(piece.axes[local][axis]) * piece.dimensions[local] * .5f;
            require(std::abs(piece.position[axis] - coordinates[axis] - .5f) + extent < .501f,
                    "The rotated ore stays within the host face footprint");
        }
        const float center = (piece.position[0] - p.x - .5f) * n.x +
                             (piece.position[1] - p.y - .5f) * n.y +
                             (piece.position[2] - p.z - .5f) * n.z;
        require(center > .5f && center - piece.dimensions[1] * .5f < .5f &&
                piece.dimensions[0] > .23f && piece.dimensions[0] > piece.dimensions[1],
                "Ore pieces are larger than pebbles and partially embedded in the host face");
    }
    require(floor_deposit && wall_deposit && ceiling_deposit,
            "Ore can use the floor, walls and ceiling of the same cave");
    require(pillar_top && pillar_side && pillar_pieces == 4,
            "One host distributes its four pieces among several exposed faces, including its top");
}

static void test_surface_reform() {
    test_waterbeds_and_mountain_ledges();
    test_resource_deposits();
    Registry r=registry();
    std::string error;
    BlockDef log;
    log.key="altaria:wood";log.id=9;log.wood=true;log.texture_layer=9;log.texture_end_layer=5;
    require(r.add(log,error),"Register textured log fixture");
    BlockDef leaf;
    leaf.key="altaria:leaves";leaf.id=10;leaf.foliage=true;leaf.alpha_cutout=true;
    leaf.distant_outline=false;leaf.texture_layer=10;
    require(r.add(leaf,error),"Register cutout leaf fixture");
    BlockDef rock_grass;
    rock_grass.key = "altaria:rock_grass";
    rock_grass.id = 29;
    rock_grass.grass_on_rock = true;
    rock_grass.texture_layer = 3;
    require(r.add(rock_grass, error), "Register rock grass fixture");
    Settings s;
    s.generator_version=3;
    s.min_y=-896;s.continental_scale=5300;s.mountain_scale=680;s.mountain_amplitude=204;
    s.warp_amplitude=34;s.cliff_strength=6;s.river_width=.028f;
    Generator g(s,r,1337);
    std::vector<float> xs,zs;
    for(int z=125;z<=225;z++)for(int x=-128;x<=0;x++) {
        xs.push_back(x);zs.push_back(z);
    }
    auto heights=g.columns(xs,zs);
    constexpr size_t stride=129;
    float jump=0;
    for(size_t i=0;i<heights.size();i++) {
        if(i%stride)jump=std::max(jump,std::abs(heights[i].height-heights[i-1].height));
        if(i>=stride)jump=std::max(jump,std::abs(heights[i].height-heights[i-stride].height));
    }
    require(jump<6.f,"Seed 1337 river trench and arch pillar sites have gradual height transitions");
    const auto channel=g.column(272,16);
    const auto current=g.river_current(272,16);
    require(channel.river>.5f && channel.height<s.sea_level && channel.region!=5 &&
            g.terrain_sample(272,s.sea_level,16)==r.water,
            "The new lowland valley carries a shallow inland river");
    require(std::hypot(current.first,current.second)>.25f,"The new river has a downstream current");
    Settings hilly=s;hilly.meadow_hill_amplitude=9;
    Generator meadow(hilly,r,1337);
    Settings gentle=hilly;
    gentle.smooth_lowland_relief=true;
    Generator smooth_meadow(gentle,r,1337);
    // Both the dry slope above the river and a hill farther inland must fit
    // a one-block rise (a slab step), including across corners.
    for (const auto &origin : {IVec3{250,0,-10}, IVec3{604,0,-466}}) {
        constexpr int width=70;
        xs.clear();zs.clear();
        for(int z=0;z<width;++z)
            for(int x=0;x<width;++x) {
                xs.push_back(origin.x+x);
                zs.push_back(origin.z+z);
            }
        const auto patch=smooth_meadow.columns(xs,zs);
        const auto dry=[&](int i) {
            return patch[i].height>=gentle.sea_level+3 && patch[i].mountain<=.08f;
        };
        float low=gentle.max_y, high=gentle.min_y;
        int comparisons=0;
        int largest_step=0;
        for(int z=1;z<width;++z)
            for(int x=1;x<width-1;++x) {
                const int i=z*width+x;
                if(!dry(i))
                    continue;
                low=std::min(low,patch[i].height);
                high=std::max(high,patch[i].height);
                for(int j:{i-1,i-width-1,i-width,i-width+1}) {
                    if(!dry(j))
                        continue;
                    const int step=std::abs(int(std::floor(patch[i].height))-int(std::floor(patch[j].height)));
                    largest_step=std::max(largest_step,step);
                    ++comparisons;
                }
            }
        require(comparisons>4000 && high-low>3.f,"The dry test patches still have rolling hills");
        require(largest_step<=1,"Dry rolling hills rise one block at most, even across diagonal neighbours");
    }
    const auto gentle_current=smooth_meadow.river_current(272,16);
    require(smooth_meadow.terrain_sample(272,gentle.sea_level,16)==r.water &&
            std::hypot(gentle_current.first,gentle_current.second)>.25f,
            "The gentler valley retains its flowing river channel");
    xs.clear();zs.clear();
    for(int z=-1024;z<=1024;z+=32)for(int x=-1024;x<=1024;x+=32) {
        xs.push_back(x);zs.push_back(z);
    }
    auto flat=g.columns(xs,zs),hills=meadow.columns(xs,zs);
    float highest_hill=0;
    bool snow_checked=false;
    std::optional<IVec3> mountain_grass;
    for(size_t i=0;i<hills.size();i++) {
        if(flat[i].buildable>.65f)highest_hill=std::max(highest_hill,hills[i].height-flat[i].height);
        if (!mountain_grass && hills[i].surface == r.stone && hills[i].mountain > .25f) {
            const int x = int(xs[i]), z = int(zs[i]), height = int(std::floor(hills[i].height));
            for (int y = height - 6; y <= height + 7; ++y)
                if (meadow.terrain_sample(x, y, z) == r.grass) {
                    require(meadow.terrain_sample(x, y-1, z) == r.stone, "Mountain grass is supported by intact stone");
                    mountain_grass = IVec3{x,y,z};
                    break;
                }
        }
        if(!snow_checked && hills[i].surface==r.snow && hills[i].height>150) {
            auto temp=temperature_at(meadow,EnvironmentSettings{},
                {xs[i],hills[i].height+2,zs[i]},14,Weather::Clear,0,[](int,int,int){return BlockId(0);});
            require(temp.ambient<=0.f,"Snowy summit is freezing at midday without block effects");
            snow_checked=true;
        }
    }
    require(highest_hill>3.f && highest_hill<=9.001f,"Meadow hills add small relief in buildable land");
    require(snow_checked,"Seed provides a snowy summit for the climate regression");
    require(mountain_grass.has_value(), "Seed provides a grass-covered mountain");
    const auto [gx, gy, gz] = *mountain_grass;
    const TileKey ground{int(std::floor(gx/32.0))*32, int(std::floor(gz/32.0))*32, 0};
    const auto grassy_tile = build_tile(ground,0,meadow,std::make_shared<EditMap>());
    const int y = gy - grassy_tile.volume->min_y;
    require(grassy_tile.volume->at(gx-ground.x,y,gz-ground.z) == r.grass &&
            grassy_tile.volume->at(gx-ground.x,y-1,gz-ground.z) == r.stone,
            "The streamed mountain keeps the grass and underlying rock as separate blocks");
    require(grassy_tile.volume->shapes.empty(), "Default terrain generation uses full blocks");

    Surface seam;
    TerrainEdgeColumn lower, upper;
    lower.spans.push_back({0.f, 21.f, true, {}});
    upper.spans.push_back({0.f, 24.f, true, {}});
    append_terrain_seam(seam, 0, 32.f, 12.f, 13.f, lower, upper);
    require(seam.vertices.size() == 4 && std::all_of(seam.vertices.begin(), seam.vertices.end(),
                [](const Vertex &v) { return v.x == 32.f && v.y >= 21.f && v.y <= 24.f; }),
            "A LOD seam joins the exact plane and only the exposed height");
    Surface opening;
    upper.spans = {{0.f, 18.f, true, {}}, {22.f, 24.f, true, {}}};
    lower.spans = {{0.f, 18.f, true, {}}, {22.f, 24.f, true, {}}};
    append_terrain_seam(opening, 0, 32.f, 12.f, 13.f, lower, upper);
    require(opening.vertices.empty(), "An opening crossing both edge columns remains empty");
    Surface bank;
    lower.spans = {{0.f, 19.f, true, {}}, {19.f, 21.5f, false, {}}};
    upper.spans = {{0.f, 21.f, true, {}}, {21.f, 22.f, false, {}}};
    append_terrain_seam(bank, 0, 32.f, 12.f, 13.f, lower, upper);
    require(bank.vertices.size() == 8 && bank.vertices[0].y == 21.f && bank.vertices[3].y == 19.f &&
            bank.vertices[4].y == 22.f && bank.vertices[7].y == 21.5f,
            "Mixed LOD shores close the exposed rock and water without hiding either liquid plane");
    Surface sloped_seam;
    lower.spans = {{0.f, 20.f, true, {}}, {20.f, 20.f, true, {}, 21.f}};
    upper.spans = {{0.f, 20.5f, true, {}}};
    append_terrain_seam(sloped_seam, 0, 32.f, 12.f, 13.f, lower, upper);
    require(sloped_seam.vertices.size() == 8 &&
            std::count_if(sloped_seam.vertices.begin(), sloped_seam.vertices.end(),
                          [](const Vertex &v) { return v.y == 20.5f && v.z == 12.5f; }) == 4,
            "A mixed-LOD seam follows both sides of a sloped edge through the crossing");

    auto check_log=[](const Surface &surface,float y) {
        int ends=0,sides=0;
        for(const auto &v:surface.vertices)if(v.y>=y && (v.texture==5.f || v.texture==9.f)) {
            if(std::abs(v.ny)>.5f) { require(v.texture==5.f,"Log ends use the end-grain texture");ends++; }
            else { require(v.texture==9.f,"Log sides use the bark texture");sides++; }
        }
        require(ends>0 && sides>0,"Log has independently textured ends and sides");
    };
    Surface loose;
    append_block_surface(loose,{0,0,0},0,log.id,Appearance{},Column{},r);
    check_log(loose,0);
    auto edits=std::make_shared<EditMap>();
    (*edits)[{2,305,2}]=log.id;
    (*edits)[{4,305,2}]=leaf.id;
    (*edits)[{7,304,2}]=r.stone;
    (*edits)[{7,305,2}]=leaf.id;
    auto tile=build_tile({0,0,0},0,g,edits,std::make_shared<AppearanceMap>());
    check_log(tile.solid,305.f);
    bool cutout = false;
    for (const auto &v : tile.leaf_cards.vertices) if (v.texture == 10.f && v.y >= 305.f) {
        require(int(v.decal) % 4 == 1, "Both leaf surfaces preserve the outline exclusion");
        require((int(v.decal) & 32) != 0, "Visible foliage is made entirely of textured cards");
        cutout = true;
    }
    for (const auto &v : tile.solid.vertices)
        require(!(v.texture == 10.f && v.y >= 305.f), "Leaf cards live in their own shadowless surface");
    require(cutout && !tile.leaf_shadows.vertices.empty(),
            "Leaf cards cast a solid shadow using separate geometry");
    require(std::any_of(tile.solid.vertices.begin(), tile.solid.vertices.end(), [&](const Vertex &v) {
                return v.x >= 7.f && v.x <= 8.f && v.z >= 2.f && v.z <= 3.f && v.y == 305.f &&
                    v.ny > .99f && v.texture == r.get(r.stone).texture_layer;
            }), "Ground remains visible through cutout leaves");
    for (const auto &v : tile.leaf_shadows.vertices)
        require(v.texture == 10.f && (int(v.decal) & 32) == 0, "The shadow mesh only contains opaque foliage");
    const auto &volume = *tile.volume;
    const int blade_y = 304 - volume.min_y;
    require(volume.sky_exposure(4, blade_y, 2) == 15, "Leaf canopy transmits diffuse sky fill");
    for (int z = 1; z <= 3; ++z) {
        for (int x = -8; x <= 20; ++x) {
            (*edits)[{x,305,z}] = r.stone;
            (*edits)[{x,303,z}] = r.stone;
            if (z != 2 || x == 20)
                (*edits)[{x,304,z}] = r.stone;
        }
    }
    auto entry_tile = build_tile({-32,0,0},0,g,edits);
    auto roofed = build_tile({0,0,0},0,g,edits);
    const int tunnel_y = 304 - roofed.volume->min_y;
    const int entry_y = 304 - entry_tile.volume->min_y;
    const auto mouth = entry_tile.volume->sky_exposure(24, entry_y, 2);
    const auto boundary = roofed.volume->sky_exposure(0, tunnel_y, 2);
    require(mouth == 14 && boundary == 6 && entry_tile.volume->sky_exposure(32, entry_y, 2) == boundary &&
            roofed.volume->sky_exposure(7, tunnel_y, 2) == 0,
            "Daylight reaches across the chunk boundary and fades to darkness in a tunnel");
    (*edits)[{-5,304,2}] = r.stone;
    auto sealed = build_tile({0,0,0},0,g,edits);
    require(sealed.volume->sky_exposure(0, 304 - sealed.volume->min_y, 2) == 0,
            "Closing the opening in a neighbouring chunk blocks daylight");
    auto coarse = build_tile({0,0,2},0,g,edits,std::make_shared<AppearanceMap>());
    for (const auto &v : coarse.solid.vertices)
        require(v.y < 305.f, "A single voxel edit never expands to an oversized LOD block");
    std::cout<<"PASS regional surface and block textures. Seed 1337 maximum local step: "<<jump<<" m.\n";
}
