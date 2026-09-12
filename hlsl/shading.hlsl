// define BINDLESS root signature
#define ROOT_SIGNATURE \
    "RootFlags("\
        "CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED|"\
        "ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT|"\
        "SAMPLER_HEAP_DIRECTLY_INDEXED),"\
    "CBV(b0),"\
    "CBV(b1),"\
    "SRV(t0),"\
    "SRV(t1)"

struct instance
{
    float4x4    transform;
    float4      color;
    uint        texid_basecolor;
};
struct bone
{
    float4x4 transform;
};
struct vs_input
{
    float3 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    uint4 bone_ids : BLENDINDICES0;
    float4 bone_weights : BLENDWEIGHT0;
};
struct ps_input
{
    float4 position_cs : SV_POSITION;
    float4 position_ws : POSITION;
    float4 color : COLOR0;
    float3 normal_ws : NORMAL0;
    float2 uv : TEXCOORD0;
    uint texid_basecolor : TEXCOORD1;
};
cbuffer cbuffer_global : register(b0)
{
    float4x4 k_mat_to_lightspace;
    float4 k_light_color;
    float4 k_light_direction;
    uint k_texid_shadows;
};
cbuffer cbuffer_view : register(b1)
{
    float4x4 k_viewprojection;
};
StructuredBuffer<instance>  t_instances  : register(t0);
StructuredBuffer<bone>      t_bones      : register(t1);

bool is_texid_valid(uint id)
{
    return id != 0;
}

[Shader("vertex")]
ps_input main_vs(vs_input input, uint instance_id : SV_InstanceID, uint start_instance_id : SV_StartInstanceLocation, uint vertex_id : SV_VertexID)
{
    instance instance = t_instances[instance_id + start_instance_id];
    
    ps_input output;
    output.position_ws = mul(instance.transform, float4(input.position, 1));
    output.position_cs = mul(k_viewprojection, float4(output.position_ws.xyz, 1));
    output.color = instance.color;
    output.normal_ws = normalize(mul((float3x3)instance.transform, input.normal));
    output.uv = input.uv;
    output.texid_basecolor = instance.texid_basecolor;
    return output;
}

float shadow_term(float3 position_ws, float3 normal_ws)
{
    static const float k_no_shadow = 1.0f;
    if (!is_texid_valid(k_texid_shadows)) 
        return k_no_shadow;

    if (dot(normal_ws, k_light_direction.xyz) > 0.0f) 
        return k_no_shadow; 

    float4 shadowmap_position = mul(k_mat_to_lightspace, float4(position_ws.xyz, 1.0f));
    shadowmap_position.xyz /= shadowmap_position.w;
    
    const float2 shadowmap_uv = shadowmap_position.xy * float2(1,-1) * 0.5 + 0.5;
    const bool out_of_bounds = shadowmap_uv.x > 1.0f
        || shadowmap_uv.y > 1.0f
        || shadowmap_uv.x < 0.0f
        || shadowmap_uv.y < 0.0f;
    
    if (out_of_bounds) return k_no_shadow;

    Texture2D tex_shadows = ResourceDescriptorHeap[k_texid_shadows];
    uint width, height, num_levels;
    tex_shadows.GetDimensions(0, width, height, num_levels);

    const float2 sample_coord = shadowmap_uv * float2(width, height);
    const float shadowmap_depth = tex_shadows.Load(int3(sample_coord, 0)).x;
    return float(shadowmap_position.z - shadowmap_depth < 0.01);
}

[Shader("pixel")]
float4 main_ps(ps_input input) : SV_Target0
{
    const float3 normal_ws = input.normal_ws;
    const float3 ndotl = dot(normal_ws, normalize(k_light_direction.xyz));
    const float3 ambient = float3(1, 1, 1) * 0.05;
    const float3 diffuse = clamp(-ndotl, 0.2, 1);
    const float shadow = shadow_term(input.position_ws, normal_ws);

    float3 basecolor = input.color.rgb;
    if (is_texid_valid(input.texid_basecolor))
    {
        Texture2D tex_basecolor = ResourceDescriptorHeap[input.texid_basecolor];
        uint width, height, num_levels;
        tex_basecolor.GetDimensions(0, width, height, num_levels);

        const float2 uv = clamp(input.uv, float2(0.01, 0.01), float2(0.99, 0.99));
        basecolor *= tex_basecolor[uv * float2(width, height)];
    }

    return float4(saturate((basecolor * diffuse * shadow) + ambient), 1);
}