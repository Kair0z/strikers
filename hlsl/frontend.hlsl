
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

struct imgui_vertex
{
#ifdef __cplusplus
    float2 position;
    float2 uv;
    uint color;
#else
    float2 position : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR; // on gpu, uint is translated to r8g8b8a8
#endif
};

struct mesh_instance
{
    float4x4        transform;
    float4          color;
    uint            bitflags;
    gpu_optional    bone_instance_offset;
    gpu_optional    texid_basecolor;
    uint            padding;
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
    float4 color;
    float depth;
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

struct curve2D
{
    float2 m_points[3]; // quadratic bezier

    float2 p0() {
        return m_points[0];
    }
    float2 p1() {
        return m_points[1];
    }
    float2 cp0() {
        return m_points[2];
    }
};

struct glyph
{
    uint first_curve;
    uint num_curves;
};

struct glyph_instance
{
    uint glyph_index;
    float4 color;
    float3x3 inv_transform2D;
};

struct trail_point
{
    float4 position_and_width;
    float4 color;
    float age;
};

struct trail_instance
{
    uint num_points;
    uint first_point;
};

struct cbuffer_global
{
    float4x4 mat_to_lightspace;
    float4x4 mat_imgui_viewprojection;
    float4 light_color;
    float4 light_direction;
    float3 camera_position;
    gpu_optional texid_shadows;
    gpu_optional texid_bitmap_uav;
    gpu_optional texid_scenecolor_uav;
    gpu_optional texid_imgui_font;
    uint num_bone_instances;
    float4 outline_params; // a is enabled / not enabled
    float4 player_colors[8];
};

struct cbuffer_view
{
    float4x4 viewprojection;
    uint2 screen_size;
};

struct bitflags
{
    enum flags
    {
        none        = 0,
        player_ids  = (1 << 0) | (1 << 1) | (1 << 2), // 3 bits for 8 players
		outline     = (1 << 3),
	};

    static bool is_outlined(uint flgs)
    {
        return flgs & bitflags::outline;
    }

    static uint get_player_index(uint flgs)
    {
        return (flgs & bitflags::player_ids);
    }

    static uint make_player_index(uint index)
    {
        return (index & bitflags::player_ids);
    }
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

uint2 get_texture_size(Texture2D tex) {
    uint num_levels;
    uint width;
    uint height;
    tex.GetDimensions(0, width, height, num_levels);
    return uint2(width, height);
}

bool get_quad_vertex(uint vertex_id, out float4 out_position, out float2 out_uv) {

    [branch] switch(vertex_id) {
        case 0:
            out_position = float4(-1,-1,0,1);
            out_uv       = float2(0,1);
            return true;

        case 1:
            out_position = float4(-1,+1,0,1);
            out_uv       = float2(0,0);
            return true;
        case 2:
            out_position = float4(+1,+1,0,1);
            out_uv       = float2(1,0);
            return true;
        case 3:
            out_position = float4(+1,+1,0,1);
            out_uv       = float2(1,0);
            return true;
        case 4:
            out_position = float4(+1,-1,0,1);
            out_uv       = float2(1,1);
            return true;
        case 5:
            out_position = float4(-1,-1,0,1);
            out_uv       = float2(0,1);
            return true;
        default:
            return false;
    }  
}

#endif

#if __cplusplus
} // namespace hlsl
#endif