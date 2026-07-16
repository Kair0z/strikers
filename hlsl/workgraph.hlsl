
// define BINDLESS root signature
#define ROOT_SIGNATURE \
    "RootFlags("\
        "CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED|"\
        "SAMPLER_HEAP_DIRECTLY_INDEXED),"\
    "RootConstants(num32BitConstants=1, b0)"

cbuffer resource_ids : register(b0)
{
    uint id_uav_output;
};

[Shader("node")]
[NodeIsProgramEntry]
[NodeId("entry", 0)]
[NodeLaunch("thread")]
void main_entry(
    [MaxRecords(1)]
    [NodeId("worker")]
    EmptyNodeOutput output
)
{
    RWTexture2D<float4> uav_output = ResourceDescriptorHeap[id_uav_output];
    
    for (int y = 0; y < 400; ++y)
        for (int x = 0; x < 400; ++x)
            uav_output[uint2(x, y)] = float4(1, 1, 1, 1);

    // flag 1 active output
    output.ThreadIncrementOutputCount(1);
}

[Shader("node")]
[NodeId("worker", 0)]
[NodeLaunch("thread")]
void main_worker()
{
    
}