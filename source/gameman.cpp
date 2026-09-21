#include "gameman.h"
#include "renderman.h"
#include "contentman.h"
#include "inputman.h"
#include "commandman.h"
#include "iniman.h"
#include "fontman.h"

namespace strikers {

// game commands
command cm_camera_sensy("camera_sensy", "0.1");
command cm_camera_dev_control("camera_dev_control", "0");
command cm_camera_dev_reset("camera_dev_reset", "r");
command cm_camera_dev_left("camera_dev_left", "a");
command cm_camera_dev_right("camera_dev_right", "d");
command cm_camera_dev_forward("camera_dev_forward", "w");
command cm_camera_dev_back("camera_dev_back", "s");
command cm_camera_dev_up("camera_dev_up", "space");
command cm_camera_dev_down("camera_dev_down", "shift");
command cm_camera_dev_sprint("camera_dev_sprint", "shift");
command cm_camera_maxspeed("camera_maxspeed", "10");
command cm_camera_acceleration("camera_acceleration", "10");

// game controls
command cm_controls_left("controls_left", "a");
command cm_controls_up("controls_up", "w");
command cm_controls_right("controls_right", "d");
command cm_controls_down("controls_down", "s");
command cm_controls_pass("controls_pass", "shift");
command cm_controls_shoot("controls_shoot", "space");

// physics
command cm_phx_enabled("phx_enabled", "1");
command cm_phx_gravity_enabled("phx_gravity_enabled", "1");

// game
command cm_game_reset("game_reset", "r");
command cm_game_airdrag("game_airdrag", "1.225");
command cm_game_movement_acc("game_movement_acc", "2");
command cm_game_movement_mxsp("game_movement_mxsp", "6");
command cm_game_ball_mxsp("game_ball_mxsp", "1000");
command cm_game_ball_pass_spd("game_ball_pass_spd", "1");
command cm_game_ball_shot_spd("game_ball_shot_spd", "1");
command cm_game_ball_decay_seconds("game_ball_decay_seconds", "8"); // time it takes for a ball to decay entirely to 0
command cm_game_ball_maxshoot_seconds("game_ball_maxshoot_seconds", "2"); // time until shoot releases
command cm_game_ball_maxcharge_seconds("game_ball_maxcharge_seconds", "4"); // time until charge is full

command cm_game_ai("game_ai", "1");
command cm_game_ai_min_dtime("game_ai_min_dtime", "1");
command cm_game_ai_max_dtime("game_ai_max_dtime", "1");
command cm_game_ai_random("game_ai_random", "1");

command cm_draw_dbg("draw_dbg", "0");
command cm_draw_dbg_bounds("draw_dbg_bounds", "0");
command cm_draw_dbg_transforms("draw_dbg_transforms", "0");
command cm_draw_dbg_ball("draw_dbg_ball", "0");

command cm_gfx_shadowmap_size("gfx_shadowmap_size", "1024");
command cm_gfx_shadowfrustrum_size("gfx_shadowfrustrum_size", "50");

// in this function, we parse the fbx asset data (loaded in cman) 
// and construct a hierarchy of actors, transforms & components.
void gameman::assemble_fbx_scene(const contentman& cman, const stringview& filepath)
{
	const scene_id scid = contentman::make_scene_id(filepath);
	const scene_asset& scene = *cman.find_typed_asset<asset_type::scene>(scid).claim();

	// load camera data
	if (!scene.m_cameras.empty())
	{
		m_camera.m_asset_id = scene.m_cameras[0];
		camera_asset const* camera = cman.find_camera(m_camera.m_asset_id);
		if (camera)
		{
			m_camera.m_transform.m_matrix = camera->m_scene_transform;
			m_camera.m_transform.add_position_local({ 0,0,15 });
			m_camera.m_transform_original = m_camera.m_transform;
		}
	}

	// translate the scene hierarchy to 'game transform hierarchy'
	umap<uint32, actor_id> node_id_to_actor_id{};
	scene.m_graph.traverse([cman, this, &scene, &node_id_to_actor_id](uint32 node_id, uint32 parent_id)
	{
		const bool valid_parent = scene.m_graph.is_valid(parent_id) && node_id_to_actor_id.contains(parent_id);
		const actor_id parent_actor = valid_parent ? node_id_to_actor_id[parent_id] : k_actor_invalid;
		const scene_asset::node& parent = scene.m_graph.get(parent_id).data();
		const scene_asset::node& node = scene.m_graph.get(node_id).data();

		auto instantiate = [this, &node_id_to_actor_id, node_id, &cman](
			const actor_id parent_actor,
			const scene_asset::node& node,
			const scene_asset::node& parent_node,
			bool is_runner) -> actor_id
		{
			const actor_id root_actor = create_actor(parent_actor);
			node_id_to_actor_id[node_id] = root_actor;
			actor(root_actor)
				.set_transform(node.m_scene_transform, space::world)
				.set_name(node.m_name);

			auto initialize_dynamic_physics = [this](actor_id id) {
				actor(id).add_component<component::type::physics>();
				actor(id).component<component::type::physics>()
					->set_mass(1.0f)
					.set_layer(collision_layers::common);
			};
			auto initialize_static_physics = [this](actor_id id) {
				actor(id).add_component<component::type::physics>();
				actor(id).component<component::type::physics>()
					->set_static()
					.set_layer(collision_layers::static_level);
			};
			auto initialize_runner = [this](actor_id id) {
				actor(id)
					.add_component<component::type::movement>()
					.add_component<component::type::brain>();
			};

			if (is_runner)
			{
				initialize_dynamic_physics(root_actor);
				initialize_runner(root_actor);
			}

			if (strstr(node.m_name.c_str(), "kritter_left"))
			{
				initialize_dynamic_physics(root_actor);
				m_goalies[team::left].m_actor = root_actor;
			}
			if (strstr(node.m_name.c_str(), "kritter_right"))
			{
				initialize_dynamic_physics(root_actor);
				m_goalies[team::right].m_actor = root_actor;
			}
			if (strstr(node.m_name.c_str(), "ball"))
			{
				initialize_dynamic_physics(root_actor);
				m_ball.m_actor = root_actor;
			}
			if (strstr(node.m_name.c_str(), "wall"))
			{
				initialize_static_physics(root_actor);
			}
			if (strstr(node.m_name.c_str(), "floor"))
			{
				initialize_static_physics(root_actor);
			}
			if (strstr(node.m_name.c_str(), "light"))
			{
				m_light.m_transform = actor(root_actor).get_transform(space::world);
			}
			if (strstr(node.m_name.c_str(), "goalzone_left"))
			{
				initialize_static_physics(root_actor);
				m_teams[team::left].m_goal_actor = root_actor;
			}
			if (strstr(node.m_name.c_str(), "goalzone_right"))
			{
				initialize_static_physics(root_actor);
				m_teams[team::right].m_goal_actor = root_actor;
			}
			if (strstr(node.m_name.c_str(), "kp_middle"))
			{
				m_field.m_midpoint_actor = root_actor;
			}

			// parse the keypoint transforms
			if (strstr(node.m_name.c_str(), "team_"))
			{
				for (uint32 t = 0u; t < team::num; ++t)
					for (uint32 r = 0u; r < runner::num; ++r)
					{
						const string keypoint_name = format("{}_{}", team::get_name(t), runner::get_name(r));
						if (strstr(node.m_name.c_str(), keypoint_name.c_str()))
						{
							m_runners[get_global_runner_idx(t, r)].m_start_transform
								= actor(root_actor).get_transform(space::world);
						}
					}
			}
			
			// create each mesh as child actor
			for (uint32 i = 0u; i < node.m_meshes.size(); ++i)
			{
				const mesh_id meshid = node.m_meshes[i];
				actor_id mesh_actor = create_actor(root_actor);
				m_transman.set_transform(m_actors[mesh_actor].m_transform_id, transform::identity(), space::local);

				// get the mesh & material, if material is named 'collider' we add a bounds component to the actor
				bool is_material_collider = false;
				mesh_asset const* mesh = nullptr; material_asset const* material = nullptr;
				if (cman.find_mesh_and_material(meshid, mesh, material))
				{
					if (strstr(material->m_name.c_str(), "collider"))
					{
						actor(root_actor).add_component<component::type::bounds>();
						auto* bounds = actor(root_actor).component<component::type::bounds>();
						bounds->m_box = mesh->m_bounds_box;
						bounds->m_sphere = mesh->m_bounds_sphere;
						// mesh_transform.m_name = node.m_name + ": collider";
					}
					else
					{
						actor(mesh_actor).add_component<component::type::render>();
						auto* render = actor(mesh_actor).component<component::type::render>();
						render->m_mesh = meshid;
						// mesh_transform.m_name = node.m_name + ": mesh-" + std::to_string(i);
					}
				}
			}

			return root_actor;
		};

		// check if we want to create this actor as a character...
		character::type character = character::num;
		for (uint32 c = 0u; c < character::num; ++c)
		{
			if (strstr(node.m_name.c_str(), character::get_name((character::type)c)))
			{
				character = (character::type)c;
			}
		}

		// in case of a character, we may instantiate it multiple times, may instantiate it 0 times
		// it depends on the setupscript
		uint32 global_runner_idx = (uint32)-1;
		if (character == character::num)
		{
			instantiate(parent_actor, node, parent, false);
		}
		else
		{
			for (uint32 t = 0u; t < team::num; ++t)
			{
				for (uint32 r = 0u; r < runner::num; ++r)
				{
					const uint32 runner_index = get_global_runner_idx(t, r);
					runner& rnr = m_runners[runner_index];
					if (rnr.m_character == character && rnr.m_actor == k_actor_invalid)
					{
						rnr.m_actor = instantiate(parent_actor, node, parent, true);
					}
				}
			}
		}
	});

	// set the runner transforms to their designated keypoint
	for (uint32 r = 0u; r < team::num * runner::num; ++r)
	{
		const runner& rnr = m_runners[r];
		m_actor_to_runner_idx[rnr.m_actor] = r;
		actor(rnr.m_actor)
			.set_position(rnr.m_start_transform.get_position(), space::world)
			.set_rotation(rnr.m_start_transform.get_rotation(), space::world);
	}

	// resolve & save as 'reset' position
	m_transman.save_as_reset();
}

void gameman::reset(reset::flags flags)
{
	if (flags & reset::game)
	{
		m_transman.reset();

		for (auto& phys : components<component::type::physics>())
			phys.reset();
		for (auto& move : components<component::type::movement>())
			move.m_input = float2();

		m_ball.m_state = ball::state::idle;
	}
	
	if (flags & reset::camera)
	{
		m_camera.m_velocity = {};
		m_camera.m_transform = m_camera.m_transform_original;
	}
}

void gameman::start(const contentman& cman)
{
	// parse setup script
	umap<string, string> setup_settings{};
	if (iniman::parse(DF_SETUP_SCRIPT, setup_settings))
	{
		for (uint32 t = 0u; t < team::num; ++t)
		{
			for (uint32 i = 0u; i < runner::num; ++i)
			{
				const uint32 runner_idx = get_global_runner_idx(t, i);
				const string varname = format("{}.{}", team::get_name((team::slot)t), runner::get_name((runner::slot)i));
				for (uint32 c = 0u; c < character::num; ++c)
				{
					if (strstr(character::get_name(c), setup_settings[varname].c_str()))
					{
						m_runners[runner_idx].m_character = (character::type)c;
					}
				}
			}
		}
	}

	assemble_fbx_scene(cman, string(k_content_folder) + "scene.fbx");

	// setup collision layers
	{
		m_collisionman.set_layer_response(
			collision_layers::common, 
			collision_layers::common,
			collision_response::block);

		m_collisionman.set_layer_response(
			collision_layers::common, 
			collision_layers::common_ignored,
			collision_response::ignore);

		m_collisionman.set_layer_response(
			collision_layers::common,
			collision_layers::common_trigger,
			collision_response::trigger);

		// level_bounds blocks all! (except itself)
		for (uint32 i = 0u; i < collision_layers::num; ++i)
		{
			const collision_layers::layer lyr = (collision_layers::layer)i;
			m_collisionman.set_layer_response(collision_layers::static_level, i,
				lyr == collision_layers::static_level ? collision_response::ignore : collision_response::block);
		}
	}
	
	// setup players
	m_players[player::one].m_team_idx = team::left;
	m_players[player::one].m_active = true;
	m_players[player::two].m_team_idx = team::right;
	m_players[player::two].m_active = false;
}

void gameman::tick(const tick_context& ctx)
{
	tick_game(ctx);
	tick_systems(ctx);
}

void gameman::tick_game(const tick_context & ctx)
{
	inputman& input = inputman::get();
	const bool devcontrols_enabled = cm_camera_dev_control.enabled();

	// ball logic
	{
		float maxspeed = cm_game_ball_mxsp.get_value();
		float3 velocity = {};

		// if idle or being passed, any runner can intercept and dribble the ball
		if (m_ball.m_state == ball::state::idle || m_ball.m_state == ball::state::passing)
		{
			foreach_collision(m_ball.m_actor, [this](const comp_bounds& other_bounds) {
				const actor_id other_actor = other_bounds.m_owner;
				if (m_actor_to_runner_idx.contains(other_actor))
				{
					// when being passed, the pass source cannot re-pick up the dribble
					if (m_ball.m_state == ball::state::passing
						&& m_actor_to_runner_idx[other_actor] == m_ball.m_pass.m_runner_source)
						return;

					runner_start_dribble(m_actor_to_runner_idx[other_actor]);
				}
			});
		}

		// ball decay
		if (m_ball.m_state != ball::state::charging
			&& m_ball.m_state != ball::state::launching
			&& m_ball.m_state != ball::state::passing)
		{
			m_ball.m_charge.m_value -= ctx.delta_seconds / cm_game_ball_decay_seconds.get_value();
			m_ball.m_charge.m_value = glm::clamp(m_ball.m_charge.m_value, 0.0f, 1.0f);
		}

		switch (m_ball.m_state)
		{
		case ball::state::idle:
		{
			actor(m_ball.m_actor).component<component::type::physics>()
				->set_layer(collision_layers::common_trigger);
		} break;
		case ball::state::dribble:
		{
			actor(m_ball.m_actor).component<component::type::physics>()
				->set_layer(collision_layers::common_ignored);

			const uint32 runner_idx = m_ball.m_dribble.m_runner_idx;
			const runner& rnr = m_runners[runner_idx];
			const float3 rnr_position = actor(rnr.m_actor).get_position();
			actor(m_ball.m_actor).set_position(rnr_position);
		} break;
		case ball::state::passing:
		{
			actor(m_ball.m_actor).component<component::type::physics>()
				->set_layer(collision_layers::common_trigger);

			const uint32 source_rnr = m_ball.m_pass.m_runner_source;
			const uint32 dest_rnr = m_ball.m_pass.m_runner_dest;
			
			const actor_id dst_actor = m_runners[dest_rnr].m_actor;
			const actor_id src_actor = m_runners[source_rnr].m_actor;
			const float3 delta = actor(dst_actor).get_position() - actor(m_ball.m_actor).get_position();
			velocity = glm::normalize(delta) * cm_game_ball_pass_spd.get_value();
		} break;
		case ball::state::charging: 
		{
			actor(m_ball.m_actor).component<component::type::physics>()
				->set_layer(collision_layers::common_ignored);

			const uint32 runner_idx = m_ball.m_charge.m_runner_idx;
			const runner& rnr = m_runners[runner_idx];
			const float3 rnr_position = actor(rnr.m_actor).get_position();
			actor(m_ball.m_actor).set_position(rnr_position);

			m_ball.m_charge.m_seconds_since_start += ctx.delta_seconds;
			m_ball.m_charge.m_value += ctx.delta_seconds / cm_game_ball_maxcharge_seconds.get_value();
			m_ball.m_charge.m_value = glm::clamp(m_ball.m_charge.m_value, 0.0f, 1.0f);

			const bool release_by_button = !input.is_button_down(cm_controls_shoot.value().c_str());
			const bool release_by_maxtime = m_ball.m_charge.m_seconds_since_start > cm_game_ball_maxshoot_seconds.get_value();
			if (release_by_button || release_by_maxtime)
			{
				runner_shoot(m_ball.m_charge.m_runner_idx);
			}
		}break;
		case ball::state::launching:
		{
			actor(m_ball.m_actor).component<component::type::physics>()
				->set_layer(collision_layers::common_trigger);

			const uint32 source_rnr = m_ball.m_pass.m_runner_source;
			const uint32 dest_rnr = m_ball.m_pass.m_runner_dest;
			const float3 delta = m_ball.m_launch.m_point_dest - m_ball.m_launch.m_point_source;
			velocity = glm::normalize(delta)
				* cm_game_ball_shot_spd.get_value();

			// check for collision with goal
			const uint32 enemy_team = runner_get_enemy_team(m_ball.m_launch.m_runner_source);
			const actor_id enemy_goal = m_teams[enemy_team].m_goal_actor;
			foreach_collision(m_ball.m_actor, [this, enemy_goal](const comp_bounds& other_bounds) {
				if (other_bounds.m_owner == enemy_goal)
					reset(reset::game); // GOAL
			});

		}break;
		}

		auto* ball_physx = actor(m_ball.m_actor).component<component::type::physics>();
		if (ball_physx)
		{
			ball_physx->m_maxspeed = maxspeed;
			ball_physx->m_velocity = velocity;
			ball_physx->m_drag_multiplier = 0.0f;
		}
	}

	// AI logic
	if (cm_game_ai.enabled())
	{
		for (auto& cmp_brain : components<component::type::brain>())
		{
			actor_id brain_owner = cmp_brain.m_owner;
			
			// skip if this actor is a runner controlled by player
			if (m_actor_to_runner_idx.contains(brain_owner))
			{
				if (is_runner_controlled_by_player(m_actor_to_runner_idx[brain_owner]))
				{
					continue;
				}
			}

			// decide
			cmp_brain.m_decision_timer -= ctx.delta_seconds;
			if (cmp_brain.m_decision_timer < 0.0f)
			{
				// make decision
				const float random_angle = random_flt(0.0f, 2.0f * 3.14f);
				const float2 random_direction = float2(cos(random_angle), sin(random_angle));

				const float3 brain_position = actor(brain_owner).get_position(space::world);
				const float2 brain_hor_position = float2(brain_position.x, brain_position.z);

				const float distance_from_middle = glm::dot(brain_hor_position, brain_hor_position) * 0.00001f * cm_game_ai_random.get_value();
				cmp_brain.m_decision = glm::normalize(lerp(random_direction, brain_hor_position, distance_from_middle));
				cmp_brain.m_decision.y = -cmp_brain.m_decision.y;
				cmp_brain.m_decision *= 0.01f;

				// time until new decision
				const float decision_time = random_flt(cm_game_ai_min_dtime.get_value(), cm_game_ai_max_dtime.get_value());
				cmp_brain.m_decision_timer = decision_time;
			}

			// act
			auto* cmp_movement = actor(brain_owner).component<component::type::movement>();
			if (cmp_movement)
			{
				cmp_movement->m_input = cmp_brain.m_decision;
			}
		}
	}

	// player logic
	if (!devcontrols_enabled)
	{
		for (uint32 p = 0u; p < player::num; ++p)
		{
			player& ply = m_players[p];
			if (!ply.m_active)
				continue;

			const uint32 team_idx = ply.m_team_idx;
			const uint32 current_local_runner = ply.m_current_local_runner;
			const uint32 runner_idx = get_global_runner_idx(team_idx, current_local_runner);
			runner& rnr = m_runners[runner_idx];
			float2 movement_input = {};
			comp_movement* cmp_movement = actor(rnr.m_actor).component<component::type::movement>();
			if (cmp_movement && !runner_has_ball_charge(runner_idx))
			{
				const float left = input.is_button_down(cm_controls_left.value().c_str());
				const float fwd = input.is_button_down(cm_controls_up.value().c_str());
				const float back = input.is_button_down(cm_controls_down.value().c_str());
				const float right = input.is_button_down(cm_controls_right.value().c_str());
				movement_input = float2(right - left, back - fwd);
				cmp_movement->m_input = movement_input;
			}

			comp_physics* cmp_physics = actor(rnr.m_actor).component<component::type::physics>();
			if (cmp_physics)
			{
				cmp_physics->m_maxspeed = cm_game_movement_mxsp.get_value();
				if (runner_is_pass_dest(runner_idx))
					cmp_physics->m_maxspeed *= 0.1f;
			}

			if (runner_can_pass(runner_idx)
				&& input.is_button_down(cm_controls_pass.value().c_str()))
			{
				// choose the next local_runner
				uint32 next_local_runner = current_local_runner;
				float next_local_runner_distance = FLT_MAX;
				for (uint32 i = 0u; i < runner::num; ++i)
				{
					if (i == current_local_runner)
						continue;

					const runner& nxt_rnr = get_runner_in_team(team_idx, i);
					const runner& cr_rnr = get_runner_in_team(team_idx, current_local_runner);
					float3 delta = actor(nxt_rnr.m_actor).get_position() - actor(cr_rnr.m_actor).get_position();
					delta.y = 0.0f; // squash heigth, it doesn't matter here

					const float input_add = 5.0f * glm::dot(delta, float3(-movement_input.x, 0.0f, movement_input.y));
					const float distance = glm::length(delta) - input_add;
					if (distance < next_local_runner_distance)
					{
						next_local_runner = i;
						next_local_runner_distance = distance;
					}
				}

				const uint32 next_runner = get_global_runner_idx(team_idx, next_local_runner);
				runner_pass(runner_idx, next_runner);

				ply.m_current_local_runner = next_local_runner;
			}

			if (runner_can_charge(runner_idx)
				&& input.is_button_down(cm_controls_shoot.value().c_str()))
			{
				runner_charge(runner_idx);
			}
		}
	}

	// team logic
	for (uint32 t = 0u; t < team::num; ++t)
	{
		m_teams[t].m_action_cooldown_timer -= ctx.delta_seconds;
	}

	// resolve dev-camera controls
	if (devcontrols_enabled)
	{
		// move the camera
		const float left = input.is_button_down(cm_camera_dev_left.value().c_str());
		const float fwd = input.is_button_down(cm_camera_dev_forward.value().c_str());
		const float back = input.is_button_down(cm_camera_dev_back.value().c_str());
		const float right = input.is_button_down(cm_camera_dev_right.value().c_str());
		const float up = input.is_button_down(cm_camera_dev_up.value().c_str());
		const float down = input.is_button_down(cm_camera_dev_down.value().c_str());
		const float sprint_multiplier = 1.0f + input.is_button_down(cm_camera_dev_sprint.value().c_str()) * 2;

		float3 delta_horizontal =
			(m_camera.m_transform.get_right() * (right - left)) +
			(m_camera.m_transform.get_forward() * (fwd - back));
		float3 delta_vertical =
			(float3(0, 1, 0) * (up - down));

		float3 delta_position = delta_horizontal + delta_vertical;
		m_camera.m_velocity += delta_position * ctx.delta_seconds * cm_camera_acceleration.get_value();
		m_camera.m_velocity = clamp_length(m_camera.m_velocity, cm_camera_maxspeed.get_value() * sprint_multiplier);
		m_camera.m_transform.add_position_world(m_camera.m_velocity * ctx.delta_seconds);

		// drag camera velocity
		if (glm::dot(delta_position, delta_position) < 0.0001f)
		{
			m_camera.m_velocity *= 0.001f;
		}

		const float2 mouse_delta = input.get_mouse_delta();

		if (input.is_button_down(inputman::button::rmouse) && glm::dot(mouse_delta, mouse_delta) > 0.001)
		{
			const float2 delta_rotation = float2(mouse_delta.y, mouse_delta.x) * ctx.delta_seconds * cm_camera_sensy.get_value();
			m_camera.m_transform.add_rotation_camera(delta_rotation.y, delta_rotation.x);
		}

		if (input.is_button_down(cm_camera_dev_reset.value().c_str()))
		{
			reset(reset::camera);
		}
	}

	if (input.is_button_down(cm_game_reset.value().c_str()))
	{
		reset(reset::game);
	}
}

void gameman::tick_systems(const tick_context& ctx)
{
	// resolve all movements
	// components::movement -> components::physics
	{
		PIXScopedEvent(0, "system_movements");
		for (auto& cmp_movement : components<component::type::movement>())
		{
			const auto owner = cmp_movement.m_owner;
			const float3 delta_movement = float3(-cmp_movement.m_input.x, 0, cmp_movement.m_input.y) * cm_game_movement_acc.get_value();
			const float any_movement = dot(delta_movement, delta_movement) > 0;

			comp_physics* phys = actor(owner).component<component::type::physics>();
			if (phys)
			{
				phys->m_acceleration += delta_movement;
				phys->m_maxspeed = glm::min(phys->m_maxspeed, cm_game_movement_mxsp.get_value());
				// phys->m_drag_multiplier = 1.0f - any_movement;
			}

			if (any_movement > 0.0f)
			{
				actor(owner)
					.set_rotation(glm::quatLookAt(glm::normalize(-delta_movement), float3(0, 1, 0)), space::world);
			}

			// reset input
			cmp_movement.m_input = float2();
		}
	}

	// physics system
	// - update acceleration/velocity/deltapositions
	// - detect collisions
	// - handle collisions
	if (cm_phx_enabled.enabled())
	{
		PIXScopedEvent(0, "system_physics");

		// components::physics: calculate acceleration, velocity & delta_position
		for (auto& cmp_physics : components<component::type::physics>())
		{
			cmp_physics.m_delta_position = {};

			// resolve velocity
			auto& acceleration = cmp_physics.m_acceleration;
			auto& velocity = cmp_physics.m_velocity;
			float gravity = cmp_physics.m_gravity;
			if (!cm_phx_gravity_enabled.enabled())
				gravity = 0;

			// apply gravity acceleration (if object isn't massless (static))
			if (!cmp_physics.is_static())
				acceleration += float3(0, gravity, 0) / cmp_physics.m_inv_mass;
			
			velocity += acceleration * ctx.delta_seconds;

			// apply air drag to velocity
			const float velocity_sqr = glm::dot(velocity, velocity);
			if (velocity_sqr < 0.00001f) velocity = {};
			else
			{
				const float air_density = cm_game_airdrag.get_value() * cmp_physics.m_drag_multiplier;
				const float area = 1.0f; // dont care
				const float mass = 1.0f; // dont care
				const float drag_deceleration = (air_density * velocity_sqr * area) / (2.0f * mass);
				const float3 dragged_velocity = velocity - (glm::normalize(velocity) * drag_deceleration * ctx.delta_seconds);
				if (glm::dot(dragged_velocity, velocity) >= 0)
				{
					velocity = dragged_velocity;
				}
			}

			// apply max speed to velocity
			if (cmp_physics.m_maxspeed >= 0.0f)
			{
				velocity = clamp_length(velocity, cmp_physics.m_maxspeed);
			}

			cmp_physics.m_delta_position = velocity * ctx.delta_seconds;
			acceleration = {};
		}

		detect_collisions();
		m_collisionman.solve_collisions();

		// components::physics:
		// - apply deltapos
		// - apply collision deltapos + deltavel
		for (auto& cmp_physics : components<component::type::physics>())
		{
			const actor_id owner = cmp_physics.m_owner;
			float3 delta_pos = cmp_physics.m_delta_position;
			float3 delta_vel{};

			// if physics has bounds, check what deltas the collision man has solved
			const bool ignore_collision = cmp_physics.m_is_trigger;
			comp_bounds const* bounds = actor(owner).component<component::type::bounds>();
			if (!ignore_collision && bounds != nullptr)
			{
				m_collisionman.collider_add_solved_deltas(bounds->m_id, delta_pos, delta_vel);
			}

			cmp_physics.m_velocity += delta_vel;
			cmp_physics.m_delta_position = {};

			actor(owner).add_position(delta_pos, strikers::space::world);
		}
	}

	// resolve the transform graph. (we expect everything has finished moving from now)
	{
		PIXScopedEvent(0, "resolve_transforms");
		m_transman.resolve_graph();
	}

	// one more time we detect from fresh all collisions (after the transforms were applied)
	// this is so that next frame, game can query collisions
	if (cm_phx_enabled.enabled())
	{
		detect_collisions();
	}
}

void gameman::detect_collisions()
{
	m_collisionman.reset_colliders();

	// components::bounds: register all world aabbs (including deltaposition)
	for (auto& cmp_bounds : components<component::type::bounds>())
	{
		cmp_bounds.m_world_aabb = {};

		actor_id owner = cmp_bounds.m_owner;

		// get the deltapos of physics component if any
		float3 deltapos_this_frame = {};
		float3 velocity_this_frame = {};
		float inverse_mass = 0.0f;
		uint32 layer = 0u;
		uint32 mask = (uint32)-1;
		if (comp_physics const* physics = actor(owner).component<component::type::physics>())
		{
			deltapos_this_frame = physics->m_delta_position;
			velocity_this_frame = physics->m_velocity;
			inverse_mass = physics->m_inv_mass;
			layer = physics->m_collision_layer;
			mask = physics->m_collision_mask;
		}

		const box& box = cmp_bounds.m_box;
		const float3 corners[8] = {
			{ box.abs_min().x, box.abs_min().y, box.abs_min().z },
			{ box.abs_max().x, box.abs_min().y, box.abs_min().z },
			{ box.abs_max().x, box.abs_max().y, box.abs_min().z },
			{ box.abs_min().x, box.abs_max().y, box.abs_min().z },
			{ box.abs_min().x, box.abs_min().y, box.abs_max().z },
			{ box.abs_max().x, box.abs_min().y, box.abs_max().z },
			{ box.abs_max().x, box.abs_max().y, box.abs_max().z },
			{ box.abs_min().x, box.abs_max().y, box.abs_max().z },
		};

		const auto& transform = actor(cmp_bounds.m_owner).get_transform(space::world);
		cmp_bounds.m_world_aabb.m_position = transform.get_position() + deltapos_this_frame;
		for (uint32 i = 0u; i < 8; ++i)
		{
			cmp_bounds.m_world_aabb.grow_to_fit(transform.m_matrix * float4(corners[i], 1));
		}

		m_collisionman.write_collider(cmp_bounds.m_id, collisionman::collider_builder()
			.aabb(cmp_bounds.m_world_aabb)
			.inv_mass(inverse_mass)
			.velocity(velocity_this_frame)
			.set_layer(layer)
			.dbg_tag(actor(owner).get_name())
		);
	}
	m_collisionman.detect_collisions();
}

void gameman::build_renderscene(contentman& cman, renderscene& scene)
{
	PIXScopedEvent(0, "build_renderscene");

	// setup camera
	{
		scene.m_camera.m_near = 0.001f;
		scene.m_camera.m_far = 1000.0f;
		scene.m_camera.m_fov = 1;
		scene.m_camera.m_transform = m_camera.m_transform;

		// use camera ASSET to guide camera settings
		camera_asset const* camera_asset = cman.find_camera(m_camera.m_asset_id);
		if (camera_asset && false)
		{
			scene.m_camera.m_near = camera_asset->m_clip_near;
			scene.m_camera.m_far = camera_asset->m_clip_far;

			// we use vertical
			const float ar = 1280.0f / 720.f;
			const float vert_fov = 2 * glm::atan(glm::tan(camera_asset->m_fov_horizontal * 0.5f) / ar);
			scene.m_camera.m_fov = 1.0f / vert_fov;
		}
	}
	
	// setup directional light
	{
		scene.m_light.m_color = m_light.m_color;
		scene.m_light.m_transform = m_light.m_transform;
		scene.m_light.m_frustrum = box::unit(cm_gfx_shadowfrustrum_size.get_value());
		scene.m_light.m_shadowmap_resolution = uint2(1, 1) * cm_gfx_shadowmap_size.get_value<uint32>();
	}
	
	// draw meshes
	for (const auto& cmp_render : components<component::type::render>())
	{
		if (!actor(cmp_render.m_owner).is_active())
			continue;

		const auto& transform = actor(cmp_render.m_owner).get_transform(space::world);

		// draw the pawn mesh instance
		const mesh_id meshid = cmp_render.m_mesh;
		mesh_asset const* mesh = cman.find_mesh(meshid);
		if (!mesh)
		{
			continue;
		}

		auto& mesh_instance = scene.add_mesh_instance(meshid, shader::slot::shaded);
		mesh_instance.m_transform = transform;
		mesh_instance.apply_material(cman, cman.get_mesh_material_id(meshid));

		const skel_id skelid = mesh->m_skeleton_id;
		vector<anim_id> compatible_animations{};
		if (cman.find_compatible_animations(skelid, compatible_animations) && !compatible_animations.empty())
		{
			mesh_instance.m_animation = compatible_animations[0];
		}

		if (skelid != k_id_invalid)
		{
			mesh_instance.m_skeleton = skelid;
		}
	}

	// draw quads
	for (uint32 r = 0u; r < runner::num * team::num; ++r) 
	{
		if (!is_runner_controlled_by_player(r))
			continue;

		auto& quad = scene.add_quad_instance();

		image_id font_image{};
		rect font_uv_rect{};
		if (fontman::get().parse_font(cman, DF_MAIN_FONT, '1', font_image, font_uv_rect))
		{
			quad.m_rect_uv = font_uv_rect;
			quad.m_image = font_image;
		}
		quad.m_color = float4(1, 1, 1, 1);
		quad.m_transform = transform::identity();

		const float3 forward = quad.m_transform.get_position() - m_camera.m_transform.get_position();
		quad.m_transform.look_twd(forward);
		quad.m_transform.set_position(actor(m_runners[r].m_actor).get_position() + float3(0,2,0));
	}

	// draw debug lines
	if (cm_draw_dbg.enabled())
	{
		renderscene::line_builder lines{ scene };
		
		// bounds
		if (cm_draw_dbg_bounds.enabled())
		for (const auto& cmp_bounds : components<component::type::bounds>())
		{
			const auto& transform = actor(cmp_bounds.m_owner).get_transform(space::world);

			color bounds_color = colors::green();
			
			if (m_collisionman.num_collisions(cmp_bounds.m_id))
				bounds_color = colors::red();

			if (auto* phys = actor(cmp_bounds.m_owner).component<component::type::physics>())
			{
				if (phys->is_static())
					bounds_color = colors::purple();
			}

			lines.add_box(transform::identity(), cmp_bounds.m_world_aabb, bounds_color);
		}

		// transforms
		if (cm_draw_dbg_transforms.enabled())
		{
			lines.add_transform(transform::identity());
			for (const auto& trans_id : m_transman.get_transforms())
			{
				lines.add_transform(m_transman.get_transform(trans_id, space::world));
			}
		}

		// ball
		if (cm_draw_dbg_ball.enabled())
		{
			const float size = lerp(2.0f, 0.5f, m_ball.m_charge.m_value);
			lines.add_sphere(transform::identity(), float4(actor(m_ball.m_actor).get_position(), size), colors::white());

			switch (m_ball.m_state)
			{
			case ball::state::passing:
			{
				const runner& dst_rnr = m_runners[m_ball.m_pass.m_runner_dest];
				lines.add_line(actor(dst_rnr.m_actor).get_position(), actor(m_ball.m_actor).get_position(), colors::blue());
			}break;
			case ball::state::launching:
				lines.add_line(m_ball.m_launch.m_point_source, m_ball.m_launch.m_point_dest, colors::red());
				break;
			}
		}
	}
	
	// draw ui
	for (const auto& cmp_ui_render : components<component::type::renderui>())
	{
		auto& instance = scene.add_ui_instance();
		instance.m_image;
		instance.m_box;
	}
}

const string& gameman::actor_scope::get_name() const {
	return m_owner.get_actor(m_id).m_name;
}
const bool gameman::actor_scope::is_active() const {
	return m_owner.get_actor(m_id).m_active;
}
const trans_id gameman::actor_scope::get_transform_id() const {
	return m_owner.get_actor(m_id).m_transform_id;
}
const transform gameman::actor_scope::get_transform(space spc) const {
	trans_id trid = get_transform_id();
	return m_owner.m_transman.get_transform(trid, spc);
}
const float3 gameman::actor_scope::get_position(space spc) const {
	trans_id trid = get_transform_id();
	return m_owner.m_transman.get_position(trid, spc);
}
const rotation gameman::actor_scope::get_rotation(space spc) const {
	trans_id trid = get_transform_id();
	return m_owner.m_transman.get_rotation(trid, spc);
}
const float3 gameman::actor_scope::get_scale(space spc) const {
	trans_id trid = get_transform_id();
	return m_owner.m_transman.get_scale(trid, spc);
}
const gameman::actor_scope& gameman::actor_scope::set_name(const string& name) const {
	m_owner.get_actor(m_id).m_name = name; return *this;
}
const gameman::actor_scope& gameman::actor_scope::set_active(const bool active) const {
	m_owner.get_actor(m_id).m_active = active; return *this;
}
const gameman::actor_scope& gameman::actor_scope::set_transform(const transform& trans, space spc) const {
	trans_id trid = get_transform_id();
	m_owner.m_transman.set_transform(trid, trans, spc);
	return *this;
}
const gameman::actor_scope& gameman::actor_scope::set_position(const float3& position, space spc) const {
	trans_id trid = get_transform_id();
	m_owner.m_transman.set_position(trid, position, spc);
	return *this;
}
const gameman::actor_scope& gameman::actor_scope::set_rotation(const rotation& rotation, space spc) const {
	trans_id trid = get_transform_id();
	m_owner.m_transman.set_rotation(trid, rotation, spc);
	return *this;
}
const gameman::actor_scope& gameman::actor_scope::set_scale(const float3& scale, space spc) const {
	trans_id trid = get_transform_id();
	m_owner.m_transman.set_scale(trid, scale, spc);
	return *this;
}
const gameman::actor_scope& gameman::actor_scope::add_position(const float3& delta, space spc) const {
	trans_id trid = get_transform_id();
	m_owner.m_transman.add_position(trid, delta, spc);
	return *this;
}
const gameman::actor_scope& gameman::actor_scope::add_rotation(const rotation& delta, space spc) const {
	trans_id trid = get_transform_id();
	m_owner.m_transman.add_rotation(trid, delta, spc);
	return *this;
}
const gameman::actor_scope& gameman::actor_scope::multiply_scale(const float3& multiplier, space spc) const {
	trans_id trid = get_transform_id();
	m_owner.m_transman.mult_scale(trid, multiplier, spc);
	return *this;
}
}

