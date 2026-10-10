#define ROOT_SIGNATURE \
    "RootFlags("\
        "CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED|"\
        "SAMPLER_HEAP_DIRECTLY_INDEXED),"\
    "SRV(t0),"\
    "SRV(t1),"\
    "SRV(t2)"
#include "frontend.hlsl"

struct ps_input
{
    float4 position         : SV_POSITION;
    float4 color            : COLOR;
    float2 uv               : TEXCOORD0;
    uint glyph_id           : TEXCOORD1;
};
StructuredBuffer<glyph_instance>    t_instances    : register(t0);
StructuredBuffer<glyph>             t_glyphs       : register(t1);
StructuredBuffer<curve2D>           t_curves       : register(t2);

[Shader("vertex")]
ps_input main_vs(uint instance_id : SV_InstanceID, uint vertex_id : SV_VertexID)
{
    ps_input output;
    if (!get_quad_vertex(vertex_id, output.position, output.uv))
        return output;

    glyph_instance instance = t_instances[instance_id];
    output.glyph_id = instance.glyph_index;
    output.position.xy = mul( instance.inv_transform2D, float3(output.position.xy,1) ).xy;
    output.position.y *= (1280.0f / 720.0f);
    output.color = instance.color;
    return output;
}

uint count_curve_crossings(in float2 coordinate, in curve2D curve)
{
    coordinate.y = -coordinate.y;

    const float2 p0 = curve.p0();
    const float2 p1 = curve.p1();
    const float2 cp = curve.cp0();

    // y(t) = a*t*t + b*t + d
    float a = p0.y - 2.0 * cp.y + p1.y;
    float b = 2.0 * (cp.y - p0.y);
    float d = p0.y - coordinate.y;

    int crossings = 0;
    if (abs(a) < 1e-6)
    {
        if (abs(b) > 1e-6)
        {
            float t = -d / b;
            if (t >= 0.0 && t < 1.0)
            {
                float x = lerp(p0.x, p1.x, t);
                if (x > coordinate.x) crossings++;
            }
        }
        return crossings;
    }

    float disc = b*b - 4.0*a*d;
    if (disc < 0.0) return 0;

    float s = sqrt(disc);
    float roots[2] = {
        (-b - s) / (2.0*a),
        (-b + s) / (2.0*a)
    };

    for (int i = 0; i < 2; i++)
    {
        float t = roots[i];

        if (t >= 0.0 && t < 1.0)
        {
            float u = 1.0 - t;
            float x = u*u*p0.x
                    + 2.0*u*t*cp.x
                    + t*t*p1.x;

            if (x > coordinate.x) crossings++;
        }
    }

    return crossings;
}

[Shader("pixel")]
float4 main_ps(ps_input input) : SV_Target0
{   
    glyph glyph = t_glyphs[input.glyph_id];

    uint num_crossings = 0;
    for (uint i = 0; i < glyph.num_curves; ++i)
    {
        num_crossings += count_curve_crossings(input.uv * 2 - 1, t_curves[glyph.first_curve + i]);
    }
    clip(((num_crossings & 1) != 0) - 0.5f);

    return input.color;
}