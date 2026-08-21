#include "gameman.h"
#include "renderman.h"
#include "contentman.h"
#include "inputman.h"
#include "commandman.h"
#include "physman.h"

namespace strikers {

command cm_camera_sensy("camera.sensy", "0.1");
command cm_camera_devcontrol("camera.devcontrol", "0");
command cm_camera_maxspeed("camera.maxspeed", "10");
command cm_camera_acceleration("camera.acceleration", "10");

command cm_game_physics_enable("game.physics.enable", "1");
command cm_game_gravity_enable("game.gravity.enable", "1");
command cm_game_air_density("game.airdensity", "1.225");
command cm_draw_debug("draw.debug", "1");
command cm_draw_debug_physics("draw.debug.physics", "1");

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
			auto& render = add_component<component::type::render>(new_actor);
			render.m_mesh = meshid;
			auto& transform = add_component<component::type::transform>(new_actor);
			add_component<component::type::physics>(new_actor);
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
	if (cm_camera_devcontrol.get_value() > 0)
	{
		// move the camera
		const float4 awsd = {
			input.is_button_down('a'),
			input.is_button_down('w'),
			input.is_button_down('s'),
			input.is_button_down('d')
		};
		const float2 updown {
			input.is_button_down(inputman::button::space),
			input.is_button_down(inputman::button::shift)
		};
		float3 delta_horizontal =
			(m_camera.m_transform.get_right() * (awsd.w - awsd.x)) +
			(m_camera.m_transform.get_forward() * (awsd.y - awsd.z));
		float3 delta_position = delta_horizontal + (float3(0, 1, 0) * (updown.x - updown.y));
		m_camera.m_velocity += delta_position * delta_seconds * cm_camera_acceleration.get_value();
		m_camera.m_velocity = clamp_length(m_camera.m_velocity, cm_camera_maxspeed.get_value());

		m_camera.m_transform.add_position_world(m_camera.m_velocity * delta_seconds);

		// drag camera velocity
		if (glm::dot(awsd, awsd) < 0.0001f && glm::dot(updown, updown) < 0.0001f)
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
		if (input.is_button_down('r'))
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

	for (const auto& cmp_render : components<component::type::render>())
	{
		comp_transform const* transform = get_component<component::type::transform>(cmp_render.m_owner);
		if (!transform)
		{
			continue;
		}

		// draw the pawn mesh instance
		const mesh_id mesh = cmp_render.m_mesh;
		auto& mesh_instance = scene.add_mesh_instance(mesh, shader::shaded);
		mesh_instance.m_transform = transform->m_transform;
		mesh_instance.apply_material(cman, cman.get_mesh_material_id(mesh));
	}

	for (const auto& cmp_ui_render : components<component::type::renderui>())
	{
		auto& instance = scene.add_ui_instance();
		instance.m_image = k_img_checkerboard;
		instance.m_box;
	}
}
}

