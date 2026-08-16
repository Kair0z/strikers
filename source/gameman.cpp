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

gameman::actor& gameman::create_actor(const actor_create_args& create_args)
{
	physman& physics = physman::get();
	const role rl = create_args.m_role;
	actor new_actor{};
	new_actor.m_role = rl;
	new_actor.m_transform = create_args.m_transform;
	new_actor.m_transform_original = new_actor.m_transform;
	new_actor.m_transform_physics = new_actor.m_transform;
	new_actor.m_mesh = create_args.m_mesh;
	
	// create physics body
	{
		physman::create_body_args body_args;
		switch (rl)
		{
		case role::statik:
			body_args.m_flags = physman::body_flags::none;
			break;
		default:
			body_args.m_flags = physman::body_flags::dynamic;
			break;
		}
		body_args.m_shape = physman::body_shape::sphere;
		body_args.m_transform = new_actor.m_transform;
		new_actor.m_body = physics.create_body(body_args);
	}

	m_actors.push_back(new_actor);
	const uint32 id = (uint32)m_actors.size() - 1u;
	m_role_lookup[rl].push_back(id);
	return m_actors.back();
}

bool gameman::find_role_actor(role role, uint32 index, actor*& out_actor_ptr)
{
	auto found = m_role_lookup.find(role);
	if (found != m_role_lookup.cend())
	{
		const vector<uint32>& role_actor_indices = found->second;
		if (index < role_actor_indices.size())
		{
			out_actor_ptr = &m_actors[role_actor_indices[index]];
			return true;
		}
		else return false;
	}
	else return false;
}

void gameman::assemble_scene(const contentman& cman, const stringview& filepath)
{
	const scene_id scid = contentman::make_scene_id(filepath);
	auto found_result = cman.find_typed_asset<asset_type::scene>(scid);
	if (found_result.is_fail())
	{
		return;
	}

	const scene_asset& scene = *found_result.claim();
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
		
#if 0
		const float3 position = m_camera_transform.get_position();
		const float3 forward = m_camera_transform.get_forward();
		const float3 up = m_camera_transform.get_up();
		const float3 right = m_camera_transform.get_right();

		logman::log("[camera]");
		logman::log("> pos: " format_f3, position.x, position.y, position.z);
		logman::log("> fwd: " format_f3, forward.x, forward.y, forward.z);
		logman::log("> up: " format_f3, up.x, up.y, up.z);
		logman::log("> right: " format_f3, right.x, right.y, right.z);
#endif
	}

	scene.m_graph.traverse([cman, this, &scene](uint32 node_id)
	{
		const scene_asset::node& node = scene.m_graph.get(node_id).data();
		for (mesh_id mesh : node.m_meshes)
		{
			actor_create_args new_actor{};
			if (strstr(node.m_name.c_str(), "kritter"))
				new_actor.m_role = role::keeper;
			if (strstr(node.m_name.c_str(), "goal"))
				new_actor.m_role = role::goal;
			if (strstr(node.m_name.c_str(), "toad"))
				new_actor.m_role = role::pawn;
			
			new_actor.m_mesh = mesh;
			new_actor.m_transform.m_matrix = node.m_scene_transform;
			create_actor(new_actor);
			
#if 0
			const float3 position = pwn.m_transform.get_position();
			const float3 forward = pwn.m_transform.get_forward();
			const float3 up = pwn.m_transform.get_up();
			const string mesh_name = cman.find_typed_asset<asset_type::mesh>(mesh).claim()->m_name;

			logman::log("[mesh {}]", mesh_name);
			logman::log("> pos: " format_f3, position.x, position.y, position.z);
			logman::log("> fwd: " format_f3, forward.x, forward.y, forward.z);
			logman::log("> up: " format_f3, up.x, up.y, up.z);

			if (strstr(mesh_name.c_str(), "kritter") != nullptr)
			{
				static int a = 0;
				a++;
			}
#endif
		}
	});
}

void gameman::start(const contentman& cman)
{
	assemble_scene(cman, string(k_content_folder) + "scene.fbx");
}

void gameman::tick(float seconds, float delta_seconds)
{
	inputman& input = inputman::get();
	commandman& commands = commandman::get();
	physman& physics = physman::get();
	
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

	// gather physics
	const bool space_down = input.is_button_down(inputman::button::space);
	for (auto& actor : m_actors)
	{
		physics.query_body_transform(actor.m_body, actor.m_transform);
		if (space_down) physics.body_apply_force(actor.m_body,
			physman::force_args::force(100.0f * float3{ 0,1,0 }));
	}
}

void gameman::build_renderscene(const contentman& cman, renderscene& scene) const
{
	physman& physics = physman::get();

	scene.m_camera.m_near = 0.001f;
	scene.m_camera.m_far = 1000.0f;
	scene.m_camera.m_fov = 140;
	scene.m_camera.m_transform = m_camera.m_transform;
	scene.m_light.m_color = float4(1, 1, 1, 1);
	scene.m_light.m_direction = float4(-1, 1, -1, -1);
	
	camera_asset const* camera_asset = cman.find_camera(m_camera.m_asset_id);
	if (camera_asset && false)
	{
		scene.m_camera.m_near = camera_asset->m_clip_near;
		scene.m_camera.m_far = camera_asset->m_clip_far;
		scene.m_camera.m_fov = camera_asset->m_fov_horizontal;
	}

	// gather ui instances
	for (uint32 i = 0; i < 0; ++i)
	{
		auto& instance = scene.add_ui_instance();
		instance.m_image = k_img_checkerboard;
		instance.m_box;
	}
	
	// draw physics debug
	if (cm_draw_debug_physics.get_value() > 0)
	{
		physman::db_draw args{};
		args.m_flags |= physman::db_draw::draw_bounds;
		args.m_flags |= physman::db_draw::draw_all;
		physics.debug_draw(args, scene);
	}

	// gather mesh instances
	for (const auto& actor : m_actors)
	{
		// draw the pawn mesh instance
		const mesh_id mesh = actor.m_mesh;
		auto& mesh_instance = scene.add_mesh_instance(mesh, shader::shaded);
		mesh_instance.m_transform = actor.m_transform;
		mesh_instance.m_color = k_team_colors[actor.m_team_id % _countof(k_team_colors)];
		mesh_instance.apply_material(cman, cman.get_mesh_material_id(mesh));
	}

	// draw debug
	renderscene::line_builder lines{ scene };
	transform trans = transform::identity();
	trans.set_scale(1000);
	lines.add_transform(trans);
}
}

