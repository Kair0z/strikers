// define BINDLESS root signature
#define ROOT_SIGNATURE \
    "RootFlags("\
        "CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED|"\
        "ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT|"\
        "SAMPLER_HEAP_DIRECTLY_INDEXED),"\
    "CBV(b0),"\
    "CBV(b1),"\
    "SRV(t0),"\
    "SRV(t1),"
#include "frontend.hlsl"

struct ps_input
{
    float4 position_cs : SV_POSITION;
    float4 position_ws : POSITION;
    float4 color : COLOR0;
    float3 normal_ws : NORMAL0;
    float2 uv : TEXCOORD0;
    int texid_basecolor : TEXCOORD1;
    uint bitflags : TEXCOORD2;
};
ConstantBuffer<cbuffer_global>      c_global            : register(b0);
ConstantBuffer<cbuffer_view>        c_view              : register(b1);
StructuredBuffer<mesh_instance>     t_instances         : register(t0);
StructuredBuffer<bone_instance>     t_bone_instances    : register(t1);

[Shader("vertex")]
ps_input main_vs(mesh_vertex input, uint instance_id : SV_InstanceID, uint start_instance_id : SV_StartInstanceLocation, uint vertex_id : SV_VertexID)
{
    ps_input output;
    mesh_instance instance = t_instances[instance_id + start_instance_id];
     
    float4 position_os = float4(input.position.xyz, 1);
    if (instance.bone_instance_offset != k_invalid)
    {        
        position_os = calculate_skinned_position(
            t_bone_instances,
            position_os.xyz,
            instance.bone_instance_offset,
            input.bone_ids,
            input.bone_weights);
    }

    output.position_ws = mul(instance.transform, float4(position_os.xyz, 1));
    output.position_cs = mul(c_view.viewprojection, float4(output.position_ws.xyz, 1));

    output.color = instance.color;
    output.normal_ws = normalize(mul((float3x3)instance.transform, input.normal));
    output.uv = input.uv;
    output.texid_basecolor = instance.texid_basecolor;
    output.bitflags = instance.bitflags;
    return output;
}

float shadow_term(float3 position_ws, float3 normal_ws)
{
    static const float k_no_shadow = 1.0f;
    if (c_global.texid_shadows == k_invalid) 
        return k_no_shadow;

    if (dot(normal_ws, c_global.light_direction.xyz) > 0.0f) 
        return k_no_shadow; 

    float4 shadowmap_position = mul(c_global.mat_to_lightspace, float4(position_ws.xyz, 1.0f));
    shadowmap_position.xyz /= shadowmap_position.w;
    
    const float2 shadowmap_uv = shadowmap_position.xy * float2(1,-1) * 0.5 + 0.5;
    const bool out_of_bounds = shadowmap_uv.x > 1.0f
        || shadowmap_uv.y > 1.0f
        || shadowmap_uv.x < 0.0f
        || shadowmap_uv.y < 0.0f;
    
    if (out_of_bounds) 
        return k_no_shadow;

    Texture2D tex_shadows = ResourceDescriptorHeap[c_global.texid_shadows];
    const uint2 shadowmap_size = get_texture_size(tex_shadows);

    const float2 sample_coord = shadowmap_uv * shadowmap_size;
    const float shadowmap_depth = tex_shadows.Load(int3(sample_coord, 0)).x;
    return float(shadowmap_position.z - shadowmap_depth < 0.01);
}

[Shader("pixel")]
float4 main_ps(ps_input input) : SV_Target0
{
    const float3 normal_ws = input.normal_ws;
    const float3 ndotl = dot(normal_ws, normalize(c_global.light_direction.xyz));
    const float3 ambient = float3(1, 1, 1) * 0.05;
    const float3 diffuse = clamp(-ndotl, 0, 1);
    const float shadow = shadow_term(input.position_ws, normal_ws);

    float3 basecolor = input.color.rgb;
    if (input.texid_basecolor != k_invalid)
    {
        Texture2D tex_basecolor = ResourceDescriptorHeap[input.texid_basecolor];
        uint width, height, num_levels;
        tex_basecolor.GetDimensions(0, width, height, num_levels);

        const float2 uv = clamp(input.uv, float2(0.01, 0.01), float2(0.99, 0.99));
        basecolor *= tex_basecolor[uv * float2(width, height)];
    }

    // write to the bitmap
    if (c_global.texid_bitmap_uav != k_invalid)
    {
        RWTexture2D<uint> bitmap = ResourceDescriptorHeap[c_global.texid_bitmap_uav];
        bitmap[input.position_cs.xy].r = input.bitflags;
    }

    const float3 lighting = clamp(diffuse * shadow, 0.2, 1);
    return float4(saturate((basecolor * lighting) + ambient), 1);
}