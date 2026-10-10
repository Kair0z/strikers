#include "contentman.h"

#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/postprocess.h"	// Post processing flags
#pragma comment(lib, "assimp.lib")

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include "logman.h"
#include "commandman.h"

namespace strikers {

command cm_log_content("log_content", "0");

bool find_file(const stringview& directory, const stringview& filename, string& out_filepath)
{
    const std::filesystem::path filename_as_path{ filename };

    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) 
    {
        const string filename_a = to_lowercase(entry.path().filename().string());
        const string filename_b = to_lowercase(filename_as_path.filename().string());
        
        if (entry.is_regular_file() && filename_a == filename_b) {
            out_filepath = normalize_path(entry.path().string());
            return true;
        }
    }
    return false;
}

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
            else if (extension == ".png")
            {
                load_png(entry.path().string()).claim();
            }
        }
    }
}

float4x4 to_glm(const aiMatrix4x4& mat)
{
    float4x4 result {
        mat.a1, mat.b1, mat.c1, mat.d1,
        mat.a2, mat.b2, mat.c2, mat.d2,
        mat.a3, mat.b3, mat.c3, mat.d3,
        mat.a4, mat.b4, mat.c4, mat.d4
    };
    return result;
}

float3 to_glm(const aiVector3D& vec)
{
    return float3(vec.x, vec.y, vec.z);
}

