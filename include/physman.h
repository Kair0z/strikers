#pragma once
#include "common.h"
#include "box3d/box3d.h"

namespace strikers {

class renderscene;
class physman final
{
public:
    using world_id = b3WorldId;
    using shape_id = b3ShapeId;
    struct body_id final : public b3BodyId
    {
        body_id() = default;
        body_id(b3BodyId id) : b3BodyId(id) {}
        struct hash_t {
            uint64 operator()(const body_id& k) const {
                return ((uint64)k.generation ^ (uint64)k.index1 ^ (uint64)k.world0) << 1;
            }
        };
        struct equal_t {
            static bool operator()(const body_id& lhs, const body_id& rhs)
            {
                return lhs.generation == rhs.generation &&
                    lhs.index1 == rhs.index1 &&
                    lhs.world0 == rhs.world0;
            }
        };
    };

    enum class body_flags
    {
        none = 0,
        dynamic = (1 << 0),
        kinematic = (1 << 1)
    };

    enum class body_shape
    {
        cube,
        sphere,
        num
    };

    struct create_body_args final
    {
        body_shape m_shape = body_shape::cube;
        body_flags m_flags = body_flags::none;
        transform m_transform = {};
    };

    struct body final
    {
        create_body_args m_create_args;
        body_flags m_flags;
        body_id m_id;
        shape_id m_shape;
    };
    
    static physman& get() {
        static physman singleton{};
        return singleton;
    }

    body_id create_body(const create_body_args& args);

    bool query_body_transform(const body_id id, transform& out_transform) const;

    void set_body_transform(const body_id id, const transform& transform) const;

    enum class force_type
    {
        force,
        impulse_linear,
        impulse_angular
    };

    struct force_args
    {
        static force_args force(const float3& force);
        static force_args force_at_point(const float3& force, const float3& point);
        static force_args impulse(const float3& force);
        static force_args impulse_at_point(const float3& force, const float3& point);
        static force_args angular_impulse(const float3& force);

        force_type m_type;
        float3 m_force;
        bool m_wake;
        option<float3> m_position;
    };
    void body_apply_force(const body_id id, const force_args& force);
    
    void initialize();
    void tick();

    struct db_draw
    {
        enum : uint32 {
            none                  = 0,
            draw_shapes           = (1 << 0),
            draw_joints           = (1 << 1),
            draw_jointextras      = (1 << 2),
            draw_bounds           = (1 << 3),
            draw_mass             = (1 << 4),
            draw_sleep            = (1 << 5),
            draw_bodynames        = (1 << 6),
            draw_contacts         = (1 << 7),
            draw_anchorA          = (1 << 8),
            draw_graphcolors      = (1 << 9),
            draw_contactfeatures  = (1 << 10),
            draw_contactnormals   = (1 << 11),
            draw_contactforces    = (1 << 12),
            draw_islands          = (1 << 13),

            draw_all = 0xffffffff
        };
        uint32 m_flags;
    };
    void debug_draw(const db_draw& args, renderscene& scene);

private:
    world_id m_world;
    vector<body> m_bodies{};
    umap<body_id, uint32, body_id::hash_t, body_id::equal_t> m_body_id_to_index;
    
    
};
}