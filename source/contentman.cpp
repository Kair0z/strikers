#include "contentman.h"
#include <filesystem>

#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/postprocess.h"	// Post processing flags

#pragma comment(lib, "assimp.lib")

namespace strikers {
void contentman::scan_assets_in_folder(const stringview& folderpath, asset_scan_report* report)
{
    for (const auto& entry : std::filesystem::recursive_directory_iterator(folderpath))
    {
        if (entry.is_regular_file())
        {
            // report
            if (report) {}

            const auto& extension = entry.path().extension();
            if (extension == ".fbx")
            {
                load_fbx(entry.path().string()).claim();
            }
            else if (extension == ".obj")
            {
                load_obj(entry.path().string()).claim();
            }
        }
    }
}

result<asset_id> contentman::load_assimp_file(const stringview& filepath)
{
    using restype = result<asset_id>;
    if (!std::filesystem::exists(filepath))
    {
        return restype::make_fail("file not found!");
    }
    if (!std::filesystem::is_regular_file(filepath))
    {
        return restype::make_fail("filepath is not a file!");
    }

    asset_data root_node{};
    root_node.type = asset_type::none;
    asset_id root_id = m_asset_graph.add_node(root_node);

    // load the stuff (assimp)
    {
        Assimp::Importer assimp{};
        int flags =
            aiProcess_CalcTangentSpace |
            aiProcess_Triangulate |
            aiProcess_JoinIdenticalVertices |
            aiProcess_FlipUVs |
            aiProcess_PopulateArmatureData |
            aiProcess_SortByPType |
            aiProcess_PreTransformVertices;

        const aiScene* scene = assimp.ReadFile(string(filepath), flags);
        for (uint32 i = 0u; i < scene->mNumMeshes; ++i)
        {
            const auto& mesh = scene->mMeshes[i];

            mesh_asset asset{};
            asset.m_mesh_id = make_mesh_id(filepath, i);
            asset.m_vertices.resize(mesh->mNumVertices);
            asset.m_normals.resize(mesh->mNumVertices);
            for (uint32 v = 0; v < mesh->mNumVertices; ++v)
            {
                memcpy(&asset.m_vertices[v], &mesh->mVertices[v], sizeof(float3));
                memcpy(&asset.m_normals[v], &mesh->mNormals[v], sizeof(float3));
            }
            for (uint32 f = 0; f < mesh->mNumFaces; ++f)
            {
                const auto& face = mesh->mFaces[f];
                for (uint32 idx = 0; idx < face.mNumIndices; ++idx)
                {
                    asset.m_indices.push_back({});
                    memcpy(&asset.m_indices.back(), &face.mIndices[idx], sizeof(uint32));
                }
            }
            m_mesh_assets.push_back(asset);

            asset_data mesh_node{};
            mesh_node.m_asset_idx = (uint32)m_mesh_assets.size() - 1;
            mesh_node.type = asset_type::mesh;
            asset_id mesh_asset_id = m_asset_graph.add_node(mesh_node, root_id);

            // map meshid -> asset
            m_meshid_to_asset[asset.m_mesh_id] = mesh_asset_id;
        }
    }
    return root_id;
}

result<asset_id> contentman::load_obj(const stringview& filepath)
{
    using restype = result<asset_id>;
    if (!std::filesystem::exists(filepath))
    {
        return restype::make_fail("file not found!");
    }
    if (!std::filesystem::is_regular_file(filepath))
    {
        return restype::make_fail("filepath is not a file!");
    }
    if (std::filesystem::path(filepath).extension() != ".obj")
    {
        return restype::make_fail("file at path is not .obj!");
    }
    return load_assimp_file(filepath);
}

result<asset_id> contentman::load_fbx(const stringview& filepath)
{
	using restype = result<asset_id>;
    if (!std::filesystem::exists(filepath))
    {
        return restype::make_fail("file not found!");
    }
    if (!std::filesystem::is_regular_file(filepath))
    {
        return restype::make_fail("filepath is not a file!");
    }
    if (std::filesystem::path(filepath).extension() != ".fbx")
    {
        return restype::make_fail("file at path is not .fbx!");
    }
    return load_assimp_file(filepath);
}
}