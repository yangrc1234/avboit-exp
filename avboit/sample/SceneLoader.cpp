// SPDX-License-Identifier: MIT
#include "sample/SceneLoader.h"
#include <cgltf.h>
#include <cmath>
#include <memory>
#include <stdexcept>
namespace avboit
{
SceneData LoadSponza(const std::filesystem::path &path)
{
    cgltf_options options = {};
    cgltf_data *raw = nullptr;
    if (cgltf_parse_file(&options, path.string().c_str(), &raw) != cgltf_result_success)
        throw std::runtime_error("Cannot parse Sponza glTF");
    std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(raw, cgltf_free);
    if (cgltf_load_buffers(&options, raw, path.string().c_str()) != cgltf_result_success ||
        cgltf_validate(raw) != cgltf_result_success)
        throw std::runtime_error("Invalid glTF buffers");
    SceneData scene;
    auto imagePath = [&](const cgltf_texture_view &view)
    {
        return view.texture && view.texture->image && view.texture->image->uri
                   ? path.parent_path() / view.texture->image->uri
                   : std::filesystem::path();
    };
    for (size_t nodeIndex = 0; nodeIndex < raw->nodes_count; ++nodeIndex)
    {
        auto &node = raw->nodes[nodeIndex];
        if (!node.mesh)
            continue;
        float matrix[16];
        cgltf_node_transform_world(&node, matrix);
        auto transform = [&](const float *value, bool position)
        {
            std::array<float, 3> v;
            for (unsigned j = 0; j < 3; ++j)
                v[j] = matrix[j] * value[0] + matrix[4 + j] * value[1] + matrix[8 + j] * value[2] +
                       (position ? matrix[12 + j] : 0);
            // Match browser fixture coordinates. This asset has uniform node scale.
            return std::array<float, 3>{-v[2], v[1], v[0] + (position ? 8.f : 0.f)};
        };
        for (size_t pi = 0; pi < node.mesh->primitives_count; ++pi)
        {
            auto &primitive = node.mesh->primitives[pi];
            if (primitive.type != cgltf_primitive_type_triangles)
                continue;
            const cgltf_accessor *positions = nullptr, *normals = nullptr, *uvs = nullptr, *tangents = nullptr;
            for (size_t ai = 0; ai < primitive.attributes_count; ++ai)
            {
                auto &a = primitive.attributes[ai];
                if (a.type == cgltf_attribute_type_position)
                    positions = a.data;
                if (a.type == cgltf_attribute_type_normal)
                    normals = a.data;
                if (a.type == cgltf_attribute_type_texcoord && a.index == 0)
                    uvs = a.data;
                if (a.type == cgltf_attribute_type_tangent)
                    tangents = a.data;
            }
            if (!positions)
                continue;
            ScenePrimitive draw;
            draw.firstVertex = unsigned(scene.vertices.size() / 5);
            draw.vertexCount = unsigned(primitive.indices ? primitive.indices->count : positions->count);
            std::array<float, 4> factor = {1, 1, 1, 1};
            float cutoff = -1, rough = 1, metal = 1;
            if (auto *material = primitive.material)
            {
                auto &pbr = material->pbr_metallic_roughness;
                for (unsigned c = 0; c < 4; ++c)
                    factor[c] = pbr.base_color_factor[c];
                rough = pbr.roughness_factor;
                metal = pbr.metallic_factor;
                cutoff = material->alpha_mode == cgltf_alpha_mode_mask ? material->alpha_cutoff : -1;
                draw.albedo = imagePath(pbr.base_color_texture);
                draw.normal = imagePath(material->normal_texture);
                draw.metallicRoughness = imagePath(pbr.metallic_roughness_texture);
            }
            for (unsigned index = 0; index < draw.vertexCount; ++index)
            {
                size_t vertex = primitive.indices ? cgltf_accessor_read_index(primitive.indices, index) : index;
                float p[3] = {}, n[3] = {0, 1, 0}, uv[2] = {}, t[4] = {1, 0, 0, 1};
                cgltf_accessor_read_float(positions, vertex, p, 3);
                if (normals)
                    cgltf_accessor_read_float(normals, vertex, n, 3);
                if (uvs)
                    cgltf_accessor_read_float(uvs, vertex, uv, 2);
                if (tangents)
                    cgltf_accessor_read_float(tangents, vertex, t, 4);
                auto world = transform(p, true), normal = transform(n, false), tangent = transform(t, false);
                scene.vertices.push_back({world[0], world[1], world[2], 1});
                scene.vertices.push_back({normal[0], normal[1], normal[2], cutoff});
                scene.vertices.push_back({uv[0], uv[1], rough, metal});
                scene.vertices.push_back({tangent[0], tangent[1], tangent[2], t[3]});
                scene.vertices.push_back(factor);
            }
            scene.primitives.push_back(draw);
        }
    }
    return scene;
}
} // namespace avboit
