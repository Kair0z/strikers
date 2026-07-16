#include "common.h"

namespace strikers {
class renderscene;
class gameman
{
	struct ball
	{
		transform m_transform;
	};
	struct pawn
	{
		transform m_transform;
		mesh_id m_mesh;
		int m_team_id;
	};

	vector<pawn> m_pawns;
	ball m_ball;
	float3 m_camera_position;
	float3 m_camera_target;

public:
	void start();
	void tick(float seconds, float delta_seconds);
	void tick_input();
	void build_renderscene(renderscene& scene) const;
};
}