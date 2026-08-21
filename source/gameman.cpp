#include "gameman.h"
#include "renderman.h"
#include "contentman.h"
#include "inputman.h"
#include "commandman.h"
#include "physman.h"

namespace strikers {

command cm_camera_sensy("camera.sensy", "0.1");
command cm_camera_dev_control("camera.dev.control", "0");
command cm_camera_dev_reset("camera.dev.reset", "r");
command cm_camera_dev_left("camera.dev.left", "a");
command cm_camera_dev_right("camera.dev.right", "d");
command cm_camera_dev_forward("camera.dev.forward", "w");
command cm_camera_dev_back("camera.dev.back", "s");
command cm_camera_dev_up("camera.dev.up", "space");
command cm_camera_dev_down("camera.dev.down", "shift");

command cm_camera_maxspeed("camera.maxspeed", "10");
command cm_camera_acceleration("camera.acceleration", "10");

command cm_game_physics_enable("game.physics.enable", "1");
command cm_game_gravity_enable("game.gravity.enable", "1");
command cm_game_air_density("game.airdensity", "1.225");
command cm_draw_debug("draw.debug", "1");

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

	scene.m_graph.traverse([cman, this, &scene](uint32 node_id)
	{
		// for each mesh entry in our scene asset: add a new actor
		const scene_asset::node& node = scene.m_graph.get(node_id).data();
		for (mesh_id meshid : node.m_meshes)
		{
			actor_id new_actor = create_actor();
			auto& transform = add_component<component::type::transform>(new_actor);
			auto& physics = add_component<component::type::physics>(new_actor);

			// get the mesh & material, if material is named 'collider' we add a bounds component to the actor
			bool is_material_collider = false;
			mesh_asset const* mesh = nullptr; material_asset const* material = nullptr;
			if (cman.find_mesh_and_material(meshid, mesh, material))
			{
				if (strstr(material->m_name.c_str(), "collider"))
				{
					auto& bounds = add_component<component::type::bounds>(new_actor);
					bounds.m_box = mesh->m_bounds_box;
					bounds.m_sphere = mesh->m_bounds_sphere;
				}
				else
				{
					auto& render = add_component<component::type::render>(new_actor);
					render.m_mesh = meshid;
				}
			}
			transform.m_transform.m_matrix = node.m_scene_transform;
		}
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

	// resolve physics
	if (cm_game_physics_enable.get_value() > 0)
	for (auto& cmp_physics : components<component::type::physics>())
	{
		comp_transform* transform = get_component<component::type::transform>(cmp_physics.m_owner);
		if (!transform)
		{
			continue;
		}

		// resolve velocity
		auto& velocity = cmp_physics.m_velocity;
		float gravity = cmp_physics.m_gravity;
		if (cm_game_gravity_enable.get_value() <= 0)
			gravity = 0;

		// apply gravity acceleration
		const float3 acceleration = float3(0, gravity, 0);
		velocity += acceleration * delta_seconds;

		// apply air drag
		const float velocity_sqr = glm::dot(velocity, velocity);
		if (velocity_sqr < 0.00001f) velocity = {};
		else
		{
			const float air_density = cm_game_air_density.get_value();
			const float area = 1.0f; // dont care
			const float mass = 1.0f; // dont care
			const float drag_deceleration = (air_density * velocity_sqr * area) / (2.0f * mass);
			velocity -= glm::normalize(velocity) * drag_deceleration * delta_seconds;
		}

		// resolve deltapos
		const float3 delta_position = velocity * delta_seconds;
		transform->m_transform.add_position_world(delta_position);
	}

	// tick dev-camera controls
	if (cm_camera_dev_control.get_value() > 0)
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
}

void gameman::build_renderscene(const contentman& cman, renderscene& scene) const
{
	physman& physics = physman::get();
	renderscene::line_builder lines{ scene };

	// hardcoded camera
	scene.m_camera.m_near = 0.001f;
	scene.m_camera.m_far = 1000.0f;
	scene.m_camera.m_fov = 140;
	scene.m_camera.m_transform = m_camera.m_transform;
	
	// use camera ASSET to guide camera settings
	camera_asset const* camera_asset = cman.find_camera(m_camera.m_asset_id);
	if (camera_asset && false)
	{
		scene.m_camera.m_near = camera_asset->m_clip_near;
		scene.m_camera.m_far = camera_asset->m_clip_far;
		scene.m_camera.m_fov = camera_asset->m_fov_horizontal;
	}

	// hardcoded light
	scene.m_light.m_color = float4(1, 1, 1, 1);
	scene.m_light.m_direction = float4(-1, 1, -1, -1);

	// draw meshes
	for (const auto& cmp_render : components<component::type::render>())
	{
		comp_transform const* transform = get_component<component::type::transform>(cmp_render.m_owner);
		if (!transform)
		{
			continue;
		}

		// draw the pawn mesh instance
		const mesh_id meshid = cmp_render.m_mesh;
		mesh_asset const* mesh = cman.find_mesh(meshid);
		if (!mesh)
		{
			continue;
		}

		auto& mesh_instance = scene.add_mesh_instance(meshid, shader::shaded);
		mesh_instance.m_transform = transform->m_transform;
		mesh_instance.apply_material(cman, cman.get_mesh_material_id(meshid));
	}

	// draw bounds
	if (cm_draw_debug.get_value() > 0)
	for (const auto& cmp_bounds : components<component::type::bounds>())
	{
		comp_transform const* transform = get_component<component::type::transform>(cmp_bounds.m_owner);
		if (!transform)
		{
			continue;
		}

		// lines.add_sphere(transform->m_transform, cmp_bounds.m_sphere, colors::green());
		lines.add_box(transform->m_transform, cmp_bounds.m_box, colors::green());
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

