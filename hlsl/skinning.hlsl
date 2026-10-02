#define ROOT_SIGNATURE \
    "RootFlags("\
        "CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED|"\
        "ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT|"\
        "SAMPLER_HEAP_DIRECTLY_INDEXED),"\
    "CBV(b0),"\
    "SRV(t0),"\
    "SRV(t1),"\
    "SRV(t2),"\
    "UAV(u0)"\

#include "frontend.hlsl"

ConstantBuffer<cbuffer_global> c_global : register(b0);
StructuredBuffer<bone> t_bones : register(t0);
StructuredBuffer<animation_channel> t_animation_channels : register(t1);
StructuredBuffer<keyframe> t_keyframes : register(t2);
RWStructuredBuffer<bone_instance> u_bone_instances : register(u0);

bool is_valid_bone_instance_idx(uint bone_instance_idx)
{
    return bone_instance_idx < c_global.num_bone_instances;
}

float4x4 quaternion_to_matrix(float4 q)
{
    // assuming quaternion wxyz
    float w = q.x;
    float x = q.y;
    float y = q.z;
    float z = q.w;

    return float4x4(
        1 - 2*(y*y + z*z),  2*(x*y - z*w),      2*(x*z + y*w),      0,
        2*(x*y + z*w),      1 - 2*(x*x + z*z),  2*(y*z - x*w),      0,
        2*(x*z - y*w),      2*(y*z + x*w),      1 - 2*(x*x + y*y),  0,
        0,                  0,                  0,                  1
    );
}

float4x4 keyframe_to_matrix(keyframe k)
{
    float4x4 rotation = quaternion_to_matrix(k.rotation);
    rotation[0].xyz *= k.scale.x;
    rotation[1].xyz *= k.scale.y;
    rotation[2].xyz *= k.scale.z;
    rotation[3] = float4(k.position, 1.0);
    return rotation;
}

float4x4 get_bone_local_transform(uint bone_instance_idx)
{
    const uint bone_index = u_bone_instances[bone_instance_idx].bone_index;
    const uint anim_channel_index = u_bone_instances[bone_instance_idx].anim_channel_index;
    #if 0
    if (anim_channel_index != k_invalid)
    {
        const uint first_keyframe = t_animation_channels[anim_channel_index].first_keyframe;
        const uint num_keyframes = t_animation_channels[anim_channel_index].num_keyframes;

        const float time = sin(u_bone_instances[bone_instance_idx].anim_time) * 0.5 + 0.5;
        const uint current_keyframe_idx = round(lerp(first_keyframe, first_keyframe + num_keyframes - 1, time));

        const keyframe current_keyframe = t_keyframes[current_keyframe_idx];
        return mul(t_bones[bone_index].transform, keyframe_to_matrix(current_keyframe));
    }
    #endif

    return t_bones[bone_index].transform;
}

[Shader("compute")] 
[numthreads(64, 1, 1)]
void main_cs(uint3 tid : SV_DispatchThreadID)
{
    uint bone_instance_idx = tid.x;
    if (!is_valid_bone_instance_idx(bone_instance_idx))
    {
        return;
    }

    // get current bone's local transform
    float4x4 resolved_transform = get_bone_local_transform(bone_instance_idx);
    
    // traverse the bone parent hierarchy
    const uint bone_index = u_bone_instances[bone_instance_idx].bone_index;
    int parent_bone_index = t_bones[bone_index].parent;
    while (parent_bone_index != k_invalid)
    {
        // we can calculate the parent bone instance by mapping the distance parent-current onto the current instance  
        const uint parent_delta = bone_index - parent_bone_index;
        const uint parent_instance_index = bone_instance_idx - parent_delta;

        resolved_transform = mul(get_bone_local_transform(parent_instance_index), resolved_transform);
        parent_bone_index = t_bones[parent_bone_index].parent;
    }

    // output resolved matrix
    u_bone_instances[bone_instance_idx].skinned_matrix = mul(resolved_transform, t_bones[bone_index].inverse_bind);
}