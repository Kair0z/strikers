#define ROOT_SIGNATURE \
    "RootFlags("\
        "CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED|"\
        "ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT|"\
        "SAMPLER_HEAP_DIRECTLY_INDEXED),"\
    "CBV(b0),"\
    "SRV(t0),"\
    "SRV(t1)"

#include "frontend.hlsl"

ConstantBuffer<cbuffer_view>    c_view      : register(b0);
StructuredBuffer<mesh_instance> t_instances : register(t0);
StructuredBuffer<bone_instance> t_bone_instances : register(t1);

[Shader("vertex")]
float4 main_vs(mesh_vertex vertex, uint instance_id : SV_InstanceID, uint start_instance_id : SV_StartInstanceLocation) : SV_POSITION
{
    float4 out_position;
    mesh_instance instance = t_instances[instance_id + start_instance_id];
    
    float4 position_os = float4(vertex.position.xyz, 1);
    if (instance.bone_instance_offset != k_invalid)
    {   
        position_os = calculate_skinned_position(
            t_bone_instances,
            position_os.xyz,
            instance.bone_instance_offset,
            vertex.bone_ids,
            vertex.bone_weights);
    }

    out_position = mul(instance.transform, float4(position_os.xyz, 1)); // now ws (worldspace)
    out_position = mul(c_view.viewprojection, float4(out_position.xyz, 1)); // now ls (lightspace)
    return out_position;
}