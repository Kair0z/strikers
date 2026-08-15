// define BINDLESS root signature
#define ROOT_SIGNATURE \
    "RootFlags("\
        "CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED|"\
        "SAMPLER_HEAP_DIRECTLY_INDEXED),"\
    "CBV(b0),"\
    "SRV(t0)"

struct instance
{
    float4 rect;
    uint texture_heap_id;
};
struct vs_input
{
    uint vertex_id : SV_VertexID;
};
struct ps_input
{
    float4 position         : SV_POSITION;
    float2 uv               : TEXCOORD0;
    uint texture_heap_id    : TEXCOORD1;
};

cbuffer constants : register(b0)
{
};
StructuredBuffer<instance> t_instances : register(t0);

[Shader("vertex")]
ps_input main_vs(uint instance_id : SV_InstanceID, uint vertex_id : SV_VertexID)
{   
    ps_input output;
    if (vertex_id == 0)
        output.position = float4(-1,-1,0,1),
        output.uv       = float2(0,1);
    else if (vertex_id == 1)
        output.position = float4(-1,+1,0,1),
        output.uv       = float2(0,0);
    else if (vertex_id == 2)
        output.position = float4(+1,+1,0,1),
        output.uv       = float2(1,0);
    else if (vertex_id == 3)
        output.position = float4(+1,+1,0,1),
        output.uv       = float2(1,0);
    else if (vertex_id == 4)
        output.position = float4(+1,-1,0,1),
        output.uv       = float2(1,1);
    else if (vertex_id == 5)
        output.position = float4(-1,-1,0,1),
        output.uv       = float2(0,1);
    
    output.texture_heap_id = t_instances[instance_id].texture_heap_id;
    return output;
}

[Shader("pixel")]
float4 main_ps(ps_input input) : SV_Target0
{
    const int2 dimensions = int2(1280, 720);
    int2 location = input.uv * dimensions * 2;
    
    Texture2D texture = ResourceDescriptorHeap[input.texture_heap_id];
    
    float4 loaded_sample = texture.Load(int3(location, 0));
    return loaded_sample.r > 0 ? 1 : 0;
}