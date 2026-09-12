#pragma once
#include "common.h"
#include "components.h"
#include "flatgraph.h"
#include "transman.h"
#include "collisionman.h"

namespace strikers {
class renderscene;
class contentman;

class gameman final
{
	// each game can host up to 2 players
	struct player final
	{
		
	};

	// per character info
	struct character final
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

		character::type m_character;
		bool m_active = true;
		actor_id m_actor = k_actor_invalid;
		transform m_start_transform;
	};

	// each game always has 2 teams (football)
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

		uint32 m_current_local_runner;
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

	player m_players[2];
	team m_teams[team::num];
	runner m_runners[team::num * runner::num];
	ball m_ball;

	uint32 get_global_runner_idx(uint32 team_slot, uint32 runner_slot) { return (team_slot * runner::num) + runner_slot; }
	runner& get_runner_in_global(uint32 global_idx) { return m_runners[global_idx]; }
	runner& get_runner_in_team(uint32 team_idx, uint32 local_idx) { return get_runner_in_global(get_global_runner_idx(team_idx, local_idx)); }
	
private:
	struct camera
	{
		camera_id m_asset_id;
		transform m_transform;
		transform m_transform_original;
		float3 m_velocity;
	};
	camera m_camera;

	struct light
	{
		transform m_transform;
		float4 m_color;
	};
	light m_light;

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
	void build_renderscene(const contentman& cman, renderscene& scene);

private:
	void assemble_fbx_scene(const contentman& cman, const stringview& filepath);

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

	transman m_transman;
	collisionman m_collisionman;
	umap<string, actor_id> m_name_to_actor{};

	actor_id create_actor(const actor_id parent = k_actor_invalid)
	{
		m_actors.push_back({});
		actor_id new_id = (actor_id)m_actors.size() - 1;
		actor_entry& new_actor = m_actors[new_id];

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
		const transform get_transform(space spc) const;
		const float3 get_position(space spc) const;
		const rotation get_rotation(space spc) const;
		const float3 get_scale(space spc) const;
		const actor_scope& set_name(const string& name) const;
		const actor_scope& set_active(const bool active) const;
		const actor_scope& set_transform(const transform& trans, space spc) const;
		const actor_scope& set_position(const float3& position, space spc) const;
		const actor_scope& set_rotation(const rotation& rotation, space spc) const;
		const actor_scope& set_scale(const float3& scale, space spc) const;
		const actor_scope& add_position(const float3& delta, space spc) const;
		const actor_scope& add_rotation(const rotation& delta, space spc) const;
		const actor_scope& multiply_scale(const float3& multiplier, space spc) const;
		
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
};
}