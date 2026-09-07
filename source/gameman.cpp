#include "gameman.h"
#include "renderman.h"
#include "contentman.h"
#include "inputman.h"
#include "commandman.h"

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

command cm_game_reset("game_reset", "r");
command cm_game_physics_enable("game_physics_enable", "1");
command cm_game_gravity_enable("game_gravity_enable", "0");
command cm_game_airdrag("game_airdrag", "1.225");
command cm_game_movement_acc("game_movement_acc", "2");
command cm_game_movement_mxsp("game_movement_mxsp", "-1");
command cm_game_ai("game_ai", "1");
command cm_game_ai_min_dtime("game_ai_min_dtime", "1");
command cm_game_ai_max_dtime("game_ai_max_dtime", "1");
command cm_game_ai_random("game_ai_random", "1");

command cm_game_ctrl0_pass("game_ctrl0_pass", "space");
command cm_game_ctrl0_charge("game_ctrl0_charge", "shift");

command cm_draw_debug("draw_debug", "1");
command cm_log_transforms("log_transforms", "1");

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

	// this is all kinda temp setup stuff..
	static const uint32 k_team_mario = 0;
	static const uint32 k_team_luigi = 1;
	static uint32 s_running_num_toads = 0;

	// translate the scene hierarchy to 'game transform hierarchy'
	umap<uint32, actor_id> node_id_to_actor_id{};
	scene.m_graph.traverse([cman, this, &scene, &node_id_to_actor_id](uint32 node_id, uint32 parent_id)
	{
		const bool valid_parent = scene.m_graph.is_valid(parent_id) && node_id_to_actor_id.contains(parent_id);
		const actor_id parent_actor = valid_parent ? node_id_to_actor_id[parent_id] : k_actor_invalid;
		const transform parent_world_transform = valid_parent ? m_transman.get_transform(m_actors[parent_actor].m_transform_id, space::world) : transform::identity();
		const transform parent_inv_world = glm::inverse(parent_world_transform.m_matrix);

		// for each array of meshes, add a new actor as root...
		const scene_asset::node& parent = scene.m_graph.get(parent_id).data();
		const scene_asset::node& node = scene.m_graph.get(node_id).data();

		actor_id root_actor = create_actor(parent_actor);
		m_transman.set_transform(m_actors[root_actor].m_transform_id, node.m_scene_transform, space::world);
		node_id_to_actor_id[node_id] = root_actor;
		if (strstr(node.m_name.c_str(), "mario"))
		{
			const uint32 mario_runner_idx = get_runner_idx(k_team_mario, 0);;
			m_players[k_team_mario].m_current_local_runner = mario_runner_idx;
			runner& mario_runner = get_runner(mario_runner_idx);
			mario_runner.m_actor = root_actor;
			mario_runner.m_flags = runner::flags::mario;
			ball_start_dribble(mario_runner_idx);

			auto& movement = add_component<component::type::movement>(root_actor);
			auto& brain = add_component<component::type::brain>(root_actor);
			auto& physics = add_component<component::type::physics>(root_actor);
		}
		if (strstr(node.m_name.c_str(), "luigi"))
		{
			const uint32 luigi_runner_idx = get_runner_idx(k_team_luigi, 0);
			m_players[k_team_luigi].m_current_local_runner = luigi_runner_idx;
			runner& luigi_runner = get_runner(luigi_runner_idx);
			luigi_runner.m_actor = root_actor;
			luigi_runner.m_flags = runner::flags::luigi;

			auto& movement = add_component<component::type::movement>(root_actor);
			auto& brain = add_component<component::type::brain>(root_actor);
			auto& physics = add_component<component::type::physics>(root_actor);
		}
		if (strstr(node.m_name.c_str(), "toad"))
		{
			const uint32 runner_idx = 1 + (s_running_num_toads++ % (k_num_runners_per_team - 1));
			runner& runner = get_runner(runner_idx);
			runner.m_actor = root_actor;
			runner.m_flags = runner::flags::toad;

			auto& movement = add_component<component::type::movement>(root_actor);
			auto& brain = add_component<component::type::brain>(root_actor);
			auto& physics = add_component<component::type::physics>(root_actor);
		}
		if (strstr(node.m_name.c_str(), "kritter"))
		{
			auto& brain = add_component<component::type::brain>(root_actor);
			auto& physics = add_component<component::type::physics>(root_actor);
		}
		if (strstr(node.m_name.c_str(), "ball"))
		{
			m_ball.m_actor = root_actor;
			auto& physics = add_component<component::type::physics>(root_actor);
		}

		// ... each meshes as child actor
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
					auto& bounds = add_component<component::type::bounds>(root_actor);
					bounds.m_box = mesh->m_bounds_box;
					bounds.m_sphere = mesh->m_bounds_sphere;
					// mesh_transform.m_name = node.m_name + ": collider";
				}
				else
				{
					auto& render = add_component<component::type::render>(mesh_actor);
					render.m_mesh = meshid;
					// mesh_transform.m_name = node.m_name + ": mesh-" + std::to_string(i);
				}
			}
		}
	});

	// resolve & save as 'reset' position
	m_transman.save_as_reset();

	// log scene graph
