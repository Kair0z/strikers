#include "common.h"
#include "physman.h"

namespace strikers {
class renderscene;
class contentman;
class gameman
{
	enum class role
	{
		statik,
		ball,
		keeper,
		pawn,
		goal,
		num
	};

	struct actor
	{
		role m_role;
		transform m_transform;
		transform m_transform_original;
		transform m_transform_physics;
		mesh_id m_mesh;
		int m_team_id;
		float3 m_velocity;
		physman::body_id m_body;
	};

	vector<actor> m_actors;
	umap<role, vector<uint32>> m_role_lookup;

	struct actor_create_args
	{
		role m_role;
		transform m_transform;
		mesh_id m_mesh;
	};

	struct camera
	{
		camera_id m_asset_id;
		transform m_transform;
		transform m_transform_original;
		float3 m_velocity;
	};
	camera m_camera;
	
	void assemble_scene(const contentman& cman, const stringview& filepath);

	actor& create_actor(const actor_create_args& create_args);

	bool find_role_actor(role role, uint32 index, actor*& out_actor_ptr);

public:
	void start(const contentman& cman);
	void tick(float seconds, float delta_seconds);
	void build_renderscene(const contentman& cman, renderscene& scene) const;
};
}