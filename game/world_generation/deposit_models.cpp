#include "voxel_world.h"
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/resource_loader.hpp>

namespace godot {
using namespace altaria;

bool VoxelWorld::load_deposit_model(const BlockDef &block, String &error) {
    const String path(block.deposit_model.c_str());
    Ref<PackedScene> scene = ResourceLoader::get_singleton()->load(path);
    if (scene.is_null()) {
        error = "Could not load deposit model " + path;
        return false;
    }
    DepositModel model;
    Node *root = scene->instantiate();
    const auto collect = [&](auto &&visit, Node *node, Transform3D transform) -> void {
        if (const auto *spatial = Object::cast_to<Node3D>(node))
            transform = transform * spatial->get_transform();
        if (const auto *instance = Object::cast_to<MeshInstance3D>(node)) {
            Ref<Mesh> mesh = instance->get_mesh();
            if (mesh.is_valid()) {
                const AABB bounds = transform.xform(mesh->get_aabb());
                model.bounds = model.parts.empty() ? bounds : model.bounds.merge(bounds);
                model.parts.push_back({mesh, transform});
            }
        }
        for (int i = 0; i < node->get_child_count(); ++i)
            visit(visit, node->get_child(i), transform);
    };
    if (root) {
        collect(collect, root, Transform3D());
        memdelete(root);
    }
    if (model.parts.empty() || !model.bounds.has_volume()) {
        error = "Deposit model has no 3D mesh " + path;
        return false;
    }
    deposit_models.emplace(block.id, std::move(model));
    return true;
}

void VoxelWorld::upload_deposits(Tile &tile, const std::vector<ResourcePiece> &pieces) {
    for (auto *old : tile.deposit_batches) {
        tile.node->remove_child(old);
        old->queue_free();
    }
    tile.deposit_batches.clear();
    for (const auto &[block, model] : deposit_models) {
        std::vector<Transform3D> instances;
        for (const auto &piece : pieces) {
            if (piece.block != block)
                continue;
            const auto axis = [&](int i) {
                const auto &a = piece.axes[i];
                return Vector3(a[0], a[1], a[2]) * (piece.dimensions[i] / model.bounds.size[i]);
            };
            const Basis basis(axis(0), axis(1), axis(2));
            const Vector3 position(piece.position[0], piece.position[1], piece.position[2]);
            instances.emplace_back(basis, position - basis.xform(model.bounds.get_center()));
        }
        if (instances.empty())
            continue;
        for (const auto &part : model.parts) {
            Ref<MultiMesh> batch;
            batch.instantiate();
            batch->set_transform_format(MultiMesh::TRANSFORM_3D);
            batch->set_mesh(part.mesh);
            batch->set_instance_count(int(instances.size()));
            for (int i = 0; i < int(instances.size()); ++i)
                batch->set_instance_transform(i, instances[i] * part.transform);

            auto *node = memnew(MultiMeshInstance3D);
            node->set_name(String(registry().get(block).key.c_str()).get_slice(":", 1) + String("_deposit"));
            node->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
            node->set_multimesh(batch);
            tile.node->add_child(node, true);
            tile.deposit_batches.push_back(node);
        }
    }
}
}
