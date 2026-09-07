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
    uint        tex_basecolor_idx;
};
struct vs_input
{
    float3 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    uint4 bone_ids : BLENDINDICES0;
    float4 bone_weights : BLENDWEIGHT0;
};
cbuffer cbuffer_view : register(b0)
{
    float4x4 k_viewprojection;
};
StructuredBuffer<instance> t_instances : register(t0);

[Shader("vertex")]
float4 main_vs(
    vs_input vertex, 
    uint instance_id : SV_InstanceID, 
    uint start_instance_id : SV_StartInstanceLocation) : SV_POSITION
{
    float4 out_position;
    instance instance = t_instances[instance_id + start_instance_id];
    out_position = mul(instance.transform, float4(vertex.position, 1));
    out_position = mul(k_viewprojection, float4(out_position.xyz, 1));
    return out_position;
}