float3 to_glm(const aiColor3D& col)
{
    return float3(col.r, col.g, col.b);
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

    asset_graph_entry root_node{};
    root_node.m_type = asset_type::none;
    root_node.m_asset_idx = 0;
    asset_id root_id = allocate_asset<asset_type::none>(0u, none_asset{});

    // load the stuff (assimp)
    {
        Assimp::Importer assimp{};
        int flags =
            aiProcess_CalcTangentSpace |
            aiProcess_Triangulate |
            aiProcess_JoinIdenticalVertices |
            aiProcess_PopulateArmatureData |
            aiProcess_SortByPType;
        const aiScene* scene = assimp.ReadFile(string(filepath), flags);

        // start building the entire scene
        const scene_id sc_id = make_scene_id(filepath);
        scene_asset sc_asset{};

        // parse materials
        auto fetch_material_first_texture = [this](const aiMaterial* material, aiTextureType type, image_id& out_id) {
            const uint32 num_textures = aiGetMaterialTextureCount(material, type);
            if (num_textures > 0)
            {
                aiString ai_out_path{};
                aiGetMaterialTexture(material, type, 0, &ai_out_path);

                string full_filepath;
                if (find_file(k_content_folder, ai_out_path.C_Str(), full_filepath))
                {
                    load_image_file(full_filepath).claim();
                    out_id = make_image_id(full_filepath.c_str());
                    return true;
                }
                else return false;
            }
            else return false;
        };

        vector<mat_id> material_ids{};
        for (uint32 i = 0u; i < scene->mNumMaterials; ++i)
        {
            material_asset asset{};
            const auto& material = scene->mMaterials[i];

            const string name = string(material->GetName().C_Str());
            const mat_id material_id = make_material_id(name);
            for (uint32 p = 0u; p < material->mNumProperties; ++p)
            {

            }

            aiColor4D ai_out_color{};
            aiGetMaterialColor(material, AI_MATKEY_BASE_COLOR, &ai_out_color);
            aiGetMaterialColor(material, AI_MATKEY_COLOR_DIFFUSE, &ai_out_color);
            asset.m_basecolor = float4(ai_out_color.r, ai_out_color.g, ai_out_color.b, ai_out_color.a);

            if (strstr(name.c_str(), "kritter"))
            {
                static int a = 0;
                a++;
            }

            asset.m_name = name;
            asset.m_mat_id = material_id;
            fetch_material_first_texture(material, aiTextureType_DIFFUSE, asset.m_tex_basecolor);
            allocate_asset<asset_type::material>(material_id, asset, root_id);
            material_ids.push_back(material_id);
        }
        
        // list all camera-names that want to know their scene_transform (this is necessary because assimp is crazy)
        umap<string, uint32> camera_name_to_node_idx{};
        umap<string, uint32> light_name_to_node_idx{};
        for (uint32 i = 0u; i < scene->mNumCameras; ++i)
        {
            const auto& camera = scene->mCameras[i];
            camera_name_to_node_idx[camera->mName.C_Str()] = 0u;
        }
        for (uint32 i = 0u; i < scene->mNumLights; ++i)
        {
            light_name_to_node_idx[scene->mLights[i]->mName.C_Str()] = 0u;
        }

        // traverse the entire scene hierarchy
        {
            func<void(const aiNode&, uint32, const float4x4&)> traverse_node;
            static constexpr uint32 k_invalid = (uint32)-1;
            traverse_node = [&traverse_node, &sc_asset, &filepath, &camera_name_to_node_idx, &light_name_to_node_idx](const aiNode& current_node, uint32 parent, const float4x4& parent_scene_transform)
            {
                flatgraph<scene_asset::node>& graph = sc_asset.m_graph;
                uint32 current_node_idx = parent == k_invalid ? graph.add_node({}, graph.k_root) : graph.add_node({}, parent);
                scene_asset::node& current_data = graph.get(current_node_idx).data();

                // parse this node's transforms
                const float4x4& local_transform = to_glm(current_node.mTransformation);
                const float4x4 scene_transform = local_transform * parent_scene_transform;
                current_data.m_local_transform = transform(local_transform);
                current_data.m_scene_transform = transform(scene_transform);
                current_data.m_scene_transform_inv = transform(glm::inverse(current_data.m_scene_transform.m_matrix));

                // parse the meshes associated with this node
                const uint32 num_meshes = current_node.mNumMeshes;
                current_data.m_meshes.resize(num_meshes);
                for (uint32 i = 0u; i < num_meshes; ++i)
                {
                    current_data.m_meshes[i] = make_mesh_id(filepath, current_node.mMeshes[i]);
                }

                // parse the node's name (and add to the name-to-idx map)
                current_data.m_name = string(current_node.mName.C_Str());
                sc_asset.m_name_to_node_idx[current_data.m_name] = current_node_idx;

                // parse the node scene transform of each camera node
                {
                    auto found = camera_name_to_node_idx.find(current_data.m_name);
                    if (found != camera_name_to_node_idx.cend())
                    {
                        camera_name_to_node_idx[current_data.m_name] = current_node_idx;
                    }
                }
                {
                    auto found = light_name_to_node_idx.find(current_data.m_name);
                    if (found != light_name_to_node_idx.cend())
                    {
                        light_name_to_node_idx[current_data.m_name] = current_node_idx;
                    }
                }

                // traverse children
                for (uint32 i = 0u; i < current_node.mNumChildren; ++i)
                {
                    traverse_node(*current_node.mChildren[i], current_node_idx, scene_transform);
                }
            };
            traverse_node(*scene->mRootNode, k_invalid, to_glm(scene->mRootNode->mTransformation));

            // log scene graph
            if (cm_log_content.enabled())
            {
                sc_asset.m_graph.traverse([&sc_asset](uint32 c, uint32 p)
                {
                    const auto& node = sc_asset.m_graph.get(c);
                    const auto& data = sc_asset.m_graph.get(c).data();
                    const float3& position = data.m_scene_transform.get_position();
                    string message = strikers::format("{}: [{}, {}, {}]", data.m_name, position.x, position.y, position.z);

                    // const float3& forward = data.m_scene_transform.get_forward();
                    // string message = strikers::format("{}: [{}, {}, {}]", data.m_name, forward.x, forward.y, forward.z);

                    // const uint32 num_meshes = (uint32)data.m_meshes.size();
                    // string message = strikers::format("{}: [{}]", data.m_name, num_meshes);

                    for (uint32 i = 0u; i < node.get_depth(); ++i)
                        message = "  " + message;
                    logman::log(message);
                }, sc_asset.m_graph.k_root, scene_asset::nodegraph::traverse_mode::depth);
            }
        }

        // parse fbx meshes
        umap<string, skel_id> unique_skeletons{};
        for (uint32 i = 0u; i < scene->mNumMeshes; ++i)
        {
            const auto& mesh = scene->mMeshes[i];
            const mesh_id mesh_id = make_mesh_id(filepath, i);

            mesh_asset asset_data{};
            asset_data.m_name = string(mesh->mName.C_Str());
            asset_data.m_scene_id = sc_id;
            const uint32 num_vertices = mesh->mNumVertices;
            const uint32 num_bones = mesh->mNumBones;
            asset_data.m_vertices.resize(num_vertices);

            float3 average_position = {};
            for (uint32 v = 0; v < num_vertices; ++v)
            {
                memcpy(&asset_data.m_vertices[v].m_position, &mesh->mVertices[v], sizeof(float3));
                memcpy(&asset_data.m_vertices[v].m_normal, &mesh->mNormals[v], sizeof(float3));
                average_position += asset_data.m_vertices[v].m_position;

                const float3 uv = to_glm(mesh->mTextureCoords[0][v]);;
                asset_data.m_vertices[v].m_uv.x = uv.x;
                asset_data.m_vertices[v].m_uv.y = 1 - uv.y;
            }
            for (uint32 f = 0; f < mesh->mNumFaces; ++f)
            {
                const auto& face = mesh->mFaces[f];
                for (uint32 idx = 0; idx < face.mNumIndices; ++idx)
                {
                    asset_data.m_indices.push_back({});
                    memcpy(&asset_data.m_indices.back(), &face.mIndices[idx], sizeof(uint32));
                }
            }

            // parse skeleton if this mesh has bones
            if (num_bones > 0)
            {
                skeleton_asset skeleton_asset{};
                skeleton_asset.m_name = asset_data.m_name; // copy mesh name

                const skel_id skeleton_id = make_skeleton_id(filepath, i);
                skeleton_asset.m_skeleton_id = skeleton_id;
                skeleton_asset.m_bones.resize(num_bones);

                // parse bones
                umap<aiNode*, uint32> bone_node_to_idx{};
                for (uint32 b = 0u; b < num_bones; ++b)
                {
                    auto& asset_bone = skeleton_asset.m_bones[b];
                    const aiBone* bone = mesh->mBones[b];

                    asset_bone.m_mat_inverse_bind = to_glm(bone->mOffsetMatrix);
                    asset_bone.m_mat_transform = (b == 0) ? float4x4(1) : to_glm(bone->mNode->mTransformation);
                   
                    asset_bone.m_name = bone->mName.C_Str();
                    bone_node_to_idx[bone->mNode] = b;
                    skeleton_asset.m_name_to_bone_idx[asset_bone.m_name] = b;

                    for (uint32 w = 0u; w < bone->mNumWeights; ++w)
                    {
                        const aiVertexWeight& weight = bone->mWeights[w];
                        mesh_asset::vertex& vertex = asset_data.m_vertices[weight.mVertexId];

                        // bind the bone to the vertex with a weight
                        const float added_weight = glm::min(vertex.m_weight_remainder, weight.mWeight);
                        vertex.m_bone_indices[vertex.m_num_active_bones] = b;
                        vertex.m_bone_weights[vertex.m_num_active_bones] = added_weight;
                        vertex.m_weight_remainder -= added_weight;
                        vertex.m_num_active_bones++;
                    }
                }

                // parent bones through index
                for (uint32 b = 0u; b < num_bones; ++b)
                {
                    aiNode* parent_node = mesh->mBones[b]->mNode->mParent;
                    if (bone_node_to_idx.contains(parent_node))
                    {
                        const uint32 parent_index = bone_node_to_idx[mesh->mBones[b]->mNode->mParent];
                        skeleton_asset.m_bones[b].m_parent_idx = bone_node_to_idx[parent_node];
                        skeleton_asset.m_bones[b].m_valid_parent = true;
                    }
                }

                // actually SKIP the new asset since we're detecting this was all just a duplicate.
                // we can't skip the earlier work, because it populates our mesh vertices with bone values!
                if (!unique_skeletons.contains(skeleton_asset.m_name))
                {
                    allocate_asset<asset_type::skeleton>(skeleton_id, skeleton_asset, root_id);
                    asset_data.m_skeleton_id = skeleton_id;

                    if (cm_log_content.enabled())
                        logman::log("- skeleton: {}", skeleton_asset.m_name.c_str());
                    unique_skeletons[skeleton_asset.m_name] = skeleton_id;
                }

                asset_data.m_skeleton_id = unique_skeletons[skeleton_asset.m_name];
            }

            // calculate avg distances
            average_position /= num_vertices;
            asset_data.m_bounds_box.m_position = average_position;
            asset_data.m_bounds_sphere = sphere(average_position, 0.0f);
            float furthest_distance_sqr = 0;
            for (uint32 v = 0; v < num_vertices; ++v)
            {
                const float3 point = asset_data.m_vertices[v].m_position;
                asset_data.m_bounds_box.grow_to_fit(point);

                const float3 delta = point - average_position;
                const float sqr_distance = glm::dot(delta, delta);
                if (sqr_distance > furthest_distance_sqr)
                {
                    furthest_distance_sqr = sqr_distance;
                }
            }
            asset_data.m_bounds_sphere.m_position_radius.w = glm::sqrt(furthest_distance_sqr);
            asset_data.m_mat_id = material_ids[mesh->mMaterialIndex];
            allocate_asset<asset_type::mesh>(mesh_id, asset_data, root_id);
        }

        // parse cameras
        for (uint32 i = 0u; i < scene->mNumCameras; ++i)
        {
            // we only add cameras of which we found a node in the scene hierarchy
            const auto& ai_camera = scene->mCameras[i];
            const string camera_name = string(ai_camera->mName.C_Str());
            const uint32 camera_idx = (uint32)sc_asset.m_cameras.size();

            // apply camera intrinsic transform to the scene transform
            if (camera_name_to_node_idx.contains(camera_name))
            {
                transform& camera_scene_transform = sc_asset.m_graph.get(camera_name_to_node_idx[camera_name]).data().m_scene_transform;
                aiMatrix4x4 ai_camera_matrix;
                ai_camera->GetCameraMatrix(ai_camera_matrix);
                const float4x4 camera_matrix = to_glm(ai_camera_matrix);
                camera_scene_transform.m_matrix = camera_scene_transform.m_matrix * camera_matrix;

                sc_asset.m_camera_to_node_idx[camera_idx] = camera_name_to_node_idx[camera_name];
                sc_asset.m_node_to_camera_idx[camera_name_to_node_idx[camera_name]] = camera_idx;
            }

            camera cm{};
            cm.m_aspect_ratio = ai_camera->mAspect;
            cm.m_clip_near = ai_camera->mClipPlaneNear;
            cm.m_clip_far = ai_camera->mClipPlaneFar;
            cm.m_fov_vertical = 2.0f * atan(tan(ai_camera->mHorizontalFOV) / ai_camera->mAspect);
            cm.m_ortho_width = ai_camera->mOrthographicWidth;
            sc_asset.m_cameras.push_back(cm);
        }

        // parse lights
        for (uint32 i = 0u; i < scene->mNumLights; ++i)
        {
            const auto& aiLight = scene->mLights[i];

            light light{};
            switch (aiLight->mType) {
            case aiLightSource_DIRECTIONAL: light.m_type = light::directional; break;
            case aiLightSource_POINT: light.m_type = light::point; break;
            case aiLightSource_SPOT: light.m_type = light::cone; break;
            }

            const uint32 light_idx = (uint32)sc_asset.m_lights.size();

            // apply camera intrinsic transform to the scene transform
            const string name = string(aiLight->mName.C_Str());
            if (light_name_to_node_idx.contains(name))
            {
                transform& light_scene_transform = sc_asset.m_graph.get(light_name_to_node_idx[name]).data().m_scene_transform;
                const transform light_transform = transform::build(
                    to_glm(aiLight->mPosition),
                    make_look_rotation(to_glm(aiLight->mDirection), to_glm(aiLight->mUp)),
                    float3(1, 1, 1)
                );
                light_scene_transform.m_matrix = light_scene_transform.m_matrix * light_transform.m_matrix;

                sc_asset.m_light_to_node_idx[light_idx] = light_name_to_node_idx[name];
                sc_asset.m_node_to_light_idx[light_name_to_node_idx[name]] = light_idx;
            }

            aiLight->mAngleInnerCone;
            aiLight->mAngleOuterCone;
            aiLight->mAttenuationConstant;
            aiLight->mAttenuationLinear;
            aiLight->mSize;
            light.m_color_ambient = to_glm(aiLight->mColorAmbient);
            light.m_color_diffuse = to_glm(aiLight->mColorDiffuse);
            light.m_color_specular = to_glm(aiLight->mColorSpecular);
            light.m_name = string(aiLight->mName.C_Str());
            sc_asset.m_lights.push_back(light);
        }

        // parse animations
        for (uint32 i = 0u; i < scene->mNumAnimations; ++i)
        {
            const anim_id animation_id = make_animation_id(filepath, i);
            animation_asset asset{};
            asset.m_animation_id = animation_id;

            const aiAnimation* animation = scene->mAnimations[i];
            asset.m_name = animation->mName.C_Str();
            const uint32 num_channels = animation->mNumChannels;
            asset.m_channels.resize(num_channels);

            // find the parent object node
#if 0
            uint32 node_idx = 0u;
            const string object_name = asset.m_name.substr(0, asset.m_name.find('|'));
            if (sc_asset.find_node_by_name(object_name, node_idx))
            {
                const auto& graph_entry = sc_asset.m_graph.get(node_idx).data();
                graph_entry.m_scene_transform;
            }
#endif

            for (uint32 c = 0u; c < animation->mNumChannels; ++c)
            {
                animation_asset::channel& target_channel = asset.m_channels[c];
                const auto& channel = animation->mChannels[c];
                
                const uint32 num_position_keys = channel->mNumPositionKeys;
                const uint32 num_rotation_keys = channel->mNumRotationKeys;
                const uint32 num_scaling_keys = channel->mNumScalingKeys;
                target_channel.m_position_keys.resize(num_position_keys);
                target_channel.m_rotation_keys.resize(num_rotation_keys);
                target_channel.m_scale_keys.resize(num_scaling_keys);
                target_channel.m_name = channel->mNodeName.C_Str();
                asset.m_name_to_channel_idx[target_channel.m_name] = c;

                // if this is ever different, our systems can't handle that...
                assert((num_position_keys == num_rotation_keys) && num_scaling_keys == num_rotation_keys);
                
                for (uint32 k = 0u; k < num_position_keys; ++k)
                {
                    auto& target_key = target_channel.m_position_keys[k];
                    const auto& key = channel->mPositionKeys[k];
                    memcpy(&target_key.m_time, &key.mTime, sizeof(double));
                    memcpy(&target_key.m_value, &key.mValue, sizeof(float3));
                    memcpy(&target_key.m_blend, &key.mInterpolation, sizeof(uint32));
                }
                for (uint32 k = 0u; k < num_rotation_keys; ++k)
                {
                    auto& target_key = target_channel.m_rotation_keys[k];
                    const auto& key = channel->mRotationKeys[k];
                    memcpy(&target_key.m_time, &key.mTime, sizeof(double));
                    memcpy(&target_key.m_value, &key.mValue, sizeof(float4));
                    memcpy(&target_key.m_blend, &key.mInterpolation, sizeof(uint32));
                }
                for (uint32 k = 0u; k < num_scaling_keys; ++k)
                {
                    auto& target_key = target_channel.m_scale_keys[k];
                    const auto& key = channel->mScalingKeys[k];
                    memcpy(&target_key.m_time, &key.mTime, sizeof(double));
                    memcpy(&target_key.m_value, &key.mValue, sizeof(float3));
                    memcpy(&target_key.m_blend, &key.mInterpolation, sizeof(uint32));
                }
            }

#if 0
            for (uint32 c = 0u; c < animation->mNumMeshChannels; ++c)
            {
                const auto& channel = animation->mMeshChannels[c];
            }
            for (uint32 c = 0u; c < animation->mNumMorphMeshChannels; ++c)
            {
                const auto& channel = animation->mMorphMeshChannels[c];
            }
#endif
            allocate_asset<asset_type::animation>(animation_id, asset, root_id);
        }

        // load fbx textures
        for (uint32 i = 0u; i < scene->mNumTextures; ++i)
        {
            load_image_file(scene->mTextures[i]->mFilename.C_Str());
        }

        // load skeletons
        for (uint32 i = 0u; i < scene->mNumSkeletons; ++i)
        {
            //
            static int a = 0;
            a++;
        }

        // allocate the scene asset
        allocate_asset<asset_type::scene>(sc_id, sc_asset, root_id);

        if (cm_log_content.enabled())
            logman::log("- num meshes: {}", scene->mNumMeshes);
    }
    return root_id;
}

