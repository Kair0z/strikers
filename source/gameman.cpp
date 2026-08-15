#include "gameman.h"
#include "renderman.h"
#include "contentman.h"
#include "inputman.h"

namespace strikers {

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
		m_camera.m_transform.m_matrix = scene.m_cameras[0].m_scene_transform;
		m_camera.m_transform_original = m_camera.m_transform;
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
			m_pawns.push_back({});
			pawn& pwn = m_pawns.back();
			pwn.m_mesh = mesh;
			pwn.m_team_id;
			pwn.m_transform.m_matrix = node.m_scene_transform;
			pwn.m_transform_original = pwn.m_transform;
			
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

void gameman::tick_input()
{
	
}

void gameman::tick(float seconds, float delta_seconds)
{
	inputman& input = inputman::get();

	// move the camera
	const float a_acc = input.is_button_down('a');
	const float w_acc = input.is_button_down('w');
	const float s_acc = input.is_button_down('s');
	const float d_acc = input.is_button_down('d');
	const float up_acc = input.is_button_down(inputman::button::space);
	const float down_acc = input.is_button_down(inputman::button::shift);

	m_camera.m_velocity += delta_seconds * 0.005f * float3(a_acc - d_acc, up_acc - down_acc, w_acc - s_acc);
	m_camera.m_transform.add_position_world(m_camera.m_velocity * delta_seconds);

	// reset camera
	if (input.is_button_down('r'))
	{
		m_camera.m_velocity = {};
		m_camera.m_transform = m_camera.m_transform_original;
	}
}

void gameman::build_renderscene(const contentman& cman, renderscene& scene) const
{
	scene.m_camera.m_transform = m_camera.m_transform;
	scene.m_camera.m_near = 0.001f;
	scene.m_camera.m_far = 1000.0f;
	scene.m_camera.m_fov = 120.0f;
	scene.m_light.m_color = float4(1, 1, 1, 1);
	scene.m_light.m_direction = float4(-1, 1, -1, -1);

	// gather ui instances
	for (uint32 i = 0; i < 0; ++i)
	{
		auto& instance = scene.add_ui_instance();
		instance.m_image = k_img_checkerboard;
		instance.m_box;
	}
	
	// gather mesh instances
	for (const auto& pawn : m_pawns)
	{
		const mesh_id mesh = pawn.m_mesh;
		{
			auto& mesh_instance = scene.add_mesh_instance(mesh, shader::shaded);
			mesh_instance.m_transform = pawn.m_transform;
			mesh_instance.m_color = k_team_colors[pawn.m_team_id % _countof(k_team_colors)];

			string name;
			mat_id material = cman.get_mesh_material_id(mesh);
			mesh_instance.apply_material(cman, material);
		}

		auto& dbg_instance = scene.add_mesh_instance(k_box, shader::wireframe);
		dbg_instance.m_transform = pawn.m_transform;
		dbg_instance.m_color = { 1,1,1,1 };
	}
}
}

