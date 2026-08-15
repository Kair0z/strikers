#pragma once
#include "common.h"
#include "flatgraph.h"

namespace strikers {
	
using asset_id = uint32;
enum class asset_type
{
	none,
	scene,
	mesh,
	image,
	skeleton,
	animation,
	material,
	num
};

struct fbx_load_args final
{

};

struct asset_scan_report
{

};

struct none_asset final
{
	int none;
};

struct scene_asset final
{
	struct camera
	{
		float4x4 m_scene_transform;
		float m_fov;
	};
	vector<camera> m_cameras{};

	scene_id m_scene_id{};
	struct node final
	{
		float4x4 m_local_transform; // local transform in parent space
		float4x4 m_scene_transform; // scene space -> model space
		float4x4 m_scene_transform_inv; // scene space -> model space
		vector<mesh_id> m_meshes{};
	};
	using nodegraph = flatgraph<node>;
	nodegraph m_graph;

	bool find_node_with_mesh(const mesh_id& mesh, uint32& out_node_idx)
	{
		int found_node = -1;
		m_graph.traverse([this, mesh, &found_node](uint32 node_idx)
		{
			if (found_node >= 0) return;

			const node& node = m_graph.get(node_idx).data();
			const auto& meshes = node.m_meshes;
			for (uint32 i = 0u; i < meshes.size(); ++i)
			{
				if (meshes[i] == mesh)
				{
					found_node = (int)node_idx;
					return;
				}
			}
		});

		if (found_node >= 0)
		{
			out_node_idx = found_node;
			return true;
		}
		else return false;
	}
};

struct mesh_asset final
{
	mesh_id m_mesh_id{};
	scene_id m_scene_id{};
	mat_id m_mat_id{};
	string m_name;

	struct vertex
	{
		float3 m_position;
		float3 m_normal;
		float3 m_uv;
		uint4 m_bone_indices;
		float4 m_bone_weights;
		uint32 m_num_active_bones;
	};
	vector<vertex> m_vertices{};
	vector<uint32> m_indices{};

	const uint32 get_num_vertices() const
	{
		return (uint32)m_vertices.size();
	}
};

struct image_asset final
{
	image_id m_image_id;
	unsigned char* m_raw_data_ptr;
	uint32 m_pixel_width;
	uint32 m_pixel_height;
	uint32 m_num_channels;
	uint64 m_bytesize;

	uint8 get_pixel_channel_value(const uint64 pixel_idx, const uint32 channel_idx) const
	{
		return m_raw_data_ptr[(pixel_idx * m_num_channels) + channel_idx];
	}
	uint64 calculate_num_pixels() const
	{
		return m_pixel_height * m_pixel_width;
	}
	uint64 calculate_pixel_bytesize()
	{
		return (m_bytesize / calculate_num_pixels());
	}
};

struct skeleton_asset final
{
	struct bone
	{
		uint32 m_parent_idx;
		float4x4 m_offset_matrix;
		float4x4 m_local_matrix;
	};

	skel_id m_skeleton_id;
	vector<bone> m_bones{};
};

struct animation_asset final
{
	enum class blend_type
	{
		step,
		linear,
		slinear,
		spline,
		num
	};

	template <typename _t>
	struct keyframe
	{
		_t m_value;
		double m_time;
		blend_type m_blend;
	};

	struct channel
	{
		vector<keyframe<float3>> m_position_keys;
		vector<keyframe<float3>> m_scale_keys;
		vector<keyframe<float4>> m_rotation_keys; // float4 -> quaternion
	};

	anim_id m_animation_id;
	vector<channel> m_channels{};
};

struct material_asset final
{
	mat_id m_mat_id;
	string m_name;
	image_id m_tex_basecolor = k_id_invalid;
};

class contentman final
{
	template <asset_type _t>
	using asset_type_t = std::tuple_element_t<static_cast<uint64>(_t), std::tuple<
		none_asset,
		scene_asset,
		mesh_asset,
		image_asset,
		skeleton_asset,
		animation_asset,
		material_asset>>;
	template <asset_type _t>
	using asset_id_t = std::tuple_element_t<static_cast<uint64>(_t), std::tuple<
		uint64,
		scene_id,
		mesh_id,
		image_id,
		skel_id,
		anim_id,
		mat_id>>;

