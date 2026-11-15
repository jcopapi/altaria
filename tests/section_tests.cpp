#include "game/world_generation/sections.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace altaria;
static double area(const Surface &s) {
    double total = 0;
    for (size_t t = 0; t + 2 < s.indices.size(); t += 3) {
        const auto &a = s.vertices[s.indices[t]], &b = s.vertices[s.indices[t + 1]], &c = s.vertices[s.indices[t + 2]];
        const double ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z, vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
        const double cx = uy * vz - uz * vy, cy = uz * vx - ux * vz, cz = ux * vy - uy * vx;
        total += .5 * std::sqrt(cx * cx + cy * cy + cz * cz);
    }
    return total;
}
static Registry blocks() {
    Registry r;std::string error;
    const char *ids[]={"air","stone","dirt","grass","sand","water","snow","bedrock"};
    for(int i=0;i<8;i++){BlockDef b;b.id=i;b.key=ids[i];b.solid=i!=0 && i!=5;b.water=i==5;assert(r.add(b,error));}
    BlockDef lava;lava.id=13;lava.key="altaria:lava";lava.solid=false;lava.liquid=true;lava.lava=true;assert(r.add(lava,error));
    r.lava=13;
    return r;
}
int main() {
    int failures = 0;
    auto check = [&](bool ok, const char *what) { if (!ok) { std::cerr << "FAIL " << what << "\n"; ++failures; } };
    // A tall wall quad and a sloped triangle crossing several 32 m bands.
    Surface wall;
    wall.vertices = {{0, -40, 0, 1, 0, 0, 0, 0, 1, 1, 1}, {0, 70, 0, 1, 0, 0, 0, 1, 1, 1, 1},
                     {0, 70, 5, 1, 0, 0, 1, 1, 1, 1, 1}, {0, -40, 5, 1, 0, 0, 1, 0, 1, 1, 1},
                     {3, 1, 0, 0, 1, 0, 0, 0, 1, 1, 1}, {3, 33, 2, 0, 1, 0, 0, 0, 1, 1, 1}, {4, 64, 0, 0, 1, 0, 0, 0, 1, 1, 1}};
    wall.indices = {0, 1, 2, 0, 2, 3, 4, 5, 6};
    auto parts = split_sections(wall, Surface{});
    double split_area = 0;
    for (const auto &part : parts) {
        split_area += area(part.solid);
        for (const auto &v : part.solid.vertices)
            check(v.y >= part.band * section_height - 1e-3f && v.y <= (part.band + 1) * section_height + 1e-3f,
                  "every clipped vertex lies inside its band");
    }
    check(parts.size() == 5, "a wall from y -40 to 70 spans the five bands -2..2");
    check(std::abs(split_area - area(wall)) < 1e-3, "clipping keeps the total surface area");
    // A real full-resolution tile, including caves and liquids, splits losslessly.
    Settings cfg;cfg.min_y=-704;cfg.vegetation=false;cfg.caves.enabled=true;cfg.caves.noise.enabled=true;
    auto r=blocks();Generator g(cfg,r,1337);
    auto tile = build_tile({0, 0, 0}, 0, g, std::make_shared<EditMap>(), std::make_shared<AppearanceMap>(), false,
                           VerticalWindow{VerticalWindow::underground, -620, -430});
    double solid = 0, water = 0;
    size_t vertices = 0;
    for (const auto &part : split_sections(tile.solid, tile.water)) {
        solid += area(part.solid);
        water += area(part.water);
        vertices += part.solid.vertices.size() + part.water.vertices.size();
    }
    const size_t original = tile.solid.vertices.size() + tile.water.vertices.size();
    std::cout << "tile vertices " << original << " -> " << vertices << ", area " << area(tile.solid) << " -> " << solid << "\n";
    check(std::abs(solid - area(tile.solid)) < 1e-2 * std::max(1.0, area(tile.solid) * 1e-4), "tile solid area preserved");
    check(std::abs(water - area(tile.water)) < 1e-2 * std::max(1.0, area(tile.water) * 1e-4), "tile liquid area preserved");
    check(vertices < original * 11 / 10, "sections add under 10% vertices");
    if (failures) return 1;
    std::cout << "PASS section tests\n";
}
