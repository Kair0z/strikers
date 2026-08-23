#include "gameman.h"
#include "renderman.h"
#include "contentman.h"
#include "inputman.h"
#include "commandman.h"
#include "physman.h"

namespace strikers {
command cm_camera_sensy("camera_sensy", "0.1");
command cm_camera_dev_control("camera_dev_control", "0");
command cm_camera_dev_reset("camera_dev_reset", "r");
command cm_camera_dev_left("camera_dev_left", "a");
command cm_camera_dev_right("camera_dev_right", "d");
command cm_camera_dev_forward("camera_dev_forward", "w");
command cm_camera_dev_back("camera_dev_back", "s");
command cm_camera_dev_up("camera_dev_up", "space");
command cm_camera_dev_down("camera_dev_down", "shift");

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

command cm_draw_debug("draw_debug", "1");

command cm_log_transforms("log_transforms", "1");

static const float3 k_pitch_center = {};
static const uint32 k_players_per_team = 4;
static const mesh_id k_mesh_toad = contentman::make_mesh_id(string(k_content_folder) + "meshes/toad/toad.obj", 1);
static const mesh_id k_mesh_ball = contentman::make_mesh_id(string(k_content_folder) + "meshes/ball/ball.obj", 0);
static const mesh_id k_kritter = contentman::make_mesh_id(string(k_content_folder) + "meshes/kritter/kritter.obj", 1);
static const mesh_id k_transistor = contentman::make_mesh_id(string(k_content_folder) + "meshes/transistor.fbx", 0);
static const mesh_id k_jake = contentman::make_mesh_id(string(k_content_folder) + "meshes/jake/jake.fbx", 0);
static const mesh_id k_box = contentman::make_mesh_id(string(k_content_folder) + "meshes/box.fbx", 0);
static const mesh_id k_all_meshes[]
{
	k_mesh_toad,
	k_mesh_ball,
	k_kritter,
	k_transistor,
	k_box
};

static const image_id k_img_checkerboard = contentman::make_image_id(string(k_content_folder) + "textures/checkerboard.png");
static const image_id k_img_jake = contentman::make_image_id(string(k_content_folder) + "meshes/jake/jake_outfit0_tex.png");
static const float4 k_team_colors[]
{
	float4(0,1,0,1),
	float4(0,0,1,1)
};

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
		const transform parent_world_transform = valid_parent ? get_actor_transform(parent_actor).m_world : transform::identity();
		const transform parent_inv_world = glm::inverse(parent_world_transform.m_matrix);

		// for each array of meshes, add a new actor as root...
		const scene_asset::node& parent = scene.m_graph.get(parent_id).data();
		const scene_asset::node& node = scene.m_graph.get(node_id).data();
		actor_id root_actor = create_actor(parent_actor);
		auto& root_transform = get_actor_transform(root_actor);
		{
			const transform world = node.m_scene_transform;
			root_transform.m_world = world;
			root_transform.m_local = valid_parent ? parent_inv_world.m_matrix * world.m_matrix : world;
			root_transform.m_local_reset = root_transform.m_local;
			root_transform.m_name = node.m_name;
			node_id_to_actor_id[node_id] = root_actor;

			if (strstr(node.m_name.c_str(), "toad"))
			{
				set_actor_flag(root_actor, actor::flags::toad);
				auto& movement	= add_component<component::type::movement>(root_actor);
				auto& brain		= add_component<component::type::brain>(root_actor);
				auto& physics	= add_component<component::type::physics>(root_actor);
			}
			if (strstr(node.m_name.c_str(), "kritter"))
			{
				set_actor_flag(root_actor, actor::flags::kritter);
				auto& brain = add_component<component::type::brain>(root_actor);
				auto& physics = add_component<component::type::physics>(root_actor);
			}
			if (strstr(node.m_name.c_str(), "ball"))
			{
				set_actor_flag(root_actor, actor::flags::ball);
				auto& physics = add_component<component::type::physics>(root_actor);

				m_ball.m_actor = root_actor;
			}
		}

		// ... each meshes as child actor
		for (uint32 i = 0u; i < node.m_meshes.size(); ++i)
		{
			const mesh_id meshid = node.m_meshes[i];
			actor_id mesh_actor = create_actor(root_actor);
			auto& mesh_transform = get_actor_transform(mesh_actor);
			const transform world = node.m_scene_transform;
			mesh_transform.m_world = root_transform.m_world;
			mesh_transform.m_local = transform::identity(); // no transform from root
			mesh_transform.m_local_reset = mesh_transform.m_local;
			mesh_transform.m_name = node.m_name;

			// get the mesh & material, if material is named 'collider' we add a bounds component to the actor
			bool is_material_collider = false;
			mesh_asset const* mesh = nullptr; material_asset const* material = nullptr;
			if (cman.find_mesh_and_material(meshid, mesh, material))
			{
				if (strstr(material->m_name.c_str(), "collider"))
				{
					auto& bounds = add_component<component::type::bounds>(mesh_actor);
					bounds.m_box = mesh->m_bounds_box;
					bounds.m_sphere = mesh->m_bounds_sphere;
					mesh_transform.m_name = node.m_name + ": collider";
				}
				else
				{
					auto& render = add_component<component::type::render>(mesh_actor);
					render.m_mesh = meshid;
					mesh_transform.m_name = node.m_name + ": mesh-" + std::to_string(i);
				}
			}
		}
	});

	// log scene graph
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
}

void gameman::start(const contentman& cman)
{
	assemble_fbx_scene(cman, string(k_content_folder) + "scene.fbx");
}

