#include "common.h"
#include "physman.h"
#include "components.h"
#include "flatgraph.h"

namespace strikers {
class renderscene;
class contentman;
class gameman
{
	// per player data
	struct player_data final
	{
		uint32 current_movement = 0u;
	};
	player_data m_players[2];

	struct ball_data final
	{
		actor_id m_actor;
	};
	ball_data m_ball;

	struct transform_entry final
	{
		transform m_local_reset;
		transform m_world;
		transform m_local;
		string m_name;
	};
	using transform_graph = flatgraph<transform_entry>;
	using transform_id = transform_graph::node_idx;
	transform_graph m_transform_graph;

	struct actor final
	{
		enum flags
		{
			none = 0,
			kritter = (1 << 0),
			toad	= (1 << 1),
			ball	= (1 << 2),
		};

		uint32 m_flags;
		actor_id m_id;
		transform_id m_transform_id;
		uint64 m_component_idxs[component::k_num_types]{ k_component_invalid };

		actor()
		{
			for (uint32 i = 0u; i < component::k_num_types; ++i)
				m_component_idxs[i] = k_component_invalid;
		}

		void mark_component(component::type type, uint32 component_idx)
		{
			m_component_idxs[(uint32)type] = component_idx;
		}
		void unmark_component(component::type type)
		{
			mark_component(type, k_component_invalid);
		}
		bool has_component(component::type type) const
		{
			return m_component_idxs[(int)type] != k_component_invalid;
		}
	};

private:
	template <component::type _t>
	using component_array = vector<component_t<_t>>;
	struct
	{
		component_array<component::type::physics> m_physics;
		component_array<component::type::bounds> m_bounds;
		component_array<component::type::render> m_renders;
		component_array<component::type::renderui> m_renderuis;
		component_array<component::type::movement> m_movements;
		component_array<component::type::brain> m_brains;
	} m_components;

	template <component::type _t>
	component_array<_t>& components()
	{
		if constexpr (_t == component::type::physics) return m_components.m_physics;
		else if constexpr (_t == component::type::bounds) return m_components.m_bounds;
		else if constexpr (_t == component::type::render) return m_components.m_renders;
		else if constexpr (_t == component::type::renderui) return m_components.m_renderuis;
		else if constexpr (_t == component::type::movement) return m_components.m_movements;
		else if constexpr (_t == component::type::brain) return m_components.m_brains;
	}
	template <component::type _t>
	const component_array<_t>& components() const
	{
		if constexpr (_t == component::type::physics) return m_components.m_physics;
		else if constexpr (_t == component::type::bounds) return m_components.m_bounds;
		else if constexpr (_t == component::type::render) return m_components.m_renders;
		else if constexpr (_t == component::type::renderui) return m_components.m_renderuis;
		else if constexpr (_t == component::type::movement) return m_components.m_movements;
		else if constexpr (_t == component::type::brain) return m_components.m_brains;
	}

	struct camera
	{
		camera_id m_asset_id;
		transform m_transform;
		transform m_transform_original;
		float3 m_velocity;
	};
	camera m_camera;

	vector<actor> m_actors;

	const transform_entry& get_actor_transform(const actor_id id) const
	{
		const transform_id trans_id = m_actors[id].m_transform_id;
		return m_transform_graph.get(trans_id).data();
	}
	transform_entry& get_actor_transform(const actor_id id)
	{
		const transform_id trans_id = m_actors[id].m_transform_id;
		return m_transform_graph.get(trans_id).data();
	}

	void set_actor_flag(actor_id actor, actor::flags flag, bool enabled = true)
	{
		m_actors[actor].m_flags |= (uint32)flag;
	}

	bool actor_has_flag(actor_id actor, actor::flags flag) const
	{
		return m_actors[actor].m_flags & flag;
	}

	template <component::type _t>
	component_t<_t>& add_component(actor_id owner)
	{
		if (has_component(owner, _t))
		{
			return *get_component<_t>(owner);
		}

		// first find an inactive entry: reparent
		auto& comp_array = components<_t>();
		for (uint32 i = 0u; i < comp_array.size(); ++i)
		{
			if (comp_array[i].has_owner() == false)
			{
				comp_array[i] = {}; // reset data
				comp_array[i].m_owner = owner; // reparent
				comp_array[i].m_id = i;
				m_actors[owner].mark_component(_t, i);
				return comp_array[i];
			}
		}
		
		// allocate a new component if no unused was found
		comp_array.push_back({});
		const uint32 comp_idx = (uint32)comp_array.size() - 1u;
		auto& new_component = comp_array[comp_idx];
		new_component.m_owner = owner;
		new_component.m_id = comp_idx;
		m_actors[owner].mark_component(_t, comp_idx);
		return new_component;
	}

	template <component::type _t>
	void rem_component(actor_id owner)
	{
		m_actors[owner].unmark_component(_t);
		components<_t>().clear_owner();
	}
	
	actor_id create_actor(const actor_id parent = k_actor_invalid)
	{
		m_actors.push_back({});
		actor_id new_id = (actor_id)m_actors.size() - 1;
		actor& new_actor = m_actors[new_id];

		// add a transform
		transform_entry transform{};
		if (parent == k_actor_invalid)
		{
			new_actor.m_transform_id = m_transform_graph.add_node(transform);
		}
		else
		{
			const transform_id parent_trans_id = m_actors[parent].m_transform_id;
			new_actor.m_transform_id = m_transform_graph.add_node(transform, parent_trans_id);
		}
		return new_id;
	}

	bool has_component(actor_id owner, component::type type) const
	{
		return m_actors[owner].has_component(type);
	}

	template <component::type _t>
	component_t<_t>* get_component(actor_id owner)
	{
		if (has_component(owner, _t))
		{
			const auto& actor = m_actors[owner];
			const auto comp_idx = actor.m_component_idxs[(int)_t];
			return &components<_t>()[comp_idx];
		}
		else return nullptr;
	}

	template <component::type _t>
	component_t<_t> const* get_component(actor_id owner) const
	{
		if (has_component(owner, _t))
		{
			const auto& actor = m_actors[owner];
			const auto comp_idx = actor.m_component_idxs[(int)_t];
			return &components<_t>()[comp_idx];
		}
		else return nullptr;
	}

public:
	void start(const contentman& cman);
	void tick(float seconds, float delta_seconds);
	void build_renderscene(const contentman& cman, renderscene& scene) const;

private:
	void assemble_fbx_scene(const contentman& cman, const stringview& filepath);
};
}