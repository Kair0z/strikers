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
command cm_game_camera_speed("game_camera_speed", "1");
command cm_game_camera_ball_offset_x("game_camera_ball_offset_x", "0");
command cm_game_camera_ball_offset_z("game_camera_ball_offset_z", "23");
command cm_draw_dbg("draw_dbg", "0");
command cm_draw_dbg_bounds("draw_dbg_bounds", "0");
command cm_draw_dbg_transforms("draw_dbg_transforms", "0");
command cm_draw_dbg_axis("draw_dbg_axis", "0");
command cm_draw_dbg_ball("draw_dbg_ball", "0");

// animation
command cm_anim_speed("cm_anim_speed", "1");

command cm_gfx_shadowmap_size("gfx_shadowmap_size", "1024");
command cm_gfx_shadowfrustrum_size("gfx_shadowfrustrum_size", "50");

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
		actor(m_camera.m_actor).reset_transform();
		m_camera.m_velocity = {};
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
				const uint32 runner_idx = get_runner_idx(t, i);
				const string varname = format("{}.{}", team::get_name((team::slot)t), runner::get_name((runner::slot)i));
				for (uint32 c = 0u; c < character_runner::num; ++c)
				{
					if (strstr(character_runner::get_name(c), setup_settings[varname].c_str()))
					{
						m_runners[runner_idx].m_character = (character_runner::type)c;
					}
				}
			}
		}
	}

	// fbx scene -> actors & components
	{
		const string scene_filepath = string(k_content_folder) + "scene.fbx";
		const scene_id scid = contentman::make_scene_id(scene_filepath);
		const scene_asset& scene = *cman.find_typed_asset<asset_type::scene>(scid).claim();

		// struct that keeps track of nodes that have important traits!
		struct important_info_builder final
		{
			enum flags
			{
				ball = (1 << 0),
				goalie = (1 << 1),
				keypoint = (1 << 2),
				runner = (1 << 3),
				light = (1 << 4),
				goal = (1 << 5),
				camera = (1 << 6),
				goalzone = (1 << 7),
				num_flags = 8
			};
			uint32 m_flags;
			uint32 m_char_runner;
			uint32 m_char_goalie;

			important_info_builder& as_runner(uint32 chr) { 
				if (chr != character_runner::num) 
					m_flags |= runner; 
				m_char_runner = chr; 
				return *this; 
			}
			important_info_builder& as_goalie(uint32 chr) { 
				if (chr != character_goalie::num) 
					m_flags |= goalie; 
				m_char_goalie = chr;
				return *this; 
			}
			important_info_builder& name(const char* name)
			{
				if (strstr(name, "kp_")) m_flags |= keypoint;
				if (strstr(name, "camera")) m_flags |= camera;
				if (strstr(name, "light")) m_flags |= light;
				if (strstr(name, "ball")) m_flags |= ball;
				if (strstr(name, "goalzone")) m_flags |= goalzone;
				return *this;
			}
		};

		// we're also keeping track of all important objects
		uint32 important_counters[important_info_builder::num_flags]{};

		umap<uint32, actor_id> node_id_to_actor_id{};
		auto instantiate_actor = [this, &node_id_to_actor_id, &scene, &cman, &important_counters]
		(uint32 node_id, const actor_id parent_actor = k_actor_invalid, const important_info_builder& builder = {}) -> actor_id
		{
			const scene_asset::node& current_node = scene.m_graph.get(node_id).data();

			// create & register the actor
			const actor_id main_actor = create_actor(parent_actor);
			node_id_to_actor_id[node_id] = main_actor;
			
			// each actor has a transform & name
			actor(main_actor)
				.set_transform(current_node.m_scene_transform, space::world)
				.set_name(current_node.m_name);

			const auto& new_actor_transform = actor(main_actor).get_transform(space::world);

			// if this object is the area pitch, capture the bounds of the mesh
			if (strstr(current_node.m_name.c_str(), "area_pitch"))
			{
				if (current_node.m_meshes.size() > 0)
				{
					if (auto* mesh = cman.find_mesh(current_node.m_meshes[0]))
					{
						m_field.m_pitch_bounds = box::transformed_aabb(mesh->m_bounds_box, new_actor_transform);
						return main_actor;
					}
				}
			}

			if (builder.m_flags & important_info_builder::keypoint)
			{
				for (uint32 t = 0u; t < team::num; ++t)
				for (uint32 r = 0u; r < runner::num; ++r)
				{
					const string keypoint_name = format("kp_{}_{}", team::get_name(t), runner::get_name(r));
					if (strstr(current_node.m_name.c_str(), keypoint_name.c_str()))
					{
						m_runners[get_runner_idx(t, r)].m_start_transform = new_actor_transform;
					}
				}
				for (uint32 t = 0u; t < team::num; ++t)
				{
					const string keypoint_name = format("kp_{}_{}", team::get_name(t), "goalie");
					if (strstr(current_node.m_name.c_str(), keypoint_name.c_str()))
					{
						m_goalies[t].m_start_transform = new_actor_transform;
					}
				}
				if (strstr(current_node.m_name.c_str(), "middle"))
				{
					m_field.m_midpoint_actor = main_actor;
				}
			}
			if (builder.m_flags & important_info_builder::goalie)
			{
				actor(main_actor).add_component<component::type::physics>();
				actor(main_actor).component<component::type::physics>()
					->set_mass(1.0f)
					.set_layer(collision_layers::common);
			}
			if (builder.m_flags & important_info_builder::ball)
			{
				m_ball.m_actor = main_actor;
				actor(main_actor).add_component<component::type::physics>();
				actor(main_actor).component<component::type::physics>()
					->set_mass(1.0f)
					.set_layer(collision_layers::common);
			}
			if (builder.m_flags& important_info_builder::runner)
			{
				actor(main_actor).add_component<component::type::physics>();
				actor(main_actor).add_component<component::type::movement>();
				actor(main_actor).component<component::type::physics>()
					->set_mass(1.0f)
					.set_layer(collision_layers::common);
			}
			if (builder.m_flags & important_info_builder::light)
			{
				m_lights.m_actor_directional = main_actor;
				actor(main_actor).add_component<component::type::light>();
				
				if (scene.m_node_to_light_idx.contains(node_id))
				{
					light& target_light = actor(main_actor).component<component::type::light>()->m_light;
					const light& scene_light = scene.m_lights[scene.m_node_to_light_idx.at(node_id)];
					target_light = scene_light;
				}
			}
			if (builder.m_flags & important_info_builder::camera)
			{
				m_camera.m_actor = main_actor;
				m_camera.m_initial_right = new_actor_transform.get_right();
				m_camera.m_velocity = {};

				if (scene.m_node_to_camera_idx.contains(node_id))
				{
					const camera& scene_camera = scene.m_cameras[scene.m_node_to_camera_idx.at(node_id)];
					m_camera.m_camera = scene_camera;
				}
			}
			if (builder.m_flags & important_info_builder::goalzone)
			{
				for (uint32 t = 0u; t < team::num; ++t)
				{
					const string goalzone_name = format("goalzone_{}", team::get_name(t));
					if (strstr(current_node.m_name.c_str(), goalzone_name.c_str()))
					{
						m_teams[t].m_goal_actor = main_actor;
					}
				}
			}

			// each submesh of this node becomes a child actor (except if tagged 'collider')
			for (uint32 i = 0u; i < current_node.m_meshes.size(); ++i)
			{
				// get the mesh & material, if material is tagged 'collider' we skip the mesh, and add the 'bounds' component to the root actor
				const mesh_id meshid = current_node.m_meshes[i];
				mesh_asset const* mesh = nullptr; material_asset const* material = nullptr;
				if (cman.find_mesh_and_material(meshid, mesh, material))
				{
					if (strstr(material->m_name.c_str(), "collider"))
					{
						actor(main_actor).add_component<component::type::bounds>();
						auto* bounds = actor(main_actor).component<component::type::bounds>();
						bounds->m_box = mesh->m_bounds_box;
						bounds->m_sphere = mesh->m_bounds_sphere;
						// mesh_transform.m_name = node.m_name + ": collider";
						continue;
					}
				}

				actor_id mesh_actor = create_actor(main_actor);
				actor(mesh_actor).set_transform(transform::identity(), space::local);

				// configure the render component
				actor(mesh_actor).add_component<component::type::render>();
				auto* render = actor(mesh_actor).component<component::type::render>();
				render->m_mesh = meshid;

				// configure the skeleton & animations (if the mesh has those)
				const skel_id skeleton_id = mesh->m_skeleton_id;
				if (skeleton_id != k_id_invalid)
				{
					actor(mesh_actor).add_component<component::type::skeleton>();
					auto* skeleton = actor(mesh_actor).component<component::type::skeleton>();
					skeleton->m_skeleton = skeleton_id;

					vector<anim_id> compatible_animations{};
					if (cman.find_compatible_animations(skeleton_id, compatible_animations))
					{
						actor(mesh_actor).add_component<component::type::animation>();
						auto* animation = actor(mesh_actor).component<component::type::animation>();
						animation->m_animation = compatible_animations[0];
					}
				}
			}

			// count the instantiated important objects
			for (uint32 i = 0u; i < important_info_builder::num_flags; ++i)
			{
				important_counters[i] += (builder.m_flags & (1 << i)) != 0;
			}

			return main_actor;
		};

		// traverse each node in the scene, and parse the relevant actors
		scene.m_graph.traverse([cman, this, &scene, &node_id_to_actor_id, &instantiate_actor](uint32 node_id, uint32 parent_id)
		{
			const bool valid_parent = scene.m_graph.is_valid(parent_id) && node_id_to_actor_id.contains(parent_id);
			const actor_id parent_actor = valid_parent ? node_id_to_actor_id[parent_id] : k_actor_invalid;

			const scene_asset::node& parent_node = scene.m_graph.get(parent_id).data();
			const scene_asset::node& current_node = scene.m_graph.get(node_id).data();

			// check if this node is a character (by name)
			character_runner::type character_runner = character_runner::num;
			character_goalie::type character_goalie = character_goalie::num;
			for (uint32 c = 0u; c < character_runner::num; ++c)
			{
				if (strstr(current_node.m_name.c_str(), character_runner::get_name((character_runner::type)c)))
				{
					character_runner = (character_runner::type)c;
				}
			}
			for (uint32 c = 0u; c < character_goalie::num; ++c)
			{
				if (strstr(current_node.m_name.c_str(), character_goalie::get_name((character_goalie::type)c)))
				{
					character_goalie = (character_goalie::type)c;
				}
			}

			// if character_runner, instantiate for each runner with that character (and return)
			if (character_runner != character_runner::num)
			{
				for (uint32 t = 0u; t < team::num; ++t)
					for (uint32 r = 0u; r < runner::num; ++r)
					{
						runner& rnr = m_runners[get_runner_idx(t, r)];
						if (rnr.m_character == character_runner && rnr.m_actor == k_actor_invalid)
						{
							rnr.m_actor = instantiate_actor(node_id, parent_actor, important_info_builder()
								.name(current_node.m_name.c_str())
								.as_runner(character_runner)
							);
						}
					}
				return;
			}

			// if character_goalie, instantiate for each goalie with that character (and return)
			if (character_goalie != character_goalie::num)
			{
				for (uint32 t = 0u; t < team::num; ++t)
				{
					goalie& goalie = m_goalies[t];
					if (goalie.m_character == character_goalie && goalie.m_actor == k_actor_invalid)
					{
						goalie.m_actor = instantiate_actor(node_id, parent_actor, important_info_builder()
							.name(current_node.m_name.c_str())
							.as_goalie(character_goalie)
						);
					}
				}
				return;
			}

			instantiate_actor(node_id, parent_actor, important_info_builder()
				.name(current_node.m_name.c_str())
			);
		});

		// set the runner & goalie transforms to their designated keypoint
		for (uint32 r = 0u; r < team::num * runner::num; ++r)
		{
			const runner& rnr = m_runners[r];
			actor(rnr.m_actor)
				.set_position(rnr.m_start_transform.get_position(), space::world)
				.set_rotation(rnr.m_start_transform.get_rotation(), space::world);

			// register the actor (and its children) to the runner collection
			m_actor_to_runner_idx[rnr.m_actor] = r;
			actor(rnr.m_actor).traverse_children([this, r](actor_id child){
				m_actor_to_runner_idx[child] = r;
			});
		}
		for (uint32 t = 0u; t < team::num; ++t)
		{
			const goalie& goal = m_goalies[t];
			m_actor_to_goalie_idx[goal.m_actor] = t;
			actor(goal.m_actor)
				.set_position(goal.m_start_transform.get_position(), space::world)
				.set_rotation(goal.m_start_transform.get_rotation(), space::world);
		}

		// resolve & save as 'reset' position
		m_transman.save_as_reset();
		
		// game validity checks
		{
			logman::log("-- checking for necessary game parts");
			
			uint32 num_valid_goals = 0;
			uint32 num_valid_keepers = 0;
			for (uint32 t = 0; t < team::num; ++t)
				num_valid_goals += (m_teams->m_goal_actor != k_actor_invalid),
				num_valid_keepers += (m_goalies[t].m_actor != k_actor_invalid);

			if (num_valid_goals < 2)			logman::color(logcolor::red), logman::log("- check failed! not enough valid goals!");
			else if (num_valid_keepers < 2)		logman::color(logcolor::red), logman::log("- check failed! not enough valid keepers!");
			else								logman::color(logcolor::green), logman::log("-- all checks passed! game is ready!");
			logman::prev_color();
		}
	}

	// setup collision layers
	{
		// common x block x common
		m_collisionman.set_layer_response(
			collision_layers::common, collision_layers::common, collision_response::block);
		
		// common x ignore x common_ignored
		m_collisionman.set_layer_response(
			collision_layers::common, collision_layers::common_ignored, collision_response::ignore);

		// common x trigger x common_trigger
		m_collisionman.set_layer_response(
			collision_layers::common, collision_layers::common_trigger, collision_response::trigger);

		// level_bounds blocks everything! (except itself)
		for (uint32 i = 0u; i < collision_layers::num; ++i)
		{
			const collision_layers::layer lyr = (collision_layers::layer)i;
			m_collisionman.set_layer_response(collision_layers::static_level, i,
				lyr == collision_layers::static_level ? collision_response::ignore : collision_response::block);
		}
	}
	
	// setup players
	for (uint32 i = 0u; i < player::num; ++i)
	{
		m_players[i].m_active = true;
		m_players[i].m_team_idx = (i / 2) % team::num;
		m_players[i].m_current_local_runner = i;
	}
}

