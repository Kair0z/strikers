#pragma once
#include "common.h"

namespace strikers
{
	struct collision_layers
	{
		enum layer
		{
			common = 0,			// where most reside
			static_level = 1,	// blocks everything
			common_ignored = 2,	// ignored by most
			common_trigger = 3,	// trigger by most
			num
		};
	};

	struct collision_response
	{
		enum response
		{
			block,		// solve_collisions will process it in the delta_pos correction
			trigger,	// detect_collisions will raise a collision flag (but no delta_pos correction will be calculated)
			ignore,		// no collision is detected
			num
		};
		response m_response{};
	};

	class collisionman final
	{
	public:
		struct collision_entry final
		{
			uint32 m_a;
			uint32 m_b;
			collision m_info;
		};

		enum class collider_type
		{
			invalid,
			sphere,
			aabb,
			inv_aabb,
			inv_sphere,
			num
		};

		struct collider_builder final
		{
			box m_aabb{};
			sphere m_sphere{};
			float m_inv_mass = 0.0f;
			collider_type m_type{};
			float3 m_velocity{};
			string m_debug_tag = "";
			uint32 m_layer{};

			collider_builder& sphere(const sphere& sph) { m_sphere = sph; m_type = collider_type::sphere; return *this; }
			collider_builder& aabb(const box& box) { m_aabb = box; m_type = collider_type::aabb; return *this; }
			collider_builder& inv_aabb(const box& box) { m_aabb = box; m_type = collider_type::inv_aabb; return *this; }
			collider_builder& inv_sphere(const strikers::sphere& sph) { m_sphere = sph; m_type = collider_type::inv_sphere; return *this; }
			collider_builder& set_static() { m_inv_mass = 0.0f; return *this; }
			collider_builder& set_layer(uint32 layer) { m_layer = layer; return *this; }
			collider_builder& mass(float mass) { m_inv_mass = 1.0f / mass; return *this; }
			collider_builder& inv_mass(float inv_mass) { m_inv_mass = inv_mass; return *this; }
			collider_builder& velocity(float3 velocity) { m_velocity = velocity; return *this; }
			collider_builder& dbg_tag(const string& tag) { m_debug_tag = tag; return *this; }
		};

		struct collider_entry final
		{
			collider_builder m_builder;
			float3 m_solved_deltapos;
			float3 m_solved_deltavel;
			
			bool is_sphere() const { return m_builder.m_type == collider_type::sphere; }
			bool is_aabb() const { return m_builder.m_type == collider_type::aabb; }
			const float3& velocity() const { return m_builder.m_velocity; }
			float inv_mass() const { return m_builder.m_inv_mass; }
			const box& aabb() const { return m_builder.m_aabb; }
		};
	
		void set_layer_response(uint32 layer_a, uint32 layer_b, collision_response::response response)
		{
			m_response_matrix[(layer_b * collision_layers::num) + layer_a].m_response = response;
			m_response_matrix[(layer_a * collision_layers::num) + layer_b].m_response = response;
		}

		collision_response::response get_layer_response(uint32 layer_a, uint32 layer_b) const
		{
			return m_response_matrix[(layer_b * collision_layers::num) + layer_a].m_response;
		}

		void reset_colliders()
		{
			m_collisions.clear();
			m_collider_to_collisions.clear();
		}
		
		void write_collider(const uint32 slot, const collider_builder& builder)
		{
			m_colliders.resize(slot + 1);
			m_colliders[slot].m_builder = builder;
			m_colliders[slot].m_solved_deltapos = {};
			m_colliders[slot].m_solved_deltavel = {};
		}

