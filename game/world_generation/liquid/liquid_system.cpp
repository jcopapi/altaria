#include "liquid_system.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace altaria {

LiquidSimulation::LiquidSimulation() {}

void LiquidSimulation::configure(const Registry &reg, std::shared_ptr<const Generator> gen,
                                 std::function<BlockId(int, int, int)> query,
                                 std::function<void(int, int, int, BlockId)> edit,
                                 std::function<bool(int,int,int)> settled_reservoir) {
    registry = reg;
    generator = std::move(gen);
    world_query = std::move(query);
    world_edit = std::move(edit);
    static_liquid=std::move(settled_reservoir);
    
    // Register default data-driven definitions
    LiquidDef water_def;
    water_def.id = 1;
    water_def.key = "water";
    water_def.block_key = "altaria:water";
    water_def.block_id = registry.water;
    water_def.name = "Water";
    water_def.name_pt = "Água";
    water_def.viscosity = 1.0f;
    water_def.tick_delay = 1;
    water_def.max_spread_rate = 64;
    water_def.current_push_strength = 9.5f;
    water_def.density = 1000.0f;
    water_def.damage_per_second = 0.0f;
    water_def.reactions.push_back({"lava", "altaria:stone", registry.stone});

    LiquidDef lava_def;
    lava_def.id = 2;
    lava_def.key = "lava";
    lava_def.block_key = "altaria:lava";
    lava_def.block_id = registry.lava != 0 ? registry.lava : (registry.keys.count("altaria:lava") ? registry.keys.at("altaria:lava") : 13);
    lava_def.name = "Lava";
    lava_def.name_pt = "Lava";
    lava_def.viscosity = 4.5f;
    lava_def.tick_delay = 4;
    lava_def.max_spread_rate = 16;
    lava_def.current_push_strength = 2.0f;
    lava_def.density = 3100.0f;
    lava_def.damage_per_second = 25.0f;
    lava_def.reactions.push_back({"water", "altaria:stone", registry.stone});

    liquid_defs[water_def.block_id] = water_def;
    liquid_keys[water_def.key] = water_def.block_id;
    liquid_keys[water_def.block_key] = water_def.block_id;

    liquid_defs[lava_def.block_id] = lava_def;
    liquid_keys[lava_def.key] = lava_def.block_id;
    liquid_keys[lava_def.block_key] = lava_def.block_id;
    
    // Try to load external data/liquids.json if present
    std::ifstream f("game/world_generation/data/liquids.json");
    if (f.is_open()) {
        std::stringstream ss;
        ss << f.rdbuf();
        load_definitions_json(ss.str());
    }
}

static std::string extract_json_field(const std::string &json, const std::string &key) {
    size_t pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos);
    if (pos == std::string::npos) return "";
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r'))
        pos++;
    if (pos >= json.size()) return "";
    if (json[pos] == '\"') {
        size_t end = json.find('\"', pos + 1);
        if (end != std::string::npos) return json.substr(pos + 1, end - pos - 1);
    } else {
        size_t end = json.find_first_of(",}\n\r", pos);
        if (end != std::string::npos) return json.substr(pos, end - pos);
    }
    return "";
}