void gameman::tick(const tick_context& ctx)
{
	// resolve dev-camera controls
	inputman& input = inputman::get();
	const bool devcontrols_enabled = cm_camera_dev_control.enabled();
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

		const auto transform_world = actor(m_camera.m_actor).get_transform();
		const float3 delta_horizontal = (transform_world.get_right() * (right - left)) + (transform_world.get_forward() * (back - fwd));
		const float3 delta_vertical = (float3(0, 1, 0) * (up - down));
		const float3 delta_position = delta_horizontal + delta_vertical;
		m_camera.m_movement_input = delta_position;

		// orient
		const float2 mouse_delta = input.get_mouse_delta();
		if (input.is_button_down(inputman::button::rmouse) && glm::dot(mouse_delta, mouse_delta) > 0.001)
		{
			m_camera.m_orient_input = mouse_delta;
		}
		else m_camera.m_orient_input = {};

		if (input.is_button_down(cm_camera_dev_reset.value().c_str()))
		{
			reset(reset::camera);
		}
	}

	// only tick game if devcontrol is off!
	if (!devcontrols_enabled)
	{
		tick_game(ctx);
	}

	// always tick the systems
	tick_systems(ctx);

	if (input.is_button_down(cm_game_reset.value().c_str()))
	{
		reset(reset::game);
	}
}