void gameman::tick(float seconds, float delta_seconds)
{
	inputman& input = inputman::get();
	commandman& commands = commandman::get();
	physman& physics = physman::get();
	
	if (input.is_button_down( cm_game_reset.value().c_str() ))
	{
		for (auto& trs : m_transform_graph.get_flat())
		{
			trs.data().m_local = trs.data().m_local_reset;
		}
		for (auto& phys : components<component::type::physics>())
		{
			phys.reset();
		}
	}

	const bool devcontrols_enabled = cm_camera_dev_control.get_value() > 0;

	// ball logic
	{
		auto& trs = get_actor_transform(m_ball.m_actor);

		auto& current_player_movement = components<component::type::movement>()[m_players[0].current_movement];
		actor_id player_movement_actor = current_player_movement.m_owner;
		const auto& current_player_trs = get_actor_transform(player_movement_actor);

		trs.m_local.set_position(
			current_player_trs.m_world.get_position() + current_player_trs.m_world.get_forward() * 50.0f);
	}

	// brain logic (AI)
	if (cm_game_ai.get_value())
	{
		for (auto& cmp_brain : components<component::type::brain>())
		{
			actor_id brain_owner = cmp_brain.m_owner;
			const auto& brain_trs = get_actor_transform(brain_owner);

			// decide
			cmp_brain.m_decision_timer -= delta_seconds;
			if (cmp_brain.m_decision_timer < 0.0f)
			{
				// make decision
				const float random_angle = random_flt(0, 2 * 3.14);
				const float2 random_direction = float2(cos(random_angle), sin(random_angle));

				const float3 brain_position = brain_trs.m_world.get_position();
				const float2 brain_hor_position = float2(brain_position.x, brain_position.z);

				const float distance_from_middle = glm::dot(brain_hor_position, brain_hor_position) * 0.00001 * cm_game_ai_random.get_value();
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

	// player input -> player movement
	if (!devcontrols_enabled)
	{
		auto& current_player_movement = components<component::type::movement>()[m_players[0].current_movement];
		
		const float2 hor_input = float2(
			(float)input.is_button_down('a') - (float)input.is_button_down('d'),
			(float)input.is_button_down('w') - (float)input.is_button_down('s')
		);
		current_player_movement.m_input = glm::dot(hor_input, hor_input) > 0.0f ? glm::normalize(hor_input) : float2(0,0);

		// re-apply max speed for each pawn
		comp_physics* phys = get_component<component::type::physics>(current_player_movement.m_owner);
		if (phys)
		{
			phys->m_maxspeed = cm_game_movement_mxsp.get_value();
		}
	}

	// resolve all movements (applies to physics component)
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
				// phys->m_drag_multiplier = 1.0f - any_movement;
			}

			if (any_movement > 0.0f)
			{
				auto& local_trs = get_actor_transform(owner).m_local;
				local_trs.look_twd(delta_movement);
			}
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

		float3 delta_horizontal =
			(m_camera.m_transform.get_right() * (right - left)) +
			(m_camera.m_transform.get_forward() * (fwd - back));
		float3 delta_vertical =
			(float3(0, 1, 0) * (up - down));

		float3 delta_position = delta_horizontal + delta_vertical;
		m_camera.m_velocity += delta_position * delta_seconds * cm_camera_acceleration.get_value();
		m_camera.m_velocity = clamp_length(m_camera.m_velocity, cm_camera_maxspeed.get_value());
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

		// reset camera
		if (input.is_button_down(cm_camera_dev_reset.value().c_str()))
		{
			m_camera.m_velocity = {};
			m_camera.m_transform = m_camera.m_transform_original;
		}
	}

	// resolve physics
	if (cm_game_physics_enable.get_value() > 0)
	{
		PIXScopedEvent(0, "system_physics");
		for (auto& cmp_physics : components<component::type::physics>())
		{
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

			// resolve transform
			const float3 delta_position = velocity * delta_seconds;
			auto& transform = get_actor_transform(cmp_physics.m_owner);
			transform.m_local.add_position_world(delta_position);

			// reset acceleration
			acceleration = {};
		}
	}

	// resolve transform hierarchy
	{
		PIXScopedEvent(0, "resolve_transforms");
		m_transform_graph.traverse([this](uint32 c, uint32 p)
		{
			const auto& parent = m_transform_graph.get(p).data();
			auto& data = m_transform_graph.get(c).data();
			if (!m_transform_graph.is_valid(p))
			{
				data.m_world = data.m_local;
			}
			else
			{
				data.m_world = parent.m_world.m_matrix * data.m_local.m_matrix;
			}
		});
	}
}

void gameman::build_renderscene(const contentman& cman, renderscene& scene) const
{
	PIXScopedEvent(0, "build_renderscene");
	physman& physics = physman::get();
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
		const auto& transform = get_actor_transform(cmp_render.m_owner).m_world;

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
		const auto& transform = get_actor_transform(cmp_bounds.m_owner).m_world;

		// lines.add_sphere(transform->m_transform, cmp_bounds.m_sphere, colors::green());
		lines.add_box(transform, cmp_bounds.m_box, colors::green());
	}

	// draw transforms
	if (cm_draw_debug.get_value() > 0)
	{
		lines.add_transform(transform::identity());
		for (const auto& trans_node : m_transform_graph.get_flat())
		{
			lines.add_transform(trans_node.data().m_world);
		}
	}
	
	// draw ui
	for (const auto& cmp_ui_render : components<component::type::renderui>())
	{
		auto& instance = scene.add_ui_instance();
		instance.m_image = k_img_checkerboard;
		instance.m_box;
	}
}
}

