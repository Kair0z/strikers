
#define ROOT_SIGNATURE \
    "RootFlags("\
        "CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED|"\
        "ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT|"\
        "SAMPLER_HEAP_DIRECTLY_INDEXED),"\
    "CBV(b0),"\
    "CBV(b1),"\
    "SRV(t0),"\
    "SRV(t1),"

#define MAX_TRAIL_POINTS 64
#include "frontend.hlsl"

ConstantBuffer<cbuffer_global>      c_global        : register(b0);
ConstantBuffer<cbuffer_view>        c_view          : register(b1);
StructuredBuffer<trail_point>       t_trailpoints   : register(t0);
StructuredBuffer<trail_instance>    t_trails        : register(t1);

struct ps_input
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
    float4 color    : COLOR0;
};

[outputtopology("triangle")]
[numthreads(MAX_TRAIL_POINTS, 1, 1)]
void main_ms(
    uint3 gid : SV_GroupID,
    uint3 gtid : SV_GroupThreadID,
    out vertices ps_input vertices[MAX_TRAIL_POINTS * 2],
    out indices uint3 triangles[(MAX_TRAIL_POINTS * 2) - 2])
{
    trail_instance trail = t_trails[gid.x];

    const uint num_points = min(trail.num_points, MAX_TRAIL_POINTS);
    if (num_points < 2)
    {
        // a ribbon needs at least 2 points
        return;
    }

    const uint num_vertices = 2 * num_points;
    const uint num_triangles =  2 * (num_points - 1);
    SetMeshOutputCounts(num_vertices, num_triangles);

    // each thread generates one pair of ribbon vertices.
    if (gtid.x < num_points)
    {
        uint i = gtid.x;
        uint prev_index = (i == 0) ? 0 : i - 1;
        uint next_index = min(i + 1, num_points - 1);

        // gather the points of our triangle
        trail_point p = t_trailpoints[trail.first_point + i];
        trail_point a = t_trailpoints[trail.first_point + prev_index];
        trail_point b = t_trailpoints[trail.first_point + next_index];

        const float3 position_b = b.position_and_width.xyz;
        const float3 position_a = a.position_and_width.xyz;
        const float3 position_p = p.position_and_width.xyz;

        const float3 a_to_b = position_b - position_a;
        const float a_to_b_length_sqr = dot(a_to_b, a_to_b);

        float3 tangent = float3(0,0,1);
        if (a_to_b_length_sqr > 1e-6) 
        {
            tangent = normalize(a_to_b);
        }

        // ribbon faces camera
        const float3 camera_position = c_global.camera_position;
        const float3 viewdir = float3(0,-1,0); //normalize(camera_position - position_p);
        float3 side = cross(tangent, viewdir);

        // fallback for near-parallel tangent and view direction.
        if (dot(side, side) < 1e-6)
        {
            side = cross(tangent, float3(0, 1, 0));
            if (dot(side, side) < 1e-6)
            {
                side = cross(tangent, float3(1, 0, 0));
            }
        }
        side = normalize(side);

        const float width_p = p.position_and_width.w / max(p.age, 0.0001);
        const float3 left  = position_p - side * (width_p * 0.5);
        const float3 right = position_p + side * (width_p * 0.5);

        ps_input v0;
        v0.position = mul(c_view.viewprojection, float4(left, 1.0));
        v0.uv = float2(0.0, (float)i / (num_points - 1));
        v0.color = p.color;

        ps_input v1;
        v1.position = mul(c_view.viewprojection, float4(right, 1.0));
        v1.uv = float2(1.0, (float)i / (num_points - 1));
        v1.color = p.color;

        vertices[2 * i] = v0;
        vertices[2 * i + 1] = v1;
    }

    // Each thread creates the two triangles for one segment.
    if (gtid.x < num_points - 1)
    {
        const uint i = gtid.x;
        const uint base = 2 * i;
        triangles[2 * i] = uint3(base, base + 1, base + 2);
        triangles[2 * i + 1] = uint3(base + 1, base + 3, base + 2);
    }
}

float4 main_ps(ps_input input) : SV_Target0
{
    // const float2 signed_uv = input.uv * 2 - 1;
    // clip(1 - dot(signed_uv,signed_uv) - 0.5);
    return input.color;
}