void gameman::tick_game(const tick_context & ctx)
{
	inputman& input = inputman::get();

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
			// ignore common collisions
			actor(m_ball.m_actor).component<component::type::physics>()
				->set_layer(collision_layers::common_ignored);

			// clamp to owner position
			const uint32 runner_idx = m_ball.m_dribble.m_runner_idx;
			const runner& rnr = m_runners[runner_idx];
			const float3 rnr_position = actor(rnr.m_actor).get_position();
			actor(m_ball.m_actor).set_position(rnr_position);

		} break;
		case ball::state::passing:
		{
			// re-enable trigger collisions
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

	// camera logic
	{
		const float3 field_min = m_field.m_pitch_bounds.abs_min();
		const float3 field_max = m_field.m_pitch_bounds.abs_max();
		const float3 ball_position = actor(m_ball.m_actor).get_position();

		float3 camera_position = actor(m_camera.m_actor).get_position();
		camera_position.x += cm_game_camera_ball_offset_x.get_value();
		camera_position.z -= cm_game_camera_ball_offset_z.get_value();

		const float2 ball_normalized_position = m_field.normalize_position(ball_position);
		const float2 camera_normalized_position = m_field.normalize_position(camera_position);
		const float2 clamped_ball_position = glm::clamp(ball_normalized_position, float2(0.3f, 0.4f), float2(0.7f, 0.6f));
		const float2 camera_movement = clamped_ball_position - camera_normalized_position;

		if (glm::dot(camera_movement, camera_movement) > 0.001)
			m_camera.m_movement_input += float3(camera_movement.x, 0.0f, camera_movement.y);
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
	for (uint32 p = 0u; p < player::num; ++p)
	{
		player& ply = m_players[p];
		if (!ply.m_active)
		{
			continue;
		}

		const uint32 team_idx = ply.m_team_idx;
		const uint32 current_local_runner = ply.m_current_local_runner;
		const uint32 runner_idx = get_runner_idx(team_idx, current_local_runner);
		const runner& rnr = m_runners[runner_idx];

		// figure out the movement input
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

		// adjust the maxspeed of the physics component
		comp_physics* cmp_physics = actor(rnr.m_actor).component<component::type::physics>();
		if (cmp_physics)
		{
			cmp_physics->m_maxspeed = cm_game_movement_mxsp.get_value();
			if (runner_is_pass_dest(runner_idx))
				cmp_physics->m_maxspeed *= 0.1f;
		}

		// process 'pass' input
		if (runner_can_pass(runner_idx) && input.is_button_down(cm_controls_pass.value().c_str()))
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

				const float3 current_to_next = actor(nxt_rnr.m_actor).get_position() - actor(cr_rnr.m_actor).get_position();
				const float2 current_to_next_hor = float2(current_to_next.x, current_to_next.z);
				const float2 input_delta = movement_input * 5.0f;
				const float distance = glm::length(current_to_next_hor - input_delta);
				if (distance < next_local_runner_distance)
				{
					next_local_runner = i;
					next_local_runner_distance = distance;
				}
			}

			const uint32 next_runner = get_runner_idx(team_idx, next_local_runner);
			runner_pass(runner_idx, next_runner);

			ply.m_current_local_runner = next_local_runner;
		}

		// process 'charge' input
		if (runner_can_charge(runner_idx) && input.is_button_down(cm_controls_shoot.value().c_str()))
		{
			runner_charge(runner_idx);
		}
	}

	// team logic (cooldowns)
	for (uint32 t = 0u; t < team::num; ++t)
	{
		m_teams[t].m_action_cooldown_timer -= ctx.delta_seconds;
	}
}