bool LiquidSimulation::load_definitions_json(const std::string &json_str) {
    size_t cur = 0;
    while ((cur = json_str.find("\"block_key\"", cur)) != std::string::npos) {
        size_t obj_start = json_str.rfind('{', cur);
        size_t obj_end = json_str.find('}', cur);
        if (obj_start != std::string::npos && obj_end != std::string::npos) {
            std::string block = json_str.substr(obj_start, obj_end - obj_start + 1);
            std::string key = extract_json_field(block, "key");
            std::string block_key = extract_json_field(block, "block_key");
            std::string name = extract_json_field(block, "name");
            std::string name_pt = extract_json_field(block, "name_pt");
            std::string visc_str = extract_json_field(block, "viscosity");
            std::string delay_str = extract_json_field(block, "tick_delay");
            std::string spread_str = extract_json_field(block, "max_spread_rate");
            std::string push_str = extract_json_field(block, "current_push_strength");
            std::string density_str = extract_json_field(block, "density");
            std::string dmg_str = extract_json_field(block, "damage_per_second");

            BlockId bid = registry.find(block_key);
            if (bid != 0 || block_key == "altaria:water" || block_key == "altaria:lava") {
                if (bid == 0) {
                    if (block_key == "altaria:water") bid = registry.water;
                    else if (block_key == "altaria:lava") bid = registry.lava != 0 ? registry.lava : 13;
                }
                LiquidDef def;
                def.key = key.empty() ? (block_key == "altaria:water" ? "water" : "lava") : key;
                def.block_key = block_key;
                def.block_id = bid;
                def.name = name.empty() ? def.key : name;
                def.name_pt = name_pt.empty() ? def.name : name_pt;
                if (!visc_str.empty()) def.viscosity = std::stof(visc_str);
                if (!delay_str.empty()) def.tick_delay = std::max(1, std::stoi(delay_str));
                if (!spread_str.empty()) def.max_spread_rate = uint8_t(std::clamp(std::stoi(spread_str), 1, 255));
                if (!push_str.empty()) def.current_push_strength = std::stof(push_str);
                if (!density_str.empty()) def.density = std::stof(density_str);
                if (!dmg_str.empty()) def.damage_per_second = std::stof(dmg_str);

                if (def.key == "water") {
                    def.reactions.push_back({"lava", "altaria:stone", registry.stone});
                } else if (def.key == "lava") {
                    def.reactions.push_back({"water", "altaria:stone", registry.stone});
                }

                liquid_defs[bid] = def;
                liquid_keys[def.key] = bid;
                liquid_keys[def.block_key] = bid;
            }
        }
        cur += 11;
    }
    return !liquid_defs.empty();
}

const LiquidDef *LiquidSimulation::get_def(BlockId type) const {
    auto it = liquid_defs.find(type);
    if (it != liquid_defs.end()) return &it->second;
    return nullptr;
}

const LiquidDef *LiquidSimulation::get_def_by_key(const std::string &key) const {
    auto it = liquid_keys.find(key);
    if (it != liquid_keys.end()) return get_def(it->second);
    return nullptr;
}

bool LiquidSimulation::is_liquid(BlockId type) const {
    if (type == 0) return false;
    if (liquid_defs.count(type)) return true;
    if (type == registry.water || type == registry.lava) return true;
    if (type < registry.blocks.size()) {
        const auto &b = registry.blocks[type];
        return b.liquid || b.water || b.lava;
    }
    return false;
}

uint8_t LiquidSimulation::get_amount(int x, int y, int z) const {
    IVec3 pos{x, y, z};
    auto it = dynamic_cells.find(pos);
    if (it != dynamic_cells.end())
        return it->second.amount;
    BlockId b = world_query ? world_query(x, y, z) : (generator ? generator->sample(x, y, z) : 0);
    return is_liquid(b) ? MAX_LIQUID : 0;
}

BlockId LiquidSimulation::get_type(int x, int y, int z) const {
    IVec3 pos{x, y, z};
    auto it = dynamic_cells.find(pos);
    if (it != dynamic_cells.end() && it->second.amount > 0)
        return it->second.type;
    return world_query ? world_query(x, y, z) : (generator ? generator->sample(x, y, z) : 0);
}

bool LiquidSimulation::is_settled(int x, int y, int z) const {
    IVec3 pos{x, y, z};
    auto it = dynamic_cells.find(pos);
    if (it != dynamic_cells.end())
        return it->second.settled;
    return true; // natural world liquids start settled
}

void LiquidSimulation::set_cell(int x, int y, int z, BlockId type, uint8_t amount, bool settled) {
    if(static_liquid && static_liquid(x,y,z))return;
    IVec3 pos{x, y, z};
    if (amount == 0 || !is_liquid(type)) {
        remove_cell(x, y, z);
        return;
    }
    LiquidCell cell;
    cell.type = type;
    cell.amount = amount;
    cell.settled = settled;
    cell.sleep_ticks = settled ? 3 : 0;
    dynamic_cells[pos] = cell;
    if (!settled)
        wake_up(x, y, z);
}

