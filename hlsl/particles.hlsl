#define ROOT_SIGNATURE_COMPUTE \
    "RootFlags("\
        "CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED|"\
        "ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT|"\
        "SAMPLER_HEAP_DIRECTLY_INDEXED),"\
    "CBV(b0),"\
    "SRV(t0),"\
    "UAV(u0),"\
    "UAV(u1),"\
    "UAV(u2),"\
    "UAV(u3),"\
    "UAV(u4),"\
    "UAV(u5)"

#include "frontend.hlsl"

ConstantBuffer<cbuffer_global>              c_global            : register(b0);
StructuredBuffer<pfx_spawn>                 t_spawns            : register(t0);
RWStructuredBuffer<pfx_particle>            u_particles         : register(u0);
RWStructuredBuffer<uint>                    u_pfx_alive_list    : register(u1);
RWStructuredBuffer<uint>                    u_pfx_dead_list     : register(u2);
RWStructuredBuffer<int>                     u_pfx_num_alive     : register(u3);
RWStructuredBuffer<int>                     u_pfx_num_dead      : register(u4);
RWStructuredBuffer<draw_arguments>          u_particles_draw    : register(u5); // Output indirect arguments

uint total_num_particles() 
{
    return c_global.max_num_particles;
}

static const uint k_invalid_particle = (uint)-1;

uint allocate_particle()
{
    // Reserve one dead-stack slot without allowing underflow.
    int num_dead = u_pfx_num_dead[0];
    for (;;)
    {
        if (num_dead <= 0)
            return k_invalid_particle;

        int old_num_dead;

        InterlockedCompareExchange(
            u_pfx_num_dead[0],
            num_dead,
            num_dead - 1,
            old_num_dead);

        if (old_num_dead == num_dead)
            break;

        num_dead = old_num_dead;
    }

    // The previous count identifies the slot we reserved.
    const uint dead_stack_index = (uint)(num_dead - 1);

    return u_pfx_dead_list[dead_stack_index];
}

void free_particle(uint particle_index)
{
    // PRECONDITION: this particle is alive and is being freed exactly once.
    // The caller must mark its lifetime as expired before returning it here.

    const uint max_particles = total_num_particles();
    if (particle_index >= max_particles)
        return;

    int dead_stack_index;

    InterlockedAdd(
        u_pfx_num_dead[0],
        1,
        dead_stack_index);

    // Each successful free owns a unique slot.
    // Capacity is safe as long as the precondition holds.
    u_pfx_dead_list[dead_stack_index] = particle_index;
}

[RootSignature(ROOT_SIGNATURE_COMPUTE)]
[Shader("compute")]
[numthreads(64, 1, 1)]
void reset_cs(uint3 tid : SV_DispatchThreadID)
{
    const uint particle_idx = tid.x;
    const uint num_particles = total_num_particles();

    if (particle_idx == 0)
    {
        u_pfx_num_dead[0]  = (int)num_particles;
        u_pfx_num_alive[0] = 0;
    }

    if (particle_idx >= num_particles)
        return;

    // Mark every particle as initially dead.
    pfx_particle particle = u_particles[particle_idx];
    particle.seconds_remaining = -1.0f;
    u_particles[particle_idx] = particle;

    // Initialize the free list.
    u_pfx_dead_list[particle_idx] = num_particles - particle_idx - 1;
    u_pfx_alive_list[particle_idx] = k_invalid_particle;
}

// - for each pfx_spawn
[RootSignature(ROOT_SIGNATURE_COMPUTE)]
[Shader("compute")]
[numthreads(64, 1, 1)]
void spawn_cs(uint3 tid : SV_DispatchThreadID)
{
    const uint spawn_idx = tid.x;

    uint max_spawns, spawn_stride;
    t_spawns.GetDimensions(max_spawns, spawn_stride);

    if (spawn_idx >= 1)
        return;

    const pfx_spawn spawn = t_spawns[spawn_idx];

    for (uint i = 0; i < spawn.num_to_spawn; ++i)
    {
        const uint particle_idx = allocate_particle();

        if (particle_idx == k_invalid_particle)
            break;

        u_particles[particle_idx] = spawn.particle_init;
    }
}

