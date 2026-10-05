
#ifdef __cplusplus
namespace hlsl {
using uint = uint32;
using uint2 = strikers::uint2;
struct gpu_optional // default initializes -1
{
	int32 m_value = -1;
	void set_null() { m_value = -1; }
	void set(int value) { m_value = value; }

	gpu_optional& operator=(int32 value) {
		set(value);
		return *this;
	}

	int32 get() const { return m_value; }
};
#else 
typedef int gpu_optional;
#endif

static const int k_invalid = -1;

struct mesh_vertex
{
#ifdef __cplusplus
    float3 position;
    float3 normal;
    float2 uv;
    uint4 bone_ids;
    float4 bone_weights;
#else
    float3 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    uint4 bone_ids : BLENDINDICES0;
    float4 bone_weights : BLENDWEIGHT0;
#endif
};

struct mesh_instance
{
    float4x4        transform;
    float4          color;
    gpu_optional    bone_instance_offset;
    gpu_optional    texid_basecolor;
    uint            bitflags;
};

// each skeleton is an array of bones, and each skeleton can have multiple instances
// so the bone struct contains that all instances share...
struct bone
{
    float4x4 transform;
    float4x4 inverse_bind;
    gpu_optional parent; // root will have no parent
};

// ... and each bone instance has a link to their shared bone info,
// as well as per-instance params for animations etc..
struct bone_instance
{
    uint bone_index;
    float anim_time; // processed as [0,1]
    gpu_optional anim_channel_index;

    // the skinning compute pass writes this as a result of all skinning input
    float4x4 skinned_matrix;
};

struct quad_instance
{
    float4x4 transform;
    float4 color;
    float4 rect_uv;
    gpu_optional texid_color;
};

struct line_instance
{
    float4 color;
    float3 point_a;
    float3 point_b;
};

struct ui_instance
{
    float4 rect;
    gpu_optional texture_heap_id;
};

struct keyframe
{
    float4 rotation; // quaternion
    float3 position;
    float3 scale;
};

struct animation_channel
{
    uint first_keyframe;
    uint num_keyframes;
};

struct cbuffer_global
{
    float4x4 mat_to_lightspace;
    float4 light_color;
    float4 light_direction;
    gpu_optional texid_shadows;
    gpu_optional texid_bitmap_uav;
    gpu_optional texid_scenecolor_uav;
    uint num_bone_instances;
    float4 outline_params; // a is enabled / not enabled
};

struct cbuffer_view
{
    float4x4 viewprojection;
    uint2 screen_size;
};

#ifndef __cplusplus
float4 calculate_skinned_position(
    StructuredBuffer<bone_instance> bone_instances,
    float3 in_position,
    uint bone_instance_offset,
    uint4 bone_idxs,
    float4 bone_weights)
{
    return
        bone_weights.x * mul(bone_instances[bone_instance_offset + bone_idxs.x].skinned_matrix, float4(in_position.xyz, 1)) +
        bone_weights.y * mul(bone_instances[bone_instance_offset + bone_idxs.y].skinned_matrix, float4(in_position.xyz, 1)) +
        bone_weights.z * mul(bone_instances[bone_instance_offset + bone_idxs.z].skinned_matrix, float4(in_position.xyz, 1)) +
        bone_weights.w * mul(bone_instances[bone_instance_offset + bone_idxs.w].skinned_matrix, float4(in_position.xyz, 1));
}

enum bitflags
{
    none = 0,
    outline = (1 << 0)
};
#endif

#if __cplusplus
} // namespace hlsl
#endif