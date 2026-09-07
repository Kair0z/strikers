#include "transman.h"

namespace strikers
{
trans_id transman::add_local_transform(const transform& local, trans_id parent)
{
	entry new_entry{};
	trans_id new_id = m_graph.add_node(new_entry, parent);
	set_transform(new_id, local, space::local);
	return new_id;
}

trans_id transman::add_world_transform(const transform& world, trans_id parent)
{
	entry new_entry{};
	trans_id new_id = m_graph.add_node(new_entry, parent);
	set_transform(new_id, world, space::world);
	return new_id;
}

void transman::resolve_graph(trans_id root)
{
	m_graph.traverse([this](uint32 current, uint32 parent)
	{
		entry& current_entry = m_graph.get(current).data();
		if (current_entry.m_world_dirty == false)
		{
			return;
		}

		if (!m_graph.is_valid(parent))
		{
			current_entry.m_trs_world = current_entry.m_trs_local;
		}
		else
		{
			const entry& parent_entry = m_graph.get(parent).data();
			current_entry.m_trs_world.m_scale = transform_scale(parent_entry.m_trs_world, current_entry.m_trs_local.m_scale);
			current_entry.m_trs_world.m_rotation = transform_rotation(parent_entry.m_trs_world, current_entry.m_trs_local.m_rotation);
			current_entry.m_trs_world.m_position = transform_position(parent_entry.m_trs_world, current_entry.m_trs_local.m_position);
		}

		current_entry.m_trs_world_inv = current_entry.m_trs_world.get_inverse();
		current_entry.m_world_dirty = false;
	}, root);
}

void transman::resolve_parents(trans_id id)
{
	trans_id parent = m_graph.parent(id);
	const bool is_parent_valid = m_graph.is_valid(parent);
	if (is_parent_valid)
	{
		resolve_parents(parent);
	}

	entry& current_entry = m_graph.get(id).data();
	if (current_entry.m_world_dirty)
	{
		if (!is_parent_valid)
		{
			current_entry.m_trs_world = current_entry.m_trs_local;
		}
		else
		{
			const entry& parent_entry = m_graph.get(parent).data();
			current_entry.m_trs_world.m_scale = transform_scale(parent_entry.m_trs_world, current_entry.m_trs_local.m_scale);
			current_entry.m_trs_world.m_rotation = transform_rotation(parent_entry.m_trs_world, current_entry.m_trs_local.m_rotation);
			current_entry.m_trs_world.m_position = transform_position(parent_entry.m_trs_world, current_entry.m_trs_local.m_position);
		}
		current_entry.m_trs_world_inv = current_entry.m_trs_world.get_inverse();
	}
	current_entry.m_world_dirty = false;
}

void transman::save_as_reset()
{
	resolve_graph();
	for (auto& entry : m_graph.get_flat())
	{
		entry.data().m_trs_local_reset = entry.data().m_trs_local;
	}
}

void transman::reset()
{
	for (auto& entry : m_graph.get_flat())
	{
		entry.data().m_trs_local = entry.data().m_trs_local_reset;
	}
	resolve_graph();
}

void transman::set_transform(trans_id id, const transform& trans, space spc)
{
	set_scale(id, trans.get_scale(), spc);
	set_rotation(id, trans.get_rotation(), spc);
	set_position(id, trans.get_position(), spc);
}

void transman::set_position(trans_id id, const float3& position, space spc)
{
	entry& entri = m_graph.get(id).data();
	trans_id parent = m_graph.parent(id);
	if (spc == space::local || !m_graph.is_valid(parent))
	{
		entri.m_trs_local.m_position = position;
	}
	else
	{
		resolve_parents(id);
		entry& parent_entry = m_graph.get(parent).data();
		entri.m_trs_local.m_position = world_to_local_position(parent_entry.m_trs_world_inv, position);
	}
	entri.m_world_dirty = true;
}

void transman::set_rotation(trans_id id, const rotation& rotation, space spc)
{
	entry& entri = m_graph.get(id).data();
	trans_id parent = m_graph.parent(id);
	if (spc == space::local || !m_graph.is_valid(parent))
	{
		entri.m_trs_local.m_rotation = rotation;
	}
	else
	{
		resolve_parents(id);
		entry& parent_entry = m_graph.get(parent).data();
		entri.m_trs_local.m_rotation = 
			transform_rotation(parent_entry.m_trs_world_inv, rotation);
	}
	entri.m_world_dirty = true;
}

void transman::set_scale(trans_id id, const float3& scale, space spc)
{
	entry& entri = m_graph.get(id).data();
	trans_id parent = m_graph.parent(id);
	if (spc == space::local || !m_graph.is_valid(parent))
	{
		entri.m_trs_local.m_scale = scale;
	}
	else
	{
		resolve_parents(id);
		entry& parent_entry = m_graph.get(parent).data();
		entri.m_trs_local.m_scale = transform_scale(parent_entry.m_trs_world_inv, scale);
	}
	entri.m_world_dirty = true;
}

transform transman::get_transform(trans_id id, space spc)
{
	return transform::build(
		get_position(id, spc),
		get_rotation(id, spc),
		get_scale(id, spc));
}

float3 transman::get_position(trans_id id, space spc)
{
	entry& entri = m_graph.get(id).data();
	trans_id parent = m_graph.parent(id);
	if (spc == space::local || !m_graph.is_valid(parent))
	{
		return entri.m_trs_local.m_position;
	}
	else
	{
		resolve_parents(id);
		entry& parent_entry = m_graph.get(parent).data();
		return transform_position(parent_entry.m_trs_world, entri.m_trs_local.m_position);
	}
}

rotation transman::get_rotation(trans_id id, space spc)
{
	entry& entri = m_graph.get(id).data();
	trans_id parent = m_graph.parent(id);
	if (spc == space::local || !m_graph.is_valid(parent))
	{
		return entri.m_trs_local.m_rotation;
	}
	else
	{
		resolve_parents(id);
		entry& parent_entry = m_graph.get(parent).data();
		return transform_rotation(parent_entry.m_trs_world, entri.m_trs_local.m_rotation);
	}
}

float3 transman::get_scale(trans_id id, space spc)
{
	entry& entri = m_graph.get(id).data();
	trans_id parent = m_graph.parent(id);
	if (spc == space::local || !m_graph.is_valid(parent))
	{
		return entri.m_trs_local.m_scale;
	}
	else
	{
		resolve_parents(id);
		entry& parent_entry = m_graph.get(parent).data();
		return transform_scale(parent_entry.m_trs_world, entri.m_trs_local.m_scale);
	}
}

void transman::add_position(trans_id id, const float3& delta, space spc)
{
	entry& entri = m_graph.get(id).data();
	trans_id parent = m_graph.parent(id);
	if (spc == space::local || !m_graph.is_valid(parent))
	{
		entri.m_trs_local.m_position += delta;
	}
	else
	{
		resolve_parents(id);
		entry& parent_entry = m_graph.get(parent).data();
		entri.m_trs_local.m_position += world_to_local_delta_position(parent_entry.m_trs_world_inv, delta);
	}
	entri.m_world_dirty = true;
}

void transman::add_rotation(trans_id id, const rotation& delta, space spc)
{
	entry& entri = m_graph.get(id).data();
	trans_id parent = m_graph.parent(id);
	if (spc == space::local || !m_graph.is_valid(parent))
	{
		entri.m_trs_local.m_rotation *= delta;
	}
	else
	{
		resolve_parents(id);
		entry& parent_entry = m_graph.get(parent).data();
		entri.m_trs_local.m_rotation *= transform_rotation(parent_entry.m_trs_world_inv, delta);
	}

	entri.m_world_dirty = true;
}

void transman::mult_scale(trans_id id, const float3& multiplier, space spc)
{
	entry& entri = m_graph.get(id).data();
	trans_id parent = m_graph.parent(id);
	if (spc == space::local || !m_graph.is_valid(parent))
	{
		entri.m_trs_local.m_scale *= multiplier;
	}
	else
	{
		resolve_parents(id);
		entry& parent_entry = m_graph.get(parent).data();
		entri.m_trs_local.m_scale *= transform_scale(parent_entry.m_trs_world_inv, multiplier);
	}
	entri.m_world_dirty = true;
}

vector<trans_id> transman::get_transforms() const
{
	vector<trans_id> ids{};
	const auto& flat_data = m_graph.get_flat();
	ids.resize(flat_data.size());

	for (uint32 i = 0u; i < flat_data.size(); ++i)
		ids[i] = flat_data[i].get_index();
	return ids;
}

float3 transman::transform_position(const trs& trans, const float3& position)
{
	return trans.m_position + (trans.m_rotation * (trans.m_scale * position));
}

rotation transman::transform_rotation(const trs& trans, const rotation& rotation)
{
	return trans.m_rotation * rotation;
}

float3 transman::transform_scale(const trs& trans, const float3& scale)
{
	return trans.m_scale * scale;
}

float3 transman::world_to_local_position(const trs& parent_world_inv, const float3& world_position)
{
	return parent_world_inv.m_scale * (parent_world_inv.m_rotation * (world_position + parent_world_inv.m_position));
}

float3 transman::world_to_local_delta_position(const trs& parent_world_inv, const float3& world_delta)
{
	return (parent_world_inv.m_rotation * world_delta) * parent_world_inv.m_scale;
}
}