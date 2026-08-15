#include "common.h"

namespace strikers {
class renderscene;
class contentman;
class gameman
{
	struct pawn
	{
		transform m_transform;
		transform m_transform_original;
		mesh_id m_mesh;
		int m_team_id;
		float3 m_velocity;
	};

	vector<pawn> m_pawns;

	struct camera
	{
		transform m_transform;
		transform m_transform_original;
		float3 m_velocity;
	};
	camera m_camera;
	
	void assemble_scene(const contentman& cman, const stringview& filepath);

public:
	void start(const contentman& cman);
	void tick(float seconds, float delta_seconds);
	void tick_input();
	void build_renderscene(const contentman& cman, renderscene& scene) const;
};
}