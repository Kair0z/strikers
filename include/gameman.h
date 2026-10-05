#pragma once
#include "common.h"
#include "components.h"
#include "flatgraph.h"
#include "transman.h"
#include "collisionman.h"

namespace strikers {
class renderscene;
class contentman;
class fontman;

class gameman final
{
	// per character info
	struct character_goalie final
	{
		enum type
		{
			kritter,
			num
		};
		static const char* get_name(uint32 tpe)
		{
			static const char* k_names[]
			{
				"kritter"
			};
			return k_names[tpe];
		}
	};
	struct character_runner final
	{
		enum type
		{
			toad,
			mario,
			luigi,
			yoshi,
			num
		};

		static const char* get_name(uint32 tpe)
		{
			static const char* k_names[]
			{
				"toad",
				"mario",
				"luigi",
				"yoshi"
			};
			return k_names[tpe];
		}
	};

	// any character that runs on the pitch (not including goalies)
	struct runner final
	{
		enum slot
		{
			cb,
			lw,
			rw,
			captain,
			num
		};

		static const char* get_name(uint32 slt)
		{
			static const char* k_names[]
			{
				"cb",
				"lw",
				"rw",
				"cap"
			};
			return k_names[slt];
		}

		character_runner::type m_character;
		bool m_active = true;
		actor_id m_actor = k_actor_invalid;
		transform m_start_transform;
	};

	// each game always has 2 teams
	struct team final
	{
		enum slot
		{
			left,
			right,
			num
		};
		static const char* get_name(uint32 slt)
		{
			static const char* k_names[]
			{
				"team_left",
				"team_right"
			};
			return k_names[slt];
		}

		actor_id m_goal_actor = k_actor_invalid;
		float m_action_cooldown_timer = 0.0f;

		bool action_on_cooldown() const
		{
			return m_action_cooldown_timer > 0.0f;
		}
		void reset_cooldown()
		{
			m_action_cooldown_timer = 0.8f;
		}
	};

	// each team has 1 goalie
	struct goalie final
	{
		character_goalie::type m_character;
		actor_id m_actor = k_actor_invalid;
		transform m_start_transform;
	};

	// each game can host up to 4 players (actual persons)
	struct player final
	{
		enum slot
		{
			one,
			two,
			three,
			four,
			num
		};

		uint32 m_team_idx;
		uint32 m_current_local_runner = runner::captain;
		bool m_active = false;
	};

	// each game has only 1 ball
	struct ball final
	{
		enum state
		{
			idle,
			keeper,
			dribble,
			charging,
			passing,
			launching,
			num
		};

		struct {
			uint32 m_runner_source;
			uint32 m_runner_dest;
			float m_init_distance;
		} m_pass;
		struct {
			uint32 m_runner_idx;
		} m_dribble;
		struct {
			float3 m_point_source;
			float3 m_point_dest;
			uint32 m_runner_source;
		} m_launch;
		struct
		{
			uint32 m_runner_idx;
			float m_value;
			float m_seconds_since_start;
		} m_charge;
		
		actor_id m_actor = k_actor_invalid;
		state m_state;
	};

	// the state of the pitch
	struct field final
	{
		actor_id m_midpoint_actor = k_actor_invalid;
		actor_id m_pitch_bounds_actor = k_actor_invalid;
		box m_pitch_bounds;

		float2 normalize_position(const float3& position) const
		{
			const float3 field_min = m_pitch_bounds.abs_min();
			const float3 field_max = m_pitch_bounds.abs_max();
			return float2(
				remap(position.x, field_min.x, field_max.x, 0, 1),
				remap(position.z, field_min.z, field_max.z, 0, 1)
			);
		}
	};

	float2 get_field_normalized_position(const float3& position) const
	{
		const float2 normalized_position = m_field.normalize_position(position);
		const float2 signed_position = float2(
			(normalized_position.x * 2) - 1.0f,
			(normalized_position.y * 2) - 1.0f
		);

		const float2 camera_relative_signed_position = float2(
			m_camera.m_initial_right.x < 0.0f ? -signed_position.x : signed_position.x,
			normalized_position.y
		);
		return float2(
			camera_relative_signed_position.x * 0.5 + 0.5,
			camera_relative_signed_position.y * 0.5 + 0.5);
	}

	player m_players[player::num];
	team m_teams[team::num];
	goalie m_goalies[team::num];
	runner m_runners[team::num * runner::num];
	ball m_ball;
	field m_field;

	umap<actor_id, uint32> m_actor_to_runner_idx;
	umap<actor_id, uint32> m_actor_to_goalie_idx;

	struct main_camera final
	{
		actor_id m_actor = k_actor_invalid;
		float3 m_velocity;
		float3 m_initial_right;
		float3 m_movement_input;
		float2 m_orient_input;
		float m_sprint;
		camera m_camera;
	};
	main_camera m_camera;

	struct lights final
	{
		actor_id m_actor_directional;
	};
	lights m_lights{};

