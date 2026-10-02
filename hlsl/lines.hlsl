// define BINDLESS root signature
#define ROOT_SIGNATURE \
    "RootFlags("\
        "CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED|"\
        "SAMPLER_HEAP_DIRECTLY_INDEXED),"\
    "CBV(b0),"\
    "CBV(b1),"\
    "SRV(t0)"

#include "frontend.hlsl"

struct ps_input
{
    float4 position         : SV_POSITION;
    float4 color            : COLOR0;
};

ConstantBuffer<cbuffer_global>  c_global    : register(b0);
ConstantBuffer<cbuffer_view>    c_view      : register(b1);
StructuredBuffer<line_instance> t_instances : register(t0);

[Shader("vertex")]
ps_input main_vs(uint instance_id : SV_InstanceID, uint vertex_id : SV_VertexID)
{
    ps_input output;
    line_instance inst = t_instances[instance_id];
    output.position = (vertex_id == 0 ? float4(inst.point_a,1) : float4(inst.point_b,1));
    output.position = mul(c_view.viewprojection, output.position);
    output.color = inst.color;
    return output;
}

[Shader("pixel")]
float4 main_ps(ps_input input) : SV_Target0
{
    return input.color;
}