	template <asset_type _t>
	struct typed_assets final
	{
		vector<asset_type_t<_t>> m_asset_datas;
		umap<asset_id_t<_t>, asset_id> m_typed_id_to_asset_id;
	};
	typed_assets<asset_type::none> m_none_assets;
	typed_assets<asset_type::scene> m_scene_assets;
	typed_assets<asset_type::mesh> m_mesh_assets;
	typed_assets<asset_type::image> m_image_assets;
	typed_assets<asset_type::skeleton> m_skeleton_assets;
	typed_assets<asset_type::animation> m_animation_assets;
	typed_assets<asset_type::material> m_material_assets;

	template <asset_type _t>
	const typed_assets<_t>& get_typed_assets() const
	{
		if constexpr (_t == asset_type::mesh) return m_mesh_assets;
		else if constexpr (_t == asset_type::scene) return m_scene_assets;
		else if constexpr (_t == asset_type::image) return m_image_assets;
		else if constexpr (_t == asset_type::skeleton) return m_skeleton_assets;
		else if constexpr (_t == asset_type::animation) return m_animation_assets;
		else if constexpr (_t == asset_type::material) return m_material_assets;
		else return m_none_assets;
	}
	template <asset_type _t>
	typed_assets<_t>& get_typed_assets()
	{
		if constexpr (_t == asset_type::mesh) return m_mesh_assets;
		else if constexpr (_t == asset_type::scene) return m_scene_assets;
		else if constexpr (_t == asset_type::image) return m_image_assets;
		else if constexpr (_t == asset_type::skeleton) return m_skeleton_assets;
		else if constexpr (_t == asset_type::animation) return m_animation_assets;
		else if constexpr (_t == asset_type::material) return m_material_assets;
		else return m_none_assets;
	}

	template <asset_type _t>
	bool fetch_typed_asset(const asset_id_t<_t>& id, asset_type_t<_t>*& out_asset_data_ptr, asset_id* out_asset_id = nullptr)
	{
		if (!is_typed_asset_loaded<_t>(id))
		{
			return false;
		}

		typed_assets<_t>& typed_assets = get_typed_assets<_t>();
		asset_id asset_id = typed_assets.m_typed_id_to_asset_id.at(id);
		const asset_graph_entry& asset = m_asset_graph.get(asset_id).data();
		if (asset.m_type != _t)
		{
			return false;
		}

		if (out_asset_id != nullptr) (*out_asset_id) = asset_id;
		out_asset_data_ptr = &typed_assets.m_asset_datas[asset.m_asset_idx];
		return true;
	}

public:
	void scan_assets_in_folder(const stringview& folderpath, asset_scan_report* report = nullptr);
	
	result<asset_id> load_fbx(const stringview& filepath);

	result<asset_id> load_obj(const stringview& filepath);

	result<asset_id> load_png(const stringview& filepath);

	result<asset_id> load_custom_mesh(const mesh_asset& mesh_data, const mesh_id id);

	static mesh_id make_mesh_id(const stringview& filepath, uint32 index)
	{
		string copy = normalize_path(filepath);
		const uint64 hash1 = std::hash<stringview>{}(copy);
		const uint64 hash2 = std::hash<uint32>{}(index);

		// hash combine (boost-style)
		return static_cast<mesh_id>(
			hash1 ^ (hash2 + 0x9e3779b9 + (hash1 << 6) + (hash1 >> 2))
			);
	}

	result<mesh_id> find_mesh(const stringview& name) const;

	static image_id make_image_id(const stringview& filepath)
	{
		string copy = normalize_path(filepath);
		const uint64 hash1 = std::hash<stringview>{}(copy);
		return hash1;
	}

	static skel_id make_skeleton_id(const stringview& filepath, uint32 index)
	{
		string copy = normalize_path(filepath);
		const uint64 hash1 = std::hash<stringview>{}(copy);
		const uint64 hash2 = std::hash<uint32>{}(index);

		// hash combine (boost-style)
		return static_cast<skel_id>(
			hash1 ^ (hash2 + 0x9e3779b9 + (hash1 << 6) + (hash1 >> 2))
			);
	}

