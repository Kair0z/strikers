#include "common.h"
#include "components.h"
#include "flatgraph.h"
#include "transman.h"

#define on_cooldown(condition, delta, cd) \
	{ \
	static float s_timer = 0.0f; \
	s_timer -= delta;\
	bool is_tick = s_timer < 0; \
	if (is_tick && condition) s_timer = cd; \
	if (is_tick && condition)\

namespace strikers {
class renderscene;
class contentman;

class colman final
{
public:
	struct collider_entry
	{
		box m_aabb;
		vector<uint32> m_collisions{};
	};
	struct collision_info
	{
		uint32 m_collider_y;
		uint32 m_collider_x;
		float3 m_overlap{};
	};

	void write_aabb(const uint32 slot, const box& aabb)
	{
		// resize if necessary
		const uint32 new_size = slot + 1;
		m_collisions.reserve(new_size * new_size);
		if (m_colliders.size() <= new_size) 
			m_colliders.resize(new_size);
		
		m_colliders[slot].m_aabb = aabb;
	}

	void calculate_collisions()
	{
		for (uint32 x = 0u; x < m_colliders.size(); ++x)
			m_colliders[x].m_collisions.clear();

		m_collisions.clear();
		for (uint32 y = 0u; y < m_colliders.size(); ++y)
		{
			for (uint32 x = 0u; x < m_colliders.size(); ++x)
			{
				// only test half of the matrix
				if (x <= y) continue;

				float3 collision_overlap{};
				const bool is_collision = box::calculate_collision(m_colliders[x].m_aabb, m_colliders[y].m_aabb, &collision_overlap);
				if (is_collision)
				{
					m_collisions.push_back({});
					const uint32 collision_index = (uint32)m_collisions.size() - 1;
					collision_info& collision = m_collisions[collision_index];
					collision.m_collider_y = y;
					collision.m_collider_x = x;
					collision.m_overlap = collision_overlap;
					m_colliders[y].m_collisions.push_back(collision_index);
					m_colliders[x].m_collisions.push_back(collision_index);
				}
			}
		}
	}

	uint32 num_collisions() const
	{
		return (uint32)m_collisions.size();
	}

	uint32 num_collisions(const uint32 slot) const
	{
		return (uint32)m_colliders[slot].m_collisions.size();
	}

	const collision_info& get_collision_at_index(const uint32 slot, uint32 index)
	{
		return m_collisions[m_colliders[slot].m_collisions[index]];
	}

	bool test_collision(const box& aabb, const float3 delta_position, float3& out_) const
	{
		for (uint32 x = 0u; x < m_colliders.size(); ++x)
		{
			if (box::calculate_collision(aabb, m_colliders[x].m_aabb))
				return true;
		}
		return false;
	}
	
private:
	vector<collider_entry> m_colliders{};
	vector<collision_info> m_collisions{};
};

class gameman final
{
	// per team data
	struct team
	{
	};

	// per player data
	struct player_data final
	{
		uint32 m_current_local_runner;
	};
	
	// any 'runner' pawn that runs on the pitch
	struct runner final
	{
		enum flags
		{
			none = 0,
			kritter = (1 << 0),
			toad = (1 << 1),
			ball = (1 << 2),
			mario = (1 << 3),
			luigi = (1 << 4)
		};

		actor_id m_actor = k_actor_invalid;
		uint32 m_flags = 0u;
	};

	// the ball state
	struct ball final
	{
		enum state
		{
			idle,
			keeper,
			dribbled,
			passing,
			launching,
			num
		};

		struct {
			uint32 m_runner_source;
			uint32 m_runner_dest;
		} m_pass;
		struct {
			uint32 m_runner_dribble;
		} m_dribble;
		struct {
			float3 m_point_source;
			float3 m_point_dest;
		} m_launch;

		actor_id m_actor;
		state m_state;
		float m_charge = 0.0f;
	};

	player_data m_players[2];
	team m_teams[2];
	runner m_runners[k_num_runners_per_team * 2];
	ball m_ball;

	runner& get_runner(uint32 team_idx, uint32 local_idx) { return m_runners[get_runner_idx(team_idx, local_idx)]; }
	runner& get_runner(uint32 index) { return m_runners[index]; }
	uint32 get_runner_team_idx(uint32 runner_idx) const { return runner_idx / k_num_runners_per_team; }
	uint32 get_runner_local_idx(uint32 runner_idx) const { return runner_idx % k_num_runners_per_team; }
	uint32 get_runner_idx(const uint32 team_idx, uint32 local_idx) { return (k_num_runners_per_team * team_idx) + local_idx; }
	void set_runner_active(const uint32 team_idx, uint32 local_idx)
	{
		m_players[team_idx].m_current_local_runner = local_idx;
	}
	void set_runner_active(const uint32 index)
	{
		const uint32 team = get_runner_team_idx(index);
		const uint32 local = get_runner_local_idx(index);
		set_runner_active(team, local);
	}

	void ball_start_dribble(const uint32 runner_idx)
	{
		m_ball.m_dribble.m_runner_dribble = runner_idx;
		m_ball.m_state = ball::state::dribbled;
		set_runner_active(runner_idx);
	}
	void ball_pass(const uint32 runner_source, const uint32 runner_dest)
	{
		m_ball.m_pass.m_runner_dest = runner_dest;
		m_ball.m_pass.m_runner_source = runner_source;
		m_ball.m_state = ball::state::passing;
	}
	bool ball_held_by_runner(const uint32 rnn) const
	{
		if (m_ball.m_state != ball::dribbled) return false;
		return m_ball.m_dribble.m_runner_dribble == rnn;
	}

	// any actor that has a transform in the scene
	struct actor final
	{
		actor_id m_id;
		trans_id m_transform_id;
		uint64 m_component_idxs[component::k_num_types]{ k_component_invalid };
		bool m_is_static = false;

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

	void actor_set_static(actor_id id, bool is_static)
	{
		m_actors[id].m_is_static = is_static;
	}
	bool actor_is_static(actor_id id) const { return m_actors[id].m_is_static; }

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
		if (parent == k_actor_invalid)
		{
			new_actor.m_transform_id = m_transman.add_world_transform(transform::identity());
		}
		else
		{
			new_actor.m_transform_id = m_transman.add_world_transform(transform::identity(), m_actors[parent].m_transform_id);
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
	void build_renderscene(const contentman& cman, renderscene& scene);

private:
	void assemble_fbx_scene(const contentman& cman, const stringview& filepath);
	void reset();

	transman m_transman;
	trans_id actor_transform(actor_id id) { return m_actors[id].m_transform_id; }
	void actor_set_transform(actor_id id, const transform& trans, space spc)	{ if (!actor_is_static(id)) m_transman.set_transform(actor_transform(id), trans, spc); }
	void actor_set_position(actor_id id, const float3& position, space spc)		{ if (!actor_is_static(id)) m_transman.set_position(actor_transform(id), position, spc); }
	void actor_set_rotation(actor_id id, const rotation& rotation, space spc)	{ if (!actor_is_static(id)) m_transman.set_rotation(actor_transform(id), rotation, spc); }
	void actor_set_scale(actor_id id, const float3& scale, space spc)			{ if (!actor_is_static(id)) m_transman.set_scale(actor_transform(id), scale, spc); }
	transform actor_get_transform(actor_id id, space spc)						{ return m_transman.get_transform(actor_transform(id), spc); }
	float3 actor_get_position(actor_id id, space spc)							{ return m_transman.get_position(actor_transform(id), spc); }
	rotation actor_get_rotation(actor_id id, space spc)							{ return m_transman.get_rotation(actor_transform(id), spc); }
	float3 actor_get_scale(actor_id id, space spc)								{ return m_transman.get_scale(actor_transform(id), spc); }
	void actor_add_position(actor_id id, const float3& delta, space spc)		{ if (!actor_is_static(id)) m_transman.add_position(actor_transform(id), delta, spc); }
	void actor_add_rotation(actor_id id, const rotation& delta, space spc)		{ if (!actor_is_static(id)) m_transman.add_rotation(actor_transform(id), delta, spc); }
	void actor_mult_scale(actor_id id, const float3& multiplier, space spc)		{ if (!actor_is_static(id)) m_transman.mult_scale(actor_transform(id), multiplier, spc); }
	
	colman m_colman;
};
}