	uint32 get_runner_idx(uint32 team_slot, uint32 runner_slot) const { return (team_slot * runner::num) + runner_slot; }
	uint32 runner_get_team_idx(uint32 runner_idx) const { return runner_idx / runner::num; }
	uint32 runner_get_local_idx(uint32 runner_idx) const { return runner_idx % runner::num; }
	runner& get_runner_in_global(uint32 global_idx) { return m_runners[global_idx]; }
	runner& get_runner_in_team(uint32 team_idx, uint32 local_idx) { return get_runner_in_global(get_runner_idx(team_idx, local_idx)); }

	void runner_start_dribble(uint32 runner_idx)
	{
		m_ball.m_state = ball::state::dribble;
		m_ball.m_dribble.m_runner_idx = runner_idx;

		const uint32 team = runner_get_team_idx(runner_idx);
		const uint32 local = runner_get_local_idx(runner_idx);
		for (uint32 i = 0u; i < player::num; ++i)
		{
			if (m_players[i].m_team_idx == team)
			{
				m_players[i].m_current_local_runner = local;
			}
		}
	}
	void runner_shoot(uint32 runner_idx)
	{
		if (runner_has_ball_charge(runner_idx))
		{
			const runner& rnr = m_runners[runner_idx];

			const uint32 enemy_team = (runner_get_team_idx(runner_idx) + 1) % team::num;
			float3 dest_point = actor(m_teams[enemy_team].m_goal_actor).get_position();
			dest_point.y += 1.5f;

			m_ball.m_state = ball::state::launching;
			m_ball.m_launch.m_point_source = actor(rnr.m_actor).get_position();
			m_ball.m_launch.m_point_dest = dest_point;
		}
	}
	void runner_pass(uint32 runner_idx, const uint32 dest_runner)
	{
		const uint32 team_idx = runner_get_team_idx(runner_idx);
		if (runner_can_pass(runner_idx))
		{
			m_ball.m_state = ball::state::passing;
			m_ball.m_pass.m_runner_source = runner_idx;
			m_ball.m_pass.m_runner_dest = dest_runner;

			const actor_id dst_actor = m_runners[dest_runner].m_actor;
			const actor_id src_actor = m_runners[runner_idx].m_actor;
			m_ball.m_pass.m_init_distance = glm::length(actor(dst_actor).get_position() - actor(m_ball.m_actor).get_position());
			
			m_teams[team_idx].reset_cooldown();
		}
	}
	void runner_charge(uint32 runner_idx)
	{
		if (runner_can_charge(runner_idx))
		{
			m_ball.m_state = ball::state::charging;
			m_ball.m_charge.m_runner_idx = runner_idx;
			m_ball.m_charge.m_seconds_since_start = 0.0f;
		}
	}
	bool runner_can_pass(uint32 runner_idx) const
	{
		const uint32 team_idx = runner_get_team_idx(runner_idx);
		return !m_teams[team_idx].action_on_cooldown() &&
			(runner_has_ball_dribble(runner_idx) || runner_has_ball_charge(runner_idx));
	}
	bool runner_can_charge(uint32 runner_idx)
	{
		const uint32 team_idx = runner_get_team_idx(runner_idx);

		const float3 runner_position = actor(m_runners[runner_idx].m_actor).get_position();
		const float2 field_normalized_position = get_field_normalized_position(runner_position);
		const bool on_correct_side = (team_idx == team::left && field_normalized_position.x < 0.5f)
			|| (team_idx == team::right && field_normalized_position.x > 0.5f);

		return on_correct_side && runner_has_ball_dribble(runner_idx) &&
			!m_teams[team_idx].action_on_cooldown();
	}
	bool runner_has_ball_dribble(uint32 runner_idx) const
	{
		return m_ball.m_state == ball::state::dribble && runner_idx == m_ball.m_dribble.m_runner_idx;
	}
	bool runner_has_ball_charge(uint32 runner_idx) const
	{
		return m_ball.m_state == ball::state::charging && runner_idx == m_ball.m_charge.m_runner_idx;
	}
	bool runner_is_pass_dest(uint32 runner_idx) const
	{
		return m_ball.m_state == ball::state::passing && runner_idx == m_ball.m_pass.m_runner_dest;
	}
	bool runner_is_on_own_half(uint32 runner_idx)
	{
		const uint32 team_idx = runner_get_team_idx(runner_idx);
		return team_idx == get_team_on_position(actor(m_runners[runner_idx].m_actor).get_position());
	}
	bool is_runner_controlled_by_player(uint32 runner_idx) const
	{
		const uint32 team_idx = runner_get_team_idx(runner_idx);
		const uint32 local_idx = runner_get_local_idx(runner_idx);
		for (uint32 i = 0u; i < player::num; ++i)
		{
			if (m_players[i].m_active 
				&& m_players[i].m_team_idx == team_idx
				&& m_players[i].m_current_local_runner == local_idx)
			{
				return true;
			}
		}
		return false;
	}
	uint32 runner_get_enemy_team(uint32 runner_idx) const
	{
		return (runner_get_team_idx(runner_idx) + 1) % team::num;
	}
	uint32 get_team_on_position(float3 position)
	{
		return (position.x > actor(m_field.m_midpoint_actor).get_position().x) ? 0 : 1;
	}
	uint32 count_active_players() const
	{
		uint32 count = 0u;
		for (uint32 i = 0u; i < player::num; ++i)
		{
			count += m_players[i].m_active;
		}
		return count;
	}

public:
	void start(const contentman& cman);

