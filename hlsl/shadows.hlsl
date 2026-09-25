// define BINDLESS root signature
#define ROOT_SIGNATURE \
    "RootFlags("\
        "CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED|"\
        "ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT|"\
        "SAMPLER_HEAP_DIRECTLY_INDEXED),"\
    "CBV(b0),"\
    "SRV(t0),"\
    "SRV(t1)"

static const int k_invalid = -1;

struct instance
{
    float4x4    transform;
    float4      color;
    int         bone_instance_offset;
    int         texid_basecolor;
};
struct bone_instance
{
    uint bone_index;
    int anim_index;
    float anim_time; // normalized (not seconds)
    float4x4 skinned_matrix;
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
StructuredBuffer<instance>          t_instances : register(t0);
StructuredBuffer<bone_instance>     t_bone_instances    : register(t1);

[Shader("vertex")]
float4 main_vs(
    vs_input vertex, 
    uint instance_id : SV_InstanceID, 
    uint start_instance_id : SV_StartInstanceLocation) : SV_POSITION
{
    float4 out_position;
    instance instance = t_instances[instance_id + start_instance_id];
    
    float4 position_os = float4(vertex.position.xyz, 1);
    if (instance.bone_instance_offset != k_invalid)
    {        
        const float4 skinned_position = 
            vertex.bone_weights.x * mul(t_bone_instances[instance.bone_instance_offset + vertex.bone_ids.x].skinned_matrix, float4(position_os.xyz, 1)) +
            vertex.bone_weights.y * mul(t_bone_instances[instance.bone_instance_offset + vertex.bone_ids.y].skinned_matrix, float4(position_os.xyz, 1)) +
            vertex.bone_weights.z * mul(t_bone_instances[instance.bone_instance_offset + vertex.bone_ids.z].skinned_matrix, float4(position_os.xyz, 1)) +
            vertex.bone_weights.w * mul(t_bone_instances[instance.bone_instance_offset + vertex.bone_ids.w].skinned_matrix, float4(position_os.xyz, 1));
        
        position_os = skinned_position;
    }

    out_position = mul(instance.transform, float4(position_os.xyz, 1)); // now ws (worldspace)
    out_position = mul(k_viewprojection, float4(out_position.xyz, 1)); // now ls (lightspace)
    return out_position;
}