void LiquidSimulation::remove_cell(int x, int y, int z) {
    IVec3 pos{x, y, z};
    dynamic_cells.erase(pos);
    in_active_queue.erase(pos);
}

void LiquidSimulation::wake_up(int x, int y, int z) {
    if(static_liquid && static_liquid(x,y,z))return;
    IVec3 pos{x, y, z};
    if (in_active_queue.insert(pos).second) {
        active_queue.push_back(pos);
    }
    auto it = dynamic_cells.find(pos);
    if (it != dynamic_cells.end()) {
        it->second.settled = false;
        it->second.sleep_ticks = 0;
    }
}

void LiquidSimulation::wake_neighbors(int x, int y, int z) {
    wake_up(x, y, z);
    wake_up(x + 1, y, z);
    wake_up(x - 1, y, z);
    wake_up(x, y + 1, z);
    wake_up(x, y - 1, z);
    wake_up(x, y, z + 1);
    wake_up(x, y, z - 1);
}

void LiquidSimulation::wake_region(IVec3 minimum, IVec3 maximum) {
    // Include liquids resting just across a newly available boundary.
    for(auto &[p,cell]:dynamic_cells)
        if(cell.settled && p.x>=minimum.x-1 && p.x<=maximum.x &&
           p.y>=minimum.y-1 && p.y<=maximum.y && p.z>=minimum.z-1 && p.z<=maximum.z)
            wake_up(p.x,p.y,p.z);
}

// Jon Gallant's mass-conserving vertical flow equation (adapted to 0..255 byte domain)
uint8_t LiquidSimulation::calculate_vertical_flow(uint8_t remaining, uint8_t bottom_amount, uint8_t max_spread) const {
    if (bottom_amount >= MAX_LIQUID)
        return 0;
    int room = int(MAX_LIQUID) - int(bottom_amount);
    int flow = std::min({int(remaining), int(max_spread), room});
    return uint8_t(std::max(0, flow));
}

int LiquidSimulation::total_dynamic_volume() const {
    int total = 0;
    for (const auto &pair : dynamic_cells)
        total += pair.second.amount;
    return total;
}

