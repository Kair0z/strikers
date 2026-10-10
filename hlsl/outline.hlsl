#define ROOT_SIGNATURE \
    "RootFlags("\
        "CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED|"\
        "ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT|"\
        "SAMPLER_HEAP_DIRECTLY_INDEXED),"\
    "CBV(b0),"
#include "frontend.hlsl"

ConstantBuffer<cbuffer_global>      c_global            : register(b0);

[Shader("compute")]
[numthreads(8, 8, 1)]
void main_cs(uint3 tid : SV_DispatchThreadID)
{
    if (c_global.texid_scenecolor_uav == k_invalid)
        return;
    if (c_global.texid_bitmap_uav == k_invalid)
        return;
    
    RWTexture2D<uint> t_bitmap = ResourceDescriptorHeap[c_global.texid_bitmap_uav];
    RWTexture2D<float4> u_scenecolor = ResourceDescriptorHeap[c_global.texid_scenecolor_uav];

    const uint width = 1280;
    const uint height = 720;
    if (tid.x >= width || tid.y >= height)
        return;

    int2 p = int2(tid.xy);
    uint center = t_bitmap[p];

    // Only pixels that are NOT selected can become outline pixels.
    if (bitflags::is_outlined(center))
        return;

    float4 color = float4(0,0,0,1);

    uint num_outlines = 0;
    static const int2 offsets[8] =
    {
        int2(-1,  0),
        int2( 1,  0),
        int2( 0, -1),
        int2( 0,  1),
        int2(-1, -1),
        int2( 1, -1),
        int2(-1,  1),
        int2( 1,  1)
    };

    for (uint i = 0; i < 8; ++i)
    {
        int2 q = clamp( p + offsets[i], int2(0, 0), int2(width - 1, height - 1));
        uint neighbor = t_bitmap[q];

        if (bitflags::is_outlined(neighbor))
        {
            num_outlines++;
            color += c_global.player_colors[ bitflags::get_player_index(neighbor) ];
            break;
        }
    }

    if (num_outlines != 0)
        u_scenecolor[p] = color / num_outlines;
}