result<asset_id> contentman::load_image_file(const stringview& filepath)
{
    if (cm_log_content.enabled()) 
        logman::log("loading_image({})", filepath);

    using restype = result<asset_id>;
    if (!std::filesystem::exists(filepath))
    {
        return restype::make_fail("file not found!");
    }
    if (!std::filesystem::is_regular_file(filepath))
    {
        return restype::make_fail("filepath is not a file!");
    }

    int out_x, out_y;
    int out_num_channels;
    unsigned char* raw_image_data = stbi_load(filepath.data(), &out_x, &out_y, &out_num_channels, 0);
    if (raw_image_data == nullptr)
    {
        return restype::make_fail("stbi_load() failed!");
    }

    const image_id img_id = make_image_id(filepath);
    auto fill_data = [&](image_asset& asset)
    {
        asset.m_image_id = img_id;
        asset.m_raw_data_ptr = raw_image_data;
        asset.m_desc.m_size.x = out_x;
        asset.m_desc.m_size.y = out_y;
        asset.m_desc.m_num_channels = out_num_channels;
        asset.m_bytesize = out_x * out_y * sizeof(uint8) * out_num_channels;
    };

    image_asset* img_data = nullptr;
    asset_id asset_id = 0u;
    if (fetch_typed_asset<asset_type::image>(img_id, img_data, &asset_id))
    {
        if (cm_log_content.enabled())
            logman::log("- reload!");
        stbi_image_free(img_data->m_raw_data_ptr); // free the previous data
        fill_data(*img_data);
        return asset_id;
    }
    else
    {
        image_asset img_data{};
        fill_data(img_data);
        return allocate_asset<asset_type::image>(img_id, img_data);
    }
}

