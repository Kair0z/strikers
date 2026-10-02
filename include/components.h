#include "common.h"
#include "collisionman.h"

namespace strikers
{
using actor_id = uint32;
using comp_id = uint32;
using player_id = uint32;
static constexpr actor_id k_actor_invalid = -1;
static constexpr comp_id k_component_invalid = -1;

struct component
{
	enum class type
	{
		physics,
		bounds,
		render,
		animation,
		skeleton,
		light,
		renderui,
		movement,	// only attached to actors ON the pitch
		brain,		// AI decision component
		num
	};

	static const uint32 k_num_types = static_cast<uint32>(type::num);
	actor_id m_owner;
	comp_id m_id; // index in the component array

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

struct comp_physics final : public detail::component_t<component::type::physics>
{
	float3 m_acceleration;
	float3 m_velocity;
	float3 m_delta_position;
	bool m_is_trigger = false;
	uint32 m_collision_layer;
	uint32 m_collision_mask = ((uint32)-1) & ~(1 << collision_layers::common_ignored);
	float m_inv_mass;
	float m_maxspeed = -1.0f; // no max
	float m_gravity = -9.81f;
	float m_drag_multiplier = 1; // per-body 'drag coefficient'

	comp_physics& set_layer(uint32 layer) { m_collision_layer = layer; return *this; }
	comp_physics& ignore_layer(uint32 layer, bool ignore) { if (!ignore) m_collision_mask |= (1 << layer); else m_collision_mask &= ~(1 << layer); return *this; }
	comp_physics& set_trigger(bool trigger) { m_is_trigger = trigger; return *this; }
	comp_physics& set_static() { m_inv_mass = 0.0f; return *this; }
	comp_physics& set_mass(float mass) { m_inv_mass = 1.0f / mass; return *this; }
	bool is_static() const { return m_inv_mass <= 0.0f; }
	void reset() { m_velocity = {}; m_acceleration = {}; }
};

struct comp_bounds final : public detail::component_t<component::type::bounds>
{
	box m_box;
	box m_world_aabb;
	sphere m_sphere;
	bool m_world_aabb_dirty = true;
};

struct comp_render final : public detail::component_t<component::type::render>
{
	mesh_id m_mesh;
	mat_id m_material;
};

struct comp_animation final : public detail::component_t<component::type::animation>
{
	anim_id m_animation;
	float m_time;
};
struct comp_skeleton final : public detail::component_t<component::type::skeleton>
{
	skel_id m_skeleton;
};

struct comp_light final : public detail::component_t<component::type::light>
{
	light m_light;
};

struct comp_ui final : public detail::component_t<component::type::renderui>
{
	
};

struct comp_movement final : public detail::component_t<component::type::movement>
{
	float2 m_input;
};

struct comp_brain final : public detail::component_t<component::type::brain>
{
	float m_decision_timer;
	float2 m_decision;
};

template <component::type _t>
using component_t = std::tuple_element_t<static_cast<uint64>(_t), std::tuple<
	comp_physics,
	comp_bounds,
	comp_render,
	comp_animation,
	comp_skeleton,
	comp_light,
	comp_ui,
	comp_movement,
	comp_brain>>;

struct component_collection
{
	template <component::type _t>
	using component_array = vector<component_t<_t>>;
	component_array<component::type::physics> m_physics;
	component_array<component::type::bounds> m_bounds;
	component_array<component::type::render> m_renders;
	component_array<component::type::renderui> m_renderuis;
	component_array<component::type::movement> m_movements;
	component_array<component::type::brain> m_brains;
	component_array<component::type::animation> m_animations;
	component_array<component::type::skeleton> m_skeletons;
	component_array<component::type::light> m_lights;

	template <component::type _t>
	component_array<_t>& components()
	{
		if constexpr (_t == component::type::physics) return m_physics;
		else if constexpr (_t == component::type::bounds) return m_bounds;
		else if constexpr (_t == component::type::render) return m_renders;
		else if constexpr (_t == component::type::renderui) return m_renderuis;
		else if constexpr (_t == component::type::movement) return m_movements;
		else if constexpr (_t == component::type::brain) return m_brains;
		else if constexpr (_t == component::type::animation) return m_animations;
		else if constexpr (_t == component::type::skeleton) return m_skeletons;
		else if constexpr (_t == component::type::light) return m_lights;
	}

	template <component::type _t>
	const component_array<_t>& components() const
	{
		if constexpr (_t == component::type::physics) return m_physics;
		else if constexpr (_t == component::type::bounds) return m_bounds;
		else if constexpr (_t == component::type::render) return m_renders;
		else if constexpr (_t == component::type::renderui) return m_renderuis;
		else if constexpr (_t == component::type::movement) return m_movements;
		else if constexpr (_t == component::type::brain) return m_brains;
		else if constexpr (_t == component::type::skeleton) return m_skeletons;
		else if constexpr (_t == component::type::light) return m_lights;
	}
};
}