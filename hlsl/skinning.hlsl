#define ROOT_SIGNATURE \
    "RootFlags("\
        "CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED|"\
        "ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT|"\
        "SAMPLER_HEAP_DIRECTLY_INDEXED),"\
    "CBV(b0),"\
    "SRV(t0),"\
    "SRV(t1),"\
    "UAV(u0)"

struct animation_track
{
    
};
struct keyframe
{
    
};
struct instance
{
    uint first_bone_index;
};
struct bone
{
    float4x4 inverse_bind;
    int parent;
};

cbuffer constants : register(b0)
{
    uint num_bones;
    uint num_instances;
};
StructuredBuffer<instance>      t_instances  : register(t0);
StructuredBuffer<bone>          t_bones      : register(t1);
RWStructuredBuffer<float4x4>    u_skinned_matrices : register(u0);

[Shader("compute")]
[numthreads(64, 1, 1)]
void main_cs(uint3 tid : SV_DispatchThreadID)
{
    const uint instance_idx = tid.x;
    if (instance_idx >= num_instances)
        return;
    
    for (uint b = 0; b < num_bones; ++b)
    {
        instance inst = t_instances[instance_idx];
        uint first_bone = inst.first_bone_index;
        
        float4x4 skinned_result = (float4x4)0;
        u_skinned_matrices[first_bone + b] = skinned_result;
    }
}