void gameman::tick_systems(const tick_context& ctx)
{
	// camera system
	{
		auto transform_world = actor(m_camera.m_actor).get_transform();
		m_camera.m_velocity += m_camera.m_movement_input * ctx.delta_seconds * cm_camera_acceleration.get_value();
		m_camera.m_velocity = clamp_length(m_camera.m_velocity, cm_camera_maxspeed.get_value() * (1 + m_camera.m_sprint));
		transform_world.add_position(m_camera.m_velocity * ctx.delta_seconds);

		const float2 delta_rotation = float2(-m_camera.m_orient_input.y, -m_camera.m_orient_input.x) * ctx.delta_seconds * cm_camera_sensy.get_value();
		transform_world.add_rotation_camera(delta_rotation.y, delta_rotation.x);

		// drag camera velocity
		if (glm::dot(m_camera.m_movement_input, m_camera.m_movement_input) < 0.0001f)
		{
			m_camera.m_velocity *= 0.001f;
		}

		// finally set the transform
		actor(m_camera.m_actor).set_transform(transform_world);

		// reset input
		m_camera.m_movement_input = {};
	}

	// resolve all movements
	// components::movement -> components::physics
	{
		PIXScopedEvent(0, "system_movements");
		for (auto& cmp_movement : components<component::type::movement>())
		{
			const auto owner = cmp_movement.m_owner;
			const float3 delta_movement = float3(cmp_movement.m_input.x, 0, cmp_movement.m_input.y) * cm_game_movement_acc.get_value();
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

	// animations system
	{
		PIXScopedEvent(0, "system_animations");
		for (auto& animation : components<component::type::animation>())
		{
			animation.m_time += ctx.delta_seconds;
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
	m_collisionman.reset_collisions();

	// register each bounds component's world aabb into the collisionman
	for (auto& cmp_bounds : components<component::type::bounds>())
	{
		const actor_id owner = cmp_bounds.m_owner;

		// if we have a physics component, grab that info
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

		// calculate the world aabb for this bounds
		const auto& transform = actor(cmp_bounds.m_owner).get_transform(space::world);
		cmp_bounds.m_world_aabb = box::transformed_aabb(cmp_bounds.m_box, transform, transform.get_position() + deltapos_this_frame);

		// write the collider into the collisionman
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

	// update camera
	scene.m_camera = m_camera.m_camera;
	scene.m_camera_transform = actor(m_camera.m_actor).get_transform();
	
	// setup directional light
	{
		comp_light* light_comp = actor(m_lights.m_actor_directional).component<component::type::light>();
		if (light_comp)
		{
			scene.m_light.m_color = float4(light_comp->m_light.m_color_diffuse, 1);
			scene.m_light.m_transform = actor(m_lights.m_actor_directional).get_transform();
			scene.m_light.m_frustrum = box::unit(cm_gfx_shadowfrustrum_size.get_value());
			scene.m_light.m_shadowmap_resolution = uint2(1, 1) * cm_gfx_shadowmap_size.get_value<uint32>();
		}
	}
	
	// draw meshes
	for (const auto& cmp_render : components<component::type::render>())
	{
		const actor_id parent_actor = actor(cmp_render.m_owner).get_parent();

		if (!actor(parent_actor).is_active())
			continue;
		
		const mesh_id meshid = cmp_render.m_mesh;
		auto& mesh_instance = scene.add_mesh_instance(meshid, modelshader::slot::shaded);
		const auto& transform = actor(cmp_render.m_owner).get_transform(space::world);
		mesh_instance.m_transform = transform;
		mesh_instance.apply_material(cman, cman.get_mesh_material_id(meshid));

		// draw outline
		const bool is_runner = m_actor_to_runner_idx.contains(parent_actor);
		if (is_runner && is_runner_controlled_by_player(m_actor_to_runner_idx[parent_actor]))
		{
			mesh_instance.m_bitflags = render_bitflags::outline;
		}

		// attach skeleton
		auto* skel_comp = actor(cmp_render.m_owner).component<component::type::skeleton>();
		if (skel_comp && skel_comp->m_skeleton != k_id_invalid)
		{
			mesh_instance.m_skeleton = skel_comp->m_skeleton;
		}

		// attach animation
		auto* anim_comp = actor(cmp_render.m_owner).component<component::type::animation>();
		if (anim_comp && anim_comp->m_animation != k_id_invalid)
		{
			const anim_id animid = anim_comp->m_animation;
			mesh_instance.m_animation = anim_comp->m_animation;
			mesh_instance.m_time = anim_comp->m_time * cm_anim_speed.get_value();
		}
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

			lines.add_box(transform::identity(), m_field.m_pitch_bounds, colors::green());
		}

		// transforms
		if (cm_draw_dbg_axis.enabled())
		{
			lines.add_transform(transform::identity());
		}

		if (cm_draw_dbg_transforms.enabled())
		{
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

#pragma region ugly_actor_interface
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
const gameman::actor_scope& gameman::actor_scope::add_position(const float3& delta, space spc = space::world) const {
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
const gameman::actor_scope& gameman::actor_scope::reset_transform() const
{
	m_owner.m_transman.reset(get_transform_id());
	return *this;
}
#pragma endregion
}

