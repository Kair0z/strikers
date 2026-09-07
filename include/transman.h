#pragma once
#include "common.h"
#include "flatgraph.h"

namespace strikers
{
using trans_id = uint32;
class transman
{
	struct trs final
	{
		float3		m_position;
		rotation	m_rotation;
		float3		m_scale = float3(1, 1, 1);

		trs() = default;
		trs(const float3& position, const rotation& rotation, const float3& scale)
			: m_position{ position }, m_rotation{ rotation }, m_scale{ scale } {
		}
		trs(const transform& trans)
		{
			m_position = trans.get_position();
			m_rotation = trans.get_rotation();
			m_scale = trans.get_scale();
		}

		trs get_inverse() const
		{
			return trs(
				-m_position,
				glm::inverse(m_rotation),
				float3(1.0f / m_scale.x,1.0f / m_scale.y,1.0f / m_scale.z)
			);
		}
	};

	static float3 transform_position(const trs& trans, const float3& position);
	static rotation transform_rotation(const trs& trans, const rotation& rotation);
	static float3 transform_scale(const trs& trans, const float3& scale);
	static float3 world_to_local_delta_position(const trs& parent_world_inv, const float3& world_delta);
	static float3 world_to_local_position(const trs& parent_world_inv, const float3& world_position);

	struct entry final
	{
	public:
		trs m_trs_local;
		trs m_trs_world;
		trs m_trs_world_inv;
		trs m_trs_local_reset;
		bool m_world_dirty = false;
	};

	using graph = flatgraph<entry>;
	using transform_id = graph::node_idx;
	graph m_graph;
	
	void resolve_parents(trans_id id);

public:
	// adds a transform to the hierarchy, ensuring absolute world transform is maintained despite the parent
	trans_id add_world_transform(const transform& world, trans_id parent = graph::k_root);

	// adds a transform to the hierarchy, calculates a world transform relative to parent
	trans_id add_local_transform(const transform& local, trans_id parent = graph::k_root);

	// resolves all updates inside root and all its children
	void resolve_graph(trans_id root = graph::k_root);

	void set_transform(trans_id id, const transform& trans, space spc);
	void set_position(trans_id id, const float3& position, space spc);
	void set_rotation(trans_id id, const rotation& rotation, space spc);
	void set_scale(trans_id id, const float3& scale, space spc);

	transform get_transform(trans_id id, space spc);
	float3 get_position(trans_id id, space spc);
	rotation get_rotation(trans_id id, space spc);
	float3 get_scale(trans_id id, space spc);

	void add_position(trans_id id, const float3& delta, space spc);
	void add_rotation(trans_id id, const rotation& delta, space spc);
	void mult_scale(trans_id id, const float3& multiplier, space spc);

	void save_as_reset(); // saves all transforms as 'reset'
	void reset();

	vector<trans_id> get_transforms() const;
};
}