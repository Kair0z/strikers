#include "gameman.h"
#include "renderman.h"
#include "contentman.h"

namespace strikers {

static const float3 k_pitch_center = {};
static const uint32 k_players_per_team = 4;
static const mesh_id k_mesh_toad = contentman::make_mesh_id(string(k_content_folder) + "meshes/toad/toad.obj", 1);
static const mesh_id k_mesh_ball = contentman::make_mesh_id(string(k_content_folder) + "meshes/ball/ball.obj", 0);
static const mesh_id k_kritter = contentman::make_mesh_id(string(k_content_folder) + "meshes/kritter/kritter.obj", 1);
static const float4 k_team_colors[]
{
	float4(0,1,0,1),
	float4(0,0,1,1)
};

void gameman::start()
{
	// initialize players
	for (uint32 t = 0; t < 2; ++t)
	for (uint32 i = 0; i < k_players_per_team; ++i)
	{
		m_pawns.push_back({});
		pawn& pwn = m_pawns.back();
		pwn.m_mesh = k_kritter;
		pwn.m_team_id = t;
	}

	// initialize ball
	m_ball.m_transform.set_position(k_pitch_center);
	m_ball.m_transform.set_scale(1);

	// initialize camera
	const float distance = 350;
	m_camera_position = float3( 0, distance * 1.4f, distance );
	m_camera_target = k_pitch_center;
}

void gameman::tick_input()
{
	
}

void gameman::tick(float seconds, float delta_seconds)
{
	static const float k_team_distance = 400;
	static const float k_player_distance = 200;
	for (uint32 i = 0; i < m_pawns.size(); ++i)
	{
		const uint32 team = i / k_players_per_team;
		const uint32 player_id = i % k_players_per_team;

		// initial position
		const float angle = (0.01 * seconds) + (i * (3.14 * 2 / k_players_per_team));
		const float3 team_center_pos = float3((team == 0 ? -k_team_distance : k_team_distance), 0.0f, 0.0f);
		const float3 instance_offset = k_player_distance * float3(cos(angle), 0.0f, sin(angle));
		m_pawns[i].m_transform.set_position(team_center_pos + instance_offset);
		m_pawns[i].m_transform.look_at(k_pitch_center);
	}
}

void gameman::build_renderscene(renderscene& scene) const
{
	for (const auto& pawn : m_pawns)
	{
		auto& instance = scene.add_instance(pawn.m_mesh);
		instance.m_transform = pawn.m_transform;
		instance.m_color = k_team_colors[pawn.m_team_id % _countof(k_team_colors)];
	}
	scene.add_instance(k_mesh_ball).m_transform = m_ball.m_transform;

	scene.m_camera.m_transform.m_matrix = calculate_transform(m_camera_position, m_camera_target, float3(0, 1, 0));
	scene.m_camera.m_near = 0.001f;
	scene.m_camera.m_far = 1000.0f;
	scene.m_camera.m_fov = 90.0f;

	scene.m_light.m_color = float4(1, 1, 1, 1);
	scene.m_light.m_direction = float4(-1,-1,-1,-1);
}
}