void LiquidSimulation::step(float delta, int max_updates_per_step) {
    (void)delta;
    tick_counter++;
    if (active_queue.empty()) return;

    std::vector<IVec3> to_process = std::move(active_queue);
    in_active_queue.clear();

    size_t processed = 0;
    for (size_t i = 0; i < to_process.size() && processed < size_t(max_updates_per_step); i++) {
        IVec3 pos = to_process[i];
        processed++;

        if(static_liquid && static_liquid(pos.x,pos.y,pos.z))continue;
        BlockId type = get_type(pos.x, pos.y, pos.z);
        if (!is_liquid(type)) continue;

        const LiquidDef *def = get_def(type);
        if (!def) continue;

        // Rate limit according to liquid tick_delay (Water flows fast every tick, Lava flows every 4 ticks)
        if (tick_counter % def->tick_delay != 0) {
            wake_up(pos.x, pos.y, pos.z);
            continue;
        }

        uint8_t amount = get_amount(pos.x, pos.y, pos.z);
        if (amount == 0) {
            remove_cell(pos.x, pos.y, pos.z);
            if (world_edit) world_edit(pos.x, pos.y, pos.z, registry.air);
            wake_neighbors(pos.x, pos.y, pos.z);
            continue;
        }

        uint8_t total_flow = 0;
        float fx = 0.f, fy = 0.f, fz = 0.f;

        // 1. Water + Lava interaction check with 6 orthogonal neighbors
        bool reacted = false;
        const IVec3 neighbors6[6] = {
            {pos.x, pos.y - 1, pos.z}, {pos.x, pos.y + 1, pos.z},
            {pos.x - 1, pos.y, pos.z}, {pos.x + 1, pos.y, pos.z},
            {pos.x, pos.y, pos.z - 1}, {pos.x, pos.y, pos.z + 1}
        };
        for (const auto &npos : neighbors6) {
            if(static_liquid && static_liquid(npos.x,npos.y,npos.z))continue;
            BlockId ntype = get_type(npos.x, npos.y, npos.z);
            if (is_liquid(ntype) && ntype != type) {
                // Water touching Lava solidifies into Stone
                BlockId product = registry.stone;
                remove_cell(pos.x, pos.y, pos.z);
                remove_cell(npos.x, npos.y, npos.z);
                if (world_edit) {
                    world_edit(pos.x, pos.y, pos.z, product);
                }
                wake_neighbors(pos.x, pos.y, pos.z);
                wake_neighbors(npos.x, npos.y, npos.z);
                reacted = true;
                break;
            }
        }
        if (reacted) continue;

        // 2. Downward Flow (Gravity into pos.y - 1)
        IVec3 below{pos.x, pos.y - 1, pos.z};
        BlockId below_block = world_query ? world_query(below.x, below.y, below.z)
                                          : (generator ? generator->sample(below.x, below.y, below.z) : 0);
        bool below_solid = below_block != 0 && below_block < registry.blocks.size() && registry.blocks[below_block].solid;

        if (!below_solid && pos.y - 1 >= (generator ? generator->settings().min_y : -64)) {
            BlockId below_type = get_type(below.x, below.y, below.z);
            if (below_type == 0 || below_type == type) {
                uint8_t below_amount = get_amount(below.x, below.y, below.z);
                if (below_amount < MAX_LIQUID) {
                    uint8_t flow = calculate_vertical_flow(amount, below_amount, def->max_spread_rate);
                    if (flow > 0) {
                        amount -= flow;
                        below_amount += flow;
                        total_flow += flow;
                        fy -= float(flow);

                        set_cell(below.x, below.y, below.z, type, below_amount, false);
                        if (world_edit && (below_type == 0 || below_type == registry.air))
                            world_edit(below.x, below.y, below.z, type);

                        wake_neighbors(below.x, below.y, below.z);
                    }
                }
            }
        }

        // 3. Lateral Flow (Horizontal Equalization across 4 neighbors +X, -X, +Z, -Z)
        if (amount > 0 && (below_solid || get_amount(below.x, below.y, below.z) >= MAX_LIQUID - 8)) {
            const IVec3 horiz[4] = {
                {pos.x - 1, pos.y, pos.z}, {pos.x + 1, pos.y, pos.z},
                {pos.x, pos.y, pos.z - 1}, {pos.x, pos.y, pos.z + 1}
            };
            const float hdir[4][2] = {{-1.f, 0.f}, {1.f, 0.f}, {0.f, -1.f}, {0.f, 1.f}};

            int valid_count = 0;
            uint8_t h_amounts[4] = {};
            bool h_valid[4] = {};

            for (int h = 0; h < 4; h++) {
                BlockId nb = world_query ? world_query(horiz[h].x, horiz[h].y, horiz[h].z)
                                         : (generator ? generator->sample(horiz[h].x, horiz[h].y, horiz[h].z) : 0);
                bool solid = nb != 0 && nb < registry.blocks.size() && registry.blocks[nb].solid;
                if (!solid) {
                    BlockId nt = get_type(horiz[h].x, horiz[h].y, horiz[h].z);
                    if (nt == 0 || nt == type) {
                        uint8_t na = get_amount(horiz[h].x, horiz[h].y, horiz[h].z);
                        if (na < amount) {
                            h_valid[h] = true;
                            h_amounts[h] = na;
                            valid_count++;
                        }
                    }
                }
            }

            if (valid_count > 0) {
                int total_mass = amount;
                for (int h = 0; h < 4; h++)
                    if (h_valid[h]) total_mass += h_amounts[h];
                uint8_t target = uint8_t(total_mass / (valid_count + 1));

                for (int h = 0; h < 4; h++) {
                    if (h_valid[h] && amount > target && target > h_amounts[h]) {
                        uint8_t diff = target - h_amounts[h];
                        uint8_t max_transfer = std::min(uint8_t(amount - target), def->max_spread_rate);
                        uint8_t flow = std::min(max_transfer, diff);
                        if (flow == 0 && amount > h_amounts[h] + 1)
                            flow = 1;
                        flow = std::min({flow, amount, uint8_t(MAX_LIQUID - h_amounts[h])});
                        if (flow > 0) {
                            amount -= flow;
                            h_amounts[h] += flow;
                            total_flow += flow;
                            fx += hdir[h][0] * float(flow);
                            fz += hdir[h][1] * float(flow);

                            set_cell(horiz[h].x, horiz[h].y, horiz[h].z, type, h_amounts[h], false);
                            if (world_edit && (world_query(horiz[h].x, horiz[h].y, horiz[h].z) == registry.air))
                                world_edit(horiz[h].x, horiz[h].y, horiz[h].z, type);

                            wake_neighbors(horiz[h].x, horiz[h].y, horiz[h].z);
                        }
                    }
                }
            }
        }

        // 4. Update current cell state and settled status
        if (amount == 0) {
            remove_cell(pos.x, pos.y, pos.z);
            if (world_edit) world_edit(pos.x, pos.y, pos.z, registry.air);
            wake_neighbors(pos.x, pos.y, pos.z);
        } else {
            auto &cell = dynamic_cells[pos];
            cell.type = type;
            cell.amount = amount;
            cell.flow_x = fx * 0.15f;
            cell.flow_y = fy * 0.15f;
            cell.flow_z = fz * 0.15f;

            if (total_flow == 0) {
                cell.sleep_ticks++;
                if (cell.sleep_ticks >= 2) {
                    cell.settled = true; // goes to sleep, zero CPU usage
                } else {
                    if (in_active_queue.insert(pos).second)
                        active_queue.push_back(pos);
                }
            } else {
                cell.settled = false;
                cell.sleep_ticks = 0;
                wake_up(pos.x, pos.y, pos.z);
            }
        }
    }

    // Keep any remaining unprocessed cells for the next slice
    for (size_t i = processed; i < to_process.size(); i++) {
        wake_up(to_process[i].x, to_process[i].y, to_process[i].z);
    }
}

