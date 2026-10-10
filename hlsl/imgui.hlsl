// define BINDLESS root signature
#define ROOT_SIGNATURE \
    "RootFlags("\
        "CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED|"\
        "ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT|"\
        "SAMPLER_HEAP_DIRECTLY_INDEXED),"\
    "CBV(b0),"\

#include "frontend.hlsl"

struct ps_input
{
    float4 position         : SV_POSITION;
    float4 color            : COLOR;
    float2 uv               : TEXCOORD0;
    uint texture_heap_id    : TEXCOORD1;
};

ConstantBuffer<cbuffer_global>      c_global            : register(b0);

[Shader("vertex")]
ps_input main_vs(imgui_vertex vertex)
{
    ps_input result = (ps_input)0;
    result.position = mul( c_global.mat_imgui_viewprojection, float4(vertex.position.xy, 0.0f, 1.0f));
    result.uv = vertex.uv;
    result.texture_heap_id = k_invalid;
    result.color = vertex.color;
    return result;
}

[Shader("pixel")]
float4 main_ps(ps_input input) : SV_Target0
{   
    if (c_global.texid_imgui_font != k_invalid) 
    {
        Texture2D tex_font = ResourceDescriptorHeap[c_global.texid_imgui_font];
        const uint2 size = get_texture_size(tex_font);
        const float2 sample_coord = input.uv * size;
        const float value = tex_font.Load(int3(sample_coord, 0)).x;
        clip(value);
    }
    return input.color;
}