	struct tick_context
	{
		float seconds;
		float delta_seconds;
	};
	void tick(const tick_context& ctx);
	void tick_game(const tick_context& ctx);
	void tick_systems(const tick_context& ctx);
	void build_renderscene(contentman& cman, renderscene& scene);
	void detect_collisions();
	
	template <typename _fn>
	void foreach_collision(actor_id actr, _fn&& func)
	{
		if (auto* bounds = actor(actr).component<component::type::bounds>())
		{
			uint32 other_collider{};
			for (uint32 i = 0u; i < m_collisionman.num_collisions(bounds->m_id); ++i)
			{
				if (m_collisionman.get_collision(bounds->m_id, i, other_collider))
				{
					const comp_bounds& other_bounds = components<component::type::bounds>()[other_collider];
					func(other_bounds);
				}
			}
		}
	}

private:
	struct reset
	{
		enum flags
		{
			none = 0,
			camera = 1 << 0,
			game = 1 << 1,
			all = game | camera
		};
	};
	void reset(reset::flags flags);

	component_collection m_components;
	transman m_transman;
	collisionman m_collisionman;

	actor_id create_actor(const actor_id parent = k_actor_invalid)
	{
		m_actors.push_back({});
		actor_id new_id = (actor_id)m_actors.size() - 1;
		actor_entry& new_actor = m_actors[new_id];

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

	struct actor_entry final
	{
		actor_id m_id;
		trans_id m_transform_id;
		string m_name;
		uint64 m_component_idxs[component::k_num_types]{ k_component_invalid };
		bool m_active;

		actor_entry()
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
	vector<actor_entry> m_actors;
	actor_entry& get_actor(actor_id id)
	{
		return (id != k_actor_invalid) ? m_actors[id] : null_actor();
	}
	static actor_entry& null_actor()
	{
		static actor_entry null{};
		return null;
	}
	struct actor_scope final
	{
		actor_id m_id{};
		gameman& m_owner;
		actor_scope(gameman& game, actor_id id) : m_owner{ game }, m_id { id } {}

		const string& get_name() const;
		const bool is_active() const;
		const trans_id get_transform_id() const;
		const transform get_transform(space spc = space::world) const;
		const float3 get_position(space spc = space::world) const;
		const rotation get_rotation(space spc = space::world) const;
		const float3 get_scale(space spc = space::world) const;
		const actor_scope& set_name(const string& name) const;
		const actor_scope& set_active(const bool active) const;
		const actor_scope& set_transform(const transform& trans, space spc = space::world) const;
		const actor_scope& set_position(const float3& position, space spc = space::world) const;
		const actor_scope& set_rotation(const rotation& rotation, space spc = space::world) const;
		const actor_scope& set_scale(const float3& scale, space spc) const;
		const actor_scope& add_position(const float3& delta, space spc) const;
		const actor_scope& add_rotation(const rotation& delta, space spc) const;
		const actor_scope& multiply_scale(const float3& multiplier, space spc) const;
		const actor_scope& reset_transform() const;

		bool has_component(component::type type) const {
			return m_owner.get_actor(m_id).has_component(type);
		}

		template <component::type _t>
		component_t<_t>* component() const
		{
			if (has_component(_t))
			{
				const auto& actor = m_owner.get_actor(m_id);
				const auto comp_idx = actor.m_component_idxs[(int)_t];
				return &m_owner.components<_t>()[comp_idx];
			}
			else return nullptr;
		}

		template <component::type _t>
		const actor_scope& add_component() const
		{
			if (has_component(_t))
			{
				return *this; // skip, already existing component
			}

			// first find an inactive entry: reparent
			auto& comp_array = m_owner.components<_t>();
			for (uint32 i = 0u; i < comp_array.size(); ++i)
			{
				if (comp_array[i].has_owner() == false)
				{
					comp_array[i] = {}; // reset data
					comp_array[i].m_owner = m_id; // reparent
					comp_array[i].m_id = i;
					m_owner.get_actor(m_id).mark_component(_t, i);
					return *this;
				}
			}

			// allocate a new component if no unused was found
			comp_array.push_back({});
			const uint32 comp_idx = (uint32)comp_array.size() - 1u;
			auto& new_component = comp_array[comp_idx];
			new_component.m_owner = m_id;
			new_component.m_id = comp_idx;
			m_owner.get_actor(m_id).mark_component(_t, comp_idx);
			return *this;
		}

		template <component::type _t>
		void rem_component()
		{
			m_owner.get_actor(m_id).unmark_component(_t);
			m_owner.components<_t>().clear_owner();
		}
	};
	actor_scope actor(actor_id id) { return actor_scope(*this, id); }

	template <component::type _t>
	component_collection::component_array<_t>& components() { return m_components.components<_t>(); }
	template <component::type _t>
	const component_collection::component_array<_t>& components() const { return m_components.components<_t>(); }
};
}