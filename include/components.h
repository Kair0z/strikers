#include "common.h"

namespace strikers
{
using actor_id = uint32;
using comp_id = uint32;
static constexpr actor_id k_actor_invalid = -1;
static constexpr comp_id k_component_invalid = -1;

struct component
{
	enum class type
	{
		transform,
		physics,
		render,
		renderui,
		num
	};

	static const uint32 k_num_types = static_cast<uint32>(type::num);
	actor_id m_owner;
	comp_id m_id;

	bool has_owner() const
	{
		return m_owner != k_actor_invalid;
	}

	void clear_owner()
	{
		m_owner = k_actor_invalid;
	}
};

namespace detail
{
template <component::type _t>
struct component_t : public component
{
	static const component::type k_type = _t;
};
}

struct comp_transform final : public detail::component_t<component::type::transform>
{
	// world transform (no hierarchy implemented yet)
	transform m_transform;
};

struct comp_physics final : public detail::component_t<component::type::physics>
{
	float3 m_velocity;
	float m_gravity = -9.81f;
	float m_drag_air = 1; // per-body 'drag coefficient'
};

struct comp_render final : public detail::component_t<component::type::render>
{
	mesh_id m_mesh;
	mat_id m_material;
};

struct comp_ui final : public detail::component_t<component::type::renderui>
{
	
};

template <component::type _t>
using component_t = std::tuple_element_t<static_cast<uint64>(_t), std::tuple<
	comp_transform,
	comp_physics,
	comp_render,
	comp_ui>>;

}