result<asset_id> contentman::load_font_file(const stringview& filepath)
{
    if (cm_log_content.enabled())
        logman::log("loading_font({})", filepath);

    using restype = result<asset_id>;

    std::ifstream file(filepath.data(), std::ios::binary | std::ios::ate);
    if (!file) return restype::make_fail("failed to read font");

    const size_t size = static_cast<size_t>(file.tellg());
    std::vector<unsigned char> fontData(size);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(fontData.data()), size);
    if (!file) return restype::make_fail("failed to read font file");

    const int offset = stbtt_GetFontOffsetForIndex(fontData.data(), 0);
    if (offset < 0) return restype::make_fail("invalid font file");

    stbtt_fontinfo font;
    if (!stbtt_InitFont(&font, fontData.data(), offset))
    {
        return restype::make_fail("failed to initialise font");
    }

    const font_id fid = make_font_id(filepath);

    font_asset new_asset{};
    new_asset.m_raw_data = font.data;
    new_asset.m_font_id = fid;

    // extract the base characters by default
    constexpr std::string_view default_characters =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789"
        "!@#$%^&*()-_=+[]{};:',.<>/? ";

    for (char ch : default_characters) {
        
        int glyph_index = stbtt_FindGlyphIndex(&font, ch);

        font_asset::glyph glyph{};
        glyph.m_first_curve = (uint32)new_asset.m_curves.size();

        float2 current{};
        float2 contour_start{};

        stbtt_vertex* vertices = nullptr;
        const int vertex_count = stbtt_GetGlyphShape(&font, glyph_index, &vertices);

        float2 avg_point = {};
        for (int v = 0u; v < vertex_count; ++v) {
            const stbtt_vertex& vert = vertices[v];
            avg_point += float2(vert.x, vert.y);

            switch (vert.type) {
            case STBTT_vmove: {
                current = {(float)vert.x,(float)vert.y};
                contour_start = current;
                break;
            }
            case STBTT_vline: {
                const float2 end = {(float)vert.x,(float)vert.y};
                font_asset::curve new_curve{};
                new_curve.m_points[bezier::p0] = current;
                new_curve.m_points[bezier::p1] = end;
                new_curve.reset_control_points(); // if it's a line, control points are 'inactive'
                new_asset.m_curves.push_back(new_curve);
                current = end;
                break;
            }
            case STBTT_vcurve: {
                const float2 control = {(float)vert.cx, (float)vert.cy};
                const float2 end = { (float)vert.x, (float)vert.y };
                font_asset::curve new_curve{};
                new_curve.m_points[bezier::p0] = current;
                new_curve.m_points[bezier::p1] = end;
                new_curve.m_points[bezier::cp0] = control;
                new_asset.m_curves.push_back(new_curve);
                current = end;
                break;
            }
            }
        }
        avg_point /= vertex_count;

        glyph.m_num_curves = (uint32)new_asset.m_curves.size() - glyph.m_first_curve;

        // apply average point
        float inv_avg_distance = 0.0f;
        for (int c = 0u; c < glyph.m_num_curves; ++c) {
            auto& curve = new_asset.m_curves[c + glyph.m_first_curve];
            for (uint32 p = 0; p < _countof(curve.m_points); ++p)
            {
                inv_avg_distance += glm::length(curve.m_points[p] - avg_point);
                curve.m_points[p] -= avg_point;
            }
        };
        inv_avg_distance = 1 / (inv_avg_distance / glyph.m_num_curves);

        for (int c = 0u; c < glyph.m_num_curves; ++c) {
            auto& curve = new_asset.m_curves[c + glyph.m_first_curve];
            for (uint32 p = 0; p < _countof(curve.m_points); ++p)
            {
                curve.m_points[p] *= inv_avg_distance;
            }
        };

        if (glyph.m_num_curves > 0)
        {
            new_asset.m_char_to_glyph_index[ch] = (uint32)new_asset.m_glyphs.size();
            new_asset.m_glyphs.push_back(glyph);
        }

        stbtt_FreeShape(&font, vertices);
    }

    return allocate_asset<asset_type::font>(fid, new_asset);
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

    if (cm_log_content.enabled())
        logman::log("loading_obj({})", filepath);
    return load_assimp_file(filepath);
}

