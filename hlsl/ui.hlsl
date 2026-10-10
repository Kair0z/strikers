#include "frontend.hlsl"

// define BINDLESS root signature
#define ROOT_SIGNATURE \
    "RootFlags("\
        "CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED|"\
        "SAMPLER_HEAP_DIRECTLY_INDEXED),"\
    "SRV(t0)"

struct ps_input
{
    float4 position         : SV_POSITION;
    float4 color            : COLOR;
    float2 uv               : TEXCOORD0;
    uint texture_heap_id    : TEXCOORD1;
};
StructuredBuffer<ui_instance> t_instances : register(t0);

[Shader("vertex")]
ps_input main_vs(uint instance_id : SV_InstanceID, uint vertex_id : SV_VertexID)
{   
    ps_input output;
    if (!get_quad_vertex(vertex_id, output.position, output.uv))
        return output;
    
    ui_instance instance = t_instances[instance_id];
    output.texture_heap_id = instance.texture_heap_id;

    float2 position_uv = output.position.xy * 0.5f + 0.5f;
    position_uv.xy += instance.rect.xy;
    position_uv.xy *= instance.rect.zw;
    output.position.xy = (position_uv * 2) - 1;

    output.position.z = instance.depth;
    output.color = instance.color;
    return output;
}

[Shader("pixel")]
float4 main_ps(ps_input input) : SV_Target0
{   
    float4 color = input.color;
    if (input.texture_heap_id != k_invalid)
    {
        Texture2D texture = ResourceDescriptorHeap[input.texture_heap_id];
        const uint2 texsize = get_texture_size(texture);
        color *= texture.Load(int3(input.uv * texsize, 0));
    }
    
    return color;
}