#if 0
	if (cm_log_transforms.get_value() > 0)
	m_transform_graph.traverse([this](uint32 c, uint32 p)
	{
		const auto& node = m_transform_graph.get(c);
		const auto& data = m_transform_graph.get(c).data();
		string message = data.m_name;
		for (uint32 i = 0u; i < node.get_depth(); ++i)
			message = "  " + message;
		logman::log(message);
	});
#endif
}

void gameman::start(const contentman& cman)
{
	assemble_fbx_scene(cman, string(k_content_folder) + "scene.fbx");
}

void gameman::tick(float seconds, float delta_seconds)
{
	inputman& input = inputman::get();
	commandman& commands = commandman::get();

	// reset physics and transform graph
	if (input.is_button_down(cm_game_reset.value().c_str()))
		reset();

	// 1. ball logic
	switch (m_ball.m_state)
	{
	case ball::state::dribbled:
	{
		const runner& runner = get_runner(m_ball.m_dribble.m_runner_dribble);
		const float3 dribbler_position = actor_get_position(runner.m_actor, space::world);
		const float3 dribbler_fwd = actor_get_rotation(runner.m_actor, space::world) * float3(0, 0, 0.5);

		actor_set_position(m_ball.m_actor, dribbler_position + dribbler_fwd, space::world);
	}break;
	case ball::state::passing:
	{
		runner& dest = get_runner(m_ball.m_pass.m_runner_dest);
		runner& source = get_runner(m_ball.m_pass.m_runner_source);

		const float3 current_position = actor_get_position(m_ball.m_actor, space::world);
		const float3 dest_position = actor_get_position(dest.m_actor, space::world);
		const float3 delta = dest_position - current_position;

		comp_physics* phys = get_component<component::type::physics>(m_ball.m_actor);
		if (phys)
		{
			phys->m_acceleration += glm::normalize(delta) * 1000.0f;
			phys->m_maxspeed = 100;
		}

		if (glm::dot(delta, delta) < 0.5)
		{
			ball_start_dribble(m_ball.m_pass.m_runner_dest);
		}
	} break;
	}

	// player logic (input handling)
	const bool devcontrols_enabled = cm_camera_dev_control.get_value() > 0;
	if (!devcontrols_enabled)
	{
		const float2 hor_inputs[2] = {
			float2((float)input.is_button_down('a') - (float)input.is_button_down('d'), (float)input.is_button_down('w') - (float)input.is_button_down('s')),
			float2(0,0)
		};
		const bool is_pass_down[2] = {
			input.is_button_down(cm_game_ctrl0_pass.value().c_str()),
			false
		};

		for (uint32 tid = 0u; tid < 2; ++tid)
		{
			const uint32 global_runner = get_runner_idx(tid, m_players[tid].m_current_local_runner);
			const uint32 runner_idx = m_players[tid].m_current_local_runner;
			runner& current_runner = get_runner(tid, runner_idx);
			comp_movement* movement = get_component<component::type::movement>(current_runner.m_actor);
			if (movement) movement->m_input = hor_inputs[tid];

			// pass on cooldown
			on_cooldown(is_pass_down[tid] && ball_held_by_runner(global_runner), delta_seconds, 2.0f)
			{
				const uint32 source_idx = get_runner_idx(tid, runner_idx);
				const uint32 dest_idx = get_runner_idx(tid, (m_players[tid].m_current_local_runner + 1) % k_num_runners_per_team);
				ball_pass(source_idx, dest_idx);
			}};
		}
	}

	// AI logic
	if (!devcontrols_enabled && cm_game_ai.get_value())
	{
		for (auto& cmp_brain : components<component::type::brain>())
		{
			actor_id brain_owner = cmp_brain.m_owner;

			// decide
			cmp_brain.m_decision_timer -= delta_seconds;
			if (cmp_brain.m_decision_timer < 0.0f)
			{
				// make decision
				const float random_angle = random_flt(0.0f, 2.0f * 3.14f);
				const float2 random_direction = float2(cos(random_angle), sin(random_angle));

				const float3 brain_position = actor_get_position(brain_owner, space::world);
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
			auto* cmp_movement = get_component<component::type::movement>(brain_owner);
			if (cmp_movement)
			{
				cmp_movement->m_input = cmp_brain.m_decision;
			}
		}
	}

	// resolve all movements
	// adds accelerations to the corresponding physics components
	{
		PIXScopedEvent(0, "system_movements");
		for (auto& cmp_movement : components<component::type::movement>())
		{
			const auto owner = cmp_movement.m_owner;
			const float3 delta_movement = float3(-cmp_movement.m_input.x, 0, cmp_movement.m_input.y) * cm_game_movement_acc.get_value();
			const float any_movement = dot(delta_movement, delta_movement) > 0;

			comp_physics* phys = get_component<component::type::physics>(owner);
			if (phys)
			{
				phys->m_acceleration += delta_movement;
				phys->m_maxspeed = cm_game_movement_mxsp.get_value();
				// phys->m_drag_multiplier = 1.0f - any_movement;
			}

			if (any_movement > 0.0f)
			{
				actor_set_rotation(owner,
					glm::quatLookAt(glm::normalize(-delta_movement), float3(0, 1, 0)), space::world);
			}

			// reset input
			cmp_movement.m_input = float2();
		}
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
		m_camera.m_velocity += delta_position * delta_seconds * cm_camera_acceleration.get_value();
		m_camera.m_velocity = clamp_length(m_camera.m_velocity, cm_camera_maxspeed.get_value() * sprint_multiplier);
		m_camera.m_transform.add_position_world(m_camera.m_velocity * delta_seconds);

		// drag camera velocity
		if (glm::dot(delta_position, delta_position) > 0.0001f)
		{
			m_camera.m_velocity *= 0.001f;
		}

		const float2 mouse_delta = input.get_mouse_delta();

		if (input.is_button_down(inputman::button::rmouse) && glm::dot(mouse_delta, mouse_delta) > 0.001)
		{
			const float2 delta_rotation = float2(mouse_delta.y, mouse_delta.x) * delta_seconds * cm_camera_sensy.get_value();
			m_camera.m_transform.add_rotation_camera(delta_rotation.y, delta_rotation.x);
		}
	}

	// resolve physics
	if (cm_game_physics_enable.get_value() > 0)
	{
		PIXScopedEvent(0, "system_physics");

		// step 1: calculate delta positions
		for (auto& cmp_physics : components<component::type::physics>())
		{
			cmp_physics.m_deltapos_candidate = {};

			// resolve velocity
			auto& acceleration = cmp_physics.m_acceleration;
			auto& velocity = cmp_physics.m_velocity;
			float gravity = cmp_physics.m_gravity;
			if (cm_game_gravity_enable.get_value() <= 0)
				gravity = 0;

			// apply gravity acceleration
			acceleration += float3(0, gravity, 0);
			velocity += acceleration * delta_seconds;

			// apply air drag to velocity
			const float velocity_sqr = glm::dot(velocity, velocity);
			if (velocity_sqr < 0.00001f) velocity = {};
			else
			{
				const float air_density = cm_game_airdrag.get_value() * cmp_physics.m_drag_multiplier;
				const float area = 1.0f; // dont care
				const float mass = 1.0f; // dont care
				const float drag_deceleration = (air_density * velocity_sqr * area) / (2.0f * mass);
				const float3 dragged_velocity = velocity - (glm::normalize(velocity) * drag_deceleration * delta_seconds);
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

			cmp_physics.m_deltapos_candidate = velocity * delta_seconds;
			acceleration = {};
		}

		// step 2: calculate the aabbs (and write them to collision man)
		for (auto& cmp_bounds : components<component::type::bounds>())
		{
			actor_id owner = cmp_bounds.m_owner;

			// get the deltapos of physics component if any
			float3 deltapos = {};
			if (comp_physics const* physics = get_component<component::type::physics>(owner))
			{
				deltapos = physics->m_deltapos_candidate;
			}

			// recompute world aabb
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

			const auto& transform = actor_get_transform(cmp_bounds.m_owner, space::world);
			cmp_bounds.m_world_aabb = {};
			cmp_bounds.m_world_aabb.m_position = transform.get_position();
			for (uint32 i = 0u; i < 8; ++i)
			{
				cmp_bounds.m_world_aabb.grow_to_fit(transform.m_matrix * float4(corners[i], 1));
			}
			m_colman.write_aabb(cmp_bounds.m_id, cmp_bounds.m_world_aabb);
		}
		
		// step 3: collision man gathers all collision pairs
		m_colman.calculate_collisions();

		// step 4: resolve collisions (adjusts delta positions)
		for (auto& cmp_physics : components<component::type::physics>())
		{
			if (comp_bounds* bounds = get_component<component::type::bounds>(cmp_physics.m_owner))
			{
				for (uint32 i = 0u; i < m_colman.num_collisions(bounds->m_id); ++i)
				{
					const auto& collision = m_colman.get_collision_at_index(bounds->m_id, i);
					const float overlap_flip = (collision.m_collider_x == bounds->m_id ? 1.0f : -1.0f);
					// cmp_physics.m_deltapos_candidate += collision.m_overlap * overlap_flip;
				}
			}
		}

		// step 5: apply resolved delta positions to the transform
		for (auto& cmp_physics : components<component::type::physics>())
		{
			const auto& current_position = actor_get_position(cmp_physics.m_owner, strikers::space::world);
			actor_set_position(cmp_physics.m_owner, current_position + cmp_physics.m_deltapos_candidate, strikers::space::world);
		}
	}

	// resolve transform hierarchy
	{
		PIXScopedEvent(0, "resolve_transforms");
		m_transman.resolve_graph();
	}
}

void gameman::build_renderscene(const contentman& cman, renderscene& scene)
{
	PIXScopedEvent(0, "build_renderscene");
	renderscene::line_builder lines{ scene };

	// hardcoded camera settings
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

	// hardcoded light
	scene.m_light.m_color = float4(1, 1, 1, 1);
	scene.m_light.m_direction = float4(-1, 1, -1, -1);

	// draw meshes
	for (const auto& cmp_render : components<component::type::render>())
	{
		const auto& transform = actor_get_transform(cmp_render.m_owner, space::world);

		// draw the pawn mesh instance
		const mesh_id meshid = cmp_render.m_mesh;
		mesh_asset const* mesh = cman.find_mesh(meshid);
		if (!mesh)
		{
			continue;
		}

		auto& mesh_instance = scene.add_mesh_instance(meshid, shader::shaded);
		mesh_instance.m_transform = transform;
		mesh_instance.apply_material(cman, cman.get_mesh_material_id(meshid));
	}

	// draw bounds
	if (cm_draw_debug.get_value() > 0)
	for (const auto& cmp_bounds : components<component::type::bounds>())
	{
		const auto& transform = actor_get_transform(cmp_bounds.m_owner, space::world);

		// lines.add_sphere(transform->m_transform, cmp_bounds.m_sphere, colors::green());
		const bool collision = m_colman.num_collisions(cmp_bounds.m_id) > 0;
		lines.add_box(transform::identity(), cmp_bounds.m_world_aabb, collision ? colors::red() : colors::green());
	}

	// draw transforms
	if (cm_draw_debug.get_value() > 0)
	{
		lines.add_transform(transform::identity());
		for (const auto& trans_id : m_transman.get_transforms())
		{
			lines.add_transform(m_transman.get_transform(trans_id, space::world));
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

void gameman::reset()
{
	m_transman.reset();
	for (auto& phys : components<component::type::physics>())
	{
		phys.reset();
	}
	for (auto& move : components<component::type::movement>())
	{
		move.m_input = float2();
	}
	m_camera.m_velocity = {};
	m_camera.m_transform = m_camera.m_transform_original;
}
}

