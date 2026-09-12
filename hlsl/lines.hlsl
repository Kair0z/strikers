// define BINDLESS root signature
#define ROOT_SIGNATURE \
    "RootFlags("\
        "CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED|"\
        "SAMPLER_HEAP_DIRECTLY_INDEXED),"\
    "CBV(b0),"\
    "CBV(b1),"\
    "SRV(t0)"

struct instance
{
    float4 color;
    float3 point_a;
    float3 point_b;
};
struct ps_input
{
    float4 position         : SV_POSITION;
    float4 color            : COLOR0;
};

cbuffer cbuffer_global : register(b0)
{
    float4x4 k_mat_to_lightspace;
    float4 k_light_color;
    float4 k_light_direction;
    uint k_texid_shadows;
};
cbuffer cbuffer_view : register(b1)
{
    float4x4 k_viewprojection;
};
StructuredBuffer<instance> t_instances : register(t0);

[Shader("vertex")]
ps_input main_vs(uint instance_id : SV_InstanceID, uint vertex_id : SV_VertexID)
{
    ps_input output;
    instance inst = t_instances[instance_id];
    output.position = (vertex_id == 0 ? float4(inst.point_a,1) : float4(inst.point_b,1));
    output.position = mul(k_viewprojection, output.position);
    output.color = inst.color;
    return output;
}

[Shader("pixel")]
float4 main_ps(ps_input input) : SV_Target0
{
    return input.color;
}