#include "common.h"
#include "physman.h"
#include "components.h"

namespace strikers {
class renderscene;
class contentman;
class gameman
{
private:
	template <component::type _t>
	using component_array = vector<component_t<_t>>;
	struct
	{
		component_array<component::type::transform> m_transforms;
		component_array<component::type::physics> m_physics;
		component_array<component::type::bounds> m_bounds;
		component_array<component::type::render> m_renders;
		component_array<component::type::renderui> m_renderuis;
	} m_components;

	template <component::type _t>
	component_array<_t>& components()
	{
		if constexpr (_t == component::type::transform) return m_components.m_transforms;
		else if constexpr (_t == component::type::physics) return m_components.m_physics;
		else if constexpr (_t == component::type::bounds) return m_components.m_bounds;
		else if constexpr (_t == component::type::render) return m_components.m_renders;
		else if constexpr (_t == component::type::renderui) return m_components.m_renderuis;
	}
	template <component::type _t>
	const component_array<_t>& components() const
	{
		if constexpr (_t == component::type::transform) return m_components.m_transforms;
		else if constexpr (_t == component::type::physics) return m_components.m_physics;
		else if constexpr (_t == component::type::bounds) return m_components.m_bounds;
		else if constexpr (_t == component::type::render) return m_components.m_renders;
		else if constexpr (_t == component::type::renderui) return m_components.m_renderuis;
	}

	struct camera
	{
		camera_id m_asset_id;
		transform m_transform;
		transform m_transform_original;
		float3 m_velocity;
	};
	camera m_camera;

	struct actor final
	{
		actor_id m_id;
		uint64 m_component_idxs[component::k_num_types]{ k_component_invalid };

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
	vector<actor> m_actors;

	template <component::type _t>
	component_t<_t>& add_component(actor_id owner)
	{
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
	
	actor_id create_actor()
	{
		m_actors.push_back({});
		actor_id new_id = (actor_id)m_actors.size() - 1;
		actor& new_actor = m_actors[new_id];
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