std::array<float, 3> LiquidSimulation::current_at(float x, float y, float z) const {
    int ix = int(std::floor(x)), iy = int(std::floor(y)), iz = int(std::floor(z));
    BlockId type = get_type(ix, iy, iz);
    if (!is_liquid(type)) return {0.f, 0.f, 0.f};
    if(static_liquid && static_liquid(ix,iy,iz))return {0.f,0.f,0.f};

    const LiquidDef *def = get_def(type);
    float base_strength = def ? def->current_push_strength : 9.0f;

    std::array<float, 3> result = {0.f, 0.f, 0.f};

    // Dynamic flow vector from cellular automata
    IVec3 pos{ix, iy, iz};
    auto it = dynamic_cells.find(pos);
    if (it != dynamic_cells.end()) {
        result[0] += it->second.flow_x * base_strength * 0.3f;
        result[1] += it->second.flow_y * base_strength * 0.3f;
        result[2] += it->second.flow_z * base_strength * 0.3f;
    }

    // River currents: downstream direction along river channels toward the ocean
    if (generator && type == registry.water && iy <= generator->settings().sea_level + 1) {
        auto r_cur = generator->river_current(ix, iz);
        result[0] += r_cur.first * base_strength;
        result[2] += r_cur.second * base_strength;
    }

    return result;
}

bool LiquidSimulation::intersects_liquid(float min_x, float min_y, float min_z,
                                        float max_x, float max_y, float max_z,
                                        BlockId &out_type, float &out_submergence) const {
    int x0 = int(std::floor(min_x)), x1 = int(std::floor(max_x));
    int y0 = int(std::floor(min_y)), y1 = int(std::floor(max_y));
    int z0 = int(std::floor(min_z)), z1 = int(std::floor(max_z));

    int total_cells = 0, liquid_cells = 0;
    BlockId found_type = 0;

    for (int y = y0; y <= y1; y++) {
        for (int z = z0; z <= z1; z++) {
            for (int x = x0; x <= x1; x++) {
                total_cells++;
                BlockId t = get_type(x, y, z);
                if (is_liquid(t)) {
                    liquid_cells++;
                    found_type = t;
                }
            }
        }
    }

    if (liquid_cells > 0) {
        out_type = found_type;
        out_submergence = float(liquid_cells) / float(std::max(1, total_cells));
        return true;
    }
    out_type = 0;
    out_submergence = 0.f;
    return false;
}

} // namespace altaria
