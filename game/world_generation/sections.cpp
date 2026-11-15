#include "sections.h"
#include <algorithm>
#include <map>
namespace altaria {
static Vertex lerp_vertex(const Vertex &a, const Vertex &b, float t) {
    static_assert(sizeof(Vertex) % sizeof(float) == 0, "Vertex holds only floats");
    Vertex out;
    const float *pa = reinterpret_cast<const float *>(&a), *pb = reinterpret_cast<const float *>(&b);
    float *po = reinterpret_cast<float *>(&out);
    for (size_t i = 0; i < sizeof(Vertex) / sizeof(float); ++i)
        po[i] = pa[i] + (pb[i] - pa[i]) * t;
    return out;
}
static void split_surface(const Surface &in, bool water, std::map<int, SectionMesh> &out) {
    if (in.vertices.empty())
        return;
    // Shared corners stay shared inside the band that first uses them.
    std::vector<int32_t> remap(in.vertices.size(), -1);
    std::vector<int> owner(in.vertices.size(), 0);
    auto target = [&](int band) -> Surface & {
        auto &mesh = out[band];
        mesh.band = band;
        return water ? mesh.water : mesh.solid;
    };
    for (size_t t = 0; t + 2 < in.indices.size(); t += 3) {
        const int32_t id[3] = {in.indices[t], in.indices[t + 1], in.indices[t + 2]};
        const Vertex *v[3] = {&in.vertices[id[0]], &in.vertices[id[1]], &in.vertices[id[2]]};
        const float lo = std::min({v[0]->y, v[1]->y, v[2]->y}), hi = std::max({v[0]->y, v[1]->y, v[2]->y});
        const int first = section_band(lo);
        int last = first;
        if (hi > lo)
            last = int(std::ceil(hi / section_height)) - 1;
        if (last <= first) {
            const int band = section_band((v[0]->y + v[1]->y + v[2]->y) / 3.f);
            auto &surface = target(band);
            for (int k = 0; k < 3; ++k) {
                if (remap[id[k]] < 0 || owner[id[k]] != band) {
                    if (remap[id[k]] >= 0) {
                        // Corner already lives in another band: duplicate it.
                        surface.indices.push_back(int32_t(surface.vertices.size()));
                        surface.vertices.push_back(*v[k]);
                        continue;
                    }
                    remap[id[k]] = int32_t(surface.vertices.size());
                    owner[id[k]] = band;
                    surface.vertices.push_back(*v[k]);
                }
                surface.indices.push_back(remap[id[k]]);
            }
            continue;
        }
        for (int band = first; band <= last; ++band) {
            const float y0 = float(band * section_height), y1 = y0 + section_height;
            std::vector<Vertex> poly = {*v[0], *v[1], *v[2]};
            for (int side = 0; side < 2 && !poly.empty(); ++side) {
                std::vector<Vertex> next;
                auto inside = [&](const Vertex &p) { return side == 0 ? p.y >= y0 : p.y <= y1; };
                const float plane = side == 0 ? y0 : y1;
                for (size_t i = 0; i < poly.size(); ++i) {
                    const Vertex &a = poly[i], &b = poly[(i + 1) % poly.size()];
                    const bool ia = inside(a), ib = inside(b);
                    if (ia)
                        next.push_back(a);
                    if (ia != ib)
                        next.push_back(lerp_vertex(a, b, (plane - a.y) / (b.y - a.y)));
                }
                poly.swap(next);
            }
            if (poly.size() < 3)
                continue;
            auto &surface = target(band);
            const int32_t base = int32_t(surface.vertices.size());
            surface.vertices.insert(surface.vertices.end(), poly.begin(), poly.end());
            for (size_t i = 1; i + 1 < poly.size(); ++i) {
                surface.indices.push_back(base);
                surface.indices.push_back(base + int32_t(i));
                surface.indices.push_back(base + int32_t(i + 1));
            }
        }
    }
}
std::vector<SectionMesh> split_sections(const Surface &solid, const Surface &water) {
    std::map<int, SectionMesh> bands;
    split_surface(solid, false, bands);
    split_surface(water, true, bands);
    std::vector<SectionMesh> out;
    out.reserve(bands.size());
    for (auto &[band, mesh] : bands)
        out.push_back(std::move(mesh));
    return out;
}
} // namespace altaria