result<asset_id> contentman::load_raw_mesh(const mesh_asset& mesh_data, const mesh_id id)
{
    using restype = result<asset_id>;

    if (cm_log_content.enabled())
        logman::log("loading_raw_mesh({})", id);

    return allocate_asset<asset_type::mesh>(id, mesh_data);
}

result<asset_id> contentman::load_raw_image(
    const image_asset::desc& desc, 
    unsigned char* image_data,
    const image_id id)
{
    using restype = result<asset_id>;

    if (cm_log_content.enabled())
        logman::log("loading_raw_image({})", id);
    
    image_asset asset{};
    asset.m_bytesize = sizeof(uint32) * desc.m_size.x * desc.m_size.y;
    asset.m_image_id = id;
    asset.m_desc = desc;
    asset.m_raw_data_ptr = new byte[asset.m_bytesize];
    memcpy(asset.m_raw_data_ptr, image_data, asset.m_bytesize);
    return allocate_asset<asset_type::image>(id, asset);
}

result<mesh_id> contentman::find_mesh(const stringview& name) const
{
    using restype = result<mesh_id>;
    for (uint32 i = 0u; i < m_mesh_assets.m_asset_datas.size(); ++i)
    {
        const mesh_asset& mesh = m_mesh_assets.m_asset_datas[i];
        if (strcmp(mesh.m_name.c_str(), name.data()) == 0)
        {
            return mesh.m_mesh_id;
        }
    }

    return restype::make_fail("mesh not found!");
}