	static anim_id make_animation_id(const stringview& filepath, uint32 index)
	{
		string copy = normalize_path(filepath);
		const uint64 hash1 = std::hash<stringview>{}(copy);
		const uint64 hash2 = std::hash<uint32>{}(index);

		// hash combine (boost-style)
		return static_cast<skel_id>(
			hash1 ^ (hash2 + 0x9e3779b9 + (hash1 << 6) + (hash1 >> 2))
			);
	}

	static scene_id make_scene_id(const stringview& filepath)
	{
		string copy = normalize_path(filepath);
		const uint64 hash1 = std::hash<stringview>{}(copy);
		return hash1;
	}
	
	static mat_id make_material_id(const stringview& name)
	{
		return std::hash<stringview>{}(name);
	}

	template <asset_type _t>
	bool is_typed_asset_loaded(const asset_id_t<_t>& id) const
	{
		return get_typed_assets<_t>().m_typed_id_to_asset_id.contains(id);
	}

	template <asset_type _t>
	result<asset_type_t<_t> const*> find_typed_asset(const asset_id_t<_t>& id) const
	{
		using restype = result<asset_type_t<_t> const*>;
		if (!is_typed_asset_loaded<_t>(id))
		{
			return restype::make_fail("typed_asset at id is not loaded!");
		}

		asset_id asset_id = get_typed_assets<_t>().m_typed_id_to_asset_id.at(id);
		const asset_graph_entry& asset = m_asset_graph.get(asset_id).data();
		if (asset.m_type != _t)
		{
			return restype::make_fail("typed_asset id points at an asset_id that is the wrong type!");
		}

		return &get_typed_assets<_t>().m_asset_datas[asset.m_asset_idx];
	}

	mesh_asset const* find_mesh(const mesh_id id) const {
		return find_typed_asset<asset_type::mesh>(id).claim();
	}
	material_asset const* find_material(const mat_id id) const {
		return find_typed_asset<asset_type::material>(id).claim();
	}
	mat_id get_mesh_material_id(const mesh_id id) const
	{
		auto mesh = find_mesh(id);
		if (mesh) return mesh->m_mat_id;
		else return k_id_invalid;
	}
	image_id get_mesh_basecolor_image_id(const mesh_id id) const
	{
		mat_id mat = get_mesh_material_id(id);
		if (mat != k_id_invalid)
		{
			auto material = find_material(mat);
			if (material) return material->m_tex_basecolor;
		}
		else return k_id_invalid;
	}
	bool get_mesh_name(const mesh_id id, string& out_name) const
	{
		auto mesh = find_mesh(id);
		if (mesh)
		{
			out_name = mesh->m_name;
			return true;
		}
		else return false;
	}

private:

	struct asset_graph_entry final
	{
		asset_type m_type;
		uint32 m_asset_idx;
	};
	using assetgraph = flatgraph<asset_graph_entry>;
	assetgraph m_asset_graph{};

	template <asset_type _t>
	uint32 get_num_assets() const 
	{
		return (uint32)get_typed_assets<_t>().m_asset_datas.size();
	}

	template <asset_type _t>
	asset_id allocate_asset(const asset_id_t<_t>& type_id, const asset_type_t<_t>& data, const asset_id parent = assetgraph::k_root)
	{
		typed_assets<_t>& assets_container = get_typed_assets<_t>();
		auto found_already = assets_container.m_typed_id_to_asset_id.find(type_id);
		if (found_already != assets_container.m_typed_id_to_asset_id.cend())
		{
			return found_already->second;
		}

		// add a node to the asset graph
		asset_graph_entry asset_entry{};
		asset_entry.m_type = _t;
		asset_entry.m_asset_idx = get_num_assets<_t>();
		asset_id new_asset_id = m_asset_graph.add_node(asset_entry, parent);

		// add a typed asset
		assets_container.m_asset_datas.push_back(data);
		assets_container.m_typed_id_to_asset_id[type_id] = new_asset_id;
		return new_asset_id;
	}

	result<asset_id> load_assimp_file(const stringview& filepath);
	result<asset_id> load_image_file(const stringview& filepath);
};
}