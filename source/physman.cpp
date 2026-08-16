#include "physman.h"
#pragma comment(lib, "box3dd.lib")

#include "renderman.h"
#include "commandman.h"

namespace strikers {

command cm_phys_debug_disable("phys.debug.disable", "1");
command cm_phys_timestep("phys.timestep", "10");

b3Pos translate(const float3& f3)
{
	return { f3.x, f3.y, f3.z };
}
float3 translate(const b3Pos& f3)
{
	return { f3.x, f3.y, f3.z };
}
rotation translate(const b3Quat& f4)
{
	return { f4.v.x, f4.v.y, f4.v.z, f4.s };
}
b3Quat translate(const rotation& f4)
{
	return { f4.w, f4.x, f4.y, f4.z };
}
transform translate(const b3WorldTransform& trans)
{
	return transform::build(
		translate(trans.p),
		translate(trans.q),
		{ 1,1,1 });
}
void physman::initialize()
{
	b3WorldDef world_def = b3DefaultWorldDef();
	world_def.gravity = { 0, -1, 0 };
	m_world = b3CreateWorld(&world_def);
}

void physman::tick()
{
	if (cm_phys_debug_disable.get_value() > 0)
		return;

	float timeStep = 1.0f / 60.0f;
	int num_substeps = 4;
	for (uint32 i = 0u; i < cm_phys_timestep.get_value<int>(); ++i)
	{
		b3World_Step(m_world, timeStep, num_substeps);
	}
}

physman::body_id physman::create_body(const create_body_args& args)
{
	body new_body{};
	b3BodyDef def = b3DefaultBodyDef();
	def.type = b3_staticBody;

	if ((uint32)args.m_flags & (uint32)body_flags::dynamic)
	{
		def.type = b3_dynamicBody;
	}
	if ((uint32)args.m_flags & (uint32)body_flags::kinematic)
	{
		def.type = b3_kinematicBody;
	}
	def.position = translate(args.m_transform.get_position());
	def.rotation = translate(args.m_transform.get_rotation());
	
	const body_id id = b3CreateBody(m_world, &def);
	
	// build the shape
	{
		b3ShapeDef shape_def = b3DefaultShapeDef();
		shape_def.density = 1;
		shape_def.baseMaterial.friction = 0.3f;

		switch (args.m_shape)
		{
		case body_shape::cube:
		{
			b3HullData* hull = nullptr;
			b3BoxHull box_hull;
			box_hull = b3MakeCubeHull(1);
			hull = &box_hull.base;
			new_body.m_shape = b3CreateHullShape(id, &shape_def, hull);
		} break;

		case body_shape::sphere:
		{
			b3Sphere sphere{};
			sphere.center = def.position;
			sphere.radius = 2.0f;
			new_body.m_shape = b3CreateSphereShape(id, &shape_def, &sphere);
		} break;
		}
	}
	
	new_body.m_id = id;
	new_body.m_create_args = args;
	m_bodies.push_back(new_body);
	m_body_id_to_index[id] = m_bodies.size() - 1u;
	return id;
}

bool physman::query_body_transform(const body_id id, transform& out_transform) const
{
	auto found = m_body_id_to_index.find(id);
	if (found != m_body_id_to_index.cend())
	{
		b3Vec3 position = b3Body_GetPosition(id);
		b3Quat rotation = b3Body_GetRotation(id);
		out_transform = transform::build(translate(position), translate(rotation), out_transform.get_scale());
		return true;
	}
	else return false;
}

void physman::set_body_transform(const body_id id, const transform& transform) const
{
	auto found = m_body_id_to_index.find(id);
	if (found != m_body_id_to_index.cend())
	{
		b3Body_SetTransform(id, 
			translate(transform.get_position()), 
			translate(transform.get_rotation()));
	}
}

void physman::body_apply_force(const body_id id, const force_args& args)
{
	auto found = m_body_id_to_index.find(id);
	if (found != m_body_id_to_index.cend())
	{
		if (args.m_type == force_type::force)
		{
			if (args.m_position.has_value())
			{
				b3Body_ApplyForce(id,
					translate(args.m_force),
					translate(args.m_position.value()),
					args.m_wake);
			}
			else
			{
				b3Body_ApplyForceToCenter(id,
					translate(args.m_force),
					args.m_wake);
			}
		}
		else if (args.m_type == force_type::impulse_linear)
		{
			if (args.m_position.has_value())
			{
				b3Body_ApplyLinearImpulse(id,
					translate(args.m_force),
					translate(args.m_position.value()),
					args.m_wake);
			}
			else
			{
				b3Body_ApplyLinearImpulseToCenter(id,
					translate(args.m_force),
					args.m_wake);
			}
		}
		else if (args.m_type == force_type::impulse_angular)
		{
			b3Body_ApplyAngularImpulse(id,
				translate(args.m_force),
				args.m_wake);
		}
	}
}

void physman::debug_draw(const db_draw& args, renderscene& scene)
{
	b3DebugDraw debug_draw = b3DefaultDebugDraw();
	
	debug_draw.DrawShapeFcn = [](void* shape, b3WorldTransform transform, b3HexColor hex_color, void* context)
	{
		renderscene& scene = (*(renderscene*)context);
	};
	debug_draw.DrawSegmentFcn = [](b3Pos p1, b3Pos p2, b3HexColor color, void* context)
	{
		renderscene& scene = (*(renderscene*)context);
	};
	debug_draw.DrawTransformFcn = [](b3WorldTransform transform, void* context)
	{
		renderscene& scene = (*(renderscene*)context);
	};
	debug_draw.DrawPointFcn = [](b3Pos p, float size, b3HexColor color, void* context)
	{
		renderscene& scene = (*(renderscene*)context);
	};
	debug_draw.DrawSphereFcn = [](b3Pos p, float radius, b3HexColor color, float alpha, void* context) 
	{
		renderscene& scene = (*(renderscene*)context);
		renderscene::line_builder lines{ scene };

		transform trans = transform::build(
			translate(p),
			rotation{},
			float3(1, 1, 1));
		lines.add_sphere(trans, radius, float4(1,1,1,1));
	};
	debug_draw.DrawCapsuleFcn = [](b3Pos p1, b3Pos p2, float radius, b3HexColor color, float alpha, void* context)
	{
		renderscene& scene = (*(renderscene*)context);
	};
	debug_draw.DrawBoundsFcn = [](b3AABB aabb, b3HexColor color, void* context)
	{
		renderscene& scene = (*(renderscene*)context);
		renderscene::line_builder lines{ scene };
		
		transform trans = transform::identity();
		trans.set_scale(100);
		lines.add_box(trans,
			translate(aabb.lowerBound), 
			translate(aabb.upperBound),
			{1,1,1,1});
	};
	debug_draw.DrawBoxFcn = [](b3Vec3 extents, b3WorldTransform transform, b3HexColor color, void* context)
	{
		renderscene& scene = (*(renderscene*)context);
		
	};
	debug_draw.DrawStringFcn = [](b3Pos p, const char* s, b3HexColor color, void* context)
	{
		renderscene& scene = (*(renderscene*)context);
	};
	
	debug_draw.drawingBounds.lowerBound *= 1000.0f;
	debug_draw.drawingBounds.upperBound *= 1000.0f;
	debug_draw.context = &scene;
	debug_draw.drawShapes = args.m_flags & db_draw::draw_shapes;
	debug_draw.drawJoints = args.m_flags & db_draw::draw_joints;
	debug_draw.drawJointExtras = args.m_flags & db_draw::draw_jointextras;
	debug_draw.drawBounds = args.m_flags & db_draw::draw_bounds;
	debug_draw.drawMass = args.m_flags & db_draw::draw_mass;
	debug_draw.drawSleep = args.m_flags & db_draw::draw_sleep;
	debug_draw.drawBodyNames = args.m_flags & db_draw::draw_bodynames;
	debug_draw.drawContacts = args.m_flags & db_draw::draw_contacts;
	debug_draw.drawAnchorA = args.m_flags & db_draw::draw_anchorA;
	debug_draw.drawGraphColors = args.m_flags & db_draw::draw_graphcolors;
	debug_draw.drawContactFeatures = args.m_flags & db_draw::draw_contactfeatures;
	debug_draw.drawContactNormals = args.m_flags & db_draw::draw_contactnormals;
	debug_draw.drawContactForces = args.m_flags & db_draw::draw_contactforces;
	debug_draw.drawIslands = args.m_flags & db_draw::draw_islands;

	const uint64 mask = UINT64_MAX;
	b3World_Draw(m_world, &debug_draw, mask);
}
physman::force_args physman::force_args::force(const float3& force)
{
	force_args args{};
	args.m_force = force;
	args.m_type = force_type::force;
	return args;
}
physman::force_args physman::force_args::force_at_point(const float3& force, const float3& point)
{
	force_args args{};
	args.m_force = force;
	args.m_type = force_type::force;
	args.m_position = point;
	return args;
}
physman::force_args physman::force_args::impulse(const float3& force)
{
	force_args args{};
	args.m_force = force;
	args.m_type = force_type::impulse_linear;
	return args;
}
physman::force_args physman::force_args::impulse_at_point(const float3& force, const float3& point)
{
	force_args args{};
	args.m_force = force;
	args.m_type = force_type::impulse_linear;
	args.m_position = point;
	return args;
}
physman::force_args physman::force_args::angular_impulse(const float3& force)
{
	force_args args{};
	args.m_force = force;
	args.m_type = force_type::impulse_angular;
	return args;
}
}