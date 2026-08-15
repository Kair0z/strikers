// define BINDLESS root signature
#define ROOT_SIGNATURE \
    "RootFlags("\
        "CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED|"\
        "ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT|"\
        "SAMPLER_HEAP_DIRECTLY_INDEXED),"\
    "CBV(b0),"\
    "SRV(t0),"\
    "SRV(t1)"

struct instance
{
    float4x4    transform;
    float4      color;
    uint        tex_basecolor_idx;
};
struct bone
{
    float4x4 transform;
};
struct vs_input
{
    float3 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    uint4 bone_ids : BLENDINDICES0;
    float4 bone_weights : BLENDWEIGHT0;
};
struct ps_input
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
    float3 normal : NORMAL0;
    float2 uv : TEXCOORD0;
    uint tex_basecolor_idx : TEXCOORD1;
};

cbuffer constants : register(b0)
{
    float4x4 k_mat_viewprojection;
    float4 k_light_color;
    float4 k_light_direction;
};
StructuredBuffer<instance> t_instances  : register(t0);
StructuredBuffer<bone> t_bones          : register(t1);

[Shader("vertex")]
ps_input main_vs(vs_input input, uint instance_id : SV_InstanceID, uint start_instance_id : SV_StartInstanceLocation, uint vertex_id : SV_VertexID)
{
    instance instance = t_instances[instance_id + start_instance_id];
    
    ps_input output;
    output.position = mul(instance.transform, float4(input.position, 1));
    output.position = mul(k_mat_viewprojection, float4(output.position.xyz, 1));
    output.color = instance.color;
    output.normal = normalize(mul((float3x3) instance.transform, input.normal));
    output.uv = input.uv;
    output.tex_basecolor_idx = instance.tex_basecolor_idx;
    return output;
}

[Shader("pixel")]
float4 main_ps(ps_input input) : SV_Target0
{
    const float3 ndotl = max(0, dot(input.normal, normalize(k_light_direction.xyz)));
    const float3 ambient = float3(1, 1, 1) * 0.005;
    const float3 diffuse = ndotl;
    
    const bool has_texture = input.tex_basecolor_idx != 0;
    Texture2D tex_basecolor = ResourceDescriptorHeap[input.tex_basecolor_idx];
    
    uint width, height, num_levels;
    tex_basecolor.GetDimensions(0, width, height, num_levels);
    
    const float2 uv = clamp(input.uv, float2(0.01, 0.01), float2(0.99, 0.99));
    const float3 basecolor = has_texture ? tex_basecolor[uv * float2(width, height)] : float3(0.9, 0.2, 0.2);
    return float4(saturate((basecolor * diffuse) + ambient), 1);
}