[RootSignature(ROOT_SIGNATURE_COMPUTE)]
[Shader("compute")]
[numthreads(1, 1, 1)]
void pretick_cs(uint3 tid : SV_DispatchThreadID)
{
    u_pfx_num_alive[0] = 0;
}

// - for each pfx_particle
[RootSignature(ROOT_SIGNATURE_COMPUTE)]
[Shader("compute")]
[numthreads(64, 1, 1)]
void tick_cs(uint3 tid : SV_DispatchThreadID)
{
    const uint particle_idx = tid.x;
    const uint max_particles = total_num_particles();

    if (particle_idx >= max_particles)
        return;

    pfx_particle particle = u_particles[particle_idx];

    // Dead particles aren't simulated or freed again.
    if (particle.seconds_remaining < 0.0f)
        return;

    const float delta_seconds = c_global.delta_seconds;

    particle.velocity += particle.acceleration * delta_seconds;
    particle.position += particle.velocity * delta_seconds;
    particle.seconds_remaining -= delta_seconds;

    // Store the updated state before making the particle available
    // for reuse in a later dispatch.
    u_particles[particle_idx] = particle;

    if (particle.seconds_remaining < 0.0f)
    {
        free_particle(particle_idx);
        return;
    }

    // Rebuild the compact alive list.
    int alive_stack_index;

    InterlockedAdd(
        u_pfx_num_alive[0],
        1,
        alive_stack_index);

    u_pfx_alive_list[alive_stack_index] = particle_idx;
}

[RootSignature(ROOT_SIGNATURE_COMPUTE)]
[Shader("compute")] 
[numthreads(1, 1, 1)]
void generate_draw_cs(uint3 tid : SV_DispatchThreadID)
{
    draw_arguments args;
    args.num_vertices_per_instance  = 6; // quad
    args.num_instances              = u_pfx_num_alive[0]; // Drawing 1 unique instance per command slot
    args.first_vertex               = 0;
    args.first_instance             = 0;
    u_particles_draw[0]             = args;
}

// ----------------- draw part
#define ROOT_SIGNATURE\
    "RootFlags("\
        "CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED|"\
        "ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT|"\
        "SAMPLER_HEAP_DIRECTLY_INDEXED),"\
    "CBV(b0),"\
    "SRV(t0),"\
    "SRV(t1)"

ConstantBuffer<cbuffer_view>    c_view              : register(b0);
StructuredBuffer<pfx_particle>  t_particles         : register(t0);
StructuredBuffer<uint>          t_pfx_alive_list    : register(t1);

struct ps_input
{
    float4 position         : SV_POSITION;
    float4 color            : COLOR0;
    float2 uv               : TEXCOORD0;
    uint texid_color        : TEXCOORD1;
};

[RootSignature(ROOT_SIGNATURE)]
[Shader("vertex")]
ps_input main_vs(uint instance_id : SV_InstanceID, uint vertex_id : SV_VertexID)
{
    ps_input output;
    if (!get_quad_vertex(vertex_id, output.position, output.uv))
        return output;

    const uint particle_idx = t_pfx_alive_list[instance_id];
    pfx_particle particle = t_particles[particle_idx];

    output.position.x *= c_view.get_aspect_ratio().y;
    output.position.xy *= particle.size;
    output.position.xyz += particle.position;
    output.position = mul(c_view.viewprojection, output.position);
    output.color = particle.color;
    return output;
}

[RootSignature(ROOT_SIGNATURE)]
[Shader("pixel")]
float4 main_ps(ps_input input) : SV_Target0
{
    clip(0.5f - length(0.5f - input.uv));
    return input.color;
}