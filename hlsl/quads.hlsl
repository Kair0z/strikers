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
    float2 uv               : TEXCOORD0;
    uint texid_color      : TEXCOORD1;
};

ConstantBuffer<cbuffer_global>  c_global    : register(b0);
ConstantBuffer<cbuffer_view>    c_view      : register(b1);
StructuredBuffer<quad_instance> t_instances : register(t0);

[Shader("vertex")]
ps_input main_vs(uint instance_id : SV_InstanceID, uint vertex_id : SV_VertexID)
{
    quad_instance inst = t_instances[instance_id];
    const float2 rect_uv_min = inst.rect_uv.xy;
    const float2 rect_uv_max = float2(1,1) - inst.rect_uv.zw;
    ps_input output;
    if (vertex_id == 0)
        output.position = float4(-1,-1,0,1),
        output.uv       = float2(rect_uv_min.x,rect_uv_max.y);
    else if (vertex_id == 1)
        output.position = float4(-1,+1,0,1),
        output.uv       = float2(rect_uv_min.x,rect_uv_min.y);
    else if (vertex_id == 2)
        output.position = float4(+1,+1,0,1),
        output.uv       = float2(rect_uv_max.x,rect_uv_min.y);
    else if (vertex_id == 3)
        output.position = float4(+1,+1,0,1),
        output.uv       = float2(rect_uv_max.x,rect_uv_min.y);
    else if (vertex_id == 4)
        output.position = float4(+1,-1,0,1),
        output.uv       = float2(rect_uv_max.x,rect_uv_max.y);
    else if (vertex_id == 5)
        output.position = float4(-1,-1,0,1),
        output.uv       = float2(rect_uv_min.x,rect_uv_max.y);
    
    output.position = mul(inst.transform, output.position);
    output.position = mul(c_view.viewprojection, output.position);
    output.color = inst.color;
    output.texid_color = inst.texid_color;
    return output;
}

[Shader("pixel")]
float4 main_ps(ps_input input) : SV_Target0
{
    const bool tex_valid = input.texid_color != 0; 
    Texture2D tex_color = ResourceDescriptorHeap[input.texid_color];
    uint width, height, num_levels;
    tex_color.GetDimensions(0, width, height, num_levels);

    float2 coordinate = input.uv * float2(width, height);
    float4 result = input.color;
    if (tex_valid) result *= tex_color[coordinate];

    return result;
}