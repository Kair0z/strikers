#pragma once
#include "common.h"
#include "flatgraph.h"

namespace strikers {
	
using asset_id = uint32;
enum class asset_type
{
	none,
	mesh,
	image,
};

struct fbx_load_args final
{

};

struct asset_scan_report
{

};

struct mesh_asset final
{
	mesh_id m_mesh_id;
	vector<float3> m_vertices;
	vector<float3> m_normals;
	vector<uint32> m_indices;

	const uint64 calculate_vertexbuffer_bytesize() const
	{
		return m_vertices.size() * sizeof(float3);
	}
	const uint64 calculate_normalbuffer_bytesize() const
	{
		return m_normals.size() * sizeof(float3);
	}
	const uint64 calculate_indexbuffer_bytesize() const
	{
		return m_indices.size() * sizeof(uint32);
	}
};

class contentman final
{
public:
	void scan_assets_in_folder(const stringview& folderpath, asset_scan_report* report = nullptr);
	
	result<asset_id> load_fbx(const stringview& filepath);

	result<asset_id> load_obj(const stringview& filepath);

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

	bool is_mesh_loaded(const mesh_id& id) const
	{
		return m_meshid_to_asset.contains(id);
	}

	result<mesh_asset const*> find_mesh_asset(const mesh_id& id) const
	{
		using restype = result<mesh_asset const*>;
		if (!is_mesh_loaded(id))
		{
			return restype::make_fail("mesh at id is not loaded!");
		}

		asset_id asset_id = m_meshid_to_asset.at(id);
		const asset_data& asset = m_asset_graph.get(asset_id).data();
		if (asset.type != asset_type::mesh)
		{
			return restype::make_fail("mesh_id points at an asset_id that is not of type mesh!");
		}

		const mesh_asset& mesh_asset = m_mesh_assets[asset.m_asset_idx];
		return &mesh_asset;
	}

private:
	struct asset_data final
	{
		asset_type type;
		uint32 m_asset_idx;
	};
	
	umap<string, asset_id> m_filepath_to_asset;
	umap<mesh_id, asset_id> m_meshid_to_asset;
	flatgraph<asset_data> m_asset_graph{};
	vector<mesh_asset> m_mesh_assets;

	result<asset_id> load_assimp_file(const stringview& filepath);
};
}