		void detect_collisions()
		{
			m_collisions.clear();
			m_collider_to_collisions.clear();

			for (uint32 a = 0u; a < m_colliders.size(); ++a)
			{
				for (uint32 b = 0u; b < m_colliders.size(); ++b)
				{
					// this avoids duplicate pairs of collisions
					if (a >= b)
						continue;

					collider_entry& cola = get_collider(a);
					collider_entry& colb = get_collider(b);

					const uint32 layer_a = cola.m_builder.m_layer;
					const uint32 layer_b = colb.m_builder.m_layer;
					if (get_layer_response(layer_a, layer_b) == collision_response::ignore)
					{
						continue;
					}

					// in case we're iterating multiple times, we integrate our previously solved deltapos
					box a_moved_aabb = cola.aabb();
					box b_moved_aabb = colb.aabb();
					a_moved_aabb.m_position += cola.m_solved_deltapos;
					b_moved_aabb.m_position += colb.m_solved_deltapos;

					collision collision_info{};
					if (cola.is_aabb() && colb.is_aabb())
						collision::calculate(a_moved_aabb, b_moved_aabb, &collision_info);
					// ... other types todo

					if (collision_info.m_collided)
					{
						collision_entry entry{};
						entry.m_a = a;
						entry.m_b = b;
						entry.m_info = collision_info;
						m_collisions.push_back(entry);
						const uint32 collision_index = (uint32)m_collisions.size() - 1u;
						m_collider_to_collisions[a].push_back(collision_index);
						m_collider_to_collisions[b].push_back(collision_index);
					}
				}
			}
		}
		
		void solve_collisions()
		{
			for (const collision_entry& collision : m_collisions)
			{
				collider_entry& a = get_collider(collision.m_a);
				collider_entry& b = get_collider(collision.m_b);
				const strikers::collision& info = collision.m_info;

				const float sum_inv_mass = a.inv_mass() + b.inv_mass();
				if (sum_inv_mass <= 0.0f) // both are static...
					continue;

				const uint32 layer_a = a.m_builder.m_layer;
				const uint32 layer_b = b.m_builder.m_layer;
				if (get_layer_response(layer_a, layer_b) != collision_response::block)
				{
					continue;
				}

				const float3& col_intersection = info.m_intersection_depth;
				const float3& col_normal = info.m_collision_normal;
				const float penetration = glm::abs(glm::dot(col_intersection, col_normal));
				const float3 correction = col_normal * penetration;
				const float3 correction_per_mass = correction / sum_inv_mass;

				// solve delta pos
				a.m_solved_deltapos += correction_per_mass * a.inv_mass();
				b.m_solved_deltapos -= correction_per_mass * b.inv_mass();

				// solve velocities (calculate a deltavelocity
				const float3 relative_veloctiy = b.velocity() - a.velocity();

				const float vel_dot_normal = glm::dot(relative_veloctiy, col_normal);
				if (vel_dot_normal > 0.0f)
					continue; // the velocity is already separating from the normal

				const float restitution = 0.0f;
				const float impulse_magnitude = -(1.0f + restitution) * vel_dot_normal / sum_inv_mass;

				const float3 impulse = col_normal * impulse_magnitude;
				a.m_solved_deltavel += impulse * a.inv_mass();
				b.m_solved_deltavel -= impulse * b.inv_mass();
			}
		}

		const collider_entry& get_collider(const uint32 slot) const
		{
			return m_colliders[slot];
		}

		const collision_entry& get_collision(const uint32 slot) const
		{
			return m_collisions[slot];
		}
		
		uint32 num_collisions(const uint32 slot) const
		{
			if (slot >= m_colliders.size())
				return 0u;

			if (!m_collider_to_collisions.contains(slot))
				return 0u;

			return (uint32)m_collider_to_collisions.at(slot).size();
		}

		bool get_collision(
			const uint32 collider, 
			const uint32 index, 
			uint32& out_other_collider,
			collision* out_info = nullptr) const
		{
			const uint32 num = num_collisions(collider);
			if (index >= num) return false;
			
			const collision_entry& collision = m_collisions[m_collider_to_collisions.at(collider)[index]];
			if (collision.m_a == collider)
			{
				out_other_collider = collision.m_b;
				if (out_info) *out_info = collision.m_info;
				return true;
			}
			else if (collision.m_b == collider)
			{
				out_other_collider = collision.m_a;
				if (out_info) *out_info = collision.m_info.inverted();
				return true;
			}
			else return false;
		}

		bool collider_add_solved_deltas(const uint32 slot,
			float3& out_solved_deltapos,
			float3& out_solved_deltavel)
		{
			out_solved_deltapos += m_colliders[slot].m_solved_deltapos;
			out_solved_deltavel += m_colliders[slot].m_solved_deltavel;
			return true;
		}

	private:
		vector<collider_entry> m_colliders{};
		vector<collision_entry> m_collisions{};
		umap<uint32, vector<uint32>> m_collider_to_collisions{};

		collision_response m_response_matrix[collision_layers::num * collision_layers::num];

		collider_entry& get_collider(const uint32 slot)
		{
			return m_colliders[slot];
		}
	};
}