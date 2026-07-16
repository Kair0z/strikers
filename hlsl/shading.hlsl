// define BINDLESS root signature
#define ROOT_SIGNATURE \
    "RootFlags("\
        "CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED|"\
        "ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT|"\
        "SAMPLER_HEAP_DIRECTLY_INDEXED),"\
    "CBV(b0),"\
    "SRV(t0)"

struct instance
{
    float4x4    transform;
    float4      color;
};
struct vs_input
{
    float3 position : SV_POSITION;
    float3 normal : NORMAL;
};
struct ps_input
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
    float3 normal : NORMAL0;
};

cbuffer constants : register(b0)
{
    float4x4 k_mat_viewprojection;
    float4 k_light_color;
    float4 k_light_direction;
};
StructuredBuffer<instance> t_instances : register(t0);

[Shader("vertex")]
ps_input main_vs(vs_input input, uint instance_id : SV_InstanceID, uint vertex_id : SV_VertexID)
{
    ps_input output;
    instance inst = t_instances[instance_id];
    output.position = mul(inst.transform, float4(input.position, 1));
    output.position = mul(k_mat_viewprojection, float4(output.position.xyz, 1));
    output.color = inst.color;
    output.normal = input.normal;
    return output;
}
[Shader("pixel")]
float4 main_ps(ps_input input) : SV_Target0
{
    const float3 ndotl = dot(input.normal, normalize(k_light_color));
    const float3 ambient = float3(1, 1, 1) * 0.0;
    const float3 diffuse = ndotl * input.color.rgb;
    return float4(saturate(diffuse + ambient), 1);
}