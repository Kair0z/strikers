#define ROOT_SIGNATURE \
    "RootFlags("\
        "CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED|"\
        "ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT|"\
        "SAMPLER_HEAP_DIRECTLY_INDEXED),"\
    "SRV(t0),"\
    "UAV(u0)"\

static const int k_invalid = -1;
struct bone
{
    int parent; // -1 is invalid
    float4x4 transform;
    float4x4 inverse_bind;
};
struct bone_instance
{
    uint bone_index;
    int anim_index;
    float anim_time; // normalized (not seconds)
    float4x4 skinned_matrix;
};
struct keyframe
{
    float4 rotation; // quaternion
    float3 position;
    float3 scale;
};
struct animation
{
    uint first_keyframe;
    uint num_keyframes;
};

StructuredBuffer<bone> t_bones : register(t0);
StructuredBuffer<animation> t_animations : register(t1);
StructuredBuffer<keyframe> t_keyframes : register(t2);
RWStructuredBuffer<bone_instance> u_bone_instances : register(u0);

float4x4 get_bone_local_transform(uint bone_instance_idx)
{
    const uint bone_index = u_bone_instances[bone_instance_idx].bone_index;
    const uint anim_index = u_bone_instances[bone_instance_idx].anim_index;
    if (anim_index != k_invalid)
    {
        return t_bones[bone_index].transform;
    }
    else 
    {
        const float4x4 base_transform = t_bones[bone_index].transform;
        const uint first_keyframe = t_animations[anim_index].first_keyframe;
        const uint num_keyframes = t_animations[anim_index].num_keyframes;
        const float time = u_bone_instances[bone_instance_idx].anim_time;

        const uint current_keyframe_idx = round(lerp(first_keyframe, num_keyframes - 1, time));
        const keyframe current_keyframe = t_keyframes[current_keyframe_idx];
        return base_transform;
    }
}

[Shader("compute")] 
[numthreads(64, 1, 1)]
void main_cs(uint3 tid : SV_DispatchThreadID)
{
    uint bone_instance_idx = tid.x;
    
    // resolve bone matrix graph
    float4x4 resolved_transform = get_bone_local_transform(bone_instance_idx);
    
    // bone instance -> bone
    const uint bone_index = u_bone_instances[bone_instance_idx].bone_index;
    int parent_bone_index = t_bones[bone_index].parent;

    while (parent_bone_index != k_invalid)
    {
        // here we can figure out the parent_instance index: (get_bone_local_transform() needs it)
        const uint parent_delta = bone_index - parent_bone_index; 
        const uint parent_instance_index = bone_instance_idx - parent_delta;

        resolved_transform = mul(get_bone_local_transform(parent_instance_index), resolved_transform);
        parent_bone_index = t_bones[parent_bone_index].parent;
    }

    // output resolved matrix
    u_bone_instances[bone_instance_idx].skinned_matrix = mul(resolved_transform, t_bones[bone_index].inverse_bind);
}