bool contentman::is_compatible(const anim_id animation, const skel_id skeleton) const
{
    animation_asset const* anim_asset = nullptr;
    auto found_anim = find_typed_asset<asset_type::animation>(animation);
    if (found_anim.is_fail())
    {
        return false;
    }

    skeleton_asset const* skel_asset = nullptr;
    auto found_skeleton = find_typed_asset<asset_type::skeleton>(skeleton);
    if (found_skeleton.is_fail())
    {
        return false;
    }

    const auto& bone_names = found_skeleton.claim()->m_name_to_bone_idx;
    const auto& channel_names = found_anim.claim()->m_name_to_channel_idx;
    for (const auto& pair : channel_names)
    {
        if (bone_names.contains(pair.first))
            return true;
    }

    return false;
}

bool contentman::find_compatible_animations(const skel_id skeleton, vector<anim_id>& out_anims) const
{
    skeleton_asset const* skel_asset = nullptr;
    auto found_skeleton = find_typed_asset<asset_type::skeleton>(skeleton);
    if (found_skeleton.is_fail())
    {
        return false;
    }
    const auto& bone_names = found_skeleton.claim()->m_name_to_bone_idx;
    
    for (const auto& anim_asset : get_typed_assets<asset_type::animation>().m_asset_datas)
    {
        bool is_compatible = false;
        const auto& channel_names = anim_asset.m_name_to_channel_idx;
        for (const auto& pair : channel_names)
        {
            if (bone_names.contains(pair.first))
            {
                is_compatible = true;
                break;
            }
        }

        if (is_compatible)
        {
            out_anims.push_back(anim_asset.m_animation_id);
        }
    }
    return out_anims.size() > 0;
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

    if (cm_log_content.enabled())
        logman::log("loading_fbx({})", filepath);
    return load_assimp_file(filepath);
}

result<asset_id> contentman::load_png(const stringview& filepath)
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
    if (std::filesystem::path(filepath).extension() != ".png")
    {
        return restype::make_fail("file at path is not .png!");
    }

    return load_image_file(filepath);
}

result<asset_id> contentman::load_ttf(const stringview& filepath)
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
    if (std::filesystem::path(filepath).extension() != ".ttf")
    {
        return restype::make_fail("file at path is not .ttf!");
    }

    return load_font_file(filepath);
}

result<asset_id> contentman::load_otf(const stringview& filepath)
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
    if (std::filesystem::path(filepath).extension() != ".otf")
    {
        return restype::make_fail("file at path is not .otf!");
    }

    return load_font_file(filepath);
}
}